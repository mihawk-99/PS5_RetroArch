/*
 * PS5 RetroArch - the console's gamepad, as RetroArch's input driver.
 *
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The joypad interface owns the native pad. RetroArch polls it for binding
 * discovery, menu stick navigation and mapped core input. The input interface
 * deliberately reports no additional gamepad state: duplicating raw buttons there
 * would OR the old hardcoded mapping back into user-configured bindings.
 *
 * The ABI is not derivable and is not guessed. `scePadInit`, `scePadOpen`,
 * `scePadRead` and the 120-byte sample layout below were verified on hardware by
 * ../ProsperoLight, a native PS5 title whose controls work: its src/radio_input.cpp
 * and src/moonlight_stream.cpp carry the same calls, the same button bits and the
 * same offsets, including `connected` at 0x4c and the timestamp at 0x50. The static
 * assertions are what keeps a wrong layout from compiling quietly.
 *
 * Up to four controllers, as ../PS5_ProsperoEden connects them: the console pairs
 * each controller with a signed-in user, and scePadOpen opens a user's controller.
 * Port 0 (player 1) is the user who started the title, and stays theirs. About once
 * a second the signed-in users are read again (sceUserServiceGetLoginUserIdList): a
 * new user's controller takes the first free port, 1 to 3, and a port is let go when
 * its user signs out. Each port is announced to RetroArch, rumbles and streams its
 * own haptics, as the first one always has. A test run can trace each port's buttons
 * as they change (/app0/pad-monitor.txt), which is how two controllers are told apart
 * in a log.
 *
 * A run without a person at the pad can script it: /app0/pad-script.txt, when
 * present, holds one press a line, `<seconds> <BUTTON>[+<BUTTON>...] [<held
 * seconds>]`, timed from the first poll, with RetroPad names (B Y SELECT START
 * UP DOWN LEFT RIGHT A X L R L2 R2 L3 R3, and LS_ or RS_ with UP DOWN LEFT or RIGHT
 * for a stick pushed all the way) and `#` comments. The pressed buttons
 * are added to the pad's own, and the pad counts as connected while a script is
 * loaded, so RetroArch binds it. One trace line a press. One line is an action
 * rather than a press, for the reload stress (Profile 9): `<seconds> RELOAD`
 * loads /app0/args.txt's core and content again in the same process, as the
 * menu's history does. It is meant for the menu, after the script closed the
 * content through the Quick Menu: the poll runs inside the core's retro_run
 * while content runs, and unloading a core from there crashed Dolphin inside
 * its own frame (2026-09-25), which is why there is no CLOSE action. And
 * `<seconds> STOP` ends the run as --max-frames does, with its screenshot:
 * RetroArch forgets --max-frames when a core closes, so a run that reloads
 * content otherwise never ends or takes its screenshot. `<seconds> SAVE_STATE`
 * and `<seconds> LOAD_STATE` save and load the current slot, as the menu's
 * items do, for a run that starts from a known scene; a number after either
 * names another slot for that action only. `<seconds> SCREENSHOT`
 * saves the frame shown then as /app0/pad-shot-<n>.png (n counts from 1), so a
 * run that walks through a game's screens shows each of them; it is encoded on
 * RetroArch's task thread, so the game runs on (a 4K PNG held the frontend's own
 * thread about 20 s). `<seconds> MARK`
 * prints how many new frames the core has made and the longest gap between
 * two since the last MARK, so a run's frame rate is read over exact script
 * times. `<seconds> RECORD <n>` records the next n pictures the display is given (the
 * real frames and the generated ones, default 240, at most 2000) to /app0/rec-<k>/ as
 * small raw frames with an index of when each was shown. RUMBLE_STRONG and RUMBLE_WEAK take a
 * strength (0..65535) and exercise controller feedback; send 0 to stop. `<seconds> MENU` prints
 * where the menu is: the list on top of its stack (label and path), the selected entry and the
 * pending selection, which pictures cannot show (SCREENSHOT leaves the XMB out
 * when no content runs). Testing only: the file is never shipped.
 *
 * Reference: docs/REFERENCE.md, "Input".
 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <new>
#include <pthread.h>

#include <gfx/video_defines.h>

#include <input/input_driver.h>
#include "content.h"
#include "webui_update.h"
#include "webui_link.h"
#include "memory_status.h"
#ifdef HAVE_MENU
#include "command.h"
#include "configuration.h"
#include "core_option_manager.h"
#include "core.h"
#include "core_info.h"
#include "menu/menu_driver.h"
#if defined(HAVE_SLANG)
#include "gfx/video_shader_parse.h"
#include "menu/menu_shader.h"
#endif
#endif
#include "gfx/video_driver.h"
#include "retroarch_types.h"
#include "runloop.h"
#include "tasks/task_content.h"
#include <tasks/tasks_internal.h>
#include "title_threads.hpp"

/* scePadSetVibration's parameter block: a level per motor, 0..255. The shape is
 * the one libScePad documents and shadPS4's pad library implements, and the
 * OpenOrbis pad documentation states the same two fields. */
struct ScePadVibrationParam
{
    std::uint8_t largeMotor;
    std::uint8_t smallMotor;
};

extern "C"
{
    // Scoped to synchronous test capture; normal screenshots keep the viewport.
    bool ps5_test_full_screenshot = false;
    /* The console's pad service. Declarations rather than the SDK's headers: this
     * payload SDK ships no header for these, and ../ProsperoLight declares the same
     * shapes, which the console accepted. */
    std::int32_t scePadInit();
    std::int32_t scePadOpen(std::int32_t user_id, std::int32_t port_type, std::int32_t index,
                            const void *params);
    std::int32_t scePadRead(std::int32_t handle, void *samples, std::int32_t capacity);
    std::int32_t scePadClose(std::int32_t handle);
    std::int32_t scePadSetVibration(std::int32_t handle, const ScePadVibrationParam *param);
    /* Vibration output mode: 1 = advanced (DualSense haptics), 2 = compatible,
     * the classic dual-motor path scePadSetVibration drives. A DualSense opens
     * in advanced mode, so without this call the two motor levels move nothing.
     * Values per ../ps5-native-gamepad-input-research's FEATURES.md, validated
     * on hardware. */
    std::int32_t scePadSetVibrationMode(std::int32_t handle, std::int32_t mode);
    std::int32_t sceUserServiceInitialize(const void *params);
    std::int32_t sceUserServiceGetInitialUser(std::int32_t *user_id);
    /* The signed-in users, at most four; unused entries are -1 (the shape
     * ../PS5_ProsperoEden reads them with on the console). */
    struct SceUserServiceLoginUserIdList
    {
        std::int32_t user_id[4];
    };
    std::int32_t sceUserServiceGetLoginUserIdList(SceUserServiceLoginUserIdList *list);
    std::int32_t sceUserServiceTerminate();
    /* The handle this process already holds for a user's controller. */
    std::int32_t scePadGetHandle(std::int32_t user_id, std::int32_t port_type, std::int32_t index);
    std::int32_t sceKernelUsleep(std::uint32_t microseconds);
    /* The audio service. The same declarations and call shape as
     * src/audio_ps5.cpp, which follows ProsperoLight's moonlight_stream.cpp.
     * Port type 10 is the pad's vibration port: PCM written there reaches the
     * DualSense's voice-coil actuators, which is how titles produce real
     * haptic feedback rather than the compatible-mode motor emulation. The
     * constant follows PR #6 (rpf16rj), which cites AnyPS5. */
    std::int32_t sceAudioOutInit();
    std::int32_t sceAudioOutOpen(std::int32_t user_id, std::int32_t type, std::int32_t index,
                                 std::uint32_t len, std::uint32_t freq, std::uint32_t param);
    std::int32_t sceAudioOutOutput(std::int32_t handle, const void *data);
    std::int32_t sceAudioOutClose(std::int32_t handle);
}

