/*
 * PS5 RetroArch - the title's folders and files stay reachable over FTP.
 *
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Why. The title runs as its own user and the console's FTP server as another,
 * so what the title creates is reachable over FTP only through the permission
 * bits it gives everyone else. RetroArch makes its folders 0750
 * (libretro-common's retro_vfs_mkdir_impl), its unbuffered files with mode 0
 * (retro_vfs_file_open_impl passes 0), and the process umask takes write away
 * from the rest: savestates/dolphin-emu answered FTP with "550 Permission
 * denied", so my save states could be neither copied off nor backed up
 * (2026-09-25). src/frontend_ps5.cpp already gave the seven top-level folders
 * 0777; everything below them kept RetroArch's modes.
 *
 * So every folder and every file the title or a core creates is 0777 (files were
 * 0666 until users of v0.6.7 met FTP refusals again, 2026-10-07): mkdir, open
 * and fopen are wrapped at link time (tools/build-title.sh; a
 * core's own imports of them bind to the same wrappers) and give what they
 * create those bits after creating it, which the umask cannot take away. The
 * console's libraries do not resolve umask itself: a first build that cleared
 * it jumped to address 0 at start. What an earlier build made is repaired at
 * start: every folder and file under /app0 that lacks those bits is given
 * them. Only permission bits change; no file's contents are touched.
 *
 * The repair runs beside the frontend's start, and since v0.6.7 the title usually
 * hands itself over at once - to the picker or EmulationStation, through
 * LoadExec, which ends this process - so the walk was cut short and what
 * EmulationStation and the picker wrote (they are other executables, without
 * these wrappers) stayed out of FTP's reach. ps5_permissions_settle waits for the
 * walk to finish; every handover calls it first.
 */

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>

#include <fcntl.h>
#include <atomic>
#include <pthread.h>
#include <unistd.h>
#include <sys/stat.h>

#include <ps5platform/libc.h>
#include "trace.hpp"
#include "title_threads.hpp"
#include "webui_link.h"

namespace
{
constexpr mode_t kFolderMode = 0777;
constexpr mode_t kFileMode = 0777;
/* Deeper than any folder the title or a core makes (Dolphin's user folder is
 * the deepest, about six below /app0). */
constexpr int kRepairDepth = 16;

struct RepairCounts
{
    unsigned checked = 0;
    unsigned repaired = 0;
    unsigned refused = 0;
};

void repair(const char *path, int depth, RepairCounts &counts)
{
    DIR *const directory = ps5_opendir(path);
    if (directory == nullptr)
        return;
    while (const struct dirent *const entry = ps5_readdir(directory))
    {
        if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0)
            continue;
        char child[1024];
        const int length = std::snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
        if (length < 0 || static_cast<std::size_t>(length) >= sizeof(child))
            continue;
        /* The scraper's sign-ins stay private to the title (0700/0600, src/scraper.cpp). */
        if (std::strcmp(child, "/app0/config/private") == 0)
            continue;
        struct stat status{};
        if (stat(child, &status) != 0)
            continue;
        ++counts.checked;
        const bool folder = S_ISDIR(status.st_mode);
        const mode_t wanted = folder ? kFolderMode : kFileMode;
        if ((status.st_mode & wanted) != wanted)
        {
            /* A file the FTP account uploaded belongs to it and refuses the
             * title's chmod; it is reachable over FTP already. */
            if (chmod(child, (status.st_mode & 07777) | wanted) == 0)
                ++counts.repaired;
            else
                ++counts.refused;
        }
        if (folder && depth > 0)
            repair(child, depth - 1, counts);
    }
    ps5_closedir(directory);
}

double now_ms()
{
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<double>(now.tv_sec) * 1000.0 + static_cast<double>(now.tv_nsec) / 1e6;
}
} // namespace

namespace
{
/* The repair walks everything under /app0 -- 17,550 folders and files on my
 * console, 389 ms -- so it runs beside the frontend's start rather than before
 * it. Which of them the title made cannot be told from inside it: /app0's
 * stat reports every entry as uid 0, the FTP account's own as well. */
std::atomic<int> repair_state{0}; /* 0 not started, 1 walking, 2 done */

void *repair_thread(void *)
{
    const double start = now_ms();
    RepairCounts counts;
    repair("/app0", kRepairDepth, counts);
    char line[160];
    std::snprintf(line, sizeof(line),
                  "permissions: /app0 checked=%u repaired=%u refused=%u in %.1f ms", counts.checked,
                  counts.repaired, counts.refused, now_ms() - start);
    ps5::debug::mark(line);
    repair_state.store(2);
    return nullptr;
}
} // namespace

/* Starts the repair of /app0; once, first thing in main. */
extern "C" void ps5_open_permissions()
{
    pthread_t thread;
    repair_state.store(1);
    if (create_title_thread(&thread, repair_thread, nullptr) == 0)
        pthread_detach(thread);
    else
    {
        repair_state.store(0);
        ps5::debug::mark("permissions: the repair thread could not start");
    }
}

/* Waits, at most a minute, for the repair to finish; before any LoadExec. */
extern "C" void ps5_permissions_settle()
{
    /* The WebUI's daemon, if this process is sending it to the loader, goes whole
     * (src/webui_link.h): every LoadExec of eboot.bin waits here first. */
    ps5_webui_link_settle();
    if (repair_state.load() != 1)
        return;
    const double start = now_ms();
    while (repair_state.load() == 1 && now_ms() - start < 60000.0)
        usleep(10000);
    char line[96];
    std::snprintf(line, sizeof(line), "permissions: handover waited %.1f ms for the repair%s",
                  now_ms() - start, repair_state.load() == 2 ? "" : " (gave up)");
    ps5::debug::mark(line);
}
