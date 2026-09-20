/* Small XMB nodes and menu callbacks live in reclaimable mapped slabs.
 * Release with ordinary free(), which the native title routes by ownership. */
#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C"
{
#endif
    /* Sizes above 1024 are rejected; callers must check for NULL. */
    void *ps5_menu_malloc(size_t size);
#ifdef __cplusplus
}
#endif
