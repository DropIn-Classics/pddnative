/* menu.c - DDPCMAIN.ASM's menu, from `start` to `exit`, and the routines
 * it calls.  See menu.h.
 *
 * The picture is SELECT.VGA (320x790) in video memory from 3C0h on: the
 * list of the ten entries (the eight tables, the history, the options)
 * at the top, the high scores of both programs drawn into the part below
 * it.  The CRTC's start address (menu_start) scrolls it; the screen's
 * last 12 rows are a split screen showing video memory from 0, where the
 * ticker line is drawn.  The sound driver's tick moves the pointer and
 * the ticker (sound_callback). */
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "frame.h"
#include "gen/ddnames.h"
#include "mem.h"
#include "menu.h"
#include "sound.h"
#include "sys.h"
#include "vga.h"

/* the engine's routines the menu shares (pd.h; the menu's own routines
 * have the same names as others there, so pd.h is not included): the
 * same code in both programs, or the files as the programs look them up */
void set_mode_x(void);
void clear_vram(void);
void set_planes(uint8_t map_mask, uint8_t read_map);
void screen_off(void);
void screen_on(void);
int dos_path(uint16_t seg, uint16_t off, char *out, size_t n);
extern const char *pd_game_dir;

/* the names of DDPCMAIN.hints: M_name is its offset in its segment */
#define DDM_ENUM(seg, name, off) M_##name = off,
enum { DDM_NAMES(DDM_ENUM) M_NONE = 0xFFFF };
#undef DDM_ENUM

static uint16_t s_code, s_data, s_fonts, s_history;

static uint8_t db(uint16_t o) { return frb(s_data, o); }
static void dwb(uint16_t o, uint8_t v) { fwb(s_data, o, v); }
static uint16_t dw(uint16_t o) { return frw(s_data, o); }
static void dww(uint16_t o, uint16_t v) { fww(s_data, o, v); }
static int16_t dsw(uint16_t o) { return (int16_t)dw(o); }
static uint8_t cb(uint16_t o) { return frb(s_code, o); }
static void cwb(uint16_t o, uint8_t v) { fwb(s_code, o, v); }
static uint16_t cw(uint16_t o) { return frw(s_code, o); }
static void cww(uint16_t o, uint16_t v) { fww(s_code, o, v); }

static const MenuHooks *hooks;
static jmp_buf exit_jmp;
static char *exit_err;
static size_t exit_err_n;

static void leave(void) { longjmp(exit_jmp, 1); }

static void fatal(const char *what)
{
    snprintf(exit_err, exit_err_n, "%s", what);
    longjmp(exit_jmp, 2);
}

static void checkpoint_menu(const char *where)
{
    static const char *stop;
    static unsigned long count, want;
    static size_t len;
    static int parsed;

    if (!parsed) {
        const char *hash;
        parsed = 1;
        stop = getenv("PD_STOP");
        if (stop) {
            hash = strchr(stop, '#');
            len = hash ? (size_t)(hash - stop) : strlen(stop);
            want = hash ? strtoul(hash + 1, NULL, 10) : 1;
        }
    }
    if (getenv("PD_TRACE"))
        fprintf(stderr, "%s picture %lu\n", where, frame_count());
    if (stop && strlen(where) == len && !strncmp(where, stop, len) && ++count == want)
        leave();
}

/* ---- the screen */

static void wait_retrace(void);

static void clear_palette(int count)
{
    wait_retrace();
    vga_outb(0x3C8, 0);
    while (count--)
        vga_outb(0x3C9, 0);
}

static void set_split(void)
{
    uint16_t line = 0x178;

    wait_retrace();
    vga_outw(0x3D4, (uint16_t)(0x18 | (line & 0xFF) << 8));
    vga_outb(0x3D4, 7);
    vga_outw(0x3D4, (uint16_t)(7 | ((vga_inb(0x3D5) & 0xEF) | (line >> 8 & 1) << 4) << 8));
    vga_outb(0x3D4, 9);
    vga_outw(0x3D4, (uint16_t)(9 | ((vga_inb(0x3D5) & 0xBF) | (line >> 9 & 1) << 6) << 8));
}

/* the DAC from 0: the `count` bytes at seg:src times fade_level / 32, in
 * palette_work first; 32 steps, a frame each, up (fade_in, the last 31/32)
 * or down to 0 (fade_out) */
