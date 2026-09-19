#!/usr/bin/env python3
# PS5 RetroArch - the runtime probes, kept so a debug run does not start from zero.
#
#   python3 tools/apply-runtime-probes.py <configured-tree>          apply all
#   python3 tools/apply-runtime-probes.py <configured-tree> --revert  remove all
#   python3 tools/apply-runtime-probes.py --list                      show them
#
# Why this exists. Every probe in this project is written into the configured copy
# of RetroArch, and `tools/retroarch-sources.sh` rebuilds that copy from upstream -
# so a probe vanishes the moment anyone reconfigures, which is exactly what
# happened to the set that finally found the input fault. These are kept here
# instead: version-controlled, re-appliable in one command, and never part of the
# shipping build, because `tools/build-title.sh` does not call this script.
#
# Why probes in the frontend at all. The kernel log cannot see inside the
# frontend, and RetroArch's own file logger is initialised *after* it parses its
# config - so anything that goes wrong during startup is invisible to both. A line
# written to /app0/trace.txt from inside the failing function is the only
# instrument that works there, and it is what turned three "somewhere in init"
# theories into named statements.
#
# The probes are plain fprintf calls appended to the title's trace file. They are
# deliberately not routed through src/trace.cpp: the configured tree is C, and a
# raw stdio call is one line that cannot itself be the reason a build fails.
#
# What each one is for is in the note beside it - these are the landmarks that
# were hard to find the first time.

from __future__ import annotations

import sys
from pathlib import Path

