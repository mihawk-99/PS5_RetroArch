/* PS5 RetroArch - observe the Vulkan API results the frontend discards.
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "gfx/include/vulkan/vulkan.h"

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <ctime>

namespace
{
PFN_vkEndCommandBuffer end_command_buffer;
PFN_vkQueueSubmit queue_submit;
PFN_vkQueuePresentKHR queue_present;

/* Single-threaded title video loop. All durations are monotonic wall time,
 * not GPU timestamps or CPU utilisation. No per-frame I/O or allocation. */
#ifndef PS5_VULKAN_PROFILE_NOW
uint64_t profile_now()
{
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return uint64_t(now.tv_sec) * 1000000000ULL + uint64_t(now.tv_nsec);
}
#define PS5_VULKAN_PROFILE_NOW profile_now
#endif

enum Metric
{
    Interval,
    Outside,
    Texture,
    Prepare,
    End,
    Submit,
    Present,
    Wait,
    MetricCount
};
struct Sample
{
    uint64_t sum = 0, maximum = 0;
    void add(uint64_t value)
    {
        sum += value;
        if (value > maximum)
            maximum = value;
    }
};
struct Profile
{
    bool active = false;
    unsigned warmup = 0, frames = 0;
    uint64_t start = 0, previous_end = 0, texture_pending = 0;
    uint64_t api[MetricCount]{};
    Sample samples[MetricCount]{};

    void begin(uint64_t now)
    {
        // An early return has no matching end. Do not charge its gap to the next frame.
        if (active)
            previous_end = 0;
        active = true;
        start = now;
        std::memset(api, 0, sizeof(api));
    }
    void finish(uint64_t now)
    {
        if (!active)
            return;
        active = false;
        if (warmup < 4 || !previous_end)
        {
            ++warmup;
            previous_end = now;
            texture_pending = 0;
            return;
        }
        uint64_t measured = 0;
        for (unsigned i = End; i < MetricCount; ++i)
        {
            samples[i].add(api[i]);
            measured += api[i];
        }
        samples[Interval].add(now - previous_end);
        const uint64_t outside = start - previous_end;
        samples[Texture].add(texture_pending);
        samples[Outside].add(outside >= texture_pending ? outside - texture_pending : 0);
        texture_pending = 0;
        const uint64_t video = now - start;
        samples[Prepare].add(video >= measured ? video - measured : 0);
        previous_end = now;
        ++frames;
        if (samples[Interval].sum < 5000000000ULL)
            return;
        // One write per window; its cost belongs to the next Outside sample.
        std::fprintf(stderr,
                     "gpu timing: frames=%u seconds=%.3f fps=%.3f ms_avg/max "
                     "interval=%.3f/%.3f outside=%.3f/%.3f texture=%.3f/%.3f prepare=%.3f/%.3f "
                     "end=%.3f/%.3f submit=%.3f/%.3f present=%.3f/%.3f wait=%.3f/%.3f\n",
                     frames, samples[Interval].sum / 1e9, frames * 1e9 / samples[Interval].sum,
                     samples[Interval].sum / (1e6 * frames), samples[Interval].maximum / 1e6,
                     samples[Outside].sum / (1e6 * frames), samples[Outside].maximum / 1e6,
                     samples[Texture].sum / (1e6 * frames), samples[Texture].maximum / 1e6,
                     samples[Prepare].sum / (1e6 * frames), samples[Prepare].maximum / 1e6,
                     samples[End].sum / (1e6 * frames), samples[End].maximum / 1e6,
                     samples[Submit].sum / (1e6 * frames), samples[Submit].maximum / 1e6,
                     samples[Present].sum / (1e6 * frames), samples[Present].maximum / 1e6,
                     samples[Wait].sum / (1e6 * frames), samples[Wait].maximum / 1e6);
        frames = 0;
        for (auto &sample : samples)
            sample = {};
    }
};
Profile profile;

struct ApiTimer
{
    Metric metric;
    bool enabled;
    uint64_t start;
    explicit ApiTimer(Metric kind)
        : metric(kind), enabled(profile.active), start(enabled ? PS5_VULKAN_PROFILE_NOW() : 0)
    {
    }
    void finish()
    {
        if (enabled)
            profile.api[metric] += PS5_VULKAN_PROFILE_NOW() - start;
    }
};

struct Results
{
    unsigned calls = 0;
    unsigned failures = 0;

    void record(const char *name, VkResult result)
    {
        ++calls;
        if (result != VK_SUCCESS)
            ++failures;
        if (calls <= 4 || calls % 600 == 0 || result != VK_SUCCESS)
            std::fprintf(stderr, "gpu result: %s calls=%u failures=%u result=%d\n", name, calls,
                         failures, static_cast<int>(result));
    }
};