static void fade(uint16_t seg, uint16_t src, int count, int up)
{
    int step, i;

    dwb(M_fade_level, up ? 0 : 0x1F);
    for (step = 0; step < 0x20; step++) {
        for (i = 0; i < count; i++)
            dwb((uint16_t)(M_palette_work + i),
                (uint8_t)(frb(seg, (uint16_t)(src + i)) * db(M_fade_level) / 0x20));
        vga_outb(0x3C8, 0);
        wait_retrace();
        for (i = 0; i < count; i++)
            vga_outb(0x3C9, db((uint16_t)(M_palette_work + i)));
        dwb(M_fade_level, (uint8_t)(db(M_fade_level) + (up ? 1 : -1)));
    }
}

static void fade_in(uint16_t seg, uint16_t src, int count) { fade(seg, src, count, 1); }
static void fade_out(uint16_t seg, uint16_t src, int count) { fade(seg, src, count, 0); }

/* the menu's start address, then a frame (CX = menu_start after it) */
static void show_menu_start(void)
{
    uint16_t start = dw(M_menu_start);
    vga_outw(0x3D4, (uint16_t)(0x0C | (start & 0xFF00)));
    vga_outw(0x3D4, (uint16_t)(0x0D | start << 8));
    wait_retrace();
}

/* `count` groups of four bytes at seg:0 to the four planes at `di` */
static void copy_vga_image_planes(uint16_t seg, uint16_t di, uint16_t count)
{
    uint16_t si = 0, i;
    uint8_t mask;

    for (mask = 1; mask < 0x10; mask = (uint8_t)(mask + mask), si++) {
        set_planes(mask, 0);
        for (i = 0; i < count; i++)
            vga_write((uint16_t)(di + i), frb(seg, (uint16_t)(si + 4 * i)));
    }
}

/* the file named at DATA:name, 320 pixels a row, into video memory from
 * `di`: read in chunks of 0FA00h bytes into a DOS block, 3E80h groups of
 * four copied after each (a short last chunk too: the rest of the group
 * from the chunk before), until a read is short */
static void load_vga_image(uint16_t name, uint16_t di)
{
    char path[SYS_PATH];
    uint8_t *data;
    size_t size = 0, pos = 0, k, i;
    uint16_t block = dos_alloc(0x0FA1);

    if (!block)
        fatal("Not enough memory for the menu's picture.");
    cww(M_image_block, block);
    if (dos_path(s_data, name, path, sizeof path) || (data = sys_load(path, &size)) == NULL)
        fatal("The menu's picture (SELECT.VGA) could not be read.");
    do {
        k = size - pos < 0xFA00 ? size - pos : 0xFA00;
        for (i = 0; i < k; i++)
            fwb(block, (uint16_t)i, data[pos + i]);
        pos += k;
        copy_vga_image_planes(block, di, 0x3E80);
        di = (uint16_t)(di + 0x3E80);
    } while (k == 0xFA00);
    free(data);
    dos_free(block);
}

/* ---- the keyboard (INT 9) */

static void keyboard_handler(unsigned char b)
{
    uint8_t code, bit;
    uint16_t at;

    if (b == 0xE0) {
        cwb(M_kbd_e0, 0xE0);
        return;
    }
    code = b & 0x7F;
    if (cb(M_kbd_e0)) {
        cwb(M_kbd_e0, 0);
        if (code == 0x2A || code == 0x36)
            return;
        code |= 0x80;
    }
    dwb(M_last_scancode, code);
    bit = (uint8_t)(1 << (code & 7));
    at = (uint16_t)(M_key_map + (code >> 3));
    if (b & 0x80)
        dwb(at, (uint8_t)(db(at) & ~bit));
    else
        dwb(at, (uint8_t)(db(at) | bit));
}

static void install_keyboard(void) { frame_set_keyboard(keyboard_handler); }
static void restore_keyboard(void) { frame_set_keyboard(NULL); }

static int key_down(int code)
{
    return (db((uint16_t)(M_key_map + (code >> 3))) >> (code & 7)) & 1;
}

static int any_key_down(void)
{
    int i;
    for (i = 0; i < 0x20; i++)
        if (db((uint16_t)(M_key_map + i)))
            return 1;
    return 0;
}

