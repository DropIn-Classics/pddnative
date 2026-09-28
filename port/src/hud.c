/* hud.c - see hud.h */
#include <math.h>
#include <string.h>
#include "hud.h"
#include "platform.h"
#include "sound.h"

#define SHOW_PICTURES 140       /* two seconds at the game's rate */

static int volume = HUD_VOLUME_MAX, muted, shaping = 1;
static int shown;               /* pictures left to show the box */
static int what;                /* the control the box shows */

/* 3 dB a step, 0 silent */
static void apply(void)
{
    float gain = 0.0f;

    if (!muted && volume > 0)
        gain = powf(10.0f, (float)(volume - HUD_VOLUME_MAX) * 3.0f / 20.0f);
    snd_set_gain(gain);
    snd_set_fx_bypass(!shaping);
}

void hud_control(int control)
{
    switch (control) {
    case PLAT_VOLUME_UP:
        if (volume < HUD_VOLUME_MAX)
            volume++;
        muted = 0;
        break;
    case PLAT_VOLUME_DOWN:
        if (volume > 0)
            volume--;
        muted = 0;
        break;
    case PLAT_MUTE:
        muted = !muted;
        break;
    case PLAT_EQ:
        shaping = !shaping;
        break;
    default:
        return;
    }
    apply();
    what = control == PLAT_VOLUME_DOWN ? PLAT_VOLUME_UP : control;
    shown = SHOW_PICTURES;
}

/* ---- drawing: a 5x7 font of the letters the box needs */

static const struct {
    char c;
    unsigned char rows[7];      /* bit 4 the leftmost pixel */
} font[] = {
    { 'E', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F } },
    { 'F', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 } },
    { 'L', { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F } },
    { 'M', { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 } },
    { 'N', { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 } },
    { 'O', { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
    { 'Q', { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D } },
    { 'T', { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 } },
    { 'U', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
    { 'V', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 } },
};

static VgaFrame *pic;
static int scale_y;
static uint8_t ink, paper;

/* the palette entry nearest to the colour */
static uint8_t nearest(uint32_t rgb)
{
    long best = -1;
    int i, found = 0;

    for (i = 0; i < 256; i++) {
        long dr = (long)(pic->palette[i] >> 16 & 0xFF) - (long)(rgb >> 16 & 0xFF);
        long dg = (long)(pic->palette[i] >> 8 & 0xFF) - (long)(rgb >> 8 & 0xFF);
        long db = (long)(pic->palette[i] & 0xFF) - (long)(rgb & 0xFF);
        long d = dr * dr + dg * dg + db * db;
        if (best < 0 || d < best) {
            best = d;
            found = i;
        }
    }
    return (uint8_t)found;
}

/* a rectangle in picture pixels (y in the font's rows, scaled) */
static void fill(int x, int y, int w, int h, uint8_t colour)
{
    int i, j;

    for (j = y * scale_y; j < (y + h) * scale_y; j++)
        for (i = x; i < x + w; i++)
            if (i >= 0 && i < pic->width && j >= 0 && j < pic->height)
                pic->pixels[j * pic->width + i] = colour;
}

static void text(int x, int y, const char *s)
{
    int k, r, b;

    for (; *s; s++, x += 6) {
        for (k = 0; k < (int)(sizeof font / sizeof font[0]); k++)
            if (font[k].c == *s)
                break;
        if (k == (int)(sizeof font / sizeof font[0]))
            continue;                   /* a space */
        for (r = 0; r < 7; r++)
            for (b = 0; b < 5; b++)
                if (font[k].rows[r] & (0x10 >> b))
                    fill(x + b, y + r, 1, 1, ink);
    }
}

void hud_draw(VgaFrame *picture)
{
    const char *label = "VOLUME";
    int bar = what == PLAT_VOLUME_UP && !muted, w, x, i;

    if (shown <= 0)
        return;
    shown--;
    pic = picture;
    scale_y = picture->height >= 400 ? 2 : 1;   /* the 200 lines scanned twice */
    ink = nearest(0xFFFFFF);
    paper = nearest(0x000000);
    if (what == PLAT_EQ)
        label = shaping ? "EQ ON" : "EQ OFF";
    else if (muted)
        label = "MUTE";
    w = 6 * (int)strlen(label) - 1 + (bar ? 5 + 4 * HUD_VOLUME_MAX - 1 : 0);
    x = (picture->width - w) / 2;
    fill(x - 4, 4, w + 8, 11, paper);
    text(x, 6, label);
    for (i = 0; bar && i < HUD_VOLUME_MAX; i++) {
        int bx = x + 6 * (int)strlen(label) + 4 + 4 * i;
        if (i < volume)
            fill(bx, 6, 3, 7, ink);
        else
            fill(bx, 12, 3, 1, ink);
    }
}
