/*
 * PS5 RetroArch - the console's display, owned by this project.
 *
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This is the layer RetroArch's video driver sits on: it opens VideoOut, takes the
 * direct memory the console's GPU can actually scan out, registers two buffers and
 * presents one at a time. Nothing here is RetroArch-specific, so it can be tested
 * on its own.
 *
 * The sequence is the one the sibling native application established on hardware
 * (ps5-native-app-boilerplate-main, src/demo_renderer.cpp): open, allocate direct
 * memory, map it, register the buffers, then flip. Two details are load-bearing
 * and were learned there rather than here:
 *
 *   - the frames must live in direct memory: a buffer from the ordinary heap is
 *     not what VideoOut scans out;
 *   - the submission is completed by WaitVblank before the buffer is drawn into
 *     again, so a buffer is never handed back while the display still reads it.
 *
 * Reference: docs/REFERENCE.md, "The display".
 */

#pragma once

#include <cstdint>
#include <span>

namespace ps5::display
{
/** The pixel layout VideoOut is configured for: 8-bit RGBA, sRGB order. */
constexpr std::uint32_t pixel_format_rgba8_srgb = 0x80000000u | 0x0000000au;

/** What a caller draws into: one 32-bit pixel per element, row-major. */
struct Surface
{
    std::uint32_t *pixels = nullptr;
    unsigned width = 0;
    unsigned height = 0;

    /** Bytes from the start of one row to the next. */
    [[nodiscard]] std::size_t stride_bytes() const noexcept
    {
        return static_cast<std::size_t>(width) * sizeof(std::uint32_t);
    }

    [[nodiscard]] std::size_t size_bytes() const noexcept
    {
        return stride_bytes() * height;
    }
};

/**
 * Opens the display and prepares two buffers.
 *
 * Returns false and leaves the object closed if any step fails; `open` is then
 * safe to call again and `close` is safe to call on a closed display.
 */
class Display final
{
  public:
    Display() = default;
    ~Display();
    Display(const Display &) = delete;
    Display &operator=(const Display &) = delete;

    bool open(unsigned width, unsigned height) noexcept;
    void close() noexcept;

    [[nodiscard]] bool is_open() const noexcept { return handle_ >= 0; }
    [[nodiscard]] unsigned width() const noexcept { return width_; }
    [[nodiscard]] unsigned height() const noexcept { return height_; }

    /** The buffer the caller may draw into now. */
    [[nodiscard]] Surface back_surface() const noexcept;

    /**
     * Submits the back buffer and waits for the display to take it.
     *
     * Returns false if the submission was refused, in which case the buffers have
     * not been swapped and the caller may try again.
     */
    bool present() noexcept;

    /** The last failure, for a log line; never null. */
    [[nodiscard]] const char *last_error() const noexcept { return error_; }

  private:
    int handle_ = -1;
    void *mapped_ = nullptr;
    std::size_t mapped_bytes_ = 0;
    std::uint32_t *frames_[2] = {nullptr, nullptr};
    int registered_[2] = {-1, -1};
    int back_ = 0;
    unsigned width_ = 0;
    unsigned height_ = 0;
    const char *error_ = "display not opened";
};
} // namespace ps5::display
