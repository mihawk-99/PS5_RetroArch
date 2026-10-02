/* PS5 RetroArch - host filesystem access from inside the title sandbox.
 * Copyright (C) 2026 Rodrigo Figueiredo
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A title's filesystem view is a sandbox: /app0 is the only real content and
 * paths like /mnt or /data do not exist from inside it. Homebrew escapes by
 * asking the resident jailbreak to run code in ring 0 (the kexec syscall) and
 * pointing the process's file credentials at the real filesystem root - that
 * is what Itemzflow's jb.prx does with libjbc.
 *
 * Unjailing the whole process would break /app0, so this does the weaker,
 * safer thing: while unjailed it bind-mounts pieces of the host filesystem
 * into directories inside the sandbox root (/mnt/sandbox/<TITLE>_000), then
 * jails the process again. The mounts live in the global mount table, so they
 * survive until the console reboots and a second launch finds them already
 * there. Inside the sandbox the host root appears at /hostroot and the real
 * /mnt replaces the sandbox's empty one, so USB drives and extended storage
 * become browsable.
 *
 * Kernel structure offsets are the ones libjbc uses on this console (they are
 * the FreeBSD proc/ucred/filedesc layout): thread->proc at +0x08, proc->ucred
 * at +0x40, proc->fd at +0x48, pid at +0xb0, the allproc le_next at +0x00 and
 * le_prev of the next entry at +0x08; ucred prison at +0x30 and the filedesc
 * root/jail dirs at +0x18/+0x20.
 */
extern "C"
{
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
}
#include <cstdint>
#include <cstdio>
#include <cstring>

/* Raw syscall entry point: the SDK's libc carries a syscall() shim for this
 * but it is not linked into the title's symbol set, so the same six-argument
 * register shuffle lives here. */
extern "C" long sandbox_syscall(long number, ...);
asm(".globl sandbox_syscall\n"
    "sandbox_syscall:\n"
    "mov %rdi, %rax\n"
    "mov %rsi, %rdi\n"
    "mov %rdx, %rsi\n"
    "mov %rcx, %rdx\n"
    "mov %r8, %r10\n"
    "mov %r9, %r8\n"
    "mov 8(%rsp), %r9\n"
    "syscall\n"
    "ret\n");
extern "C" void ps5_input_trace(const char *line) noexcept;

/* Crash-surviving diagnostic: each line is open/write/close, so even if the
 * process is killed by the kernel (a bad kernel write in the unjail cannot be
 * caught by the userspace handler and the buffered trace loses the session),
 * bootlog.txt still shows the last step that completed. */
extern "C" void ps5_bootlog(const char *line) noexcept
{
    char buffer[512];
    const int length = std::snprintf(buffer, sizeof(buffer), "%s\n", line);
    const int fd = open("/app0/bootlog.txt", O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd < 0)
        return;
    const ssize_t unused = write(fd, buffer, static_cast<std::size_t>(length));
    (void)unused;
    close(fd);
}

