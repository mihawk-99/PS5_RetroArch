/*
 * PS5 RetroArch - the FreeBSD locale variants the shader path needs.
 *
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Why this file exists. Linking RetroArch's real Vulkan filter chain - the one
 * that builds the stock shader into a pipeline instead of returning NULL - pulls
 * in SPIRV-Cross and glslang, and those are written against FreeBSD's xlocale
 * interface: the `_l` functions take the locale a call should use, and the
 * console's SDK exports the plain functions but not the `_l` ones. The link named
 * thirty-four of them at once.
 *
 * A title has exactly one locale, the C locale: there is nothing to switch to and
 * nothing to pass in. So every function here does what its unsuffixed counterpart
 * does and ignores the argument, which is what the C locale means. The two that
 * are not forwards say so where they are, and `nl_langinfo(RADIXCHAR)` is the one
 * with a caller that reads its answer: SPIRV-Cross asks for the decimal point and
 * would otherwise print shader constants with the wrong separator.
 *
 * This file exists because the alternative is not building the shader path at all,
 * which is where this port was: src/video_filters_stub.cpp defined the whole chain
 * as NULL-returning stubs and the driver read that as "no chain", so no pipeline
 * was ever created. Removing that stub is what made these symbols appear.
 */

#include <langinfo.h>
#include <limits.h>
#include <locale.h>
#include <runetype.h>
#include <nl_types.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>
#include <wctype.h>
#include <xlocale.h>

/* This SDK's headers define several of these as macros over FreeBSD internals -
 * ___runetype, __getCurrentRuneLocale, ___mb_cur_max - and the console's libc
 * exports the functions but none of the internals, so a macro use links against
 * symbols that are not there. Asking for the function by name is what makes it
 * linkable, and the function answers the same thing for the C locale. */
#undef MB_CUR_MAX
#undef iswprint
#undef iswctype
#undef towlower
#undef towupper
extern int iswprint(wint_t);
extern int iswctype(wint_t, wctype_t);
extern wint_t towlower(wint_t);
extern wint_t towupper(wint_t);
extern size_t mbrtowc(wchar_t *__restrict, const char *__restrict, size_t, mbstate_t *__restrict);
extern size_t wcrtomb(char *__restrict, wchar_t, mbstate_t *__restrict);

/* The C locale's multibyte encoding is one byte per character. */
#define PS5_MB_CUR_MAX 1

/* The xlocale.h of a FreeBSD-derived SDK declares a few of these and not the
 * rest; a locale is an opaque pointer either way. */
#ifndef LC_GLOBAL_LOCALE
#define LC_GLOBAL_LOCALE ((locale_t)0)
#endif

/* newlocale/freelocale: the console has one locale, so a locale object is a
 * non-NULL token that nothing switches. Returning (locale_t)1 rather than NULL
 * matters: callers treat NULL as "could not create" and take a fallback path that
 * parses without a locale at all. */
locale_t newlocale(int mask, const char *locale, locale_t base)
{
    (void)mask;
    (void)locale;
    (void)base;
    return (locale_t)1;
}

int freelocale(locale_t loc)
{
    (void)loc;
    return 0;
}

struct lconv *localeconv_l(locale_t loc)
{
    (void)loc;
    return localeconv();
}

/* The one whose answer is read: SPIRV-Cross asks RADIXCHAR and prints shader
 * constants with it (deps/SPIRV-Cross/spirv_glsl.cpp). */
char *nl_langinfo(nl_item item)
{
    static char radix[] = ".";

    if (item == RADIXCHAR)
        return radix;
    return (char *)"";
}

/* Number parsing: the C locale's own functions are the answer the `_l` callers
 * want. glslang parses shader literals through these. */
long long strtoll_l(const char *nptr, char **endptr, int base, locale_t loc)
{
    (void)loc;
    return strtoll(nptr, endptr, base);
}

unsigned long long strtoull_l(const char *nptr, char **endptr, int base, locale_t loc)
{
    (void)loc;
    return strtoull(nptr, endptr, base);
}

double strtod_l(const char *nptr, char **endptr, locale_t loc)
{
    (void)loc;
    return strtod(nptr, endptr);
}

float strtof_l(const char *nptr, char **endptr, locale_t loc)
{
    (void)loc;
    return strtof(nptr, endptr);
}

long double strtold_l(const char *nptr, char **endptr, locale_t loc)
{
    (void)loc;
    return strtold(nptr, endptr);
}

/* Formatting: the same forwards, through the v-forms so the argument list can be
 * passed on. */
int snprintf_l(char *str, size_t size, locale_t loc, const char *format, ...)
{
    int result;
    va_list args;

    (void)loc;
    va_start(args, format);
    result = vsnprintf(str, size, format, args);
    va_end(args);
    return result;
}

int sscanf_l(const char *str, locale_t loc, const char *format, ...)
{
    int result;
    va_list args;

    (void)loc;
    va_start(args, format);
    result = vsscanf(str, format, args);
    va_end(args);
    return result;
}

int asprintf_l(char **strp, locale_t loc, const char *format, ...)
{
    int result;
    va_list args;

    (void)loc;
    va_start(args, format);
    result = vasprintf(strp, format, args);
    va_end(args);
    return result;
}

int vasprintf_l(char **strp, locale_t loc, const char *format, va_list ap)
{
    (void)loc;
    return vasprintf(strp, format, ap);
}

/* Collation and time: the C locale compares bytes and formats in the C locale. */
int strcoll_l(const char *s1, const char *s2, locale_t loc)
{
    (void)loc;
    return strcoll(s1, s2);
}

size_t strxfrm_l(char *dst, const char *src, size_t len, locale_t loc)
{
    (void)loc;
    return strxfrm(dst, src, len);
}

