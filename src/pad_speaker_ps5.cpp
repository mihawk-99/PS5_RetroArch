/* PS5 RetroArch - the controllers' own speakers, for a core (pad_speaker_ps5.h).
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "pad_speaker_ps5.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <pthread.h>

#include "title_threads.hpp"

extern "C"
{
    std::int32_t sceAudioOutInit();
    std::int32_t sceAudioOutOpen(std::int32_t user_id, std::int32_t type, std::int32_t index,
                                 std::uint32_t len, std::uint32_t freq, std::uint32_t param);
    std::int32_t sceAudioOutOutput(std::int32_t handle, const void *data);
    std::int32_t sceAudioOutClose(std::int32_t handle);
    int ps5_input_user_id(unsigned port);       /* src/input_ps5.cpp */
    void ps5_input_trace(const char *line) noexcept;
}

namespace
{
constexpr std::int32_t pad_speaker_type = 4;   /* SCE_AUDIO_OUT_PORT_TYPE_PADSPK */
constexpr std::uint32_t mono_s16 = 0;          /* SCE_AUDIO_OUT_PARAM_FORMAT_S16_MONO */
constexpr std::uint32_t grain = 256;           /* samples per sceAudioOutOutput, ~5.3 ms */
constexpr std::size_t queue_size = 9600;       /* 200 ms */
constexpr unsigned ports = 4;

struct Speaker
{
    std::mutex lock; /* the queue */
    std::int16_t queue[queue_size];
    std::size_t head = 0, count = 0;
    std::int32_t handle = -1;
    pthread_t thread{};
    bool running = false;
    std::atomic<bool> stop{false};
    /* For the line at close: grains output, grains that had too few samples (silence filled in),
     * failed outputs, samples refused because the queue was full. */
    unsigned outputs = 0, short_grains = 0, errors = 0;
    std::size_t dropped = 0;
};

Speaker speakers[ports];
std::mutex life; /* open / close */

template <typename... Values> void trace(const char *format, Values... values)
{
    char line[160];
    std::snprintf(line, sizeof line, format, values...);
    ps5_input_trace(line);
}

void *feeder(void *opaque)
{
    auto *s = static_cast<Speaker *>(opaque);
    const unsigned port = static_cast<unsigned>(s - speakers);
    alignas(16) std::int16_t out[grain];
    int failures = 0;
    while (!s->stop.load(std::memory_order_relaxed))
    {
        {
            std::lock_guard<std::mutex> guard(s->lock);
            const std::size_t take = std::min<std::size_t>(grain, s->count);
            for (std::size_t i = 0; i < take; ++i)
                out[i] = s->queue[(s->head + i) % queue_size];
            s->head = (s->head + take) % queue_size;
            s->count -= take;
            if (take < grain)
                ++s->short_grains;
            std::memset(out + take, 0, (grain - take) * sizeof out[0]);
        }
        /* Blocks until the speaker wants the next grain: this is the pace. */
        const std::int32_t result = sceAudioOutOutput(s->handle, out);
        if (result < 0)
        {
            ++s->errors;
            if (++failures >= 40)
            {
                trace("pad speaker: port %u stopped answering result=%08x", port + 1,
                      static_cast<unsigned>(result));
                break;
            }
        }
        else
        {
            failures = 0;
            ++s->outputs;
        }
    }
    return nullptr;
}

bool open_speaker(unsigned port)
{
    if (port >= ports)
        return false;
    std::lock_guard<std::mutex> guard(life);
    Speaker &s = speakers[port];
    if (s.running)
        return true;
    const int user = ps5_input_user_id(port);
    if (user < 0)
    {
        trace("pad speaker: port %u has no signed-in controller (%d)", port + 1, user);
        return false;
    }
    (void)sceAudioOutInit(); /* already initialised by the main output: harmless */
    const std::int32_t handle =
        sceAudioOutOpen(user, pad_speaker_type, 0, grain, PS5_PAD_SPEAKER_RATE, mono_s16);
    if (handle < 0)
    {
        trace("pad speaker: port %u open failed user=%d result=%08x", port + 1, user,
              static_cast<unsigned>(handle));
        return false;
    }
    {
        std::lock_guard<std::mutex> q(s.lock);
        s.handle = handle;
        s.head = s.count = 0;
        s.outputs = s.short_grains = s.errors = 0;
        s.dropped = 0;
    }
    s.stop.store(false);
    const int thread = create_title_thread(&s.thread, feeder, &s);
    s.running = thread == 0;
    if (!s.running)
    {
        trace("pad speaker: port %u thread failed result=%d", port + 1, thread);
        std::lock_guard<std::mutex> q(s.lock);
        sceAudioOutClose(s.handle);
        s.handle = -1;
        return false;
    }
    trace("pad speaker: port %u opened user=%d handle=%d", port + 1, user, handle);
    return true;
}

std::size_t write_speaker(unsigned port, const std::int16_t *samples, std::size_t count)
{
    if (port >= ports || !samples)
        return 0;
    Speaker &s = speakers[port];
    std::lock_guard<std::mutex> guard(s.lock);
    if (s.handle < 0)
        return 0;
    const std::size_t take = std::min(count, queue_size - s.count);
    s.dropped += count - take;
    for (std::size_t i = 0; i < take; ++i)
        s.queue[(s.head + s.count + i) % queue_size] = samples[i];
    s.count += take;
    return take;
}

void close_speaker(unsigned port)
{
    if (port >= ports)
        return;
    std::lock_guard<std::mutex> guard(life);
    Speaker &s = speakers[port];
    if (!s.running)
        return;
    s.stop.store(true);
    pthread_join(s.thread, nullptr);
    s.running = false;
    std::int32_t handle;
    {
        std::lock_guard<std::mutex> q(s.lock);
        handle = s.handle;
        s.handle = -1;
        s.count = 0;
    }
    sceAudioOutClose(handle);
    trace("pad speaker: port %u closed outputs=%u short=%u errors=%u dropped=%zu", port + 1,
          s.outputs, s.short_grains, s.errors, s.dropped);
}

const ps5_pad_speaker table = {PS5_PAD_SPEAKER_VERSION, open_speaker, write_speaker, close_speaker};
} // namespace

extern "C" const ps5_pad_speaker *ps5_pad_speaker_interface(void)
{
    return &table;
}

extern "C" void ps5_pad_speaker_close_all(void)
{
    for (unsigned port = 0; port < ports; ++port)
        close_speaker(port);
}