namespace
{
/* The console's pad words. Verified on hardware by ../ProsperoLight; each is a bit
 * of what is held down, not an index. */
constexpr std::uint32_t pad_button_l3 = 0x000002u;
constexpr std::uint32_t pad_button_r3 = 0x000004u;
constexpr std::uint32_t pad_button_options = 0x000008u;
constexpr std::uint32_t pad_button_up = 0x000010u;
constexpr std::uint32_t pad_button_right = 0x000020u;
constexpr std::uint32_t pad_button_down = 0x000040u;
constexpr std::uint32_t pad_button_left = 0x000080u;
constexpr std::uint32_t pad_button_l1 = 0x000400u;
constexpr std::uint32_t pad_button_r1 = 0x000800u;
constexpr std::uint32_t pad_button_triangle = 0x001000u;
constexpr std::uint32_t pad_button_circle = 0x002000u;
constexpr std::uint32_t pad_button_cross = 0x004000u;
constexpr std::uint32_t pad_button_square = 0x008000u;
constexpr std::uint32_t pad_button_touch_pad = 0x100000u;
/* Set while the shell is intercepting the pad; no button means anything then. */
constexpr std::uint32_t pad_button_intercepted = UINT32_C(0x80000000);

/* One sample, as the console writes it. */
struct PadSample
{
    std::uint32_t buttons;
    std::uint8_t left_x;
    std::uint8_t left_y;
    std::uint8_t right_x;
    std::uint8_t right_y;
    std::uint8_t left_trigger;
    std::uint8_t right_trigger;
    std::uint8_t reserved_to_touch[42];
    std::uint8_t touch_count;
    std::uint8_t reserved_touch[7];
    struct
    {
        std::uint16_t x, y;
        std::uint8_t id, reserved[3];
    } touches[2];
    std::int32_t connected;
    std::uint64_t timestamp_us;
    std::uint8_t extension[16];
    std::uint8_t connected_count;
    std::uint8_t remaining[15];
};

static_assert(sizeof(PadSample) == 120, "the console's pad samples are 120 bytes");
static_assert(offsetof(PadSample, left_x) == 0x04, "the stick bytes follow the button word");
// Touch fields verified by PS5_Proton's ps5/src/platform.c.
static_assert(offsetof(PadSample, touch_count) == 0x34, "touch count sits at 0x34");
static_assert(offsetof(PadSample, touches) == 0x3c, "touch contacts sit at 0x3c");
static_assert(offsetof(PadSample, connected) == 0x4c, "connection state sits at 0x4c");
static_assert(offsetof(PadSample, timestamp_us) == 0x50, "the timestamp sits at 0x50");

/* One read returns a batch of the samples taken since the last one. */
constexpr int sample_capacity = 64;
constexpr std::int32_t pad_open_attempts = 10;
constexpr std::uint32_t pad_open_retry_microseconds = 100000;
/* A console signs in at most four users, each with one controller. */
constexpr unsigned max_pads = 4;
/* How often the signed-in users are read again. */
constexpr auto user_scan_interval = std::chrono::seconds(1);
/* The pad monitor's budget of trace lines for a run. */
constexpr unsigned monitor_line_budget = 600;
/* The stick bytes run 0..255 with 128 centred. */
constexpr int stick_centre = 128;

/* The trace is a development aid: one line goes to the title's own file, which is
 * the only output this project has that survives a run. It lives in src/trace.cpp,
 * whose signature is C++ and therefore not reachable from an `extern "C"`
 * declaration - the driver calls through this small C-linkage door instead. */
} // namespace

extern "C" void ps5_input_trace(const char *line) noexcept;
extern "C" void
ps5_fg_record_request(unsigned pictures); /* PS5_FrameGen, gfx/vulkan_framegen.cpp */

