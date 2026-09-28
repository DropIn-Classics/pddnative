/* pd_sprite.c - the four sprites (ball, left and right flipper, plunger):
 * PD.ASM from draw_sprites to make_sprite_shape.  See pd.h.
 *
 * A sprite record (sprites, 20h bytes each; hints comment at DATA:7D00):
 * +0 flags (1 shown, 2 changed, 4 drawn, 40h/80h draw_sprites' own), +1 the
 * ball's level, +2 x, +4 y, +6 width, +8 height, +0Ah where its background
 * is saved (video memory), +0Ch/+0Eh its shape (offset, segment; segment 0
 * = in video memory), +10h..+16h where it was drawn (column, line,
 * columns, lines), +18h..+1Eh the area to redraw (columns x1, x2, lines
 * y1, y2).
 *
 * Video memory is reached at [vram_seg], which holds A000h. */
#include "pd.h"
#include "vga.h"

enum {
    SP_FLAGS = 0, SP_LEVEL = 1, SP_X = 2, SP_Y = 4, SP_W = 6, SP_H = 8, SP_BG = 0x0A,
    SP_SHAPE = 0x0C, SP_SHAPE_SEG = 0x0E, SP_OLD_COL = 0x10, SP_OLD_Y = 0x12,
    SP_OLD_COLS = 0x14, SP_OLD_LINES = 0x16, SP_X1 = 0x18, SP_X2 = 0x1A, SP_Y1 = 0x1C, SP_Y2 = 0x1E,
    SPRITE_SIZE = 0x20
};

#define AREA_BUFFER 0xBB10u     /* where draw_sprites works, in video memory */
#define SHAPES 0xA820u          /* the flipper shapes in video memory, 180h a frame */

static uint16_t sp(uint16_t s, int field) { return (uint16_t)(V(sprites) + s + field); }
static int16_t sw(uint16_t a) { return (int16_t)rw(a); }

/* write mode (graphics controller register 5, the low two bits) */
static void write_mode(int mode)
{
    vga_outb(0x3CE, 5);
    vga_outb(0x3CF, (uint8_t)((vga_inb(0x3CF) & 0xFC) | mode));
}

/* `rows` rows of `cols` bytes, from `src` to `dst` in video memory, as REP
 * MOVSB does it (in write mode 1: all four planes through the latches) */
static void vram_copy_rows(uint16_t src, int src_step, uint16_t dst, int dst_step, int cols, int rows)
{
    int i;
    for (; rows > 0; rows--) {
        for (i = 0; i < cols; i++)
            vga_write(dst++, vga_read(src++));
        src = (uint16_t)(src + src_step);
        dst = (uint16_t)(dst + dst_step);
    }
}

/* the sprites of an area: those that overlap the first one's, merged, the
 * area's bounds into area_col..area_lines; returns the last list entry */
static uint16_t sprite_area(uint16_t s0)
{
    int16_t x1, x2, y1, y2;
    uint16_t di = V(sprite_list), s;

    wb(sp(s0, SP_FLAGS), (uint8_t)(rb(sp(s0, SP_FLAGS)) | 0x40));
    x1 = sw(sp(s0, SP_X1));
    x2 = sw(sp(s0, SP_X2));
    y1 = sw(sp(s0, SP_Y1));
    y2 = sw(sp(s0, SP_Y2));
    ww(di, s0);
    for (s = 0; s < 4 * SPRITE_SIZE;) {
        if ((rb(sp(s, SP_FLAGS)) & 0x40) || (int16_t)(sw(sp(s, SP_X1)) - x2) >= 0 ||
            (int16_t)(x1 - sw(sp(s, SP_X2))) >= 0 || (int16_t)(sw(sp(s, SP_Y1)) - y2) >= 0 ||
            (int16_t)(y1 - sw(sp(s, SP_Y2))) >= 0) {
            s = (uint16_t)(s + SPRITE_SIZE);
            continue;
        }
        di = (uint16_t)(di + 2);
        ww(di, s);
        wb(sp(s, SP_FLAGS), (uint8_t)(rb(sp(s, SP_FLAGS)) | 0x40));
        if ((int16_t)(sw(sp(s, SP_X1)) - x1) < 0)
            x1 = sw(sp(s, SP_X1));
        if ((int16_t)(x2 - sw(sp(s, SP_X2))) < 0)
            x2 = sw(sp(s, SP_X2));
        if ((int16_t)(sw(sp(s, SP_Y1)) - y1) < 0)
            y1 = sw(sp(s, SP_Y1));
        if ((int16_t)(y2 - sw(sp(s, SP_Y2))) < 0)
            y2 = sw(sp(s, SP_Y2));
        s = 0;                                  /* and again from the first */
    }
    ww(V(area_col), (uint16_t)x1);
    ww(V(area_y), (uint16_t)y1);
    ww(V(area_cols), (uint16_t)(x2 - x1));
    ww(V(area_lines), (uint16_t)(y2 - y1));
    return di;
}

