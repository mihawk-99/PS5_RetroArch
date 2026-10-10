/* PS5 RetroArch - EmulationStation's systems and game lists, from RetroArch's playlists.
 *
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ES-DE keeps no library of its own here: each time it starts (main_ps5.cpp, before
 * ES-DE reads anything), the game library every frontend shares (src/ps5_library.h)
 * is read from RetroArch's playlists and core info, and ES-DE's files are written
 * from it:
 *
 *   - ES-DE/custom_systems/es_systems.xml: a system for each of the library's, named
 *     as ES-DE names the platform (its full name, platform and theme taken from
 *     ES-DE's own systems file, resources/systems/unix/es_systems.xml), its path the
 *     folder of its games, its command game mode's: "/app0/eboot.bin -L <core> %ROM%";
 *   - ES-DE/gamelists/<system>/gamelist.xml: the system's games. A game ES-DE already
 *     lists keeps everything ES-DE knows of it (play count and time, favourite,
 *     scraped details); a new one gets its playlist label; one no playlist has any
 *     more is left out. What RetroArch remembers of play is added:
 *       - favourite: RetroArch's favourites as they changed since the last start
 *         (ES-DE/ps5-favorites.txt holds them as they were): a game RetroArch added
 *         or removed since is ES-DE's favourite or not, and any other keeps ES-DE's
 *         own, so a favourite set in either frontend shows here;
 *       - lastplayed, playcount, playtime: the later and the larger of ES-DE's and
 *         RetroArch's runtime log, so a game played from RetroArch shows in ES-DE's
 *         Last Played collection.
 *     ES-DE shows both as its Favorites and Last Played collections: the port's
 *     default (patches/0001), set once on an install whose settings had none.
 *
 * ES-DE then runs with --gamelist-only, so its systems hold exactly those games, as
 * RetroArch's playlists do, and nothing is scanned for.
 */
#include "library_ps5.h"

#include "ps5_library.h"

#include <pugixml.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <sys/stat.h>

