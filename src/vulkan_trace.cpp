/* PS5 RetroArch - observe the Vulkan API results the frontend discards.
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "gfx/include/vulkan/vulkan.h"

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <ctime>
#include "memory_diagnostics.hpp"

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
struct Frame
{
    uint64_t ns[MetricCount]{};
    uint64_t present_interval = 0;
    unsigned present_calls = 0;
};
struct Window
{
    unsigned frames = 0;
    Sample samples[MetricCount]{};
};
unsigned gpu_failures = 0;
struct Profile
{
    static constexpr unsigned capacity = 8192;
    bool enabled = false, active = false, finished = false;
    unsigned warmup = 0, warmup_limit = 120, frames = 0;
    unsigned record_count = 0, window_count = 0;
    uint64_t duration = 0, elapsed = 0;
    uint64_t start = 0, previous_end = 0, texture_pending = 0;
    uint64_t previous_present = 0, present_interval = 0;
    unsigned present_calls = 0;
    uint64_t api[MetricCount]{};
    Sample samples[MetricCount]{};
    Frame records[capacity]{};
    Window windows[13]{};

    void configure(unsigned seconds, unsigned skip = 120)
    {
        enabled = seconds >= 1 && seconds <= 60;
        duration = uint64_t(seconds) * 1000000000ULL;
        warmup_limit = skip;
    }
    void begin(uint64_t now)
    {
        if (!enabled || finished)
            return;
        if (active)
        {
            previous_end = 0;
            previous_present = 0;
        }
        active = true;
        start = now;
        present_calls = 0;
        present_interval = 0;
        std::memset(api, 0, sizeof(api));
    }
    void presented(uint64_t now)
    {
        if (!active)
            return;
        ++present_calls;
        if (previous_present)
            present_interval = now - previous_present;
        previous_present = now;
    }
    void save_window()
    {
        if (!frames)
            return;
        windows[window_count].frames = frames;
        std::memcpy(windows[window_count++].samples, samples, sizeof(samples));
        frames = 0;
        for (auto &sample : samples)
            sample = {};
    }
    void finish(uint64_t now)
    {
        if (!active)
            return;
        active = false;
        if (warmup < warmup_limit || !previous_end)
        {
            ++warmup;
            previous_end = now;
            texture_pending = 0;
            return;
        }
        Frame &frame = records[record_count++];
        uint64_t measured = 0;
        for (unsigned i = End; i < MetricCount; ++i)
        {
            frame.ns[i] = api[i];
            measured += api[i];
        }
        frame.ns[Interval] = now - previous_end;
        const uint64_t outside = start - previous_end;
        frame.ns[Texture] = texture_pending;
        frame.ns[Outside] = outside >= texture_pending ? outside - texture_pending : 0;
        texture_pending = 0;
        const uint64_t video = now - start;
        frame.ns[Prepare] = video >= measured ? video - measured : 0;
        frame.present_interval = present_interval;
        frame.present_calls = present_calls;
        previous_end = now;
        elapsed += frame.ns[Interval];
        ++frames;
        for (unsigned i = 0; i < MetricCount; ++i)
            samples[i].add(frame.ns[i]);
        if (samples[Interval].sum >= 5000000000ULL)
            save_window();
        if (elapsed >= duration || record_count == capacity)
        {
            save_window();
            finished = true;
        }
    }
    void dump(FILE *out) const
    {
        for (unsigned w = 0; w < window_count; ++w)
        {
            const auto &window = windows[w];
            const auto *s = window.samples;
            const double n = window.frames;
            std::fprintf(out,
                         "gpu timing: frames=%u seconds=%.3f fps=%.3f ms_avg/max "
                         "interval=%.3f/%.3f outside=%.3f/%.3f texture=%.3f/%.3f prepare=%.3f/%.3f "
                         "end=%.3f/%.3f submit=%.3f/%.3f present=%.3f/%.3f wait=%.3f/%.3f\n",
                         window.frames, s[Interval].sum / 1e9, n * 1e9 / s[Interval].sum,
                         s[Interval].sum / (1e6 * n), s[Interval].maximum / 1e6,
                         s[Outside].sum / (1e6 * n), s[Outside].maximum / 1e6,
                         s[Texture].sum / (1e6 * n), s[Texture].maximum / 1e6,
                         s[Prepare].sum / (1e6 * n), s[Prepare].maximum / 1e6,
                         s[End].sum / (1e6 * n), s[End].maximum / 1e6, s[Submit].sum / (1e6 * n),
                         s[Submit].maximum / 1e6, s[Present].sum / (1e6 * n),
                         s[Present].maximum / 1e6, s[Wait].sum / (1e6 * n), s[Wait].maximum / 1e6);
        }
        std::fprintf(out, "# frames=%u elapsed_ns=%llu capacity_reached=%u api_failures=%u\n",
                     record_count, static_cast<unsigned long long>(elapsed),
                     record_count == capacity ? 1u : 0u, gpu_failures);
        std::fputs("frame\tinterval_ns\toutside_ns\ttexture_ns\tprepare_ns\tend_ns\tsubmit_ns"
                   "\tpresent_ns\twait_ns\tpresent_interval_ns\tpresent_calls\n",
                   out);
        for (unsigned i = 0; i < record_count; ++i)
        {
            std::fprintf(out, "%u", i);
            for (auto value : records[i].ns)
                std::fprintf(out, "\t%llu", static_cast<unsigned long long>(value));
            std::fprintf(out, "\t%llu\t%u\n",
                         static_cast<unsigned long long>(records[i].present_interval),
                         records[i].present_calls);
        }
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
    uint64_t finish()
    {
        if (!enabled)
            return 0;
        const uint64_t now = PS5_VULKAN_PROFILE_NOW();
        profile.api[metric] += now - start;
        return now;
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
        {
            ++failures;
            ++gpu_failures;
        }
        if (calls <= 4 || result != VK_SUCCESS)
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
    ps5::memory::tick();
    ApiTimer timer(Present);
    const VkResult result = queue_present(queue, present);
    const uint64_t completed = timer.finish();
    if (result == VK_SUCCESS)
        profile.presented(completed);
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

extern "C" void ps5_vulkan_profile_init()
{
    FILE *config = std::fopen("/app0/gpu-profile.txt", "r");
    if (!config)
        return;
    unsigned seconds = 0;
    const int parsed = std::fscanf(config, "%u", &seconds);
    std::fclose(config);
    // Consume the opt-in so a later manual launch cannot inherit the experiment.
    std::remove("/app0/gpu-profile.txt");
    if (parsed == 1 && seconds >= 1 && seconds <= 60)
    {
        profile.configure(seconds);
        std::fprintf(stderr, "gpu profile: armed seconds=%u warmup=120 buffered=1\n", seconds);
    }
    else
        std::fputs("gpu profile: invalid duration (expected 1..60 seconds)\n", stderr);
}

extern "C" uint64_t ps5_vulkan_profile_texture_begin()
{
    return profile.enabled && !profile.finished ? PS5_VULKAN_PROFILE_NOW() : 0;
}
extern "C" void ps5_vulkan_profile_texture_end(uint64_t start)
{
    if (start)
        profile.texture_pending += PS5_VULKAN_PROFILE_NOW() - start;
}

extern "C" void ps5_vulkan_profile_begin()
{
    if (profile.enabled && !profile.finished)
        profile.begin(PS5_VULKAN_PROFILE_NOW());
}
extern "C" void ps5_vulkan_profile_end()
{
    if (!profile.active)
        return;
    profile.finish(PS5_VULKAN_PROFILE_NOW());
    if (profile.finished)
    {
        FILE *out = std::fopen("/app0/gpu-profile.tsv", "w");
        if (!out)
        {
            std::fputs("gpu profile: could not open result file\n", stderr);
            return;
        }
        // Reporting happens only after the measured interval, never inside it.
        char buffer[65536];
        std::setvbuf(out, buffer, _IOFBF, sizeof(buffer));
        profile.dump(out);
        const bool failed = std::ferror(out) != 0;
        const int closed = std::fclose(out);
        std::fprintf(stderr, "gpu profile: completed frames=%u seconds=%.3f errors=%u write=%s\n",
                     profile.record_count, profile.elapsed / 1e9, gpu_failures,
                     failed || closed != 0 ? "failed" : "ok");
    }
}

/* Called by both frontend symbol loaders. Keep the driver's real function,
 * preserve NULL lookups and return every result unchanged. Reloading a device
 * must neither reset the run totals nor wrap our own wrapper recursively. */
