/* PS5 RetroArch - the game library: what every frontend shows, read from RetroArch.
 *
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A frontend keeps no game library of its own (docs/FRONTENDS.md): its systems and
 * game lists are RetroArch's playlists, read again each time it starts, so a game
 * added to or removed from a playlist in RetroArch appears or goes in every frontend.
 * This header and src/ps5_library.c are that contract, compiled into eboot.bin and
 * into every frontend executable (tools/build-frontend.sh), as game mode's is
 * (src/ps5_game.h):
 *
 *   - the games are the entries of the playlists in /app0/playlists (RetroArch's
 *     own history, favourites and media playlists left out), read in name order, each
 *     game once, as the first playlist that lists it has it; then, with
 *     ps5_library_load_content, the games in the content folders that no playlist
 *     lists (issue 25: a game copied into content/ shows without a scan);
 *   - a game's system is the first of these that names a known platform: the
 *     RetroArch database its entry was scanned against, the playlist's name, the
 *     folder the game is in, the database of the core its entry names; else its own
 *     system, named after the playlist or the folder;
 *   - a system's core is the one its entries name most, else the core RetroArch's
 *     core info (/app0/info) associates with the platform's database, among the
 *     cores the title carries; a game's own playlist core still wins when it is
 *     launched (game mode);
 *   - a system's folder is the deepest folder all its games are in, and its
 *     extensions those of its games and of its core;
 *   - what RetroArch remembers of play goes with each game: whether it is in the
 *     favourites (builtin/content_favorites.lpl), its place in the history
 *     (builtin/content_history.lpl), and its runtime log
 *     (logs/<core name>/<content name>.lrtl): when it was last played, how often and
 *     for how long. A game in the history with no runtime log is given a time from
 *     its place: the history file's, a minute earlier for each place below the top,
 *     so the order holds.
 *
 * A frontend writes its own files from this (frontends/es-de/ps5/library_ps5.cpp
 * writes ES-DE's systems file and game lists) and launches every game through game
 * mode with ps5_library_command.
 */
