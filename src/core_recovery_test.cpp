/* Opt-in refused-load test against the actual menu/task pipeline. No ROM runs. */
extern "C"
{
#include <runloop.h>
#include <paths.h>
#include <gfx/video_driver.h>
#include <tasks/task_content.h>
    bool ps5_core_recovery_test_pending();
    const char *ps5_core_loader_test_path();
    const char *ps5_core_loader_test_name();
    void *ps5_core_dlopen(const char *, int);
    void *ps5_core_dlsym(void *, const char *);
    int ps5_core_dlclose(void *);
    const char *ps5_frontend_build_identity();
}
#include <cstdio>
#include <cstring>

extern "C" void ps5_core_recovery_test_if_requested()
{
    if (!ps5_core_recovery_test_pending())
        return;
    auto *runloop = runloop_state_get_ptr();
    auto *video = video_state_get_ptr();
    void *context = video->data;
    bool passed = false;
    bool selection = true, content = true;
    bool menu_core_load = false;
    if (context && (runloop->flags & RUNLOOP_FLAG_IS_INITED) &&
        runloop->current_core_type == CORE_TYPE_DUMMY)
    {
        char old_paths[3][PATH_MAX_LENGTH];
        const rarch_path_type slots[] = {RARCH_PATH_CORE, RARCH_PATH_CORE_LAST, RARCH_PATH_CONTENT};
        for (unsigned i = 0; i < 3; ++i)
            std::snprintf(old_paths[i], sizeof(old_paths[i]), "%s", path_get(slots[i]));
        // Exercise allocation, I/O and execution with the full frontend resident.
        if (void *core = ps5_core_dlopen(ps5_core_loader_test_path(), 0))
        {
            auto api = reinterpret_cast<unsigned (*)()>(ps5_core_dlsym(core, "retro_api_version"));
            menu_core_load = api && api() == RETRO_API_VERSION;
            menu_core_load = ps5_core_dlclose(core) == 0 && menu_core_load;
        }
        content_ctx_info_t info{};
        const char *missing = "/app0/cores/__missing_loader_test__.so";
        selection =
            task_push_load_new_core(missing, nullptr, &info, CORE_TYPE_PLAIN, nullptr, nullptr);
        content = task_push_load_content_with_new_core_from_menu(
            missing, "/app0/__not_loaded_test__.nes", &info, CORE_TYPE_PLAIN, nullptr, nullptr);
        passed = menu_core_load && !selection && !content && context == video->data &&
                 (runloop->flags & RUNLOOP_FLAG_IS_INITED) &&
                 runloop->current_core_type == CORE_TYPE_DUMMY;
        for (unsigned i = 0; i < 3; ++i)
            path_set(slots[i], old_paths[i]);
    }
    std::fprintf(stderr,
                 "core recovery test: passed=%d selection_accepted=%d content_accepted=%d "
                 "menu_context_preserved=%d menu_core_load=%d\n",
                 passed, selection, content, context && context == video->data, menu_core_load);
    if (FILE *out = std::fopen("/app0/core-recovery-test.json", "w"))
    {
        std::fprintf(out,
                     "{\"build_identity\":\"%s\",\"core\":\"%s\",\"passed\":%s,\"selection_"
                     "rejected\":%s,\"content_"
                     "rejected\":%s,\"menu_context_preserved\":%s,\"menu_core_load\":%s}\n",
                     ps5_frontend_build_identity(), ps5_core_loader_test_name(),
                     passed ? "true" : "false", selection ? "false" : "true",
                     content ? "false" : "true",
                     context && context == video->data ? "true" : "false",
                     menu_core_load ? "true" : "false");
        std::fclose(out);
    }
}
