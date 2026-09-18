/*
 * PS5 RetroArch - the console's display, owned by this project.
 *
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The layer a RetroArch video driver sits on: VideoOut, the direct memory the GPU
 * scans out, two registered buffers, present-and-wait. Nothing here is
 * RetroArch-specific, so it can be tested on its own.
 *
 * The frame is tiled. A caller does not write pixels directly: it uses `write` and
 * `clear`, which address the tiled layout. That keeps the one thing that is easy
 * to get wrong in one place.
 *
 * Reference: docs/REFERENCE.md, "The display".
 */

#pragma once

#include <cstdint>
#include <cstddef>

namespace ps5::display
{
/** What a caller draws through: an opaque frame and its size. */
struct Surface
{
    void *base = nullptr;
    unsigned width = 0;
    unsigned height = 0;
};

class Display final
{
  public:
    Display() = default;
    ~Display();
    Display(const Display &) = delete;
    Display &operator=(const Display &) = delete;

    /** Opens the display and prepares two buffers at the console's frame size. */
    bool open(unsigned width, unsigned height) noexcept;
    void close() noexcept;

    [[nodiscard]] bool is_open() const noexcept
    {
        return handle_ >= 0;
    }
    [[nodiscard]] unsigned width() const noexcept
    {
        return width_;
    }
    [[nodiscard]] unsigned height() const noexcept
    {
        return height_;
    }

    /** The buffer the caller may write into now. */
    [[nodiscard]] Surface back_surface() const noexcept;

    /** One pixel, through the tiled layout. Out-of-range writes are ignored. */
    static void write(Surface surface, unsigned x, unsigned y, std::uint32_t colour) noexcept;

    /** Fills the whole frame. */
    static void clear(Surface surface, std::uint32_t colour) noexcept;

    /** Submits the back buffer and waits for the display to take it. */
    bool present() noexcept;

    /** The last failure, for one log line; never null. */
    [[nodiscard]] const char *last_error() const noexcept
    {
        return error_;
    }

  private:
    int handle_ = -1;
    void *mapped_ = nullptr;
    void *frames_[2] = {nullptr, nullptr};
    std::uint64_t flip_status_[16] = {0};
    bool agc_ready_ = false;
    int registered_[2] = {-1, -1};
    int back_ = 0;
    unsigned width_ = 0;
    unsigned height_ = 0;
    const char *error_ = "display not opened";
};
} // namespace ps5::display
