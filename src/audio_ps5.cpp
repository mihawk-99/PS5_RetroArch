/* PS5 RetroArch - native PCM audio backend.
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AudioOut ABI/constants and call ordering follow ProsperoLight's
 * moonlight_stream.cpp (Copyright 2026 BlackBearReloaded, GPL-3.0-or-later).
 * RetroArch supplies PCM; no SDL, network decoder or Opus dependency is used.
 */
#include <audio/audio_driver.h>
#include <pthread.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <ctime>

extern "C"
{
    int32_t sceAudioOutInit();
    int32_t sceAudioOutOpen(int32_t, int32_t, int32_t, uint32_t, uint32_t, uint32_t);
    int32_t sceAudioOutOutput(int32_t, const void *);
    int32_t sceAudioOutClose(int32_t);
    const char *ps5_frontend_build_identity();
}

namespace
{
constexpr unsigned rate = 48000;
constexpr size_t grain = 256;
constexpr size_t frame_bytes = 2 * sizeof(int16_t);
constexpr uint32_t already_initialized = 0x8026000e;

struct Audio
{
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    pthread_t thread;
    int port = -1;
    int16_t *ring = nullptr;
    size_t capacity = 0, head = 0, count = 0, peak = 0;
    bool active = true, shutdown = false, failed = false, nonblock = false, in_output = false;
    uint64_t accepted = 0, played = 0, discarded = 0, silence = 0, calls = 0, errors = 0;
    alignas(16) int16_t output[grain * 2] = {};
};

void *worker(void *opaque)
{
    auto *a = static_cast<Audio *>(opaque);
    pthread_mutex_lock(&a->mutex);
    while (!a->shutdown && !a->failed)
    {
        while (!a->active && !a->shutdown)
            pthread_cond_wait(&a->changed, &a->mutex);
        if (a->shutdown)
            break;
        const size_t frames = std::min(a->count, grain);
        const size_t first = std::min(frames, a->capacity - a->head);
        std::memcpy(a->output, a->ring + a->head * 2, first * frame_bytes);
        std::memcpy(a->output + first * 2, a->ring, (frames - first) * frame_bytes);
        std::memset(a->output + frames * 2, 0, (grain - frames) * frame_bytes);
        a->head = (a->head + frames) % a->capacity;
        a->count -= frames;
        a->in_output = true;
        pthread_cond_broadcast(&a->changed);
        pthread_mutex_unlock(&a->mutex);
        /* AudioOut paces this worker. Never hold the producer mutex during output. */
        const int result = sceAudioOutOutput(a->port, a->output);
        pthread_mutex_lock(&a->mutex);
        a->in_output = false;
        ++a->calls;
        if (result < 0)
        {
            ++a->errors;
            a->discarded += frames;
            a->failed = true;
            std::fprintf(stderr, "audio ps5: output failed result=%08x\n", unsigned(result));
        }
        else
        {
            a->played += frames;
            a->silence += grain - frames;
        }
        pthread_cond_broadcast(&a->changed);
    }
    pthread_mutex_unlock(&a->mutex);
    return nullptr;
}

void *audio_init(const char *device, unsigned requested_rate, unsigned latency, unsigned,
                 unsigned *new_rate)
{
    if (device && *device && std::strcmp(device, "default") != 0)
    {
        std::fprintf(stderr, "audio ps5: only the default main output is supported\n");
        return nullptr;
    }
    const int result = sceAudioOutInit();
    if (result != 0 && uint32_t(result) != already_initialized)
    {
        std::fprintf(stderr, "audio ps5: init failed result=%08x\n", unsigned(result));
        return nullptr;
    }
    auto *a = new (std::nothrow) Audio;
    if (!a)
        return nullptr;
    const size_t milliseconds = std::min(latency ? latency : 64u, 170u);
    a->capacity = std::max(grain * 2, ((milliseconds * rate / 1000 + grain - 1) / grain) * grain);
    a->ring = static_cast<int16_t *>(std::calloc(a->capacity, frame_bytes));
    if (!a->ring)
    {
        delete a;
        return nullptr;
    }
    if (pthread_mutex_init(&a->mutex, nullptr) != 0)
    {
        std::free(a->ring);
        delete a;
        return nullptr;
    }
    if (pthread_cond_init(&a->changed, nullptr) != 0)
    {
        pthread_mutex_destroy(&a->mutex);
        std::free(a->ring);
        delete a;
        return nullptr;
    }
    a->port = sceAudioOutOpen(0xff, 0, 0, grain, rate, 1);
    const int thread_result = a->port > 0 ? pthread_create(&a->thread, nullptr, worker, a) : -1;
    if (thread_result != 0)
    {
        std::fprintf(stderr, "audio ps5: open/thread failed port=%08x thread=%d\n",
                     unsigned(a->port), thread_result);
        if (a->port > 0)
            sceAudioOutClose(a->port);
        pthread_cond_destroy(&a->changed);
        pthread_mutex_destroy(&a->mutex);
        std::free(a->ring);
        delete a;
        return nullptr;
    }
    if (new_rate)
        *new_rate = rate;
    std::fprintf(
        stderr,
        "audio ps5: ready requested_rate=%u rate=%u channels=2 s16 grain=%zu buffer_bytes=%zu\n",
        requested_rate, rate, grain, a->capacity * frame_bytes);
    return a;
}

ssize_t audio_write(void *opaque, const void *data, size_t bytes)
{
    auto *a = static_cast<Audio *>(opaque);
    if (!a || (bytes && !data) || bytes % frame_bytes)
        return -1;
    const auto *source = static_cast<const uint8_t *>(data);
    size_t written = 0;
    pthread_mutex_lock(&a->mutex);
    while (written < bytes)
    {
        if (a->failed || a->shutdown)
        {
            pthread_mutex_unlock(&a->mutex);
            return written ? ssize_t(written) : -1;
        }
        if (!a->active)
            break;
        const size_t free_frames = a->capacity - a->count;
        if (!free_frames)
        {
            if (a->nonblock)
                break;
            pthread_cond_wait(&a->changed, &a->mutex);
            continue;
        }
        const size_t frames = std::min((bytes - written) / frame_bytes, free_frames);
        const size_t tail = (a->head + a->count) % a->capacity;
        const size_t first = std::min(frames, a->capacity - tail);
        std::memcpy(a->ring + tail * 2, source + written, first * frame_bytes);
        std::memcpy(a->ring, source + written + first * frame_bytes,
                    (frames - first) * frame_bytes);
        written += frames * frame_bytes;
        a->count += frames;
        a->accepted += frames;
        a->peak = std::max(a->peak, a->count);
        pthread_cond_broadcast(&a->changed);
    }
    pthread_mutex_unlock(&a->mutex);
    /* The frontend call sites pass and consume BYTES, despite the header's frame wording. */
    return ssize_t(written);
}

bool audio_stop(void *opaque)
{
    auto *a = static_cast<Audio *>(opaque);
    if (!a)
        return false;
    pthread_mutex_lock(&a->mutex);
    a->active = false;
    a->discarded += a->count;
    a->count = 0;
    a->head = 0;
    pthread_cond_broadcast(&a->changed);
    while (a->in_output)
        pthread_cond_wait(&a->changed, &a->mutex);
    const bool healthy = !a->failed;
    pthread_mutex_unlock(&a->mutex);
    const int result = sceAudioOutOutput(a->port, nullptr);
    if (result < 0)
    {
        pthread_mutex_lock(&a->mutex);
        a->failed = true;
        ++a->errors;
        pthread_mutex_unlock(&a->mutex);
        std::fprintf(stderr, "audio ps5: pause drain failed result=%08x\n", unsigned(result));
    }
    return healthy && result >= 0;
}

bool audio_start(void *opaque, bool)
{
    auto *a = static_cast<Audio *>(opaque);
    if (!a)
        return false;
    pthread_mutex_lock(&a->mutex);
    const bool ok = !a->shutdown && !a->failed;
    if (ok)
        a->active = true;
    pthread_cond_broadcast(&a->changed);
    pthread_mutex_unlock(&a->mutex);
    return ok;
}

bool audio_alive(void *opaque)
{
    auto *a = static_cast<Audio *>(opaque);
    if (!a)
        return false;
    pthread_mutex_lock(&a->mutex);
    const bool alive = a->active && !a->failed && !a->shutdown;
    pthread_mutex_unlock(&a->mutex);
    return alive;
}

void nonblock(void *opaque, bool enabled)
{
    auto *a = static_cast<Audio *>(opaque);
    pthread_mutex_lock(&a->mutex);
    a->nonblock = enabled;
    pthread_cond_broadcast(&a->changed);
    pthread_mutex_unlock(&a->mutex);
}

size_t available(void *opaque)
{
    auto *a = static_cast<Audio *>(opaque);
    pthread_mutex_lock(&a->mutex);
    const size_t bytes = (a->failed || !a->active) ? 0 : (a->capacity - a->count) * frame_bytes;
    pthread_mutex_unlock(&a->mutex);
    return bytes;
}

size_t buffer_size(void *opaque)
{
    return static_cast<Audio *>(opaque)->capacity * frame_bytes;
}

bool destroy(Audio *a)
{
    if (!a)
        return true;
    pthread_mutex_lock(&a->mutex);
    a->shutdown = true;
    a->discarded += a->count;
    a->count = 0;
    pthread_cond_broadcast(&a->changed);
    pthread_mutex_unlock(&a->mutex);
    pthread_join(a->thread, nullptr);
    const int drain = sceAudioOutOutput(a->port, nullptr);
    const int close = sceAudioOutClose(a->port);
    std::fprintf(stderr,
                 "audio ps5: closed accepted=%llu played=%llu discarded=%llu silence=%llu "
                 "calls=%llu errors=%llu peak=%zu capacity=%zu drain=%08x close=%08x\n",
                 (unsigned long long)a->accepted, (unsigned long long)a->played,
                 (unsigned long long)a->discarded, (unsigned long long)a->silence,
                 (unsigned long long)a->calls, (unsigned long long)a->errors, a->peak, a->capacity,
                 unsigned(drain), unsigned(close));
    pthread_cond_destroy(&a->changed);
    pthread_mutex_destroy(&a->mutex);
    std::free(a->ring);
    delete a;
    return drain >= 0 && close >= 0;
}

void audio_free(void *opaque)
{
    destroy(static_cast<Audio *>(opaque));
}

bool use_float(void *)
{
    return false;
}
} // namespace