/* until no key is down; the driver's tick goes on meanwhile */
static void wait_keys_up(void)
{
    while (any_key_down())
        if (!frame_wait_keys())
            leave();
}

/* ---- the sound driver */

static void pointer_tick(uint16_t routine);
static void ticker_step(void);

/* the driver's tick: the pointer (when pointer_on) and the ticker moved,
 * the music mixed (AL=8), frame_tick set */
static void sound_callback(void)
{
    if (cb(M_in_callback))
        return;
    cwb(M_in_callback, 0xFF);
    if (cw(M_pointer_on)) {
        pointer_tick(dw(M_tick_remove));
        pointer_tick(dw(M_tick_draw));
    }
    pointer_tick(dw(M_tick_ticker));
    snd_tick(vga_refresh_hz(), 0);
    dwb(M_frame_tick, 0xFF);
    cwb(M_in_callback, 0);
}

/* the module intro_module_table[index] loaded by the driver, the tick
 * callback set, the music playing (from DELUXE, where SOUND.CFG names the
 * driver: the port's driver is sound.c, always there) */
static void load_sound(int index)
{
    char path[SYS_PATH];
    uint16_t name = frw(s_history, (uint16_t)(M_intro_module_table + 2 * index));

    cww(M_module_index, (uint16_t)index);
    snd_init();
    if (dos_path(s_history, name, path, sizeof path) || snd_load_module(path))
        fatal("The menu's music could not be loaded.");
    frame_set_tick(sound_callback);                 /* AL=0Bh */
    snd_jump_callback(NULL);                        /* a driver loaded anew has none */
    snd_play();                                     /* AL=4 */
    /* The driver's timer runs from its loading on: in tools/run its next
     * tick calls the routine before the fade's first wait (before or after
     * AL=4, at each load in the runs looked at; on a PC it depends on the
     * timer's phase and the CPU, not checked there) */
    sound_callback();
    dwb(M_sound_loaded, 1);
}

static void stop_sound(void)
{
    if (db(M_sound_loaded) == 0)
        return;
    snd_stop();
    frame_set_tick(NULL);
    dwb(M_sound_loaded, 0);
}

/* until the driver's tick sets frame_tick, or (no driver) the next
 * vertical retrace: either is one picture here */
static void wait_retrace(void)
{
    if (db(M_sound_loaded))
        dwb(M_frame_tick, 0);
    if (!frame_wait())
        leave();
}

/* ---- the pointer: records of 13h bytes at pointer_table's offsets */

enum { P_X = 0, P_Y = 2, P_W = 4, P_H = 6, P_IMG_SEG = 8, P_IMG_OFF = 0x0A,
       P_SAVE_SEG = 0x0C, P_SAVE_OFF = 0x0E, P_SAVED_START = 0x10, P_KEY = 0x12 };

static uint16_t pointer_rec(int index)
{
    return dw((uint16_t)(M_pointer_table + 2 * index));
}

/* the plane of pixel x + p (p = 0..3 in turn), and the address one on
 * once the planes wrap past 3 (sprite_plane_wrap) */
static uint16_t plane_step(int p, uint16_t *ax, uint16_t addr)
{
    if (p != 0 && (*ax & 3) == 0 && cw(M_sprite_plane_wrap) == 0) {
        addr++;
        cww(M_sprite_plane_wrap, 1);
    }
    *ax &= 3;
    return addr;
}

/* the picture under the pointer (at `si` + menu_start) into its buffer */
static void save_under_pointer(uint16_t rec, uint16_t si)
{
    uint16_t seg = dw((uint16_t)(rec + P_SAVE_SEG)), di = dw((uint16_t)(rec + P_SAVE_OFF));
    uint16_t ax = cw(M_sprite_x), start;
    int p, row, col;

    si = (uint16_t)(si + dw(M_menu_start));
    dww((uint16_t)(rec + P_SAVED_START), dw(M_menu_start));
    for (p = 0; p < 4; p++, ax++) {
        start = plane_step(p, &ax, si);
        si = start;
        set_planes(0, (uint8_t)ax);
        for (row = 0; row < cb(M_sprite_h); row++, start = (uint16_t)(start + 0x4C))
            for (col = 0; col < cw(M_sprite_w) >> 2; col++, start++, di++)
                fwb(seg, di, vga_read(start));
    }
}

