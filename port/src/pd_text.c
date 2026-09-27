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

/* running_object and message_busy cleared */
void end_running(void)
{
    wb(V(message_busy), 0);
    ww(V(running_object), 0);
}

/* the message's text from `text` into the display, from display_col,
 * until FFh or the columns (msg_cols) are used up */
static void draw_message_text(uint16_t text)
{
    for (;;) {
        uint8_t c = rb(text++);
        if (c == 0xFF)
            return;
        draw_char(c);
        wb(V(display_col), (uint8_t)(rb(V(display_col)) + 1));
        ww(V(msg_cols), (uint16_t)(rw(V(msg_cols)) - 1));
        if (rw(V(msg_cols)) == 0xFFFF)
            return;
    }
}

/* the message ended: message_step's (not message_start's) clears message
 * and the running object */
static int message_end(void)
{
    if (!rb(V(msg_starting))) {
        ww(V(message), 0);
        end_running();
    }
    return 0;
}

/* the count-up (flag 10h): each step the characters shown may be one
 * higher (msg_count_max), until all are their own; then msg_length frames */
static int message_count_up(uint16_t text)
{
    uint16_t bp = V(msg_count_buf);

    ww(V(msg_count_src), text);
    if (rb(V(msg_count_max)) <= 0x5A) {
        uint8_t max;
        wb(V(msg_timer), (uint8_t)(rb(V(msg_timer)) - 1));
        if (rb(V(msg_timer)) != 0)
            goto draw;
        wb(V(msg_timer), rb(V(msg_period)));
        if (!rb(V(msg_period)))
            return message_end();
        ww(V(msg_counting), 0);
        max = rb(V(msg_count_max));
        do {
            uint16_t src = rw(V(msg_count_src));
            wb(V(event_arm), rb(src));
            ww(V(msg_count_src), (uint16_t)(src + 1));
            if (!(max > rb(V(event_arm)))) {
                wb(V(event_arm), max);
                ww(V(msg_counting), 0xFFFF);
            }
            wb(bp++, rb(V(event_arm)));
            ww(V(msg_cols), (uint16_t)(rw(V(msg_cols)) - 1));
        } while (rw(V(msg_cols)) != 0xFFFF);
        wb(bp, 0xFF);
        wb(V(msg_count_max), (uint8_t)(max + 1));
        if (rb(V(msg_counting)) & 0x80)
            goto draw;
    }
    ww(V(msg_length), (uint16_t)(rw(V(msg_length)) - 1));
    if (rw(V(msg_length)) == 0)
        return message_end();
draw:
    draw_message_text(V(msg_count_buf));
    return 1;
}

/* a frame of the message at `msg` (8 bytes: +0 flags, +1 0 to start /
 * FFh running, +2 flashes, +3 their period, +4 the text, +6 a length; see
 * the hints at message_step); 1 while it runs, 0 when it has ended */
static int message_run(uint16_t msg, int starting)
{
    uint16_t text = rw((uint16_t)(msg + 4));
    uint8_t flags;

    wb(V(msg_starting), starting ? 0xFF : 0);
    ww(V(msg_cols), 0);
    if (!rb((uint16_t)(msg + 1))) {
        wb(V(display_col), 0);
        wb(V(msg_timer), 1);
        wb(V(message_busy), 0xFF);
        wb(V(msg_period), rb((uint16_t)(msg + 3)));
        wb(V(msg_flashes), rb((uint16_t)(msg + 2)));
        ww(V(msg_length), rw((uint16_t)(msg + 6)));
        ww(V(msg_frame), 0);
        wb((uint16_t)(msg + 1), 0xFF);
        wb(V(msg_count_max), 0x30);
    }
    ww(V(msg_cols), 0x13);
    wb(V(display_col), 0);
    flags = rb(msg);
    if (flags & 1) {                    /* scrolls in from the right, a column every 8 frames */
        uint16_t cx = (uint16_t)(rw(V(msg_frame)) >> 3);
        ww(V(msg_frame), (uint16_t)(rw(V(msg_frame)) + 1));
        ww(V(event_arm), (uint16_t)(rw(V(msg_length)) + rw(V(msg_frame))));
        if (rw(V(event_arm)) & 0x8000)
            return message_end();
        cx = (uint16_t)(cx - 0x14);
        if (cx & 0x8000) {
            cx = (uint16_t)-cx;
            ww(V(msg_cols), (uint16_t)(rw(V(msg_cols)) - cx));
            if (rw(V(msg_cols)) & 0x8000)
                return 1;
            wb(V(display_col), (uint8_t)(rb(V(display_col)) + (uint8_t)cx));
        } else {
            text = (uint16_t)(text + cx);
            if (rb(text) == 0xFF)
                return message_end();
        }
    }
    if (flags & 4) {                    /* flashes */
        wb(V(msg_timer), (uint8_t)(rb(V(msg_timer)) - 1));
        if (rb(V(msg_timer)) == 0) {
            wb(V(msg_blank), (uint8_t)~rb(V(msg_blank)));
            wb(V(msg_timer), rb(V(msg_period)));
            wb(V(msg_flashes), (uint8_t)(rb(V(msg_flashes)) - 1));
            if (rb(V(msg_flashes)) == 0)
                return message_end();
        }
        if (rb(V(msg_blank)))
            text = V(txt_blank);
    }
    if ((flags & 8) && rb((uint16_t)(text + 0x12)) == 0xFF)
        return message_end();
    if (flags & 0x10)
        return message_count_up(text);
    draw_message_text(text);
    return 1;
}

int message_start(uint16_t msg)
{
    return message_run(msg, 1);
}

int message_step(uint16_t msg)
{
    return message_run(msg, 0);
}
