/* modplay.c - see modplay.h.
 *
 * micromod.c is included here rather than compiled on its own: it stays
 * as upstream wrote it apart from the pattern jump's hook (marked
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
    memset(out, 0, (size_t)frames * 2 * sizeof *out);
    if (module)
        micromod_get_audio(out, frames);
}

void modplay_set_position(int pos)
{
    if (module)
        micromod_set_position(pos);
}

int modplay_jump(int pos)
{
    int was = (int)pattern;

    if (module) {
        break_pattern = pos < song_length ? pos : 0;
        next_row = 0;
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

void modplay_set_gain(int value)
{
    micromod_set_gain(value);
}
