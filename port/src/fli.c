/* fli.c - see fli.h.  The format is Autodesk's FLI: a 128-byte header
 * (AF11h), then frames (F1FAh) of chunks: COLOR_64 (the palette, six bits
 * a component), LC (changed lines), BLACK, BRUN (the whole picture, run
 * length coded) and COPY (the whole picture as it is).  The last chunk
 * of a frame can declare a byte more than the file holds (tools/flifiles.py
 * found seven), so every chunk is read only as far as its frame goes. */
#include <stdlib.h>
#include <string.h>
#include "fli.h"
#include "platform.h"
#include "sys.h"

#define W 320
#define H 200
/* DDFLIPLY's clock: the PIT's channel 0 with divisor 0, 256 units a BIOS
 * tick */
#define CLOCK_HZ (1193182.0 / 65536.0 * 256.0)
/* the menu's wait_retrace: the refresh of mode 13h, which DDFLIPLY leaves
 * (25.175 MHz / (8 * 100 * 449), as vga_refresh_hz computes it) */
#define RETRACE_HZ 70.086

enum { CLOSED, KEY, TIME_UP };

static uint8_t screen[W * H];
static uint8_t dac[768];                /* six bits a component */
static uint32_t palette[256];

static unsigned u16(const uint8_t *p) { return (unsigned)(p[0] | p[1] << 8); }
static unsigned long u32(const uint8_t *p) { return u16(p) | (unsigned long)u16(p + 2) << 16; }

/* the DAC's colours times f/32 (L10D9's fade step; 32 as they are) */
static void set_palette(int f)
{
    int i;
    for (i = 0; i < 256; i++) {
        uint32_t c[3];
        int k;
        for (k = 0; k < 3; k++) {
            unsigned v = (unsigned)dac[3 * i + k] * (unsigned)f / 32;
            c[k] = (v << 2) | (v >> 4);
        }
        palette[i] = c[0] << 16 | c[1] << 8 | c[2];
    }
}

static void color_64(const uint8_t *p, const uint8_t *e)
{
    unsigned packets, i, n, c = 0;

    if (e - p < 2)
        return;
    packets = u16(p);
    p += 2;
    while (packets-- && e - p >= 2) {
        c += p[0];
        n = p[1] ? p[1] : 256;
        p += 2;
        for (i = 0; i < n && e - p >= 3; i++, c++, p += 3)
            if (c < 256) {
                dac[3 * c] = p[0] & 63;
                dac[3 * c + 1] = p[1] & 63;
                dac[3 * c + 2] = p[2] & 63;
            }
    }
    set_palette(32);
}

/* n bytes to (x, y), as far as the line and the data go */
static void copy(int x, int y, const uint8_t *p, const uint8_t *e, int n)
{
    if (n > W - x)
        n = W - x;
    if (n > e - p)
        n = (int)(e - p);
    if (n > 0 && x >= 0)
        memcpy(screen + y * W + x, p, (size_t)n);
}

static void fill(int x, int y, uint8_t v, int n)
{
    if (n > W - x)
        n = W - x;
    if (n > 0 && x >= 0)
        memset(screen + y * W + x, v, (size_t)n);
}

/* LC: the first line, the number of lines; per line packets of a skip
 * and a count (positive: bytes, negative: one byte repeated) */
static void lc(const uint8_t *p, const uint8_t *e)
{
    unsigned y, lines, packets;

    if (e - p < 4)
        return;
    y = u16(p);
    lines = u16(p + 2);
    p += 4;
    for (; lines-- && y < H; y++) {
        int x = 0;
        if (p >= e)
            return;
        for (packets = *p++; packets--; ) {
            int n;
            if (e - p < 2)
                return;
            x += p[0];
            n = (int8_t)p[1];
            p += 2;
            if (n >= 0) {
                copy(x, (int)y, p, e, n);
                p += n;
            } else {
                if (p >= e)
                    return;
                fill(x, (int)y, *p++, -n);
                n = -n;
            }
            x += n;
        }
    }
}

/* BRUN: each line a packet count (not needed), then counts (positive: one
 * byte repeated, negative: bytes) until the line is full */
static void brun(const uint8_t *p, const uint8_t *e)
{
    int y, x, n;

    for (y = 0; y < H; y++) {
        if (p >= e)
            return;
        p++;
        for (x = 0; x < W; x += n) {
            if (p >= e)
                return;
            n = (int8_t)*p++;
            if (n > 0) {
                if (p >= e)
                    return;
                fill(x, y, *p++, n);
            } else {
                n = -n;
                if (n == 0)
                    return;
                copy(x, y, p, e, n);
                p += n;
            }
        }
    }
}

/* the frame at d[pos] decoded into screen and dac; the next frame's
 * position, 0 when there is none */