static void draw_pointer_sprite(int index)
{
    uint16_t rec = pointer_rec(index);
    uint16_t y = dw((uint16_t)(rec + P_Y)), off, di, seg, si, ax;
    int p, row, col;

    cww(M_sprite_plane_wrap, 0);
    off = (uint16_t)(y * 0x50);
    cww(M_sprite_x, dw((uint16_t)(rec + P_X)));
    off = (uint16_t)(off + (cw(M_sprite_x) >> 2));
    cww(M_sprite_w, dw((uint16_t)(rec + P_W)));
    cwb(M_sprite_h, db((uint16_t)(rec + P_H)));
    cwb(M_sprite_key, db((uint16_t)(rec + P_KEY)));
    save_under_pointer(rec, off);
    cww(M_sprite_plane_wrap, 0);
    di = (uint16_t)(off + dw(M_menu_start));
    si = dw((uint16_t)(rec + P_IMG_OFF));
    seg = dw((uint16_t)(rec + P_IMG_SEG));
    ax = cw(M_sprite_x);
    for (p = 0; p < 4; p++, ax++) {
        uint16_t a;
        di = plane_step(p, &ax, di);
        set_planes((uint8_t)(1 << ax), 0);
        a = di;
        for (row = 0; row < cb(M_sprite_h); row++, a = (uint16_t)(a + 0x4C))
            for (col = 0; col < cw(M_sprite_w) >> 2; col++, si++, a++)
                if (frb(seg, si) != cb(M_sprite_key))
                    vga_write(a, frb(seg, si));
    }
}

static void restore_under_pointer(int index)
{
    uint16_t rec = pointer_rec(index);
    uint16_t di, si, seg, ax;
    int p, row, col;

    cwb(M_sprite_h, db((uint16_t)(rec + P_H)));
    cww(M_sprite_w, dw((uint16_t)(rec + P_W)));
    di = (uint16_t)(dw((uint16_t)(rec + P_Y)) * 0x50 + dw((uint16_t)(rec + P_SAVED_START)));
    cww(M_sprite_x, dw((uint16_t)(rec + P_X)));
    di = (uint16_t)(di + (cw(M_sprite_x) >> 2));
    cww(M_sprite_plane_wrap, 0);
    si = dw((uint16_t)(rec + P_SAVE_OFF));
    seg = dw((uint16_t)(rec + P_SAVE_SEG));
    ax = cw(M_sprite_x);
    for (p = 0; p < 4; p++, ax++) {
        uint16_t a;
        di = plane_step(p, &ax, di);
        set_planes((uint8_t)(1 << ax), 0);
        a = di;
        for (row = 0; row < cb(M_sprite_h); row++, a = (uint16_t)(a + 0x4C))
            for (col = 0; col < cw(M_sprite_w) >> 2; col++, si++, a++)
                vga_write(a, frb(seg, si));
    }
}

/* pointer record `index` to the pointer (mouse_x at most 130h), drawn */
static void draw_pointer(int index)
{
    uint16_t rec = pointer_rec(index);

    if (dsw(M_mouse_x) > 0x130)
        dww(M_mouse_x, 0x130);
    dww((uint16_t)(rec + P_X), dw(M_mouse_x));
    dww((uint16_t)(rec + P_Y), dw(M_mouse_y));
    draw_pointer_sprite(index);
}

static void remove_pointer(int index)
{
    restore_under_pointer(index);
}

/* a DOS block for the picture under each pointer */
static void alloc_pointer_buffers(void)
{
    uint16_t at, rec, seg;

    for (at = M_pointer_table; (rec = dw(at)) != 0; at = (uint16_t)(at + 2)) {
        seg = dos_alloc((uint16_t)((uint8_t)dw((uint16_t)(rec + P_W)) * db((uint16_t)(rec + P_H)) >> 4));
        if (!seg)
            fatal("Not enough memory for the menu.");
        dww((uint16_t)(rec + P_SAVE_SEG), seg);
        dww((uint16_t)(rec + P_SAVE_OFF), 0);
    }
}

/* INT 33h AX=0: no mouse driver answers here */
static void detect_mouse(void)
{
    dwb(M_mouse_present, 0xFE);
}

/* INT 33h AX=3 when a mouse was found: never here */
static void poll_mouse(void)
{
}

/* ---- the ticker */

