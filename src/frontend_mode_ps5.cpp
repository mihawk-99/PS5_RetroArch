/* PS5 RetroArch - which frontend a launch of eboot.bin starts, and game mode.
 *
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The title has a pre-screen that chooses between RetroArch and EmulationStation
 * (docs/FRONTENDS.md), and each runs in a process of its own: the title restarts
 * itself through LoadExec (approach B). eboot.bin is what the home screen starts,
 * so it decides first, before RetroArch sets anything up:
 *
 *   --ps5-mode=retroarch   RetroArch, here (the picker chose it)
 *   --ps5-mode=game        a game a frontend asked for, in RetroArch, here
 *   --ps5-mode=es-de       EmulationStation, /app0/es-de/es-de.bin
 *   --ps5-mode=picker      the picker, /app0/picker/picker.bin
 *   --ps5-mode=quit        a frontend quit (EmulationStation, or RetroArch the
 *                          picker started): the picker, or, when a frontend is
 *                          remembered, the title closes
 *   --rom <file> [--core <core>] [--exit-after-game]
 *                          a home screen forwarder's game (no mode): run as a frontend's
 *                          game, then the title as from the home screen, or closed
 *   no mode                a launch from the home screen: the frontend remembered
 *                          in config/frontend.cfg (src/ps5_frontend_choice.h), or
 *                          the picker when none is or L1 is held as the title
 *                          starts; but a test run's launch (/app0/test-run.txt,
 *                          which tools/run-title.sh writes) stays RetroArch, as
 *                          every test expects, unless a picker test is armed
 *                          (/app0/picker/picker-test.txt)
 *
 * Game mode is the one way any frontend starts a game (src/ps5_game.h has the
 * contract). Its request is taken here and checked. The core is the one RetroArch's
 * playlists associate with the content, when they do, else the frontend's. A request
 * that cannot
 * run is refused back to its frontend, with the reason in the result. RetroArch then
 * starts on the game (src/main.cpp adds -L, the content and Close Content quitting),
 * and once it has quit, ps5_frontend_after_retroarch writes the result and restarts
 * the title as the frontend.
 *
 * Quitting a frontend goes back to the picker: EmulationStation restarts eboot.bin
 * with no mode (frontends/es-de/ps5/main_ps5.cpp), and RetroArch, when the picker
 * started it, does the same once it has quit and closed its drivers. The picker's
 * CIRCLE closes the title. RetroArch started any other way (a test run) closes the
 * title as before.
 *
 * A title built without the picker, or without EmulationStation, runs RetroArch
 * as it always has. LoadExec, accepted, returns and the shell replaces the process a
 * moment later (evidence/loadexec-relaunch): this waits for that, up to a minute. A
 * LoadExec refused, or a process still here after the wait, is recorded in the trace
 * and RetroArch runs instead, so a launch never ends on a black screen.
 */
#include "frontend_mode_ps5.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ps5_frontend_choice.h"
#include "trace.hpp"

extern "C" int sceSystemServiceLoadExec(const char *path, const char *const *argv);

