/*
 * PS5 RetroArch - an opt-in sampling profiler for the thread that runs frames.
 *
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Why this exists. A frame that stalls for a tenth of a second or for twenty
 * seconds shows up in ../PS5_Vulkan's hitch report as "app" time: the driver
 * knows the frame spent it outside every driver call, not where. This samples
 * where: a thread interrupts the main thread (the one RetroArch runs the core
 * and the driver on) about every millisecond with a signal, whose handler
 * records the interrupted instruction pointer. A sample taken while no frame
 * has been presented for over 50 ms (the driver's own present clock,
 * ps5vk_debug_last_present_ns) counts as a stall sample.
 *
 * Every ten seconds the window's stall samples are summarised as their most
 * frequent addresses, one line each, which tools resolve against the title's
 * map and the loaded core's base. Nothing is written per frame.
 *
 * Testing only: enabled by /app0/ps5-sampler.txt, which is never shipped.
 */

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <pthread.h>
#include <signal.h>
#include <ucontext.h>

extern "C"
{
    /* ../PS5_Vulkan's debug API; the title always links the driver. */
    std::uint64_t ps5vk_debug_now_ns(void);
    std::uint64_t ps5vk_debug_last_present_ns(void);
}

namespace
{
constexpr std::uint32_t kRingSize = 1u << 16;
/* The main thread is thread 0; core threads (src/core_threads_ps5.cpp) follow. */
constexpr unsigned kMaxThreads = 64;
/* Per sample: the flags word (bit 0: a stall), rip, the return address on top
 * of the stack and the frame-pointer chain's return addresses. */
constexpr unsigned kFrames = 10;
constexpr std::uint64_t kStallNs = 50ull * 1000 * 1000;
constexpr std::uint64_t kWindowNs = 10ull * 1000 * 1000 * 1000;
constexpr int kSampleSignal = SIGUSR2;
constexpr unsigned kTop = 60;

pthread_t g_threads[kMaxThreads];
std::atomic<unsigned> g_thread_count{0};
std::atomic<bool> g_running{false};
/* The core threads to sample, by start routine: the flag file's hex addresses,
 * one a line. Interrupting every thread every few milliseconds stopped PPSSPP's
 * emulation (a wait it does not retry), so only the named ones are. */
constexpr unsigned kMaxStarts = 16;
std::uint64_t g_starts[kMaxStarts];
unsigned g_start_count = 0;
std::atomic<bool> g_stall{false};
std::atomic<std::uint32_t> g_head{0};
std::uint64_t g_ring[kRingSize][kFrames + 1];

/* The stack a walk may read: from rsp up to a bound no thread stack exceeds. */
constexpr std::uint64_t kStackSpan = 16ull * 1024 * 1024;

void on_sample(int, siginfo_t *, void *context_pointer)
{
    const auto *context = static_cast<const ucontext_t *>(context_pointer);
    /* The console's mcontext is the SDK header's shifted by six words
     * (src/crash_report.cpp): rip at 26, rsp at 29, and so rbp at 15. */
    const auto *words = reinterpret_cast<const std::uint64_t *>(&context->uc_mcontext);
    const std::uint32_t at = g_head.fetch_add(1, std::memory_order_relaxed);
    std::uint64_t *const sample = g_ring[at & (kRingSize - 1)];
    const std::uint64_t rsp = words[29];
    unsigned thread = 0;
    const pthread_t self = pthread_self();
    const unsigned count = g_thread_count.load(std::memory_order_acquire);
    for (unsigned index = 0; index < count; ++index)
        if (pthread_equal(g_threads[index], self))
            thread = index;
    sample[0] = (g_stall.load(std::memory_order_relaxed) ? 1u : 0u) | (std::uint64_t{thread} << 8);
    sample[1] = words[26];
    sample[2] = (rsp & 7) == 0 && rsp != 0 ? *reinterpret_cast<const std::uint64_t *>(rsp) : 0;
    std::uint64_t frame = words[15];
    for (unsigned depth = 3; depth <= kFrames; ++depth)
    {
        if ((frame & 7) != 0 || frame < rsp || frame + 16 > rsp + kStackSpan)
        {
            sample[depth] = 0;
            continue;
        }
        const auto *const pair = reinterpret_cast<const std::uint64_t *>(frame);
        sample[depth] = pair[1];
        frame = pair[0] > frame ? pair[0] : 0;
    }
}

/* libkernel and libc, where a blocked thread sits; the first frame outside
 * them is the one that asked to wait. */
bool system_address(std::uint64_t address)
{
    return address >= 0x800000000ull && address < 0x800200000ull;
}

struct Count
{
    std::uint64_t rip;
    std::uint64_t leaf;
    std::uint32_t samples;
    std::uint32_t thread;
    std::uint32_t example;
};

Count g_counts[4096];

/* Folds one window's stall samples into address counts and prints the most
 * frequent. Linear probing in a fixed table: the sampler allocates nothing. */
void report(std::uint32_t from, std::uint32_t to, std::uint32_t stall_samples)
{
    std::memset(g_counts, 0, sizeof(g_counts));
    constexpr std::uint32_t slots = sizeof(g_counts) / sizeof(g_counts[0]);
    for (std::uint32_t at = from; at != to; ++at)
    {
        const std::uint64_t *const sample = g_ring[at & (kRingSize - 1)];
        if ((sample[0] & 1u) == 0)
            continue;
        const std::uint64_t leaf = sample[1] & ~std::uint64_t{0xff};
        std::uint64_t rip = sample[1];
        for (unsigned depth = 1; depth <= kFrames && system_address(rip); ++depth)
            if (sample[depth] != 0 && !system_address(sample[depth]))
                rip = sample[depth];
        const auto thread = static_cast<std::uint32_t>((sample[0] >> 8) & 0xff);
        std::uint32_t slot =
            static_cast<std::uint32_t>(((rip >> 4) ^ (leaf >> 8) ^ (std::uint64_t{thread} << 40)) *
                                       2654435761u) %
            slots;
        for (std::uint32_t probe = 0; probe < slots; ++probe, slot = (slot + 1) % slots)
        {
            if (g_counts[slot].samples == 0 ||
                (g_counts[slot].rip == rip && g_counts[slot].leaf == leaf &&
                 g_counts[slot].thread == thread))
            {
                g_counts[slot].rip = rip;
                g_counts[slot].leaf = leaf;
                g_counts[slot].example = at;
                g_counts[slot].thread = thread;
                ++g_counts[slot].samples;
                break;
            }
        }
    }
    std::fprintf(stderr, "sampler: window samples=%u stall=%u\n", to - from, stall_samples);
    for (unsigned rank = 0; rank < kTop; ++rank)
    {
        std::uint32_t best = slots;
        for (std::uint32_t slot = 0; slot < slots; ++slot)
            if (g_counts[slot].samples != 0 &&
                (best == slots || g_counts[slot].samples > g_counts[best].samples))
                best = slot;
        if (best == slots)
            break;
        std::fprintf(stderr, "sampler: stall thread=%u caller=0x%016llx leaf=0x%016llx n=%u\n",
                     g_counts[best].thread, static_cast<unsigned long long>(g_counts[best].rip),
                     static_cast<unsigned long long>(g_counts[best].leaf), g_counts[best].samples);
        /* The first ten groups carry one whole sampled chain. */
        if (rank < 10)
        {
            const std::uint64_t *const chain = g_ring[g_counts[best].example & (kRingSize - 1)];
            char line[512];
            int used = std::snprintf(line, sizeof(line), "sampler:   chain");
            for (unsigned depth = 1; depth <= kFrames && used > 0 && used < 480; ++depth)
                used += std::snprintf(line + used, sizeof(line) - used, " %llx",
                                      static_cast<unsigned long long>(chain[depth]));
            std::fprintf(stderr, "%s\n", line);
        }
        g_counts[best].samples = 0;
    }
}

void *sampler(void *)
{
    const timespec interval = {0, 2 * 1000 * 1000};
    std::uint64_t window_start = ps5vk_debug_now_ns();
    std::uint32_t window_from = g_head.load();
    std::uint32_t stall_samples = 0;
    for (;;)
    {
        nanosleep(&interval, nullptr);
        const std::uint64_t now = ps5vk_debug_now_ns();
        const std::uint64_t last = ps5vk_debug_last_present_ns();
        const bool stall = last != 0 && now > last && now - last > kStallNs;
        g_stall.store(stall, std::memory_order_relaxed);
        stall_samples += stall ? 1u : 0u;
        const unsigned count = g_thread_count.load(std::memory_order_acquire);
        for (unsigned index = 0; index < count; ++index)
            pthread_kill(g_threads[index], kSampleSignal);
        if (now - window_start >= kWindowNs)
        {
            const std::uint32_t to = g_head.load();
            /* A window longer than the ring keeps only its last samples. */
            const std::uint32_t from = to - window_from > kRingSize ? to - kRingSize : window_from;
            if (stall_samples != 0)
                report(from, to, stall_samples);
            window_start = now;
            window_from = to;
            stall_samples = 0;
        }
    }
    return nullptr;
}
} // namespace

