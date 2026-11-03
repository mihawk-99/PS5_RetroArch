/* PS5 RetroArch - the controllers' own speakers, for a core (patches/series, 0139).
 *
 * A core asks for this with the port-private environment call 0x20000 | 3, which hands back a
 * pointer to the table below (or fails on a RetroArch without it). Each RetroArch port's
 * controller speaker takes 48 kHz mono S16 that the core writes whenever it has some; a thread per
 * open speaker feeds AudioOut's pad speaker port (SCE_AUDIO_OUT_PORT_TYPE_PADSPK, as PS4 and PS5
 * games use it) at the speaker's pace, with silence when the core has nothing queued. Dolphin's
 * integrated GBA uses it for each GBA's sound on its player's controller.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PS5_PAD_SPEAKER_H
#define PS5_PAD_SPEAKER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PS5_PAD_SPEAKER_ENVIRONMENT (0x20000 | 3)
#define PS5_PAD_SPEAKER_VERSION 1
#define PS5_PAD_SPEAKER_RATE 48000

struct ps5_pad_speaker
{
   unsigned version; /* PS5_PAD_SPEAKER_VERSION */
   /* Opens the speaker of the controller in RetroArch port `port` (0-3); false when that port has
    * no signed-in controller or its speaker does not open. Opening an open one is true. */
   bool (*open)(unsigned port);
   /* Queues `count` samples (48 kHz mono S16) for it; returns how many were taken (a queue of
    * about a fifth of a second: the rest would only add delay). Any thread. */
   size_t (*write)(unsigned port, const int16_t *samples, size_t count);
   /* Stops and closes it. */
   void (*close)(unsigned port);
};

/* The table (the environment call's answer). */
const struct ps5_pad_speaker *ps5_pad_speaker_interface(void);

/* Closes every open speaker (the core went away). */
void ps5_pad_speaker_close_all(void);

#ifdef __cplusplus
}
#endif

#endif
