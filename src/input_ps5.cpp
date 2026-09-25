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
 * A run without a person at the pad can script it: /app0/pad-script.txt, when
 * present, holds one press a line, `<seconds> <BUTTON>[+<BUTTON>...] [<held
 * seconds>]`, timed from the first poll, with RetroPad names (B Y SELECT START
 * UP DOWN LEFT RIGHT A X L R L2 R2 L3 R3) and `#` comments. The pressed buttons
 * are added to the pad's own, and the pad counts as connected while a script is
 * loaded, so RetroArch binds it. One trace line a press. One line is an action
 * rather than a press, for the reload stress (Profile 9): `<seconds> RELOAD`
 * loads /app0/args.txt's core and content again in the same process, as the
 * menu's history does. It is meant for the menu, after the script closed the
 * content through the Quick Menu: the poll runs inside the core's retro_run
 * while content runs, and unloading a core from there crashed Dolphin inside
 * its own frame (2026-09-25), which is why there is no CLOSE action. Testing
 * only: the file is never shipped.
 *
 * Reference: docs/REFERENCE.md, "Input".
 */

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <new>

#include <gfx/video_defines.h>

#include <input/input_driver.h>
#include "content.h"
#include "retroarch_types.h"
#include "tasks/task_content.h"
#include <tasks/tasks_internal.h>

extern "C"
{
    /* The console's pad service. Declarations rather than the SDK's headers: this
     * payload SDK ships no header for these, and ../ProsperoLight declares the same
     * shapes, which the console accepted. */
    std::int32_t scePadInit();
    std::int32_t scePadOpen(std::int32_t user_id, std::int32_t port_type, std::int32_t index,
                            const void *params);
    std::int32_t scePadRead(std::int32_t handle, void *samples, std::int32_t capacity);
    std::int32_t scePadClose(std::int32_t handle);
    std::int32_t sceUserServiceInitialize(const void *params);
    std::int32_t sceUserServiceGetInitialUser(std::int32_t *user_id);
    std::int32_t sceUserServiceTerminate();
    std::int32_t sceKernelUsleep(std::uint32_t microseconds);
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
    std::uint8_t reserved_to_connected[66];
    std::int32_t connected;
    std::uint64_t timestamp_us;
    std::uint8_t extension[16];
    std::uint8_t connected_count;
    std::uint8_t remaining[15];
};

static_assert(sizeof(PadSample) == 120, "the console's pad samples are 120 bytes");
static_assert(offsetof(PadSample, left_x) == 0x04, "the stick bytes follow the button word");
static_assert(offsetof(PadSample, connected) == 0x4c, "connection state sits at 0x4c");
static_assert(offsetof(PadSample, timestamp_us) == 0x50, "the timestamp sits at 0x50");

/* One read returns a batch of the samples taken since the last one. */
constexpr int sample_capacity = 64;
constexpr std::int32_t pad_open_attempts = 10;
constexpr std::uint32_t pad_open_retry_microseconds = 100000;
/* The stick bytes run 0..255 with 128 centred. */
constexpr int stick_centre = 128;

/* The trace is a development aid: one line goes to the title's own file, which is
 * the only output this project has that survives a run. It lives in src/trace.cpp,
 * whose signature is C++ and therefore not reachable from an `extern "C"`
 * declaration - the driver calls through this small C-linkage door instead. */
} // namespace

extern "C" void ps5_input_trace(const char *line) noexcept;

