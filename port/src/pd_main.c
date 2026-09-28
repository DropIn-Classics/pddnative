/* pd_main.c - start-up, the main loop, the game states and the table's
 * set-up (PD.ASM from `start` to `st_exit`).  See pd.h. */
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "frame.h"
#include "pad.h"
#include "pd.h"
#include "vga.h"

const char *pd_game_dir;
int pd_qol;

int qol_frames(int fix, int original, int shorter)
{
    return pd_qol & fix ? shorter : original;
}

static jmp_buf exit_jmp;
static char *exit_err;
static size_t exit_err_n;

void pd_exit(void)
{
    longjmp(exit_jmp, 1);
}

void pd_fatal(const char *what)
{
    if (exit_err)
        snprintf(exit_err, exit_err_n, "%s", what);
    longjmp(exit_jmp, 2);
}

void pump_frame(void)
{
    if (!frame_wait())
        pd_exit();
}

/* PD_POKE="where#N OFF HEX[;...]": the Nth time the checkpoint `where` is
 * passed, the bytes HEX are written at DATA:OFF (tools/run's -poke at the
 * same place) */
#define POKE_MAX 64
static struct { char where[32]; unsigned long n, count; uint16_t off; uint8_t b[16]; int len; } pokes[POKE_MAX];
static int npokes;

static void parse_pokes(void)
{
    const char *s = getenv("PD_POKE");

    while (s && *s) {
        char where[32], hex[40];
        unsigned off;
        int k, used = 0;
        const char *h;
        if (npokes == POKE_MAX) {
            fprintf(stderr, "PD_POKE: at most %d\n", POKE_MAX);
            exit(1);
        }
        if (sscanf(s, " %31[^#]#%lu %x %39[0-9A-Fa-f ]%n", where, &pokes[npokes].n, &off, hex, &used) < 4)
            break;
        strcpy(pokes[npokes].where, where);
        pokes[npokes].off = (uint16_t)off;
        for (h = hex, k = 0; *h && k < 16; ) {
            unsigned v;
            if (*h == ' ') { h++; continue; }
            if (sscanf(h, "%2x", &v) != 1)
                break;
            pokes[npokes].b[k++] = (uint8_t)v;
            h += 2;
        }
        pokes[npokes++].len = k;
        s += used;
        s = strchr(s, ';');
        if (s)
            s++;
    }
}

/* PD_STOP=where#N: the program ends the Nth time it passes the checkpoint
 * `where` (for comparing its memory with a run of the original stopped at
 * the same place with tools/run's -break ADDR#N); PD_TRACE: each
 * checkpoint passed is printed with the number of pictures shown;
 * PD_TRACE_BALL: at each frame of play the ball's place and speed */
void checkpoint(const char *where)
{
    static const char *stop;
    static unsigned long count, want;
    static size_t len;
    static int parsed;
    int i, k;

    if (!parsed) {
        const char *hash;
        parsed = 1;
        stop = getenv("PD_STOP");
        if (stop) {
            hash = strchr(stop, '#');
            len = hash ? (size_t)(hash - stop) : strlen(stop);
            want = hash ? strtoul(hash + 1, NULL, 10) : 1;
        }
        parse_pokes();
    }
    if (getenv("PD_TRACE"))
        fprintf(stderr, "%s picture %lu\n", where, frame_count());
    if (getenv("PD_TRACE_BALL") && !strcmp(where, "st_play"))   /* the ball's pixel and speed */
        fprintf(stderr, "ball %d %d %d %d\n", (int16_t)rw(V(ball_x_hi)) >> 2, (int16_t)rw(V(ball_y_hi)) >> 2,
                (int16_t)rw(V(ball_vx)), (int16_t)rw(V(ball_vy)));
    for (i = 0; i < npokes; i++)
        if (!strcmp(where, pokes[i].where) && ++pokes[i].count == pokes[i].n)
            for (k = 0; k < pokes[i].len; k++)
                wb((uint16_t)(pokes[i].off + k), pokes[i].b[k]);
    if (stop && strlen(where) == len && !strncmp(where, stop, len) && ++count == want)
        pd_exit();
}

void trace_note(const char *what)
{
    if (getenv("PD_TRACE"))
        fprintf(stderr, "note %s picture %lu\n", what, frame_count());
}

void not_ported(const char *name)
{
    char msg[200];
    snprintf(msg, sizeof msg, "%s is not ported yet.", name);
    pd_fatal(msg);
}

/* the four keys of the options (key_map's byte, then the bit as a mask)
 * for the controller's buttons (pad.h); not the original's */
static void tell_pad_keys(void)
{
    int codes[4], k, bit;

    for (k = 0; k < 4; k++) {
        uint16_t w = rw((uint16_t)(V(key_lflipper) + 2 * k));
        for (bit = 0; bit < 8 && (w >> 8) != 1 << bit; bit++)
            ;
        if (bit == 8 || (w & 0xFF) >= 0x20)
            return;                     /* not a key: the buttons keep theirs */
        codes[k] = (w & 0xFF) * 8 + bit;
    }
    pad_set_game_keys(codes);
}

