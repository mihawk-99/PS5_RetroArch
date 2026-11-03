/* The controllers' speakers (src/pad_speaker_ps5.cpp, patch 0139) against a mocked AudioOut:
 * open, the order samples reach the speaker, silence when nothing is queued, the queue's limit,
 * a speaker that will not open, one that stops answering, and close. */
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern "C"
{
std::int32_t sceAudioOutInit();
std::int32_t sceAudioOutOpen(std::int32_t, std::int32_t, std::int32_t, std::uint32_t, std::uint32_t,
                             std::uint32_t);
std::int32_t sceAudioOutOutput(std::int32_t, const void *);
std::int32_t sceAudioOutClose(std::int32_t);
int ps5_input_user_id(unsigned port);
void ps5_input_trace(const char *line) noexcept;
}

#include "../src/pad_speaker_ps5.cpp"

namespace
{
std::mutex mock;
std::condition_variable changed;
struct Open
{
    std::int32_t user, type, index;
    std::uint32_t len, freq, param;
};
std::vector<Open> opens;
std::vector<std::int32_t> closes;
std::vector<std::vector<std::int16_t>> grains; /* what each output call was handed */
std::vector<std::string> lines;
int users[4] = {0x1001, -1, 0x1003, 0x1004};
std::int32_t open_result = 7;
std::int32_t output_result = 0;
bool hold = false; /* output waits (the speaker's pace) until released */

template <typename Done> void wait_for(Done done)
{
    std::unique_lock<std::mutex> lock(mock);
    assert(changed.wait_for(lock, std::chrono::seconds(5), done));
}
bool traced(const char *text)
{
    std::lock_guard<std::mutex> lock(mock);
    for (const std::string &line : lines)
        if (line.find(text) != std::string::npos)
            return true;
    return false;
}
} // namespace

extern "C"
{
std::int32_t sceAudioOutInit()
{
    return 0;
}
std::int32_t sceAudioOutOpen(std::int32_t user, std::int32_t type, std::int32_t index,
                             std::uint32_t len, std::uint32_t freq, std::uint32_t param)
{
    std::lock_guard<std::mutex> lock(mock);
    opens.push_back({user, type, index, len, freq, param});
    return open_result;
}
std::int32_t sceAudioOutOutput(std::int32_t, const void *data)
{
    std::unique_lock<std::mutex> lock(mock);
    changed.wait(lock, [] { return !hold; });
    const auto *samples = static_cast<const std::int16_t *>(data);
    grains.emplace_back(samples, samples + grain);
    changed.notify_all();
    lock.unlock();
    std::this_thread::sleep_for(std::chrono::microseconds(200));
    return output_result;
}
std::int32_t sceAudioOutClose(std::int32_t handle)
{
    std::lock_guard<std::mutex> lock(mock);
    closes.push_back(handle);
    return 0;
}
int ps5_input_user_id(unsigned port)
{
    return port < 4 ? users[port] : -1;
}
void ps5_input_trace(const char *line) noexcept
{
    std::lock_guard<std::mutex> lock(mock);
    lines.emplace_back(line);
}
}

int main()
{
    const ps5_pad_speaker *speaker = ps5_pad_speaker_interface();
    assert(speaker && speaker->version == PS5_PAD_SPEAKER_VERSION);

    /* A port with no signed-in controller, and one out of range, do not open. */
    assert(!speaker->open(1));
    assert(!speaker->open(4));
    assert(opens.empty() && traced("port 2 has no signed-in controller"));
    assert(speaker->write(1, nullptr, 0) == 0);

    /* Open: the pad speaker port of that user, 48 kHz mono S16; a second open is the same one. */
    {
        std::lock_guard<std::mutex> lock(mock);
        hold = true;
    }
    assert(speaker->open(0));
    assert(speaker->open(0));
    assert(opens.size() == 1);
    assert(opens[0].user == 0x1001 && opens[0].type == 4 && opens[0].freq == 48000 &&
           opens[0].param == 0 && opens[0].len == grain);
    assert(traced("pad speaker: port 1 opened user=4097 handle=7"));

    /* Samples reach the speaker in the order written; a short grain is filled with silence. */
    std::vector<std::int16_t> ramp(grain + 10);
    for (std::size_t i = 0; i < ramp.size(); ++i)
        ramp[i] = static_cast<std::int16_t>(i + 1);
    {
        std::lock_guard<std::mutex> lock(mock);
        grains.clear();
    }
    assert(speaker->write(0, ramp.data(), ramp.size()) == ramp.size());
    {
        std::lock_guard<std::mutex> lock(mock);
        hold = false;
    }
    changed.notify_all();
    wait_for([] {
        for (const auto &g : grains)
            if (g[0] == static_cast<std::int16_t>(grain + 1))
                return true;
        return false;
    });
    {
        std::lock_guard<std::mutex> lock(mock);
        /* The grain that took the ramp's start (one may have gone out, silent, before it). */
        std::size_t first = 0;
        while (grains[first][0] != 1)
            ++first;
        for (unsigned i = 0; i < grain; ++i)
            assert(grains[first][i] == static_cast<std::int16_t>(i + 1));
        const auto &rest = grains[first + 1];
        for (unsigned i = 0; i < 10; ++i)
            assert(rest[i] == static_cast<std::int16_t>(grain + i + 1));
        for (unsigned i = 10; i < grain; ++i)
            assert(rest[i] == 0);
    }

    /* The queue holds a fifth of a second; the rest is refused (and counted). */
    {
        std::lock_guard<std::mutex> lock(mock);
        hold = true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20)); /* the feeder waits in output */
    std::vector<std::int16_t> lots(queue_size + 500, 3);
    const std::size_t taken = speaker->write(0, lots.data(), lots.size());
    assert(taken <= queue_size && taken >= queue_size - grain);
    {
        std::lock_guard<std::mutex> lock(mock);
        hold = false;
    }
    changed.notify_all();

    /* Close: the thread stops, the port closes, and the line says how it went. */
    speaker->close(0);
    assert(closes.size() == 1 && closes[0] == 7);
    assert(traced("pad speaker: port 1 closed outputs="));
    assert(traced("dropped="));
    assert(speaker->write(0, ramp.data(), 4) == 0);
    speaker->close(0); /* closing a closed one does nothing */
    assert(closes.size() == 1);

    /* A speaker the system refuses: false, with the result in hex. */
    open_result = static_cast<std::int32_t>(0x80260005u);
    assert(!speaker->open(2));
    assert(traced("pad speaker: port 3 open failed user=4099 result=80260005"));
    open_result = 9;

    /* A speaker that stops answering: its thread ends and says so; close still closes it. */
    output_result = -1;
    assert(speaker->open(3));
    for (int i = 0; i < 400 && !traced("port 4 stopped answering"); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    assert(traced("pad speaker: port 4 stopped answering result=ffffffff"));
    output_result = 0;

    /* Every speaker closes with the core. */
    assert(speaker->open(0));
    ps5_pad_speaker_close_all();
    assert(closes.size() == 3);
    return 0;
}
