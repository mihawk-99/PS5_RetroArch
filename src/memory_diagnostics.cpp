/* Fixed-size metadata and stack-only formatting: diagnostics must survive OOM.
 * No native allocator calls, stdio, dynamic C++ containers or worker threads. */
#include "memory_diagnostics.hpp"
#include "memory_xmb.h"
#ifdef PS5_MEMORY_DIAGNOSTICS
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

namespace ps5::memory
{
namespace
{
constexpr size_t capacity = 131072;
Record records[capacity];
// 0 = never used, 1 = live, 2 = tombstone. Pointer values remain unmodified.
unsigned char states[capacity];
Stats stats;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
int output = -1;
uint64_t start_ns = 0, next_ns = 0, sequence = 0, failure_next_ns = 0;
uint64_t image_create = 0, image_destroy = 0, image_failed = 0;
uint64_t idle_begin = 0, idle_end = 0, idle_failed = 0;
struct XmbState
{
    uint64_t tab = 0, kind = 0, current = 0, old = 0, horizontal = 0;
    uint64_t phase = 0, list_size = 0, index = 0, ms = 0;
    uint64_t nodes = 0, created = 0, copied = 0, freed = 0, unmatched_frees = 0;
    uint64_t native_bytes = 0, native_count = 0;
};
XmbState xmb;
XmbState xmb_history[8];
uint64_t xmb_events = 0;

uint64_t now()
{
    timespec t{};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return uint64_t(t.tv_sec) * 1000000000ULL + uint64_t(t.tv_nsec);
}
struct Line
{
    char data[4096];
    size_t length = 0;
    void text(const char *s)
    {
        while (*s && length < sizeof(data) - 1)
            data[length++] = *s++;
    }
    void number(uint64_t n, unsigned base = 10)
    {
        char digits[32];
        size_t count = 0;
        do
        {
            digits[count++] = "0123456789abcdef"[n % base];
            n /= base;
        } while (n);
        while (count && length < sizeof(data) - 1)
            data[length++] = digits[--count];
    }
    void field(const char *name, uint64_t n)
    {
        text(" ");
        text(name);
        text("=");
        number(n);
    }
    void emit()
    {
        if (output < 0)
            return;
        data[length++] = '\n';
        size_t done = 0;
        while (done < length)
        {
            const auto written = write(output, data + done, length - done);
            if (written < 0 && errno == EINTR)
                continue;
            if (written <= 0)
                break;
            done += size_t(written);
        }
    }
};
size_t hash(void *p)
{
    return ((uintptr_t(p) >> 4) * uintptr_t(11400714819323198485ULL)) & (capacity - 1);
}
void summary(const char *kind)
{
    Line line;
    line.text(kind);
    line.field("seq", ++sequence);
    line.field("ms", (now() - start_ns) / 1000000);
    const char *bytes[] = {"native_bytes", "mapped_bytes", "aligned_bytes"};
    const char *counts[] = {"native_count", "mapped_count", "aligned_count"};
    const char *peaks[] = {"native_peak", "mapped_peak", "aligned_peak"};
    for (unsigned i = 0; i < 3; ++i)
    {
        line.field(bytes[i], stats.bytes[i]);
        line.field(counts[i], stats.count[i]);
        line.field(peaks[i], stats.peak[i]);
    }
    line.field("failures", stats.failures);
    line.field("failure_records", stats.failure_records);
    line.field("failure_suppressed", stats.failures - stats.failure_records);
    line.field("dropped", stats.dropped);
    line.field("foreign_frees", stats.foreign_frees);
    line.field("foreign_reallocs", stats.foreign_reallocs);
    line.field("image_create", image_create);
    line.field("image_destroy", image_destroy);
    line.field("image_failed", image_failed);
    line.field("idle_begin", idle_begin);
    line.field("idle_end", idle_end);
    line.field("idle_failed", idle_failed);
    line.emit();
}
// Fixed aggregation table: caller attribution may overflow independently of
// the live-pointer table. Report that explicitly, never silently lose counts.
void callers(const char *kind = "caller", int route = -1, unsigned limit = 8)
{
    struct Site
    {
        uintptr_t caller = 0;
        uint64_t bytes = 0, count = 0;
    };
    Site sites[2048]{};
    uint64_t omitted = 0;
    for (size_t i = 0; i < capacity; ++i)
    {
        if (states[i] != 1)
            continue;
        const Record &r = records[i];
        if (route >= 0 && unsigned(r.route) != unsigned(route))
            continue;
        size_t j = (r.caller >> 4) & 2047, tries = 0;
        while (sites[j].count && sites[j].caller != r.caller && tries < 2048)
        {
            j = (j + 1) & 2047;
            ++tries;
        }
        if (tries == 2048)
        {
            ++omitted;
            continue;
        }
        sites[j].caller = r.caller;
        sites[j].bytes += r.bytes;
        ++sites[j].count;
    }
    for (unsigned rank = 0; rank < limit; ++rank)
    {
        size_t best = 0;
        for (size_t i = 1; i < 2048; ++i)
            if (sites[i].bytes > sites[best].bytes)
                best = i;
        if (!sites[best].count)
            break;
        Line line;
        line.text(kind);
        line.field("seq", sequence);
        if (route >= 0)
            line.field("route", unsigned(route));
        line.text(" pc=0x");
        line.number(sites[best].caller, 16);
        line.field("bytes", sites[best].bytes);
        line.field("count", sites[best].count);
        line.field("site_records_omitted", omitted);
        line.emit();
        sites[best] = {};
    }
}
void xmb_line(const char *kind, const XmbState &s, uint64_t event)
{
    Line line;
    line.text(kind);
    line.field("seq", sequence);
    line.field("event", event);
    line.field("ms", s.ms);
    line.field("tab", s.tab);
    line.field("kind", s.kind);
    line.field("phase", s.phase);
    line.field("current", s.current);
    line.field("old", s.old);
    line.field("horizontal", s.horizontal);
    line.field("list_size", s.list_size);
    line.field("index", s.index);
    line.field("nodes", s.nodes);
    line.field("created", s.created);
    line.field("copied", s.copied);
    line.field("freed", s.freed);
    line.field("unmatched_frees", s.unmatched_frees);
    line.field("native_bytes", s.native_bytes);
    line.field("native_count", s.native_count);
    line.emit();
}
} // namespace
void add(Record record)
{
    if (!record.pointer)
        return;
    const int saved = errno;
    pthread_mutex_lock(&mutex);
    size_t slot = hash(record.pointer), vacant = capacity;
    for (size_t n = 0; n < capacity; ++n, slot = (slot + 1) & (capacity - 1))
    {
        if (states[slot] != 1)
        {
            vacant = slot;
            break;
        }
    }
    if (vacant == capacity)
        ++stats.dropped;
    else
    {
        states[vacant] = 1;
        records[vacant] = record;
        const auto route = unsigned(record.route);
        stats.bytes[route] += record.bytes;
        ++stats.count[route];
        if (stats.bytes[route] > stats.peak[route])
            stats.peak[route] = stats.bytes[route];
    }
    pthread_mutex_unlock(&mutex);
    errno = saved;
}
Record take(void *pointer, bool resizing)
{
    Record result;
    if (!pointer)
        return result;
    const int saved = errno;
    pthread_mutex_lock(&mutex);
    size_t slot = hash(pointer);
    for (size_t n = 0; n < capacity && states[slot]; ++n, slot = (slot + 1) & (capacity - 1))
    {
        if (states[slot] == 1 && records[slot].pointer == pointer)
        {
            result = records[slot];
            states[slot] = 2;
            stats.bytes[unsigned(result.route)] -= result.bytes;
            --stats.count[unsigned(result.route)];
            break;
        }
    }
    if (!result.pointer)
    {
        if (resizing)
            ++stats.foreign_reallocs;
        else
            ++stats.foreign_frees;
    }
    pthread_mutex_unlock(&mutex);
    errno = saved;
    return result;
}
void failure(const char *operation, size_t bytes, size_t alignment, uintptr_t caller, int error)
{
    const int saved = errno;
    pthread_mutex_lock(&mutex);
    ++stats.failures;
    // A failed allocation can be retried thousands of times by menu code.
    // Preserve the first four, then at most one detail+summary per five seconds,
    // even if presentation has stopped. Counters still include every failure.
    const uint64_t time = now();
    if (stats.failure_records < 4 || time >= failure_next_ns)
    {
        ++stats.failure_records;
        failure_next_ns = time + 5000000000ULL;
        Line line;
        line.text("failure op=");
        line.text(operation);
        line.field("ms", (now() - start_ns) / 1000000);
        line.field("bytes", bytes);
        line.field("alignment", alignment);
        line.field("error", unsigned(error));
        line.text(" pc=0x");
        line.number(caller, 16);
        line.emit();
        summary("failure-summary");
        if (stats.failures == 1)
        {
            // Cache numeric state before allocations; never call frontend code
            // or dereference its lists from this allocator failure path.
            XmbState first = xmb;
            first.native_bytes = stats.bytes[0];
            first.native_count = stats.count[0];
            xmb_line("first-failure-xmb", first, xmb_events);
            const uint64_t begin = xmb_events > 8 ? xmb_events - 8 : 0;
            for (uint64_t i = begin; i < xmb_events; ++i)
                xmb_line("first-failure-xmb-history", xmb_history[i % 8], i + 1);
            for (int route = 0; route < 3; ++route)
                callers("first-failure-caller", route, 16);
        }
    }
    pthread_mutex_unlock(&mutex);
    errno = saved;
}
Stats snapshot()
{
    pthread_mutex_lock(&mutex);
    Stats result = stats;
    pthread_mutex_unlock(&mutex);
    return result;
}
void init(const char *path, const char *identity)
{
    const int saved = errno;
    pthread_mutex_lock(&mutex);
    if (output >= 0)
        close(output);
    output = open(path, O_WRONLY | O_CREAT | O_APPEND, 0666);
    start_ns = now();
    next_ns = start_ns + 5000000000ULL;
    Line line;
    line.text("session ");
    line.text(identity);
    line.field("pid", uint64_t(getpid()));
    line.field("table_capacity", capacity);
    line.field("table_bytes", sizeof(records) + sizeof(states));
    line.emit();
    summary("initial");
    pthread_mutex_unlock(&mutex);
    errno = saved;
}
void tick()
{
    const int saved = errno;
    pthread_mutex_lock(&mutex);
    const uint64_t time = now();
    if (output >= 0 && time >= next_ns)
    {
        next_ns = time + 5000000000ULL;
        summary("sample");
        callers();
    }
    pthread_mutex_unlock(&mutex);
    errno = saved;
}
void event(const char *name, bool success)
{
    const int saved = errno;
    pthread_mutex_lock(&mutex);
    if (!std::strcmp(name, "image_create"))
    {
        if (success)
            ++image_create;
        else
            ++image_failed;
    }
    else if (!std::strcmp(name, "image_destroy"))
        ++image_destroy;
    else if (!std::strcmp(name, "idle_begin"))
        ++idle_begin;
    else if (!std::strcmp(name, "idle_end"))
    {
        ++idle_end;
        if (!success)
            ++idle_failed;
    }
    pthread_mutex_unlock(&mutex);
    errno = saved;
}
void finish()
{
    const int saved = errno;
    pthread_mutex_lock(&mutex);
    summary("final");
    callers();
    if (output >= 0)
        close(output);
    output = -1;
    pthread_mutex_unlock(&mutex);
    errno = saved;
}
} // namespace ps5::memory

