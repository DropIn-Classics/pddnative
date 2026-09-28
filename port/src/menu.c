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
#include "platform.h"
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
void checkpoint_pokes(const char *where, uint16_t seg);
extern const char *pd_game_dir;
extern const char *dos_dir;

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
    checkpoint_pokes(where, s_data);
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

/* the file named at seg:name, 320 pixels a row, into video memory from
 * `di`: read in chunks of 0FA00h bytes into a DOS block, 3E80h groups of
 * four copied after each (a short last chunk too: the rest of the group
 * from the chunk before), until a read is short */
static void load_vga_image(uint16_t seg, uint16_t name, uint16_t di)
{
    char path[SYS_PATH];
    uint8_t *data;
    size_t size = 0, pos = 0, k, i;
    uint16_t block = dos_alloc(0x0FA1);

    if (!block)
        fatal("Not enough memory for the menu's picture.");
    cww(M_image_block, block);
    if (dos_path(seg, name, path, sizeof path) || (data = sys_load(path, &size)) == NULL)
        fatal("A picture of the menu could not be read.");
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

/* ---- the mouse driver (INT 33h): the port's.  The pointer the programs
 * give it (AX=4) is moved by the keyboard (the programs' own key
 * routines) and by the mouse (plat_mouse: where it is on the picture,
 * taken when it moved), within the ranges they set (AX=7, 8) or a mode
 * set gives (mouse_new_mode).  AX=3 reports a button down once for each
 * click, where a real driver reports it while it is held: the browser
 * takes a held button again each pass, and the port shows the next
 * table's page at once, not after a disk's time.  In the history's
 * 640x480 screens the program has it show its pointer (AX=1), which the
 * port draws over the picture (mouse_overlay), not into video memory.
 * menu_mouse 0: no driver, as in tools/run. */

int menu_mouse = 1;

static struct {
    int present;
    int x, y, x1, x2, y1, y2;
    int height;                         /* the mode's rows (x counts 640) */
    int shown;                          /* the pointer is shown at 0 */
    int clicks;                         /* the buttons clicked, for AX=3 */
    int host_x, host_y;                 /* plat_mouse's last place */
    int host_off;                       /* the mouse not taken (the menu box) */
} drv;

static void mouse_clamp(void)
{
    drv.x = drv.x < drv.x1 ? drv.x1 : drv.x > drv.x2 ? drv.x2 : drv.x;
    drv.y = drv.y < drv.y1 ? drv.y1 : drv.y > drv.y2 ? drv.y2 : drv.y;
}

/* AX=4 */
static void mouse_set(uint16_t x, uint16_t y)
{
    if (!drv.present)
        return;
    drv.x = (int16_t)x;
    drv.y = (int16_t)y;
    mouse_clamp();
}

/* mouse_range: y from CX to DX (AX=8), x from AX to BX (AX=7) */
static void mouse_range(uint16_t ax, uint16_t bx, uint16_t cx, uint16_t dx)
{
    if (!drv.present)
        return;
    drv.y1 = (int16_t)cx;
    drv.y2 = (int16_t)dx;
    drv.x1 = (int16_t)ax;
    drv.x2 = (int16_t)bx;
    mouse_clamp();
}

/* AX=1, AX=2 */
static void mouse_show(void)
{
    if (drv.shown < 0)
        drv.shown++;
}

static void mouse_hide(void)
{
    drv.shown--;
}

/* the pointer a driver draws: an arrow, its tip at the position */
static void mouse_overlay(VgaFrame *f)
{
    static const char *const arrow[] = {
        "X", "XX", "XWX", "XWWX", "XWWWX", "XWWWWX", "XWWWWWX", "XWWWWWWX",
        "XWWWWWWWX", "XWWWWWXXXX", "XWWXWWX", "XWX XWWX", "XX  XWWX",
        "X    XWWX", "     XWWX", "      XX", NULL };
    int best[2] = { 0, 0 }, dist[2] = { 1 << 30, 1 << 30 }, i, r, c;

    if (!drv.present || drv.shown < 0 || f->width < 640)
        return;
    for (i = 0; i < 256; i++) {
        int rr = f->palette[i] >> 16 & 0xFF, g = f->palette[i] >> 8 & 0xFF, b = f->palette[i] & 0xFF;
        int dark = rr * rr + g * g + b * b;
        int light = (255 - rr) * (255 - rr) + (255 - g) * (255 - g) + (255 - b) * (255 - b);
        if (dark < dist[0]) { dist[0] = dark; best[0] = i; }
        if (light < dist[1]) { dist[1] = light; best[1] = i; }
    }
    for (r = 0; arrow[r]; r++)
        for (c = 0; arrow[r][c]; c++) {
            int x = drv.x + c, y = drv.y + r;
            if (arrow[r][c] != ' ' && x < f->width && y < f->height)
                f->pixels[y * f->width + x] = (uint8_t)best[arrow[r][c] == 'W'];
        }
}

/* what the driver does when INT 10h sets a mode: the ranges the whole
 * screen, 640 wide in both modes (as DOSBox's driver does, from memory,
 * not checked with a real one).  The program sets no range for the
 * history's lists, which need the 480 rows of mode 12h.  Clicks made
 * before (in a table, in a fade) are dropped. */
static void mouse_new_mode(int height)
{
    int x, y, clicks;

    if (!drv.present)
        return;
    plat_mouse(&x, &y, &clicks);
    drv.clicks = 0;
    drv.x1 = drv.y1 = 0;
    drv.x2 = 639;
    drv.y2 = height - 1;
    drv.height = height;
    mouse_clamp();
}

static void menu_mode_x(void)
{
    set_mode_x();
    mouse_new_mode(200);
}

static void menu_mode_12(void)
{
    vga_set_mode(0x12);
    mouse_new_mode(480);
}

/* INT 33h AX=0: mouse_present 1 when a driver answers (the pointer
 * hidden, in the middle of a 640x200 screen, as a driver has it after a
 * reset in the 320x200 mode; not checked with a real one), else 0FEh */
static void detect_mouse(void)
{
    if (!menu_mouse) {
        drv.present = 0;
        dwb(M_mouse_present, 0xFE);
        return;
    }
    drv.present = 1;
    drv.shown = -1;
    drv.x1 = drv.y1 = 0;
    drv.x2 = 639;
    drv.y2 = 199;
    drv.x = 320;
    drv.y = 100;
    drv.height = 200;
    drv.clicks = 0;
    drv.host_x = drv.host_y = -1;
    dwb(M_mouse_present, 1);
}

/* the mouse's moves and clicks to the driver */
static void mouse_from_host(void)
{
    int x, y, clicks;

    if (!plat_mouse(&x, &y, &clicks) || !drv.present || drv.host_off)
        return;
    drv.clicks |= clicks;
    if (x == drv.host_x && y == drv.host_y)
        return;
    drv.host_x = x;
    drv.host_y = y;
    drv.x = x * 640 >> 16;
    drv.y = y * drv.height >> 16;
    mouse_clamp();
}

/* INT 33h AX=3 when a mouse was found: x halved (the driver counts 640 a
 * row in the 320x200 mode) */
static void poll_mouse(void)
{
    if (db(M_mouse_present) != 1)
        return;
    mouse_from_host();
    dww(M_mouse_x, (uint16_t)((uint16_t)drv.x >> 1));
    dww(M_mouse_y, (uint16_t)drv.y);
    dwb(M_mouse_buttons, (uint8_t)drv.clicks);
    drv.clicks = 0;
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
    mouse_set((uint16_t)(dw(M_mouse_x) << 1), dw(M_mouse_y));
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

/* ---- the box (menu_box): the port's own, in place of the pointer.  A
 * frame drawn into the list's picture around one of the eight tables'
 * entries (menu_ranges), moved with Up and Down; the list scrolls so that
 * it stays on the screen; a dark line either side of it, for the light
 * entries.  The pixels under it are kept and put back
 * before it moves. */

int menu_box;

enum { BOX_TABLES = 8, BOX_EDGE = 5, BOX_VISIBLE = 0xBC, BOX_LAST_ROW = 0x118,
       BOX_SCROLL = 6, BOX_PULSE = 24, BOX_DELAY = 20, BOX_REPEAT = 6 };

static int box_entry;                   /* 0-7, kept while the program runs */
static int box_drawn, box_x1, box_y1, box_x2, box_y2;
static int box_held, box_hold, box_moved, box_pulse;
static int box_back;                    /* back to the list from the high score show */
static uint8_t box_colour[3];           /* two to pulse between, the dark lines' */
static uint8_t box_saved[2 * BOX_EDGE * (320 + 480)];

static uint16_t box_range(int entry, int word)
{
    return dw((uint16_t)(M_menu_ranges + 1 + 8 * entry + 2 * word));
}

/* the palette entry of select_palette nearest to r, g, b (0-63) */
static uint8_t box_nearest(int r, int g, int b)
{
    long best = -1, d;
    int i, dr, dg, db_;
    uint8_t at = 0;

    for (i = 0; i < 256; i++) {
        dr = db((uint16_t)(M_select_palette + 3 * i)) - r;
        dg = db((uint16_t)(M_select_palette + 3 * i + 1)) - g;
        db_ = db((uint16_t)(M_select_palette + 3 * i + 2)) - b;
        d = (long)dr * dr + (long)dg * dg + (long)db_ * db_;
        if (best < 0 || d < best) {
            best = d;
            at = (uint8_t)i;
        }
    }
    return at;
}

/* each pixel of the frame, x/y in the list; `ring` 0 the outermost */
static void box_walk(void (*fn)(int x, int y, int ring, int n))
{
    int x, y, ring, n = 0;

    for (y = box_y1; y <= box_y2; y++)
        for (x = box_x1; x <= box_x2; x++) {
            ring = x - box_x1;
            if (box_x2 - x < ring) ring = box_x2 - x;
            if (y - box_y1 < ring) ring = y - box_y1;
            if (box_y2 - y < ring) ring = box_y2 - y;
            if (ring < BOX_EDGE && n < (int)sizeof box_saved)
                fn(x, y, ring, n++);
        }
}

static uint16_t box_addr(int x, int y)
{
    return (uint16_t)(0x3C0 + y * 0x50 + (x >> 2));
}

static void box_save_pixel(int x, int y, int ring, int n)
{
    (void)ring;
    set_planes(0, (uint8_t)(x & 3));
    box_saved[n] = vga_read(box_addr(x, y));
}

static void box_restore_pixel(int x, int y, int ring, int n)
{
    (void)ring;
    set_planes((uint8_t)(1 << (x & 3)), 0);
    vga_write(box_addr(x, y), box_saved[n]);
}

static void box_draw_pixel(int x, int y, int ring, int n)
{
    (void)n;
    set_planes((uint8_t)(1 << (x & 3)), 0);
    vga_write(box_addr(x, y), ring == 0 || ring == BOX_EDGE - 1 ? box_colour[2]
                              : box_colour[box_pulse / BOX_PULSE]);
}

static void box_remove(void)
{
    if (box_drawn)
        box_walk(box_restore_pixel);
    box_drawn = 0;
}

static void box_draw(void)
{
    box_remove();
    box_x1 = box_range(box_entry, 0);
    box_y1 = box_range(box_entry, 1);
    box_x2 = box_range(box_entry, 2);
    box_y2 = box_range(box_entry, 3);
    box_walk(box_save_pixel);
    box_walk(box_draw_pixel);
    box_drawn = 1;
}

/* the list scrolled towards the box (at once when `jump`), menu_row
 * following menu_start as edge_scroll keeps it */
static void box_scroll(int jump)
{
    int row = dsw(M_menu_row), want = row;

    if (box_y1 < want)
        want = box_y1;
    if (box_y2 >= want + BOX_VISIBLE)
        want = box_y2 - BOX_VISIBLE + 1;
    if (want < 0)
        want = 0;
    if (want > BOX_LAST_ROW)
        want = BOX_LAST_ROW;
    if (!jump && want > row + BOX_SCROLL)
        want = row + BOX_SCROLL;
    if (!jump && want < row - BOX_SCROLL)
        want = row - BOX_SCROLL;
    dww(M_menu_row, (uint16_t)want);
    dww(M_menu_start, (uint16_t)(0x3C0 + want * 0x50));
}

/* the box drawn anew on the picture just loaded, the list at it */
static void box_init(void)
{
    box_colour[0] = box_nearest(63, 63, 0);
    box_colour[1] = box_nearest(63, 63, 63);
    box_colour[2] = box_nearest(0, 0, 0);
    box_drawn = 0;
    box_pulse = 0;
    box_draw();
    box_scroll(1);
}

/* read_menu_keys with the box: Up and Down move it (held, they repeat),
 * Enter chooses its table; in the high score show (`in_list` 0) Enter and
 * the arrows go back to the list.  F1-F10 and Esc as there; 1 when Esc
 * is down */
static int box_keys(int in_list)
{
    static const uint8_t fkeys[10] = { 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44 };
    int dir = key_down(0xC8) ? -1 : key_down(0xD0) ? 1 : 0, step = 0, i;

    box_moved = 0;
    if (key_down(0x01))
        return 1;
    if (dir != box_held) {
        box_held = dir;
        box_hold = 0;
        step = dir;
    } else if (dir && ++box_hold >= BOX_DELAY) {
        box_hold = BOX_DELAY - BOX_REPEAT;
        step = dir;
    }
    if (!in_list && (step || key_down(0x1C))) {
        box_back = 1;                   /* a click, which the driver's poll would undo */
        box_held = 0;
        wait_keys_up();
        return 0;
    }
    if (step && box_entry + step >= 0 && box_entry + step < BOX_TABLES) {
        box_entry += step;
        box_pulse = 0;
        box_draw();
        box_moved = 1;
    }
    if (key_down(0x1C)) {
        dwb(M_selection, (uint8_t)(box_entry + 1));
        dwb(M_mouse_buttons, 2);
        wait_keys_up();
    } else {
        for (i = 0; i < 10; i++)
            if (key_down(fkeys[i])) {
                dwb(M_selection, (uint8_t)(i + 1));
                dwb(M_mouse_buttons, 2);
                if (i < BOX_TABLES)
                    box_entry = i;
                wait_keys_up();
                break;
            }
    }
    return 0;
}

/* a pass of the list with the box: the pulse, the scroll */
static void box_pass(void)
{
    if (++box_pulse == 2 * BOX_PULSE)
        box_pulse = 0;
    if (box_pulse % BOX_PULSE == 0)
        box_draw();
    box_scroll(0);
}

/* ---- the history (F9): the language screen (DDPCLANG.VGA), then in the
 * HISTORY folder a start screen (DDPCHIST.VGA) whose five boxes choose a
 * list (the tables, or their manufacturers, designers or years; the fifth
 * leaves), then in mode 12h the list and the browser: a table's picture
 * (.016, 640x480) with its text of the language's .HOP, and eight buttons
 * below (the four lists, exit, the next picture, the table before and
 * after).  Without a VESA BIOS (as in tools/run: INT 10h AX=4F00h fails)
 * the program keeps to the .016 pictures; its VESA routines are not
 * translated. */

static uint8_t hb(uint16_t o) { return frb(s_history, o); }
static void hwb(uint16_t o, uint8_t v) { fwb(s_history, o, v); }
static uint16_t hw(uint16_t o) { return frw(s_history, o); }
static void hww(uint16_t o, uint16_t v) { fww(s_history, o, v); }

/* read_file_far: the file named at seg:name into dst:di on; 1 when it
 * could not be opened */
static int read_file_far(uint16_t seg, uint16_t name, uint16_t dst, uint16_t di)
{
    char path[SYS_PATH];
    uint8_t *data;
    size_t size, i;
    uint32_t a = ((uint32_t)dst << 4) + di;

    if (dos_path(seg, name, path, sizeof path) || (data = sys_load(path, &size)) == NULL)
        return 1;
    for (i = 0; i < size && a + i < MEM_SIZE; i++)
        mem[a + i] = data[i];
    free(data);
    return 0;
}

/* the language screen: 1 (CF) when left with Esc, else language_index
 * set to the flag clicked */
static int language_screen(void)
{
    uint16_t si;
    int esc, count, dx;

    dww(M_menu_start, 0);
    mouse_set(0x140, 0x64);
    screen_off();
    clear_vram();
    menu_mode_x();
    screen_off();
    clear_palette(0x300);
    screen_on();
    load_vga_image(s_data, M_language_vga_path, 0);
    fade_in(s_data, M_language_palette, 0x300);
    poll_mouse();
    draw_pointer(0);
    for (;;) {
        checkpoint_menu("language_loop");
        wait_retrace();
        remove_pointer(0);
        poll_mouse();
        esc = read_menu_keys();
        draw_pointer(0);
        if (esc)
            break;
        si = M_language_ranges;
        count = db(si++);
        for (dx = 0; dx < count; dx++, si = (uint16_t)(si + 8))
            if (dw(M_mouse_y) >= dw((uint16_t)(si + 2)) && dw(M_mouse_y) <= dw((uint16_t)(si + 6)) &&
                dw(M_mouse_x) >= dw(si) && dw(M_mouse_x) <= dw((uint16_t)(si + 4)))
                break;
        /* a flag under the pointer: chosen with Enter (or anything with no
         * selection made, which cannot be: F9 made one); the F keys not */
        if (dx < count && db(M_mouse_buttons) &&
            (db(M_selection) == 0 || db(M_mouse_buttons) == 1)) {
            dwb(M_language_index, (uint8_t)dx);
            break;
        }
    }
    wait_keys_up();
    fade_out(s_data, M_language_palette, 0x300);
    return esc;
}

/* the index of the rectangle at HISTORY:bx (a count, then x1, y1, x2, y2)
 * the pointer is in, else 0FFh */
static uint16_t hit_test(uint16_t bx)
{
    uint16_t si = (uint16_t)(bx + 1), n = hb(bx), bp;
    uint16_t x = dw(M_mouse_x), y = dw(M_mouse_y);

    for (bp = 0; bp < n; bp++, si = (uint16_t)(si + 8))
        if (y >= hw((uint16_t)(si + 2)) && y <= hw((uint16_t)(si + 6)) &&
            x >= hw(si) && x <= hw((uint16_t)(si + 4)))
            return bp;
    return 0xFF;
}

/* INT 33h AX=3 without halving x: the history's 640-pixel screens */
static void poll_history_mouse(void)
{
    if (db(M_mouse_present) != 1)
        return;
    mouse_from_host();
    dww(M_mouse_x, (uint16_t)drv.x);
    dww(M_mouse_y, (uint16_t)drv.y);
    dwb(M_mouse_buttons, (uint8_t)drv.clicks);
    drv.clicks = 0;
}

/* read_menu_keys without the F keys, the pointer given to the driver only
 * after a key; 1 (CF) when Esc is down, once it is let go */
static int read_history_keys(void)
{
    if (key_down(0x01)) {
        wait_keys_up();
        return 1;
    }
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
        return 0;
    }
    mouse_set(dw(M_mouse_x), dw(M_mouse_y));
    return 0;
}

/* the NUL-terminated string at seg:si in HISTORY.FNT's glyphs, a byte a
 * glyph row, at byte x, line y of the 640x480 screen, in all four planes
 * (inverted when `inverted`); the offset of its NUL */
static uint16_t draw_history_string(uint16_t seg, uint16_t si, uint16_t x, uint16_t y, int inverted)
{
    uint16_t di = (uint16_t)(y * 0x50 + x), font = hw(M_history_font_segment), s, d;
    uint8_t mask, c, v;
    int r;

    cww(M_string_planes, 4);
    cww(M_string_inverted, (uint16_t)inverted);
    for (mask = 1; mask < 0x10; mask = (uint8_t)(mask + mask)) {
        set_planes(mask, 0);
        for (s = si, d = di; (c = frb(seg, s)) != 0; s++, d++)
            for (r = 0; r < 8; r++) {
                v = frb(font, (uint16_t)(c * 8 + r));
                vga_write((uint16_t)(d + r * 0x50), inverted ? (uint8_t)~v : v);
            }
        cww(M_string_end, s);
        cww(M_string_planes, (uint16_t)(cw(M_string_planes) - 1));
    }
    return cw(M_string_end);
}

/* (50h - the length of the string at HISTORY:si) / 2; *len the length */
static uint16_t centre_string(uint16_t si, uint16_t *len)
{
    uint16_t dx = 0;

    while (hb((uint16_t)(si + dx)))
        dx++;
    *len = dx;
    return (uint16_t)((uint16_t)(0x50 - dx) >> 1);
}

/* the first line of a list of `count` rows centred on the screen (the x
 * the routine works out too goes unused) */
static uint16_t centre_list(uint16_t di, uint16_t count)
{
    uint16_t dx = 0, n, bp;

    for (n = count; n; n--) {
        centre_string(hw(di), &bp);
        if ((int16_t)bp >= (int16_t)dx) {
            dx = bp;
            di = (uint16_t)(di + 2);
        }
    }
    return (uint16_t)((uint16_t)(0x1E0 - (count << 3)) >> 1);
}

/* plane 0 of `cx` bytes in eight lines at byte x, line y inverted */
static void invert_history_row(uint16_t x, uint16_t y, uint16_t cx)
{
    uint16_t di, a;
    int r, i;

    if (cx == 0)
        return;
    di = (uint16_t)(y * 0x50 + x);
    set_planes(1, 0);
    mouse_hide();
    for (r = 0; r < 8; r++, di = (uint16_t)(di + 0x50))
        for (i = 0, a = di; i < cx; i++, a++)
            vga_write(a, (uint8_t)~vga_read(a));
    mouse_show();
}

/* the list's row under the pointer inverted until a click (its index)
 * or the pointer leaves it or Esc (0FFFFh) */
static uint16_t choose_history_row(uint16_t rows, uint16_t count)
{
    uint16_t si, c, bx, dx;

    for (;;) {
        checkpoint_menu("history_row_loop");
        si = rows;
        wait_retrace();
        poll_history_mouse();
        if (read_history_keys())
            return 0xFFFF;
        bx = (uint16_t)(dw(M_mouse_y) >> 3);
        for (c = count; c; c--, si = (uint16_t)(si + 6)) {
            dx = hw((uint16_t)(si + 2));
            cww(M_history_row_y, dx);
            if ((uint16_t)(dx >> 3) == bx)
                break;
        }
        if (c == 0)
            return 0xFFFF;
        cww(M_history_rows_left, c);
        cww(M_history_row, bx);
        invert_history_row(0, cw(M_history_row_y), 0x50);
        for (;;) {
            checkpoint_menu("history_row_wait");
            wait_retrace();
            poll_mouse();
            read_menu_keys();
            if (db(M_mouse_buttons))
                return (uint16_t)(count - cw(M_history_rows_left));
            if (cw(M_history_row) != (uint16_t)(dw(M_mouse_y) >> 3))
                break;
        }
        invert_history_row(0, cw(M_history_row_y), 0x50);
    }
}

/* the `count` strings whose offsets are at HISTORY:table as a list, one
 * of them chosen; its index */
static uint8_t select_history_filter(uint16_t table, uint16_t count)
{
    uint16_t y, i, x, len, si, rec;
    uint16_t r;

    dwb(M_mouse_buttons, 0);
    y = centre_list(table, count);
    for (i = 0; i < count; i++, y = (uint16_t)(y + 8)) {
        si = hw((uint16_t)(table + 2 * i));
        x = centre_string(si, &len);
        rec = (uint16_t)(M_history_rows + 6 * i);
        hww(rec, x);
        hww((uint16_t)(rec + 2), y);
        hww((uint16_t)(rec + 4), len);
        draw_history_string(s_history, si, x, y, 0);
    }
    mouse_show();
    do
        r = choose_history_row(M_history_rows, count);
    while (r == 0xFFFF);
    mouse_hide();
    return (uint8_t)r;
}

/* filtered_table_records: the nine-byte table records of the filter
 * history_filter_mask chooses (the first bit set of 1 manufacturer, 2
 * designer, 4 year), all of them for none */
static void filter_history_records(void)
{
    uint16_t si = M_table_records, di = M_filtered_table_records, n;
    uint8_t mask;
    int take, i;

    hww(M_filtered_table_count, 0);
    for (n = 0x35; n; n--, si = (uint16_t)(si + 9)) {
        mask = hb(M_history_filter_mask);
        if (mask & 1)
            take = hb(M_manufacturer_filter) == hb((uint16_t)(si + 1));
        else if (mask & 2)
            take = hb(M_designer_filter) == hb((uint16_t)(si + 3));
        else if (mask & 4)
            take = hb(M_year_filter) == hb((uint16_t)(si + 2));
        else
            take = 1;
        if (take) {
            for (i = 0; i < 9; i++)
                hwb((uint16_t)(di + i), hb((uint16_t)(si + i)));
            di = (uint16_t)(di + 9);
            hww(M_filtered_table_count, (uint16_t)(hw(M_filtered_table_count) + 1));
        }
    }
}

static void history_title(uint16_t text)
{
    uint16_t len;
    draw_history_string(s_history, text, centre_string(text, &len), 1, 0);
}

/* a list of manufacturers (1), designers (2) or years (4): the tables of
 * the one chosen */
static void filter_by(uint8_t mask, uint16_t text, uint16_t table, uint16_t count, uint16_t var)
{
    hwb(M_history_filter_mask, mask);
    history_title(text);
    hwb(var, select_history_filter(table, count));
    filter_history_records();
    cww(M_shown_table, 0xFFFF);
    hww(M_selected_table, 0);
}

/* the list of the tables: all of them, the one chosen shown */
static void select_history_table(void)
{
    uint8_t al;

    history_title(M_select_table_text);
    hwb(M_history_filter_mask, 8);
    al = select_history_filter(M_table_name_table, 0x35);
    hwb(M_direct_table_selection, al);
    hww(M_selected_table, al);
    filter_history_records();
    cww(M_shown_table, 0xFFFF);
}

/* the routine history_action_table has for `choice` (0-3; 4 leaves) */
static void history_action(uint16_t choice)
{
    uint16_t routine = hw((uint16_t)(M_history_action_table + 2 * choice));

    if (routine == M_select_history_table)
        select_history_table();
    else if (routine == M_filter_manufacturer)
        filter_by(1, M_select_manufacturer_text, M_manufacturer_table, 0x0A, M_manufacturer_filter);
    else if (routine == M_filter_designer)
        filter_by(2, M_select_designer_text, M_designer_table, 0x1D, M_designer_filter);
    else if (routine == M_filter_year)
        filter_by(4, M_select_year_text, M_year_table, 0x16, M_year_filter);
}

/* the .016 file named at HISTORY:name into the four planes, 9600h bytes
 * each, through a DOS block (a short read copies what the block held
 * before); nothing when it is not there */
static void load_history_picture_16(uint16_t name)
{
    char path[SYS_PATH];
    uint8_t *data = NULL;
    size_t size = 0, pos = 0, k, i;
    uint16_t block = dos_alloc(0x961), a;
    uint8_t mask;

    if (!block)
        fatal("Not enough memory for the history.");
    cww(M_picture_block, block);
    if (!dos_path(s_history, name, path, sizeof path) && (data = sys_load(path, &size)) != NULL) {
        mask = 1;
        do {
            k = size - pos < 0x9600 ? size - pos : 0x9600;
            for (i = 0; i < k; i++)
                fwb(block, (uint16_t)i, data[pos + i]);
            pos += k;
            set_planes(mask, 0);
            for (a = 0; a < 0x9600; a++)
                vga_write(a, frb(block, a));
            mask = (uint8_t)(mask + mask);
        } while (k == 0x9600);
        free(data);
    }
    dos_free(block);
}

/* one string of the .HOP at x 28h (the right half), line y, inverted;
 * the NULs and CR/LF pairs after it skipped (the rows the program adds
 * for them are lost with its POP DX); the offset after them */
static uint16_t draw_history_line(uint16_t si, uint16_t y)
{
    uint16_t hop = hw(M_hop_segment);

    si = draw_history_string(hop, si, 0x28, y, 1);
    while (frb(hop, si) == 0)
        si++;
    while (frw(hop, si) == 0x0A0D)
        si = (uint16_t)(si + 2);
    return si;
}

/* the table's text: its .IDX record's strings from line 8 on, 35h rows at
 * most; the rest (scroll_active) at the next call for the same table */
static void draw_history_record(void)
{
    uint16_t sel = hw(M_selected_table), dx = 8, cx, si, idx;
    uint8_t cl;

    if (cw(M_drawn_table) != sel) {
        hww(M_scroll_active, 0);
        cww(M_drawn_table, sel);
    }
    if (hw(M_scroll_active)) {
        cx = hw(M_scroll_line_count);
        si = hw(M_scroll_text_ptr);
        do {
            si = draw_history_line(si, dx);
            dx = (uint16_t)(dx + 8);
        } while (--cx);
        hww(M_scroll_line_count, 0);
        hww(M_scroll_active, 0);
        hww(M_scroll_text_ptr, 0);
        return;
    }
    idx = (uint16_t)(M_history_text_index +
                     5 * hb((uint16_t)(M_filtered_table_records + 9 * sel)));
    cl = (uint8_t)(hb((uint16_t)(idx + 4)) + 1);
    cx = cl;
    hww(M_scroll_active, 0);
    if ((int8_t)cl > 0x35) {
        hww(M_scroll_line_count, (uint16_t)(cx - 0x35));
        cx = 0x35;
        hww(M_scroll_active, 1);
    }
    si = hw(idx);
    do {
        si = draw_history_line(si, dx);
        dx = (uint16_t)(dx + 8);
    } while (--cx);
    hww(M_scroll_text_ptr, si);
}

/* the selected table's first picture and its text, unless shown */
static void show_history_table(void)
{
    uint16_t sel = hw(M_selected_table);

    if (cw(M_shown_table) == sel)
        return;
    cww(M_shown_table, sel);
    mouse_hide();
    screen_off();
    load_history_picture_16(hw((uint16_t)(M_filtered_table_records + 9 * sel + 5)));
    draw_history_record();
    wait_retrace();
    screen_on();
    mouse_show();
}

/* the record's other picture, when it has two */
static void next_history_picture(void)
{
    uint16_t si = (uint16_t)(M_filtered_table_records + 9 * hw(M_selected_table));
    uint16_t n = (uint16_t)(cw(M_picture_number) + 1);

    cww(M_picture_number, n);
    if ((int16_t)n > (int16_t)hb((uint16_t)(si + 4)))
        cww(M_picture_number, 1);
    if (hb((uint16_t)(si + 4)) != 1) {
        mouse_hide();
        screen_off();
        load_history_picture_16(hw((uint16_t)(si + (cw(M_picture_number) == 1 ? 5 : 7))));
        draw_history_record();
        wait_retrace();
        screen_on();
    }
    mouse_show();
}

/* the browser until one of its buttons 0-4 (the lists, exit) or Esc (4) */
static uint16_t browse_history_tables(void)
{
    uint16_t ax, sel;

    menu_mode_12();                     /* set_history_mode: no VESA */
    mouse_range(0, 0x276, 0, 0x1D0);
    cww(M_picture_number, 1);
    dwb(M_mouse_buttons, 0);
    mouse_show();
    for (;;) {
        show_history_table();
        poll_history_mouse();
        for (;;) {
            checkpoint_menu("history_browse_loop");
            wait_retrace();
            poll_history_mouse();
            if (read_history_keys()) {
                mouse_hide();
                return 4;
            }
            if (!db(M_mouse_buttons))
                continue;
            ax = hit_test(M_history_menu_hitboxes);
            if (ax == 0xFF)
                continue;
            if (ax == 5) {
                next_history_picture();
                continue;
            }
            sel = hw(M_selected_table);
            if (ax == 6) {
                hww(M_selected_table, (uint16_t)(sel - 1));
                if ((int16_t)(sel - 1) < 0)
                    hww(M_selected_table, 0);
                break;
            }
            if (ax == 7) {
                hww(M_selected_table, (uint16_t)(sel + 1));
                if ((int16_t)(sel + 1) >= (int16_t)hw(M_filtered_table_count))
                    hww(M_selected_table, (uint16_t)(hw(M_filtered_table_count) - 1));
                break;
            }
            mouse_hide();
            return ax;
        }
    }
}

/* mode 12h, the .HOP; the start screen's choice, then the browser and the
 * lists its buttons choose until it is left; the font and the .HOP
 * released (not when the start screen's fifth box was chosen: then both
 * blocks stay taken, as in the program) */
static void run_history_menus(void)
{
    uint16_t block, ax;

    screen_off();
    clear_vram();
    menu_mode_12();
    block = dos_alloc(0xFF0);
    if (!block)
        fatal("Not enough memory for the history.");
    hww(M_hop_segment, block);
    if (read_file_far(s_history, hw((uint16_t)(M_hop_file_table + 2 * db(M_language_index))), block, 0)) {
        dos_free(block);
        fatal("A file of the history (the .HOP text) could not be read.");
    }
    ax = hw(M_history_start_choice);
    if (ax != 4) {
        history_action(ax);
        while ((ax = browse_history_tables()) != 4) {
            menu_mode_12();
            history_action(ax);
        }
        if (dos_free(hw(M_history_font_segment)) || dos_free(hw(M_hop_segment)))
            fatal("The history's memory could not be released.");
    }
    dwb(M_menu_unused_byte, 1);
}

/* the start screen until a box is clicked or Esc */
static void history_loop(void)
{
    uint16_t ax;

    poll_mouse();
    draw_pointer(2);
    for (;;) {
        checkpoint_menu("history_start_loop");
        wait_retrace();
        remove_pointer(2);
        poll_mouse();
        if (read_menu_keys())
            break;
        if (!db(M_mouse_buttons)) {
            draw_pointer(2);
            continue;
        }
        ax = hit_test(M_history_start_hitboxes);
        if (ax == 0xFF)
            continue;
        hww(M_history_start_choice, ax);
        fade_out(s_history, M_history_palette, 0x300);
        run_history_menus();
        return;
    }
    fade_out(s_history, M_history_palette, 0x300);
}

static void history_screen(void)
{
    uint16_t block;
    int i;

    /* history_init */
    hww(M_scroll_line_count, 0);
    hww(M_scroll_text_ptr, 0);
    hww(M_scroll_active, 0);
    dww(M_menu_start, 0);
    dwb(M_menu_unused_byte, 1);
    dos_dir = "HISTORY";                    /* CHDIR ..\history */
    screen_off();
    clear_vram();
    menu_mode_x();
    screen_off();
    load_vga_image(s_history, M_history_vga_path, 0);
    clear_palette(0x300);
    screen_on();
    fade_in(s_history, M_history_palette, 0x300);
    hwb(M_vesa_available, 0);               /* detect_vesa: INT 10h AX=4F00h fails */
    block = dos_alloc(0x80);                /* load_history_font */
    if (!block)
        fatal("Not enough memory for the history.");
    hww(M_history_font_segment, block);
    if (read_file_far(s_history, M_history_font_path, block, 0))
        fatal("A file of the history (HISTORY.FNT) could not be read.");
    if (read_file_far(s_history, hw((uint16_t)(M_idx_file_table + 2 * db(M_language_index))),
                      s_history, M_history_text_index))
        fatal("A file of the history (the .IDX index) could not be read.");
    /* select_history_picture_format: the .016 names stay without VESA */
    history_loop();

    /* leave_history: the DAC read and faded out (the screen is off) */
    screen_off();
    vga_outb(0x3C7, 0);
    for (i = 0; i < 0x300; i++)
        dwb((uint16_t)(M_palette_work + i), vga_inb(0x3C9));
    fade_out(s_data, M_palette_work, 0x300);
    clear_vram();
    dos_dir = NULL;                         /* CHDIR ..\deluxe */
    stop_sound();
    screen_off();
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

/* F1-F8: the table (its animation first); F9 the history after the
 * language screen; F10 (the options): not translated */
static void run_selection(void)
{
    uint8_t sel = db(M_selection);

    fade_out(s_data, M_select_palette, 0x300);
    stop_sound();
    dww(M_tick_draw, M_tick_nothing);
    dww(M_tick_remove, M_tick_nothing);
    dww(M_tick_ticker, M_tick_nothing);
    drv.host_off = 0;
    if (sel == 9) {
        if (!language_screen())
            history_screen();
        /* else CHDIR deluxe_dir: the folder did not change */
    } else if (sel != 0x0A) {
        restore_keyboard();
        run_table(sel - 1 >= 4 ? 1 : 0);
    }
    menu_init();
    drv.host_off = menu_box;
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
    menu_mode_x();
    screen_off();
    load_vga_image(s_data, M_select_vga_path, 0x3C0);
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
    mouse_range(0, 0x270, 0, 0xAC);
    mouse_set(0x140, 0x64);
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
    if (menu_box) {
        box_init();
        show_menu_start();
        dww(M_tick_draw, M_tick_nothing);
        dww(M_tick_remove, M_tick_nothing);
    } else {
        draw_pointer(1);
        dww(M_tick_draw, M_tick_draw_pointer);
        dww(M_tick_remove, M_tick_remove_pointer);
    }
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
    if (menu_box)
        box_scroll(1);
    show_menu_start();
}

/* the list, then the high score show, until Esc */
static void menu(void)
{
    int cx = 0x870, chosen;

    menu_init();
    drv.host_off = menu_box;
    poll_mouse();
    cww(M_menu_unused_word, 1);
    for (;;) {
        do {
            checkpoint_menu("menu_loop");
            show_menu_start();
            poll_mouse();
            if (menu_box) {
                if (box_keys(1))
                    return;
                if (box_moved)
                    cx = 0x870;
                box_pass();
            } else {
                if (read_menu_keys())
                    return;
                edge_scroll();
            }
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
            if (db(M_mouse_buttons) || box_back) {
                dwb(M_mouse_buttons, 0);
                box_back = 0;
                break;
            }
            if (any_key_down()) {
                if (menu_box ? box_keys(0) : read_menu_keys())
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
        frame_set_overlay(NULL);
        dos_dir = NULL;
        return r == 2 ? -1 : 0;
    }

    /* start: check_memory (mem_load_menu's arena is as after it) */
    install_keyboard();
    alloc_pointer_buffers();
    detect_mouse();
    frame_set_overlay(mouse_overlay);
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
    frame_set_overlay(NULL);
    return 0;
}