/* BX = the index of DL in char_set; *dl = 0 when it is not there */
static uint16_t char_index(uint8_t *dl)
{
    uint16_t i;

    for (i = 0; i < 0x27; i++)
        if (db((uint16_t)(M_char_set + i)) == *dl)
            return i;
    *dl = 0;
    return i;
}

static void ticker_next_char(void)
{
    uint16_t bx = (uint16_t)(dw(M_ticker_ptr) + 1), si, di;
    uint8_t al = db(bx), dl;
    int row, i;

    if (al == 0) {
        bx = dw(M_ticker_text);
        al = db(bx);
    }
    if (al == '@') {
        dwb(M_ticker_state, 1);
        return;
    }
    dww(M_ticker_ptr, bx);
    dl = al;
    bx = char_index(&dl);
    if (dl != al)
        return;
    si = (uint16_t)(M_ticker_font + (bx << 3));
    di = (uint16_t)(M_ticker_buffer + dw(M_ticker_x) + 0x140);
    for (row = 0; row < 10; row++, si = (uint16_t)(si + 0x140), di = (uint16_t)(di + 0x280))
        for (i = 0; i < 8; i++)
            dwb((uint16_t)(di + i), frb(s_fonts, (uint16_t)(si + i)));
}

static void ticker_step(void)
{
    uint16_t x, si, di, bp;
    int row, p, i;

    if (db(M_ticker_state) != 1) {
        x = dw(M_ticker_x);
        if ((x & 7) != 0 || x != 0x140) {
            if ((x & 7) == 0)
                ticker_next_char();
            si = (uint16_t)(M_ticker_buffer + dw(M_ticker_x));
            di = (uint16_t)(si - 1);
            si = (uint16_t)(si + 0x140);
            for (row = 0; row < 10; row++, si = (uint16_t)(si + 0x280), di = (uint16_t)(di + 0x280))
                dwb(di, db(si));
        }
    }
    bp = (uint16_t)(M_ticker_buffer + dw(M_ticker_x));
    for (p = 0; p < 4; p++, bp++) {
        uint16_t line = bp;
        di = 0x50;
        set_planes((uint8_t)(1 << p), 0);
        for (row = 0; row < 10; row++, line = (uint16_t)(line + 0x280))
            for (i = 0; i < 0x50; i++)
                vga_write(di++, db((uint16_t)(line + 4 * i)));
    }
    dww(M_ticker_x, (uint16_t)(dw(M_ticker_x) + 1));
    if (dw(M_ticker_x) > 0x140)
        dww(M_ticker_x, 0);
}

/* the routines the tick calls through tick_remove, tick_draw, tick_ticker */
static void pointer_tick(uint16_t routine)
{
    if (routine == M_tick_remove_pointer)
        remove_pointer(1);
    else if (routine == M_tick_draw_pointer)
        draw_pointer(1);
    else if (routine == M_ticker_step)
        ticker_step();
    else if (routine != M_tick_nothing)
        fatal("The menu's tick called a routine not translated.");
}

/* ---- the high scores */

static void load_hiscores(void)
{
    static const uint16_t files[2][3] = {
        { M_hiscores_pd1_path, M_hiscores_pd1, M_hiscores_pd1_default },
        { M_hiscores_pd2_path, M_hiscores_pd2, M_hiscores_pd2_default },
    };
    char path[SYS_PATH];
    uint8_t *data;
    size_t size, i;
    int f;

    for (f = 0; f < 2; f++) {
        if (dos_path(s_data, files[f][0], path, sizeof path) == 0 &&
            (data = sys_load(path, &size)) != NULL) {
            for (i = 0; i < size; i++)
                dwb((uint16_t)(files[f][1] + i), data[i]);
            free(data);
        } else {
            for (i = 0; i < 0x90; i++)
                dwb((uint16_t)(files[f][1] + i), db((uint16_t)(files[f][2] + i)));
        }
    }
}

/* the glyph `index` of hiscore_font at char_x/char_y of the picture below
 * the list (PD2's 3200h lower), colour 0 transparent; char_x + 8.  The
 * planes start at the FONTS segment's low bits, not char_x's (AX holds the
 * segment when the loop starts): the glyph lands at char_x rounded down to
 * 4 plus (FONTS AND 3), wherever DOS loaded the program. */
