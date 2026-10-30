/* PS5 RetroArch - EmulationStation's entry point on the PS5.
 *
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ES-DE runs as its own executable of the title, /app0/es-de/es-de.bin, started
 * by eboot.bin through LoadExec (docs/FRONTENDS.md). Its own main (renamed
 * esde_main for this port, patches/0001) expects what a desktop gives it, which
 * a PS5 process does not have:
 *
 *  - argv[0] is the program: on the PS5 LoadExec's arguments are the whole argv,
 *    so this builds one with /app0/es-de/es-de.bin first, from which ES-DE
 *    finds its resources (/app0/es-de/resources; it has no /proc/self/exe here);
 *  - $HOME names its data directory: /app0/es-de, so its settings, game lists
 *    and themes are /app0/es-de/ES-DE, inside the title's folder;
 *  - its systems and game lists, written from RetroArch's playlists before it starts
 *    (library_ps5.cpp, the library every frontend shares), and --gamelist-only;
 *  - a working directory getcwd can name: /app0/es-de again
 *    (frontends/common/frontend_shims.c), since ES-DE makes every path absolute
 *    against it.
 *
 * The OpenGL driver's own reports (its printf lines: presentation, batching,
 * glthread) go to /app0/es-de/stdout.txt, a line at a time; the last start's is kept
 * as stdout.1.txt, so a game in between does not lose it, and this port's own log,
 * es-de-ps5.log, starts again as es-de-ps5.1.log past 512 KiB. /app0/es-de/env.txt,
 * when present, sets environment variables before ES-DE starts, one NAME=VALUE a
 * line: the driver's switches (PS5_GLTHREAD=1 runs Mesa's GL thread) can be tried
 * on the console without a new build.
 *
 * A game ES-DE starts runs in RetroArch through the title's game mode, and the title
 * comes back here when it is closed (game_ps5.cpp says how).
 *
 * While ES-DE shows, the WebUI is its daemon's (src/webui_link.h), which a LoadExec
 * leaves running; when the daemon asks for the title to close for an update, ES-DE
 * is sent SDL_QUIT, as its Quit entry does.
 *
 * When ES-DE returns (Quit in its menu), the title goes back to eboot.bin with
 * --ps5-mode=quit, which shows the picker or, when a frontend is remembered, closes
 * the title (src/frontend_mode_ps5.cpp), and with the arguments of this port that
 * this run was given (a relaunch test's, so the test sees its next generation).
 */
#include <SDL.h>
#include <cerrno>
#include <cstdio>
#include <sys/stat.h>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "webui_link.h"

int esde_main(int argc, char *argv[]);

extern "C" int sceSystemServiceLoadExec(const char *path, const char *const *argv);
extern "C" int sceSystemServiceHideSplashScreen(void);
extern "C" int sceKernelUsleep(unsigned int microseconds);
extern "C" int ps5_frontend_chdir(const char *path);
extern "C" int ps5_esde_take_game_result(void);
extern "C" int ps5_esde_write_library(char *summary, size_t summary_size);

/* ../PS5_OpenGL's heap (native-app/app_heap.c), which every allocation of this
 * program goes through: its 128 MiB default refused the Alekfull NX theme's
 * 1920x1080 backgrounds (8 MiB each decoded, one a system) at about 120 MiB live
 * and operator new trapped (klog/run-PPSA99169-162412.log). A size over 128 MiB is
 * mapped from direct memory, of which a title has about 12 GiB. */
extern "C" const size_t ps5_opengl_heap_size = size_t(1) << 30;

namespace
{
constexpr const char *program = "/app0/es-de/es-de.bin";
constexpr const char *home = "/app0/es-de";
constexpr const char *start_log = "/app0/es-de/es-de-ps5.log";
constexpr long start_log_limit = 512 * 1024;

/* Keeps a log's last copy as previous. */
void keep_previous(const char *path, const char *previous)
{
    struct stat status;
    if (stat(path, &status) != 0)
        return;
    std::remove(previous);
    std::rename(path, previous);
}

void note(const char *what, int value)
{
    if (std::FILE *log = std::fopen(start_log, "a"))
    {
        std::fprintf(log, "%lld %s %d\n", static_cast<long long>(std::time(nullptr)), what, value);
        std::fclose(log);
    }
}
} // namespace

