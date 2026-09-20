#include "src/menu_memory.h"
#include <atomic>
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <thread>
#include <vector>

static std::atomic<size_t> mappings{0}, peak{0}, native_calls{0};
static std::atomic<bool> fail_mapping{false};
extern "C"
{
    void *__real_mmap(void *, size_t, int, int, int, off_t);
    int __real_munmap(void *, size_t);
    void *__wrap_mmap(void *p, size_t n, int prot, int flags, int fd, off_t offset)
    {
        if (fail_mapping)
        {
            errno = ENOMEM;
            return MAP_FAILED;
        }
        void *result = __real_mmap(p, n, prot, flags, fd, offset);
        if (result != MAP_FAILED)
        {
            size_t count = ++mappings;
            size_t old = peak;
            while (old < count && !peak.compare_exchange_weak(old, count))
            {
            }
        }
        return result;
    }
    int __wrap_munmap(void *p, size_t n)
    {
        int result = __real_munmap(p, n);
        assert(result == 0);
        --mappings;
        return result;
    }
    void *__real_malloc(size_t n)
    {
        ++native_calls;
        return std::malloc(n);
    }
    void *__real_calloc(size_t n, size_t s)
    {
        ++native_calls;
        return std::calloc(n, s);
    }
    void *__real_realloc(void *p, size_t n)
    {
        ++native_calls;
        return std::realloc(p, n);
    }
    void __real_free(void *p)
    {
        std::free(p);
    }
    void __wrap_free(void *);
    void *__wrap_realloc(void *, size_t);
}
int main()
{
    assert(!ps5_menu_malloc(1025) && errno == ENOMEM);
    fail_mapping = true;
    assert(!ps5_menu_malloc(96));
    assert(mappings == 0);
    fail_mapping = false;
    for (int cycle = 0; cycle < 4; ++cycle)
    {
        std::vector<void *> pointers;
        for (size_t i = 0; i < 7384; ++i)
            for (size_t size : {size_t(96), size_t(648)})
            {
                void *p = ps5_menu_malloc(size);
                assert(p && uintptr_t(p) % alignof(std::max_align_t) == 0);
                memset(p, int(i % 256), size);
                pointers.push_back(p);
            }
        assert(native_calls == 0 && mappings < 150);
        // Non-LIFO release and slot reuse must not overwrite live neighbours.
        for (size_t i = 0; i < pointers.size(); i += 2)
            __wrap_free(pointers[i]);
        for (size_t i = 1; i < pointers.size(); i += 2)
        {
            auto *p = static_cast<unsigned char *>(pointers[i]);
            assert(p[0] == (i / 2) % 256 && p[647] == (i / 2) % 256);
            __wrap_free(p);
        }
        assert(mappings == 0);
    }
    auto *p = static_cast<unsigned char *>(ps5_menu_malloc(96));
    memset(p, 0x5a, 96);
    fail_mapping = true;
    assert(!__wrap_realloc(p, 648)); // New size class requires another slab.
    assert(p[95] == 0x5a && mappings == 1);
    fail_mapping = false;
    p = static_cast<unsigned char *>(__wrap_realloc(p, 648));
    assert(p && p[95] == 0x5a && mappings == 1);
    p = static_cast<unsigned char *>(__wrap_realloc(p, 2 * 1024 * 1024));
    assert(p && p[95] == 0x5a && mappings == 1);
    assert(!__wrap_realloc(p, 0) && mappings == 0);
    void *foreign = std::malloc(32);
    foreign = __wrap_realloc(foreign, 64);
    __wrap_free(foreign);
    std::vector<std::thread> workers;
    for (int t = 0; t < 4; ++t)
        workers.emplace_back(
            []
            {
                for (int j = 0; j < 1000; ++j)
                {
                    void *a = ps5_menu_malloc(96), *b = ps5_menu_malloc(648);
                    assert(a && b);
                    memset(a, 1, 96);
                    memset(b, 2, 648);
                    __wrap_free(a);
                    __wrap_free(b);
                }
            });
    for (auto &worker : workers)
        worker.join();
    assert(mappings == 0);
}