#ifndef PS5_RETROARCH_PS5_LIBRARY_H
#define PS5_RETROARCH_PS5_LIBRARY_H

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define PS5_LIBRARY_PLAYLISTS "/app0/playlists"
#define PS5_LIBRARY_INFO "/app0/info"
#define PS5_LIBRARY_CORES "/app0/cores"
#define PS5_LIBRARY_PATH_MAX 1024

    /* A core the title carries, from its core info file. */
    struct ps5_library_core
    {
        char path[PS5_LIBRARY_PATH_MAX]; /* /app0/cores/<name>_libretro.so */
        char name[128];                  /* its display_name */
        char core_name[128];             /* its corename: its runtime logs' folder */
        char databases[1024];            /* RetroArch databases, '|'-separated */
        char extensions[512];            /* supported extensions, '|'-separated */
    };

    struct ps5_library_system
    {
        char id[64];    /* the platform ("snes", "psx"...), or a slug of its name */
        char name[256]; /* RetroArch's name for it: its database, or the playlist's or folder's */
        int known;      /* 1 when id is a platform of the table, 0 for its own system */
        char core[PS5_LIBRARY_PATH_MAX]; /* its default core, or "" */
        char folder[PS5_LIBRARY_PATH_MAX];
        char extensions[1024];         /* lower case, '|'-separated, without dots */
        size_t first_game, game_count; /* its games, consecutive in the library's */
    };

    struct ps5_library_game
    {
        char path[PS5_LIBRARY_PATH_MAX];
        char label[512];
        char core[PS5_LIBRARY_PATH_MAX]; /* its playlist's core, or "" (DETECT or none) */
        char crc32[32];
        char playlist[256]; /* the playlist's file name */
        size_t system;
        int favorite;               /* in RetroArch's favourites */
        unsigned history;           /* its place in RetroArch's history, 1 the newest, or 0 */
        char last_played[20];       /* "YYYY-MM-DD HH:MM:SS", local time, or "" */
        unsigned play_count;        /* from its runtime log */
        unsigned long play_seconds; /* from its runtime log */
    };

    struct ps5_library
    {
        struct ps5_library_system *systems;
        size_t system_count;
        struct ps5_library_game *games; /* by system, then by label */
        size_t game_count;
        struct ps5_library_core *cores;
        size_t core_count;
    };

    /* Reads the library; 0, or -1 when memory ran out. Missing folders are an empty
     * library, not an error. ps5_library_free releases it either way. */
    int ps5_library_load(struct ps5_library *library, const char *playlists, const char *info,
                         const char *cores);
    /* The same, and then the games in the content roots no playlist lists: each folder at
     * a root's top that names a platform the title has a core for (its database's name,
     * "PS1", "Sega 32X", "NEC PC-FX"...) is that system's, and in it every file a core of
     * the platform takes, but firmware (named in a core's info, or [BIOS]...) and the
     * track files of a folder that has a .cue, .gdi or .m3u. A DOS game is a folder,
     * started by the program named like it; a ScummVM game is a folder, started from a
     * data file in it. content ends with NULL; NULL or empty scans nothing. */
    int ps5_library_load_content(struct ps5_library *library, const char *playlists,
                                 const char *info, const char *cores, const char *const *content);
    /* The same, read from outside the title (the WebUI's daemon): title_root is the
     * title's real folder (/data/homebrew/PPSA99169), and every game under it is
     * recorded by its /app0 path, as the title and its frontends see it. */
    int ps5_library_load_content_at(struct ps5_library *library, const char *playlists,
                                    const char *info, const char *cores, const char *const *content,
                                    const char *title_root);
    void ps5_library_free(struct ps5_library *library);

    /* The platform a name stands for (a RetroArch database, a playlist's or a folder's
     * name: "Sony - PlayStation", "PS1.lpl", "Nintendo - Game Boy"): its id, or NULL. */
    const char *ps5_library_platform(const char *name);
    /* A platform's RetroArch database name ("Sony - PlayStation"), or NULL. */
    const char *ps5_library_platform_database(const char *id);

    /* A folder in content for each system the installed cores run, named as RetroArch
     * names it ("Nintendo - Nintendo Entertainment System"), so games have a place to go
     * and every frontend knows their system. A system the content folder already has a
     * folder for, whatever its name ("PS1", "Genesis"), gets none: nothing is renamed or
     * moved. A database no platform here stands for (Satellaview, CD32...) gets none.
     * Returns how many folders it made, or -1 when content cannot be read. */
    int ps5_library_make_system_folders(const char *content, const char *info, const char *cores);

    /* Game mode's launch command for a game (src/ps5_game.h): RetroArch's command line
     * with its system's core, shell-quoted. 0, or -1 when it does not fit. */
    int ps5_library_command(const struct ps5_library *library, const struct ps5_library_game *game,
                            char *out, size_t size);

    /* One entry of a playlist, as RetroArch wrote it. */
    struct ps5_playlist_entry
    {
        const char *path, *label, *core_path, *crc32, *db_name;
    };
    /* Calls each for every entry of a playlist file, with its default core; the number
     * of entries, or -1 when the file cannot be read or is not a playlist. */
    int ps5_playlist_read(const char *file, char *default_core, size_t default_core_size,
                          void (*each)(void *context, const struct ps5_playlist_entry *entry),
                          void *context);

    /* The shared media library (src/scraper.h): every frontend reads a game's media from
     * the same files, PS5_LIBRARY_MEDIA/<system id>/<folder>/<key>.<ext>, folders named
     * as EmulationStation names them (covers, screenshots, titlescreens, marquees,
     * videos). A game's key is its content's file name without the extension (an
     * archive member's "#inner" part dropped). */
#define PS5_LIBRARY_MEDIA "/app0/library"
    /* Writes the key of a content path; 0, or -1 when it does not fit. */
    int ps5_library_media_key(const char *path, char *out, size_t size);
    /* The stored file of a game's media: 1 with its path, 0 when there is none. system
     * is anything ps5_library_platform reads (a database, playlist or folder name);
     * folder is the store's ("covers"...) or RetroArch's thumbnail type ("Named_Boxarts",
     * "Named_Snaps", "Named_Titles", "Named_Logos"). */
    int ps5_library_media(const char *root, const char *system, const char *content_path,
                          const char *folder, char *out, size_t size);
    /* RetroArch's thumbnail lookup (patch 0114) asks this first: the store under /app0. */
    int ps5_library_thumbnail(const char *system, const char *content_path, const char *type,
                              char *out, size_t size);
#ifdef __cplusplus
}
#endif

#endif
