/* Exercise the real raw joypad callbacks used by the binding screen. */
#include <cassert>
#include <cmath>
#include <cstring>
#include <vector>
#include "../src/input_ps5.cpp"
#include "input_state_wrap.inc"

namespace
{
std::vector<PadSample> pending;
int read_result = 0;
unsigned reads = 0, opens = 0, closes = 0, connects = 0, disconnects = 0;
unsigned vibrations = 0;
ScePadVibrationParam last_vibration{};
unsigned vibration_modes = 0;
int32_t last_vibration_mode = -1;
unsigned trigger_effects = 0;
ScePadTriggerEffectParam last_trigger_effect{};
PadSample sample()
{
    PadSample p{};
    p.left_x = p.left_y = p.right_x = p.right_y = 128;
    p.connected = 1;
    p.timestamp_us = 10;
    return p;
}
void feed(PadSample p)
{
    pending = {p};
    read_result = 1;
    ps5_joypad.poll();
}
} // namespace
extern "C"
{
    int32_t scePadInit()
    {
        return 0;
    }
    int32_t scePadOpen(int32_t, int32_t, int32_t, const void *)
    {
        ++opens;
        return 1;
    }
    int32_t scePadClose(int32_t)
    {
        ++closes;
        return 0;
    }
    int32_t scePadSetVibration(int32_t handle, const ScePadVibrationParam *param)
    {
        assert(handle == 1 && param);
        ++vibrations;
        last_vibration = *param;
        return 0;
    }
    int32_t scePadSetVibrationMode(int32_t handle, int32_t mode)
    {
        assert(handle == 1);
        ++vibration_modes;
        last_vibration_mode = mode;
        return 0;
    }
    int32_t scePadSetTriggerEffect(int32_t handle, const ScePadTriggerEffectParam *param)
    {
        assert(handle == 1 && param && param->trigger_mask == 0x03);
        ++trigger_effects;
        last_trigger_effect = *param;
        return 0;
    }
    int32_t scePadRead(int32_t, void *out, int32_t capacity)
    {
        ++reads;
        assert(capacity == 64);
        for (size_t i = 0; i < pending.size(); ++i)
            static_cast<PadSample *>(out)[i] = pending[i];
        pending.clear();
        int result = read_result;
        read_result = 0;
        return result;
    }
    int32_t sceUserServiceInitialize(const void *)
    {
        return 0;
    }
    int32_t sceUserServiceGetInitialUser(int32_t *user)
    {
        *user = 1;
        return 0;
    }
    int32_t sceUserServiceTerminate()
    {
        return 0;
    }
    int32_t sceKernelUsleep(uint32_t)
    {
        return 0;
    }
    void ps5_input_trace(const char *) noexcept
    {
    }
    bool input_autoconfigure_connect(const char *name, const char *, const char *,
                                     const char *driver, unsigned port, unsigned, unsigned)
    {
        assert(std::strcmp(name, "PS5 Controller") == 0);
        assert(std::strcmp(driver, "ps5") == 0 && port == 0);
        ++connects;
        return true;
    }
    bool input_autoconfigure_disconnect(unsigned port, const char *)
    {
        assert(port == 0);
        ++disconnects;
        return true;
    }
    // The core options the script's OPTION action sets: one option, index 7,
    // whose value "8x" is its fourth (index 3)
    size_t option_set_idx = 99, option_set_val = 99;
    bool core_option_manager_get_idx(core_option_manager_t *opt, const char *key, size_t *idx)
    {
        assert(opt);
        if (std::strcmp(key, "mupen64plus-parallel-rdp-upscaling") != 0)
            return false;
        *idx = 7;
        return true;
    }
    bool core_option_manager_get_val_idx(core_option_manager_t *, size_t idx, const char *val,
                                         size_t *val_idx)
    {
        if (idx != 7 || std::strcmp(val, "8x") != 0)
            return false;
        *val_idx = 3;
        return true;
    }
    void core_option_manager_set_val(core_option_manager_t *, size_t idx, size_t val_idx, bool)
    {
        option_set_idx = idx;
        option_set_val = val_idx;
    }
}
// The runloop and video state the script's STOP action arms.
runloop_state_t test_runloop{};
video_driver_state_t test_video{};
runloop_state_t *runloop_state_get_ptr(void)
{
    return &test_runloop;
}
video_driver_state_t *video_state_get_ptr(void)
{
    return &test_video;
}
int main()
{
    void *input = input_ps5.init("");
    assert(ps5_joypad.init(input));
    assert(opens == 1 && !ps5_joypad.query_pad(0));
    // Opening the pad selects compatible vibration mode once (DualSense opens
    // in advanced/haptics mode, where the two motor levels move nothing).
    assert(vibration_modes == 1 && last_vibration_mode == 2);
    auto p = sample();
    feed(p);
    assert(ps5_joypad.query_pad(0) && !ps5_joypad.query_pad(1) && connects == 1);
    assert(ps5_joypad.axis(0, AXIS_POS(0)) == 0);
    size_t n;
    const auto *map = ps5_input_button_map(&n);
    for (size_t i = 0; i < n; ++i)
    {
        p = sample();
        p.buttons = map[i * 2 + 1];
        feed(p);
        for (unsigned b = 0; b < 16; ++b)
            assert(ps5_joypad.button(0, b) == (b == map[i * 2]));
    }
    assert(!ps5_joypad.button(0, NO_BTN) && !ps5_joypad.button(1, 0));
    assert(!ps5_joypad.button(0, HAT_MAP(0, HAT_UP_MASK)));
    p = sample();
    p.left_x = 0;
    p.left_y = 255;
    p.right_x = 255;
    p.right_y = 0;
    p.left_trigger = 255;
    feed(p);
    for (unsigned a = 0; a < 4; ++a)
    {
        int sign = (a == 0 || a == 3) ? -1 : 1;
        assert(ps5_joypad.axis(0, AXIS_NEG(a)) == (sign < 0 ? -32767 : 0));
        assert(ps5_joypad.axis(0, AXIS_POS(a)) == (sign > 0 ? 32767 : 0));
    }
    assert(ps5_joypad.axis(0, AXIS_POS(4)) == 32767);
    assert(ps5_joypad.axis(0, AXIS_NEG(4)) == 0);
    assert(ps5_joypad.button(0, RETRO_DEVICE_ID_JOYPAD_L2));
    assert(!ps5_joypad.button(0, RETRO_DEVICE_ID_JOYPAD_R2));
    assert(ps5_joypad.axis(0, AXIS_NONE) == 0 && ps5_joypad.axis(1, AXIS_POS(0)) == 0);
    assert(ps5_joypad.axis(0, AXIS_POS(31)) == 0);
    // Binding capture polls again in the same frame; no new samples is not a release.
    unsigned before = reads;
    input_ps5.poll(input);
    assert(reads == before);
    ps5_joypad.poll();
    assert(ps5_joypad.axis(0, AXIS_NEG(0)) == -32767);
    assert(ps5_joypad.button(0, RETRO_DEVICE_ID_JOYPAD_L2));

    static retro_keybind_set binds[1]{};
    static retro_keybind autos[RARCH_BIND_LIST_END]{};
    for (unsigned i = 0; i < RARCH_BIND_LIST_END; ++i)
    {
        binds[0][i].valid = true;
        binds[0][i].joykey = NO_BTN;
        binds[0][i].joyaxis = AXIS_NONE;
        autos[i].joykey = NO_BTN;
        autos[i].joyaxis = AXIS_NONE;
    }
    autos[RETRO_DEVICE_ID_JOYPAD_B].joykey = RETRO_DEVICE_ID_JOYPAD_B;
    // Bind B to Square, not Cross, and A to the negative left X axis.
    binds[0][RETRO_DEVICE_ID_JOYPAD_B].joykey = RETRO_DEVICE_ID_JOYPAD_Y;
    binds[0][RETRO_DEVICE_ID_JOYPAD_A].joyaxis = AXIS_NEG(0);
    rarch_joypad_info_t info{};
    info.auto_binds = autos;
    info.joy_idx = 0;
    info.axis_threshold = 0.5f;
    auto mapped = [&](unsigned id)
    {
        return input_state_wrap(&input_ps5, input, &ps5_joypad, nullptr, &info, binds, false, 0,
                                RETRO_DEVICE_JOYPAD, 0, id);
    };
    p = sample();
    p.buttons = pad_button_cross;
    feed(p);
    assert(mapped(RETRO_DEVICE_ID_JOYPAD_B) == 0);
    assert(mapped(RETRO_DEVICE_ID_JOYPAD_MASK) == 0);
    p.buttons = pad_button_square;
    p.left_x = 0;
    feed(p);
    assert(mapped(RETRO_DEVICE_ID_JOYPAD_B) == 1);
    assert(mapped(RETRO_DEVICE_ID_JOYPAD_A) == 1);
    assert(mapped(RETRO_DEVICE_ID_JOYPAD_MASK) ==
           ((1 << RETRO_DEVICE_ID_JOYPAD_B) | (1 << RETRO_DEVICE_ID_JOYPAD_A)));
    // Use the same upstream analog helper as menu navigation, including deadzone.
    const unsigned minus[] = {RARCH_ANALOG_LEFT_X_MINUS, RARCH_ANALOG_LEFT_Y_MINUS,
                              RARCH_ANALOG_RIGHT_X_MINUS, RARCH_ANALOG_RIGHT_Y_MINUS};
    const unsigned plus[] = {RARCH_ANALOG_LEFT_X_PLUS, RARCH_ANALOG_LEFT_Y_PLUS,
                             RARCH_ANALOG_RIGHT_X_PLUS, RARCH_ANALOG_RIGHT_Y_PLUS};
    for (unsigned a = 0; a < 4; ++a)
    {
        autos[minus[a]].joyaxis = AXIS_NEG(a);
        autos[plus[a]].joyaxis = AXIS_POS(a);
    }
    auto menu_axis = [&](unsigned stick, unsigned axis)
    {
        return input_joypad_analog_axis(ANALOG_DPAD_NONE, 0.15f, 1.0f, &ps5_joypad, &info, stick,
                                        axis, binds[0]);
    };
    assert(menu_axis(RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X) == -32767);
    assert(menu_axis(RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y) == 0);
    p.left_x = 140;
    feed(p);
    assert(menu_axis(RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X) == 0);
    p.left_y = 255;
    feed(p);
    assert(menu_axis(RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y) > 30000);
    assert(std::strstr(ps5_controller_profile, "input_l_x_minus_axis = \"-0\"\n"));
    assert(std::strstr(ps5_controller_profile, "input_l_y_plus_axis = \"+1\"\n"));
    p.buttons |= pad_button_intercepted;
    feed(p);
    assert(mapped(RETRO_DEVICE_ID_JOYPAD_MASK) == 0 && disconnects == 0);
    p.connected = 0;
    feed(p);
    assert(!ps5_joypad.query_pad(0) && disconnects == 1);
    p = sample();
    feed(p);
    assert(connects == 2);
    // A config reset must refresh automatic binds without a physical reconnect.
    ps5_input_reset_autoconfig();
    ps5_joypad.poll(); // No new samples; the connected sample is retained.
    assert(connects == 3 && opens == 1 && ps5_joypad.query_pad(0));
    ps5_joypad.poll();
    assert(connects == 3); // No per-frame announcement/task storm.
    read_result = -1;
    ps5_joypad.poll();
    assert(!ps5_joypad.query_pad(0) && mapped(RETRO_DEVICE_ID_JOYPAD_MASK) == 0);
    input_bits_t bits;
    std::memset(&bits, 0xff, sizeof(bits));
    ps5_joypad.get_buttons(1, &bits);
    for (auto byte : bits.data)
        assert(byte == 0);
    // Rumble: strength scales 0..65535 to the pad's 0..255 motors. Each call
    // restates both motors because scePadSetVibration is the whole state.
    assert(ps5_joypad.set_rumble(0, RETRO_RUMBLE_STRONG, 65535));
    assert(last_vibration.largeMotor == 255 && last_vibration.smallMotor == 0);
    assert(ps5_joypad.set_rumble(0, RETRO_RUMBLE_WEAK, 32768));
    assert(last_vibration.largeMotor == 255 && last_vibration.smallMotor == 128);
    assert(ps5_joypad.set_rumble(0, RETRO_RUMBLE_STRONG, 0));
    assert(last_vibration.largeMotor == 0 && last_vibration.smallMotor == 128);
    assert(!ps5_joypad.set_rumble(1, RETRO_RUMBLE_STRONG, 65535)); // no second pad
    assert(vibrations == 3);
    // Rumble also drives the adaptive triggers: vibration mode on both
    // triggers, amplitude/frequency tracking the stronger motor, and the
    // service call skipped while the effect is unchanged.
    assert(trigger_effects == 2); // the WEAK call at same max level was deduped
    assert(last_trigger_effect.command[0].mode == 3 &&
           last_trigger_effect.command[1].mode == 3);
    assert(last_trigger_effect.command[0].command_data[1] == 5); // amplitude for 128
    assert(last_trigger_effect.command[0].command_data[2] == 174); // 110 + 128/2
    assert(ps5_joypad.set_rumble(0, RETRO_RUMBLE_WEAK, 0));
    assert(trigger_effects == 3); // level 0 releases the triggers
    assert(last_trigger_effect.command[0].mode == 0);
    assert(ps5_joypad.set_rumble(0, RETRO_RUMBLE_STRONG, 0));
    assert(trigger_effects == 3); // already released; no repeat call
    input_ps5.free(input);
    ps5_joypad.destroy();
    ps5_joypad.destroy();
    assert(closes == 1);
    assert(ps5_joypad.init(input));
    ps5_joypad.destroy();
    assert(opens == 2 && closes == 2);
    assert(vibration_modes == 2);
    ps5_input_reset_autoconfig(); // Safe before/after driver lifetime.

    // STOP ends the run on the next frame, as --max-frames does, and only once.
    test_video.frame_count = 1234;
    actions[0] = ScriptAction{0.0, false, ScriptActionKind::stop, -1, {}, {}};
    action_count = 1;
    run_script_actions();
    assert(test_runloop.max_frames == 1235 && actions[0].done);
    test_video.frame_count = 2000;
    run_script_actions();
    assert(test_runloop.max_frames == 1235);

    // OPTION sets a core option by key and value, as the Quick Menu does, and
    // leaves the options alone when the core has no such option or value.
    core_option_manager_t *options = reinterpret_cast<core_option_manager_t *>(&test_video);
    test_runloop.core_options = options;
    actions[0] = ScriptAction{
        0.0, false, ScriptActionKind::option, -1, "mupen64plus-parallel-rdp-upscaling", "8x"};
    actions[1] = ScriptAction{
        0.0, false, ScriptActionKind::option, -1, "mupen64plus-parallel-rdp-upscaling", "16x"};
    actions[2] = ScriptAction{0.0, false, ScriptActionKind::option, -1, "no-such-option", "8x"};
    action_count = 1;
    run_script_actions();
    assert(actions[0].done && option_set_idx == 7 && option_set_val == 3);
    option_set_idx = option_set_val = 99;
    action_count = 3;
    actions[0] = actions[2];
    run_script_actions();
    assert(option_set_idx == 99 && option_set_val == 99);
    test_runloop.core_options = nullptr;
    action_count = 0;
    std::puts("PS5 joypad: raw binding capture, axes, user mappings, rumble, poll retention, "
              "lifecycle and the script's STOP and OPTION PASS");
}
