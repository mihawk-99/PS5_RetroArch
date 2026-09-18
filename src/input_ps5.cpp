/*
 * PS5 RetroArch - the console's gamepad, as RetroArch's input driver.
 *
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Why this file is not a joypad driver. RetroArch reads a controller through one
 * of two interfaces: `input_driver_t`, whose `input_state` is asked about one
 * control at a time, and `rarch_joypad_driver`, a device abstraction the frontend
 * joins to a "joypad driver" by name. Every joypad driver upstream ships needs a
 * library this SDK does not have, so `joypad_drivers[]` in this build is
 * effectively empty and `primary_joypad` is NULL - which is also why the menu
 * crashed on its second frame until `patches/series` 0004 guarded the analog read.
 * An input driver needs none of that: `input_state_wrap` consults the joypad only
 * `if (joypad)`, and calls this driver's own `input_state` unconditionally at the
 * end. So this reads the pad itself and reports the controls, and the frontend's
 * MENU_ACTION path sees them exactly as it sees a mapped joypad.
 *
 * The ABI is not derivable and is not guessed. `scePadInit`, `scePadOpen`,
 * `scePadRead` and the 120-byte sample layout below were verified on hardware by
 * ../ProsperoLight, a native PS5 title whose controls work: its src/radio_input.cpp
 * and src/moonlight_stream.cpp carry the same calls, the same button bits and the
 * same offsets, including `connected` at 0x4c and the timestamp at 0x50. The static
 * assertions are what keeps a wrong layout from compiling quietly.
 *
 * Reference: docs/REFERENCE.md, "Input".
 */

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

#include <gfx/video_defines.h>

#include <input/input_driver.h>

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
constexpr int stick_full_scale = 128;

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
    bool released_reported = false;
};

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
    int scaled = offset * 32767 / stick_full_scale;
    if (scaled > 32767)
        scaled = 32767;
    if (scaled < -32768)
        scaled = -32768;
    return static_cast<std::int16_t>(scaled);
}

void *ps5_input_init(const char *joypad_driver) noexcept
{
    /* The joypad abstraction is not what this driver uses, so the name RetroArch
     * passes - which names no driver this build has - is deliberately ignored. */
    (void)joypad_driver;
    ps5_input_trace("input: ps5_input_init entered");
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

void ps5_input_poll(void *data) noexcept
{
    PadState *state = state_of(data);
    if (state == nullptr || state->handle < 0)
        return;
    const std::int32_t count = scePadRead(state->handle, state->samples, sample_capacity);
    if (count <= 0)
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
    const std::uint32_t previous = state->buttons;
    state->buttons = newest != nullptr ? newest->buttons : 0;

    /* The first press is written to the trace, and the first release after it, so
     * a run's own file answers "did a controller reach the frontend" - the
     * question this step exists for - without anyone watching a screen at the
     * right moment. */
    if (previous == 0 && state->buttons != 0)
    {
        char line[176];
        std::snprintf(line, sizeof(line), "input: press, pad=0x%08x retropad=0x%08x",
                      state->buttons, pad_buttons_to_retropad(state->buttons));
        ps5_input_trace(line);
    }
    else if (previous != 0 && state->buttons == 0 && !state->released_reported)
    {
        state->released_reported = true;
        char line[176];
        std::snprintf(line, sizeof(line), "input: release, pad was 0x%08x", previous);
        ps5_input_trace(line);
    }
}

std::int16_t ps5_input_state(void *data, const input_device_driver_t *joypad_data,
                             const input_device_driver_t *sec_joypad_data,
                             rarch_joypad_info_t *joypad_info,
                             const retro_keybind_set *retro_keybinds, bool keyboard_mapping_blocked,
                             unsigned port, unsigned device, unsigned index, unsigned id) noexcept
{
    (void)joypad_data;
    (void)sec_joypad_data;
    (void)joypad_info;
    (void)retro_keybinds;
    (void)keyboard_mapping_blocked;
    PadState *state = state_of(data);
    if (state == nullptr || state->handle < 0 || port != 0)
        return 0;

    switch (device)
    {
    case RETRO_DEVICE_JOYPAD:
    {
        const std::uint32_t mask = pad_buttons_to_retropad(state->buttons);
        if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
            return static_cast<std::int16_t>(mask & 0xffffu);
        if (id >= RARCH_FIRST_CUSTOM_BIND)
            return 0;
        return (mask & (UINT32_C(1) << id)) != 0 ? 1 : 0;
    }
    case RETRO_DEVICE_ANALOG:
    {
        const PadSample *newest = newest_sample(*state);
        if (newest == nullptr)
            return 0;
        if (index == RETRO_DEVICE_INDEX_ANALOG_LEFT)
        {
            if (id == RETRO_DEVICE_ID_ANALOG_X)
                return stick_axis(newest->left_x);
            if (id == RETRO_DEVICE_ID_ANALOG_Y)
                return stick_axis(newest->left_y);
        }
        else if (index == RETRO_DEVICE_INDEX_ANALOG_RIGHT)
        {
            if (id == RETRO_DEVICE_ID_ANALOG_X)
                return stick_axis(newest->right_x);
            if (id == RETRO_DEVICE_ID_ANALOG_Y)
                return stick_axis(newest->right_y);
        }
        else if (index == RETRO_DEVICE_INDEX_ANALOG_BUTTON)
        {
            /* Triggers arrive as bytes; RetroArch's axis runs from -0x8000 at
             * rest to 0x7fff fully pressed. */
            if (id == RETRO_DEVICE_ID_JOYPAD_L2)
                return static_cast<std::int16_t>(newest->left_trigger * 257 - 32768);
            if (id == RETRO_DEVICE_ID_JOYPAD_R2)
                return static_cast<std::int16_t>(newest->right_trigger * 257 - 32768);
        }
        return 0;
    }
    default:
        return 0;
    }
}

void ps5_input_free(void *data) noexcept
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
