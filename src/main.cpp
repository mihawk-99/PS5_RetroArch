/*
 * PS5 RetroArch - the title's entry point.
 *
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is deliberately small. Its job is to own the process: open the
 * display, hand it to whatever drives frames, and stay alive until the shell
 * closes the title. RetroArch's frontend is driven from here once its video
 * driver is in place (docs/REFERENCE.md, "The port").
 *
 * Until then it draws a moving frame, which is the cheapest thing that proves the
 * title pipeline end to end: if this appears on the console, then the build, the
 * title format, the launcher, VideoOut and the direct-memory path all work, and
 * every later failure belongs to the port rather than to the scaffolding.
 */

#include "display.hpp"

#include <cstdint>

namespace
{
/* Returning from main, or calling exit, crashes this launch context: the shell
 * owns the process lifetime. The loop is therefore unconditional. */
void fill(ps5::display::Surface surface, std::uint32_t colour) noexcept
{
    const std::size_t count = surface.width * surface.height;
    for (std::size_t index = 0; index < count; ++index)
        surface.pixels[index] = colour;
}

void draw_progress(ps5::display::Surface surface, unsigned step) noexcept
{
    const std::uint32_t background = 0xff0a0d19u;
    const std::uint32_t bar = 0xff00ffffu;
    fill(surface, background);

    const unsigned bar_height = surface.height / 24;
    const unsigned top = surface.height / 2 - bar_height / 2;
    const unsigned filled = (surface.width / 60) * (step % 60);
    for (unsigned y = top; y < top + bar_height; ++y)
    {
        std::uint32_t *row = surface.pixels + static_cast<std::size_t>(y) * surface.width;
        for (unsigned x = 0; x < filled; ++x)
            row[x] = bar;
    }
}
} // namespace

int main()
{
    ps5::display::Display display;
    if (!display.open(1920, 1080))
        return 1;

    for (unsigned step = 0;; ++step)
    {
        draw_progress(display.back_surface(), step);
        if (!display.present())
            return 1;
    }
}
