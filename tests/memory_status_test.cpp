#include "memory_status.h"
#include "trace.hpp"
#include <ps5platform/heap.h>
#include <ps5platform/memory.h>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
static ps5_memory_stats snapshot{};
static bool known = true;
static unsigned queries;
static std::string report;
extern "C" bool ps5_memory_query(ps5_memory_stats *out)
{
    ++queries;
    *out = snapshot;
    return known;
}
extern "C" void ps5_heap_stats(struct ps5_heap_stats *out)
{
    *out = {};
    out->mapped_bytes = 123;
}
extern "C" void radv_ps5_winsys_heap_usage(uint64_t *pool, uint64_t *free, uint64_t *used)
{
    *pool = 1000;
    *free = 800;
    *used = 200;
}
namespace ps5::debug
{
void mark(const char *text) noexcept
{
    report = text;
}
} // namespace ps5::debug
static void refresh()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(510));
}
int main()
{
    constexpr uint64_t mib = UINT64_C(1) << 20;
    snapshot.free_bytes = 4096 * mib;
    snapshot.largest_bytes = 2048 * mib;
    assert(!ps5_memory_pressure(0));
    assert(!ps5_memory_pressure(512 * mib));
    assert(queries == 1); // Cache pressure checks do not scan the kernel per frame.
    assert(ps5_memory_pressure(4097 * mib)); // No unsigned subtraction wrap.
    assert(ps5_memory_pressure(2049 * mib)); // Enough total, no contiguous block.
    snapshot.free_bytes = 767 * mib;
    snapshot.largest_bytes = 767 * mib;
    refresh();
    assert(ps5_memory_pressure(0));
    snapshot.free_bytes = 4096 * mib;
    snapshot.largest_bytes = 255 * mib;
    refresh();
    assert(ps5_memory_pressure(0)); // Fragmentation independently triggers cleanup.
    known = false;
    refresh();
    assert(!ps5_memory_pressure(SIZE_MAX)); // Unknown does not force eviction.
    ps5_memory_report("test-failure", 42, -2);
    assert(report.find("event=test-failure result=-2 request=42 known=0") != std::string::npos);
    assert(report.find("cpu_heap=123 gpu=200 gpu_known=1") != std::string::npos);
    puts("memory policy: headroom, fragmentation, oversized requests, cached and unavailable "
         "queries pass");
}