/* the list sorted downwards (bubble sort) */
static void sort_sprite_list(void)
{
    uint16_t last = (uint16_t)(rw(V(sprite_list_end)) - 2), si;
    int swapped;

    do {
        swapped = 0;
        for (si = V(sprite_list); si < last; si = (uint16_t)(si + 2)) {
            uint16_t a = rw(si), b = rw((uint16_t)(si + 2));
            if (a < b) {
                ww(si, b);
                ww((uint16_t)(si + 2), a);
                swapped = 1;
            }
        }
    } while (swapped);
}

/* the area from the screen into video memory BB10h..; 1 when nothing of
 * it is on the table (it starts below or ends above) */
static int grab_area(void)
{
    uint16_t y = rw(V(area_y)), lines = rw(V(area_lines)), off;
    int16_t below;

    vga_outw(0x3C4, 0x0F02);
    write_mode(1);
    if (y & 0x8000)
        return 1;
    off = (uint16_t)(y * 0x50 + 0x780);
    ww(V(area_offset), off);
    below = (int16_t)(y + lines - 0x200);
    if (below >= 0) {
        if (lines <= (uint16_t)below)
            return 1;
        lines = (uint16_t)(lines - below);
    }
    ww(V(area_rows), lines);
    vram_copy_rows((uint16_t)(off + rw(V(area_col))), 0x50 - rw(V(area_cols)), AREA_BUFFER, 0,
                   rw(V(area_cols)), lines);
    return 0;
}

/* the sprites' old backgrounds put back into the area */
static void restore_backgrounds(void)
{
    uint16_t e;

    for (e = V(sprite_list); e < rw(V(sprite_list_end)); e = (uint16_t)(e + 2)) {
        uint16_t s = rw(e), di;
        int16_t dy, dx;
        if (!(rb(sp(s, SP_FLAGS)) & 4))
            continue;
        dy = (int16_t)(rw(sp(s, SP_OLD_Y)) - rw(V(area_y)));
        if (dy < 0)
            continue;
        di = (uint16_t)(dy * rw(V(area_cols)) + AREA_BUFFER);
        dx = (int16_t)(rw(sp(s, SP_OLD_COL)) - rw(V(area_col)));
        if (dx < 0)
            continue;
        vram_copy_rows(rw(sp(s, SP_BG)), 0, (uint16_t)(di + dx),
                       rw(V(area_cols)) - rw(sp(s, SP_OLD_COLS)), rw(sp(s, SP_OLD_COLS)),
                       rw(sp(s, SP_OLD_LINES)));
    }
}

/* the sprites' new backgrounds saved from the area; flag 4 := flag 1 */
static void save_backgrounds(void)
{
    uint16_t e;

    for (e = V(sprite_list); e < rw(V(sprite_list_end)); e = (uint16_t)(e + 2)) {
        uint16_t s = rw(e), ax;
        uint8_t old = rb(sp(s, SP_FLAGS)), flags = old & 0xF9;
        int16_t col, dy, dx;

        if (flags & 1)
            flags |= 4;
        wb(sp(s, SP_FLAGS), flags);
        if (!(old & 2))
            continue;
        col = (int16_t)(sw(sp(s, SP_X)) >> 2);
        ax = (uint16_t)((int16_t)(rw(sp(s, SP_W)) + rw(sp(s, SP_X)) + 3) >> 2);
        ww(sp(s, SP_OLD_COL), (uint16_t)col);
        ww(sp(s, SP_OLD_COLS), (uint16_t)(ax - col));
        ww(sp(s, SP_OLD_LINES), rw(sp(s, SP_H)));
        ww(sp(s, SP_OLD_Y), rw(sp(s, SP_Y)));
        dy = (int16_t)(rw(sp(s, SP_Y)) - rw(V(area_y)));
        if (dy < 0)
            continue;
        dx = (int16_t)(col - rw(V(area_col)));
        if (dx < 0)
            continue;
        vram_copy_rows((uint16_t)(dy * rw(V(area_cols)) + AREA_BUFFER + dx),
                       rw(V(area_cols)) - rw(sp(s, SP_OLD_COLS)), rw(sp(s, SP_BG)), 0,
                       rw(sp(s, SP_OLD_COLS)), rw(sp(s, SP_H)));
    }
}

