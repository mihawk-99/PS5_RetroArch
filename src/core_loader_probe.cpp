/* Opt-in native loader diagnostics: no content is loaded or executed. */
#include <cstdio>
#include <cstring>
#include <libretro.h>

extern "C"
{
    const char *ps5_frontend_build_identity();
    void *ps5_core_dlopen(const char *, int);
    void *ps5_core_dlsym(void *, const char *);
    int ps5_core_dlclose(void *);
    char *ps5_core_dlerror();
}

namespace
{
bool recovery_pending = false;
const char *test_core = "fceumm";
const char *test_path = "/app0/cores/fceumm_libretro.so";
const char *test_name = "FCEUmm";
} // namespace
extern "C" const char *ps5_core_loader_test_path()
{
    return test_path;
}
extern "C" const char *ps5_core_loader_test_name()
{
    return test_core;
}

extern "C" bool ps5_core_recovery_test_pending()
{
    bool pending = recovery_pending;
    recovery_pending = false;
    return pending;
}

extern "C" void ps5_core_loader_test_if_requested()
{
    FILE *control = std::fopen("/app0/core-loader-test.txt", "r");
    if (!control)
        return;
    char selection[32] = {};
    const int fields = std::fscanf(control, "%31s", selection);
    std::fclose(control);
    std::remove("/app0/core-loader-test.txt");
    if (fields == 1 && !std::strcmp(selection, "mgba"))
    {
        test_core = "mgba";
        test_path = "/app0/cores/mgba_libretro.so";
        test_name = "mGBA";
    }
    else if (fields == 1 && !std::strcmp(selection, "snes9x"))
    {
        test_core = "snes9x";
        test_path = "/app0/cores/snes9x_libretro.so";
        test_name = "Snes9x";
    }
    else if (fields == 1 && !std::strcmp(selection, "fbneo"))
    {
        test_core = "fbneo";
        test_path = "/app0/cores/fbneo_libretro.so";
        test_name = "FinalBurn Neo";
    }
    else if (fields != 1 || std::strcmp(selection, "fceumm"))
    {
        std::fprintf(stderr, "core loader test: unsupported selection\n");
        return;
    }
    recovery_pending = true;
    const char *exports[] = {"retro_api_version",
                             "retro_init",
                             "retro_deinit",
                             "retro_run",
                             "retro_get_system_info",
                             "retro_get_system_av_info",
                             "retro_load_game",
                             "retro_load_game_special",
                             "retro_unload_game",
                             "retro_reset",
                             "retro_set_environment",
                             "retro_set_video_refresh",
                             "retro_set_audio_sample",
                             "retro_set_audio_sample_batch",
                             "retro_set_input_poll",
                             "retro_set_input_state",
                             "retro_set_controller_port_device",
                             "retro_serialize_size",
                             "retro_serialize",
                             "retro_unserialize",
                             "retro_get_region",
                             "retro_get_memory_data",
                             "retro_get_memory_size",
                             "retro_cheat_reset",
                             "retro_cheat_set"};
    bool missing = ps5_core_dlopen("/app0/cores/__missing_loader_test__.so", 0) == nullptr &&
                   ps5_core_dlerror() != nullptr;
    unsigned cycles = 0, found = 0, api = 0;
    char name[64] = {}, error[512] = {};
    bool unknown_symbol = false;
    for (unsigned i = 0; i < 8; ++i)
    {
        void *core = ps5_core_dlopen(test_path, 0);
        if (!core)
        {
            std::snprintf(error, sizeof(error), "%s", ps5_core_dlerror());
            break;
        }
        found = 0;
        for (const char *symbol : exports)
            found += ps5_core_dlsym(core, symbol) != nullptr;
        if (found == 25)
        {
            auto version =
                reinterpret_cast<unsigned (*)()>(ps5_core_dlsym(core, "retro_api_version"));
            auto info = reinterpret_cast<void (*)(retro_system_info *)>(
                ps5_core_dlsym(core, "retro_get_system_info"));
            retro_system_info system{};
            api = version();
            info(&system);
            std::snprintf(name, sizeof(name), "%s", system.library_name ? system.library_name : "");
            unknown_symbol = ps5_core_dlsym(core, "__missing_export__") == nullptr &&
                             ps5_core_dlerror() != nullptr;
        }
        if (ps5_core_dlclose(core) || found != 25 || api != RETRO_API_VERSION ||
            std::strcmp(name, test_name))
            break;
        ++cycles;
    }
    const bool passed = missing && unknown_symbol && cycles == 8;
    std::fprintf(stderr,
                 "core loader test: passed=%d cycles=%u exports=%u api=%u name=%s error=%s\n",
                 passed, cycles, found, api, name, error);
    if (FILE *out = std::fopen("/app0/core-loader-test.json", "w"))
    {
        std::fprintf(
            out,
            "{\"build_identity\":\"%s\",\"core\":\"%s\",\"passed\":%s,\"cycles\":%u,\"exports\":%u,"
            "\"api\":%u,\"missing_rejected\":%s,\"unknown_symbol_rejected\":%s}\n",
            ps5_frontend_build_identity(), test_core, passed ? "true" : "false", cycles, found, api,
            missing ? "true" : "false", unknown_symbol ? "true" : "false");
        std::fclose(out);
    }
}
