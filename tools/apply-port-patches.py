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
        "#ifdef HAVE_VULKAN\n   &video_vulkan,\n#endif\n   &video_ps5,\n",
        "#ifdef HAVE_VULKAN\n   &video_vulkan,\n#endif\n   &video_ps5,\n",
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
        "      case VIDEO_NULL:\n         break;",
        "      case VIDEO_NULL:\n"
        "          /* Named by this port (patches/series, 0010): the console's Vulkan\n"
        "           * driver is the graphics backend this project consumes, and with no\n"
        "           * config file read it has to come from the compiled default. */\n"
        "          return \"vulkan\";",
        "the console's Vulkan",
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