namespace
{
// Relative contact tracking adapted from PS5_Proton src/proton_touchpad.h
// (LGPL-2.1-or-later; provenance and licence in third_party/proton_touchpad).
struct Touchpad
{
    bool active = false, clicked = false;
    unsigned finger = 0;
    int x = 0, y = 0, pending_x = 0, pending_y = 0;
    std::int16_t frame_x = 0, frame_y = 0;
};

bool stylus_enabled() noexcept
{
#ifdef HAVE_MENU
    if (menu_state_get_ptr()->flags & MENU_ST_FLAG_ALIVE)
        return false;
#endif
    const auto *runloop = runloop_state_get_ptr();
    const char *name = runloop->system.info.library_name;
    return (runloop->flags & RUNLOOP_FLAG_CORE_RUNNING) && name &&
           (!std::strcmp(name, "DeSmuME") || !std::strcmp(name, "Azahar") ||
            !std::strcmp(name, "melonDS DS"));
}

struct PadState
{
    std::int32_t user_id = -1;
    unsigned port = 0;
    std::int32_t handle = -1;
    PadSample samples[sample_capacity];
    /* How many entries of `samples` the last read actually filled. A read returns
     * only what happened since the previous one, so the rest of the buffer still
     * holds older frames and must not be reported as current. */
    std::int32_t sample_count = 0;
    std::uint32_t buttons = 0;
    Touchpad touch;
    /* Last motor levels pushed to the pad: one scePadSetVibration call carries
     * both, while RetroArch sets one motor at a time. Atomic because the
     * haptic feeder thread reads them from outside the input path. */
    std::atomic<std::uint8_t> rumble_large{0};
    std::atomic<std::uint8_t> rumble_small{0};
    /* The vibration audio port (sceAudioOutOpen type 10) and its feeder thread.
     * -1 / not started when the port could not be opened and rumble falls back
     * to the compatible-mode motor path. */
    std::int32_t haptic_port = -1;
    bool haptic_float = false;
    pthread_mutex_t rumble_mutex = PTHREAD_MUTEX_INITIALIZER;
    bool haptic_active = false; // guarded by rumble_mutex
    pthread_t haptic_thread{};
    bool haptic_thread_started = false;
    std::atomic<bool> haptic_stop{false};
    bool announced = false;
    /* The button word the pad monitor last traced for this port. */
    std::uint32_t monitored_buttons = 0;
};

/* The controller on each port, or null. */
PadState *pads[max_pads] = {};
/* The joypad driver's handle for RetroArch: the driver stays registered with no
 * controller at all, and reports nothing until one comes. */
int joypad_cookie;
bool joypad_ready = false;
bool owns_user_service = false;
/* scePadInit answered, so controllers can be opened. */
bool pad_service_ready = false;
std::chrono::steady_clock::time_point next_user_scan{};
/* The signed-in users the last scan read, traced when they change. */
std::int32_t known_users[4] = {-1, -1, -1, -1};
/* A user whose controller would not open, traced once rather than every scan. */
std::int32_t unopened_user = -1;
/* /app0/pad-monitor.txt: trace each port's buttons as they change. */
bool pad_monitor = false;
unsigned monitor_lines = 0;

/* The pad script (see the top of this file). */
struct ScriptPress
{
    double at;
    double until;
    std::uint32_t mask;
    bool announced;
};
constexpr int script_capacity = 128;
constexpr double script_default_hold = 0.15;
ScriptPress script[script_capacity];
int script_count = 0;
/* The script's actions (the top of this file), run once each from the poll on
 * the main thread. */
enum class ScriptActionKind
{
    reload,
    stop,
    save_state,
    load_state,
    screenshot,
    screenshot_full,
    mark,
    option,
    effect,
    rumble_strong,
    rumble_weak,
    menu,
    record,
};
// The core's new frames (gfx/video_driver.c, patches/series 0101), the
// longest time between two of them since the last MARK and how many came more
// than 40 ms after the one before (a 30 fps frame that missed its slot on a
// 60 Hz display), which a MARK line prints: a run's frame rate, worst frame
// and stutters over exact script times.
std::atomic<std::uint64_t> new_frames{0};
std::atomic<std::uint64_t> new_frame_worst_ns{0};
std::atomic<std::uint64_t> new_frames_slow{0};
std::uint64_t new_frame_last_ns = 0;

struct ScriptAction
{
    double at;
    bool done;
    ScriptActionKind kind;
    int slot;        // SAVE_STATE and LOAD_STATE: the slot given, or -1 for the current one
    char key[64];    // OPTION: the core option's key
    char value[512]; // OPTION value, or EFFECT preset path
};
constexpr int action_capacity = 32;
ScriptAction actions[action_capacity];
int action_count = 0;
int screenshot_count = 0;
bool script_started = false;
std::chrono::steady_clock::time_point script_start;

/* A script's stick directions follow the 16 RetroPad buttons in its masks, two
 * a stick axis (left X, left Y, right X, right Y), the negative one first. */
constexpr unsigned script_stick_shift = 16;

std::uint32_t retropad_button(const char *name) noexcept
{
    static const char *const names[24] = {
        "B",       "Y",        "SELECT", "START",   "UP",      "DOWN",     "LEFT",  "RIGHT",
        "A",       "X",        "L",      "R",       "L2",      "R2",       "L3",    "R3",
        "LS_LEFT", "LS_RIGHT", "LS_UP",  "LS_DOWN", "RS_LEFT", "RS_RIGHT", "RS_UP", "RS_DOWN"};
    for (unsigned i = 0; i < 24; ++i)
        if (std::strcmp(name, names[i]) == 0)
            return UINT32_C(1) << i;
    return 0;
}

void load_script() noexcept
{
    std::FILE *file = std::fopen("/app0/pad-script.txt", "rb");
    if (file == nullptr)
        return;
    char line[640];
    while (script_count < script_capacity && std::fgets(line, sizeof(line), file) != nullptr)
    {
        char *hash = std::strchr(line, '#');
        if (hash != nullptr)
            *hash = '\0';
        double at = 0.0;
        double held = script_default_hold;
        char buttons[96] = {0};
        const int fields = std::sscanf(line, "%lf %95s %lf", &at, buttons, &held);
        if (fields < 2)
            continue;
        static const struct
        {
            const char *name;
            ScriptActionKind kind;
        } action_names[] = {{"RELOAD", ScriptActionKind::reload},
                            {"STOP", ScriptActionKind::stop},
                            {"SAVE_STATE", ScriptActionKind::save_state},
                            {"LOAD_STATE", ScriptActionKind::load_state},
                            {"SCREENSHOT", ScriptActionKind::screenshot},
                            {"SCREENSHOT_FULL", ScriptActionKind::screenshot_full},
                            {"MARK", ScriptActionKind::mark},
                            {"RUMBLE_STRONG", ScriptActionKind::rumble_strong},
                            {"RUMBLE_WEAK", ScriptActionKind::rumble_weak},
                            {"MENU", ScriptActionKind::menu},
                            {"RECORD", ScriptActionKind::record}};
        // OPTION key value: set a core option as the Quick Menu does (the core
        // sees it as an update at its next frame)
        if (std::strcmp(buttons, "OPTION") == 0 && action_count < action_capacity)
        {
            ScriptAction option{at, false, ScriptActionKind::option, -1, {0}, {0}};
            if (std::sscanf(line, "%lf %*s %63s %31s", &at, option.key, option.value) == 3)
                actions[action_count++] = option;
            continue;
        }
        // Development scripts share the normal frontend shader/overlay APIs.
        // EFFECT shader|overlay <absolute path or off>; permits switch stress.
        if (std::strcmp(buttons, "EFFECT") == 0 && action_count < action_capacity)
        {
            ScriptAction effect{at, false, ScriptActionKind::effect, -1, {0}, {0}};
            if (std::sscanf(line, "%lf %*s %63s %511[^\r\n]", &at, effect.key, effect.value) == 3)
                actions[action_count++] = effect;
            continue;
        }
        bool is_action = false;
        for (const auto &named : action_names)
            if (std::strcmp(buttons, named.name) == 0 && action_count < action_capacity)
            {
                actions[action_count++] = ScriptAction{
                    at, false, named.kind, fields >= 3 ? static_cast<int>(held) : -1, {0}, {0}};
                is_action = true;
            }
        if (is_action)
            continue;
        std::uint32_t mask = 0;
        for (char *name = std::strtok(buttons, "+"); name != nullptr;
             name = std::strtok(nullptr, "+"))
            mask |= retropad_button(name);
        if (mask != 0)
            script[script_count++] = ScriptPress{at, at + held, mask, false};
    }
    std::fclose(file);
    char note[96];
    std::snprintf(note, sizeof(note), "input: pad script loaded, %d presses", script_count);
    ps5_input_trace(note);
}

/* The scripted buttons held now, as RetroPad bits. */
std::uint32_t script_buttons() noexcept
{
    if (script_count == 0)
        return 0;
    const auto now = std::chrono::steady_clock::now();
    if (!script_started)
    {
        script_started = true;
        script_start = now;
    }
    const double seconds = std::chrono::duration<double>(now - script_start).count();
    std::uint32_t mask = 0;
    for (int i = 0; i < script_count; ++i)
    {
        ScriptPress &press = script[i];
        if (seconds < press.at || seconds >= press.until)
            continue;
        mask |= press.mask;
        if (!press.announced)
        {
            press.announced = true;
            char note[96];
            std::snprintf(note, sizeof(note), "input: pad script press %d at %.2f s: 0x%04x", i,
                          seconds, static_cast<unsigned>(press.mask));
            ps5_input_trace(note);
        }
    }
    return mask;
}

PadState *state_of(void *data) noexcept
{
    return static_cast<PadState *>(data);
}

/* The newest of the samples the last read filled, or null when the pad is not
 * there or the shell is intercepting it. */
const PadSample *newest_sample(const PadState &state) noexcept
{
    const PadSample *newest = nullptr;
    for (std::int32_t index = 0; index < state.sample_count; ++index)
    {
        const PadSample &sample = state.samples[index];
        if (newest == nullptr || sample.timestamp_us > newest->timestamp_us)
            newest = &sample;
    }
    if (newest == nullptr || !newest->connected || (newest->buttons & pad_button_intercepted) != 0)
        return nullptr;
    return newest;
}

/* The pad's held buttons as RetroArch's own button indices, as a bitmask.
 *
 * The two orderings do not correspond: the console numbers its buttons by position
 * around the shell and RetroArch numbers them by the position they held on a Super
 * Nintendo pad - A rightmost, B bottom, X top, Y left. CIRCLE is therefore
 * RetroArch's A and CROSS is its B, which is what makes CIRCLE confirm a menu entry
 * and CROSS cancel it, the pairing a PlayStation player expects. */
std::uint32_t pad_buttons_to_retropad(std::uint32_t pad) noexcept
{
    std::uint32_t mask = 0;
    if (pad & pad_button_up)
        mask |= UINT32_C(1) << RETRO_DEVICE_ID_JOYPAD_UP;
    if (pad & pad_button_down)
        mask |= UINT32_C(1) << RETRO_DEVICE_ID_JOYPAD_DOWN;
    if (pad & pad_button_left)
        mask |= UINT32_C(1) << RETRO_DEVICE_ID_JOYPAD_LEFT;
    if (pad & pad_button_right)
        mask |= UINT32_C(1) << RETRO_DEVICE_ID_JOYPAD_RIGHT;
    if (pad & pad_button_circle)
        mask |= UINT32_C(1) << RETRO_DEVICE_ID_JOYPAD_A;
    if (pad & pad_button_cross)
        mask |= UINT32_C(1) << RETRO_DEVICE_ID_JOYPAD_B;
    if (pad & pad_button_triangle)
        mask |= UINT32_C(1) << RETRO_DEVICE_ID_JOYPAD_X;
    if (pad & pad_button_square)
        mask |= UINT32_C(1) << RETRO_DEVICE_ID_JOYPAD_Y;
    if (pad & pad_button_l1)
        mask |= UINT32_C(1) << RETRO_DEVICE_ID_JOYPAD_L;
    if (pad & pad_button_r1)
        mask |= UINT32_C(1) << RETRO_DEVICE_ID_JOYPAD_R;
    if (pad & pad_button_l3)
        mask |= UINT32_C(1) << RETRO_DEVICE_ID_JOYPAD_L3;
    if (pad & pad_button_r3)
        mask |= UINT32_C(1) << RETRO_DEVICE_ID_JOYPAD_R3;
    if (pad & pad_button_options)
        mask |= UINT32_C(1) << RETRO_DEVICE_ID_JOYPAD_START;
    if (pad & pad_button_touch_pad)
        mask |= UINT32_C(1) << RETRO_DEVICE_ID_JOYPAD_SELECT;
    return mask;
}

/* A scripted stick direction's full deflection on a stick axis (0 to 3), or 0;
 * on the trigger axes (4 and 5, which the profile binds L2 and R2 to), a
 * scripted L2 or R2 pulls the trigger fully. */
int script_axis(unsigned index) noexcept
{
    if (index == 4 || index == 5)
        return script_buttons() & (UINT32_C(1) << (index == 4 ? RETRO_DEVICE_ID_JOYPAD_L2
                                                              : RETRO_DEVICE_ID_JOYPAD_R2))
                   ? 32767
                   : 0;
    if (index > 3)
        return 0;
    const std::uint32_t held = script_buttons() >> (script_stick_shift + index * 2);
    return (held & 2) ? 32767 : (held & 1) ? -32768 : 0;
}

/* A stick byte as a signed 16-bit axis. */
std::int16_t stick_axis(std::uint8_t value) noexcept
{
    const int offset = static_cast<int>(value) - stick_centre;
    int scaled = offset * 32767 / (offset < 0 ? 128 : 127);
    if (scaled > 32767)
        scaled = 32767;
    if (scaled < -32768)
        scaled = -32768;
    return static_cast<std::int16_t>(scaled);
}

/* Vibration-port streaming. A DualSense's actuators are voice coils played by
 * an audio stream, and titles send that stream to a dedicated port rather than
 * through libScePad. The shape mirrors the main-output path in
 * src/audio_ps5.cpp: 256 S16-stereo frames at 48 kHz per sceAudioOutOutput
 * call, which the service paces for us. */
constexpr std::int32_t haptic_port_type = 10;
constexpr std::uint32_t haptic_rate = 48000;
constexpr std::uint32_t haptic_grain = 256;
/* param low byte 1 = S16 stereo, the same format the main port negotiates. */
constexpr std::uint32_t haptic_format_s16_stereo = 1;
constexpr std::uint32_t haptic_format_f32_stereo = 4;
constexpr int haptic_output_failures_to_quit = 40;

float haptic_noise(std::uint32_t &rng) noexcept
{
    rng = rng * 1664525u + 1013904223u;
    return static_cast<float>((rng >> 9) & 0xffffu) / 32768.0f - 1.0f;
}

void *haptic_worker(void *opaque) noexcept
{
    auto *state = static_cast<PadState *>(opaque);
    alignas(16) std::int16_t out[haptic_grain * 2];
    alignas(16) float float_out[haptic_grain * 2];
    double phase_low = 0.0, phase_high = 0.0;
    float env_strong = 0.0f, env_weak = 0.0f;
    float lp_left = 0.0f, lp_right = 0.0f;
    std::uint32_t rng = 0x9e3779b9u;
    int failures = 0;
    unsigned outputs = 0, active_outputs = 0, errors = 0;
    while (!state->haptic_stop.load(std::memory_order_relaxed))
    {
        bool nonzero = false;
        for (std::uint32_t i = 0; i < haptic_grain; ++i)
        {
            const float strong = state->rumble_large.load(std::memory_order_relaxed) / 255.0f;
            const float weak = state->rumble_small.load(std::memory_order_relaxed) / 255.0f;
            /* Smooth toward the target so square rumble changes do not click. */
            env_strong += (strong - env_strong) * 0.02f;
            env_weak += (weak - env_weak) * 0.02f;
            const float n_l = haptic_noise(rng);
            const float n_r = haptic_noise(rng);
            lp_left += 0.08f * (n_l - lp_left);
            lp_right += 0.08f * (n_r - lp_right);
            phase_low += 55.0 / haptic_rate;
            phase_high += 190.0 / haptic_rate;
            if (phase_low >= 1.0)
                phase_low -= 1.0;
            if (phase_high >= 1.0)
                phase_high -= 1.0;
            const double two_pi = 6.283185307179586;
            /* Left and right actuators get decorrelated noise so textures read
             * as surface, not as one monotone buzz. */
            const float left =
                env_strong *
                    (0.7f * static_cast<float>(std::sin(two_pi * phase_low)) + 0.5f * lp_left) +
                env_weak *
                    (0.55f * static_cast<float>(std::sin(two_pi * phase_high)) + 0.45f * lp_right);
            const float right =
                env_strong *
                    (0.7f * static_cast<float>(std::sin(two_pi * phase_low)) + 0.5f * lp_right) +
                env_weak *
                    (0.55f * static_cast<float>(std::sin(two_pi * phase_high)) + 0.45f * lp_left);
            const auto to_s16 = [](float v) -> std::int16_t
            {
                const float scaled = v * 26000.0f;
                if (scaled > 32767.0f)
                    return 32767;
                if (scaled < -32768.0f)
                    return -32768;
                return static_cast<std::int16_t>(scaled);
            };
            out[2 * i] = to_s16(left);
            out[2 * i + 1] = to_s16(right);
            nonzero |= out[2 * i] != 0 || out[2 * i + 1] != 0;
        }
        const void *buffer = out;
        if (state->haptic_float)
        {
            for (unsigned i = 0; i < haptic_grain * 2; ++i)
                float_out[i] = out[i] / 32768.0f;
            buffer = float_out;
        }
        if (sceAudioOutOutput(state->haptic_port, buffer) < 0)
        {
            ++errors;
            if (++failures >= haptic_output_failures_to_quit)
            {
                /* The port went dead: put the pad back on the motor path so
                 * rumble still works, then let the thread end. */
                pthread_mutex_lock(&state->rumble_mutex);
                state->haptic_active = false;
                const int mode = scePadSetVibrationMode(state->handle, 2);
                const ScePadVibrationParam levels{state->rumble_large.load(),
                                                  state->rumble_small.load()};
                const int motor = scePadSetVibration(state->handle, &levels);
                pthread_mutex_unlock(&state->rumble_mutex);
                char line[128];
                std::snprintf(line, sizeof(line), "input: haptic fallback mode=%d motors=%d", mode,
                              motor);
                ps5_input_trace(line);
                ps5_input_trace("input: haptic port stopped answering; back to motor rumble");
                break;
            }
            (void)sceKernelUsleep(2000);
        }
        else
        {
            failures = 0;
            ++outputs;
            active_outputs += nonzero ? 1 : 0;
        }
    }
    char note[128];
    std::snprintf(note, sizeof(note), "input: haptic stream outputs=%u active=%u errors=%u",
                  outputs, active_outputs, errors);
    ps5_input_trace(note);
    return nullptr;
}

/* Opens a user's controller on a port: the pad, its vibration port and haptic
 * stream. Null when the pad would not open. */
PadState *open_pad(std::int32_t user_id, unsigned port, std::int32_t attempts) noexcept
{
    auto *state = new (std::nothrow) PadState();
    if (state == nullptr)
    {
        ps5_input_trace("input: the pad state could not be allocated");
        return nullptr;
    }
    state->user_id = user_id;
    state->port = port;
    /* The pad is not always there the first time it is asked for - a title started
     * from the shell can arrive before the pad service has published the device -
     * so the first controller's open is retried, as ../ProsperoLight retries it. */
    for (std::int32_t attempt = 0; attempt < attempts; ++attempt)
    {
        state->handle = scePadOpen(user_id, 0, 0, nullptr);
        if (state->handle >= 0 || attempt + 1 == attempts)
            break;
        (void)sceKernelUsleep(pad_open_retry_microseconds);
    }
    /* A handle this process already holds for the user is reused. */
    if (state->handle < 0)
    {
        const std::int32_t held = scePadGetHandle(user_id, 0, 0);
        if (held >= 0)
            state->handle = held;
    }
    if (state->handle < 0)
    {
        if (user_id != unopened_user)
        {
            char line[176];
            std::snprintf(line, sizeof(line),
                          "input: port %u: scePadOpen for user %d failed after %d attempts, "
                          "handle=%d",
                          port, static_cast<int>(user_id), static_cast<int>(attempts),
                          state->handle);
            ps5_input_trace(line);
            unopened_user = user_id;
        }
        pthread_mutex_destroy(&state->rumble_mutex);
        delete state;
        return nullptr;
    }
    /* Try the vibration audio port. When it opens, the pad goes to advanced
     * mode (1), where the actuators follow the PCM stream the worker feeds -
     * and the motor-level path stays silent, which is fine because the worker
     * synthesizes from the same rumble levels. When the port refuses, the pad
     * keeps compatible mode (2) and scePadSetVibration still moves the motors. */
    const int audio_init = sceAudioOutInit();
    if (audio_init == 0 || static_cast<std::uint32_t>(audio_init) == 0x8026000eu)
    {
        state->haptic_port = sceAudioOutOpen(user_id, haptic_port_type, 0, haptic_grain,
                                             haptic_rate, haptic_format_s16_stereo);
        if (state->haptic_port < 0)
        {
            state->haptic_float = true;
            state->haptic_port = sceAudioOutOpen(user_id, haptic_port_type, 0, haptic_grain,
                                                 haptic_rate, haptic_format_f32_stereo);
        }
    }
    int mode_result = -1;
    int thread_result = -1;
    if (state->haptic_port >= 0)
    {
        mode_result = scePadSetVibrationMode(state->handle, 1);
        if (mode_result == 0)
        {
            state->haptic_active = true;
            thread_result = create_title_thread(&state->haptic_thread, haptic_worker, state);
            state->haptic_thread_started = thread_result == 0;
        }
        if (!state->haptic_thread_started)
        {
            state->haptic_active = false;
            (void)sceAudioOutClose(state->haptic_port);
            state->haptic_port = -1;
        }
    }
    if (!state->haptic_thread_started)
        mode_result = scePadSetVibrationMode(state->handle, 2);
    {
        char line[256];
        std::snprintf(line, sizeof(line),
                      "input: pad opened, port=%u user=%d handle=%d haptic_port=%d "
                      "haptic_thread=%d vibration_mode=%d format=%s thread_result=%d",
                      port, static_cast<int>(user_id), state->handle, state->haptic_port,
                      state->haptic_thread_started ? 1 : 0, mode_result,
                      state->haptic_float ? "f32" : "s16", thread_result);
        ps5_input_trace(line);
    }
    return state;
}

void poll_pad(void *data) noexcept
{
    PadState *state = state_of(data);
    if (state == nullptr || state->handle < 0)
        return;
    if (state->port != 0 || !stylus_enabled())
        state->touch = {};
    const std::int32_t count = scePadRead(state->handle, state->samples, sample_capacity);
    if (count == 0)
        return; // No new samples: preserve the last state across a second binding poll.
    if (count < 0 || count > sample_capacity)
    {
        /* The service refused the read or the pad went away. Nothing is reported
         * rather than the last state being repeated, so a button cannot stick
         * down. */
        state->sample_count = 0;
        state->buttons = 0;
        state->touch = {};
        return;
    }
    state->sample_count = count;
    const PadSample *newest = newest_sample(*state);
    state->buttons = newest != nullptr ? newest->buttons : 0;
    if (newest && state->port == 0 && stylus_enabled())
    {
        auto &touch = state->touch;
        const auto &finger = newest->touches[0];
        const bool touching = newest->touch_count != 0;
        if (touching && touch.active && finger.id == touch.finger)
        {
            touch.pending_x = std::clamp(touch.pending_x + finger.x - touch.x, -32767, 32767);
            touch.pending_y = std::clamp(touch.pending_y + finger.y - touch.y, -32767, 32767);
        }
        touch.active = touching;
        touch.clicked = (newest->buttons & pad_button_touch_pad) != 0;
        touch.finger = finger.id;
        touch.x = finger.x;
        touch.y = finger.y;
    }
    else
        state->touch = {};

    /* Successful button transitions are routine, not diagnostics. Keep pad-open
     * failures and lifecycle logs, without a synchronous file write per press. */
}

void close_pad(void *data) noexcept
{
    PadState *state = state_of(data);
    if (state == nullptr)
        return;
    if (state->haptic_thread_started)
    {
        state->haptic_stop.store(true, std::memory_order_relaxed);
        (void)pthread_join(state->haptic_thread, nullptr);
        state->haptic_thread_started = false;
    }
    if (state->haptic_port >= 0)
    {
        (void)sceAudioOutClose(state->haptic_port);
        state->haptic_port = -1;
    }
    if (state->handle >= 0)
    {
        (void)scePadClose(state->handle);
        state->handle = -1;
    }
    pthread_mutex_destroy(&state->rumble_mutex);
    delete state;
}

/* Lets a port's controller go: RetroArch is told it left, then it is closed. */
void release_port(unsigned port, const char *why) noexcept
{
    PadState *state = pads[port];
    if (state == nullptr)
        return;
    if (state->announced)
        input_autoconfigure_disconnect(port, "PS5 Controller");
    char line[128];
    std::snprintf(line, sizeof(line), "input: port %u: user %d's controller let go (%s)", port,
                  static_cast<int>(state->user_id), why);
    ps5_input_trace(line);
    pads[port] = nullptr;
    close_pad(state);
}

/* Reads the signed-in users again: a port is let go when its user signed out
 * (port 0 stays with the user who started the title), and a new user's
 * controller takes the first free port. */
void scan_users() noexcept
{
    SceUserServiceLoginUserIdList list{{-1, -1, -1, -1}};
    if (!pad_service_ready || sceUserServiceGetLoginUserIdList(&list) < 0)
        return;
    if (std::memcmp(list.user_id, known_users, sizeof(known_users)) != 0)
    {
        std::memcpy(known_users, list.user_id, sizeof(known_users));
        char line[128];
        std::snprintf(line, sizeof(line), "input: signed-in users %d %d %d %d",
                      static_cast<int>(list.user_id[0]), static_cast<int>(list.user_id[1]),
                      static_cast<int>(list.user_id[2]), static_cast<int>(list.user_id[3]));
        ps5_input_trace(line);
    }
    const auto signed_in = [&list](std::int32_t user)
    {
        for (const std::int32_t id : list.user_id)
            if (id >= 0 && id == user)
                return true;
        return false;
    };
    for (unsigned port = 1; port < max_pads; ++port)
        if (pads[port] != nullptr && !signed_in(pads[port]->user_id))
            release_port(port, "signed out");
    for (const std::int32_t user : list.user_id)
    {
        if (user < 0)
            continue;
        bool known = false;
        for (const PadState *state : pads)
            known |= state != nullptr && state->user_id == user;
        if (known)
            continue;
        for (unsigned port = 0; port < max_pads; ++port)
            if (pads[port] == nullptr)
            {
                pads[port] = open_pad(user, port, 1);
                break;
            }
    }
}

void *joypad_init(void *) noexcept
{
    if (!joypad_ready)
    {
        joypad_ready = true;
        owns_user_service = sceUserServiceInitialize(nullptr) == 0;
        std::int32_t user_id = -1;
        if (sceUserServiceGetInitialUser(&user_id) < 0)
            ps5_input_trace("input: sceUserServiceGetInitialUser found no user");
        pad_service_ready = scePadInit() >= 0;
        if (!pad_service_ready)
            ps5_input_trace("input: scePadInit failed; no pad will be read");
        else if (user_id >= 0)
            pads[0] = open_pad(user_id, 0, pad_open_attempts);
        unopened_user = -1;
        scan_users();
        next_user_scan = std::chrono::steady_clock::now() + user_scan_interval;
        if (script_count == 0)
            load_script();
        std::FILE *monitor = std::fopen("/app0/pad-monitor.txt", "rb");
        pad_monitor = monitor != nullptr;
        monitor_lines = 0;
        if (monitor != nullptr)
            std::fclose(monitor);
    }
    ps5_input_trace("input: ps5 joypad registered (4 ports, 16 buttons, 6 axes)");
    return &joypad_cookie;
}

void joypad_destroy() noexcept
{
    for (PadState *&state : pads)
    {
        close_pad(state);
        state = nullptr;
    }
    if (owns_user_service)
    {
        (void)sceUserServiceTerminate();
        owns_user_service = false;
    }
    joypad_ready = false;
    pad_service_ready = false;
    std::memset(known_users, 0xff, sizeof(known_users));
}

/* The core and content /app0/args.txt names: the line after -L, and the first
 * line that is not an option. */
bool args_paths(char *core, std::size_t core_size, char *content, std::size_t content_size) noexcept
{
    std::FILE *file = std::fopen("/app0/args.txt", "rb");
    if (file == nullptr)
        return false;
    char line[512];
    bool next_is_core = false;
    core[0] = content[0] = '\0';
    while (std::fgets(line, sizeof(line), file) != nullptr)
    {
        line[std::strcspn(line, "\r\n")] = '\0';
        if (next_is_core)
        {
            std::snprintf(core, core_size, "%s", line);
            next_is_core = false;
        }
        else if (std::strcmp(line, "-L") == 0)
            next_is_core = true;
        else if (line[0] != '-' && line[0] != '\0' && content[0] == '\0')
            std::snprintf(content, content_size, "%s", line);
    }
    std::fclose(file);
    return core[0] != '\0' && content[0] != '\0';
}

/* Runs the script's actions whose time has come. */
bool joypad_set_rumble(unsigned, enum retro_rumble_effect, std::uint16_t) noexcept;

void run_script_actions() noexcept
{
    if (action_count == 0)
        return;
    if (!script_started)
    {
        script_started = true;
        script_start = std::chrono::steady_clock::now();
    }
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - script_start).count();
    for (int i = 0; i < action_count; ++i)
    {
        ScriptAction &action = actions[i];
        if (action.done || seconds < action.at)
            continue;
        action.done = true;
        if (action.kind == ScriptActionKind::effect)
        {
            bool ok = false;
#ifdef HAVE_MENU
            settings_t *settings = config_get_ptr();
            const bool off = std::strcmp(action.value, "off") == 0;
#if defined(HAVE_SLANG)
            if (std::strcmp(action.key, "shader") == 0)
                ok = video_shader_apply_shader(settings, RARCH_SHADER_SLANG,
                                               off ? "" : action.value, true);
            if (std::strcmp(action.key, "save-shader") == 0)
            {
                auto_shader_type scope = SHADER_PRESET_CURRENT;
                if (std::strcmp(action.value, "global") == 0)
                    scope = SHADER_PRESET_GLOBAL;
                else if (std::strcmp(action.value, "core") == 0)
                    scope = SHADER_PRESET_CORE;
                else if (std::strcmp(action.value, "directory") == 0)
                    scope = SHADER_PRESET_PARENT;
                else if (std::strcmp(action.value, "game") == 0)
                    scope = SHADER_PRESET_GAME;
                if (scope != SHADER_PRESET_CURRENT)
                    ok = menu_shader_manager_save_auto_preset(
                        menu_shader_get(), scope, settings->paths.directory_video_shader,
                        settings->paths.directory_menu_config, false);
            }
            if (std::strcmp(action.key, "parameter") == 0)
            {
                char id[64] = {};
                float value = 0;
                video_shader_ctx_t driver{};
                video_shader *menu = menu_shader_get();
                if (std::sscanf(action.value, "%63s %f", id, &value) == 2 && menu &&
                    video_shader_driver_get_current_shader(&driver) && driver.data)
                {
                    auto *shader = static_cast<video_shader *>(driver.data);
                    for (unsigned i = 0; i < shader->num_parameters; ++i)
                        if (std::strcmp(shader->parameters[i].id, id) == 0)
                        {
                            const auto &param = shader->parameters[i];
                            if (!(value >= param.minimum && value <= param.maximum))
                                break;
                            shader->parameters[i].current = value;
                            for (unsigned j = 0; j < menu->num_parameters; ++j)
                                if (std::strcmp(menu->parameters[j].id, id) == 0)
                                    menu->parameters[j].current = value;
                            menu->flags |= SHDR_FLAG_MODIFIED;
                            ok = true;
                            break;
                        }
                }
            }
#endif
#ifdef HAVE_OVERLAY
            if (std::strcmp(action.key, "overlay") == 0)
            {
                settings->bools.input_overlay_enable = !off;
                std::snprintf(settings->paths.path_overlay, sizeof(settings->paths.path_overlay),
                              "%s", off ? "" : action.value);
                ok =
                    command_event(off ? CMD_EVENT_OVERLAY_UNLOAD : CMD_EVENT_OVERLAY_INIT, nullptr);
            }
#endif
#endif
            char note[640];
            std::snprintf(note, sizeof(note), "input: EFFECT %s %s result=%d", action.key,
                          action.value, ok);
            ps5_input_trace(note);
            continue;
        }
        if (action.kind == ScriptActionKind::save_state ||
            action.kind == ScriptActionKind::load_state)
        {
            const bool save = action.kind == ScriptActionKind::save_state;
#ifdef HAVE_MENU
            // A slot given names the file for this action only (the path is
            // made from the slot when the command runs)
            settings_t *settings = config_get_ptr();
            const int slot = settings->ints.state_slot;
            if (action.slot >= 0)
                settings->ints.state_slot = action.slot;
            {
                // What a state needs, in the trace: why a load that logs nothing returns false
                char why[320];
                char path[512] = "";
                runloop_get_current_savestate_path(path, sizeof(path));
                std::snprintf(
                    why, sizeof(why),
                    "input: state check: core supports savestate=%d serialize_size=%zu path=%s",
                    core_info_current_supports_savestate() ? 1 : 0, core_serialize_size(), path);
                ps5_input_trace(why);
            }
            const bool ok =
                command_event(save ? CMD_EVENT_SAVE_STATE : CMD_EVENT_LOAD_STATE, nullptr);
            settings->ints.state_slot = slot;
#else
            const bool ok = false;
#endif
            char note[96];
            std::snprintf(note, sizeof(note), "input: pad script %s %d at %.2f s: %d",
                          save ? "SAVE_STATE" : "LOAD_STATE", action.slot, seconds, ok ? 1 : 0);
            ps5_input_trace(note);
            continue;
        }
        if (action.kind == ScriptActionKind::screenshot ||
            action.kind == ScriptActionKind::screenshot_full)
        {
            char path[64];
            std::snprintf(path, sizeof(path), "/app0/pad-shot-%d.png", ++screenshot_count);
#ifdef HAVE_SCREENSHOTS
            ps5_test_full_screenshot = action.kind == ScriptActionKind::screenshot_full;
            const video_driver_state_t *video_st = video_state_get_ptr();
            const bool ok =
                take_screenshot(nullptr, path, false,
                                video_st->frame_cache_data &&
                                    video_st->frame_cache_data == RETRO_HW_FRAME_BUFFER_VALID,
                                true, true);
            ps5_test_full_screenshot = false;
#else
            const bool ok = false;
#endif
            char note[128];
            std::snprintf(note, sizeof(note), "input: pad script SCREENSHOT at %.2f s: %s %d",
                          seconds, path, ok ? 1 : 0);
            ps5_input_trace(note);
            continue;
        }
        if (action.kind == ScriptActionKind::record)
        {
            // The present-stream recorder (PS5_FrameGen, gfx/vulkan_framegen.cpp): the next
            // pictures the display is given, written to /app0/rec-<n> when the window is full
            const unsigned pictures = action.slot > 0 ? static_cast<unsigned>(action.slot) : 240u;
            ps5_fg_record_request(pictures);
            char note[96];
            std::snprintf(note, sizeof(note), "input: pad script RECORD %u pictures at %.2f s",
                          pictures, seconds);
            ps5_input_trace(note);
            continue;
        }
        if (action.kind == ScriptActionKind::menu)
        {
            char note[768];
#ifdef HAVE_MENU
            struct menu_state *menu_st = menu_state_get_ptr();
            menu_list_t *menu_list = menu_st->entries.list;
            const file_list_t *stack = MENU_LIST_GET(menu_list, 0);
            const file_list_t *entries = MENU_LIST_GET_SELECTION(menu_list, 0);
            const char *label = "", *path = "", *selected = "";
            if (stack != nullptr && stack->size > 0)
            {
                const struct item_file &top = stack->list[stack->size - 1];
                label = top.label != nullptr ? top.label : "";
                path = top.path != nullptr ? top.path : "";
            }
            const std::size_t selection = menu_st->selection_ptr;
            if (entries != nullptr && selection < entries->size &&
                entries->list[selection].path != nullptr)
                selected = entries->list[selection].path;
            std::snprintf(note, sizeof(note),
                          "input: pad script MENU at %.2f s: depth %zu, list '%.120s' '%.200s', "
                          "entry %zu of %zu '%.200s', pending '%.120s'",
                          seconds, stack != nullptr ? stack->size : 0, label, path, selection,
                          entries != nullptr ? entries->size : 0, selected,
                          menu_st->pending_selection);
#else
            std::snprintf(note, sizeof(note), "input: pad script MENU at %.2f s: no menu", seconds);
#endif
            ps5_input_trace(note);
            continue;
        }
        if (action.kind == ScriptActionKind::rumble_strong ||
            action.kind == ScriptActionKind::rumble_weak)
        {
            const bool strong = action.kind == ScriptActionKind::rumble_strong;
            const bool ok = action.slot >= 0 && action.slot <= 65535 &&
                            joypad_set_rumble(0, strong ? RETRO_RUMBLE_STRONG : RETRO_RUMBLE_WEAK,
                                              static_cast<std::uint16_t>(action.slot));
            char note[128];
            std::snprintf(note, sizeof(note), "input: pad script RUMBLE_%s %d: %d",
                          strong ? "STRONG" : "WEAK", action.slot, ok ? 1 : 0);
            ps5_input_trace(note);
            continue;
        }
        if (action.kind == ScriptActionKind::option)
        {
            runloop_state_t *runloop = runloop_state_get_ptr();
            size_t idx = 0;
            size_t val_idx = 0;
            const bool ok =
                runloop->core_options &&
                core_option_manager_get_idx(runloop->core_options, action.key, &idx) &&
                core_option_manager_get_val_idx(runloop->core_options, idx, action.value, &val_idx);
            if (ok)
                core_option_manager_set_val(runloop->core_options, idx, val_idx, false);
            char note[160];
            std::snprintf(note, sizeof(note), "input: pad script OPTION %s=%s at %.2f s: %d",
                          action.key, action.value, seconds, ok ? 1 : 0);
            ps5_input_trace(note);
            continue;
        }
        if (action.kind == ScriptActionKind::mark)
        {
            const std::uint64_t worst_ns =
                new_frame_worst_ns.exchange(0, std::memory_order_relaxed);
            char note[128];
            std::snprintf(
                note, sizeof(note),
                "input: pad script MARK at %.3f s: frames=%llu worst_ms=%.1f over_40ms=%llu",
                seconds,
                static_cast<unsigned long long>(new_frames.load(std::memory_order_relaxed)),
                static_cast<double>(worst_ns) / 1e6,
                static_cast<unsigned long long>(new_frames_slow.load(std::memory_order_relaxed)));
            ps5_input_trace(note);
            ps5_memory_report("pad-mark", 0, 0);
            continue;
        }
        if (action.kind == ScriptActionKind::stop)
        {
            // The next frame is the last, as when --max-frames is reached.
            runloop_state_get_ptr()->max_frames =
                static_cast<unsigned>(video_state_get_ptr()->frame_count + 1);
            char note[96];
            std::snprintf(note, sizeof(note), "input: pad script STOP at %.2f s", seconds);
            ps5_input_trace(note);
            continue;
        }
        char core[256];
        char content[256];
        bool pushed = false;
#ifdef HAVE_MENU
        if (args_paths(core, sizeof(core), content, sizeof(content)))
        {
            content_ctx_info_t info{};
            pushed = task_push_load_content_with_new_core_from_menu(
                core, content, &info, CORE_TYPE_PLAIN, nullptr, nullptr);
        }
#else
        (void)core;
        (void)content;
#endif
        char note[160];
        std::snprintf(note, sizeof(note), "input: pad script RELOAD at %.2f s: %d", seconds,
                      pushed ? 1 : 0);
        ps5_input_trace(note);
    }
}

