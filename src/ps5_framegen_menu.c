/* Frame generation in RetroArch's menus (patches/series, 0120).
 *
 * One list of options, shown in three places: the XMB's leftmost tab, a submenu opened from the
 * first entry of RGUI's main menu, and (ES-DE's own menu writes the same file) the shared options
 * file. Entries are plain actions the menu code binds by label: "ps5_fg_<n>" cycles with OK, left
 * and right; "ps5_fgt_<n>" (the XMB tab, where left and right move between tabs) cycles with OK;
 * "ps5_framegen" opens the submenu. The values live in RetroArch's settings (the driver reads them
 * each frame) and every change is written to /app0/config/framegen.cfg, which the title appends
 * when it starts RetroArch.
 *
 * This file is compiled into the frontend archive by tools/build-retroarch.sh, against the
 * configured RetroArch tree.
 *
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <lists/file_list.h>
#include <retro_miscellaneous.h>
#include <string/stdstring.h>

#include "command.h"
#include "configuration.h"
#include "msg_hash.h"
#include "runloop.h"
#include "verbosity.h"
#include "menu/menu_defines.h"
#include "menu/menu_driver.h"
#include "menu/menu_entries.h"

#include "ps5_framegen_config.h"

#define PS5_FG_LABEL_FOLDER "ps5_framegen"

/* Presents of each picture (the display's rate over the target's), set with the rate
 * (patches/series, 0121) */
unsigned ps5_fg_dup = 1;
/* The rate of the pictures the display gets: 119.88 or 59.94 (the Target Framerate), set with the
 * rate */
float ps5_fg_target_fps = 119.88f;
/* The instant of the picture the last present showed (1: a real frame), set by the driver */
float ps5_fg_dbg_t = 1.0f;
/* 1: the core cannot keep its own rate, so frames are shown as it makes them (the driver reads it)
 */
int ps5_fg_floor;
/* For the FPS counter (gfx/video_driver.c): 1 while frames are being generated; the presents a
 * second over the last window */
int ps5_fg_active;
float ps5_fg_presents_per_s;
float ps5_fg_base_fps; /* the core's own new frames a second, as the FPS line last measured them */

/* The loaded core's frame rate, kept by the video setup (patches/series, 0121) for the driver's
 * cadence check */
float ps5_fg_core_fps;

/* "ps5_fg_3" -> 3; the tab's "ps5_fgt_3" -> 3; anything else -> -1 */
static int ps5_fg_item_of(const char *label, bool *tab)
{
    const char *digits;
    if (!label)
        return -1;
    if (!strncmp(label, "ps5_fgt_", 8))
    {
        digits = label + 8;
        if (tab)
            *tab = true;
    }
    else if (!strncmp(label, "ps5_fg_", 7))
    {
        digits = label + 7;
        if (tab)
            *tab = false;
    }
    else
        return -1;
    if (digits[0] < '0' || digits[0] > '9' || digits[1])
        return -1;
    return digits[0] - '0' < PS5_FG_ITEM_COUNT ? digits[0] - '0' : -1;
}

bool ps5_fg_menu_is_folder(const char *label)
{
    return label && !strcmp(label, PS5_FG_LABEL_FOLDER);
}

bool ps5_fg_menu_is_label(const char *label)
{
    return ps5_fg_menu_is_folder(label) || ps5_fg_item_of(label, NULL) >= 0;
}

/* Left and right change the value, except in the XMB tab where they move between tabs */
bool ps5_fg_menu_has_values(const char *label)
{
    bool tab = false;
    return ps5_fg_item_of(label, &tab) >= 0 && !tab;
}

static void ps5_fg_current(struct ps5_fg_options *o)
{
    settings_t *settings = config_get_ptr();
    o->enable = settings->bools.video_framegen ? 1 : 0;
    o->target = settings->uints.video_framegen_target == 60 ? 1 : 0;
    o->method = settings->uints.video_framegen_method % 3;
    o->cadence = settings->uints.video_framegen_cadence % 5;
    o->size = settings->uints.video_framegen_size % 4;
    o->osd = settings->bools.video_framegen_osd ? 1 : 0;
}