/* ---- start: memory, the table number, mode 13h, INT 9, options, high scores */

int pd_run(int table, const char *game_dir, char *err, size_t n)
{
    int r;
    uint16_t a;

    pd_game_dir = game_dir;
    exit_err = err;
    exit_err_n = n;
    if (n)
        err[0] = 0;
    r = setjmp(exit_jmp);
    if (r) {
        frame_set_tick(NULL);
        frame_set_keyboard(NULL);
        return r == 1 ? 0 : -1;
    }

    /* INT 21h 4Ah (keep 1A40h paragraphs): mem.c's arena starts there.
     * parse_cmdline: the table is the argument. */
    for (a = V(cleared_vars); a != V(sprite_seg); a++)
        wb(a, 0);
    vga_set_mode(0x13);
    frame_set_keyboard(kbd_int);                    /* hook_int9 */
    wb(V(tune_request), 0xFF);
    wb(V(sound_request), 0xFF);
    wb(V(jingle_request), 0xFF);
    wb(V(table_num), (uint8_t)table);
    ww(V(frame_callback), V(no_callback));
    wb(V(sprites_in_irq), 0);
    load_hiscores();
    wait_keys_up();
    ww(V(game_state), 1);
    load_options();
    tell_pad_keys();

    /* main_loop: the routine of the game state, for ever */
    for (;;)
        call_code(rw((uint16_t)(V(state_table) + 2 * (rw(V(game_state)) & 0x0F))));
}

/* ---- state 1: loads the table and the sound driver, then state 2 */

void st_load(void)
{
    screen_off();
    load_table_files();
    table_setup();
    init_lights();
    index_flipper_shapes();
    setup_screen();
    screen_off();
    format_hiscores();
    wb(V(tune_request), 0);
    ww(V(game_state), 2);
    ww(V(frame_callback), V(upload_lights));
    if (load_sound_driver() || start_music())
        pd_fatal("The sound could not be started.");
    set_palette(seg_tdata, rw(V(table_palette)));
    screen_on();
    ww(V(unused_845C), 0x5208);
}

/* states 11, 13-15: state 1 again */
void st_restart(void)
{
    ww(V(game_state), 1);
}

/* state 0: saves the high scores, then state 10 */
void st_quit(void)
{
    save_hiscores();
    ww(V(game_state), 10);
}

/* state 10: INT 9 back, palette black, video memory cleared, exit to DOS */
void st_exit(void)
{
    frame_set_keyboard(NULL);                       /* unhook_int9 */
    black_palette(0x300);
    clear_vram();
    pd_exit();
}

/* ---- the table's files and set-up */

/* TBLDETLO.xxx (collision map, lower level) to BSS:0960, TBLDETHI.xxx
 * (upper level; PD.EXE has none for Ignition) to BSS:47E0, TABLE2M.xxx (the picture)
 * to a new block ([table_pic_seg]) */
void load_table_files(void)
{
    uint16_t ext = (uint16_t)(V(table_exts) + 3 * rb(V(table_num)));
    uint16_t di = (uint16_t)(V(name_tbldetlo) + 9), seg;
    int i;

    for (i = 0; i < 3; i++, di = (uint16_t)(di + 0x0D)) {
        ww(di, rw(ext));
        wb((uint16_t)(di + 2), rb((uint16_t)(ext + 2)));
    }
    if (load_file(V(name_tbldetlo), seg_bss, 0x0960))
        pd_fatal("TBLDETLO could not be loaded.");
    if ((prog_id == 2 || rb(V(table_num)) != 0) && load_file(V(name_tbldethi), seg_bss, 0x47E0))
        pd_fatal("TBLDETHI could not be loaded.");
    seg = dos_alloc(0x2801);
    if (!seg)
        pd_fatal("No memory for the table picture.");
    ww(V(table_pic_seg), seg);
    if (load_file(V(name_table2m), seg, 0))
        pd_fatal("TABLE2M could not be loaded.");
}

/* the ball, plunger and two unused object records, the table's own
 * set-up, the flipper sprites' first frames */
void table_setup(void)
{
    uint16_t si = V(plunger_obj), bx;

    ww(V(unused_obj1), 0x81);
    ww(V(unused_obj2), 0x81);
    ww(V(ball_obj), 1);
    ww(si, 1);                                      /* PD2.EXE's plunger differs */
    ww((uint16_t)(si + 2), prog_id == 1 ? 0 : 0x8000);
    ww((uint16_t)(si + 4), prog_id == 1 ? 0x26 : 0x25);
    ww((uint16_t)(si + 6), 0x2000);
    ww((uint16_t)(si + 8), prog_id == 1 ? 0x3D : 0x3C);
    if (prog_id == 1)
        pd1_setup_table(rb(V(table_num)));
    else
        pd2_setup_table(rb(V(table_num)));
    /* the flippers (sprites 1 and 2) at their first frames' places */
    bx = rw(V(sprite_frames));
    ww((uint16_t)(V(sprites) + 0x22), rw(bx));
    ww((uint16_t)(V(sprites) + 0x24), rw((uint16_t)(bx + 4)));
    ww((uint16_t)(V(sprites) + 0x42), rw((uint16_t)(bx + 0x32)));
    ww((uint16_t)(V(sprites) + 0x44), rw((uint16_t)(bx + 0x36)));
}