/* The controller on a port, or null. */
PadState *pad_at(unsigned port) noexcept
{
    return port < max_pads ? pads[port] : nullptr;
}

void joypad_poll() noexcept
{
    run_script_actions();
    if (pad_service_ready && std::chrono::steady_clock::now() >= next_user_scan)
    {
        scan_users();
        next_user_scan = std::chrono::steady_clock::now() + user_scan_interval;
    }
    for (unsigned port = 0; port < max_pads; ++port)
    {
        PadState *state = pads[port];
        if (state == nullptr)
            continue;
        poll_pad(state);
        // Do not announce a disconnect while the shell temporarily intercepts input.
        const PadSample *latest = nullptr;
        for (int i = 0; i < state->sample_count; ++i)
            if (!latest || state->samples[i].timestamp_us > latest->timestamp_us)
                latest = &state->samples[i];
        const bool connected = (latest && latest->connected) || (port == 0 && script_count != 0);
        if (connected != state->announced)
        {
            state->announced = connected;
            if (connected)
                input_autoconfigure_connect("PS5 Controller", nullptr, nullptr, "ps5", port, 0, 0);
            else
                input_autoconfigure_disconnect(port, "PS5 Controller");
            char line[96];
            std::snprintf(line, sizeof(line), "input: port %u: user %d's controller %s", port,
                          static_cast<int>(state->user_id),
                          connected ? "connected" : "disconnected");
            ps5_input_trace(line);
        }
        // The monitor traces the newest sample's whole button word, the bit that
        // says the system has the controller (its menus, a sign-in) included.
        if (pad_monitor && latest != nullptr && latest->buttons != state->monitored_buttons &&
            monitor_lines < monitor_line_budget)
        {
            state->monitored_buttons = latest->buttons;
            ++monitor_lines;
            char line[128];
            std::snprintf(
                line, sizeof(line), "input: monitor port %u user %d buttons 0x%08x%s", port,
                static_cast<int>(state->user_id), static_cast<unsigned>(latest->buttons),
                (latest->buttons & pad_button_intercepted) != 0 ? " (the system has it)" : "");
            ps5_input_trace(line);
        }
    }
}