static void draw_hiscore_char(uint16_t index)
{
    uint16_t y = dw(M_char_y), di, ax, si;
    int p, row;

    cww(M_sprite_plane_wrap, 0);
    di = (uint16_t)(y * 0x50 + (dw(M_char_x) >> 2) + 0x9240 + 0x3C0);
    if (cw(M_hiscore_program))
        di = (uint16_t)(di + 0x3200);
    ax = s_fonts;
    si = (uint16_t)(M_hiscore_font + ((index & 0xFF) << 3));
    for (p = 0; p < 4; p++, ax++, si++) {
        uint16_t a, s = si;
        uint8_t v;
        di = plane_step(p, &ax, di);
        set_planes((uint8_t)(1 << ax), 0);
        a = di;
        for (row = 0; row < 10; row++, s = (uint16_t)(s + 0x140), a = (uint16_t)(a + 0x50)) {
            if ((v = frb(s_fonts, s)) != 0)
                vga_write(a, v);
            if ((v = frb(s_fonts, (uint16_t)(s + 4))) != 0)
                vga_write((uint16_t)(a + 1), v);
        }
    }
    dww(M_char_x, (uint16_t)(dw(M_char_x) + 8));
}

/* four tables at the x/y pairs at `si`, from the entries at `di` */
static void draw_hiscore_tables(uint16_t si, uint16_t di)
{
    int t, e, k;

    for (t = 0; t < 4; t++, si = (uint16_t)(si + 4)) {
        dww(M_char_x, dw(si));
        dww(M_char_y, dw((uint16_t)(si + 2)));
        for (e = 0; e < 4; e++) {
            for (k = 0; k < 3; k++) {
                uint8_t dl = db(di);
                uint16_t bx = char_index(&dl);
                if (dl == db(di)) {
                    di++;
                    draw_hiscore_char(bx);
                }
            }
            dww(M_char_x, (uint16_t)(dw(M_char_x) + 8));
            draw_hiscore_char(db(di) & 0x0F);
            di++;
            for (k = 0; k < 5; k++, di++) {
                draw_hiscore_char(db(di) >> 4);
                draw_hiscore_char(db(di) & 0x0F);
            }
            dww(M_char_x, dw(si));
            dww(M_char_y, (uint16_t)(dw(M_char_y) + 0x0B));
        }
    }
}

static void draw_hiscores(void)
{
    cww(M_hiscore_program, 0);
    draw_hiscore_tables(M_hiscore_places_pd1, M_hiscores_pd1);
    cww(M_hiscore_program, 1);
    draw_hiscore_tables(M_hiscore_places_pd2, M_hiscores_pd2);
}

/* menu_start between 9240h and menu_start_end, a rest at each end */
static void hiscore_scroll(void)
{
    uint16_t ax;

    if (cw(M_hiscore_resting)) {
        cww(M_hiscore_pause, (uint16_t)(cw(M_hiscore_pause) - 1));
        if ((int16_t)cw(M_hiscore_pause) >= 0)
            return;
        cww(M_hiscore_resting, 0);
        cww(M_hiscore_pause, 0xD8);
    }
    ax = (uint16_t)(dw(M_menu_start) + dw(M_hiscore_scroll_step));
    if (ax >= dw(M_menu_start_end)) {
        cww(M_hiscore_resting, 1);
        ax = dw(M_menu_start_end);
        dww(M_hiscore_scroll_step, 0xFFB0);
    } else if (ax <= 0x9240) {
        ax = 0x9240;
        dww(M_hiscore_scroll_step, 0x50);
        cww(M_hiscore_resting, 1);
    }
    dww(M_menu_start, ax);
}

/* ---- the list */

/* the keyboard as a mouse; 1 (CF) when Esc is down */
static int read_menu_keys(void)
{
    static const uint8_t fkeys[10] = { 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44 };
    int i;

    if (key_down(0x01))
        return 1;
    if (key_down(0xC8)) {
        dww(M_mouse_y, (uint16_t)(dw(M_mouse_y) - 2));
        if (dsw(M_mouse_y) < 0)
            dww(M_mouse_y, 0);
    } else if (key_down(0xD0)) {
        dww(M_mouse_y, (uint16_t)(dw(M_mouse_y) + 2));
    } else if (key_down(0xCB)) {
        dww(M_mouse_x, (uint16_t)(dw(M_mouse_x) - 2));
        if (dsw(M_mouse_x) < 0)
            dww(M_mouse_x, 0);
    } else if (key_down(0xCD)) {
        dww(M_mouse_x, (uint16_t)(dw(M_mouse_x) + 2));
    } else if (key_down(0x1C)) {
        dwb(M_mouse_buttons, 1);
        wait_keys_up();
    } else {
        for (i = 0; i < 10; i++)
            if (key_down(fkeys[i])) {
                dwb(M_selection, (uint8_t)(i + 1));
                dwb(M_mouse_buttons, 2);
                wait_keys_up();
                break;
            }
    }
    /* INT 33h AX=4: the pointer to the mouse driver (none here) */
    dwb(M_menu_key_seen, 1);
    return 0;
}

