/* Copyright (C) 2026 Mihawk; SPDX-License-Identifier: GPL-3.0-or-later */
#include "webui_update.h"
#include "scraper_http.h"
#include <unzip.h>
#include <mbedtls/sha256.h>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <sys/stat.h>
#include <pthread.h>
#include <unistd.h>
#include <vector>
#ifdef __PROSPERO__
extern "C"
{
    int sceNetInit();
    int sceNetPoolCreate(const char *, int, int);
    int sceNetPoolDestroy(int);
    int sceSslInit(size_t);
    int sceSslTerm(int);
    int sceHttp2Init(int, int, size_t, int);
    int sceHttp2Term(int);
    int sceHttp2CreateTemplate(int, const char *, int, int);
    int sceHttp2DeleteTemplate(int);
    int sceHttp2CreateRequestWithURL(int, const char *, const char *, uint64_t);
    int sceHttp2DeleteRequest(int);
    int sceHttp2SendRequest(int, const void *, size_t);
    int sceHttp2GetStatusCode(int, int *);
    int sceHttp2ReadData(int, void *, size_t);
    int sceHttp2SetAutoRedirect(int, int);
    int sceHttp2SetConnectTimeOut(int, unsigned);
    int sceHttp2SetRecvTimeOut(int, unsigned);
}
#endif
namespace ps5_update
{
namespace
{
std::mutex guard;
Status current{"idle", "", "", 0};
std::string root;
pthread_t worker{};
bool worker_running = false;
std::atomic<bool> cancel{false}, quit{false};
constexpr uint64_t max_zip = UINT64_C(2) * 1024 * 1024 * 1024;
constexpr uint64_t max_expanded = UINT64_C(8) * 1024 * 1024 * 1024;
void report(const char *state, const std::string &message)
{
    std::lock_guard<std::mutex> lock(guard);
    current.state = state;
    current.message = message;
}
bool regular(const std::string &p)
{
    int fd = open(p.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0)
        return false;
    struct stat st{};
    bool ok = !fstat(fd, &st) && S_ISREG(st.st_mode);
    close(fd);
    return ok;
}
bool exists(const std::string &p)
{
    int fd = open(p.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd >= 0)
    {
        close(fd);
        return true;
    }
    return errno != ENOENT;
}
bool path_ok(const std::string &p)
{
    if (p.empty() || p.size() > 1024 || p.front() == '/' || p.back() == '/')
        return false;
    size_t start = 0;
    while (start < p.size())
    {
        size_t end = p.find('/', start);
        auto part = p.substr(start, end - start);
        if (part.empty() || part == "." || part == ".." || part.size() > 255)
            return false;
        for (unsigned char c : part)
            if (c < 32 || c == 127 || c == '\\' || c == ':')
                return false;
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return true;
}
bool parents(const std::string &base, const std::string &relative)
{
    std::string p = base;
    for (size_t i = 0; (i = relative.find('/', i)) != std::string::npos; ++i)
    {
        p = base + '/' + relative.substr(0, i);
        if (mkdir(p.c_str(), 0755) && errno != EEXIST)
            return false;
        int fd = open(p.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
        if (fd < 0)
            return false;
        struct stat st{};
        bool ok = !fstat(fd, &st) && S_ISDIR(st.st_mode);
        close(fd);
        if (!ok)
            return false;
    }
    return true;
}
bool write_all(int fd, const void *data, size_t n)
{
    auto p = static_cast<const char *>(data);
    while (n)
    {
        ssize_t w = write(fd, p, n);
        if (w < 0 && errno == EINTR)
            continue;
        if (w <= 0)
            return false;
        p += w;
        n -= size_t(w);
    }
    return true;
}
bool save(const std::string &p, const std::string &s)
{
    int fd = open(p.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0644);
    if (fd < 0)
        return false;
    bool ok = write_all(fd, s.data(), s.size()) && !fsync(fd);
    if (close(fd))
        ok = false;
    return ok;
}
std::string read_text(const std::string &p, size_t limit = 4 * 1024 * 1024)
{
    int fd = open(p.c_str(), O_RDONLY | O_NOFOLLOW);
    if (fd < 0)
        return {};
    std::string out;
    char buf[4096];
    ssize_t n;
    while ((n = read(fd, buf, sizeof buf)) > 0)
    {
        if (out.size() + size_t(n) > limit)
        {
            out.clear();
            break;
        }
        out.append(buf, size_t(n));
    }
    close(fd);
    return n < 0 ? std::string() : out;
}
std::string digest(const std::string &p)
{
    int fd = open(p.c_str(), O_RDONLY | O_NOFOLLOW);
    if (fd < 0)
        return {};
    mbedtls_sha256_context c;
    mbedtls_sha256_init(&c);
    mbedtls_sha256_starts(&c, 0);
    // On the heap: the worker's stack in the WebUI daemon is a payload thread's, and a
    // 64 KiB frame faulted there the moment a download was verified (issue 75).
    std::vector<unsigned char> buf(65536);
    unsigned char sum[32];
    ssize_t n = 0;
    while (!cancel && (n = read(fd, buf.data(), buf.size())) > 0)
        mbedtls_sha256_update(&c, buf.data(), size_t(n));
    close(fd);
    mbedtls_sha256_finish(&c, sum);
    mbedtls_sha256_free(&c);
    if (n < 0 || cancel)
        return {};
    std::string out;
    for (auto x : sum)
    {
        char h[3];
        std::snprintf(h, sizeof h, "%02x", x);
        out += h;
    }
    return out;
}
bool hash_ok(const std::string &s)
{
    return s.size() == 64 && s.find_first_not_of("0123456789abcdef") == std::string::npos;
}
std::map<std::string, std::string> manifest(const std::string &text)
{
    std::map<std::string, std::string> out;
    size_t start = 0;
    while (start < text.size())
    {
        size_t end = text.find('\n', start);
        auto l = text.substr(start, end - start);
        if (l.size() < 67 || l.substr(64, 2) != " *" || !hash_ok(l.substr(0, 64)) ||
            !path_ok(l.substr(66)) || out.count(l.substr(66)))
            return {};
        out[l.substr(66)] = l.substr(0, 64);
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return out;
}
bool managed(const std::string &p)
{
    // Everything a release ships but the user's own: content/ and the default retroarch.cfg.
    // es-de/, picker/, forwarder/ and the Lapy service came after this list was written,
    // and an update left them at the old version (issue 75). ES-DE's own data is in
    // es-de/ES-DE, which no release carries.
    const std::set<std::string> files = {"eboot.bin",         "libvulkan.so.1",
                                         "LEGAL.txt",         "manifest.sha256",
                                         "video-assets.json", "lapy-root-daemon.elf"};
    const std::set<std::string> dirs = {"assets",     "cores", "info",    "licenses", "sce_sys",
                                        "sce_module", "webui", "shaders", "filters",  "overlays",
                                        "system",     "es-de", "picker",  "forwarder"};
    return files.count(p) || dirs.count(p.substr(0, p.find('/')));
}
bool remove_tree(const std::string &p, bool interruptible = false)
{
    if (interruptible && cancel)
        return false;
    int fd = open(p.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0)
        return errno == ENOENT;
    struct stat st{};
    if (fstat(fd, &st))
    {
        close(fd);
        return false;
    }
    close(fd);
    if (!S_ISDIR(st.st_mode))
        return !unlink(p.c_str());
    DIR *dir = opendir(p.c_str());
    if (!dir)
        return false;
    bool ok = true;
    while (auto *e = readdir(dir))
        if (std::strcmp(e->d_name, ".") && std::strcmp(e->d_name, ".."))
            ok = remove_tree(p + '/' + e->d_name, interruptible) && ok;
    closedir(dir);
    return ok && !rmdir(p.c_str());
}
bool tag_ok(const std::string &s)
{
    return s.size() > 1 && s.size() < 64 && s[0] == 'v' && s[1] >= '0' && s[1] <= '9' &&
           s.find("..") == std::string::npos &&
           s.find_first_not_of("v0123456789abcdefghijklmnopqrstuvwxyz.-") == std::string::npos;
}
bool fetch(const std::string &url, const std::string &file, uint64_t limit)
{
#if defined(PS5_SCRAPER_CURL)
    // The WebUI's daemon: libcurl over mbedTLS (src/scraper_http.h). Sony's SSL failed
    // every handshake from the payload (0x8095f00c), and the daemon runs the updater.
    const int fd = open(file.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0644);
    if (fd < 0)
        return false;
    ps5_scraper::Http http("PS5-RetroArch-Updater/1");
    const auto response = http.get(url, limit, &cancel, fd,
                                   [](uint64_t received)
                                   {
                                       std::lock_guard<std::mutex> lock(guard);
                                       current.received = received;
                                   });
    const bool ok = response.status == 200 && response.error.empty() && !response.cancelled &&
                    !response.too_large && fsync(fd) == 0;
    close(fd);
    return ok;
#endif
#if defined(__PROSPERO__) && !defined(PS5_SCRAPER_CURL)
    // Keep the platform's certificate/hostname checks enabled.
    struct Handles
    {
        int pool = -1, ssl = -1, http = -1, templ = -1, req = -1, fd = -1;
        ~Handles()
        {
            if (fd >= 0)
                close(fd);
            if (req >= 0)
                sceHttp2DeleteRequest(req);
            if (templ >= 0)
                sceHttp2DeleteTemplate(templ);
            if (http >= 0)
                sceHttp2Term(http);
            if (ssl >= 0)
                sceSslTerm(ssl);
            if (pool >= 0)
                sceNetPoolDestroy(pool);
        }
    } h;
    (void)sceNetInit();
    h.pool = sceNetPoolCreate("RetroArch update", 1024 * 1024, 0);
    if (h.pool < 0)
        return false;
    h.ssl = sceSslInit(1024 * 1024);
    if (h.ssl < 0)
        return false;
    h.http = sceHttp2Init(h.pool, h.ssl, 1024 * 1024, 1);
    if (h.http < 0)
        return false;
    h.templ = sceHttp2CreateTemplate(h.http, "PS5-RetroArch-Updater/1", 3, 1);
    if (h.templ < 0)
        return false;
    if (sceHttp2SetAutoRedirect(h.templ, 1) < 0)
        return false;
    sceHttp2SetConnectTimeOut(h.templ, 15000000);
    sceHttp2SetRecvTimeOut(h.templ, 30000000);
    h.req = sceHttp2CreateRequestWithURL(h.templ, "GET", url.c_str(), 0);
    if (h.req < 0)
        return false;
    if (sceHttp2SendRequest(h.req, nullptr, 0) < 0)
        return false;
    int code = 0;
    if (sceHttp2GetStatusCode(h.req, &code) < 0 || code != 200)
        return false;
    h.fd = open(file.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0644);
    if (h.fd < 0)
        return false;
    std::vector<char> buf(65536);
    int n;
    uint64_t total = 0;
    while (!cancel && (n = sceHttp2ReadData(h.req, buf.data(), buf.size())) > 0)
    {
        total += unsigned(n);
        if (total > limit || !write_all(h.fd, buf.data(), size_t(n)))
            return false;
        std::lock_guard<std::mutex> lock(guard);
        current.received = total;
    }
    return !cancel && n == 0 && !fsync(h.fd);
#else
    (void)url;
    (void)file;
    (void)limit;
    return false;
#endif
}
} // namespace
Status status()
{
    std::lock_guard<std::mutex> lock(guard);
    return current;
}
bool recover(const std::string &base)
{
    const auto journal = read_text(base + "/.update/journal");
    if (journal.empty())
        return !exists(base + "/.update/journal");
    std::vector<std::string> paths;
    size_t start = 0;
    while (start < journal.size())
    {
        auto e = journal.find('\n', start);
        auto p = journal.substr(start, e - start);
        if (!path_ok(p) || !managed(p))
            return false;
        paths.push_back(p);
        if (e == std::string::npos)
            break;
        start = e + 1;
    }
    bool ok = true;
    for (auto it = paths.rbegin(); it != paths.rend(); ++it)
    {
        auto dest = base + '/' + *it, old = base + "/.update/backup/" + *it,
             staged = base + "/.update/stage/" + *it;
        if (regular(old))
        {
            if (!parents(base, *it) || rename(old.c_str(), dest.c_str()))
                ok = false;
        }
        else if (!regular(staged) && exists(dest) && unlink(dest.c_str()))
            ok = false;
    }
    if (ok)
        ok = !unlink((base + "/.update/journal").c_str());
    return ok;
}
void initialize(const std::string &base)
{
    root = base;
    quit = false;
    cancel = false;
    if (!recover(root))
        report("error",
               "An interrupted update could not be restored. Reinstall the complete package.");
    else
    {
        const auto result = read_text(root + "/.update-result", 4096);
        if (!result.empty())
            report(result.rfind("OK\n", 0) == 0 ? "installed" : "error",
                   result.substr(result.find('\n') + 1));
        else
            report("idle", "");
    }
}
bool prepare(const std::string &base, const std::string &zip, const std::string &sha)
{
    if (!hash_ok(sha) || digest(zip) != sha)
    {
        report("error", "The download checksum did not match. Nothing was installed.");
        return false;
    }
    const std::string stage = base + "/.update/stage";
    if (!remove_tree(stage, true) || mkdir(stage.c_str(), 0755))
        return false;
    unzFile z = unzOpen64(zip.c_str());
    if (!z)
        return false;
    // Read the verified package's manifest first. Unchanged files still pass
    // through the decompressor, CRC and SHA checks, but need no disk writes.
    std::string inventory;
    std::vector<char> block(65536); // on the heap, as digest()'s
    char *const buffer = block.data();
    const unsigned buffer_size = unsigned(block.size());
    int count = 0;
    if (unzLocateFile(z, "PPSA99169/manifest.sha256", 1) == UNZ_OK &&
        unzOpenCurrentFile(z) == UNZ_OK)
    {
        while (!cancel && (count = unzReadCurrentFile(z, buffer, buffer_size)) > 0)
        {
            inventory.append(buffer, size_t(count));
            if (inventory.size() > 4 * 1024 * 1024)
            {
                inventory.clear();
                break;
            }
        }
        if (unzCloseCurrentFile(z) != UNZ_OK || count < 0)
            inventory.clear();
    }
    auto records = manifest(inventory), old = manifest(read_text(base + "/manifest.sha256"));
    if (records.empty())
    {
        unzClose(z);
        report("error", "The release manifest is invalid. Nothing was installed.");
        return false;
    }
    std::set<std::string> entries;
    uint64_t expanded = 0;
    bool ok = true;
    std::string plan;
    int step = unzGoToFirstFile(z);
    while (step == UNZ_OK && !cancel)
    {
        char name[1200]{};
        unz_file_info64 info{};
        if (unzGetCurrentFileInfo64(z, &info, name, sizeof name, nullptr, 0, nullptr, 0) !=
                UNZ_OK ||
            info.size_filename >= sizeof name || std::strlen(name) != info.size_filename)
        {
            ok = false;
            break;
        }
        std::string p = name;
        const std::string prefix = "PPSA99169/";
        if (p.compare(0, prefix.size(), prefix) || p.find("test_overlays") != std::string::npos)
        {
            ok = false;
            break;
        }
        p = p.substr(prefix.size());
        unsigned mode = (info.external_fa >> 16) & 0170000;
        // An empty folder of the package (a release carries content/<system>/ for every
        // system): not a file, so not in the manifest. Refusing it refused every release
        // (issue 75). A content folder that is missing is made; nothing else is touched.
        if (!p.empty() && p.back() == '/')
        {
            const std::string folder = p.substr(0, p.size() - 1);
            if (!path_ok(folder) || (mode && mode != 0040000) || info.uncompressed_size ||
                (folder.rfind("content/", 0) == 0 && !parents(base, p + '.')))
            {
                ok = false;
                break;
            }
            step = unzGoToNextFile(z);
            continue;
        }
        if (!path_ok(p) || (mode && mode != 0100000) || !entries.insert(p).second ||
            entries.size() > 30000 || info.uncompressed_size > max_expanded - expanded)
        {
            ok = false;
            break;
        }
        expanded += info.uncompressed_size;
        bool is_manifest = p == "manifest.sha256";
        auto expected = records.find(p);
        if (!is_manifest && expected == records.end())
        {
            ok = false;
            break;
        }
        bool stage_file = managed(p);
        if (stage_file && exists(base + '/' + p))
        {
            if (!regular(base + '/' + p))
            {
                ok = false;
                break;
            }
            const auto installed_hash = digest(base + '/' + p);
            if (!is_manifest && p != "eboot.bin" && installed_hash == expected->second)
                stage_file = false;
            if (p.rfind("system/", 0) == 0 || p.rfind("shaders/", 0) == 0 ||
                p.rfind("filters/", 0) == 0 || p.rfind("overlays/", 0) == 0)
            {
                auto previous = old.find(p);
                if (previous == old.end() || installed_hash != previous->second)
                    stage_file = false;
            }
        }
        if ((stage_file && !parents(stage, p)) || unzOpenCurrentFile(z) != UNZ_OK)
        {
            ok = false;
            break;
        }
        int fd = stage_file ? open((stage + '/' + p).c_str(),
                                   O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0644)
                            : -1;
        if (stage_file && fd < 0)
        {
            unzCloseCurrentFile(z);
            ok = false;
            break;
        }
        mbedtls_sha256_context hash;
        mbedtls_sha256_init(&hash);
        mbedtls_sha256_starts(&hash, 0);
        int n = 0;
        uint64_t size = 0;
        while (!cancel && (n = unzReadCurrentFile(z, buffer, buffer_size)) > 0)
        {
            size += unsigned(n);
            mbedtls_sha256_update(&hash, reinterpret_cast<unsigned char *>(buffer), size_t(n));
            if (size > info.uncompressed_size || (stage_file && !write_all(fd, buffer, size_t(n))))
            {
                ok = false;
                break;
            }
        }
        unsigned char sum[32];
        mbedtls_sha256_finish(&hash, sum);
        mbedtls_sha256_free(&hash);
        std::string actual;
        for (auto byte : sum)
        {
            char hex[3];
            std::snprintf(hex, sizeof hex, "%02x", byte);
            actual += hex;
        }
        if (n < 0 || cancel || size != info.uncompressed_size ||
            (!is_manifest && actual != expected->second))
            ok = false;
        if (fd >= 0)
        {
            // The title launcher requires execute permission on eboot.bin.
            // Apply it explicitly: creation modes are affected by the process umask.
            const mode_t mode = (p == "eboot.bin" || (info.external_fa >> 16 & 0111)) ? 0755 : 0666;
            if (fchmod(fd, mode))
                ok = false;
            if (fsync(fd))
                ok = false;
            if (close(fd))
                ok = false;
        }
        if (unzCloseCurrentFile(z) != UNZ_OK)
            ok = false;
        if (!ok)
            break;
        if (stage_file && !is_manifest && p != "eboot.bin")
            plan += p + '\n';
        if (entries.size() % 64 == 0)
            report("verifying",
                   "Preparing the update… " + std::to_string(entries.size()) + " files checked.");
        step = unzGoToNextFile(z);
    }
    unzClose(z);
    ok = ok && !cancel && step == UNZ_END_OF_LIST_OF_FILE && records.size() + 1 == entries.size();
    for (const char *required :
         {"eboot.bin", "sce_module/libc.prx", "sce_sys/param.json", "webui/version.json",
          "licenses/components.json", "video-assets.json"})
        if (!records.count(required) || !entries.count(required))
            ok = false;
    if (!ok)
    {
        report("error",
               "The release package or installation path is invalid. Nothing was installed.");
        return false;
    }
    plan += "manifest.sha256\neboot.bin\n";
    if (!save(base + "/.update/plan", plan))
        return false;
    report("ready", "Download verified. Installing will close RetroArch. Save your game first.");
    return true;
}
bool download(const std::string &tag)
{
    if (!tag_ok(tag))
        return false;
    auto s = status();
    if (s.state == "downloading" || s.state == "verifying" || s.state == "installing" ||
        exists(root + "/.update/journal"))
        return false;
    if (worker_running)
    {
        pthread_join(worker, nullptr);
        worker_running = false;
    }
    cancel = false;
    {
        std::lock_guard<std::mutex> lock(guard);
        current = {"downloading", "Preparing space on your console…", tag, 0};
    }
    // A payload thread's default stack is small; the scraper's workers get the same.
    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    pthread_attr_setstacksize(&attributes, 1u << 20);
    const int created = pthread_create(
        &worker, &attributes,
        [](void *) -> void *
        {
            const auto tag = status().tag;
            auto run = [&]
            {
                if (!remove_tree(root + "/.update", true) ||
                    mkdir((root + "/.update").c_str(), 0755))
                {
                    report("error",
                           "Could not prepare update storage. Check console storage and retry.");
                    return;
                }
                report("downloading", "Downloading the release to your console…");
                const auto name = "PS5_RetroArch-" + tag + ".zip";
                const auto url = "https://github.com/mihawk-99/PS5_RetroArch/releases/download/" +
                                 tag + '/' + name;
                const auto zip = root + "/.update/release.zip",
                           checksum = root + "/.update/checksum";
                if (!fetch(url + ".sha256", checksum, 4096) || !fetch(url, zip, max_zip))
                {
                    report("error", "Download failed. Check the console internet connection "
                                    "and available storage, then retry.");
                    return;
                }
                auto sum = read_text(checksum, 4096);
                if (sum.size() < 66 || sum.find(name, 64) == std::string::npos)
                {
                    report("error", "The release checksum file is invalid.");
                    return;
                }
                report("verifying", "Verifying and preparing the update…");
                if (!prepare(root, zip, sum.substr(0, 64)) && status().state != "error")
                    report("error", "Could not prepare the update. Check available storage.");
            };
            run();
            return nullptr;
        },
        nullptr);
    pthread_attr_destroy(&attributes);
    if (created)
    {
        report("error", "Could not start the download. Retry after closing content.");
        return false;
    }
    worker_running = true;
    return true;
}
bool request_install()
{
    if (status().state != "ready")
        return false;
    report("installing", "Closing the title to install the update…");
    quit = true;
    return true;
}
bool exit_requested()
{
    return quit;
}
void stop()
{
    cancel = true;
    if (worker_running)
    {
        pthread_join(worker, nullptr);
        worker_running = false;
    }
}
bool install()
{
    if (!quit)
        return true;
    const auto plan = read_text(root + "/.update/plan");
    if (plan.empty() || !save(root + "/.update/journal", plan))
    {
        report("error", "Could not start installation.");
        return false;
    }
    if (mkdir((root + "/.update/backup").c_str(), 0755) && errno != EEXIST)
    {
        recover(root);
        save(root + "/.update-result",
             "ERROR\nCould not create the update backup. Nothing was installed.");
        return false;
    }
    size_t start = 0;
    bool ok = true;
    while (start < plan.size())
    {
        auto e = plan.find('\n', start);
        auto p = plan.substr(start, e - start);
        if (!path_ok(p) || !managed(p) || !parents(root, p) ||
            !parents(root + "/.update/backup", p))
        {
            ok = false;
            break;
        }
        auto dest = root + '/' + p, old = root + "/.update/backup/" + p,
             staged = root + "/.update/stage/" + p;
        if (exists(dest) && (!regular(dest) || rename(dest.c_str(), old.c_str())))
        {
            ok = false;
            break;
        }
        if (rename(staged.c_str(), dest.c_str()))
        {
            ok = false;
            break;
        }
        if (e == std::string::npos)
            break;
        start = e + 1;
    }
    if (!ok)
    {
        bool restored = recover(root);
        const auto message = restored
                                 ? "Installation failed; the previous files were restored."
                                 : "Installation failed. Restore the complete release package.";
        report("error", message);
        save(root + "/.update-result", std::string("ERROR\n") + message);
        return false;
    }
    if (unlink((root + "/.update/journal").c_str()))
        return false;
    save(root + "/.update-result", "OK\nUpdate installed. Check the version above.");
    remove_tree(root + "/.update");
    report("installed", "Update installed. Reopen RetroArch from your launcher.");
    return true;
}
} // namespace ps5_update
