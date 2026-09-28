/* vga.h - the part of a VGA that the table programs use.
 *
 * Planar video memory (4 x 64 KB), the sequencer, graphics controller, CRTC,
 * attribute controller and DAC, reached through the same I/O ports as on the
 * real card, plus CPU access to the A000h window.  vga_render() scans the
 * memory out the way the CRTC would and gives an indexed picture and the
 * palette; the platform layer shows it.
 *
 * This is a stepping stone: the translated routines can write to video
 * memory the way the assembly did, and the picture can be compared with the
 * original.  Once they draw into plain bitmaps, this module goes away.
 */
#ifndef PD_VGA_H
#define PD_VGA_H

#include <stdint.h>

#define VGA_PLANE_SIZE 0x10000
#define VGA_MAX_W 640
#define VGA_MAX_H 480

/* I/O ports */
void vga_outb(uint16_t port, uint8_t value);
void vga_outw(uint16_t port, uint16_t value);   /* OUT DX,AX: index in AL, data in AH */
uint8_t vga_inb(uint16_t port);

/* CPU reads and writes at A000:offset */
void vga_write(uint16_t offset, uint8_t value);
uint8_t vga_read(uint16_t offset);

/* INT 10h AH=00h: mode 12h (640x480, 16 colours) or 13h (320x200, 256
 * colours, chain-4); the registers the BIOS sets, memory cleared.  The
 * BIOS palette is loaded for mode 12h (the menu's history shows its
 * pictures in it), not for 13h: the programs set their own. */
void vga_set_mode(int mode);

/* the picture the CRTC would show now */
typedef struct {
    int width, height;                    /* in pixels, after double scan */
    uint8_t pixels[VGA_MAX_W * VGA_MAX_H]; /* palette indexes, row by row */
    uint32_t palette[256];                /* 0x00RRGGBB, DAC values scaled to 8 bits */
} VgaFrame;

void vga_render(VgaFrame *frame);

/* vertical refresh rate the current CRTC timing gives, in Hz */
double vga_refresh_hz(void);

/* The platform calls this at the start of every frame.  Polling the input
 * status register (3DAh) moves a virtual beam on by one scan line per read,
 * so loops that wait for the retrace terminate. */
void vga_frame_start(void);

/* direct access for tests and debugging */
uint8_t vga_plane_byte(int plane, uint16_t addr);
uint8_t vga_crtc_reg(int index);

#endif
