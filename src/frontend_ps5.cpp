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
#include "ps5_smb.h"
extern "C" int ps5_webui_settings_fold(const char *webui, const char *config);
#include <cerrno>
#include <cstdio>
#include <climits>
#include <cstring>
#include <ps5platform/libc.h>
#include <initializer_list>
#include <string>
#include <sys/stat.h>

namespace
{
constexpr const char *saved_config = "/app0/config/retroarch.cfg";

// The top of the file browser, Load Content included (patches/series, 0097):
// INTERNAL, the title's own folder, then the USB and extended storage drives
// mounted into the title's sandbox. A sandbox shows the drives under /mnt only
// when something mounts them into it: ShadowMountPlus 1.7beta4 and later does,
// for every PPSA title, for as long as it runs. A drive slot is offered only if a
// drive is mounted there; without any, the browser keeps EXTERNAL, the bare /mnt,
// as before. The console's own /data is never offered (owner's decision), though
// ShadowMountPlus mounts it too. Each shows its name.
struct Root
{
    const char *path;
    const char *name;
};
constexpr Root internal_root{"/app0", "INTERNAL"};
constexpr Root storage_roots[] = {{"/mnt/usb0", "USB 0"},      {"/mnt/usb1", "USB 1"},
                                  {"/mnt/usb2", "USB 2"},      {"/mnt/usb3", "USB 3"},
                                  {"/mnt/usb4", "USB 4"},      {"/mnt/usb5", "USB 5"},
                                  {"/mnt/usb6", "USB 6"},      {"/mnt/usb7", "USB 7"},
                                  {"/mnt/ext0", "EXTENDED 0"}, {"/mnt/ext1", "EXTENDED 1"}};
constexpr Root external_root{"/mnt", "EXTERNAL"};
// Last, SMB: the shared folder the WebUI's SMB page saved (src/ps5_smb.h), its share
// and folder on the server, read through RetroArch's VFS (src/ps5_smb_vfs.c).
constexpr const char *smb_config = "/app0/config/smb.cfg";

// Whether a directory opens, and how many entries it holds besides . and ..
bool opens(const char *path, size_t *entries = nullptr, int *error = nullptr)
{
    errno = 0;
    DIR *dir = ps5_opendir(path);
    size_t count = 0;
    if (dir)
    {
        while (const struct dirent *entry = ps5_readdir(dir))
            if (std::strcmp(entry->d_name, ".") != 0 && std::strcmp(entry->d_name, ".."))
                ++count;
        ps5_closedir(dir);
    }
    if (entries)
        *entries = count;
    if (error)
        *error = dir ? 0 : errno;
    return dir != nullptr;
}

// A drive slot holds a drive when its root is another filesystem than /mnt
// itself; an empty slot directory is the same filesystem.
bool available(const Root &root)
{
    size_t entries = 0;
    if (!opens(root.path, &entries))
        return false;
    struct stat slot{}, parent{};
    if (stat(root.path, &slot) == 0 && stat("/mnt", &parent) == 0)
        return slot.st_dev != parent.st_dev;
    return entries != 0;
}

// Graphics widgets (patches/series, 0138) are RetroArch's default once built in, but a
// build before them could only save menu_enable_widgets = "false", and RetroArch keeps
// what it saved. A config from such a build gets them once: the marker is written when
// that is done (or was not needed), and from then on the setting is the user's.
constexpr const char *widgets_marker = "/app0/config/widgets-default.done";
// The achievement unlock sound is on by default from the build that first ships its file
// (patches/series, 0143; assets/sounds), and every config saved before could only hold
// cheevos_unlock_sound_enable = "false", RetroArch's own default: the same once.
constexpr const char *unlock_sound_marker = "/app0/config/unlock-sound-default.done";

/* Turns a setting that retroarch.cfg holds as "false" to "true", once: marker is written
 * when that is done or was not needed, and from then on the setting is the user's. */
void on_once(const char *setting, const char *marker, const char *what)
{
    struct stat st{};
    if (stat(marker, &st) == 0)
        return;
    const std::string off = std::string(setting) + " = \"false\"";
    std::string text;
    if (FILE *in = std::fopen(saved_config, "rb"))
    {
        char buffer[4096];
        size_t count;
        while ((count = std::fread(buffer, 1, sizeof(buffer), in)) != 0)
            text.append(buffer, count);
        std::fclose(in);
    }
    size_t at = text.find(off);
    while (at != std::string::npos && at != 0 && text[at - 1] != '\n')
        at = text.find(off, at + 1);
    bool ok = true;
    if (at != std::string::npos)
    {
        text.replace(at, off.size(), std::string(setting) + " = \"true\"");
        FILE *out = std::fopen("/app0/config/retroarch.cfg.tmp", "wb");
        ok = out && std::fwrite(text.data(), 1, text.size(), out) == text.size();
        if (out && std::fclose(out) != 0)
            ok = false;
        ok = ok && std::rename("/app0/config/retroarch.cfg.tmp", saved_config) == 0;
        if (!ok)
            std::remove("/app0/config/retroarch.cfg.tmp");
        std::fprintf(stderr, "frontend ps5: %s turned on once %s\n", what,
                     ok ? "(saved off by a build without it)" : "FAILED");
    }
    if (ok)
        if (FILE *file = std::fopen(marker, "wb"))
            std::fclose(file);
}

void set_directory(default_dirs slot, const char *path)
{
    std::snprintf(g_defaults.dirs[slot], sizeof(g_defaults.dirs[slot]), "%s", path);
}

void initialize(void *)
{
    const char *directories[] = {"/app0/config",    "/app0/cores",         "/app0/content",
                                 "/app0/system",    "/app0/savefiles",     "/app0/savestates",
                                 "/app0/playlists", "/app0/system/Saturn", "/app0/shaders",
                                 "/app0/filters",   "/app0/overlays",      "/app0/config/remaps"};
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
    on_once("menu_enable_widgets", widgets_marker, "graphics widgets");
    on_once("cheevos_unlock_sound_enable", unlock_sound_marker, "the achievement unlock sound");
    /* What the WebUI saved since the last start becomes RetroArch's own setting, once, so a
     * later change in the menu stays (issue 130; src/ps5_webui_settings.c). */
    {
        const int taken = ps5_webui_settings_fold("/app0/config/webui.cfg", saved_config);
        if (taken)
            std::fprintf(stderr, "frontend ps5: %d WebUI setting(s) taken into retroarch.cfg%s\n",
                         taken < 0 ? 0 : taken, taken < 0 ? " FAILED" : "");
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
    /* input_remapping_directory "default" means this: with it unset, RetroArch loads no
     * remap at all (config_load_remap) and saves one to a path with no folder. */
    set_directory(DEFAULT_DIR_REMAP, "/app0/config/remaps");
    /* RetroArch's own thumbnails; the scraper's media, shared by every frontend, is
     * /app0/library, which the lookup asks first (patch 0114). */
    set_directory(DEFAULT_DIR_THUMBNAILS, "/app0/thumbnails");
    set_directory(DEFAULT_DIR_LOGS, "/app0");
    std::fprintf(stderr, "frontend ps5: config=%s browser=/app0 cores=/app0/cores\n", saved_config);
    // Startup summary: what this sandbox exposes, roots and mount names only;
    // never the user's file names.
    for (const char *path : {"/app0", "/app0/cores", "/data", "/mnt"})
    {
        size_t entries = 0;
        int error = 0;
        const bool opened = opens(path, &entries, &error);
        std::fprintf(stderr, "frontend ps5: directory %s opened=%d entries=%zu errno=%d\n", path,
                     opened, entries, error);
    }
    for (const Root &root : storage_roots)
        std::fprintf(stderr, "frontend ps5: storage %s (%s) %s\n", root.path, root.name,
                     available(root) ? "available" : "absent");
    if (DIR *dir = ps5_opendir("/mnt"))
    {
        std::string names;
        while (const struct dirent *entry = ps5_readdir(dir))
            if (entry->d_name[0] != '.' && names.size() < 400)
                names += std::string(names.empty() ? "" : " ") + entry->d_name;
        ps5_closedir(dir);
        std::fprintf(stderr, "frontend ps5: /mnt holds %s\n",
                     names.empty() ? "nothing" : names.c_str());
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
    const auto add = [&](const Root &root)
    {
        if (menu_entries_append(list, root.path, "", label, FILE_TYPE_DIRECTORY, 0, 0, nullptr))
            file_list_set_alt_at_offset(list, list->size - 1, root.name);
    };
    add(internal_root);
    bool any = false;
    for (const Root &root : storage_roots)
        if (available(root))
        {
            add(root);
            any = true;
        }
    if (!any)
        add(external_root);
    ps5_smb_config smb{};
    char smb_path[PATH_MAX];
    if (ps5_smb_config_load(smb_config, &smb) && ps5_smb_root(&smb, smb_path, sizeof(smb_path)))
        add(Root{smb_path, "SMB"});
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
