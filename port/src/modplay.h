/* modplay.h - MOD playback: micromod (third_party/micromod) behind a small
 * interface.
 *
 * Not thread-safe: the audio thread renders while the program calls the
 * rest, so the caller holds plat_audio_lock() around every call. */
#ifndef PD_MODPLAY_H
#define PD_MODPLAY_H

#include <stddef.h>
#include <stdint.h>

/* Takes a copy of the module and starts it at position 0.  0 on success,
 * -1 if it is not a MOD micromod understands. */
int modplay_load(const uint8_t *data, size_t size, long rate);
void modplay_unload(void);
int modplay_loaded(void);

/* `frames` stereo 16-bit frames, both sides the same (mono, as
 * SBLASTER.SDR); silence without a module */
void modplay_render(int16_t *out, int frames);

/* the module on by `ticks` of its ticks (to the start of the next one),
 * without output */
void modplay_skip_ticks(int ticks);

/* row 0 of the order list's position `pos` */
void modplay_set_position(int pos);

/* at the next tick the module plays its next row and then goes on at
 * position `pos` (SBLASTER.SDR's AL=10h); the position it plays now */
int modplay_jump(int pos);

/* a pattern jump (Bxx) goes on at the position `hook` returns for its
 * target, and is ignored while a jump is pending in the row (as
 * SBLASTER.SDR does); the hook runs on the audio thread.  NULL: micromod's
 * own jumps */
void modplay_set_jump_hook(int (*hook)(int pos));

/* a pattern cell played at once in channel `chan` (from 0): the period
 * (0: none), instrument (1-31, 0: none), effect and parameter as in a MOD
 * row; the module's next row on that channel follows as usual */
void modplay_note(int chan, int period, int instrument, int effect, int param);

/* 0-64; 64 is the loudest a 4-channel module plays without clipping */
void modplay_set_gain(int value);

#endif
