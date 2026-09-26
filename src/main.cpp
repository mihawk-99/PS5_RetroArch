/*
 * PS5 RetroArch - the title's entry point.
 *
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * RetroArch's own `main` is one line: `return rarch_main(argc, argv, NULL)`. So
 * the entry point is not reconciled with RetroArch's, it simply calls the same
 * function with the arguments this title wants, and the pipeline's `_start` stays
 * the process entry. That is the whole of the "entry point" problem the plan
 * listed.
 *
 * The display is opened by the video driver, not here: `video_ps5` owns it, and
 * this file's job is to hand RetroArch its arguments and let its runloop drive.
 *
 * Arguments, and why each is here:
 *   -f              fullscreen, which on a console is the only mode
 *   -c <path>       the config to read and write, inside the title's own folder
 *   --verbose       so the run is readable in the console's log
 *   --menu          start with the menu up rather than waiting for content: this
 *                   title has no content, and the menu is what is being proven
 *
 * Why this file writes a trace. The first run of this title on the console ended
 * with the kernel reporting `eboot.bin calls exit() exit_value=0` and nothing
 * else: no output, no crash, no message. A program that exits zero and says
 * nothing is indistinguishable from one that never reached `main`, and the
 * console's log cannot be asked which it was. So the entry point and the video
 * driver append a line per step to /app0/trace.txt, and after a run that file
 * says how far it got. See src/trace.hpp.
 */

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <new>
#include <stdexcept>
#include <system_error>
#include <typeinfo>
#include <pthread.h>
#include <unistd.h>

#include <cxxabi.h>
#include <ps5platform/platform.h>
#include <ps5platform/probe.h>

#include "trace.hpp"
#include "memory_diagnostics.hpp"
#include "../build/title_build_identity.h"

/* RetroArch's entry, in C. */
extern "C" int rarch_main(int argc, char *argv[], void *data);
extern "C" int sceSystemServiceHideSplashScreen();

extern "C" void ps5_vulkan_profile_init();
extern "C" int sceKernelAvailableFlexibleMemorySize(std::size_t *size);
extern "C" void ps5_crash_report_install();
extern "C" void ps5_core_threads_start();
extern "C" void ps5_sampler_start();
extern "C" void ps5_open_permissions();
/* ../PS5_Vulkan's driver/ps5vk_debug.h: whether VideoOut outlives a swapchain.
 * Weak, so a build without the driver links. */
extern "C" void ps5vk_display_retain(bool retain) __attribute__((weak));
extern "C" void ps5_audio_test_if_requested();
extern "C" void ps5_core_loader_test_if_requested();
extern "C" void ps5_thread_test_if_requested();
extern "C" const char *ps5_frontend_build_identity()
{
    return PS5_RETROARCH_BUILD_ID;
}

