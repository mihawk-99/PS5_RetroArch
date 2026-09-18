/*
 * PS5 RetroArch - the title's entry point.
 *
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file owns the process: open the display, draw, present, repeat. RetroArch's
 * frontend is driven from here once its video driver is in place
 * (docs/REFERENCE.md, "The port").
 *
 * Until then it draws a moving bar, which is the cheapest thing that proves the
 * title pipeline end to end: the build, the title format, the launcher, VideoOut
 * and the tiled direct-memory path. If a step here fails, the title reports it in
 * the kernel log instead of exiting silently, because a launch context that
 * returns from main is closed by the shell as a crash.
 */

#include "display.hpp"

#include <cstdint>

extern "C" int sceKernelUsleep(std::uint32_t microseconds);

namespace
{
/* Never returns: the shell owns this process's lifetime, and returning from main
 * is reported as `eboot.bin calls exit()`. */
[[noreturn]] void halt(const char *reason) noexcept
{
    for (;;)
        (void)sceKernelUsleep(1000000);
    (void)reason;
}

void draw_progress(ps5::display::Surface surface, unsigned step) noexcept
{
    const std::uint32_t background = 0xff0a0d19u;
    const std::uint32_t bar = 0xff00ffffu;
    ps5::display::Display::clear(surface, background);

    const unsigned bar_height = surface.height / 24;
    const unsigned top = surface.height / 2 - bar_height / 2;
    const unsigned filled = (surface.width / 60) * (step % 60);
    for (unsigned y = top; y < top + bar_height; ++y)
        for (unsigned x = 0; x < filled; ++x)
            ps5::display::Display::write(surface, x, y, bar);
}
} // namespace

int main()
{
    ps5::display::Display display;
    if (!display.open(1920, 1080))
        halt(display.last_error());

    for (unsigned step = 0;; ++step)
    {
        draw_progress(display.back_surface(), step);
        if (!display.present())
            halt(display.last_error());
    }
}
