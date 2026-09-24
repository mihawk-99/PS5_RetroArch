#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

extern "C"
{
    void *__wrap_malloc(size_t);
    void *__wrap_calloc(size_t, size_t);
    void *__wrap_realloc(void *, size_t);
    void __wrap_free(void *);
    // Host build binds these to libc, preserving genuinely foreign pointers.
    void *__real_malloc(size_t n)
    {
        return std::malloc(n);
    }
    void *__real_calloc(size_t n, size_t s)
    {
        return std::calloc(n, s);
    }
    // A libc heap that can be told to refuse growth, for the overflow move.
    bool refuse_native_realloc = false;
    void *__real_realloc(void *p, size_t n)
    {
        return refuse_native_realloc ? nullptr : std::realloc(p, n);
    }
    // A stand-in overflow heap: libc-backed, tagged so ownership is testable.
    std::vector<void *> overflow_blocks;
    int ps5_overflow_owns(const void *p)
    {
        for (void *block : overflow_blocks)
            if (block == p)
                return 1;
        return 0;
    }
    void *ps5_overflow_malloc(size_t n)
    {
        void *p = std::malloc(n);
        if (p)
            overflow_blocks.push_back(p);
        return p;
    }
    void *ps5_overflow_calloc(size_t c, size_t n)
    {
        void *p = std::calloc(c, n);
        if (p)
            overflow_blocks.push_back(p);
        return p;
    }
    void *ps5_overflow_realloc(void *, size_t)
    {
        return nullptr;
    }
    void *ps5_overflow_memalign(size_t, size_t)
    {
        return nullptr;
    }
    void ps5_overflow_free(void *p)
    {
        for (auto &block : overflow_blocks)
            if (block == p)
                block = nullptr;
        std::free(p);
    }
    void __real_free(void *p)
    {
        std::free(p);
    }
}
int main()
{
    constexpr size_t large = 16 * 1024 * 1024;
    auto *p = static_cast<unsigned char *>(__wrap_malloc(large));
    assert(p && uintptr_t(p) % alignof(std::max_align_t) == 0);
    std::memset(p, 0xA5, large);
    assert(!__wrap_realloc(p, SIZE_MAX));
    assert(p[0] == 0xA5 && p[large - 1] == 0xA5);
    p = static_cast<unsigned char *>(__wrap_realloc(p, large * 2));
    assert(p && p[0] == 0xA5 && p[large - 1] == 0xA5);
    p = static_cast<unsigned char *>(__wrap_realloc(p, 13));
    assert(p && p[12] == 0xA5);
    __wrap_free(p);
    p = static_cast<unsigned char *>(__wrap_calloc(2, large));
    assert(p);
    for (size_t i = 0; i < 2 * large; ++i)
        assert(p[i] == 0);
    __wrap_free(p);
    assert(!__wrap_calloc(SIZE_MAX, 2) && errno == ENOMEM);
    assert(!__wrap_malloc(SIZE_MAX) && errno == ENOMEM);
    // strdup/native library buffers must still be freed by libc.
    char *foreign = strdup("native allocation");
    foreign = static_cast<char *>(__wrap_realloc(foreign, 128));
    assert(foreign && !strcmp(foreign, "native allocation"));
    __wrap_free(foreign);
    __wrap_free(nullptr);
    // A native block whose heap refuses to grow moves to the overflow heap with
    // its contents, and is freed there.
    // (A libc-owned block, as strdup or a system library hands the title.)
    auto *native = static_cast<unsigned char *>(std::malloc(100));
    std::memset(native, 0x5A, 100);
    refuse_native_realloc = true;
    auto *moved = static_cast<unsigned char *>(__wrap_realloc(native, 4096));
    refuse_native_realloc = false;
    assert(moved && ps5_overflow_owns(moved));
    for (size_t i = 0; i < 100; ++i)
        assert(moved[i] == 0x5A);
    __wrap_free(moved);
    assert(!ps5_overflow_owns(moved));
    void *empty = __wrap_calloc(0, SIZE_MAX);
    assert(empty);
    __wrap_free(empty);
    assert(!__wrap_realloc(__wrap_malloc(large), 0));
    std::vector<std::thread> workers;
    for (int t = 0; t < 4; ++t)
        workers.emplace_back(
            []
            {
                for (int i = 0; i < 50; ++i)
                {
                    void *buffer = __wrap_malloc(1024 * 1024);
                    assert(buffer);
                    memset(buffer, i, 1024 * 1024);
                    __wrap_free(buffer);
                }
            });
    for (auto &worker : workers)
        worker.join();
}
