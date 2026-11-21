/* Copyright (C) 2026 Mihawk; SPDX-License-Identifier: GPL-3.0-or-later */
/* The scraper: game metadata and media, kept on the console for every frontend.
 *
 * The store (<title>/library), one folder a system, named by the library's system id
 * (src/ps5_library.h), the id EmulationStation's systems are named by:
 *
 *     library/<system>/metadata/<game>.meta         key = "value" lines
 *     library/<system>/covers|screenshots|titlescreens|marquees|videos/<game>.<ext>
 *
 * <game> is the content's file name without its extension (a folder game's folder
 * name), as frontends name media. Every file is written under a hidden name and
 * renamed when whole: a cut download never replaces good media. What exists is kept
 * unless a job asks to overwrite, and a field edited by hand is never replaced.
 * Every frontend reads these same files, never a copy: RetroArch's thumbnail lookup
 * asks the store first (patch 0114, ps5_library_thumbnail), EmulationStation has the
 * store as its media folder and its game lists' fields from the .meta files, the
 * WebUI's games view reads them too; a future frontend calls ps5_library_media.
 *
 * Sources: libretro's thumbnail server (no account: covers, screenshots, title
 * screens) and ScreenScraper (every media type and the games' details; the user's
 * account, and this app's developer account, built in scrambled by
 * tools/build-webui-daemon.sh and sent over https only; never handed to a browser or a
 * PC helper, so ScreenScraper jobs run in PS5 mode). A ScreenScraper job keeps to the
 * account's thread count and pauses on a quota or a closed API, to be resumed.
 *
 * Jobs run in one of two ways, chosen by the user and remembered on the console:
 *   - "ps5": the console downloads, on worker threads of the server process, so a
 *     job goes on with no browser open (the WebUI daemon stays up while one runs);
 *   - "pc": the console identifies each game and hands out downloads as tasks; a
 *     helper on the PC (webui/ps5-media-helper.py) fetches each file and sends the
 *     bytes to the console, which writes them into the store. The bytes pass
 *     through the PC, and so does every question the identifying asks: a probe
 *     (does the source have this file) and a listing or database the console
 *     reads itself are tasks too, answered by the helper, so a console with no
 *     route to the internet still runs a PC-mode job. A task not delivered within
 *     its lease goes back to the queue.
 * A job's items are saved as they finish (library/.jobs/<id>.job), so a job cut short
 * (a restart, a closed helper, a cancel) resumes without redoing finished items. */
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ps5_scraper
{
struct Selection
{
    std::string system; /* a library system id */
    std::string path;   /* one game's path (as the playlists have it), or "" for all */
};
struct Options
{
    std::string mode = "ps5";        /* "ps5" or "pc" */
    std::string source = "libretro"; /* the first of sources */
    /* The sources in order: each is asked only for what the ones before it did not find
     * (a limited source's quota goes on the rest). Empty: source alone. */
    std::vector<std::string> sources;
    std::vector<std::string> kinds; /* cover, screenshot, title, logo, video */
    std::string region = "us";      /* us, eu, jp, wor */
    std::string language = "en";
    bool overwrite = false;
    bool details = false; /* the games' details too (description, developer...) */
};

/* Where the title is (its real folder, or /app0 in eboot.bin), and the sources' base
 * URLs (the tests point them at fake servers). Once, before anything else. */
void configure(const std::string &root, const std::string &libretro_base);
/* Stops the workers (the server is stopping); jobs stay saved. */
void shutdown();
/* A job is running that the console itself drives (keeps the daemon up). */
bool busy();

/* The library with what the store holds for each game, as JSON for the WebUI. */
std::string library_json();
std::string add_systems_json();
bool add_target(const std::string &system, const std::string &filename, std::string &relative,
                std::string &key, std::string &why);
std::string launchbox_lookup(const std::string &system, const std::string &query, std::string &why);
/* The remembered choices (method, media, region), as JSON; and saving them. */
std::string settings_json();
bool save_settings(const Options &options);

/* A source's sign-in (ScreenScraper): the account as JSON (the name and the quotas, never
 * the password); signing in checks the name and password with the source before they
 * are kept, on the console only (config/private, 0600); signing out forgets them. */
std::string account_json(const std::string &source);
bool sign_in(const std::string &source, const std::string &user, const std::string &password,
             std::string &why);
bool sign_out(const std::string &source);
/* One game, for its page: its details and, for every kind of media, whether it is
 * stored (type, size, uploaded by the user) and the file types it accepts; "" for no
 * such game. */
std::string game_json(const std::string &system, const std::string &game);
bool game_identity(const std::string &system, const std::string &game, const std::string &path,
                   std::string &core);
bool edit_game(const std::string &system, const std::string &game, const std::string &body,
               std::string &why);
/* A game file the WebUI is deleting (issue 57): its entries leave every playlist, and its
 * details and media go too unless another file of the same name (an ISO beside a CHD)
 * still uses them. The caller deletes the file itself afterwards. */
bool forget_game(const std::string &system, const std::string &game, const std::string &path,
                 std::string &why);
/* A media file the user uploads for a game: where to write it (a hidden file beside its
 * final name); false with why for a type the frontends cannot show. Once committed,
 * uploaded() makes it the kind's only file and marks it the user's: no scrape replaces it. */
bool upload_target(const std::string &system, const std::string &game, const std::string &kind,
                   const std::string &type, std::string &temporary, std::string &destination,
                   std::string &why);
void uploaded(const std::string &system, const std::string &game, const std::string &kind,
              const std::string &destination);
/* A stored media file's path for the WebUI, or "" (kind: cover, screenshot...). */
std::string media_file(const std::string &system, const std::string &game, const std::string &kind);

/* Starts a job (one at a time); false with why. */
bool start(const Options &options, const std::vector<Selection> &selection, std::string &id,
           std::string &why);
/* Resumes a job cut short; false with why. */
bool resume(const std::string &id, std::string &why);
/* The current (or named) job as JSON, items in trouble listed. */
std::string job_json(const std::string &id);
/* What a job did for each game, by system: the kinds it stored, those already there,
 * those missing, with totals per kind (JSON, for the page's recap). */
std::string recap_json(const std::string &id);
bool cancel(const std::string &id);
/* An item waiting on the user: action "choose" (value: a candidate), "search"
 * (value: words to search for), or "skip". */
bool resolve(const std::string &id, unsigned item, const std::string &action,
             const std::string &value, std::string &why);

/* PC mode. Leases up to max download tasks: JSON [{task, url, kind, item}]. fresh: a
 * helper just started, so what an earlier one held is handed out again at once. */
std::string pc_tasks(const std::string &id, unsigned max, bool fresh = false);
/* Where a task's bytes go: a hidden file in the store, and its final name. */
bool pc_target(const std::string &id, unsigned task, std::string &temporary,
               std::string &destination);
/* The helper downloaded a task's file (found: false when the source has none). */
void pc_downloaded(const std::string &id, unsigned task, bool found, uint64_t bytes);
/* The task's bytes were written and committed (or not). */
void pc_delivered(const std::string &id, unsigned task, bool ok, uint64_t bytes);
} // namespace ps5_scraper
