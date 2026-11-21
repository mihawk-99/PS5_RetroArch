/* PS5 RetroArch - the RetroAchievements password the WebUI saved, forgotten once used.
 *
 * The WebUI writes a new password into /app0/config/webui.cfg (with an empty token, so the
 * new account signs in with it), and the title appends that file to RetroArch's config at
 * every start. Once RetroArch has signed in it keeps a token and clears its password, but
 * only in its own settings, and retroarch.cfg holds the old password until RetroArch next
 * saves it: patches/series 0130 calls this then, so the password stays in neither file
 * (/app0/config, a folder FTP can read). RetroArch's next save is when it quits, and a
 * title the console closes never quits: 0139 writes the account into retroarch.cfg at
 * sign-in, before the password goes, so the next start still has it (issue 110).
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <stdio.h>
#include <string.h>

/* Whether a config line sets the key name ("  cheevos_password = ..."). */
static int sets(const char *line, const char *name)
{
    const size_t length = strlen(name);
    while (*line == ' ' || *line == '\t')
        line++;
    if (strncmp(line, name, length) != 0)
        return 0;
    line += length;
    while (*line == ' ' || *line == '\t')
        line++;
    return *line == '=';
}

/* Removes the cheevos_password line, and the cheevos_token line when token_too, from the
 * config at path. Returns the lines removed, 0 when there were none (the file is left
 * alone), -1 when it could not be rewritten (the file is left alone too). */
int ps5_cheevos_forget_password(const char *path, int token_too)
{
    char line[1024], temporary[512];
    int removed = 0;
    FILE *in = fopen(path, "r"), *out;
    if (!in)
        return 0;
    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    out = fopen(temporary, "w");
    if (!out)
    {
        fclose(in);
        return -1;
    }
    /* shortcut: a line over 1023 bytes is copied in pieces, each checked as a line; the
     * WebUI writes no such line (its values have no line breaks and are short). */
    while (fgets(line, sizeof(line), in))
    {
        if (sets(line, "cheevos_password") || (token_too && sets(line, "cheevos_token")))
            removed++;
        else
            fputs(line, out);
    }
    fclose(in);
    if (fclose(out) != 0 || removed == 0)
    {
        remove(temporary);
        return removed ? -1 : 0;
    }
    return rename(temporary, path) == 0 ? removed : -1;
}

/* Whether value can stand between a config line's quotes. */
static int plain(const char *value)
{
    if (!value || !*value)
        return 0;
    for (; *value; value++)
        if (*value == '"' || (unsigned char)*value < 0x20)
            return 0;
    return 1;
}

/* Writes the account RetroArch has just signed in with into the config at path: its
 * cheevos_username and cheevos_token lines (added, or put in place of other ones) and no
 * cheevos_password line. Returns 0 when the file holds them (it is left alone when it
 * already did), -1 when it does not: no such file, a value that cannot be written as a
 * config value, or a file that could not be rewritten (left as it was). */
int ps5_cheevos_keep_account(const char *path, const char *username, const char *token)
{
    char line[1024], want_user[256], want_token[256], temporary[512];
    int changed = 0, have_user = 0, have_token = 0, ended = 1;
    FILE *in, *out;
    if (!plain(username) || !plain(token) ||
        snprintf(want_user, sizeof(want_user), "cheevos_username = \"%s\"\n", username) >=
            (int)sizeof(want_user) ||
        snprintf(want_token, sizeof(want_token), "cheevos_token = \"%s\"\n", token) >=
            (int)sizeof(want_token))
        return -1;
    in = fopen(path, "r");
    if (!in)
        return -1;
    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    out = fopen(temporary, "w");
    if (!out)
    {
        fclose(in);
        return -1;
    }
    while (fgets(line, sizeof(line), in))
    {
        int *have = sets(line, "cheevos_username") ? &have_user
                    : sets(line, "cheevos_token")  ? &have_token
                                                   : NULL;
        if (sets(line, "cheevos_password") ||
            (have && (*have || strcmp(line, have == &have_user ? want_user : want_token) != 0)))
        {
            changed = 1;
            continue;
        }
        if (have)
            *have = 1;
        fputs(line, out);
        ended = line[strlen(line) - 1] == '\n';
    }
    fclose(in);
    if (!have_user || !have_token)
    {
        changed = 1;
        fprintf(out, "%s%s%s", ended ? "" : "\n", have_user ? "" : want_user,
                have_token ? "" : want_token);
    }
    if (fclose(out) != 0 || !changed)
    {
        remove(temporary);
        return changed ? -1 : 0;
    }
    return rename(temporary, path) == 0 ? 0 : -1;
}
