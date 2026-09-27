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

typedef void (*FrameCallback)(void);
typedef void (*KeyHandler)(unsigned char scancode);

/* the routine the program gave the driver for its tick; NULL for none */
void frame_set_tick(FrameCallback tick);

/* the program's INT 9 handler, which gets each byte of port 60h */
void frame_set_keyboard(KeyHandler handler);

/* waits for the next picture; 0 once the window was closed */
int frame_wait(void);

/* pictures shown so far */
unsigned long frame_count(void);

#endif
