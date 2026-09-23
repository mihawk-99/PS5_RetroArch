#!/usr/bin/env python3
"""Apply this port's changes to RetroArch's sources.

    python3 tools/apply-port-patches.py <configured-tree>

Why a script and not `patch`. A unified diff carries line numbers and context
taken from one revision, so it either fails outright or applies in the wrong
place when upstream moves by a few lines. This port's changes to RetroArch are
small insertions at known anchors, and each anchor is a line upstream has no
reason to change, so the changes are matched on their anchors and are safe to
re-run:

    gfx/video_driver.h   declare the driver beside video_null's declaration
    gfx/video_driver.c   list the driver in video_drivers[] beside &video_null
    qb/config.params.sh  give HAVE_XKBCOMMON the same `auto` default that every
                         other optional library already declares, so that
                         --disable-xkbcommon is an option configure accepts

Every edit prints `applied` or `present`, so the script reports what it did rather
than leaving the tree in an unknown state. Nothing is ever written to
vendor/retroarch: the tree passed in is the configured copy under build/.

A change the driver has outgrown is withdrawn rather than deleted: its marker
goes in WITHDRAWN below, and the script exchanges that insertion for the upstream
text it replaced, so a tree that already carried it and a fresh one agree.

Exit status is 0 when every edit is present at the end, 1 otherwise.
"""

from __future__ import annotations

import sys
from pathlib import Path

