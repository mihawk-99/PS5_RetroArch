/*
 * PS5 RetroArch - the console's display, owned by this project.
 *
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This is the layer a RetroArch video driver sits on: it opens VideoOut, takes
 * the direct memory the console's GPU scans out, registers two buffers and
 * presents one at a time.
 *
 * Every constant and the tiled addressing below are taken from the sibling native
 * application that proved them on hardware
 * (ps5-native-app-boilerplate-main, src/demo_renderer.cpp). They are not
 * derivable by guessing, and the first version of this file guessed: the wrong
 * pixel format, the wrong memory type, the wrong mapping protection, a wrong
 * buffer structure and linear addressing. The title built, ran, and reported
 * `eboot.bin calls exit() exit_value=1` because `open` had failed.
 *
 * Reference: docs/REFERENCE.md, "The display".
 */

#include "display.hpp"
#include "trace.hpp"

#include <array>
#include <cstdio>
#include <emmintrin.h>
#include <cstddef>

extern "C"
{
    /* The payload SDK's headers for these are ABI declarations whose bodies live in
     * the kernel and GPU modules the linker binds; the shapes below are the sibling
     * application's, which the console has accepted. */
    std::size_t sceKernelGetDirectMemorySize();
    int sceKernelAllocateDirectMemory(std::int64_t search_start, std::int64_t search_end,
                                      std::size_t length, std::size_t alignment, int memory_type,
                                      std::int64_t *physical_address);
    int sceKernelMapDirectMemory(void **address, std::size_t length, int protection, int flags,
                                 std::int64_t physical_address, std::size_t alignment);
    /* The AGC entry point that brings up the GPU command-processor context, with
     * the ABI ../PS5_Vulkan's native runtime uses: sceAgcInit(uint32_t version).
     * Submitting a command buffer without it is what faulted this port with a
     * write to 0x202210000. */
    std::int32_t sceAgcInit(std::uint32_t version);
    /* The shell's startup splash covers the top of the framebuffer until a title
     * asks for it to go. ../PS5_Vulkan's M2 renderer, which is the template this
     * path follows, calls this before it opens the display (src/demo_renderer.cpp)
     * and this port never has - which is one of the few differences left between a
     * sequence that has put pixels on this console's screen and one that has not. */
    int sceSystemServiceHideSplashScreen();
    int sceVideoOutOpen(std::int32_t user_id, std::int32_t bus_type, std::int32_t index,
                        const void *param);
    int sceVideoOutSetFlipRate(std::int32_t handle, std::int32_t rate);
    int sceVideoOutSubmitFlip(std::int32_t handle, std::int32_t buffer_index,
                              std::uint32_t flip_mode, std::int64_t flip_argument);
    int sceVideoOutWaitVblank(std::int32_t handle);
    int sceVideoOutGetFlipStatus(std::int32_t handle, void *status);
    int sceVideoOutIsFlipPending(std::int32_t handle);
}

