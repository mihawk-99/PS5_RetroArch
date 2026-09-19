/* Numeric-only XMB observation. No pointers or user strings survive a hook. */
#pragma once
#include <stddef.h>

enum ps5_xmb_phase
{
    PS5_XMB_CACHE_BEGIN = 1,
    PS5_XMB_CACHE_END,
    PS5_XMB_COPY_BEGIN,
    PS5_XMB_COPY_END,
    PS5_XMB_CLEAR_BEGIN,
    PS5_XMB_CLEAR_END,
    PS5_XMB_INSERT,
    PS5_XMB_POPULATE
};
#ifdef PS5_MEMORY_DIAGNOSTICS
#ifdef __cplusplus
extern "C"
{
#endif
    void ps5_memory_xmb_context(unsigned tab, unsigned kind, size_t current, size_t old,
                                size_t horizontal);
    void ps5_memory_xmb_stage(unsigned phase, size_t list_size, size_t index);
    /* 1 = allocate, 2 = copy, 0 = free; called only after success/before free. */
    void ps5_memory_xmb_node(unsigned operation);
#ifdef __cplusplus
}
#endif
#else
#define ps5_memory_xmb_context(...) ((void)0)
#define ps5_memory_xmb_stage(...) ((void)0)
#define ps5_memory_xmb_node(...) ((void)0)
#endif