bool joypad_query(unsigned port) noexcept
{
    const PadState *state = pad_at(port);
    return state != nullptr && state->announced;
}

std::uint32_t joypad_buttons(unsigned port) noexcept
{
    PadState *state = pad_at(port);
    if (state == nullptr)
        return 0;
    // The pad script plays the first controller.
    const std::uint32_t scripted = port == 0 ? script_buttons() & 0xffff : 0;
    const PadSample *sample = newest_sample(*state);
    if (!sample)
        return scripted;
    auto buttons = sample->buttons;
    if (port == 0 && stylus_enabled())
        buttons &= ~pad_button_touch_pad;
    auto mask = pad_buttons_to_retropad(buttons) | scripted;
    if (sample->left_trigger > 127)
        mask |= UINT32_C(1) << RETRO_DEVICE_ID_JOYPAD_L2;
    if (sample->right_trigger > 127)
        mask |= UINT32_C(1) << RETRO_DEVICE_ID_JOYPAD_R2;
    return mask;
}

std::int32_t joypad_button(unsigned port, std::uint16_t key) noexcept
{
    return key < 16 && (joypad_buttons(port) & (UINT32_C(1) << key)) ? 1 : 0;
}

void joypad_get_buttons(unsigned port, input_bits_t *bits) noexcept
{
    BIT256_CLEAR_ALL_PTR(bits);
    BITS_COPY16_PTR(bits, joypad_buttons(port));
}

