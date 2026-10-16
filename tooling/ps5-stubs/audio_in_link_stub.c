/* SPDX-License-Identifier: GPL-3.0-or-later
 * Host-link provider only. Records resident libSceAudioIn imports;
 * this stub must never be packaged or run on the PS5.
 */
int sceAudioInInit(void)
{
    return -1;
}
int sceAudioInOpen(int user, int type, int index, unsigned len, unsigned rate, unsigned param)
{
    (void)user;
    (void)type;
    (void)index;
    (void)len;
    (void)rate;
    (void)param;
    return -1;
}
int sceAudioInInput(int handle, void *dest)
{
    (void)handle;
    (void)dest;
    return -1;
}
int sceAudioInClose(int handle)
{
    (void)handle;
    return -1;
}
int sceAudioInGetSilentState(int handle)
{
    (void)handle;
    return -1;
}
