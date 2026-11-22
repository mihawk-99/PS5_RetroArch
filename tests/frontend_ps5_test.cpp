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
std::vector<std::string> root_paths;
std::vector<std::string> root_names;
// Drive slots that hold a mounted drive: their stat reports another device.
std::vector<std::string> mounted;
std::string physical(const char *path)
{
    std::string p(path);
    if (p == "/" || p == "/app0" || p.find("/app0/") == 0 || p == "/data" ||
        p.find("/data/") == 0 || p == "/mnt" || p.find("/mnt/") == 0)
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
        const int result = __real_stat(physical(p).c_str(), s);
        for (const std::string &slot : mounted)
            if (result == 0 && slot == p)
                s->st_dev += 1;
        return result;
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
    bool menu_entries_append(file_list_t *list, const char *path, const char *,
                             msg_hash_enums label, unsigned type, size_t, size_t, rarch_setting_t *)
    {
        assert(label == MENU_ENUM_LABEL_FILE_DETECT_CORE_LIST_PUSH_DIR ||
               label == MENU_ENUM_LABEL_FILE_BROWSER_DIRECTORY);
        assert(type == FILE_TYPE_DIRECTORY);
        root_paths.emplace_back(path);
        ++list->size;
        return true;
    }
    void file_list_set_alt_at_offset(file_list_t *list, size_t index, const char *alt)
    {
        assert(index == list->size - 1 && index == root_names.size());
        root_names.emplace_back(alt);
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
    for (const char *name : {"config", "cores", "content", "system", "savefiles", "savestates",
                             "playlists", "system/Saturn"})
    {
        struct stat metadata{};
        assert(__real_stat((fixture + "/app0/" + name).c_str(), &metadata) == 0);
        assert((metadata.st_mode & 0777) == 0777);
    }
    assert(read(fixture + "/app0/config/retroarch.cfg") == "audio_driver = \"ps5\"\n");
    assert(std::string(g_defaults.path_config) == "/app0/config/retroarch.cfg");
    assert(std::string(g_defaults.dirs[DEFAULT_DIR_CORE]) == "/app0/cores");
    assert(std::string(g_defaults.dirs[DEFAULT_DIR_MENU_CONTENT]) == "/app0");
    assert(std::string(g_defaults.dirs[DEFAULT_DIR_REMAP]) == "/app0/config/remaps");
    assert(std::filesystem::is_directory(fixture + "/app0/content"));
    /* The old bare name: content's system folders are made in main.cpp (src/ps5_library.c). */
    assert(!std::filesystem::exists(fixture + "/app0/content/Saturn"));
    std::ofstream(fixture + "/app0/config/retroarch.cfg") << "user settings\n";
    std::ofstream(fixture + "/app0/retroarch.cfg") << "updated seed\n";
    frontend_ctx_ps5.init(nullptr);
    assert(read(fixture + "/app0/config/retroarch.cfg") == "user settings\n");
    assert(!std::filesystem::exists(fixture + "/app0/config/retroarch.cfg.tmp"));
    // Graphics widgets, on once: a config a build without them saved has them off. The
    // first start above found no such line and left its marker, so the user's later
    // choice of off stays; without the marker the line is turned on, once.
    const std::string marker = fixture + "/app0/config/widgets-default.done";
    assert(std::filesystem::exists(marker));
    const std::string saved_off = "video_smooth = \"false\"\nmenu_enable_widgets = \"false\"\n"
                                  "not_menu_enable_widgets = \"false\"\n";
    std::ofstream(fixture + "/app0/config/retroarch.cfg") << saved_off;
    frontend_ctx_ps5.init(nullptr);
    assert(read(fixture + "/app0/config/retroarch.cfg") == saved_off);
    std::filesystem::remove(marker);
    frontend_ctx_ps5.init(nullptr);
    assert(read(fixture + "/app0/config/retroarch.cfg") ==
           "video_smooth = \"false\"\nmenu_enable_widgets = \"true\"\n"
           "not_menu_enable_widgets = \"false\"\n");
    assert(std::filesystem::exists(marker));
    assert(!std::filesystem::exists(fixture + "/app0/config/retroarch.cfg.tmp"));
    // The achievement unlock sound, on once the same way (its own marker).
    const std::string sound_marker = fixture + "/app0/config/unlock-sound-default.done";
    assert(std::filesystem::exists(sound_marker));
    const std::string sound_off = "cheevos_unlock_sound_enable = \"false\"\n"
                                  "menu_enable_widgets = \"false\"\n";
    std::ofstream(fixture + "/app0/config/retroarch.cfg") << sound_off;
    frontend_ctx_ps5.init(nullptr);
    assert(read(fixture + "/app0/config/retroarch.cfg") == sound_off); // both markers there
    std::filesystem::remove(sound_marker);
    frontend_ctx_ps5.init(nullptr);
    assert(read(fixture + "/app0/config/retroarch.cfg") ==
           "cheevos_unlock_sound_enable = \"true\"\nmenu_enable_widgets = \"false\"\n");
    assert(std::filesystem::exists(sound_marker));
    // A setting the WebUI saved is RetroArch's own from this start on (issue 130): it moves
    // into retroarch.cfg, and the RetroAchievements account stays where its code reads it.
    std::ofstream(fixture + "/app0/config/retroarch.cfg") << "audio_volume = \"0.000000\"\n";
    std::ofstream(fixture + "/app0/config/webui.cfg")
        << "audio_volume = \"12\"\ncheevos_username = \"someone\"\n";
    frontend_ctx_ps5.init(nullptr);
    assert(read(fixture + "/app0/config/retroarch.cfg") == "audio_volume = \"12\"\n");
    assert(read(fixture + "/app0/config/webui.cfg") == "cheevos_username = \"someone\"\n");
    std::ofstream(fixture + "/app0/config/retroarch.cfg") << "audio_volume = \"0.000000\"\n";
    frontend_ctx_ps5.init(nullptr); // the menu's later value stays
    assert(read(fixture + "/app0/config/retroarch.cfg") == "audio_volume = \"0.000000\"\n");
    std::filesystem::remove(fixture + "/app0/config/webui.cfg");
    std::ofstream(fixture + "/app0/config/retroarch.cfg") << "user settings\n";
    assert(std::string(ps5_core_system_directory("Beetle Saturn", "/app0/system")) ==
           "/app0/system/Saturn");
    assert(std::string(ps5_core_system_directory("Beetle Saturn", "/mnt/usb0/bios")) ==
           "/mnt/usb0/bios");
    assert(std::string(ps5_core_system_directory("PPSSPP", "/app0/system")) == "/app0/system");
    assert(ps5_core_system_directory(nullptr, nullptr) == nullptr);
    // A non-null environment callback prevents task_content's menu fallback from
    // rebuilding the title's startup argv. It must leave these arguments intact.
    int count = argc;
    unsigned untouched = 0x1234;
    assert(frontend_ctx_ps5.environment_get);
    frontend_ctx_ps5.environment_get(&count, argv, nullptr, &untouched);
    assert(count == argc && untouched == 0x1234);
    // Without anything mounted into the sandbox, the browser's top is the title's
    // folder and the bare mounts, as before: an empty drive slot is not a drive.
    file_list_t list{};
    assert(frontend_ctx_ps5.parse_drive_list(&list, true) == 0);
    assert((root_paths == std::vector<std::string>{"/app0", "/mnt"}));
    assert((root_names == std::vector<std::string>{"INTERNAL", "EXTERNAL"}));
    // With /data and a USB drive in the sandbox (ShadowMountPlus 1.7beta4), the
    // drive is a root of its own, by name, and EXTERNAL gives way to it; /data is
    // never offered.
    std::filesystem::create_directories(fixture + "/data/roms");
    std::ofstream(fixture + "/mnt/usb0/game.bin") << "x";
    std::filesystem::create_directories(fixture + "/mnt/ext0");
    mounted = {"/mnt/usb0"};
    root_paths.clear();
    root_names.clear();
    list = {};
    assert(frontend_ctx_ps5.parse_drive_list(&list, false) == 0);
    assert((root_paths == std::vector<std::string>{"/app0", "/mnt/usb0"}));
    assert((root_names == std::vector<std::string>{"INTERNAL", "USB 0"}));
    // SMB, after them, once the WebUI saved a server and share: the share's folder.
    std::ofstream(fixture + "/app0/config/smb.cfg")
        << "smb_server = \"192.0.2.10\"\nsmb_share = \"share\"\nsmb_folder = "
           "\"Games/Homebrew\"\n"
           "smb_user = \"\"\nsmb_password = \"\"\nsmb_workgroup = \"\"\n";
    root_paths.clear();
    root_names.clear();
    list = {};
    assert(frontend_ctx_ps5.parse_drive_list(&list, true) == 0);
    assert((root_paths == std::vector<std::string>{"/app0", "/mnt/usb0",
                                                   "smb://192.0.2.10/share/Games/Homebrew"}));
    assert((root_names == std::vector<std::string>{"INTERNAL", "USB 0", "SMB"}));
    // no share saved: no SMB
    std::ofstream(fixture + "/app0/config/smb.cfg") << "smb_server = \"192.0.2.10\"\n";
    root_paths.clear();
    root_names.clear();
    list = {};
    assert(frontend_ctx_ps5.parse_drive_list(&list, true) == 0);
    assert((root_names == std::vector<std::string>{"INTERNAL", "USB 0"}));
    frontend_ctx_ps5.init(nullptr); // the startup summary walks every root
    std::puts("frontend_ps5: config seed/preservation, directories, argv, the INTERNAL and "
              "EXTERNAL roots, the mounted storage roots and SMB PASS");
}