std::int16_t joypad_axis(unsigned port, std::uint32_t axis) noexcept
{
    PadState *state = pad_at(port);
    if (state == nullptr || axis == AXIS_NONE)
        return 0;
    bool negative = AXIS_NEG_GET(axis) < 6;
    unsigned index = negative ? AXIS_NEG_GET(axis) : AXIS_POS_GET(axis);
    int value = port == 0 ? script_axis(index) : 0;
    const PadSample *sample = newest_sample(*state);
    if (value != 0)
        ;
    else if (!sample)
        return 0;
    else
        switch (index)
        {
        case 0:
            value = stick_axis(sample->left_x);
            break;
        case 1:
            value = stick_axis(sample->left_y);
            break;
        case 2:
            value = stick_axis(sample->right_x);
            break;
        case 3:
            value = stick_axis(sample->right_y);
            break;
        case 4:
            value = sample->left_trigger * 32767 / 255;
            break;
        case 5:
            value = sample->right_trigger * 32767 / 255;
            break;
        default:
            return 0;
        }
    return negative ? (value < 0 ? value : 0) : (value > 0 ? value : 0);
}

std::int16_t joypad_state(rarch_joypad_info_t *info, const retro_keybind *binds, unsigned) noexcept
{
    if (!info || !binds)
        return 0;
    std::uint16_t mask = 0;
    for (unsigned i = 0; i < RARCH_FIRST_CUSTOM_BIND; ++i)
    {
        if (!binds[i].valid)
            continue;
        const auto key = binds[i].joykey != NO_BTN
                             ? binds[i].joykey
                             : (info->auto_binds ? info->auto_binds[i].joykey : NO_BTN);
        const auto axis = binds[i].joyaxis != AXIS_NONE
                              ? binds[i].joyaxis
                              : (info->auto_binds ? info->auto_binds[i].joyaxis : AXIS_NONE);
        if (joypad_button(info->joy_idx, key) ||
            std::abs(int(joypad_axis(info->joy_idx, axis))) / 32768.0f > info->axis_threshold)
            mask |= UINT16_C(1) << i;
    }
    return static_cast<std::int16_t>(mask);
}

