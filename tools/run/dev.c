/* 8259 PIC, 8253 PIT, keyboard controller, port dispatch, display timing
 * (see pddrun.h). */
#include "pddrun.h"
#include <math.h>

double emu_time = 0.0;            /* emulated seconds since boot */
double emu_ips  = 6000000.0;      /* emulated instructions per second */
double emu_inv_ips = 1.0 / 6000000.0;
static uint64_t last_cycles = 0;

void emu_advance(void){
    uint64_t d = cpu.cycles - last_cycles;
    last_cycles = cpu.cycles;
    emu_time += (double)d * emu_inv_ips;
}

/* emu_time is only folded forward every batch, but whatever a program polls
 * in a tight loop - the CRT status register, the PIT counters, the port-61h
 * refresh bit - has to be exact to the instruction, or short pulses vanish
 * between samples (the sound drivers phase-lock their timer by counting
 * horizontal blanking pulses). */
double emu_now(void){
    return emu_time + (double)(cpu.cycles - last_cycles) * emu_inv_ips;
}

/* ------------------------------------------------------------------ PIC */
typedef struct { uint8_t imr, irr, isr, base, icw_step, icw4, auto_eoi, read_isr; } PIC8259;
static PIC8259 pic[2];

static void pic_init(void){
    memset(pic,0,sizeof(pic));
    pic[0].base = 0x08; pic[1].base = 0x70;
    pic[0].imr = 0xB8; pic[1].imr = 0xFF;   /* as BIOS POST leaves them: IRQ0/1/2/6 on */
}
void pic_raise(int irq){
    if(irq<8) pic[0].irr |= (uint8_t)(1<<irq);
    else { pic[1].irr |= (uint8_t)(1<<(irq-8)); pic[0].irr |= 0x04; }
}
void pic_lower(int irq){
    if(irq<8) pic[0].irr &= (uint8_t)~(1<<irq);
    else pic[1].irr &= (uint8_t)~(1<<(irq-8));
}
int pic_pending(void){
    int i;
    uint8_t act = pic[0].irr & ~pic[0].imr;
    if(!act) return -1;
    for(i=0;i<8;i++){
        if(pic[0].isr & (1<<i)) return -1;          /* higher/equal in service */
        if(act & (1<<i)){
            if(i==2){
                uint8_t s = pic[1].irr & ~pic[1].imr;
                int j;
                for(j=0;j<8;j++) if(s & (1<<j)){
                    pic[1].irr &= (uint8_t)~(1<<j); pic[1].isr |= (uint8_t)(1<<j);
                    pic[0].isr |= 0x04; pic[0].irr &= (uint8_t)~0x04;
                    return pic[1].base + j;
                }
                pic[0].irr &= (uint8_t)~0x04;
                return -1;
            }
            pic[0].irr &= (uint8_t)~(1<<i);
            pic[0].isr |= (uint8_t)(1<<i);
            return pic[0].base + i;
        }
    }
    return -1;
}
static void pic_write(int n, int a0, uint8_t v){
    PIC8259 *p = &pic[n];
    if(a0==0){
        if(v & 0x10){ p->icw_step = 1; p->imr = 0; p->isr = 0; p->irr = 0; p->icw4 = v&1; return; }
        if(v & 0x08){ if(v & 0x02) p->read_isr = v & 1; return; }   /* OCW3 */
        if((v & 0xE0) == 0x20){                                     /* EOI */
            int i;
            if(v & 0x40){ p->isr &= (uint8_t)~(1 << (v&7)); }
            else for(i=0;i<8;i++) if(p->isr & (1<<i)){ p->isr &= (uint8_t)~(1<<i); break; }
        }
        return;
    }
    if(p->icw_step==1){ p->base = v & 0xF8; p->icw_step=2; return; }
    if(p->icw_step==2){ p->icw_step = p->icw4 ? 3 : 0; return; }
    if(p->icw_step==3){ p->icw_step = 0; return; }
    p->imr = v;
}
static uint8_t pic_read(int n, int a0){
    PIC8259 *p = &pic[n];
    if(a0==0) return p->read_isr ? p->isr : p->irr;
    return p->imr;
}

