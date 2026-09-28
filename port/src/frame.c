/* frame.c - see frame.h */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "frame.h"
#include "hud.h"
#include "platform.h"
#include "vga.h"

static FrameCallback tick_routine;
static KeyHandler key_handler;
static unsigned long frames;
static uint64_t next_due;
static int closed;
static int tick_due;                    /* the keys came, the tick not yet */
static int handed;                      /* bytes handed to the program this picture */
static VgaFrame picture;
static FILE *record;

void frame_set_tick(FrameCallback tick)
{
    tick_routine = tick;
}

void frame_set_keyboard(KeyHandler handler)
{
    key_handler = handler;
}

static void hand_over_byte(int b)
{
    if (record) {
        fprintf(record, "%lu:%02X\n", frames, (unsigned)b);
        fflush(record);
    }
    if (key_handler)
        key_handler((unsigned char)b);
    handed++;
}

void frame_record(const char *path)
{
    record = fopen(path, "w");
}

/* sleeps until the next picture is due at the refresh rate of the mode */
static void pace(void)
{
    double hz = vga_refresh_hz();
    uint64_t period = (uint64_t)(1000000.0 / (hz > 10.0 ? hz : 70.0));
    uint64_t now = plat_micros();

    if (next_due == 0 || now > next_due + 10 * period)
        next_due = now;                 /* the first picture, or far behind */
    while (now < next_due) {
        plat_sleep_ms(next_due - now > 2000 ? 1 : 0);  /* 0: yield, then look again */
        now = plat_micros();
    }
    next_due += period;
}

/* a byte for the program: not the keypad's + - * /, which are the sound
 * keys (hud.c; they come again as characters) */
static void hand_over(int b)
{
    static int e0;
    int code = b & 0x7F;

    if (b == 0xE0) {
        e0 = 1;                         /* held back until the next byte */
        return;
    }
    if (e0 ? code == 0x35 : code == 0x37 || code == 0x4A || code == 0x4E) {
        e0 = 0;
        return;
    }
    if (e0)
        hand_over_byte(0xE0);
    e0 = 0;
    hand_over_byte(b);
}

/* the tick, the picture shown */
static void finish(void)
{
    tick_due = 0;
    if (tick_routine)
        tick_routine();
    vga_frame_start();
    vga_render(&picture);
    hud_draw(&picture);
    plat_present(picture.pixels, picture.width, picture.height, picture.palette);
    frames++;
}

/* the next picture's time, its keys handed over; 0 once the window was
 * closed */
static int next_picture(void)
{
    int b;

    if (closed)
        return 0;
    pace();
    if (!plat_pump()) {
        closed = 1;
        return 0;
    }
    handed = 0;
    while ((b = plat_read_scancode()) >= 0)
        hand_over(b);
    while ((b = plat_read_control()) >= 0)
        hud_control(b);
    return 1;
}

int frame_wait(void)
{
    if (tick_due) {
        finish();
        return 1;
    }
    if (!next_picture())
        return 0;
    finish();
    return 1;
}

int frame_wait_keys(void)
{
    if (tick_due) {
        finish();
        return 1;
    }
    if (!next_picture())
        return 0;
    if (handed)
        tick_due = 1;
    else
        finish();
    return 1;
}

unsigned long frame_count(void)
{
    return frames;
}
