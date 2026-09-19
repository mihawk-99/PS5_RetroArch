/* PS5 RetroArch - observe the Vulkan API results the frontend discards.
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "gfx/include/vulkan/vulkan.h"

#include <cstdio>
#include <cstring>

namespace
{
PFN_vkEndCommandBuffer end_command_buffer;
PFN_vkQueueSubmit queue_submit;
PFN_vkQueuePresentKHR queue_present;

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
    const VkResult result = end_command_buffer(command);
    results.record("vkEndCommandBuffer", result);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL traced_submit(VkQueue queue, uint32_t count,
                                             const VkSubmitInfo *submits, VkFence fence)
{
    static Results results;
    const VkResult result = queue_submit(queue, count, submits, fence);
    results.record("vkQueueSubmit", result);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL traced_present(VkQueue queue, const VkPresentInfoKHR *present)
{
    static Results results;
    static Results images;
    const VkResult result = queue_present(queue, present);
    results.record("vkQueuePresentKHR", result);
    if (present->pResults)
        for (uint32_t i = 0; i < present->swapchainCount; ++i)
            images.record("swapchain image", present->pResults[i]);
    return result;
}
} // namespace

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
}
