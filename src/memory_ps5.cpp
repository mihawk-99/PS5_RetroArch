/* Large application buffers must not exhaust SceLibcInternal's private heap.
 * Wrap only title/core references; native library allocations stay libc-owned.
 * A locked list identifies mappings without reading before foreign pointers. */
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <pthread.h>
#include <sys/mman.h>

extern "C"
{
    void *__real_malloc(size_t);
    void *__real_calloc(size_t, size_t);
    void *__real_realloc(void *, size_t);
    void __real_free(void *);
    void *__wrap_malloc(size_t);
    void __wrap_free(void *);
}

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

Mapping **find(void *pointer)
{
    Mapping **entry = &mappings;
    while (*entry && static_cast<void *>(*entry + 1) != pointer)
        entry = &(*entry)->next;
    return entry;
}
} // namespace

extern "C" void *__wrap_malloc(size_t size)
{
    if (size < threshold)
        return __real_malloc(size ? size : 1);
    if (size > SIZE_MAX - sizeof(Mapping) - (page - 1))
    {
        errno = ENOMEM;
        return nullptr;
    }
    const size_t span = (sizeof(Mapping) + size + page - 1) & ~(page - 1);
    void *memory = mmap(nullptr, span, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (memory == MAP_FAILED)
        return nullptr;
    auto *entry = static_cast<Mapping *>(memory);
    entry->requested = size;
    entry->span = span;
    pthread_mutex_lock(&lock);
    entry->next = mappings;
    mappings = entry;
    pthread_mutex_unlock(&lock);
    return entry + 1;
}

extern "C" void __wrap_free(void *pointer)
{
    if (!pointer)
        return;
    pthread_mutex_lock(&lock);
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

extern "C" void *__wrap_calloc(size_t count, size_t size)
{
    if (size && count > SIZE_MAX / size)
    {
        errno = ENOMEM;
        return nullptr;
    }
    const size_t bytes = count * size;
    if (bytes < threshold)
        return bytes ? __real_calloc(count, size) : __real_calloc(1, 1);
    /* Anonymous mappings are zero-initialized by the kernel. */
    return __wrap_malloc(bytes);
}

extern "C" void *__wrap_realloc(void *pointer, size_t size)
{
    if (!pointer)
        return __wrap_malloc(size);
    if (!size)
    {
        __wrap_free(pointer);
        return nullptr;
    }
    pthread_mutex_lock(&lock);
    Mapping *entry = *find(pointer);
    const size_t old_size = entry ? entry->requested : 0;
    pthread_mutex_unlock(&lock);
    /* Native buffers retain their allocator: their usable size is not part of
     * this ABI. Never guess it or read a private libc allocation header. */
    if (!entry)
        return __real_realloc(pointer, size);
    void *replacement = __wrap_malloc(size);
    if (!replacement)
        return nullptr;
    std::memcpy(replacement, pointer, old_size < size ? old_size : size);
    __wrap_free(pointer);
    return replacement;
}