namespace
{
constexpr long kexec_syscall = 11;
constexpr long nmount_syscall = 378;

/* sys/mount.h on this target is the kernel-side header, so the userspace
 * pieces nmount wants are declared here. */
struct MountIovec
{
    void *base;
    std::size_t len;
};

/* Runs in ring 0 via the jailbreak's kexec syscall. uap[0] is this function's
 * own address as the syscall saw it; uap[1] is the caller's state block.
 * op 0 saves the current jail fields and unjails, op 1 puts the saved ones
 * back. Everything user-visible is read or written through `state`, whose
 * addresses are plain process memory the kernel context can reach. */
__attribute__((no_stack_protector)) void
jail_toggle(void *td, std::uint64_t **uap)
{
    auto *state = reinterpret_cast<std::uint64_t *>(uap[1]);
    const std::uint64_t op = state[0];
    const auto proc = *reinterpret_cast<std::uintptr_t *>(static_cast<char *>(td) + 0x08);
    const auto ucred = *reinterpret_cast<std::uintptr_t *>(proc + 0x40);
    const auto fdesc = *reinterpret_cast<std::uintptr_t *>(proc + 0x48);
    if (!proc || !ucred || !fdesc)
    {
        state[9] = 1;
        return;
    }
    if (op == 1)
    {
        *reinterpret_cast<std::uintptr_t *>(ucred + 0x30) = state[1]; /* prison */
        *reinterpret_cast<std::uintptr_t *>(fdesc + 0x10) = state[4]; /* cdir */
        *reinterpret_cast<std::uintptr_t *>(fdesc + 0x18) = state[2]; /* rdir */
        *reinterpret_cast<std::uintptr_t *>(fdesc + 0x20) = state[3]; /* jdir */
        state[9] = 0;
        return;
    }
    /* Find pid 1 by walking the allproc list forward; its credentials carry
     * prison0 and its filedesc points at the real root vnode. The le_prev
     * check is the list-integrity test libjbc uses before trusting a step. */
    std::uintptr_t prison0 = 0, rootvnode = 0, entry = proc;
    for (int step = 0; step < 4096; ++step)
    {
        if (*reinterpret_cast<std::int32_t *>(entry + 0xb0) == 1)
        {
            const auto p1_ucred = *reinterpret_cast<std::uintptr_t *>(entry + 0x40);
            const auto p1_fd = *reinterpret_cast<std::uintptr_t *>(entry + 0x48);
            prison0 = *reinterpret_cast<std::uintptr_t *>(p1_ucred + 0x30);
            rootvnode = *reinterpret_cast<std::uintptr_t *>(p1_fd + 0x18);
            break;
        }
        const auto next = *reinterpret_cast<std::uintptr_t *>(entry);
        if (!next || *reinterpret_cast<std::uintptr_t *>(next + 0x08) != entry)
            break;
        entry = next;
    }
    if (!prison0 || !rootvnode)
    {
        state[9] = 2;
        return;
    }
    state[1] = *reinterpret_cast<std::uintptr_t *>(ucred + 0x30);
    state[2] = *reinterpret_cast<std::uintptr_t *>(fdesc + 0x18);
    state[3] = *reinterpret_cast<std::uintptr_t *>(fdesc + 0x20);
    state[4] = *reinterpret_cast<std::uintptr_t *>(fdesc + 0x10);
    *reinterpret_cast<std::uintptr_t *>(ucred + 0x30) = prison0;
    *reinterpret_cast<std::uintptr_t *>(fdesc + 0x18) = rootvnode;
    *reinterpret_cast<std::uintptr_t *>(fdesc + 0x20) = rootvnode;
    *reinterpret_cast<std::uintptr_t *>(fdesc + 0x10) = rootvnode;
    state[9] = 0;
}

void kexec(void *fn, void *uap)
{
    (void)sandbox_syscall(kexec_syscall, fn, uap);
}

/* Ring-0 probe: writes a marker into userspace only. If the jailbreak's kexec
 * syscall and its uap ABI are what this code expects, it comes back with
 * state[9] == 0x42 and nothing in kernel memory was ever dereferenced. */
__attribute__((no_stack_protector)) void
kexec_probe(void *, std::uint64_t **uap)
{
    auto *state = reinterpret_cast<std::uint64_t *>(uap[1]);
    state[9] = 0x42;
}

bool dir_exists(const char *path)
{
    DIR *dir = opendir(path);
    if (dir)
    {
        closedir(dir);
        return true;
    }
    return false;
}

int mount_nullfs(const char *what, const char *where)
{
    MountIovec iov[8];
    int count = 0;
    const auto push = [&iov, &count](const char *name, const char *value)
    {
        iov[count].base = const_cast<char *>(name);
        iov[count].len = std::strlen(name) + 1;
        ++count;
        iov[count].base = const_cast<char *>(value);
        iov[count].len = std::strlen(value) + 1;
        ++count;
    };
    push("fstype", "nullfs");
    push("fspath", where);
    push("target", what);
    push("rw", "");
    return static_cast<int>(sandbox_syscall(nmount_syscall, iov, count, 0));
}

/* True when the sandbox escapes already happened this boot: mounts survive a
 * title restart because the table is global. */
bool mounts_present()
{
    return dir_exists("/hostroot/mnt") || dir_exists("/hostroot/data");
}
} // namespace

/* Returns a bitmask: bit 0 = host root mounted at /hostroot, bit 1 = the real
 * /mnt mounted over the sandbox one. 0 = still fully sandboxed. */
