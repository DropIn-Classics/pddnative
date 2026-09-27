/* mem.h - the table program's memory, as it was under DOS.
 *
 * PD.EXE and PD2.EXE are hand-written assembly whose tables are data: the
 * records (lamps, targets, holes, event objects ...) sit in the program's
 * DATA segment and point at each other with 16-bit offsets, and some hold
 * offsets of routines.  The port keeps that memory as it was: one flat
 * megabyte of real-mode memory, the program image loaded from the player's
 * file at the same place pddrun loads it (relocations applied, BSS
 * cleared), the DOS blocks the program allocates behind it.  The C
 * routines read and write it through the accessors below, by the names
 * of the hints (gen/pdnames.h), so one engine runs both programs, and the
 * port's memory can be compared byte for byte with the original's in
 * pddrun.
 *
 * This is a stepping stone like vga.c: once the engine is C throughout,
 * the variables can move into C structures one by one.
 */
#ifndef PD_MEM_H
#define PD_MEM_H

#include <stddef.h>
#include <stdint.h>
#include "gen/pdnames.h"

#define MEM_SIZE 0x100000u

extern uint8_t mem[MEM_SIZE];

/* which program is loaded: 1 = PD.EXE (Pinball Dreams), 2 = PD2.EXE */
extern int prog_id;

/* the segments of the loaded program (absolute, as DS would hold them) */
extern uint16_t seg_psp, seg_code, seg_tdata, seg_data, seg_xdata, seg_bss;

/* the names of the hints, for the program loaded: V(game_state) is the
 * offset of game_state in its segment (DATA for most), C.. of a routine
 * in CODE.  0xFFFF where the program has no such name. */
typedef struct {
#define PDN_FIELD(seg, name, a1, a2) uint16_t name;
    PDN_NAMES(PDN_FIELD)
#undef PDN_FIELD
} PdNames;
extern PdNames nm;
#define V(name) (nm.name)
#define NONE 0xFFFFu

/* Loads PD.EXE (id 1) or PD2.EXE (id 2) from the unpacked CD in game_dir
 * and checks that it is the file the hints describe.  0 on success, else
 * -1 and a message in err. */
int mem_load(int id, const char *game_dir, char *err, size_t n);

/* ---- access, segment:offset (the offset wraps at 64 KB as on the CPU) */

static inline uint32_t lin(uint16_t seg, uint16_t off)
{
    return (((uint32_t)seg << 4) + off) & (MEM_SIZE - 1);
}
static inline uint8_t frb(uint16_t seg, uint16_t off) { return mem[lin(seg, off)]; }
static inline void fwb(uint16_t seg, uint16_t off, uint8_t v) { mem[lin(seg, off)] = v; }
static inline uint16_t frw(uint16_t seg, uint16_t off)
{
    return (uint16_t)(frb(seg, off) | frb(seg, (uint16_t)(off + 1)) << 8);
}
static inline void fww(uint16_t seg, uint16_t off, uint16_t v)
{
    fwb(seg, off, (uint8_t)v);
    fwb(seg, (uint16_t)(off + 1), (uint8_t)(v >> 8));
}

/* DATA, where DS points nearly always */
static inline uint8_t rb(uint16_t off) { return frb(seg_data, off); }
static inline void wb(uint16_t off, uint8_t v) { fwb(seg_data, off, v); }
static inline uint16_t rw(uint16_t off) { return frw(seg_data, off); }
static inline void ww(uint16_t off, uint16_t v) { fww(seg_data, off, v); }
static inline int16_t rsw(uint16_t off) { return (int16_t)rw(off); }

/* two words as the 32-bit number the code keeps in them (low word first) */
static inline uint32_t rd(uint16_t off) { return rw(off) | (uint32_t)rw((uint16_t)(off + 2)) << 16; }
static inline void wd(uint16_t off, uint32_t v)
{
    ww(off, (uint16_t)v);
    ww((uint16_t)(off + 2), (uint16_t)(v >> 16));
}

/* ---- DOS memory (INT 21h 48h/49h): blocks behind the program */

/* a block of `paras` paragraphs; its segment, or 0 when there is no room */
uint16_t dos_alloc(uint16_t paras);
/* 0 on success */
int dos_free(uint16_t seg);

#endif