extern "C"
{
    audio_driver_t audio_ps5 = {audio_init, audio_write, audio_stop, audio_start, audio_alive,
                                nonblock,   audio_free,  use_float,  "ps5",       nullptr,
                                nullptr,    available,   buffer_size};
}

/* Opt-in bring-up test; normal launches never generate audio themselves. */
extern "C" void ps5_audio_test_if_requested()
{
    FILE *control = std::fopen("/app0/audio-test.txt", "rb");
    if (!control)
        return;
    std::fclose(control);
    std::remove("/app0/audio-test.txt");
    std::fprintf(stderr, "audio ps5 test: left 440 Hz, then right 660 Hz, repeated; 12.5%% peak\n");
    unsigned actual_rate = 0;
    auto *a = static_cast<Audio *>(audio_ps5.init(nullptr, 44100, 32, 0, &actual_rate));
    bool ok = a && actual_rate == rate;
    uint64_t accepted = 0, played = 0, errors = 0;
    size_t high_water = 0, capacity = 0;
    ssize_t nonblocking_bytes = -1;
    auto wait_empty = [](Audio *state)
    {
        timespec deadline{};
        deadline.tv_sec = std::time(nullptr) + 10;
        pthread_mutex_lock(&state->mutex);
        const uint64_t target = state->accepted;
        while (!state->failed && state->played < target)
        {
            if (pthread_cond_timedwait(&state->changed, &state->mutex, &deadline) != 0)
                break;
        }
        const bool drained = !state->failed && state->played == target;
        pthread_mutex_unlock(&state->mutex);
        return drained;
    };
    if (a)
    {
        const unsigned chunks[] = {1, 255, 257, 1000};
        int16_t samples[2000];
        for (unsigned phase = 0; ok && phase < 4; ++phase)
        {
            unsigned offset = 0, chunk = 0;
            while (ok && offset < rate)
            {
                const unsigned frames = std::min(chunks[chunk++ % 4], rate - offset);
                for (unsigned i = 0; i < frames; ++i)
                {
                    // Short ramps avoid clicks at the tone boundaries.
                    const unsigned at = offset + i;
                    const double envelope = std::min(1.0, std::min(at, rate - 1 - at) / 480.0);
                    const int16_t value = int16_t(
                        4096 * envelope *
                        std::sin(6.283185307179586 * (phase % 2 ? 660.0 : 440.0) * at / rate));
                    samples[i * 2] = phase % 2 ? 0 : value;
                    samples[i * 2 + 1] = phase % 2 ? value : 0;
                }
                ok = audio_ps5.write(a, samples, frames * frame_bytes) ==
                     ssize_t(frames * frame_bytes);
                offset += frames;
            }
            ok = ok && wait_empty(a);
            ok = audio_ps5.stop(a) && ok;
            ok = !audio_ps5.alive(a) && ok;
            ok = audio_ps5.start(a, false) && ok;
        }
        // A full nonblocking write accepts only the queue's capacity, without waiting.
        const size_t size = audio_ps5.buffer_size(a);
        auto *quiet = static_cast<uint8_t *>(std::calloc(size * 2, 1));
        if (ok && quiet)
        {
            audio_ps5.set_nonblock_state(a, true);
            nonblocking_bytes = audio_ps5.write(a, quiet, size * 2);
            ok = nonblocking_bytes == ssize_t(size) && wait_empty(a);
            audio_ps5.set_nonblock_state(a, false);
        }
        else
            ok = false;
        std::free(quiet);
        ok = audio_ps5.stop(a) && ok;
        pthread_mutex_lock(&a->mutex);
        accepted = a->accepted;
        played = a->played;
        errors = a->errors;
        high_water = a->peak;
        capacity = a->capacity;
        ok = ok && accepted == played && a->discarded == 0 && errors == 0 && high_water <= capacity;
        pthread_mutex_unlock(&a->mutex);
        ok = destroy(a) && ok;
    }
    FILE *report = std::fopen("/app0/audio-test.json", "wb");
    if (report)
    {
        std::fprintf(report,
                     "{\"build_identity\":\"%s\",\"passed\":%s,\"rate\":%u,"
                     "\"grain_frames\":256,\"frame_bytes\":4,\"accepted_frames\":%llu,"
                     "\"played_frames\":%llu,\"errors\":%llu,\"peak_frames\":%zu,"
                     "\"capacity_frames\":%zu,\"nonblocking_bytes\":%lld}\n",
                     ps5_frontend_build_identity(), ok ? "true" : "false", actual_rate,
                     (unsigned long long)accepted, (unsigned long long)played,
                     (unsigned long long)errors, high_water, capacity,
                     (long long)nonblocking_bytes);
        std::fclose(report);
    }
    std::fprintf(stderr, "audio ps5 test: %s accepted=%llu played=%llu errors=%llu\n",
                 ok ? "PASS" : "FAIL", (unsigned long long)accepted, (unsigned long long)played,
                 (unsigned long long)errors);
}