/* the pointer at the top or the bottom scrolls the list a row */
static void edge_scroll(void)
{
    int16_t y = dsw(M_mouse_y);

    if (y <= 0x10) {
        if (dw(M_menu_start) == 0x3C0)
            return;
        dww(M_menu_start, (uint16_t)(dw(M_menu_start) - 0x50));
        dww(M_menu_row, (uint16_t)(dw(M_menu_row) - 1));
        if (dsw(M_menu_row) < 0)
            dww(M_menu_row, 0);
    } else if (y >= 0xA4) {
        if (dsw(M_menu_start) >= 0x5780)
            return;
        dww(M_menu_start, (uint16_t)(dw(M_menu_start) + 0x50));
        dww(M_menu_row, (uint16_t)(dw(M_menu_row) + 1));
        if (dsw(M_menu_row) > 0x118)
            dww(M_menu_row, 0x118);
    }
}

static void menu_init(void);

/* the table run: its program loaded over the menu's memory, which is put
 * back after it (under DOS the table ran above the menu) */
static void run_table(int bx)
{
    static uint8_t saved[MEM_SIZE];
    uint16_t psp = seg_psp, arena = mem_arena;
    int sel = db(M_selection) - 1, r;
    char err[512];

    memcpy(saved, mem, MEM_SIZE);
    r = hooks->table(bx + 1, sel >= 4 ? sel - 4 : sel, err, sizeof err);
    memcpy(mem, saved, MEM_SIZE);
    prog_id = PROG_MENU;
    seg_psp = psp;
    mem_arena = arena;
    install_keyboard();
    if (r == 0)
        leave();
    if (r < 0)
        fatal(err);
}

/* F1-F8: the table (its animation first); F9, F10: not translated */
static void run_selection(void)
{
    uint8_t sel = db(M_selection);

    fade_out(s_data, M_select_palette, 0x300);
    stop_sound();
    dww(M_tick_draw, M_tick_nothing);
    dww(M_tick_remove, M_tick_nothing);
    dww(M_tick_ticker, M_tick_nothing);
    if (sel != 0x0A && sel != 9) {
        restore_keyboard();
        run_table(sel - 1 >= 4 ? 1 : 0);
    }
    menu_init();
}

/* a selection made with a key, else the list's row under the pointer */
static void select_at_pointer(void)
{
    uint16_t bx = M_menu_ranges, y;
    int count, dx;

    if (db(M_selection) != 0) {
        run_selection();
        return;
    }
    count = db(bx++);
    y = (uint16_t)(dw(M_menu_row) + dw(M_mouse_y));
    for (dx = 1; dx <= count; dx++, bx = (uint16_t)(bx + 8))
        if (y >= dw((uint16_t)(bx + 2)) && y <= dw((uint16_t)(bx + 6))) {
            dwb(M_selection, (uint8_t)dx);
            run_selection();
            return;
        }
}

static void menu_video_init(void)
{
    screen_off();
    clear_vram();
    set_mode_x();
    screen_off();
    load_vga_image(M_select_vga_path, 0x3C0);
    load_hiscores();
    draw_hiscores();
    set_split();
    clear_palette(0x300);
    dww(M_menu_start, 0x3C0);
    show_menu_start();
    screen_on();
}