/* each shape record of flipper_shape_data: x := [flipper_x], y from the
 * list `ys` (a word per frame) */
void place_flipper_shapes(uint16_t ys)
{
    uint16_t es = rw((uint16_t)(V(flipper_shape_data) + 2));
    uint16_t di = rw(V(flipper_shape_data));

    while (frw(es, di) != 0) {
        fww(es, (uint16_t)(di + 2), rw(V(flipper_x)));
        fww(es, (uint16_t)(di + 4), rw(ys));
        di = (uint16_t)(di + frw(es, di));
        ys = (uint16_t)(ys + 2);
    }
}

/* the 17 shape pointers (flipper_shapes) into flipper_shape_data (whose
 * far pointer points into DATA: the records are read with DS) */
void index_flipper_shapes(void)
{
    uint16_t bx = rw(V(flipper_shape_data)), di = V(flipper_shapes);
    int i;

    for (i = 0; i < 0x11; i++, di = (uint16_t)(di + 2)) {
        uint16_t size = rw(bx);
        ww(di, (uint16_t)(bx + 2));
        bx = (uint16_t)(bx + size);
    }
}

/* gives each light record its palette slot (+22h) and copies the lights'
 * "off" colours into light_palette */
void init_lights(void)
{
    uint16_t di = V(lights), count = rw(V(light_count)), i;
    uint16_t si = rw(V(lights_off_pal)), dst = V(light_palette);

    for (i = 0; i < count; i++, di = (uint16_t)(di + 0x26)) {
        ww(di, 0);
        ww((uint16_t)(di + 0x22), i);
    }
    for (i = 0; i < 3 * count; i++)
        wb((uint16_t)(dst + i), frb(seg_tdata, (uint16_t)(si + i)));
}

/* one high score (3 initials, 6 bytes BCD) as a text: the initials, two
 * characters left as they are, the score without leading zeros (a last
 * digit 0 is always written) */
static uint16_t format_hiscore(uint16_t si, uint16_t di)
{
    int i, lead = 0;

    for (i = 0; i < 3; i++)
        wb(di++, rb(si++));
    di = (uint16_t)(di + 2);
    for (i = 0; i < 6; i++, si++) {
        uint8_t hi = rb(si) >> 4, lo = rb(si) & 0x0F;
        lead |= hi;
        wb(di++, lead ? (uint8_t)(hi + '0') : ' ');
        lead |= lo;
        wb(di++, lead ? (uint8_t)(lo + '0') : ' ');
    }
    if ((rb((uint16_t)(si - 1)) & 0x0F) == 0)
        wb((uint16_t)(di - 1), '0');
    return si;
}

/* the table's four high scores into txt_hiscore1..4 */
void format_hiscores(void)
{
    uint16_t si = (uint16_t)(V(hiscores) + rb(V(table_num)) * 0x24);

    si = format_hiscore(si, V(txt_hiscore1));
    si = format_hiscore(si, V(txt_hiscore2));
    si = format_hiscore(si, V(txt_hiscore3));
    format_hiscore(si, V(txt_hiscore4));
}

/* CX RGB triples at TDATA:SI into grey (30/59/11) */
static void to_grey(uint16_t si, int count)
{
    static const uint8_t weight[3] = {30, 59, 11};
    uint8_t c[3];
    int i, k;

    for (i = 0; i < count; i++, si = (uint16_t)(si + 3)) {
        for (k = 0; k < 3; k++) {
            uint8_t q = (uint8_t)((frb(seg_tdata, (uint16_t)(si + k)) << 6) / 100);
            c[k] = (uint8_t)((q * weight[k]) >> 6);
        }
        for (k = 0; k < 3; k++)
            fwb(seg_tdata, (uint16_t)(si + k), (uint8_t)(c[0] + c[1] + c[2]));
    }
}

/* opt_palette 1: the table's and the lights' palettes made grey */
void grey_palettes(void)
{
    int count = rw(V(light_count));

    if (rb(V(opt_palette)) != 1)
        return;
    to_grey(rw(V(lights_on_pal)), count);
    to_grey(rw(V(lights_off_pal)), count);
    to_grey(rw(V(table_palette)), 0x40);
}

/* does nothing (called by each table's set-up; see machine_check) */
void table_check(void)
{
}

void no_callback(void)
{
}
