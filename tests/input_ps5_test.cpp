/* Exercise the real raw joypad callbacks used by the binding screen. */
#include <cassert>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <unistd.h>
#include <vector>
#include "../src/input_ps5.cpp"
#include "input_state_wrap.inc"

static bool request_update_exit = false;
namespace
{
// Each user's controller has the user's id as its handle.
std::map<int32_t, std::vector<PadSample>> pending;
std::map<int32_t, int> read_results;
unsigned reads = 0, opens = 0, closes = 0, connects = 0, disconnects = 0;
unsigned port_connects[4] = {}, port_disconnects[4] = {};
int32_t login_users[4] = {1, -1, -1, -1};
int32_t refused_user = -1, held_user = -1;
unsigned vibrations = 0;
ScePadVibrationParam last_vibration{};
int32_t last_vibration_handle = -1;
std::mutex trace_mutex;
std::vector<std::string> traces;
unsigned traced(const char *text)
{
    std::lock_guard<std::mutex> lock(trace_mutex);
    unsigned count = 0;
    for (const auto &line : traces)
        count += line.find(text) != std::string::npos;
    return count;
}
unsigned vibration_modes = 0;
int32_t last_vibration_mode = -1;
int accepted_format = 1, opened_format = -1;
bool fail_thread = false, fail_advanced = false;
std::atomic<bool> fail_output{false}, recovered{false}, nonzero_pcm{false};
int vibration_result = 0;
int32_t haptic_open_result = -1;
unsigned audio_inits = 0, audio_opens = 0, audio_closes = 0;
std::atomic<unsigned> audio_outputs{0};
PadSample sample()
{
    PadSample p{};
    p.left_x = p.left_y = p.right_x = p.right_y = 128;
    p.connected = 1;
    p.timestamp_us = 10;
    return p;
}
void feed(PadSample p, int32_t handle = 1)
{
    pending[handle] = {p};
    read_results[handle] = 1;
    ps5_joypad.poll();
}
} // namespace
extern "C"
{
    int32_t scePadInit()
    {
        return 0;
    }
    int32_t scePadOpen(int32_t user, int32_t, int32_t, const void *)
    {
        ++opens;
        return user == refused_user ? -2137784316 : user;
    }
    int32_t scePadGetHandle(int32_t user, int32_t, int32_t)
    {
        return user == held_user ? user : -1;
    }
    int32_t scePadClose(int32_t)
    {
        ++closes;
        return 0;
    }
    int32_t scePadSetVibration(int32_t handle, const ScePadVibrationParam *param)
    {
        assert(handle >= 1 && param);
        ++vibrations;
        last_vibration = *param;
        last_vibration_handle = handle;
        return vibration_result;
    }
    int32_t scePadSetVibrationMode(int32_t handle, int32_t mode)
    {
        assert(handle >= 1);
        ++vibration_modes;
        last_vibration_mode = mode;
        return mode == 1 && fail_advanced ? -1 : 0;
    }
    int32_t scePadRead(int32_t handle, void *out, int32_t capacity)
    {
        ++reads;
        assert(capacity == 64);
        auto &queue = pending[handle];
        for (size_t i = 0; i < queue.size(); ++i)
            static_cast<PadSample *>(out)[i] = queue[i];
        queue.clear();
        int result = read_results[handle];
        read_results[handle] = 0;
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
    int32_t sceUserServiceGetLoginUserIdList(SceUserServiceLoginUserIdList *list)
    {
        std::memcpy(list->user_id, login_users, sizeof(login_users));
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
    int32_t sceAudioOutInit()
    {
        ++audio_inits;
        return 0;
    }
    int32_t sceAudioOutOpen(int32_t, int32_t type, int32_t, uint32_t frames, uint32_t rate,
                            uint32_t format)
    {
        assert(frames == 256 && rate == 48000);
        if (static_cast<int>(format) != accepted_format)
            return -1;
        opened_format = format;
        if (haptic_open_result >= 0)
        {
            assert(type == 10);
            ++audio_opens;
        }
        return haptic_open_result;
    }
    int32_t sceAudioOutOutput(int32_t handle, const void *data)
    {
        assert(handle == 77);
        // Consume every negotiated sample: ASan catches an undersized float buffer.
        bool nonzero = false;
        for (unsigned i = 0; i < 256 * 2; ++i)
        {
            const float value = opened_format == 4
                                    ? static_cast<const float *>(data)[i]
                                    : static_cast<const int16_t *>(data)[i] / 32768.0f;
            assert(std::isfinite(value) && value >= -1.0f && value <= 1.0f);
            nonzero |= value != 0.0f;
        }
        if (nonzero)
            nonzero_pcm = true;
        ++audio_outputs;
        usleep(1000);
        return fail_output ? -1 : 256;
    }
    int32_t sceAudioOutClose(int32_t handle)
    {
        assert(handle == 77);
        ++audio_closes;
        return 0;
    }
    void ps5_memory_report(const char *, std::size_t, int)
    {
    }

    void ps5_input_trace(const char *line) noexcept
    {
        if (std::strstr(line, "back to motor rumble"))
            recovered = true;
        std::lock_guard<std::mutex> lock(trace_mutex);
        traces.emplace_back(line);
    }
    bool input_autoconfigure_connect(const char *name, const char *, const char *,
                                     const char *driver, unsigned port, unsigned, unsigned)
    {
        assert(std::strcmp(name, "PS5 Controller") == 0);
        assert(std::strcmp(driver, "ps5") == 0 && port < 4);
        ++connects;
        ++port_connects[port];
        return true;
    }
    bool input_autoconfigure_disconnect(unsigned port, const char *)
    {
        assert(port < 4);
        ++disconnects;
        ++port_disconnects[port];
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
extern "C" int __real_pthread_create(pthread_t *, const pthread_attr_t *, void *(*)(void *),
                                     void *);
extern "C" int __wrap_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                                     void *(*fn)(void *), void *data)
{
    return fail_thread ? EAGAIN : __real_pthread_create(thread, attr, fn, data);
}
void wait_for(const std::atomic<bool> &flag)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!flag.load() && std::chrono::steady_clock::now() < deadline)
        usleep(1000);
    assert(flag.load());
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
    read_results[1] = -1;
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
    input_ps5.free(input);
    ps5_joypad.destroy();
    ps5_joypad.destroy();
    assert(closes == 1);
    assert(ps5_joypad.init(input));
    ps5_joypad.destroy();
    assert(opens == 2 && closes == 2);
    assert(vibration_modes == 2);
    ps5_input_reset_autoconfig(); // Safe before/after driver lifetime.

    // Both PCM formats, streaming failure with existing motor levels, and restart.
    haptic_open_result = 77;
    for (int format : {1, 4})
    {
        accepted_format = format;
        nonzero_pcm = false;
        recovered = false;
        assert(ps5_joypad.init(input));
        assert(opened_format == format && last_vibration_mode == 1);
        const auto before_motors = vibrations;
        vibration_result = -1; // The motor API need not work in advanced mode.
        assert(ps5_joypad.set_rumble(0, RETRO_RUMBLE_STRONG, 65535));
        assert(ps5_joypad.set_rumble(0, RETRO_RUMBLE_WEAK, 32768));
        wait_for(nonzero_pcm);
        assert(vibrations == before_motors);
        vibration_result = 0;
        fail_output = true;
        wait_for(recovered);
        assert(last_vibration_mode == 2);
        assert(last_vibration.largeMotor == 255 && last_vibration.smallMotor == 128);
        assert(ps5_joypad.set_rumble(0, RETRO_RUMBLE_STRONG, 0));
        assert(ps5_joypad.set_rumble(0, RETRO_RUMBLE_WEAK, 0));
        ps5_joypad.destroy();
        fail_output = false;
        assert(audio_closes == audio_opens);
    }
    // Either startup failure must close the port and restore motor rumble.
    for (int failure : {0, 1})
    {
        fail_thread = failure == 0;
        fail_advanced = failure == 1;
        assert(ps5_joypad.init(input));
        assert(last_vibration_mode == 2 && audio_closes == audio_opens);
        assert(!pads[0]->haptic_thread_started && pads[0]->haptic_port == -1);
        assert(ps5_joypad.set_rumble(0, RETRO_RUMBLE_STRONG, 65535));
        ps5_joypad.destroy();
        fail_thread = fail_advanced = false;
    }
    haptic_open_result = -1;
    assert(ps5_joypad.init(input));
    vibration_result = -1;
    assert(!ps5_joypad.set_rumble(0, RETRO_RUMBLE_STRONG, 65535));
    assert(!ps5_joypad.set_rumble(0, static_cast<retro_rumble_effect>(99), 65535));
    vibration_result = 0;
    ps5_joypad.destroy();
    assert(!ps5_joypad.set_rumble(0, RETRO_RUMBLE_STRONG, 65535));

    // Four players, as the console pairs controllers with signed-in users: port 0
    // is the user who started the title, a new user's controller takes the first
    // free port at the next scan, and a port is let go when its user signs out.
    {
        const auto scan = []
        {
            next_user_scan = std::chrono::steady_clock::time_point{};
            ps5_joypad.poll();
        };
        const unsigned opens_before = opens, closes_before = closes;
        assert(ps5_joypad.init(input));
        assert(opens == opens_before + 1 && pads[0] && pads[0]->user_id == 1 && !pads[1]);
        feed(sample());
        assert(ps5_joypad.query_pad(0) && !ps5_joypad.query_pad(1));
        assert(std::strcmp(ps5_joypad.name(0), "PS5 Controller") == 0 && !ps5_joypad.name(1));

        login_users[1] = 5;
        login_users[2] = 3;
        scan();
        assert(opens == opens_before + 3 && pads[1] && pads[1]->user_id == 5 && pads[1]->port == 1);
        assert(pads[2] && pads[2]->user_id == 3 && !pads[3]);
        assert(traced("signed-in users 1 5 3 -1") == 1);
        // A port is announced once its controller reports itself connected.
        assert(!ps5_joypad.query_pad(1) && std::strcmp(ps5_joypad.name(1), "PS5 Controller") == 0);
        auto two = sample();
        two.buttons = pad_button_cross;
        feed(two, 5);
        assert(ps5_joypad.query_pad(1) && port_connects[1] == 1);
        assert(ps5_joypad.button(1, RETRO_DEVICE_ID_JOYPAD_B) &&
               !ps5_joypad.button(0, RETRO_DEVICE_ID_JOYPAD_B));
        auto three = sample();
        three.right_x = 255;
        feed(three, 3);
        assert(ps5_joypad.query_pad(2) && ps5_joypad.axis(2, AXIS_POS(2)) == 32767);
        assert(ps5_joypad.axis(0, AXIS_POS(2)) == 0 && ps5_joypad.axis(1, AXIS_POS(2)) == 0);
        // Each port rumbles its own controller.
        assert(ps5_joypad.set_rumble(1, RETRO_RUMBLE_STRONG, 65535) && last_vibration_handle == 5);
        assert(ps5_joypad.set_rumble(2, RETRO_RUMBLE_WEAK, 65535) && last_vibration_handle == 3);
        assert(!ps5_joypad.set_rumble(3, RETRO_RUMBLE_STRONG, 65535));

        // User 5 signs out: port 1 is let go and announced gone; port 2 stays.
        login_users[1] = 3;
        login_users[2] = -1;
        const unsigned closes_mid = closes;
        scan();
        assert(!pads[1] && pads[2] && pads[2]->user_id == 3 && port_disconnects[1] == 1);
        assert(closes == closes_mid + 1 && !ps5_joypad.query_pad(1) &&
               !ps5_joypad.button(1, RETRO_DEVICE_ID_JOYPAD_B));
        // A controller that will not open is skipped, traced once, and tried again;
        // then the new user takes the first free port.
        login_users[2] = 7;
        refused_user = 7;
        scan();
        scan();
        assert(!pads[1] && traced("port 1: scePadOpen for user 7 failed") == 1);
        refused_user = -1;
        scan();
        assert(pads[1] && pads[1]->user_id == 7 && port_connects[1] == 1);
        // The user who started the title keeps port 0 after signing out.
        login_users[0] = 3;
        login_users[1] = 7;
        login_users[2] = 9;
        login_users[3] = -1;
        scan();
        assert(pads[0] && pads[0]->user_id == 1 && pads[3] && pads[3]->user_id == 9);
        // A full house: every port is taken, so a fourth signed-in user besides the
        // three has none, and is not even tried.
        login_users[3] = 11;
        const unsigned opens_full = opens;
        scan();
        assert(opens == opens_full);
        for (unsigned port = 0; port < 4; ++port)
            assert(pads[port] && pads[port]->user_id != 11);
        // The pad monitor traces each port's buttons when they change.
        pad_monitor = true;
        two.buttons = pad_button_circle;
        feed(two, 7);
        feed(two, 7);
        assert(traced("monitor port 1 user 7 buttons 0x00002000") == 1);
        two.buttons = pad_button_intercepted;
        feed(two, 7);
        assert(traced("buttons 0x80000000 (the system has it)") == 1 &&
               !ps5_joypad.button(1, RETRO_DEVICE_ID_JOYPAD_A));
        pad_monitor = false;
        // A config reset re-announces every connected port.
        const unsigned connects_before = connects;
        ps5_input_reset_autoconfig();
        ps5_joypad.poll();
        assert(connects == connects_before + 3); // ports 0, 1 and 2 had reported in
        ps5_joypad.destroy();
        assert(closes == closes_before + 5 && !pads[0] && !pads[3]); // user 5, then all four
        login_users[0] = 1;
        login_users[1] = login_users[2] = login_users[3] = -1;
    }

    assert(ps5_joypad.init(input));
    // Proton-style relative tracking: hover, drag, finger identity, release,
    // duplicate binding polls and loss of the pad must not jump or stick.
    auto mouse = [&](unsigned id, unsigned port = 0)
    {
        return input_ps5.input_state(input, &ps5_joypad, nullptr, nullptr, nullptr, false, port,
                                     RETRO_DEVICE_MOUSE, 0, id);
    };
    for (const char *core : {"DeSmuME", "Azahar", "melonDS DS"})
    {
        test_runloop.system.info.library_name = core;
        test_runloop.flags |= RUNLOOP_FLAG_CORE_RUNNING;
        p = sample();
        p.touch_count = 1;
        p.touches[0].id = 7;
        p.touches[0].x = 500;
        p.touches[0].y = 400;
        feed(p);
        input_ps5.poll(input);
        assert(mouse(RETRO_DEVICE_ID_MOUSE_X) == 0);
        p.touches[0].x += 20;
        p.touches[0].y -= 10;
        feed(p);
        ps5_joypad.poll(); // No new sample: pending movement is retained.
        input_ps5.poll(input);
        assert(mouse(RETRO_DEVICE_ID_MOUSE_X) == 20);
        assert(mouse(RETRO_DEVICE_ID_MOUSE_X) == 20); // Stable within this poll.
        assert(mouse(RETRO_DEVICE_ID_MOUSE_Y) == -10);
        assert(!mouse(RETRO_DEVICE_ID_MOUSE_LEFT));
        assert(!mouse(RETRO_DEVICE_ID_MOUSE_X, 1));
        input_ps5.poll(input);
        assert(!mouse(RETRO_DEVICE_ID_MOUSE_X));
        p.buttons = pad_button_touch_pad;
        feed(p);
        input_ps5.poll(input);
        assert(mouse(RETRO_DEVICE_ID_MOUSE_LEFT));
        assert(!ps5_joypad.button(0, RETRO_DEVICE_ID_JOYPAD_SELECT));
        p.touches[0].id = 8;
        p.touches[0].x = 10;
        feed(p);
        input_ps5.poll(input);
        assert(!mouse(RETRO_DEVICE_ID_MOUSE_X) && mouse(RETRO_DEVICE_ID_MOUSE_LEFT));
        p.touch_count = 0;
        p.buttons = 0;
        feed(p);
        input_ps5.poll(input);
        assert(!mouse(RETRO_DEVICE_ID_MOUSE_LEFT));
        p.touch_count = 1;
        p.touches[0].x = 800;
        feed(p);
        input_ps5.poll(input);
        assert(!mouse(RETRO_DEVICE_ID_MOUSE_X));
        for (int failure = 0; failure < 3; ++failure)
        {
            p.connected = 1;
            p.buttons = pad_button_touch_pad;
            feed(p);
            input_ps5.poll(input);
            assert(mouse(RETRO_DEVICE_ID_MOUSE_LEFT));
            if (failure == 0)
                p.connected = 0;
            if (failure == 1)
                p.buttons |= pad_button_intercepted;
            if (failure == 2)
            {
                read_results[1] = -1;
                ps5_joypad.poll();
            }
            else
                feed(p);
            input_ps5.poll(input);
            assert(!mouse(RETRO_DEVICE_ID_MOUSE_LEFT));
        }
        test_runloop.flags &= ~RUNLOOP_FLAG_CORE_RUNNING;
        p = sample();
        p.buttons = pad_button_touch_pad;
        feed(p);
        input_ps5.poll(input);
        assert(!mouse(RETRO_DEVICE_ID_MOUSE_LEFT));
        assert(ps5_joypad.button(0, RETRO_DEVICE_ID_JOYPAD_SELECT));
    }
    test_runloop.system.info.library_name = "Other core";
    test_runloop.flags |= RUNLOOP_FLAG_CORE_RUNNING;
    feed(p);
    assert(!mouse(RETRO_DEVICE_ID_MOUSE_LEFT));
    assert(ps5_joypad.button(0, RETRO_DEVICE_ID_JOYPAD_SELECT));
    test_runloop.flags = 0;
    test_runloop.system.info.library_name = nullptr;

    ps5_joypad.destroy();

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
    request_update_exit = true;
    test_video.frame_count = 50;
    ps5_input_init(nullptr);
    ps5_input_poll(nullptr);
    assert(test_runloop.max_frames == 51);
    test_video.frame_count = 51;
    ps5_input_poll(nullptr);
    assert(test_runloop.max_frames == 51); // repeated polls cannot postpone exit
    request_update_exit = false;
    std::puts("PS5 joypad: raw binding capture, axes, user mappings, rumble, poll retention, "
              "lifecycle, touchpad stylus, four players by signed-in user and the script's STOP "
              "and OPTION PASS");
}

namespace ps5_update
{
bool exit_requested()
{
    return request_update_exit;
}
} // namespace ps5_update
/* PS5_FrameGen's present-stream recorder (the pad script's RECORD): not linked here. */
extern "C" void ps5_fg_record_request(unsigned pictures)
{
    (void)pictures;
}
/* src/webui_link.cpp: no daemon here; the in-process request above is the one tested. */
extern "C" int ps5_webui_link_install_requested(void)
{
    return 0;
}