namespace
{
struct Reference
{
    std::string fullname, platform, theme;
};

/* ES-DE's own systems: name -> full name, platform, theme. */
std::map<std::string, Reference> read_reference(const std::string &path)
{
    std::map<std::string, Reference> systems;
    pugi::xml_document document;
    if (!document.load_file(path.c_str()))
        return systems;
    for (pugi::xml_node system : document.child("systemList").children("system"))
        systems[system.child_value("name")] = {system.child_value("fullname"),
                                               system.child_value("platform"),
                                               system.child_value("theme")};
    return systems;
}

std::string extensions(const char *list)
{
    std::string out;
    for (const char *at = list; at && *at;)
    {
        const char *bar = std::strchr(at, '|');
        const std::string extension(at, bar ? static_cast<size_t>(bar - at) : std::strlen(at));
        std::string upper = extension;
        for (char &c : upper)
            c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
        out += (out.empty() ? "." : " .") + extension;
        if (upper != extension)
            out += " ." + upper;
        at = bar ? bar + 1 : nullptr;
    }
    return out;
}

/* A game list's path, absolute: "./x" is relative to the system's folder. */
std::string absolute(const std::string &folder, const std::string &path)
{
    if (path.rfind("./", 0) == 0)
        return (folder == "/" ? "" : folder) + "/" + path.substr(2);
    return path;
}

/* A child's text, set, the child made when the node has none. */
void set_child(pugi::xml_node node, const char *name, const std::string &value)
{
    pugi::xml_node child = node.child(name);
    if (!child)
        child = node.append_child(name);
    child.text().set(value.c_str());
}

/* RetroArch's "YYYY-MM-DD HH:MM:SS" as ES-DE's "YYYYMMDDTHHMMSS". */
std::string esde_time(const char *retroarch)
{
    int year, month, day, hour, minute, second;
    if (std::sscanf(retroarch, "%4d-%2d-%2d %2d:%2d:%2d", &year, &month, &day, &hour, &minute,
                    &second) != 6)
        return "";
    char out[20];
    std::snprintf(out, sizeof(out), "%04d%02d%02dT%02d%02d%02d", year, month, day, hour, minute,
                  second);
    return out;
}

/* What RetroArch remembers of a game's play, into its game list entry. */
void add_play(pugi::xml_node node, const struct ps5_library_game &game, bool was_favorite)
{
    const bool es_favorite = std::strcmp(node.child_value("favorite"), "true") == 0;
    const bool favorite = game.favorite != 0 ? (was_favorite ? es_favorite : true)
                                             : (was_favorite ? false : es_favorite);
    if (favorite)
        set_child(node, "favorite", "true");
    else if (node.child("favorite"))
        node.remove_child("favorite");
    const std::string played = esde_time(game.last_played);
    const std::string known = node.child_value("lastplayed");
    if (!played.empty() && (known.size() != played.size() || played > known))
        set_child(node, "lastplayed", played);
    if (game.play_count > std::strtoul(node.child_value("playcount"), nullptr, 10))
        set_child(node, "playcount", std::to_string(game.play_count));
    if (game.play_seconds > std::strtoul(node.child_value("playtime"), nullptr, 10))
        set_child(node, "playtime", std::to_string(game.play_seconds));
}

std::set<std::string> read_lines(const std::string &path)
{
    std::set<std::string> lines;
    std::ifstream input(path);
    for (std::string line; std::getline(input, line);)
        if (!line.empty())
            lines.insert(line);
    return lines;
}

/* An install whose settings predate the collections default (it wrote
 * CollectionSystemsAuto empty) gets Favorites and Last Played once; the marker keeps
 * a later choice of the user's. */
void enable_collections(const std::string &data)
{
    const std::string settings = data + "/settings/es_settings.xml";
    const std::string marker = data + "/ps5-collections-enabled";
    struct stat status;
    if (stat(marker.c_str(), &status) == 0)
        return;
    pugi::xml_document document;
    if (document.load_file(settings.c_str()))
        for (pugi::xml_node node : document.children("string"))
            if (std::strcmp(node.attribute("name").value(), "CollectionSystemsAuto") == 0 &&
                node.attribute("value").value()[0] == '\0')
            {
                node.attribute("value").set_value("favorites,recent");
                document.save_file(settings.c_str()); // as ES-DE saves it (Settings::saveFile)
            }
    if (std::FILE *file = std::fopen(marker.c_str(), "w"))
        std::fclose(file);
}

/* ES-DE's folders, set in its settings before each start (it keeps them in this file):
 * - ROMDirectory: the title's content folder, where games go (issue 33). ES-DE's own
 *   default, ~/ROMs, is /app0/es-de/ROMs: its no-games screen pointed there. A folder the
 *   user chose stays; only an empty one or ES-DE's default is replaced.
 * - MediaDirectory: the shared media library (src/ps5_library.h), the same files
 *   RetroArch's thumbnails and every frontend read, never a copy.
 * A first start has no settings yet: a file with just these is written, which ES-DE
 * reads and completes with its defaults when it saves (Settings::loadFile). */
void use_title_folders(const std::string &data, const char *roms, const char *media)
{
    const std::string folder = data + "/settings", settings = folder + "/es_settings.xml";
    pugi::xml_document document;
    struct stat status;
    if (stat(settings.c_str(), &status) == 0 && !document.load_file(settings.c_str()))
        return; /* a file ES-DE cannot read either: left as it is */
    bool changed = false;
    auto set = [&](const char *name, const char *value, bool keep_users)
    {
        if (!value)
            return;
        pugi::xml_node node;
        for (pugi::xml_node entry : document.children("string"))
            if (std::strcmp(entry.attribute("name").value(), name) == 0)
                node = entry;
        if (node)
        {
            const std::string now = node.attribute("value").value();
            if (now == value ||
                (keep_users && !now.empty() && now != "~/ROMs" && now != "~/ROMs/" &&
                 now != "/app0/es-de/ROMs" && now != "/app0/es-de/ROMs/"))
                return;
        }
        else
        {
            node = document.append_child("string");
            node.append_attribute("name").set_value(name);
            node.append_attribute("value");
        }
        node.attribute("value").set_value(value);
        changed = true;
    };
    set("ROMDirectory", roms, true);
    set("MediaDirectory", media, false);
    if (!changed)
        return;
    mkdir(folder.c_str(), 0777);
    chmod(folder.c_str(), 0777); /* FTP's reach: the title's folders are 0777 */
    if (document.save_file(settings.c_str())) // as ES-DE saves it (Settings::saveFile)
        chmod(settings.c_str(), 0777);
}

/* A game's scraped metadata (library/<system>/metadata/<key>.meta, key = "value"). */
std::map<std::string, std::string> read_meta(const std::string &path)
{
    std::map<std::string, std::string> fields;
    std::ifstream input(path);
    for (std::string line; std::getline(input, line);)
    {
        const size_t equals = line.find(" = \"");
        if (equals == std::string::npos || line.size() < equals + 5 || line.back() != '"')
            continue;
        std::string value;
        for (size_t i = equals + 4; i + 1 < line.size(); ++i)
            if (line[i] == '\\' && i + 2 < line.size())
                value += line[++i] == 'n' ? '\n' : line[i];
            else
                value += line[i];
        fields[line.substr(0, equals)] = value;
    }
    return fields;
}

/* The scraped fields into a game's list entry: a field ES-DE has is kept, unless it is
 * still the library's default (the name as the label). */
void add_meta(pugi::xml_node node, const char *media, const char *system,
              const struct ps5_library_game &game)
{
    char key[512];
    if (!media || ps5_library_media_key(game.path, key, sizeof(key)) != 0)
        return;
    const auto meta = read_meta(std::string(media) + '/' + system + "/metadata/" + key + ".meta");
    static const char *const fields[][2] = {
        {"name", "name"},           {"description", "desc"},    {"developer", "developer"},
        {"publisher", "publisher"}, {"genre", "genre"},         {"players", "players"},
        {"rating", "rating"},       {"released", "releasedate"}};
    for (const auto &field : fields)
    {
        const auto value = meta.find(field[0]);
        if (value == meta.end() || value->second.empty())
            continue;
        std::string text = value->second;
        /* The store keeps a date as its source gives it (1994-04-19, 1994-04, 1994); ES-DE
         * reads 19940419T000000. A rating is 0 to 1 in both. */
        if (std::strcmp(field[1], "releasedate") == 0)
        {
            std::string digits;
            for (char c : text)
                if (c >= '0' && c <= '9')
                    digits += c;
            if (digits.size() != 4 && digits.size() != 6 && digits.size() != 8)
                continue;
            while (digits.size() < 8)
                digits += "01";
            text = digits + "T000000";
        }
        const std::string current = node.child_value(field[1]);
        if (current.empty() || (std::strcmp(field[1], "name") == 0 && current == game.label))
            set_child(node, field[1], text);
    }
}

bool save(pugi::xml_document &document, const std::string &path)
{
    const std::string temporary = path + ".tmp";
    if (!document.save_file(temporary.c_str(), "\t", pugi::format_default, pugi::encoding_utf8))
        return false;
    chmod(temporary.c_str(), 0777); /* FTP's reach: the title's files are 0777 */
    if (std::rename(temporary.c_str(), path.c_str()) != 0)
    {
        std::remove(temporary.c_str());
        return false;
    }
    return true;
}
} // namespace

