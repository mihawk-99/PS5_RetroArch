/* PS5 PPSSPP - the four libc entry points this target must implement itself.
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * These are not PPSSPP's own calls. Each one is made by vendored third-party code
 * that the libretro core links, and the payload SDK declares but does not export
 * them, so the title's generated binding table cannot resolve them:
 *
 *   swab        ext/libpng17/pngtrans.c, for 16-bit row transforms
 *   nl_langinfo ext/armips and ext/SPIRV-Cross, which ask only for the codeset
 *   tmpfile     ext/lua/liolib.c, io.tmpfile()
 *   tmpnam      ext/lua/loslib.c, os.tmpname()
 *
 * Implementing them inside the core, rather than teaching the title's import table
 * about them, keeps the resolution where the reference is and needs no SDK change.
 * swab and the codeset query are real implementations; the two temporary-file
 * entries refuse honestly, because this target has no writable /tmp contract and
 * neither call is on any PPSSPP path that boots or plays a game. Refusing returns
 * the NULL both callers already handle.
 */
#include <cstddef>
#include <cstdio>
#include <langinfo.h>
#include <sys/types.h>

extern "C" void swab(const void *from, void *to, ssize_t count)
{
    if (count <= 0)
        return;
    const auto *source = static_cast<const unsigned char *>(from);
    auto *destination = static_cast<unsigned char *>(to);
    /* POSIX: swap every adjacent pair; a trailing odd byte is left alone. */
    for (ssize_t index = 0; index + 1 < count; index += 2)
    {
        destination[index] = source[index + 1];
        destination[index + 1] = source[index];
    }
    if (count & 1)
        destination[count - 1] = source[count - 1];
}

extern "C" char *nl_langinfo(nl_item item)
{
    /* The port's filesystem, Lua strings and configuration are UTF-8, and the two
     * callers use this to decide how to interpret a filename or a shader source.
     * Anything else answers as the C locale, which is what the emulator assumes. */
    static char codeset[] = "UTF-8";
    static char c_locale[] = "C";
    static char empty[] = "";
    switch (item)
    {
    case CODESET: return codeset;
    case RADIXCHAR:
    case THOUSEP: return empty;
    default: return c_locale;
    }
}

namespace
{
/* One line each, so a future failure names the call instead of a NULL. */
void note(const char *name)
{
    static bool reported_file = false;
    static bool reported_name = false;
    bool &reported = *name == 't' && name[3] == 'f' ? reported_file : reported_name;
    if (!reported)
    {
        reported = true;
        std::fprintf(stderr, "ppsspp core: %s() has no writable temporary path on this target; returning NULL\n", name);
    }
}
} // namespace

extern "C" FILE *tmpfile(void)
{
    note("tmpfile");
    return nullptr;
}

extern "C" char *tmpnam(char *)
{
    note("tmpnam");
    return nullptr;
}
