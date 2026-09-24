/* PS5 RetroArch - bounded in-process ELF loader for libretro cores.
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Runtime imports are bound to the title's public SDK imports, not websrv hooks.
 */
#include <elf.h>
#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#ifndef R_X86_64_JUMP_SLOT
#define R_X86_64_JUMP_SLOT R_X86_64_JMP_SLOT
#endif

extern "C" void *ps5_core_import(const char *name);

namespace
{
constexpr size_t page = 0x4000;
constexpr size_t max_file = 128 * 1024 * 1024;
constexpr size_t max_image = 512 * 1024 * 1024;
struct Module
{
    Module *next;
    unsigned references;
    char path[1024];
    unsigned char *file;
    size_t file_size;
    size_t file_span;
    unsigned char *base;
    size_t span;
    const Elf64_Phdr *ph;
    size_t ph_count;
    const Elf64_Sym *symbols;
    size_t symbol_count;
    const char *strings;
    size_t string_size;
    const uintptr_t *finalizers;
    size_t finalizer_count;
};
Module *modules = nullptr;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
thread_local char error_text[512];

bool fail(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    std::vsnprintf(error_text, sizeof(error_text), format, args);
    va_end(args);
    std::fprintf(stderr, "core loader: %s\n", error_text);
    return false;
}

bool range(uint64_t offset, uint64_t size, uint64_t limit)
{
    return offset <= limit && size <= limit - offset;
}

template <typename T> const T *records(const Module *m, uint64_t offset, uint64_t count)
{
    if (offset % alignof(T) || count > m->file_size / sizeof(T) ||
        !range(offset, count * sizeof(T), m->file_size))
        return nullptr;
    return reinterpret_cast<const T *>(m->file + offset);
}

const char *string_at(const Module *m, uint64_t offset)
{
    if (offset >= m->string_size || !std::memchr(m->strings + offset, 0, m->string_size - offset))
        return nullptr;
    return m->strings + offset;
}

bool mapped(const Module *m, uint64_t address, uint64_t bytes, unsigned flags = 0)
{
    for (size_t i = 0; i < m->ph_count; ++i)
    {
        const auto &p = m->ph[i];
        if (p.p_type == PT_LOAD && (p.p_flags & flags) == flags && address >= p.p_vaddr &&
            range(address - p.p_vaddr, bytes, p.p_memsz))
            return true;
    }
    return false;
}

void release(Module *m)
{
    if (m->base)
        munmap(m->base, m->span);
    if (m->file)
        munmap(m->file, m->file_span);
    std::free(m);
}

bool symbol_address(Module *m, size_t index, uintptr_t &address)
{
    if (index >= m->symbol_count)
        return fail("relocation symbol index out of range");
    const auto &s = m->symbols[index];
    const char *name = string_at(m, s.st_name);
    if (!name)
        return fail("unterminated symbol name");
    const unsigned type = ELF64_ST_TYPE(s.st_info);
    if (type != STT_NOTYPE && type != STT_FUNC && type != STT_OBJECT && type != STT_SECTION)
        return fail("unsupported symbol type %u for %s", type, name);
    if (s.st_shndx == SHN_UNDEF)
    {
        address = reinterpret_cast<uintptr_t>(ps5_core_import(name));
        if (!address && ELF64_ST_BIND(s.st_info) != STB_WEAK)
            return fail("unresolved native runtime import: %s", name);
        return true;
    }
    if (s.st_shndx == SHN_ABS)
    {
        address = s.st_value;
        return true;
    }
    if (s.st_shndx >= SHN_LORESERVE || !mapped(m, s.st_value, s.st_size ? s.st_size : 1))
        return fail("symbol outside loaded segments: %s", name);
    address = reinterpret_cast<uintptr_t>(m->base) + s.st_value;
    return true;
}

bool load(Module *m)
{
    const int fd = open(m->path, O_RDONLY);
    if (fd < 0)
        return fail("open failed errno=%d", errno);
    const off_t size = lseek(fd, 0, SEEK_END);
    if (size < off_t(sizeof(Elf64_Ehdr)) || size > off_t(max_file) || lseek(fd, 0, SEEK_SET) < 0)
    {
        const int error = errno;
        close(fd);
        return fail("invalid core file size/seek: size=%lld errno=%d", (long long)size, error);
    }
    m->file_size = size_t(size);
    m->file_span = (m->file_size + page - 1) & ~(page - 1);
    void *buffer =
        mmap(nullptr, m->file_span, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (buffer == MAP_FAILED)
    {
        const int error = errno;
        close(fd);
        return fail("core file buffer mapping failed: bytes=%zu errno=%d", m->file_span, error);
    }
    m->file = static_cast<unsigned char *>(buffer);
    size_t done = 0;
    while (done < m->file_size)
    {
        const size_t remaining = m->file_size - done;
        const ssize_t count = read(fd, m->file + done, remaining < 65536 ? remaining : 65536);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
        {
            const int error = count < 0 ? errno : 0;
            close(fd);
            return fail("core read failed: offset=%zu size=%zu count=%lld errno=%d", done,
                        m->file_size, (long long)count, error);
        }
        done += size_t(count);
    }
    close(fd);
    const auto *eh = records<Elf64_Ehdr>(m, 0, 1);
    const unsigned char expected[] = {0x7f, 'E', 'L', 'F', 2, 1, 1, 9};
    if (!eh || std::memcmp(eh->e_ident, expected, sizeof(expected)) || eh->e_type != ET_DYN ||
        eh->e_machine != EM_X86_64 || eh->e_version != EV_CURRENT || eh->e_ehsize != sizeof(*eh) ||
        eh->e_phentsize != sizeof(Elf64_Phdr) || eh->e_shentsize != sizeof(Elf64_Shdr) ||
        !eh->e_phnum || eh->e_phnum > 32 || !eh->e_shnum || eh->e_shnum > 4096)
        return fail("expected PS5 ELF64 shared core with program/section headers");
    m->ph = records<Elf64_Phdr>(m, eh->e_phoff, eh->e_phnum);
    m->ph_count = eh->e_phnum;
    const auto *sections = records<Elf64_Shdr>(m, eh->e_shoff, eh->e_shnum);
    if (!m->ph || !sections)
        return fail("truncated ELF header tables");
    const Elf64_Phdr *dynamic = nullptr;
    size_t loads = 0, sym_index = 0;
    for (size_t i = 0; i < m->ph_count; ++i)
    {
        const auto &p = m->ph[i];
        if (p.p_type == PT_TLS || p.p_type == PT_INTERP)
            return fail("TLS and ELF interpreters are not supported by this core loader");
        if (p.p_type == PT_DYNAMIC)
        {
            if (dynamic)
                return fail("duplicate dynamic segment");
            dynamic = &p;
        }
        if (p.p_type != PT_LOAD)
            continue;
        if (!p.p_memsz)
        {
            if (p.p_filesz)
                return fail("nonempty file segment without memory");
            continue;
        }
        if (p.p_filesz > p.p_memsz || !range(p.p_offset, p.p_filesz, m->file_size) ||
            !range(p.p_vaddr, p.p_memsz, max_image - page) || p.p_vaddr % page ||
            p.p_offset % page || p.p_align < page || (p.p_align & (p.p_align - 1)) ||
            (p.p_flags & (PF_W | PF_X)) == (PF_W | PF_X))
            return fail("invalid, unaligned or writable-executable load segment");
        const uint64_t end = (p.p_vaddr + p.p_memsz + page - 1) & ~(page - 1);
        for (size_t j = 0; j < i; ++j)
        {
            const auto &q = m->ph[j];
            const uint64_t qend = (q.p_vaddr + q.p_memsz + page - 1) & ~(page - 1);
            if (q.p_type == PT_LOAD && q.p_memsz && p.p_vaddr < qend && q.p_vaddr < end)
                return fail("overlapping load segment pages");
        }
        if (end > m->span)
            m->span = end;
        ++loads;
    }
    if (!loads || !dynamic || !m->span)
        return fail("missing load/dynamic segments");
    for (size_t i = 0; i < eh->e_shnum; ++i)
    {
        const auto &s = sections[i];
        if (s.sh_type != SHT_DYNSYM)
            continue;
        if (m->symbols || s.sh_entsize != sizeof(Elf64_Sym) || s.sh_size % sizeof(Elf64_Sym) ||
            s.sh_link >= eh->e_shnum || s.sh_size / sizeof(Elf64_Sym) > 100000)
            return fail("invalid dynamic symbol table");
        m->symbol_count = s.sh_size / sizeof(Elf64_Sym);
        m->symbols = records<Elf64_Sym>(m, s.sh_offset, m->symbol_count);
        sym_index = i;
        const auto &str = sections[s.sh_link];
        if (!m->symbols || !m->symbol_count || str.sh_type != SHT_STRTAB ||
            !range(str.sh_offset, str.sh_size, m->file_size))
            return fail("invalid symbol strings");
        m->strings = reinterpret_cast<const char *>(m->file + str.sh_offset);
        m->string_size = str.sh_size;
    }
    if (!m->symbols)
        return fail("missing dynamic symbols");
    const auto *dt =
        records<Elf64_Dyn>(m, dynamic->p_offset, dynamic->p_filesz / sizeof(Elf64_Dyn));
    if (!dt || dynamic->p_filesz % sizeof(Elf64_Dyn))
        return fail("invalid dynamic entries");
    bool terminated = false;
    uint64_t init_array = 0, init_bytes = 0, fini_array = 0, fini_bytes = 0;
    bool have_fini_array = false, have_fini_size = false;
    bool have_init_array = false, have_init_size = false;
    for (size_t i = 0; i < dynamic->p_filesz / sizeof(Elf64_Dyn); ++i)
    {
        auto tag = dt[i].d_tag;
        auto value = dt[i].d_un.d_val;
        if (tag == DT_NULL)
        {
            terminated = true;
            break;
        }
        if (tag == DT_NEEDED)
        {
            const char *name = string_at(m, value);
            if (!name || (std::strcmp(name, "libkernel_web.sprx") &&
                          std::strcmp(name, "libSceLibcInternal.sprx") &&
                          std::strcmp(name, "libScePosixForWebKit.sprx")))
                return fail("unsupported core dependency");
        }
        if (tag == DT_INIT_ARRAY)
        {
            if (have_init_array)
                return fail("duplicate initializer array");
            have_init_array = true;
            init_array = value;
        }
        if (tag == DT_INIT_ARRAYSZ)
        {
            if (have_init_size)
                return fail("duplicate initializer array size");
            have_init_size = true;
            init_bytes = value;
        }
        if (tag == DT_FINI_ARRAY)
        {
            if (have_fini_array)
                return fail("duplicate finalizer array");
            have_fini_array = true;
            fini_array = value;
        }
        if (tag == DT_FINI_ARRAYSZ)
        {
            if (have_fini_size)
                return fail("duplicate finalizer array size");
            have_fini_size = true;
            fini_bytes = value;
        }
        // Legacy init/fini functions, TLS and C++ unwinding remain unsupported.
        if (tag == DT_TEXTREL || ((tag == DT_INIT || tag == DT_FINI || tag == DT_PREINIT_ARRAYSZ ||
                                   tag == DT_RELSZ || tag == 35 /* DT_RELRSZ */) &&
                                  value))
            return fail("unsupported core initialization/relocation feature");
    }
    if (!terminated)
        return fail("unterminated dynamic entries");
    if (init_bytes &&
        (!have_init_array || init_array % sizeof(uint64_t) || init_bytes % sizeof(uint64_t) ||
         init_bytes > 1024 * sizeof(uint64_t) || !mapped(m, init_array, init_bytes, PF_R)))
        return fail("invalid initializer array range/size");
    if (have_init_array && !have_init_size)
        return fail("initializer array has no size");
    if (fini_bytes &&
        (!have_fini_array || fini_array % sizeof(uint64_t) || fini_bytes % sizeof(uint64_t) ||
         fini_bytes > 1024 * sizeof(uint64_t) || !mapped(m, fini_array, fini_bytes, PF_R)))
        return fail("invalid finalizer array range/size");
    if (have_fini_array && !have_fini_size)
        return fail("finalizer array has no size");
    // Reserve aligned memory without ever making a page writable and executable.
    void *allocation =
        mmap(nullptr, m->span + page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (allocation == MAP_FAILED)
        return fail("core mmap failed errno=%d", errno);
    const uintptr_t address = reinterpret_cast<uintptr_t>(allocation);
    const uintptr_t aligned = (address + page - 1) & ~(page - 1);
    const size_t prefix = aligned - address;
    const size_t suffix = page - prefix;
    if (prefix)
        munmap(allocation, prefix);
    if (suffix)
        munmap(reinterpret_cast<void *>(aligned + m->span), suffix);
    m->base = reinterpret_cast<unsigned char *>(aligned);
    for (size_t i = 0; i < m->ph_count; ++i)
        if (m->ph[i].p_type == PT_LOAD && m->ph[i].p_filesz)
            std::memcpy(m->base + m->ph[i].p_vaddr, m->file + m->ph[i].p_offset, m->ph[i].p_filesz);
    // Resolve every import before publishing the handle or executing code.
    for (size_t i = 1; i < m->symbol_count; ++i)
    {
        uintptr_t unused;
        if (!symbol_address(m, i, unused))
            return false;
    }
    size_t relocations = 0;
    for (size_t i = 0; i < eh->e_shnum; ++i)
    {
        const auto &s = sections[i];
        if (!(s.sh_flags & SHF_ALLOC))
            continue;
        if (s.sh_type == SHT_REL || s.sh_type == 19 /* SHT_RELR */)
            return fail("unsupported relocation encoding");
        if (s.sh_type != SHT_RELA)
            continue;
        if (s.sh_link != sym_index || s.sh_entsize != sizeof(Elf64_Rela) ||
            s.sh_size % sizeof(Elf64_Rela))
            return fail("invalid relocation section");
        const size_t count = s.sh_size / sizeof(Elf64_Rela);
        const auto *rel = records<Elf64_Rela>(m, s.sh_offset, count);
        if (!rel)
            return fail("truncated relocation section");
        for (size_t j = 0; j < count; ++j)
        {
            const auto &r = rel[j];
            const unsigned type = ELF64_R_TYPE(r.r_info);
            if (type == R_X86_64_NONE)
                continue;
            if (!mapped(m, r.r_offset, sizeof(uint64_t), PF_W))
                return fail("relocation destination outside writable segment");
            uintptr_t value = 0;
            if (type == R_X86_64_RELATIVE)
            {
                if (ELF64_R_SYM(r.r_info) || r.r_addend < 0 || !mapped(m, uint64_t(r.r_addend), 1))
                    return fail("invalid relative relocation");
                value = reinterpret_cast<uintptr_t>(m->base) + r.r_addend;
            }
            else if (type == R_X86_64_64 || type == R_X86_64_GLOB_DAT || type == R_X86_64_JUMP_SLOT)
            {
                if (!symbol_address(m, ELF64_R_SYM(r.r_info), value))
                    return false;
                value += uintptr_t(r.r_addend);
            }
            else
                return fail("unsupported x86-64 relocation %u", type);
            std::memcpy(m->base + r.r_offset, &value, sizeof(value));
            ++relocations;
        }
    }
    // Validate every relocated callback before executing any initializer.
    const auto *initializers =
        init_bytes ? reinterpret_cast<const uintptr_t *>(m->base + init_array) : nullptr;
    const size_t initializer_count = init_bytes / sizeof(uintptr_t);
    for (size_t i = 0; i < initializer_count; ++i)
    {
        const uintptr_t base = reinterpret_cast<uintptr_t>(m->base);
        if (initializers[i] < base || !mapped(m, initializers[i] - base, 1, PF_X))
            return fail("initializer callback outside executable segment");
    }
    const auto *finalizers =
        fini_bytes ? reinterpret_cast<const uintptr_t *>(m->base + fini_array) : nullptr;
    const size_t finalizer_count = fini_bytes / sizeof(uintptr_t);
    for (size_t i = 0; i < finalizer_count; ++i)
    {
        const uintptr_t base = reinterpret_cast<uintptr_t>(m->base);
        if (finalizers[i] < base || !mapped(m, finalizers[i] - base, 1, PF_X))
            return fail("finalizer callback outside executable segment");
    }
    if (mprotect(m->base, m->span, PROT_NONE))
        return fail("core protect reserve failed errno=%d", errno);
    for (size_t i = 0; i < m->ph_count; ++i)
    {
        const auto &p = m->ph[i];
        if (p.p_type != PT_LOAD || !p.p_memsz)
            continue;
        int protection = ((p.p_flags & PF_R) ? PROT_READ : 0) |
                         ((p.p_flags & PF_W) ? PROT_WRITE : 0) |
                         ((p.p_flags & PF_X) ? PROT_EXEC : 0);
        const size_t length = (p.p_memsz + page - 1) & ~(page - 1);
        if (mprotect(m->base + p.p_vaddr, length, protection))
            return fail("core segment mprotect flags=%u failed errno=%d", p.p_flags, errno);
    }
    for (size_t i = 0; i < initializer_count; ++i)
        reinterpret_cast<void (*)()>(initializers[i])();
    m->finalizers = finalizers;
    m->finalizer_count = finalizer_count;
    if (initializer_count)
        std::fprintf(stderr, "core loader: ran %zu initializers\n", initializer_count);
    std::fprintf(stderr,
                 "core loader: ready symbols=%zu relocations=%zu mapped_bytes=%zu base=%p\n",
                 m->symbol_count, relocations, m->span, static_cast<void *>(m->base));
    return true;
}
} // namespace

extern "C" void *ps5_core_dlopen(const char *path, int)
{
    pthread_mutex_lock(&mutex);
    error_text[0] = 0;
    if (!path || !*path || std::strlen(path) >= sizeof(Module::path))
    {
        fail("invalid core path (no process-global lookup)");
        pthread_mutex_unlock(&mutex);
        return nullptr;
    }
    unsigned count = 0;
    for (auto *m = modules; m; m = m->next)
    {
        ++count;
        if (!std::strcmp(path, m->path))
        {
            ++m->references;
            pthread_mutex_unlock(&mutex);
            return m;
        }
    }
    auto *m = count < 8 ? static_cast<Module *>(std::calloc(1, sizeof(Module))) : nullptr;
    if (!m)
        fail("core handle capacity/allocation exhausted");
    if (m)
    {
        std::strcpy(m->path, path);
        if (!load(m))
        {
            release(m);
            m = nullptr;
        }
        else
        {
            m->references = 1;
            m->next = modules;
            modules = m;
        }
    }
    pthread_mutex_unlock(&mutex);
    return m;
}

extern "C" void *ps5_core_dlsym(void *handle, const char *name)
{
    pthread_mutex_lock(&mutex);
    error_text[0] = 0;
    void *answer = nullptr;
    for (auto *m = modules; m; m = m->next)
        if (m == handle && name)
            for (size_t i = 1; i < m->symbol_count; ++i)
            {
                const auto &s = m->symbols[i];
                const unsigned bind = ELF64_ST_BIND(s.st_info);
                if (s.st_shndx == SHN_UNDEF || (bind != STB_GLOBAL && bind != STB_WEAK) ||
                    ELF64_ST_VISIBILITY(s.st_other) == STV_HIDDEN ||
                    std::strcmp(string_at(m, s.st_name), name))
                    continue;
                uintptr_t address;
                if (symbol_address(m, i, address))
                    answer = reinterpret_cast<void *>(address);
                break;
            }
    if (!answer && handle)
        fail("core export not found: %s", name ? name : "<null>");
    pthread_mutex_unlock(&mutex);
    return answer;
}

extern "C" unsigned ps5_core_threads_live();

extern "C" int ps5_core_dlclose(void *handle)
{
    pthread_mutex_lock(&mutex);
    error_text[0] = 0;
    for (Module **at = &modules; *at; at = &(*at)->next)
        if (*at == handle)
        {
            Module *m = *at;
            if (--m->references == 0)
            {
                *at = m->next;
                /* A core's threads must be gone before its code is: PPSSPP
                 * detaches the threads of its dedicated tasks, and one still
                 * running after the unmap faulted on a jump into code that was
                 * no longer there -- closing a PSP game and loading the next
                 * crashed (2026-09-24). Their exit is waited for, up to two
                 * seconds; a core whose threads outlive that stays mapped. */
                unsigned live = ps5_core_threads_live();
                for (unsigned waited = 0; live != 0 && waited < 200; ++waited)
                {
                    const timespec pause = {0, 10 * 1000 * 1000};
                    nanosleep(&pause, nullptr);
                    live = ps5_core_threads_live();
                }
                for (size_t i = m->finalizer_count; i; --i)
                    reinterpret_cast<void (*)()>(m->finalizers[i - 1])();
                if (m->finalizer_count)
                    std::fprintf(stderr, "core loader: ran %zu finalizers\n", m->finalizer_count);
                if (live == 0)
                {
                    release(m);
                }
                else
                {
                    std::fprintf(
                        stderr,
                        "core loader: %u core thread(s) still running; the core stays mapped\n",
                        live);
                }
            }
            pthread_mutex_unlock(&mutex);
            return 0;
        }
    fail("invalid core handle on close");
    pthread_mutex_unlock(&mutex);
    return -1;
}

extern "C" char *ps5_core_dlerror()
{
    return error_text[0] ? error_text : nullptr;
}