static size_t frame(const uint8_t *d, size_t size, size_t pos)
{
    size_t fsize, end, c;
    unsigned chunks;

    if (size < pos + 16)
        return 0;
    fsize = u32(d + pos);
    if (fsize < 16)
        return 0;
    end = pos + fsize < size ? pos + fsize : size;
    if (u16(d + pos + 4) == 0xF1FA)
        for (chunks = u16(d + pos + 6), c = pos + 16; chunks-- && c + 6 <= end; ) {
            size_t csize = u32(d + c), cend = c + csize < end ? c + csize : end;
            const uint8_t *p = d + c + 6, *e = d + cend;
            switch (u16(d + c + 4)) {
            case 11: color_64(p, e); break;
            case 12: lc(p, e); break;
            case 13: memset(screen, 0, sizeof screen); break;
            case 15: brun(p, e); break;
            case 16: memcpy(screen, p, (size_t)(e - p) < sizeof screen ? (size_t)(e - p) : sizeof screen); break;
            }
            if (csize < 6)
                break;
            c += csize;
        }
    return pos + fsize;
}

/* a key as DOS's kbhit sees one: a make code, not of a shift, Ctrl, Alt
 * or a lock key, which put nothing into the BIOS's buffer */
static int is_key(int b)
{
    static int e0;
    int code = b & 0x7F;

    if (b == 0xE0) {
        e0 = 1;
        return 0;
    }
    if (b & 0x80) {
        e0 = 0;
        return 0;
    }
    if (code == 0x1D || code == 0x38 || code == 0x2A || code == 0x36 ||
        (!e0 && (code == 0x3A || code == 0x45 || code == 0x46))) {
        e0 = 0;
        return 0;
    }
    e0 = 0;
    return 1;
}

/* the picture shown until `due` (plat_micros); with `keys`, a key ends it */
static int show_until(uint64_t due, int keys)
{
    for (;;) {
        uint64_t now;
        int b;

        if (!plat_pump())
            return CLOSED;
        while (plat_read_control() >= 0)
            ;
        while ((b = plat_read_scancode()) >= 0)
            if (keys && is_key(b))
                return KEY;
        plat_present(screen, W, H, palette);
        now = plat_micros();
        if (now >= due)
            return TIME_UP;
        plat_sleep_ms(due - now > 10000 ? 10 : (int)((due - now) / 1000));
    }
}

static void drain_keys(void)
{
    while (plat_read_scancode() >= 0)
        ;
    while (plat_read_control() >= 0)
        ;
}

/* one file: CLOSED, KEY, or TIME_UP when it has played to its end */
static int play(const char *game, const char *name)
{
    char dir[SYS_PATH], path[SYS_PATH];
    uint8_t *d;
    size_t size, pos = 128;
    unsigned frames, speed, i;
    uint64_t start, period;
    int r = TIME_UP;

    if (!sys_find(game, "DELUXE", dir, sizeof dir) || !sys_find(dir, name, path, sizeof path) ||
        (d = sys_load(path, &size)) == NULL)
        return TIME_UP;
    if (size < 128 || u16(d + 4) != 0xAF11 || u16(d + 8) != W || u16(d + 10) != H) {
        free(d);
        return TIME_UP;
    }
    frames = u16(d + 6);
    speed = u16(d + 16);
    period = (uint64_t)(speed * 0x41 * 1e6 / CLOCK_HZ);
    start = plat_micros();
    for (i = 0; i < frames && pos; i++) {
        pos = frame(d, size, pos);
        r = show_until(start + (i + 1) * period, 1);
        if (r != TIME_UP)
            break;
    }
    free(d);
    return r;
}

int fli_play(const char *game, const char *const *names, int count)
{
    int i, r = TIME_UP;

    memset(screen, 0, sizeof screen);
    memset(dac, 0, sizeof dac);
    set_palette(32);
    for (i = 0; i < count && r == TIME_UP; i++)
        r = play(game, names[i]);
    drain_keys();                       /* DDFLIPLY empties the buffer at its end */
    return r != CLOSED;
}

int fli_hold_fade(int hold)
{
    uint64_t at = plat_micros();
    int f;

    /* the menu's keyboard handler has the keys meanwhile: none reaches
     * what comes next */
    if (show_until(at += (uint64_t)(hold * 1e6 / RETRACE_HZ), 0) == CLOSED)
        return 0;
    for (f = 31; f >= 0; f--) {
        set_palette(f);
        if (show_until(at += (uint64_t)(1e6 / RETRACE_HZ), 0) == CLOSED)
            return 0;
    }
    drain_keys();
    return 1;
}

int fli_before_table(const char *game, int t)
{
    static const char *const names[8] = {
        "IGNITION.FLI", "STEELWHL.FLI", "BEATBOX.FLI", "NIGHTMRE.FLI",
        "NEPTUNE.FLI", "SAFARI.FLI", "REVENGE.FLI", "STALLTRN.FLI",
    };

    return fli_play(game, &names[t & 7], 1) && fli_hold_fade(0x46);
}

int fli_intro(const char *game)
{
    static const char *const names[4] = {
        "SPIN21ST.FLI", "INTRO_P1.FLI", "INTRO_P2.FLI", "INTRO_P3.FLI",
    };

    return fli_play(game, names, 4) && fli_hold_fade(0);
}
