/* mGBA's native 32-bit software pixels are XBGR; libretro promises XRGB. */
#ifndef PS5_MGBA_VIDEO_H
#define PS5_MGBA_VIDEO_H
#include <stdint.h>
static inline uint32_t ps5_mgba_xrgb(uint32_t pixel)
{
    return (pixel & UINT32_C(0xff00ff00)) | ((pixel & UINT32_C(0xff)) << 16) |
           ((pixel >> 16) & UINT32_C(0xff));
}
#endif
