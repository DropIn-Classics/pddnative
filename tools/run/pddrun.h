/* pddrun - a headless PC for running the shipped programs of Pinball Dreams
 * Deluxe.  The emulation core: cpu.c (386 real mode), vga.c, dev.c (PIC,
 * PIT, keyboard controller), bios.c, sound.c (DMA, Sound Blaster),
 * vgafont.c, png.c; dos.c is the DOS layer (memory, EXEC, files).  main.c
 * is the session: options, the loop, what is written out.
 */
#ifndef PDDRUN_H
#define PDDRUN_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifndef _WIN32
#include <strings.h>
#define _stricmp  strcasecmp
#define _strnicmp strncasecmp
#endif

#define RAM_SIZE 0x1000000u          /* 16 MB linear space (only <1MB used) */
/* the 16/32-bit accessors mask to RAM_SIZE-1 and then touch up to a+3 */
#define RAM_ALLOC (RAM_SIZE + 4u)

/* ---------------------------------------------------------------- CPU ---- */
enum { R_EAX, R_ECX, R_EDX, R_EBX, R_ESP, R_EBP, R_ESI, R_EDI };
enum { S_ES, S_CS, S_SS, S_DS, S_FS, S_GS };

typedef struct {
    uint32_t regs[8];
    uint16_t sreg[6];
    uint32_t sbase[6];
    uint32_t eip;
    /* flags kept unpacked for speed; assembled on demand */
    uint32_t cf, pf, af, zf, sf, tf, iflag, df, of, nt, iopl, ac;
    int halted;
    uint64_t cycles;
    int shutdown;
} CPU;

extern CPU cpu;
extern uint8_t *ram;

#define REG32(i)  (cpu.regs[i])
#define REG16(i)  (*(uint16_t*)&cpu.regs[i])
#define REG8(i)   (*((uint8_t*)&cpu.regs[(i)&3] + (((i)>>2)&1)))

/* Unaligned little-endian access to guest memory (memcpy is the defined
 * spelling; compilers make one mov of it). */
static inline uint16_t ld16u(const void *p){ uint16_t v; memcpy(&v, p, 2); return v; }
static inline uint32_t ld32u(const void *p){ uint32_t v; memcpy(&v, p, 4); return v; }
static inline void st16u(void *p, uint16_t v){ memcpy(p, &v, 2); }
static inline void st32u(void *p, uint32_t v){ memcpy(p, &v, 4); }

void cpu_reset(void);
void cpu_step(void);
void cpu_interrupt(int n, int soft);   /* push flags/cs/ip, vector through IVT */
uint32_t cpu_getflags(void);
void cpu_setflags(uint32_t f);
void set_sreg(int s, uint16_t v);
void cpu_no_iret(void);
extern void (*cb_table[256])(void);
extern uint32_t insn_ip;               /* IP of the instruction being executed */

/* Breakpoints: linear addresses; reaching one stops the run before the
 * instruction executes (cpu.shutdown, brk_hit = its index). */
#define BRK_MAX 64
extern uint32_t brk_lin[BRK_MAX];
extern int brk_n, brk_hit;
extern uint32_t brk_resume;

/* Execution trace (-trace): one line per instruction into trace_fp while
 * the linear address of CS:IP is inside [trace_lo, trace_hi]. */
extern FILE *xtrace_fp;
extern uint32_t xtrace_lo, xtrace_hi;
extern uint64_t xtrace_left;

/* ------------------------------------------------------------- memory ---- */
uint8_t  mem_r8 (uint32_t a);
uint16_t mem_r16(uint32_t a);
uint32_t mem_r32(uint32_t a);
void     mem_w8 (uint32_t a, uint8_t v);
void     mem_w16(uint32_t a, uint16_t v);
void     mem_w32(uint32_t a, uint32_t v);
extern uint32_t a20_mask;
extern uint32_t memwatch_addr;         /* -watch: log writes to this byte */
void memwatch_hit(uint32_t a, uint8_t v);
void memwatch_report(void);
extern int prof_on;                    /* -prof: sample CS:IP */
void prof_report(void);
extern int int_watch;                  /* -intwatch NN: log INT NN calls */

