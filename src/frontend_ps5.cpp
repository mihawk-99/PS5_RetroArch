/* PS5 RetroArch - platform paths and browser roots.
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
extern "C"
{
#include <defaults.h>
#include <frontend/frontend_driver.h>
#include <lists/file_list.h>
#include <menu/menu_entries.h>
}
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ps5platform/libc.h>
#include <initializer_list>
#include <sys/stat.h>

namespace
{
extern "C" unsigned ps5_sandbox_mount_host_fs() noexcept;
extern "C" void ps5_bootlog(const char *line) noexcept;

/* libSceNet: brings the socket resolver and the net subsystem up. The BSD
 * socket calls themselves are kernel exports, but getaddrinfo goes through
 * sceNetResolver, which fails until sceNetInit ran once. */
extern "C" int sceNetInit(void);
extern "C" int sceNetCtlInit(void);

extern "C" void ps5_input_trace(const char *line) noexcept;

constexpr const char *saved_config = "/app0/config/retroarch.cfg";

// The top of the file browser, Load Content included (patches/series, 0097):
// INTERNAL, the title's own folder, and EXTERNAL, where the console mounts USB
// drives, an extended storage drive and any other external storage. Each shows
// its name, and opening it opens its folder. SYSTEM is the console's real
// filesystem, visible only while the sandbox mounts from
// ps5_sandbox_mount_host_fs succeeded - without them the title sees nothing
// outside /app0.
struct Root
{
    const char *path;
    const char *name;
};
constexpr Root internal_root{"/app0", "INTERNAL"};
constexpr Root external_root{"/mnt", "EXTERNAL"};
constexpr Root host_root{"/hostroot", "SYSTEM"};
unsigned sandbox_mounts = 0;

void set_directory(default_dirs slot, const char *path)
{
    std::snprintf(g_defaults.dirs[slot], sizeof(g_defaults.dirs[slot]), "%s", path);
}

void initialize(void *)
{
    /* The sandbox mount happens before anything else touches the filesystem:
     * the browser roots below depend on whether /hostroot and the real /mnt
     * came up. */
    sandbox_mounts = ps5_sandbox_mount_host_fs();
    {
        /* libSceNet carries the resolver; NetCtl brings the interface state
         * machine up. Both stay silent unless they fail - a bootlog line is
         * the only witness an early crash leaves behind. */
        const int net_result = sceNetInit();
        const int ctl_result = sceNetCtlInit();
        char line[96];
        std::snprintf(line, sizeof(line),
                      "frontend: sceNetInit=%d sceNetCtlInit=%d",
                      net_result, ctl_result);
        ps5_input_trace(line);
        if (net_result || ctl_result)
            ps5_bootlog(line);
    }
    const char *directories[] = {"/app0/config",    "/app0/cores",     "/app0/content",
                                 "/app0/system",    "/app0/savefiles", "/app0/savestates",
                                 "/app0/playlists", "/app0/cheats",    "/app0/shaders",
                                 "/app0/overlays",  "/app0/autoconfig",
                                 "/app0/database",  "/app0/thumbnails",
                                 "/app0/screenshots", "/app0/remaps",  "/app0/recordings",
                                 "/app0/content/Saturn", "/app0/system/Saturn",
                                 "/app0/filters",   "/app0/filters/audio",
                                 "/app0/filters/video", "/app0/cache"};
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
    set_directory(DEFAULT_DIR_CORE_INFO, "/app0/info");
    set_directory(DEFAULT_DIR_CORE_ASSETS, "/app0/content");
    set_directory(DEFAULT_DIR_SYSTEM, "/app0/system");
    set_directory(DEFAULT_DIR_SRAM, "/app0/savefiles");
    set_directory(DEFAULT_DIR_SAVESTATE, "/app0/savestates");
    set_directory(DEFAULT_DIR_PLAYLIST, "/app0/playlists");
    set_directory(DEFAULT_DIR_ASSETS, "/app0/assets");
    set_directory(DEFAULT_DIR_SHADER, "/app0/shaders");
    set_directory(DEFAULT_DIR_VIDEO_FILTER, "/app0/filters");
    set_directory(DEFAULT_DIR_OVERLAY, "/app0/overlays");
    set_directory(DEFAULT_DIR_LOGS, "/app0");
    /* The rest of the PC layout: cheats, shaders, overlays, autoconfig and the
     * database directories a RetroArch asset drop expects to find. */
    set_directory(DEFAULT_DIR_CHEATS, "/app0/cheats");
    set_directory(DEFAULT_DIR_SHADER, "/app0/shaders");
    set_directory(DEFAULT_DIR_OVERLAY, "/app0/overlays");
    set_directory(DEFAULT_DIR_OSK_OVERLAY, "/app0/overlays");
    set_directory(DEFAULT_DIR_AUTOCONFIG, "/app0/autoconfig");
    set_directory(DEFAULT_DIR_AUDIO_FILTER, "/app0/filters/audio");
    set_directory(DEFAULT_DIR_VIDEO_FILTER, "/app0/filters/video");
    set_directory(DEFAULT_DIR_DATABASE, "/app0/database");
    set_directory(DEFAULT_DIR_THUMBNAILS, "/app0/thumbnails");
    set_directory(DEFAULT_DIR_SCREENSHOT, "/app0/screenshots");
    set_directory(DEFAULT_DIR_REMAP, "/app0/remaps");
    set_directory(DEFAULT_DIR_RECORD_CONFIG, "/app0/recordings");
    set_directory(DEFAULT_DIR_RECORD_OUTPUT, "/app0/recordings");
    set_directory(DEFAULT_DIR_WALLPAPERS, "/app0/assets/wallpapers");
    set_directory(DEFAULT_DIR_CONTENT_FAVORITES, "/app0/playlists");
    set_directory(DEFAULT_DIR_CONTENT_HISTORY, "/app0/playlists");
    set_directory(DEFAULT_DIR_CONTENT_IMAGE_HISTORY, "/app0/playlists");
    set_directory(DEFAULT_DIR_CONTENT_MUSIC_HISTORY, "/app0/playlists");
    set_directory(DEFAULT_DIR_CONTENT_VIDEO_HISTORY, "/app0/playlists");
    set_directory(DEFAULT_DIR_CACHE, "/app0/cache");
    std::fprintf(stderr, "frontend ps5: config=%s browser=/app0 cores=/app0/cores\n", saved_config);
    // Startup summary: known roots only; never log the user's file names.
    for (const char *path : {"/app0", "/app0/cores", "/mnt", "/mnt/usb0", "/hostroot",
                             "/hostroot/mnt", "/hostroot/data"})
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
    const Root *const all_roots[] = {&internal_root, &external_root, &host_root};
    for (const Root *root : all_roots)
    {
        if (root == &host_root && !(sandbox_mounts & 1))
            continue; /* no host root without the sandbox mount */
        if (menu_entries_append(list, root->path, "", label, FILE_TYPE_DIRECTORY, 0, 0,
                                nullptr))
            file_list_set_alt_at_offset(list, list->size - 1, root->name);
    }
    return 0;
}
} // namespace

extern "C"
{
    const char *ps5_core_system_directory(const char *core, const char *directory)
    {
        if (core && directory && std::strcmp(core, "Beetle Saturn") == 0 &&
            std::strcmp(directory, "/app0/system") == 0)
            return "/app0/system/Saturn";
        return directory;
    }

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