namespace
{
/* The title's own folder, as the console mounts it: the application image is at
 * /app0 and this is where a title may keep its configuration. */
constexpr const char *config_path = "/app0/config/retroarch.cfg";

/* A profiled test run (the driver's /app0/ps5vk-profile.txt survived the stale
 * test file sweep) also reports the flexible memory left every ten seconds,
 * and what the platform layer holds: the cores' executable code, their
 * direct-memory arenas and the ranges reserved for them. With the driver's
 * direct_live, a soak's leak is a line that only moves one way. Set once main
 * has swept. */
std::atomic<bool> g_memory_report{false};

/* Writes every buffered output stream out -- the trace file and RetroArch's
 * log among them -- four times a second, off the threads that log. */
void *log_flusher(void *)
{
    const timespec interval = {0, 250 * 1000 * 1000};
    for (unsigned tick = 1;; ++tick)
    {
        nanosleep(&interval, nullptr);
        if (tick % 40 == 0 && g_memory_report.load(std::memory_order_relaxed))
        {
            std::size_t flexible = 0;
            sceKernelAvailableFlexibleMemorySize(&flexible);
            std::fprintf(stderr, "memory: flexible_free=%zu KiB\n", flexible >> 10);
            char platform[192];
            ps5_platform_report(platform, sizeof(platform));
            std::fprintf(stderr, "memory: %s\n", platform);
        }
        std::fflush(nullptr);
    }
    return nullptr;
}

void start_log_flusher()
{
    pthread_t thread;
    if (pthread_create(&thread, nullptr, log_flusher, nullptr) == 0)
        pthread_detach(thread);
}

/* The platform probe (the SDK fork's platform/src/probe.c):
 * /app0/platform-probe.txt, a test file of one launch like the others, runs the
 * console capability probe in this process -- the one whose flexible budget,
 * imports, GPU window and core loader the cores live with -- and ends the
 * launch. Its words select the optional tests: "large" (4 GiB of guest memory
 * beside 1 GiB of code), "huge" (10 GiB mapped and written at once), "full"
 * (the largest direct allocation mapped and written), "jit" (the
 * shared-memory JIT interface, last, since a refused import would stop the
 * process) and "files" (the file probe: 256 MiB written and read back in the
 * title's own folder at three chunk sizes, the file removed after each). Every
 * line goes to the trace as it is measured. */
static void probe_line(void *, const char *line)
{
    ps5::debug::mark(line);
}

static bool run_platform_probe()
{
    std::FILE *const file = std::fopen("/app0/platform-probe.txt", "r");
    if (!file)
        return false;
    char words[128] = {};
    const size_t read = std::fread(words, 1, sizeof(words) - 1, file);
    words[read] = '\0';
    std::fclose(file);
    std::remove("/app0/platform-probe.txt");
    unsigned flags = 0;
    if (std::strstr(words, "large"))
        flags |= PS5_PROBE_LARGE;
    if (std::strstr(words, "huge"))
        flags |= PS5_PROBE_HUGE;
    if (std::strstr(words, "full"))
        flags |= PS5_PROBE_FULL;
    if (std::strstr(words, "jit"))
        flags |= PS5_PROBE_JIT_API;
    int failures = ps5_platform_probe(probe_line, nullptr, flags);
    if (std::strstr(words, "files"))
        failures += ps5_platform_probe_files(probe_line, nullptr, "/app0");
    ps5::debug::mark_value("platform probe: failures", failures);
    std::fflush(stderr);
    return true;
}

/* Profile 10's entry state (docs/PHASE_LOG.md): /app0/state-copy.txt names a
 * source and a destination, a line each, and a test run copies the one to the
 * other before RetroArch starts. The save-state stress keeps its states in a
 * directory of its own, so my entry state has to be there too, and the FTP
 * account may not read mine. The copy only reads my files: it never replaces a
 * file and never writes under /app0/savestates or /app0/savefiles. Testing
 * only: the file is never shipped. */
void copy_test_file()
{
    std::FILE *const list = std::fopen("/app0/state-copy.txt", "r");
    if (list == nullptr)
        return;
    char source[512] = {};
    char destination[512] = {};
    const bool named = std::fgets(source, sizeof(source), list) != nullptr &&
                       std::fgets(destination, sizeof(destination), list) != nullptr;
    std::fclose(list);
    source[std::strcspn(source, "\r\n")] = '\0';
    destination[std::strcspn(destination, "\r\n")] = '\0';
    const bool mine = std::strncmp(destination, "/app0/savestates", 16) == 0 ||
                      std::strncmp(destination, "/app0/savefiles", 15) == 0;
    bool exists = false;
    if (std::FILE *const existing = std::fopen(destination, "rb"))
    {
        exists = true;
        std::fclose(existing);
    }
    long long copied = -1;
    std::FILE *const in = named && !mine && !exists ? std::fopen(source, "rb") : nullptr;
    std::FILE *const out = in != nullptr ? std::fopen(destination, "wb") : nullptr;
    constexpr std::size_t chunk = std::size_t{1} << 20;
    char *const buffer = out != nullptr ? static_cast<char *>(std::malloc(chunk)) : nullptr;
    if (buffer != nullptr)
    {
        copied = 0;
        std::size_t got = 0;
        while (copied >= 0 && (got = std::fread(buffer, 1, chunk, in)) > 0)
        {
            const bool written = std::fwrite(buffer, 1, got, out) == got;
            copied = written ? copied + static_cast<long long>(got) : -1;
        }
        std::free(buffer);
    }
    if (out != nullptr && std::fclose(out) != 0)
        copied = -1;
    if (in != nullptr)
        std::fclose(in);
    ps5::debug::mark_value("test state copy bytes", copied);
}

/* Why a terminate handler exists.
 *
 * The driver's shader compiler is C++ and aborts the process from deep inside
 * itself: the console reports `reason: abort is called(system)` with a backtrace
 * through aco::visit_alu_instr, and nothing says what went wrong. The two abort
 * sites in that function are clang's throw helpers for std::vector's length error
 * and for a bad array length (`call __throw_length_error` followed by `ud2`), so
 * something threw and the exception never came back as an error.
 *
 * An abort is also the one failure with no message anywhere: the title's trace
 * says how far it got, and the frontend's log is a buffer that dies with the
 * process. libc++abi still knows the exception while terminate runs, so the type
 * and its what() are written to the trace here - the difference between "the
 * compiler refused this shader" and "a wild size reached a container", which are
 * not the same bug and were not distinguishable from the outside. */
void on_terminate()
{
    const std::type_info *type = abi::__cxa_current_exception_type();
    if (!type)
    {
        ps5::debug::mark("terminate: called with no active exception");
        std::abort();
    }

    /* src/ is compiled without exceptions, so the exception cannot be rethrown
     * here to read what(); libc++abi still reports its type, and the demangled
     * name is what separates the two throw sites that were found in the shader
     * compiler - std::length_error (a container asked for an impossible size)
     * from std::bad_array_new_length (an array length that cannot be valid). */
    int status = 0;
    char *pretty = abi::__cxa_demangle(type->name(), nullptr, nullptr, &status);
    /* The thrown object itself is readable without rethrowing: for the
     * standard exception types a message and, for std::system_error, the
     * error code say which call failed (a thread that could not be created, a
     * mutex that could not be locked) rather than only that one did. */
    const char *what = "";
    int code = 0;
    void *object = abi::__cxa_current_primary_exception();
    if (object)
    {
        /* Without RTTI the type is matched by its mangled name. */
        const char *name = type->name();
        const auto is = [name](const char *mangled) { return std::strcmp(name, mangled) == 0; };
        if (is("NSt3__112system_errorE"))
        {
            const auto *error = static_cast<const std::system_error *>(object);
            what = error->what();
            code = error->code().value();
        }
        else if (is("NSt3__112length_errorE") || is("NSt3__113runtime_errorE") ||
                 is("NSt3__111logic_errorE") || is("NSt3__112out_of_rangeE") ||
                 is("St9bad_alloc") || is("St20bad_array_new_length"))
        {
            what = static_cast<const std::exception *>(object)->what();
        }
    }
    char line[512];
    std::snprintf(line, sizeof line, "terminate: exception type=%s what=\"%s\" code=%d",
                  (status == 0 && pretty) ? pretty : type->name(), what, code);
    if (pretty)
        std::free(pretty);
    ps5::debug::mark(line);
    std::fflush(stderr);

    std::abort();
}
} // namespace

