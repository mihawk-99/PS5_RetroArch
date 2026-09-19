/* Exercise the platform frontend against an isolated host filesystem. */
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "../src/frontend_ps5.cpp"

namespace
{
std::string fixture;
std::vector<std::string> roots;
std::string physical(const char *path)
{
    std::string p(path);
    if (p == "/" || p == "/app0" || p.find("/app0/") == 0 || p == "/data" ||
        p.find("/mnt/usb") == 0)
        return fixture + p;
    return p;
}
std::string read(const std::string &path)
{
    std::ifstream in(path);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
} // namespace
extern "C"
{
    struct defaults g_defaults{};
    FILE *__real_fopen(const char *, const char *);
    int __real_stat(const char *, struct stat *);
    int __real_mkdir(const char *, mode_t);
    int __real_chmod(const char *, mode_t);
    int __real_rename(const char *, const char *);
    int __real_remove(const char *);
    FILE *__wrap_fopen(const char *p, const char *mode)
    {
        return __real_fopen(physical(p).c_str(), mode);
    }
    int __wrap_stat(const char *p, struct stat *s)
    {
        return __real_stat(physical(p).c_str(), s);
    }
    int __wrap_mkdir(const char *p, mode_t mode)
    {
        return __real_mkdir(physical(p).c_str(), mode);
    }
    int __wrap_chmod(const char *p, mode_t mode)
    {
        return __real_chmod(physical(p).c_str(), mode);
    }
    int __wrap_rename(const char *from, const char *to)
    {
        return __real_rename(physical(from).c_str(), physical(to).c_str());
    }
    int __wrap_remove(const char *p)
    {
        return __real_remove(physical(p).c_str());
    }
    DIR *ps5_opendir(const char *p)
    {
        return opendir(physical(p).c_str());
    }
    struct dirent *ps5_readdir(DIR *p)
    {
        return readdir(p);
    }
    int ps5_closedir(DIR *p)
    {
        return closedir(p);
    }
    bool menu_entries_append(file_list_t *, const char *path, const char *, msg_hash_enums label,
                             unsigned type, size_t, size_t, rarch_setting_t *)
    {
        assert(label == MENU_ENUM_LABEL_FILE_DETECT_CORE_LIST_PUSH_DIR ||
               label == MENU_ENUM_LABEL_FILE_BROWSER_DIRECTORY);
        assert(type == FILE_TYPE_DIRECTORY);
        roots.emplace_back(path);
        return true;
    }
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    fixture = argv[1];
    std::filesystem::create_directories(fixture + "/app0");
    std::filesystem::create_directories(fixture + "/mnt/usb0");
    std::ofstream(fixture + "/app0/retroarch.cfg") << "audio_driver = \"ps5\"\n";
    umask(0077); // Creation alone must not lose FTP write permission.
    std::filesystem::create_directories(fixture + "/app0/cores");
    assert(__real_chmod((fixture + "/app0/cores").c_str(), 0755) == 0);
    std::filesystem::create_directories(fixture + "/app0/content");
    assert(__real_chmod((fixture + "/app0/content").c_str(), 0775) == 0);
    frontend_ctx_ps5.init(nullptr);
    for (const char *name :
         {"config", "cores", "content", "system", "savefiles", "savestates", "playlists"})
    {
        struct stat metadata{};
        assert(__real_stat((fixture + "/app0/" + name).c_str(), &metadata) == 0);
        assert((metadata.st_mode & 0777) == 0777);
    }
    assert(read(fixture + "/app0/config/retroarch.cfg") == "audio_driver = \"ps5\"\n");
    assert(std::string(g_defaults.path_config) == "/app0/config/retroarch.cfg");
    assert(std::string(g_defaults.dirs[DEFAULT_DIR_CORE]) == "/app0/cores");
    assert(std::string(g_defaults.dirs[DEFAULT_DIR_MENU_CONTENT]) == "/app0");
    assert(std::filesystem::is_directory(fixture + "/app0/content"));
    std::ofstream(fixture + "/app0/config/retroarch.cfg") << "user settings\n";
    std::ofstream(fixture + "/app0/retroarch.cfg") << "updated seed\n";
    frontend_ctx_ps5.init(nullptr);
    assert(read(fixture + "/app0/config/retroarch.cfg") == "user settings\n");
    assert(!std::filesystem::exists(fixture + "/app0/config/retroarch.cfg.tmp"));
    // A non-null environment callback prevents task_content's menu fallback from
    // rebuilding the title's startup argv. It must leave these arguments intact.
    int count = argc;
    unsigned untouched = 0x1234;
    assert(frontend_ctx_ps5.environment_get);
    frontend_ctx_ps5.environment_get(&count, argv, nullptr, &untouched);
    assert(count == argc && untouched == 0x1234);
    assert(frontend_ctx_ps5.parse_drive_list(nullptr, true) == 0);
    assert((roots == std::vector<std::string>{"/app0", "/mnt/usb0", "/"}));
    std::puts(
        "frontend_ps5: config seed/preservation, directories, argv and accessible roots PASS");
}
