/* textmode.h - an 80x25 text screen as a PC shows it in its text mode:
 * a character and an attribute per cell (foreground in the low nibble,
 * background in the high one, the 16 colours of the CGA palette), drawn
 * with 8x16 cells into a 640x400 picture for plat_present.
 *
 * The font is our own (textmode.c): printable ASCII drawn as bitmaps, and
 * the characters of code page 437 that a DOS setup screen uses (lines,
 * boxes, shades, arrows) drawn by rules, under code page 437's numbers.
 * Other codes are blank. */
#ifndef PD_TEXTMODE_H
#define PD_TEXTMODE_H

#include <stdint.h>

#define TM_COLS 80
#define TM_ROWS 25
#define TM_WIDTH 640
#define TM_HEIGHT 400

enum {
    TM_BLACK, TM_BLUE, TM_GREEN, TM_CYAN, TM_RED, TM_MAGENTA, TM_BROWN, TM_LIGHTGREY,
    TM_DARKGREY, TM_LIGHTBLUE, TM_LIGHTGREEN, TM_LIGHTCYAN, TM_LIGHTRED, TM_LIGHTMAGENTA,
    TM_YELLOW, TM_WHITE
};
#define TM_ATTR(fg, bg) ((uint8_t)((bg) << 4 | (fg)))

/* code page 437 */
enum {
    TM_RIGHT_TRIANGLE = 0x10, TM_LEFT_TRIANGLE = 0x11, TM_UP_ARROW = 0x18, TM_DOWN_ARROW = 0x19,
    TM_RIGHT_ARROW = 0x1A, TM_LEFT_ARROW = 0x1B, TM_UP_TRIANGLE = 0x1E, TM_DOWN_TRIANGLE = 0x1F,
    TM_SHADE_LIGHT = 0xB0, TM_SHADE_MEDIUM = 0xB1, TM_SHADE_DARK = 0xB2,
    TM_V = 0xB3, TM_V_LEFT = 0xB4, TM_VV_LEFT = 0xB6, TM_VV_LEFT2 = 0xB9, TM_VV = 0xBA,
    TM_DD_TOP_RIGHT = 0xBB, TM_DD_BOTTOM_RIGHT = 0xBC, TM_TOP_RIGHT = 0xBF,
    TM_BOTTOM_LEFT = 0xC0, TM_V_RIGHT = 0xC3, TM_H = 0xC4, TM_VV_RIGHT = 0xC7,
    TM_DD_BOTTOM_LEFT = 0xC8, TM_DD_TOP_LEFT = 0xC9, TM_VV_RIGHT2 = 0xCC, TM_HH = 0xCD,
    TM_BOTTOM_RIGHT = 0xD9, TM_TOP_LEFT = 0xDA, TM_BLOCK = 0xDB, TM_LOWER_HALF = 0xDC,
    TM_UPPER_HALF = 0xDF, TM_MIDDLE_DOT = 0xFA, TM_SQUARE = 0xFE
};

/* every cell `ch` in `attr` */
void tm_clear(uint8_t ch, uint8_t attr);

/* one cell; outside the screen it is left out */
void tm_put(int x, int y, uint8_t ch, uint8_t attr);

/* a string from (x, y) on one row, cut at the screen's edge; the column
 * after it */
int tm_text(int x, int y, const char *s, uint8_t attr);

/* a rectangle of cells */
void tm_fill(int x, int y, int w, int h, uint8_t ch, uint8_t attr);

/* a frame of double lines around w x h cells from (x, y), inside left as
 * it is */
void tm_frame(int x, int y, int w, int h, uint8_t attr);

/* the shadow a window casts: the cells right of and below the rectangle
 * keep their character in dark grey on black */
void tm_shadow(int x, int y, int w, int h);

/* the screen as a picture (TM_WIDTH x TM_HEIGHT palette indexes, colours
 * 0-15 of `palette`, 0x00RRGGBB) */
void tm_render(uint8_t *pixels, uint32_t palette[256]);

#endif