void ps5_fg_menu_fill(file_list_t *list, bool tab)
{
    unsigned i;
    char label[16];
    for (i = 0; i < PS5_FG_ITEM_COUNT; i++)
    {
        snprintf(label, sizeof(label), tab ? "ps5_fgt_%u" : "ps5_fg_%u", i);
        menu_entries_append(list, ps5_fg_item_name(i), label, MSG_UNKNOWN, MENU_SETTING_ACTION, 0,
                            0, NULL);
    }
}

void ps5_fg_menu_step(const char *label, int direction)
{
    struct ps5_fg_options now, before;
    settings_t *settings = config_get_ptr();
    int item = ps5_fg_item_of(label, NULL);
    if (item < 0)
        return;
    ps5_fg_current(&now);
    before = now;
    ps5_fg_step(&now, (unsigned)item, direction);
    settings->bools.video_framegen = now.enable != 0;
    settings->bools.video_framegen_auto = true;
    settings->uints.video_framegen_target = now.target ? 60 : 120;
    settings->uints.video_framegen_method = now.method;
    settings->uints.video_framegen_cadence = now.cadence;
    settings->uints.video_framegen_size = now.size;
    settings->bools.video_framegen_osd = now.osd != 0;
    ps5_fg_save(&now);
    /* The rate and the presents per game frame are read when the video output starts: its timing
     * follows them */
    if (now.target != before.target || now.enable != before.enable)
        command_event(CMD_EVENT_REINIT,
                      NULL); /* (Auto picks its rate as the video output starts) */
}

/* The entry's name (s2) and its value (s), as the menu's value callback wants them */
size_t ps5_fg_menu_text(const char *label, char *value, size_t value_len, char *name,
                        size_t name_len)
{
    struct ps5_fg_options now;
    int item;
    if (ps5_fg_menu_is_folder(label))
    {
        strlcpy(name, "Frame Generation", name_len);
        return strlcpy(value, "", value_len);
    }
    item = ps5_fg_item_of(label, NULL);
    if (item < 0)
        return 0;
    strlcpy(name, ps5_fg_item_name((unsigned)item), name_len);
    if (item == PS5_FG_RESET)
        return strlcpy(value, "", value_len);
    ps5_fg_current(&now);
    {
        static unsigned logged;
        if (logged < 24)
        {
            logged++;
            RARCH_LOG("[FrameGen] menu entry %s: \"%s\" = \"%s\"\n", label, name,
                      ps5_fg_choice_name((unsigned)item, ps5_fg_get(&now, (unsigned)item)));
        }
    }
    return strlcpy(value, ps5_fg_choice_name((unsigned)item, ps5_fg_get(&now, (unsigned)item)),
                   value_len);
}

void ps5_fg_menu_help(const char *label, char *s, size_t len)
{
    int item = ps5_fg_item_of(label, NULL);
    if (item >= 0)
    {
        static unsigned logged;
        strlcpy(s, ps5_fg_item_help((unsigned)item), len);
        if (logged++ < 3)
            RARCH_LOG("[FrameGen] menu description %s: \"%s\"\n", label, s);
    }
    else if (ps5_fg_menu_is_folder(label))
        strlcpy(s, "Choose how frames are generated between a game's frames, and test the result.",
                len);
}

/* The floor is off unless PS5_FRAMEGEN_FLOOR=1 names it (a test): every picture of the output rate
 * is generated, whatever rate the core makes its frames at. The rates a core makes with and without
 * generation were compared in docs/design.md and did not tell a core that generation slows from a
 * game that changed its own rate. */
static bool floor_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0)
    {
        const char *e = getenv("PS5_FRAMEGEN_FLOOR");
        enabled = e && e[0] == '1';
    }
    return enabled == 1;
}

