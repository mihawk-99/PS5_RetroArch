/*
 * PS5 RetroArch - one line about a fatal signal, before the console's own report.
 *
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The console's log says that a title died and where it faulted, but not what
 * called what: a call through a null pointer reports fault address 0 and nothing
 * else. This handler appends the instruction pointer, the stack pointer and the
 * return address on top of the stack to /app0/trace.txt, with the runtime address
 * of this handler, so build/title.map turns each into "object + offset" (subtract
 * the handler's slide) and a core's load base in the loader's "ready" line turns
 * an address inside a core into an offset in that core. It then flushes stdio, so
 * the buffered trace and RetroArch log lines that led up to the fault are kept,
 * and lets the signal take its default course. Nothing here runs unless a fatal
 * signal arrives.
 */

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include <fcntl.h>
#include <ucontext.h>
#include <unistd.h>

#include <ps5platform/context.h>

extern "C" int sceKernelAvailableFlexibleMemorySize(std::size_t *size);

namespace
{
void report(int signal, siginfo_t *info, void *context_pointer)
{
    /* stderr is buffered (src/main.cpp): what it holds -- an assertion's
     * message above all -- is written out first, unless the crashing thread
     * holds the stream, where flushing would deadlock instead of reporting. */
    if (ftrylockfile(stderr) == 0)
    {
        std::fflush(stderr);
        funlockfile(stderr);
    }
    const ucontext_t *context = static_cast<const ucontext_t *>(context_pointer);
    /* The SDK fork's ucontext_t is the console's layout (ps5platform/context.h
     * refuses any other), so the registers are the header's own fields. */
    const std::uintptr_t rip = static_cast<std::uintptr_t>(context->uc_mcontext.mc_rip);
    const std::uintptr_t rsp = static_cast<std::uintptr_t>(context->uc_mcontext.mc_rsp);
    /* After a call through a bad pointer the return address is on top of the
     * stack; otherwise the word is only a hint, and is read only when rsp looks
     * like a stack address. */
    std::uintptr_t top = 0;
    if (rsp >= 4096 && (rsp & 7) == 0)
        top = *reinterpret_cast<const std::uintptr_t *>(rsp);
    std::size_t flexible = 0;
    sceKernelAvailableFlexibleMemorySize(&flexible);
    char line[256];
    const int length = std::snprintf(
        line, sizeof(line),
        "crash: signal %d fault=0x%016llx rip=0x%016llx rsp=0x%016llx [rsp]=0x%016llx "
        "handler=0x%016llx flexible_free=%zu\n",
        signal, static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(info->si_addr)),
        static_cast<unsigned long long>(rip), static_cast<unsigned long long>(rsp),
        static_cast<unsigned long long>(top),
        static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(&report)), flexible);
    const int fd = open("/app0/trace.txt", O_WRONLY | O_APPEND);
    if (fd >= 0 && length > 0)
        (void)!write(fd, line, static_cast<std::size_t>(length));
    /* Sixteen more stack words: with frame pointers omitted, the callers'
     * return addresses are among them, and the map tells code from data. */
    if (fd >= 0 && top != 0)
    {
        (void)!write(fd, "crash: stack", 12);
        for (int i = 1; i <= 16; ++i)
        {
            char word[24];
            const int n = std::snprintf(
                word, sizeof(word), " %llx",
                static_cast<unsigned long long>(reinterpret_cast<const std::uintptr_t *>(rsp)[i]));
            (void)!write(fd, word, static_cast<std::size_t>(n));
        }
        (void)!write(fd, "\n", 1);
    }
    if (fd >= 0)
        close(fd);
    /* Not async-signal-safe, and worth the risk: the process is ending anyway,
     * and without it the lines just before the fault stay in their buffers. */
    std::fflush(nullptr);
    std::signal(signal, SIG_DFL);
    std::raise(signal);
}
} // namespace

extern "C" void ps5_crash_report_install()
{
    struct sigaction action;
    std::memset(&action, 0, sizeof(action));
    action.sa_sigaction = report;
    action.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigemptyset(&action.sa_mask);
    for (const int signal : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT})
        sigaction(signal, &action, nullptr);
}
