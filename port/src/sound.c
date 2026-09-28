/* sound.c - see sound.h.  The platform's audio thread pulls the samples
 * from micromod; every call from the program takes the audio lock.
 * Without an audio device the program's ticks move the module on
 * (snd_tick). */
#include <math.h>
#include <stdlib.h>
#include "audiofx.h"
#include "modplay.h"
#include "platform.h"
#include "sound.h"
#include "sys.h"

#define RATE 44100

static int opened, playing;
static float out_gain = 1.0f;          /* the player's volume (hud.c) */

/* SBLASTER.SDR at quality 0, as tools/run runs it: a module tick is 240
 * samples at 12 kHz ([795] of its data, 125 BPM; the modules set no other
 * tempo), the DMA buffer four ticks.  At play it mixes the first four
 * ticks and starts the DMA; then each AL=8 mixes up to where the DMA reads
 * and steps a tick at each 240-sample boundary it passes (the routine at
 * 0C7Ah of its code).  The read position in samples at the AL=8 of the
 * nth tick after play: DMA_PHASE + n * DMA_RATE / the program's tick rate,
 * plus what the AL=8 comes later in the tick (snd_tick's `late`).
 * DMA_RATE is 1e6/83 (time constant 173) less what the restart of each
 * block from the interrupt costs, measured over 240 s; DMA_PHASE measured
 * on five tables and in both screen modes.  The rest varies by a sample
 * or so with the instructions the original runs before its AL=8, so the
 * module's ticks so far can be one off the original's for a program
 * tick: at 1 to 5 of some 4,000 in 60 s runs of the attract show and of
 * play (Ignition, Steel Wheel, Nightmare). */
#define TICK_SAMPLES 240
#define DMA_BUFFER 960
#define DMA_RATE 12048.136
#define DMA_PHASE (-36.5)

static double dma_read;         /* samples read since the DMA started, + phase */
static int mix_pos;             /* where the driver's mixing goes on, 0-959 */

static void fill(int16_t *out, int frames, void *user)
{
    int i;
    (void)user;
    if (playing) {
        modplay_render(out, frames);
    } else {
        for (i = 0; i < frames * 2; i++)
            out[i] = 0;
    }
    audiofx_process(out, frames, out_gain);
}

void snd_init(void)
{
    if (!opened) {
        audiofx_init(RATE);
        opened = plat_audio_start(RATE, fill, NULL);
    }
}

void snd_set_fx(int bass, int treble, int oomph, int headphone)
{
    plat_audio_lock();
    audiofx_set(bass, treble, oomph, headphone);
    plat_audio_unlock();
}

void snd_set_fx_bypass(int on)
{
    plat_audio_lock();
    audiofx_set_bypass(on);
    plat_audio_unlock();
}

void snd_set_gain(float gain)
{
    plat_audio_lock();
    out_gain = gain < 0 ? 0 : gain > 1 ? 1 : gain;
    plat_audio_unlock();
}

int snd_load_module(const char *path)
{
    size_t size;
    uint8_t *data = sys_load(path, &size);
    int r;

    if (!data)
        return 1;
    plat_audio_lock();
    playing = 0;
    r = modplay_load(data, size, RATE);
    modplay_set_gain(64);
    plat_audio_unlock();
    free(data);
    return r != 0;
}

void snd_play(void)
{
    plat_audio_lock();
    playing = modplay_loaded();
    if (playing && !opened) {
        modplay_skip_ticks(3);  /* the first of the four: set_position's */
        mix_pos = 0;
        dma_read = DMA_PHASE;
    }
    plat_audio_unlock();
}

void snd_tick(double hz, double late)
{
    int free, n, total;

    if (opened || !playing || hz <= 0)
        return;
    dma_read += DMA_RATE / hz;
    free = (int)fmod(floor(dma_read + late * DMA_RATE), DMA_BUFFER) - mix_pos;
    if (free == 0)
        return;
    if (free < 0)
        free += DMA_BUFFER;
    n = TICK_SAMPLES;
    if (free < TICK_SAMPLES) {
        if (free < 0x50)
            return;
        n = free;
    }
    do {
        total = n;
        if (mix_pos % TICK_SAMPLES) {
            /* a part: to the buffer's end or the next tick */
            if (mix_pos + n > DMA_BUFFER)
                n = DMA_BUFFER - mix_pos;
            if ((mix_pos + n) % TICK_SAMPLES < n)
                n -= (mix_pos + n) % TICK_SAMPLES;
        } else {
            modplay_skip_ticks(1);
        }
        mix_pos += n;
        if (mix_pos >= DMA_BUFFER)
            mix_pos = 0;
        n = total - n;
    } while (n >= 0x50);
}

void snd_stop(void)
{
    plat_audio_lock();
    playing = 0;
    modplay_unload();
    plat_audio_unlock();
}

void snd_volume(int cx)
{
    cx = cx < 0 ? 0 : cx > 0x100 ? 0x100 : cx;
    plat_audio_lock();
    modplay_set_gain(64 * cx / 0x100);
    plat_audio_unlock();
}

int snd_position(int bx)
{
    int was;

    plat_audio_lock();          /* also taken by the jump callback's thread */
    was = modplay_jump(bx);
    if (!playing)
        modplay_set_position(bx);   /* not started yet: from its first row */
    plat_audio_unlock();
    return was;
}

void snd_jump_callback(int (*callback)(int target))
{
    plat_audio_lock();
    modplay_set_jump_hook(callback);
    plat_audio_unlock();
}

/* the ProTracker periods of the notes C-1 to B-3, which SBLASTER.SDR
 * numbers 1-36 (in the pattern cells it converts at loading and in
 * AL=11h's BL) */
static const short periods[36] = {
    856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453,
    428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226,
    214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113
};

void snd_effect(int bl, int bh, int cl, int dl)
{
    int period = bl >= 1 && bl <= 36 ? periods[bl - 1] : 0;

    plat_audio_lock();
    modplay_note(dl - 1, period, cl & 0x1F, bh ? 0xC : 0, bh);
    plat_audio_unlock();
}
