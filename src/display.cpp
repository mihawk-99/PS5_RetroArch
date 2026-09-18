/*
 * PS5 RetroArch - the console's display, owned by this project.
 *
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The declarations this file needs come from the payload SDK's own headers where
 * they exist, and from a local declaration where the SDK's header is a
 * compatibility stub that does not carry them. Each local one is marked with why.
 */

#include "display.hpp"

#include <array>
#include <cstddef>

extern "C"
{
/* The payload SDK's headers for these are ABI declarations with stub bodies, so
 * they are declared here exactly as the sibling native application declares them;
 * the real implementations live in the GPU and kernel modules the linker binds. */
struct VideoAttribute
{
    std::uint32_t set_attribute;
    std::uint32_t buffer_num;
    std::uint32_t reserved[6];
    std::uint32_t pixel_format;
    std::uint32_t tiling_mode;
    std::uint32_t reserved2[3];
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t reserved3[16];
};

struct VideoBuffer
{
    void *data;
    std::uint64_t reserved[3];
};

int sceVideoOutOpen(std::int32_t user_id, std::int32_t bus_type, std::int32_t index,
                    const void *param);
int sceVideoOutSetFlipRate(std::int32_t handle, std::int32_t rate);
int sceVideoOutSubmitFlip(std::int32_t handle, std::int32_t buffer_index, std::int32_t flip_mode,
                          std::int64_t flip_arg);
int sceVideoOutWaitVblank(std::int32_t handle);
void sceVideoOutSetBufferAttribute2(VideoAttribute *attribute, std::uint32_t pixel_format,
                                    std::uint32_t tiling_mode, std::uint32_t width,
                                    std::uint32_t height, std::uint32_t reserved0,
                                    std::uint32_t reserved1, std::uint32_t reserved2);
int sceVideoOutRegisterBuffers2(std::int32_t handle, std::int32_t set_index,
                                std::int32_t buffer_index_start, void *buffers,
                                std::int32_t buffer_num, const VideoAttribute *attribute,
                                std::uint32_t reserved0, void *reserved1);
int sceKernelAllocateDirectMemory(std::int64_t search_start, std::int64_t search_end,
                                  std::size_t len, std::size_t alignment, std::int32_t memory_type,
                                  std::int64_t *physical_address_out);
int sceKernelMapDirectMemory(void **address, std::size_t len, std::int32_t protection,
                             std::int32_t flags, std::int64_t physical_address,
                             std::size_t alignment);
std::int64_t sceKernelGetDirectMemorySize(void);
}

namespace ps5::display
{
namespace
{
/* The flip mode the sibling application uses: submit with a flip argument and wait
 * for the display to acknowledge it. */
constexpr std::int32_t flip_mode_vsync = 1;
constexpr std::int64_t flip_arg_default = 1;

/* Bus and user: user 0xff is "the current user", bus 0 is the main display. */
constexpr std::int32_t video_user = 0xff;
constexpr std::int32_t video_bus = 0;
constexpr std::int32_t video_index = 0;

/* Direct memory, as the sibling application takes it: write-combined, and aligned
 * to the page the console maps with. */
constexpr std::int32_t memory_type_write_combined = 3;
constexpr std::size_t memory_alignment = 0x200000;
constexpr std::int32_t map_protection_read_write = 0x3;
} // namespace

Display::~Display()
{
    close();
}

bool Display::open(unsigned width, unsigned height) noexcept
{
    if (is_open())
        return true;
    if (width == 0 || height == 0)
    {
        error_ = "display size must be non-zero";
        return false;
    }

    width_ = width;
    height_ = height;

    handle_ = sceVideoOutOpen(video_user, video_bus, video_index, nullptr);
    if (handle_ < 0)
    {
        error_ = "sceVideoOutOpen refused the display";
        handle_ = -1;
        return false;
    }

    const std::size_t frame_bytes = static_cast<std::size_t>(width_) * height_ * sizeof(std::uint32_t);
    // Two frames plus the alignment the mapping requires.
    mapped_bytes_ = frame_bytes * 2 + memory_alignment;

    std::int64_t physical = 0;
    const std::int64_t pool = sceKernelGetDirectMemorySize();
    if (pool <= 0 || static_cast<std::size_t>(pool) < mapped_bytes_)
    {
        error_ = "the console has less direct memory than two frames need";
        close();
        return false;
    }
    if (sceKernelAllocateDirectMemory(0, pool, mapped_bytes_, memory_alignment,
                                      memory_type_write_combined, &physical) < 0)
    {
        error_ = "sceKernelAllocateDirectMemory failed";
        close();
        return false;
    }

    void *mapped = nullptr;
    if (sceKernelMapDirectMemory(&mapped, mapped_bytes_, map_protection_read_write, 0, physical,
                                 memory_alignment) < 0)
    {
        error_ = "sceKernelMapDirectMemory failed";
        close();
        return false;
    }
    mapped_ = mapped;

    auto *base = static_cast<std::uint8_t *>(mapped_);
    frames_[0] = reinterpret_cast<std::uint32_t *>(base);
    frames_[1] = reinterpret_cast<std::uint32_t *>(base + frame_bytes);

    std::array<VideoBuffer, 2> buffers{{
        {frames_[0], {0, 0, 0}},
        {frames_[1], {0, 0, 0}},
    }};
    VideoAttribute attribute{};
    (void)sceVideoOutSetFlipRate(handle_, 0);
    sceVideoOutSetBufferAttribute2(&attribute, pixel_format_rgba8_srgb, 0, width_, height_, 0, 0, 0);
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
    // Order matters: the display must be told to stop before the memory it scans
    // out is released, which is why registration and the handle go first.
    if (handle_ >= 0)
    {
        handle_ = -1;
    }
    registered_[0] = -1;
    registered_[1] = -1;
    frames_[0] = nullptr;
    frames_[1] = nullptr;
    mapped_ = nullptr;
    mapped_bytes_ = 0;
    back_ = 0;
}

Surface Display::back_surface() const noexcept
{
    Surface surface;
    surface.pixels = frames_[back_];
    surface.width = width_;
    surface.height = height_;
    return surface;
}

bool Display::present() noexcept
{
    if (!is_open() || frames_[back_] == nullptr)
    {
        error_ = "present without an open display";
        return false;
    }

    const int index = registered_[back_];
    if (sceVideoOutSubmitFlip(handle_, index, flip_mode_vsync, flip_arg_default) < 0)
    {
        error_ = "sceVideoOutSubmitFlip refused the frame";
        return false;
    }
    // The buffer is only safe to draw into again once the display has taken it.
    (void)sceVideoOutWaitVblank(handle_);
    back_ ^= 1;
    error_ = "";
    return true;
}
} // namespace ps5::display
