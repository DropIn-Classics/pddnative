/* pd_text.c - the display below the table: texts drawn character by
 * character (show_text, draw_char).  See pd.h.
 *
 * The display is at the start of video memory (the split screen), 4 bytes
 * a character column.  display_font (TDATA) has 16 bytes a character, one
 * per row: first the space's (which 2Fh also gets), then 30h's on; each
 * row's byte is written to the row's dots, which display_dots (TDATA)
 * lists per plane and row: a count, a word that skips the list, the
 * count's video memory offsets. */
#include "pd.h"
#include "vga.h"

/* the character c at column [display_col] of the display */
void draw_char(uint8_t c)
{
    uint16_t font = V(display_font), si = V(display_dots), di;
    int p, row;

    vga_outb(0x3CE, 5);
    vga_outb(0x3CF, (uint8_t)(vga_inb(0x3CF) & 0xFC));
    if (c != ' ')
        font = (uint16_t)(font + (uint8_t)(c - 0x2F) * 16);
    di = (uint16_t)(rb(V(display_col)) * 4);
    for (p = 0; p < 4; p++) {
        uint16_t bp = font;
        set_planes((uint8_t)(1 << p), (uint8_t)p);
        for (row = 0; row < 16; row++, bp++) {
            uint16_t count = frw(seg_tdata, si), i;
            uint8_t dot;
            si = (uint16_t)(si + 2);
            if (count == 0)
                continue;
            dot = frb(seg_tdata, bp);
            if (vga_read((uint16_t)(frw(seg_tdata, (uint16_t)(si + 2)) + di)) == dot) {
                si = (uint16_t)(si + frw(seg_tdata, si));   /* unchanged: the list skipped */
                continue;
            }
            si = (uint16_t)(si + 2);
            for (i = 0; i < count; i++, si = (uint16_t)(si + 2))
                vga_write((uint16_t)(frw(seg_tdata, si) + di), dot);
        }
    }
}

/* the text at `text` (FFh at its end) on the display from column 0 */
void show_text(uint16_t text)
{
    wb(V(display_col), 0);
    for (;;) {
        uint8_t c = rb(text++);
        if (c == 0xFF)
            return;
        draw_char(c);
        wb(V(display_col), (uint8_t)(rb(V(display_col)) + 1));
    }
}
