/* PS5 RetroArch - platform paths and browser roots.
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
extern "C"
{
#include <defaults.h>
#include <frontend/frontend_driver.h>
#include <menu/menu_entries.h>
}
#include <cerrno>
#include <cstdio>
#include <cstring>
#include "ps5_directory.h"
#include <initializer_list>
#include <sys/stat.h>

namespace
{
constexpr const char *saved_config = "/app0/config/retroarch.cfg";

void set_directory(default_dirs slot, const char *path)
{
    std::snprintf(g_defaults.dirs[slot], sizeof(g_defaults.dirs[slot]), "%s", path);
}

void initialize(void *)
{
    const char *directories[] = {"/app0/config",   "/app0/cores",     "/app0/content",
                                 "/app0/system",   "/app0/savefiles", "/app0/savestates",
                                 "/app0/playlists"};
    for (const char *path : directories)
    {
        if (mkdir(path, 0777) != 0 && errno != EEXIST)
        {
            std::fprintf(stderr, "frontend ps5: mkdir %s failed errno=%d\n", path, errno);
            continue;
        }
        // FTP still denies uploads with group-write 0775 on this console. Apply
        // the authorized 0777 after creation, defeating umask and repairing old folders.
        if (chmod(path, 0777) != 0)
            std::fprintf(stderr, "frontend ps5: chmod %s to 0777 failed errno=%d\n", path, errno);
    }

    /* A packaged seed can change on update; the user's live config must survive it. */
    struct stat st{};
    if (stat(saved_config, &st) != 0 && errno == ENOENT)
    {
        FILE *source = std::fopen("/app0/retroarch.cfg", "rb");
        FILE *dest = source ? std::fopen("/app0/config/retroarch.cfg.tmp", "wb") : nullptr;
        bool ok = source && dest;
        if (ok)
        {
            char buffer[4096];
            size_t count;
            while ((count = std::fread(buffer, 1, sizeof(buffer), source)) != 0)
                if (std::fwrite(buffer, 1, count, dest) != count)
                {
                    ok = false;
                    break;
                }
            ok = !std::ferror(source) && ok;
        }
        if (source)
            std::fclose(source);
        if (dest && std::fclose(dest) != 0)
            ok = false;
        if (ok)
            ok = std::rename("/app0/config/retroarch.cfg.tmp", saved_config) == 0;
        if (!ok)
            std::remove("/app0/config/retroarch.cfg.tmp");
        std::fprintf(stderr, "frontend ps5: seed config %s\n", ok ? "installed" : "FAILED");
    }
    std::snprintf(g_defaults.path_config, sizeof(g_defaults.path_config), "%s", saved_config);
    set_directory(DEFAULT_DIR_MENU_CONFIG, "/app0/config");
    set_directory(DEFAULT_DIR_MENU_CONTENT, "/app0");
    set_directory(DEFAULT_DIR_CORE, "/app0/cores");
    set_directory(DEFAULT_DIR_CORE_ASSETS, "/app0/content");
    set_directory(DEFAULT_DIR_SYSTEM, "/app0/system");
    set_directory(DEFAULT_DIR_SRAM, "/app0/savefiles");
    set_directory(DEFAULT_DIR_SAVESTATE, "/app0/savestates");
    set_directory(DEFAULT_DIR_PLAYLIST, "/app0/playlists");
    set_directory(DEFAULT_DIR_ASSETS, "/app0/assets");
    set_directory(DEFAULT_DIR_LOGS, "/app0");
    std::fprintf(stderr, "frontend ps5: config=%s browser=/app0 cores=/app0/cores\n", saved_config);
    // Startup summary: known roots only; never log the user's file names.
    for (const char *path : {"/", "/app0", "/app0/cores", "/data", "/mnt/usb0"})
    {
        errno = 0;
        DIR *dir = ps5_opendir(path);
        size_t entries = 0;
        if (dir)
        {
            while (ps5_readdir(dir))
                ++entries;
            ps5_closedir(dir);
        }
        std::fprintf(stderr, "frontend ps5: directory %s opened=%d entries=%zu errno=%d\n", path,
                     dir != nullptr, entries, errno);
    }
}

void environment(int *, char **, void *, void *)
{
    /* Keep the title's original argv on startup. A NULL callback makes task_content
     * substitute menu_content_environment_get(), which loses the initial -c. */
}

int drives(void *data, bool content)
{
    auto *list = static_cast<file_list_t *>(data);
    const auto label = content ? MENU_ENUM_LABEL_FILE_DETECT_CORE_LIST_PUSH_DIR
                               : MENU_ENUM_LABEL_FILE_BROWSER_DIRECTORY;
    for (const char *path : {"/app0", "/data", "/mnt/usb0", "/mnt/usb1", "/"})
    {
        DIR *dir = ps5_opendir(path);
        if (!dir)
            continue;
        ps5_closedir(dir);
        menu_entries_append(list, path, "", label, FILE_TYPE_DIRECTORY, 0, 0, nullptr);
    }
    return 0;
}
} // namespace

extern "C"
{
    frontend_ctx_driver_t frontend_ctx_ps5 = []
    {
        frontend_ctx_driver_t driver{};
        driver.environment_get = environment;
        driver.init = initialize;
        driver.parse_drive_list = drives;
        driver.ident = "ps5";
        return driver;
    }();
}
