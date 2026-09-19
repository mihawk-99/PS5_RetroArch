/*
 * PS5 RetroArch - the title's entry point.
 *
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * RetroArch's own `main` is one line: `return rarch_main(argc, argv, NULL)`. So
 * the entry point is not reconciled with RetroArch's, it simply calls the same
 * function with the arguments this title wants, and the pipeline's `_start` stays
 * the process entry. That is the whole of the "entry point" problem the plan
 * listed.
 *
 * The display is opened by the video driver, not here: `video_ps5` owns it, and
 * this file's job is to hand RetroArch its arguments and let its runloop drive.
 *
 * Arguments, and why each is here:
 *   -f              fullscreen, which on a console is the only mode
 *   -c <path>       the config to read and write, inside the title's own folder
 *   --verbose       so the run is readable in the console's log
 *   --menu          start with the menu up rather than waiting for content: this
 *                   title has no content, and the menu is what is being proven
 *
 * Why this file writes a trace. The first run of this title on the console ended
 * with the kernel reporting `eboot.bin calls exit() exit_value=0` and nothing
 * else: no output, no crash, no message. A program that exits zero and says
 * nothing is indistinguishable from one that never reached `main`, and the
 * console's log cannot be asked which it was. So the entry point and the video
 * driver append a line per step to /app0/trace.txt, and after a run that file
 * says how far it got. See src/trace.hpp.
 */

#include <cstddef>

#include "trace.hpp"

/* RetroArch's entry, in C. */
extern "C" int rarch_main(int argc, char *argv[], void *data);

namespace
{
/* The title's own folder, as the console mounts it: the application image is at
 * /app0 and this is where a title may keep its configuration. */
constexpr const char *config_path = "/app0/retroarch.cfg";
} // namespace

int main()
{
    /* First thing: prove that control reached this function at all, before
     * anything that could fail. */
    ps5::debug::mark("main() entered; static constructors have already run");

    /* argv must be writable and NULL-terminated: RetroArch's option parsing
     * walks it the way the C runtime would have. */
    char arg0[] = "retroarch";
    char arg_fullscreen[] = "-f";
    char arg_config[] = "-c";
    char arg_config_path[] = "/app0/retroarch.cfg";
    char arg_verbose[] = "--verbose";
    char arg_menu[] = "--menu";
    /* RetroArch's own log, on the console, from the first line of main.
     *
     * This matters more than the config's log settings: `--log-file` sets the
     * override and enables file logging *before* the config is parsed, so it
     * records what happens during startup - which is exactly where the Vulkan
     * path dies silently. A failure that says nothing is the one thing a console
     * run cannot diagnose, and this is how the frontend is made to speak. */
    char arg_log[] = "--log-file=/app0/retroarch.log";
    char *argv[] = {
        arg0, arg_fullscreen, arg_config, arg_config_path, arg_verbose, arg_log, arg_menu, nullptr,
    };
    (void)config_path;

    ps5::debug::mark("argv built: retroarch -f -c /app0/retroarch.cfg --verbose --log-file");

    const int status =
        rarch_main(static_cast<int>(sizeof(argv) / sizeof(argv[0])) - 1, argv, nullptr);

    /* If this line is on the console, the frontend ran and returned by itself. */
    ps5::debug::mark_value("rarch_main returned", status);

    return status;
}
