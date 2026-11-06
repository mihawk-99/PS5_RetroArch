/* Copyright (C) 2026 Mihawk; SPDX-License-Identifier: GPL-3.0-or-later */
/* The scraper (src/scraper.h says what it keeps where and why). */
#include "scraper.h"

#include "ps5_library.h"
#include "scraper_http.h"
#include "scraper_json.h"
#include "unzip.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <functional>
#include <unordered_map>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <map>
#include <memory>
#include <mutex>
#include <pthread.h>
#include <set>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

/* ScreenScraper's developer account, scrambled (tools/build-webui-daemon.sh writes the
 * header under build/, from .env); a build without one offers no ScreenScraper. */
#if defined(__has_include)
#if __has_include("screenscraper_dev.h")
#include "screenscraper_dev.h"
#endif
#endif
#ifndef PS5_SS_DEV
#define PS5_SS_DEV 0
#endif

namespace ps5_scraper
{
namespace
{
/* ---- small helpers ------------------------------------------------------------- */
std::string quote(const std::string &text)
{
    std::string out = "\"";
    for (unsigned char c : text)
    {
        if (c == '"' || c == '\\')
            out += '\\', out += char(c);
        else if (c < 32)
        {
            char escape[7];
            std::snprintf(escape, sizeof escape, "\\u%04x", c);
            out += escape;
        }
        else
            out += char(c);
    }
    return out + '"';
}
std::string now_text()
{
    const std::time_t t = std::time(nullptr);
    char text[32];
    std::strftime(text, sizeof text, "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
    return text;
}
std::string nonce()
{
    unsigned char bytes[8];
    arc4random_buf(bytes, sizeof bytes);
    char out[17];
    for (int i = 0; i < 8; ++i)
        std::snprintf(out + 2 * i, 3, "%02x", bytes[i]);
    return out;
}
bool is_file(const std::string &path)
{
    struct stat st{};
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
}
void make_folders(const std::string &path)
{
    for (size_t at = 1; (at = path.find('/', at)) != std::string::npos; ++at)
        mkdir(path.substr(0, at).c_str(), 0777);
    mkdir(path.c_str(), 0777);
}
std::string read_text(const std::string &path, size_t limit = 64 << 20)
{
    std::string out;
    if (std::FILE *file = std::fopen(path.c_str(), "rb"))
    {
        std::vector<char> buffer(65536); /* the heap: payload threads have small stacks */
        size_t got;
        while ((got = std::fread(buffer.data(), 1, buffer.size(), file)) > 0 && out.size() < limit)
            out.append(buffer.data(), got);
        std::fclose(file);
    }
    return out;
}
/* Writes a file whole or not at all: a hidden name, synced, renamed. */
bool write_atomic(const std::string &path, const std::string &text)
{
    const std::string temporary =
        path.substr(0, path.find_last_of('/') + 1) + ".partial-" + nonce();
    const int fd = open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd < 0)
        return false;
    size_t done = 0;
    while (done < text.size())
    {
        const ssize_t n = write(fd, text.data() + done, text.size() - done);
        if (n <= 0)
            break;
        done += size_t(n);
    }
    const bool ok = done == text.size() && fsync(fd) == 0;
    close(fd);
    if (!ok || rename(temporary.c_str(), path.c_str()))
    {
        unlink(temporary.c_str());
        return false;
    }
    chmod(path.c_str(), 0777);
    return true;
}
/* key = "value" lines (the metadata, the settings), with \", \\ and \n escaped. */
using Fields = std::map<std::string, std::string>;
Fields read_fields(const std::string &path)
{
    Fields out;
    const std::string text = read_text(path, 1 << 20);
    size_t start = 0;
    while (start < text.size())
    {
        size_t end = text.find('\n', start);
        if (end == std::string::npos)
            end = text.size();
        const std::string line = text.substr(start, end - start);
        start = end + 1;
        const size_t equals = line.find(" = \"");
        if (equals == std::string::npos || line.size() < equals + 5 || line.back() != '"')
            continue;
        std::string value;
        for (size_t i = equals + 4; i + 1 < line.size(); ++i)
        {
            if (line[i] == '\\' && i + 2 < line.size())
            {
                const char next = line[++i];
                value += next == 'n' ? '\n' : next;
            }
            else
                value += line[i];
        }
        out[line.substr(0, equals)] = value;
    }
    return out;
}
std::string fields_text(const Fields &fields)
{
    std::string out;
    for (const auto &field : fields)
    {
        out += field.first + " = \"";
        for (char c : field.second)
            out += c == '\n'               ? std::string("\\n")
                   : c == '"' || c == '\\' ? std::string("\\") + c
                                           : std::string(1, c);
        out += "\"\n";
    }
    return out;
}

/* ---- the store ------------------------------------------------------------------ */
/* The media types, in the order the WebUI offers them: the store's folder (named as
 * EmulationStation names its own), and what the user reads to choose. */
struct KindInfo
{
    const char *id, *folder, *name, *description;
};
const KindInfo kind_catalog[] = {
    {"cover", "covers", "Box art (front)",
     "The front of the game's box or case. The picture RetroArch shows in its playlists, "
     "EmulationStation in its game lists, and this page on each game."},
    {"backcover", "backcovers", "Box art (back)",
     "The back of the box, with its description and screenshots. Some EmulationStation "
     "themes show it."},
    {"box3d", "3dboxes", "3D box",
     "The box as a 3D picture, standing at an angle. Shown by "
     "themes that prefer it to the flat front."},
    {"screenshot", "screenshots", "Screenshot",
     "A picture of the game being played. RetroArch shows it beside the box art; "
     "EmulationStation in its details."},
    {"title", "titlescreens", "Title screen", "The game's title screen, as it starts."},
    {"logo", "marquees", "Logo",
     "The game's logo on a transparent background (a wheel or marquee). Themes show it over "
     "videos and backgrounds."},
    {"physical", "physicalmedia", "Cartridge or disc", "A picture of the cartridge, disc or card."},
    {"fanart", "fanart", "Fan art", "Large artwork some themes use as a game's background."},
    {"manual", "manuals", "Manual", "The game's printed manual, as a PDF."},
    {"video", "videos", "Video preview",
     "A short clip of the game. EmulationStation plays it in its game lists; it takes the "
     "most space of all."}};
const std::map<std::string, std::string> kind_folders = []
{
    std::map<std::string, std::string> folders;
    for (const auto &kind : kind_catalog)
        folders[kind.id] = kind.folder;
    return folders;
}();
std::string kind_name(const std::string &id)
{
    for (const auto &kind : kind_catalog)
        if (id == kind.id)
            return kind.name;
    return id;
}
/* libretro's thumbnail folders, and RetroArch's own (the same names). Logos exist for
 * some systems only. */
const std::map<std::string, std::string> libretro_folders = {{"cover", "Named_Boxarts"},
                                                             {"screenshot", "Named_Snaps"},
                                                             {"title", "Named_Titles"},
                                                             {"logo", "Named_Logos"}};

/* libretro's thumbnails over HTTPS, verified by curl against the bundled certificates
 * (Sony's SSL failed from the daemon: src/scraper_http.h). */
std::string root_path = "/app0", libretro_base = "https://thumbnails.libretro.com";

std::string library_root()
{
    return root_path + "/library";
}
/* A game's key: the shared contract's (src/ps5_library.h), as every frontend reads it. */
std::string game_key(const std::string &path)
{
    char key[512];
    return ps5_library_media_key(path.c_str(), key, sizeof key) == 0 ? key : "_";
}
bool safe_part(const std::string &part)
{
    return !part.empty() && part[0] != '.' && part.size() < 256 &&
           part.find('/') == std::string::npos && part.find('\\') == std::string::npos;
}
/* A stored media file of a kind, whatever its extension, or "". */
std::string stored_media(const std::string &system, const std::string &key, const std::string &kind)
{
    const auto folder = kind_folders.find(kind);
    if (folder == kind_folders.end())
        return "";
    const std::string base = library_root() + '/' + system + '/' + folder->second + '/' + key;
    for (const char *ext : {".png", ".jpg", ".jpeg", ".webp", ".gif", ".mp4", ".webm", ".pdf"})
        if (is_file(base + ext))
            return base + ext;
    return "";
}
std::string meta_path(const std::string &system, const std::string &key)
{
    return library_root() + '/' + system + "/metadata/" + key + ".meta";
}
/* The title's own path for a file (the playlists' /app0/...), and back. */
std::string title_path(const std::string &real)
{
    if (root_path != "/app0" && real.rfind(root_path + '/', 0) == 0)
        return "/app0" + real.substr(root_path.size());
    return real;
}
// RetroArch's thumbnail name for a label: & * / : ` < > ? \ | become _.
std::string thumbnail_name(const std::string &label)
{
    std::string out = label;
    for (char &c : out)
        if (std::strchr("&*/:`<>?\\|\"", c))
            c = '_';
    return out;
}
/* "Name (USA) [!]" -> "Name": a title without its tags. */
std::string clean_title(const std::string &name)
{
    std::string out;
    int depth = 0;
    for (char c : name)
    {
        if (c == '(' || c == '[')
            ++depth;
        else if ((c == ')' || c == ']') && depth)
            --depth;
        else if (!depth)
            out += c;
    }
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}

/* ---- the library ------------------------------------------------------------------ */
struct Game
{
    std::string system, system_name, database, path, label, crc32, key, core;
};
std::vector<Game> load_games()
{
    std::vector<Game> out;
    ps5_library library{};
    const std::string playlists = root_path + "/playlists", info = root_path + "/info",
                      cores = root_path + "/cores", content = root_path + "/content";
    std::vector<const char *> roots = {content.c_str()};
    static const char *mounts[] = {"/mnt/usb0", "/mnt/usb1", "/mnt/usb2", "/mnt/usb3", "/mnt/usb4",
                                   "/mnt/usb5", "/mnt/usb6", "/mnt/usb7", "/mnt/ext0", "/mnt/ext1"};
    if (root_path != "/app0")
        for (const char *mount : mounts)
            roots.push_back(mount);
    roots.push_back(nullptr);
    if (ps5_library_load_content_at(&library, playlists.c_str(), info.c_str(), cores.c_str(),
                                    roots.data(),
                                    root_path == "/app0" ? nullptr : root_path.c_str()) == 0)
        for (size_t i = 0; i < library.game_count; ++i)
        {
            const auto &game = library.games[i];
            const auto &system = library.systems[game.system];
            const char *database =
                system.known ? ps5_library_platform_database(system.id) : nullptr;
            const std::string path = title_path(game.path);
            out.push_back({system.id, system.name, database ? database : "", path, game.label,
                           game.crc32, game_key(path), *game.core ? game.core : system.core});
        }
    ps5_library_free(&library);
    return out;
}

/* ---- jobs ------------------------------------------------------------------------- */
enum class State
{
    pending,
    working,
    transferring, /* PC mode: identified, its downloads handed out */
    done,
    partial,   /* some of the media asked for exists at the source */
    skipped,   /* already in the store */
    ambiguous, /* candidates wait for the user */
    unmatched,
    failed
};
const char *state_name(State s)
{
    static const char *names[] = {"pending", "working",   "transferring", "done",  "partial",
                                  "skipped", "ambiguous", "unmatched",    "failed"};
    return names[int(s)];
}
State state_from(const std::string &name)
{
    for (int i = 0; i <= int(State::failed); ++i)
        if (name == state_name(State(i)))
            return State(i);
    return State::pending;
}
struct Item
{
    Game game;
    State state = State::pending;
    std::string matched, message;
    std::vector<std::string> candidates;
    unsigned found = 0, missing = 0, tasks_open = 0;
    bool started = false;         /* this job began fetching for it (an interruption may follow) */
    std::vector<std::string> got; /* the kinds this job stored for it (the recap) */
    std::map<std::string, std::string> matches; /* source: the name it knows the game by */
    std::string candidates_source;              /* whose names the candidates are */
    unsigned pass = 0;              /* the funnel's source it waits for (an index of sources) */
    std::string held, held_message; /* an earlier source's ambiguous/unmatched/failed */
    unsigned holder = 0; /* the worker holding it (1...), whatever its state says (not saved) */
};
/* Each source's name for a game, "source=name" separated by the unit separator. */
std::string matches_text(const std::map<std::string, std::string> &matches)
{
    std::string out;
    for (const auto &match : matches)
        out += (out.empty() ? "" : "\x1f") + match.first + '=' + match.second;
    return out;
}
std::map<std::string, std::string> matches_from(const std::string &text)
{
    std::map<std::string, std::string> out;
    size_t start = 0;
    while (start < text.size())
    {
        size_t end = text.find('\x1f', start);
        if (end == std::string::npos)
            end = text.size();
        const std::string entry = text.substr(start, end - start);
        const size_t equals = entry.find('=');
        if (equals != std::string::npos && equals > 0)
            out[entry.substr(0, equals)] = entry.substr(equals + 1);
        start = end + 1;
    }
    return out;
}
std::vector<std::string> candidates_from_list(const std::string &text)
{
    std::vector<std::string> out;
    size_t start = 0;
    while (start < text.size())
    {
        size_t end = text.find(',', start);
        if (end == std::string::npos)
            end = text.size();
        out.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return out;
}
void note_got(Item &item, const std::string &kind)
{
    if (std::find(item.got.begin(), item.got.end(), kind) == item.got.end())
        item.got.push_back(kind);
}
struct Task
{
    unsigned id, item;
    std::string kind, url, destination;
    /* Media is a file to store; a probe asks the source whether an address has a file
     * (a HEAD) and a fetch brings the source's own bytes (a folder's listing, a
     * database) to destination for the console to read; neither is stored. PC mode
     * sends all three to the helper: the console may have no route to the internet at
     * all, and the helper answers its questions about the source too (src/scraper.h).
     * `result`: 1 when answered (the file is there, the bytes arrived), 0 when the
     * source has nothing, -1 while no answer came. */
    enum Work
    {
        media,
        probe,
        fetch
    } work = media;
    enum
    {
        queued,
        leased,
        done
    } state = queued;
    std::time_t lease = 0;
    int result = -1;
};
struct Job
{
    std::string id, created;
    Options options;
    std::vector<Item> items;
    std::vector<Task> tasks;
    std::string state = "running"; /* running, cancelled, done, interrupted, paused */
    unsigned pass = 0;             /* the funnel's source whose pass is running */
    std::string message;
    uint64_t downloaded_files = 0, downloaded_bytes = 0, transferred_files = 0,
             transferred_bytes = 0, missing_files = 0;
    std::time_t saved = 0;
    /* Each pass of the funnel: the games its source has checked, those it gave
     * something, and how many kinds of media and details it gave. */
    struct PassCount
    {
        unsigned checked = 0, games = 0, files = 0;
    };
    std::vector<PassCount> passes;
    PassCount &pass_count(unsigned pass)
    {
        if (passes.size() <= pass)
            passes.resize(pass + 1);
        return passes[pass];
    }
};

std::mutex lock;
std::condition_variable wake;
std::map<std::string, std::shared_ptr<Job>> jobs;
std::shared_ptr<Job> active;
std::vector<pthread_t> workers;
std::atomic<bool> stopping{false}, cancelling{false};
std::map<std::string, std::vector<std::string>> indexes; /* database -> libretro names */

/* PC mode: a question for the helper, given out and leased like a download task.
 * A probe asks the source whether an address has a file (the helper sends a HEAD);
 * a fetch brings the source's own bytes (a folder's listing, a database) to
 * destination, which the caller reads and removes. The worker that asks waits here
 * for the answer: the console keeps no route to the internet of its own in PC mode
 * (src/scraper.h). Returns 1 (answered: the file is there, the bytes arrived), 0
 * (the source has nothing) or -1 with why when the job was cut short underneath;
 * a resume clears the tasks, so the question is asked again. */
int ask_helper(Job &job, unsigned item, Task::Work work, const std::string &url,
               const std::string &destination, std::string &why)
{
    std::unique_lock<std::mutex> guard(lock);
    auto queue = [&]
    {
        Task task;
        task.id = unsigned(job.tasks.size());
        task.item = item;
        task.kind = work == Task::probe ? "probe" : "fetch";
        task.url = url;
        task.destination = destination;
        task.work = work;
        job.tasks.push_back(task);
        return task.id;
    };
    unsigned id = queue();
    for (;;)
    {
        bool open = false;
        for (auto &task : job.tasks)
            if (task.id == id)
            {
                if (task.state == Task::done)
                    return task.result;
                open = true;
                break;
            }
        if (stopping || cancelling || job.state != "running")
        {
            why = cancelling ? "Cancelled." : "The job stopped.";
            return -1;
        }
        if (!open)
            id = queue(); /* resumed: the cleared tasks are asked again */
        wake.wait_for(guard, std::chrono::seconds(1));
    }
}

std::string jobs_folder()
{
    return library_root() + "/.jobs";
}
std::string escape_field(const std::string &text)
{
    std::string out;
    for (char c : text)
        out += c == '\t'   ? std::string("\\t")
               : c == '\n' ? std::string("\\n")
               : c == '\\' ? std::string("\\\\")
                           : std::string(1, c);
    return out;
}
std::string unescape_field(const std::string &text)
{
    std::string out;
    for (size_t i = 0; i < text.size(); ++i)
        if (text[i] == '\\' && i + 1 < text.size())
        {
            const char c = text[++i];
            out += c == 't' ? '\t' : c == 'n' ? '\n' : c;
        }
        else
            out += text[i];
    return out;
}
std::string kinds_text(const std::vector<std::string> &kinds)
{
    std::string out;
    for (const auto &kind : kinds)
        out += (out.empty() ? "" : ",") + kind;
    return out;
}
std::vector<std::string> comma_values(const std::string &text)
{
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= text.size())
    {
        size_t end = text.find(',', start);
        if (end == std::string::npos)
            end = text.size();
        const std::string kind = text.substr(start, end - start);
        if (!kind.empty() && std::find(out.begin(), out.end(), kind) == out.end())
            out.push_back(kind);
        start = end + 1;
    }
    return out;
}
std::vector<std::string> kinds_from(const std::string &text)
{
    auto out = comma_values(text);
    out.erase(std::remove_if(out.begin(), out.end(),
                             [](const std::string &kind) { return !kind_folders.count(kind); }),
              out.end());
    return out;
}
/* An ambiguous item's candidates in the job file, separated by the unit separator. */
std::string candidates_text(const std::vector<std::string> &candidates)
{
    std::string out;
    for (const auto &candidate : candidates)
        out += (out.empty() ? "" : "\x1f") + candidate;
    return out;
}
std::vector<std::string> candidates_from(const std::string &text)
{
    std::vector<std::string> out;
    size_t start = 0;
    while (start < text.size())
    {
        size_t end = text.find('\x1f', start);
        if (end == std::string::npos)
            end = text.size();
        out.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return out;
}
/* Saves a job (under lock); at most every 2 s unless forced. */
void save_job(Job &job, bool force)
{
    const std::time_t now = std::time(nullptr);
    if (!force && now - job.saved < 2)
        return;
    job.saved = now;
    std::string text = "PS5 RetroArch scraper job 1\n";
    text += "id\t" + job.id + "\ncreated\t" + job.created + "\nstate\t" + job.state + "\nmode\t" +
            job.options.mode + "\npass\t" + std::to_string(job.pass) + "\nsource\t" +
            job.options.source + "\nsources\t" + kinds_text(job.options.sources) + "\nkinds\t" +
            kinds_text(job.options.kinds) + "\nregion\t" + job.options.region + "\nlanguage\t" +
            job.options.language + "\noverwrite\t" + (job.options.overwrite ? "1" : "0") +
            "\ndetails\t" + (job.options.details ? "1" : "0") + "\ncounters\t" +
            std::to_string(job.downloaded_files) + ' ' + std::to_string(job.downloaded_bytes) +
            ' ' + std::to_string(job.transferred_files) + ' ' +
            std::to_string(job.transferred_bytes) + '\n';
    if (!job.passes.empty())
    {
        text += "passes\t";
        for (size_t i = 0; i < job.passes.size(); ++i)
            text += (i ? "," : "") + std::to_string(job.passes[i].checked) + ' ' +
                    std::to_string(job.passes[i].games) + ' ' + std::to_string(job.passes[i].files);
        text += '\n';
    }
    for (const auto &item : job.items)
    {
        /* An item mid-way is saved as pending: it is redone (its finished media kept). */
        const State saved = item.state == State::working || item.state == State::transferring
                                ? State::pending
                                : item.state;
        text += std::string("item\t") + state_name(saved) + '\t' + escape_field(item.game.system) +
                '\t' + escape_field(item.game.path) + '\t' + escape_field(item.game.label) + '\t' +
                escape_field(item.game.crc32) + '\t' + escape_field(item.matched) + '\t' +
                escape_field(item.message) + '\t' + (item.started ? "1" : "0") + '\t' +
                escape_field(candidates_text(item.candidates)) + '\t' +
                escape_field(kinds_text(item.got)) + '\t' +
                escape_field(matches_text(item.matches)) + '\t' +
                escape_field(item.candidates_source) + '\t' + std::to_string(item.pass) + '\t' +
                escape_field(item.held) + '\t' + escape_field(item.held_message) + '\n';
    }
    make_folders(jobs_folder());
    write_atomic(jobs_folder() + '/' + job.id + ".job", text);
}
std::shared_ptr<Job> load_job(const std::string &path, const std::vector<Game> &games)
{
    const std::string text = read_text(path);
    if (text.rfind("PS5 RetroArch scraper job 1\n", 0) != 0)
        return nullptr;
    auto job = std::make_shared<Job>();
    std::map<std::string, const Game *> by_path;
    for (const auto &game : games)
        by_path[game.path] = &game;
    size_t start = 0;
    while (start < text.size())
    {
        size_t end = text.find('\n', start);
        if (end == std::string::npos)
            end = text.size();
        std::vector<std::string> f;
        const std::string line = text.substr(start, end - start);
        size_t s = 0;
        while (s <= line.size())
        {
            size_t e = line.find('\t', s);
            if (e == std::string::npos)
                e = line.size();
            f.push_back(unescape_field(line.substr(s, e - s)));
            s = e + 1;
        }
        start = end + 1;
        if (f.size() == 2)
        {
            const auto &k = f[0], &v = f[1];
            if (k == "id")
                job->id = v;
            else if (k == "created")
                job->created = v;
            else if (k == "state")
                job->state = v;
            else if (k == "mode")
                job->options.mode = v;
            else if (k == "source")
                job->options.source = v;
            else if (k == "pass")
                job->pass = unsigned(std::strtoul(v.c_str(), nullptr, 10));
            else if (k == "sources")
                job->options.sources = candidates_from_list(v);
            else if (k == "kinds")
                job->options.kinds = kinds_from(v);
            else if (k == "region")
                job->options.region = v;
            else if (k == "language")
                job->options.language = v;
            else if (k == "details")
                job->options.details = v == "1";
            else if (k == "overwrite")
                job->options.overwrite = v == "1";
            else if (k == "passes")
                for (const auto &entry : candidates_from_list(v))
                {
                    Job::PassCount count;
                    if (std::sscanf(entry.c_str(), "%u %u %u", &count.checked, &count.games,
                                    &count.files) == 3)
                        job->passes.push_back(count);
                }
            else if (k == "counters") /* what was downloaded and sent, kept across restarts */
            {
                unsigned long long a = 0, b = 0, c = 0, d = 0;
                if (std::sscanf(v.c_str(), "%llu %llu %llu %llu", &a, &b, &c, &d) == 4)
                {
                    job->downloaded_files = a;
                    job->downloaded_bytes = b;
                    job->transferred_files = c;
                    job->transferred_bytes = d;
                }
            }
        }
        else if (f.size() >= 8 && f.size() <= 16 && f[0] == "item")
        {
            Item item;
            item.state = state_from(f[1]);
            const auto known = by_path.find(f[3]);
            if (known != by_path.end())
                item.game = *known->second;
            else
                item.game = {f[2], "", "", f[3], f[4], f[5], game_key(f[3]), ""};
            item.matched = f[6];
            item.message = f[7];
            item.started = f.size() >= 9 && f[8] == "1";
            /* The choices offered stay offered after a restart. */
            if (f.size() >= 10)
                item.candidates = candidates_from(f[9]);
            if (f.size() >= 11)
                for (const auto &kind : candidates_from_list(f[10]))
                    if (kind == "details" || kind_folders.count(kind))
                        note_got(item, kind);
            if (f.size() >= 12)
                item.matches = matches_from(f[11]);
            if (f.size() >= 13)
                item.candidates_source = f[12];
            if (f.size() >= 16)
            {
                item.pass = unsigned(std::strtoul(f[13].c_str(), nullptr, 10));
                item.held = f[14];
                item.held_message = f[15];
            }
            job->items.push_back(item);
        }
    }
    if (job->options.sources.empty())
        job->options.sources = {job->options.source};
    return job->id.empty() ? nullptr : job;
}

/* ---- the libretro source ---------------------------------------------------------- */
std::string libretro_url(const std::string &database, const std::string &kind,
                         const std::string &name)
{
    return libretro_base + '/' + url_encode(database) + '/' + libretro_folders.at(kind) + '/' +
           url_encode(name) + ".png";
}
std::string percent_decode(const std::string &text)
{
    std::string out;
    for (size_t i = 0; i < text.size(); ++i)
        if (text[i] == '%' && i + 2 < text.size())
        {
            out += char(std::strtol(text.substr(i + 1, 2).c_str(), nullptr, 16));
            i += 2;
        }
        else
            out += text[i];
    return out;
}
/* The names in an Apache-style folder listing: its .png links, decoded. */
std::vector<std::string> names_from_listing(const std::string &body)
{
    std::vector<std::string> names;
    size_t at = 0;
    while ((at = body.find("href=\"", at)) != std::string::npos)
    {
        at += 6;
        const size_t end = body.find('"', at);
        if (end == std::string::npos)
            break;
        const std::string href = body.substr(at, end - at);
        if (href.size() > 4 && href.compare(href.size() - 4, 4, ".png") == 0 &&
            href.find('/') == std::string::npos)
            names.push_back(percent_decode(href.substr(0, href.size() - 4)));
    }
    return names;
}
/* A stable hidden name for one database's listing, kept where the job files live (the
 * .jobs folder): a second fetch overwrites it, and a restart leaves nothing else. */
std::string listing_name(const std::string &database)
{
    uint64_t hash = 1469598103934665603ull;
    for (unsigned char c : database)
        hash = (hash ^ c) * 1099511628211ull;
    char text[32];
    std::snprintf(text, sizeof text, "listing-%016llx.html", static_cast<unsigned long long>(hash));
    return text;
}
/* The names libretro has covers for, from the folder's listing (once a session).
 * PC mode asks the helper for the listing: the console may have no route to the
 * source at all, and without it a game no name matches offers no candidates. */
std::mutex index_fetch_lock; /* one fetch of a listing at a time: never the same twice */
const std::vector<std::string> &libretro_index(Http &http, const std::string &database,
                                               const Job *job)
{
    std::lock_guard<std::mutex> fetching(index_fetch_lock);
    {
        std::lock_guard<std::mutex> guard(lock);
        auto it = indexes.find(database);
        if (it != indexes.end())
            return it->second;
    }
    const std::string url = libretro_base + '/' + url_encode(database) + "/Named_Boxarts/";
    std::string body;
    if (job && job->options.mode == "pc")
    {
        const std::string destination = jobs_folder() + '/' + listing_name(database);
        std::string why;
        unlink(destination.c_str());
        if (ask_helper(*const_cast<Job *>(job), 0, Task::fetch, url, destination, why) != 1)
        {
            unlink(destination.c_str());
            static const std::vector<std::string> none;
            return none;
        }
        body = read_text(destination, 32 << 20);
        unlink(destination.c_str());
    }
    else
    {
        const Response listing = http.get(url, 32 << 20, &cancelling);
        if (listing.status != 200)
        {
            static const std::vector<std::string> none;
            return none;
        }
        body = listing.body;
    }
    const std::vector<std::string> names = names_from_listing(body);
    std::lock_guard<std::mutex> guard(lock);
    return indexes[database] = names;
}
std::vector<std::string> words(const std::string &text)
{
    std::vector<std::string> out;
    std::string word;
    for (char c : clean_title(text) + ' ')
    {
        if (std::isalnum((unsigned char)c))
            word += char(std::tolower((unsigned char)c));
        else if (!word.empty())
        {
            /* "&" is no word, "and" counts for none: "Track & Field" is "Track and Field". */
            if (word != "the" && word != "a" && word != "of" && word != "and")
                out.push_back(word);
            word.clear();
        }
    }
    return out;
}
/* The closest names: shared words over all words, the region asked for first. */
std::vector<std::string> closest(const std::vector<std::string> &names, const std::string &query,
                                 const std::string &region)
{
    const auto wanted = words(query);
    if (wanted.empty())
        return {};
    const std::set<std::string> want(wanted.begin(), wanted.end());
    static const std::map<std::string, std::string> region_tags = {
        {"us", "USA"}, {"eu", "Europe"}, {"jp", "Japan"}, {"wor", "World"}};
    const auto tag = region_tags.count(region) ? region_tags.at(region) : "USA";
    std::vector<std::pair<double, std::string>> scored;
    for (const auto &name : names)
    {
        const auto have = words(name);
        if (have.empty())
            continue;
        const std::set<std::string> got(have.begin(), have.end());
        size_t shared = 0;
        for (const auto &word : want)
            shared += got.count(word);
        const double score = double(shared) / double(want.size() + got.size() - shared) +
                             (name.find(tag) != std::string::npos ? 0.01 : 0.0);
        if (score >= 0.5)
            scored.emplace_back(score, name);
    }
    std::sort(scored.begin(), scored.end(),
              [](const auto &a, const auto &b) { return a.first > b.first; });
    std::vector<std::string> out;
    for (size_t i = 0; i < scored.size() && i < 8; ++i)
        out.push_back(scored[i].second);
    return out;
}

/* The candidate whose title, tags aside, has the same words as the game's: the
 * region asked for first, else the first; "" when none has. */
std::string same_title(const std::vector<std::string> &candidates, const std::string &label,
                       const std::string &region)
{
    auto sorted_words = [](const std::string &text)
    {
        auto w = words(text);
        std::sort(w.begin(), w.end());
        return w;
    };
    const auto wanted = sorted_words(label);
    if (wanted.empty())
        return "";
    static const std::map<std::string, std::string> region_tags = {
        {"us", "USA"}, {"eu", "Europe"}, {"jp", "Japan"}, {"wor", "World"}};
    const std::string tag = region_tags.count(region) ? region_tags.at(region) : "USA";
    std::string first;
    for (const auto &candidate : candidates)
        if (sorted_words(candidate) == wanted)
        {
            if (candidate.find(tag) != std::string::npos)
                return candidate;
            if (first.empty())
                first = candidate;
        }
    return first;
}

/* ---- processing an item ---------------------------------------------------------- */
/* The media file's place in the store, with a hidden name to write it under. */
std::string media_destination(const Game &game, const std::string &kind, const char *ext)
{
    return library_root() + '/' + game.system + '/' + kind_folders.at(kind) + '/' + game.key + ext;
}
/* A game has details when a source gave its description, developer or release date. */
bool has_details(const std::string &system, const std::string &key)
{
    Fields meta = read_fields(meta_path(system, key));
    return !meta["description"].empty() || !meta["developer"].empty() || !meta["released"].empty();
}
/* The kinds the user put in place themselves (the .meta's "uploaded" list). */
std::vector<std::string> uploaded_kinds(const std::string &system, const std::string &key)
{
    return kinds_from(read_fields(meta_path(system, key))["uploaded"]);
}
/* A kind a job fetches for a game: one it lacks, or any when replacing; never one the
 * user uploaded (a scrape does not undo the user's choice). */
bool wanted(const Game &game, const std::string &kind, const Options &options)
{
    if (!options.overwrite)
        return stored_media(game.system, game.key, kind).empty();
    const auto mine = uploaded_kinds(game.system, game.key);
    return std::find(mine.begin(), mine.end(), kind) == mine.end();
}
/* What is written of a game's metadata (fields edited by hand are kept). */
std::mutex metadata_lock;
void save_meta(const Item &item, const Options &options, const Fields &details = {})
{
    std::lock_guard<std::mutex> guard(metadata_lock);
    const std::string path = meta_path(item.game.system, item.game.key);
    Fields fields = read_fields(path);
    std::set<std::string> edited;
    {
        const std::string list = fields["edited"];
        size_t start = 0;
        while (start < list.size())
        {
            size_t end = list.find(',', start);
            if (end == std::string::npos)
                end = list.size();
            edited.insert(list.substr(start, end - start));
            start = end + 1;
        }
    }
    auto set = [&](const char *field, const std::string &value)
    {
        if (value.empty() || edited.count(field))
            return;
        if (options.overwrite || fields[field].empty())
            fields[field] = value;
    };
    /* A source's details (name, description, developer...) before the fallbacks. */
    for (const auto &detail : details)
        set(detail.first.c_str(), detail.second);
    set("name", clean_title(item.matched.empty() ? item.game.label : item.matched));
    set("source", options.source);
    set("source_name", item.matched);
    fields["path"] = item.game.path;
    if (!item.game.crc32.empty())
        fields["crc32"] = item.game.crc32;
    fields["system"] = item.game.system;
    fields["scraped"] = now_text();
    for (const auto &kind : kind_folders)
    {
        const std::string file = stored_media(item.game.system, item.game.key, kind.first);
        if (!file.empty())
            fields["media." + kind.first] = file.substr(library_root().size() + 1);
    }
    make_folders(library_root() + '/' + item.game.system + "/metadata");
    write_atomic(path, fields_text(fields));
}
/* One download into the store, PS5 mode: 1 when stored, 0 when the source has none,
 * -1 on failure (why set). Retried on network trouble. */
int download(Http &http, const Game &game, const std::string &kind, const std::string &url,
             Job &job, unsigned index, std::string &why, const std::string &ext = ".png")
{
    const std::string destination = media_destination(game, kind, ext.c_str());
    make_folders(destination.substr(0, destination.find_last_of('/')));
    for (int attempt = 0; attempt < 4 && !cancelling; ++attempt)
    {
        if (attempt)
            std::this_thread::sleep_for(std::chrono::seconds(1 << attempt));
        const std::string temporary =
            destination.substr(0, destination.find_last_of('/') + 1) + ".partial-" + nonce();
        const int fd = open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (fd < 0)
        {
            why = "The console could not write to its storage.";
            return -1;
        }
        const Response response = http.get(url, 64 << 20, &cancelling, fd);
        const bool complete = response.status == 200 && response.error.empty() &&
                              !response.cancelled && !response.too_large && fsync(fd) == 0;
        close(fd);
        if (complete && rename(temporary.c_str(), destination.c_str()) == 0)
        {
            chmod(destination.c_str(), 0777);
            std::lock_guard<std::mutex> guard(lock);
            ++job.downloaded_files;
            job.downloaded_bytes += response.bytes;
            note_got(job.items[index], kind);
            return 1;
        }
        unlink(temporary.c_str());
        if (response.status == 404)
            return 0;
        if (response.cancelled)
            break;
        why = response.status ? "The server answered " + std::to_string(response.status) + '.'
                              : response.error;
        if (response.status == 429)
            std::this_thread::sleep_for(std::chrono::seconds(30));
        else if (response.status >= 400 && response.status < 500)
            return -1;
    }
    if (cancelling)
        why = "Cancelled.";
    return -1;
}
/* ---- ScreenScraper ------------------------------------------------------------------ */
std::string screenscraper_base = "https://api.screenscraper.fr/api2";
const std::map<std::string, unsigned> screenscraper_systems = {
#include "scraper_screenscraper_systems.inc"
};
/* Its media types for each of ours, in order of preference. */
const std::map<std::string, std::vector<std::string>> screenscraper_types = {
    {"cover", {"box-2D"}},        {"backcover", {"box-2D-back"}},
    {"box3d", {"box-3D"}},        {"screenshot", {"ss"}},
    {"title", {"sstitle"}},       {"logo", {"wheel-hd", "wheel"}},
    {"physical", {"support-2D"}}, {"fanart", {"fanart"}},
    {"manual", {"manuel"}},       {"video", {"video-normalized", "video"}}};

/* The developer account: built in (scrambled), or, on the host only, the tests'. */
bool dev_account(std::string &id, std::string &password)
{
#if PS5_SS_DEV
    id.clear();
    password.clear();
    for (size_t i = 0; i < sizeof ps5_ss_devid; ++i)
        id += char(ps5_ss_devid[i] ^ ps5_ss_key[i % sizeof ps5_ss_key]);
    for (size_t i = 0; i < sizeof ps5_ss_devpassword; ++i)
        password += char(ps5_ss_devpassword[i] ^ ps5_ss_key[i % sizeof ps5_ss_key]);
    return true;
#else
#ifndef __PROSPERO__
    const char *test_id = std::getenv("PS5_SCRAPER_TEST_DEVID");
    const char *test_password = std::getenv("PS5_SCRAPER_TEST_DEVPASSWORD");
    if (test_id && test_password)
    {
        id = test_id;
        password = test_password;
        return true;
    }
#endif
    id.clear();
    password.clear();
    return false;
#endif
}

bool screenscraper_ready()
{
    std::string id, password;
    return dev_account(id, password);
}

/* The user's account, kept on the console only (never returned to a browser): its own
 * folder, out of the title's 0777 repair (src/permissions_ps5.cpp), the file 0600. */
std::string account_path()
{
    return root_path + "/config/private/screenscraper.cfg";
}
Fields account_fields()
{
    return read_fields(account_path());
}
bool save_account(const Fields &fields)
{
    make_folders(root_path + "/config/private");
    chmod((root_path + "/config/private").c_str(), 0700);
    if (!write_atomic(account_path(), fields_text(fields)))
        return false;
    chmod(account_path().c_str(), 0600);
    return true;
}
std::mutex account_lock;
/* How many games ScreenScraper lets this account ask about at once (its maxthreads). */
std::atomic<unsigned> screenscraper_threads{1};

/* An API URL: the developer account, the user's when signed in, and the parameters. */
std::string screenscraper_url(const std::string &endpoint, const std::string &parameters,
                              bool with_user = true)
{
    std::string id, password;
    dev_account(id, password);
    std::string url = screenscraper_base + '/' + endpoint + "?devid=" + url_encode(id) +
                      "&devpassword=" + url_encode(password) + "&softname=PS5RetroArch&output=json";
    if (with_user)
    {
        const Fields account = account_fields();
        if (account.count("user") && account.count("password"))
            url += "&ssid=" + url_encode(account.at("user")) +
                   "&sspassword=" + url_encode(account.at("password"));
    }
    return url + parameters;
}

/* What the user's account allows and has used, from any answer that carries it. */
void note_quota(const Json &user)
{
    if (user.kind != Json::object)
        return;
    std::lock_guard<std::mutex> guard(account_lock);
    Fields account = account_fields();
    if (!account.count("user"))
        return;
    for (const char *key : {"requeststoday", "maxrequestsperday", "maxthreads", "niveau"})
        if (!user[key].str().empty())
            account[key] = user[key].str();
    if (std::atoi(account["maxthreads"].c_str()) > 0)
        screenscraper_threads = unsigned(std::atoi(account["maxthreads"].c_str()));
    save_account(account);
}

/* One API call: 1 with the answer, 0 when ScreenScraper has no such game, -1 when this
 * game failed (why), -2 when the job must wait (quota, closed API, refused account). */
int screenscraper_call(Http &http, const std::string &url, Json &out, std::string &why)
{
    for (int attempt = 0; attempt < 4 && !cancelling; ++attempt)
    {
        if (attempt)
            std::this_thread::sleep_for(std::chrono::seconds(2 << attempt));
        const Response response = http.get(url, 8 << 20, &cancelling);
        switch (response.status)
        {
        case 200:
            out = Json::parse(response.body);
            if (out["response"].kind != Json::object)
            {
                if (response.body.find("Erreur") != std::string::npos)
                    return 0; /* a French message instead of JSON: not found */
                why = "ScreenScraper's answer could not be read.";
                return -1;
            }
            note_quota(out["response"]["ssuser"]);
            return 1;
        case 404:
            return 0;
        case 400:
            why = "ScreenScraper could not read the request.";
            return -1;
        case 401:
            why = "ScreenScraper's API is closed to non-members right now. Try again later.";
            return -2;
        case 403:
            why = "ScreenScraper refused this app's developer account.";
            return -2;
        case 423:
            why = "ScreenScraper's API is closed right now (maintenance). Resume later.";
            return -2;
        case 426:
            why = "ScreenScraper no longer accepts this version of the app. Update it.";
            return -2;
        case 429:
            /* Too many requests a minute: wait, then again. */
            std::this_thread::sleep_for(std::chrono::seconds(30));
            continue;
        case 430:
            why = "Your ScreenScraper quota for today is used up. Resume the job tomorrow.";
            return -2;
        case 431:
            why = "ScreenScraper stopped answering: too many unrecognised games today. Resume "
                  "tomorrow.";
            return -2;
        default:
            why = response.status
                      ? "ScreenScraper answered " + std::to_string(response.status) + '.'
                      : response.error;
        }
    }
    if (why.empty())
        why = "ScreenScraper did not answer.";
    return cancelling ? -1 : -1;
}

/* A text in the region (names, dates) or language (descriptions) asked for, else the
 * fallbacks, else the first. */
std::string pick_text(const Json &list, const char *key, const std::vector<std::string> &order)
{
    if (list.kind != Json::array || list.items.empty())
        return "";
    for (const auto &want : order)
        for (const auto &entry : list.items)
            if (entry[key].str() == want)
                return entry["text"].str();
    return list.items.front()["text"].str();
}
std::vector<std::string> region_order(const std::string &region)
{
    std::vector<std::string> out = {region};
    for (const char *other : {"wor", "us", "eu", "jp", "ss", "cus"})
        if (region != other)
            out.push_back(other);
    return out;
}
/* "Name [12345]": a candidate's ScreenScraper game id, or "". */
std::string candidate_id(const std::string &candidate)
{
    const size_t open = candidate.rfind(" [");
    if (open == std::string::npos || candidate.back() != ']')
        return "";
    const std::string id = candidate.substr(open + 2, candidate.size() - open - 3);
    return !id.empty() && id.find_first_not_of("0123456789") == std::string::npos ? id : "";
}
/* The game's file size, read where the file is (its /app0 path is the title's folder). */
std::string file_size(const std::string &path)
{
    std::string real = path;
    if (real.rfind("/app0/", 0) == 0 && root_path != "/app0")
        real = root_path + real.substr(5);
    const size_t hash = real.find('#');
    if (hash != std::string::npos)
        real.resize(hash);
    struct stat st{};
    return stat(real.c_str(), &st) == 0 && S_ISREG(st.st_mode) ? std::to_string(st.st_size) : "";
}

/* Games by title, as "Name [id]" candidates (1, 0 for none, or a call's failure). */
int screenscraper_search(Http &http, unsigned system, const std::string &words,
                         const std::string &region, std::vector<std::string> &out, std::string &why)
{
    Json results;
    out.clear();
    const int searched = screenscraper_call(
        http,
        screenscraper_url("jeuRecherche.php", "&systemeid=" + std::to_string(system) +
                                                  "&recherche=" + url_encode(words)),
        results, why);
    if (searched <= 0)
        return searched;
    for (const auto &game : results["response"]["jeux"].items)
    {
        const std::string name = pick_text(game["noms"], "region", region_order(region));
        if (!game["id"].str().empty() && !name.empty() && out.size() < 8)
            out.push_back(name + " [" + game["id"].str() + ']');
    }
    return out.empty() ? 0 : 1;
}

void pause_job(Job &job, const std::string &why)
{
    std::lock_guard<std::mutex> guard(lock);
    if (job.state != "running")
        return;
    job.state = "paused";
    job.message = why;
    save_job(job, true);
}

void process_screenscraper(Http &http, Job &job, unsigned index, const Options &options)
{
    Item item;
    {
        std::lock_guard<std::mutex> guard(lock);
        item = job.items[index];
    }
    auto finish = [&](State state, const std::string &message)
    {
        std::lock_guard<std::mutex> guard(lock);
        auto &target = job.items[index];
        target.state = state;
        target.message = message;
        target.matched = item.matched;
        target.candidates = item.candidates;
        target.found = item.found;
        target.missing = item.missing;
        save_job(job, false);
    };
    const auto system = screenscraper_systems.find(item.game.system);
    if (system == screenscraper_systems.end())
        return finish(State::unmatched, "ScreenScraper has no system for these games.");
    std::vector<std::string> needed;
    for (const auto &kind : options.kinds)
        if (wanted(item.game, kind, options))
            needed.push_back(kind);
    const bool details_wanted =
        options.details && (options.overwrite || !has_details(item.game.system, item.game.key));
    if (needed.empty() && !details_wanted)
        return item.started ? finish(State::done, "")
                            : finish(State::skipped, "Already in the library.");
    {
        std::lock_guard<std::mutex> guard(lock);
        if (!job.items[index].started)
        {
            job.items[index].started = true;
            save_job(job, true);
        }
    }
    const std::string systemeid = "&systemeid=" + std::to_string(system->second);
    std::string why;
    Json answer;
    int found;
    const std::string chosen = candidate_id(item.matched);
    if (!chosen.empty())
        found = screenscraper_call(
            http, screenscraper_url("jeuInfos.php", systemeid + "&gameid=" + chosen), answer, why);
    else
    {
        /* Identified by the playlist's CRC (no disc is hashed again), the file's name and
         * size; then, failing that, by a search of its title. */
        std::string parameters =
            systemeid + "&romtype=rom&romnom=" +
            url_encode(item.game.path.substr(item.game.path.find_last_of('/') + 1));
        if (item.game.crc32.size() >= 8 && item.game.crc32.find("00000000") != 0)
            parameters += "&crc=" + item.game.crc32.substr(0, 8);
        const std::string size = file_size(item.game.path);
        if (!size.empty())
            parameters += "&romtaille=" + size;
        found =
            screenscraper_call(http, screenscraper_url("jeuInfos.php", parameters), answer, why);
        if (found == 0)
        {
            const int searched =
                screenscraper_search(http, system->second, clean_title(item.game.label),
                                     options.region, item.candidates, why);
            if (searched == -2)
            {
                pause_job(job, why);
                return finish(State::pending, "");
            }
            if (searched < 0)
                return finish(State::failed, why);
            const std::string exact = same_title(item.candidates, item.game.label, options.region);
            if (exact.empty())
                return finish(item.candidates.empty() ? State::unmatched : State::ambiguous,
                              item.candidates.empty()
                                  ? "Not found at ScreenScraper. Search by name or skip."
                                  : "Choose the right game, search, or skip.");
            found = screenscraper_call(
                http,
                screenscraper_url("jeuInfos.php", systemeid + "&gameid=" + candidate_id(exact)),
                answer, why);
            item.candidates.clear();
        }
    }
    if (found == -2)
    {
        pause_job(job, why);
        return finish(State::pending, "");
    }
    if (found < 0)
        return finish(State::failed, why);
    if (found == 0)
        return finish(State::unmatched, "Not found at ScreenScraper. Search by name or skip.");
    const Json &game = answer["response"]["jeu"];
    const auto regions = region_order(options.region);
    const std::vector<std::string> languages = {options.language, "en"};
    const std::string name = pick_text(game["noms"], "region", regions);
    item.matched = name + " [" + game["id"].str() + ']';
    /* Its details, for every frontend's game lists (ES-DE's fields). */
    Fields details;
    details["source_id"] = game["id"].str();
    if (details_wanted)
    {
        details["name"] = name;
        details["description"] = pick_text(game["synopsis"], "langue", languages);
        details["developer"] = game["developpeur"]["text"].str();
        details["publisher"] = game["editeur"]["text"].str();
        details["players"] = game["joueurs"]["text"].str();
        if (!game["note"]["text"].str().empty())
        {
            char rating[16];
            std::snprintf(rating, sizeof rating, "%.2f",
                          std::atof(game["note"]["text"].str().c_str()) / 20.0);
            details["rating"] = rating;
        }
        details["released"] = pick_text(game["dates"], "region", regions);
        if (!game["genres"].items.empty())
            details["genre"] = pick_text(game["genres"].items.front()["noms"], "langue", languages);
    }
    /* Its media, each in the region asked for first. */
    unsigned hits = 0, misses = 0;
    for (const auto &kind : needed)
    {
        const Json *chosen_media = nullptr;
        for (const auto &type : screenscraper_types.at(kind))
        {
            for (const auto &region : regions)
            {
                for (const auto &media : game["medias"].items)
                    if (media["type"].str() == type &&
                        (media["region"].str() == region || media["region"].str().empty()))
                    {
                        chosen_media = &media;
                        break;
                    }
                if (chosen_media)
                    break;
            }
            if (!chosen_media)
                for (const auto &media : game["medias"].items)
                    if (media["type"].str() == type)
                    {
                        chosen_media = &media;
                        break;
                    }
            if (chosen_media)
                break;
        }
        if (!chosen_media || (*chosen_media)["url"].str().empty())
        {
            ++misses;
            continue;
        }
        const std::string format = (*chosen_media)["format"].str();
        /* Its file type, as a short name of letters and digits ("png", "mp4"), else png. */
        const bool plain = !format.empty() && format.size() <= 5 &&
                           std::all_of(format.begin(), format.end(),
                                       [](char c) { return std::isalnum((unsigned char)c) != 0; });
        const std::string ext = '.' + (plain ? format : std::string("png"));
        /* The address carries the developer account: it is used here, never handed out. */
        std::string url = (*chosen_media)["url"].str();
        /* Over https only, as the API itself (a sniffer on the network reads nothing). */
        if (screenscraper_base.rfind("https://", 0) == 0 && url.rfind("http://", 0) == 0)
            url.insert(4, "s");
        const int result = download(http, item.game, kind, url, job, index, why, ext);
        if (result < 0)
            return finish(State::failed, why.empty() ? "The download failed." : why);
        (result > 0 ? hits : misses)++;
    }
    if (cancelling)
        return finish(State::pending, "");
    item.found = hits;
    item.missing = misses;
    save_meta(item, options, details);
    if (details_wanted && has_details(item.game.system, item.game.key))
    {
        std::lock_guard<std::mutex> guard(lock);
        note_got(job.items[index], "details");
    }
    finish(misses ? State::partial : State::done,
           misses ? "ScreenScraper has " + std::to_string(hits) + " of " +
                        std::to_string(hits + misses) + " media."
                  : "");
}

void process_libretro(Http &http, Job &job, unsigned index, const Options &options)
{
    Item item;
    {
        std::lock_guard<std::mutex> guard(lock);
        item = job.items[index];
    }
    std::vector<std::string> kinds, unavailable;
    for (const auto &kind : options.kinds)
        (libretro_folders.count(kind) ? kinds : unavailable).push_back(kind);
    auto finish = [&](State state, const std::string &message)
    {
        std::lock_guard<std::mutex> guard(lock);
        auto &target = job.items[index];
        target.state = state;
        target.message = message;
        target.matched = item.matched;
        target.candidates = item.candidates;
        target.found = item.found;
        target.missing = item.missing;
        save_job(job, false);
    };
    if (item.game.database.empty())
        return finish(State::unmatched, "libretro has no thumbnails for this system.");
    /* What is already stored is kept (unless overwrite): nothing to fetch for it. */
    std::vector<std::string> needed;
    for (const auto &kind : kinds)
        if (wanted(item.game, kind, options))
            needed.push_back(kind);
    if (needed.empty() && !kinds.empty())
    {
        save_meta(item, options);
        /* Stored by this very job before an interruption: done, not someone else's. */
        return item.started ? finish(State::done, "")
                            : finish(State::skipped, "Already in the library.");
    }
    {
        std::lock_guard<std::mutex> guard(lock);
        if (!job.items[index].started)
        {
            job.items[index].started = true;
            save_job(job, true);
        }
    }
    std::vector<std::string> names;
    if (!item.matched.empty())
        names.push_back(item.matched);
    else
    {
        for (const auto &name : {thumbnail_name(item.game.label), thumbnail_name(item.game.key)})
            if (!name.empty() && std::find(names.begin(), names.end(), name) == names.end())
                names.push_back(name);
    }
    std::string why;
    std::vector<std::pair<std::string, std::string>> found; /* kind, url (PC mode) */
    /* Tries names in turn: 1 when one had media, 0 when none did, -1 on failure. */
    auto attempt = [&](const std::vector<std::string> &tries)
    {
        for (const auto &name : tries)
        {
            unsigned hits = 0;
            for (const auto &kind : needed)
            {
                const std::string url = libretro_url(item.game.database, kind, name);
                const int result = options.mode == "pc"
                                       ? ask_helper(job, index, Task::probe, url, "", why)
                                       : download(http, item.game, kind, url, job, index, why);
                if (result < 0)
                    return -1;
                if (result > 0)
                {
                    ++hits;
                    found.emplace_back(kind, url);
                }
            }
            if (hits)
            {
                item.matched = name;
                item.found = hits;
                item.missing = unsigned(needed.size()) - hits;
                return 1;
            }
            found.clear();
        }
        return 0;
    };
    if (attempt(names) < 0)
        return finish(State::failed, why.empty() ? "The download failed." : why);
    if (item.matched.empty() && !cancelling)
    {
        item.candidates = closest(libretro_index(http, item.game.database, &job), item.game.label,
                                  options.region);
        /* The same title once tags are set aside ("Awesome Golf (1991)" and "Awesome Golf
         * (USA, Europe)") is the game: taken without asking, the region asked for first.
         * Only titles that differ wait for the user. */
        const std::string exact = same_title(item.candidates, item.game.label, options.region);
        if (!exact.empty() && attempt({exact}) < 0)
            return finish(State::failed, why.empty() ? "The download failed." : why);
        if (!item.matched.empty())
            item.candidates.clear();
    }
    if (cancelling)
        return finish(State::pending, "");
    if (item.matched.empty())
        return finish(item.candidates.empty() ? State::unmatched : State::ambiguous,
                      item.candidates.empty() ? "No match at libretro. Search by name or skip."
                                              : "Choose the right game, search, or skip.");
    /* A game identified by an earlier pass can find nothing left to fetch either:
     * the name it was matched by may hold none of what the source still lacks (a
     * logo it never had). There is then no task to hand out, and a transferring
     * item with no tasks waits on a helper forever (a resumed PC-mode job hung
     * exactly this way on the console, 2026-11-06): with nothing found, the item
     * finishes like any other miss. */
    if (options.mode == "pc" && !found.empty())
    {
        std::lock_guard<std::mutex> guard(lock);
        auto &target = job.items[index];
        target.state = State::transferring;
        target.matched = item.matched;
        target.found = 0;
        target.missing = item.missing;
        target.tasks_open = unsigned(found.size());
        for (const auto &entry : found)
        {
            Task task;
            task.id = unsigned(job.tasks.size());
            task.item = index;
            task.kind = entry.first;
            task.url = entry.second;
            task.destination = media_destination(item.game, entry.first, ".png");
            job.tasks.push_back(task);
        }
        target.message = "Waiting for the PC helper.";
        wake.notify_all();
        return;
    }
    save_meta(item, options);
    std::string note = item.missing ? "libretro has " + std::to_string(item.found) + " of " +
                                          std::to_string(item.found + item.missing) + " images."
                                    : "";
    if (!unavailable.empty())
    {
        std::string names;
        for (const auto &kind : unavailable)
            names += (names.empty() ? "" : ", ") + kind_name(kind);
        note += (note.empty() ? "" : " ") + std::string("Not at libretro: ") + names + '.';
    }
    finish(item.missing || !unavailable.empty() ? State::partial : State::done, note);
}

/* ---- the chain of sources ------------------------------------------------------------ */
std::string source_name(const std::string &source)
{
    return source == "screenscraper" ? "ScreenScraper"
           : source == "launchbox"   ? "LaunchBox"
           : source == "libretro"    ? "libretro"
                                     : source;
}
bool source_offers(const std::string &source, const std::string &kind)
{
    if (kind == "details")
        return source != "libretro";
    if (source == "libretro")
        return libretro_folders.count(kind) > 0;
    if (source == "launchbox")
        return kind == "cover" || kind == "backcover" || kind == "box3d" || kind == "screenshot" ||
               kind == "title" || kind == "logo" || kind == "physical" || kind == "fanart";
    return screenscraper_types.count(kind) > 0;
}
bool holds(const std::vector<std::string> &list, const std::string &value)
{
    return std::find(list.begin(), list.end(), value) != list.end();
}
/* ---- LaunchBox Games Database ---------------------------------------------------------- */
/* No account and no API: the whole database is one download (Metadata.zip, about 110 MB:
 * games, alternate names, images, and MAME's romset names). The console fetches it once
 * a month and keeps a small index a platform (library/.launchbox/<platform>.index); the
 * images are public files on its image server. */
std::string launchbox_metadata = "https://gamesdb.launchbox-app.com/Metadata.zip";
std::string launchbox_images = "https://images.launchbox-app.com";
/* The library's systems as LaunchBox names its platforms, in the order they are asked. */
const std::map<std::string, std::vector<std::string>> launchbox_platforms = {
    {"3do", {"3DO Interactive Multiplayer"}},
    {"amiga", {"Commodore Amiga"}},
    {"amigacd32", {"Commodore Amiga CD32"}},
    {"amstradcpc", {"Amstrad CPC"}},
    {"arcade", {"Arcade"}},
    {"atari2600", {"Atari 2600"}},
    {"atari5200", {"Atari 5200"}},
    {"atari7800", {"Atari 7800"}},
    {"atari800", {"Atari 800"}},
    {"atarijaguar", {"Atari Jaguar"}},
    {"atarijaguarcd", {"Atari Jaguar CD"}},
    {"atarilynx", {"Atari Lynx"}},
    {"atarist", {"Atari ST"}},
    {"atomiswave", {"Sammy Atomiswave", "Arcade"}},
    {"c64", {"Commodore 64"}},
    {"cdimono1", {"Philips CD-i"}},
    {"channelf", {"Fairchild Channel F"}},
    {"colecovision", {"ColecoVision"}},
    {"cps", {"Arcade"}},
    {"dos", {"MS-DOS"}},
    {"dreamcast", {"Sega Dreamcast"}},
    {"famicom", {"Nintendo Entertainment System"}},
    {"fbneo", {"Arcade", "SNK Neo Geo MVS"}},
    {"fds", {"Nintendo Famicom Disk System"}},
    {"fmtowns", {"Fujitsu FM Towns Marty"}},
    {"gameandwatch", {"Nintendo Game & Watch"}},
    {"gamegear", {"Sega Game Gear"}},
    {"gb", {"Nintendo Game Boy"}},
    {"gba", {"Nintendo Game Boy Advance"}},
    {"gbc", {"Nintendo Game Boy Color"}},
    {"gc", {"Nintendo GameCube"}},
    {"genesis", {"Sega Genesis"}},
    {"intellivision", {"Mattel Intellivision"}},
    {"mame", {"Arcade"}},
    {"mastersystem", {"Sega Master System"}},
    {"megadrive", {"Sega Genesis"}},
    {"msx", {"Microsoft MSX"}},
    {"msx2", {"Microsoft MSX2"}},
    {"n3ds", {"Nintendo 3DS"}},
    {"n64", {"Nintendo 64"}},
    {"naomi", {"Sega Naomi", "Arcade"}},
    {"nds", {"Nintendo DS"}},
    {"neogeo", {"SNK Neo Geo MVS", "SNK Neo Geo AES", "Arcade"}},
    {"neogeocd", {"SNK Neo Geo CD"}},
    {"nes", {"Nintendo Entertainment System"}},
    {"ngp", {"SNK Neo Geo Pocket"}},
    {"ngpc", {"SNK Neo Geo Pocket Color"}},
    {"odyssey2", {"Magnavox Odyssey 2"}},
    {"openbor", {"OpenBOR"}},
    {"pc88", {"NEC PC-8801"}},
    {"pc98", {"NEC PC-9801"}},
    {"pcengine", {"NEC TurboGrafx-16"}},
    {"pcenginecd", {"NEC TurboGrafx-CD"}},
    {"pcfx", {"NEC PC-FX"}},
    {"pico8", {"PICO-8"}},
    {"plus4", {"Commodore Plus 4"}},
    {"pokemini", {"Nintendo Pokemon Mini"}},
    {"ps2", {"Sony Playstation 2"}},
    {"ps3", {"Sony Playstation 3"}},
    {"psp", {"Sony PSP"}},
    {"psvita", {"Sony Playstation Vita"}},
    {"psx", {"Sony Playstation"}},
    {"satellaview", {"Nintendo Satellaview"}},
    {"saturn", {"Sega Saturn"}},
    {"scummvm", {"ScummVM"}},
    {"sega32x", {"Sega 32X"}},
    {"segacd", {"Sega CD"}},
    {"segapico", {"Sega Pico"}},
    {"snes", {"Super Nintendo Entertainment System"}},
    {"supergrafx", {"PC Engine SuperGrafx"}},
    {"vectrex", {"GCE Vectrex"}},
    {"vic20", {"Commodore VIC-20"}},
    {"virtualboy", {"Nintendo Virtual Boy"}},
    {"wii", {"Nintendo Wii"}},
    {"wiiu", {"Nintendo Wii U"}},
    {"wonderswan", {"WonderSwan"}},
    {"wonderswancolor", {"WonderSwan Color"}},
    {"x1", {"Sharp X1"}},
    {"x68000", {"Sharp X68000"}},
    {"xbox", {"Microsoft Xbox"}},
    {"zxspectrum", {"Sinclair ZX Spectrum"}},
};
/* Systems whose games are romsets ("runner3"): their titles come from MAME's names. */
bool arcade_system(const std::string &system)
{
    static const std::set<std::string> arcade = {"arcade", "mame",  "fbneo",     "cps",
                                                 "neogeo", "naomi", "atomiswave"};
    return arcade.count(system) > 0;
}
/* Its image types for each of our kinds, in order of preference. */
const std::map<std::string, std::vector<std::string>> launchbox_types = {
    {"cover", {"Box - Front", "Box - Front - Reconstructed", "Fanart - Box - Front"}},
    {"backcover", {"Box - Back", "Box - Back - Reconstructed", "Fanart - Box - Back"}},
    {"box3d", {"Box - 3D"}},
    {"screenshot", {"Screenshot - Gameplay"}},
    {"title", {"Screenshot - Game Title"}},
    {"logo", {"Clear Logo"}},
    {"physical", {"Disc", "Cart - Front", "Fanart - Disc", "Fanart - Cart - Front"}},
    {"fanart", {"Fanart - Background"}},
};
std::vector<std::string> launchbox_regions(const std::string &region)
{
    if (region == "eu")
        return {"Europe", "United Kingdom", "Germany", "France", "Spain", "Italy", "World", ""};
    if (region == "jp")
        return {"Japan", "World", ""};
    if (region == "wor")
        return {"World", "", "North America", "United States", "Europe", "Japan"};
    return {"North America", "United States", "World", "", "Canada"};
}
std::string launchbox_folder()
{
    return library_root() + "/.launchbox";
}
std::string platform_file(const std::string &platform)
{
    std::string name;
    for (char c : platform)
        name += std::isalnum((unsigned char)c) ? char(std::tolower((unsigned char)c)) : '_';
    return launchbox_folder() + '/' + name + ".index";
}

/* A streaming reader of the database's XML: records two levels down (<Game>, <GameImage>,
 * <GameAlternateName>, <MameFile>), their fields one level further; nothing kept but the
 * record being read. */
struct XmlRecords
{
    std::function<void(const std::string &, const Fields &)> on_record;
    int depth = 0;
    bool in_tag = false;
    std::string tag, text, record;
    Fields fields;
    static std::string decode(const std::string &s)
    {
        std::string out;
        for (size_t i = 0; i < s.size(); ++i)
        {
            if (s[i] != '&')
            {
                out += s[i];
                continue;
            }
            const size_t end = s.find(';', i);
            if (end == std::string::npos || end - i > 10)
            {
                out += s[i];
                continue;
            }
            const std::string entity = s.substr(i + 1, end - i - 1);
            if (entity == "amp")
                out += '&';
            else if (entity == "lt")
                out += '<';
            else if (entity == "gt")
                out += '>';
            else if (entity == "quot")
                out += '"';
            else if (entity == "apos")
                out += '\'';
            else if (!entity.empty() && entity[0] == '#')
            {
                const unsigned code =
                    unsigned(entity.size() > 1 && (entity[1] == 'x' || entity[1] == 'X')
                                 ? std::strtoul(entity.c_str() + 2, nullptr, 16)
                                 : std::strtoul(entity.c_str() + 1, nullptr, 10));
                if (code < 0x80)
                    out += char(code);
                else if (code < 0x800)
                    out += char(0xC0 | code >> 6), out += char(0x80 | (code & 0x3F));
                else if (code < 0x10000)
                    out += char(0xE0 | code >> 12), out += char(0x80 | (code >> 6 & 0x3F)),
                        out += char(0x80 | (code & 0x3F));
                else
                    out += char(0xF0 | code >> 18), out += char(0x80 | (code >> 12 & 0x3F)),
                        out += char(0x80 | (code >> 6 & 0x3F)), out += char(0x80 | (code & 0x3F));
            }
            else
            {
                out += s.substr(i, end - i + 1);
            }
            i = end;
        }
        return out;
    }
    void element(const std::string &raw)
    {
        if (raw.empty() || raw[0] == '?' || raw[0] == '!')
            return;
        const bool closing = raw[0] == '/', empty = raw.back() == '/';
        std::string name = raw.substr(closing ? 1 : 0);
        if (empty)
            name.pop_back();
        name = name.substr(0, name.find_first_of(" \t\r\n"));
        if (closing)
        {
            if (depth == 3)
                fields[name] = decode(text);
            else if (depth == 2 && on_record)
                on_record(record, fields);
            --depth;
            return;
        }
        ++depth;
        text.clear();
        if (depth == 2)
        {
            record = name;
            fields.clear();
        }
        if (empty)
        {
            if (depth == 3)
                fields[name] = "";
            --depth;
        }
    }
    void feed(const char *data, size_t size)
    {
        for (size_t i = 0; i < size; ++i)
        {
            const char c = data[i];
            if (in_tag)
            {
                if (c == '>')
                {
                    in_tag = false;
                    element(tag);
                    tag.clear();
                }
                else
                    tag += c;
            }
            else if (c == '<')
                in_tag = true;
            else if (depth == 3)
                text += c;
        }
    }
};
/* Reads one file of the zip through the reader; false when it is not there. */
bool read_zipped(const std::string &zip, const char *name, XmlRecords &reader,
                 const std::atomic<bool> &cancel)
{
    unzFile file = unzOpen(zip.c_str());
    if (!file)
        return false;
    bool ok = unzLocateFile(file, name, 0) == UNZ_OK && unzOpenCurrentFile(file) == UNZ_OK;
    if (ok)
    {
        std::vector<char> buffer(1 << 20);
        int got;
        while ((got = unzReadCurrentFile(file, buffer.data(), unsigned(buffer.size()))) > 0 &&
               !cancel)
            reader.feed(buffer.data(), size_t(got));
        ok = got == 0;
        unzCloseCurrentFile(file);
    }
    unzClose(file);
    return ok;
}

std::mutex launchbox_lock;
/* The index, built from a fresh database when it is missing or a month old. */
bool launchbox_prepare(Http &http, Job &job, std::string &why,
                       const std::atomic<bool> &cancel = cancelling)
{
    std::lock_guard<std::mutex> guard(launchbox_lock);
    const std::string folder = launchbox_folder(), done = folder + "/index.done";
    struct stat st{};
    if (stat(done.c_str(), &st) == 0 && std::time(nullptr) - st.st_mtime < 30 * 24 * 3600)
        return true;
    /* Another worker of this job failed while this one waited: it waits for the resume. */
    {
        std::lock_guard<std::mutex> job_guard(lock);
        if (job.state != "running")
            return false;
    }
    auto say = [&](const std::string &message)
    {
        std::lock_guard<std::mutex> job_guard(lock);
        job.message = message;
    };
    make_folders(folder);
    const std::string zip = folder + "/Metadata.zip", partial = folder + "/.partial-metadata";
    say("Getting the LaunchBox database (about 110 MB, once a month)…");
    if (job.options.mode == "pc")
    {
        /* The console may have no route to LaunchBox at all: the helper on the PC
         * fetches the database and sends the bytes over, as it does every media file
         * (src/scraper.h). It lands on its final name; the reading below is shared. */
        std::string reason;
        unlink(zip.c_str());
        if (ask_helper(job, 0, Task::fetch, launchbox_metadata, zip, reason) != 1 || !is_file(zip))
        {
            unlink(zip.c_str());
            why = "LaunchBox could not be reached through the PC helper.";
            return false;
        }
    }
    else
    {
        const int fd = open(partial.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (fd < 0)
        {
            why = "The console could not write to its storage.";
            return false;
        }
        const Response response = http.get(launchbox_metadata, uint64_t(1) << 30, &cancel, fd);
        const bool complete = response.status == 200 && response.error.empty() &&
                              !response.cancelled && fsync(fd) == 0;
        close(fd);
        if (!complete || rename(partial.c_str(), zip.c_str()) != 0)
        {
            unlink(partial.c_str());
            why = response.status ? "LaunchBox answered " + std::to_string(response.status) + '.'
                                  : "LaunchBox could not be reached: " + response.error;
            return false;
        }
    }
    say("Reading the LaunchBox database…");
    /* The platforms the library can have, a file each: G (a game), A (another name of
     * it), I (an image) lines, tab-separated. */
    std::map<std::string, std::FILE *> files;
    for (const auto &entry : launchbox_platforms)
        for (const auto &platform : entry.second)
            if (!files.count(platform))
                files[platform] = std::fopen((platform_file(platform) + ".new").c_str(), "w");
    std::unordered_map<std::string, std::FILE *> by_id; /* a game's id: its platform's file */
    auto line = [](std::FILE *file, std::initializer_list<std::string> parts)
    {
        bool first = true;
        for (const auto &part : parts)
        {
            if (!first)
                std::fputc('\t', file);
            std::fputs(escape_field(part).c_str(), file);
            first = false;
        }
        std::fputc('\n', file);
    };
    XmlRecords games;
    games.on_record = [&](const std::string &record, const Fields &f)
    {
        auto get = [&](const char *key)
        {
            auto it = f.find(key);
            return it == f.end() ? std::string() : it->second;
        };
        if (record == "Game")
        {
            auto file = files.find(get("Platform"));
            if (file == files.end() || !file->second)
                return;
            by_id[get("DatabaseID")] = file->second;
            line(file->second, {"G", get("DatabaseID"), get("Name"),
                                get("ReleaseDate").empty() ? get("ReleaseYear")
                                                           : get("ReleaseDate").substr(0, 10),
                                get("Developer"), get("Publisher"), get("Genres"),
                                get("MaxPlayers"), get("CommunityRating"), get("Overview")});
        }
        else if (record == "GameAlternateName")
        {
            auto file = by_id.find(get("DatabaseID"));
            if (file != by_id.end())
                line(file->second, {"A", get("DatabaseID"), get("AlternateName")});
        }
        else if (record == "GameImage")
        {
            auto file = by_id.find(get("DatabaseID"));
            if (file == by_id.end())
                return;
            const std::string type = get("Type");
            for (const auto &kind : launchbox_types)
                if (holds(kind.second, type))
                {
                    line(file->second,
                         {"I", get("DatabaseID"), type, get("Region"), get("FileName")});
                    break;
                }
        }
    };
    bool ok = read_zipped(zip, "Metadata.xml", games, cancel);
    for (auto &file : files)
        if (file.second)
            ok = std::fclose(file.second) == 0 && ok;
    /* MAME's romset names, for arcade games: "runner3" is "Run and Gun 3". */
    std::FILE *names = std::fopen((folder + "/arcade.names.new").c_str(), "w");
    XmlRecords mame;
    mame.on_record = [&](const std::string &record, const Fields &f)
    {
        if (record == "MameFile" && names && f.count("FileName") && f.count("Name"))
            line(names, {f.at("FileName"), f.at("Name")});
    };
    ok = names && read_zipped(zip, "Mame.xml", mame, cancel) && ok;
    if (names)
        ok = std::fclose(names) == 0 && ok;
    unlink(zip.c_str());
    if (!ok || cancel)
    {
        why = cancel ? "Cancelled." : "The LaunchBox database could not be read.";
        return false;
    }
    for (const auto &file : files)
        rename((platform_file(file.first) + ".new").c_str(), platform_file(file.first).c_str());
    rename((folder + "/arcade.names.new").c_str(), (folder + "/arcade.names").c_str());
    write_atomic(done, now_text() + '\n');
    say("");
    return true;
}

/* A platform's index in memory, read once per server. */
struct LaunchboxGame
{
    std::string id, name, released, developer, publisher, genres, players, rating, overview;
};
struct LaunchboxImage
{
    std::string type, region, file;
};
struct LaunchboxIndex
{
    std::vector<LaunchboxGame> games;
    std::unordered_map<std::string, size_t> by_id;
    std::unordered_map<std::string, std::vector<std::string>> by_title; /* words: ids */
    std::unordered_map<std::string, std::vector<LaunchboxImage>> images;
    std::vector<std::string> names; /* "Name [id]", for closest() */
};
std::string title_key(const std::string &title)
{
    std::string key;
    for (const auto &word : words(title))
        key += (key.empty() ? "" : " ") + word;
    return key;
}
std::map<std::string, std::shared_ptr<LaunchboxIndex>> launchbox_indexes;
std::shared_ptr<LaunchboxIndex> launchbox_index(const std::string &platform)
{
    std::lock_guard<std::mutex> guard(launchbox_lock);
    auto found = launchbox_indexes.find(platform);
    if (found != launchbox_indexes.end())
        return found->second;
    auto index = std::make_shared<LaunchboxIndex>();
    if (std::FILE *file = std::fopen(platform_file(platform).c_str(), "r"))
    {
        std::string text;
        char buffer[1 << 16];
        size_t got;
        while ((got = std::fread(buffer, 1, sizeof buffer, file)) > 0)
            text.append(buffer, got);
        std::fclose(file);
        size_t start = 0;
        while (start < text.size())
        {
            size_t end = text.find('\n', start);
            if (end == std::string::npos)
                end = text.size();
            std::vector<std::string> f;
            for (size_t s = start; s <= end;)
            {
                size_t e = text.find('\t', s);
                if (e == std::string::npos || e > end)
                    e = end;
                f.push_back(unescape_field(text.substr(s, e - s)));
                s = e + 1;
            }
            start = end + 1;
            if (f[0] == "G" && f.size() >= 10)
            {
                index->by_id[f[1]] = index->games.size();
                index->games.push_back({f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[9]});
                index->by_title[title_key(f[2])].push_back(f[1]);
                index->names.push_back(f[2] + " [" + f[1] + ']');
            }
            else if (f[0] == "A" && f.size() >= 3)
                index->by_title[title_key(f[2])].push_back(f[1]);
            else if (f[0] == "I" && f.size() >= 5)
                index->images[f[1]].push_back({f[2], f[3], f[4]});
        }
    }
    launchbox_indexes[platform] = index;
    return index;
}
std::map<std::string, std::string> arcade_names;
std::string arcade_name(const std::string &romset)
{
    std::lock_guard<std::mutex> guard(launchbox_lock);
    if (arcade_names.empty())
    {
        const std::string text = read_text(launchbox_folder() + "/arcade.names");
        size_t start = 0;
        while (start < text.size())
        {
            size_t end = text.find('\n', start);
            if (end == std::string::npos)
                end = text.size();
            const std::string row = text.substr(start, end - start);
            const size_t tab = row.find('\t');
            if (tab != std::string::npos)
                arcade_names[unescape_field(row.substr(0, tab))] =
                    unescape_field(row.substr(tab + 1));
            start = end + 1;
        }
    }
    auto found = arcade_names.find(romset);
    return found == arcade_names.end() ? "" : found->second;
}
/* The title a game is known by: an arcade romset's name, else its label. */
std::string launchbox_title(const Game &game)
{
    if (arcade_system(game.system))
    {
        const std::string named = arcade_name(game.key);
        if (!named.empty())
            return named;
    }
    return game.label;
}
/* Games by name in the system's platforms, as "Name [id]" candidates. */
std::vector<std::string> launchbox_search(const std::string &system, const std::string &words_of,
                                          const std::string &region)
{
    std::vector<std::string> out;
    const auto platforms = launchbox_platforms.find(system);
    if (platforms == launchbox_platforms.end())
        return out;
    for (const auto &platform : platforms->second)
        for (const auto &name : closest(launchbox_index(platform)->names, words_of, region))
            if (!holds(out, name) && out.size() < 8)
                out.push_back(name);
    return out;
}

void process_launchbox(Http &http, Job &job, unsigned index, const Options &options)
{
    Item item;
    {
        std::lock_guard<std::mutex> guard(lock);
        item = job.items[index];
    }
    auto finish = [&](State state, const std::string &message)
    {
        std::lock_guard<std::mutex> guard(lock);
        auto &target = job.items[index];
        target.state = state;
        target.message = message;
        target.matched = item.matched;
        target.candidates = item.candidates;
        target.found = item.found;
        target.missing = item.missing;
        save_job(job, false);
    };
    const auto platforms = launchbox_platforms.find(item.game.system);
    if (platforms == launchbox_platforms.end())
        return finish(State::unmatched, "LaunchBox has no platform for these games.");
    std::vector<std::string> needed;
    for (const auto &kind : options.kinds)
        if (launchbox_types.count(kind) && wanted(item.game, kind, options))
            needed.push_back(kind);
    const bool details_wanted =
        options.details && (options.overwrite || !has_details(item.game.system, item.game.key));
    if (needed.empty() && !details_wanted)
        return item.started ? finish(State::done, "")
                            : finish(State::skipped, "Already in the library.");
    std::string why;
    if (!launchbox_prepare(http, job, why))
    {
        if (!why.empty())
            pause_job(job, why + " Resume to try again.");
        return finish(State::pending, "");
    }
    {
        std::lock_guard<std::mutex> guard(lock);
        if (!job.items[index].started)
        {
            job.items[index].started = true;
            save_job(job, true);
        }
    }
    /* The game: one chosen by the user, else its title (an alternate name counts), else
     * the closest names, the same title taken unasked. */
    const LaunchboxGame *game = nullptr;
    std::shared_ptr<LaunchboxIndex> holder;
    auto find_id = [&](const std::string &id)
    {
        for (const auto &platform : platforms->second)
        {
            auto in = launchbox_index(platform);
            auto at = in->by_id.find(id);
            if (at != in->by_id.end())
            {
                holder = in;
                game = &in->games[at->second];
                return;
            }
        }
    };
    const Fields stored = read_fields(meta_path(item.game.system, item.game.key));
    const std::string chosen = stored.count("launchbox_id") && !stored.at("launchbox_id").empty()
                                   ? stored.at("launchbox_id")
                                   : candidate_id(item.matched);
    if (!chosen.empty())
        find_id(chosen);
    else
    {
        const std::string title = launchbox_title(item.game);
        const std::string key = title_key(title);
        for (const auto &platform : platforms->second)
        {
            auto in = launchbox_index(platform);
            auto hit = in->by_title.find(key);
            if (hit != in->by_title.end() && !hit->second.empty())
            {
                find_id(hit->second.front());
                break;
            }
        }
        if (!game)
        {
            item.candidates =
                launchbox_search(item.game.system, clean_title(title), options.region);
            const std::string exact = same_title(item.candidates, title, options.region);
            if (exact.empty())
                return finish(item.candidates.empty() ? State::unmatched : State::ambiguous,
                              item.candidates.empty()
                                  ? "Not found at LaunchBox. Search by name or skip."
                                  : "Choose the right game, search, or skip.");
            find_id(candidate_id(exact));
            item.candidates.clear();
        }
    }
    if (!game)
        return finish(State::unmatched, "Not found at LaunchBox. Search by name or skip.");
    item.matched = game->name + " [" + game->id + ']';
    Fields details;
    details["source_id"] = game->id;
    if (details_wanted)
    {
        details["name"] = game->name;
        details["description"] = game->overview;
        details["developer"] = game->developer;
        details["publisher"] = game->publisher;
        details["released"] = game->released;
        std::string genre = game->genres;
        for (size_t at; (at = genre.find(';')) != std::string::npos;)
            genre.replace(at, 1, ",");
        details["genre"] = genre;
        details["players"] = game->players;
        if (!game->rating.empty())
        {
            char rating[16];
            std::snprintf(rating, sizeof rating, "%.2f", std::atof(game->rating.c_str()) / 5.0);
            details["rating"] = rating;
        }
    }
    const auto &images = holder->images[game->id];
    const auto regions = launchbox_regions(options.region);
    unsigned hits = 0, misses = 0;
    std::vector<std::pair<std::string, std::string>> tasks; /* kind, file (PC mode) */
    for (const auto &kind : needed)
    {
        const LaunchboxImage *pick = nullptr;
        for (const auto &type : launchbox_types.at(kind))
        {
            for (const auto &region : regions)
            {
                for (const auto &image : images)
                    if (image.type == type && image.region == region)
                    {
                        pick = &image;
                        break;
                    }
                if (pick)
                    break;
            }
            for (const auto &image : images)
                if (!pick && image.type == type)
                    pick = &image;
            if (pick)
                break;
        }
        const std::string ext = pick ? pick->file.substr(pick->file.find_last_of('.')) : "";
        if (!pick || (ext != ".png" && ext != ".jpg" && ext != ".jpeg"))
        {
            ++misses;
            continue;
        }
        const std::string url = launchbox_images + '/' + url_encode(pick->file);
        if (options.mode == "pc")
        {
            tasks.emplace_back(kind, url);
            continue;
        }
        const int result = download(http, item.game, kind, url, job, index, why, ext);
        if (result < 0)
            return finish(State::failed, why.empty() ? "The download failed." : why);
        (result > 0 ? hits : misses)++;
    }
    if (cancelling)
        return finish(State::pending, "");
    save_meta(item, options, details);
    if (details_wanted && has_details(item.game.system, item.game.key))
    {
        std::lock_guard<std::mutex> guard(lock);
        note_got(job.items[index], "details");
    }
    if (options.mode == "pc" && !tasks.empty())
    {
        std::lock_guard<std::mutex> guard(lock);
        auto &target = job.items[index];
        target.state = State::transferring;
        target.matched = item.matched;
        target.found = 0;
        target.missing = misses;
        target.tasks_open = unsigned(tasks.size());
        for (const auto &entry : tasks)
        {
            Task task;
            task.id = unsigned(job.tasks.size());
            task.item = index;
            task.kind = entry.first;
            task.url = entry.second;
            const std::string ext = entry.second.substr(entry.second.find_last_of('.'));
            task.destination = media_destination(item.game, entry.first, ext.c_str());
            job.tasks.push_back(task);
        }
        target.message = "Waiting for the PC helper.";
        wake.notify_all();
        return;
    }
    item.found = hits;
    item.missing = misses;
    finish(misses ? State::partial : State::done,
           misses ? "LaunchBox has " + std::to_string(hits) + " of " +
                        std::to_string(hits + misses) + " media."
                  : "");
}

/* ScreenScraper lets an account ask about so many games at once (its maxthreads): a
 * game waits for a slot only while ScreenScraper works on it. */
std::mutex screenscraper_slots_lock;
std::condition_variable screenscraper_slot_free;
unsigned screenscraper_busy = 0;
struct ScreenscraperSlot
{
    ScreenscraperSlot()
    {
        std::unique_lock<std::mutex> guard(screenscraper_slots_lock);
        screenscraper_slot_free.wait(
            guard, [] { return screenscraper_busy < std::max(1u, screenscraper_threads.load()); });
        ++screenscraper_busy;
    }
    ~ScreenscraperSlot()
    {
        std::lock_guard<std::mutex> guard(screenscraper_slots_lock);
        --screenscraper_busy;
        screenscraper_slot_free.notify_all();
    }
};
void process_one(Http &http, Job &job, unsigned index, const Options &options)
{
    if (options.source == "screenscraper")
    {
        ScreenscraperSlot slot;
        {
            /* Paused or cancelled while it waited for its turn: nothing asked. */
            std::lock_guard<std::mutex> guard(lock);
            if (cancelling || job.state != "running")
            {
                job.items[index].state = State::pending;
                return;
            }
        }
        process_screenscraper(http, job, index, options);
    }
    else if (options.source == "launchbox")
        process_launchbox(http, job, index, options);
    else
        process_libretro(http, job, index, options);
}

/* The funnel: the job's sources in passes. The first source goes through every game as
 * if no other came after it; once its pass is over, the next one gets only the games
 * still missing something, and only for what they miss; and so on. A limited source
 * (ScreenScraper's quota and threads) never holds up the ones above it. Each source keeps
 * its own name for a game; a game no source could tell waits for the user with the first
 * source's choices (a choice runs that source again, then the ones below it). */
void funnel_finish(Job &job, unsigned index, const Options &options,
                   const std::vector<std::string> &remaining, bool details_left)
{
    auto &target = job.items[index];
    target.matched.clear();
    for (const auto &source : options.sources)
        if (target.matches.count(source))
        {
            target.matched = target.matches.at(source);
            break;
        }
    std::string sources;
    for (size_t i = 0; i < options.sources.size(); ++i)
        sources += (i ? (i + 1 == options.sources.size() ? " or " : ", ") : "") +
                   source_name(options.sources[i]);
    const std::string held = target.held;
    if (held != "ambiguous")
        target.candidates.clear();
    if (remaining.empty() && !details_left)
    {
        target.state = State::done;
        target.message.clear();
        target.candidates.clear();
        target.candidates_source.clear();
    }
    else if (held == "ambiguous")
    {
        target.state = State::ambiguous;
        target.message =
            (target.got.empty() ? source_name(target.candidates_source) + " offers these"
                                : "Some found; for the rest, " +
                                      source_name(target.candidates_source) + " offers these") +
            ": choose, search, or skip.";
    }
    else if (!target.got.empty())
    {
        std::string names;
        for (const auto &kind : remaining)
            names += (names.empty() ? "" : ", ") + kind_name(kind);
        if (details_left)
            names += std::string(names.empty() ? "" : ", ") + "Game details";
        target.state = State::partial;
        target.message = "Not found at " + sources + ": " + names + '.';
    }
    else if (held == "failed")
    {
        target.state = State::failed;
        target.message = target.held_message;
    }
    else
    {
        target.state = State::unmatched;
        target.message = "Not found at " + sources + ". Search by name or skip.";
    }
    target.held.clear();
    target.held_message.clear();
}

void process(Http &http, Job &job, unsigned index)
{
    Item item;
    Options options;
    {
        std::lock_guard<std::mutex> guard(lock);
        item = job.items[index];
        options = job.options;
    }
    if (options.sources.size() <= 1)
    {
        if (!options.sources.empty())
            options.source = options.sources.front();
        return process_one(http, job, index, options);
    }
    const unsigned pass = std::min<unsigned>(item.pass, unsigned(options.sources.size()) - 1);
    /* What this game still lacks of what the job asks (a kind found earlier in this job
     * is not asked again). */
    auto lacking = [&](const Item &now, std::vector<std::string> &remaining, bool &details_left)
    {
        remaining.clear();
        for (const auto &kind : options.kinds)
            if (!holds(now.got, kind) && wanted(now.game, kind, options))
                remaining.push_back(kind);
        details_left = options.details && !holds(now.got, "details") &&
                       (options.overwrite || !has_details(now.game.system, now.game.key));
    };
    std::vector<std::string> remaining;
    bool details_left;
    lacking(item, remaining, details_left);
    if (remaining.empty() && !details_left)
    {
        std::lock_guard<std::mutex> guard(lock);
        auto &target = job.items[index];
        const bool ours = target.started || !target.got.empty();
        ++job.pass_count(pass).checked;
        if (ours)
            funnel_finish(job, index, options, remaining, details_left);
        else
        {
            target.state = State::skipped;
            target.message = "Already in the library.";
        }
        save_job(job, false);
        return;
    }
    const size_t given_before = item.got.size();
    const std::string source = options.sources[pass];
    Options one = options;
    one.source = source;
    one.kinds.clear();
    for (const auto &kind : remaining)
        if (source_offers(source, kind))
            one.kinds.push_back(kind);
    one.details = details_left && source_offers(source, "details");
    if (!one.kinds.empty() || one.details)
    {
        /* What an earlier source left for the user stays aside while this one runs. */
        {
            std::lock_guard<std::mutex> guard(lock);
            auto &target = job.items[index];
            target.matched = target.matches.count(source) ? target.matches.at(source) : "";
            target.candidates.clear();
            target.state = State::working;
        }
        process_one(http, job, index, one);
        std::lock_guard<std::mutex> guard(lock);
        auto &target = job.items[index];
        /* A source that must wait (a quota, a closed API) keeps the game for the resume. */
        if (cancelling || job.state != "running")
        {
            target.state = State::pending;
            target.candidates = item.candidates;
            target.candidates_source = item.candidates_source;
            save_job(job, true);
            return;
        }
        if (!target.matched.empty() && target.candidates.empty() && target.state != State::failed)
            target.matches[source] = target.matched;
        if (target.state == State::ambiguous && item.held != "ambiguous")
        {
            target.held = "ambiguous";
            target.candidates_source = source;
            target.held_message = target.message;
        }
        else
        {
            if (target.state == State::unmatched && item.held.empty())
            {
                target.held = "unmatched";
                target.candidates_source = source;
                target.held_message = target.message;
            }
            else if (target.state == State::failed && item.held.empty())
            {
                target.held = "failed";
                target.held_message = source_name(source) + ": " + target.message;
            }
            target.candidates = item.candidates;
            if (target.held == item.held)
                target.candidates_source = item.candidates_source;
        }
        item = target;
    }
    lacking(item, remaining, details_left);
    std::lock_guard<std::mutex> guard(lock);
    auto &target = job.items[index];
    {
        /* What this source gave it: kinds new since the pass began. */
        auto &count = job.pass_count(pass);
        ++count.checked;
        const size_t gave = target.got.size() > given_before ? target.got.size() - given_before : 0;
        count.files += unsigned(gave);
        count.games += gave ? 1 : 0;
    }
    if ((!remaining.empty() || details_left) && pass + 1 < options.sources.size())
    {
        /* Down the funnel: the next source's pass takes it. */
        target.pass = pass + 1;
        target.state = State::pending;
        target.message = "Waiting for " + source_name(options.sources[pass + 1]) + '.';
    }
    else
        funnel_finish(job, index, options, remaining, details_left);
    save_job(job, false);
    wake.notify_all();
}

/* ---- workers ---------------------------------------------------------------------- */
/* Downloads are small and wait on the network: 32 games at once (each worker holds a
 * 1 MiB stack and one connection a host). ScreenScraper keeps to its own limit
 * (ScreenscraperSlot); the other sources never wait for it. */
constexpr unsigned worker_count = 32;
std::atomic<unsigned> worker_numbers{0};
void worker()
{
    const unsigned me = ++worker_numbers; /* never 0: 0 is "no worker" */
    Http http("PS5-RetroArch-Scraper/1");
    for (;;)
    {
        std::shared_ptr<Job> job;
        unsigned index = 0;
        {
            std::unique_lock<std::mutex> guard(lock);
            for (;;)
            {
                if (stopping)
                    return;
                if (active && active->state == "running" && !cancelling)
                {
                    auto &items = active->items;
                    const unsigned pass = active->pass;
                    auto next = std::find_if(
                        items.begin(), items.end(), [pass](const Item &i)
                        { return i.state == State::pending && i.pass <= pass && !i.holder; });
                    if (next != items.end())
                    {
                        next->state = State::working;
                        next->holder = me;
                        job = active;
                        index = unsigned(next - items.begin());
                        break;
                    }
                    /* Nothing left to take: the job ends once nothing is in flight (a game a
                     * worker holds may show a source's result while the funnel moves it on). */
                    const bool flight =
                        std::any_of(items.begin(), items.end(), [](const Item &i)
                                    { return i.holder || i.state == State::transferring; });
                    if (!flight)
                    {
                        /* A pass over: the next source's, if a game waits for it. */
                        unsigned later = UINT_MAX;
                        for (const auto &item : items)
                            if (item.state == State::pending && item.pass > pass)
                                later = std::min(later, item.pass);
                        if (later != UINT_MAX)
                        {
                            active->pass = later;
                            save_job(*active, true);
                            continue;
                        }
                        active->state = "done";
                        save_job(*active, true);
                    }
                }
                wake.wait_for(guard, std::chrono::seconds(1));
            }
        }
        process(http, *job, index);
        std::lock_guard<std::mutex> guard(lock);
        /* Only its own hold: another worker may have taken the game on already. */
        if (job->items[index].holder == me)
            job->items[index].holder = 0;
        wake.notify_all();
    }
}
/* Each worker on a 1 MiB stack: a payload's default thread stack is small (src/scraper_http.cpp).
 */
void ensure_workers()
{
    if (!workers.empty())
        return;
    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    pthread_attr_setstacksize(&attributes, 1u << 20);
    for (unsigned i = 0; i < worker_count; ++i)
    {
        pthread_t thread;
        if (pthread_create(
                &thread, &attributes,
                [](void *) -> void *
                {
                    worker();
                    return nullptr;
                },
                nullptr) == 0)
            workers.push_back(thread);
    }
    pthread_attr_destroy(&attributes);
}
} // namespace

/* Hidden files a writer left when it died (a crash mid-download): older than ten
 * minutes, no live writer has them (a stale daemon finishing a file is younger). */
static void sweep_partials(const std::string &folder, int depth)
{
    DIR *dir = opendir(folder.c_str());
    if (!dir)
        return;
    const std::time_t now = std::time(nullptr);
    while (const dirent *entry = readdir(dir))
    {
        const std::string name = entry->d_name;
        if (name == "." || name == "..")
            continue;
        const std::string path = folder + '/' + name;
        struct stat st{};
        if (stat(path.c_str(), &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode) && depth > 0)
            sweep_partials(path, depth - 1);
        else if (S_ISREG(st.st_mode) && name.rfind(".partial-", 0) == 0 && now - st.st_mtime > 600)
            unlink(path.c_str());
    }
    closedir(dir);
}

void configure(const std::string &root, const std::string &base)
{
    std::lock_guard<std::mutex> guard(lock);
    root_path = root;
    if (!base.empty())
        libretro_base = base;
    if (const char *ss = std::getenv("PS5_SCRAPER_SCREENSCRAPER_BASE")) /* the tests' */
        screenscraper_base = ss;
    if (const char *lb = std::getenv("PS5_SCRAPER_LAUNCHBOX_BASE")) /* the tests' */
    {
        launchbox_metadata = std::string(lb) + "/Metadata.zip";
        launchbox_images = std::string(lb) + "/images";
    }
    if (std::atoi(account_fields()["maxthreads"].c_str()) > 0)
        screenscraper_threads = unsigned(std::atoi(account_fields()["maxthreads"].c_str()));
    /* HTTPS is verified against the certificates the title ships (the daemon's curl). */
    if (is_file(root + "/webui/ca-bundle.crt"))
        set_certificates(root + "/webui/ca-bundle.crt");
    stopping = false;
    sweep_partials(library_root(), 3);
    /* A job the last server left running goes on: PS5 mode by itself, PC mode when the
     * helper is back. */
    std::vector<Game> games;
    if (DIR *dir = opendir(jobs_folder().c_str()))
    {
        while (const dirent *entry = readdir(dir))
        {
            const std::string name = entry->d_name;
            if (name.size() < 5 || name.compare(name.size() - 4, 4, ".job") != 0)
                continue;
            /* The library, read once for every job (a scan is seconds on a big one). */
            if (games.empty())
                games = load_games();
            auto job = load_job(jobs_folder() + '/' + name, games);
            if (!job)
                continue;
            jobs[job->id] = job;
            if (job->state == "running" && (!active || job->created > active->created))
                active = job;
        }
        closedir(dir);
    }
    if (active)
        ensure_workers();
}

void shutdown()
{
    {
        std::lock_guard<std::mutex> guard(lock);
        stopping = true;
        if (active)
            save_job(*active, true);
    }
    wake.notify_all();
    for (auto &thread : workers)
        pthread_join(thread, nullptr);
    workers.clear();
}

bool busy()
{
    std::lock_guard<std::mutex> guard(lock);
    return active && active->state == "running";
}

/* What a kind of media accepts from the user: the types every frontend reads. */
const std::vector<std::string> &accepted_types(const std::string &kind)
{
    static const std::vector<std::string> images = {"png", "jpg", "jpeg"}, video = {"mp4", "webm"},
                                          manual = {"pdf"};
    return kind == "video" ? video : kind == "manual" ? manual : images;
}
bool find_game(const std::string &system, const std::string &key, Game &out)
{
    if (!safe_part(system) || !safe_part(key))
        return false;
    for (const auto &game : load_games())
        if (game.system == system && game.key == key)
        {
            out = game;
            return true;
        }
    return false;
}

std::string game_json(const std::string &system, const std::string &key)
{
    Game game;
    if (!find_game(system, key, game))
        return "";
    const Fields meta = read_fields(meta_path(system, key));
    const auto mine = uploaded_kinds(system, key);
    std::string kinds;
    for (const auto &kind : kind_catalog)
    {
        const std::string file = stored_media(system, key, kind.id);
        struct stat st{};
        const bool present = !file.empty() && stat(file.c_str(), &st) == 0;
        std::string accepts;
        const std::string id = kind.id;
        for (const auto &type : accepted_types(id))
            accepts += std::string(accepts.empty() ? "" : ",") + quote(type);
        kinds += std::string(kinds.empty() ? "" : ",") + "{\"id\":" + quote(kind.id) +
                 ",\"name\":" + quote(kind.name) + ",\"description\":" + quote(kind.description) +
                 ",\"accepts\":[" + accepts + "],\"present\":" + (present ? "true" : "false");
        if (present)
            kinds +=
                ",\"type\":" + quote(file.substr(file.find_last_of('.') + 1)) +
                ",\"bytes\":" + std::to_string(st.st_size) +
                ",\"changed\":" + std::to_string(st.st_mtime) + ",\"uploaded\":" +
                (std::find(mine.begin(), mine.end(), kind.id) != mine.end() ? "true" : "false");
        kinds += '}';
    }
    std::string details;
    for (const char *field : {"name", "description", "developer", "publisher", "genre", "players",
                              "rating", "released", "source", "scraped"})
        if (meta.count(field) && !meta.at(field).empty())
            details += std::string(details.empty() ? "" : ",") + quote(field) + ':' +
                       quote(meta.at(field));
    return "{\"system\":" + quote(system) + ",\"system_name\":" + quote(game.system_name) +
           ",\"key\":" + quote(key) + ",\"label\":" + quote(game.label) +
           ",\"path\":" + quote(game.path) + ",\"details\":{" + details + "},\"kinds\":[" + kinds +
           "]}";
}

bool game_identity(const std::string &system, const std::string &key, const std::string &path,
                   std::string &core)
{
    for (const auto &game : load_games())
        if (game.system == system && game.key == key && game.path == path)
        {
            core = game.core;
            return true;
        }
    return false;
}

/* Every RetroArch playlist entry for a game's file: edit changes it in place, or
 * returns false to remove it. A playlist is written only when it held the game; false
 * when one could not be written. RetroArch's menus read the label (issues 55, 57). */
bool edit_playlist_entries(const std::string &path, const std::function<bool(Json &)> &edit)
{
    const std::string folder = root_path + "/playlists";
    std::vector<std::string> files;
    if (DIR *dir = opendir(folder.c_str()))
    {
        while (const dirent *entry = readdir(dir))
        {
            const std::string name = entry->d_name;
            if (name.size() > 4 && name.compare(name.size() - 4, 4, ".lpl") == 0)
                files.push_back(folder + '/' + name);
        }
        closedir(dir);
    }
    bool ok = true;
    for (const auto &file : files)
    {
        Json playlist = Json::parse(read_text(file));
        const auto items = playlist.fields.find("items");
        if (playlist.kind != Json::object || items == playlist.fields.end() ||
            items->second.kind != Json::array)
            continue;
        auto &entries = items->second.items;
        bool changed = false;
        for (size_t i = 0; i < entries.size();)
        {
            if (entries[i]["path"].str() != path)
            {
                ++i;
                continue;
            }
            changed = true;
            if (edit(entries[i]))
                ++i;
            else
                entries.erase(entries.begin() + long(i));
        }
        if (changed && !write_atomic(file, playlist.dump()))
            ok = false;
    }
    return ok;
}

bool edit_game(const std::string &system, const std::string &key, const std::string &body,
               std::string &why)
{
    Game game;
    const Json input = Json::parse(body);
    if (!find_game(system, key, game) || input.kind != Json::object)
    {
        why = "Choose a game and valid details.";
        return false;
    }
    std::lock_guard<std::mutex> guard(metadata_lock);
    Fields meta = read_fields(meta_path(system, key));
    auto edited = comma_values(meta["edited"]);
    const bool partial = input["partial"].str() == "true";
    for (const char *field : {"name", "description", "developer", "publisher", "genre", "players",
                              "rating", "released"})
    {
        if (partial && input[field].kind == Json::null)
            continue;
        if (input[field].kind != Json::string)
        {
            why = "Every detail must be text.";
            return false;
        }
        const std::string value = input[field].str();
        if (value.size() > (std::string(field) == "description" ? 12000u : 512u) ||
            value.find('\0') != std::string::npos ||
            (std::string(field) == "name" &&
             value.find_first_not_of(" \t\r\n") == std::string::npos))
        {
            why = "Enter a game name and keep each detail within its limit.";
            return false;
        }
        if (std::string(field) == "rating" && !value.empty())
        {
            char *end = nullptr;
            const double rating = std::strtod(value.c_str(), &end);
            if (*end || !std::isfinite(rating) || rating < 0 || rating > 1)
            {
                why = "Rating must be between 0 and 100 percent.";
                return false;
            }
        }
        meta[field] = value;
        if (!holds(edited, field))
            edited.push_back(field);
    }
    if (input["launchbox_id"].kind == Json::string)
    {
        const auto id = input["launchbox_id"].str();
        if (id.size() > 20 || id.find_first_not_of("0123456789") != std::string::npos)
        {
            why = "Choose a valid LaunchBox result.";
            return false;
        }
        meta["launchbox_id"] = id;
    }
    meta["edited"] = kinds_text(edited);
    make_folders(library_root() + '/' + system + "/metadata");
    if (write_atomic(meta_path(system, key), fields_text(meta)))
    {
        // The new name in RetroArch's menus too (issue 55); the metadata is the record.
        if (input["name"].kind == Json::string)
            edit_playlist_entries(game.path,
                                  [&](Json &entry)
                                  {
                                      entry.fields["label"] = Json::of(meta["name"]);
                                      return true;
                                  });
        return true;
    }
    why = "Details could not be saved. Check console storage.";
    return false;
}

bool forget_game(const std::string &system, const std::string &key, const std::string &path,
                 std::string &why)
{
    if (!safe_part(system) || !safe_part(key))
    {
        why = "Choose a game in the library.";
        return false;
    }
    std::lock_guard<std::mutex> guard(metadata_lock);
    const std::string file = path.substr(0, path.find('#'));
    bool shared_name = false;
    for (const auto &game : load_games())
        if (game.path != path)
        {
            if (game.path.substr(0, game.path.find('#')) == file)
            {
                why = "This archive holds other games too, so it was not deleted.";
                return false;
            }
            shared_name = shared_name || (game.system == system && game.key == key);
        }
    if (!edit_playlist_entries(path, [](Json &) { return false; }))
    {
        why = "A playlist could not be updated. Check console storage; nothing was deleted.";
        return false;
    }
    if (shared_name)
        return true; // the other copy keeps the shared details and media
    for (const auto &kind : kind_catalog)
        for (const char *ext : {".png", ".jpg", ".jpeg", ".webp", ".gif", ".mp4", ".webm", ".pdf"})
            unlink((library_root() + '/' + system + '/' + kind.folder + '/' + key + ext).c_str());
    unlink(meta_path(system, key).c_str());
    return true;
}

bool upload_target(const std::string &system, const std::string &key, const std::string &kind,
                   const std::string &type, std::string &temporary, std::string &destination,
                   std::string &why)
{
    Game game;
    if (!kind_folders.count(kind))
    {
        why = "No such kind of media.";
        return false;
    }
    const auto &types = accepted_types(kind);
    if (std::find(types.begin(), types.end(), type) == types.end())
    {
        std::string list;
        for (const auto &t : types)
            list += (list.empty() ? "" : ", ") + t;
        why = kind_name(kind) + " takes " + list + " files (what every frontend can show).";
        return false;
    }
    if (!find_game(system, key, game))
    {
        why = "This game is not in the library.";
        return false;
    }
    destination = media_destination(game, kind, ('.' + type).c_str());
    const std::string folder = destination.substr(0, destination.find_last_of('/'));
    make_folders(folder);
    temporary = folder + "/.partial-" + nonce();
    return true;
}

void uploaded(const std::string &system, const std::string &key, const std::string &kind,
              const std::string &destination)
{
    std::lock_guard<std::mutex> guard(metadata_lock);
    /* The new file is the kind's only one (a lookup takes the first type it finds). */
    const std::string stem = destination.substr(0, destination.find_last_of('.'));
    for (const char *ext : {".png", ".jpg", ".jpeg", ".webp", ".gif", ".mp4", ".webm", ".pdf"})
        if (stem + ext != destination)
            unlink((stem + ext).c_str());
    make_folders(library_root() + '/' + system + "/metadata");
    Fields meta = read_fields(meta_path(system, key));
    auto mine = kinds_from(meta["uploaded"]);
    if (std::find(mine.begin(), mine.end(), kind) == mine.end())
        mine.push_back(kind);
    meta["uploaded"] = kinds_text(mine);
    meta["media." + kind] = destination.substr(library_root().size() + 1);
    write_atomic(meta_path(system, key), fields_text(meta));
}

namespace
{
std::map<std::string, Fields> add_systems()
{
    ps5_library lib{};
    ps5_library_load(&lib, (root_path + "/playlists").c_str(), (root_path + "/info").c_str(),
                     (root_path + "/cores").c_str());
    std::map<std::string, Fields> out;
    for (size_t i = 0; i < lib.core_count; ++i)
    {
        const auto &core = lib.cores[i];
        const std::string databases = core.databases;
        for (size_t at = 0; at < databases.size();)
        {
            auto end = databases.find('|', at);
            const auto database = databases.substr(at, end - at);
            const char *id = ps5_library_platform(database.c_str());
            if (id)
            {
                auto &entry = out[id];
                entry["name"] = ps5_library_platform_database(id);
                if (entry["extensions"].empty())
                    entry["extensions"] = core.extensions;
                else
                    entry["extensions"] += '|' + std::string(core.extensions);
            }
            if (end == std::string::npos)
                break;
            at = end + 1;
        }
    }
    ps5_library_free(&lib);
    return out;
}
} // namespace
std::string add_systems_json()
{
    std::string out = "{\"systems\":[";
    for (const auto &system : add_systems())
    {
        if (out.back() != '[')
            out += ',';
        out += "{\"id\":" + quote(system.first) + ",\"name\":" + quote(system.second.at("name")) +
               ",\"extensions\":" + quote(system.second.at("extensions")) + '}';
    }
    return out + "]}";
}
bool add_target(const std::string &system, const std::string &filename, std::string &relative,
                std::string &key, std::string &why)
{
    const auto systems = add_systems();
    auto selected = systems.find(system);
    if (selected == systems.end() || filename.empty() || filename.size() > 240 ||
        filename.front() == '.' || filename.find_first_of("/\\#\t\r\n") != std::string::npos ||
        filename.find('\0') != std::string::npos)
    {
        why = "Choose an installed system and a valid game filename.";
        return false;
    }
    const auto dot = filename.find_last_of('.');
    std::string ext = dot == std::string::npos ? "" : filename.substr(dot + 1);
    for (char &c : ext)
        c = char(std::tolower((unsigned char)c));
    if (ext.empty() ||
        ("|" + selected->second.at("extensions") + "|").find('|' + ext + '|') == std::string::npos)
    {
        why = "This file type is not supported by the installed cores for this system.";
        return false;
    }
    relative = selected->first + '/' + filename; /* the system's folder is its ES-DE name */
    key = game_key("/app0/content/" + relative);
    for (const auto &game : load_games())
        if (game.system == system && game.key == key)
        {
            why = "This game filename is already in your library. Open its overlay to replace it, "
                  "or rename the new file.";
            return false;
        }
    return true;
}
std::string launchbox_lookup(const std::string &system, const std::string &query, std::string &why)
{
    if (!launchbox_platforms.count(system) || query.size() < 2 || query.size() > 160)
    {
        why = "Choose a supported system and enter at least two letters.";
        return "";
    }
    Http http("PS5-RetroArch");
    Job lookup;
    if (!launchbox_prepare(http, lookup, why, stopping))
        return "";
    std::vector<std::string> candidates;
    const auto key = title_key(query);
    std::vector<std::pair<std::string, std::string>> matches;
    for (const auto &platform : launchbox_platforms.at(system))
    {
        const auto index = launchbox_index(platform);
        for (const auto &title : index->by_title)
        {
            if (title.first.find(key) == std::string::npos)
                continue;
            for (const auto &id : title.second)
            {
                const auto found = index->by_id.find(id);
                if (found == index->by_id.end())
                    continue;
                const auto &game = index->games[found->second];
                const auto candidate = game.name + " [" + id + ']';
                /* The normalized index strips parenthesized edition tags. A literal
                 * title must outrank an enhanced edition sharing that index key. */
                const bool exact = game.name.size() == query.size() &&
                                   std::equal(game.name.begin(), game.name.end(), query.begin(),
                                              [](unsigned char a, unsigned char b)
                                              { return std::tolower(a) == std::tolower(b); });
                matches.emplace_back((exact                ? "0"
                                      : title.first == key ? "1"
                                                           : "2") +
                                         title.first,
                                     candidate);
            }
        }
    }
    std::sort(matches.begin(), matches.end());
    for (const auto &match : matches)
        if (!holds(candidates, match.second) && candidates.size() < 20)
            candidates.push_back(match.second);
    for (const auto &candidate : launchbox_search(system, query, "wor"))
        if (!holds(candidates, candidate) && candidates.size() < 20)
            candidates.push_back(candidate);
    std::string out = "{\"results\":[";
    for (const auto &candidate : candidates)
    {
        const auto id = candidate_id(candidate);
        for (const auto &platform : launchbox_platforms.at(system))
        {
            auto index = launchbox_index(platform);
            auto hit = index->by_id.find(id);
            if (hit == index->by_id.end())
                continue;
            const auto &g = index->games[hit->second];
            if (out.back() != '[')
                out += ',';
            out +=
                "{\"id\":" + quote(g.id) + ",\"name\":" + quote(g.name) +
                ",\"description\":" + quote(g.overview) + ",\"developer\":" + quote(g.developer) +
                ",\"publisher\":" + quote(g.publisher) + ",\"released\":" + quote(g.released) +
                ",\"genre\":" + quote(g.genres) + ",\"players\":" + quote(g.players) +
                ",\"rating\":" +
                quote(g.rating.empty() ? "" : std::to_string(std::atof(g.rating.c_str()) / 5.0)) +
                '}';
            break;
        }
    }
    return out + "]}";
}

std::string library_json()
{
    const auto games = load_games();
    std::string out = "{\"systems\":[";
    std::string current;
    bool first_game = true;
    for (const auto &game : games)
    {
        if (game.system != current)
        {
            out += std::string(current.empty() ? "" : "]},") + "{\"id\":" + quote(game.system) +
                   ",\"name\":" + quote(game.system_name) +
                   ",\"thumbnails\":" + (game.database.empty() ? "false" : "true") + ",\"games\":[";
            current = game.system;
            first_game = true;
        }
        const Fields meta = read_fields(meta_path(game.system, game.key));
        std::string media;
        for (const auto &kind : kind_folders)
            if (!stored_media(game.system, game.key, kind.first).empty())
                media += (media.empty() ? "" : ",") + quote(kind.first);
        out += std::string(first_game ? "" : ",") + "{\"path\":" + quote(game.path) +
               ",\"label\":" + quote(game.label) + ",\"key\":" + quote(game.key) +
               ",\"core\":" + quote(game.core) +
               ",\"name\":" + quote(meta.count("name") ? meta.at("name") : "") +
               ",\"scraped\":" + quote(meta.count("scraped") ? meta.at("scraped") : "") +
               ",\"media\":[" + media + "]";
        for (const char *field : {"released", "genre", "developer", "publisher", "rating"})
            if (meta.count(field) && !meta.at(field).empty())
                out += ',' + quote(field) + ':' + quote(meta.at(field));
        out += '}';
        first_game = false;
    }
    return out + (current.empty() ? "" : "]}") + "]}";
}

std::string settings_json()
{
    const Fields saved = read_fields(root_path + "/config/scraper.cfg");
    auto get = [&](const char *key, const char *fallback)
    { return saved.count(key) ? saved.at(key) : std::string(fallback); };
    auto list = [](const std::vector<std::string> &ids)
    {
        std::string out;
        for (const auto &id : ids)
            out += (out.empty() ? "" : ",") + quote(id);
        return '[' + out + ']';
    };
    std::string catalog;
    for (const auto &kind : kind_catalog)
        catalog += std::string(catalog.empty() ? "" : ",") + "{\"id\":" + quote(kind.id) +
                   ",\"name\":" + quote(kind.name) + ",\"description\":" + quote(kind.description) +
                   ",\"folder\":" + quote(kind.folder) +
                   ",\"accepts\":" + list(accepted_types(kind.id)) + '}';
    /* The sources: libretro now; the others are named so the page can say where each
     * media type will come from (they need accounts or keys, to be added). */
    struct SourceInfo
    {
        const char *id, *name, *description;
        bool account, available;
        std::vector<std::string> kinds;
    };
    const SourceInfo sources[] = {
        {"libretro",
         "libretro thumbnails",
         "Free, no account. Box art, screenshots, title screens and, for some systems, logos, "
         "found by the game's name (the No-Intro name RetroArch's scanner gives).",
         false,
         true,
         {"cover", "screenshot", "title", "logo"}},
        {"screenscraper",
         "ScreenScraper",
         "Every media type and the games' details (description, developer, release date, "
         "rating), found by checksum or name. Sign in with a free screenscraper.fr account.",
         true,
         screenscraper_ready(),
         {"cover", "backcover", "box3d", "screenshot", "title", "logo", "physical", "fanart",
          "manual", "video"}},
        {"launchbox",
         "LaunchBox Games Database",
         "Free, no account. Box art (front, back, 3D), screenshots, title screens, logos, "
         "discs and cartridges, fan art and the games' details; arcade romsets by name. Its "
         "database (about 110 MB) is fetched once a month.",
         false,
         true,
         {"cover", "backcover", "box3d", "screenshot", "title", "logo", "physical", "fanart"}},
        {"emumovies",
         "EmuMovies",
         "Video previews, manuals and artwork. Needs an EmuMovies "
         "account. Coming.",
         true,
         false,
         {"cover", "backcover", "box3d", "screenshot", "title", "logo", "physical", "fanart",
          "manual", "video"}}};
    std::string source_list;
    for (const auto &source : sources)
        source_list +=
            std::string(source_list.empty() ? "" : ",") + "{\"id\":" + quote(source.id) +
            ",\"name\":" + quote(source.name) + ",\"description\":" + quote(source.description) +
            ",\"account\":" + (source.account ? "true" : "false") +
            ",\"available\":" + (source.available ? "true" : "false") + ",\"signed_in\":" +
            (std::string(source.id) == "screenscraper" && account_fields().count("password")
                 ? "true"
                 : "false") +
            ",\"details\":" + (std::string(source.id) == "libretro" ? "false" : "true") +
            ",\"kinds\":" + list(source.kinds) + '}';
    return "{\"mode\":" + quote(get("mode", "")) +
           ",\"source\":" + quote(get("source", "libretro")) + ",\"chain\":" +
           list(candidates_from_list(get("sources", get("source", "libretro").c_str()))) +
           ",\"kinds\":" + list(kinds_from(get("kinds", "cover,screenshot,title"))) +
           ",\"details\":" + (get("details", "1") == "1" ? "true" : "false") +
           ",\"region\":" + quote(get("region", "us")) +
           ",\"language\":" + quote(get("language", "en")) + ",\"catalog\":[" + catalog +
           "],\"sources\":[" + source_list + "]}";
}

std::string account_json(const std::string &source)
{
    if (source != "screenscraper")
        return "{\"source\":" + quote(source) + ",\"available\":false,\"signed_in\":false}";
    std::lock_guard<std::mutex> guard(account_lock);
    Fields account = account_fields();
    const bool signed_in = account.count("user") && account.count("password");
    /* The password never leaves the console: only the name and the quotas. */
    std::string out = "{\"source\":\"screenscraper\",\"available\":" +
                      std::string(screenscraper_ready() ? "true" : "false") +
                      ",\"signed_in\":" + (signed_in ? "true" : "false");
    if (signed_in)
        out += ",\"user\":" + quote(account["user"]) + ",\"level\":" + quote(account["niveau"]) +
               ",\"requests_today\":" + quote(account["requeststoday"]) +
               ",\"max_requests\":" + quote(account["maxrequestsperday"]) +
               ",\"max_threads\":" + quote(account["maxthreads"]);
    return out + '}';
}

bool sign_in(const std::string &source, const std::string &user, const std::string &password,
             std::string &why)
{
    if (source != "screenscraper")
    {
        why = "This source has no sign-in.";
        return false;
    }
    if (!screenscraper_ready())
    {
        why = "This build has no ScreenScraper developer account.";
        return false;
    }
    if (user.empty() || password.empty())
    {
        why = "Enter your ScreenScraper name and password.";
        return false;
    }
    /* The account is checked with ScreenScraper before it is kept. */
    Http http("PS5-RetroArch-Scraper/1");
    const Response response =
        http.get(screenscraper_url(
                     "ssuserInfos.php",
                     "&ssid=" + url_encode(user) + "&sspassword=" + url_encode(password), false),
                 1 << 20, nullptr);
    const Json answer = response.status == 200 ? Json::parse(response.body) : Json();
    const Json &ssuser = answer["response"]["ssuser"];
    if (ssuser.kind != Json::object || ssuser["id"].str().empty())
    {
        if (response.status == 0)
            why = "ScreenScraper could not be reached: " + response.error;
        else if (response.body.find("utilisateur") != std::string::npos)
            why = "ScreenScraper did not accept this name and password.";
        else if (response.body.find("veloppeur") != std::string::npos)
            why = "ScreenScraper refused this app's developer account.";
        else if (response.status == 423 || response.status == 401)
            why = "ScreenScraper's API is closed right now. Try again later.";
        else
            why = "ScreenScraper did not accept this name and password.";
        return false;
    }
    std::lock_guard<std::mutex> guard(account_lock);
    Fields account;
    account["user"] = ssuser["id"].str();
    account["password"] = password;
    for (const char *key : {"requeststoday", "maxrequestsperday", "maxthreads", "niveau"})
        account[key] = ssuser[key].str();
    if (std::atoi(account["maxthreads"].c_str()) > 0)
        screenscraper_threads = unsigned(std::atoi(account["maxthreads"].c_str()));
    if (!save_account(account))
    {
        why = "The account could not be saved on the console.";
        return false;
    }
    return true;
}

bool sign_out(const std::string &source)
{
    if (source != "screenscraper")
        return false;
    std::lock_guard<std::mutex> guard(account_lock);
    return unlink(account_path().c_str()) == 0 || errno == ENOENT;
}

bool save_settings(const Options &options)
{
    Fields fields;
    fields["mode"] = options.mode;
    fields["source"] = options.source;
    fields["sources"] = kinds_text(
        options.sources.empty() ? std::vector<std::string>{options.source} : options.sources);
    fields["kinds"] = kinds_text(options.kinds);
    fields["details"] = options.details ? "1" : "0";
    fields["region"] = options.region;
    fields["language"] = options.language;
    make_folders(root_path + "/config");
    return write_atomic(root_path + "/config/scraper.cfg", fields_text(fields));
}

std::string media_file(const std::string &system, const std::string &game, const std::string &kind)
{
    if (!safe_part(system) || !safe_part(game))
        return "";
    return stored_media(system, game, kind);
}

bool start(const Options &requested, const std::vector<Selection> &selection, std::string &id,
           std::string &why)
{
    Options options = requested;
    if (options.mode != "ps5" && options.mode != "pc")
    {
        why = "Choose where to download: this PC or the PS5.";
        return false;
    }
    if (options.sources.empty())
        options.sources = {options.source};
    {
        std::vector<std::string> unique;
        for (const auto &source : options.sources)
            if (!holds(unique, source))
                unique.push_back(source);
        options.sources = unique;
    }
    options.source = options.sources.front();
    if (options.mode == "pc" && options.sources.size() > 1)
    {
        why = "This PC → PS5 takes one source at a time. Choose PS5 to combine sources.";
        return false;
    }
    for (const auto &source : options.sources)
        if (source != "libretro" && source != "screenscraper" && source != "launchbox")
        {
            why = "This source is not available yet.";
            return false;
        }
    if (holds(options.sources, "screenscraper"))
    {
        if (!screenscraper_ready())
        {
            why = "This build has no ScreenScraper developer account.";
            return false;
        }
        if (!account_fields().count("password"))
        {
            why = "Sign in to ScreenScraper first.";
            return false;
        }
        /* Its media addresses carry this app's developer account, which never leaves the
         * console: no helper on a PC gets them. */
        if (options.mode == "pc")
        {
            why = "ScreenScraper downloads run on the PS5 only: its media links carry this "
                  "app's private account details, which never leave the console. Choose PS5.";
            return false;
        }
    }
    if (options.sources == std::vector<std::string>{"libretro"})
        options.details = false; /* libretro has pictures only */
    if (options.kinds.empty() && !options.details)
    {
        why = requested.details ? "libretro has no game details: choose ScreenScraper for them."
                                : "Choose at least one kind of media, or the games' details.";
        return false;
    }
    const auto games = load_games();
    auto job = std::make_shared<Job>();
    job->id = nonce();
    job->created = now_text();
    job->options = options;
    std::set<std::string> seen;
    for (const auto &game : games)
        for (const auto &chosen : selection)
            if (chosen.system == game.system && (chosen.path.empty() || chosen.path == game.path) &&
                seen.insert(game.path).second)
            {
                Item item;
                item.game = game;
                const auto metadata = read_fields(meta_path(game.system, game.key));
                if (metadata.count("edited") &&
                    holds(comma_values(metadata.at("edited")), "name") && metadata.count("name") &&
                    !metadata.at("name").empty())
                    item.game.label = metadata.at("name");
                job->items.push_back(item);
            }
    if (job->items.empty())
    {
        why = "Choose at least one game or system.";
        return false;
    }
    std::lock_guard<std::mutex> guard(lock);
    if (active && active->state == "running")
    {
        why = "A scraping job is already running. Cancel it or wait for it to finish.";
        return false;
    }
    cancelling = false;
    jobs[job->id] = job;
    active = job;
    id = job->id;
    save_job(*job, true);
    ensure_workers();
    wake.notify_all();
    return true;
}

bool resume(const std::string &id, std::string &why)
{
    std::lock_guard<std::mutex> guard(lock);
    auto it = jobs.find(id);
    if (it == jobs.end())
    {
        why = "This job no longer exists.";
        return false;
    }
    if (active && active != it->second && active->state == "running")
    {
        why = "Another scraping job is running.";
        return false;
    }
    cancelling = false;
    active = it->second;
    /* A game in a worker's hands is left to it (it puts it back itself). */
    for (auto &item : active->items)
        if (!item.holder && (item.state == State::working || item.state == State::transferring ||
                             item.state == State::failed))
        {
            /* A failed game goes down the funnel again from its top (what it has is kept). */
            if (item.state == State::failed && active->options.sources.size() > 1)
            {
                item.pass = 0;
                item.held.clear();
                item.held_message.clear();
            }
            item.state = State::pending;
        }
    if (active->options.sources.size() > 1)
    {
        active->pass = UINT_MAX;
        for (const auto &item : active->items)
            if (item.state == State::pending)
                active->pass = std::min(active->pass, item.pass);
        if (active->pass == UINT_MAX)
            active->pass = 0;
    }
    active->tasks.clear();
    active->state = "running";
    active->message.clear();
    save_job(*active, true);
    ensure_workers();
    wake.notify_all();
    return true;
}

/* The funnel's passes for the page: per source, the games that reached it, those it has
 * checked, those it gave something and how much. */
std::string passes_json(const Job &job)
{
    if (job.options.sources.size() <= 1)
        return "[]";
    std::string out;
    for (unsigned i = 0; i < job.options.sources.size(); ++i)
    {
        const Job::PassCount count = i < job.passes.size() ? job.passes[i] : Job::PassCount{};
        /* Reached: checked, plus those waiting for it or in its hands now. */
        unsigned reached = count.checked;
        for (const auto &item : job.items)
            if (item.pass == i && (item.state == State::pending || item.holder) &&
                !(item.state == State::pending && item.pass < job.pass))
                ++reached;
        out += std::string(out.empty() ? "" : ",") +
               "{\"source\":" + quote(job.options.sources[i]) +
               ",\"reached\":" + std::to_string(reached) +
               ",\"checked\":" + std::to_string(count.checked) +
               ",\"games\":" + std::to_string(count.games) +
               ",\"files\":" + std::to_string(count.files) + '}';
    }
    return '[' + out + ']';
}

std::string job_json(const std::string &id)
{
    std::lock_guard<std::mutex> guard(lock);
    std::shared_ptr<Job> job = id.empty() ? active : (jobs.count(id) ? jobs.at(id) : nullptr);
    /* No job running: the latest one, so a page opened later still shows how it ended. */
    if (!job && id.empty())
        for (const auto &entry : jobs)
            if (!job || entry.second->created > job->created)
                job = entry.second;
    if (!job)
        return "{\"job\":null}";
    std::map<std::string, unsigned> counts;
    std::string problems;
    unsigned listed = 0;
    for (size_t i = 0; i < job->items.size(); ++i)
    {
        const auto &item = job->items[i];
        ++counts[state_name(item.state)];
        if ((item.state == State::ambiguous || item.state == State::unmatched ||
             item.state == State::failed) &&
            listed++ < 500)
        {
            std::string candidates;
            for (const auto &candidate : item.candidates)
                candidates += (candidates.empty() ? "" : ",") + quote(candidate);
            problems +=
                std::string(problems.empty() ? "" : ",") + "{\"item\":" + std::to_string(i) +
                ",\"system\":" + quote(item.game.system) + ",\"label\":" + quote(item.game.label) +
                ",\"state\":" + quote(state_name(item.state)) +
                ",\"message\":" + quote(item.message) + ",\"candidates\":[" + candidates + "]}";
        }
    }
    std::string state_counts;
    for (const auto &count : counts)
        state_counts += (state_counts.empty() ? "" : ",") + quote(count.first) + ':' +
                        std::to_string(count.second);
    unsigned queued = 0, leased = 0;
    for (const auto &task : job->tasks)
        (task.state == Task::queued   ? queued
         : task.state == Task::leased ? leased
                                      : queued) += task.state != Task::done;
    return "{\"job\":{\"id\":" + quote(job->id) + ",\"created\":" + quote(job->created) +
           ",\"state\":" + quote(job->state) + ",\"message\":" + quote(job->message) +
           ",\"mode\":" + quote(job->options.mode) + ",\"source\":" + quote(job->options.source) +
           ",\"sources\":" + quote(kinds_text(job->options.sources)) +
           ",\"passes\":" + passes_json(*job) + ",\"pass\":" + std::to_string(job->pass) +
           ",\"waiting\":" +
           std::to_string(
               std::count_if(job->items.begin(), job->items.end(), [&](const Item &i)
                             { return i.state == State::pending && i.pass > job->pass; })) +
           ",\"overwrite\":" + (job->options.overwrite ? "true" : "false") +
           ",\"total\":" + std::to_string(job->items.size()) + ",\"counts\":{" + state_counts +
           "},\"downloaded\":{\"files\":" + std::to_string(job->downloaded_files) +
           ",\"bytes\":" + std::to_string(job->downloaded_bytes) +
           "},\"transferred\":{\"files\":" + std::to_string(job->transferred_files) +
           ",\"bytes\":" + std::to_string(job->transferred_bytes) +
           "},\"tasks\":{\"queued\":" + std::to_string(queued) +
           ",\"leased\":" + std::to_string(leased) + "},\"problems\":[" + problems + "]}}";
}

std::string recap_json(const std::string &id)
{
    std::shared_ptr<Job> job;
    std::vector<Item> items;
    {
        std::lock_guard<std::mutex> guard(lock);
        job = jobs.count(id) ? jobs.at(id) : nullptr;
        if (!job)
            return "{\"recap\":null}";
        items = job->items;
    }
    std::vector<std::string> kinds = job->options.kinds;
    if (job->options.details)
        kinds.push_back("details");
    auto list = [](const std::vector<std::string> &values)
    {
        std::string out;
        for (const auto &value : values)
            out += std::string(out.empty() ? "" : ",") + quote(value);
        return '[' + out + ']';
    };
    std::map<std::string, std::array<unsigned, 3>> totals; /* kind: got, had, missed */
    std::vector<std::string> order;                        /* systems, as first met */
    std::map<std::string, std::string> names, games;
    for (const auto &item : items)
    {
        const std::string &system = item.game.system;
        if (!names.count(system))
        {
            order.push_back(system);
            names[system] = item.game.system_name.empty() ? system : item.game.system_name;
        }
        /* What the store holds now, set against what this job stored: the rest was there. */
        std::vector<std::string> got, had, missed;
        const bool settled = item.state != State::pending && item.state != State::working &&
                             item.state != State::transferring;
        for (const auto &kind : kinds)
        {
            const bool stored = kind == "details"
                                    ? has_details(item.game.system, item.game.key)
                                    : !stored_media(item.game.system, item.game.key, kind).empty();
            const bool fetched =
                std::find(item.got.begin(), item.got.end(), kind) != item.got.end();
            auto &total = totals[kind];
            if (fetched && stored)
                got.push_back(kind), ++total[0];
            else if (stored)
                had.push_back(kind), ++total[1];
            else if (settled)
                missed.push_back(kind), ++total[2];
        }
        std::string &out = games[system];
        out += std::string(out.empty() ? "" : ",") + "{\"label\":" + quote(item.game.label) +
               ",\"key\":" + quote(item.game.key) + ",\"state\":" + quote(state_name(item.state)) +
               ",\"matched\":" + quote(clean_title(item.matched)) +
               ",\"message\":" + quote(item.message) + ",\"got\":" + list(got) +
               ",\"had\":" + list(had) + ",\"missed\":" + list(missed) + '}';
    }
    std::string systems, total_text;
    for (const auto &system : order)
        systems += std::string(systems.empty() ? "" : ",") + "{\"id\":" + quote(system) +
                   ",\"name\":" + quote(names[system]) + ",\"games\":[" + games[system] + "]}";
    for (const auto &kind : kinds)
        total_text += std::string(total_text.empty() ? "" : ",") + quote(kind) +
                      ":{\"got\":" + std::to_string(totals[kind][0]) +
                      ",\"had\":" + std::to_string(totals[kind][1]) +
                      ",\"missed\":" + std::to_string(totals[kind][2]) + '}';
    return "{\"recap\":{\"id\":" + quote(job->id) + ",\"source\":" + quote(job->options.source) +
           ",\"sources\":" + quote(kinds_text(job->options.sources)) + ",\"kinds\":" + list(kinds) +
           ",\"totals\":{" + total_text + "},\"systems\":[" + systems + "]}}";
}

bool cancel(const std::string &id)
{
    std::lock_guard<std::mutex> guard(lock);
    auto job = id.empty() ? active : (jobs.count(id) ? jobs.at(id) : nullptr);
    if (!job || job->state != "running")
        return false;
    job->state = "cancelled";
    cancelling = true;
    for (auto &item : job->items)
        if (item.state == State::working || item.state == State::transferring)
            item.state = State::pending;
    job->tasks.clear();
    save_job(*job, true);
    wake.notify_all();
    return true;
}

bool resolve(const std::string &id, unsigned index, const std::string &action,
             const std::string &value, std::string &why)
{
    std::shared_ptr<Job> job;
    {
        std::lock_guard<std::mutex> guard(lock);
        job = id.empty() ? active : (jobs.count(id) ? jobs.at(id) : nullptr);
        if (!job || index >= job->items.size())
        {
            why = "This game is no longer in the job.";
            return false;
        }
        auto &item = job->items[index];
        if (action == "skip")
        {
            item.state = State::skipped;
            item.message = "Skipped.";
            save_job(*job, true);
            return true;
        }
        const std::string source =
            !item.candidates_source.empty() ? item.candidates_source : job->options.source;
        if (action == "choose")
        {
            if (value.empty() || (source == "libretro" && value.find('/') != std::string::npos) ||
                (source != "libretro" && candidate_id(value).empty()))
            {
                why = "Choose one of the names offered.";
                return false;
            }
            item.matched = value;
            item.matches[source] = value;
            /* The source the choice is for runs again, then the ones below it. */
            for (unsigned i = 0; i < job->options.sources.size(); ++i)
                if (job->options.sources[i] == source)
                    item.pass = std::min(i, job->pass);
            item.held.clear();
            item.held_message.clear();
            item.candidates.clear();
            item.state = State::pending;
            if (job->state != "running")
                job->state = "running";
            cancelling = false;
            active = job;
            save_job(*job, true);
            ensure_workers();
            wake.notify_all();
            return true;
        }
        if (action != "search" || value.empty())
        {
            why = "Choose, search or skip.";
            return false;
        }
    }
    /* A search runs here, on the request's thread: the index is cached after once. */
    Http http("PS5-RetroArch-Scraper/1");
    std::string database, region, source;
    {
        std::lock_guard<std::mutex> guard(lock);
        database = job->items[index].game.database;
        region = job->options.region;
        source = !job->items[index].candidates_source.empty() ? job->items[index].candidates_source
                                                              : job->options.source;
    }
    std::vector<std::string> found;
    if (source == "launchbox")
    {
        std::string system_id;
        {
            std::lock_guard<std::mutex> guard(lock);
            system_id = job->items[index].game.system;
        }
        found = launchbox_search(system_id, value, region);
    }
    else if (source == "screenscraper")
    {
        std::string system_id;
        {
            std::lock_guard<std::mutex> guard(lock);
            system_id = job->items[index].game.system;
        }
        const auto system = screenscraper_systems.find(system_id);
        if (system != screenscraper_systems.end() &&
            screenscraper_search(http, system->second, value, region, found, why) < 0)
            return false;
    }
    else
        found = closest(libretro_index(http, database, job.get()), value, region);
    std::lock_guard<std::mutex> guard(lock);
    auto &item = job->items[index];
    item.candidates = found;
    item.candidates_source = source;
    item.state = found.empty() ? State::unmatched : State::ambiguous;
    item.message = found.empty() ? "Nothing found for \"" + value + "\"."
                                 : "Choose the right game, search, or skip.";
    return true;
}

/* How long a PC task waits for its file before it is handed out again (the tests
 * shorten it). */
std::time_t lease_seconds()
{
    const char *value = std::getenv("PS5_SCRAPER_LEASE_SECONDS");
    return value && std::atoi(value) > 0 ? std::atoi(value) : 120;
}

std::string pc_tasks(const std::string &id, unsigned max, bool fresh)
{
    std::lock_guard<std::mutex> guard(lock);
    auto job = jobs.count(id) ? jobs.at(id) : nullptr;
    if (!job || job->options.mode != "pc")
        return "{\"error\":\"This job is not a PC job.\"}";
    const std::time_t now = std::time(nullptr);
    std::string out;
    unsigned given = 0;
    /* One helper a job: a new one means the last stopped, its leases with it. */
    if (fresh)
        for (auto &task : job->tasks)
            if (task.state == Task::leased)
                task.state = Task::queued;
    for (auto &task : job->tasks)
    {
        if (given >= max)
            break;
        if (task.state == Task::leased && now > task.lease)
            task.state = Task::queued; /* the helper went away: someone else takes it */
        if (task.state != Task::queued)
            continue;
        task.state = Task::leased;
        task.lease = now + lease_seconds();
        out += std::string(given++ ? "," : "") + "{\"task\":" + std::to_string(task.id) +
               ",\"item\":" + std::to_string(task.item) + ",\"kind\":" + quote(task.kind) +
               ",\"url\":" + quote(task.url) + '}';
    }
    const bool identifying =
        std::any_of(job->items.begin(), job->items.end(), [](const Item &i)
                    { return i.state == State::pending || i.state == State::working; });
    return "{\"state\":" + quote(job->state) + ",\"more\":" +
           (identifying || std::any_of(job->tasks.begin(), job->tasks.end(),
                                       [](const Task &t) { return t.state != Task::done; })
                ? "true"
                : "false") +
           ",\"tasks\":[" + out + "]}";
}

bool pc_target(const std::string &id, unsigned task, std::string &temporary,
               std::string &destination)
{
    std::lock_guard<std::mutex> guard(lock);
    auto job = jobs.count(id) ? jobs.at(id) : nullptr;
    if (!job || job->state != "running" || task >= job->tasks.size() ||
        job->tasks[task].state == Task::done)
        return false;
    destination = job->tasks[task].destination;
    make_folders(destination.substr(0, destination.find_last_of('/')));
    temporary = destination.substr(0, destination.find_last_of('/') + 1) + ".partial-" + nonce();
    job->tasks[task].lease = std::time(nullptr) + lease_seconds(); /* renewed as it arrives */
    return true;
}

void pc_downloaded(const std::string &id, unsigned task, bool found, uint64_t bytes)
{
    std::lock_guard<std::mutex> guard(lock);
    auto job = jobs.count(id) ? jobs.at(id) : nullptr;
    if (!job || task >= job->tasks.size())
        return;
    auto &asked = job->tasks[task];
    if (asked.work != Task::media)
    {
        /* A probe's answer, or a fetch the source does not have: the worker waiting
         * on it can go on. A whole fetch ends with its bytes (pc_delivered), not
         * here, and neither kind moves the item's or the job's media counters. */
        if (asked.state != Task::done && !(asked.work == Task::fetch && found))
        {
            asked.state = Task::done;
            asked.result = found ? 1 : 0;
        }
        wake.notify_all();
        return;
    }
    if (found)
    {
        ++job->downloaded_files;
        job->downloaded_bytes += bytes;
        return;
    }
    /* The source had nothing after all: the task ends without bytes. */
    auto &t = asked;
    if (t.state == Task::done)
        return;
    t.state = Task::done;
    auto &item = job->items[t.item];
    ++item.missing;
    if (--item.tasks_open == 0)
        item.state = item.found ? State::partial : State::unmatched;
}

void pc_delivered(const std::string &id, unsigned task, bool ok, uint64_t bytes)
{
    Item finished;
    Options options;
    bool complete = false;
    {
        std::lock_guard<std::mutex> guard(lock);
        auto job = jobs.count(id) ? jobs.at(id) : nullptr;
        if (!job || task >= job->tasks.size() || job->tasks[task].state == Task::done)
            return;
        auto &t = job->tasks[task];
        if (t.work == Task::probe)
            return; /* a probe is answered by pc_downloaded, never by bytes */
        if (t.work == Task::fetch)
        {
            /* A database or listing the console asked for: its bytes are in place at
             * the destination the waiting worker reads. The media's own counters are
             * not moved by the console's questions. */
            t.state = ok ? Task::done : Task::queued;
            if (ok)
                t.result = 1;
            wake.notify_all();
            return;
        }
        if (!ok)
        {
            t.state = Task::queued; /* handed out again */
            return;
        }
        t.state = Task::done;
        ++job->transferred_files;
        job->transferred_bytes += bytes;
        auto &item = job->items[t.item];
        ++item.found;
        note_got(item, t.kind);
        if (--item.tasks_open == 0)
        {
            item.state = item.missing ? State::partial : State::done;
            item.message = item.missing ? source_name(job->options.source) + " has " +
                                              std::to_string(item.found) + " of " +
                                              std::to_string(item.found + item.missing) + " images."
                                        : "";
            finished = item;
            options = job->options;
            complete = true;
        }
        save_job(*job, false);
    }
    if (complete)
        save_meta(finished, options);
    wake.notify_all();
}
} // namespace ps5_scraper
