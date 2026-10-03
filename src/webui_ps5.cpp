/* Copyright (C) 2026 Mihawk; SPDX-License-Identifier: GPL-3.0-or-later
 * HTTP transport follows ps5-payload-dev/websrv: libmicrohttpd manages framing,
 * partial bodies, timeouts and connections. Routes are restricted to RetroArch.
 */
#include "webui_ps5.h"
#include "../vendor/retroarch/libretro-common/include/libretro.h"
#include <microhttpd.h>
#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <map>
#include <new>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <vector>

namespace
{
MHD_Daemon *web_daemon = nullptr;
std::string root_path, token;
unsigned short listen_port;
constexpr uint64_t upload_limit = UINT64_C(64) * 1024 * 1024 * 1024;
struct Setting
{
    const char *key;
    const char *label;
    const char *kind;
    const char *initial;
    int min, max;
};
const Setting settings[] = {
    {"audio_volume", "Audio volume", "number", "0", -80, 12},
    {"input_rumble_gain", "Rumble strength", "number", "100", 0, 100},
    {"video_smooth", "Smooth image scaling", "bool", "false", 0, 0},
    {"video_vsync", "Vertical sync", "bool", "true", 0, 0},
    {"savestate_auto_save", "Save state when closing content", "bool", "false", 0, 0},
    {"savestate_auto_load", "Load state when opening content", "bool", "false", 0, 0},
    {"menu_show_advanced_settings", "Show advanced settings", "bool", "false", 0, 0},
    {"menu_driver", "Console menu", "menu", "xmb", 0, 0},
};
struct Request
{
    int file = -1;
    std::string temporary, destination, body;
    uint64_t received = 0, expected = 0;
    unsigned error = 0;
    const char *message = "";
    ~Request()
    {
        if (file >= 0)
            close(file);
        if (!temporary.empty())
            unlink(temporary.c_str());
    }
};
std::string quote(const std::string &text)
{
    std::string out = "\"";
    for (unsigned char c : text)
    {
        if (c == '"' || c == '\\')
        {
            out += '\\';
            out += c;
        }
        else if (c < 32)
        {
            char escape[7];
            std::snprintf(escape, sizeof escape, "\\u%04x", c);
            out += escape;
        }
        else
            out += c;
    }
    return out + '"';
}
std::string read_file(const std::string &name, size_t limit)
{
    std::string out;
    int fd = open(name.c_str(), O_RDONLY | O_NOFOLLOW);
    if (fd < 0)
        return out;
    char buffer[4096];
    while (out.size() < limit)
    {
        ssize_t n = read(fd, buffer, std::min(sizeof buffer, limit - out.size()));
        if (n <= 0)
            break;
        out.append(buffer, size_t(n));
    }
    close(fd);
    return out;
}
std::string nonce()
{
    unsigned char bytes[16];
    arc4random_buf(bytes, sizeof bytes);
    std::string out;
    for (auto b : bytes)
    {
        char hex[3];
        std::snprintf(hex, sizeof hex, "%02x", b);
        out += hex;
    }
    return out;
}
MHD_Result respond(MHD_Connection *c, unsigned status, const std::string &body,
                   const char *type = "application/json")
{
    auto *response = MHD_create_response_from_buffer(body.size(), const_cast<char *>(body.data()),
                                                     MHD_RESPMEM_MUST_COPY);
    if (!response)
        return MHD_NO;
    MHD_add_response_header(response, "Content-Type", type);
    MHD_add_response_header(response, "Cache-Control", "no-store");
    MHD_add_response_header(response, "X-Content-Type-Options", "nosniff");
    MHD_add_response_header(response, "Referrer-Policy", "no-referrer");
    MHD_add_response_header(response, "Content-Security-Policy",
                            "default-src 'self'; connect-src 'self' https://api.github.com; "
                            "img-src 'self'; style-src 'self'; script-src 'self'; font-src 'self'; "
                            "frame-ancestors 'none'; base-uri 'none'; form-action 'self'");
    auto result = MHD_queue_response(c, status, response);
    MHD_destroy_response(response);
    return result;
}
MHD_Result error(MHD_Connection *c, unsigned status, const char *message)
{
    return respond(c, status, "{\"error\":" + quote(message) + "}");
}
const char *arg(MHD_Connection *c, const char *key)
{
    const char *v = MHD_lookup_connection_value(c, MHD_GET_ARGUMENT_KIND, key);
    return v ? v : "";
}
bool local_origin(MHD_Connection *c)
{
    const char *host = MHD_lookup_connection_value(c, MHD_HEADER_KIND, "Host");
    if (!host)
        return false;
    const auto *info = MHD_get_connection_info(c, MHD_CONNECTION_INFO_CONNECTION_FD);
    if (!info)
        return false;
    sockaddr_in addr{};
    socklen_t size = sizeof addr;
    if (getsockname(info->connect_fd, reinterpret_cast<sockaddr *>(&addr), &size) != 0 ||
        addr.sin_family != AF_INET)
        return false;
    char ip[INET_ADDRSTRLEN];
    if (!inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof ip))
        return false;
    const std::string expected = std::string(ip) + ":" + std::to_string(listen_port);
    if (host != expected)
        return false; // Literal console address also prevents DNS rebinding.
    const char *origin = MHD_lookup_connection_value(c, MHD_HEADER_KIND, "Origin");
    return !origin || std::string(origin) == "http://" + expected;
}
bool valid_path(const std::string &relative)
{
    if (relative.size() > 1024 || (!relative.empty() && relative.front() == '/'))
        return false;
    size_t start = 0;
    while (start < relative.size())
    {
        const auto end = relative.find('/', start);
        const auto part = relative.substr(start, end - start);
        if (part.empty() || part.front() == '.' || part.size() > 255)
            return false;
        for (unsigned char c : part)
            if (c < 32 || c == 127 || c == '\\' || c == ':')
                return false;
        if (end == std::string::npos)
            return true;
        start = end + 1;
        if (start == relative.size())
            return false;
    }
    return true;
}
// A title cannot use lstat reliably. Opening without following links preserves
// the path boundary and lets fstat obtain the native file type and size.
int content_stat(const char *path, struct stat *st)
{
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0)
        return -1;
    int result = fstat(fd, st);
    int saved_errno = errno;
    close(fd);
    errno = saved_errno;
    return result;
}
bool storage_space(struct statvfs &storage)
{
#ifdef __PROSPERO__
    // The SDK's statvfs compatibility answer is synthetic, not free disk space.
    (void)storage;
    return false;
#else
    return statvfs((root_path + "/content").c_str(), &storage) == 0;
#endif
}
// Refuse symlinks at every component. Only files below content/ are exposed.
bool content_path(const std::string &relative, std::string &absolute, bool new_leaf = false)
{
    if (!valid_path(relative))
        return false;
    absolute = root_path + "/content";
    struct stat st{};
    if (content_stat(absolute.c_str(), &st) || !S_ISDIR(st.st_mode))
        return false;
    size_t start = 0;
    while (start < relative.size())
    {
        auto end = relative.find('/', start);
        absolute += '/' + relative.substr(start, end - start);
        if (end == std::string::npos && new_leaf)
            return true;
        if (content_stat(absolute.c_str(), &st) || S_ISLNK(st.st_mode))
            return false;
        if (end != std::string::npos && !S_ISDIR(st.st_mode))
            return false;
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return true;
}
std::string trim(std::string s)
{
    auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return "";
    s = s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        s = s.substr(1, s.size() - 2);
    return s;
}
using Config = std::map<std::string, std::string>;
bool setting_key(const std::string &key)
{
    return !key.empty() && key.size() <= 160 &&
           key.find_first_not_of(
               "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.") ==
               std::string::npos;
}
Config parse_config(const std::string &text)
{
    Config values;
    size_t start = 0;
    while (start < text.size())
    {
        auto end = text.find('\n', start);
        auto line = text.substr(start, end - start);
        auto eq = line.find('=');
        if (eq != std::string::npos)
        {
            auto key = trim(line.substr(0, eq));
            if (setting_key(key))
                values[key] = trim(line.substr(eq + 1));
        }
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return values;
}
Config read_config(const std::string &path)
{
    return parse_config(read_file(path, 2 * 1024 * 1024));
}
void overlay(Config &to, const Config &from)
{
    for (const auto &entry : from)
        to[entry.first] = entry.second;
}
bool write_config(const std::string &path, const Config &values, std::string text = {})
{
    for (const auto &entry : values)
        text += entry.first + " = \"" + entry.second + "\"\n";
    const std::string temporary = path + ".webui-" + nonce();
    int fd = open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0)
        return false;
    size_t offset = 0;
    while (offset < text.size())
    {
        ssize_t n = write(fd, text.data() + offset, text.size() - offset);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;
        offset += size_t(n);
    }
    bool ok = offset == text.size() && fsync(fd) == 0;
    close(fd);
    if (ok)
        ok = rename(temporary.c_str(), path.c_str()) == 0;
    unlink(temporary.c_str());
    return ok;
}
// Only catalogs whose core binary is installed participate in the profile list.
std::vector<std::string> installed_cores()
{
    std::vector<std::string> cores;
    for (const auto &entry : read_config(root_path + "/webui/core-metadata/index.cfg"))
    {
        struct stat st{};
        if (!valid_path(entry.first) || entry.first.find('/') != std::string::npos ||
            !valid_path(entry.second) || entry.second.find('/') != std::string::npos)
            continue;
        if (!content_stat((root_path + "/cores/" + entry.first).c_str(), &st) &&
            S_ISREG(st.st_mode))
            cores.push_back(entry.second);
    }
    return cores;
}
std::vector<std::string> core_profiles()
{
    auto cores = installed_cores();
    DIR *dir = opendir((root_path + "/config").c_str());
    if (!dir)
        return cores;
    while (auto *entry = readdir(dir))
    {
        std::string name = entry->d_name;
        if (!valid_path(name) || name.find('/') != std::string::npos || name == "webui-cores")
            continue;
        struct stat st{};
        std::string folder = root_path + "/config/" + name;
        if (content_stat(folder.c_str(), &st) || !S_ISDIR(st.st_mode))
            continue;
        for (const char *ext : {".opt", ".cfg"})
            if (!content_stat((folder + '/' + name + ext).c_str(), &st) && S_ISREG(st.st_mode))
            {
                cores.push_back(name);
                break;
            }
    }
    closedir(dir);
    std::sort(cores.begin(), cores.end());
    cores.erase(std::unique(cores.begin(), cores.end()), cores.end());
    return cores;
}
Config global_values()
{
    Config values;
    for (const auto &s : settings)
        values[s.key] = s.initial;
    overlay(values, read_config(root_path + "/retroarch.cfg"));
    overlay(values, read_config(root_path + "/config/retroarch.cfg"));
    overlay(values, read_config(root_path + "/config/webui.cfg"));
    return values;
}
std::string revision(const Config &values)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    for (const auto &entry : values)
        for (unsigned char c : entry.first + '=' + entry.second + '\n')
            hash = (hash ^ c) * UINT64_C(1099511628211);
    return std::to_string(hash);
}
std::string catalog_revision(const std::string &core)
{
    // Catalogs include the staged core's binary hash, covering dynamic options
    // even when its static option table has not changed in a new release.
    return revision({{"catalog", read_file(root_path + "/webui/core-metadata/" + core + ".json",
                                           2 * 1024 * 1024)}});
}
std::string runtime_metadata(const std::string &core, const char *extension)
{
    const std::string folder = root_path + "/config/webui-metadata";
    struct stat st{};
    if (content_stat(folder.c_str(), &st) || !S_ISDIR(st.st_mode))
        return {};
    auto text = read_file(folder + '/' + core + extension, 2 * 1024 * 1024);
    const auto tag = catalog_revision(core);
    const auto prefix = std::strcmp(extension, ".json") == 0
                            ? "{\"catalogRevision\":" + quote(tag) + ','
                            : "# catalog-revision: " + tag + '\n';
    return text.compare(0, prefix.size(), prefix) == 0 ? text : std::string();
}
const char *value_kind(const std::string &value)
{
    if (value == "true" || value == "false")
        return "bool";
    char *end = nullptr;
    errno = 0;
    double n = std::strtod(value.c_str(), &end);
    if (!value.empty() && end && !*end && !errno && std::isfinite(n))
        return "number";
    return "text";
}
const char *setting_kind(const std::string &key, bool core_option)
{
    if (!core_option)
        for (const auto &s : settings)
            if (key == s.key)
                return std::strcmp(s.kind, "menu") == 0 ? "text" : s.kind;
    // Config files do not carry type metadata. Numeric-looking bindings and
    // enum choices remain strings, so changing their value cannot change type.
    return "text";
}
bool valid_setting_value(const std::string &value, const std::string &kind)
{
    if (value.size() > 4096)
        return false;
    // Config values are quoted. Reject syntax that could escape that value.
    for (unsigned char c : value)
        if (c < 32 || c == 127 || c == '"' || c == '\\')
            return false;
    return kind == "text" || kind == value_kind(value);
}
MHD_Result config_editor(MHD_Connection *c, const std::string &method, const std::string &body)
{
    const std::string scope = arg(c, "scope"), core = arg(c, "core");
    const bool global = scope == "global", options = scope == "core-options";
    if (!global && !options && scope != "core-settings")
        return error(c, 400, "Choose global settings or a core profile.");
    std::string path = root_path + "/config/webui.cfg";
    Config baseline = global_values(), saved;
    if (!global)
    {
        auto cores = core_profiles();
        if (std::find(cores.begin(), cores.end(), core) == cores.end())
            return error(c, 404, "This core is not installed and has no saved profile.");
        std::string ext = options ? ".opt" : ".cfg";
        if (options)
        {
            baseline = parse_config(runtime_metadata(core, ".opt"));
            overlay(baseline, read_config(root_path + "/webui/core-metadata/" + core + ".opt"));
        }
        overlay(baseline, read_config(root_path + "/config/" + core + '/' + core + ext));
        path = root_path + "/config/webui-cores/" + core + ext;
    }
    saved = read_config(path);
    overlay(baseline, saved);
    const std::string tag = revision(baseline);
    if (method == "GET")
    {
        std::string out = "{\"revision\":" + quote(tag) + ",\"settings\":[";
        for (const auto &entry : baseline)
        {
            if (out.back() != '[')
                out += ',';
            out += "{\"key\":" + quote(entry.first) + ",\"value\":" + quote(entry.second) +
                   ",\"kind\":" + quote(setting_kind(entry.first, options)) + '}';
        }
        return respond(c, 200, out + "],\"apply\":\"next_launch\"}");
    }
    if (method != "POST")
        return error(c, 405, "This action is not supported.");
    const char *expected = MHD_lookup_connection_value(c, MHD_HEADER_KIND, "X-RetroArch-Revision");
    if (!expected || tag != expected)
        return error(c, 409,
                     "Settings changed since this page loaded. Refresh before saving again.");
    size_t start = 0;
    unsigned changed = 0;
    while (start < body.size())
    {
        auto end = body.find('\n', start), eq = body.find('=', start);
        if (eq == std::string::npos || (end != std::string::npos && eq > end))
            return error(c, 400, "Invalid settings. Reload the page and try again.");
        auto key = body.substr(start, eq - start);
        auto value = body.substr(eq + 1, end == std::string::npos ? end : end - eq - 1);
        auto original = baseline.find(key);
        if (original == baseline.end() || !valid_setting_value(value, setting_kind(key, options)))
            return error(
                c, 400, "Use an existing setting and a valid value without quotes or line breaks.");
        if (!options)
            for (const auto &s : settings)
                if (key == s.key)
                {
                    if (std::strcmp(s.kind, "menu") == 0 && value != "xmb" && value != "rgui")
                        return error(c, 400, "Choose XMB or RGUI for the console menu.");
                    if (std::strcmp(s.kind, "number") == 0 &&
                        (std::strtod(value.c_str(), nullptr) < s.min ||
                         std::strtod(value.c_str(), nullptr) > s.max))
                        return error(c, 400, "A setting is outside its supported range.");
                }
        saved[key] = value;
        ++changed;
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    if (!changed)
        return error(c, 400, "No settings were provided.");
    if (!global)
    {
        std::string folder = root_path + "/config/webui-cores";
        mkdir(folder.c_str(), 0755);
        struct stat st{};
        if (content_stat(folder.c_str(), &st) || !S_ISDIR(st.st_mode))
            return error(c, 500, "The settings folder is not available.");
    }
    return write_config(path, saved)
               ? respond(c, 200, "{\"saved\":true,\"apply\":\"next_launch\"}")
               : error(c, 500, "Settings could not be saved. Check console storage.");
}
void apply_core_settings()
{
    for (const auto &core : core_profiles())
        for (const char *ext : {".opt", ".cfg"})
        {
            auto saved = read_config(root_path + "/config/webui-cores/" + core + ext);
            if (saved.empty())
                continue;
            const std::string folder = root_path + "/config/" + core;
            mkdir(folder.c_str(), 0755);
            struct stat st{};
            if (content_stat(folder.c_str(), &st) || !S_ISDIR(st.st_mode))
                continue;
            std::string destination = folder + '/' + core + ext;
            auto values = read_config(destination);
            overlay(values, saved);
            if (!write_config(destination, values))
                std::fprintf(stderr, "webui: could not apply saved core settings\n");
            else
            {
                // These two ports retire pre-profile option files on first use.
                // A deliberate WebUI edit is already a user profile: do not let
                // that one-time migration rename it away when the core starts.
                if (std::strcmp(ext, ".opt") == 0 && (core == "PPSSPP" || core == "dolphin-emu"))
                {
                    const std::string marker = folder + "/ps5-default-profile-v1";
                    int fd = open(marker.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
                    if (fd >= 0)
                        close(fd);
                    else if (errno != EEXIST)
                    {
                        std::fprintf(stderr, "webui: could not preserve first-use core profile\n");
                        continue; // Keep the pending edit for a later retry.
                    }
                }
                if (unlink((root_path + "/config/webui-cores/" + core + ext).c_str()))
                    std::fprintf(stderr, "webui: could not clear applied core settings\n");
            }
        }
}
Config config_values(bool overrides_only = false)
{
    return overrides_only ? read_config(root_path + "/config/webui.cfg") : global_values();
}
MHD_Result get_settings(MHD_Connection *c)
{
    auto values = config_values();
    std::string out = "{\"settings\":[";
    for (const auto &s : settings)
    {
        if (out.back() != '[')
            out += ',';
        out += "{\"key\":" + quote(s.key) + ",\"label\":" + quote(s.label) +
               ",\"kind\":" + quote(s.kind) + ",\"value\":" + quote(values[s.key]) +
               ",\"min\":" + std::to_string(s.min) + ",\"max\":" + std::to_string(s.max) + '}';
    }
    return respond(c, 200, out + "],\"apply\":\"next_launch\"}");
}
MHD_Result save_settings(MHD_Connection *c, const std::string &body)
{
    // A bounded, plain key=value body: no arbitrary config paths or keys.
    auto values = config_values(true);
    size_t start = 0;
    unsigned changed = 0;
    while (start < body.size())
    {
        auto end = body.find('\n', start);
        auto line = body.substr(start, end - start);
        auto eq = line.find('=');
        if (eq == std::string::npos)
            return error(c, 400, "Invalid settings. Reload the page and try again.");
        auto key = line.substr(0, eq), value = line.substr(eq + 1);
        const Setting *setting = nullptr;
        for (auto &s : settings)
            if (key == s.key)
                setting = &s;
        if (!setting)
            return error(c, 400, "This setting cannot be changed through the WebUI.");
        bool valid = false;
        if (std::strcmp(setting->kind, "bool") == 0)
            valid = value == "true" || value == "false";
        else if (std::strcmp(setting->kind, "menu") == 0)
            valid = value == "xmb" || value == "rgui";
        else
        {
            char *tail = nullptr;
            errno = 0;
            long n = std::strtol(value.c_str(), &tail, 10);
            valid = !value.empty() && !errno && !*tail && n >= setting->min && n <= setting->max;
        }
        if (!valid)
            return error(c, 400, "A setting is outside its supported range.");
        values[key] = value;
        ++changed;
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    if (!changed)
        return error(c, 400, "No settings were provided.");
    // Keep advanced global overrides when updating a quick setting.
    return write_config(root_path + "/config/webui.cfg", values)
               ? respond(c, 200, "{\"saved\":true,\"apply\":\"next_launch\"}")
               : error(c, 500, "Settings could not be saved. Check console storage.");
}

MHD_Result list_content(MHD_Connection *c)
{
    const std::string relative = arg(c, "path");
    std::string path;
    if (!content_path(relative, path))
        return error(c, 400, "Choose a folder inside RetroArch content.");
    DIR *dir = opendir(path.c_str());
    if (!dir)
        return error(c, 404, "This folder is not available.");
    struct Entry
    {
        std::string name;
        bool folder;
        uint64_t size;
    };
    std::vector<Entry> entries;
    bool truncated = false;
    while (auto *item = readdir(dir))
    {
        std::string name = item->d_name;
        if (!valid_path(name) || name.find('/') != std::string::npos)
            continue;
        struct stat st{};
        if (content_stat((path + '/' + name).c_str(), &st) ||
            (!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode)))
            continue;
        if (entries.size() >= 10000)
        {
            truncated = true;
            break;
        }
        entries.push_back({name, S_ISDIR(st.st_mode), uint64_t(st.st_size)});
    }
    closedir(dir);
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b)
              { return a.folder != b.folder ? a.folder > b.folder : a.name < b.name; });
    std::string out = "{\"path\":" + quote(relative) + ",\"entries\":[";
    for (const auto &e : entries)
    {
        if (out.back() != '[')
            out += ',';
        out += "{\"name\":" + quote(e.name) + ",\"directory\":" + (e.folder ? "true" : "false") +
               ",\"size\":" + std::to_string(e.size) + '}';
    }
    return respond(c, 200, out + "],\"truncated\":" + (truncated ? "true" : "false") + '}');
}
MHD_Result download(MHD_Connection *c)
{
    std::string path;
    if (!content_path(arg(c, "path"), path))
        return error(c, 400, "Invalid content path.");
    int fd = open(path.c_str(), O_RDONLY | O_NOFOLLOW);
    struct stat st{};
    if (fd < 0)
        return error(c, 404, "This file is not available.");
    if (fstat(fd, &st) || !S_ISREG(st.st_mode))
    {
        close(fd);
        return error(c, 400, "Choose a file to download.");
    }
    auto *response = MHD_create_response_from_fd64(st.st_size, fd);
    if (!response)
    {
        close(fd);
        return MHD_NO;
    }
    const std::string disposition =
        "attachment; filename=" + quote(path.substr(path.find_last_of('/') + 1));
    MHD_add_response_header(response, "Content-Disposition", disposition.c_str());
    MHD_add_response_header(response, "Content-Type", "application/octet-stream");
    MHD_add_response_header(response, "X-Content-Type-Options", "nosniff");
    MHD_add_response_header(response, "Cache-Control", "no-store");
    auto result = MHD_queue_response(c, 200, response);
    MHD_destroy_response(response);
    return result;
}
void prepare_upload(MHD_Connection *c, Request &r)
{
    auto fail = [&](unsigned code, const char *message)
    {
        r.error = code;
        r.message = message;
    };
    const std::string relative = arg(c, "path");
    if (relative.empty() || !content_path(relative, r.destination, true))
    {
        fail(400, "Choose a valid content folder and filename.");
        return;
    }
    struct stat st{};
    if (content_stat(r.destination.c_str(), &st) == 0 || errno != ENOENT)
    {
        fail(409, "A file with this name already exists. Rename your file first.");
        return;
    }
    const char *length = MHD_lookup_connection_value(c, MHD_HEADER_KIND, "Content-Length");
    char *end = nullptr;
    errno = 0;
    if (!length || !*length || *length == '-')
    {
        fail(411, "A file size is required.");
        return;
    }
    r.expected = std::strtoull(length, &end, 10);
    if (errno || *end || r.expected > upload_limit)
    {
        fail(413, "Files must be 64 GiB or smaller.");
        return;
    }
    struct statvfs storage{};
    if (storage_space(storage) && r.expected > uint64_t(storage.f_bavail) * storage.f_frsize)
    {
        fail(507, "There is not enough free space on the console.");
        return;
    }
    r.temporary =
        r.destination.substr(0, r.destination.find_last_of('/') + 1) + ".upload-" + nonce();
    r.file = open(r.temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0644);
    if (r.file < 0)
    {
        fail(507, "Could not create the upload. Check console storage.");
        return;
    }
}
MHD_Result route(MHD_Connection *c, const std::string &url, const std::string &method, Request &r)
{
    if (method == "GET" && url == "/api/status")
    {
        struct statvfs fs{};
        bool space_known = storage_space(fs);
        return respond(
            c, 200,
            "{\"name\":\"RetroArch\",\"port\":" + std::to_string(listen_port) +
                ",\"token\":" + quote(token) + ",\"uploadLimit\":" + std::to_string(upload_limit) +
                ",\"freeBytes\":" +
                (space_known ? std::to_string(uint64_t(fs.f_bavail) * fs.f_frsize) : "null") + '}');
    }
    if (method == "GET" && url == "/api/content")
        return list_content(c);
    if (method == "GET" && url == "/api/download")
        return download(c);
    if (url == "/api/core-metadata" && method == "GET")
    {
        const char *name = MHD_lookup_connection_value(c, MHD_GET_ARGUMENT_KIND, "core");
        const std::string core = name ? name : "";
        if (!valid_path(core) || core.find('/') != std::string::npos)
            return error(c, 400, "Choose a core profile.");
        const std::string empty = "{\"categories\":[],\"settings\":[]}";
        auto bundled =
            read_file(root_path + "/webui/core-metadata/" + core + ".json", 2 * 1024 * 1024);
        auto runtime = runtime_metadata(core, ".json");
        // Keep bundled options when a game's runtime table exposes only a subset.
        return respond(c, 200,
                       "{\"bundled\":" + (bundled.empty() ? empty : bundled) +
                           ",\"runtime\":" + (runtime.empty() ? empty : runtime) + '}');
    }
    if (url == "/api/config")
        return config_editor(c, method, r.body);
    if (method == "GET" && url == "/api/cores")
    {
        std::string out = "{\"cores\":[";
        for (const auto &core : core_profiles())
        {
            if (out.back() != '[')
                out += ',';
            out += quote(core);
        }
        return respond(c, 200, out + "]}");
    }
    if (method == "GET" && url == "/api/settings")
        return get_settings(c);
    if (method == "POST" && url == "/api/settings")
        return save_settings(c, r.body);
    if (method == "POST" && url == "/api/folder")
    {
        std::string path;
        const std::string relative = arg(c, "path");
        if (relative.empty() || !content_path(relative, path, true))
            return error(c, 400, "Enter a folder name inside content.");
        if (mkdir(path.c_str(), 0755))
            return error(c, errno == EEXIST ? 409 : 500,
                         "This folder already exists or could not be created.");
        return respond(c, 201, "{\"created\":true}");
    }
    if (method == "PUT" && url == "/api/upload")
    {
        if (r.error)
            return error(c, r.error, r.message);
        if (r.received != r.expected)
            return error(c, 400, "The upload was incomplete. Try again.");
        if (fsync(r.file))
            return error(c, 507, "Could not finish writing the file. Check console storage.");
        close(r.file);
        r.file = -1;
        struct stat st{};
        // All HTTP handlers run on one MHD thread; concurrent uploads cannot replace a completed
        // file.
        if (content_stat(r.destination.c_str(), &st) == 0 || errno != ENOENT)
            return error(c, 409, "A file with this name already exists. Rename your file first.");
        if (rename(r.temporary.c_str(), r.destination.c_str()))
            return error(c, 500, "Could not finish the upload.");
        r.temporary.clear();
        return respond(c, 201, "{\"uploaded\":true,\"bytes\":" + std::to_string(r.received) + '}');
    }
    if (method != "GET")
        return error(c, 405, "This action is not supported.");
    // Only shipped assets are reachable; no filesystem passthrough.
    const std::map<std::string, const char *> assets = {
        {"/", "text/html; charset=utf-8"},
        {"/index.html", "text/html; charset=utf-8"},
        {"/app.css", "text/css; charset=utf-8"},
        {"/app.js", "text/javascript; charset=utf-8"},
        {"/settings-guide.js", "text/javascript; charset=utf-8"},
        {"/version.json", "application/json"},
        {"/assets/mihawk.png", "image/png"},
        {"/assets/ui.woff2", "font/woff2"},
        {"/assets/retroarch.svg", "image/svg+xml"}};
    auto asset = assets.find(url);
    if (asset == assets.end())
        return error(c, 404, "This page was not found.");
    auto content =
        read_file(root_path + "/webui" + (url == "/" ? "/index.html" : url), 2 * 1024 * 1024);
    if (content.empty())
        return error(c, 404, "WebUI assets are missing. Reinstall the complete RetroArch package.");
    return respond(c, 200, content, asset->second);
}
MHD_Result handle(void *, MHD_Connection *c, const char *url, const char *method, const char *,
                  const char *data, size_t *size, void **context)
{
    if (!*context)
    {
        auto *r = new (std::nothrow) Request;
        if (!r)
            return MHD_NO;
        *context = r;
        if (!local_origin(c))
            return error(c, 403, "Open this page using the console IP address and port.");
        const bool write_request = std::strcmp(method, "GET") != 0;
        const char *provided = MHD_lookup_connection_value(c, MHD_HEADER_KIND, "X-RetroArch-Token");
        if (write_request && (!provided || token != provided))
            return error(c, 403, "Your session expired. Reload the page and try again.");
        if (std::strcmp(method, "PUT") == 0 && std::strcmp(url, "/api/upload") == 0)
        {
            prepare_upload(c, *r);
            if (r->error)
                return error(c, r->error, r->message);
        }
        else
        {
            const char *length = MHD_lookup_connection_value(c, MHD_HEADER_KIND, "Content-Length");
            if (length && std::strtoull(length, nullptr, 10) > 16384)
                return error(c, 413, "This request is too large.");
        }
        return MHD_YES;
    }
    auto &r = *static_cast<Request *>(*context);
    if (*size)
    {
        if (r.file >= 0 && !r.error)
        {
            if (*size > r.expected - r.received)
            {
                r.error = 413;
                r.message = "Upload exceeded its declared file size.";
            }
            else
            {
                size_t offset = 0;
                while (offset < *size)
                {
                    ssize_t n = write(r.file, data + offset, *size - offset);
                    if (n < 0 && errno == EINTR)
                        continue;
                    if (n <= 0)
                    {
                        r.error = 507;
                        r.message = "The console could not write the upload. Check free space.";
                        break;
                    }
                    offset += size_t(n);
                    r.received += uint64_t(n);
                }
            }
        }
        else if (!r.error)
        {
            if (*size > 16384 - r.body.size())
            {
                r.error = 413;
                r.message = "This request is too large.";
            }
            else
                r.body.append(data, *size);
        }
        *size = 0;
        // MHD allows a response before receiving the body or after consuming it,
        // never during a body callback. Drain failed streams without buffering.
        return MHD_YES;
    }
    if (r.error)
        return error(c, r.error, r.message);
    return route(c, url, method, r);
}
void completed(void *, MHD_Connection *, void **context, MHD_RequestTerminationCode)
{
    delete static_cast<Request *>(*context);
    *context = nullptr;
}
// Registration runs on the emulator thread. HTTP reads only the atomic snapshot.
void save_metadata(const char *name, std::string json, const Config &defaults)
{
    if (!name || root_path.empty() || !valid_path(name) || std::strchr(name, '/') ||
        json.size() > 2 * 1024 * 1024)
        return;
    const std::string folder = root_path + "/config/webui-metadata";
    mkdir(folder.c_str(), 0755);
    struct stat st{};
    if (content_stat(folder.c_str(), &st) || !S_ISDIR(st.st_mode))
        return;
    const auto tag = catalog_revision(name);
    json.insert(1, "\"catalogRevision\":" + quote(tag) + ',');
    // Both files carry their revision inside the atomic replacement, so an
    // interrupted write or an upgrade cannot validate a stale companion file.
    write_config(folder + '/' + name + ".opt", defaults, "# catalog-revision: " + tag + '\n');
    const std::string path = folder + '/' + name + ".json";
    const std::string temporary = path + ".tmp";
    int fd = open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
    if (fd < 0)
        return;
    size_t offset = 0;
    while (offset < json.size())
    {
        ssize_t count = write(fd, json.data() + offset, json.size() - offset);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            break;
        offset += static_cast<size_t>(count);
    }
    bool ok = offset == json.size() && fsync(fd) == 0;
    close(fd);
    if (!ok || rename(temporary.c_str(), path.c_str()))
        unlink(temporary.c_str());
}
std::string metadata_text(const char *value)
{
    return quote(value ? value : "");
}
} // namespace
extern "C" void ps5_webui_core_options(const char *name, const retro_core_options_v2 *options)
{
    if (!options || !options->definitions)
        return;
    Config defaults;
    std::string json = "{\"categories\":[";
    if (options->categories)
        for (const auto *c = options->categories; c->key; ++c)
        {
            if (c != options->categories)
                json += ',';
            json += "{\"key\":" + metadata_text(c->key) + ",\"label\":" + metadata_text(c->desc) +
                    ",\"description\":" + metadata_text(c->info) + '}';
        }
    json += "],\"settings\":[";
    for (const auto *s = options->definitions; s->key; ++s)
    {
        if (s != options->definitions)
            json += ',';
        const char *value = s->default_value ? s->default_value : s->values[0].value;
        if (value && setting_key(s->key) && valid_setting_value(value, "text"))
            defaults[s->key] = value;
        json += "{\"key\":" + metadata_text(s->key) +
                ",\"label\":" + metadata_text(s->desc_categorized ? s->desc_categorized : s->desc) +
                ",\"description\":" +
                metadata_text(s->info_categorized ? s->info_categorized : s->info) +
                ",\"category\":" + metadata_text(s->category_key) +
                ",\"default\":" + metadata_text(value) + ",\"choices\":[";
        for (size_t i = 0; i < RETRO_NUM_CORE_OPTION_VALUES_MAX && s->values[i].value; ++i)
        {
            if (i)
                json += ',';
            json += '[' + metadata_text(s->values[i].value) + ',' +
                    metadata_text(s->values[i].label ? s->values[i].label : s->values[i].value) +
                    ']';
        }
        json += "]}";
    }
    save_metadata(name, json + "]}", defaults);
}
extern "C" void ps5_webui_core_variables(const char *name, const retro_variable *vars)
{
    if (!vars)
        return;
    Config defaults;
    std::string json = "{\"categories\":[],\"settings\":[";
    bool first = true;
    for (const auto *s = vars; s->key; ++s)
    {
        std::string value = s->value ? s->value : "";
        const size_t separator = value.find("; ");
        if (separator == std::string::npos)
            continue;
        if (!first)
            json += ',';
        first = false;
        json += "{\"key\":" + metadata_text(s->key) +
                ",\"label\":" + quote(value.substr(0, separator)) +
                ",\"description\":\"\",\"category\":\"\",\"choices\":[";
        size_t start = separator + 2;
        while (start <= value.size())
        {
            size_t end = value.find('|', start);
            if (end == std::string::npos)
                end = value.size();
            if (start != separator + 2)
                json += ',';
            const auto raw = value.substr(start, end - start);
            if (start == separator + 2 && setting_key(s->key) && valid_setting_value(raw, "text"))
                defaults[s->key] = raw;
            auto choice = quote(raw);
            json += '[' + choice + ',' + choice + ']';
            start = end + 1;
        }
        json += "]}";
    }
    save_metadata(name, json + "]}", defaults);
}
bool ps5_webui_start(const char *root, unsigned short port)
{
    if (web_daemon)
        return true;
    root_path = root;
    apply_core_settings();
    listen_port = port;
    token = nonce();
    mkdir((root_path + "/content").c_str(), 0755);
    web_daemon = MHD_start_daemon(
        MHD_USE_INTERNAL_POLLING_THREAD | MHD_USE_ITC | MHD_USE_ERROR_LOG, port, nullptr, nullptr,
        handle, nullptr, MHD_OPTION_CONNECTION_LIMIT, unsigned(8), MHD_OPTION_CONNECTION_TIMEOUT,
        unsigned(30), MHD_OPTION_CONNECTION_MEMORY_LIMIT, size_t(65536),
        MHD_OPTION_THREAD_STACK_SIZE, size_t(256 * 1024), MHD_OPTION_NOTIFY_COMPLETED, completed,
        nullptr, MHD_OPTION_END);
    std::fprintf(stderr, "webui: %s port=%u\n", web_daemon ? "listening" : "unavailable", port);
    return web_daemon != nullptr;
}
void ps5_webui_stop()
{
    if (web_daemon)
    {
        MHD_stop_daemon(web_daemon);
        web_daemon = nullptr;
        std::fprintf(stderr, "webui: stopped\n");
    }
}