int main()
{
    /* First thing: prove that control reached this function at all, before
     * anything that could fail. */
    {
        /* A zero-initialised static: it lives in the BSS, which this port's own
         * CRT now clears before main. Printed so a run says whether that happened -
         * the alternative was to read it from the far side of the frontend, where
         * the same question cost a round. */
        static long ps5_bss_check;
        static long ps5_data_check = 7;
        char line[128];
        std::snprintf(line, sizeof line, "bss check=%ld (must be 0), data check=%ld (must be 7)",
                      ps5_bss_check, ps5_data_check);
        ps5::debug::mark(line);
    }

    ps5::debug::mark("main() entered; static constructors have already run");

    /* What an earlier build made is made reachable over FTP again before the
     * frontend starts (src/permissions_ps5.cpp); what this run creates is,
     * through the link-time wrappers. */
    ps5_open_permissions();

    /* Everything the libraries say goes to stderr, and a title's stderr reaches
     * nothing on this console: not the kernel log, not RetroArch's log file, not
     * FTP. That is why the driver's own refusals - the ones ../PS5_Vulkan states
     * by name before returning VK_ERROR_UNKNOWN - could not be read, and why a
     * console round could only report the error code. Pointing the stream at the
     * trace file makes those messages part of the same record as the marks, which
     * is also how an assertion's message stops being lost: __assert prints the
     * expression it failed and then aborts. */
    if (!std::freopen("/app0/trace.txt", "a", stderr))
        ps5::debug::mark("could not send stderr to the trace file");
    else
    {
        /* Buffered, and written out by a flusher thread four times a second. It
         * was unbuffered so that an assertion or a terminate, which abort without
         * flushing, kept their message; but every line was then its own write to
         * the console's storage, which takes milliseconds, on whatever thread
         * logged it -- often the one running frames. Loading a God of War save
         * state stalled the picture for hundreds of milliseconds in fprintf alone
         * (2026-09-24, the sampler). The crash and terminate handlers flush
         * first, so a failing run still leaves its last lines. */
        static char stderr_buffer[64 * 1024];
        std::setvbuf(stderr, stderr_buffer, _IOFBF, sizeof(stderr_buffer));
        std::fputs("stderr is the trace file (buffered, flushed every 250 ms)\n", stderr);
        start_log_flusher();
    }

    std::set_terminate(on_terminate);
    ps5::debug::mark(PS5_RETROARCH_BUILD_ID);
    ps5::memory::init("/app0/memory-diagnostics.log", PS5_RETROARCH_BUILD_ID);
    ps5_vulkan_profile_init();

    /* The shell's splash covers the title until it explicitly dismisses it.
     * video_ps5 does this while opening its display, but video_vulkan never
     * enters that code. This is title startup work for either video driver. */
    ps5::debug::mark_value("startup: sceSystemServiceHideSplashScreen",
                           sceSystemServiceHideSplashScreen());

    /* argv must be writable and NULL-terminated: RetroArch's option parsing
     * walks it the way the C runtime would have. */
    char arg0[] = "retroarch";
    char arg_fullscreen[] = "-f";
    char arg_config[] = "-c";
    char arg_config_path[] = "/app0/config/retroarch.cfg";
    char arg_verbose[] = "--verbose";
    char arg_menu[] = "--menu";
    /* RetroArch's own log, on the console, from the first line of main.
     *
     * This matters more than the config's log settings: `--log-file` sets the
     * override and enables file logging *before* the config is parsed, so it
     * records what happens during startup - which is exactly where the Vulkan
     * path dies silently. A failure that says nothing is the one thing a console
     * run cannot diagnose, and this is how the frontend is made to speak. */
    char arg_log[] = "--log-file=/app0/retroarch.log";
    char *argv[] = {
        arg0, arg_fullscreen, arg_config, arg_config_path, arg_verbose, arg_log, arg_menu, nullptr,
    };
    (void)config_path;

    ps5::debug::mark("argv built: retroarch -f -c /app0/config/retroarch.cfg --verbose --log-file");

    /* Test files belong to one launch. A test run writes /app0/test-run.txt just
     * before it starts the title, and the launch consumes it; a launch without
     * it is someone playing, so any test file an interrupted run left behind is
     * deleted before anything reads it. Otherwise a stale args.txt started every
     * later launch straight into a test game that quit itself, and a stale flag
     * kept the driver's logging or the sampler running (2026-09-24). */
    if (std::remove("/app0/test-run.txt") != 0)
    {
        static const char *const test_files[] = {
            "/app0/args.txt",
            "/app0/pad-script.txt",
            "/app0/ps5vk-no-retain.txt",
            "/app0/ppsspp-options.txt",
            "/app0/ps5vk-ab.txt",
            "/app0/ps5-sampler.txt",
            "/app0/ps5vk-profile.txt",
            "/app0/ps5vk-log.txt",
            "/app0/ps5vk-vblank-probe.txt",
            "/app0/ps5vk-spirv-dump.txt",
            "/app0/ps5vk-shader-cache-dir.txt",
            "/app0/dolphin-options.txt",
            "/app0/dolphin-debug.txt",
            "/app0/state-copy.txt",
            "/app0/platform-probe.txt",
        };
        unsigned removed = 0;
        for (const char *const path : test_files)
            removed += std::remove(path) == 0 ? 1u : 0u;
        if (removed != 0)
            ps5::debug::mark_value("stale test files removed", static_cast<int>(removed));
    }
    copy_test_file();
    std::remove("/app0/state-copy.txt");
    if (run_platform_probe())
        return 0;
    if (std::FILE *profile = std::fopen("/app0/ps5vk-profile.txt", "rb"))
    {
        std::fclose(profile);
        g_memory_report.store(true, std::memory_order_relaxed);
    }

    /* Extra arguments, one per line, from /app0/args.txt when that file is there.
     *
     * Why a file rather than the launch arguments: the console starts this title
     * from its own launcher, which passes none, and the frontend's most useful
     * unattended options are exactly the ones a run needs to change - RetroArch
     * already knows how to take a screenshot at the end of a fixed number of frames
     * (`--max-frames=N --max-frames-ss --max-frames-ss-path=FILE`), which is the
     * only way this port can show what the GPU produced without a camera at the
     * screen. The file is optional, empty lines and `#` comments are skipped, and
     * the storage is static because RetroArch's option parsing keeps pointers into
     * it for the whole run. */
    constexpr int max_extra_args = 16;
    constexpr std::size_t max_extra_arg_len = 256;
    static char extra_storage[max_extra_args][max_extra_arg_len];
    int extra_count = 0;

    if (std::FILE *extra = std::fopen("/app0/args.txt", "r"))
    {
        while (extra_count < max_extra_args &&
               std::fgets(extra_storage[extra_count], max_extra_arg_len, extra))
        {
            char *line = extra_storage[extra_count];
            std::size_t len = std::strlen(line);
            while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
                line[--len] = '\0';
            if (len == 0 || line[0] == '#')
                continue;
            /* The port's own options are read from this file by the frontend code that
             * uses them (patch 0031 reads `--ps5-capture=`), and RetroArch must not see
             * them: an option it does not know is an option it complains about. */
            if (std::strncmp(line, "--ps5-", 6) == 0)
                continue;
            extra_count++;
        }
        std::fclose(extra);
        ps5::debug::mark_value("argv extras from /app0/args.txt", extra_count);
    }

    char *argv_with_extras[sizeof(argv) / sizeof(argv[0]) + max_extra_args];
    std::size_t base_count = sizeof(argv) / sizeof(argv[0]) - 1;
    for (std::size_t i = 0; i < base_count; i++)
        argv_with_extras[i] = argv[i];
    /* --menu starts in the menu and RetroArch refuses it beside a content path
     * ("--menu was used, but content file was passed as well"), so it is left out
     * when the extras name a core or content: `-L core` and a path launch a game
     * directly, which is how a run measures a core without a person at the pad. */
    bool extras_launch_content = false;
    for (int i = 0; i < extra_count; i++)
        if (std::strcmp(extra_storage[i], "-L") == 0 || extra_storage[i][0] != '-')
            extras_launch_content = true;
    if (extras_launch_content && base_count > 0 && argv[base_count - 1] == arg_menu)
        base_count--;
    for (int i = 0; i < extra_count; i++)
        argv_with_extras[base_count + i] = extra_storage[i];
    argv_with_extras[base_count + extra_count] = nullptr;

    ps5_crash_report_install();
    ps5_core_threads_start();
    ps5_sampler_start();
    ps5_audio_test_if_requested();
    ps5_core_loader_test_if_requested();
    ps5_thread_test_if_requested();
    /* RetroArch tears its whole Vulkan context down and builds it again when it
     * loads or closes content and when a video setting needs a reinit. Retained,
     * the driver keeps VideoOut, its mode and the last frame on screen across
     * that, where it would otherwise blank the panel and switch the output back
     * to 60 Hz and up again: about half a second of black each time.
     * /app0/ps5vk-no-retain.txt turns it off, for a comparison run. */
    if (std::FILE *no_retain = std::fopen("/app0/ps5vk-no-retain.txt", "rb"))
    {
        std::fclose(no_retain);
        ps5::debug::mark("display: VideoOut released with each swapchain (ps5vk-no-retain.txt)");
    }
    else if (ps5vk_display_retain != nullptr)
        ps5vk_display_retain(true);
    const int status =
        rarch_main(static_cast<int>(base_count + extra_count), argv_with_extras, nullptr);

    /* If this line is on the console, the frontend ran and returned by itself. */
    ps5::debug::mark_value("rarch_main returned", status);
    if (ps5vk_display_retain != nullptr)
        ps5vk_display_retain(false);
    ps5::memory::finish();

    return status;
}

/* RetroArch has already closed its drivers and written configuration here.
 * Native title shutdown goes through the shell; kernel exit(0) raises SIGSYS
 * for this application instead of returning cleanly to the home screen. */
extern "C" int sceSystemServiceLoadExec(const char *, const char *const *);
extern "C" void catchReturnFromMain(int status)
{
    ps5::debug::mark_value("native quit: frontend status", status);
    /* The output leaves in its default mode (a second release is a no-op). */
    if (ps5vk_display_retain != nullptr)
        ps5vk_display_retain(false);
    std::fflush(nullptr);
    const int result = sceSystemServiceLoadExec("exit", nullptr);
    ps5::debug::mark_value("native quit: system service result", result);
    if (result >= 0)
        for (;;)
            usleep(100000); /* Shell termination is asynchronous. */
}
