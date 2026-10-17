/* PS5 RetroArch - which frontend a launch of eboot.bin starts (src/frontend_mode_ps5.cpp says why).
 *
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef PS5_RETROARCH_FRONTEND_MODE_PS5_H
#define PS5_RETROARCH_FRONTEND_MODE_PS5_H

#include "ps5_game.h"

#ifdef __cplusplus
#include <string>
#include <vector>

namespace ps5::frontend_mode
{
enum class Next
{
    retroarch, /* RetroArch, in this process */
    game,      /* a frontend's game, in RetroArch in this process (src/ps5_game.h) */
    picker,    /* the frontend picker, /app0/picker/picker.bin */
    es_de,     /* EmulationStation, /app0/es-de/es-de.bin */
    close      /* nothing: the title closes (a frontend quit, and one is remembered) */
};

struct Launch
{
    std::string mode;            /* --ps5-mode=<mode>, or empty */
    bool test_run = false;       /* a test run's launch (/app0/test-run.txt) */
    bool picker_test = false;    /* an armed picker test (/app0/picker/picker-test.txt) */
    bool picker_present = false; /* the title carries the picker */
    bool es_de_present = false;  /* the title carries EmulationStation */
    std::string choice = "ask";  /* the frontend remembered (src/ps5_frontend_choice.h) */
    bool reopen_held = false;    /* L1 held as the title started: the picker anyway */
};

struct Paths
{
    std::string picker, es_de, test_run, picker_test, eboot;
    std::string request, result, playlists; /* game mode's (src/ps5_game.h) */
    std::string content;                    /* a forwarder's relative --rom is in it */
    std::string info;                       /* the cores' .info files (supported_extensions) */
    std::string choice;                     /* config/frontend.cfg */
    std::string trace, retroarch_log, game_log;
    /* Reads the pad as the title starts: true when L1 is held. Asked only when a
     * frontend is remembered and the launch is the home screen's. */
    bool (*reopen_held)();
};

/* The mode the process arguments name with --ps5-mode=, from argv[0] on (LoadExec's
 * arguments are the whole argv), or an empty string. */
std::string mode_argument(int argc, char **argv);

/* A home screen forwarder's launch: a small app with a home screen tile of its own that
 * starts the title with
 *
 *   --rom <file>        the content: an absolute path, or one inside /app0/content/
 *   --core <core>       the core: snes9x, snes9x_libretro.so or /app0/cores/snes9x_libretro.so;
 *                       without it, the one RetroArch's playlists associate with the content
 *   --exit-after-game   when RetroArch quits, the title closes rather than opening as from
 *                       the home screen
 *
 * (each --x <value> also as --x=<value>). The game runs as a frontend's does in game mode
 * (src/ps5_game.h): checked, then RetroArch with -L, Close Content quitting it. */
struct Forward
{
    std::string rom, core;
    bool exit_after_game = false;
};
Forward forward_arguments(int argc, char **argv);
/* The content's path: an absolute one as it is; a relative one inside content (a folder
 * ending in '/'), never above it: empty for a path with a ".." part. A trailing '/' goes. */
std::string forward_content(const std::string &rom, const std::string &content);
/* The core's path inside cores (a folder ending in '/'), from a name with or without
 * _libretro.so; a path with a '/' as it is (ps5_game_check decides). Empty for none. */
std::string forward_core(const std::string &core, const std::string &cores);
/* The cores whose .info file in info (a folder ending in '/') lists the content's extension in
 * supported_extensions, by name (snes9x), sorted: a forwarder without --core whose content no
 * playlist names takes the one core that runs it. */
std::vector<std::string> forward_extension_cores(const std::string &info,
                                                 const std::string &content);
/* Of several such cores, the one a cartridge extension two cores share is meant for (the Mega
 * Drive and Master System extensions: Genesis Plus GX before PicoDrive); empty when there is
 * no such choice and --core has to name one. */
/* The cores whose .info lists "/" in supported_extensions: they open a folder as content
 * (DOSBox Pure, PUAE, VICE). A forwarder's --rom may name a folder for them. */
std::vector<std::string> forward_folder_cores(const std::string &info);
std::string forward_preferred_core(const std::string &content,
                                   const std::vector<std::string> &cores);
/* A launch that starts a session: no --ps5- argument at all, as from the home screen
 * or a test run. Every handover names a mode, and a test's restarts their generation. */
bool session_start(int argc, char **argv);
/* Keeps a log's last copy as previous (one), so the next run does not overwrite it. */
void rotate(const std::string &path, const std::string &previous);
/* The logs a launch keeps apart (docs/FRONTENDS.md): at a session start the trace's last
 * session becomes trace.1.txt. */
void start_session_logs(const Paths &paths, int argc, char **argv);
/* RetroArch's log for this process, its previous copy kept: a game's (game mode) apart
 * from RetroArch's own. */
std::string retroarch_log(const Paths &paths);
Next decide(const Launch &launch);
const char *name(Next next);
/* Starts what this launch is for. It returns only when RetroArch runs in this
 * process: chosen, a game it runs, or a LoadExec refused or that did not replace the
 * process within replaced_wait_seconds. */
void run(const Paths &paths, int argc, char **argv, unsigned replaced_wait_seconds);
/* In game mode, the game RetroArch is to run; nullptr otherwise. */
const struct ps5_game *running_game();
/* After RetroArch has quit: true when the title goes back to the picker instead of
 * closing, which it does when the picker started RetroArch (--ps5-mode=retroarch). */
bool back_to_picker(const std::string &mode, bool picker_present);
/* After RetroArch has quit with status: in game mode, the result for the frontend and
 * a restart as it; else, when back_to_picker says so, a restart as a frontend that
 * quit (the picker, or the title closes when a frontend is remembered). After an
 * update was installed, neither: the title closes, so no frontend of the old build
 * starts beside the new. It returns only when the title is to close, or when LoadExec
 * did not replace the process. */
void after_retroarch(const Paths &paths, const std::string &mode, int status,
                     unsigned replaced_wait_seconds, bool update_installed);
} // namespace ps5::frontend_mode

extern "C"
{
#endif

    /* First thing in main: at a session start the trace's last session is kept. */
    void ps5_frontend_session_logs(int argc, char **argv);
    void ps5_frontend_dispatch(int argc, char **argv);
    /* In game mode, the game RetroArch runs (its core and content); NULL otherwise. */
    const struct ps5_game *ps5_frontend_game(void);
    /* RetroArch's log for this launch (after the dispatch), the previous one kept. */
    const char *ps5_frontend_retroarch_log(void);
    /* Called once RetroArch has quit with status, before the title closes;
     * update_installed when an update was installed as it quit. */
    void ps5_frontend_after_retroarch(int status, int update_installed);
    /* src/frontend_hold_ps5.cpp: L1 held on the first user's pad as the title starts. */
    bool ps5_frontend_reopen_held(void);

#ifdef __cplusplus
}
#endif

#endif