extern "C" unsigned ps5_sandbox_mount_host_fs() noexcept
{
    if (mounts_present())
    {
        ps5_input_trace("sandbox: host mounts already present");
        return 3;
    }
    ps5_bootlog("sandbox: entering ps5_sandbox_mount_host_fs");
    /* The kexec unjail kills the process outright when the jailbreak does not
     * hook syscall 11 or the kernel offsets do not match this firmware - the
     * last release closed on startup exactly there. It now runs only when the
     * user drops a flag file into /app0, and even then a userspace-only probe
     * payload goes first so a mismatched kexec ABI is detected before any
     * kernel pointer is touched. */
    if (!dir_exists("/app0/enable_unjail"))
    {
        ps5_input_trace("sandbox: unjail disabled (no /app0/enable_unjail); staying inside /app0");
        return 0;
    }
    std::uint64_t state[10] = {};
    state[9] = 0xff; /* untouched => the kexec syscall itself failed */
    ps5_bootlog("sandbox: before kexec(probe)");
    kexec(reinterpret_cast<void *>(&kexec_probe), state);
    {
        char line[160];
        std::snprintf(line, sizeof(line), "sandbox: after kexec(probe) status=%llu",
                      static_cast<unsigned long long>(state[9]));
        ps5_bootlog(line);
    }
    if (state[9] != 0x42)
    {
        ps5_input_trace("sandbox: kexec probe failed; staying inside /app0");
        return 0;
    }
    state[9] = 0xff;
    ps5_bootlog("sandbox: before kexec(unjail)");
    kexec(reinterpret_cast<void *>(&jail_toggle), state);
    {
        char line[160];
        std::snprintf(line, sizeof(line), "sandbox: after kexec(unjail) status=%llu",
                      static_cast<unsigned long long>(state[9]));
        ps5_bootlog(line);
    }
    if (state[9] != 0)
    {
        char line[160];
        std::snprintf(line, sizeof(line),
                      "sandbox: unjail unavailable (status=%llu, 0xff=no jailbreak); staying inside /app0",
                      static_cast<unsigned long long>(state[9]));
        ps5_input_trace(line);
        return 0;
    }
    /* Unjailed: paths now see the real filesystem. Locate this title's
     * sandbox root under /mnt/sandbox (named <TITLE_ID>_000). */
    char sandbox_root[160] = {};
    DIR *sandboxes = opendir("/mnt/sandbox");
    if (sandboxes)
    {
        while (dirent *entry = readdir(sandboxes))
            if (std::strncmp(entry->d_name, "PPSA99169", 9) == 0)
            {
                std::snprintf(sandbox_root, sizeof(sandbox_root), "/mnt/sandbox/%s",
                              entry->d_name);
                break;
            }
        closedir(sandboxes);
    }
    {
        char line[192];
        std::snprintf(line, sizeof(line), "sandbox: unjailed; root=%s",
                      sandbox_root[0] ? sandbox_root : "NOT FOUND");
        ps5_bootlog(line);
    }
    unsigned mounted = 0;
    int mkdir_result = -1, mount_root = -1, mount_mnt = -1;
    if (sandbox_root[0])
    {
        char hostroot[192], realmnt[192];
        std::snprintf(hostroot, sizeof(hostroot), "%s/hostroot", sandbox_root);
        std::snprintf(realmnt, sizeof(realmnt), "%s/mnt", sandbox_root);
        mkdir_result = mkdir(hostroot, 0777);
        mount_root = mount_nullfs("/", hostroot);
        if (mount_root == 0)
            mounted |= 1;
        /* The sandbox's own /mnt is empty; overlay the real mount directory so
         * usb drives and extended storage show up where the menu expects them. */
        if (!dir_exists(realmnt))
            (void)mkdir(realmnt, 0777);
        mount_mnt = mount_nullfs("/mnt", realmnt);
        if (mount_mnt == 0)
            mounted |= 2;
    }
    {
        char line[192];
        std::snprintf(line, sizeof(line),
                      "sandbox: mounts done mkdir=%d root=%d mnt=%d; before rejail",
                      mkdir_result, mount_root, mount_mnt);
        ps5_bootlog(line);
    }
    /* Rejail with the saved fields no matter how the mounts went. */
    state[0] = 1;
    kexec(reinterpret_cast<void *>(&jail_toggle), state);
    if (state[9] != 0)
        ps5_input_trace("sandbox: rejail failed; process stays unjailed");
    ps5_bootlog("sandbox: after rejail; done");
    char line[224];
    std::snprintf(line, sizeof(line),
                  "sandbox: root=%s mkdir=%d mount_root=%d mount_mnt=%d mounted=%u",
                  sandbox_root[0] ? sandbox_root : "not found", mkdir_result, mount_root,
                  mount_mnt, mounted);
    ps5_input_trace(line);
    return mounted;
}
