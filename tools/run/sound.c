/* Sound: 8237 DMA controller and Sound Blaster DSP, pfemu's (see pddrun.h;
 * its sound.c tells what SBLASTER.SDR does with them, re-derived there from
 * the driver's code).  The samples the card would play go to a WAV file
 * (-wav) or nowhere.
 */
#include "pddrun.h"

int sound_debug = 0;

/* -dmairq in pfemu: interrupt on the DMA controller's wrap instead of the
 * DSP's own transfer count.  Kept at pfemu's default (off). */
int sb_dmairq = 0;

/* ------------------------------------------------------------ WAV sink */
static FILE *wav_fp;
static unsigned long wav_n;
static int wav_hz = 0;

static void wav_put32(uint8_t *p, uint32_t v){
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
static void wav_header(void){
    uint8_t h[44];
    memset(h, 0, sizeof(h));
    memcpy(h, "RIFF", 4); memcpy(h+8, "WAVEfmt ", 8);
    h[16] = 16; h[20] = 1; h[22] = 1;                  /* PCM, mono */
    wav_put32(h+24, (uint32_t)wav_hz);
    wav_put32(h+28, (uint32_t)wav_hz*2);
    h[32] = 2; h[34] = 16;
    memcpy(h+36, "data", 4);
    wav_put32(h+4, 36 + (uint32_t)wav_n*2);
    wav_put32(h+40, (uint32_t)wav_n*2);
    fseek(wav_fp, 0, SEEK_SET);
    fwrite(h, 1, 44, wav_fp);
    fseek(wav_fp, 0, SEEK_END);
}
void sound_wav_open(const char *path){
    wav_fp = fopen(path, "wb");
    if(!wav_fp){ fprintf(stderr, "cannot write %s\n", path); return; }
    wav_n = 0;
    wav_header();
}
/* The rate is the first one the driver programs; a driver that changes it
 * later is not expected (one DSP time constant per program run). */
static void wav_rate(int hz){
    if(wav_fp && !wav_hz){ wav_hz = hz; wav_header(); }
}
static void wav_push(const int16_t *s, int n){
    if(!wav_fp) return;
    fwrite(s, 2, (size_t)n, wav_fp);
    wav_n += (unsigned long)n;
}
void sound_wav_close(void){
    if(!wav_fp) return;
    wav_header();
    fclose(wav_fp); wav_fp = NULL;
}

/* ------------------------------------------------------------------ 8237 */
typedef struct {
    uint16_t addr, count, base_addr, base_count;
    uint8_t  page, mode, masked;
} DMACH;
static DMACH dma[4];
static int dma_ff;                     /* low/high byte flip-flop */

static void dma_reset(void){
    int i;
    memset(dma, 0, sizeof(dma));
    for(i=0;i<4;i++) dma[i].masked = 1;
    dma_ff = 0;
}

void dma_write(uint16_t p, uint8_t v){
    switch(p){
    case 0x00: case 0x02: case 0x04: case 0x06: {        /* address */
        int c = p >> 1;
        if(!dma_ff) dma[c].base_addr = (dma[c].base_addr & 0xFF00) | v;
        else        dma[c].base_addr = (uint16_t)((dma[c].base_addr & 0x00FF) | (v << 8));
        dma[c].addr = dma[c].base_addr;
        dma_ff ^= 1;
        return; }
    case 0x01: case 0x03: case 0x05: case 0x07: {        /* count */
        int c = p >> 1;
        if(!dma_ff) dma[c].base_count = (dma[c].base_count & 0xFF00) | v;
        else        dma[c].base_count = (uint16_t)((dma[c].base_count & 0x00FF) | (v << 8));
        dma[c].count = dma[c].base_count;
        dma_ff ^= 1;
        return; }
    case 0x0A: dma[v & 3].masked = (v & 4) ? 1 : 0; return;   /* single mask  */
    case 0x0B: dma[v & 3].mode = v; return;                   /* mode         */
    case 0x0C: dma_ff = 0; return;                            /* clear ff     */
    case 0x0D: dma_reset(); return;                           /* master clear */
    case 0x0F: { int i; for(i=0;i<4;i++) dma[i].masked = (v >> i) & 1; return; }
    case 0x87: dma[0].page = v; return;
    case 0x83: dma[1].page = v; return;
    case 0x81: dma[2].page = v; return;
    case 0x82: dma[3].page = v; return;
    }
}

uint8_t dma_read(uint16_t p){
    switch(p){
    case 0x00: case 0x02: case 0x04: case 0x06: {
        int c = p >> 1; uint8_t r;
        r = dma_ff ? (uint8_t)(dma[c].addr >> 8) : (uint8_t)dma[c].addr;
        dma_ff ^= 1; return r; }
    case 0x01: case 0x03: case 0x05: case 0x07: {
        int c = p >> 1; uint8_t r;
        r = dma_ff ? (uint8_t)(dma[c].count >> 8) : (uint8_t)dma[c].count;
        dma_ff ^= 1; return r; }
    case 0x87: return dma[0].page;
    case 0x83: return dma[1].page;
    case 0x81: return dma[2].page;
    case 0x82: return dma[3].page;
    }
    return 0xFF;
}

/* Pull one byte across the channel.  Returns -1 when the block has ended and
 * the channel is not auto-init (so the caller knows to stop). */
static int dma_fetch(int c, int *end_of_block){
    uint32_t phys;
    int b;
    *end_of_block = 0;
    if(dma[c].masked) return -1;
    phys = ((uint32_t)dma[c].page << 16) | dma[c].addr;
    b = mem_r8(phys);
    dma[c].addr++;
    if(dma[c].count == 0){
        *end_of_block = 1;
        if(dma[c].mode & 0x10){                 /* auto-init: reload and go on */
            dma[c].addr  = dma[c].base_addr;
            dma[c].count = dma[c].base_count;
        } else {
            dma[c].masked = 1;
        }
    } else {
        dma[c].count--;
    }
    return b;
}

/* ------------------------------------------------------- Sound Blaster DSP */
#define SB_BASE 0x220
int sb_irq = 7;

static struct {
    int  reset_stage;
    uint8_t outbuf[8]; int outlen, outpos;
    uint8_t cmd; int need_args; uint8_t arg[2]; int nargs;
    int  time_constant;
    int  block_size;          /* bytes in one DSP transfer */
    int  dsp_left;            /* bytes still owed on it, for -dspcount */
    int  playing, auto_init;
    int  speaker;
    int  irq_pending;
    double rate;              /* samples per second */
    double frac;              /* fractional sample carried between ticks */
    double last_t;
} sb;

static void sb_out(uint8_t v){
    if(sb.outlen < (int)sizeof(sb.outbuf)) sb.outbuf[sb.outlen++] = v;
}

void sb_reset_dev(void){
    memset(&sb, 0, sizeof(sb));
    sb.rate = 22050.0;
    sb.last_t = -1.0;
}

static void sb_start(int auto_init, int len){
    sb.playing = 1;
    sb.auto_init = auto_init;
    if(len > 0) sb.block_size = len;
    sb.dsp_left = sb.block_size;
    if(sb.time_constant > 0 && sb.time_constant < 256)
        sb.rate = 1000000.0 / (256.0 - (double)sb.time_constant);
    if(sb.rate < 4000.0) sb.rate = 4000.0;
    if(sb.rate > 48000.0) sb.rate = 48000.0;
    sb.last_t = emu_now();
    sb.frac = 0.0;
    wav_rate((int)(sb.rate + 0.5));
    if(sound_debug){
        /* Both lengths and the gap between re-arms, because the interesting
         * question is how the DSP's transfer length relates to the DMA
         * block the controller is actually looping - see the header. */
        static double prev_start = -1.0;
        fprintf(stderr,
                "[sb] start %s, %d bytes, tc=%d -> %.0f Hz | t=%.3f dt=%.3f | "
                "dma1 mode=%02X (%s) page=%02X addr=%04X count=%u\n",
                auto_init ? "auto-init" : "single", len, sb.time_constant, sb.rate,
                sb.last_t, prev_start < 0.0 ? 0.0 : sb.last_t - prev_start,
                dma[1].mode, (dma[1].mode & 0x10) ? "auto-init" : "single",
                dma[1].page, dma[1].base_addr, (unsigned)dma[1].base_count + 1u);
        prev_start = sb.last_t;
    }
}

static void sb_command(uint8_t c){
    switch(c){
    case 0x10: sb.cmd = c; sb.need_args = 1; sb.nargs = 0; return;   /* direct DAC */
    case 0x14: case 0x24:
    case 0x1C: case 0x2C:
        sb.cmd = c; sb.need_args = (c == 0x14 || c == 0x24) ? 2 : 0;
        sb.nargs = 0;
        if(!sb.need_args) sb_start(1, sb.block_size);
        return;
    case 0x40: sb.cmd = c; sb.need_args = 1; sb.nargs = 0; return;   /* time const */
    case 0x48: sb.cmd = c; sb.need_args = 2; sb.nargs = 0; return;   /* block size */
    case 0xD0: sb.playing = 0; return;                               /* halt DMA   */
    case 0xD4: if(sb.block_size) sb.playing = 1; sb.last_t = emu_now(); return;
    case 0xD1: sb.speaker = 1; return;
    case 0xD3: sb.speaker = 0; return;
    case 0xD8: sb_out((uint8_t)(sb.speaker ? 0xFF : 0x00)); return;
    case 0xE1: sb_out(0x01); sb_out(0x05); return;   /* DSP 1.05: an original SB */
    case 0xE0: sb.cmd = c; sb.need_args = 1; sb.nargs = 0; return;   /* identify   */
    case 0xF2: sb.irq_pending = 1; pic_raise(sb_irq); return;        /* force IRQ  */
    default:
        if(sound_debug) fprintf(stderr, "[sb] unhandled DSP command %02X\n", c);
        return;
    }
}

static void sb_command_arg(uint8_t v){
    sb.arg[sb.nargs++] = v;
    if(sb.nargs < sb.need_args) return;
    switch(sb.cmd){
    case 0x10: break;                                   /* direct DAC: ignored */
    case 0x40: sb.time_constant = v; break;
    case 0x48: sb.block_size = (sb.arg[0] | (sb.arg[1] << 8)) + 1; break;
    case 0x14: case 0x24:
        sb_start(0, (sb.arg[0] | (sb.arg[1] << 8)) + 1); break;
    case 0xE0: sb_out((uint8_t)~v); break;
    }
    sb.need_args = 0; sb.nargs = 0;
}

void sb_write(uint16_t p, uint8_t v){
    switch(p - SB_BASE){
    case 0x06:                                   /* DSP reset */
        if(v & 1) sb.reset_stage = 1;
        else if(sb.reset_stage){
            sb.reset_stage = 0;
            sb.playing = 0; sb.outlen = sb.outpos = 0;
            sb.need_args = 0; sb.nargs = 0;
            /* A reset also drops any interrupt the card was still asserting.
             * Without this, a second driver instance (each game program loads
             * its own) unmasks the line and immediately takes an interrupt
             * left over from the previous one, part-way through its own
             * initialisation. */
            sb.irq_pending = 0;
            pic_lower(sb_irq);
            sb_out(0xAA);                        /* the "I am here" reply */
            if(sound_debug) fprintf(stderr, "[sb] DSP reset\n");
        }
        return;
    case 0x0C:                                   /* command / data */
        if(sb.need_args) sb_command_arg(v);
        else sb_command(v);
        return;
    }
}

uint8_t sb_read(uint16_t p){
    switch(p - SB_BASE){
    case 0x0A:                                   /* read data */
        if(sb.outpos < sb.outlen){
            uint8_t r = sb.outbuf[sb.outpos++];
            if(sb.outpos >= sb.outlen) sb.outpos = sb.outlen = 0;
            return r;
        }
        return 0x00;
    case 0x0C: return 0x7F;                      /* write buffer always ready */
    case 0x0E:                                   /* read-buffer status + IRQ ack */
        sb.irq_pending = 0;
        pic_lower(sb_irq);
        return (uint8_t)((sb.outpos < sb.outlen) ? 0xFF : 0x7F);
    }
    return 0xFF;
}

/* Called often from the main loop: move emulated time forward, pull the bytes
 * the card would have consumed in that interval, and hand them to the host. */
#define SB_CHUNK 512
static int16_t pcm[SB_CHUNK];
static int pcm_n;

void sb_tick(void){
    double now, dt, t_first;
    int due;
    if(!sb.playing || sb.last_t < 0.0) return;
    now = emu_now();
    dt = now - sb.last_t;
    if(dt <= 0.0) return;
    if(dt > 0.25) dt = 0.25;                     /* never try to catch up far */
    /* when the first sample of this tick is due: one whole sample after the
     * last, less the fraction already carried */
    t_first = sb.last_t + (1.0 - sb.frac) / sb.rate;
    sb.last_t = now;
    sb.frac += dt * sb.rate;
    due = (int)sb.frac;
    if(due <= 0) return;
    sb.frac -= due;
    if(due > 4096) due = 4096;
    while(due-- > 0){
        int eob, b = dma_fetch(1, &eob);
        if(b < 0){ sb.playing = 0; break; }
        pcm[pcm_n++] = (int16_t)((b - 128) * 192);
        if(pcm_n == SB_CHUNK){ wav_push(pcm, pcm_n); pcm_n = 0; }
        /* What ends a transfer, and so interrupts: the DSP's own byte count,
         * which is the card's job on hardware.  The controller's wrap (eob)
         * is invisible to the DSP - it just reloads and keeps going.  With no
         * length ever given (a 1Ch with no preceding 48h, which none of the
         * drivers here do) there is no count to run down, so fall back to the
         * wrap rather than fire on every single sample. */
        { int fire;
          if(sb_dmairq || sb.block_size <= 0) fire = eob;
          else fire = (--sb.dsp_left <= 0);
          if(fire){
              sb.dsp_left = sb.block_size;
              sb.irq_pending = 1;
              pic_raise(sb_irq);
              if(!sb.auto_init) sb.playing = 0;
          } }
    }
    (void)t_first;
}

/* ---------------------------------------------------------------- OPL stub */
static uint8_t opl_regs[512];
void opl_write(int reg, uint8_t v){ opl_regs[reg & 511] = v; }
uint8_t opl_status(void){ return 0x06; }
void spk_update(int on, uint16_t div){ (void)on; (void)div; }


void sound_init(void){ dma_reset(); sb_reset_dev(); }