extern "C" void ps5_memory_xmb_context(unsigned tab, unsigned kind, size_t current, size_t old,
                                       size_t horizontal)
{
    using namespace ps5::memory;
    const int saved = errno;
    pthread_mutex_lock(&mutex);
    xmb.tab = tab;
    xmb.kind = kind;
    xmb.current = current;
    xmb.old = old;
    xmb.horizontal = horizontal;
    pthread_mutex_unlock(&mutex);
    errno = saved;
}
extern "C" void ps5_memory_xmb_stage(unsigned phase, size_t list_size, size_t index)
{
    using namespace ps5::memory;
    const int saved = errno;
    pthread_mutex_lock(&mutex);
    xmb.phase = phase;
    xmb.list_size = list_size;
    xmb.index = index;
    xmb.ms = (now() - start_ns) / 1000000;
    xmb.native_bytes = stats.bytes[0];
    xmb.native_count = stats.count[0];
    // Per-entry progress replaces a single snapshot; only boundaries enter
    // the fixed ring. Nothing is written on the navigation path.
    if (phase != PS5_XMB_INSERT)
        xmb_history[xmb_events++ % 8] = xmb;
    pthread_mutex_unlock(&mutex);
    errno = saved;
}
extern "C" void ps5_memory_xmb_node(unsigned operation)
{
    using namespace ps5::memory;
    const int saved = errno;
    pthread_mutex_lock(&mutex);
    if (operation)
    {
        ++xmb.nodes;
        if (operation == 2)
            ++xmb.copied;
        else
            ++xmb.created;
    }
    else
    {
        ++xmb.freed;
        if (xmb.nodes)
            --xmb.nodes;
        else
            ++xmb.unmatched_frees;
    }
    pthread_mutex_unlock(&mutex);
    errno = saved;
}
#endif
