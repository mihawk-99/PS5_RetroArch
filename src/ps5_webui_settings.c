/* PS5 RetroArch - the WebUI's saved settings, taken into RetroArch's configuration once.
 *
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The WebUI saves a setting into /app0/config/webui.cfg, which the title appends to
 * RetroArch's configuration at each start (src/main.cpp): RetroArch may be running, or be
 * closed from the home screen without saving, so the page cannot write retroarch.cfg
 * itself. Appended at every start, a value saved there once won for ever: whatever was
 * then set in RetroArch's own menu, or in retroarch.cfg by hand, was back at the WebUI's
 * value at the next start (issue 130, a volume gain that returned to +12 dB).
 *
 * So at a start, before RetroArch reads its configuration, each such setting is moved:
 * its line replaces retroarch.cfg's and leaves webui.cfg. It is applied once, and a later
 * change in the menu stays. What other code reads from webui.cfg stays there: the
 * RetroAchievements account (src/ps5_cheevos.c) and EmulationStation's
 * esde_separate_systems (frontends/es-de/ps5/library_ps5.cpp).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A whole file, at most limit bytes, as a string; NULL when it is not there or larger. */
static char *read_whole(const char *path, size_t limit, size_t *length)
{
    FILE *file = fopen(path, "rb");
    if (!file)
        return NULL;
    char *text = (char *)malloc(limit + 1);
    size_t count = text ? fread(text, 1, limit + 1, file) : 0;
    fclose(file);
    if (!text || count > limit)
    {
        free(text);
        return NULL;
    }
    text[count] = '\0';
    *length = count;
    return text;
}

/* The setting a line sets: its name's start and length, or 0 for a blank or comment line. */
static size_t line_key(const char *line, size_t length, const char **key)
{
    size_t start = 0;
    while (start < length && (line[start] == ' ' || line[start] == '\t'))
        start++;
    size_t end = start;
    while (end < length && line[end] != '=' && line[end] != ' ' && line[end] != '\t' &&
           line[end] != '\r')
        end++;
    if (end == start || line[start] == '#' || !memchr(line + end, '=', length - end))
        return 0;
    *key = line + start;
    return end - start;
}

/* What stays in webui.cfg, for the code that reads it there. */
static int stays(const char *key, size_t length)
{
    static const char separate[] = "esde_separate_systems";
    return (length >= 8 && memcmp(key, "cheevos_", 8) == 0) ||
           (length == sizeof(separate) - 1 && memcmp(key, separate, length) == 0);
}

static int write_whole(const char *path, const char *text, size_t length)
{
    char temporary[1100];
    if (snprintf(temporary, sizeof(temporary), "%s.tmp", path) >= (int)sizeof(temporary))
        return -1;
    FILE *file = fopen(temporary, "wb");
    int ok = file && fwrite(text, 1, length, file) == length;
    if (file && fclose(file) != 0)
        ok = 0;
    if (ok && rename(temporary, path) == 0)
        return 0;
    remove(temporary);
    return -1;
}

/* Moves each setting of webui that is RetroArch's into config: the settings moved, 0 when
 * there was none (or no config to take them: nothing is touched), -1 when a file could not
 * be written (webui is then as it was, and is appended as before). */
int ps5_webui_settings_fold(const char *webui, const char *config)
{
    size_t webui_length = 0, config_length = 0;
    char *saved = read_whole(webui, 1u << 20, &webui_length);
    char *main_text = saved ? read_whole(config, 8u << 20, &config_length) : NULL;
    if (!saved || !main_text)
    {
        free(saved);
        return 0;
    }
    /* Room for every line of both, each with a line end of its own. */
    char *kept = (char *)malloc(webui_length + 2);
    char *moved = (char *)malloc(webui_length + 2);
    char *merged = (char *)malloc(config_length + webui_length + 3);
    size_t kept_length = 0, moved_length = 0, merged_length = 0;
    int count = -1;
    if (kept && moved && merged)
    {
        count = 0;
        for (size_t at = 0; at < webui_length;)
        {
            const char *end = memchr(saved + at, '\n', webui_length - at);
            const size_t length = end ? (size_t)(end - (saved + at)) : webui_length - at;
            const char *key = NULL;
            const size_t key_length = line_key(saved + at, length, &key);
            char *into = key_length && !stays(key, key_length) ? moved : kept;
            size_t *into_length = into == moved ? &moved_length : &kept_length;
            if (key_length || into == kept)
            {
                memcpy(into + *into_length, saved + at, length);
                *into_length += length;
                into[(*into_length)++] = '\n';
            }
            count += into == moved;
            at += length + 1;
        }
        /* The configuration without the lines the moved settings replace, then those. */
        for (size_t at = 0; count > 0 && at < config_length;)
        {
            const char *end = memchr(main_text + at, '\n', config_length - at);
            const size_t length = end ? (size_t)(end - (main_text + at)) : config_length - at;
            const char *key = NULL;
            const size_t key_length = line_key(main_text + at, length, &key);
            int replaced = 0;
            for (size_t m = 0; key_length && !replaced && m < moved_length;)
            {
                const char *moved_end = memchr(moved + m, '\n', moved_length - m);
                const size_t moved_line = (size_t)(moved_end - (moved + m));
                const char *moved_key = NULL;
                const size_t moved_key_length = line_key(moved + m, moved_line, &moved_key);
                replaced =
                    moved_key_length == key_length && memcmp(moved_key, key, key_length) == 0;
                m += moved_line + 1;
            }
            if (!replaced)
            {
                memcpy(merged + merged_length, main_text + at, length);
                merged_length += length;
                merged[merged_length++] = '\n';
            }
            at += length + 1;
        }
        if (count > 0)
        {
            memcpy(merged + merged_length, moved, moved_length);
            merged_length += moved_length;
            /* The configuration first: were the second write lost, the setting is in both,
             * and is taken again at the next start. */
            if (write_whole(config, merged, merged_length) != 0 ||
                write_whole(webui, kept, kept_length) != 0)
                count = -1;
        }
    }
    free(kept);
    free(moved);
    free(merged);
    free(saved);
    free(main_text);
    return count;
}