# (file, anchor, inserted-before-anchor, marker, note)
PROBES = [
    (
        "input/input_driver.c",
        # The anchor is the comment *after* the declarations, not the declarations
        # themselves: a probe inserted before them reads `input` before it exists,
        # and `input/input_driver.c: use of undeclared identifier 'input'` is what
        # that costs - the file does not compile, the archive keeps 275 of its 276
        # objects, and the link then reports every symbol that file defines as
        # undefined. Placed here, the probe runs at the first statement, which is
        # where it was meant to be read anyway.
        "   /* Changed by this port (patches/series, 0009): a driver selected during\n",
        "   { FILE *f = fopen(\"/app0/trace.txt\", \"a\");\n"
        "     if (f) { fprintf(f, \"probe INV: entered tmp=%p *input=%p configured=\\\"%s\\\"\\n\",\n"
        "                      (void*)tmp, (void*)*input, settings->arrays.input_driver); fclose(f); } }\n",
        "probe INV: entered",
        "what the input driver initialisation is handed: `tmp` is the pointer the "
        "video driver carried in - if it equals the selected driver, no video driver "
        "supplied one and the early return would skip initialisation entirely",
    ),
    (
        "input/input_driver.c",
        "   if (tmp)\n      *input = tmp;\n",
        "   { FILE *f = fopen(\"/app0/trace.txt\", \"a\");\n"
        "     if (f) { fprintf(f, \"probe INV: after-selection *input=%p tmp=%p\\n\",\n"
        "                      (void*)*input, (void*)tmp); fclose(f); } }\n",
        "probe INV: after-selection",
        "whether the selection survived to the point the wrap is decided - the "
        "difference between the wrap below being reachable and being dead code",
    ),
    (
        "input/input_driver.c",
        "   input_driver_st.current_data = new_data;\n",
        "   { FILE *f = fopen(\"/app0/trace.txt\", \"a\");\n"
        "     if (f) { fprintf(f, \"probe INV: after-wrap data=%p\\n\", new_data); fclose(f); } }\n",
        "probe INV: after-wrap data",
        "the state the wrapped driver returned - NULL here means the driver's own "
        "init refused, not that it was never called",
    ),
    (
        "input/input_driver.c",
        "   if (!input)\n      return NULL;\n   if ((ret = input->init(name)))",
        "   { FILE *f = fopen(\"/app0/trace.txt\", \"a\");\n"
        "     if (f) { fprintf(f, \"probe INV: wrap calling init\\n\"); fclose(f); } }\n",
        "probe INV: wrap calling init",
        "the last line before any input driver's own init runs",
    ),
    (
        "gfx/video_driver.c",
        "   if (!video_driver_init_input(tmp, settings, verbosity_enabled))",
        "   { FILE *f = fopen(\"/app0/trace.txt\", \"a\");\n"
        "     if (f) { fprintf(f, \"probe: about to call video_driver_init_input\\n\"); fclose(f); } }\n",
        "probe: about to call video_driver_init_input",
        "brackets the whole input-initialisation step from the video driver's side",
    ),
    (
        "retroarch.c",
        "   drivers_init(settings, DRIVERS_CMD_ALL, (enum driver_lifetime_flags)0, verbosity_enabled);",
        "   { FILE *f = fopen(\"/app0/trace.txt\", \"a\");\n"
        "     if (f) { fprintf(f, \"probe M:drivers-init-all\\n\"); fclose(f); } }\n",
        "probe M:drivers-init-all",
        "the landmark that separated \"the crash is in a driver\" from \"the crash is "
        "after every driver\", which took several runs to establish",
    ),
    (
        "retroarch.c",
        "   command_event(CMD_EVENT_CONTROLLER_INIT, NULL);",
        "   { FILE *f = fopen(\"/app0/trace.txt\", \"a\");\n"
        "     if (f) { fprintf(f, \"probe M:controller-init\\n\"); fclose(f); } }\n",
        "probe M:controller-init",
        "the last landmark before the crash that reading the config causes",
    ),
    (
        "gfx/video_driver.c",
        "   video_driver_find_driver(settings, \"video driver\", verbosity_enabled);\n"
        "\n"
        "#ifdef HAVE_THREADS\n",
        "   { FILE *f = fopen(\"/app0/trace.txt\", \"a\");\n"
        "     if (f) { fprintf(f, \"probe VDRV: configured=\\\"%s\\\" selected=\\\"%s\\\"\\n\",\n"
        "                      settings->arrays.video_driver,\n"
        "                      video_st->current_video ? video_st->current_video->ident : \"(none)\");\n"
        "              fclose(f); } }\n",
        "probe VDRV: configured",
        "which video driver the frontend actually selected. The log is silent about "
        "it: a driver that logs nothing on init (video_null does) looks identical to "
        "one that never ran, and the trace is the only place the identity is written",
    ),
    (
        "gfx/video_driver.c",
        "   if (!video_st->data)\n"
        "   {\n"
        "      RARCH_ERR(\"[Video] Cannot open video driver. Exiting...\\n\");\n",
        "   { FILE *f = fopen(\"/app0/trace.txt\", \"a\");\n"
        "     if (f) { fprintf(f, \"probe VDRV: init returned data=%p driver=\\\"%s\\\"\\n\",\n"
        "                      video_st->data,\n"
        "                      video_st->current_video ? video_st->current_video->ident : \"(none)\");\n"
        "              fclose(f); } }\n",
        "probe VDRV: init returned",
        "what the selected driver's own init handed back - the difference between a "
        "driver that refused and one that was never asked",
    ),
    (
        "gfx/drivers/vulkan.c",
        "   ctx_driver                         = vulkan_get_context(vk, settings);\n",
        "   { FILE *f = fopen(\"/app0/trace.txt\", \"a\");\n"
        "     if (f) { fprintf(f, \"probe VK: init entered %ux%u rgb32=%d\\n\",\n"
        "                      video->width, video->height, video->rgb32); fclose(f); } }\n",
        "probe VK: init entered",
        "whether RetroArch reached the Vulkan driver at all - the question the "
        "frontend's own log cannot answer, because it prints nothing for a driver "
        "that is never called and nothing for one that returns early",
    ),
    (
        "gfx/drivers/vulkan.c",
        "   if (!ctx_driver)\n"
        "   {\n"
        "      RARCH_ERR(\"[Vulkan] Failed to get Vulkan context.\\n\");\n",
        "   { FILE *f = fopen(\"/app0/trace.txt\", \"a\");\n"
        "     if (f) { fprintf(f, \"probe VK: context driver=%p ident=\\\"%s\\\"\\n\",\n"
        "                      (void*)ctx_driver, ctx_driver ? ctx_driver->ident : \"(none)\");\n"
        "              fclose(f); } }\n",
        "probe VK: context driver",
        "what vulkan_get_context resolved: this is the linked driver's context "
        "driver, and NULL here means the swap from dlopen to a linked symbol did "
        "not take",
    ),
    (
        "retroarch.c",
        "      if (!task_push_load_content_from_cli(\n"
        "               NULL,\n"
        "               NULL,\n"
        "               &info,\n"
        "               CORE_TYPE_PLAIN,\n"
        "               NULL,\n"
        "               NULL))\n",
        "      { FILE *f = fopen(\"/app0/trace.txt\", \"a\");\n"
        "        if (f) { fprintf(f, \"probe RMAIN: content push reached\\n\"); fclose(f); } }\n",
        "probe RMAIN: content push reached",
        "whether startup got as far as the content-load push, which is the first "
        "silent `return 1` in rarch_main after every driver is initialised",
    ),
    (
        "retroarch.c",
        "   settings = config_get_ptr();\n"
        "\n"
        "   ui_companion_driver_init_first(\n",
        "   { FILE *f = fopen(\"/app0/trace.txt\", \"a\");\n"
        "     if (f) { fprintf(f, \"probe RMAIN: content push returned true\\n\"); fclose(f); } }\n",
        "probe RMAIN: content push returned true",
        "the other side of that push: if `reached` is present and this line is not, "
        "the push returned false and is the `return 1`",
    ),
    (
        # The driver's ps5vk_CreateImage asserts that the format, type, tiling and
        # usage are a combination it supports, and the assert says nothing about
        # which part failed - the console reports only `abort is called(system)`.
        # This prints the request beside what the driver advertises for that
        # format, which is everything the check compares: image_supported() allows
        # 2D, OPTIMAL, no flags, and a usage that is a subset of the format's
        # optimalTilingFeatures.
        "gfx/drivers/vulkan.c",
        "      vkCreateImage(device, &info, NULL, &tex.image);\n",
        "      { FILE *f = fopen(\"/app0/trace.txt\", \"a\");\n"
        "        if (f) {\n"
        "           VkFormatProperties advertised;\n"
        "           memset(&advertised, 0, sizeof(advertised));\n"
        "           vkGetPhysicalDeviceFormatProperties(vk->context->gpu, info.format, &advertised);\n"
        "           fprintf(f, \"probe IMG: %ux%u type=%d fmt=%d tiling=%d usage=0x%x \"\n"
        "                      \"flags=0x%x mips=%u layers=%u samples=0x%x | optimal=0x%x linear=0x%x\\n\",\n"
        "                   info.extent.width, info.extent.height, (int)info.imageType, (int)info.format,\n"
        "                   (int)info.tiling, (unsigned)info.usage, (unsigned)info.flags,\n"
        "                   info.mipLevels, info.arrayLayers, (unsigned)info.samples,\n"
        "                   (unsigned)advertised.optimalTilingFeatures,\n"
        "                   (unsigned)advertised.linearTilingFeatures);\n"
        "           fclose(f);\n"
        "        } }\n",
        "probe IMG:",
        "what image the frontend asks the driver for, next to what the driver "
        "advertises for that format - the whole of the driver's own support check",
    ),
    (
        # vulkan_init returns NULL here, and the only error path between the last
        # image the driver accepted and that return is the default filter chain -
        # which builds RetroArch's stock shader into a pipeline. The three marks
        # below split it into "shader modules" and "graphics pipeline", because
        # those are two different driver code paths: the module is where the
        # SPIR-V is compiled to console ISA, and the pipeline is where the driver's
        # own state checks live.
        "gfx/drivers_shader/shader_vulkan.cpp",
        "   vkCreateShaderModule(device, &module_info, NULL, &shader_stages[0].module);\n",
        "   { FILE *f = fopen(\"/app0/trace.txt\", \"a\");\n"
        "     if (f) { fprintf(f, \"probe CHAIN: creating vertex module, %u bytes of SPIR-V\\n\",\n"
        "                      (unsigned)module_info.codeSize); fclose(f); } }\n",
        "probe CHAIN: creating vertex module",
        "that the stock shader reached the driver as SPIR-V at all",
    ),
    (
        "gfx/drivers_shader/shader_vulkan.cpp",
        "   pipe.sType                = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;\n",
        "   { FILE *f = fopen(\"/app0/trace.txt\", \"a\");\n"
        "     if (f) { fprintf(f, \"probe CHAIN: both modules created; creating pipeline\\n\"); fclose(f); } }\n",
        "probe CHAIN: both modules created",
        "the driver accepted both shader modules, so a failure after this is in "
        "pipeline creation rather than in compiling the shaders",
    ),
    (
        "gfx/drivers_shader/shader_vulkan.cpp",
        "   vkDestroyShaderModule(device, shader_stages[0].module, NULL);\n"
        "   vkDestroyShaderModule(device, shader_stages[1].module, NULL);\n"
        "   return true;\n",
        "   { FILE *f = fopen(\"/app0/trace.txt\", \"a\");\n"
        "     if (f) { fprintf(f, \"probe CHAIN: pipeline created\\n\"); fclose(f); } }\n",
        "probe CHAIN: pipeline created",
        "the other side: present means the stock shader became a real pipeline",
    ),
    (
        # The chain failed before it created a shader module, so it is in the two
        # steps before that: init_alias() fills the semantic maps the reflection
        # resolves against, and slang_reflect_spirv() then either refuses the
        # resource usage or catches a SPIRV-Cross exception. Each has its own mark.
        "gfx/drivers_shader/shader_vulkan.cpp",
        "   common.texture_semantic_map.clear();\n",
        "   { FILE *f = fopen(\"/app0/trace.txt\", \"a\");\n"
        "     if (f) { fprintf(f, \"probe REFL: init_alias with %u pass(es), first name=\\\"%s\\\"\\n\",\n"
        "                      (unsigned)passes.size(), passes.empty() ? \"(none)\" : passes[0]->get_name().c_str());\n"
        "              fclose(f); } }\n",
        "probe REFL: init_alias with",
        "whether the default chain's pass carries a name at all - an unnamed pass "
        "is skipped by the semantic map, and the reflection has nothing to resolve",
    ),
    (
        "gfx/drivers_shader/slang_reflection.cpp",
        "   try\n"
        "   {\n"
        "      spirv_cross::Compiler vertex_compiler(vertex);\n",
        "   { FILE *f = fopen(\"/app0/trace.txt\", \"a\");\n"
        "     if (f) { fprintf(f, \"probe REFL: entered, vertex=%u words fragment=%u words\\n\",\n"
        "                      (unsigned)vertex.size(), (unsigned)fragment.size()); fclose(f); } }\n",
        "probe REFL: entered",
        "whether the stock shader reached the reflection as non-empty SPIR-V",
    ),
    (
        "gfx/drivers_shader/slang_reflection.cpp",
        "      RARCH_ERR(\"[Slang] Failed to reflect SPIR-V.\"\n",
        "      { FILE *f = fopen(\"/app0/trace.txt\", \"a\");\n"
        "        if (f) { fprintf(f, \"probe REFL: slang_reflect refused the resources\\n\"); fclose(f); } }\n",
        "probe REFL: slang_reflect refused",
        "the resources the stock shader uses are not the ones this chain declared",
    ),
    (
        "gfx/drivers_shader/slang_reflection.cpp",
        "      RARCH_ERR(\"[Slang] SPIRV-Cross threw exception: %s.\\n\", e.what());\n",
        "      { FILE *f = fopen(\"/app0/trace.txt\", \"a\");\n"
        "        if (f) { fprintf(f, \"probe REFL: SPIRV-Cross threw: %s\\n\", e.what()); fclose(f); } }\n",
        "probe REFL: SPIRV-Cross threw",
        "the reflection threw rather than refusing - the message says which part of "
        "the SPIR-V it could not read",
    ),
    (
        # None of the marks inside the chain fired, so the failure is before
        # init_alias(): the first thing that chain's constructor does is build its
        # vertex buffer, and the driver's buffer path is the one part of it not yet
        # exercised on this console.
        "gfx/drivers_shader/shader_vulkan.cpp",
        "   vkCreateBuffer(device, &info, nullptr, &buffer);\n",
        "   { FILE *f = fopen(\"/app0/trace.txt\", \"a\");\n"
        "     if (f) { fprintf(f, \"probe BUF: create size=%llu usage=0x%x\\n\",\n"
        "                      (unsigned long long)info.size, (unsigned)info.usage); fclose(f); } }\n",
        "probe BUF: create size=",
        "the buffer the chain constructor asks for, which is the last thing before "
        "the marks that did not fire",
    ),
    (
        "gfx/drivers_shader/shader_vulkan.cpp",
        "   vkBindBufferMemory(device, buffer, memory, 0);\n",
        "   { FILE *f = fopen(\"/app0/trace.txt\", \"a\");\n"
        "     if (f) { fprintf(f, \"probe BUF: bound memory=%p\\n\", (void*)memory); fclose(f); } }\n",
        "probe BUF: bound memory=",
        "whether the driver handed back real memory for that buffer",
    ),
    (
        "gfx/drivers/vulkan.c",
        "   if (!vulkan_init_filter_chain(vk))\n",
        "   { FILE *f = fopen(\"/app0/trace.txt\", \"a\");\n"
        "     if (f) { fprintf(f, \"probe VK: everything before the filter chain is done\\n\"); fclose(f); } }\n",
        "probe VK: everything before the filter chain",
        "separates a failure in the descriptor/buffer setup after the textures from "
        "a failure inside the filter chain itself",
    ),
    (
        # The driver refused the stock shader's pipeline, and a refusal should have
        # printed ../PS5_Vulkan's own message - the shims write Mesa's log to
        # stderr, which src/main.cpp now points at this file. This calls the same
        # entry point with the same state again and prints the VkResult, so the
        # answer does not depend on that message arriving.
        "gfx/drivers_shader/shader_vulkan.cpp",
        "   if (vkCreateGraphicsPipelines(device,\n"
        "            cache, 1, &pipe, NULL, &pipeline) != VK_SUCCESS)\n",
        "   { VkPipeline probe_pipeline = VK_NULL_HANDLE;\n"
        "     VkResult probe_result = vkCreateGraphicsPipelines(device, cache, 1, &pipe, NULL,\n"
        "                                                       &probe_pipeline);\n"
        "     FILE *f = fopen(\"/app0/trace.txt\", \"a\");\n"
        "     if (f) { fprintf(f, \"probe PIPE: vkCreateGraphicsPipelines -> %d (pipeline=%p)\\n\",\n"
        "                      (int)probe_result, (void*)probe_pipeline); fclose(f); } }\n",
        "probe PIPE:",
        "the exact VkResult the driver gives the stock shader's pipeline, which the "
        "caller only compares against VK_SUCCESS",
    ),
    (
        # The result is VK_ERROR_UNKNOWN and ../PS5_Vulkan's own message never
        # arrives - its Mesa log is compiled out - so the state has to be read
        # from this side and checked against that driver's own conditions by hand.
        "gfx/drivers_shader/shader_vulkan.cpp",
        "   if (vkCreateGraphicsPipelines(device,\n"
        "            cache, 1, &pipe, NULL, &pipeline) != VK_SUCCESS)\n",
        "   { FILE *f = fopen(\"/app0/trace.txt\", \"a\");\n"
        "     if (f) {\n"
        "        unsigned bi, ai;\n"
        "        fprintf(f, \"probe PIPE2: renderPass=%p stages=%u verts=%u attrs=%u topo=%d \"\n"
        "                   \"blendAtt=%u blendEnable=%d writeMask=0x%x depthTest=%d samples=0x%x\\n\",\n"
        "                (void*)pipe.renderPass, (unsigned)pipe.stageCount,\n"
        "                (unsigned)(pipe.pVertexInputState ? pipe.pVertexInputState->vertexBindingDescriptionCount : 0),\n"
        "                (unsigned)(pipe.pVertexInputState ? pipe.pVertexInputState->vertexAttributeDescriptionCount : 0),\n"
        "                (int)(pipe.pInputAssemblyState ? pipe.pInputAssemblyState->topology : -1),\n"
        "                (unsigned)(pipe.pColorBlendState ? pipe.pColorBlendState->attachmentCount : 0),\n"
        "                (int)(pipe.pColorBlendState && pipe.pColorBlendState->attachmentCount\n"
        "                        ? pipe.pColorBlendState->pAttachments[0].blendEnable : 0),\n"
        "                (unsigned)(pipe.pColorBlendState && pipe.pColorBlendState->attachmentCount\n"
        "                        ? pipe.pColorBlendState->pAttachments[0].colorWriteMask : 0),\n"
        "                (int)(pipe.pDepthStencilState ? pipe.pDepthStencilState->depthTestEnable : 0),\n"
        "                (unsigned)(pipe.pMultisampleState ? pipe.pMultisampleState->rasterizationSamples : 0));\n"
        "        for (bi = 0; pipe.pVertexInputState && bi < pipe.pVertexInputState->vertexBindingDescriptionCount; bi++)\n"
        "           fprintf(f, \"probe PIPE2: binding %u = {%u, %u}\\n\", bi,\n"
        "                   (unsigned)pipe.pVertexInputState->pVertexBindingDescriptions[bi].binding,\n"
        "                   (unsigned)pipe.pVertexInputState->pVertexBindingDescriptions[bi].stride);\n"
        "        for (ai = 0; pipe.pVertexInputState && ai < pipe.pVertexInputState->vertexAttributeDescriptionCount; ai++)\n"
        "        { const VkVertexInputAttributeDescription *a =\n"
        "             &pipe.pVertexInputState->pVertexAttributeDescriptions[ai];\n"
        "          fprintf(f, \"probe PIPE2: attr %u = {loc %u, bind %u, fmt %d, off %u}\\n\",\n"
        "                  ai, (unsigned)a->location, (unsigned)a->binding, (int)a->format, (unsigned)a->offset); }\n"
        "        fclose(f);\n"
        "     } }\n",
        "probe PIPE3:",
        "the whole pipeline state the driver refuses, printed so it can be compared "
        "against that driver's conditions without its log",
    ),
    (
        "gfx/drivers_shader/shader_vulkan.cpp",
        "   if (vkCreatePipelineLayout(device,\n"
        "            &layout_info, NULL, &pipeline_layout) != VK_SUCCESS)\n",
        "   { FILE *f = fopen(\"/app0/trace.txt\", \"a\");\n"
        "     if (f) {\n"
        "        unsigned s;\n"
        "        fprintf(f, \"probe LAYOUT: bindings=%u pushRanges=%u pushSize=%u pushStages=0x%x\\n\",\n"
        "                (unsigned)set_layout_info.bindingCount, (unsigned)layout_info.pushConstantRangeCount,\n"
        "                (unsigned)push_range.size, (unsigned)push_range.stageFlags);\n"
        "        for (s = 0; s < set_layout_info.bindingCount; s++)\n"
        "           fprintf(f, \"probe LAYOUT: binding %u = {%u, type %d, count %u, stages 0x%x}\\n\", s,\n"
        "                   (unsigned)set_layout_info.pBindings[s].binding,\n"
        "                   (int)set_layout_info.pBindings[s].descriptorType,\n"
        "                   (unsigned)set_layout_info.pBindings[s].descriptorCount,\n"
        "                   (unsigned)set_layout_info.pBindings[s].stageFlags);\n"
        "        fclose(f);\n"
        "     } }\n",
        "probe LAYOUT:",
        "the descriptor bindings and push-constant range the driver's layout check "
        "reads, which is the other refusal it can give before compiling anything",
    ),
    (
        # The compiler's header, at file scope: it opens an extern "C" block, which
        # cannot be included inside a function body.
        "gfx/drivers_shader/shader_vulkan.cpp",
        "#include \"../include/vulkan/vk_sdk_platform.h\"\n",
        "/* Added by this port (probes): the shader compiler's header, so the probe that\n"
        " * asks it directly about the stock shader can name its types. It belongs to\n"
        " * ../PS5_Vulkan and is only used by that probe. */\n"
        "#include \"../../../PS5_Vulkan/.deps/native/psbc/include/psbc_compile.h\"\n"
        "#include \"../include/vulkan/vk_sdk_platform.h\"\n",
        "#include \"../../../PS5_Vulkan/.deps/native/psbc/include/psbc_compile.h\"",
        "the compiler's own types, for the probe that calls it",
    ),
    (
        # Both stock shaders compile with the host build of this compiler, using
        # the driver's own options (UBO stride 16, sampler at binding 2, stride 48).
        # The driver refuses the pipeline anyway, and its message is compiled out,
        # so the compiler is asked here on the console with the same options - the
        # one thing a host test cannot reproduce. psbc_init() is left alone: the
        # driver has already initialised the library by the time a pipeline is
        # built, and calling it again is what crashed the first version of this
        # probe.
        "gfx/drivers_shader/shader_vulkan.cpp",
        "   { VkPipeline probe_pipeline = VK_NULL_HANDLE;\n",
        "   {\n"
        "     PsbcCompileOptions probe_options;\n"
        "     PsbcShaderOutput probe_output;\n"
        "     PsbcResult probe_result;\n"
        "     FILE *f;\n"
        "\n"
        "     memset(&probe_options, 0, sizeof(probe_options));\n"
        "     memset(&probe_output, 0, sizeof(probe_output));\n"
        "     probe_options.target       = PSBC_TARGET_PS5;\n"
        "     probe_options.optimise     = true;\n"
        "     probe_options.entrypoint   = \"main\";\n"
        "     probe_options.address32_hi = 2u;\n"
        "     probe_options.vertex_attributes[0] = (PsbcVertexAttribute){\n"
        "        .location = 0, .binding = 0, .format = PSBC_VERTEX_FORMAT_R32G32_FLOAT,\n"
        "        .offset = 0, .stride = 16, .alignment = 4};\n"
        "     probe_options.vertex_attributes[1] = (PsbcVertexAttribute){\n"
        "        .location = 1, .binding = 0, .format = PSBC_VERTEX_FORMAT_R32G32_FLOAT,\n"
        "        .offset = 8, .stride = 16, .alignment = 4};\n"
        "     probe_options.vertex_attribute_count = 2;\n"
        "     probe_options.descriptor_bindings[0] = (PsbcDescriptorBinding){\n"
        "        .set = 0, .binding = 0, .type = PSBC_DESCRIPTOR_UNIFORM_BUFFER,\n"
        "        .array_size = 1, .offset = 0, .stride = 16};\n"
        "     probe_options.descriptor_bindings[1] = (PsbcDescriptorBinding){\n"
        "        .set = 0, .binding = 2, .type = PSBC_DESCRIPTOR_COMBINED_IMAGE_SAMPLER,\n"
        "        .array_size = 1, .offset = 16, .stride = 48};\n"
        "     probe_options.descriptor_binding_count = 2;\n"
        "\n"
        "     probe_options.stage = PSBC_STAGE_VERTEX;\n"
        "     probe_result = psbc_compile_shader(vertex_shader.data(), vertex_shader.size(),\n"
        "                                        &probe_options, &probe_output);\n"
        "     f = fopen(\"/app0/trace.txt\", \"a\");\n"
        "     if (f) { fprintf(f, \"probe PSBC: vertex -> %d (%s)\\n\", (int)probe_result,\n"
        "                      psbc_result_string(probe_result)); fclose(f); }\n"
        "     psbc_free_output(&probe_output);\n"
        "     memset(&probe_output, 0, sizeof(probe_output));\n"
        "     probe_options.stage = PSBC_STAGE_FRAGMENT;\n"
        "     probe_result = psbc_compile_shader(fragment_shader.data(), fragment_shader.size(),\n"
        "                                        &probe_options, &probe_output);\n"
        "     f = fopen(\"/app0/trace.txt\", \"a\");\n"
        "     if (f) { fprintf(f, \"probe PSBC: fragment -> %d (%s)\\n\", (int)probe_result,\n"
        "                      psbc_result_string(probe_result)); fclose(f); }\n"
        "     psbc_free_output(&probe_output);\n"
        "   }\n",
        "probe PSBC:",
        "the console's own answer for the stock shader, with the driver's options",
    ),
]


