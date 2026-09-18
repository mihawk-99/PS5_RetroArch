/*
 * PS5 RetroArch - symbols the console's image format expects but no stub defines.
 *
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * On a normal link the linker itself invents __bss_start, _end and friends as
 * part of the C runtime's bookkeeping. The image converter reads the finished
 * image's symbols and refuses to write eboot.bin while anything it sees
 * referenced has no definition anywhere:
 *
 *   error: no public SDK stub exports required symbol __bss_start
 *
 * No SDK stub can define them, because they describe this image's own layout,
 * so they are defined here, in the image, which is where they belong.
 */

    .globl __bss_start
    .globl _end
    .globl end
    .globl _edata
    .globl __bss_end
    .globl __image_start
    .globl __image_end
    .globl __bss_start__
    .globl __bss_end__
    .globl __init_array_start
    .globl __init_array_end

    .section .data
__bss_start:
_end:
end:
_edata:
__bss_end:
__image_start:
__image_end:
__bss_start__:
__bss_end__:
__init_array_start:
__init_array_end:
    .quad 0
