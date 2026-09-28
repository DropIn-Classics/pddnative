/* plat_null.c - platform.h without a window or sound: for tests and
 * scripted runs.  The clock is virtual (plat_sleep_ms moves it on), so a
 * run takes no real time and repeats exactly.  Environment:
 *
 *   PD_FRAMES=n     plat_pump() reports the window closed after n pictures
 *   PD_DUMP=file    the last picture is written there as a PPM file
 *   PD_KEYS=...     scan codes by picture number: "120:3B 125:BB" presses
 *                   and releases F1 at pictures 120 and 125; E0 keys as
 *                   "E0-50"
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"

static long frames_left = -1, picture;
static const char *dump_path, *keys;
static uint64_t now_us;
static uint8_t pending[64];
static int npending, pos_pending;

static uint8_t last[640 * 480];
static uint32_t last_pal[256];
static int last_w, last_h;

int plat_init(const char *title)
{
    const char *n = getenv("PD_FRAMES");
    (void)title;
    if (n)
        frames_left = atol(n);
    dump_path = getenv("PD_DUMP");
    keys = getenv("PD_KEYS");
    return 1;
}

static void dump(void)
{
    FILE *f;
    int i;
    if (!dump_path || !last_w)
        return;
    f = fopen(dump_path, "wb");
    if (!f)
        return;
    fprintf(f, "P6\n%d %d\n255\n", last_w, last_h);
    for (i = 0; i < last_w * last_h; i++) {
        uint32_t c = last_pal[last[i]];
        fputc((int)(c >> 16) & 0xFF, f);
        fputc((int)(c >> 8) & 0xFF, f);
        fputc((int)c & 0xFF, f);
    }
    fclose(f);
}

void plat_shutdown(void)
{
    dump();
}

void plat_message(const char *text)
{
    fprintf(stderr, "%s\n", text);
}

/* the scan codes PD_KEYS gives for picture `picture` */
static void keys_for_picture(void)
{
    const char *p = keys;
    npending = pos_pending = 0;
    while (p && *p) {
        char *end;
        long at = strtol(p, &end, 10);
        if (end == p || *end != ':')
            break;
        p = end + 1;
        if (at == picture && npending < (int)sizeof pending - 1) {
            if (!strncmp(p, "E0-", 3)) {
                pending[npending++] = 0xE0;
                p += 3;
            }
            pending[npending++] = (uint8_t)strtol(p, NULL, 16);
        }
        while (*p && *p != ' ')
            p++;
        while (*p == ' ')
            p++;
    }
}

int plat_pump(void)
{
    if (frames_left == 0)
        return 0;
    return 1;
}

int plat_has_window(void)
{
    return 0;
}

void plat_set_fullscreen(int on)
{
    (void)on;
}

int plat_fullscreen(void)
{
    return 0;
}

void plat_present(const uint8_t *pixels, int width, int height, const uint32_t palette[256])
{
    if (width * height <= (int)sizeof last) {
        memcpy(last, pixels, (size_t)(width * height));
        memcpy(last_pal, palette, sizeof last_pal);
        last_w = width;
        last_h = height;
    }
    picture++;
    if (frames_left > 0)
        frames_left--;
    keys_for_picture();
}

int plat_read_scancode(void)
{
    return pos_pending < npending ? pending[pos_pending++] : -1;
}

int plat_read_control(void)
{
    return -1;
}

uint64_t plat_micros(void)
{
    return now_us;
}

void plat_sleep_ms(int ms)
{
    now_us += (uint64_t)(ms > 0 ? ms : 1) * 1000;      /* 0 (a yield) too */
}

int plat_audio_start(int rate, PlatAudioFill fill, void *user)
{
    (void)rate;
    (void)fill;
    (void)user;
    return 0;                   /* no device: sound.c moves the module */
}

void plat_audio_lock(void)
{
}

void plat_audio_unlock(void)
{
}
