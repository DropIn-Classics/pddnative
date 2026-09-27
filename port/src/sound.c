/* sound.c - see sound.h.  The platform's audio thread pulls the samples
 * from micromod; every call from the program takes the audio lock. */
#include <stdlib.h>
#include "modplay.h"
#include "platform.h"
#include "sound.h"
#include "sys.h"

#define RATE 44100

static int opened, playing;

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
}

void snd_init(void)
{
    if (!opened)
        opened = plat_audio_start(RATE, fill, NULL);
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
    plat_audio_unlock();
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
    plat_audio_lock();
    modplay_set_position(bx);
    plat_audio_unlock();
    return 0;
}

void snd_effect(int bl, int bh, int cl, int dl)
{
    (void)bl;
    (void)bh;
    (void)cl;
    (void)dl;
}