/* Starts the sampler on the calling thread when /app0/ps5-sampler.txt exists. */
extern "C" void ps5_sampler_start()
{
    std::FILE *const flag = std::fopen("/app0/ps5-sampler.txt", "rb");
    if (flag == nullptr)
        return;
    char line[64];
    while (g_start_count < kMaxStarts && std::fgets(line, sizeof(line), flag) != nullptr)
    {
        char *end = nullptr;
        const unsigned long long start = std::strtoull(line, &end, 16);
        if (end != line && start != 0)
            g_starts[g_start_count++] = start;
    }
    std::fclose(flag);
    g_threads[0] = pthread_self();
    g_thread_count.store(1, std::memory_order_release);
    g_running.store(true, std::memory_order_release);
    struct sigaction action;
    std::memset(&action, 0, sizeof(action));
    action.sa_sigaction = on_sample;
    action.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&action.sa_mask);
    if (sigaction(kSampleSignal, &action, nullptr) != 0)
    {
        std::fputs("sampler: the sample signal could not be installed\n", stderr);
        return;
    }
    pthread_t thread;
    if (pthread_create(&thread, nullptr, sampler, nullptr) == 0)
    {
        pthread_detach(thread);
        std::fputs("sampler: sampling the main thread and core threads every 2 ms\n", stderr);
    }
}

/* A core thread to sample too (src/core_threads_ps5.cpp); its number is its
 * order of creation after the main thread's 0. */
extern "C" void ps5_sampler_add_thread(pthread_t thread, const void *start)
{
    if (!g_running.load(std::memory_order_acquire))
        return;
    bool named = false;
    for (unsigned at = 0; at < g_start_count; ++at)
        named = named || g_starts[at] == reinterpret_cast<std::uintptr_t>(start);
    if (!named)
        return;
    const unsigned index = g_thread_count.load(std::memory_order_relaxed);
    if (index >= kMaxThreads)
        return;
    g_threads[index] = thread;
    g_thread_count.store(index + 1, std::memory_order_release);
    std::fprintf(stderr, "sampler: thread %u added, start=%p\n", index, start);
}
