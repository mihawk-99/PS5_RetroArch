/*
 * PS5 RetroArch - the startup trace. See src/trace.hpp for why it exists.
 *
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "trace.hpp"

#include <cstdio>

namespace ps5::debug
{
namespace
{
/* Inside the title's own folder, which the console mounts at /app0 and which the
 * trace files written by an earlier build proved is writable. */
constexpr const char *trace_path = "/app0/trace.txt";

void write(const char *line) noexcept
{
    std::FILE *file = std::fopen(trace_path, "a");
    if (file == nullptr)
        return;
    std::fputs(line, file);
    std::fputc('\n', file);
    std::fflush(file);
    std::fclose(file);
}
} // namespace

void mark(const char *step) noexcept
{
    write(step);
}

void mark_value(const char *step, long long value) noexcept
{
    char line[192];
    std::snprintf(line, sizeof(line), "%s = %lld", step, value);
    write(line);
}

void mark_init(const char *step, bool have_video, int width, int height) noexcept
{
    char line[192];
    std::snprintf(line, sizeof(line), "%s (video=%s width=%d height=%d)", step,
                  have_video ? "present" : "NULL", width, height);
    write(line);
}
} // namespace ps5::debug

/* TEMPORARY PROBE - removed before commit. */
extern "C" void rarch_probe(const char *step, long long a, long long b, long long c,
                            long long d) noexcept
{
    char line[256];
    std::snprintf(line, sizeof(line), "probe %s a=%lld b=%lld c=%lld d=%lld", step, a, b, c, d);
    ps5::debug::mark(line);
}
