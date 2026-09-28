/* pd_sound.c - the table programs' side of the sound driver (PD.ASM from
 * load_sound_driver to sound_request_run): the module, the tick callback,
 * the requests the game makes (tune_request, jingle_request,
 * sound_request).  The routines from play_tune on run with DS = XDATA,
 * where the per-table lists are.  See pd.h, sound.h. */
#include <stdio.h>
#include "frame.h"
#include "pd.h"
#include "sound.h"
#include "vga.h"

static uint8_t xrb(uint16_t off) { return frb(seg_xdata, off); }
static uint16_t xrw(uint16_t off) { return frw(seg_xdata, off); }
static void xww(uint16_t off, uint16_t v) { fww(seg_xdata, off, v); }
static uint8_t table(void) { return rb(V(table_num)); }

/* drive C:, \DELUXE, SOUND.CFG names the driver, EXEC of it: the port's
 * driver is sound.c, always there; the program's variable as after the
 * EXEC */
int load_sound_driver(void)
{
    snd_init();
    wb(V(sound_on), 0);
    return 0;
}

/* the table's main tune (main_tunes) started */
static void play_tune(void)
{
    uint16_t pos = xrw((uint16_t)(V(main_tunes) + 2 * (table() + 2)));
    xww(V(music_pos), (uint16_t)(snd_position(pos) & 0xFF));
}

/* module_callback, the driver's at a pattern jump (INT 66h AL=13h; called
 * on the audio thread): AX = the jump's target in both halves, the module
 * goes on at AL.  Jingle 6 back to the main tune, jingle 5 asks for tune
 * 0, another jingle back to where the tune was (music_pos); the tune's own
 * jumps as they are. */
static int module_callback(int target)
{
    int ax = target << 8 | target;

    if (xrb(V(jingle_num)) == 6) {
        play_tune();
        ax = xrw(V(music_pos));
    } else if (xrb(V(jingle_num)) == 5) {
        wb(V(tune_request), 0);
    } else if (xrw(V(tune_playing)) == 0) {
        ax = xrw(V(music_pos));
    }
    return ax & 0xFF;
}

/* the table's module (LEVELn.MOD) loaded by the driver, the tick callback
 * set, the music playing */
int start_music(void)
{
    uint16_t name = xrw((uint16_t)(V(music_files) + 4 * (table() + 2)));
    char path[1024];

    if (dos_path(seg_xdata, name, path, sizeof path) || snd_load_module(path))
        return 1;
    if ((int8_t)table() >= 0) {
        snd_position(xrw((uint16_t)(V(main_tunes) + 2 * (table() + 2))));
        wait_frame();
    }
    wb(V(music_fade_dir), 0);
    frame_set_tick(timer_callback);                 /* AL=0Bh */
    snd_jump_callback(module_callback);             /* AL=13h */
    snd_play();
    wb(V(sound_on), 1);
    return 0;
}

/* the driver stopped (INT 66h AL=0), no frame callback */
void stop_sound(void)
{
    if (rb(V(sound_on)) == 0)
        return;
    snd_stop();
    frame_set_tick(NULL);
    wb(V(sound_on), 0);
    ww(V(frame_callback), V(no_callback));
}

/* music_fade_dir 1: the volume up in 10h steps, 2: down, then the main tune */
static void music_fade(void)
{
    if (rb(V(music_fade_dir)) == 1) {
        wb(V(fade_steps), (uint8_t)(rb(V(fade_steps)) - 1));
        if (rb(V(fade_steps)) != 0) {
            ww(V(fade_volume), (uint16_t)(rw(V(fade_volume)) + 0x10));
            if (!(rw(V(fade_volume)) & 0x100)) {
                snd_volume(rw(V(fade_volume)) & 0x1FF);
                return;
            }
        }
        wb(V(music_fade_dir), 0);
        wb(V(fade_steps), 0x10);
        ww(V(fade_volume), 0x100);
    }
    if (rb(V(music_fade_dir)) == 2) {
        wb(V(fade_steps), (uint8_t)(rb(V(fade_steps)) - 1));
        if (rb(V(fade_steps)) != 0) {
            ww(V(fade_volume), (uint16_t)(rw(V(fade_volume)) - 0x10));
            if (!(rw(V(fade_volume)) & 0x8000)) {
                ww(V(fade_volume), rw(V(fade_volume)) & 0x1FF);
                snd_volume(rw(V(fade_volume)));
                return;
            }
        }
        wb(V(music_fade_dir), 0);
        wb(V(fade_steps), 0x10);
        ww(V(fade_volume), 1);
        play_tune();
    }
}

