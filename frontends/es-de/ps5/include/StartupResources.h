// PS5-only, startup-scoped reuse of immutable local theme resources.
// Never caches games, SMB, settings or user metadata; releases everything before navigation.
#ifndef PS5_ESDE_STARTUP_RESOURCES_H
#define PS5_ESDE_STARTUP_RESOURCES_H
#include <pugixml.hpp>
#include <cerrno>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <sys/stat.h>

namespace StartupResources {
struct State {
    std::mutex mutex;
    bool active = false;
    std::string root;
    std::map<std::string, std::pair<bool, bool>> stats;
    std::map<std::string, std::shared_ptr<pugi::xml_document>> xml;
    size_t bytes = 0, statHits = 0, xmlHits = 0;
};
inline State& state() { static State value; return value; }
inline bool eligible(const State& s, const std::string& path) {
    return s.active && path.rfind(s.root, 0) == 0 &&
        path.find("/../") == std::string::npos &&
        (path.rfind(s.root + "resources/", 0) == 0 ||
         path.rfind(s.root + "themes/", 0) == 0 ||
         path.rfind(s.root + "ES-DE/themes/", 0) == 0);
}
inline void begin(const std::string& root = "/app0/es-de/") {
    auto& s = state(); std::lock_guard<std::mutex> lock(s.mutex);
    s.stats.clear(); s.xml.clear(); s.bytes = s.statHits = s.xmlHits = 0;
    s.root = root; s.active = true;
}
inline void end() {
    auto& s = state(); std::lock_guard<std::mutex> lock(s.mutex);
    std::printf("[ES-DE startup] resource reuse: %zu stat hits, %zu XML hits, %zu XML bytes\n",
                s.statHits, s.xmlHits, s.bytes);
    s.active = false; s.stats.clear(); s.xml.clear(); s.bytes = 0;
}
// Returns false when outside the scope, so the original filesystem implementation runs.
inline bool query(const std::string& path, bool directory, bool& result) {
    auto& s = state(); std::lock_guard<std::mutex> lock(s.mutex);
    if (!eligible(s, path)) return false;
    auto found = s.stats.find(path);
    if (found == s.stats.end()) {
        struct stat info {};
        const bool exists = ::stat(path.c_str(), &info) == 0;
        if (!exists && errno != ENOENT && errno != ENOTDIR) return false;
        const auto value = std::make_pair(exists, exists && S_ISDIR(info.st_mode));
        result = directory ? value.second : value.first;
        if (s.stats.size() < 8192) s.stats.emplace(path, value);
    } else {
        ++s.statHits;
        result = directory ? found->second.second : found->second.first;
    }
    return true;
}
inline pugi::xml_parse_result loadXml(pugi::xml_document& doc, const std::string& path) {
    auto& s = state(); std::lock_guard<std::mutex> lock(s.mutex);
    if (!eligible(s, path)) return doc.load_file(path.c_str());
    auto found = s.xml.find(path);
    if (found != s.xml.end()) {
        ++s.xmlHits;
        doc.reset(*found->second); // Each caller gets its own mutable tree/variables.
        pugi::xml_parse_result result; result.status = pugi::status_ok; return result;
    }
    const auto result = doc.load_file(path.c_str());
    struct stat info {};
    if (result && s.xml.size() < 256 && ::stat(path.c_str(), &info) == 0 && info.st_size >= 0 &&
        static_cast<size_t>(info.st_size) <= 2 * 1024 * 1024 - s.bytes) {
        auto copy = std::make_shared<pugi::xml_document>(); copy->reset(doc);
        s.xml.emplace(path, std::move(copy)); s.bytes += static_cast<size_t>(info.st_size);
    }
    return result;
}
}
#endif
