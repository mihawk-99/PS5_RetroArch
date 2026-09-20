/* PS5 PPSSPP - the libc entry points this target must implement itself.
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
 * Four more were added after the first console run, and they are a different kind of
 * case: the SDK's libScePosixForWebKit stub lists them, but no core this title has
 * ever shipped imported anything from that module, and on the console the first
 * load of this core failed with "unresolved native runtime import: gai_strerror" -
 * the loader walked the dynamic symbols in order and reached these four, which means
 * the module's stub-declared exports are not all present at runtime. The symbols are
 * the network resolver and a terminal check, neither of which this title provides:
 *
 *   gai_strerror, getaddrinfo, freeaddrinfo   PPSSPP's ad-hoc networking
 *   isatty                                    terminal detection for log colouring
 *
 * Implementing them inside the core, rather than teaching the title's import table
 * about them, keeps the resolution where the reference is and needs no SDK change.
 * swab and the codeset query are real implementations; the resolver refuses with
 * EAI_FAIL and the temporary-file entries refuse with NULL, all of which the callers
 * already handle. isatty answers 0, which is true: the title's stdout is a log file.
 */
#include <cstddef>
#include <cstdio>
#include <langinfo.h>
#include <netdb.h>
#include <sys/types.h>
#include <unistd.h>

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

/* No resolver in this title. PPSSPP uses these for ad-hoc multiplayer, which the
 * frontend does not enable; a non-recoverable answer is the honest one. */
extern "C" int getaddrinfo(const char *, const char *, const struct addrinfo *,
                           struct addrinfo **)
{
    return EAI_FAIL;
}

extern "C" void freeaddrinfo(struct addrinfo *)
{
}

extern "C" const char *gai_strerror(int code)
{
    switch (code)
    {
    case 0: return "no error";
    case EAI_FAIL: return "name resolution is not available in this title";
    case EAI_MEMORY: return "out of memory";
    case EAI_SYSTEM: return "system error";
    default: return "name resolution error";
    }
}

/* The title's stdout is a log file, not a terminal. */
extern "C" int isatty(int)
{
    return 0;
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
