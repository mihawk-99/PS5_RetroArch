/* Optional host-allocation observation; no allocator policy changes. */
#pragma once
#include <cstddef>
#include <cstdint>

namespace ps5::memory
{
enum class Route : unsigned
{
    Native,
    Mapped,
    Aligned
};
struct Record
{
    void *pointer = nullptr;
    size_t bytes = 0;
    uintptr_t caller = 0;
    Route route = Route::Native;
};
struct Stats
{
    uint64_t bytes[3]{}, count[3]{}, peak[3]{};
    uint64_t failures = 0, dropped = 0, foreign_frees = 0, foreign_reallocs = 0;
};
#ifdef PS5_MEMORY_DIAGNOSTICS
void init(const char *path, const char *identity);
void finish();
void tick();
void event(const char *name, bool success = true);
void add(Record record);
Record take(void *pointer, bool resizing = false);
void failure(const char *operation, size_t bytes, size_t alignment, uintptr_t caller, int error);
Stats snapshot();
#else
inline void init(const char *, const char *)
{
}
inline void finish()
{
}
inline void tick()
{
}
inline void event(const char *, bool = true)
{
}
inline void add(Record)
{
}
inline Record take(void *, bool = false)
{
    return {};
}
inline void failure(const char *, size_t, size_t, uintptr_t, int)
{
}
#endif
} // namespace ps5::memory
