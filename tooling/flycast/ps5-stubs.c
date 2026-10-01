/* PS5 stubs for symbols Flycast references but the title's generated binding
 * table does not provide. Each one sits on a code path that never runs on this
 * target: the serial PTY mode needs /dev/ptmx, unwind registration exists only
 * for an external debugger that is never attached, and the locale entry points
 * back std::locale code that stays on the classic "C" locale. A failing stub is
 * the correct behaviour if any of them is ever reached. */
#include <locale.h>
#include <stddef.h>

void __register_frame(const void *frame) { (void)frame; }
void __deregister_frame(const void *frame) { (void)frame; }

int grantpt(int fd) { (void)fd; return -1; }
int unlockpt(int fd) { (void)fd; return -1; }
char *ptsname(int fd) { (void)fd; return NULL; }

locale_t newlocale(int mask, const char *locale, locale_t base)
{
    (void)mask; (void)locale; (void)base;
    return (locale_t)0;
}

locale_t uselocale(locale_t loc) { (void)loc; return (locale_t)0; }
