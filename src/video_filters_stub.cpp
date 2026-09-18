/*
 * PS5 RetroArch - the Vulkan filter chain, stubbed out.
 *
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Why this file exists. RetroArch's Vulkan video driver is built with video
 * filters disabled, which is this port's configuration: nothing here loads a
 * `.slangp` preset, and the menu this project is proving needs no shader at all.
 * The driver, however, still names the filter chain's entry points, and the
 * implementation lives in a source tree upstream keeps out of the tarball - the
 * twenty symbols below are declared in gfx/drivers_shader/shader_vulkan.h and
 * defined nowhere in vendor/retroarch, so the link fails on all of them.
 *
 * The alternative to this file is building glslang and the whole shader compiler
 * into the title to support a feature the port does not use. These bodies are the
 * honest consequence of switching that feature off:
 *
 *   - every create returns NULL, which is what the driver reads as "no chain", and
 *     it already handles that because filters are optional;
 *   - every setter does nothing, because there is no chain to set anything on;
 *   - the queries answer the neutral value a missing chain has: 0 format, no
 *     passes, not HDR10.
 *
 * If this port ever grows shader support, this file is what has to go, and the
 * step that does it names the preset it is loading and the capture that proves it.
 */

#include <cstddef>
#include <cstdint>

/* The chain is opaque here on purpose: nothing in this file dereferences one. */
struct vulkan_filter_chain;
using vulkan_filter_chain_t = struct vulkan_filter_chain;

struct vulkan_filter_chain_create_info;
struct vulkan_filter_chain_swapchain_info;
struct vulkan_filter_chain_texture;
struct video_shader;
struct VkCommandBuffer_T;
struct VkViewport;
using VkCommandBuffer = struct VkCommandBuffer_T *;
using VkViewport = struct VkViewport;
using VkFormat = int;
using VkShaderStageFlags = unsigned;

namespace
{
/* There is no chain, so every pointer the driver holds from here is the NULL this
 * file returns. */
constexpr int vk_format_undefined = 0;
} // namespace

extern "C"
{
    vulkan_filter_chain_t *
    vulkan_filter_chain_new(const struct vulkan_filter_chain_create_info *info)
    {
        (void)info;
        return nullptr;
    }

    void vulkan_filter_chain_free(vulkan_filter_chain_t *chain)
    {
        (void)chain;
    }

    void vulkan_filter_chain_set_shader(vulkan_filter_chain_t *chain, unsigned pass,
                                        VkShaderStageFlags stage, const std::uint32_t *spirv,
                                        std::size_t spirv_words)
    {
        (void)chain;
        (void)pass;
        (void)stage;
        (void)spirv;
        (void)spirv_words;
    }

    VkFormat vulkan_filter_chain_get_pass_rt_format(vulkan_filter_chain_t *chain, unsigned pass)
    {
        (void)chain;
        (void)pass;
        return vk_format_undefined;
    }

    bool
    vulkan_filter_chain_update_swapchain_info(vulkan_filter_chain_t *chain,
                                              const struct vulkan_filter_chain_swapchain_info *info)
    {
        (void)chain;
        (void)info;
        return false;
    }

    void vulkan_filter_chain_notify_sync_index(vulkan_filter_chain_t *chain, unsigned index)
    {
        (void)chain;
        (void)index;
    }

    bool vulkan_filter_chain_init(vulkan_filter_chain_t *chain)
    {
        (void)chain;
        return false;
    }

    void vulkan_filter_chain_set_input_texture(vulkan_filter_chain_t *chain,
                                               const struct vulkan_filter_chain_texture *texture)
    {
        (void)chain;
        (void)texture;
    }

    void vulkan_filter_chain_set_frame_count(vulkan_filter_chain_t *chain, std::uint64_t count)
    {
        (void)chain;
        (void)count;
    }

    void vulkan_filter_chain_set_frame_count_period(vulkan_filter_chain_t *chain, unsigned pass,
                                                    unsigned period)
    {
        (void)chain;
        (void)pass;
        (void)period;
    }

    void vulkan_filter_chain_set_shader_subframes(vulkan_filter_chain_t *chain,
                                                  std::uint32_t total_subframes)
    {
        (void)chain;
        (void)total_subframes;
    }

    void vulkan_filter_chain_set_current_shader_subframe(vulkan_filter_chain_t *chain,
                                                         std::uint32_t current_subframe)
    {
        (void)chain;
        (void)current_subframe;
    }

    void vulkan_filter_chain_set_simulate_scanline(vulkan_filter_chain_t *chain,
                                                   bool simulate_scanline)
    {
        (void)chain;
        (void)simulate_scanline;
    }

    void vulkan_filter_chain_set_frame_direction(vulkan_filter_chain_t *chain,
                                                 std::int32_t direction)
    {
        (void)chain;
        (void)direction;
    }

    void vulkan_filter_chain_set_frame_time_delta(vulkan_filter_chain_t *chain,
                                                  std::uint32_t time_delta)
    {
        (void)chain;
        (void)time_delta;
    }

    void vulkan_filter_chain_set_original_fps(vulkan_filter_chain_t *chain, float fps)
    {
        (void)chain;
        (void)fps;
    }

    void vulkan_filter_chain_set_rotation(vulkan_filter_chain_t *chain, std::uint32_t rotation)
    {
        (void)chain;
        (void)rotation;
    }

    void vulkan_filter_chain_set_core_aspect(vulkan_filter_chain_t *chain, float core_aspect)
    {
        (void)chain;
        (void)core_aspect;
    }

    void vulkan_filter_chain_set_core_aspect_rot(vulkan_filter_chain_t *chain,
                                                 float core_aspect_rot)
    {
        (void)chain;
        (void)core_aspect_rot;
    }

    void vulkan_filter_chain_build_offscreen_passes(vulkan_filter_chain_t *chain,
                                                    VkCommandBuffer cmd, const VkViewport *vp)
    {
        (void)chain;
        (void)cmd;
        (void)vp;
    }

    void vulkan_filter_chain_build_viewport_pass(vulkan_filter_chain_t *chain, VkCommandBuffer cmd,
                                                 const VkViewport *vp, const float *mvp)
    {
        (void)chain;
        (void)cmd;
        (void)vp;
        (void)mvp;
    }

    void vulkan_filter_chain_end_frame(vulkan_filter_chain_t *chain, VkCommandBuffer cmd)
    {
        (void)chain;
        (void)cmd;
    }

    vulkan_filter_chain_t *
    vulkan_filter_chain_create_default(const struct vulkan_filter_chain_create_info *info,
                                       int filter)
    {
        (void)info;
        (void)filter;
        return nullptr;
    }

    vulkan_filter_chain_t *
    vulkan_filter_chain_create_from_preset(const struct vulkan_filter_chain_create_info *info,
                                           const char *path, int filter)
    {
        (void)info;
        (void)path;
        (void)filter;
        return nullptr;
    }

    struct video_shader *vulkan_filter_chain_get_preset(vulkan_filter_chain_t *chain)
    {
        (void)chain;
        return nullptr;
    }

    bool vulkan_filter_chain_emits_hdr10(vulkan_filter_chain_t *chain)
    {
        (void)chain;
        return false;
    }

    /* The one symbol outside the chain itself that the disabled shader path
     * still names: parsing a preset's parameters. With no shader support there is
     * no preset to parse, so it reports that it parsed nothing. */
    bool slang_preprocess_parse_parameters(const char *shader_path, struct video_shader *shader)
    {
        (void)shader_path;
        (void)shader;
        return false;
    }
}
