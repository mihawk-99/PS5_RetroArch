/*
 * PS5 RetroArch - create a core's threads from a stack without core frames.
 *
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The console's libkernel attributes a new thread to the module that asked for
 * it: pthread_create walks the caller's frame-pointer chain, takes a return
 * address two frames up, looks it up in the table of loaded modules and reads
 * the record without checking for a miss (docs/PHASE_LOG.md, the PPSSPP thread
 * investigation: pthread_get_specificarray_np+0x3b from pthread_create_name_np,
 * fault address 0x70). This title loads cores itself, into anonymous memory that
 * is not a registered module, so a core that creates a thread directly - PPSSPP's
 * thread pool does, through std::thread - hands libkernel an address it cannot
 * resolve and the title dies. Every thread creation the title itself makes
 * succeeds, including ones whose entry point is core code.
 *
 * So a core's pthread_create is bound here (tools/core-imports.py) and forwarded
 * to one factory thread the title starts before any core loads: the factory calls
 * the real pthread_create from its own stack, which holds title frames only, and
 * hands back the result. Attributes and the start routine pass through unchanged;
 * the caller waits, so the pointers it passes stay valid until the factory is done.
 */

#include <condition_variable>
#include <cstdio>
#include <mutex>

#include <pthread.h>

namespace
{
/* A core thread's smallest stack. The console's default is far smaller than a
 * desktop's, and PPSSPP's shader workers run glslang's parser, whose recursion
 * overflowed it (yyparse faulting below its stack). Stacks come out of the
 * title's small flexible-memory budget, and PPSSPP starts about 32 threads: 8
 * MiB each left too little for its 144 MB guest memory, so the minimum is 2 MiB.
 * Asked-for sizes above it are kept. */
constexpr size_t core_minimum_stack = 2u * 1024u * 1024u;

/* The attributes a core's thread is created with: the core's own, with the stack
 * raised to the minimum. */
int create_core_thread(pthread_t *thread, const pthread_attr_t *attributes, void *(*start)(void *),
                       void *argument)
{
    pthread_attr_t own;
    if (attributes != nullptr)
    {
        own = *attributes;
    }
    else if (pthread_attr_init(&own) != 0)
    {
        return pthread_create(thread, nullptr, start, argument);
    }
    size_t stack = 0;
    if (pthread_attr_getstacksize(&own, &stack) != 0 || stack < core_minimum_stack)
        pthread_attr_setstacksize(&own, core_minimum_stack);
    const int result = pthread_create(thread, &own, start, argument);
    if (attributes == nullptr)
        pthread_attr_destroy(&own);
    return result;
}

struct Request
{
    pthread_t *thread = nullptr;
    const pthread_attr_t *attributes = nullptr;
    void *(*start)(void *) = nullptr;
    void *argument = nullptr;
    int result = 0;
    bool pending = false;
    bool done = false;
};

std::mutex callers; // one request at a time
std::mutex lock;
std::condition_variable wake;
Request request;
bool factory_running = false;

void *factory(void *)
{
    std::unique_lock<std::mutex> guard(lock);
    for (;;)
    {
        wake.wait(guard, [] { return request.pending; });
        request.pending = false;
        request.result =
            create_core_thread(request.thread, request.attributes, request.start, request.argument);
        request.done = true;
        wake.notify_all();
    }
    return nullptr;
}
} // namespace

extern "C" void ps5_core_threads_start()
{
    std::lock_guard<std::mutex> guard(lock);
    if (factory_running)
        return;
    pthread_t thread;
    if (pthread_create(&thread, nullptr, factory, nullptr) == 0)
    {
        pthread_detach(thread);
        factory_running = true;
    }
    else
    {
        std::fputs("core threads: the factory thread could not start; cores create their own\n",
                   stderr);
    }
}

/* The pthread_create a core is bound to. */
extern "C" int ps5_core_pthread_create(pthread_t *thread, const pthread_attr_t *attributes,
                                       void *(*start)(void *), void *argument)
{
    std::lock_guard<std::mutex> one_at_a_time(callers);
    std::unique_lock<std::mutex> guard(lock);
    if (!factory_running)
    {
        guard.unlock();
        return create_core_thread(thread, attributes, start, argument);
    }
    request.thread = thread;
    request.attributes = attributes;
    request.start = start;
    request.argument = argument;
    request.done = false;
    request.pending = true;
    wake.notify_all();
    wake.wait(guard, [] { return request.done; });
    return request.result;
}

/* The same lookup runs in libkernel's thread-specific-data calls: a core's
 * std::thread start routine calls pthread_setspecific itself, and the console
 * faulted there, with the core's return address as the unresolvable one (klog of
 * the first v1.20.4 PPSSPP run: __thread_proxy+0x2a). These wrappers make a title
 * frame the caller libkernel inspects. They must stay real calls - a tail call
 * would leave the core's return address in place - hence disable_tail_calls. */
#define PS5_CORE_WRAPPER extern "C" __attribute__((noinline, disable_tail_calls))

PS5_CORE_WRAPPER int ps5_core_pthread_key_create(pthread_key_t *key, void (*destructor)(void *))
{
    return pthread_key_create(key, destructor);
}

PS5_CORE_WRAPPER int ps5_core_pthread_key_delete(pthread_key_t key)
{
    return pthread_key_delete(key);
}

PS5_CORE_WRAPPER void *ps5_core_pthread_getspecific(pthread_key_t key)
{
    return pthread_getspecific(key);
}

PS5_CORE_WRAPPER int ps5_core_pthread_setspecific(pthread_key_t key, const void *value)
{
    return pthread_setspecific(key, value);
}
