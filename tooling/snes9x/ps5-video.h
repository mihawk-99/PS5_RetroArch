/* Preserve Snes9x's RGB565 renderer; libretro receives independent XRGB8888. */
#ifndef PS5_SNES9X_VIDEO_H
#define PS5_SNES9X_VIDEO_H
#include <stddef.h>
#include <stdint.h>

static inline uint32_t ps5_snes9x_xrgb(uint16_t pixel)
{
    const uint32_t r = pixel >> 11;
    const uint32_t g = (pixel >> 5) & 63u;
    const uint32_t b = pixel & 31u;
    return (((r << 3) | (r >> 2)) << 16) | (((g << 2) | (g >> 4)) << 8) |
           (b << 3) | (b >> 2);
}

static inline bool ps5_snes9x_frame(uint32_t *output, size_t capacity,
                                   const uint16_t *input, size_t pitch,
                                   unsigned width, unsigned height)
{
    if (!width || !height || width > capacity / height || pitch % sizeof(uint16_t) ||
        pitch / sizeof(uint16_t) < width)
        return false;
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x)
            output[(size_t)y * width + x] = ps5_snes9x_xrgb(input[(size_t)y * (pitch / 2) + x]);
    return true;
}
#endif