/* ---------------------------------------------------------------- I/O ---- */
uint8_t  io_r8 (uint16_t p);
uint16_t io_r16(uint16_t p);
void     io_w8 (uint16_t p, uint8_t v);
void     io_w16(uint16_t p, uint16_t v);

/* ------------------------------------------------------------- devices --- */
extern double emu_time, emu_ips, emu_inv_ips;
void dev_init(void);
void dev_tick(void);                  /* called from the main loop */
uint64_t dev_next_deadline(void);     /* cpu.cycles at which IRQ0 is next due */
double emu_now(void);                 /* instruction-exact emulated time */
void emu_advance(void);
void pic_raise(int irq);
void pic_lower(int irq);
int  pic_pending(void);               /* returns vector or -1 */
void kbd_key(int scancode, int down); /* scancode | 0xE000 for E0-prefixed */
void dev_report(void);

/* ---------------------------------------------------------------- VGA ---- */
void vga_init(void);
uint8_t vga_mem_r(uint32_t a);
void    vga_mem_w(uint32_t a, uint8_t v);
uint8_t vga_io_r(uint16_t p);
void    vga_io_w(uint16_t p, uint8_t v);
void    vga_render(uint32_t *out, int *w, int *h);
void    vga_set_mode_bios(int mode);
int     vga_get_mode(void);
uint8_t vga_status1(void);
void    vga_timing(double*,int*,int*,int*,int*,double*);
void    vga_timing_cached(double*,double*,int*,int*,int*,int*,double*);
void    vga_font_init(void);
void    vga_state_dump(void);
extern uint8_t vga_vram[256*1024];
extern uint8_t vga_dac[256][3];
extern int vga_dirty;
extern unsigned long vsync_edges;
extern const uint8_t bios_font8x8[128*8];

/* ---------------------------------------------------------------- BIOS --- */
void bios_init(void);
int  bios_kbuf_get(uint16_t *out);    /* type-ahead queue for DOS input */
int  bios_kbuf_peek(uint16_t *out);
void bios_set_cf(int v);
void bios_tty(uint8_t c);

/* ---------------------------------------------------------------- DOS ---- */
/* The guest's C: drive is the unpacked CD (game_dir) with a writable layer
 * above it (state_dir): a guest file is read from the layer when it is
 * there, else from the CD; anything opened for writing is copied into the
 * layer first.  The CD is never written. */
void dos_init(const char *game_dir, const char *state_dir);
void dos_loadfix(void);               /* -loadfix: load above 64 KB */
int  dos_start(const char *dospath, const char *tail, uint16_t *load_seg);
int  dos_host_path(const char *dospath, char *out, size_t n, int for_write);
extern int dos_done;                  /* the first program has exited */
extern int dos_exit_code;
extern int dos_log;                   /* -dos: log every INT 21h call */
void dos_flush_con(void);             /* the last line printed without CR/LF */
/* Called for each program EXEC loads, with its load segment (PSP + 10h),
 * so main.c can resolve addresses given relative to one program. */
extern void (*dos_on_load)(const char *dospath, uint16_t load_seg);

/* ----------------------------------------------------------- sound ------ */
void sound_init(void);
void sound_wav_open(const char *path);
void sound_wav_close(void);
void dma_write(uint16_t p, uint8_t v);
uint8_t dma_read(uint16_t p);
void sb_write(uint16_t p, uint8_t v);
uint8_t sb_read(uint16_t p);
void sb_tick(void);
void opl_write(int reg, uint8_t v);
uint8_t opl_status(void);
extern int sound_debug;

/* ------------------------------------------------------------- output ---- */
int save_png(const char *path, const uint32_t *pix, int w, int h);

/* ------------------------------------------------------------ tracing ---- */
extern int trace_level;
void trc(const char *fmt, ...);

#endif
