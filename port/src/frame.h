/* frame.h - one picture after the other.
 *
 * Under DOS the sound driver owns the timer and calls the program back
 * once a tick (INT 66h AL=0Bh; PD.EXE's timer_callback), and the program
 * waits for the next tick by polling a flag the callback sets (wait_frame).
 * With SBLASTER.SDR the tick comes 70.09 times a second in the 320x200
 * mode, the mode's refresh rate (measured with tools/run).  frame_wait()
 * is that wait: it keeps the rate of the video mode, hands the keys to the
 * program's keyboard handler, runs the tick callback and shows the
 * picture. */
#ifndef PD_FRAME_H
#define PD_FRAME_H

#include "vga.h"

typedef void (*FrameCallback)(void);
typedef void (*KeyHandler)(unsigned char scancode);

/* the routine the program gave the driver for its tick; NULL for none */
void frame_set_tick(FrameCallback tick);

/* the program's INT 9 handler, which gets each byte of port 60h */
void frame_set_keyboard(KeyHandler handler);

/* drawn into each picture after vga_render, before it is shown (what
 * video memory does not hold: a mouse driver's pointer); NULL for none */
void frame_set_overlay(void (*draw)(VgaFrame *picture));

/* waits for the next picture; 0 once the window was closed */
int frame_wait(void);
/* For a loop that waits on the keyboard alone (the menu's wait_keys_up):
 * the next picture's keys handed over, and when there were any, back to
 * the program before the tick, which the next wait runs without waiting
 * (under DOS the key's interrupt came before the tick, and such a loop
 * went on at once).  0 once the window was closed. */
int frame_wait_keys(void);

/* each keyboard byte handed to the program is written to the file `path`
 * as PICTURE:HEX (the picture count when it was handed over) */
void frame_record(const char *path);

/* pictures shown so far */
unsigned long frame_count(void);

#endif
