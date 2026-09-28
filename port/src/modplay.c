/* modplay.c - see modplay.h.
 *
 * micromod.c is included here rather than compiled on its own: it stays
 * as upstream wrote it apart from the pattern jumps' hooks (marked
 * "pddnative"), and its warnings are not ours to fix. */
#include <stdlib.h>
#include <string.h>
#include "modplay.h"

#if defined(_MSC_VER)
#pragma warning(push, 1)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include "../third_party/micromod/micromod.c"
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

static signed char *module;

int modplay_load(const uint8_t *data, size_t size, long rate)
{
    long len;

    modplay_unload();
    if (size < 1084)
        return -1;
    len = micromod_calculate_mod_file_len((signed char *)data);
    if (len <= 0)
        return -1;
    /* a file shorter than its header says is padded with zeros */
    module = (signed char *)calloc(1, (size_t)len > size ? (size_t)len : size);
    if (!module)
        return -1;
    memcpy(module, data, size);
    if (micromod_initialise(module, rate) != 0) {
        modplay_unload();
        return -1;
    }
    return 0;
}

void modplay_unload(void)
{
    free(module);
    module = NULL;
}

int modplay_loaded(void)
{
    return module != NULL;
}

void modplay_render(int16_t *out, int frames)
{
    int i;

    memset(out, 0, (size_t)frames * 2 * sizeof *out);
    if (!module)
        return;
    micromod_get_audio(out, frames);
    /* micromod pans the channels hard left and right as the Amiga does;
     * SBLASTER.SDR plays mono (DSP command 14h, the SB Pro's stereo
     * mixer not set), so both sides get the mean */
    for (i = 0; i < frames * 2; i += 2)
        out[i] = out[i + 1] = (int16_t)((out[i] + out[i + 1]) / 2);
}

void modplay_skip_ticks(int ticks)
{
    if (!module)
        return;
    while (ticks-- > 0)
        micromod_get_audio(NULL, tick_len - tick_offset);
}

void modplay_set_position(int pos)
{
    if (module)
        micromod_set_position(pos);
}

/* SBLASTER.SDR's AL=10h: the position one less than `pos` at once (and
 * returned so by a second call before the next tick), the row counter to
 * 1, so the next tick plays the row after the current one and then goes
 * to `pos`; a jump the rows asked for is already the position */
int modplay_jump(int pos)
{
    int was;

    if (set_pattern >= 0)
        was = (int)set_pattern - 1;
    else if (break_pattern >= 0)
        was = (int)(break_pattern < song_length ? break_pattern : 0);
    else if (next_row < 0)
        was = (int)(pattern + 1 < song_length ? pattern + 1 : 0);
    else
        was = (int)pattern;
    if (module) {
        set_pattern = pos < song_length ? pos : 0;
        tick = 1;
    }
    return was;
}

static int (*program_hook)(int pos);

static long call_hook(long pos)
{
    return program_hook((int)pos) & 0xFF;
}

void modplay_set_jump_hook(int (*hook)(int pos))
{
    program_hook = hook;
    jump_hook = hook ? call_hook : NULL;
}

void modplay_note(int chan, int period, int instrument, int effect, int param)
{
    struct note *note;

    if (!module || chan < 0 || chan >= num_channels)
        return;
    note = &channels[chan].note;
    note->key = (unsigned short)period;
    note->instrument = (unsigned char)instrument;
    note->effect = (unsigned char)effect;
    note->param = (unsigned char)param;
    channel_row(&channels[chan]);
}

void modplay_set_gain(int value)
{
    micromod_set_gain(value);
}
