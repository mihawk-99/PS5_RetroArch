/* Large application buffers must not exhaust SceLibcInternal's private heap.
 * Wrap only title/core references; native library allocations stay libc-owned.
 * A locked list identifies mappings without reading before foreign pointers. */
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <pthread.h>
#include <sys/mman.h>
#include "memory_diagnostics.hpp"
#include "menu_memory.h"

extern "C"
{
    void *__real_malloc(size_t);
    void *__real_calloc(size_t, size_t);
    void *__real_realloc(void *, size_t);
    void __real_free(void *);
    void *__wrap_malloc(size_t);
    void __wrap_free(void *);
    /* src/overflow_heap.c: direct memory, for when these routes refuse. Weak, so
     * the host tests, which link this file without it, run with no overflow heap. */
    __attribute__((weak)) int ps5_overflow_owns(const void *);
    __attribute__((weak)) void *ps5_overflow_malloc(size_t);
    __attribute__((weak)) void *ps5_overflow_calloc(size_t, size_t);
    __attribute__((weak)) void *ps5_overflow_realloc(void *, size_t);
    __attribute__((weak)) void *ps5_overflow_memalign(size_t, size_t);
    __attribute__((weak)) void ps5_overflow_free(void *);
}

namespace
{
bool overflow_owns(const void *p)
{
    return ps5_overflow_owns && ps5_overflow_owns(p);
}
void *overflow_malloc(size_t n)
{
    return ps5_overflow_malloc ? ps5_overflow_malloc(n) : nullptr;
}
void *overflow_calloc(size_t c, size_t n)
{
    return ps5_overflow_calloc ? ps5_overflow_calloc(c, n) : nullptr;
}
void *overflow_realloc(void *p, size_t n)
{
    return ps5_overflow_realloc(p, n);
}
void *overflow_memalign(size_t a, size_t n)
{
    return ps5_overflow_memalign ? ps5_overflow_memalign(a, n) : nullptr;
}
void overflow_free(void *p)
{
    ps5_overflow_free(p);
}
} // namespace