/* ------------------------------------------------------------------ PIT */
#define PIT_HZ 1193182.0
typedef struct {
    uint16_t reload; uint8_t mode, rw, latched_cnt_valid;
    uint16_t latch; int rd_hi, wr_hi; uint16_t wr_tmp;
    double next_irq;
    double inv_per;           /* PIT_HZ / effective reload: ticks per second */
    int armed;                /* mode 0: one interrupt per count written */
} PITCH;
static PITCH pit[3];
static uint8_t port61 = 0x00;
static unsigned long pit0_irqs = 0;

static void pit_init(void){
    int i;
    memset(pit,0,sizeof(pit));
    for(i=0;i<3;i++){ pit[i].reload = 0; pit[i].rw = 3; pit[i].mode = 3;
                      pit[i].inv_per = PIT_HZ / 65536.0; }
    pit[0].next_irq = 65536.0 / PIT_HZ;
}

static uint16_t pit_count(int c){
    uint32_t rel = pit[c].reload ? pit[c].reload : 65536u;
    double q;
    /* Mode 0 is a one-shot: the count runs down from when it was written,
     * and past terminal count it keeps decrementing through FFFF, so a read
     * after the interrupt fired is a small negative count.  The Sound
     * Blaster drivers' timer ISR reads it to take the interrupt latency out
     * of the next delay (a free-running reading there loses every second
     * timer interrupt). */
    if(c == 0 && pit[0].mode == 0){
        double left = (pit[0].next_irq - emu_now()) * PIT_HZ;
        if(left >= (double)rel) return (uint16_t)rel;   /* written, not started */
        if(left > 0.0) return (uint16_t)left;
        {   /* past zero: -left ticks have elapsed since terminal count */
            double past = -left;
            uint32_t p = past >= 4294967296.0 ? 0xFFFFFFFFu : (uint32_t)past;
            return (uint16_t)(0u - p);
        }
    }
    q = emu_now() * pit[c].inv_per;
    q -= (int64_t)q;
    return (uint16_t)((1.0 - q) * (double)rel);
}
static void pit_write(int port, uint8_t v){
    if(port==3){
        int c = v>>6;
        if(c==3) return;
        if(((v>>4)&3)==0){ pit[c].latch = pit_count(c); pit[c].latched_cnt_valid=1; pit[c].rd_hi=0; return; }
        pit[c].rw = (v>>4)&3; pit[c].mode = (v>>1)&7; pit[c].wr_hi = 0;
        return;
    }
    {
        int c = port;
        uint16_t nv = pit[c].reload;
        if(pit[c].rw==1) nv = v;
        else if(pit[c].rw==2) nv = (uint16_t)(v<<8);
        else {
            if(!pit[c].wr_hi){ pit[c].wr_tmp = v; pit[c].wr_hi = 1; return; }
            nv = (uint16_t)(pit[c].wr_tmp | (v<<8)); pit[c].wr_hi = 0;
        }
        pit[c].reload = nv;
        pit[c].inv_per = PIT_HZ / (double)(nv ? nv : 65536u);
        if(c==0){
            /* instruction-exact start: a driver calibrating against the
             * retrace counts pulses from here */
            double per = (nv ? nv : 65536) / PIT_HZ;
            pit[0].next_irq = emu_now() + per;
            pit[0].armed = 1;
        }
    }
}
static uint8_t pit_read(int c){
    uint16_t v;
    if(pit[c].latched_cnt_valid) v = pit[c].latch; else v = pit_count(c);
    if(pit[c].rw==1) { pit[c].latched_cnt_valid=0; return (uint8_t)v; }
    if(pit[c].rw==2) { pit[c].latched_cnt_valid=0; return (uint8_t)(v>>8); }
    if(!pit[c].rd_hi){ pit[c].rd_hi=1; return (uint8_t)v; }
    pit[c].rd_hi=0; pit[c].latched_cnt_valid=0; return (uint8_t)(v>>8);
}

/* -------------------------------------------------------------- keyboard */
static uint8_t kbd_buf[64];
static int kbd_head, kbd_tail;
static uint8_t kbd_last = 0;
static int kbd_a20 = 1;
static uint8_t kbc_cmd = 0;
static unsigned long kbd_dropped = 0;