namespace ps5::frontend_mode
{
namespace
{
/* This launch's --ps5-mode, kept for the quit */
std::string launch_mode;
/* The game this launch runs, in game mode */
struct ps5_game game;
bool game_running = false;
std::time_t game_started = 0;
/* The game is a forwarder's (--rom), not a frontend's: nothing to hand a result back to */
bool game_forwarded = false;
bool forward_exit_after_game = false;

extern "C" void ps5_permissions_settle(); /* src/permissions_ps5.cpp */

std::string joined(const std::vector<std::string> &names)
{
    std::string text;
    for (const std::string &name : names)
        text += (text.empty() ? "" : ", ") + name;
    return text;
}

bool is_folder(const std::string &path)
{
    struct stat status;
    return stat(path.c_str(), &status) == 0 && S_ISDIR(status.st_mode);
}

bool exists(const std::string &path)
{
    std::FILE *file = std::fopen(path.c_str(), "rb");
    if (file)
        std::fclose(file);
    return file != nullptr;
}

/* Restarts the title as the image, with one argument; it returns only when that did
 * not happen. */
void restart_as(const std::string &image, const char *argument, unsigned replaced_wait_seconds,
                const char *failure)
{
    const char *const arguments[] = {argument, nullptr};
    ps5_permissions_settle();
    std::fflush(nullptr);
    const int result = sceSystemServiceLoadExec(image.c_str(), arguments);
    if (result >= 0)
        for (unsigned waited = 0; waited < replaced_wait_seconds * 10; waited++)
            usleep(100000);
    ps5::debug::mark_value(failure, result);
}

void restart_as(const std::string &image, unsigned replaced_wait_seconds, const char *failure)
{
    restart_as(image, "", replaced_wait_seconds, failure);
}

/* Asks the shell to close the title, as RetroArch's quit does; it returns only when
 * that did not happen. */
void close_title(unsigned replaced_wait_seconds)
{
    ps5_permissions_settle();
    std::fflush(nullptr);
    const int result = sceSystemServiceLoadExec("exit", nullptr);
    if (result >= 0)
        for (unsigned waited = 0; waited < replaced_wait_seconds * 10; waited++)
            usleep(100000);
    ps5::debug::mark_value("frontend: the shell did not close the title; RetroArch runs; result",
                           result);
}

/* A request refused: its frontend gets the reason, and the title goes back to it. */
void refuse(const Paths &paths, struct ps5_game &request, unsigned replaced_wait_seconds)
{
    char line[400];
    std::snprintf(line, sizeof line, "game mode: refused: %s", request.error);
    ps5::debug::mark(line);
    request.status = -1;
    request.seconds = 0;
    if (request.frontend[0] != '/' || !exists(request.frontend) ||
        ps5_game_write(paths.result.c_str(), &request, 1) != 0)
        return; /* nowhere to go back to: RetroArch runs */
    restart_as(
        request.frontend, replaced_wait_seconds,
        "game mode: LoadExec of the frontend did not replace the process; RetroArch runs; result");
}

/* Takes the request: true when RetroArch is to run its game in this process. */
bool take_game(const Paths &paths, unsigned replaced_wait_seconds)
{
    int kind = -1;
    struct ps5_game request;
    if (ps5_game_read(paths.request.c_str(), &request, &kind) != 0 || kind != 0)
    {
        ps5::debug::mark("game mode: no request; RetroArch runs");
        return false;
    }
    std::remove(paths.request.c_str()); /* a request runs once */
    if (ps5_game_check(&request) != 0)
    {
        refuse(paths, request, replaced_wait_seconds);
        return false;
    }
    /* RetroArch's playlists decide first: a core they associate with this content is
     * the one RetroArch would use, so it wins over the frontend's default for the
     * system, if it is one of the title's cores. */
    struct ps5_game associated = request;
    if (ps5_game_playlist_core(paths.playlists.c_str(), request.content, associated.core,
                               sizeof(associated.core)) &&
        std::strcmp(associated.core, request.core) != 0)
    {
        if (ps5_game_check(&associated) == 0)
        {
            ps5::debug::mark(
                "game mode: the core RetroArch's playlist associates with the content");
            std::snprintf(request.core, sizeof(request.core), "%s", associated.core);
        }
        else
            ps5::debug::mark(
                "game mode: the playlist's core is not one of the title's; kept the frontend's");
    }
    if (!request.core[0])
    {
        std::snprintf(
            request.error, sizeof(request.error),
            "no core: neither the frontend nor RetroArch's playlists name one for this content");
        refuse(paths, request, replaced_wait_seconds);
        return false;
    }
    game = request;
    game_running = true;
    game_forwarded = false;
    game_started = std::time(nullptr);
    char line[PS5_GAME_PATH_MAX * 3 + 64];
    std::snprintf(line, sizeof line, "game mode: %s with %s, for %s", game.content, game.core,
                  game.frontend);
    ps5::debug::mark(line);
    return true;
}

/* A forwarder's game: true when RetroArch is to run it in this process. Not runnable,
 * the reason goes to the trace and the launch goes on as one from the home screen. */
bool take_forward(const Paths &paths, const Forward &forward)
{
    struct ps5_game request = {};
    const std::string content = forward_content(forward.rom, paths.content);
    const std::string core = forward_core(forward.core, PS5_GAME_CORES);
    std::snprintf(request.content, sizeof(request.content), "%s", content.c_str());
    std::snprintf(request.core, sizeof(request.core), "%s", core.c_str());
    /* game mode's check wants a frontend to come back to: the title itself */
    std::snprintf(request.frontend, sizeof(request.frontend), "%s", paths.eboot.c_str());
    const auto refused = [&](const char *why)
    {
        char line[PS5_GAME_PATH_MAX + 400];
        std::snprintf(line, sizeof line, "forwarder: --rom %s --core %s not run: %s",
                      forward.rom.c_str(), forward.core.empty() ? "(none)" : forward.core.c_str(),
                      why);
        ps5::debug::mark(line);
        return false;
    };
    if (content.empty())
        return refused("the content is not a path inside the content folder (no \"..\")");
    if (core.size() >= sizeof(request.core) || content.size() >= sizeof(request.content))
        return refused("a path is too long");
    /* A folder is content for a core that opens folders, as DOSBox Pure opens a DOS or Windows
     * installation: its .info lists "/" in supported_extensions. Game mode's check wants a file,
     * so a folder's request is checked with the title in its place, for the core and frontend. */
    const bool folder = is_folder(content);
    const std::vector<std::string> folder_cores =
        folder ? forward_folder_cores(paths.info) : std::vector<std::string>();
    const auto check = [&](struct ps5_game &candidate)
    {
        if (!folder)
            return ps5_game_check(&candidate);
        struct ps5_game probe = candidate;
        std::snprintf(probe.content, sizeof(probe.content), "%s", paths.eboot.c_str());
        const int result = ps5_game_check(&probe);
        std::snprintf(candidate.error, sizeof(candidate.error), "%s", probe.error);
        if (result != 0 || !candidate.core[0])
            return result;
        std::string name = candidate.core;
        name = name.substr(name.rfind('/') + 1);
        name.erase(name.size() - std::strlen("_libretro.so"));
        if (std::find(folder_cores.begin(), folder_cores.end(), name) != folder_cores.end())
            return 0;
        std::snprintf(candidate.error, sizeof(candidate.error),
                      "the content is a folder, and %s does not open folders (its .info lists no "
                      "\"/\"%s%s)",
                      name.c_str(),
                      folder_cores.empty() ? "" : "; these do: ", joined(folder_cores).c_str());
        return -1;
    };
    if (check(request) != 0)
        return refused(request.error);
    /* --core is the user's choice for this tile; without it, RetroArch's playlists choose,
     * as they do for a frontend's game. */
    if (!request.core[0])
    {
        struct ps5_game associated = request;
        if (ps5_game_playlist_core(paths.playlists.c_str(), request.content, associated.core,
                                   sizeof(associated.core)) &&
            check(associated) == 0)
            std::snprintf(request.core, sizeof(request.core), "%s", associated.core);
        /* nor a playlist: the core whose .info file lists the content's extension, when one
         * does (or a shared cartridge extension's own core) */
        std::vector<std::string> runnable;
        for (const std::string &name :
             folder ? folder_cores : forward_extension_cores(paths.info, request.content))
        {
            struct ps5_game candidate = request;
            const std::string path = forward_core(name, PS5_GAME_CORES);
            std::snprintf(candidate.core, sizeof(candidate.core), "%s", path.c_str());
            if (path.size() < sizeof(candidate.core) && check(candidate) == 0)
                runnable.push_back(name);
        }
        std::string chosen =
            runnable.size() == 1 ? runnable[0] : forward_preferred_core(request.content, runnable);
        if (!request.core[0] && !chosen.empty())
            std::snprintf(request.core, sizeof(request.core), "%s",
                          forward_core(chosen, PS5_GAME_CORES).c_str());
        if (!request.core[0] && runnable.empty())
            return refused(folder
                               ? "no core: the content is a folder, and no core of the title "
                                 "opens folders"
                               : "no core: neither --core nor RetroArch's playlists name one, and "
                                 "no core lists its extension");
        if (!request.core[0])
        {
            const std::string why = "no core: neither --core nor RetroArch's playlists name one, "
                                    "and several cores " +
                                    std::string(folder ? "open folders" : "list its extension") +
                                    " (" + joined(runnable) + "); --core names the one";
            return refused(why.c_str());
        }
    }
    game = request;
    game_running = true;
    game_forwarded = true;
    forward_exit_after_game = forward.exit_after_game;
    game_started = std::time(nullptr);
    char line[PS5_GAME_PATH_MAX * 2 + 64];
    std::snprintf(line, sizeof line, "forwarder: %s with %s%s", game.content, game.core,
                  forward_exit_after_game ? ", closing after it" : "");
    ps5::debug::mark(line);
    return true;
}
} // namespace

std::string mode_argument(int argc, char **argv)
{
    static const char prefix[] = "--ps5-mode=";
    for (int i = 0; i < argc && argv && argv[i]; i++)
        if (std::strncmp(argv[i], prefix, sizeof(prefix) - 1) == 0)
            return argv[i] + sizeof(prefix) - 1;
    return "";
}

Forward forward_arguments(int argc, char **argv)
{
    Forward forward;
    const auto value = [&](int &i, const char *flag, std::string &out)
    {
        const std::size_t length = std::strlen(flag);
        if (std::strcmp(argv[i], flag) == 0)
        {
            if (i + 1 < argc && argv[i + 1])
                out = argv[++i];
            return true;
        }
        if (std::strncmp(argv[i], flag, length) == 0 && argv[i][length] == '=')
        {
            out = argv[i] + length + 1;
            return true;
        }
        return false;
    };
    for (int i = 0; i < argc && argv && argv[i]; i++)
    {
        if (std::strcmp(argv[i], "--exit-after-game") == 0)
            forward.exit_after_game = true;
        else if (!value(i, "--rom", forward.rom))
            value(i, "--core", forward.core);
    }
    return forward;
}

std::string forward_content(const std::string &rom, const std::string &content)
{
    std::string path = rom;
    while (!path.empty() && (path.back() == ' ' || path.back() == '\r' || path.back() == '\n'))
        path.pop_back();
    while (path.size() > 1 && path.back() == '/') /* a folder given as "dos/w95/" */
        path.pop_back();
    if (path.empty())
        return "";
    if (path[0] == '/')
        return path;
    for (std::string::size_type at = 0; at <= path.size();)
    {
        const std::string::size_type slash = path.find('/', at);
        const std::string::size_type end = slash == std::string::npos ? path.size() : slash;
        if (path.compare(at, end - at, "..") == 0 && end - at == 2)
            return "";
        at = end + 1;
    }
    return content + path;
}

std::string forward_core(const std::string &core, const std::string &cores)
{
    if (core.empty() || core.find('/') != std::string::npos)
        return core;
    static const char suffix[] = "_libretro.so";
    const std::string::size_type suffix_length = sizeof(suffix) - 1;
    std::string name = core;
    if (name.size() > 3 && name.compare(name.size() - 3, 3, ".so") == 0)
        name.erase(name.size() - 3);
    if (name.size() >= suffix_length - 3 &&
        name.compare(name.size() - (suffix_length - 3), suffix_length - 3, "_libretro") == 0)
        name.erase(name.size() - (suffix_length - 3));
    return name.empty() ? "" : cores + name + suffix;
}

namespace
{
std::string lower(std::string text)
{
    for (char &c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

/* The content's extension, lower case, without the dot; empty for none */
std::string extension_of(const std::string &content)
{
    const std::string::size_type slash = content.rfind('/');
    const std::string::size_type dot = content.rfind('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash) ||
        dot + 1 == content.size())
        return "";
    return lower(content.substr(dot + 1));
}
} // namespace

namespace
{
/* The cores whose .info lists token in supported_extensions ("sfc", or "/" for a folder) */
std::vector<std::string> cores_listing(const std::string &info, const std::string &extension)
{
    std::vector<std::string> cores;
    if (extension.empty())
        return cores;
    DIR *folder = opendir(info.c_str());
    if (!folder)
        return cores;
    static const char suffix[] = "_libretro.info";
    const std::string::size_type suffix_length = sizeof(suffix) - 1;
    for (struct dirent *entry; (entry = readdir(folder)) != nullptr;)
    {
        const std::string file = entry->d_name;
        if (file.size() <= suffix_length ||
            file.compare(file.size() - suffix_length, suffix_length, suffix) != 0)
            continue;
        std::FILE *input = std::fopen((info + file).c_str(), "r");
        if (!input)
            continue;
        char line[4096];
        bool listed = false;
        while (!listed && std::fgets(line, sizeof line, input))
        {
            /* supported_extensions = "sfc|smc|swc" */
            const char *at = line;
            while (*at == ' ' || *at == '\t')
                at++;
            if (std::strncmp(at, "supported_extensions", 20) != 0)
                continue;
            const char *open = std::strchr(at, '"');
            const char *close = open ? std::strchr(open + 1, '"') : nullptr;
            if (!close)
                break;
            const std::string list = lower(std::string(open + 1, close));
            for (std::string::size_type start = 0; start <= list.size();)
            {
                std::string::size_type bar = list.find('|', start);
                if (bar == std::string::npos)
                    bar = list.size();
                if (list.compare(start, bar - start, extension) == 0 &&
                    bar - start == extension.size())
                    listed = true;
                start = bar + 1;
            }
            break;
        }
        std::fclose(input);
        if (listed)
            cores.push_back(file.substr(0, file.size() - suffix_length));
    }
    closedir(folder);
    std::sort(cores.begin(), cores.end());
    return cores;
}
} // namespace

std::vector<std::string> forward_extension_cores(const std::string &info,
                                                 const std::string &content)
{
    return cores_listing(info, extension_of(content));
}

std::vector<std::string> forward_folder_cores(const std::string &info)
{
    return cores_listing(info, "/");
}

std::string forward_preferred_core(const std::string &content,
                                   const std::vector<std::string> &cores)
{
    static const char *const sega[] = {"md", "smd", "gen", "sms", "gg", "sg", "68k", "sgd"};
    const std::string extension = extension_of(content);
    for (const char *shared : sega)
        if (extension == shared &&
            std::find(cores.begin(), cores.end(), "genesis_plus_gx") != cores.end())
            return "genesis_plus_gx";
    return "";
}

bool session_start(int argc, char **argv)
{
    for (int i = 0; i < argc && argv && argv[i]; i++)
        if (std::strncmp(argv[i], "--ps5-", 6) == 0)
            return false;
    return true;
}

void rotate(const std::string &path, const std::string &previous)
{
    if (!exists(path))
        return;
    std::remove(previous.c_str());
    std::rename(path.c_str(), previous.c_str());
}

namespace
{
/* "/app0/retroarch.log" -> "/app0/retroarch.1.log" */
std::string previous_of(const std::string &path)
{
    const std::string::size_type dot = path.rfind('.');
    const std::string::size_type slash = path.rfind('/');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return path + ".1";
    return path.substr(0, dot) + ".1" + path.substr(dot);
}
} // namespace

void start_session_logs(const Paths &paths, int argc, char **argv)
{
    if (session_start(argc, argv))
        rotate(paths.trace, previous_of(paths.trace));
}

std::string retroarch_log(const Paths &paths)
{
    const std::string &log = game_running ? paths.game_log : paths.retroarch_log;
    rotate(log, previous_of(log));
    return log;
}

Next decide(const Launch &launch)
{
    if (launch.mode == "game")
        return Next::game;
    if (launch.mode == "es-de")
        return launch.es_de_present ? Next::es_de : Next::retroarch;
    if (launch.mode == "picker")
        return launch.picker_present ? Next::picker : Next::retroarch;
    if (launch.mode == "quit")
        return launch.choice == "ask" && launch.picker_present ? Next::picker : Next::close;
    if (!launch.mode.empty())
        return Next::retroarch; /* retroarch, or a mode this build does not know */
    if (launch.picker_test && launch.picker_present)
        return Next::picker;
    if (launch.test_run)
        return Next::retroarch;
    if (!launch.reopen_held)
    {
        if (launch.choice == "retroarch")
            return Next::retroarch;
        if (launch.choice == "es-de" && launch.es_de_present)
            return Next::es_de;
    }
    return launch.picker_present ? Next::picker : Next::retroarch;
}

const char *name(Next next)
{
    switch (next)
    {
    case Next::picker:
        return "picker";
    case Next::es_de:
        return "es-de";
    case Next::game:
        return "game";
    case Next::close:
        return "close";
    default:
        return "retroarch";
    }
}

void run(const Paths &paths, int argc, char **argv, unsigned replaced_wait_seconds)
{
    Launch launch;
    launch.mode = mode_argument(argc, argv);
    launch_mode = launch.mode;
    launch.test_run = exists(paths.test_run);
    launch.picker_test = exists(paths.picker_test);
    launch.picker_present = exists(paths.picker);
    launch.es_de_present = exists(paths.es_de);
    launch.choice = ps5_frontend_choice_read(paths.choice.c_str());
    /* The pad is read only when what it decides is in question: a launch from the
     * home screen, with a frontend remembered. */
    if (launch.mode.empty() && !launch.test_run && !launch.picker_test && launch.choice != "ask" &&
        launch.picker_present && paths.reopen_held)
        launch.reopen_held = paths.reopen_held();
    /* A forwarder's game, on a launch that names no mode (a handover's mode wins). */
    const Forward forward = forward_arguments(argc, argv);
    if (!forward.rom.empty() && launch.mode.empty())
    {
        if (take_forward(paths, forward))
            return; /* RetroArch runs the game */
        /* not runnable (the trace says why): the launch goes on as from the home screen */
    }
    const Next next = decide(launch);
    char line[256];
    std::snprintf(line, sizeof line,
                  "frontend: mode '%s', test run %d, picker test %d, picker %d, es-de %d, "
                  "remembered %s, L1 %d -> %s",
                  launch.mode.c_str(), launch.test_run, launch.picker_test, launch.picker_present,
                  launch.es_de_present, launch.choice.c_str(), launch.reopen_held, name(next));
    ps5::debug::mark(line);
    if (next == Next::game)
    {
        take_game(paths, replaced_wait_seconds);
        return; /* RetroArch runs: the game, or as it always has */
    }
    if (next == Next::retroarch)
        return;
    if (next == Next::close)
    {
        close_title(replaced_wait_seconds);
        return;
    }
    restart_as(next == Next::picker ? paths.picker : paths.es_de, replaced_wait_seconds,
               "frontend: LoadExec did not replace the process; RetroArch runs; result");
}

const struct ps5_game *running_game()
{
    return game_running ? &game : nullptr;
}

bool back_to_picker(const std::string &mode, bool picker_present)
{
    return mode == "retroarch" && picker_present;
}

void after_retroarch(const Paths &paths, const std::string &mode, int status,
                     unsigned replaced_wait_seconds, bool update_installed)
{
    if (update_installed)
    {
        /* The new build's files are in place: starting a frontend now would run the
         * new picker or EmulationStation beside this old process's decisions. The
         * title closes, and the updater's message asks for it to be reopened. */
        ps5::debug::mark("frontend: an update was installed; the title closes, no handover");
        game_running = false;
        return;
    }
    if (game_running && game_forwarded)
    {
        /* A forwarder's game: no frontend to hand a result to. The title opens as from
         * the home screen (the picker, or the frontend remembered), or closes. */
        game_running = game_forwarded = false;
        char line[128];
        std::snprintf(line, sizeof line, "forwarder: RetroArch quit (status %d) after %ld s; %s",
                      status, static_cast<long>(std::time(nullptr) - game_started),
                      forward_exit_after_game ? "the title closes" : "the title as from the home screen");
        ps5::debug::mark(line);
        if (forward_exit_after_game)
            return;
        restart_as(paths.eboot, replaced_wait_seconds,
                   "forwarder: LoadExec did not replace the process; the title closes; result");
        return;
    }
    if (game_running)
    {
        game_running = false;
        game.status = status;
        game.seconds = static_cast<long>(std::time(nullptr) - game_started);
        game.error[0] = '\0';
        char line[128];
        std::snprintf(line, sizeof line,
                      "game mode: RetroArch quit (status %d) after %ld s; back to the frontend",
                      status, game.seconds);
        ps5::debug::mark(line);
        if (ps5_game_write(paths.result.c_str(), &game, 1) != 0)
            ps5::debug::mark("game mode: the result could not be written");
        restart_as(game.frontend, replaced_wait_seconds,
                   "game mode: LoadExec of the frontend did not replace the process; the title "
                   "closes; result");
        return;
    }
    if (!back_to_picker(mode, exists(paths.picker)))
        return;
    /* A frontend quit: the picker, unless one is remembered by now (its Remember
     * switch), in which case the title closes, as RetroArch alone always did. */
    ps5::debug::mark("frontend: RetroArch quit; back to the picker or closed");
    restart_as(paths.eboot, "--ps5-mode=quit", replaced_wait_seconds,
               "frontend: LoadExec did not replace the process; the title closes; result");
}
} // namespace ps5::frontend_mode

namespace
{
const ps5::frontend_mode::Paths title_paths{"/app0/picker/picker.bin",
                                            "/app0/es-de/es-de.bin",
                                            "/app0/test-run.txt",
                                            "/app0/picker/picker-test.txt",
                                            PS5_GAME_EBOOT,
                                            PS5_GAME_REQUEST_PATH,
                                            PS5_GAME_RESULT_PATH,
                                            PS5_GAME_PLAYLISTS,
                                            "/app0/content/",
                                            "/app0/info/",
                                            PS5_FRONTEND_CHOICE_PATH,
                                            "/app0/trace.txt",
                                            "/app0/retroarch.log",
                                            "/app0/retroarch-game.log",
                                            ps5_frontend_reopen_held};
std::string log_path;
} // namespace

extern "C" void ps5_frontend_session_logs(int argc, char **argv)
{
    ps5::frontend_mode::start_session_logs(title_paths, argc, argv);
}

extern "C" void ps5_frontend_dispatch(int argc, char **argv)
{
    ps5::frontend_mode::run(title_paths, argc, argv, 60);
}

extern "C" const char *ps5_frontend_retroarch_log(void)
{
    log_path = ps5::frontend_mode::retroarch_log(title_paths);
    return log_path.c_str();
}

extern "C" const struct ps5_game *ps5_frontend_game(void)
{
    return ps5::frontend_mode::running_game();
}

extern "C" void ps5_frontend_after_retroarch(int status, int update_installed)
{
    ps5::frontend_mode::after_retroarch(title_paths, ps5::frontend_mode::launch_mode, status, 60,
                                        update_installed != 0);
}