/* the sprites drawn into the area.  Shapes in video memory (the flippers)
 * are copied through the latches, all planes at once; the others (the
 * ball) plane by plane, a pixel only where the area's colour is below the
 * sprite's level byte, so the ball on the lower level passes behind colours
 * 80h and up */
static void draw_sprite_shapes(void)
{
    uint16_t e;

    for (e = V(sprite_list); e < rw(V(sprite_list_end)); e = (uint16_t)(e + 2)) {
        uint16_t s = rw(e), cols = rw(V(area_cols)), di, si, seg;
        int16_t dy, dx;
        uint8_t level, plane, mask;
        int p, row;

        if (!(rb(sp(s, SP_FLAGS)) & 4))
            continue;
        dy = (int16_t)(rw(sp(s, SP_Y)) - rw(V(area_y)));
        if (dy < 0)
            continue;
        dx = (int16_t)((sw(sp(s, SP_X)) >> 2) - rw(V(area_col)));
        if (dx < 0)
            continue;
        di = (uint16_t)(dy * cols + AREA_BUFFER + dx);
        seg = rw(sp(s, SP_SHAPE_SEG));
        level = rb(sp(s, SP_LEVEL));
        si = rw(sp(s, SP_SHAPE));
        if (seg == 0) {
            int height;
            vga_outw(0x3C4, 0x0F02);
            write_mode(1);
            height = vga_read(si++);
            for (row = 0; row < height; row++, di = (uint16_t)(di + cols)) {
                uint8_t skip = vga_read(si), count = vga_read((uint16_t)(si + 1));
                uint16_t d = (uint16_t)(di + skip);
                si = (uint16_t)(si + 2);
                while (count--)
                    vga_write(d++, vga_read(si++));
            }
            continue;
        }
        write_mode(0);
        plane = rb(sp(s, SP_X)) & 3;
        mask = (uint8_t)(1 << plane);
        for (p = 0; p < 4; p++) {
            int height;
            vga_outw(0x3C4, (uint16_t)(2 | mask << 8));
            vga_outw(0x3CE, (uint16_t)(4 | plane << 8));
            height = frb(seg, si++);
            for (row = 0; row < height; row++) {
                uint8_t skip = frb(seg, si), count = frb(seg, (uint16_t)(si + 1));
                uint16_t d = (uint16_t)(di + row * cols + skip);
                si = (uint16_t)(si + 2);
                for (; count; count--, d++) {
                    uint8_t c = frb(seg, si++);
                    if (vga_read(d) < level)
                        vga_write(d, c);
                }
            }
            mask = (uint8_t)(mask << 1);
            plane = (plane + 1) & 3;
            if (plane == 0) {
                di++;
                mask = 1;
            }
        }
    }
}

/* the area back to the screen */
static void put_area(void)
{
    vga_outw(0x3C4, 0x0F02);
    write_mode(1);
    vram_copy_rows(AREA_BUFFER, 0, (uint16_t)(rw(V(area_offset)) + rw(V(area_col))),
                   0x50 - rw(V(area_cols)), rw(V(area_cols)), rw(V(area_rows)));
}

/* the four sprites: overlapping areas merged, then per area: grab the
 * screen into a buffer (video memory BB10h), put back the old backgrounds,
 * save the new ones, draw, copy back.  Returns the areas drawn. */