#ifdef PS5_MEMORY_DIAGNOSTICS
namespace
{
PFN_vkCreateImage memory_create_image;
PFN_vkDestroyImage memory_destroy_image;
PFN_vkQueueWaitIdle memory_queue_idle;
VKAPI_ATTR VkResult VKAPI_CALL memory_create(VkDevice device, const VkImageCreateInfo *info,
                                             const VkAllocationCallbacks *alloc, VkImage *image)
{
    const VkResult result = memory_create_image(device, info, alloc, image);
    ps5::memory::event("image_create", result == VK_SUCCESS);
    return result;
}
VKAPI_ATTR void VKAPI_CALL memory_destroy(VkDevice device, VkImage image,
                                          const VkAllocationCallbacks *alloc)
{
    memory_destroy_image(device, image, alloc);
    if (image)
        ps5::memory::event("image_destroy");
}
VKAPI_ATTR VkResult VKAPI_CALL memory_idle(VkQueue queue)
{
    ps5::memory::event("idle_begin");
    const VkResult result = memory_queue_idle(queue);
    ps5::memory::event("idle_end", result == VK_SUCCESS);
    return result;
}
} // namespace
#endif

extern "C" void ps5_vulkan_trace_symbol(const char *name, PFN_vkVoidFunction *symbol)
{
#ifdef PS5_MEMORY_DIAGNOSTICS
    if (symbol && *symbol)
    {
        if (!std::strcmp(name, "vkCreateImage"))
        {
            if (*symbol != reinterpret_cast<PFN_vkVoidFunction>(memory_create))
                memory_create_image = reinterpret_cast<PFN_vkCreateImage>(*symbol);
            *symbol = reinterpret_cast<PFN_vkVoidFunction>(memory_create);
        }
        else if (!std::strcmp(name, "vkDestroyImage"))
        {
            if (*symbol != reinterpret_cast<PFN_vkVoidFunction>(memory_destroy))
                memory_destroy_image = reinterpret_cast<PFN_vkDestroyImage>(*symbol);
            *symbol = reinterpret_cast<PFN_vkVoidFunction>(memory_destroy);
        }
        else if (!std::strcmp(name, "vkQueueWaitIdle"))
        {
            if (*symbol != reinterpret_cast<PFN_vkVoidFunction>(memory_idle))
                memory_queue_idle = reinterpret_cast<PFN_vkQueueWaitIdle>(*symbol);
            *symbol = reinterpret_cast<PFN_vkVoidFunction>(memory_idle);
        }
    }
#endif

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
