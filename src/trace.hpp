/*
 * PS5 RetroArch - a startup trace, to be read from the console's title folder.
 *
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Why this exists. The title's first runs on the console ended with the kernel
 * reporting `eboot.bin calls exit() exit_value=0` and nothing else - no output, no
 * crash, no message. `main` had already been shown to run, so the question was
 * how far the frontend got before returning. There is no debugger here, no
 * console output that reaches the kernel log, and the title's own stdout has no
 * reader: files in the title's folder are the only channel.
 *
 * So each step appends one line to /app0/trace.txt. Appending rather than
 * overwriting matters - the sequence is the information, and a file per step
 * cannot show order if the program dies between two writes. The file is opened,
 * written and closed on every call so a line is on the console even if the
 * process is killed immediately afterwards.
 *
 * This is a development aid. It is small, it is on the title's own writable
 * folder, and it is removed when the reason for it is gone; docs/ACTIVE.md says
 * when that is.
 */

#ifndef PS5_RETROARCH_TRACE_HPP
#define PS5_RETROARCH_TRACE_HPP

namespace ps5::debug
{
/* Append one line to the trace file. Safe to call before anything is
 * initialised: it uses stdio only, opens on each call, and ignores every error,
 * because a trace that can fail the program it is tracing is worse than none. */
void mark(const char *step) noexcept;

/* Append a line with one unsigned value, for counts and return codes. */
void mark_value(const char *step, long long value) noexcept;

/* A line with the argument values of a driver's init call, which is where the
 * interesting mismatch would be. */
void mark_init(const char *step, bool have_video, int width, int height) noexcept;
} // namespace ps5::debug

#endif
