// Read-only SMB namespace. The small local catalog is metadata, never media/game bytes.
#ifndef PS5_ESDE_SMB_CATALOG_H
#define PS5_ESDE_SMB_CATALOG_H
#include <pugixml.hpp>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace SmbCatalog {
inline bool remote(const std::string& path) { return path.rfind("smb://", 0) == 0; }

// Preserve the URL scheme; never resolve .. beyond the share or interpret %/#+.
inline std::string canonical(const std::string& path)
{
    if (!remote(path)) return path;
    std::vector<std::string> parts;
    for (size_t at = 6; at <= path.size();) {
        const auto end = path.find('/', at);
        const auto part = path.substr(at, end == std::string::npos ? end : end - at);
        if (part == "..") {
            if (parts.size() <= 2) return {};
            parts.pop_back();
        }
        else if (!part.empty() && part != ".") parts.push_back(part);
        if (end == std::string::npos) break;
        at = end + 1;
    }
    if (parts.size() < 2 || parts[0].find('@') != std::string::npos) return {};
    std::string out = "smb://" + parts[0];
    for (size_t i = 1; i < parts.size(); ++i) out += '/' + parts[i];
    return out;
}

struct Catalog {
    std::string root;
    std::map<std::string, uint64_t> files;
    std::set<std::string> directories;
    std::map<std::pair<std::string, std::string>, std::string> media;
    explicit Catalog(const char* filename)
    {
        pugi::xml_document doc;
        if (!doc.load_file(filename)) return;
        const auto catalog = doc.child("smbCatalog");
        root = canonical(catalog.attribute("root").value());
        if (!remote(root) || catalog.attribute("version").as_int() != 1) {
            root.clear(); return;
        }
        for (auto node : catalog.children("file")) {
            const auto path = canonical(node.attribute("path").value());
            const auto size = node.attribute("size").as_ullong();
            if (path.rfind(root + '/', 0) != 0 || !size) continue;
            files[path] = size;
            for (std::string dir = path.substr(0, path.rfind('/')); dir.size() >= root.size();
                 dir = dir.substr(0, dir.rfind('/'))) {
                directories.insert(dir);
                if (dir == root) break;
            }
        }
        for (auto node : catalog.children("media")) {
            const auto game = canonical(node.attribute("game").value());
            const auto path = canonical(node.attribute("path").value());
            if (files.count(game) && files.count(path))
                media[{game, node.attribute("kind").value()}] = path;
        }
        if (files.empty()) root.clear();
    }
};

inline const Catalog& catalog()
{
#ifndef PS5_SMB_CATALOG
#define PS5_SMB_CATALOG "/app0/es-de/ES-DE/smb-catalog.xml"
#endif
    static const Catalog value(PS5_SMB_CATALOG);
    return value;
}
inline bool enabled() { return !catalog().root.empty(); }
inline uint64_t size(const std::string& path) {
    const auto it = catalog().files.find(canonical(path));
    return it == catalog().files.end() ? 0 : it->second;
}
inline bool directory(const std::string& path) {
    return catalog().directories.count(canonical(path)) != 0;
}
inline bool exists(const std::string& path) { return size(path) || directory(path); }
inline std::string media(const std::string& game, const std::string& kind) {
    const auto it = catalog().media.find({canonical(game), kind});
    return it == catalog().media.end() ? "" : it->second;
}
}
#endif