# (file, anchor, inserted-before-anchor, already-present-marker)
EDITS = [
    ('libretro-common/file/archive_file_7z.c', '#include <stdlib.h>', '/* patches/series, 0076: archive error diagnostics */\n#include <stdlib.h>\n#include <stdio.h>\n#include <errno.h>', 'patches/series, 0076: archive error diagnostics'),
    ('libretro-common/file/archive_file_7z.c', '   return malloc(len);', '/* patches/series, 0076: checked main allocation */\n   {\n      void *result = malloc(len);\n      if (!result)\n         fprintf(stderr, "archive 7z: allocation failed bytes=%zu errno=%d\\n", len, errno);\n      return result;\n   }', 'patches/series, 0076: checked main allocation'),
    ('libretro-common/file/archive_file_7z.c', 'static void *sevenzip_stream_alloc_tmp_impl(ISzAllocPtr p, size_t len)\n{\n   if (len == 0)\n      return 0;\n   return malloc(len);\n}', '/* patches/series, 0076: checked temporary allocation */\nstatic void *sevenzip_stream_alloc_tmp_impl(ISzAllocPtr p, size_t len)\n{\n   return sevenzip_stream_alloc_impl(p, len);\n}', 'patches/series, 0076: checked temporary allocation'),
    ('libretro-common/file/archive_file_7z.c', '   if (InFile_Open(&archiveStream.file, path))\n      return -1;', '/* patches/series, 0076: open failure */\n   if (InFile_Open(&archiveStream.file, path))\n   {\n      fprintf(stderr, "archive 7z: open failed errno=%d\\n", errno);\n      free(lookStream.buf);\n      return -1;\n   }', 'patches/series, 0076: open failure'),
    ('libretro-common/file/archive_file_7z.c', '   if (SzArEx_Open(&db, &lookStream.vt, &allocImp, &allocTempImp) == SZ_OK)', '/* patches/series, 0076: header failure */\n   SRes open_result = SzArEx_Open(&db, &lookStream.vt, &allocImp, &allocTempImp);\n   if (open_result != SZ_OK)\n      fprintf(stderr, "archive 7z: header failed result=%d errno=%d\\n", open_result, errno);\n   if (open_result == SZ_OK)', 'patches/series, 0076: header failure'),
    ('libretro-common/file/archive_file_7z.c', '            if (res != SZ_OK)\n               break; /* This goes to the error section. */', '/* patches/series, 0076: extraction failure */\n            if (res != SZ_OK)\n            {\n               fprintf(stderr, "archive 7z: extract failed result=%d bytes=%zu errno=%d\\n", res, output_size, errno);\n               break;\n            }', 'patches/series, 0076: extraction failure'),
    ('libretro-common/file/archive_file_7z.c', '               *buf = malloc((size_t)(outsize + 1));\n               ((char*)(*buf))[outsize]', '/* patches/series, 0076: guard content allocation */\n               *buf = malloc((size_t)(outsize + 1));\n               if (!*buf)\n               {\n                  fprintf(stderr, "archive 7z: content allocation failed bytes=%lld errno=%d\\n", (long long)outsize + 1, errno);\n                  res = SZ_ERROR_MEM;\n                  break;\n               }\n               ((char*)(*buf))[outsize]', 'patches/series, 0076: guard content allocation'),

    ('gfx/drivers/vulkan.c', '   vk->tex_fmt           = video->rgb32 ? VK_FORMAT_B8G8R8A8_UNORM : VK_FORMAT_R5G6B5_UNORM_PACK16;', '   /* patches/series, 0075: matching sampled RGBA staging and dynamic core frames. */\n   vk->tex_fmt           = video->rgb32 ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R5G6B5_UNORM_PACK16;', 'patches/series, 0075: matching'),
    ('gfx/drivers/vulkan.c', '      if (frame != chain->texture.mapped)\n      {', '      /* patches/series, 0075: libretro XRGB pixels become RGBA upload bytes. */\n      if (vk->video.rgb32)\n      {\n         extern void ps5_core_frame_rgba(void *, size_t, const void *, size_t, unsigned, unsigned);\n         ps5_core_frame_rgba(chain->texture.mapped, chain->texture.stride,\n               frame, pitch, frame_width, frame_height);\n      }\n      else if (frame != chain->texture.mapped)\n      {', 'patches/series, 0075: libretro XRGB'),
    ('retroarch.c', '   settings = config_get_ptr();\n\n   ui_companion_driver_init_first(', '   /* patches/series, 0074: opt-in refused-core test against the live menu. */\n   {\n      extern void ps5_core_recovery_test_if_requested(void);\n      ps5_core_recovery_test_if_requested();\n   }\n   settings = config_get_ptr();\n\n   ui_companion_driver_init_first(', 'patches/series, 0074: opt-in refused-core test'),
    ('libretro-common/dynamic/dylib.c', '#include <dlfcn.h>', '#include <dlfcn.h>\n/* patches/series, 0073: native title core loading, not payload dlfcn hooks. */\nextern void *ps5_core_dlopen(const char *, int);\nextern void *ps5_core_dlsym(void *, const char *);\nextern int ps5_core_dlclose(void *);\nextern char *ps5_core_dlerror(void);\n#define dlopen ps5_core_dlopen\n#define dlsym ps5_core_dlsym\n#define dlclose ps5_core_dlclose\n#define dlerror ps5_core_dlerror', 'patches/series, 0073: native title core loading'),
    ('tasks/task_content.c', '   /* Load core */\n   command_event(CMD_EVENT_LOAD_CORE, NULL);\n\n   /* Load content */', '   /* patches/series, 0072: keep the live menu when core selection fails. */\n   if (!command_event(CMD_EVENT_LOAD_CORE, NULL))\n   {\n      RARCH_ERR("[PS5] Core selection failed; preserving the running frontend.\\n");\n      ret = false;\n      goto end;\n   }\n\n   /* Load content */', 'patches/series, 0072: keep the live menu'),
    ('tasks/task_content.c', '   /* Load core */\n   command_event(CMD_EVENT_LOAD_CORE, NULL);\n\n#ifndef HAVE_DYNAMIC', '   /* patches/series, 0072: do not mark a refused core as selected. */\n   if (!command_event(CMD_EVENT_LOAD_CORE, NULL))\n      return false;\n\n#ifndef HAVE_DYNAMIC', 'patches/series, 0072: do not mark'),
    ('tasks/task_content.c', '   command_event(CMD_EVENT_LOAD_CORE, NULL);\n\n   runloop_set_current_core_type(CORE_TYPE_PLAIN, true);', '   /* patches/series, 0072: reject contentless core failure before teardown. */\n   if (!command_event(CMD_EVENT_LOAD_CORE, NULL))\n   {\n      ret = false;\n      goto end;\n   }\n\n   runloop_set_current_core_type(CORE_TYPE_PLAIN, true);', 'patches/series, 0072: reject contentless'),
    ('retroarch.c', '      ret = runloop_iterate();', '      /* patches/series, 0072: failed reinitialization cannot poll freed drivers. */\n      if (!(runloop_st->flags & RUNLOOP_FLAG_IS_INITED))\n      {\n         RARCH_ERR("[PS5] Frontend initialization failed; leaving the main loop safely.\\n");\n         break;\n      }\n      ret = runloop_iterate();', 'patches/series, 0072: failed reinitialization'),
    # 0071: native joypad discovery, binding capture and built-in defaults.
    ('input/input_driver.h', 'extern input_device_driver_t ps4_joypad;', 'extern input_device_driver_t ps4_joypad;\nextern input_device_driver_t ps5_joypad;', 'extern input_device_driver_t ps5_joypad;'),
    ('input/input_driver.c', '   &null_joypad,\n   NULL,\n};', '   &ps5_joypad,\n   &null_joypad,\n   NULL,\n};', '   &ps5_joypad,'),
    ('input/input_autodetect_builtin.c', 'const char* const input_builtin_autoconfs[] =\n{', 'extern const char ps5_controller_profile[];\nconst char* const input_builtin_autoconfs[] =\n{\n   ps5_controller_profile,', '   ps5_controller_profile,'),

    (
        "gfx/video_driver.h",
        "extern video_driver_t video_null;",
        "extern video_driver_t video_null;\n"
        "/* Supplied by this project (src/video_ps5.cpp): presents through the\n"
        " * console's VideoOut display layer. Declared with C linkage because\n"
        " * RetroArch's own sources are C. */\n"
        "extern video_driver_t video_ps5;",
        "extern video_driver_t video_ps5;",
    ),
    (
        # Position matters twice over. `video_drivers[0]` is the fallback when a
        # configured name cannot be found, so this project's own driver - the one
        # that provably runs on this console - is first. Vulkan is reachable by
        # name ("vulkan", which is also the compiled default now) and sits after
        # it; it needs ../PS5_Vulkan's shared object beside the title, so it must
        # never be the fallback.
        "gfx/video_driver.c",
        "#ifdef HAVE_VULKAN\n   &video_vulkan,\n#endif\n",
        "/* Before the Vulkan block, not after it: this is what makes this port's\n"
        " * driver video_drivers[0] and therefore the fallback as well as the named\n"
        " * default. Inserting it after the block leaves &video_vulkan at index 0\n"
        " * and any fallback still lands on Vulkan, which defeats the point. */\n"
        "   &video_ps5,\n#ifdef HAVE_VULKAN\n   &video_vulkan,\n#endif\n",
        "   &video_ps5,\n#ifdef HAVE_VULKAN",
    ),
    (
        # A1 of docs/GPU_PATH_CRITERIA.md: libps5vk refuses a pipeline it did not
        # build with a triangle list ("only triangle lists without primitive
        # restart are supported", driver/ps5vk_pipeline.c:896-904), and a refusal
        # ends recording with an error at vkEndCommandBuffer, so those command
        # buffers never submit.
        #
        # The strip topology is chosen at six upstream call sites - gfx_display.c
        # :538, :678 and :937, gfx_thumbnail.c:1005, gfx_widgets.c:645 and
        # materialui.c:2583 - and the driver derives the pipeline from it
        # (disp_pipeline = (prim_type == TRIANGLESTRIP) << 1 | blend). Topology and
        # geometry therefore move together, and changing the pipeline alone would
        # draw wrong triangles rather than refuse: a silently wrong picture, which
        # is worse than a refusal. Six edits is also the wrong shape for a port
        # whose changes are meant to be a short named list.
        #
        # So the conversion goes in the one place every menu draw passes through,
        # gfx_display_vk_draw, which already re-bakes the caller's separate vertex,
        # tex-coord and colour arrays into an interleaved VBO. The expansion is an
        # INDEX MAPPING, not a duplication, and that detail is load-bearing: when
        # the caller supplies no coordinates the arrays are the static
        # vk_vertexes[8] and vk_tex_coords[8] - exactly enough for four vertices -
        # so reading six interleaved vertices would read past the end of a static
        # array. Emitting (N-2)*3 vertices while reading source index s(i) touches
        # nothing beyond what the strip already touched.
        "gfx/drivers/vulkan.c",
        "   if (!vulkan_buffer_chain_alloc(vk->context, &vk->chain->vbo,\n"
        "            draw->coords->vertices * sizeof(struct vk_vertex), &range))\n"
        "      return;\n"
        "\n"
        "   pv = (struct vk_vertex*)range.data;\n"
        "   for (i = 0; i < draw->coords->vertices; i++, pv++)\n"
        "   {\n"
        "      pv->x       = *vertex++;\n"
        "      /* Y-flip. Vulkan is top-left clip space */\n"
        "      pv->y       = 1.0f - (*vertex++);\n"
        "      pv->tex_x   = *tex_coord++;\n"
        "      pv->tex_y   = *tex_coord++;\n"
        "      pv->color.r = *color++;\n"
        "      pv->color.g = *color++;\n"
        "      pv->color.b = *color++;\n"
        "      pv->color.a = *color++;\n"
        "   }\n",
        "   /* Named by this port (patches/series, 0011): the console's decoder only\n"
        "    * builds triangle lists, so a strip is expanded into one here and the\n"
        "    * list pipeline is used. See the note in tools/apply-port-patches.py. */\n"
        "   const bool as_strip = (draw->prim_type == GFX_DISPLAY_PRIM_TRIANGLESTRIP);\n"
        "   const unsigned source_count = draw->coords->vertices;\n"
        "   const unsigned output_count =\n"
        "         (as_strip && source_count >= 3) ? (source_count - 2) * 3 : source_count;\n"
        "\n"
        "   if (!vulkan_buffer_chain_alloc(vk->context, &vk->chain->vbo,\n"
        "            output_count * sizeof(struct vk_vertex), &range))\n"
        "      return;\n"
        "\n"
        "   pv = (struct vk_vertex*)range.data;\n"
        "   for (i = 0; i < output_count; i++, pv++)\n"
        "   {\n"
        "      /* Which source vertex this output vertex is: the first triangle of the\n"
        "       * strip, then a pair per triangle after it. */\n"
        "      const unsigned s = as_strip\n"
        "            ? (i < 3 ? i : 3 + ((i - 3) / 2) * 2 + ((i - 3) % 2 ? 0 : 1))\n"
        "            : i;\n"
        "      /* Assigned to the arrays the function already declares, not shadowed:\n"
        "       * the eight reads below must advance the same pointers as before. */\n"
        "      vertex    = draw->coords->vertex ? &draw->coords->vertex[s * 2]\n"
        "                                       : &vk_vertexes[s * 2];\n"
        "      tex_coord = draw->coords->tex_coord ? &draw->coords->tex_coord[s * 2]\n"
        "                                          : &vk_tex_coords[s * 2];\n"
        "      color     = draw->coords->color ? &draw->coords->color[s * 4]\n"
        "                                      : &vk_colors[s * 4];\n"
        "\n"
        "      pv->x       = *vertex++;\n"
        "      /* Y-flip. Vulkan is top-left clip space */\n"
        "      pv->y       = 1.0f - (*vertex++);\n"
        "      pv->tex_x   = *tex_coord++;\n"
        "      pv->tex_y   = *tex_coord++;\n"
        "      pv->color.r = *color++;\n"
        "      pv->color.g = *color++;\n"
        "      pv->color.b = *color++;\n"
        "      pv->color.a = *color++;\n"
        "   }\n",
        "Named by this port (patches/series, 0011)",
    ),
    (
        # The creation site that actually refuses. There are four topology
        # assignments in this file and only these two build a strip pipeline: the
        # display pair loop below (i & 2 selects a strip for half of the four) and
        # the HDR pair, which is a strip unconditionally. An earlier version of
        # this patch changed the *draw* path instead - the expression that picks a
        # pipeline index from the primitive type - which cannot help, because the
        # refusal happens when the pipeline is created, before any draw.
        #
        # Both loops now build lists. The vertices are expanded into lists in
        # gfx_display_vk_draw above, so a draw that names a strip still produces
        # list geometry - which is what the driver decodes.
        "gfx/drivers/vulkan.c",
        "      input_assembly.topology = i & 2 ?\n"
        "         VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP :\n"
        "         VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;\n",
        "      /* patches/series 0011: lists only; the decoder builds nothing else. */\n"
        "      input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;\n",
        "lists only; the decoder builds nothing else",
    ),
    (
        # The HDR pair, a strip unconditionally.
        "gfx/drivers/vulkan.c",
        "   /* Build display hdr pipelines. */\n"
        "   for (i = 4; i < 6; i++)\n"
        "   {\n"
        "      input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;\n",
        "   /* Build display hdr pipelines. */\n"
        "   for (i = 4; i < 6; i++)\n"
        "   {\n"
        "      /* patches/series 0011: lists only, as above. */\n"
        "      input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;\n",
        "lists only, as above",
    ),
    (
        # The fourth and last topology site: the shader-menu pipelines at indices
        # 6 and up, which to_menu_pipeline selects when a shader preset provides a
        # menu. Half of them are strips for the same reason the display pair was.
        "gfx/drivers/vulkan.c",
        "      input_assembly.topology = i & 1 ?\n"
        "         VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP :\n"
        "         VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;\n",
        "      /* patches/series 0011: the shader-menu pipelines are lists too. */\n"
        "      input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;\n",
        "the shader-menu pipelines are lists too",
    ),
    (
        # qb/config.params.sh declares the default state of every optional
        # library, and configure accepts a --enable/--disable switch for each one
        # it finds there. HAVE_XKBCOMMON is the single optional library upstream
        # checks for without declaring: check_val '' XKBCOMMON ... runs, finds
        # this machine's xkbcommon through pkg-config, and switches a feature on
        # that the build then cannot compile. Declaring it `auto` changes no
        # upstream behaviour - it is the default every sibling already has - and
        # it is what makes --disable-xkbcommon an option rather than an error.
        #
        # The inserted line is one line on purpose. qb.params.sh reads this file
        # by cutting each line at its `=` and evaluating what is left, so a
        # continuation line - even a comment one - is read as a variable
        # assignment and configure dies before it starts.
        "qb/config.params.sh",
        "HAVE_UDEV=auto             # Udev/Evdev gamepad support\n",
        "HAVE_UDEV=auto             # Udev/Evdev gamepad support\n"
        "HAVE_XKBCOMMON=auto        # xkbcommon; declared here because "
        "check_val never declares it upstream\n",
        "HAVE_XKBCOMMON=auto",
    ),
    (
        # RetroArch guards its own C entry point with `#ifndef HAVE_MAIN`, and
        # HAVE_MAIN turns out to mean something other than "the platform has a
        # main". The comment above rarch_main says it: with HAVE_MAIN undefined,
        # rarch_main *is* the program - it initialises, then runs the main loop,
        # and does not return until the frontend quits. With HAVE_MAIN defined,
        # rarch_main only initialises and returns immediately.
        #
        # This port defined HAVE_MAIN to get rid of a duplicate `main` symbol,
        # which is what the flag looks like it is for. The result was a title that
        # initialised every driver correctly - our video driver included, its
        # display open at 1920x1080, per /app0/trace.txt on the console - and then
        # exited zero without ever drawing a frame or printing a word. The loop
        # had been compiled out.
        #
        # The duplicate goes away by the change below instead: upstream's `main`
        # is renamed, HAVE_MAIN stays undefined, and rarch_main keeps the loop.
        # src/main.cpp's `main` is then the only definition, and the SDK's _start
        # reaches a real entry point rather than a wrapper that returns at once.
        "retroarch.c",
        "int main(int argc, char *argv[])\n{\n   return rarch_main(argc, argv, NULL);\n}",
        "/* Renamed by this port (patches/series, 0003). The SDK's _start calls\n"
        " * `main`, which src/main.cpp supplies, so leaving this one named `main`\n"
        " * would be two definitions of one symbol. It is kept, and stays callable,\n"
        " * so that upstream's intended entry remains visible here. */\n"
        "int rarch_main_entry(int argc, char *argv[])\n{\n"
        "   return rarch_main(argc, argv, NULL);\n}",
        "int rarch_main_entry(int argc, char *argv[])",
    ),
    (
        # This project's input driver, declared beside the others so that
        # input/input_driver.c can name it in input_drivers[]. The implementation
        # is src/input_ps5.cpp, not a file in this tree, for the same reason
        # video_ps5 is: the console's pad calls and the driver's shape are this
        # project's, and upstream stays upstream.
        #
        # The initial implementation reported pad state directly here. Patch
        # 0071 now adds the paired native joypad backend, which supplies raw
        # binding capture and mapped input; this input interface stays registered.
        "input/input_driver.h",
        "extern input_driver_t input_ps4;",
        "extern input_driver_t input_ps4;\n"
        "/* Supplied by this project (src/input_ps5.cpp): reads the console's pad\n"
        " * through scePadRead and reports it as a RetroPad. Declared with C linkage\n"
        " * because RetroArch's own sources are C. */\n"
        "extern input_driver_t input_ps5;",
        "extern input_driver_t input_ps5;",
    ),
    (
        # And listed in the table itself, before input_null so that a
        # configuration naming \"ps5\" finds it and the null driver stays the last
        # entry, which is what terminates the array.
        "input/input_driver.c",
        "   &input_null,\n   NULL,\n};",
        "   &input_ps5,\n   &input_null,\n   NULL,\n};",
        "   &input_ps5,",
    ),
    (
        # Vulkan is linked, not loaded: a PS5 title cannot dlopen a driver.
        #
        # RetroArch obtains every Vulkan entry point through one symbol,
        # vkGetInstanceProcAddr, which it fetches by dlopen of "libvulkan.so.1".
        # That cannot work in a title on this console, and ../PS5_Vulkan measured
        # it rather than assumed it - their e2-module runner asked the console's
        # loader directly:
        #
        #   sceKernelLoadStartModule("/app0/libvulkan.so.1")  -> 0x80020008 (ENOEXEC)
        #   the same for FSELF-wrapped, soname-as-path and control variants
        #   "libvulkan.so.1" bare                             -> 0x80020002 (ENOENT)
        #   dlopen answered NULL for all twelve candidates, including modules the
        #   process already holds, with dlerror() NULL every time
        #   sceKernelDlsym -> ESRCH for every name on the modules that do load
        #
        # A linker-produced .so is not a PS5 module image (it has no SCE module
        # parameters and no export table), so no name or path fixes this. The
        # route that is proven on this console is the one their runner title uses:
        # link libps5vk.ps5.a and call the entry point as an ordinary symbol.
        #
        # So the loader path is replaced by a direct reference. Everything below
        # this point in RetroArch is unchanged: it still goes through
        # vkGetInstanceProcAddr for every other entry point, which is exactly what
        # the driver expects (it exports the loader-facing spelling).
        #
        # The archives this needs are the sibling's released set, named in
        # tools/build-title.sh: libps5vk.ps5.a, Mesa's libvk_runtime.ps5.a, the
        # shader compiler libpsbc_driver.ps5.a, and libpsbc_support.ps5.a.
        "gfx/common/vulkan_common.c",
        "#else\n"
        "      vulkan_library = dylib_load(\"libvulkan.so.1\");\n"
        "      if (!vulkan_library)\n"
        "         vulkan_library = dylib_load(\"libvulkan.so\");\n"
        "#endif\n"
        "   }\n"
        "\n"
        "   if (!vulkan_library)\n"
        "   {\n"
        "      RARCH_ERR(\"[Vulkan] Failed to open Vulkan loader.\\n\");\n"
        "      return false;\n"
        "   }\n"
        "\n"
        "   RARCH_LOG(\"[Vulkan] Vulkan dynamic library loaded.\\n\");\n"
        "\n"
        "   GetInstanceProcAddr =\n"
        "      (PFN_vkGetInstanceProcAddr)dylib_proc(vulkan_library, \"vkGetInstanceProcAddr\");\n",
        "#else\n"
        "      vulkan_library = dylib_load(\"libvulkan.so.1\");\n"
        "      if (!vulkan_library)\n"
        "         vulkan_library = dylib_load(\"libvulkan.so\");\n"
        "#endif\n"
        "   }\n"
        "\n"
        "   /* Changed by this port (patches/series, 0012): this console refuses a\n"
        "    * title's dlopen, so the entry point is linked, not loaded. It is the\n"
        "    * same symbol the loader path looked up, taken from ../PS5_Vulkan's\n"
        "    * libps5vk.ps5.a, which tools/build-title.sh links into the title. The\n"
        "    * declaration is local because the Vulkan headers RetroArch carries\n"
        "    * declare the PFN_ type but not the function. */\n"
        "   extern VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL\n"
        "      vkGetInstanceProcAddr(VkInstance instance, const char *pName);\n"
        "   GetInstanceProcAddr =\n"
        "      (PFN_vkGetInstanceProcAddr)vkGetInstanceProcAddr;\n",
        "(PFN_vkGetInstanceProcAddr)vkGetInstanceProcAddr",
    ),
    (
        # A core that has not loaded registers no controller-port callback, and
        # upstream calls it anyway.
        #
        # This is a real latent fault, found while chasing the config-path crash
        # and kept because it is the same shape as the joypad guard below:
        # `core_set_controller_port_device` guards its own `pad` argument and never
        # guards the callback it exists to call. `dynamic_dummy.c` only survives it
        # because it happens to define an empty stub, so a build whose dummy core
        # does not would jump to address zero - which is exactly what the console
        # reported for the crash this was found during:
        # `page fault (user read instruction, page not present)`, `rip: 0`.
        #
        # It did NOT turn out to be that crash: with this guard compiled in
        # (verified in the object as `test %rax,%rax; je` before `call *%rax`) the
        # title still dies with a byte-identical register dump. The change is kept
        # because the missing check is real, not because it fixed that.
        "runloop.c",
        "   runloop_st->current_core.retro_set_controller_port_device(pad->port, pad->device);\n",
        "   /* Guarded by this port (patches/series, 0007): a core that has not loaded\n"
        "    * registers no callbacks, so this member can be NULL. */\n"
        "   if (!runloop_st->current_core.retro_set_controller_port_device)\n"
        "      return false;\n"
        "   runloop_st->current_core.retro_set_controller_port_device(pad->port, pad->device);\n",
        "registers no callbacks, so this member can be NULL",
    ),
    (
        # Select is not initialise: the input driver was named and never wrapped.
        #
        # `input_driver_find_driver` runs during driver pre-initialisation and only
        # *selects* a driver into `input_driver_st.current_driver`; initialisation is
        # `input_driver_init_wrap`, whose only call on this path is the tail of
        # `video_driver_init_input`. That function returns early when
        # `current_driver` is already set, and upstream intends that early return
        # for a *video* driver that pre-initialised an input driver of its own.
        #
        # What actually happens here is different, and `tmp` is the tell. Measured:
        #
        #   probe INV: entered tmp=ba41e0 *input=ba41e0 configured="ps5"
        #   probe INV: after-clear *input=ba41e0
        #
        # `tmp` is not a driver the video driver supplied - `video_driver_init_internal`
        # sets `tmp = input_state_get_ptr()->current_driver` *before* calling
        # `video_driver_find_driver`, so after the pre-initialisation pass selected a
        # driver, `tmp` is that same selection. The early return then fires and the
        # wrap never runs; the wrap at the top of the function only stores `tmp` and
        # does not initialise anything either. The result, measured from the video
        # driver's own init: `*input` non-NULL and `*input_data` NULL - a driver with
        # no state, every button read answering 0.
        #
        # So the selection is discarded when it is the same pointer as `tmp`, which
        # means no video driver supplied a *different* driver, and the code below
        # re-selects from the settings and then initialises it. A video driver that
        # really did pre-initialise one passes a pointer that differs from the
        # selected driver, or passes data, and is left alone.
        "input/input_driver.c",
        "   void              *new_data    = NULL;\n"
        "   input_driver_t         **input = &input_driver_st.current_driver;\n",
        "   void              *new_data    = NULL;\n"
        "   input_driver_t         **input = &input_driver_st.current_driver;\n"
        "   /* Changed by this port (patches/series, 0009): a driver selected during\n"
        "    * pre-initialisation is not an initialised one. When the selected driver is\n"
        "    * the same pointer the caller carried in, no video driver supplied one, so\n"
        "    * the selection is discarded and the code below re-selects and wraps it. */\n"
        "   if (*input != NULL && *input == tmp)\n"
        "      *input = NULL;\n",
        "the same pointer the caller carried in",
    ),
    (
        # The graphics backend this project consumes is the compiled video default.
        #
        # With the config path parked, the frontend runs on compiled defaults, and
        # the video driver's default resolves to "ext" - so the Vulkan driver was
        # never even attempted, whatever the config said. Naming it here is what
        # makes the Vulkan path reachable without a readable config, and it is the
        # same mechanism `0009` uses for the input driver.
        #
        # RetroArch obtains the Vulkan entry points by dlopen of "libvulkan.so.1"
        # (gfx/common/vulkan_common.c), which ../PS5_Vulkan now delivers as a
        # console shared object; the frontend's driver presents through
        # VK_KHR_display, which that driver implements. If the object is not beside
        # the title the load fails, and with --log-file the frontend now says so
        # instead of exiting silently.
        "configuration.c",
        "      case VIDEO_VULKAN:\n         return \"vulkan\";",
        "      case VIDEO_VULKAN:\n"
        "          /* Named by this port (patches/series, 0010). This is the arm that\n"
        "           * fires: VIDEO_DEFAULT_DRIVER resolves to VIDEO_VULKAN because this\n"
        "           * build has HAVE_VULKAN, so the function returns here and never\n"
        "           * reaches the VIDEO_NULL arm an earlier version of this patch\n"
        "           * edited - which is why changing that arm had no effect.\n"
        "           *\n"
        "           * It names this project's own driver because that is the one that\n"
        "           * can finish: video_ps5's display path is proven as far as the\n"
        "           * buffer (bands read back 0 of 2,073,600 pixels wrong, flip\n"
        "           * accepted, the display reporting marker 1), while the linked\n"
        "           * libps5vk refuses this frontend's draws and a refusal ends\n"
        "           * recording with an error at vkEndCommandBuffer, so those command\n"
        "           * buffers never submit.\n"
        "           *\n"
        "           * The config cannot make this choice: content loading rebuilds\n"
        "           * argv and drops the title's `-c`, so /app0/retroarch.cfg's\n"
        "           * video_driver is never parsed. */\n"
        "          return \"vulkan\";",
        "return \"vulkan\";",
    ),
    (
        # The console's pad is this build's input driver, so it is also the
        # compiled default.
        #
        # This is what makes the driver reachable without a config file. There is
        # no config file at runtime yet: content loading rebuilds argv and drops
        # the title's `-c`, and the fix for that is parked because reading the
        # config still crashes the launch. With no config read, the whole input
        # path runs on compiled defaults - `probe init_input entered: *input=ba4dc0
        # configured="null" joypad="null"` is that measurement - so naming this
        # project's driver here is what puts it in `input_drivers[]`'s place before
        # the frontend initialises anything.
        #
        # One line, and reversible: when the config file is readable again the
        # config's own `input_driver` wins at parse time and this default stops
        # mattering, at which point it can be deleted.
        "configuration.c",
        "      case INPUT_NULL:\n          break;",
        "      case INPUT_NULL:\n"
        "          /* Named by this port (patches/series, 0008): the console's own pad\n"
        "           * driver is the only input driver this build can run, and with no\n"
        "           * config file read it has to come from the compiled default. */\n"
        "          return \"ps5\";",
        "the only input driver this build can run",
    ),
    (
        # A null joypad driver is a normal state on this console, and upstream
        # dereferences it. input_driver_collect_system_input calls
        # input_joypad_analog_axis with input_st->primary_joypad, which is NULL
        # when no joypad driver initialised - and none does here, because every
        # joypad driver upstream ships (udev, linuxraw, SDL, XInput, dinput) needs
        # a library or a header this SDK does not carry. input_joypad_analog_axis
        # then reads drv->axis with no check on drv at all: the function guards
        # every `axis` member against AXIS_NONE and never guards the struct.
        #
        # The failure this caused is worth recording, because nothing about it
        # looked like a null pointer. The title started, the display opened, the
        # first frame was presented - and then it died on the second pass through
        # the runloop with SIGSEGV, fault address 0x18. Probe by probe it came down
        # to this call, which only runs when the menu is alive, which is why pass
        # one survived: menu_is_alive is set after the first frame.
        #
        # The guard below is the smallest change that makes the function honest
        # about a driver it does not have: with no driver there is no axis to read,
        # which is exactly what the function's own `res = 0` means.
        "input/input_driver.c",
        "static int16_t input_joypad_analog_axis(\n"
        "      unsigned input_analog_dpad_mode,\n"
        "      float input_analog_deadzone,\n"
        "      float input_analog_sensitivity,\n"
        "      const input_device_driver_t *drv,\n"
        "      rarch_joypad_info_t *joypad_info,\n"
        "      unsigned idx,\n"
        "      unsigned ident,\n"
        "      const struct retro_keybind *binds)\n"
        "{\n",
        "static int16_t input_joypad_analog_axis(\n"
        "      unsigned input_analog_dpad_mode,\n"
        "      float input_analog_deadzone,\n"
        "      float input_analog_sensitivity,\n"
        "      const input_device_driver_t *drv,\n"
        "      rarch_joypad_info_t *joypad_info,\n"
        "      unsigned idx,\n"
        "      unsigned ident,\n"
        "      const struct retro_keybind *binds)\n"
        "{\n"
        "   /* Added by this port (patches/series, 0004). Every member read below is\n"
        "    * reached through this driver; with no joypad driver present, which is\n"
        "    * this console's normal state, there is no axis to read and the rest of\n"
        "    * the function's answer is the zero it already starts with. */\n"
        "   if (drv == NULL)\n"
        "      return 0;\n",
        "if (drv == NULL)\n      return 0;",
    ),
    (
        # A swapchain may only use the usage bits its surface advertises, and
        # this one advertises VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT alone: the
        # console's VideoOut path has only been proven with swapchain images used
        # as render targets. RetroArch asks for four bits unconditionally, which
        # every desktop driver tolerates. ../PS5_Vulkan checks it in
        # ps5vk_CreateSwapchainKHR, and that project's asserts are live - so the
        # title aborted there, inside vulkan_init, before a single frame, with no
        # message anywhere: the console reported only `abort is called(system)`
        # and a frame that a link map resolves to that function. Clamping to what
        # the surface reports changes nothing on a desktop driver.
        "gfx/common/vulkan_common.c",
        "   info.imageUsage             =  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT\n"
        "                                | VK_IMAGE_USAGE_TRANSFER_SRC_BIT\n"
        "                                | VK_IMAGE_USAGE_TRANSFER_DST_BIT\n"
        "                                | VK_IMAGE_USAGE_SAMPLED_BIT;\n",
        "   info.imageUsage             =  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT\n"
        "                                | VK_IMAGE_USAGE_TRANSFER_SRC_BIT\n"
        "                                | VK_IMAGE_USAGE_TRANSFER_DST_BIT\n"
        "                                | VK_IMAGE_USAGE_SAMPLED_BIT;\n"
        "   /* Added by this port (patches/series, 0013): a swapchain may only use\n"
        "    * the usage bits its surface advertises, and this surface advertises\n"
        "    * VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT alone. Asking for the other\n"
        "    * three is what fails ../PS5_Vulkan's own check inside\n"
        "    * ps5vk_CreateSwapchainKHR and aborts the title. */\n"
        "   info.imageUsage            &= surface_properties.supportedUsageFlags;\n",
        "info.imageUsage            &= surface_properties.supportedUsageFlags;",
    ),
    (
        # ../PS5_Vulkan enforces its format table with live asserts inside
        # ps5vk_CreateImage, so a request the device cannot honour aborts the
        # title instead of returning VK_ERROR_FORMAT_NOT_SUPPORTED. Its
        # B8G8R8A8_UNORM entry advertises VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
        # alone, and RetroArch creates its 4x4 blank texture - and its 1x1 default
        # texture - in that format with SAMPLED | TRANSFER_DST | TRANSFER_SRC.
        # Both are one uniform colour, so neither cares which 32-bit format
        # carries it, and R8G8B8A8_UNORM is the one this driver reports as
        # sampled. The usage mask is the specification's own rule; the format
        # substitution is what keeps a texture a texture.
        "gfx/drivers/vulkan.c",
        "   if (     (type != VULKAN_TEXTURE_STAGING)\n"
        "         && (type != VULKAN_TEXTURE_READBACK))\n",
        "   /* Added by this port (patches/series, 0014): a create-info may only ask\n"
        "    * for usage bits the format advertises, and a texture that cannot be\n"
        "    * sampled is not a texture. Masking handles the first; the second is\n"
        "    * handled by asking for R8G8B8A8_UNORM, which this driver reports as\n"
        "    * sampled and which is the same image for a uniform colour. Only image\n"
        "    * types are touched: STAGING and READBACK are buffers here. */\n"
        "   if (     (type != VULKAN_TEXTURE_STAGING)\n"
        "         && (type != VULKAN_TEXTURE_READBACK))\n"
        "   {\n"
        "      VkFormatProperties format_properties;\n"
        "      VkImageUsageFlags allowed = 0;\n"
        "      VkFormat request          = info.format;\n"
        "\n"
        "      memset(&format_properties, 0, sizeof(format_properties));\n"
        "      vkGetPhysicalDeviceFormatProperties(vk->context->gpu,\n"
        "            request, &format_properties);\n"
        "      if (   !(format_properties.optimalTilingFeatures\n"
        "                  & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)\n"
        "          && request != VK_FORMAT_R8G8B8A8_UNORM)\n"
        "      {\n"
        "         request = VK_FORMAT_R8G8B8A8_UNORM;\n"
        "         memset(&format_properties, 0, sizeof(format_properties));\n"
        "         vkGetPhysicalDeviceFormatProperties(vk->context->gpu,\n"
        "               request, &format_properties);\n"
        "      }\n"
        "      if (format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)\n"
        "         allowed |= VK_IMAGE_USAGE_SAMPLED_BIT;\n"
        "      if (format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT)\n"
        "         allowed |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;\n"
        "      if (format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_DST_BIT)\n"
        "         allowed |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;\n"
        "      if (format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)\n"
        "         allowed |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;\n"
        "      if (format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)\n"
        "         allowed |= VK_IMAGE_USAGE_STORAGE_BIT;\n"
        "      info.format = request;\n"
        "      info.usage &= allowed;\n"
        "      if (request != format)\n"
        "      {\n"
        "         /* The view, the staging buffer's pitch and tex.format are all\n"
        "          * built from this local, so a substituted format has to reach\n"
        "          * all four or the view is created for a format the image is not. */\n"
        "         format           = request;\n"
        "         buffer_width     = (width * vulkan_format_to_bpp(format) + 3u) & ~3u;\n"
        "         buffer_info.size = buffer_width * height;\n"
        "      }\n"
        "   }\n"
        "\n"
        "   if (     (type != VULKAN_TEXTURE_STAGING)\n"
        "         && (type != VULKAN_TEXTURE_READBACK))\n",
        "if (request != format)",
    ),
    (
        # 0014 substitutes R8G8B8A8_UNORM for a format this driver cannot sample,
        # and vulkan_format_to_bpp() did not know that format: the lookup answered
        # 0, the staging buffer's size became 0, and ../PS5_Vulkan's vk_buffer_init
        # aborted on `size > 0`. One missing case in a switch cost a console round.
        # The format is the same 32 bits per pixel as the BGRA it replaces.
        "gfx/drivers/vulkan.c",
        "      case VK_FORMAT_R8_UNORM:\n"
        "         return 1;\n"
        "      default: /* Unknown format */\n",
        "      case VK_FORMAT_R8_UNORM:\n"
        "         return 1;\n"
        "      /* Added by this port (patches/series, 0015): the format 0014 is\n"
        "       * substituted with when a format cannot be sampled. Without this\n"
        "       * case the staging buffer sized from it came out zero bytes long,\n"
        "       * and the driver aborted inside vk_buffer_init. */\n"
        "      case VK_FORMAT_R8G8B8A8_UNORM:\n"
        "         return 4;\n"
        "      default: /* Unknown format */\n",
        "patches/series, 0015",
    ),
    (
        # ../PS5_Vulkan's pipeline check accepts only VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
        # (and Mesa's meta rectangle list), and its draw path refuses a non-indexed
        # draw whose first vertex is not zero. RetroArch's Vulkan filter chain draws
        # its two quads as a four-vertex triangle strip starting at vertex 0, so its
        # pipeline is refused with VK_ERROR_UNKNOWN and no log line - that project
        # compiles its own Mesa log out. The three edits below answer both
        # conditions: the quads become six explicit vertices each in list order, the
        # pipeline says triangle list, the final pass reads its quad at the new
        # offset, and the second triangle is drawn through the binding's own offset
        # because a first vertex other than zero is refused.
        "gfx/drivers_shader/shader_vulkan.cpp",
        "   input_assembly.primitiveRestartEnable        = VK_FALSE;\n",
        "   /* Added by this port (patches/series, 0016): this driver takes triangle\n"
        "    * lists only. */\n"
        "   input_assembly.topology                      = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;\n"
        "   input_assembly.primitiveRestartEnable        = VK_FALSE;\n",
        "VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;\n"
        "   input_assembly.primitiveRestartEnable",
    ),
    (
        "gfx/drivers_shader/shader_vulkan.cpp",
        "   vbo->unmap();\n",
        "   /* Added by this port (patches/series, 0016): the strip above cannot carry a\n"
        "    * list's six vertices, so the buffer is rebuilt in list order: each quad as\n"
        "    * two triangles, the second one reachable at a three-vertex offset because a\n"
        "    * non-indexed draw may not start anywhere but vertex zero. */\n"
        "   vbo.reset();\n"
        "   {\n"
        "      static const float ps5_triangle_list[] = {\n"
        "         /* Offscreen: (-1,-1) (-1,+1) (1,-1), then (1,-1) (-1,+1) (1,+1) */\n"
        "         -1.0f, -1.0f, 0.0f, 0.0f,\n"
        "         -1.0f, +1.0f, 0.0f, 1.0f,\n"
        "         +1.0f, -1.0f, 1.0f, 0.0f,\n"
        "         +1.0f, -1.0f, 1.0f, 0.0f,\n"
        "         -1.0f, +1.0f, 0.0f, 1.0f,\n"
        "         +1.0f, +1.0f, 1.0f, 1.0f,\n"
        "         /* Final: (0,0) (0,1) (1,0), then (1,0) (0,1) (1,1) */\n"
        "          0.0f,  0.0f, 0.0f, 0.0f,\n"
        "          0.0f, +1.0f, 0.0f, 1.0f,\n"
        "         +1.0f,  0.0f, 1.0f, 0.0f,\n"
        "         +1.0f,  0.0f, 1.0f, 0.0f,\n"
        "          0.0f, +1.0f, 0.0f, 1.0f,\n"
        "         +1.0f, +1.0f, 1.0f, 1.0f,\n"
        "      };\n"
        "      vbo = std::unique_ptr<Buffer>(new Buffer(device, memory_properties,\n"
        "               sizeof(ps5_triangle_list), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT));\n"
        "      ptr = vbo->map();\n"
        "      memcpy(ptr, ps5_triangle_list, sizeof(ps5_triangle_list));\n"
        "      vbo->unmap();\n"
        "   }\n"
        "   vbo->unmap();\n",
        "ps5_triangle_list",
    ),
    (
        "gfx/drivers_shader/shader_vulkan.cpp",
        "      vkCmdBindVertexBuffers(cmd, 0, 1,\n",
        "      /* Added by this port (patches/series, 0016): the final quad is the second\n"
        "       * of the two list quads, so it starts three vertices later than it did as a\n"
        "       * strip. */\n"
        "      if (final_pass)\n"
        "         offset = 24 * sizeof(float);\n"
        "      vkCmdBindVertexBuffers(cmd, 0, 1,\n",
        "offset = 24 * sizeof(float);",
    ),
    (
        "gfx/drivers_shader/shader_vulkan.cpp",
        "   vkCmdDraw(cmd, 4, 1, 0, 0);\n",
        "   /* Added by this port (patches/series, 0016): a triangle list uses three of\n"
        "    * these four vertices, and the quad's other triangle is drawn here through the\n"
        "    * binding's offset - naming it as a first vertex would be refused. */\n"
        "   {\n"
        "      const VkDeviceSize base   = final_pass ? 24 * sizeof(float) : 0;\n"
        "      const VkDeviceSize second = base + 3 * 4 * sizeof(float);\n"
        "      VkBuffer buffer           = common->vbo->get_buffer();\n"
        "      vkCmdBindVertexBuffers(cmd, 0, 1, &buffer, &second);\n"
        "      vkCmdDraw(cmd, 3, 1, 0, 0);\n"
        "      vkCmdBindVertexBuffers(cmd, 0, 1, &buffer, &base);\n"
        "   }\n"
        "   vkCmdDraw(cmd, 4, 1, 0, 0);\n",
        "vkCmdDraw(cmd, 3, 1, 0, 0);",
    ),

    (
        # This driver refuses a sampler whose address mode is not clamp-to-edge, and
        # a refused vkCreateSampler leaves its output handle untouched. RetroArch's
        # CommonResources destroys every handle that is not VK_NULL_HANDLE, so the
        # sixteen samplers this driver refuses are destroyed as if they were real
        # samplers and the driver asserts on the first one. Zeroing the array first
        # makes a refused creation the NULL it is.
        "gfx/drivers_shader/shader_vulkan.cpp",
        "   info.unnormalizedCoordinates = VK_FALSE;\n",
        "   info.unnormalizedCoordinates = VK_FALSE;\n"
        "   /* Added by this port (patches/series, 0017): ../PS5_Vulkan supports the\n"
        "    * clamp-to-edge samplers alone and returns an error for the rest, leaving\n"
        "    * the handle it was given untouched - so the array is cleared before it is\n"
        "    * filled, and a refused creation stays VK_NULL_HANDLE for the destructor. */\n"
        "   memset(samplers, 0, sizeof(samplers));\n",
        "memset(samplers, 0, sizeof(samplers));",
    ),

    (
        # A console title has no terminal and no SIGTERM sender, and this console
        # delivers SIGINT/SIGTERM while a title starts. RetroArch's khr_display
        # context read that state as "the user asked to quit", so the runloop ended
        # on its first iteration: the trace showed a successful vulkan_init, a
        # created pipeline, and rarch_main returning 0 without a single frame. The
        # platform's own lifecycle ends the process, so this context stops asking
        # the runloop to.
        "gfx/drivers_context/khr_display_ctx.c",
        "}\n"
        "\n"
        "static bool gfx_ctx_khr_display_set_resize(void *data,\n",
        "   /* Added by this port (patches/series, 0018): the signal-handler state is\n"
        "    * not a quit request on a console. */\n"
        "   *quit                    = false;\n"
        "}\n"
        "\n"
        "static bool gfx_ctx_khr_display_set_resize(void *data,\n",
        "the signal-handler state is",
    ),
    (
        # Every refusal ../PS5_Vulkan states by name goes through Mesa's vk_log,
        # which drops the message unless the instance has debug logging on or a
        # debug callback installed - and its `enable_debug_logging` is never
        # assigned anywhere in that tree, so a console run saw only a bare
        # VK_ERROR_UNKNOWN. A messenger created here puts those messages on stderr,
        # which src/main.cpp points at the trace. The three edits are the callback,
        # the extension request, and the messenger itself.
        "gfx/common/vulkan_common.c",
        "static VkInstance vulkan_context_create_instance_wrapper(void *opaque, const VkInstanceCreateInfo *create_info)\n",
        "/* Added by this port (patches/series, 0019): the driver's refusals, into the\n"
        " * trace. See the extension and messenger edits below. */\n"
        "static VKAPI_ATTR VkBool32 VKAPI_CALL ps5_vulkan_debug_callback(\n"
        "      VkDebugUtilsMessageSeverityFlagBitsEXT severity,\n"
        "      VkDebugUtilsMessageTypeFlagsEXT types,\n"
        "      const VkDebugUtilsMessengerCallbackDataEXT *data, void *user_data)\n"
        "{\n"
        "   (void)severity;\n"
        "   (void)types;\n"
        "   (void)user_data;\n"
        "   if (data && data->pMessage)\n"
        "      fprintf(stderr, \"vulkan: %s\\n\", data->pMessage);\n"
        "   return VK_FALSE;\n"
        "}\n"
        "\n"
        "static VkInstance vulkan_context_create_instance_wrapper(void *opaque, const VkInstanceCreateInfo *create_info)\n",
        "ps5_vulkan_debug_callback",
    ),
    (
        "gfx/common/vulkan_common.c",
        "#ifdef VULKAN_HDR_SWAPCHAIN\n"
        "   /* Check if HDR colorspace extension was enabled */\n",
        "   /* Added by this port (patches/series, 0019): VK_EXT_debug_utils, when the\n"
        "    * driver reports it, so the messenger below can be created. The list is\n"
        "    * reallocated rather than appended in place: the buffer vulkan_find_\n"
        "    * instance_extensions filled was sized for the extensions it knows. */\n"
        "   {\n"
        "      uint32_t probe_count = 0;\n"
        "      VkExtensionProperties probe_list[256];\n"
        "      if (   vkEnumerateInstanceExtensionProperties(NULL, &probe_count, NULL) == VK_SUCCESS\n"
        "          && probe_count > 0 && probe_count <= ARRAY_SIZE(probe_list)\n"
        "          && vkEnumerateInstanceExtensionProperties(NULL, &probe_count, probe_list) == VK_SUCCESS)\n"
        "      {\n"
        "         uint32_t probe_index;\n"
        "         for (probe_index = 0; probe_index < probe_count; probe_index++)\n"
        "         {\n"
        "            if (string_is_equal(probe_list[probe_index].extensionName, \"VK_EXT_debug_utils\"))\n"
        "            {\n"
        "               const char **bigger = (const char**)malloc((info.enabledExtensionCount + 1)\n"
        "                     * sizeof(const char*));\n"
        "               if (bigger)\n"
        "               {\n"
        "                  memcpy((void*)bigger, info.ppEnabledExtensionNames,\n"
        "                        info.enabledExtensionCount * sizeof(const char*));\n"
        "                  bigger[info.enabledExtensionCount++] = \"VK_EXT_debug_utils\";\n"
        "                  instance_extensions               = bigger;\n"
        "                  info.ppEnabledExtensionNames      = instance_extensions;\n"
        "               }\n"
        "               break;\n"
        "            }\n"
        "         }\n"
        "      }\n"
        "   }\n"
        "\n"
        "#ifdef VULKAN_HDR_SWAPCHAIN\n"
        "   /* Check if HDR colorspace extension was enabled */\n",
        "VK_EXT_debug_utils, when the",
    ),
    (
        "gfx/common/vulkan_common.c",
        "end:\n"
        "   free((void*)instance_extensions);\n"
        "   free((void*)instance_layers);\n"
        "   return instance;\n",
        "   /* Added by this port (patches/series, 0019): the messenger. Its callback\n"
        "    * prints through stderr, which is the trace file, so every refusal the\n"
        "    * driver states by name is readable on this console. Harmless when the\n"
        "    * driver has no such entry point. */\n"
        "   if (instance != VK_NULL_HANDLE)\n"
        "   {\n"
        "      static VkDebugUtilsMessengerEXT ps5_debug_messenger;\n"
        "      PFN_vkGetInstanceProcAddr ps5_get_proc =\n"
        "         vulkan_symbol_wrapper_instance_proc_addr();\n"
        "      PFN_vkCreateDebugUtilsMessengerEXT ps5_create_messenger =\n"
        "         ps5_get_proc\n"
        "            ? (PFN_vkCreateDebugUtilsMessengerEXT)ps5_get_proc(\n"
        "                  instance, \"vkCreateDebugUtilsMessengerEXT\")\n"
        "            : NULL;\n"
        "      if (ps5_create_messenger)\n"
        "      {\n"
        "         VkDebugUtilsMessengerCreateInfoEXT ps5_messenger_info;\n"
        "         memset(&ps5_messenger_info, 0, sizeof(ps5_messenger_info));\n"
        "         ps5_messenger_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;\n"
        "         ps5_messenger_info.messageSeverity =\n"
        "              VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT\n"
        "            | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT\n"
        "            | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;\n"
        "         ps5_messenger_info.messageType =\n"
        "              VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT\n"
        "            | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT\n"
        "            | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;\n"
        "         ps5_messenger_info.pfnUserCallback = ps5_vulkan_debug_callback;\n"
        "         (void)ps5_create_messenger(instance, &ps5_messenger_info, NULL,\n"
        "               &ps5_debug_messenger);\n"
        "      }\n"
        "   }\n"
        "\n"
        "end:\n"
        "   free((void*)instance_extensions);\n"
        "   free((void*)instance_layers);\n"
        "   return instance;\n",
        "static VkDebugUtilsMessengerEXT ps5_debug_messenger;",
    ),
    (
        # RetroArch remaps RGB565 textures to RGBA8888 because some hardware cannot
        # sample RGB565, and uploads them through a compute shader that needs a
        # storage-image descriptor. ../PS5_Vulkan reports RGB565 as sampleable, and
        # has no descriptor table entry for a storage image at all - its own message
        # is "set 0 binding 3: descriptor type 3 has no proven table entry" - so the
        # compute pipeline is never created, the dispatch is refused, the command
        # buffer carries the error and vkQueueSubmit asserts. Keeping the format the
        # driver can sample removes the compute path from the frame entirely.
        "gfx/drivers/vulkan.c",
        "   /* Compatibility concern. Some Apple hardware does not support rgb565.\n",
        "   /* Added by this port (patches/series, 0020): this driver samples the format\n"
        "    * it was given. A remap here costs more than it buys: the compute upload it\n"
        "    * switches to needs a storage-image descriptor this driver does not have. */\n"
        "   {\n"
        "      VkFormatProperties remap_probe;\n"
        "      memset(&remap_probe, 0, sizeof(remap_probe));\n"
        "      vkGetPhysicalDeviceFormatProperties(vk->context->gpu, format, &remap_probe);\n"
        "      if (remap_probe.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)\n"
        "         remap_tex_fmt                  = format;\n"
        "   }\n"
        "\n"
        "   /* Compatibility concern. Some Apple hardware does not support rgb565.\n",
        "this driver samples the format",
    ),
    (
        # ../PS5_Vulkan refuses to sample an untiled image whose width is not a
        # whole number of 256-byte rows - "the descriptor's row pitch needs a runner
        # probe" - and RetroArch's two fallback textures are 4x4 and 1x1, whose rows
        # are 16 and 4 bytes padded to 256. Both are one uniform colour, so their
        # size is nothing a shader can read: 64 texels wide is whole rows, and the
        # first draw of the menu stops being refused.
        "gfx/drivers/vulkan.c",
        "}\n"
        "\n"
        "static void vulkan_deinit_static_resources(vk_t *vk)\n",
        "   /* Added by this port (patches/series, 0021): whole rows, one colour. */\n"
        "   {\n"
        "      static const uint32_t ps5_wide_blank[64] = {[0 ... 63] = 0xffffffffu};\n"
        "      vk->display.blank_texture = vulkan_create_texture(vk, NULL,\n"
        "            64, 1, VK_FORMAT_B8G8R8A8_UNORM,\n"
        "            ps5_wide_blank, NULL, VULKAN_TEXTURE_STATIC);\n"
        "   }\n"
        "}\n"
        "\n"
        "static void vulkan_deinit_static_resources(vk_t *vk)\n",
        "ps5_wide_blank",
    ),
    (
        "gfx/drivers/vulkan.c",
        "}\n"
        "\n"
        "static void vulkan_deinit_textures(vk_t *vk)\n",
        "   /* Added by this port (patches/series, 0021): whole rows, all zero. */\n"
        "   {\n"
        "      static const uint32_t ps5_wide_zero[64] = {0};\n"
        "      vk->default_texture = vulkan_create_texture(vk, NULL,\n"
        "            64, 1, VK_FORMAT_B8G8R8A8_UNORM,\n"
        "            ps5_wide_zero, NULL, VULKAN_TEXTURE_STATIC);\n"
        "   }\n"
        "}\n"
        "\n"
        "static void vulkan_deinit_textures(vk_t *vk)\n",
        "ps5_wide_zero",
    ),
    (
        # ../PS5_Vulkan samples only untiled images whose width is a whole number of
        # 256-byte rows: its own message is "samples a 4-texel-wide image whose rows
        # are padded to 256 bytes". RetroArch creates several small ones - a 4x4
        # blank, a 1x1 default, an 8x8 checkerboard, and whatever a core hands
        # load_texture - and the chain is handed the blank as its input on the first
        # frames, so the draw is refused. The image is widened to whole rows and the
        # logical width is left alone: the extra columns hold what the row padding
        # held anyway, which for a flat colour is nothing.
        "gfx/drivers/vulkan.c",
        "      if (request != format)\n",
        "      /* Added by this port (patches/series, 0022): whole 256-byte rows. */\n"
        "      if (info.extent.width % 64u != 0u)\n"
        "         info.extent.width = (info.extent.width + 63u) & ~63u;\n"
        "      if (request != format)\n",
        "whole 256-byte rows",
    ),
    (
        # This driver supports the clamp-to-edge samplers alone and leaves the handle
        # untouched for every other address mode, so sixteen of the twenty in this
        # matrix are VK_NULL_HANDLE. A NULL sampler is a refused descriptor write at
        # draw time - "set 0 binding 1: a combined image sampler write names no
        # sampler" - and a draw that refuses leaves the command buffer in error, which
        # is what vkQueueSubmit then asserts on. The clamp-to-edge entry of the same
        # filters stands in: the difference is how a texture edge is addressed, and a
        # driver that cannot repeat can still sample it.
        "gfx/drivers_shader/shader_vulkan.cpp",
        "}\n"
        "\n"
        "CommonResources::~CommonResources()\n",
        "   /* Added by this port (patches/series, 0023): see the note above this edit. */\n"
        "   {\n"
        "      unsigned ps5_i, ps5_j, ps5_k;\n"
        "      for (ps5_i = 0; ps5_i < GLSLANG_FILTER_CHAIN_COUNT; ps5_i++)\n"
        "         for (ps5_j = 0; ps5_j < GLSLANG_FILTER_CHAIN_COUNT; ps5_j++)\n"
        "            for (ps5_k = 0; ps5_k < GLSLANG_FILTER_CHAIN_ADDRESS_COUNT; ps5_k++)\n"
        "               if (samplers[ps5_i][ps5_j][ps5_k] == VK_NULL_HANDLE)\n"
        "                  samplers[ps5_i][ps5_j][ps5_k] =\n"
        "                     samplers[ps5_i][ps5_j][GLSLANG_FILTER_CHAIN_ADDRESS_CLAMP_TO_EDGE];\n"
        "   }\n"
        "}\n"
        "\n"
        "CommonResources::~CommonResources()\n",
        "patches/series, 0023",
    ),
    (
        # The display driver's own four samplers are created with an opaque-white
        # border, and ../PS5_Vulkan creates a sampler only for the transparent black
        # its texture canary ran: vkCreateSampler refuses every other colour by name
        # and leaves the handle untouched, so all four stay VK_NULL_HANDLE. A NULL
        # sampler is a refused descriptor write at draw time - "set 0 binding 1: a
        # combined image sampler write names no sampler" - and that refusal leaves the
        # frame's command buffer in error, which is the vkQueueSubmit assertion the run
        # ends on. A clamp-to-edge address mode never samples the border, so the colour
        # is invisible to the application: transparent black is the one the driver can
        # name.
        "gfx/drivers/vulkan.c",
        "   info.borderColor             = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;\n",
        "   /* Added by this port (patches/series, 0024): the border colour the driver's\n"
        "    * own sampler canary ran. A clamp-to-edge address mode reads no border, and\n"
        "    * ../PS5_Vulkan creates no other colour. */\n"
        "   info.borderColor             = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;\n",
        "patches/series, 0024",
    ),
    (
        # A sampler this driver did not create is a draw the driver refuses, and one
        # refused draw ends the frame. The block above repairs the four the display
        # driver asks for; this keeps any later refusal from reaching a draw as
        # VK_NULL_HANDLE at all, by standing the nearest sampler in for one that is
        # missing. The difference is a filter, not a dead frame.
        "gfx/drivers/vulkan.c",
        "   info.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR;\n"
        "   vkCreateSampler(vk->context->device,\n"
        "         &info, NULL, &vk->samplers.mipmap_linear);\n"
        "}\n",
        "   info.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR;\n"
        "   vkCreateSampler(vk->context->device,\n"
        "         &info, NULL, &vk->samplers.mipmap_linear);\n"
        "   /* Added by this port (patches/series, 0025): no draw names a sampler the\n"
        "    * driver refused to create. */\n"
        "   {\n"
        "      if (vk->samplers.linear         == VK_NULL_HANDLE)\n"
        "         vk->samplers.linear          = vk->samplers.nearest;\n"
        "      if (vk->samplers.mipmap_nearest == VK_NULL_HANDLE)\n"
        "         vk->samplers.mipmap_nearest  = vk->samplers.nearest;\n"
        "      if (vk->samplers.mipmap_linear  == VK_NULL_HANDLE)\n"
        "         vk->samplers.mipmap_linear   = vk->samplers.nearest;\n"
        "   }\n"
        "}\n",
        "patches/series, 0025",
    ),
    (
        # ../PS5_Vulkan builds the stage's set-0 table from the bindings the shader
        # compiler reports and refuses a table entry with no write: "set 0 binding 2 is
        # not bound or holds no write". This driver's display layout declares binding 2
        # as a combined image sampler as well as binding 1 - the HDR shaders read their
        # source there (vulkan_shaders/hdr.frag, and vulkan_run_hdr_pipeline writes it) -
        # and a display draw is refused for it even though no display shader reads it.
        # The same image the draw samples is written to it, which is what the layout
        # declares and what the HDR path already writes there.
        "gfx/drivers/vulkan.c",
        "      write.dstSet                    = set;\n"
        "      write.dstBinding                = 1;\n"
        "      write.descriptorCount           = 1;\n"
        "      write.descriptorType            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;\n"
        "      write.pImageInfo                = &image_info;\n"
        "      vkUpdateDescriptorSets(device, 1, &write, 0, NULL);\n"
        "   }\n"
        "}\n",
        "      write.dstSet                    = set;\n"
        "      write.dstBinding                = 1;\n"
        "      write.descriptorCount           = 1;\n"
        "      write.descriptorType            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;\n"
        "      write.pImageInfo                = &image_info;\n"
        "      vkUpdateDescriptorSets(device, 1, &write, 0, NULL);\n"
        "      /* Added by this port (patches/series, 0026): the same image at the layout's\n"
        "       * second sampler binding, which the console driver's draw requires a write\n"
        "       * for (../PS5_Vulkan, ps5vk_draw.c). */\n"
        "      write.dstBinding                = 2;\n"
        "      vkUpdateDescriptorSets(device, 1, &write, 0, NULL);\n"
        "   }\n"
        "}\n",
        "patches/series, 0026",
    ),
    (
        # ../PS5_Vulkan builds a binding's descriptor from the image the view names and
        # refuses a view whose component mapping is not the identity: "set 0 binding 1
        # samples a view whose component mapping is not the identity". This path takes
        # the B4G4R4A4 texture with a B/R view swizzle whenever the device reports that
        # format's tiling, which this one does, and the menu draw is then refused. The
        # 32-bit texture with the CPU conversion below carries the same pixels with no
        # swizzle at all - it is the branch a device without B4G4R4A4 tiling already
        # takes - so the format and the swizzle both go, and the conversion stays.
        "gfx/drivers/vulkan.c",
        "   if (!rgb32)\n"
        "   {\n"
        "       VkFormatProperties formatProperties;\n"
        "       vkGetPhysicalDeviceFormatProperties(vk->context->gpu, VK_FORMAT_B4G4R4A4_UNORM_PACK16, &formatProperties);\n"
        "       if (formatProperties.optimalTilingFeatures != 0)\n"
        "       {\n"
        "          static const VkComponentMapping br_swizzle =\n"
        "          {VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_A};\n"
        "          /* B4G4R4A4 must be supported, but R4G4B4A4 is optional,\n"
        "           * just apply the swizzle in the image view instead. */\n"
        "          fmt          = VK_FORMAT_B4G4R4A4_UNORM_PACK16;\n"
        "          ptr_swizzle  = &br_swizzle;\n"
        "       }\n"
        "       else\n"
        "           do_memcpy   = false;\n"
        "   }\n",
        "   if (!rgb32)\n"
        "   {\n"
        "      /* Added by this port (patches/series, 0027): ../PS5_Vulkan samples only\n"
        "       * views whose component mapping is the identity (ps5vk_draw.c,\n"
        "       * V0-formats), so the B4G4R4A4 texture and its B/R view swizzle cannot be\n"
        "       * sampled: the draw is refused. The 32-bit texture and the CPU conversion\n"
        "       * below carry the same pixels with no swizzle, which is the branch a device\n"
        "       * without B4G4R4A4 tiling already takes. The format is named rather than\n"
        "       * left at B8G8R8A8 because the staging texture keeps the format it is\n"
        "       * created with: ../PS5_Vulkan's B8G8R8A8 entry is not a sampled one, so the\n"
        "       * image is substituted to R8G8B8A8 and a staging texture in B8G8R8A8 would\n"
        "       * make the two formats differ - which takes the compute path, whose\n"
        "       * storage-image descriptor the driver has no entry for. */\n"
        "      do_memcpy   = false;\n"
        "      fmt         = VK_FORMAT_R8G8B8A8_UNORM;\n"
        "   }\n",
        "patches/series, 0027",
    ),
    (
        # The conversion above writes byte 0 from the source's high nibble, because the
        # texture it was written for is B8G8R8A8. The texture this port asks for is
        # R8G8B8A8 - the format ../PS5_Vulkan reports as sampled - so byte 0 takes the
        # nibble B4G4R4A4 packs red in and byte 2 takes blue's. Without this the menu
        # draws with red and blue exchanged, which is what the previous round's capture
        # would have shown had the frame reached the screen.
        "gfx/drivers/vulkan.c",
        "            *dstpix      = (\n"
        "                  (pix & 0xf000) >>  8)\n"
        "               | ((pix & 0x0f00) <<  4)\n"
        "               | ((pix & 0x00f0) << 16)\n"
        "               | ((pix & 0x000f) << 28);\n",
        "            /* Added by this port (patches/series, 0028): R8G8B8A8's byte order,\n"
        "             * from the B4G4R4A4 source the caller hands over. */\n"
        "            *dstpix      = (\n"
        "                  (pix & 0x00f0)      )\n"
        "               | ((pix & 0x0f00) <<  4)\n"
        "               | ((pix & 0xf000) <<  8)\n"
        "               | ((pix & 0x000f) << 28);\n",
        "patches/series, 0028",
    ),
    (
        # The capture, on the one path every frame takes. `vulkan_frame` is called every
        # frame, and the frontend's own screenshot path never reaches the driver on a
        # content-less run (docs/PHASE_LOG.md, 2026-09-19). So the port asks the driver
        # itself - `vulkan_read_viewport` is the driver's own readback - and writes the
        # bytes out as a PPM: a header and the pixels, with no frontend writer in the way.
        # The budget and the path are the port's own options in /app0/args.txt, which
        # src/main.cpp keeps away from RetroArch's option parsing, so a run without them
        # never takes a picture. The trace line names the viewport and whether it worked.
        "gfx/drivers/vulkan.c",
        "   vulkan_filter_chain_t *filter_chain           = NULL;\n",
        "   /* Added by this port (patches/series, 0031): see the note above this edit. */\n"
        "   {\n"
        "      static int      ps5_capture_budget = -1;\n"
        "      static unsigned ps5_capture_frames;\n"
        "      static bool     ps5_capture_taken;\n"
        "      static char     ps5_capture_file[512] = \"/app0/shot.ppm\";\n"
        "\n"
        "      if (ps5_capture_budget < 0)\n"
        "      {\n"
        "         FILE *ps5_args = fopen(\"/app0/args.txt\", \"r\");\n"
        "         char  ps5_line[256];\n"
        "\n"
        "         ps5_capture_budget = 0;\n"
        "\n"
        "         while (ps5_args && fgets(ps5_line, sizeof(ps5_line), ps5_args))\n"
        "         {\n"
        "            if (strncmp(ps5_line, \"--ps5-capture=\", 14) == 0)\n"
        "               ps5_capture_budget = atoi(ps5_line + 14);\n"
        "            else if (strncmp(ps5_line, \"--ps5-capture-path=\", 19) == 0)\n"
        "            {\n"
        "               char *ps5_end = strpbrk(ps5_line + 19, \"\\r\\n\");\n"
        "\n"
        "               if (ps5_end)\n"
        "                  *ps5_end = '\\0';\n"
        "               snprintf(ps5_capture_file, sizeof(ps5_capture_file), \"%s\", ps5_line + 19);\n"
        "            }\n"
        "         }\n"
        "\n"
        "         if (ps5_args)\n"
        "            fclose(ps5_args);\n"
        "      }\n"
        "\n"
        "      if (ps5_capture_budget > 0 && !ps5_capture_taken)\n"
        "      {\n"
        "         ps5_capture_frames++;\n"
        "\n"
        "         if (ps5_capture_frames >= (unsigned)ps5_capture_budget)\n"
        "         {\n"
        "            unsigned ps5_width  = vk->vp.width;\n"
        "            unsigned ps5_height = vk->vp.height;\n"
        "            uint8_t *ps5_pixels = NULL;\n"
        "            bool     ps5_ok     = false;\n"
        "            FILE    *ps5_trace;\n"
        "\n"
        "            ps5_capture_taken = true;\n"
        "\n"
        "            if (ps5_width && ps5_height)\n"
        "               ps5_pixels = (uint8_t*)malloc((size_t)ps5_width * ps5_height * 3);\n"
        "\n"
        "            if (ps5_pixels)\n"
        "               ps5_ok = vulkan_read_viewport(vk, ps5_pixels, false);\n"
        "\n"
        "            if (ps5_ok)\n"
        "            {\n"
        "               FILE *ps5_out = fopen(ps5_capture_file, \"wb\");\n"
        "\n"
        "               if (ps5_out)\n"
        "               {\n"
        "                  unsigned ps5_y;\n"
        "\n"
        "                  /* The driver reads bottom-up BGR, which is a BMP's order; a PPM\n"
        "                   * is top-down RGB. */\n"
        "                  fprintf(ps5_out, \"P6\\n%u %u\\n255\\n\", ps5_width, ps5_height);\n"
        "                  for (ps5_y = 0; ps5_y < ps5_height; ps5_y++)\n"
        "                  {\n"
        "                     const uint8_t *ps5_row =\n"
        "                           ps5_pixels + (size_t)(ps5_height - 1 - ps5_y) * ps5_width * 3;\n"
        "                     unsigned ps5_x;\n"
        "\n"
        "                     for (ps5_x = 0; ps5_x < ps5_width; ps5_x++)\n"
        "                     {\n"
        "                        uint8_t ps5_rgb[3];\n"
        "\n"
        "                        ps5_rgb[0] = ps5_row[ps5_x * 3 + 2];\n"
        "                        ps5_rgb[1] = ps5_row[ps5_x * 3 + 1];\n"
        "                        ps5_rgb[2] = ps5_row[ps5_x * 3 + 0];\n"
        "                        fwrite(ps5_rgb, 1, 3, ps5_out);\n"
        "                     }\n"
        "                  }\n"
        "                  fclose(ps5_out);\n"
        "               }\n"
        "               else\n"
        "                  ps5_ok = false;\n"
        "            }\n"
        "\n"
        "            free(ps5_pixels);\n"
        "\n"
        "            ps5_trace = fopen(\"/app0/trace.txt\", \"a\");\n"
        "            if (ps5_trace)\n"
        "            {\n"
        "               fprintf(ps5_trace,\n"
        "                     \"capture: frame %u of %d, viewport %ux%u, read_viewport -> %s (%s)\\n\",\n"
        "                     ps5_capture_frames, ps5_capture_budget, ps5_width, ps5_height,\n"
        "                     ps5_ok ? \"ok\" : \"failed\", ps5_capture_file);\n"
        "               fclose(ps5_trace);\n"
        "            }\n"

        "            /* A capture run is over. The frontend's own shutdown is what\n"
        "             * flushes /app0/retroarch.log, the log that never flushes while\n"
        "             * the title is killed instead of exiting. */\n"
        "            command_event(CMD_EVENT_QUIT, NULL);\n"
        "         }\n"
        "      }\n"
        "   }\n"
        "\n"
        "   vulkan_filter_chain_t *filter_chain           = NULL;\n",
        "patches/series, 0031",
    ),
    (
        # The driver's own readback is defined below `vulkan_frame`, so the capture calls it
        # through a prototype at file scope - where the driver declares its other forward
        # functions (`vulkan_viewport_info`, line 1239) - because C does not allow that
        # declaration inside a function body.
        "gfx/drivers/vulkan.c",
        "static void vulkan_viewport_info(void *data, struct video_viewport *vp);\n",
        "static void vulkan_viewport_info(void *data, struct video_viewport *vp);\n"
        "/* Added by this port (patches/series, 0032): the capture's readback entry point. */\n"
        "static bool vulkan_read_viewport(void *data, uint8_t *buffer, bool is_idle);\n",
        "patches/series, 0032",
    ),
    (
        # `command_event` is how the frontend quits, and quitting is what flushes its log
        # file: the goal's evidence names /app0/retroarch.log, and a title that is killed
        # from the outside never writes it (it has been the same 1200 bytes all along).
        "gfx/drivers/vulkan.c",
        "#include \"../../retroarch.h\"\n",
        "#include \"../../retroarch.h\"\n"
        "/* Added by this port (patches/series, 0033): the capture ends the run cleanly. */\n"
        "#include \"../../command.h\"\n",
        "patches/series, 0033",
    ),
    (
        # A3 of docs/GPU_PATH_CRITERIA.md: libps5vk's sampler refuses any address
        # mode but clamp-to-edge (driver/ps5vk_image.c:800-806), and a refusal ends
        # recording with an error at vkEndCommandBuffer, so the draw never submits.
        # The maintainer's reading of this port's log is that these refusals come
        # from the shader path, not the menu's own sampler: gfx/drivers/vulkan.c
        # already asks clamp-to-edge, while this file hardcodes REPEAT here and
        # maps the preset's wrap mode further down.
        #
        # Both become clamp-to-edge. That is a real behaviour change - a filter
        # preset asking for repeat gets clamp - and it is the honest consequence of
        # a decoder that has only proven clamp: the alternative is a refused draw,
        # which is no picture at all. Lifting the restriction properly is a runner
        # probe in ../PS5_Vulkan (V0-sampler, C4 scope) and is not this project's
        # change to make.
        "gfx/drivers_shader/shader_vulkan.cpp",
        "   info.addressModeU            = VK_SAMPLER_ADDRESS_MODE_REPEAT;\n"
        "   info.addressModeV            = VK_SAMPLER_ADDRESS_MODE_REPEAT;\n"
        "   info.addressModeW            = VK_SAMPLER_ADDRESS_MODE_REPEAT;\n",
        "   /* Named by this port (patches/series, 0012): the console's decoder has\n"
        "    * only proven clamp-to-edge, and any other mode ends the command buffer\n"
        "    * with an error so the draw never submits. See tools/apply-port-patches.py. */\n"
        "   info.addressModeU            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;\n"
        "   info.addressModeV            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;\n"
        "   info.addressModeW            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;\n",
        "only proven clamp-to-edge",
    ),
    (
        # The same change for the preset-driven path: every mode a filter preset can
        # ask for becomes clamp-to-edge, because that is the only one the decoder
        # accepts. The switch is kept rather than collapsed so each case's intent
        # stays visible and a future driver can restore them one at a time.
        "gfx/drivers_shader/shader_vulkan.cpp",
        "               case GLSLANG_FILTER_CHAIN_ADDRESS_REPEAT:\n"
        "                  mode = VK_SAMPLER_ADDRESS_MODE_REPEAT;\n"
        "                  break;\n"
        "\n"
        "               case GLSLANG_FILTER_CHAIN_ADDRESS_MIRRORED_REPEAT:\n"
        "                  mode = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;\n"
        "                  break;\n",
        "               /* patches/series 0012: only clamp-to-edge is proven on this\n"
        "                * console, so a preset's repeat modes are asked for as clamp. */\n"
        "               case GLSLANG_FILTER_CHAIN_ADDRESS_REPEAT:\n"
        "                  mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;\n"
        "                  break;\n"
        "\n"
        "               case GLSLANG_FILTER_CHAIN_ADDRESS_MIRRORED_REPEAT:\n"
        "                  mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;\n"
        "                  break;\n",
        "only clamp-to-edge is proven on this",
    ),
    (
        # And the two border modes, refused for a second reason: they need
        # descriptor word 11 set to a border colour, which the decoder keeps at the
        # canary's transparent black.
        "gfx/drivers_shader/shader_vulkan.cpp",
        "               case GLSLANG_FILTER_CHAIN_ADDRESS_CLAMP_TO_BORDER:\n"
        "                  mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;\n"
        "                  break;\n"
        "\n"
        "               case GLSLANG_FILTER_CHAIN_ADDRESS_MIRROR_CLAMP_TO_EDGE:\n"
        "                  mode = VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;\n"
        "                  break;\n",
        "               /* patches/series 0012: the border modes need descriptor word 11\n"
        "                * set to a border colour, which the decoder keeps at the\n"
        "                * canary's transparent black, so they are refused too. */\n"
        "               case GLSLANG_FILTER_CHAIN_ADDRESS_CLAMP_TO_BORDER:\n"
        "                  mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;\n"
        "                  break;\n"
        "\n"
        "               case GLSLANG_FILTER_CHAIN_ADDRESS_MIRROR_CLAMP_TO_EDGE:\n"
        "                  mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;\n"
        "                  break;\n",
        "the border modes need descriptor word 11",
    ),
    (
        # A2 of docs/GPU_PATH_CRITERIA.md, and the last of the refusals.
        #
        # The surviving line is `vulkan: set 0 binding 3: descriptor type 3 has no
        # proven table entry`, and it does NOT come from an upload: it comes from
        # vkCreateComputePipelines. ../PS5_Vulkan's ps5vk_descriptor_options
        # (driver/ps5vk_pipeline.c:120-150) walks every binding of set 0 whose
        # stage flags match the stage being compiled, and refuses any whose stride
        # is zero - and ps5vk_descriptor_stride gives a storage image zero
        # deliberately, "pipelines refuse them until a probe proves their entries"
        # (driver/ps5vk_descriptor_set_layout.c:24-40).
        #
        # This frontend's set 0 declares binding 3 as a compute-stage storage image
        # (vulkan_init_pipeline_layout, "bindings[3]"), and exactly one pipeline is
        # ever compiled against the layout with COMPUTE set: rgb565_to_rgba8888,
        # the upload shader below. So the refusal fires once per
        # vulkan_init_pipelines call, which is once at init and once per swapchain
        # recreation - three times in the recorded run, all three the same line.
        #
        # The pipeline is not needed here. It exists for the compute upload, and
        # patch 0027 already names the one format this port uses for a menu frame
        # (R8G8B8A8), so the formats cannot differ and
        # vulkan_copy_staging_to_dynamic never takes its compute branch: the branch
        # is guarded by `retro_assert(staging->format == VK_FORMAT_R5G6B5_UNORM_PACK16)`,
        # which this port would fail rather than pass. Compiling a shader whose only
        # caller is unreachable buys nothing and costs a refusal per pipeline init,
        # so the handle stays null. vkDestroyPipeline(NULL) is a defined no-op, and
        # the compute branch's bind is never reached.
        #
        # This is deliberately the smallest change that removes the refusal. The
        # layout keeps binding 3, so the compute branch stays consistent with the
        # set it writes, and retiring the binding itself is a separate step that
        # waits on the upload path first being proven unnecessary rather than
        # assumed to be.
        "gfx/drivers/vulkan.c",
        "   module_info.codeSize   = sizeof(rgb565_to_rgba8888_comp);\n"
        "   module_info.pCode      = rgb565_to_rgba8888_comp;\n"
        "   vkCreateShaderModule(vk->context->device,\n"
        "         &module_info, NULL, &cpipe.stage.module);\n"
        "   vkCreateComputePipelines(vk->context->device, vk->pipelines.cache,\n"
        "         1, &cpipe, NULL, &vk->pipelines.rgb565_to_rgba8888);\n"
        "   vkDestroyShaderModule(vk->context->device, cpipe.stage.module, NULL);\n",
        "   /* Added by this port (patches/series, 0036): see the note above this edit.\n"
        "    * No compute pipeline is compiled, so set 0's compute-only storage image\n"
        "    * (binding 3) is never presented to the driver's descriptor check, which is\n"
        "    * the check that refused this frontend once per pipeline initialisation. */\n"
        "   vk->pipelines.rgb565_to_rgba8888 = VK_NULL_HANDLE;\n",
        "patches/series, 0036",
    ),
    (
        # B1 of docs/GPU_PATH_CRITERIA.md. "The title runs on video_vulkan, not
        # video_ps5" has to be a record, not an inference: the frontend logs which
        # *display server* it found and never which video driver ran, and on this
        # console the driver's own log file does not exist to be read
        # (/app0/retroarch.log answers "no such file" over FTP). The port's own
        # driver proves itself with "ps5_init entered" in the trace; this is the
        # same mark for the other driver, so a trace names exactly one of them.
        # stderr reaches the trace file (src/main.cpp), which is why fprintf and
        # not RARCH_LOG: the frontend's log is the record that is missing.
        "gfx/drivers/vulkan.c",
        "   vk_t *vk                           = (vk_t*)calloc(1, sizeof(*vk));\n"
        "   if (!vk)\n"
        "      return NULL;\n",
        "   /* Added by this port (patches/series, 0038): names the driver that ran. */\n"
        "   fprintf(stderr, \"video driver: video_vulkan init entered\\n\");\n"
        "   vk_t *vk                           = (vk_t*)calloc(1, sizeof(*vk));\n"
        "   if (!vk)\n"
        "      return NULL;\n",
        "video_vulkan init entered",
    ),
    (
        # A capture must not end the run. It did: the block above called
        # command_event(CMD_EVENT_QUIT, NULL) once the probe had fired, so a run
        # that took a picture quit itself about 90 frames in - the frontend shuts
        # down, the title disappears from the screen a second and a half after it
        # appears, and the console's crash reporter books it as an application
        # crash ("SCE_SHELL_UTIL_ERROR_APPLICATION_CRASH", with a coredump) even
        # though the process exited 0. The probe was written to close a capture
        # deterministically; what it actually closes is every run that has a
        # capture requested, including one that is meant to stay on screen for
        # somebody to look at.
        #
        # So the mark stays and the quit goes. The probe still runs, still reports
        # through /app0/trace.txt and still writes its file; if a run needs to end
        # by itself, the capture's budget is not the mechanism to use. That is the
        # run script's job (`tools/run-title.sh` watches and closes the title), and
        # it is the only place that knows how long a run should last.
        "gfx/drivers/vulkan.c",
        "            /* A capture run is over. The frontend's own shutdown is what\n"
        "             * flushes /app0/retroarch.log, the log that never flushes while\n"
        "             * the title is killed instead of exiting. */\n"
        "            command_event(CMD_EVENT_QUIT, NULL);\n",
        "            /* Added by this port (patches/series, 0039): the probe no longer\n"
        "             * ends the run. See the note above the capture block. */\n",
        "patches/series, 0039",
    ),
    (
        # What the black screen needs to be told apart. A clean trace with no
        # refusal means the driver accepted every command - and the screen is
        # still black, so the question is now whether any draw is being recorded
        # at all, and whether frames keep being presented. Those are two different
        # faults and the trace cannot currently distinguish them: presenting with
        # nothing drawn, and drawing into a buffer that is never shown, both look
        # like "no refusal, no picture".
        #
        # So the driver marks both, the way src/video_ps5.cpp marks its own
        # frames. `/app0/trace.txt` is stderr (src/main.cpp), so these lines land
        # in the same record as the driver's refusals. The draw is marked in
        # gfx_display_vk_draw, which is the one choke point every display list goes
        # through, and it names the primitive type and the vertex count, because
        # this port expands triangle strips into lists by index and a wrong index
        # count draws nothing while looking like a successful draw.
        "gfx/drivers/vulkan.c",
        "   if (!vk || !draw)\n"
        "      return;\n"
        "\n"
        "   texture                        = (struct vk_texture*)draw->texture;\n",
        "   if (!vk || !draw)\n"
        "      return;\n"
        "\n"
        "   /* Added by this port (patches/series, 0040): says whether anything is drawn. */\n"
        "   {\n"
        "      static unsigned ps5_draw_marks;\n"
        "\n"
        "      if (ps5_draw_marks < 6 || (ps5_draw_marks % 1200) == 0)\n"
        "         fprintf(stderr, \"vulkan draw %u: type=%u vertices=%u texture=%s\\n\",\n"
        "               ps5_draw_marks, (unsigned)draw->prim_type, (unsigned)draw->coords->vertices,\n"
        "               draw->texture ? \"yes\" : \"no\");\n"
        "      ps5_draw_marks++;\n"
        "   }\n"
        "\n"
        "   texture                        = (struct vk_texture*)draw->texture;\n",
        "patches/series, 0040",
    ),
    (
        # The other half of the same question: is the frame loop still running, and
        # is anything still being presented? A title that draws once and then stops
        # being called, and a title that draws every frame into a swapchain image
        # nobody flips, read the same way from the outside.
        "gfx/drivers/vulkan.c",
        "static bool vulkan_frame(void *data, const void *frame,\n",
        "/* Added by this port (patches/series, 0041): counts the frames the frontend asks for. */\n"
        "static void ps5_mark_vulkan_frame(void)\n"
        "{\n"
        "   static unsigned ps5_frames;\n"
        "\n"
        "   ps5_frames++;\n"
        "   if (ps5_frames <= 3 || (ps5_frames % 300) == 0)\n"
        "      fprintf(stderr, \"vulkan frame %u\\n\", ps5_frames);\n"
        "}\n"
        "\n"
        "static bool vulkan_frame(void *data, const void *frame,\n",
        "patches/series, 0041",
    ),
    (
        # The call itself, at the top of vulkan_frame, before anything can fail.
        "gfx/drivers/vulkan.c",
        "   VkCommandBufferBeginInfo begin_info;\n"
        "   VkSemaphore signal_semaphores[2];\n"
        "   vk_t *vk                                      = (vk_t*)data;\n",
        "   VkCommandBufferBeginInfo begin_info;\n"
        "   VkSemaphore signal_semaphores[2];\n"
        "   vk_t *vk                                      = (vk_t*)data;\n"
        "   /* Added by this port (patches/series, 0041): see ps5_mark_vulkan_frame. */\n"
        "   ps5_mark_vulkan_frame();\n",
        "ps5_mark_vulkan_frame();",
    ),
    (
        # Why the screen is black while every frame presents. RetroArch's widgets
        # branch and its normal display-driver init are mutually exclusive:
        #
        #   retroarch.c: if (current_video->gfx_widgets_enabled(...))
        #                   gfx_widgets_init(...);
        #                else
        #                   gfx_display_init_first_driver(...);
        #
        # gfx_display_init_first_driver is the only thing that sets
        # p_disp->dispctx, and gfx_display_draw returns immediately without it
        # (gfx_display.c:643, `if (!dispctx || !dispctx->draw) return;`). This
        # driver answered gfx_widgets_enabled with true, so the frontend took the
        # widgets branch, dispctx was never resolved, and every draw the menu
        # issued - menu quads, font, all of it - returned in silence. Nothing was
        # drawn, so the backbuffer stayed clear and the screen stayed black, while
        # vulkan_frame went on presenting ~45 frames a second and the trace showed
        # no error of any kind. That is the exact shape the previous rounds kept
        # misreading as an upload or presentation fault.
        #
        # Answering false is what the upstream Vulkan-derived drivers do and what
        # this port's own video_ps5 does (it has no gfx_widgets_enabled at all, so
        # the frontend takes the else branch). Declining widgets costs nothing
        # here: nothing in this driver draws widgets - gfx_widgets_frame is called
        # from vulkan_frame and does its own rendering - and the widgets system is
        # what left the menu without a display context. The menu is the thing this
        # path exists to draw.
        "gfx/drivers/vulkan.c",
        "static bool vulkan_gfx_widgets_enabled(void *data) { return true; }\n",
        "/* Added by this port (patches/series, 0042): false, because answering true\n"
        " * makes the frontend skip gfx_display_init_first_driver, which is the only\n"
        " * caller that sets the display context every menu draw needs. See the note\n"
        " * above this edit. */\n"
        "static bool vulkan_gfx_widgets_enabled(void *data) { return false; }\n",
        "patches/series, 0042",
    ),
    (
        # Which of the two mutually exclusive branches the frontend takes, and
        # what the driver identifies itself as while it takes it. The black screen
        # is one silent return deep - gfx_display_draw gives up when the display
        # context was never resolved - and there are three different reasons that
        # can happen (widgets claimed the driver, the frontend's ident does not
        # match this port's, or the display driver list was not walked at all).
        # The trace cannot currently tell them apart, so these two lines say which
        # branch was entered and what the display driver matched.
        "retroarch.c",
        "      p_dispwidget->active= gfx_widgets_init(\n",
        "      /* Added by this port (patches/series, 0044): which branch ran. */\n"
        "      fprintf(stderr, \"frontend: widgets branch taken\\n\");\n"
        "      p_dispwidget->active= gfx_widgets_init(\n",
        "frontend: widgets branch taken",
    ),
    (
        # The other branch, and the answer gfx_display_init_first_driver gets.
        "retroarch.c",
        "      gfx_display_init_first_driver(p_disp, video_is_threaded);\n",
        "      /* Added by this port (patches/series, 0045): which branch ran, and\n"
        "       * what the display driver was matched against. */\n"
        "      fprintf(stderr, \"frontend: display-driver branch taken, video_driver=%s matched=%s\\n\",\n"
        "            video_driver_get_ident() ? video_driver_get_ident() : \"(null)\",\n"
        "            gfx_display_init_first_driver(p_disp, video_is_threaded) ? \"yes\" : \"no\");\n",
        "frontend: display-driver branch taken",
    ),
    (
        # Why the screen was black with a healthy driver, a resolved display
        # context and 45 frames a second. RetroArch's Vulkan driver draws the menu
        # only when the frontend tells it that the menu texture is the thing to
        # show:
        #
        #   gfx/drivers/vulkan.c: if (vk->flags & VK_FLAG_MENU_ENABLE)
        #                            menu_driver_frame(menu_is_alive, video_info);
        #
        # and VK_FLAG_MENU_ENABLE is set by exactly one call in the whole
        # frontend, `video_st->poke->set_texture_enable(video_st->data, true,
        # false)` in runloop.c's display_menu_libretro - the function whose own
        # comment reads "Display the libretro core's framebuffer onscreen". So the
        # flag means "a core's framebuffer is up": it is a libretro concept, and it
        # is set when a core runs, not when the menu opens. menu_driver.c clears it
        # when the menu closes and never sets it.
        #
        # That is fine upstream because the drivers whose menus are drawn this way
        # are reached with a core running, and it is fine for this port's own
        # video_ps5, which ignores the flag entirely and draws whatever
        # set_texture_frame handed it. Under Vulkan it is fatal: this port launches
        # straight into the menu with no content, the flag is never set, the menu
        # branch is never entered, menu_driver_frame is never called, nothing is
        # drawn, and the backbuffer is presented black - with no refusal from the
        # driver, no error from the frontend, and a clean trace. The menu is on
        # screen only if this flag says so.
        #
        # So the menu marks its own texture displayable while it is the thing on
        # screen, and clears it when it closes. `on` is this function's own
        # argument for exactly that state, and the two sites are the menu opening
        # and the menu closing, so the flag now follows the menu instead of
        # following a core that this title does not have loaded.
        "menu/menu_driver.c",
        "      menu_st->flags |= MENU_ST_FLAG_ALIVE;\n"
        "      menu_driver_toggle(\n"
        "            video_st->current_video,\n"
        "            video_st->data,\n"
        "            menu,\n"
        "            menu_input,\n"
        "            settings,\n"
        "            (menu_st->flags & MENU_ST_FLAG_ALIVE) ? true : false,\n",
        "      menu_st->flags |= MENU_ST_FLAG_ALIVE;\n"
        "      /* Added by this port (patches/series, 0046): see the note above this\n"
        "       * edit. The menu is the texture on screen, so say so - otherwise a\n"
        "       * driver that draws the menu behind this flag (RetroArch's Vulkan\n"
        "       * driver does) draws nothing at all. */\n"
        "      if (video_st->poke && video_st->poke->set_texture_enable)\n"
        "         video_st->poke->set_texture_enable(video_st->data, true, false);\n"
        "      menu_driver_toggle(\n"
        "            video_st->current_video,\n"
        "            video_st->data,\n"
        "            menu,\n"
        "            menu_input,\n"
        "            settings,\n"
        "            (menu_st->flags & MENU_ST_FLAG_ALIVE) ? true : false,\n",
        "patches/series, 0046",
    ),
    (
        # Whether the frontend ever told the driver its menu texture is on screen,
        # and whether the menu texture exists. The driver draws the menu only
        # behind VK_FLAG_MENU_ENABLE, so a black screen with no draw can mean the
        # flag was never set - or that it was set but set_texture_frame never
        # handed over a texture, in which case the branch runs and skips its body.
        # One line, in the same place the frame counter is, says which.
        "gfx/drivers/vulkan.c",
        "   /* Added by this port (patches/series, 0041): see ps5_mark_vulkan_frame. */\n"
        "   ps5_mark_vulkan_frame();\n",
        "   /* Added by this port (patches/series, 0041): see ps5_mark_vulkan_frame. */\n"
        "   ps5_mark_vulkan_frame();\n"
        "   /* Added by this port (patches/series, 0046): why the menu branch did or\n"
        "    * did not draw. See the note above this edit. */\n"
        "   {\n"
        "      static unsigned ps5_flag_marks;\n"
        "\n"
        "      if (ps5_flag_marks < 4 || (ps5_flag_marks % 600) == 0)\n"
        "         fprintf(stderr, \"vulkan menu state: flag=%u idx=%u staging(img=%u buf=%u) optimal(img=%u buf=%u)\\n\",\n"
        "               (vk->flags & VK_FLAG_MENU_ENABLE) ? 1u : 0u,\n"
        "               (unsigned)vk->menu.last_index,\n"
        "               (vk->menu.textures[vk->menu.last_index].image != VK_NULL_HANDLE) ? 1u : 0u,\n"
        "               (vk->menu.textures[vk->menu.last_index].buffer != VK_NULL_HANDLE) ? 1u : 0u,\n"
        "               (vk->menu.textures_optimal[vk->menu.last_index].image != VK_NULL_HANDLE) ? 1u : 0u,\n"
        "               (vk->menu.textures_optimal[vk->menu.last_index].buffer != VK_NULL_HANDLE) ? 1u : 0u);\n"
        "      ps5_flag_marks++;\n"
        "   }\n",
        "patches/series, 0046): why the menu branch",
    ),
    (
        # The handover, from both ends. The driver reports texture=0, which means
        # vulkan_set_texture_frame ran and was handed nothing, or never ran at all -
        # two different faults. RGUI only hands its framebuffer over when the
        # frontend marks it dirty, and the frontend only calls the menu's
        # set_texture in the runloop, so the mark goes in RGUI's set_texture and in
        # the driver's own set_texture_frame, and it names the two facts that decide
        # it: whether RGUI was asked, and whether it had a framebuffer to give.
        "menu/drivers/rgui.c",
        "   /* Framebuffer is dirty and needs to be updated? */\n"
        "   if (!rgui || !(p_disp->flags & GFX_DISP_FLAG_FB_DIRTY))\n"
        "      return;\n",
        "   /* Added by this port (patches/series, 0047): whether the handover happens,\n"
        "    * and why not when it does not. See the note above this edit. */\n"
        "   fprintf(stderr, \"rgui set_texture: rgui=%s dirty=%u data=%s\\n\",\n"
        "         rgui ? \"yes\" : \"no\",\n"
        "         (p_disp->flags & GFX_DISP_FLAG_FB_DIRTY) ? 1u : 0u,\n"
        "         (rgui && rgui->frame_buf.data) ? \"yes\" : \"no\");\n"
        "\n"
        "   /* Framebuffer is dirty and needs to be updated? */\n"
        "   if (!rgui || !(p_disp->flags & GFX_DISP_FLAG_FB_DIRTY))\n"
        "      return;\n",
        "rgui set_texture: rgui=",
    ),
    (
        # And the receiving end, so the trace shows the format and size actually
        # handed over - the last thing that can differ between a texture that
        # samples as the menu and one that samples as nothing.
        "gfx/drivers/vulkan.c",
        "   idx                 = vk->context->current_frame_index;\n"
        "   texture             = &vk->menu.textures[idx];\n",
        "   /* Added by this port (patches/series, 0047): what the menu handed over. */\n"
        "   fprintf(stderr, \"vulkan set_texture_frame: rgb32=%u %ux%u frame=%s\\n\",\n"
        "         rgb32 ? 1u : 0u, width, height, frame ? \"yes\" : \"no\");\n"
        "\n"
        "   idx                 = vk->context->current_frame_index;\n"
        "   texture             = &vk->menu.textures[idx];\n",
        "vulkan set_texture_frame: rgb32=",
    ),
    (
        # Whether RGUI's renderer runs. rgui_set_texture is called every frame and
        # always finds the frontend's framebuffer NOT dirty, and the only thing
        # that makes it dirty for the menu is the end of rgui_render - so either
        # rgui_render never runs, or something clears the flag between the end of
        # the render and the frontend's set_texture call. This line settles which.
        "menu/drivers/rgui.c",
        "   unsigned x, y;\n"
        "   unsigned fb_width, fb_height;\n"
        "   gfx_animation_ctx_ticker_t ticker;\n",
        "   /* Added by this port (patches/series, 0048): whether the menu renders. */\n"
        "   {\n"
        "      static unsigned ps5_render_marks;\n"
        "\n"
        "      if (ps5_render_marks < 3 || (ps5_render_marks % 600) == 0)\n"
        "         fprintf(stderr, \"rgui render %u: blit-state reached\\n\", ps5_render_marks);\n"
        "      ps5_render_marks++;\n"
        "   }\n"
        "\n"
        "   unsigned x, y;\n"
        "   unsigned fb_width, fb_height;\n"
        "   gfx_animation_ctx_ticker_t ticker;\n",
        "rgui render %u: blit-state reached",
    ),
    (
        # The line that decides whether the menu redraws. generic_menu_iterate runs
        # every frame and sets MENU_STATE_BLIT here; the runloop calls the menu's
        # render callback only when that bit is set, and rgui_render is what marks
        # the frontend's framebuffer dirty, which is what makes RGUI hand its
        # texture to the driver. RGUI rendered three times and then stopped, and
        # rgui_set_texture then ran hundreds of times with the framebuffer clean.
        # This counts the iterations that actually reach the set, and reports the
        # menu state alongside, so the two readings cannot be confused.
        "menu/menu_driver.c",
        "   BIT64_SET(menu->state, MENU_STATE_BLIT);\n",
        "   /* Added by this port (patches/series, 0049): whether the render bit is set. */\n"
        "   {\n"
        "      static unsigned ps5_iter_marks;\n"
        "\n"
        "      if (ps5_iter_marks < 3 || (ps5_iter_marks % 600) == 0)\n"
        "         fprintf(stderr, \"menu iterate %u: state=%llu ret=%d\\n\",\n"
        "               ps5_iter_marks, (unsigned long long)menu->state, ret);\n"
        "      ps5_iter_marks++;\n"
        "   }\n"
        "   BIT64_SET(menu->state, MENU_STATE_BLIT);\n",
        "menu iterate %u: state=",
    ),
    (
        # What the texture actually came out as. The driver reports no menu image
        # even after the handover ran with a valid 320x240 frame, and
        # vulkan_create_texture is allowed to change the type it was asked for: a
        # STREAMED texture whose linear tiling cannot be sampled becomes STAGING,
        # and STAGING has a buffer and NO image. The two call sites hand it
        # VULKAN_TEXTURE_STREAMED first and VULKAN_TEXTURE_DYNAMIC second, so this
        # line names the type each call settled on and whether an image exists -
        # which is the difference between "the menu texture was created" and "a
        # buffer was created and the menu has nothing to sample".
        "gfx/drivers/vulkan.c",
        "   tex.width  = width;\n"
        "   tex.height = height;\n"
        "   tex.format = format;\n"
        "   tex.type   = type;\n",
        "   tex.width  = width;\n"
        "   tex.height = height;\n"
        "   tex.format = format;\n"
        "   tex.type   = type;\n"
        "   /* Added by this port (patches/series, 0050): what was created. */\n"
        "   fprintf(stderr, \"vulkan create_texture: asked=%d type=%d %ux%u fmt=%d image=%s buffer=%s\\n\",\n"
        "         (int)type, (int)tex.type, width, height, (int)format,\n"
        "         tex.image ? \"yes\" : \"no\", tex.buffer ? \"yes\" : \"no\");\n",
        "vulkan create_texture: asked=",
    ),
    (
        # The two types the handover asks for, so the ask and the result can be
        # read together. Without this the "asked=" above is a number with no
        # second reading to compare it against.
        "gfx/drivers/vulkan.c",
        "         ? VULKAN_TEXTURE_STAGING\n"
        "         : VULKAN_TEXTURE_STREAMED);\n",
        "         ? VULKAN_TEXTURE_STAGING\n"
        "         : VULKAN_TEXTURE_STREAMED);\n"
        "   /* Added by this port (patches/series, 0050): the staging texture's ask. */\n"
        "   fprintf(stderr, \"vulkan menu staging ask: streamed=%d staging=%d dynamic=%d static=%d\\n\",\n"
        "         (int)VULKAN_TEXTURE_STREAMED, (int)VULKAN_TEXTURE_STAGING,\n"
        "         (int)VULKAN_TEXTURE_DYNAMIC, (int)VULKAN_TEXTURE_STATIC);\n",
        "vulkan menu staging ask:",
    ),
    (
        # The last silent return: the menu texture is created and never filled.
        #
        # vulkan_frame draws the menu from vk->menu.textures_optimal[]. That image is
        # filled by exactly one thing - vulkan_copy_staging_to_dynamic, called from
        # vulkan_frame's upload block, which runs only when vk->menu.dirty[] is set -
        # and the pixels RGUI hands over go into vk->menu.textures[], the staging
        # texture. This function marks dirty[] at its end for both cases, but it
        # fills the optimal image itself only in the first case:
        #
        #   if (texture->type == VULKAN_TEXTURE_STAGING)
        #      *texture_optimal = vulkan_create_texture(..., VULKAN_TEXTURE_DYNAMIC);
        #   else
        #      VULKAN_SYNC_TEXTURE_TO_GPU_COND_PTR(vk, texture);   /* <-- no copy */
        #
        # On the first handover texture->type is STAGING, so a DYNAMIC optimal
        # texture is created - an image with no data. From the second handover on,
        # texture_optimal->memory is non-NULL, so the else branch is taken: the
        # staging pixels are flushed to the GPU and then never copied into the
        # image the draw samples. The result is an optimal texture that exists and
        # holds whatever the allocation gave it, which is black, drawn every frame
        # over a black clear. Confirmed on the console by the state probe:
        # staging(img=0 buf=1) optimal(img=1 buf=0) with no upload between them.
        #
        # The copy is what the dirty flag is for, and the flag is set at the end of
        # both branches, so the upload block does run - but by then this function
        # has already returned and the only work it does is the copy from the
        # staging texture this branch just flushed. Removing the guard makes the
        # flush unconditional, which is what it needs to be: the staging texture is
        # rewritten every time RGUI hands a frame over, and the optimal image has
        # to be told so. The copy itself is the frontend's own
        # vkCmdCopyBufferToImage path, which this port already verified as the
        # branch taken when both formats match (patch 0027 names the one format).
        "gfx/drivers/vulkan.c",
        "   if (texture->type == VULKAN_TEXTURE_STAGING)\n"
        "      *texture_optimal = vulkan_create_texture(vk,\n"
        "              texture_optimal->memory\n"
        "            ? texture_optimal\n"
        "            : NULL,\n"
        "            width,\n"
        "            height,\n"
        "            fmt,\n"
        "            NULL,\n"
        "            ptr_swizzle,\n"
        "            VULKAN_TEXTURE_DYNAMIC);\n"
        "   else\n"
        "   {\n"
        "      VULKAN_SYNC_TEXTURE_TO_GPU_COND_PTR(vk, texture);\n"
        "   }\n",
        "   if (texture->type == VULKAN_TEXTURE_STAGING)\n"
        "      *texture_optimal = vulkan_create_texture(vk,\n"
        "              texture_optimal->memory\n"
        "            ? texture_optimal\n"
        "            : NULL,\n"
        "            width,\n"
        "            height,\n"
        "            fmt,\n"
        "            NULL,\n"
        "            ptr_swizzle,\n"
        "            VULKAN_TEXTURE_DYNAMIC);\n"
        "   /* Added by this port (patches/series, 0051): the flush is unconditional.\n"
        "    * The else branch used to sync the staging texture to the GPU without ever\n"
        "    * copying it into the optimal image the draw samples, so from the second\n"
        "    * handover on the menu was drawn from an image nothing had written. See the\n"
        "    * note above this edit. */\n"
        "   VULKAN_SYNC_TEXTURE_TO_GPU_COND_PTR(vk, texture);\n",
        "patches/series, 0051",
    ),
    (
        # Whether the upload runs at all, and which branch of it. The optimal image
        # still is not filled after patch 0051, and this function is the only
        # writer, so the question is now inside it: is it called, does the format
        # comparison send it to the compute path whose descriptor this driver
        # refuses (it would log that itself), or does its vkCmdCopyBufferToImage
        # path run and the image simply stay unread by the draw.
        "gfx/drivers/vulkan.c",
        "   bool compute_upload = dynamic->format != staging->format;\n",
        "   /* Added by this port (patches/series, 0052): whether the upload runs. */\n"
        "   fprintf(stderr, \"vulkan copy_staging_to_dynamic: dynamic %ux%u fmt=%d type=%d, staging fmt=%d type=%d, compute=%u\\n\",\n"
        "         dynamic->width, dynamic->height, (int)dynamic->format, (int)dynamic->type,\n"
        "         (int)staging->format, (int)staging->type,\n"
        "         (dynamic->format != staging->format) ? 1u : 0u);\n"
        "   bool compute_upload = dynamic->format != staging->format;\n",
        "vulkan copy_staging_to_dynamic: dynamic",
    ),
    (
        # The draw the menu actually uses. The probe in gfx_display_vk_draw watched
        # the display-list path and reported nothing, which was true and misleading:
        # this driver composites the menu itself, in vulkan_frame, through
        # vulkan_draw_quad - so "no draw" meant "not that kind of draw". This marks
        # the quad path, with the three things that decide whether the quad becomes
        # pixels: the texture it samples, whether that texture has an image, and
        # the image's layout at the moment of the draw.
        "gfx/drivers/vulkan.c",
        "static void vulkan_draw_quad(vk_t *vk, const struct vk_draw_quad *quad)\n"
        "{\n"
        "   if (quad->texture && quad->texture->image)\n"
        "      vulkan_transition_texture(vk, vk->cmd, quad->texture);\n",
        "static void vulkan_draw_quad(vk_t *vk, const struct vk_draw_quad *quad)\n"
        "{\n"
        "   /* Added by this port (patches/series, 0053): the quad the menu uses. */\n"
        "   {\n"
        "      static unsigned ps5_quad_marks;\n"
        "\n"
        "      if (ps5_quad_marks < 4 || (ps5_quad_marks % 600) == 0)\n"
        "         fprintf(stderr, \"vulkan draw_quad %u: texture=%s image=%s layout=%d %ux%u pipe=%s\\n\",\n"
        "               ps5_quad_marks,\n"
        "               quad->texture ? \"yes\" : \"no\",\n"
        "               (quad->texture && quad->texture->image) ? \"yes\" : \"no\",\n"
        "               quad->texture ? (int)quad->texture->layout : -1,\n"
        "               quad->texture ? quad->texture->width : 0u,\n"
        "               quad->texture ? quad->texture->height : 0u,\n"
        "               quad->pipeline ? \"yes\" : \"no\");\n"
        "      ps5_quad_marks++;\n"
        "   }\n"
        "   if (quad->texture && quad->texture->image)\n"
        "      vulkan_transition_texture(vk, vk->cmd, quad->texture);\n",
        "vulkan draw_quad %u: texture=",
    ),
    (
        "libretro-common/vulkan/vulkan_symbol_wrapper.c",
        "    *ppSymbol = GetInstanceProcAddr(instance, name);\n",
        "    /* Added by this port (patches/series, 0054): observe every API result. */\n"
        "    extern void ps5_vulkan_trace_symbol(const char *, PFN_vkVoidFunction *);\n"
        "    *ppSymbol = GetInstanceProcAddr(instance, name);\n"
        "    ps5_vulkan_trace_symbol(name, ppSymbol);\n",
        "    *ppSymbol = GetInstanceProcAddr(instance, name);\n"
        "    ps5_vulkan_trace_symbol",
    ),
    (
        "libretro-common/vulkan/vulkan_symbol_wrapper.c",
        "    *ppSymbol = vkGetDeviceProcAddr(device, name);\n",
        "    /* Added by this port (patches/series, 0054): observe every API result. */\n"
        "    extern void ps5_vulkan_trace_symbol(const char *, PFN_vkVoidFunction *);\n"
        "    *ppSymbol = vkGetDeviceProcAddr(device, name);\n"
        "    ps5_vulkan_trace_symbol(name, ppSymbol);\n",
        "    *ppSymbol = vkGetDeviceProcAddr(device, name);\n"
        "    ps5_vulkan_trace_symbol",
    ),
    (
        "gfx/drivers/vulkan.c",
        "   /* Draw the quad */\n"
        "   vkCmdDraw(vk->cmd, 6, 1, 0, 0);\n",
        "   /* Added by this port (patches/series, 0054): measure the draw inputs. */\n"
        "   {\n"
        "      static unsigned ps5_draw_inputs;\n"
        "      if (ps5_draw_inputs++ < 4)\n"
        "      {\n"
        "         unsigned ps5_m;\n"
        "         fprintf(stderr, \"gpu quad: alpha=%f viewport=%f,%f %fx%f depth=%f,%f\\n\",\n"
        "               quad->color.a, vk->vk_vp.x, vk->vk_vp.y,\n"
        "               vk->vk_vp.width, vk->vk_vp.height,\n"
        "               vk->vk_vp.minDepth, vk->vk_vp.maxDepth);\n"
        "         fprintf(stderr, \"gpu quad mvp:\");\n"
        "         for (ps5_m = 0; ps5_m < 16; ps5_m++)\n"
        "            fprintf(stderr, \" %f\", quad->mvp->data[ps5_m]);\n"
        "         fprintf(stderr, \"\\n\");\n"
        "      }\n"
        "   }\n"
        "   /* Draw the quad */\n"
        "   vkCmdDraw(vk->cmd, 6, 1, 0, 0);\n",
        "gpu quad: alpha=",
    ),
    (
        "gfx/drivers/vulkan.c",
        "            /* Added by this port (patches/series, 0028): R8G8B8A8's byte order,\n"
        "             * from the B4G4R4A4 source the caller hands over. */\n"
        "            *dstpix      = (\n"
        "                  (pix & 0x00f0)      )\n"
        "               | ((pix & 0x0f00) <<  4)\n"
        "               | ((pix & 0xf000) <<  8)\n"
        "               | ((pix & 0x000f) << 28);\n",
        "            /* Supersedes patches/series, 0028. */\n"
        "            /* Added by this port (patches/series, 0055): RGUI produces RGBA4444,\n"
        "             * red in the high nibble. Expand all channels to 0..255 and store\n"
        "             * R,G,B,A bytes for the matching R8G8B8A8 staging/dynamic textures. */\n"
        "            *dstpix = (((pix >> 12) & 15u)\n"
        "                  | (((pix >> 8) & 15u) << 8)\n"
        "                  | (((pix >> 4) & 15u) << 16)\n"
        "                  | ((pix & 15u) << 24)) * 17u;\n",
        "patches/series, 0055",
    ),
    (
        "menu/drivers/rgui.c",
        "   fprintf(stderr, \"rgui set_texture: rgui=%s dirty=%u data=%s\\n\",\n"
        "         rgui ? \"yes\" : \"no\",\n"
        "         (p_disp->flags & GFX_DISP_FLAG_FB_DIRTY) ? 1u : 0u,\n"
        "         (rgui && rgui->frame_buf.data) ? \"yes\" : \"no\");\n",
        "   /* patches/series, 0056: quiet rgui_set_texture */\n"
        "   {\n"
        "      static unsigned ps5_marks;\n"
        "      if (ps5_marks++ < 4)\n"
        "         fprintf(stderr, \"rgui set_texture: rgui=%s dirty=%u data=%s\\n\",\n"
        "               rgui ? \"yes\" : \"no\",\n"
        "               (p_disp->flags & GFX_DISP_FLAG_FB_DIRTY) ? 1u : 0u,\n"
        "               (rgui && rgui->frame_buf.data) ? \"yes\" : \"no\");\n"
        "   }\n",
        "patches/series, 0056: quiet rgui_set_texture",
    ),
    (
        "gfx/drivers/vulkan.c",
        "   fprintf(stderr, \"vulkan set_texture_frame: rgb32=%u %ux%u frame=%s\\n\",\n"
        "         rgb32 ? 1u : 0u, width, height, frame ? \"yes\" : \"no\");\n",
        "   /* patches/series, 0056: quiet vulkan_set_texture_frame */\n"
        "   {\n"
        "      static unsigned ps5_marks;\n"
        "      if (ps5_marks++ < 4)\n"
        "         fprintf(stderr, \"vulkan set_texture_frame: rgb32=%u %ux%u frame=%s\\n\",\n"
        "               rgb32 ? 1u : 0u, width, height, frame ? \"yes\" : \"no\");\n"
        "   }\n",
        "patches/series, 0056: quiet vulkan_set_texture_frame",
    ),
    (
        "gfx/drivers/vulkan.c",
        "   fprintf(stderr, \"vulkan copy_staging_to_dynamic: dynamic %ux%u fmt=%d type=%d, staging fmt=%d type=%d, compute=%u\\n\",\n"
        "         dynamic->width, dynamic->height, (int)dynamic->format, (int)dynamic->type,\n"
        "         (int)staging->format, (int)staging->type,\n"
        "         (dynamic->format != staging->format) ? 1u : 0u);\n",
        "   /* patches/series, 0056: quiet vulkan_copy_staging_to_dynamic */\n"
        "   {\n"
        "      static unsigned ps5_marks;\n"
        "      if (ps5_marks++ < 4)\n"
        "         fprintf(stderr, \"vulkan copy_staging_to_dynamic: dynamic %ux%u fmt=%d type=%d, staging fmt=%d type=%d, compute=%u\\n\",\n"
        "               dynamic->width, dynamic->height, (int)dynamic->format, (int)dynamic->type,\n"
        "               (int)staging->format, (int)staging->type,\n"
        "               (dynamic->format != staging->format) ? 1u : 0u);\n"
        "   }\n",
        "patches/series, 0056: quiet vulkan_copy_staging_to_dynamic",
    ),
    (
        "gfx/drivers/vulkan.c",
        "   ps5_mark_vulkan_frame();\n",
        "   /* patches/series, 0056: time the video callback through swap. */\n"
        "   extern void ps5_vulkan_profile_begin(void);\n"
        "   extern void ps5_vulkan_profile_end(void);\n"
        "   ps5_vulkan_profile_begin();\n"
        "   ps5_mark_vulkan_frame();\n",
        "   ps5_vulkan_profile_begin();",
    ),
    (
        "gfx/drivers/vulkan.c",
        "   if (vk->ctx_driver->swap_buffers)\n"
        "      vk->ctx_driver->swap_buffers(vk->ctx_data);\n",
        "   if (vk->ctx_driver->swap_buffers)\n"
        "      vk->ctx_driver->swap_buffers(vk->ctx_data);\n"
        "   ps5_vulkan_profile_end();\n",
        "   ps5_vulkan_profile_end();",
    ),
    (
        "gfx/drivers/vulkan.c",
        "   fprintf(stderr, \"vulkan create_texture: asked=%d type=%d %ux%u fmt=%d image=%s buffer=%s\\n\",\n"
        "         (int)type, (int)tex.type, width, height, (int)format,\n"
        "         tex.image ? \"yes\" : \"no\", tex.buffer ? \"yes\" : \"no\");\n",
        "   /* patches/series, 0056: quiet vulkan_create_texture */\n"
        "   {\n"
        "      static unsigned ps5_marks;\n"
        "      if (ps5_marks++ < 4)\n"
        "         fprintf(stderr, \"vulkan create_texture: asked=%d type=%d %ux%u fmt=%d image=%s buffer=%s\\n\",\n"
        "               (int)type, (int)tex.type, width, height, (int)format,\n"
        "               tex.image ? \"yes\" : \"no\", tex.buffer ? \"yes\" : \"no\");\n"
        "   }\n",
        "patches/series, 0056: quiet vulkan_create_texture",
    ),
    (
        "gfx/drivers/vulkan.c",
        "   fprintf(stderr, \"vulkan menu staging ask: streamed=%d staging=%d dynamic=%d static=%d\\n\",\n"
        "         (int)VULKAN_TEXTURE_STREAMED, (int)VULKAN_TEXTURE_STAGING,\n"
        "         (int)VULKAN_TEXTURE_DYNAMIC, (int)VULKAN_TEXTURE_STATIC);\n",
        "   /* patches/series, 0056: quiet vulkan_menu_staging_ask */\n"
        "   {\n"
        "      static unsigned ps5_marks;\n"
        "      if (ps5_marks++ < 4)\n"
        "         fprintf(stderr, \"vulkan menu staging ask: streamed=%d staging=%d dynamic=%d static=%d\\n\",\n"
        "               (int)VULKAN_TEXTURE_STREAMED, (int)VULKAN_TEXTURE_STAGING,\n"
        "               (int)VULKAN_TEXTURE_DYNAMIC, (int)VULKAN_TEXTURE_STATIC);\n"
        "   }\n",
        "patches/series, 0056: quiet vulkan_menu_staging_ask",
    ),
    (
        "gfx/drivers/vulkan.c",
        "   /* Added by this port (patches/series, 0047): what the menu handed over. */\n",
        "   /* patches/series, 0056: texture updates happen outside vulkan_frame. */\n"
        "   extern uint64_t ps5_vulkan_profile_texture_begin(void);\n"
        "   extern void ps5_vulkan_profile_texture_end(uint64_t);\n"
        "   uint64_t ps5_texture_start = ps5_vulkan_profile_texture_begin();\n"
        "   /* Added by this port (patches/series, 0047): what the menu handed over. */\n",
        "uint64_t ps5_texture_start = ps5_vulkan_profile_texture_begin();",
    ),
    (
        "gfx/drivers/vulkan.c",
        "   vk->menu.dirty[idx] = true;\n"
        "}\n",
        "   vk->menu.dirty[idx] = true;\n"
        "   ps5_vulkan_profile_texture_end(ps5_texture_start);\n"
        "}\n",
        "   ps5_vulkan_profile_texture_end(ps5_texture_start);",
    ),
    (
        "gfx/drivers/vulkan.c",
        "if (ps5_draw_marks < 6 || (ps5_draw_marks % 1200) == 0)",
        "if (ps5_draw_marks < 6) /* patches/series, 0057: startup-only ps5_draw_marks */",
        "patches/series, 0057: startup-only ps5_draw_marks",
    ),
    (
        "gfx/drivers/vulkan.c",
        "if (ps5_frames <= 3 || (ps5_frames % 300) == 0)",
        "if (ps5_frames <= 3) /* patches/series, 0057: startup-only ps5_frames */",
        "patches/series, 0057: startup-only ps5_frames",
    ),
    (
        "gfx/drivers/vulkan.c",
        "if (ps5_flag_marks < 4 || (ps5_flag_marks % 600) == 0)",
        "if (ps5_flag_marks < 4) /* patches/series, 0057: startup-only ps5_flag_marks */",
        "patches/series, 0057: startup-only ps5_flag_marks",
    ),
    (
        "menu/drivers/rgui.c",
        "if (ps5_render_marks < 3 || (ps5_render_marks % 600) == 0)",
        "if (ps5_render_marks < 3) /* patches/series, 0057: startup-only ps5_render_marks */",
        "patches/series, 0057: startup-only ps5_render_marks",
    ),
    (
        "menu/menu_driver.c",
        "if (ps5_iter_marks < 3 || (ps5_iter_marks % 600) == 0)",
        "if (ps5_iter_marks < 3) /* patches/series, 0057: startup-only ps5_iter_marks */",
        "patches/series, 0057: startup-only ps5_iter_marks",
    ),
    (
        "gfx/drivers/vulkan.c",
        "if (ps5_quad_marks < 4 || (ps5_quad_marks % 600) == 0)",
        "if (ps5_quad_marks < 4) /* patches/series, 0057: startup-only ps5_quad_marks */",
        "patches/series, 0057: startup-only ps5_quad_marks",
    ),
    (
        "retroarch.c",
        "   /* Second pass: All other arguments override the config file */\n",
        "   /* patches/series, 0058: the title's file logger must survive argv rebuilding.\n"
        "    * Keep frontend/core INFO, WARN and ERROR output for ongoing development. */\n"
        "   {\n"
        "      extern const char *ps5_frontend_build_identity(void);\n"
        "      verbosity_enable();\n"
        "      verbosity_enabled = true;\n"
        "      rarch_log_file_set_override(\"/app0/retroarch.log\");\n"
        "      rarch_log_file_init(true, false, NULL);\n"
        "      RARCH_LOG(\"[PS5] %s\\n\", ps5_frontend_build_identity());\n"
        "   }\n"
        "\n"
        "   /* Second pass: All other arguments override the config file */\n",
        "patches/series, 0058: the title's file logger",
    ),
    (
        "gfx/drivers/vulkan.c",
        "   /* patches/series, 0056: quiet vulkan_set_texture_frame */\n"
        "   {\n"
        "      static unsigned ps5_marks;\n"
        "      if (ps5_marks++ < 4)",
        "   /* patches/series, 0056: quiet vulkan_set_texture_frame */\n"
        "   {\n"
        "      static unsigned ps5_marks;\n"
        "      if (ps5_marks++ < 1) /* patches/series, 0059: once vulkan_set_texture_frame */",
        "patches/series, 0059: once vulkan_set_texture_frame",
    ),
    (
        "gfx/drivers/vulkan.c",
        "   /* patches/series, 0056: quiet vulkan_menu_staging_ask */\n"
        "   {\n"
        "      static unsigned ps5_marks;\n"
        "      if (ps5_marks++ < 4)",
        "   /* patches/series, 0056: quiet vulkan_menu_staging_ask */\n"
        "   {\n"
        "      static unsigned ps5_marks;\n"
        "      if (ps5_marks++ < 1) /* patches/series, 0059: once vulkan_menu_staging_ask */",
        "patches/series, 0059: once vulkan_menu_staging_ask",
    ),
    (
        "gfx/drivers/vulkan.c",
        "         draw->backend_data_size          = 2 * sizeof(float);",
        "         /* patches/series, 0060: libps5vk uses 16-byte UBO records.\n"
        "          * Preserve the two shader fields and zero the trailing padding. */\n"
        "         draw->backend_data_size          = 4 * sizeof(float);\n"
        "         memset(ubo_scratch_data, 0, 4 * sizeof(float));",
        "patches/series, 0060: libps5vk uses 16-byte UBO records",
    ),
    (
        'gfx/drivers/vulkan.c',
        '   const bool as_strip = (draw->prim_type == GFX_DISPLAY_PRIM_TRIANGLESTRIP);\n'
        '   const unsigned source_count = draw->coords->vertices;\n'
        '   const unsigned output_count =\n'
        '         (as_strip && source_count >= 3) ? (source_count - 2) * 3 : source_count;\n'
        '\n'
        '   if (!vulkan_buffer_chain_alloc(vk->context, &vk->chain->vbo,\n'
        '            output_count * sizeof(struct vk_vertex), &range))\n'
        '      return;\n'
        '\n'
        '   pv = (struct vk_vertex*)range.data;\n'
        '   for (i = 0; i < output_count; i++, pv++)\n'
        '   {\n'
        '      /* Which source vertex this output vertex is: the first triangle of the\n'
        '       * strip, then a pair per triangle after it. */\n'
        '      const unsigned s = as_strip\n'
        '            ? (i < 3 ? i : 3 + ((i - 3) / 2) * 2 + ((i - 3) % 2 ? 0 : 1))\n'
        '            : i;\n',
        '   /* patches/series, 0061: expand every strip triangle with alternating winding. */\n'
        '   const bool as_strip = (draw->prim_type == GFX_DISPLAY_PRIM_TRIANGLESTRIP);\n'
        '   const unsigned source_count = draw->coords->vertices;\n'
        '   const unsigned output_count =\n'
        '         as_strip ? (source_count >= 3 ? (source_count - 2) * 3 : 0) : source_count;\n'
        '\n'
        '   if (!vulkan_buffer_chain_alloc(vk->context, &vk->chain->vbo,\n'
        '            output_count * sizeof(struct vk_vertex), &range))\n'
        '      return;\n'
        '\n'
        '   pv = (struct vk_vertex*)range.data;\n'
        '   for (i = 0; i < output_count; i++, pv++)\n'
        '   {\n'
        '      /* Triangle t uses t,t+1,t+2, swapping its first pair on odd t. */\n'
        '      const unsigned s = as_strip\n'
        '            ? i / 3 + (i % 3 == 2 ? 2 : ((i % 3) ^ ((i / 3) & 1)))\n'
        '            : i;\n',
        'patches/series, 0061: expand every strip triangle',
    ),
    (
        'gfx/drivers/vulkan.c',
        '            call.uniform_size = draw->backend_data_size;\n'
        '            call.vbo          = &range;\n'
        '            call.vertices     = draw->coords->vertices;',
        '            call.uniform_size = draw->backend_data_size;\n'
        '            call.vbo          = &range;\n'
        '            call.vertices     = output_count; /* patches/series, 0061: effect vertex count */',
        'patches/series, 0061: effect vertex count',
    ),
    (
        'gfx/drivers/vulkan.c',
        '            call.uniform_size = sizeof(math_matrix_4x4);\n'
        '            call.vbo          = &range;\n'
        '            call.vertices     = draw->coords->vertices;',
        '            call.uniform_size = sizeof(math_matrix_4x4);\n'
        '            call.vbo          = &range;\n'
        '            call.vertices     = output_count; /* patches/series, 0061: icon vertex count */',
        'patches/series, 0061: icon vertex count',
    ),
    (
        "gfx/drivers/vulkan.c",
        "            call.texture      = NULL;\n"
        "            call.sampler      = VK_NULL_HANDLE;",
        "            /* patches/series, 0062: populate unused sampler layout slots. */\n"
        "            call.texture      = &vk->display.blank_texture;\n"
        "            call.sampler      = vk->samplers.nearest;",
        "patches/series, 0062: populate unused sampler layout slots",
    ),
    (
        "configuration.c",
        "   *settings->paths.directory_assets = '\\0';",
        "   /* patches/series, 0063: the title uses the null platform frontend. */\n"
        "   strlcpy(settings->paths.directory_assets, \"/app0/assets\",\n"
        "         sizeof(settings->paths.directory_assets));",
        "patches/series, 0063: the title uses the null platform frontend",
    ),
    (
        "menu/drivers/xmb.c",
        "   xmb->font            = gfx_display_font_file(p_disp,",
        "   /* patches/series, 0063: retain useful XMB startup asset diagnostics. */\n"
        "   RARCH_LOG(\"[XMB] Icons: %s; font: %s\\n\", iconpath, fontpath);\n"
        "   xmb->font            = gfx_display_font_file(p_disp,",
        "patches/series, 0063: retain useful XMB startup asset diagnostics",
    ),
    (
        "menu/drivers/xmb.c",
        "      xmb_update_dynamic_wallpaper(xmb, true);",
        "      /* patches/series, 0063: report the actual texture reset result. */\n"
        "      RARCH_LOG(\"[XMB] Assets missing: %s; fonts ready: %s\\n\",\n"
        "            xmb->assets_missing ? \"yes\" : \"no\",\n"
        "            (xmb->font && xmb->font2) ? \"yes\" : \"no\");\n"
        "      xmb_update_dynamic_wallpaper(xmb, true);",
        "patches/series, 0063: report the actual texture reset result",
    ),
    (
        "menu/drivers/xmb.c",
        '#include "../../configuration.h"',
        '#include "../../configuration.h"\n'
        '#include "../../verbosity.h" /* patches/series, 0063: XMB asset logger */',
        "patches/series, 0063: XMB asset logger",
    ),
    (
        'gfx/drivers/vulkan.c',
        'static void vulkan_transition_texture(',
        '/* patches/series, 0065: physical rows must be a whole 256 bytes. */\n'
        'static unsigned ps5_vulkan_texture_width(unsigned width, unsigned bytes_per_pixel)\n'
        '{\n'
        '   const unsigned alignment = 256u / bytes_per_pixel;\n'
        '   return (width + alignment - 1u) & ~(alignment - 1u);\n'
        '}\n'
        '\n'
        'static void vulkan_transition_texture(',
        'patches/series, 0065: physical rows must',
    ),
    (
        'gfx/drivers/vulkan.c',
        '      if (info.extent.width % 64u != 0u)\n'
        '         info.extent.width = (info.extent.width + 63u) & ~63u;',
        '      /* patches/series, 0065: R8 needs 256 texels, RGBA8 needs 64. */\n'
        '      info.extent.width = ps5_vulkan_texture_width(width, vulkan_format_to_bpp(format));',
        'patches/series, 0065: R8 needs',
    ),
    (
        'gfx/drivers/vulkan.c',
        '      pv->tex_x   = *tex_coord++;',
        '      /* patches/series, 0065: UVs address the logical region of the padded image. */\n'
        '      pv->tex_x   = *tex_coord++ * (float)texture->width\n'
        '            / ps5_vulkan_texture_width(texture->width, vulkan_format_to_bpp(texture->format));',
        'patches/series, 0065: UVs address',
    ),
    (
        'gfx/drivers/vulkan.c',
        '   float inv_tex_size_x                   = 1.0f / font->texture.width;',
        '   /* patches/series, 0065: glyph atlas offsets refer to the physical image. */\n'
        '   float inv_tex_size_x                   = 1.0f / ps5_vulkan_texture_width(\n'
        '         font->texture.width, vulkan_format_to_bpp(font->texture.format));',
        'patches/series, 0065: glyph atlas offsets',
    ),
    (
        'gfx/drivers/vulkan.c',
        '         info.mipLevels     = vulkan_num_miplevels(width, height);',
        '         /* patches/series, 0066: single-level menu images until mip sampling is verified. */\n'
        '         info.mipLevels     = 1;',
        'patches/series, 0066: single-level menu images',
    ),
    (
        'audio/audio_driver.c',
        'audio_driver_t *audio_drivers[] = {',
        '/* patches/series, 0067: native PS5 PCM backend. */\n'
        'extern audio_driver_t audio_ps5;\n'
        'audio_driver_t *audio_drivers[] = {\n'
        '   &audio_ps5,',
        'patches/series, 0067: native PS5 PCM backend',
    ),
    (
        'configuration.c',
        'const char *config_get_default_audio(void)\n'
        '{',
        'const char *config_get_default_audio(void)\n'
        '{\n'
        '   /* patches/series, 0067: PS5 audio default. */\n'
        '   return "ps5";',
        'patches/series, 0067: PS5 audio default',
    ),
    (
        'frontend/frontend_driver.c',
        'static frontend_ctx_driver_t *frontend_ctx_drivers[] = {',
        '/* patches/series, 0068: native PS5 platform paths and browser roots. */\n'
        'extern frontend_ctx_driver_t frontend_ctx_ps5;\n'
        'static frontend_ctx_driver_t *frontend_ctx_drivers[] = {\n'
        '   &frontend_ctx_ps5,',
        'patches/series, 0068: native PS5 platform',
    ),

    (
        'libretro-common/file/file_path.c',
        'size_t fill_pathname_application_path(char *s, size_t len)\n{',
        'size_t fill_pathname_application_path(char *s, size_t len)\n{\n'
        '   /* patches/series, 0069: a title has no procfs executable symlink. */\n'
        '   return len ? strlcpy(s, "/app0/eboot.bin", len) : 0;',
        'patches/series, 0069: a title has no procfs',
    ),
    (
        'libretro-common/vfs/vfs_implementation.c',
        '#include <vfs/vfs_implementation.h>',
        '#include <vfs/vfs_implementation.h>\n'
        '/* patches/series, 0070: SDK getdents adapter for native title directories. */\n'
        'extern DIR *ps5_opendir(const char *path);\n'
        'extern struct dirent *ps5_readdir(DIR *directory);\n'
        'extern int ps5_closedir(DIR *directory);\n'
        '#define opendir ps5_opendir\n'
        '#define readdir ps5_readdir\n'
        '#define closedir ps5_closedir',
        'patches/series, 0070: SDK getdents adapter',
    ),

    ('gfx/drivers/vulkan.c', '   vk->video                          = *video;', '/* patches/series, 0077: decode menu images for sampled RGBA */\n   video_driver_set_rgba();\n   RARCH_LOG("[PS5] Vulkan menu images use RGBA8.\\n");\n   vk->video                          = *video;', 'patches/series, 0077: decode menu images for sampled RGBA'),
    ('gfx/drivers/vulkan.c', 'static bool vulkan_get_current_sw_framebuffer(void *data,\n      struct retro_framebuffer *framebuffer)\n{\n   struct vk_per_frame *chain = NULL;\n   vk_t *vk                   = (vk_t*)data;\n   vk->chain                  =\n      &vk->swapchain[vk->context->current_frame_index];\n   chain                      = vk->chain;\n\n   if (chain->texture.width != framebuffer->width ||\n         chain->texture.height != framebuffer->height)\n   {\n      chain->texture   = vulkan_create_texture(vk, &chain->texture,\n            framebuffer->width, framebuffer->height, chain->texture.format,\n            NULL, NULL, VULKAN_TEXTURE_STREAMED);\n      {\n         struct vk_texture *texture = &chain->texture;\n         VK_MAP_PERSISTENT_TEXTURE(vk->context->device, texture);\n      }\n\n      if (chain->texture.type == VULKAN_TEXTURE_STAGING)\n      {\n         chain->texture_optimal = vulkan_create_texture(\n               vk,\n               &chain->texture_optimal,\n               framebuffer->width,\n               framebuffer->height,\n               chain->texture.format, /* Ensure we use the non-remapped format. */\n               NULL, NULL, VULKAN_TEXTURE_DYNAMIC);\n      }\n   }\n\n   framebuffer->data         = chain->texture.mapped;\n   framebuffer->pitch        = chain->texture.stride;\n   framebuffer->format       = vk->video.rgb32\n      ? RETRO_PIXEL_FORMAT_XRGB8888 : RETRO_PIXEL_FORMAT_RGB565;\n   framebuffer->memory_flags = 0;\n\n   if (vk->context->memory_properties.memoryTypes[\n         chain->texture.memory_type].propertyFlags &\n         VK_MEMORY_PROPERTY_HOST_CACHED_BIT)\n      framebuffer->memory_flags |= RETRO_MEMORY_TYPE_CACHED;\n\n   return true;\n}\n', "/* patches/series, 0077: preserve original core pixels across cached frames */\nstatic bool vulkan_get_current_sw_framebuffer(void *data,\n      struct retro_framebuffer *framebuffer)\n{\n   /* Core XRGB and GPU RGBA cannot share writable storage. Cached frames must\n    * retain the core's original pixels when Quick Menu pauses emulation. */\n   (void)data;\n   (void)framebuffer;\n   return false;\n}\n", 'patches/series, 0077: preserve original core pixels across cached frames'),
    ('gfx/drivers/vulkan.c', 'static uintptr_t vulkan_load_texture(void *video_data, void *data,\n      bool threaded, enum texture_filter_type filter_type)\n{\n   struct vk_texture *texture  = NULL;\n   vk_t *vk                    = (vk_t*)video_data;\n   struct texture_image *image = (struct texture_image*)data;\n   if (!image)\n      return 0;\n\n   if (!(texture = (struct vk_texture*)calloc(1, sizeof(*texture))))\n      return 0;\n\n   if (!image->pixels || !image->width || !image->height)\n   {\n      /* Create a dummy texture instead. */\n      static const uint32_t checkerboard[] = {\n         VK_T0, VK_T1, VK_T0, VK_T1, VK_T0, VK_T1, VK_T0, VK_T1,\n         VK_T1, VK_T0, VK_T1, VK_T0, VK_T1, VK_T0, VK_T1, VK_T0,\n         VK_T0, VK_T1, VK_T0, VK_T1, VK_T0, VK_T1, VK_T0, VK_T1,\n         VK_T1, VK_T0, VK_T1, VK_T0, VK_T1, VK_T0, VK_T1, VK_T0,\n         VK_T0, VK_T1, VK_T0, VK_T1, VK_T0, VK_T1, VK_T0, VK_T1,\n         VK_T1, VK_T0, VK_T1, VK_T0, VK_T1, VK_T0, VK_T1, VK_T0,\n         VK_T0, VK_T1, VK_T0, VK_T1, VK_T0, VK_T1, VK_T0, VK_T1,\n         VK_T1, VK_T0, VK_T1, VK_T0, VK_T1, VK_T0, VK_T1, VK_T0,\n      };\n      *texture                = vulkan_create_texture(vk, NULL,\n            8, 8, VK_FORMAT_B8G8R8A8_UNORM,\n            checkerboard, NULL, VULKAN_TEXTURE_STATIC);\n      texture->flags         &= ~(VK_TEX_FLAG_DEFAULT_SMOOTH\n                                | VK_TEX_FLAG_MIPMAP);\n   }\n   else\n   {\n      *texture = vulkan_create_texture(vk, NULL,\n            image->width, image->height, VK_FORMAT_B8G8R8A8_UNORM,\n            image->pixels, NULL, VULKAN_TEXTURE_STATIC);\n      if (filter_type == TEXTURE_FILTER_MIPMAP_LINEAR || filter_type ==\n            TEXTURE_FILTER_LINEAR)\n         texture->flags |= VK_TEX_FLAG_DEFAULT_SMOOTH;\n      if (filter_type == TEXTURE_FILTER_MIPMAP_LINEAR)\n         texture->flags |= VK_TEX_FLAG_MIPMAP;\n   }\n\n   return (uintptr_t)texture;\n}\n', '/* patches/series, 0077: upload decoded menu images as RGBA */\nstatic uintptr_t vulkan_load_texture(void *video_data, void *data,\n      bool threaded, enum texture_filter_type filter_type)\n{\n   struct vk_texture *texture  = NULL;\n   vk_t *vk                    = (vk_t*)video_data;\n   struct texture_image *image = (struct texture_image*)data;\n   if (!image)\n      return 0;\n\n   if (!(texture = (struct vk_texture*)calloc(1, sizeof(*texture))))\n      return 0;\n\n   if (!image->pixels || !image->width || !image->height)\n   {\n      /* Create a dummy texture instead. */\n      static const uint32_t checkerboard[] = {\n         VK_T0, VK_T1, VK_T0, VK_T1, VK_T0, VK_T1, VK_T0, VK_T1,\n         VK_T1, VK_T0, VK_T1, VK_T0, VK_T1, VK_T0, VK_T1, VK_T0,\n         VK_T0, VK_T1, VK_T0, VK_T1, VK_T0, VK_T1, VK_T0, VK_T1,\n         VK_T1, VK_T0, VK_T1, VK_T0, VK_T1, VK_T0, VK_T1, VK_T0,\n         VK_T0, VK_T1, VK_T0, VK_T1, VK_T0, VK_T1, VK_T0, VK_T1,\n         VK_T1, VK_T0, VK_T1, VK_T0, VK_T1, VK_T0, VK_T1, VK_T0,\n         VK_T0, VK_T1, VK_T0, VK_T1, VK_T0, VK_T1, VK_T0, VK_T1,\n         VK_T1, VK_T0, VK_T1, VK_T0, VK_T1, VK_T0, VK_T1, VK_T0,\n      };\n      *texture                = vulkan_create_texture(vk, NULL,\n            8, 8, VK_FORMAT_R8G8B8A8_UNORM,\n            checkerboard, NULL, VULKAN_TEXTURE_STATIC);\n      texture->flags         &= ~(VK_TEX_FLAG_DEFAULT_SMOOTH\n                                | VK_TEX_FLAG_MIPMAP);\n   }\n   else\n   {\n      *texture = vulkan_create_texture(vk, NULL,\n            image->width, image->height, VK_FORMAT_R8G8B8A8_UNORM,\n            image->pixels, NULL, VULKAN_TEXTURE_STATIC);\n      if (filter_type == TEXTURE_FILTER_MIPMAP_LINEAR || filter_type ==\n            TEXTURE_FILTER_LINEAR)\n         texture->flags |= VK_TEX_FLAG_DEFAULT_SMOOTH;\n      if (filter_type == TEXTURE_FILTER_MIPMAP_LINEAR)\n         texture->flags |= VK_TEX_FLAG_MIPMAP;\n   }\n\n   return (uintptr_t)texture;\n}\n', 'patches/series, 0077: upload decoded menu images as RGBA'),
    ('gfx/drivers_shader/shader_vulkan.h', '   VkFormat format;', '   VkFormat format;\n   /* patches/series, 0078: zero means no padded source image. */\n   unsigned physical_width;', 'patches/series, 0078: zero means'),
    ('gfx/drivers/vulkan.c', '      struct vulkan_filter_chain_texture input;', '      /* patches/series, 0078: hardware images retain their own extent. */\n      struct vulkan_filter_chain_texture input = {0};', 'patches/series, 0078: hardware images'),
    ('gfx/drivers/vulkan.c', '            input.format       = vk->default_texture.format;', '            input.format       = vk->default_texture.format;\n            /* patches/series, 0078: black fallback is also padded. */\n            input.physical_width = ps5_vulkan_texture_width(input.width,\n                  vulkan_format_to_bpp(input.format));', 'patches/series, 0078: black fallback'),
    ('gfx/drivers/vulkan.c', "         input.format = VK_FORMAT_UNDEFINED; /* It's already configured. */", "         input.format = VK_FORMAT_UNDEFINED; /* It's already configured. */\n         /* patches/series, 0078: preserve logical size and expose sampled extent. */\n         input.physical_width = ps5_vulkan_texture_width(tex->width,\n               vulkan_format_to_bpp(tex->format));", 'patches/series, 0078: preserve logical size'),
    ('gfx/drivers_shader/shader_vulkan.cpp', '   Texture source;', '   /* patches/series, 0078: offscreen source starts without padding. */\n   Texture source = {};', 'patches/series, 0078: offscreen source starts'),
    ('gfx/drivers_shader/shader_vulkan.cpp', '   unsigned i;\n   Texture source;\n\n   /* First frame, make sure our history and', '   unsigned i;\n   /* patches/series, 0078: viewport source starts without padding. */\n   Texture source = {};\n\n   /* First frame, make sure our history and', 'patches/series, 0078: viewport source starts'),
    ('gfx/drivers_shader/shader_vulkan.cpp', '      source.texture.height   = fb.get_size().height;', '      source.texture.height   = fb.get_size().height;\n      /* patches/series, 0078: pass outputs have an unpadded sampled extent. */\n      source.texture.physical_width = 0;', 'patches/series, 0078: pass outputs have'),
    ('gfx/drivers_shader/shader_vulkan.cpp', '      bool final_pass;', '      bool final_pass;\n      /* patches/series, 0078: per-sync UV buffers cannot race submitted draws. */\n      std::vector<std::unique_ptr<Buffer>> ps5_source_vbos;', 'patches/series, 0078: per-sync UV buffers'),
    ('gfx/drivers_shader/shader_vulkan.cpp', '   uint8_t *u       = nullptr;\n\n   curr_vp', '   uint8_t *u       = nullptr;\n   /* patches/series, 0078: crop software source UVs to initialized texels. */\n   VkBuffer ps5_source_vbo = common->vbo->get_buffer();\n   if (source.texture.physical_width > source.texture.width)\n   {\n      extern void ps5_core_source_quad(float *, unsigned, unsigned);\n      if (ps5_source_vbos.empty())\n         ps5_source_vbos.resize(num_sync_indices);\n      auto &vbo = ps5_source_vbos[sync_index];\n      if (!vbo)\n         vbo.reset(new Buffer(device, memory_properties, 48 * sizeof(float),\n                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT));\n      ps5_core_source_quad(static_cast<float *>(vbo->map()),\n            source.texture.width, source.texture.physical_width);\n      vbo->unmap();\n      ps5_source_vbo = vbo->get_buffer();\n   }\n\n   curr_vp', 'patches/series, 0078: crop software source UVs'),
    ('gfx/drivers_shader/shader_vulkan.cpp', '            &common->vbo->get_buffer(),', '            /* patches/series, 0078: bind cropped first triangle. */\n            &ps5_source_vbo,', 'patches/series, 0078: bind cropped first'),
    ('gfx/drivers_shader/shader_vulkan.cpp', '      VkBuffer buffer           = common->vbo->get_buffer();', '      /* patches/series, 0078: both triangles use the same cropped UVs. */\n      VkBuffer buffer           = ps5_source_vbo;', 'patches/series, 0078: both triangles'),
    ('gfx/drivers_shader/shader_vulkan.cpp', 'void Pass::clear_vk()\n{', 'void Pass::clear_vk()\n{\n   /* patches/series, 0078: rebuild UV slots with the swapchain. */\n   ps5_source_vbos.clear();', 'patches/series, 0078: rebuild UV slots'),
    ('gfx/drivers/vulkan.c', '      info.extent.width = ps5_vulkan_texture_width(width, vulkan_format_to_bpp(format));', '      /* patches/series, 0078: padded width follows the actual sampled format. */\n      info.extent.width = ps5_vulkan_texture_width(width, vulkan_format_to_bpp(request));', 'patches/series, 0078: padded width follows'),
    ('menu/drivers/xmb.c', 'static float xmb_scale_mod[8]   = {', '/* patches/series, 0079: numeric XMB context before allocation */\n#include "memory_xmb.h"\n#ifdef PS5_MEMORY_DIAGNOSTICS\nstatic void ps5_xmb_observe(xmb_handle_t *xmb, unsigned phase,\n      const file_list_t *list, size_t index)\n{\n   struct menu_state *state = menu_state_get_ptr();\n   file_list_t *current = MENU_LIST_GET_SELECTION(state->entries.list, 0);\n   if (xmb)\n      ps5_memory_xmb_context((unsigned)xmb->categories_selection_ptr,\n            xmb->categories_selection_ptr <= xmb->system_tab_end\n                  ? xmb->tabs[xmb->categories_selection_ptr] : UINT_MAX,\n            current ? current->size : 0, xmb->selection_buf_old.size,\n            xmb->horizontal_list.size);\n   ps5_memory_xmb_stage(phase, list ? list->size : 0, index);\n}\n#else\n#define ps5_xmb_observe(...) ((void)0)\n#endif\n\nstatic float xmb_scale_mod[8]   = {', 'patches/series, 0079: numeric XMB context before allocation'),
    ('menu/drivers/xmb.c', '   node->alpha        = node->label_alpha  = 0;', '   /* patches/series, 0079: count allocated nodes */\n   ps5_memory_xmb_node(1);\n   node->alpha        = node->label_alpha  = 0;', 'patches/series, 0079: count allocated nodes'),
    ('menu/drivers/xmb.c', '   free(node);\n}', '   /* patches/series, 0079: count released nodes */\n   ps5_memory_xmb_node(0);\n   free(node);\n}', 'patches/series, 0079: count released nodes'),
    ('menu/drivers/xmb.c', '   *new_node              = *old_node;', '   /* patches/series, 0079: count copied nodes */\n   ps5_memory_xmb_node(2);\n   *new_node              = *old_node;', 'patches/series, 0079: count copied nodes'),
    ('menu/drivers/xmb.c', '   if (!(node = (xmb_node_t*)list->list[i].userdata))', '   /* patches/series, 0079: cache insertion progress */\n   ps5_xmb_observe(xmb, PS5_XMB_INSERT, list, list_size);\n   if (!(node = (xmb_node_t*)list->list[i].userdata))', 'patches/series, 0079: cache insertion progress'),
    ('menu/drivers/xmb.c', '   xmb->show_playlist_tabs            = settings->bools.menu_content_show_playlist_tabs;', '   /* patches/series, 0079: cache populated list */\n   ps5_xmb_observe(xmb, PS5_XMB_POPULATE,\n         MENU_LIST_GET_SELECTION(menu_list, 0), menu_st->selection_ptr);\n   xmb->show_playlist_tabs            = settings->bools.menu_content_show_playlist_tabs;', 'patches/series, 0079: cache populated list'),
    ('menu/drivers/xmb.c', "   /* Check whether to enable the horizontal animation.\n    * Deep copy required for 'previous' icon. */", "   /* patches/series, 0079: cache outgoing tab */\n   ps5_xmb_observe(xmb, PS5_XMB_CACHE_BEGIN, selection_buf, selection);\n   /* Check whether to enable the horizontal animation.\n    * Deep copy required for 'previous' icon. */", 'patches/series, 0079: cache outgoing tab'),
    ('menu/drivers/xmb.c', '         stack_size = menu_stack->size;', '   /* patches/series, 0079: cache destination tab */\n         ps5_xmb_observe(xmb, PS5_XMB_CACHE_END, selection_buf, selection);\n         stack_size = menu_stack->size;', 'patches/series, 0079: cache destination tab'),
    ('menu/drivers/xmb.c', 'static void xmb_list_clear(file_list_t *list)\n{\n   uintptr_t tag = (uintptr_t)list;\n\n   gfx_animation_kill_by_tag(&tag);\n\n   xmb_free_list_nodes(list, false);\n}', 'static void xmb_list_clear(file_list_t *list)\n{\n   uintptr_t tag = (uintptr_t)list;\n\n   /* patches/series, 0079: cache node clear boundaries */\n   ps5_memory_xmb_stage(PS5_XMB_CLEAR_BEGIN, list ? list->size : 0, 0);\n   gfx_animation_kill_by_tag(&tag);\n\n   xmb_free_list_nodes(list, false);\n   ps5_memory_xmb_stage(PS5_XMB_CLEAR_END, list ? list->size : 0, 0);\n}', 'patches/series, 0079: cache node clear boundaries'),
    ('menu/drivers/xmb.c', '   xmb_free_list_nodes(dst, true);\n\n   file_list_clear(dst);', '   /* patches/series, 0079: cache copy boundaries */\n   ps5_memory_xmb_stage(PS5_XMB_COPY_BEGIN, src->size, (last + 1) - first);\n   xmb_free_list_nodes(dst, true);\n\n   file_list_clear(dst);', 'patches/series, 0079: cache copy boundaries'),
    ('menu/drivers/xmb.c', '   dst->size = j;\n}', '   dst->size = j;\n   /* patches/series, 0079: cache completed copy */\n   ps5_memory_xmb_stage(PS5_XMB_COPY_END, dst->size, j);\n}', 'patches/series, 0079: cache completed copy'),
    ('menu/drivers/xmb.c', '   gfx_thumbnail_path_data_t thumbnail_path_data;', '   /* patches/series, 0080: visible icon paths only */\n   gfx_thumbnail_path_data_t *thumbnail_path_data;', 'patches/series, 0080: visible icon paths only'),
    ('menu/drivers/xmb.c', '#include "memory_xmb.h"', '#include "memory_xmb.h"\n/* patches/series, 0080: mapped menu objects */\n#include "menu_memory.h"', 'patches/series, 0080: mapped menu objects'),
    ('menu/drivers/xmb.c', 'static xmb_node_t *xmb_alloc_node(void)\n{\n   xmb_node_t *node = (xmb_node_t*)malloc(sizeof(*node));\n\n   if (!node)\n      return NULL;\n\n   /* patches/series, 0079: count allocated nodes */\n   ps5_memory_xmb_node(1);\n   node->alpha        = node->label_alpha  = 0;\n   node->zoom         = node->x = node->y  = 0;\n   node->icon         = node->content_icon = 0;\n   node->thumbnail_icon.icon.texture       = 0;\n   node->fullpath     = NULL;\n   node->console_name = NULL;\n   node->icon_hide    = false;\n\n   return node;\n}', '/* patches/series, 0080: compact initialized node */\nstatic xmb_node_t *xmb_alloc_node(void)\n{\n   xmb_node_t *node = (xmb_node_t*)ps5_menu_malloc(sizeof(*node));\n\n   if (!node)\n      return NULL;\n\n   /* patches/series, 0079: count allocated nodes */\n   ps5_memory_xmb_node(1);\n   node->alpha        = node->label_alpha  = 0;\n   node->zoom         = node->x = node->y  = 0;\n   node->icon         = node->content_icon = 0;\n   memset(&node->thumbnail_icon, 0, sizeof(node->thumbnail_icon));\n   node->fullpath     = NULL;\n   node->console_name = NULL;\n   node->icon_hide    = false;\n\n   return node;\n}', 'patches/series, 0080: compact initialized node'),
    ('menu/drivers/xmb.c', 'static void xmb_free_node(xmb_node_t *node)\n{\n   if (!node)\n      return;\n\n   if (node->fullpath)\n      free(node->fullpath);\n\n   node->fullpath = NULL;\n   gfx_thumbnail_reset(&node->thumbnail_icon.icon);\n\n   /* patches/series, 0079: count released nodes */\n   ps5_memory_xmb_node(0);\n   free(node);\n}', '/* patches/series, 0080: release all node ownership */\nstatic void xmb_free_node(xmb_node_t *node)\n{\n   if (!node)\n      return;\n\n   if (node->fullpath)\n      free(node->fullpath);\n\n   free(node->console_name);\n   free(node->thumbnail_icon.thumbnail_path_data);\n   node->fullpath = NULL;\n   gfx_thumbnail_reset(&node->thumbnail_icon.icon);\n\n   /* patches/series, 0079: count released nodes */\n   ps5_memory_xmb_node(0);\n   free(node);\n}', 'patches/series, 0080: release all node ownership'),
    ('menu/drivers/xmb.c', 'static xmb_node_t *xmb_copy_node(const xmb_node_t *old_node)\n{\n   xmb_node_t *new_node = (xmb_node_t*)malloc(sizeof(*new_node));\n\n   if (!new_node)\n      return NULL;\n\n   /* patches/series, 0079: count copied nodes */\n   ps5_memory_xmb_node(2);\n   *new_node              = *old_node;\n   new_node->fullpath     = old_node->fullpath ? strdup(old_node->fullpath) : NULL;\n   new_node->console_name = old_node->console_name ? strdup(old_node->console_name) : NULL;\n   new_node->thumbnail_icon.icon.texture = 0;\n\n   return new_node;\n}', '/* patches/series, 0080: transactional node copy */\nstatic xmb_node_t *xmb_copy_node(const xmb_node_t *old_node)\n{\n   xmb_node_t *new_node = (xmb_node_t*)ps5_menu_malloc(sizeof(*new_node));\n   if (!new_node)\n      return NULL;\n   /* patches/series, 0079: count copied nodes */\n   ps5_memory_xmb_node(2);\n   *new_node = *old_node;\n   new_node->fullpath = NULL;\n   new_node->console_name = NULL;\n   new_node->thumbnail_icon.thumbnail_path_data = NULL;\n   memset(&new_node->thumbnail_icon.icon, 0, sizeof(new_node->thumbnail_icon.icon));\n   if (old_node->fullpath && !(new_node->fullpath = strdup(old_node->fullpath)))\n      goto error;\n   if (old_node->console_name && !(new_node->console_name = strdup(old_node->console_name)))\n      goto error;\n   if (old_node->thumbnail_icon.thumbnail_path_data)\n   {\n      new_node->thumbnail_icon.thumbnail_path_data = (gfx_thumbnail_path_data_t*)\n            malloc(sizeof(gfx_thumbnail_path_data_t));\n      if (!new_node->thumbnail_icon.thumbnail_path_data)\n         goto error;\n      *new_node->thumbnail_icon.thumbnail_path_data = *old_node->thumbnail_icon.thumbnail_path_data;\n   }\n   return new_node;\nerror:\n   xmb_free_node(new_node);\n   return NULL;\n}', 'patches/series, 0080: transactional node copy'),
    ('menu/drivers/xmb.c', 'static void xmb_set_dynamic_icon_content(\n      void *xmb_handle_ptr,\n      const char *s,\n      size_t selection,\n      xmb_icons_t *thumbnail_icon)\n{\n   xmb_handle_t *xmb          = (xmb_handle_t*)xmb_handle_ptr;\n\n   if (!xmb || !xmb->is_playlist)\n      return;\n\n   /* Playlist content */\n   if (string_is_empty(s))\n   {\n      struct menu_state *menu_st = menu_state_get_ptr();\n      menu_list_t *menu_list     = menu_st->entries.list;\n      size_t list_size           = (unsigned)MENU_LIST_GET_SELECTION(menu_list, 0)->size;\n      file_list_t *list          = MENU_LIST_GET_SELECTION(menu_list, 0);\n      bool playlist_valid        = false;\n      size_t playlist_index      = selection;\n\n      /* Get playlist index corresponding\n       * to the selected entry */\n      if (    list\n            && (selection < list_size)\n            && (list->list[selection].type == FILE_TYPE_RPL_ENTRY))\n      {\n         playlist_valid = true;\n         playlist_index = list->list[selection].entry_idx;\n      }\n      gfx_thumbnail_set_icon_playlist(&thumbnail_icon->thumbnail_path_data,\n            playlist_valid ? playlist_get_cached() : NULL, playlist_index);\n   }\n}', '/* patches/series, 0080: lazy optional icon paths */\nstatic void xmb_set_dynamic_icon_content(\n      void *xmb_handle_ptr,\n      const char *s,\n      size_t selection,\n      xmb_icons_t *thumbnail_icon)\n{\n   xmb_handle_t *xmb          = (xmb_handle_t*)xmb_handle_ptr;\n\n   if (!xmb || !xmb->is_playlist)\n      return;\n\n   if (!thumbnail_icon->thumbnail_path_data)\n      thumbnail_icon->thumbnail_path_data = (gfx_thumbnail_path_data_t*)\n            calloc(1, sizeof(gfx_thumbnail_path_data_t));\n   if (!thumbnail_icon->thumbnail_path_data)\n      return; /* Optional icon: ordinary entry remains navigable. */\n\n   /* Playlist content */\n   if (string_is_empty(s))\n   {\n      struct menu_state *menu_st = menu_state_get_ptr();\n      menu_list_t *menu_list     = menu_st->entries.list;\n      size_t list_size           = (unsigned)MENU_LIST_GET_SELECTION(menu_list, 0)->size;\n      file_list_t *list          = MENU_LIST_GET_SELECTION(menu_list, 0);\n      bool playlist_valid        = false;\n      size_t playlist_index      = selection;\n\n      /* Get playlist index corresponding\n       * to the selected entry */\n      if (    list\n            && (selection < list_size)\n            && (list->list[selection].type == FILE_TYPE_RPL_ENTRY))\n      {\n         playlist_valid = true;\n         playlist_index = list->list[selection].entry_idx;\n      }\n      gfx_thumbnail_set_icon_playlist(thumbnail_icon->thumbnail_path_data,\n            playlist_valid ? playlist_get_cached() : NULL, playlist_index);\n   }\n}', 'patches/series, 0080: lazy optional icon paths'),
    ('menu/drivers/xmb.c', '      iy               = xmb_item_y(xmb, i, selection);', '      /* patches/series, 0080: discard paths outside visible slice */\n      if (i < entry_start || i > entry_end)\n      {\n         free(node->thumbnail_icon.thumbnail_path_data);\n         node->thumbnail_icon.thumbnail_path_data = NULL;\n      }\n      iy               = xmb_item_y(xmb, i, selection);', 'patches/series, 0080: discard paths outside visible slice'),
    ('menu/drivers/xmb.c', '            && gfx_thumbnail_is_enabled(&node->thumbnail_icon.thumbnail_path_data, GFX_THUMBNAIL_ICON)', '            /* patches/series, 0080: missing optional icon paths */\n            && node->thumbnail_icon.thumbnail_path_data\n            && gfx_thumbnail_is_enabled(node->thumbnail_icon.thumbnail_path_data, GFX_THUMBNAIL_ICON)', 'patches/series, 0080: missing optional icon paths'),
    ('menu/drivers/xmb.c', '         if (cur_per_frame >= max_per_frame)', '         /* patches/series, 0080: path allocation may fail */\n         if (!thumbnail_icon->thumbnail_path_data)\n         {\n            node->icon_hide = false;\n            continue;\n         }\n         if (cur_per_frame >= max_per_frame)', 'patches/series, 0080: path allocation may fail'),
    ('menu/drivers/xmb.c', '                     thumbnail_icon->thumbnail_path_data.icon_path,', '                     /* patches/series, 0080: indirect icon path */\n                     thumbnail_icon->thumbnail_path_data->icon_path,', 'patches/series, 0080: indirect icon path'),
    ('menu/drivers/xmb.c', '                     &thumbnail_icon->thumbnail_path_data,', '                     /* patches/series, 0080: indirect stream path */\n                     thumbnail_icon->thumbnail_path_data,', 'patches/series, 0080: indirect stream path'),
    ('menu/drivers/xmb.c', '         for (i = entry_start; i <= entry_end; i++)', '         /* patches/series, 0080: bounded dynamic icons */\n         for (i = entry_start; i < end && i <= entry_end; i++)', 'patches/series, 0080: bounded dynamic icons'),
    ('menu/drivers/xmb.c', '      for (i = first; i <= last; i++)', '      /* patches/series, 0080: bounded pending icons */\n      for (i = first; i < end && i <= last; i++)', 'patches/series, 0080: bounded pending icons'),
    ('menu/drivers/xmb.c', 'static void xmb_list_insert(void *userdata,\n      file_list_t *list,\n      const char *path,\n      const char *fullpath,\n      const char *unused,\n      size_t list_size,\n      unsigned entry_type)\n{\n   int current                = 0;\n   int i                      = (int)list_size;\n   xmb_node_t *node           = NULL;\n   xmb_handle_t *xmb          = (xmb_handle_t*)userdata;\n   struct menu_state *menu_st = menu_state_get_ptr();\n   size_t selection           = menu_st->selection_ptr;\n\n   if (!xmb || !list)\n      return;\n\n   /* patches/series, 0079: cache insertion progress */\n   ps5_xmb_observe(xmb, PS5_XMB_INSERT, list, list_size);\n   if (!(node = (xmb_node_t*)list->list[i].userdata))\n   {\n      if (!(node = xmb_alloc_node()))\n         return;\n   }\n\n   current           = (int)selection;\n\n   if (!string_is_empty(fullpath))\n   {\n      if (node->fullpath)\n         free(node->fullpath);\n\n      node->fullpath = strdup(fullpath);\n   }\n\n   node->alpha       = xmb->items_passive_alpha;\n   node->zoom        = xmb->items_passive_zoom;\n   node->label_alpha = node->alpha;\n   node->y           = xmb_item_y(xmb, i, current);\n   node->x           = 0;\n\n   if (i == current)\n   {\n      node->alpha       = xmb->items_active_alpha;\n      node->label_alpha = xmb->items_active_alpha;\n      node->zoom        = xmb->items_active_alpha;\n   }\n\n   list->list[i].userdata = node;\n}', '/* patches/series, 0080: reject incomplete node */\nstatic void xmb_list_insert(void *userdata,\n      file_list_t *list,\n      const char *path,\n      const char *fullpath,\n      const char *unused,\n      size_t list_size,\n      unsigned entry_type)\n{\n   int current                = 0;\n   int i                      = (int)list_size;\n   xmb_node_t *node           = NULL;\n   xmb_handle_t *xmb          = (xmb_handle_t*)userdata;\n   struct menu_state *menu_st = menu_state_get_ptr();\n   size_t selection           = menu_st->selection_ptr;\n\n   if (!xmb || !list || list_size >= list->size)\n      return;\n\n   /* patches/series, 0079: cache insertion progress */\n   ps5_xmb_observe(xmb, PS5_XMB_INSERT, list, list_size);\n   if (!(node = (xmb_node_t*)list->list[i].userdata))\n   {\n      if (!(node = xmb_alloc_node()))\n         return;\n   }\n\n   current           = (int)selection;\n\n   if (!string_is_empty(fullpath))\n   {\n      char *copy = strdup(fullpath);\n      if (!copy)\n      {\n         if (!list->list[i].userdata)\n            xmb_free_node(node);\n         return;\n      }\n      free(node->fullpath);\n      node->fullpath = copy;\n   }\n\n   node->alpha       = xmb->items_passive_alpha;\n   node->zoom        = xmb->items_passive_zoom;\n   node->label_alpha = node->alpha;\n   node->y           = xmb_item_y(xmb, i, current);\n   node->x           = 0;\n\n   if (i == current)\n   {\n      node->alpha       = xmb->items_active_alpha;\n      node->label_alpha = xmb->items_active_alpha;\n      node->zoom        = xmb->items_active_alpha;\n   }\n\n   list->list[i].userdata = node;\n}', 'patches/series, 0080: reject incomplete node'),
    ('menu/drivers/xmb.c', 'static void xmb_list_deep_copy(const file_list_t *src,\n      file_list_t *dst, size_t first, size_t last)\n{\n   size_t i, j   = 0;\n   uintptr_t tag = (uintptr_t)dst;\n\n   gfx_animation_kill_by_tag(&tag);\n\n   /* patches/series, 0079: cache copy boundaries */\n   ps5_memory_xmb_stage(PS5_XMB_COPY_BEGIN, src->size, (last + 1) - first);\n   xmb_free_list_nodes(dst, true);\n\n   file_list_clear(dst);\n   file_list_reserve(dst, (last + 1) - first);\n\n   for (i = first; i <= last; ++i)\n   {\n      struct item_file *d = &dst->list[j];\n      struct item_file *s = &src->list[i];\n\n      void *src_udata = s->userdata;\n      void *src_adata = s->actiondata;\n\n      *d       = *s;\n      d->alt   = string_is_empty(d->alt)   ? NULL : strdup(d->alt);\n      d->path  = string_is_empty(d->path)  ? NULL : strdup(d->path);\n      d->label = string_is_empty(d->label) ? NULL : strdup(d->label);\n\n      if (src_udata)\n         dst->list[j].userdata = (void*)\n            xmb_copy_node((const xmb_node_t*)src_udata);\n\n      if (src_adata)\n      {\n         void *data = malloc(sizeof(menu_file_list_cbs_t));\n         memcpy(data, src_adata, sizeof(menu_file_list_cbs_t));\n         dst->list[j].actiondata = data;\n      }\n\n      ++j;\n   }\n\n   dst->size = j;\n   /* patches/series, 0079: cache completed copy */\n   ps5_memory_xmb_stage(PS5_XMB_COPY_END, dst->size, j);\n}', '/* patches/series, 0080: failure atomic animation copy */\nstatic void xmb_list_deep_copy(const file_list_t *src,\n      file_list_t *dst, size_t first, size_t last)\n{\n   size_t i;\n   uintptr_t tag = (uintptr_t)dst;\n   gfx_animation_kill_by_tag(&tag);\n   /* patches/series, 0079: cache copy boundaries */\n   ps5_memory_xmb_stage(PS5_XMB_COPY_BEGIN, src->size, 0);\n   xmb_free_list_nodes(dst, true);\n   file_list_clear(dst);\n   if (first > last || first >= src->size)\n      return;\n   if (last >= src->size)\n      last = src->size - 1;\n   if (dst->capacity < last - first + 1 &&\n         !file_list_reserve(dst, last - first + 1))\n      goto error;\n   for (i = first; i <= last; ++i)\n   {\n      const struct item_file *s = &src->list[i];\n      struct item_file *d = &dst->list[dst->size++];\n      *d = *s;\n      d->path = d->label = d->alt = NULL;\n      d->userdata = d->actiondata = NULL;\n      if (!string_is_empty(s->path) && !(d->path = strdup(s->path)))\n         goto error;\n      if (!string_is_empty(s->label) && !(d->label = strdup(s->label)))\n         goto error;\n      if (!string_is_empty(s->alt) && !(d->alt = strdup(s->alt)))\n         goto error;\n      if (s->userdata && !(d->userdata = xmb_copy_node((const xmb_node_t*)s->userdata)))\n         goto error;\n      if (s->actiondata)\n      {\n         d->actiondata = ps5_menu_malloc(sizeof(menu_file_list_cbs_t));\n         if (!d->actiondata)\n            goto error;\n         memcpy(d->actiondata, s->actiondata, sizeof(menu_file_list_cbs_t));\n      }\n   }\n   /* patches/series, 0079: cache completed copy */\n   ps5_memory_xmb_stage(PS5_XMB_COPY_END, dst->size, dst->size);\n   return;\nerror:\n   /* Animation is optional. Keep the source list intact and publish no partial copy. */\n   xmb_free_list_nodes(dst, true);\n   file_list_clear(dst);\n}', 'patches/series, 0080: failure atomic animation copy'),
    ('libretro-common/include/lists/file_list.h', '   size_t capacity;\n   size_t size;', '   size_t capacity;\n   size_t size;\n   /* patches/series, 0080: stop population after allocation failure */\n   bool allocation_failed;', 'patches/series, 0080: stop population after allocation failure'),
    ('libretro-common/lists/file_list.c', 'void file_list_clear(file_list_t *list)\n{\n   size_t i;\n\n   if (!list)\n      return;\n\n   for (i = 0; i < list->size; i++)\n   {\n      if (list->list[i].path)\n         free(list->list[i].path);\n      list->list[i].path = NULL;\n\n      if (list->list[i].label)\n         free(list->list[i].label);\n      list->list[i].label = NULL;\n\n      if (list->list[i].alt)\n         free(list->list[i].alt);\n      list->list[i].alt = NULL;\n   }\n\n   list->size = 0;\n}', '/* patches/series, 0080: reset failure on file_list_clear */\nvoid file_list_clear(file_list_t *list)\n{\n   size_t i;\n\n   if (!list)\n      return;\n\n   for (i = 0; i < list->size; i++)\n   {\n      if (list->list[i].path)\n         free(list->list[i].path);\n      list->list[i].path = NULL;\n\n      if (list->list[i].label)\n         free(list->list[i].label);\n      list->list[i].label = NULL;\n\n      if (list->list[i].alt)\n         free(list->list[i].alt);\n      list->list[i].alt = NULL;\n   }\n\n   list->allocation_failed = false;\n   list->size = 0;\n}', 'patches/series, 0080: reset failure on file_list_clear'),
    ('libretro-common/lists/file_list.c', 'bool file_list_deinitialize(file_list_t *list)\n{\n   if (!list)\n      return false;\n   if (!file_list_deinitialize_internal(list))\n      return false;\n   list->capacity = 0;\n   list->size     = 0;\n   return true;\n}', '/* patches/series, 0080: reset failure on file_list_deinitialize */\nbool file_list_deinitialize(file_list_t *list)\n{\n   if (!list)\n      return false;\n   if (!file_list_deinitialize_internal(list))\n      return false;\n   list->capacity = 0;\n   list->allocation_failed = false;\n   list->size     = 0;\n   return true;\n}', 'patches/series, 0080: reset failure on file_list_deinitialize'),
    ('libretro-common/lists/file_list.c', 'bool file_list_append(file_list_t *list,\n      const char *path, const char *label,\n      unsigned type, size_t directory_ptr,\n      size_t entry_idx)\n{\n   unsigned idx = (unsigned)list->size;\n   /* Expand file list if needed */\n   if (idx >= list->capacity)\n      if (!file_list_reserve(list, list->capacity * 2 + 1))\n         return false;\n\n   list->list[idx].path          = NULL;\n   list->list[idx].label         = NULL;\n   list->list[idx].alt           = NULL;\n   list->list[idx].type          = type;\n   list->list[idx].directory_ptr = directory_ptr;\n   list->list[idx].entry_idx     = entry_idx;\n   list->list[idx].userdata      = NULL;\n   list->list[idx].actiondata    = NULL;\n\n   if (label)\n      list->list[idx].label      = strdup(label);\n   if (path)\n      list->list[idx].path       = strdup(path);\n\n   list->size++;\n\n   return true;\n}', '/* patches/series, 0080: checked file_list_append */\nbool file_list_append(file_list_t *list,\n      const char *path, const char *label,\n      unsigned type, size_t directory_ptr,\n      size_t entry_idx)\n{\n   struct item_file item = {0};\n   if (!list || list->allocation_failed) return false;\n\n   if (list->size >= list->capacity &&\n         (list->capacity > ((size_t)-1 - 1) / 2 ||\n          !file_list_reserve(list, list->capacity * 2 + 1)))\n      goto error;\n   if (path && !(item.path = strdup(path))) goto error;\n   if (label && !(item.label = strdup(label))) goto error;\n   item.type = type;\n   item.directory_ptr = directory_ptr;\n   item.entry_idx = entry_idx;\n   list->list[list->size] = item;\n   ++list->size;\n   return true;\nerror:\n   free(item.path);\n   free(item.label);\n   list->allocation_failed = true;\n   return false;\n}', 'patches/series, 0080: checked file_list_append'),
    ('libretro-common/lists/file_list.c', 'bool file_list_insert(file_list_t *list,\n      const char *path, const char *label,\n      unsigned type, size_t directory_ptr,\n      size_t entry_idx,\n      size_t idx)\n{\n   /* Expand file list if needed */\n   if (list->size >= list->capacity)\n   {\n      size_t new_capacity = list->capacity > 0 ? list->capacity * 2 : 1;\n      if (!file_list_reserve(list, new_capacity))\n         return false;\n   }\n\n   /* Shift elements to the right using memmove */\n   if (idx < list->size)\n      memmove(&list->list[idx + 1], &list->list[idx],\n            (list->size - idx) * sizeof(struct item_file));\n\n   init_item_file(&list->list[idx], path, label, type, directory_ptr, entry_idx);\n   list->size++;\n\n   return true;\n}', '/* patches/series, 0080: checked file_list_insert */\nbool file_list_insert(file_list_t *list,\n      const char *path, const char *label,\n      unsigned type, size_t directory_ptr,\n      size_t entry_idx,\n      size_t idx)\n{\n   struct item_file item = {0};\n   if (!list || list->allocation_failed) return false;\n   if (idx > list->size) return false;\n\n   if (list->size >= list->capacity &&\n         (list->capacity > ((size_t)-1 - 1) / 2 ||\n          !file_list_reserve(list, list->capacity * 2 + 1)))\n      goto error;\n   if (path && !(item.path = strdup(path))) goto error;\n   if (label && !(item.label = strdup(label))) goto error;\n   item.type = type;\n   item.directory_ptr = directory_ptr;\n   item.entry_idx = entry_idx;\n   if (idx < list->size)\n      memmove(&list->list[idx + 1], &list->list[idx],\n            (list->size - idx) * sizeof(struct item_file));\n   list->list[idx] = item;\n   ++list->size;\n   return true;\nerror:\n   free(item.path);\n   free(item.label);\n   list->allocation_failed = true;\n   return false;\n}', 'patches/series, 0080: checked file_list_insert'),
    ('menu/menu_driver.c', '#include <retro_timers.h>', '#include <retro_timers.h>\n/* patches/series, 0080: mapped callback ownership */\n#include "menu_memory.h"', 'patches/series, 0080: mapped callback ownership'),
    ('menu/menu_driver.c', "bool menu_entries_append(\n      file_list_t *list,\n      const char *path,\n      const char *label,\n      enum msg_hash_enums enum_idx,\n      unsigned type,\n      size_t directory_ptr,\n      size_t entry_idx,\n      rarch_setting_t *setting)\n{\n   menu_ctx_list_t list_info;\n   size_t i;\n   size_t idx, lbl_len;\n   const char *menu_path       = NULL;\n   menu_file_list_cbs_t *cbs   = NULL;\n   struct menu_state  *menu_st = &menu_driver_state;\n   const file_list_t *mlist    = MENU_LIST_GET(menu_st->entries.list, 0);\n\n   if (!list || !label)\n      return false;\n\n   file_list_append(list, path, label, type, directory_ptr, entry_idx);\n   if (mlist && mlist->size)\n      menu_path          = mlist->list[mlist->size - 1].path;\n   idx                   = list->size - 1;\n\n   list_info.fullpath    = NULL;\n\n   if (!string_is_empty(menu_path))\n      list_info.fullpath = strdup(menu_path);\n   list_info.list        = list;\n   list_info.path        = path;\n   list_info.label       = label;\n   list_info.idx         = idx;\n   list_info.entry_type  = type;\n\n   if (  menu_st->driver_ctx &&\n         menu_st->driver_ctx->list_insert)\n      menu_st->driver_ctx->list_insert(\n            menu_st->userdata,\n            list_info.list,\n            list_info.path,\n            list_info.fullpath,\n            list_info.label,\n            list_info.idx,\n            list_info.entry_type);\n\n   if (list_info.fullpath)\n      free(list_info.fullpath);\n\n   file_list_free_actiondata(list, idx);\n\n   if (!(cbs = (menu_file_list_cbs_t*)\n      malloc(sizeof(menu_file_list_cbs_t))))\n      return false;\n\n   cbs->enum_idx                   = enum_idx;\n   cbs->checked                    = false;\n   cbs->setting                    = setting;\n   cbs->action_iterate             = NULL;\n   cbs->action_deferred_push       = NULL;\n   cbs->action_select              = NULL;\n   cbs->action_get_title           = NULL;\n   cbs->action_ok                  = NULL;\n   cbs->action_cancel              = NULL;\n   cbs->action_scan                = NULL;\n   cbs->action_start               = NULL;\n   cbs->action_info                = NULL;\n   cbs->action_left                = NULL;\n   cbs->action_right               = NULL;\n   cbs->action_label               = NULL;\n   cbs->action_sublabel            = NULL;\n   cbs->action_get_value           = NULL;\n\n   cbs->search.size                = 0;\n   for (i = 0; i < MENU_SEARCH_FILTER_MAX_TERMS; i++)\n      cbs->search.terms[i][0]      = '\\0';\n\n   list->list[idx].actiondata      = cbs;\n\n   if (!cbs->setting && enum_idx != MSG_UNKNOWN)\n   {\n      if (     enum_idx != MENU_ENUM_LABEL_PLAYLIST_ENTRY\n            && enum_idx != MENU_ENUM_LABEL_PLAYLIST_COLLECTION_ENTRY\n            && enum_idx != MENU_ENUM_LABEL_EXPLORE_ITEM\n            && enum_idx != MENU_ENUM_LABEL_CONTENTLESS_CORE\n            && enum_idx != MENU_ENUM_LABEL_RDB_ENTRY)\n         cbs->setting                 = menu_setting_find_enum(enum_idx);\n   }\n\n   lbl_len  = strlen(label);\n\n   menu_cbs_init(menu_st,\n         menu_st->driver_ctx,\n         list, cbs, path, label, lbl_len, type, idx);\n\n   return true;\n}", '/* patches/series, 0080: transactional menu_entries_append */\nbool menu_entries_append(\n      file_list_t *list,\n      const char *path,\n      const char *label,\n      enum msg_hash_enums enum_idx,\n      unsigned type,\n      size_t directory_ptr,\n      size_t entry_idx,\n      rarch_setting_t *setting)\n{\n   menu_ctx_list_t list_info;\n   size_t i;\n   size_t idx, lbl_len;\n   const char *menu_path       = NULL;\n   menu_file_list_cbs_t *cbs   = NULL;\n   struct menu_state  *menu_st = &menu_driver_state;\n   const file_list_t *mlist    = MENU_LIST_GET(menu_st->entries.list, 0);\n\n   if (!list || !label || list->allocation_failed)\n      return false;\n\n   cbs = (menu_file_list_cbs_t*)ps5_menu_malloc(sizeof(*cbs));\n   if (!cbs)\n   {\n      list->allocation_failed = true;\n      RARCH_ERR("[Menu] Entry allocation failed; list population stopped.\\n");\n      return false;\n   }\n   if (!file_list_append(list, path, label, type, directory_ptr, entry_idx))\n   {\n      free(cbs);\n      RARCH_ERR("[Menu] List allocation failed; list population stopped.\\n");\n      return false;\n   }\n   if (mlist && mlist->size)\n      menu_path          = mlist->list[mlist->size - 1].path;\n   idx                   = list->size - 1;\n\n   list_info.fullpath    = NULL;\n\n   if (!string_is_empty(menu_path))\n      list_info.fullpath = (char*)menu_path; /* Borrowed until list_insert returns. */\n   list_info.list        = list;\n   list_info.path        = path;\n   list_info.label       = label;\n   list_info.idx         = idx;\n   list_info.entry_type  = type;\n\n   if (  menu_st->driver_ctx &&\n         menu_st->driver_ctx->list_insert)\n      menu_st->driver_ctx->list_insert(\n            menu_st->userdata,\n            list_info.list,\n            list_info.path,\n            list_info.fullpath,\n            list_info.label,\n            list_info.idx,\n            list_info.entry_type);\n\n   /* The driver interface returns void. XMB publishes userdata only after\n    * its complete node exists; a missing node rejects this new entry. */\n   if (menu_st->driver_ctx && string_is_equal(menu_st->driver_ctx->ident, "xmb") &&\n         !list->list[idx].userdata)\n   {\n      free(cbs);\n      free(list->list[idx].path);\n      free(list->list[idx].label);\n      free(list->list[idx].alt);\n      --list->size;\n      if (idx < list->size)\n         memmove(&list->list[idx], &list->list[idx + 1],\n               (list->size - idx) * sizeof(struct item_file));\n      memset(&list->list[list->size], 0, sizeof(struct item_file));\n      list->allocation_failed = true;\n      RARCH_ERR("[Menu] XMB node allocation failed; list population stopped.\\n");\n      return false;\n   }\n\n   cbs->enum_idx                   = enum_idx;\n   cbs->checked                    = false;\n   cbs->setting                    = setting;\n   cbs->action_iterate             = NULL;\n   cbs->action_deferred_push       = NULL;\n   cbs->action_select              = NULL;\n   cbs->action_get_title           = NULL;\n   cbs->action_ok                  = NULL;\n   cbs->action_cancel              = NULL;\n   cbs->action_scan                = NULL;\n   cbs->action_start               = NULL;\n   cbs->action_info                = NULL;\n   cbs->action_left                = NULL;\n   cbs->action_right               = NULL;\n   cbs->action_label               = NULL;\n   cbs->action_sublabel            = NULL;\n   cbs->action_get_value           = NULL;\n\n   cbs->search.size                = 0;\n   for (i = 0; i < MENU_SEARCH_FILTER_MAX_TERMS; i++)\n      cbs->search.terms[i][0]      = \'\\0\';\n\n   list->list[idx].actiondata      = cbs;\n\n   if (!cbs->setting && enum_idx != MSG_UNKNOWN)\n   {\n      if (     enum_idx != MENU_ENUM_LABEL_PLAYLIST_ENTRY\n            && enum_idx != MENU_ENUM_LABEL_PLAYLIST_COLLECTION_ENTRY\n            && enum_idx != MENU_ENUM_LABEL_EXPLORE_ITEM\n            && enum_idx != MENU_ENUM_LABEL_CONTENTLESS_CORE\n            && enum_idx != MENU_ENUM_LABEL_RDB_ENTRY)\n         cbs->setting                 = menu_setting_find_enum(enum_idx);\n   }\n\n   lbl_len  = strlen(label);\n\n   menu_cbs_init(menu_st,\n         menu_st->driver_ctx,\n         list, cbs, path, label, lbl_len, type, idx);\n\n   return true;\n}', 'patches/series, 0080: transactional menu_entries_append'),
    ('menu/menu_driver.c', "void menu_entries_prepend(file_list_t *list,\n      const char *path, const char *label,\n      enum msg_hash_enums enum_idx,\n      unsigned type, size_t directory_ptr, size_t entry_idx)\n{\n   size_t lbl_len;\n   menu_ctx_list_t list_info;\n   size_t i;\n   size_t idx                  = 0;\n   const char *menu_path       = NULL;\n   menu_file_list_cbs_t *cbs   = NULL;\n   struct menu_state  *menu_st = &menu_driver_state;\n   const file_list_t *mlist    = MENU_LIST_GET(menu_st->entries.list, 0);\n   if (!list || !label)\n      return;\n\n   file_list_insert(list, path, label, type, directory_ptr, entry_idx, 0);\n   if (mlist && mlist->size)\n      menu_path          = mlist->list[mlist->size - 1].path;\n\n   list_info.fullpath    = NULL;\n\n   if (!string_is_empty(menu_path))\n      list_info.fullpath = strdup(menu_path);\n   list_info.list        = list;\n   list_info.path        = path;\n   list_info.label       = label;\n   list_info.idx         = idx;\n   list_info.entry_type  = type;\n\n   if (  menu_st->driver_ctx &&\n         menu_st->driver_ctx->list_insert)\n      menu_st->driver_ctx->list_insert(\n            menu_st->userdata,\n            list_info.list,\n            list_info.path,\n            list_info.fullpath,\n            list_info.label,\n            list_info.idx,\n            list_info.entry_type);\n\n   if (list_info.fullpath)\n      free(list_info.fullpath);\n\n   file_list_free_actiondata(list, idx);\n   cbs                             = (menu_file_list_cbs_t*)\n      malloc(sizeof(menu_file_list_cbs_t));\n\n   if (!cbs)\n      return;\n\n   cbs->enum_idx                   = enum_idx;\n   cbs->checked                    = false;\n   cbs->setting                    = menu_setting_find_enum(cbs->enum_idx);\n   cbs->action_iterate             = NULL;\n   cbs->action_deferred_push       = NULL;\n   cbs->action_select              = NULL;\n   cbs->action_get_title           = NULL;\n   cbs->action_ok                  = NULL;\n   cbs->action_cancel              = NULL;\n   cbs->action_scan                = NULL;\n   cbs->action_start               = NULL;\n   cbs->action_info                = NULL;\n   cbs->action_left                = NULL;\n   cbs->action_right               = NULL;\n   cbs->action_label               = NULL;\n   cbs->action_sublabel            = NULL;\n   cbs->action_get_value           = NULL;\n\n   cbs->search.size                = 0;\n   for (i = 0; i < MENU_SEARCH_FILTER_MAX_TERMS; i++)\n      cbs->search.terms[i][0]      = '\\0';\n\n   list->list[idx].actiondata      = cbs;\n\n   lbl_len  = strlen(label);\n\n   menu_cbs_init(menu_st,\n         menu_st->driver_ctx,\n         list, cbs, path, label, lbl_len, type, idx);\n}", '/* patches/series, 0080: transactional menu_entries_prepend */\nvoid menu_entries_prepend(file_list_t *list,\n      const char *path, const char *label,\n      enum msg_hash_enums enum_idx,\n      unsigned type, size_t directory_ptr, size_t entry_idx)\n{\n   size_t lbl_len;\n   menu_ctx_list_t list_info;\n   size_t i;\n   size_t idx                  = 0;\n   const char *menu_path       = NULL;\n   menu_file_list_cbs_t *cbs   = NULL;\n   struct menu_state  *menu_st = &menu_driver_state;\n   const file_list_t *mlist    = MENU_LIST_GET(menu_st->entries.list, 0);\n   if (!list || !label || list->allocation_failed)\n      return;\n\n   cbs = (menu_file_list_cbs_t*)ps5_menu_malloc(sizeof(*cbs));\n   if (!cbs)\n   {\n      list->allocation_failed = true;\n      RARCH_ERR("[Menu] Entry allocation failed; list population stopped.\\n");\n      return ;\n   }\n   if (!file_list_insert(list, path, label, type, directory_ptr, entry_idx, 0))\n   {\n      free(cbs);\n      RARCH_ERR("[Menu] List allocation failed; list population stopped.\\n");\n      return ;\n   }\n   if (mlist && mlist->size)\n      menu_path          = mlist->list[mlist->size - 1].path;\n\n   list_info.fullpath    = NULL;\n\n   if (!string_is_empty(menu_path))\n      list_info.fullpath = (char*)menu_path; /* Borrowed until list_insert returns. */\n   list_info.list        = list;\n   list_info.path        = path;\n   list_info.label       = label;\n   list_info.idx         = idx;\n   list_info.entry_type  = type;\n\n   if (  menu_st->driver_ctx &&\n         menu_st->driver_ctx->list_insert)\n      menu_st->driver_ctx->list_insert(\n            menu_st->userdata,\n            list_info.list,\n            list_info.path,\n            list_info.fullpath,\n            list_info.label,\n            list_info.idx,\n            list_info.entry_type);\n\n   /* The driver interface returns void. XMB publishes userdata only after\n    * its complete node exists; a missing node rejects this new entry. */\n   if (menu_st->driver_ctx && string_is_equal(menu_st->driver_ctx->ident, "xmb") &&\n         !list->list[idx].userdata)\n   {\n      free(cbs);\n      free(list->list[idx].path);\n      free(list->list[idx].label);\n      free(list->list[idx].alt);\n      --list->size;\n      if (idx < list->size)\n         memmove(&list->list[idx], &list->list[idx + 1],\n               (list->size - idx) * sizeof(struct item_file));\n      memset(&list->list[list->size], 0, sizeof(struct item_file));\n      list->allocation_failed = true;\n      RARCH_ERR("[Menu] XMB node allocation failed; list population stopped.\\n");\n      return ;\n   }\n\n   cbs->enum_idx                   = enum_idx;\n   cbs->checked                    = false;\n   cbs->setting                    = menu_setting_find_enum(cbs->enum_idx);\n   cbs->action_iterate             = NULL;\n   cbs->action_deferred_push       = NULL;\n   cbs->action_select              = NULL;\n   cbs->action_get_title           = NULL;\n   cbs->action_ok                  = NULL;\n   cbs->action_cancel              = NULL;\n   cbs->action_scan                = NULL;\n   cbs->action_start               = NULL;\n   cbs->action_info                = NULL;\n   cbs->action_left                = NULL;\n   cbs->action_right               = NULL;\n   cbs->action_label               = NULL;\n   cbs->action_sublabel            = NULL;\n   cbs->action_get_value           = NULL;\n\n   cbs->search.size                = 0;\n   for (i = 0; i < MENU_SEARCH_FILTER_MAX_TERMS; i++)\n      cbs->search.terms[i][0]      = \'\\0\';\n\n   list->list[idx].actiondata      = cbs;\n\n   lbl_len  = strlen(label);\n\n   menu_cbs_init(menu_st,\n         menu_st->driver_ctx,\n         list, cbs, path, label, lbl_len, type, idx);\n}', 'patches/series, 0080: transactional menu_entries_prepend'),
    ('menu/menu_driver.c', 'static menu_list_t *menu_list_new(const menu_ctx_driver_t *menu_driver_ctx)\n{\n   unsigned i;\n   menu_list_t           *list = (menu_list_t*)malloc(sizeof(*list));\n\n   if (!list)\n      return NULL;\n\n   list->menu_stack_size       = 1;\n   list->selection_buf_size    = 1;\n   list->selection_buf         = NULL;\n   list->menu_stack            = (file_list_t**)\n      calloc(list->menu_stack_size, sizeof(*list->menu_stack));\n\n   if (!list->menu_stack)\n      goto error;\n\n   list->selection_buf         = (file_list_t**)\n      calloc(list->selection_buf_size, sizeof(*list->selection_buf));\n\n   if (!list->selection_buf)\n      goto error;\n\n   for (i = 0; i < list->menu_stack_size; i++)\n   {\n      list->menu_stack[i]           = (file_list_t*)\n         malloc(sizeof(*list->menu_stack[i]));\n      list->menu_stack[i]->list     = NULL;\n      list->menu_stack[i]->capacity = 0;\n      list->menu_stack[i]->size     = 0;\n   }\n\n   for (i = 0; i < list->selection_buf_size; i++)\n   {\n      list->selection_buf[i]           = (file_list_t*)\n         malloc(sizeof(*list->selection_buf[i]));\n      list->selection_buf[i]->list     = NULL;\n      list->selection_buf[i]->capacity = 0;\n      list->selection_buf[i]->size     = 0;\n   }\n\n   return list;\n\nerror:\n   menu_list_free(menu_driver_ctx, list);\n   return NULL;\n}', '/* patches/series, 0080: initialize and check list containers */\nstatic menu_list_t *menu_list_new(const menu_ctx_driver_t *menu_driver_ctx)\n{\n   unsigned i;\n   menu_list_t           *list = (menu_list_t*)malloc(sizeof(*list));\n\n   if (!list)\n      return NULL;\n\n   list->menu_stack_size       = 1;\n   list->selection_buf_size    = 1;\n   list->selection_buf         = NULL;\n   list->menu_stack            = (file_list_t**)\n      calloc(list->menu_stack_size, sizeof(*list->menu_stack));\n\n   if (!list->menu_stack)\n      goto error;\n\n   list->selection_buf         = (file_list_t**)\n      calloc(list->selection_buf_size, sizeof(*list->selection_buf));\n\n   if (!list->selection_buf)\n      goto error;\n\n   for (i = 0; i < list->menu_stack_size; i++)\n   {\n      list->menu_stack[i] = (file_list_t*)calloc(1, sizeof(*list->menu_stack[i]));\n      if (!list->menu_stack[i])\n         goto error;\n   }\n\n   for (i = 0; i < list->selection_buf_size; i++)\n   {\n      list->selection_buf[i] = (file_list_t*)calloc(1, sizeof(*list->selection_buf[i]));\n      if (!list->selection_buf[i])\n         goto error;\n   }\n\n   return list;\n\nerror:\n   menu_list_free(menu_driver_ctx, list);\n   return NULL;\n}', 'patches/series, 0080: initialize and check list containers'),
    ('menu/menu_displaylist.c', '   /* Preallocate the file list */\n   file_list_reserve(info_list, list_size);', '   /* patches/series, 0080: checked playlist preallocation */\n   if (info_list->capacity < list_size && !file_list_reserve(info_list, list_size))\n   {\n      info_list->allocation_failed = true;\n      RARCH_ERR("[Menu] Playlist list allocation failed; population stopped.\\n");\n      return 0;\n   }', 'patches/series, 0080: checked playlist preallocation'),
    ('menu/menu_displaylist.c', '   for (i = 0; i < list_size; i++)\n   {\n      char menu_entry_lbl[NAME_MAX_LENGTH];', '   /* patches/series, 0080: stop playlist at first rejected entry */\n   for (i = 0; i < list_size && !info_list->allocation_failed; i++)\n   {\n      char menu_entry_lbl[NAME_MAX_LENGTH];', 'patches/series, 0080: stop playlist at first rejected entry'),
    ('tasks/task_image.c', "   /* TODO/FIXME - shouldn't we set this ? */\n   image->ti.supports_rgba           = false;", '   /* patches/series, 0081: async images honor the renderer channel order. */\n   image->ti.supports_rgba           = supports_rgba;', 'patches/series, 0081: async images'),
    ('configuration.c', '      case JOYPAD_NULL:\n         break;', '      case JOYPAD_NULL:\n         /* patches/series, 0082: native joypad survives configuration reset. */\n         return "ps5";', 'patches/series, 0082: native joypad'),
    ('retroarch.c', '      case CMD_EVENT_MENU_RESET_TO_DEFAULT_CONFIG:\n         config_set_defaults(global_get_ptr());\n         break;', '      case CMD_EVENT_MENU_RESET_TO_DEFAULT_CONFIG:\n      {\n         /* patches/series, 0082: resetting defaults also clears auto-binds. */\n         extern void ps5_input_reset_autoconfig(void);\n         config_set_defaults(global_get_ptr());\n         ps5_input_reset_autoconfig();\n         break;\n      }', 'patches/series, 0082: resetting defaults'),
    # 0083: the requested format is the image's format.
    #
    # ../PS5_Vulkan samples B8G8R8A8_UNORM in the byte order an application
    # uploads: the 8_8_8_8_UNORM word with the ZYXW selectors, which is Mesa's
    # radv_compose_swizzle for that format's description and which that project's
    # v0-formats battery proves on the console (docs/BLOCKERS.md defect 13,
    # docs/M5_PHASE_C.md round 6). An image is therefore the format it was asked
    # for, and 0014's substitution is withdrawn: reading an application's B, G, R,
    # A bytes as R, G, B, A is the red and blue reversal that made the port ask
    # for R8G8B8A8 everywhere (0075, 0077). What 0014 was really protecting stays
    # - the usage mask, which is derived from the requested format's own
    # features, so it cannot change what an image holds. Every format the
    # frontend asks for now advertises SAMPLED, so the substitution no longer
    # fired in any case; a format that does lose SAMPLED is better refused at
    # draw time than silently reinterpreted.
    (
        "gfx/drivers/vulkan.c",
        "   /* Added by this port (patches/series, 0014): a create-info may only ask\n"
        "    * for usage bits the format advertises, and a texture that cannot be\n"
        "    * sampled is not a texture. Masking handles the first; the second is\n"
        "    * handled by asking for R8G8B8A8_UNORM, which this driver reports as\n"
        "    * sampled and which is the same image for a uniform colour. Only image\n"
        "    * types are touched: STAGING and READBACK are buffers here. */\n",
        "   /* Added by this port (patches/series, 0014): a create-info may only ask for\n"
        "    * usage bits the format advertises. ../PS5_Vulkan refuses an image whose\n"
        "    * usage its format's features do not allow, and upstream asks for\n"
        "    * SAMPLED | TRANSFER_DST | TRANSFER_SRC on the 4x4 blank and the 1x1\n"
        "    * default texture. The mask is built from the requested format's own\n"
        "    * features, so it cannot change what an image holds. Only image types are\n"
        "    * touched: STAGING and READBACK are buffers here.\n"
        "    * patches/series, 0083: the mask is the whole edit. */\n",
        "patches/series, 0083: the mask is the whole edit",
    ),
    (
        "gfx/drivers/vulkan.c",
        "      VkFormat request          = info.format;\n"
        "\n"
        "      memset(&format_properties, 0, sizeof(format_properties));\n"
        "      vkGetPhysicalDeviceFormatProperties(vk->context->gpu,\n"
        "            request, &format_properties);\n"
        "      if (   !(format_properties.optimalTilingFeatures\n"
        "                  & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)\n"
        "          && request != VK_FORMAT_R8G8B8A8_UNORM)\n"
        "      {\n"
        "         request = VK_FORMAT_R8G8B8A8_UNORM;\n"
        "         memset(&format_properties, 0, sizeof(format_properties));\n"
        "         vkGetPhysicalDeviceFormatProperties(vk->context->gpu,\n"
        "               request, &format_properties);\n"
        "      }\n",
        "      /* patches/series, 0083: the requested format is the image's format. */\n"
        "      VkFormat request          = info.format;\n"
        "\n"
        "      memset(&format_properties, 0, sizeof(format_properties));\n"
        "      vkGetPhysicalDeviceFormatProperties(vk->context->gpu,\n"
        "            request, &format_properties);\n",
        "patches/series, 0083: the requested format",
    ),
    (
        "gfx/drivers/vulkan.c",
        "      info.format = request;\n"
        "      info.usage &= allowed;\n",
        "      /* patches/series, 0083: nothing to write back to the image's format. */\n"
        "      info.usage &= allowed;\n",
        "patches/series, 0083: nothing to write back",
    ),
    (
        "gfx/drivers/vulkan.c",
        "      if (request != format)\n"
        "      {\n"
        "         /* The view, the staging buffer's pitch and tex.format are all\n"
        "          * built from this local, so a substituted format has to reach\n"
        "          * all four or the view is created for a format the image is not. */\n"
        "         format           = request;\n"
        "         buffer_width     = (width * vulkan_format_to_bpp(format) + 3u) & ~3u;\n"
        "         buffer_info.size = buffer_width * height;\n"
        "      }\n",
        "      /* patches/series, 0083: no substituted format reaches the view, the\n"
        "       * staging pitch or tex.format, so nothing is written back from it. */\n",
        "patches/series, 0083: no substituted format",
    ),
    (
        # 0027's decision stays; only its reason moves. The format it names has to
        # be a 32-bit one the conversion below can write and this driver can
        # sample, and both textures keep whatever it names, so the substitution
        # 0027 cited is no longer part of the argument.
        "gfx/drivers/vulkan.c",
        "      /* Added by this port (patches/series, 0027): ../PS5_Vulkan samples only\n"
        "       * views whose component mapping is the identity (ps5vk_draw.c,\n"
        "       * V0-formats), so the B4G4R4A4 texture and its B/R view swizzle cannot be\n"
        "       * sampled: the draw is refused. The 32-bit texture and the CPU conversion\n"
        "       * below carry the same pixels with no swizzle, which is the branch a device\n"
        "       * without B4G4R4A4 tiling already takes. The format is named rather than\n"
        "       * left at B8G8R8A8 because the staging texture keeps the format it is\n"
        "       * created with: ../PS5_Vulkan's B8G8R8A8 entry is not a sampled one, so the\n"
        "       * image is substituted to R8G8B8A8 and a staging texture in B8G8R8A8 would\n"
        "       * make the two formats differ - which takes the compute path, whose\n"
        "       * storage-image descriptor the driver has no entry for. */\n",
        "      /* Added by this port (patches/series, 0027): ../PS5_Vulkan samples only\n"
        "       * views whose component mapping is the identity (ps5vk_draw.c,\n"
        "       * V0-formats), so the B4G4R4A4 texture and its B/R view swizzle cannot be\n"
        "       * sampled: the draw is refused. The 32-bit texture and the CPU conversion\n"
        "       * below carry the same pixels with no swizzle, which is the branch a device\n"
        "       * without B4G4R4A4 tiling already takes.\n"
        "       * patches/series, 0083: the format the conversion writes is named here\n"
        "       * because it writes 32 bits per texel, and R8G8B8A8_UNORM is the 32-bit\n"
        "       * format this driver samples through the identity selectors - the order\n"
        "       * the store below is written in. Both the staging texture and the optimal\n"
        "       * image are created with this one local, so naming it here is what keeps\n"
        "       * the two from differing, which would take the compute path whose\n"
        "       * storage-image descriptor the driver has no entry for. */\n",
        "patches/series, 0083: the format the conversion writes",
    ),
    (
        # 0015's case is still required: upstream's own VK_REMAP_TO_TEXFMT turns an
        # RGB565 request into R8G8B8A8_UNORM, and 0027 names that format for the
        # 16-bit menu frame. Only its reason is rewritten.
        "gfx/drivers/vulkan.c",
        "      /* Added by this port (patches/series, 0015): the format 0014 is\n"
        "       * substituted with when a format cannot be sampled. Without this\n"
        "       * case the staging buffer sized from it came out zero bytes long,\n"
        "       * and the driver aborted inside vk_buffer_init. */\n",
        "      /* Added by this port (patches/series, 0015): 32 bits per pixel for\n"
        "       * R8G8B8A8_UNORM. Upstream's own VK_REMAP_TO_TEXFMT turns an RGB565\n"
        "       * request into this format, and the 16-bit menu frame is converted into\n"
        "       * it by hand (patches/series, 0027), so the staging buffer sized from it\n"
        "       * must not come out zero bytes long: ../PS5_Vulkan's vk_buffer_init\n"
        "       * aborts on a zero size.\n"
        "       * patches/series, 0083: 32 bits per pixel for the format 0027 names and\n"
        "       * upstream's own remap produces. */\n",
        "patches/series, 0083: 32 bits per pixel for",
    ),
    (
        # The driver now reports every mode VideoOut offers, fastest first:
        # 3840x2160 at 119880 mHz when the title declares high-frame-rate output
        # and the console supports it, and at 59940 mHz always (../PS5_Vulkan
        # jobs/r51-output-mode). Upstream picks the largest mode and ignores its
        # refresh when no size is asked for, and rejects any mode more than 1 Hz
        # from video_refresh_rate when one is -- which, with a saved 59.94, would
        # never select 120 Hz. This port's choice: the size asked for (or the
        # largest), then the highest refresh among those; the refresh chosen is
        # kept for the context, which reports it (0085).
        "gfx/common/vulkan_common.c",
        "      for (i = 0; i < mode_count; i++)\n"
        "      {\n"
        "         const VkDisplayModePropertiesKHR *mode = &modes[i];\n"
        "         if (vulkan_update_display_mode(width, height, mode, info))\n"
        "            best_mode = modes[i].displayMode;\n"
        "      }\n",
        "      /* Added by this port (patches/series, 0084): the requested size (or the\n"
        "       * largest), then the highest refresh -- 120 Hz where the display offers\n"
        "       * it, its 60 Hz mode where it does not. */\n"
        "      {\n"
        "         uint32_t best_refresh = 0;\n"
        "         for (i = 0; i < mode_count; i++)\n"
        "         {\n"
        "            const VkDisplayModePropertiesKHR *mode = &modes[i];\n"
        "            unsigned mode_width  = mode->parameters.visibleRegion.width;\n"
        "            unsigned mode_height = mode->parameters.visibleRegion.height;\n"
        "            unsigned mode_area   = mode_width * mode_height;\n"
        "            unsigned best_area   = (*width) * (*height);\n"
        "            if (info->width && info->height\n"
        "                  && (mode_width != info->width || mode_height != info->height))\n"
        "               continue;\n"
        "            if (mode_area > best_area\n"
        "                  || (mode_area == best_area && mode->parameters.refreshRate > best_refresh))\n"
        "            {\n"
        "               *width       = mode_width;\n"
        "               *height      = mode_height;\n"
        "               best_refresh = mode->parameters.refreshRate;\n"
        "               best_mode    = mode->displayMode;\n"
        "            }\n"
        "         }\n"
        "         vulkan_ps5_display_refresh_x1000 = best_refresh;\n"
        "      }\n",
        "patches/series, 0084): the requested size",
    ),
    (
        # The variable 0084 writes and 0085 reads.
        "gfx/common/vulkan_common.c",
        "static bool vulkan_create_display_surface(gfx_ctx_vulkan_data_t *vk,\n",
        "/* Added by this port (patches/series, 0084): the refresh, in millihertz, of\n"
        " * the display mode the last display surface was created with. */\n"
        "unsigned vulkan_ps5_display_refresh_x1000;\n"
        "\n"
        "static bool vulkan_create_display_surface(gfx_ctx_vulkan_data_t *vk,\n",
        "patches/series, 0084): the refresh, in millihertz,",
    ),
    (
        # The context reported video_refresh_rate as the display's refresh
        # whatever mode it got. It now reports the mode's own refresh, and makes
        # the setting follow it, because RetroArch paces, resamples audio and
        # picks its automatic swap interval from that value: a 119.88 Hz display
        # believed to be 59.94 would run a 60 FPS core twice as fast. A swap
        # interval of 1 saved from the 60 Hz era is made automatic when the mode
        # runs above 100 Hz, so a 60 FPS core presents each frame for two
        # vblanks; any other saved interval is left as chosen.
        "gfx/drivers_context/khr_display_ctx.c",
        "   khr->refresh_rate_x1000        = info.refresh_rate_x1000;\n",
        "   /* Added by this port (patches/series, 0085): the chosen mode's refresh. */\n"
        "   {\n"
        "      extern unsigned vulkan_ps5_display_refresh_x1000;\n"
        "      unsigned mode_refresh = vulkan_ps5_display_refresh_x1000;\n"
        "      khr->refresh_rate_x1000     = mode_refresh ? mode_refresh : info.refresh_rate_x1000;\n"
        "      float refresh_delta = settings->floats.video_refresh_rate - mode_refresh / 1000.0f;\n"
        "      if (refresh_delta < 0.0f)\n"
        "         refresh_delta = -refresh_delta;\n"
        "      if (mode_refresh && refresh_delta > 0.5f)\n"
        "      {\n"
        "         RARCH_LOG(\"[PS5] Display runs at %.3f Hz; video_refresh_rate follows it (was %.3f).\\n\",\n"
        "               mode_refresh / 1000.0f, settings->floats.video_refresh_rate);\n"
        "         settings->floats.video_refresh_rate = mode_refresh / 1000.0f;\n"
        "      }\n"
        "      if (mode_refresh > 100000 && settings->uints.video_swap_interval == 1)\n"
        "      {\n"
        "         RARCH_LOG(\"[PS5] Swap interval 1 at %.3f Hz made automatic.\\n\", mode_refresh / 1000.0f);\n"
        "         settings->uints.video_swap_interval = 0;\n"
        "      }\n"
        "   }\n",
        "patches/series, 0085): the chosen mode's refresh.",
    ),
]