namespace
{
constexpr size_t threshold = 1024 * 1024;
constexpr size_t page = 0x4000;
struct alignas(std::max_align_t) Mapping
{
    Mapping *next;
    size_t requested;
    size_t span;
};
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
Mapping *mappings = nullptr;

/* Only menu nodes/callbacks use these slabs. Native-library pointers still go
 * to their original allocator. A slab is reclaimed as soon as its last slot
 * is freed; no title-lifetime high-water cache and no per-entry mmap. */
constexpr size_t slab_span = 0x10000;
struct alignas(std::max_align_t) MenuSlot
{
    size_t requested;
};
struct alignas(std::max_align_t) MenuSlab
{
    MenuSlab *next;
    size_t stride, capacity, used;
    uint64_t bits[8];
};
MenuSlab *menu_slabs = nullptr;

MenuSlab **find_menu(void *pointer, size_t &index)
{
    const uintptr_t address = uintptr_t(pointer);
    MenuSlab **slot = &menu_slabs;
    while (*slot)
    {
        const uintptr_t first = uintptr_t(*slot + 1) + sizeof(MenuSlot);
        if (address >= first && address - first < (*slot)->capacity * (*slot)->stride &&
            (address - first) % (*slot)->stride == 0)
        {
            index = (address - first) / (*slot)->stride;
            return slot;
        }
        slot = &(*slot)->next;
    }
    return slot;
}

void *allocate_menu(size_t size)
{
    /* This API deliberately accepts only the two small menu object classes. */
    if (size > 1024)
    {
        errno = ENOMEM;
        return nullptr;
    }
    const size_t stride = sizeof(MenuSlot) + (size <= 128 ? 128 : 1024);
    pthread_mutex_lock(&lock);
    MenuSlab *slab = menu_slabs;
    while (slab && (slab->stride != stride || slab->used == slab->capacity))
        slab = slab->next;
    if (!slab)
    {
        void *mapped =
            mmap(nullptr, slab_span, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (mapped == MAP_FAILED)
        {
            pthread_mutex_unlock(&lock);
            return nullptr;
        }
        slab = static_cast<MenuSlab *>(mapped);
        slab->stride = stride;
        slab->capacity = (slab_span - sizeof(MenuSlab)) / stride;
        slab->next = menu_slabs;
        menu_slabs = slab;
    }
    size_t index = 0;
    while (slab->bits[index / 64] & (UINT64_C(1) << (index % 64)))
        ++index;
    slab->bits[index / 64] |= UINT64_C(1) << (index % 64);
    ++slab->used;
    auto *slot = reinterpret_cast<MenuSlot *>(reinterpret_cast<char *>(slab + 1) + index * stride);
    slot->requested = size;
    pthread_mutex_unlock(&lock);
    return slot + 1;
}

Mapping **find(void *pointer)
{
    Mapping **entry = &mappings;
    while (*entry && static_cast<void *>(*entry + 1) != pointer)
        entry = &(*entry)->next;
    return entry;
}

void *allocate(size_t size)
{
    if (size < threshold)
    {
        /* libc's private heap is small; when it refuses, direct memory serves. */
        void *native = __real_malloc(size ? size : 1);
        return native ? native : overflow_malloc(size);
    }
    if (size > SIZE_MAX - sizeof(Mapping) - (page - 1))
    {
        errno = ENOMEM;
        return nullptr;
    }
    const size_t span = (sizeof(Mapping) + size + page - 1) & ~(page - 1);
    void *memory = mmap(nullptr, span, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (memory == MAP_FAILED) /* flexible memory is small too */
        return overflow_malloc(size);
    auto *entry = static_cast<Mapping *>(memory);
    entry->requested = size;
    entry->span = span;
    pthread_mutex_lock(&lock);
    entry->next = mappings;
    mappings = entry;
    pthread_mutex_unlock(&lock);
    return entry + 1;
}

void release(void *pointer)
{
    if (!pointer)
        return;
    if (overflow_owns(pointer))
    {
        overflow_free(pointer);
        return;
    }
    pthread_mutex_lock(&lock);
    size_t index = 0;
    MenuSlab **menu_slot = find_menu(pointer, index);
    if (*menu_slot)
    {
        MenuSlab *slab = *menu_slot;
        slab->bits[index / 64] &= ~(UINT64_C(1) << (index % 64));
        const bool empty = --slab->used == 0;
        if (empty)
            *menu_slot = slab->next;
        pthread_mutex_unlock(&lock);
        if (empty)
            munmap(slab, slab_span);
        return;
    }
    Mapping **slot = find(pointer);
    Mapping *entry = *slot;
    if (entry)
        *slot = entry->next;
    pthread_mutex_unlock(&lock);
    if (entry)
        munmap(entry, entry->span);
    else
        __real_free(pointer);
}

void *resize(void *pointer, size_t size)
{
    if (!pointer)
        return allocate(size);
    if (!size)
    {
        release(pointer);
        return nullptr;
    }
    if (overflow_owns(pointer))
        return overflow_realloc(pointer, size);
    pthread_mutex_lock(&lock);
    Mapping *entry = *find(pointer);
    size_t index = 0;
    const bool menu = *find_menu(pointer, index) != nullptr;
    const size_t old_size =
        menu ? (static_cast<MenuSlot *>(pointer) - 1)->requested : (entry ? entry->requested : 0);
    pthread_mutex_unlock(&lock);
    /* Native buffers retain their allocator: their usable size is not part of
     * this ABI. Never guess it or read a private libc allocation header. */
    if (!entry && !menu)
    {
        /* A native buffer's usable size is libc's, so a refused realloc cannot
         * move it to the overflow heap; it fails as libc says. */
        return __real_realloc(pointer, size);
    }
    void *replacement = menu && size <= 1024 ? allocate_menu(size) : allocate(size);
    if (!replacement)
        return nullptr;
    std::memcpy(replacement, pointer, old_size < size ? old_size : size);
    release(pointer);
    return replacement;
}

} // namespace

extern "C" void *ps5_menu_malloc(size_t size)
{
    const auto caller = uintptr_t(__builtin_return_address(0));
    void *p = allocate_menu(size);
    if (p)
        ps5::memory::add({p, size, caller, ps5::memory::Route::Mapped});
    else
        ps5::memory::failure("menu-malloc", size, 0, caller, errno);
    return p;
}

extern "C" void *__wrap_malloc(size_t size)
{
    const auto caller = uintptr_t(__builtin_return_address(0));
    void *p = allocate(size);
    if (p)
        ps5::memory::add(
            {p, size, caller,
             size < threshold ? ps5::memory::Route::Native : ps5::memory::Route::Mapped});
    else
        ps5::memory::failure("malloc", size, 0, caller, errno);
    return p;
}
extern "C" void __wrap_free(void *pointer)
{
    ps5::memory::take(pointer);
    release(pointer);
}
extern "C" void *__wrap_calloc(size_t count, size_t size)
{
    const auto caller = uintptr_t(__builtin_return_address(0));
    if (size && count > SIZE_MAX / size)
    {
        errno = ENOMEM;
        ps5::memory::failure("calloc-overflow", size, count, caller, errno);
        return nullptr;
    }
    const size_t bytes = count * size;
    void *p = bytes < threshold ? (bytes ? __real_calloc(count, size) : __real_calloc(1, 1))
                                : allocate(bytes);
    if (!p && bytes < threshold)
        p = overflow_calloc(1, bytes);
    if (p)
        ps5::memory::add(
            {p, bytes, caller,
             bytes < threshold ? ps5::memory::Route::Native : ps5::memory::Route::Mapped});
    else
        ps5::memory::failure("calloc", bytes, 0, caller, errno);
    return p;
}
extern "C" void *__wrap_realloc(void *pointer, size_t size)
{
    const auto caller = uintptr_t(__builtin_return_address(0));
    const auto old = ps5::memory::take(pointer, true);
    void *p = resize(pointer, size);
    if (p)
    {
        // Realloc of a native pointer stays native even when it crosses 1 MiB.
        pthread_mutex_lock(&lock);
        size_t index = 0;
        const bool mapped = *find(p) || *find_menu(p, index);
        pthread_mutex_unlock(&lock);
        ps5::memory::add(
            {p, size, caller, mapped ? ps5::memory::Route::Mapped : ps5::memory::Route::Native});
    }
    else if (size)
    {
        ps5::memory::add(old); // Failed realloc retains ownership and contents.
        ps5::memory::failure("realloc", size, 0, caller, errno);
    }
    return p;
}
#ifdef PS5_MEMORY_DIAGNOSTICS
extern "C" int __real_posix_memalign(void **, size_t, size_t);
extern "C" int __wrap_posix_memalign(void **out, size_t alignment, size_t size)
{
    const auto caller = uintptr_t(__builtin_return_address(0));
    const int saved = errno;
    const int result = __real_posix_memalign(out, alignment, size);
    if (!result && *out)
        ps5::memory::add({*out, size, caller, ps5::memory::Route::Aligned});
    else if (result)
        ps5::memory::failure("posix_memalign", size, alignment, caller, result);
    errno = saved; // POSIX reports its error through the return value.
    return result;
}
#endif

/* A core's allocations, bound here by tools/core-imports.py: overflow heap first.
 * The console's system libraries allocate their own objects - a condition
 * variable, AGC's state - from the same small libc heap the title's routes use,
 * and a core that fills it breaks them from under the frontend: PPSSPP's symbol
 * map filled it, after which the driver's pthread_cond_init and sceAgcInit
 * failed inside vkCreateDevice. So a core's memory comes from direct memory
 * (src/overflow_heap.c), and the title's routes only when that refuses. Frees
 * need no binding: release() and resize() route a pointer by its address. */

extern "C" void *ps5_core_malloc(size_t size)
{
    void *p = overflow_malloc(size);
    return p ? p : allocate(size);
}

extern "C" void *ps5_core_calloc(size_t count, size_t size)
{
    if (size && count > SIZE_MAX / size)
    {
        errno = ENOMEM;
        return nullptr;
    }
    void *p = overflow_calloc(count, size);
    if (!p && (p = allocate(count * size)))
        std::memset(p, 0, count * size);
    return p;
}

extern "C" void *ps5_core_realloc(void *pointer, size_t size)
{
    if (!pointer)
        return ps5_core_malloc(size);
    return resize(pointer, size);
}

extern "C" void *ps5_core_aligned_alloc(size_t alignment, size_t size)
{
    return overflow_memalign(alignment, size);
}

extern "C" int ps5_core_posix_memalign(void **out, size_t alignment, size_t size)
{
    if (alignment < sizeof(void *) || (alignment & (alignment - 1)) != 0)
        return EINVAL;
    void *p = overflow_memalign(alignment, size);
    if (!p)
        return ENOMEM;
    *out = p;
    return 0;
}

extern "C" char *ps5_core_strdup(const char *text)
{
    const size_t bytes = std::strlen(text) + 1;
    char *copy = static_cast<char *>(ps5_core_malloc(bytes));
    if (copy)
        std::memcpy(copy, text, bytes);
    return copy;
}

extern "C" void *ps5_core_new(size_t size)
{
    if (void *p = ps5_core_malloc(size ? size : 1))
        return p;
    __builtin_trap();
}

extern "C" void *ps5_core_new_nothrow(size_t size, const void *)
{
    return ps5_core_malloc(size ? size : 1);
}