const char *joypad_name(unsigned port) noexcept
{
    return port == 0 || pad_at(port) != nullptr ? "PS5 Controller" : nullptr;
}

/* RetroArch hands rumble in 0..65535 per effect; the pad wants 0..255 per
 * motor. STRONG drives the large motor, WEAK the small one, and each call
 * restates both because scePadSetVibration is the whole state, not a delta. */
bool joypad_set_rumble(unsigned joypad, enum retro_rumble_effect effect,
                       std::uint16_t strength) noexcept
{
    PadState *pad = pad_at(joypad);
    if (pad == nullptr || pad->handle < 0)
        return false;
    if (effect != RETRO_RUMBLE_STRONG && effect != RETRO_RUMBLE_WEAK)
        return false;
    pthread_mutex_lock(&pad->rumble_mutex);
    const std::uint8_t level = static_cast<std::uint8_t>(strength >> 8);
    switch (effect)
    {
    case RETRO_RUMBLE_STRONG:
        pad->rumble_large.store(level, std::memory_order_relaxed);
        break;
    case RETRO_RUMBLE_WEAK:
        pad->rumble_small.store(level, std::memory_order_relaxed);
        break;
    default:
        break;
    }
    const ScePadVibrationParam param{pad->rumble_large.load(std::memory_order_relaxed),
                                     pad->rumble_small.load(std::memory_order_relaxed)};
    // Advanced mode consumes the levels through PCM, not scePadSetVibration.
    const bool result = pad->haptic_active || scePadSetVibration(pad->handle, &param) == 0;
    pthread_mutex_unlock(&pad->rumble_mutex);
    return result;
}

