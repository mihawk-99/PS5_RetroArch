/*
 * PS5 RetroArch - the stack the title's own threads run on.
 *
 * A thread created with no attributes gets 64 KiB on this console (the
 * payload SDK fork's thread probe, platform/docs/PROBE.md), a sixteenth of a
 * desktop's. The title's own threads -- the log flusher, the audio worker, the
 * permission repair, the sampler and the core-thread factory -- run little of
 * their own code, but libc and the system libraries they call run on the same
 * stack, and RetroArch's video thread faulted in a 66 KB frame on that
 * default. So each asks for its stack. RetroArch's threads get 2 MiB (patch
 * 0096), and the cores' 2 MiB (core_threads_ps5.cpp).
 */
#pragma once

#include <pthread.h>

#include <cstddef>

constexpr std::size_t kTitleThreadStack = 256u * 1024u;

/* pthread_create with the title's stack size; falls back to the default when
 * the attribute object cannot be made. */
inline int create_title_thread(pthread_t *thread, void *(*start)(void *), void *argument)
{
    pthread_attr_t attributes;
    if (pthread_attr_init(&attributes) != 0)
        return pthread_create(thread, nullptr, start, argument);
    pthread_attr_setstacksize(&attributes, kTitleThreadStack);
    const int result = pthread_create(thread, &attributes, start, argument);
    pthread_attr_destroy(&attributes);
    return result;
}
