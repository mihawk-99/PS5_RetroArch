/* Exercise the actual native driver with a paced AudioIn substitute. */
#include <audio/microphone_driver.h>
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>

extern "C" microphone_driver_t microphone_ps5;
static std::atomic<int> calls{0}, opens{0}, closes{0}, muted{0}, fail_read{0};
static bool reject_48k = false;
extern "C" int sceAudioInInit()
{
    return 0;
}
extern "C" int ps5_input_user_id(unsigned port)
{
    return port == 0 ? 123 : -1;
}
extern "C" int sceAudioInOpen(int user, int, int, unsigned grain, unsigned rate, unsigned format)
{
    assert(user == 123 && grain == 256 && format == 0);
    ++opens;
    return reject_48k && rate == 48000 ? -2 : 7;
}
extern "C" int sceAudioInInput(int handle, void *dest)
{
    assert(handle == 7);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    int sequence = ++calls;
    if (fail_read.load())
        return -42;
    std::fill_n(static_cast<int16_t *>(dest), 256, int16_t(sequence));
    return 256;
}
extern "C" int sceAudioInGetSilentState(int)
{
    return muted.load();
}
extern "C" int sceAudioInClose(int handle)
{
    assert(handle == 7);
    ++closes;
    return 0;
}
static void wait_calls(int target)
{
    for (int i = 0; i < 1000 && calls.load() < target; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(calls.load() >= target);
}

int main()
{
    auto &api = microphone_ps5;
    void *driver = api.init();
    assert(driver && opens == 0);
    unsigned rate = 0;
    void *mic = api.open_mic(driver, "default", 47605, 64, &rate);
    assert(mic && rate == 48000 && !api.mic_alive(driver, mic));
    assert(!api.open_mic(driver, "default", 48000, 64, nullptr));
    std::this_thread::sleep_for(std::chrono::milliseconds(8));
    assert(calls == 0); // Opening the handle does not capture before core opt-in.
    int16_t samples[256];
    assert(api.read(driver, mic, samples, sizeof(samples)) == -1);
    assert(api.start_mic(driver, mic));
    assert(api.start_mic(driver, mic)); // An already-active start must be harmless.
    wait_calls(24);
    int before = calls.load();
    assert(api.read(driver, mic, samples, sizeof(samples)) == sizeof(samples));
    assert(samples[0] >= before - 18 &&
           samples[0] > 1); // Drops old audio instead of growing without bound.
    assert(std::all_of(samples, samples + 256, [](int16_t x) { return x > 0; }));
    int16_t across_wrap[5000];
    assert(api.read(driver, mic, across_wrap, sizeof(across_wrap)) == sizeof(across_wrap));
    assert(across_wrap[0] > 0 && std::is_sorted(across_wrap, across_wrap + 5000));
    muted = 4;
    wait_calls(calls.load() + 4);
    assert(api.read(driver, mic, samples, sizeof(samples)) == sizeof(samples));
    assert(std::all_of(samples, samples + 256, [](int16_t x) { return x == 0; }));
    assert(api.stop_mic(driver, mic));
    int stopped = calls.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(8));
    assert(calls == stopped && !api.mic_alive(driver, mic));
    assert(api.read(driver, mic, nullptr, 0) == 0);
    assert(api.read(driver, mic, samples, 1) == -1);
    api.close_mic(driver, mic);
    assert(closes == 1);

    // Unsupported 48k falls back to the independently device-tested 16k route.
    reject_48k = true;
    mic = api.open_mic(driver, "default", 47605, 64, &rate);
    assert(mic && rate == 16000);
    api.set_nonblock_state(driver, true);
    assert(api.start_mic(driver, mic));
    int got = api.read(driver, mic, samples, sizeof(samples));
    assert(got >= 0 && got <= int(sizeof(samples)) && got % 2 == 0);
    muted = 0;
    fail_read = 1;
    wait_calls(calls.load() + 1);
    for (int i = 0; i < 100 && api.mic_alive(driver, mic); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(!api.mic_alive(driver, mic));
    assert(api.read(driver, mic, samples, sizeof(samples)) == -1);
    api.close_mic(driver, mic);
    api.free(driver);
    assert(closes == 2);
    std::puts(
        "microphone backend: opt-in, bounded queue, mute, stop, fallback and input failure PASS");
}