/* A core that blocks until its next frame is made says so (the private environment command of
 * patches/series, 0123); the runloop notes the instant it calls the core. */
int ps5_fg_core_blocks;
unsigned ps5_fg_core_period_us; /* the microseconds the core took to make the next frame handed over
                                   (0x25002) */
unsigned ps5_fg_core_gap_frames; /* the frames the next frame handed over covers, from the core
                                    (0x25002); 0: not said */
uint64_t ps5_fg_core_run_begin_ns;

/* The runloop calls this with the time of each call of the core (patches/series, 0122) */
static double run_sum_ms, run_max_ms;
static unsigned run_calls, run_long;
void ps5_fg_note_core_run(double ms)
{
    run_sum_ms += ms;
    if (ms > run_max_ms)
        run_max_ms = ms;
    if (ms > 20.0)
        run_long++;
    run_calls++;
}

/* The driver calls this for every present; the message is refreshed about twice a second */
void ps5_fg_osd(bool plan, unsigned subframes, unsigned period, unsigned width, unsigned height,
                unsigned long generated, unsigned long unique, unsigned level, double gen_ms,
                unsigned long calls)
{
    static unsigned osd_calls;
    const unsigned pictures = subframes / (ps5_fg_dup ? ps5_fg_dup : 1);
    static unsigned long presents, last_generated;
    static unsigned long gap_n[2], gap_off[2], last_unique, last_calls;
    static double new_per_s, calls_per_s;
    static unsigned slow_windows;
    static time_t floor_until;
    static double gap_sum[2], gap_max[2];
    static struct timespec last;
    struct ps5_fg_options now;
    char text[200];
    settings_t *settings = config_get_ptr();
    struct timespec t;
    const bool show_osd = settings->bools.video_framegen_osd;
    ps5_fg_active = plan && settings->bools.video_framegen && !ps5_fg_floor && pictures >= 2;
    presents++;
    clock_gettime(CLOCK_MONOTONIC, &t);
    {
        /* the last presents: instant shown and ms since the one before (a line with the pace) */
        static float seq_t[24];
        static float seq_ms[24];
        static unsigned seq_n;
        static struct timespec seq_prev;
        if (seq_prev.tv_sec)
            seq_ms[seq_n % 24] =
                (float)((t.tv_sec - seq_prev.tv_sec) * 1e3 + (t.tv_nsec - seq_prev.tv_nsec) / 1e6);
        seq_t[seq_n % 24] = ps5_fg_dbg_t;
        seq_prev = t;
        seq_n++;
        if (seq_n % 240 == 0)
        {
            char line[400];
            unsigned q, o = 0;
            for (q = 0; q < 24 && o < sizeof(line) - 24; q++)
                o += (unsigned)snprintf(line + o, sizeof(line) - o, " %.2f@%.1f",
                                        seq_t[(seq_n + q) % 24], seq_ms[(seq_n + q) % 24]);
            RARCH_LOG("[FrameGen] seq:%s\n", line);
        }
    }
    if (last.tv_sec == 0)
        last = t;
    {
        /* Present spacing, apart for presents that showed a generated frame and those that did not
         * (the log line below) */
        static struct timespec prev;
        static unsigned long prev_generated;
        if (prev.tv_sec)
        {
            const double ms =
                (double)(t.tv_sec - prev.tv_sec) * 1e3 + (double)(t.tv_nsec - prev.tv_nsec) / 1e6;
            const unsigned cls =
                generated != prev_generated; /* 1: this present showed a generated frame */
            gap_n[cls]++;
            gap_sum[cls] += ms;
            if (ms > gap_max[cls])
                gap_max[cls] = ms;
            if (ms > 10.5 || ms < 6.0)
                gap_off[cls]++;
        }
        prev = t;
        prev_generated = generated;
    }
    if ((t.tv_sec - last.tv_sec) * 1000000000L + (t.tv_nsec - last.tv_nsec) >= 2000000000L)
    {
        double seconds =
            (double)(t.tv_sec - last.tv_sec) + (double)(t.tv_nsec - last.tv_nsec) / 1e9;
        new_per_s = (double)(unique - last_unique) / seconds;
        calls_per_s = (double)(calls - last_calls) / seconds;
        last_unique = unique;
        last_calls = calls;
        RARCH_LOG("[FrameGen] pace: %.1f presents/s, %.1f generated/s, %.1f new frames/s, core "
                  "%.1f calls/s, governor level %u (%.2f ms a picture)\n",
                  (double)presents / seconds, (double)(generated - last_generated) / seconds,
                  new_per_s, calls_per_s, level, gen_ms);
        /* The floor: a core that cannot keep its own rate gains nothing from frames made between
         * its frames if the work only slows it more. Whether it does is measured, not assumed: a
         * core whose game steps at 30 while it declares 60 (a core that blocks for its next frame)
         * also makes fewer calls a second than the rate it declares, and the generator costs it
         * nothing. Two slow windows turn generation off for one window; the rate then, and the rate
         * with generation back, are compared: generation that costs the core under a tenth of its
         * rate stays on, and stays on while the core holds that rate. Generation that slows the
         * core is off for ten seconds, then tried again. */
        {
            static int
                phase; /* 0 generating, 1 off for the measure, 2 generating again, to be compared */
            static unsigned verify_windows, off_windows;
            static double last_slow_rate;
            static double rate_off, rate_kept; /* calls a second: without generation; the rate
                                                  generation was found harmless at */
            if (ps5_fg_floor)
            {
                if (phase == 1)
                {
                    /* the first window with generation off is a mix of both: the second is the
                     * core's rate without it */
                    if (++off_windows >= 2)
                    {
                        rate_off = calls_per_s;
                        ps5_fg_floor = 0;
                        phase = 2;
                        verify_windows = 0;
                        slow_windows = 0;
                    }
                }
                else if (t.tv_sec >= floor_until)
                {
                    ps5_fg_floor = 0;
                    slow_windows = 0;
                }
            }
            else if (phase == 2)
            {
                if (++verify_windows >= 2) /* the first window after the switch is a mix of both */
                {
                    phase = 0;
                    if (calls_per_s >= 0.9 * rate_off)
                    {
                        rate_kept = rate_off;
                        RARCH_LOG("[FrameGen] floor: the core makes %.1f calls/s with generation "
                                  "and %.1f without; generation "
                                  "costs it nothing and stays on\n",
                                  calls_per_s, rate_off);
                    }
                    else
                    {
                        rate_kept = 0.0;
                        ps5_fg_floor = 1;
                        floor_until = t.tv_sec + 10;
                        RARCH_LOG("[FrameGen] floor: the core makes %.1f calls/s with generation "
                                  "and %.1f without; frames are "
                                  "shown as it makes them for 10 s\n",
                                  calls_per_s, rate_off);
                    }
                }
            }
            else if (floor_enabled() && plan && ps5_fg_core_fps > 20.0f &&
                     calls_per_s < 0.85 * (rate_kept > 0.0 ? rate_kept : (double)ps5_fg_core_fps))
            {
                /* the core's rate must have settled: a game that has just changed its rate (a level
                 * loaded, a state loaded) is not yet a rate to judge */
                if (slow_windows == 0 || fabs(calls_per_s - last_slow_rate) <= 0.1 * last_slow_rate)
                    slow_windows++;
                else
                    slow_windows = 1;
                last_slow_rate = calls_per_s;
                if (slow_windows >= 2)
                {
                    ps5_fg_floor = 1;
                    slow_windows = 0;
                    off_windows = 0;
                    if (rate_kept > 0.0)
                        rate_kept = 0.0; /* it slowed below the rate generation was found harmless
                                            at: measure again */
                    phase = 1;
                    floor_until = t.tv_sec + 4;
                    RARCH_LOG("[FrameGen] floor: the core runs at %.1f of %.1f frames/s; "
                              "generation off for two windows to "
                              "see whether it is the cause\n",
                              calls_per_s, (double)ps5_fg_core_fps);
                }
            }
            else
                slow_windows = 0;
        }
        RARCH_LOG("[FrameGen] core run: %u calls, mean %.2f ms, max %.2f ms, %u over 20 ms\n",
                  run_calls, run_calls ? run_sum_ms / run_calls : 0.0, run_max_ms, run_long);
        {
            /* the CPU the title's threads used over the window: this thread (the frontend and the
             * generator's recording) and every thread (with the core's), in cores */
            static double thread_prev, process_prev;
            struct timespec ct, cp;
            double thread_now, process_now;
            clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ct);
            clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cp);
            thread_now = (double)ct.tv_sec + (double)ct.tv_nsec / 1e9;
            process_now = (double)cp.tv_sec + (double)cp.tv_nsec / 1e9;
            RARCH_LOG("[FrameGen] cpu: frontend thread %.2f cores, all threads %.2f cores\n",
                      (thread_now - thread_prev) / seconds, (process_now - process_prev) / seconds);
            thread_prev = thread_now;
            process_prev = process_now;
        }
        run_sum_ms = run_max_ms = 0.0;
        run_calls = run_long = 0;
        RARCH_LOG("[FrameGen] spacing (ms): real n %lu mean %.2f max %.2f off %lu | generated n "
                  "%lu mean %.2f max %.2f off %lu\n",
                  gap_n[0], gap_n[0] ? gap_sum[0] / (double)gap_n[0] : 0.0, gap_max[0], gap_off[0],
                  gap_n[1], gap_n[1] ? gap_sum[1] / (double)gap_n[1] : 0.0, gap_max[1], gap_off[1]);
        memset(gap_n, 0, sizeof(gap_n));
        memset(gap_sum, 0, sizeof(gap_sum));
        memset(gap_max, 0, sizeof(gap_max));
        memset(gap_off, 0, sizeof(gap_off));
        ps5_fg_presents_per_s = (float)((double)presents / seconds);
        presents = 0;
        last_generated = generated;
        last = t;
    }
    if (!show_osd || ++osd_calls % 120)
        return;
    ps5_fg_current(&now);
    if (!now.enable)
        snprintf(text, sizeof(text), "FrameGen: off");
    else if (pictures < 2)
        snprintf(text, sizeof(text),
                 "FrameGen: nothing to make - the game already runs at the Target Framerate (%s)",
                 ps5_fg_choice_name(PS5_FG_TARGET, now.target));
    else if (ps5_fg_floor)
        snprintf(text, sizeof(text),
                 "FrameGen: core at %.0f fps (needs %.0f), too slow: frames shown as the core "
                 "makes them",
                 calls_per_s, (double)ps5_fg_core_fps);
    else if (!plan)
        snprintf(text, sizeof(text), "FrameGen: paused (menu, fast-forward or pause)");
    else
        snprintf(text, sizeof(text),
                 "FrameGen: %s  game %.1f fps (%.1f new/s) -> %.0f fps  cadence %u%s  %ux%u  "
                 "generated %lu  cost level %u (%.1f ms)",
                 ps5_fg_choice_name(PS5_FG_METHOD, now.method), (double)ps5_fg_core_fps, new_per_s,
                 (double)ps5_fg_target_fps, period ? period : 1, now.cadence ? " (forced)" : "",
                 width, height, generated, level, gen_ms);
    runloop_msg_queue_push(text, strlen(text), 1, 150, false, NULL, MESSAGE_QUEUE_ICON_DEFAULT,
                           MESSAGE_QUEUE_CATEGORY_INFO);
}

/* The driver's working-size limit for a size choice */
unsigned long ps5_fg_size_pixels_of(unsigned size)
{
    return ps5_fg_size_pixels(size);
}