int draw_sprites(void)
{
    uint16_t s;
    int drawn = 0;

    for (s = 0; s < 4 * SPRITE_SIZE; s = (uint16_t)(s + SPRITE_SIZE)) {
        uint8_t fl = rb(sp(s, SP_FLAGS));
        int16_t x1, x2, y1, y2;
        int off = 0;

        if (!(fl & 5) || !(fl & 6)) {
            off = 1;
        } else {
            x1 = (int16_t)(sw(sp(s, SP_X)) >> 2);
            x2 = (int16_t)((int16_t)(rw(sp(s, SP_W)) + rw(sp(s, SP_X)) + 3) >> 2);
            if (fl & 4) {
                int16_t c = sw(sp(s, SP_OLD_COL));
                if ((int16_t)(c - x1) < 0)
                    x1 = c;
                c = (int16_t)(c + rw(sp(s, SP_OLD_COLS)));
                if ((int16_t)(x2 - c) < 0)
                    x2 = c;
            }
            if (x2 < 0 || (int16_t)(x1 - 0x50) >= 0) {
                off = 1;
            } else {
                ww(sp(s, SP_X1), (uint16_t)x1);
                ww(sp(s, SP_X2), (uint16_t)x2);
                y1 = sw(sp(s, SP_Y));
                y2 = (int16_t)(rw(sp(s, SP_H)) + y1);
                if (fl & 4) {
                    int16_t l = sw(sp(s, SP_OLD_Y));
                    if ((int16_t)(l - y1) < 0)
                        y1 = l;
                    l = (int16_t)(l + rw(sp(s, SP_OLD_LINES)));
                    if ((int16_t)(y2 - l) < 0)
                        y2 = l;
                }
                if (y2 < 0 || (int16_t)(y1 - 0x200) >= 0) {
                    off = 1;
                } else {
                    fl &= 0x3F;
                    if (!(fl & 2))
                        fl |= 0x80;
                    ww(sp(s, SP_Y1), (uint16_t)y1);
                    ww(sp(s, SP_Y2), (uint16_t)y2);
                }
            }
        }
        if (off) {
            fl = (uint8_t)((fl & 0x79) | 0x40);
            ww(sp(s, SP_X1), 0);
            ww(sp(s, SP_X2), 0);
            ww(sp(s, SP_Y1), 0);
            ww(sp(s, SP_Y2), 0);
        }
        wb(sp(s, SP_FLAGS), fl);
    }

    for (s = 0; s < 4 * SPRITE_SIZE; s = (uint16_t)(s + SPRITE_SIZE)) {
        uint16_t end;
        if (rb(sp(s, SP_FLAGS)) & 0xC0)
            continue;
        end = (uint16_t)(sprite_area(s) + 2);
        ww(V(sprite_list_end), end);
        if (end >= (uint16_t)(V(sprite_list) + 3))
            sort_sprite_list();
        if (grab_area())
            continue;
        drawn++;
        restore_backgrounds();
        save_backgrounds();
        draw_sprite_shapes();
        if (rb(V(sprites_scroll)) & 1) {
            wb(V(sprites_scroll), 0);
            scroll_step();
        }
        put_area();
    }
    write_mode(0);
    return drawn;
}

/* frame `frame` for the sprite at `s` (its offset in sprites): its shape,
 * position and size from sprite_frames (10 bytes a frame) */
void set_sprite_frame(uint16_t frame, uint16_t s)
{
    uint16_t shape = (uint16_t)(frame * 0x180 + SHAPES), di;

    if (shape == rw(sp(s, SP_SHAPE)))
        return;
    ww(sp(s, SP_SHAPE), shape);
    wb(sp(s, SP_FLAGS), (uint8_t)(rb(sp(s, SP_FLAGS)) | 2));
    di = (uint16_t)(frame * 10 + rw(V(sprite_frames)));
    ww(sp(s, SP_X), rw((uint16_t)(di + 2)));
    ww(sp(s, SP_Y), rw((uint16_t)(di + 4)));
    ww(sp(s, SP_W), rw((uint16_t)(di + 6)));
    ww(sp(s, SP_H), rw((uint16_t)(di + 8)));
}

/* One flipper frame: its picture (FLIPPERS.SPR at spr_seg:0, a word
 * offset per frame to width, height, pixels) over the table picture's
 * pixels behind it (pic_seg:pic, which get bit 7) in a buffer at
 * spr_seg:buf, then cut into the shape video memory keeps for it (A820h +
 * 180h a frame; per plane: the height, then per line the columns skipped,
 * the columns stored, the pixels).  Returns the aligned x in *x, the
 * width in columns * 4 in *cols4, the height. */