VKAPI_ATTR VkResult VKAPI_CALL traced_end(VkCommandBuffer command)
{
    static Results results;
    ApiTimer timer(End);
    const VkResult result = end_command_buffer(command);
    timer.finish();
    results.record("vkEndCommandBuffer", result);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL traced_submit(VkQueue queue, uint32_t count,
                                             const VkSubmitInfo *submits, VkFence fence)
{
    static Results results;
    ApiTimer timer(Submit);
    const VkResult result = queue_submit(queue, count, submits, fence);
    timer.finish();
    results.record("vkQueueSubmit", result);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL traced_present(VkQueue queue, const VkPresentInfoKHR *present)
{
    static Results results;
    static Results images;
    ApiTimer timer(Present);
    const VkResult result = queue_present(queue, present);
    timer.finish();
    results.record("vkQueuePresentKHR", result);
    if (present->pResults)
        for (uint32_t i = 0; i < present->swapchainCount; ++i)
            images.record("swapchain image", present->pResults[i]);
    return result;
}
PFN_vkWaitForFences wait_for_fences;
PFN_vkAcquireNextImageKHR acquire_next_image;
VKAPI_ATTR VkResult VKAPI_CALL traced_wait(VkDevice device, uint32_t count, const VkFence *fences,
                                           VkBool32 all, uint64_t timeout)
{
    ApiTimer timer(Wait);
    const VkResult result = wait_for_fences(device, count, fences, all, timeout);
    timer.finish();
    return result;
}
VKAPI_ATTR VkResult VKAPI_CALL traced_acquire(VkDevice device, VkSwapchainKHR swapchain,
                                              uint64_t timeout, VkSemaphore semaphore,
                                              VkFence fence, uint32_t *index)
{
    ApiTimer timer(Wait);
    const VkResult result = acquire_next_image(device, swapchain, timeout, semaphore, fence, index);
    timer.finish();
    return result;
}
} // namespace

extern "C" uint64_t ps5_vulkan_profile_texture_begin()
{
    return PS5_VULKAN_PROFILE_NOW();
}
extern "C" void ps5_vulkan_profile_texture_end(uint64_t start)
{
    profile.texture_pending += PS5_VULKAN_PROFILE_NOW() - start;
}

extern "C" void ps5_vulkan_profile_begin()
{
    profile.begin(PS5_VULKAN_PROFILE_NOW());
}
extern "C" void ps5_vulkan_profile_end()
{
    profile.finish(PS5_VULKAN_PROFILE_NOW());
}

/* Called by both frontend symbol loaders. Keep the driver's real function,
 * preserve NULL lookups and return every result unchanged. Reloading a device
 * must neither reset the run totals nor wrap our own wrapper recursively. */
extern "C" void ps5_vulkan_trace_symbol(const char *name, PFN_vkVoidFunction *symbol)
{
    if (!*symbol)
        return;
    if (std::strcmp(name, "vkEndCommandBuffer") == 0)
    {
        if (*symbol != reinterpret_cast<PFN_vkVoidFunction>(traced_end))
            end_command_buffer = reinterpret_cast<PFN_vkEndCommandBuffer>(*symbol);
        *symbol = reinterpret_cast<PFN_vkVoidFunction>(traced_end);
    }
    else if (std::strcmp(name, "vkQueueSubmit") == 0)
    {
        if (*symbol != reinterpret_cast<PFN_vkVoidFunction>(traced_submit))
            queue_submit = reinterpret_cast<PFN_vkQueueSubmit>(*symbol);
        *symbol = reinterpret_cast<PFN_vkVoidFunction>(traced_submit);
    }
    else if (std::strcmp(name, "vkQueuePresentKHR") == 0)
    {
        if (*symbol != reinterpret_cast<PFN_vkVoidFunction>(traced_present))
            queue_present = reinterpret_cast<PFN_vkQueuePresentKHR>(*symbol);
        *symbol = reinterpret_cast<PFN_vkVoidFunction>(traced_present);
    }
    else if (std::strcmp(name, "vkWaitForFences") == 0)
    {
        if (*symbol != reinterpret_cast<PFN_vkVoidFunction>(traced_wait))
            wait_for_fences = reinterpret_cast<PFN_vkWaitForFences>(*symbol);
        *symbol = reinterpret_cast<PFN_vkVoidFunction>(traced_wait);
    }
    else if (std::strcmp(name, "vkAcquireNextImageKHR") == 0)
    {
        if (*symbol != reinterpret_cast<PFN_vkVoidFunction>(traced_acquire))
            acquire_next_image = reinterpret_cast<PFN_vkAcquireNextImageKHR>(*symbol);
        *symbol = reinterpret_cast<PFN_vkVoidFunction>(traced_acquire);
    }
}
