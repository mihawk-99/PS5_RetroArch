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

Exit status is 0 when every edit is present at the end, 1 otherwise.
"""

from __future__ import annotations

import sys
from pathlib import Path

# (file, anchor, inserted-before-anchor, already-present-marker)
EDITS = [
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
        # The other half of the same change: the pipeline index is derived from the
        # primitive type, so a strip reported as a strip would still select the
        # strip pipeline. Reporting it as a list selects the list pipeline, and the
        # vertices already match because the expansion above produced a list.
        "gfx/drivers/vulkan.c",
        "                 ((draw->prim_type == GFX_DISPLAY_PRIM_TRIANGLESTRIP) << 1)\n",
        "                 /* patches/series 0011: always the list pipeline, because the\n"
        "                  * vertices were expanded into a list above. */\n"
        "                 0u << 1\n",
        "0u << 1",
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
        # It is an *input* driver and not a joypad driver because every joypad
        # driver upstream ships needs a library this SDK does not carry, so
        # primary_joypad is NULL here - see the guard below. input_state_wrap
        # consults the joypad only when there is one and calls the input driver's
        # own input_state unconditionally, so a pad read directly still reaches
        # the menu.
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
        "          return \"ps5\";",
        "return \"ps5\";",
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
        "a combined image sampler write names no sampler",
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
]


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.splitlines()[2].strip(), file=sys.stderr)
        return 2
    tree = Path(sys.argv[1])
    if not tree.is_dir():
        print(f"error: no such tree: {tree}", file=sys.stderr)
        return 2

    ok = True
    for name, anchor, replacement, marker in EDITS:
        path = tree / name
        if not path.is_file():
            print(f"  {name}: MISSING")
            ok = False
            continue
        text = path.read_text(encoding="utf-8")
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
        path.write_text(text.replace(anchor, replacement, 1), encoding="utf-8")
        print(f"  {name}: applied")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