static int make_sprite_shape(uint16_t spr_seg, uint16_t buf, int frame, uint16_t *x,
                             uint16_t pic_seg, uint16_t pic, uint16_t *cols4)
{
    uint16_t rec = (uint16_t)(frw(spr_seg, (uint16_t)(2 * frame)));
    uint16_t width = frw(spr_seg, rec), awidth, si, di;
    int height = frb(spr_seg, (uint16_t)(rec + 2)), row, i, p;
    uint16_t x0 = *x, xa = (uint16_t)(x0 & 0xFFFC);

    awidth = (uint16_t)(((x0 + width + 3) & 0xFFFC) - xa);
    /* the table's pixels behind it, bit 7 set */
    si = (uint16_t)(pic + xa);
    di = buf;
    for (row = 0; row < height; row++) {
        for (i = 0; i < awidth; i++)
            fwb(spr_seg, di++, (uint8_t)(frb(pic_seg, si++) | 0x80));
        si = (uint16_t)(si + 0x140 - awidth);
    }
    /* the flipper's own pixels over them (0 is transparent) */
    si = (uint16_t)(rec + 4);
    di = (uint16_t)(buf + (x0 & 3));
    for (row = 0; row < height; row++) {
        for (i = 0; i < width; i++, di++) {
            uint8_t c = frb(spr_seg, si++);
            if (c)
                fwb(spr_seg, di, c);
        }
        di = (uint16_t)(di + awidth - width);
    }
    write_mode(0);
    /* per plane: a column is left out where all four of its pixels are
     * table (bit 7) - only at the start of a line - and stored with bit 7
     * cleared otherwise */
    for (p = 0; p < 4; p++) {
        uint16_t out = (uint16_t)(frame * 0x180 + SHAPES), src = buf;
        vga_outw(0x3C4, (uint16_t)(2 | (1 << p) << 8));
        vga_outw(0x3CE, (uint16_t)(4 | p << 8));
        vga_write(out++, (uint8_t)height);
        for (row = 0; row < height; row++) {
            uint8_t skip = 0, count = 0;
            uint16_t head = out;
            out = (uint16_t)(out + 2);
            for (i = 0; i < awidth / 4; i++, src = (uint16_t)(src + 4)) {
                uint8_t all = frb(spr_seg, src) & frb(spr_seg, (uint16_t)(src + 1)) &
                              frb(spr_seg, (uint16_t)(src + 2)) & frb(spr_seg, (uint16_t)(src + 3));
                if (all & 0x80) {
                    if (!count)
                        skip++;
                } else {
                    vga_write(out++, frb(spr_seg, (uint16_t)(src + p)) & 0x7F);
                    count++;
                }
            }
            vga_write(head, skip);
            vga_write((uint16_t)(head + 1), count);
        }
    }
    *x = xa;
    *cols4 = awidth;
    return height;
}

/* the flipper frames (sprite_frames, 10 of them) cut from FLIPPERS.SPR
 * (at spr_seg:di) into sprite shapes, through a buffer at spr_seg:buf */
void make_sprite_shapes(uint16_t spr_seg, uint16_t di, uint16_t buf)
{
    uint16_t si = rw(V(sprite_frames));
    int frame;

    (void)di;                                       /* the file is at offset 0 */
    for (frame = 0; frame < 10; frame++, si = (uint16_t)(si + 10)) {
        uint32_t o = (uint32_t)rw((uint16_t)(si + 4)) * 0x140;
        uint16_t pic_seg = (uint16_t)((o >> 16) << 12), pic = (uint16_t)o, x = rw(si), cols4;
        int height;
        if (pic >= 0x8000) {
            pic = (uint16_t)(pic - 0x8000);
            pic_seg = (uint16_t)(pic_seg + 0x800);
        }
        pic_seg = (uint16_t)(pic_seg + rw(V(table_pic_seg)));
        height = make_sprite_shape(spr_seg, buf, frame, &x, pic_seg, pic, &cols4);
        ww((uint16_t)(si + 2), x);
        ww((uint16_t)(si + 6), cols4);
        ww((uint16_t)(si + 8), (uint16_t)height);
    }
}
