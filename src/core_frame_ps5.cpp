/* Convert libretro's little-endian XRGB8888 to the sampled RGBA8 upload format. */
#include <cstddef>
#include <cstdint>

extern "C" void ps5_core_frame_rgba(void *output, size_t stride, const void *input, size_t pitch,
                                    unsigned width, unsigned height)
{
    auto *dst = static_cast<uint8_t *>(output);
    auto *src = static_cast<const uint8_t *>(input);
    for (unsigned y = 0; y < height; ++y, dst += stride, src += pitch)
        for (unsigned x = 0; x < width; ++x)
        {
            const uint8_t blue = src[4 * x], green = src[4 * x + 1], red = src[4 * x + 2];
            dst[4 * x] = red;
            dst[4 * x + 1] = green;
            dst[4 * x + 2] = blue;
            dst[4 * x + 3] = 255;
        }
}