static int kbd_free(void){ return 63 - ((kbd_tail - kbd_head) & 63); }
static void kbd_push(uint8_t b){ kbd_buf[kbd_tail]=b; kbd_tail=(kbd_tail+1)&63; }

/* One key event from the key script: the make or break code (with an E0
 * prefix for scancode|0xE000) into the controller, IRQ1 raised. */
void kbd_key(int scancode, int down){
    uint8_t sc = (uint8_t)(scancode & 0x7F);
    int ext = (scancode & 0xE000) ? 1 : 0;
    if(kbd_free() < 1 + ext){ kbd_dropped++; return; }
    if(ext) kbd_push(0xE0);
    kbd_push(down ? sc : (uint8_t)(sc|0x80));
    pic_raise(1);
    trc("[kbd] %s%02X %s\n", ext?"E0 ":"", sc, down?"down":"up");
}
static uint8_t kbd_read60(void){
    if(kbd_head!=kbd_tail){
        kbd_last = kbd_buf[kbd_head];
        kbd_head = (kbd_head+1)&63;
    }
    if(kbd_head!=kbd_tail) pic_raise(1); else pic_lower(1);
    return kbd_last;
}
static uint8_t kbd_status(void){
    return (uint8_t)(0x14 | (kbd_head!=kbd_tail ? 0x01 : 0x00));
}

/* ---------------------------------------------------------- CRT timing  */
unsigned long vsync_edges = 0;
uint8_t vga_status1(void){
    double per, inv_per, hde_frac;
    int vtotal, vde, vrs, vre;
    double now, q, line, frac;
    uint8_t st = 0;
    static int prev = 0;
    vga_timing_cached(&per, &inv_per, &vtotal, &vde, &vrs, &vre, &hde_frac);
    (void)per; (void)vde;
    now = emu_now();
    q = now * inv_per;
    q -= (int64_t)q;
    line = q * (double)vtotal;
    frac = line - (int)line;
    if(line >= vrs && line < vre) st |= 0x08;              /* vertical retrace */
    /* Bit 0 pulses once per scan line for the whole frame, vertical blanking
     * included: the sound driver's calibration uses it as a scan-line
     * clock and only settles if it never stops. */
    if(frac >= hde_frac) st |= 0x01;
    { int v = (st>>3)&1; if(v && !prev) vsync_edges++; prev = v; }
    return st;
}

/* ------------------------------------------------------------ dispatch  */
static uint8_t cmos_idx = 0;
static uint8_t adlib_idx = 0;

uint8_t io_r8(uint16_t p){
    switch(p){
    case 0x20: case 0x21: return pic_read(0, p&1);
    case 0xA0: case 0xA1: return pic_read(1, p&1);
    case 0x40: case 0x41: case 0x42: return pit_read(p&3);
    case 0x43: return 0xFF;
    case 0x60: return kbd_read60();
    case 0x61: {
        /* bit4: RAM refresh toggle (~15.09 kHz), bit5: timer-2 output */
        double t = emu_now() * 15085.0;
        uint8_t r = (uint8_t)(port61 & 0x0F);
        if(((uint64_t)t) & 1) r |= 0x10;
        {
            double q = emu_now() * pit[2].inv_per;
            q -= (int64_t)q;
            if(q < 0.5) r |= 0x20;
        }
        return r; }
    case 0x64: return kbd_status();
    case 0x70: return cmos_idx;
    case 0x71: return 0;
    case 0x92: return (uint8_t)(kbd_a20 ? 0x02 : 0x00);
    case 0x201: return 0xF0;                    /* game port: nothing attached */
    case 0x388: case 0x389: return opl_status();
    }
    if(p < 0x10 || p==0x81 || p==0x82 || p==0x83 || p==0x87) return dma_read(p);
    if(p>=0x3B0 && p<=0x3DF) return vga_io_r(p);
    if(p>=0x220 && p<=0x22F) return sb_read(p);
    trc("[io] read of unknown port %03X from %04X:%04X\n", p, cpu.sreg[S_CS], (unsigned)insn_ip);
    return 0xFF;
}