# Changes that are withdrawn rather than deleted, by marker.
#
# An entry here names an insertion this driver no longer needs, and the
# withdrawal is that entry's own substitution in reverse: the text it inserted
# is exchanged for the upstream text it replaced. Nothing leaves EDITS, because
# build/ra-conf already holds the insertion - a patch that is merely deleted
# from this file stays applied in every tree that already has it, which is the
# silent state the whole script exists to avoid.
#
# 0075 and 0077 were written while this driver's B8G8R8A8_UNORM entry advertised
# the colour-attachment bit alone. The frontend asks for that format for its
# software core frames and for its menu images, the substitution above turned
# every one of them into an R8G8B8A8 image, and the bytes were then read in the
# wrong order: the red and blue reversal. The entry is sampled now, so the
# frontend uploads the core's own pixels again, decodes and uploads menu images
# in the order they are decoded, and hands out its writable software framebuffer
# instead of refusing it.
WITHDRAWN = (
    'patches/series, 0075: matching',
    'patches/series, 0075: libretro XRGB',
    'patches/series, 0077: decode menu images for sampled RGBA',
    'patches/series, 0077: upload decoded menu images as RGBA',
    'patches/series, 0077: preserve original core pixels across cached frames',
)


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.splitlines()[2].strip(), file=sys.stderr)
        return 2
    tree = Path(sys.argv[1])
    if not tree.is_dir():
        print(f"error: no such tree: {tree}", file=sys.stderr)
        return 2

    # Every file's final text is computed in memory -- insertions first, then the
    # withdrawals -- and written only when it differs from what is on disk. A file
    # that ends where it started keeps its timestamp, so the frontend's compile
    # loop, which rebuilds an object older than its source, leaves it alone. This
    # used to write each insertion as it went, and 0075/0077's withdrawn text was
    # inserted and withdrawn again on every run: vulkan.c was rewritten and
    # recompiled by builds that changed nothing.
    ok = True
    texts: dict[str, str] = {}
    originals: dict[str, str] = {}
    for name, anchor, replacement, marker in EDITS:
        path = tree / name
        if name not in texts:
            if not path.is_file():
                print(f"  {name}: MISSING")
                ok = False
                continue
            texts[name] = originals[name] = path.read_text(encoding="utf-8")
        text = texts[name]
        if marker in text:
            print(f"  {name}: present")
            continue
        if anchor not in text:
            # Upstream moved the anchor. Refusing here is better than inserting
            # somewhere plausible: a driver registered in the wrong table is a
            # build that links and a frontend that ignores it.
            print(f"  {name}: ANCHOR NOT FOUND ({anchor.strip()!r})")
            ok = False
            continue
        texts[name] = text.replace(anchor, replacement, 1)
        print(f"  {name}: applied")

    # Withdrawals follow every insertion, so a fresh tree and a tree that already
    # carried the withdrawn text end in the same state.
    for name, anchor, replacement, marker in EDITS:
        if marker not in WITHDRAWN or name not in texts:
            continue
        text = texts[name]
        if replacement not in text:
            print(f"  {name}: upstream ({marker})")
            continue
        texts[name] = text.replace(replacement, anchor, 1)
        print(f"  {name}: withdrawn ({marker})")

    for name, text in texts.items():
        if text != originals[name]:
            (tree / name).write_text(text, encoding="utf-8")
            print(f"  {name}: written")

    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