/* tune_request: 80h fades out, else that position of the table's tunes */
static void tune_request_run(void)
{
    uint16_t bx;

    if (rb(V(tune_request)) == 0x80) {
        wb(V(music_fade_dir), 2);
        wb(V(tune_request), 0xFF);
        return;
    }
    bx = (uint16_t)(V(music_files) + 4 * (table() + 2));
    snd_position(xrb((uint16_t)(bx + rb(V(tune_request)) + 2)));
    wb(V(tune_request), 0xFF);
    xww(V(music_pos), 0xFFFF);
    xww(V(tune_playing), 1);
}

/* jingle_request: a position of the table's jingle list played (80h: back
 * to the main tune) */
static void jingle_request_run(void)
{
    uint8_t req = rb(V(jingle_request));
    uint16_t pos;

    if (req & 0x80) {
        play_tune();
        fww(seg_code, V(tune_saved), 0);
        wb(V(jingle_request), 0xFF);
        xww(V(tune_playing), 1);
        return;
    }
    xww(V(tune_playing), 0);
    pos = xrb((uint16_t)(xrw((uint16_t)(V(jingle_lists) + 2 * table())) + req));
    fwb(seg_xdata, V(jingle_num), req);
    if (req < 2) {
        snd_volume(0xFF);
        xww(V(music_pos), pos);
        snd_position(pos);
        fww(seg_code, V(tune_saved), 0);
    } else if (frw(seg_code, V(tune_saved)) == 0) {
        xww(V(music_pos), (uint16_t)(snd_position(pos) & 0xFF));
        fww(seg_code, V(tune_saved), 1);
    } else {
        snd_position(pos);
    }
    wb(V(jingle_request), 0xFF);
}

/* sound_request: the effect of the table's list played (INT 66h AL=11h) */
static void sound_request_run(void)
{
    uint16_t si;

    if (rb(V(sound_request)) & 0x80) {
        wb(V(sound_request), 0xFF);
        return;
    }
    si = (uint16_t)(xrw((uint16_t)(V(effect_lists) + 2 * table())) + 4 * rb(V(sound_request)));
    snd_effect(xrb(si), xrb((uint16_t)(si + 1)), xrb((uint16_t)(si + 2)), xrb((uint16_t)(si + 3)));
    wb(V(sound_request), 0xFF);
}

/* each tick of the driver: the sprites (sprites_in_irq), the frame
 * callback, the music and sound requests, timer_tick */
void timer_callback(void)
{
    if (frb(seg_code, V(in_timer)))
        return;
    fwb(seg_code, V(in_timer), 0xFF);
    if (rb(V(sprites_in_irq))) {
        uint8_t map_mask, read_map;
        wb(V(sprites_scroll), 0);
        vga_outb(0x3C4, 2);
        map_mask = vga_inb(0x3C5);
        vga_outb(0x3CE, 4);
        read_map = vga_inb(0x3CF);
        draw_sprites();
        vga_outw(0x3C4, (uint16_t)(2 | map_mask << 8));
        vga_outw(0x3CE, (uint16_t)(4 | read_map << 8));
    }
    call_code(rw(V(frame_callback)));
    if (rb(V(sound_on)) == 1) {
        /* AL=8: the port's driver has nothing to do here */
        if (rb(V(music_fade_dir)) != 0) {
            music_fade();
        } else if (rb(V(tune_request)) != 0xFF) {
            if (rb(V(opt_music)) != 1) {
                wb(V(tune_request), 0xFF);
                play_tune();
            } else {
                tune_request_run();
            }
        } else if (rb(V(jingle_request)) != 0xFF) {
            if (rb(V(opt_music)) != 1) {
                wb(V(jingle_request), 0xFF);
                play_tune();
            } else {
                jingle_request_run();
            }
        } else if (rb(V(sound_request)) != 0xFF) {
            sound_request_run();
        }
    }
    wb(V(timer_tick), 0xFF);
    fwb(seg_code, V(in_timer), 0);
}
