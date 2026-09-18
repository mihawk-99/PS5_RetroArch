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
 */

#include <cstddef>

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
    /* argv must be writable and NULL-terminated: RetroArch's option parsing
     * walks it the way the C runtime would have. */
    char arg0[] = "retroarch";
    char arg_fullscreen[] = "-f";
    char arg_config[] = "-c";
    char arg_config_path[] = "/app0/retroarch.cfg";
    char arg_verbose[] = "--verbose";
    char *argv[] = {
        arg0, arg_fullscreen, arg_config, arg_config_path, arg_verbose, nullptr,
    };
    (void)config_path;

    return rarch_main(static_cast<int>(sizeof(argv) / sizeof(argv[0])) - 1, argv, nullptr);
}