def apply(tree: Path, revert: bool) -> int:
    failures = 0
    for name, anchor, insert, marker, _note in PROBES:
        path = tree / name
        if not path.is_file():
            print(f"  {name}: MISSING FILE")
            failures += 1
            continue
        text = path.read_text(encoding="utf-8")
        if revert:
            if insert not in text:
                print(f"  {name}: not probed")
                continue
            path.write_text(text.replace(insert, ""), encoding="utf-8")
            print(f"  {name}: probe removed")
            continue
        if marker in text:
            print(f"  {name}: present")
            continue
        if anchor not in text:
            print(f"  {name}: ANCHOR NOT FOUND for {marker!r}")
            failures += 1
            continue
        path.write_text(text.replace(anchor, insert + anchor, 1), encoding="utf-8")
        print(f"  {name}: applied {marker}")
    return 1 if failures else 0


def main() -> int:
    args = sys.argv[1:]
    if not args or args[0] in ("-h", "--help"):
        print(__doc__ or "")
        return 2
    if args[0] == "--list":
        for name, _anchor, _insert, marker, note in PROBES:
            print(f"{name}\n    {marker}\n    {note}\n")
        return 0
    tree = Path(args[0])
    if not (tree / "retroarch.c").is_file():
        print(f"error: {tree} does not look like a configured RetroArch tree", file=sys.stderr)
        return 2
    return apply(tree, "--revert" in args)


if __name__ == "__main__":
    raise SystemExit(main())