namespace
{
struct PadState
{
    std::int32_t handle = -1;
    PadSample samples[sample_capacity];
    /* How many entries of `samples` the last read actually filled. A read returns
     * only what happened since the previous one, so the rest of the buffer still
     * holds older frames and must not be reported as current. */
    std::int32_t sample_count = 0;
    std::uint32_t buttons = 0;
    bool owns_user_service = false;
    bool announced = false;
};

PadState *active_pad = nullptr;

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
/* The script's RELOAD actions (the top of this file), run once each from the
 * poll on the main thread. */
struct ScriptAction
{
    double at;
    bool done;
};
constexpr int action_capacity = 32;
ScriptAction actions[action_capacity];
int action_count = 0;
bool script_started = false;
std::chrono::steady_clock::time_point script_start;

std::uint32_t retropad_button(const char *name) noexcept
{
    static const char *const names[16] = {"B",    "Y",     "SELECT", "START", "UP", "DOWN",
                                          "LEFT", "RIGHT", "A",      "X",     "L",  "R",
                                          "L2",   "R2",    "L3",     "R3"};
    for (unsigned i = 0; i < 16; ++i)
        if (std::strcmp(name, names[i]) == 0)
            return UINT32_C(1) << i;
    return 0;
}

void load_script() noexcept
{
    std::FILE *file = std::fopen("/app0/pad-script.txt", "rb");
    if (file == nullptr)
        return;
    char line[160];
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
        if (std::strcmp(buttons, "RELOAD") == 0 && action_count < action_capacity)
        {
            actions[action_count++] = ScriptAction{at, false};
            continue;
        }
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

void *open_pad() noexcept
{
    auto *state = new (std::nothrow) PadState();
    if (state == nullptr)
    {
        ps5_input_trace("input: the driver state could not be allocated");
        return nullptr;
    }

    state->owns_user_service = sceUserServiceInitialize(nullptr) == 0;

    std::int32_t user_id = -1;
    if (sceUserServiceGetInitialUser(&user_id) < 0)
    {
        ps5_input_trace("input: sceUserServiceGetInitialUser found no user; no pad will be read");
        return state; /* the driver stays alive and reports nothing */
    }
    if (scePadInit() < 0)
    {
        ps5_input_trace("input: scePadInit failed; no pad will be read");
        return state;
    }
    /* The pad is not always there the first time it is asked for - a title started
     * from the shell can arrive before the pad service has published the device -
     * so the open is retried, as ../ProsperoLight retries it. */
    for (std::int32_t attempt = 0; attempt < pad_open_attempts; ++attempt)
    {
        state->handle = scePadOpen(user_id, 0, 0, nullptr);
        if (state->handle >= 0)
            break;
        (void)sceKernelUsleep(pad_open_retry_microseconds);
    }
    if (state->handle < 0)
    {
        char line[176];
        std::snprintf(line, sizeof(line),
                      "input: scePadOpen failed after %d attempts, handle=%d; no input this run",
                      static_cast<int>(pad_open_attempts), state->handle);
        ps5_input_trace(line);
        return state;
    }
    {
        char line[176];
        std::snprintf(line, sizeof(line), "input: pad opened, user=%d handle=%d",
                      static_cast<int>(user_id), state->handle);
        ps5_input_trace(line);
    }
    return state;
}

void poll_pad(void *data) noexcept
{
    PadState *state = state_of(data);
    if (state == nullptr || state->handle < 0)
        return;
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
        return;
    }
    state->sample_count = count;
    const PadSample *newest = newest_sample(*state);
    state->buttons = newest != nullptr ? newest->buttons : 0;

    /* Successful button transitions are routine, not diagnostics. Keep pad-open
     * failures and lifecycle logs, without a synchronous file write per press. */
}

void close_pad(void *data) noexcept
{
    PadState *state = state_of(data);
    if (state == nullptr)
        return;
    if (state->handle >= 0)
    {
        (void)scePadClose(state->handle);
        state->handle = -1;
    }
    if (state->owns_user_service)
    {
        (void)sceUserServiceTerminate();
        state->owns_user_service = false;
    }
    delete state;
}

void *joypad_init(void *) noexcept
{
    if (!active_pad)
    {
        active_pad = state_of(open_pad());
        if (active_pad && script_count == 0)
            load_script();
    }
    if (active_pad)
        ps5_input_trace("input: ps5 joypad registered (16 buttons, 6 axes)");
    return active_pad;
}

void joypad_destroy() noexcept
{
    close_pad(active_pad);
    active_pad = nullptr;
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

/* Runs the script's RELOAD actions whose time has come. */
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

void joypad_poll() noexcept
{
    run_script_actions();
    poll_pad(active_pad);
    if (!active_pad)
        return;
    // Do not announce a disconnect while the shell temporarily intercepts input.
    bool connected = false;
    const PadSample *latest = nullptr;
    for (int i = 0; i < active_pad->sample_count; ++i)
        if (!latest || active_pad->samples[i].timestamp_us > latest->timestamp_us)
            latest = &active_pad->samples[i];
    connected = (latest && latest->connected) || script_count != 0;
    if (connected != active_pad->announced)
    {
        active_pad->announced = connected;
        if (connected)
            input_autoconfigure_connect("PS5 Controller", nullptr, nullptr, "ps5", 0, 0, 0);
        else
            input_autoconfigure_disconnect(0, "PS5 Controller");
    }
}

bool joypad_query(unsigned port) noexcept
{
    return port == 0 && active_pad && active_pad->announced;
}

std::uint32_t joypad_buttons(unsigned port) noexcept
{
    if (port != 0 || !active_pad)
        return 0;
    const std::uint32_t scripted = script_buttons();
    const PadSample *sample = newest_sample(*active_pad);
    if (!sample)
        return scripted;
    auto mask = pad_buttons_to_retropad(sample->buttons) | scripted;
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
    if (port != 0 || !active_pad || axis == AXIS_NONE)
        return 0;
    const PadSample *sample = newest_sample(*active_pad);
    if (!sample)
        return 0;
    bool negative = AXIS_NEG_GET(axis) < 6;
    unsigned index = negative ? AXIS_NEG_GET(axis) : AXIS_POS_GET(axis);
    int value;
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
    return port == 0 ? "PS5 Controller" : nullptr;
}

void *ps5_input_init(const char *) noexcept
{
    static int cookie;
    return &cookie;
}
void ps5_input_poll(void *) noexcept
{
}
void ps5_input_free(void *) noexcept
{
}

std::int16_t ps5_input_state(void *, const input_device_driver_t *, const input_device_driver_t *,
                             rarch_joypad_info_t *, const retro_keybind_set *, bool, unsigned,
                             unsigned, unsigned, unsigned) noexcept
{
    return 0; // Mapped joypad input is supplied by RetroArch's input_state_wrap.
}

std::uint64_t ps5_input_capabilities(void *data) noexcept
{
    (void)data;
    return (UINT64_C(1) << RETRO_DEVICE_JOYPAD) | (UINT64_C(1) << RETRO_DEVICE_ANALOG);
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

// Defaults clear RetroArch's auto-binds without recreating the pad driver.
// Reannounce once on the next poll, after all default settings have been applied.
extern "C" void ps5_input_reset_autoconfig() noexcept
{
    if (active_pad)
        active_pad->announced = false;
}

extern "C" input_device_driver_t ps5_joypad = {
    joypad_init, joypad_query, joypad_destroy, joypad_button, joypad_state, joypad_get_buttons,
    joypad_axis, joypad_poll,  nullptr,        nullptr,       nullptr,      nullptr,
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
