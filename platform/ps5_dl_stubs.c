/*
 * PS5 RetroArch - the __dl* entry points the console's libc does not provide.
 *
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Why this file exists. Compiling for this target makes clang take FreeBSD's
 * libc as its model, and FreeBSD's dlfcn declares both spellings: the public
 * dlopen() and an internal __dlopen() that the public one calls. RestroArch uses
 * dlopen for its cores, so every object that touches it carries a *weak*
 * reference to __dlopen and its siblings.
 *
 * Those weak references are harmless to the linker -- an unresolved weak symbol
 * is allowed -- but not to the application-image converter, which refuses to
 * build eboot.bin while a referenced symbol has no definition anywhere:
 *
 *   error: no public SDK stub exports required symbol __dlopen
 *
 * The console's libc has no __dl* implementation, and no SDK stub exports one,
 * so the reference can never be satisfied by the platform. Defining the symbols
 * here satisfies it explicitly instead of leaving it dangling: the linker binds
 * these definitions, the converter sees a complete symbol set, and the public
 * dlopen()/dlsym() path is untouched because it is a separate symbol that this
 * file does not define.
 *
 * These return failure rather than pretending to work. If a build ever routes a
 * real call through one of them, it fails visibly at the call instead of
 * silently returning something wrong.
 */

#include <dlfcn.h>
#include <stddef.h>
#include <stdint.h>

void *__dlopen(const char *path, int mode)
{
    (void)path;
    (void)mode;
    return NULL;
}

void *__dlsym(void *handle, const char *name)
{
    (void)handle;
    (void)name;
    return NULL;
}

int __dlclose(void *handle)
{
    (void)handle;
    return -1;
}

char *__dlerror(void)
{
    return NULL;
}

int __dladdr(const void *address, Dl_info *info)
{
    (void)address;
    (void)info;
    return 0;
}

/*
 * kernel_mprotect - the one symbol this project supplies that does real work.
 *
 * The payload references it because libretro-common's memory-mapping layer asks
 * the platform for a page-protection change, and the console's SDK declares it
 * as kernel_mprotect(pid, addr, size, prot) in ps5/kernel.h. The declaration is
 * repeated here rather than including that header, so this file stays
 * self-contained.
 *
 * It is forwarded to the standard mprotect for this process rather than stubbed
 * out, because a stub returning failure is exactly what makes a dynamic
 * recompiler unable to allocate executable memory -- the failure this console's
 * emulator builds hit as "couldn't allocate ... byte block of aligned RWX
 * memory". Forwarding keeps that path open. Whether the kernel grants it is then
 * a property of the console, not of this build.
 */
int mprotect(void *addr, unsigned long len, int prot);

int kernel_mprotect(int pid, intptr_t addr, size_t size, int prot)
{
    /* pid 0 means "this process" on this platform; anything else is not ours
     * to change, so it fails instead of silently protecting the wrong range. */
    if (pid != 0)
        return -1;
    return mprotect((void *)addr, (unsigned long)size, prot);
}