size_t strftime_l(char *s, size_t max, const char *format, const struct tm *tm, locale_t loc)
{
    (void)loc;
    return strftime(s, max, format, tm);
}

int wcscoll_l(const wchar_t *s1, const wchar_t *s2, locale_t loc)
{
    (void)loc;
    return wcscoll(s1, s2);
}

size_t wcsxfrm_l(wchar_t *dst, const wchar_t *src, size_t len, locale_t loc)
{
    (void)loc;
    return wcsxfrm(dst, src, len);
}

/* Multibyte conversion: the console's C locale conversion is the answer. */
wint_t btowc_l(int c, locale_t loc)
{
    (void)loc;
    return btowc(c);
}

int wctob_l(wint_t c, locale_t loc)
{
    (void)loc;
    return wctob(c);
}

int iswctype_l(wint_t wc, wctype_t charclass, locale_t loc)
{
    (void)loc;
    return iswctype(wc, charclass);
}

size_t mbrlen_l(const char *s, size_t n, mbstate_t *ps, locale_t loc)
{
    (void)loc;
    return mbrlen(s, n, ps);
}

size_t mbrtowc_l(wchar_t *pwc, const char *s, size_t n, mbstate_t *ps, locale_t loc)
{
    (void)loc;
    return mbrtowc(pwc, s, n, ps);
}

size_t mbsrtowcs_l(wchar_t *dst, const char **src, size_t len, mbstate_t *ps, locale_t loc)
{
    (void)loc;
    return mbsrtowcs(dst, src, len, ps);
}

/* This SDK exports no mbsnrtowcs or wcsnrtombs, so the two bounded conversions
 * are the loops their unbounded forms are, stopped at the bound. Both are written
 * against mbrtowc and wcrtomb, which are exported. */
size_t mbsnrtowcs_l(wchar_t *dst, const char **src, size_t nms, size_t len, mbstate_t *ps,
                    locale_t loc)
{
    const char *s = *src;
    const char *start = *src;
    size_t converted = 0;
    mbstate_t state;

    (void)loc;
    if (!s)
        return 0;
    state = ps ? *ps : (mbstate_t){0};

    while (converted < len)
    {
        wchar_t wc;
        size_t left = nms - (size_t)(s - start);
        size_t used;

        if (!left)
            break;
        used = mbrtowc(&wc, s, left, &state);
        if (used == (size_t)-1 || used == (size_t)-2)
        {
            if (ps)
                *ps = state;
            *src = s;
            return (size_t)-1;
        }
        if (!used)
        {
            if (dst)
                dst[converted] = L'\0';
            if (ps)
                *ps = state;
            *src = NULL;
            return converted;
        }
        if (dst)
            dst[converted] = wc;
        converted++;
        s += used;
    }

    if (ps)
        *ps = state;
    *src = s;
    return converted;
}

size_t wcrtomb_l(char *s, wchar_t wc, mbstate_t *ps, locale_t loc)
{
    (void)loc;
    return wcrtomb(s, wc, ps);
}

size_t wcsnrtombs_l(char *dst, const wchar_t **src, size_t nwc, size_t len, mbstate_t *ps,
                    locale_t loc)
{
    const wchar_t *s = *src;
    size_t written = 0;
    size_t converted = 0;
    mbstate_t state;

    (void)loc;
    if (!s)
        return 0;
    state = ps ? *ps : (mbstate_t){0};

    while (converted < nwc)
    {
        char bytes[PS5_MB_CUR_MAX];
        size_t used = wcrtomb(bytes, s[converted], &state);

        if (used == (size_t)-1)
        {
            if (ps)
                *ps = state;
            *src = s;
            return (size_t)-1;
        }
        if (dst && written + used > len)
            break;
        if (dst)
            memcpy(dst + written, bytes, used);
        written += used;
        converted++;
        if (!s[converted - 1])
        {
            if (ps)
                *ps = state;
            *src = NULL;
            return written ? written - 1 : 0;
        }
    }

    if (ps)
        *ps = state;
    *src = s + converted;
    return written;
}

int mbtowc_l(wchar_t *pwc, const char *s, size_t n, locale_t loc)
{
    (void)loc;
    return mbtowc(pwc, s, n);
}

/* The ctype internals a FreeBSD <runetype.h> user reaches for. The console's
 * libc exports neither __maskrune nor the rune tables, so these answer from the
 * wide-character classification that is exported: the same answer for the C
 * locale, which is the only locale here. */
int ___mb_cur_max_l(locale_t loc)
{
    (void)loc;
    return PS5_MB_CUR_MAX;
}

unsigned long ___runetype_l(__ct_rune_t c, locale_t loc)
{
    (void)loc;
    return (unsigned long)(iswprint((wint_t)c) ? 1u : 0u);
}

int ___tolower_l(__ct_rune_t c, locale_t loc)
{
    (void)loc;
    return (int)towlower((wint_t)c);
}

int ___toupper_l(__ct_rune_t c, locale_t loc)
{
    (void)loc;
    return (int)towupper((wint_t)c);
}

/* The rune table of a locale: nothing here builds one, and the callers that ask
 * for it treat NULL as "no table", which is what the C locale needs. */
_RuneLocale *__runes_for_locale(locale_t loc, int *count)
{
    (void)loc;
    if (count)
        *count = 0;
    return NULL;
}

/* Message catalogues: a title ships its own strings and opens none. */
nl_catd catopen(const char *name, int oflag)
{
    (void)name;
    (void)oflag;
    return (nl_catd)-1;
}

char *catgets(nl_catd catalog, int set_id, int msg_id, const char *s)
{
    (void)catalog;
    (void)set_id;
    (void)msg_id;
    return (char *)s;
}

int catclose(nl_catd catalog)
{
    (void)catalog;
    return 0;
}