static void menu_init(void)
{
    /* mouse_range (INT 33h AX=7, 8) and the pointer to 140h, 64h (AX=4):
     * no mouse driver here */
    dwb(M_mouse_buttons, 0);
    dwb(M_menu_unused_byte, 0);
    dwb(M_frame_tick, 0);
    dwb(M_sound_loaded, 0);
    dwb(M_selection, 0);
    dwb(M_ticker_state, 0xFE);
    dww(M_mouse_x, 0);
    dww(M_mouse_y, 0);
    dww(M_menu_unused_word2, 0);
    dww(M_menu_row, 0);
    dww(M_hiscore_scroll_step, 0);
    dww(M_menu_start, 0x3C0);
    dww(M_menu_start_end, 0x5780);
    dww(M_char_x, 0);
    dww(M_char_y, 0);
    menu_video_init();
    poll_mouse();
    draw_pointer(1);
    dww(M_tick_draw, M_tick_draw_pointer);
    dww(M_tick_remove, M_tick_remove_pointer);
    dww(M_tick_ticker, M_ticker_step);
    cww(M_pointer_on, 1);
    load_sound(0);
    fade_in(s_data, M_select_palette, 0x300);
}

/* the list back at its top after the high score show or a table */
static void list_again(void)
{
    dww(M_hiscore_scroll_step, 0);
    dww(M_menu_row, 0);
    dww(M_menu_start, 0x3C0);
    dww(M_menu_start_end, 0x5780);
    show_menu_start();
}

/* the list, then the high score show, until Esc */
static void menu(void)
{
    int cx = 0x870, chosen;

    menu_init();
    poll_mouse();
    cww(M_menu_unused_word, 1);
    for (;;) {
        do {
            checkpoint_menu("menu_loop");
            show_menu_start();
            poll_mouse();
            if (read_menu_keys())
                return;
            edge_scroll();
            if (db(M_mouse_buttons))
                select_at_pointer();
        } while (--cx);

        fade_out(s_data, M_select_palette, 0x2D0);
        cww(M_pointer_on, 0);
        dww(M_menu_start, 0x9240);
        dww(M_menu_start_end, 0xBF40);
        show_menu_start();
        fade_in(s_data, M_select_palette, 0x2D0);
        cx = 0x5A0;
        chosen = 0;
        do {
            checkpoint_menu("hiscore_show_loop");
            show_menu_start();
            poll_mouse();
            if (db(M_mouse_buttons)) {
                dwb(M_mouse_buttons, 0);
                break;
            }
            if (any_key_down()) {
                if (read_menu_keys())
                    return;
                if (db(M_selection) != 0) {
                    select_at_pointer();
                    chosen = 1;
                    break;
                }
            }
            hiscore_scroll();
        } while (--cx);
        /* after a table the list comes without a fade (menu_init faded in) */
        if (!chosen)
            fade_out(s_data, M_select_palette, 0x2D0);
        list_again();
        if (!chosen)
            fade_in(s_data, M_select_palette, 0x2D0);
        cww(M_pointer_on, 1);
        /* 2D0h, set before show_menu_start, which leaves menu_start (3C0h)
         * in CX; the fade sets 2D0h again as its count */
        cx = chosen ? dw(M_menu_start) : 0x2D0;
    }
}

int menu_run(const char *game, const MenuHooks *h, char *err, size_t n)
{
    int r;

    if (mem_load_menu(game, err, n))
        return -1;
    s_code = (uint16_t)(seg_psp + 0x10);
    s_data = (uint16_t)(s_code + DDM_DATA);
    s_fonts = (uint16_t)(s_code + DDM_FONTS);
    s_history = (uint16_t)(s_code + DDM_HISTORY);
    pd_game_dir = game;
    hooks = h;
    exit_err = err;
    exit_err_n = n;
    if (n)
        err[0] = 0;
    r = setjmp(exit_jmp);
    if (r) {
        snd_stop();                     /* the program's memory stays as it was */
        restore_keyboard();
        frame_set_tick(NULL);
        return r == 2 ? -1 : 0;
    }

    /* start: check_memory (mem_load_menu's arena is as after it) */
    install_keyboard();
    alloc_pointer_buffers();
    detect_mouse();
    /* run_intro: DDPCINTR.EXE, not translated; run_fli_player with 0 */
    dwb(M_selection, 0);
    restore_keyboard();
    if (hooks->intro && !hooks->intro())
        leave();
    install_keyboard();
    menu();

    /* exit: from the menu's Esc */
    wait_keys_up();
    fade_out(s_data, M_select_palette, 0x300);
    stop_sound();
    restore_keyboard();
    screen_off();
    clear_vram();
    return 0;
}