extern "C" int ps5_esde_write_library_to(const struct ps5_esde_library_paths *paths, char *summary,
                                         size_t summary_size)
{
    struct ps5_library library;
    if (ps5_library_load_content(&library, paths->playlists, paths->info, paths->cores,
                                 paths->content) != 0)
    {
        ps5_library_free(&library);
        std::snprintf(summary, summary_size, "the library could not be read (memory)");
        return -1;
    }
    const std::map<std::string, Reference> reference = read_reference(paths->reference);
    const std::string data = paths->data;
    /* Folders 0777 whatever the umask, for FTP's reach (src/permissions_ps5.cpp). */
    for (const char *folder : {"/custom_systems", "/gamelists"})
    {
        mkdir((data + folder).c_str(), 0777);
        chmod((data + folder).c_str(), 0777);
    }
    enable_collections(data);
    use_title_folders(data, paths->content ? paths->content[0] : nullptr, paths->media);
    /* RetroArch's favourites as they were at the last start. */
    const std::string favorites_record = data + "/ps5-favorites.txt";
    const std::set<std::string> were_favorites = read_lines(favorites_record);
    std::string favorites_now;
    size_t favorites = 0, played = 0;

    pugi::xml_document systems;
    systems.append_child(pugi::node_comment)
        .set_value(" Written by es-de.bin each time it starts, from RetroArch's playlists "
                   "and the content folders (src/ps5_library.h). "
                   "Changes here are lost: change the playlists in RetroArch. ");
    pugi::xml_node list = systems.append_child("systemList");
    std::set<std::string> names;
    size_t games_written = 0, games_kept = 0, failures = 0;
    for (size_t s = 0; s < library.system_count; s++)
    {
        const struct ps5_library_system &system = library.systems[s];
        std::string name = system.id;
        for (int suffix = 2; names.count(name); suffix++)
            name = std::string(system.id) + "-" + std::to_string(suffix);
        names.insert(name);
        const auto known = reference.find(system.id);
        pugi::xml_node entry = list.append_child("system");
        entry.append_child("name").text().set(name.c_str());
        entry.append_child("fullname")
            .text()
            .set(known != reference.end() ? known->second.fullname.c_str() : system.name);
        entry.append_child("path").text().set(system.folder);
        entry.append_child("extension").text().set(extensions(system.extensions).c_str());
        const std::string command =
            system.core[0] ? std::string("/app0/eboot.bin -L ") + system.core + " %ROM%"
                           : std::string("/app0/eboot.bin %ROM%");
        pugi::xml_node launch = entry.append_child("command");
        launch.append_attribute("label").set_value("RetroArch");
        launch.text().set(command.c_str());
        entry.append_child("platform")
            .text()
            .set(known != reference.end() ? known->second.platform.c_str() : system.id);
        entry.append_child("theme").text().set(
            known != reference.end() ? known->second.theme.c_str() : system.id);

        /* Its game list: what ES-DE knows of each game kept, the library's games only. */
        const std::string folder = data + "/gamelists/" + name;
        const std::string gamelist = folder + "/gamelist.xml";
        mkdir(folder.c_str(), 0777);
        chmod(folder.c_str(), 0777);
        pugi::xml_document previous, games;
        previous.load_file(gamelist.c_str());
        std::map<std::string, pugi::xml_node> known_games;
        for (pugi::xml_node game : previous.child("gameList").children("game"))
            known_games[absolute(system.folder, game.child_value("path"))] = game;
        pugi::xml_node root = games.append_child("gameList");
        for (pugi::xml_node node : previous.child("gameList").children("folder"))
            root.append_copy(node);
        const std::string prefix =
            std::string(system.folder) == "/" ? "/" : std::string(system.folder) + "/";
        for (size_t g = 0; g < system.game_count; g++)
        {
            const struct ps5_library_game &game = library.games[system.first_game + g];
            const std::string path = game.path;
            const std::string relative =
                "./" + (path.rfind(prefix, 0) == 0 ? path.substr(prefix.size())
                                                   : path.substr(path.rfind('/') + 1));
            const auto previous_game = known_games.find(path);
            pugi::xml_node node;
            if (previous_game != known_games.end())
            {
                node = root.append_copy(previous_game->second);
                node.child("path").text().set(relative.c_str());
                if (!node.child("name"))
                    node.append_child("name").text().set(game.label);
                games_kept++;
            }
            else
            {
                node = root.append_child("game");
                node.append_child("path").text().set(relative.c_str());
                node.append_child("name").text().set(game.label);
            }
            add_play(node, game, were_favorites.count(path) != 0);
            add_meta(node, paths->media, system.id, game);
            if (game.favorite)
                favorites_now += path + "\n";
            favorites += std::strcmp(node.child_value("favorite"), "true") == 0;
            played +=
                node.child("lastplayed") && std::strcmp(node.child_value("lastplayed"), "0") != 0;
            games_written++;
        }
        if (!save(games, gamelist))
            failures++;
    }
    if (!save(systems, data + "/custom_systems/es_systems.xml"))
        failures++;
    if (std::FILE *record = std::fopen((favorites_record + ".tmp").c_str(), "w"))
    {
        std::fputs(favorites_now.c_str(), record);
        std::fclose(record);
        if (std::rename((favorites_record + ".tmp").c_str(), favorites_record.c_str()) != 0)
            failures++;
    }
    const size_t system_count = library.system_count;
    std::snprintf(
        summary, summary_size,
        "%zu systems, %zu games (%zu with ES-DE's details kept), %zu cores, %zu favourites, "
        "%zu played, %zu failures",
        system_count, games_written, games_kept, library.core_count, favorites, played, failures);
    ps5_library_free(&library);
    return failures ? -1 : static_cast<int>(system_count);
}

extern "C" int ps5_esde_write_library(char *summary, size_t summary_size)
{
    /* The title's content folder, and the USB and extended storage drives a title sees
     * when ShadowMountPlus mounts them (src/frontend_ps5.cpp lists the same in RetroArch's
     * browser); an empty slot holds no system folder, so it adds nothing. */
    static const char *const content[] = {"/app0/content", "/mnt/usb0", "/mnt/usb1", "/mnt/usb2",
                                          "/mnt/usb3",     "/mnt/usb4", "/mnt/usb5", "/mnt/usb6",
                                          "/mnt/usb7",     "/mnt/ext0", "/mnt/ext1", nullptr};
    const struct ps5_esde_library_paths paths = {
        PS5_LIBRARY_PLAYLISTS, PS5_LIBRARY_INFO,
        PS5_LIBRARY_CORES,     "/app0/es-de/resources/systems/unix/es_systems.xml",
        "/app0/es-de/ES-DE",   content,
        PS5_LIBRARY_MEDIA};
    return ps5_esde_write_library_to(&paths, summary, summary_size);
}
