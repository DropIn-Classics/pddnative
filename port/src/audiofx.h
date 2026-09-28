/* audiofx.h - optional shaping of what goes to the speakers: bass and
 * treble shelves, "oomph" (a second, lower bass shelf) and a headphone
 * mode.  The music is mono, as SBLASTER.SDR plays it; the headphone mode
 * gives it a narrow stereo image (a short echo per ear) and feeds a
 * little of each side into the other.  With everything at 0 the sound is
 * as it was, apart from a DC blocker.
 *
 * Only the window's audio thread runs this; the headless build (and so
 * every comparison with the original) never does.  Not thread-safe: the
 * caller holds plat_audio_lock() around audiofx_set()/_bypass(). */
#ifndef PD_AUDIOFX_H
#define PD_AUDIOFX_H

#include <stdint.h>

#define AUDIOFX_EQ_MAX 12       /* bass, treble: -12..+12 dB */
#define AUDIOFX_OOMPH_MAX 12    /* oomph: 0..+12 dB */

/* the output rate; the filters start from silence */
void audiofx_init(long rate);

/* the settings (dB, headphone 0/1); out of range is clamped */
void audiofx_set(int bass, int treble, int oomph, int headphone);

/* 1: everything flat whatever audiofx_set() said (to compare) */
void audiofx_set_bypass(int on);
int audiofx_bypass(void);

/* `frames` stereo 16-bit frames in place, then times `gain` (0-1);
 * what goes over full scale is clipped */
void audiofx_process(int16_t *buf, int frames, float gain);

#endif
