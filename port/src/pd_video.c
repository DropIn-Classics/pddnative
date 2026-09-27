/* pd_video.c - the screen: the modes, the palette, the table picture in
 * video memory, scrolling.  PD.ASM's setup_screen, copy_to_vram,
 * scroll_step, set_screen_start and set_mode_x .. set_palette.  See pd.h.
 *
 * The table picture (320x512) sits in video memory from 780h on, 50h
 * bytes a line (Mode X: 4 pixels per address, one in each plane); the
 * display below the table is a split screen showing video memory from 0.
 * The CRTC's start address scrolls the table. */
#include "pd.h"
#include "vga.h"

static void crtc_set(uint8_t index, uint8_t and_mask, uint8_t or_mask)
{
    vga_outb(0x3D4, index);
    vga_outb(0x3D5, (uint8_t)((vga_inb(0x3D5) & and_mask) | or_mask));
}

/* INT 10h AH=12h BL=36h: the screen off (AL=1) or on (AL=0), which is
 * bit 5 of the sequencer's clocking mode register */
void screen_off(void)
{
    vga_outb(0x3C4, 1);
    vga_outb(0x3C5, (uint8_t)(vga_inb(0x3C5) | 0x20));
}

void screen_on(void)
{
    vga_outb(0x3C4, 1);
    vga_outb(0x3C5, (uint8_t)(vga_inb(0x3C5) & ~0x20));
}

/* DL = map mask, DH = read map */
void set_planes(uint8_t map_mask, uint8_t read_map)
{
    vga_outw(0x3C4, (uint16_t)(2 | map_mask << 8));
    vga_outw(0x3CE, (uint16_t)(4 | read_map << 8));
}

void write_mode_0(void)
{
    vga_outw(0x3CE, 0x0005);
}

void write_mode_1(void)
{
    set_planes(0x0F, 0);
    vga_outw(0x3CE, 0x0105);
}

/* mode 13h unchained, 320x200 (opt_screen 1) */
void set_mode_x(void)
{
    uint32_t a;

    vga_set_mode(0x13);
    vga_outb(0x3CE, 5);
    vga_outb(0x3CF, (uint8_t)(vga_inb(0x3CF) & 0xEF));
    vga_outb(0x3CE, 6);
    vga_outb(0x3CF, (uint8_t)(vga_inb(0x3CF) & 0xFD));
    vga_outb(0x3C4, 4);
    vga_outb(0x3C5, (uint8_t)((vga_inb(0x3C5) & 0xF7) | 4));
    for (a = 0; a < 0x10000; a++)
        vga_write((uint16_t)a, 0);
    crtc_set(0x14, 0xBF, 0);
    crtc_set(0x17, 0xFF, 0x40);
}

/* the tweaked mode for opt_screen 2: misc output A7h, the CRTC registers
 * at crtc_350 (in CODE): 320x350 */
void set_mode_350(void)
{
    int i;

    vga_set_mode(0x13);
    vga_outw(0x3C4, 0x0604);
    vga_outw(0x3C4, 0x0100);
    vga_outb(0x3C2, 0xA7);
    vga_outw(0x3C4, 0x0300);
    crtc_set(0x11, 0x07, 0);
    for (i = 0; i < 0x10; i++)
        vga_outw(0x3D4, frw(seg_code, (uint16_t)(V(crtc_350) + 2 * i)));
    vga_outb(0x3C0, 0x30);
    vga_outb(0x3C0, (uint8_t)(vga_inb(0x3C1) | 0x20));
}

/* `count` zeros to the DAC from colour 0, after a frame */
void black_palette(int count)
{
    wait_frame();
    vga_outb(0x3C8, 0);
    while (count--)
        vga_outb(0x3C9, 0);
}

/* the line compare register: the display below the table as a split
 * screen (160h, or 146h in the 350-line mode) */
void set_split(void)
{
    uint16_t line = rb(V(opt_screen)) == 2 ? 0x146 : 0x160;

    wait_frame();
    vga_outw(0x3D4, (uint16_t)(0x18 | (line & 0xFF) << 8));
    crtc_set(0x07, 0xEF, (uint8_t)((line >> 8 & 1) << 4));
    crtc_set(0x09, 0xBF, (uint8_t)((line >> 9 & 1) << 6));
}

void clear_vram(void)
{
    int p;
    uint32_t a;

    for (p = 0; p < 4; p++) {
        set_planes((uint8_t)(1 << p), (uint8_t)p);
        for (a = 0; a < 0x10000; a++)
            vga_write((uint16_t)a, 0);
    }
}

void wait_frames(int n)
{
    while (n-- > 0)
        wait_frame();
}

/* until the driver's tick sets timer_tick, or (no driver) the next
 * vertical retrace: either is one picture here */
void wait_frame(void)
{
    if (rb(V(sound_on)) != 0) {
        wb(V(timer_tick), 0);
        while (!rb(V(timer_tick)))
            pump_frame();
    } else {
        pump_frame();
    }
}

/* the table's palette (seg:si): PD.EXE sends the table's 40h colours and
 * the lights' after them, black up to 80h, the table's 40h colours twice
 * more (80h..FFh, where the ball's level bit is set) and the display's two
 * (FEh, FFh; grey with opt_palette 1); PD2.EXE sends all 256 as they are */