void io_w8(uint16_t p, uint8_t v){
    switch(p){
    case 0x20: case 0x21: pic_write(0, p&1, v); return;
    case 0xA0: case 0xA1: pic_write(1, p&1, v); return;
    case 0x40: case 0x41: case 0x42: pit_write(p&3, v); return;
    case 0x43: pit_write(3, v); return;
    case 0x61: port61 = v; return;
    case 0x64:
        kbc_cmd = v;
        if(v==0xFE) cpu.shutdown = 1;
        return;
    case 0x70: cmos_idx = v; return;
    case 0x71: return;
    case 0x92: kbd_a20 = (v&2)?1:0; a20_mask = kbd_a20 ? 0xFFFFFFFFu : 0xFFEFFFFFu; return;
    case 0x201: return;
    case 0x388: adlib_idx = v; return;
    case 0x389: opl_write(adlib_idx, v); return;
    }
    if(p < 0x10 || p==0x81 || p==0x82 || p==0x83 || p==0x87){ dma_write(p,v); return; }
    if(p>=0x3B0 && p<=0x3DF){ vga_io_w(p,v); return; }
    if(p>=0x220 && p<=0x22F){ sb_write(p,v); return; }
    if(p==0x60){
        if(kbc_cmd==0xD1){ kbd_a20 = (v&2)?1:0; a20_mask = kbd_a20?0xFFFFFFFFu:0xFFEFFFFFu; kbc_cmd=0; }
        return;
    }
    trc("[io] write %02X to unknown port %03X from %04X:%04X\n", v, p, cpu.sreg[S_CS], (unsigned)insn_ip);
}

uint16_t io_r16(uint16_t p){ return (uint16_t)(io_r8(p) | (io_r8((uint16_t)(p+1))<<8)); }
void io_w16(uint16_t p, uint16_t v){ io_w8(p,(uint8_t)v); io_w8((uint16_t)(p+1),(uint8_t)(v>>8)); }

/* ---------------------------------------------------------------- tick  */
void dev_tick(void){
    emu_advance();
    sb_tick();
    /* PIT channel 0 -> IRQ0.  Mode 0 is one interrupt per count written;
     * treating it as periodic gives a calibrating driver interrupts it has
     * not asked for (and it runs off the end of its event list). */
    if(pit[0].mode == 0){
        if(pit[0].armed && emu_time >= pit[0].next_irq){
            pit[0].armed = 0;
            pit0_irqs++;
            pic_raise(0);
        }
    } else {
        double per = (pit[0].reload ? pit[0].reload : 65536) / PIT_HZ;
        if(emu_time >= pit[0].next_irq){
            if(emu_time - pit[0].next_irq > 0.25) pit[0].next_irq = emu_time;  /* resync */
            pit[0].next_irq += per;
            pit0_irqs++;
            pic_raise(0);
        }
    }
}

/* Cycle count at which IRQ0 is next due, so the main loop stops there
 * instead of overshooting by a batch.  While a mode-0 one-shot is spent and
 * not yet reloaded (the guest is inside its timer handler, about to arm the
 * next one) batches stay short: a one-shot armed early in a long batch would
 * otherwise be noticed late, and the driver subtracts that lateness from its
 * next delay. */
#define PIT0_UNARMED_BATCH 32

uint64_t dev_next_deadline(void){
    double dt;
    if(pit[0].mode == 0 && !pit[0].armed) return cpu.cycles + PIT0_UNARMED_BATCH;
    dt = pit[0].next_irq - emu_now();
    if(dt <= 0.0) return cpu.cycles;
    if(dt > 1.0) dt = 1.0;
    return cpu.cycles + (uint64_t)(dt * emu_ips);
}

void dev_report(void){
    printf("pit0 reload=%u mode=%u irqs=%lu\n", pit[0].reload, pit[0].mode, pit0_irqs);
    printf("pic imr=%02X irr=%02X isr=%02X\n", pic[0].imr, pic[0].irr, pic[0].isr);
    printf("ivt int8=%08X int9=%08X int66=%08X\n",
           mem_r32(8*4), mem_r32(9*4), mem_r32(0x66*4));
    if(kbd_dropped) printf("kbd dropped=%lu (controller queue full)\n", kbd_dropped);
}

void dev_init(void){
    pic_init();
    pit_init();
    sound_init();
    kbd_head = kbd_tail = 0;
    port61 = 0;
}
