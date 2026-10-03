#include "memory_status.h"
#include "trace.hpp"
#include <chrono>
#include <cstdio>
#include <mutex>
#include <ps5platform/heap.h>
#include <ps5platform/memory.h>

/* The linked RADV winsys's accounting, absent when building the old driver. */
extern "C" void radv_ps5_winsys_heap_usage(uint64_t *, uint64_t *, uint64_t *)
    __attribute__((weak));

extern "C" int ps5_memory_pressure(size_t request)
{
    static std::mutex lock;
    static std::chrono::steady_clock::time_point checked{};
    static ps5_memory_stats stats{};
    static bool known = false;
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> guard(lock);
    if (now - checked >= std::chrono::milliseconds(500))
    {
        known = ps5_memory_query(&stats);
        checked = now;
    }
    if (!known)
        return 0; // Unknown is not proof of pressure; failures still get handled.
    constexpr uint64_t reserve = UINT64_C(768) << 20;
    constexpr uint64_t contiguous = UINT64_C(256) << 20;
    return request > stats.free_bytes || stats.free_bytes - request < reserve ||
           stats.largest_bytes < (request > contiguous ? request : contiguous);
}

extern "C" void ps5_memory_report(const char *event, size_t request, int result)
{
    ps5_memory_stats stats{};
    const bool known = ps5_memory_query(&stats);
    struct ps5_heap_stats heap{};
    ps5_heap_stats(&heap);
    uint64_t gpu = 0, pool = 0, available = 0;
    if (radv_ps5_winsys_heap_usage)
        radv_ps5_winsys_heap_usage(&pool, &available, &gpu);
    char line[512];
    std::snprintf(line, sizeof(line),
                  "memory: event=%s result=%d request=%zu known=%d pool=%llu free=%llu "
                  "largest=%llu ranges=%u flexible=%llu cpu_heap=%zu gpu=%llu gpu_known=%d",
                  event, result, request, known, (unsigned long long)stats.total_bytes,
                  (unsigned long long)stats.free_bytes, (unsigned long long)stats.largest_bytes,
                  stats.free_ranges, (unsigned long long)stats.flexible_bytes, heap.mapped_bytes,
                  (unsigned long long)gpu, radv_ps5_winsys_heap_usage != nullptr);
    ps5::debug::mark(line);
}