void set_palette(uint16_t seg, uint16_t si)
{
    int i, n = 0x40 + rw(V(light_count));

    wait_frame();
    vga_outb(0x3C8, 0);
    if (prog_id == 2) {
        for (i = 0; i < 0x300; i++)
            vga_outb(0x3C9, frb(seg, (uint16_t)(si + i)));
        return;
    }
    for (i = 0; i < 3 * n; i++)
        vga_outb(0x3C9, frb(seg, (uint16_t)(si + i)));
    for (i = 0; i < 3 * (0x80 - n); i++)
        vga_outb(0x3C9, 0);
    for (i = 0; i < 2 * 0xC0; i++)
        vga_outb(0x3C9, frb(seg, (uint16_t)(si + i % 0xC0)));
    vga_outb(0x3C8, 0xFE);
    if (rb(V(opt_palette)) == 1) {
        static const uint8_t grey[6] = {0x10, 0x10, 0x10, 0x3F, 0x3F, 0x3F};
        for (i = 0; i < 6; i++)
            vga_outb(0x3C9, grey[i]);
    } else {
        static const uint8_t colours[6] = {0x14, 0x0A, 0x05, 0x3C, 0x1E, 0x0F};
        for (i = 0; i < 6; i++)
            vga_outb(0x3C9, colours[i]);
    }
}

/* light_palette to the DAC from colour 40h; the frame callback once the
 * game runs */
void upload_lights(void)
{
    int i, n = 3 * rw(V(light_count));

    vga_outb(0x3C8, 0x40);
    for (i = 0; i < n; i++)
        vga_outb(0x3C9, rb((uint16_t)(V(light_palette) + i)));
}

/* `count` bytes of a picture (4 planes interleaved, at seg:0) into video
 * memory at di, plane by plane */
void copy_to_vram(uint16_t di, uint16_t seg, uint16_t count)
{
    int p;
    uint16_t i;

    for (p = 0; p < 4; p++) {
        uint32_t a = lin(seg, 0) + (uint32_t)p;
        set_planes((uint8_t)(1 << p), 0);
        for (i = 0; i < count; i++, a += 4)
            vga_write((uint16_t)(di + i), mem[a & (MEM_SIZE - 1)]);
    }
}

/* the CRTC start address [screen_start], then wait_frame */
void set_screen_start(void)
{
    uint16_t start = rw(V(screen_start));
    vga_outw(0x3D4, (uint16_t)(0x0C | (start & 0xFF00)));
    vga_outw(0x3D4, (uint16_t)(0x0D | (start & 0xFF) << 8));
    wait_frame();
}

/* the start address from scroll (its line in bits 13..21), shaken by a
 * line while the table is nudged (L14BB, where other routines jump in) */
void scroll_redraw(void)
{
    uint16_t line = (uint16_t)((rd(V(scroll)) << 3) >> 16) & 0x1FF;
    uint16_t start = (uint16_t)(line * 0x50 + 0x780);

    if (rb(V(nudge_phase)) == 1)
        start = (uint16_t)(start + 0x50);
    else if (rb(V(nudge_phase)) == 2)
        start = (uint16_t)(start - 0x50);
    ww(V(screen_start), start);
    set_screen_start();
}

/* scroll += scroll_speed, kept within the table; the CRTC start address */
void scroll_step(void)
{
    uint32_t s = rd(V(scroll)) + rd(V(scroll_speed));

    wd(V(scroll), s);
    if (s & 0x80000000u) {
        wd(V(scroll), 0);
    } else if (rb(V(opt_screen)) != 2) {
        if ((s >> 16) >= 0x2A)
            wd(V(scroll), 0x0029E000u);
    } else if ((s >> 16) >= 0x17) {
        wd(V(scroll), 0x00172000u);
    }
    scroll_redraw();
}

/* the screen mode (opt_screen), the table picture into video memory,
 * FLIPPERS.SPR loaded and made into sprite shapes, the first picture */
void setup_screen(void)
{
    uint16_t seg;

    write_mode_0();
    if (prog_id == 1) {
        black_palette(0x300);
        clear_vram();
        screen_off();
        wait_frame();
    }
    if (rb(V(opt_screen)) == 2)
        set_mode_350();
    else
        set_mode_x();
    clear_vram();
    black_palette(0x300);
    set_split();
    copy_to_vram(0x780, rw(V(table_pic_seg)), 0xA000);
    seg = dos_alloc(prog_id == 1 ? 0x36C : 0x3E9);
    if (!seg)
        pd_fatal("No memory for the flipper sprites.");
    ww(V(sprite_seg), seg);
    if (load_file(V(name_flippers), seg, 0))
        pd_fatal("FLIPPERS.SPR could not be loaded.");
    wb(V(sprites), 2);
    wb((uint16_t)(V(sprites) + 0x20), 3);
    wb((uint16_t)(V(sprites) + 0x40), 3);
    wb((uint16_t)(V(sprites) + 0x60), 3);
    make_sprite_shapes(seg, 0, prog_id == 1 ? 0x2EE0 : 0x32C8);
    set_sprite_frame(0, 0x20);
    set_sprite_frame(5, 0x40);
    show_text(V(txt_blank));
    wd(V(scroll_speed), 0);
    wd(V(scroll), 0x00320000u);
    scroll_step();
    draw_sprites();
    if (dos_free(rw(V(table_pic_seg))) || dos_free(rw(V(sprite_seg))))
        pd_fatal("A memory block could not be freed.");
}