int main(int argc, char **argv)
{
    struct stat log_status;
    if (stat(start_log, &log_status) == 0 && log_status.st_size > start_log_limit)
        keep_previous(start_log, "/app0/es-de/es-de-ps5.1.log");
    keep_previous("/app0/es-de/stdout.txt", "/app0/es-de/stdout.1.txt");
    note("start: hide splash", sceSystemServiceHideSplashScreen());
    ps5_webui_link_start(
        "es-de",
        [] {
            SDL_Event quit{};
            quit.type = SDL_QUIT;
            SDL_PushEvent(&quit);
        },
        [](const char *line) {
            if (std::FILE *log = std::fopen(start_log, "a"))
            {
                std::fprintf(log, "%lld %s\n", static_cast<long long>(std::time(nullptr)), line);
                std::fclose(log);
            }
        });
    if (std::freopen("/app0/es-de/stdout.txt", "w", stdout))
        std::setvbuf(stdout, nullptr, _IOLBF, 0);
    setenv("HOME", home, 1);
    if (std::FILE *environment = std::fopen("/app0/es-de/env.txt", "r"))
    {
        char line[256];
        int set = 0;
        while (std::fgets(line, sizeof(line), environment))
        {
            line[std::strcspn(line, "\r\n")] = '\0';
            char *equals = std::strchr(line, '=');
            if (line[0] == '#' || !equals || equals == line)
                continue;
            *equals = '\0';
            set += setenv(line, equals + 1, 1) == 0 ? 1 : 0;
        }
        std::fclose(environment);
        note("start: variables from env.txt", set);
    }
    note("start: working directory (errno if refused)", ps5_frontend_chdir(home) == 0 ? 0 : errno);
    /* Back from a game RetroArch ran (game mode, game_ps5.cpp)? ES-DE's start asks. */
    const int back_from_game = ps5_esde_take_game_result();
    note("start: back from a game", back_from_game);

    /* Its systems and game lists, from RetroArch's playlists (library_ps5.cpp), then
     * --gamelist-only: ES-DE holds exactly those games and scans for none. On a return
     * from a game, ES-DE updates that game's play count and time from the return record
     * (patches/0001), so walking every content drive again is unnecessary (issue 87). */
    char library[256] = "";
    if (back_from_game)
        std::snprintf(library, sizeof(library), "unchanged (skipped after game return)");
    else
    {
        const int systems = ps5_esde_write_library(library, sizeof(library));
        note("start: systems written", systems);
    }
    if (std::FILE *log = std::fopen(start_log, "a"))
    {
        std::fprintf(log, "%lld start: library from RetroArch's playlists: %s\n",
                     static_cast<long long>(std::time(nullptr)), library);
        std::fclose(log);
    }

    std::vector<std::string> port_arguments;
    static char gamelist_only[] = "--gamelist-only";
    std::vector<char *> arguments{const_cast<char *>(program), gamelist_only};
    for (int i = 0; i < argc && argv && argv[i]; i++)
    {
        if (std::strncmp(argv[i], "--ps5-", 6) == 0)
            port_arguments.emplace_back(argv[i]);
        else if (argv[i][0] != '\0')
            arguments.push_back(argv[i]);
    }
    note("start: arguments for ES-DE", static_cast<int>(arguments.size()) - 1);
    arguments.push_back(nullptr);

    const int status = esde_main(static_cast<int>(arguments.size()) - 1, arguments.data());
    note("ES-DE returned", status);

    std::vector<const char *> back;
    bool mode = false;
    for (const std::string &argument : port_arguments)
    {
        back.push_back(argument.c_str());
        mode |= argument.rfind("--ps5-mode=", 0) == 0;
    }
    if (!mode)
        back.push_back("--ps5-mode=quit");
    back.push_back(nullptr);
    ps5_webui_link_settle();
    note("back to eboot.bin: LoadExec", sceSystemServiceLoadExec("/app0/eboot.bin", back.data()));
    for (;;)
        sceKernelUsleep(100000);
}