bool update_exit_armed = false;
void *ps5_input_init(const char *) noexcept
{
    update_exit_armed = false;
    static int cookie;
    return &cookie;
}
void ps5_input_poll(void *) noexcept
{
    if (auto *pad = pad_at(0))
    {
        auto &touch = pad->touch;
        if (!stylus_enabled())
            touch = {};
        // Joypad polling precedes this callback. Snapshot once per input poll:
        // repeated core queries see the same delta; binding polls lose no motion.
        touch.frame_x = static_cast<std::int16_t>(touch.pending_x);
        touch.frame_y = static_cast<std::int16_t>(touch.pending_y);
        touch.pending_x = touch.pending_y = 0;
    }
    /* An update to install: asked of this process's own WebUI, or of the daemon's
     * (src/webui_link.h), which installs once the title has closed. */
    if ((ps5_update::exit_requested() || ps5_webui_link_install_requested()) && !update_exit_armed)
    {
        // End at the next frontend frame boundary. Input polling can run inside
        // retro_run(), where saving state or tearing down the core is unsafe.
        runloop_state_get_ptr()->max_frames =
            static_cast<unsigned>(video_state_get_ptr()->frame_count + 1);
        update_exit_armed = true;
    }
}
void ps5_input_free(void *) noexcept
{
}

std::int16_t ps5_input_state(void *, const input_device_driver_t *, const input_device_driver_t *,
                             rarch_joypad_info_t *, const retro_keybind_set *, bool, unsigned port,
                             unsigned device, unsigned, unsigned id) noexcept
{
    if (port == 0 && device == RETRO_DEVICE_MOUSE && stylus_enabled())
        if (const auto *pad = pad_at(0))
        {
            switch (id)
            {
            case RETRO_DEVICE_ID_MOUSE_X:
                return pad->touch.frame_x;
            case RETRO_DEVICE_ID_MOUSE_Y:
                return pad->touch.frame_y;
            case RETRO_DEVICE_ID_MOUSE_LEFT:
                return pad->touch.clicked;
            }
        }
    return 0; // Mapped joypad input is supplied by RetroArch's input_state_wrap.
}

std::uint64_t ps5_input_capabilities(void *data) noexcept
{
    (void)data;
    return (UINT64_C(1) << RETRO_DEVICE_JOYPAD) | (UINT64_C(1) << RETRO_DEVICE_ANALOG) |
           (UINT64_C(1) << RETRO_DEVICE_MOUSE);
}
} // namespace

/* The pairing between RetroArch's button numbering and the console's own words,
 * exposed as data so the host test can pin it without a console. The two orderings
 * do not correspond, and reading one as the other is a fault whose only symptom is
 * the wrong button moving the menu. */
extern "C" const std::uint32_t *ps5_input_button_map(std::size_t *entries) noexcept
{
    static const std::uint32_t map[] = {
        RETRO_DEVICE_ID_JOYPAD_UP,     pad_button_up,
        RETRO_DEVICE_ID_JOYPAD_DOWN,   pad_button_down,
        RETRO_DEVICE_ID_JOYPAD_LEFT,   pad_button_left,
        RETRO_DEVICE_ID_JOYPAD_RIGHT,  pad_button_right,
        RETRO_DEVICE_ID_JOYPAD_B,      pad_button_cross,
        RETRO_DEVICE_ID_JOYPAD_A,      pad_button_circle,
        RETRO_DEVICE_ID_JOYPAD_Y,      pad_button_square,
        RETRO_DEVICE_ID_JOYPAD_X,      pad_button_triangle,
        RETRO_DEVICE_ID_JOYPAD_L,      pad_button_l1,
        RETRO_DEVICE_ID_JOYPAD_R,      pad_button_r1,
        RETRO_DEVICE_ID_JOYPAD_L3,     pad_button_l3,
        RETRO_DEVICE_ID_JOYPAD_R3,     pad_button_r3,
        RETRO_DEVICE_ID_JOYPAD_START,  pad_button_options,
        RETRO_DEVICE_ID_JOYPAD_SELECT, pad_button_touch_pad,
    };
    if (entries != nullptr)
        *entries = sizeof(map) / sizeof(map[0]) / 2;
    return map;
}

/* The driver table. Positions are the interface: see `struct input_driver` in
 * input/input_driver.h. It ends at keypress_vibrate, so the literal is the whole
 * table. C linkage and a global name, because input/input_driver.c is C and names
 * this symbol in input_drivers[]. */
extern "C" input_driver_t input_ps5 = {
    ps5_input_init,
    ps5_input_poll,
    ps5_input_state,
    ps5_input_free,
    nullptr, /* set_sensor_state */
    nullptr, /* get_sensor_input */
    ps5_input_capabilities,
    "ps5",
    nullptr, /* grab_mouse */
    nullptr, /* grab_stdin */
    nullptr, /* keypress_vibrate */
};

// Microphone capture follows the first controller's user and shares this
// driver's UserService lifetime rather than initializing another owner.
extern "C" int ps5_input_user_id(unsigned port)
{
    if (!joypad_ready)
        joypad_init(nullptr);
    return port < max_pads && pads[port] != nullptr ? pads[port]->user_id : -1;
}

// Defaults clear RetroArch's auto-binds without recreating the pad driver.
// Reannounce once on the next poll, after all default settings have been applied.
extern "C" void ps5_input_reset_autoconfig() noexcept
{
    for (PadState *state : pads)
        if (state != nullptr)
            state->announced = false;
}

extern "C" input_device_driver_t ps5_joypad = {
    joypad_init, joypad_query, joypad_destroy,    joypad_button, joypad_state, joypad_get_buttons,
    joypad_axis, joypad_poll,  joypad_set_rumble, nullptr,       nullptr,      nullptr,
    joypad_name, "ps5",
};

// Built into RetroArch's autoconfiguration list, so existing saved configs with
// an empty joypad driver work without replacing the owner's settings.
extern "C" const char ps5_controller_profile[] = "input_device = \"PS5 Controller\"\n"
                                                 "input_driver = \"ps5\"\n"
                                                 "input_b_btn = \"0\"\n"
                                                 "input_y_btn = \"1\"\n"
                                                 "input_select_btn = \"2\"\n"
                                                 "input_start_btn = \"3\"\n"
                                                 "input_up_btn = \"4\"\n"
                                                 "input_down_btn = \"5\"\n"
                                                 "input_left_btn = \"6\"\n"
                                                 "input_right_btn = \"7\"\n"
                                                 "input_a_btn = \"8\"\n"
                                                 "input_x_btn = \"9\"\n"
                                                 "input_l_btn = \"10\"\n"
                                                 "input_r_btn = \"11\"\n"
                                                 "input_l3_btn = \"14\"\n"
                                                 "input_r3_btn = \"15\"\n"
                                                 "input_l_x_plus_axis = \"+0\"\n"
                                                 "input_l_x_minus_axis = \"-0\"\n"
                                                 "input_l_y_plus_axis = \"+1\"\n"
                                                 "input_l_y_minus_axis = \"-1\"\n"
                                                 "input_r_x_plus_axis = \"+2\"\n"
                                                 "input_r_x_minus_axis = \"-2\"\n"
                                                 "input_r_y_plus_axis = \"+3\"\n"
                                                 "input_r_y_minus_axis = \"-3\"\n"
                                                 "input_l2_axis = \"+4\"\n"
                                                 "input_r2_axis = \"+5\"\n";

// Called by RetroArch's video driver for each frame the core made (patches/series,
// 0101), on the thread that runs frames.
extern "C" void ps5_input_note_new_frame(void)
{
    const std::uint64_t now =
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                       std::chrono::steady_clock::now().time_since_epoch())
                                       .count());
    if (new_frame_last_ns != 0 &&
        now - new_frame_last_ns > new_frame_worst_ns.load(std::memory_order_relaxed))
        new_frame_worst_ns.store(now - new_frame_last_ns, std::memory_order_relaxed);
    if (new_frame_last_ns != 0 && now - new_frame_last_ns > 40'000'000)
        new_frames_slow.fetch_add(1, std::memory_order_relaxed);
    new_frame_last_ns = now;
    new_frames.fetch_add(1, std::memory_order_relaxed);
}
