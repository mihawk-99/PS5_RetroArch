/* PS5 RetroArch - bounded diagnostic readback through the driver's debug API.
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "gfx/include/vulkan/vulkan.h"
#include <cstdio>

extern "C" void *ps5vk_debug_image_storage(VkImage image, size_t *bytes);

struct Stage
{
    void *address;
    size_t bytes;
};
extern "C" uint32_t ps5vk_debug_pipeline_stages(VkDevice, Stage *, uint32_t);
extern "C" uint32_t ps5vk_debug_buffers(VkDevice, Stage *, uint32_t);
extern "C" uint32_t ps5vk_debug_table_chunks(VkDevice, Stage *, uint32_t);
extern "C" void ps5_capture_images(unsigned frame, VkImage menu, VkImage target, VkDevice device)
{
    if (frame == 8)
    {
        Stage stages[64] = {};
        for (unsigned kind = 0; kind < 3; ++kind)
        {
            const char *name = kind == 0 ? "stages" : kind == 1 ? "buffers" : "tables";
            uint32_t count = kind == 0   ? ps5vk_debug_pipeline_stages(device, stages, 64)
                             : kind == 1 ? ps5vk_debug_buffers(device, stages, 64)
                                         : ps5vk_debug_table_chunks(device, stages, 64);
            for (uint32_t i = 0; i < count && i < 64; ++i)
            {
                if (stages[i].bytes > 4 * 1024 * 1024)
                    continue;
                char path[96];
                std::snprintf(path, sizeof(path), "/app0/gpu-%s-%u.bin", name, i);
                FILE *file = std::fopen(path, "wb");
                const size_t written =
                    file ? std::fwrite(stages[i].address, 1, stages[i].bytes, file) : 0;
                const int closed = file ? std::fclose(file) : -1;
                std::fprintf(stderr,
                             "gpu resources: %s %u address=%p bytes=%zu written=%zu close=%d\n",
                             name, i, stages[i].address, stages[i].bytes, written, closed);
            }
        }
    }
    const VkImage images[] = {menu, target};
    const char *names[] = {"menu", "target"};
    for (unsigned i = 0; i < 2; ++i)
    {
        size_t bytes = 0;
        const void *pixels = ps5vk_debug_image_storage(images[i], &bytes);
        char path[96];
        std::snprintf(path, sizeof(path), "/app0/gpu-%s-%u.bin", names[i], frame);
        FILE *file = pixels && bytes ? std::fopen(path, "wb") : nullptr;
        size_t written = file ? std::fwrite(pixels, 1, bytes, file) : 0;
        const int closed = file ? std::fclose(file) : -1;
        std::fprintf(stderr, "gpu capture: %s frame=%u bytes=%zu written=%zu close=%d\n", names[i],
                     frame, bytes, written, closed);
    }
}
