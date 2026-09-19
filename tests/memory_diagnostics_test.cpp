#include "src/memory_diagnostics.hpp"
#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>
#include <time.h>

static uint64_t clock_ns = 1;
extern "C" int __wrap_clock_gettime(clockid_t, timespec *t)
{
    t->tv_sec = clock_ns / 1000000000ULL;
    t->tv_nsec = clock_ns % 1000000000ULL;
    return 0;
}
static bool fail_malloc = false, fail_realloc = false;
extern "C"
{
    void *__wrap_malloc(size_t);
    void *__wrap_calloc(size_t, size_t);
    void *__wrap_realloc(void *, size_t);
    void __wrap_free(void *);
    int __wrap_posix_memalign(void **, size_t, size_t);
    void *__real_malloc(size_t n)
    {
        if (fail_malloc)
        {
            errno = ENOMEM;
            return nullptr;
        }
        return std::malloc(n);
    }
    void *__real_calloc(size_t n, size_t s)
    {
        return std::calloc(n, s);
    }
    void *__real_realloc(void *p, size_t n)
    {
        if (fail_realloc)
        {
            errno = ENOMEM;
            return nullptr;
        }
        return std::realloc(p, n);
    }
    void __real_free(void *p)
    {
        std::free(p);
    }
    int __real_posix_memalign(void **p, size_t a, size_t n)
    {
        return posix_memalign(p, a, n);
    }
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    using namespace ps5::memory;
    init(argv[1], "host-test");
    auto *p = static_cast<char *>(__wrap_malloc(123));
    assert(p);
    std::memset(p, 37, 123);
    assert(snapshot().bytes[0] == 123 && snapshot().count[0] == 1);
    tick(); // Before the five-second boundary, no sample is emitted.
    clock_ns += 5000000000ULL;
    tick();
    tick(); // Same instant must not emit a duplicate.
    fail_realloc = true;
    assert(!__wrap_realloc(p, 456) && errno == ENOMEM);
    assert(p[122] == 37 && snapshot().bytes[0] == 123);
    fail_realloc = false;
    p = static_cast<char *>(__wrap_realloc(p, 2 * 1024 * 1024));
    assert(p && p[122] == 37);
    assert(snapshot().bytes[0] == 2 * 1024 * 1024 && !snapshot().bytes[1]);
    __wrap_free(p);
    void *mapped = __wrap_malloc(2 * 1024 * 1024);
    assert(mapped && snapshot().bytes[1] == 2 * 1024 * 1024);
    mapped = __wrap_realloc(mapped, 47);
    assert(mapped && snapshot().bytes[0] == 47 && !snapshot().bytes[1]);
    __wrap_free(mapped);
    void *aligned = nullptr;
    errno = EDOM;
    assert(!__wrap_posix_memalign(&aligned, 256, 321) && errno == EDOM);
    assert(uintptr_t(aligned) % 256 == 0 && snapshot().bytes[2] == 321);
    __wrap_free(aligned);
    aligned = reinterpret_cast<void *>(uintptr_t(99));
    assert(__wrap_posix_memalign(&aligned, 3, 321) == EINVAL && errno == EDOM);
    assert(aligned == reinterpret_cast<void *>(uintptr_t(99)));
    fail_malloc = true;
    assert(!__wrap_malloc(66) && errno == ENOMEM);
    // The OOM logger must work while wrapped allocation fails.
    failure("forced", 77, 0, 0x1234, ENOMEM);
    fail_malloc = false;
    assert(!__wrap_calloc(SIZE_MAX, 2));
    void *foreign = std::malloc(5);
    foreign = __wrap_realloc(foreign, 9);
    __wrap_free(foreign);
    __wrap_free(strdup("foreign"));
    std::vector<std::thread> workers;
    for (int t = 0; t < 4; ++t)
        workers.emplace_back(
            []
            {
                for (int i = 0; i < 2000; ++i)
                {
                    void *q = __wrap_calloc(1, 59);
                    assert(q);
                    q = __wrap_realloc(q, 101);
                    assert(q);
                    __wrap_free(q);
                }
            });
    for (auto &worker : workers)
        worker.join();
    // Saturation is explicitly reported; it never changes allocation results.
    for (uintptr_t i = 1; i <= 131073; ++i)
        add({reinterpret_cast<void *>(i * 16), 1, 0x1000, Route::Native});
    assert(snapshot().dropped == 1);
    for (uintptr_t i = 1; i <= 131073; ++i)
        take(reinterpret_cast<void *>(i * 16));
    event("image_create");
    event("image_destroy");
    event("idle_begin");
    event("idle_end", false);
    const auto stats = snapshot();
    assert(!stats.count[0] && !stats.count[1] && !stats.count[2]);
    assert(!stats.bytes[0] && !stats.bytes[1] && !stats.bytes[2]);
    assert(stats.failures == 5 && stats.foreign_reallocs == 1 && stats.foreign_frees == 2);
    finish();
}
