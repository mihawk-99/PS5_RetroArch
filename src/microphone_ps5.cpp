/* PS5 RetroArch - user-routed native microphone input.
 * SPDX-License-Identifier: GPL-3.0-or-later
 * AudioIn ABI follows the public RafaelNGP/xbox-cloud-gaming-ps5 driver;
 * native capture and hardware mute were verified on firmware 13.60.
 * PCM lives only in the bounded queue and the frontend's sample buffers.
 */
#include <audio/microphone_driver.h>
#include <algorithm>
#include <atomic>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <pthread.h>
#include "title_threads.hpp"

extern "C"
{
    int sceAudioInInit(void);
    int sceAudioInOpen(int, int, int, unsigned, unsigned, unsigned);
    int sceAudioInInput(int, void *);
    int sceAudioInClose(int);
    int sceAudioInGetSilentState(int);
    int ps5_input_user_id(unsigned port);
}

namespace
{
constexpr size_t grain = 256;
constexpr size_t capacity = 4096;
struct Mic
{
    pthread_mutex_t lock;
    pthread_cond_t changed;
    pthread_t thread;
    int port = -1;
    unsigned rate = 48000;
    bool active = false, shutdown = false, failed = false, in_input = false;
    size_t head = 0, count = 0;
    uint64_t received = 0, delivered = 0, dropped = 0;
    int silent_state = -1;
    int16_t queue[capacity] = {};
};
struct Driver
{
    std::atomic_bool nonblock{false};
    Mic *mic = nullptr;
};

void *capture(void *opaque)
{
    auto *m = static_cast<Mic *>(opaque);
    int16_t pcm[grain] = {};
    pthread_mutex_lock(&m->lock);
    while (!m->shutdown && !m->failed)
    {
        while (!m->active && !m->shutdown)
            pthread_cond_wait(&m->changed, &m->lock);
        if (m->shutdown)
            break;
        m->in_input = true;
        pthread_mutex_unlock(&m->lock);
        const int result = sceAudioInInput(m->port, pcm);
        const int silent = result >= 0 ? sceAudioInGetSilentState(m->port) : -1;
        pthread_mutex_lock(&m->lock);
        m->in_input = false;
        if (result < 0 || silent < 0)
        {
            m->failed = true;
            std::fprintf(stderr, "microphone ps5: input failed result=%08x state=%08x\n",
                         unsigned(result), unsigned(silent));
        }
        else if (m->active && !m->shutdown)
        {
            /* Device absent (1) and user mute (4) must never leak queued speech. */
            if ((silent & 5) != 0)
            {
                std::memset(pcm, 0, sizeof(pcm));
                if (silent != m->silent_state)
                {
                    std::memset(m->queue, 0, sizeof(m->queue));
                    m->head = m->count = 0;
                }
            }
            if (silent != m->silent_state)
            {
                std::fprintf(stderr, "microphone ps5: silence state=%08x\n", unsigned(silent));
                m->silent_state = silent;
            }
            const size_t discard = grain > capacity - m->count ? grain - (capacity - m->count) : 0;
            m->head = (m->head + discard) % capacity;
            m->count -= discard;
            m->dropped += discard;
            const size_t tail = (m->head + m->count) % capacity;
            const size_t first = std::min(grain, capacity - tail);
            std::memcpy(m->queue + tail, pcm, first * sizeof(int16_t));
            std::memcpy(m->queue, pcm + first, (grain - first) * sizeof(int16_t));
            m->count += grain;
            m->received += grain;
        }
        std::memset(pcm, 0, sizeof(pcm));
        pthread_cond_broadcast(&m->changed);
    }
    pthread_cond_broadcast(&m->changed);
    pthread_mutex_unlock(&m->lock);
    return nullptr;
}

void *initialize()
{
    /* Initializing the service opens no port and captures no microphone. */
    const int result = sceAudioInInit();
    if (result < 0 && uint32_t(result) != 0x8026000e && uint32_t(result) != 0x8026000c)
    {
        std::fprintf(stderr, "microphone ps5: init failed result=%08x\n", unsigned(result));
        return nullptr;
    }
    return new (std::nothrow) Driver;
}

void close_mic(void *driver, void *opaque)
{
    if (!driver || !opaque)
        return;
    auto *d = static_cast<Driver *>(driver);
    auto *m = static_cast<Mic *>(opaque);
    pthread_mutex_lock(&m->lock);
    m->shutdown = true;
    m->active = false;
    pthread_cond_broadcast(&m->changed);
    pthread_mutex_unlock(&m->lock);
    pthread_join(m->thread, nullptr);
    const int result = sceAudioInClose(m->port);
    std::fprintf(stderr,
                 "microphone ps5: closed result=%08x received=%llu delivered=%llu dropped=%llu\n",
                 unsigned(result), (unsigned long long)m->received,
                 (unsigned long long)m->delivered, (unsigned long long)m->dropped);
    std::memset(m->queue, 0, sizeof(m->queue));
    pthread_cond_destroy(&m->changed);
    pthread_mutex_destroy(&m->lock);
    d->mic = nullptr;
    delete m;
}

void release(void *opaque)
{
    auto *d = static_cast<Driver *>(opaque);
    if (!d)
        return;
    if (d->mic)
        close_mic(d, d->mic);
    delete d;
}

void *open_mic(void *driver, const char *device, unsigned requested_rate, unsigned latency,
               unsigned *new_rate)
{
    auto *d = static_cast<Driver *>(driver);
    if (!d || d->mic || (device && *device && std::strcmp(device, "default") != 0))
        return nullptr;
    const int user = ps5_input_user_id(0);
    if (user < 0)
        return nullptr;
    auto *m = new (std::nothrow) Mic;
    if (!m)
        return nullptr;
    if (pthread_mutex_init(&m->lock, nullptr) != 0)
    {
        delete m;
        return nullptr;
    }
    if (pthread_cond_init(&m->changed, nullptr) != 0)
    {
        pthread_mutex_destroy(&m->lock);
        delete m;
        return nullptr;
    }
    m->port = sceAudioInOpen(user, 0, 0, grain, m->rate, 0);
    if (m->port < 0)
        m->port = sceAudioInOpen(user, 1, 0, grain, m->rate, 0);
    if (m->port < 0)
    {
        m->rate = 16000;
        m->port = sceAudioInOpen(user, 0, 0, grain, m->rate, 0);
        if (m->port < 0)
            m->port = sceAudioInOpen(user, 1, 0, grain, m->rate, 0);
    }
    const int thread = m->port >= 0 ? create_title_thread(&m->thread, capture, m) : -1;
    if (thread != 0)
    {
        if (m->port >= 0)
            sceAudioInClose(m->port);
        pthread_cond_destroy(&m->changed);
        pthread_mutex_destroy(&m->lock);
        delete m;
        return nullptr;
    }
    if (new_rate)
        *new_rate = m->rate;
    d->mic = m;
    std::fprintf(
        stderr,
        "microphone ps5: opened user=%08x requested=%u rate=%u latency=%u s16 mono grain=%zu\n",
        unsigned(user), requested_rate, m->rate, latency, grain);
    return m;
}

bool alive(const void *, const void *opaque)
{
    if (!opaque)
        return false;
    auto *m = const_cast<Mic *>(static_cast<const Mic *>(opaque));
    pthread_mutex_lock(&m->lock);
    const bool result = m->active && !m->failed && !m->shutdown;
    pthread_mutex_unlock(&m->lock);
    return result;
}

bool set_active(void *opaque, bool active)
{
    if (!opaque)
        return false;
    auto *m = static_cast<Mic *>(opaque);
    pthread_mutex_lock(&m->lock);
    if (m->failed || m->shutdown)
    {
        pthread_mutex_unlock(&m->lock);
        return false;
    }
    if (m->active == active)
    {
        pthread_mutex_unlock(&m->lock);
        return true;
    }
    m->active = active;
    m->head = m->count = 0;
    std::memset(m->queue, 0, sizeof(m->queue));
    pthread_cond_broadcast(&m->changed);
    while (!active && m->in_input)
        pthread_cond_wait(&m->changed, &m->lock);
    pthread_mutex_unlock(&m->lock);
    return true;
}
bool start(void *, void *mic)
{
    return set_active(mic, true);
}
bool stop(void *, void *mic)
{
    return set_active(mic, false);
}
void set_nonblock(void *opaque, bool enabled)
{
    if (opaque)
        static_cast<Driver *>(opaque)->nonblock.store(enabled);
}

int read(void *driver, void *opaque, void *buffer, size_t bytes)
{
    if (!driver || !opaque || (!buffer && bytes) || bytes % sizeof(int16_t) || bytes > INT_MAX)
        return -1;
    if (!bytes)
        return 0;
    auto *d = static_cast<Driver *>(driver);
    auto *m = static_cast<Mic *>(opaque);
    auto *out = static_cast<int16_t *>(buffer);
    const size_t wanted = bytes / sizeof(int16_t);
    size_t copied = 0;
    pthread_mutex_lock(&m->lock);
    while (copied < wanted)
    {
        if (m->failed || m->shutdown || !m->active)
        {
            pthread_mutex_unlock(&m->lock);
            return copied ? int(copied * sizeof(int16_t)) : -1;
        }
        if (!m->count)
        {
            if (d->nonblock.load())
                break;
            pthread_cond_wait(&m->changed, &m->lock);
            continue;
        }
        const size_t count = std::min(wanted - copied, m->count);
        const size_t first = std::min(count, capacity - m->head);
        std::memcpy(out + copied, m->queue + m->head, first * sizeof(int16_t));
        std::memcpy(out + copied + first, m->queue, (count - first) * sizeof(int16_t));
        m->head = (m->head + count) % capacity;
        m->count -= count;
        copied += count;
        m->delivered += count;
    }
    pthread_mutex_unlock(&m->lock);
    return int(copied * sizeof(int16_t));
}
} // namespace

extern "C"
{
    microphone_driver_t microphone_ps5 = {initialize, release, read,     set_nonblock, "ps5",
                                          nullptr,    nullptr, open_mic, close_mic,    alive,
                                          start,      stop,    nullptr};
}