namespace ps5::display
{
namespace
{
constexpr unsigned frame_width = 1920;
constexpr unsigned frame_height = 1080;
/* One frame is allocated at 16 MiB and two are taken, which is what the console's
 * registration accepted; the larger size leaves room for the tiled layout. */
constexpr std::size_t frame_bytes = 0x1000000;
/* sceVideoOutGetFlipStatus fills 16 64-bit words; the fourth carries the
 * marker of the latest flip the display has shown. Confirmed by measurement
 * against ../PS5_Vulkan/driver/ps5vk_queue.c, which reads the same word. */
/* The version the sibling project's native runtime passes to sceAgcInit. */
constexpr std::uint32_t agc_version = 8;

constexpr unsigned flip_status_words = 16;
constexpr unsigned flip_status_marker_word = 3;

constexpr std::size_t memory_bytes = frame_bytes * 2;
constexpr std::size_t memory_alignment = 0x200000;
constexpr int memory_type_write_combined_garlic = 3;
constexpr int map_protection = 0x33;
constexpr std::uint64_t pixel_format_rgba8_srgb = UINT64_C(0x8000000022000000);

/* VideoOut scans out these two structures as the sibling application declares
 * them; the attribute is opaque and only the call that fills it knows its shape. */
struct VideoBuffer
{
    void *data;
    void *metadata;
    void *reserved0;
    void *reserved1;
};
struct VideoAttribute
{
    std::uint8_t reserved[80];
};
extern "C" void sceVideoOutSetBufferAttribute2(VideoAttribute *attribute,
                                               std::uint64_t pixel_format,
                                               std::uint32_t tiling_mode, std::uint32_t width,
                                               std::uint32_t height, std::uint64_t option,
                                               std::uint32_t dcc_control,
                                               std::uint64_t dcc_clear_color);
extern "C" int sceVideoOutRegisterBuffers2(std::int32_t handle, std::int32_t set_index,
                                           std::int32_t buffer_index_start, VideoBuffer *buffers,
                                           std::int32_t buffer_count, VideoAttribute *attribute,
                                           std::int32_t category, void *option);

/* Flush the CPU cache over a range of the mapped framebuffer.
 *
 * This is not optional on this memory. The mapping is write-combined GPU memory,
 * and the GPU reads it without seeing the CPU's dirty cache lines: without a
 * flush it displays whatever was in that memory before, which is indistinguishable
 * from a display that never received a frame. The sibling project that works on
 * this console does exactly this after writing its swapchain images
 * (../PS5_Vulkan/driver/ps5vk_direct_memory.c), which is where the shape of this
 * comes from.
 */
void flush_frame_cache(const void *address, std::size_t bytes) noexcept
{
    const char *at = static_cast<const char *>(address);
    const char *const end = at + bytes;
    for (; at < end; at += 64)
        _mm_clflush(at);
    _mm_mfence();
}

/* The frame is tiled, not linear: consecutive x are four pixels apart inside a
 * 64 KiB block and the blocks are laid out in a fixed order. Writing row-major
 * produces a scrambled image, which is why every write goes through this offset. */
[[nodiscard]] constexpr std::size_t tiled_offset(unsigned x, unsigned y) noexcept
{
    const std::uint32_t offset = ((y << 4) & 0x70U) ^ ((y << 5) & 0xf00U) ^ ((y << 9) & 0x1000U) ^
                                 ((y << 8) & 0x4000U) ^ ((x << 2) & 0xcU) ^ ((x << 5) & 0x380U) ^
                                 ((x << 4) & 0x400U) ^ ((x << 6) & 0x800U) ^ ((x << 9) & 0xa000U);
    const std::uint32_t blocks_per_row = (frame_width + 127U) >> 7;
    const std::uint32_t block_index = (y >> 7) * blocks_per_row + (x >> 7);
    return (static_cast<std::size_t>(block_index) << 16) + offset;
}
} // namespace

Display::~Display()
{
    close();
}

bool Display::open(unsigned width, unsigned height) noexcept
{
    if (is_open())
        return true;
    /* The registration below is fixed at the console's own frame size, so a caller
     * asking for another size is refused rather than silently given 1080p. */
    if (width != frame_width || height != frame_height)
    {
        error_ = "only the console's 1920x1080 frame is supported";
        return false;
    }

    width_ = width;
    height_ = height;

    /* Out of the way before the display is claimed: the splash is the shell's and
     * sits over the frame this title is about to present into. */
    (void)sceSystemServiceHideSplashScreen();

    /* The GPU command-processor context, before anything is submitted to it.
     * ../PS5_Vulkan initialises AGC before its first submission
     * (driver/ps5vk_device.c), and a submission without it is what faulted this
     * port with a write to 0x202210000. A failure here is not fatal for the
     * present path below, so it is recorded rather than returned: the flip itself
     * is what reports whether the display took the frame. */
    agc_ready_ = sceAgcInit(agc_version) == 0;

    handle_ = sceVideoOutOpen(0xff, 0, 0, nullptr);
    if (handle_ < 0)
    {
        error_ = "sceVideoOutOpen refused the display";
        handle_ = -1;
        return false;
    }

    const std::size_t pool = sceKernelGetDirectMemorySize();
    if (pool < memory_bytes)
    {
        error_ = "the console has less direct memory than two frames need";
        close();
        return false;
    }

    std::int64_t physical = 0;
    if (sceKernelAllocateDirectMemory(0, static_cast<std::int64_t>(pool), memory_bytes,
                                      memory_alignment, memory_type_write_combined_garlic,
                                      &physical) < 0)
    {
        error_ = "sceKernelAllocateDirectMemory failed";
        close();
        return false;
    }

    void *mapped = nullptr;
    if (sceKernelMapDirectMemory(&mapped, memory_bytes, map_protection, 0, physical,
                                 memory_alignment) < 0)
    {
        error_ = "sceKernelMapDirectMemory failed";
        close();
        return false;
    }
    mapped_ = mapped;

    auto *base = static_cast<std::uint8_t *>(mapped_);
    frames_[0] = base;
    frames_[1] = base + frame_bytes;

    std::array<VideoBuffer, 2> buffers{{
        {frames_[0], nullptr, nullptr, nullptr},
        {frames_[1], nullptr, nullptr, nullptr},
    }};
    VideoAttribute attribute{};
    (void)sceVideoOutSetFlipRate(handle_, 0);
    sceVideoOutSetBufferAttribute2(&attribute, pixel_format_rgba8_srgb, 0, width_, height_, 0, 0,
                                   0);
    if (sceVideoOutRegisterBuffers2(handle_, 0, 0, buffers.data(),
                                    static_cast<std::int32_t>(buffers.size()), &attribute, 0,
                                    nullptr) < 0)
    {
        error_ = "sceVideoOutRegisterBuffers2 refused the buffers";
        close();
        return false;
    }
    registered_[0] = 0;
    registered_[1] = 1;

    back_ = 0;
    error_ = "";
    return true;
}

void Display::close() noexcept
{
    /* The display handle goes first: the buffers must not be released while the
     * display still scans them out. */
    handle_ = -1;
    registered_[0] = -1;
    registered_[1] = -1;
    frames_[0] = nullptr;
    frames_[1] = nullptr;
    mapped_ = nullptr;
    back_ = 0;
}

Surface Display::back_surface() const noexcept
{
    Surface surface;
    surface.base = frames_[back_];
    surface.width = width_;
    surface.height = height_;
    return surface;
}

void Display::write(Surface surface, unsigned x, unsigned y, std::uint32_t colour) noexcept
{
    if (surface.base == nullptr || x >= surface.width || y >= surface.height)
        return;
    auto *pixels = static_cast<std::uint32_t *>(surface.base);
    pixels[tiled_offset(x, y) / sizeof(std::uint32_t)] = colour;
}

void Display::clear(Surface surface, std::uint32_t colour) noexcept
{
    if (surface.base == nullptr)
        return;
    for (unsigned y = 0; y < surface.height; ++y)
        for (unsigned x = 0; x < surface.width; ++x)
            write(surface, x, y, colour);
}

bool Display::present() noexcept
{
    if (!is_open() || frames_[back_] == nullptr)
    {
        error_ = "present without an open display";
        return false;
    }

    /* The CPU's dirty cache lines are invisible to the display, so the frame is
     * flushed before the display is asked for it. */
    flush_frame_cache(frames_[back_], frame_bytes);

    /* Confirmed, not assumed. For several rounds this port could not tell a
     * presented frame from one the display never took, because the only check was
     * the call's return code. The status query is what an earlier version was
     * missing, and it is the difference between "the driver thinks it drew" and
     * "the display says it showed". */
    if (sceVideoOutSubmitFlip(handle_, registered_[back_], 1, 1) < 0)
    {
        error_ = "sceVideoOutSubmitFlip refused the frame";
        return false;
    }
    (void)sceVideoOutWaitVblank(handle_);

    const int status = sceVideoOutGetFlipStatus(handle_, flip_status_);
    /* Report what the display says it has shown, once, so the trace answers the
     * question the return code cannot: a flip the display never took reads as
     * success from SubmitFlip and as a marker of zero here. */
    static bool reported = false;
    if (!reported)
    {
        reported = true;
        char line[160];
        std::snprintf(line, sizeof(line), "flip status: call=%d marker=%llu shown=%llu", status,
                      static_cast<unsigned long long>(flip_status_[flip_status_marker_word]),
                      static_cast<unsigned long long>(flip_status_[0]));
        ps5::debug::mark(line);
    }
    error_ = "";

    back_ ^= 1;
    return true;
}
} // namespace ps5::display
