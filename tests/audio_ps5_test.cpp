/* Native output is a deterministic, explicitly clocked device in these tests. */
#include <cassert>
#include <condition_variable>
#include <future>
#include <mutex>
#include <vector>
#include "../src/audio_ps5.cpp"

namespace
{
std::mutex device_mutex;
std::condition_variable device_changed;
std::vector<std::vector<int16_t>> blocks;
unsigned permits = 0, opens = 0, closes = 0, drains = 0;
int init_result = 0, open_result = 1, output_result = 0;

void reset()
{
    blocks.clear();
    permits = opens = closes = drains = 0;
    init_result = output_result = 0;
    open_result = 1;
}
void entered(size_t count)
{
    std::unique_lock<std::mutex> lock(device_mutex);
    assert(device_changed.wait_for(lock, std::chrono::seconds(5),
                                   [&] { return blocks.size() >= count; }));
}
void tick()
{
    std::lock_guard<std::mutex> lock(device_mutex);
    ++permits;
    device_changed.notify_all();
}
void wait_state(Audio *a, bool shutdown)
{
    pthread_mutex_lock(&a->mutex);
    while (shutdown ? !a->shutdown : a->active)
        pthread_cond_wait(&a->changed, &a->mutex);
    pthread_mutex_unlock(&a->mutex);
}
void finish(Audio *a)
{
    auto done = std::async(std::launch::async, [&] { audio_ps5.free(a); });
    wait_state(a, true);
    tick();
    done.get();
    assert(closes == 1);
}
Audio *create(unsigned latency = 10)
{
    unsigned actual = 0;
    auto *a = static_cast<Audio *>(audio_ps5.init(nullptr, 44100, latency, 0, &actual));
    assert(a && actual == 48000 && !audio_ps5.use_float(a));
    entered(1); // Worker is inside output; no wall-clock sleeps are used.
    return a;
}
std::vector<int16_t> pcm(unsigned first, unsigned frames)
{
    std::vector<int16_t> samples(frames * 2);
    for (unsigned i = 0; i < frames; ++i)
    {
        samples[i * 2] = int16_t(first + i);
        samples[i * 2 + 1] = -int16_t(first + i);
    }
    return samples;
}
void fifo_and_lifecycle()
{
    reset();
    Audio *a = create();
    assert(audio_ps5.buffer_size(a) == 512 * 4);
    assert(audio_ps5.write_avail(a) == 512 * 4);
    assert(audio_ps5.write(a, nullptr, 4) == -1);
    auto data = pcm(1, 1024);
    assert(audio_ps5.write(a, data.data(), 3) == -1);
    assert(audio_ps5.write(a, data.data(), 253 * 4) == 253 * 4);
    assert(audio_ps5.write(a, data.data() + 253 * 2, 259 * 4) == 259 * 4);
    audio_ps5.set_nonblock_state(a, true);
    assert(audio_ps5.write_avail(a) == 0);
    assert(audio_ps5.write(a, data.data(), 4) == 0);
    tick();
    entered(2);
    assert(audio_ps5.write_avail(a) == 256 * 4);
    assert(audio_ps5.write(a, data.data() + 512 * 2, 300 * 4) == 256 * 4);
    tick();
    entered(3);
    assert(audio_ps5.write(a, data.data() + 768 * 2, 256 * 4) == 256 * 4);
    tick();
    entered(4);
    tick();
    entered(5);
    {
        std::lock_guard<std::mutex> lock(device_mutex);
        for (size_t b = 1; b <= 4; ++b)
            for (size_t i = 0; i < grain * 2; ++i)
                assert(blocks[b][i] == data[(b - 1) * grain * 2 + i]);
    }
    // Partial blocks must zero-fill the tail, not replay old samples.
    auto tail = pcm(1200, 17);
    assert(audio_ps5.write(a, tail.data(), tail.size() * 2) == ssize_t(tail.size() * 2));
    tick();
    entered(6);
    {
        std::lock_guard<std::mutex> lock(device_mutex);
        for (size_t i = 0; i < grain * 2; ++i)
            assert(blocks[5][i] == (i < tail.size() ? tail[i] : 0));
    }
    assert(audio_ps5.write(a, data.data(), 256 * 4) == 256 * 4);
    auto paused = std::async(std::launch::async, [&] { return audio_ps5.stop(a); });
    wait_state(a, false);
    tick();
    assert(paused.get() && !audio_ps5.alive(a));
    assert(audio_ps5.write(a, data.data(), 4) == 0);
    assert(audio_ps5.start(a, false));
    entered(7);
    {
        std::lock_guard<std::mutex> lock(device_mutex);
        for (int16_t sample : blocks[6])
            assert(sample == 0);
    }
    assert(a->accepted == 1041 + 256 && a->played == 1041 && a->discarded == 256);
    finish(a);
}
void blocking_and_failure()
{
    reset();
    Audio *a = create();
    auto data = pcm(1, 512);
    assert(audio_ps5.write(a, data.data(), 2048) == 2048);
    auto writer =
        std::async(std::launch::async, [&] { return audio_ps5.write(a, data.data(), 1024); });
    tick();
    entered(2);
    assert(writer.get() == 1024); // Full queue released by one native output block.
    auto blocked =
        std::async(std::launch::async, [&] { return audio_ps5.write(a, data.data(), 4); });
    {
        std::lock_guard<std::mutex> lock(device_mutex);
        output_result = -77;
    }
    tick();
    assert(blocked.get() == -1);
    assert(!audio_ps5.alive(a) && !audio_ps5.start(a, false));
    audio_ps5.free(a);
    assert(closes == 1);
}
} // namespace
extern "C" int32_t sceAudioOutInit()
{
    return init_result;
}
extern "C" int32_t sceAudioOutOpen(int32_t user, int32_t type, int32_t index, uint32_t count,
                                   uint32_t frequency, uint32_t format)
{
    assert(user == 0xff && type == 0 && index == 0 && count == 256 && frequency == 48000 &&
           format == 1);
    ++opens;
    return open_result;
}
extern "C" int32_t sceAudioOutOutput(int32_t handle, const void *buffer)
{
    assert(handle == 1);
    if (!buffer)
    {
        ++drains;
        return 0;
    }
    std::unique_lock<std::mutex> lock(device_mutex);
    const auto *samples = static_cast<const int16_t *>(buffer);
    blocks.emplace_back(samples, samples + 512);
    device_changed.notify_all();
    device_changed.wait(lock, [] { return permits > 0; });
    --permits;
    return output_result;
}
extern "C" int32_t sceAudioOutClose(int32_t)
{
    ++closes;
    return 0;
}
extern "C" const char *ps5_frontend_build_identity()
{
    return "host-test";
}
int main()
{
    reset();
    init_result = -2;
    assert(!audio_ps5.init(nullptr, 48000, 64, 0, nullptr) && opens == 0);
    reset();
    open_result = -3;
    assert(!audio_ps5.init(nullptr, 48000, 64, 0, nullptr) && closes == 0);
    reset();
    init_result = int32_t(already_initialized);
    Audio *a = create(9999);
    assert(audio_ps5.buffer_size(a) == 8192 * 4);
    finish(a);
    fifo_and_lifecycle();
    blocking_and_failure();
    std::puts("audio_ps5: native ABI, byte counts, rate, queue wrap/backpressure, pause/resume and "
              "failure PASS");
}
