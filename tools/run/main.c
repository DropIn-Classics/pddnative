/* pddrun: runs one of the shipped programs on a headless PC, on an emulated
 * clock, and reports what it was asked to look at.
 *
 *     pddrun -game DIR [options] PROGRAM [ARGS]
 *
 * PROGRAM is a path on the guest's C: drive, whose root is the unpacked CD
 * (DIR): DREAMS1\PD.EXE, DELUXE\DDPCMAIN.EXE.  It starts with its own
 * directory as the current one.  Nothing depends on the host's clock: the
 * same arguments give the same run (the report ends with hashes of memory to
 * check that).  tools/run.py is the friendlier front end (finds the CD,
 * builds this program, takes addresses by their names in the hints).
 *
 * Options (T = emulated seconds; ADDR = linear hex "A1B2C", absolute
 * "SEG:OFF", or "PROG+SEG:OFF": the frame SEG of the program named PROG
 * (base name, e.g. PD.EXE), i.e. relative to its load segment, as the
 * frames in the hints are; "+SEG:OFF" is the first program):
 *
 *   -state DIR       the writable layer over C: (default build/run/state)
 *   -sound none|sb|keep
 *                    write C:\DELUXE\SOUND.CFG into the layer: NOSOUND.SDR,
 *                    or SBLASTER.SDR at 220h, IRQ 7, quality 0 (default
 *                    none; keep = leave what is there)
 *   -loadfix         the programs load above the first 64 KB (DOS's LOADFIX;
 *                    the sound driver's unpacker fails below it, as under
 *                    DDPCMAIN.EXE)
 *   -until T         stop at T (default 30)
 *   -ips N           emulated instructions per second (default 6000000)
 *   -key T KEY       a key at T: tapped (down, up 0.15 s later), or KEY+ /
 *                    KEY- for down / up only.  KEY is a name (esc, enter,
 *                    space, lshift, rshift, lctrl, lalt, f1..f12, up, down,
 *                    left, right, a..z, 0..9, tab, backspace) or a hex
 *                    scancode (E0xx for the extended keys)
 *   -keys FILE       key events from a file, "T KEY" a line, # comments
 *   -shot T FILE     the screen at T as PNG
 *   -shotevery DT PREFIX   the screen every DT seconds, PREFIX_NNNNN.png
 *   -break ADDR[#N]  stop before the instruction at ADDR (the Nth time)
 *   -log ADDR        print the registers each time ADDR is reached, go on
 *   -poke ADDR[#N] TARGET HEX   when ADDR is reached (the Nth time), write
 *                    the bytes HEX ("04 00" or "0400") at TARGET, go on
 *   -watch ADDR      print each write to the byte at ADDR
 *   -trace FILE N    one line per instruction for N instructions, from the
 *                    first -break/-log hit on (or from the start without one)
 *   -dump ADDR LEN   print LEN bytes at ADDR at the end (repeatable)
 *   -dumpevery DT    print the -dump regions every DT seconds as well (from
 *                    t=DT on): a time series of chosen variables
 *   -ram FILE        write memory 0-A0000h at the end
 *   -vram FILE       write the 256 KB of video memory (planes interleaved,
 *                    byte 4*o+p = plane p, offset o) at the end
 *   -wav FILE        what the Sound Blaster played
 *   -dos             print every INT 21h call
 *   -intwatch NN     print every INT NN call (hex)
 *   -prof            the busiest CS:IP at the end
 *   -v               the devices' and DOS's trace on stderr
 */
#include "pddrun.h"
#include <stdarg.h>
#include <time.h>

uint8_t *ram;
int trace_level = 0;
void trc(const char *fmt, ...){
    va_list ap;
    if(!trace_level) return;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}
void dos_flush_con(void);

static void die(const char *fmt, ...){
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "pddrun: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(2);
}

/* ------------------------------------------------------------ addresses */
typedef struct {
    char prog[16];        /* "" = absolute; "+" = the first program */
    int rel;              /* SEG is a frame of prog */
    uint32_t seg, off;
    uint32_t lin;         /* resolved; 0xFFFFFFFF until prog is loaded */
} Addr;

static Addr parse_addr(const char *s){
    Addr a;
    const char *p = s, *plus = strchr(s, '+');
    memset(&a, 0, sizeof(a));
    a.lin = 0xFFFFFFFFu;
    if(plus){
        size_t n = (size_t)(plus - s);
        if(n >= sizeof(a.prog)) die("program name too long in %s", s);
        if(n == 0) snprintf(a.prog, sizeof(a.prog), "+");
        else { memcpy(a.prog, s, n); a.prog[n] = 0; }
        a.rel = 1;
        p = plus + 1;
    }
    if(strchr(p, ':')){
        if(sscanf(p, "%x:%x", &a.seg, &a.off) != 2) die("bad address %s", s);
        if(!a.rel) a.lin = (a.seg << 4) + a.off;
    } else {
        if(a.rel) die("%s: a program-relative address needs SEG:OFF", s);
        if(sscanf(p, "%x", &a.lin) != 1) die("bad address %s", s);
    }
    return a;
}

static char first_prog[16];
static const char *base_name(const char *path){
    const char *b = path, *p;
    for(p = path; *p; p++) if(*p=='\\' || *p=='/' || *p==':') b = p+1;
    return b;
}
/* as it was given: "PD.EXE+08BD:9AA0", "+0000:1251", "A1B2C" */
static const char *addr_str(const Addr *a){
    static char s[4][48];
    static int k = 0;
    char *o = s[k++ & 3];
    if(a->rel) snprintf(o, sizeof(s[0]), "%s+%04X:%04X", strcmp(a->prog, "+") ? a->prog : "",
                        (unsigned)a->seg, (unsigned)a->off);
    else snprintf(o, sizeof(s[0]), "%05X", (unsigned)a->lin);
    return o;
}
static void resolve(Addr *a, const char *prog, uint16_t load){
    if(!a->rel) return;
    if(!strcmp(a->prog, "+") ? _stricmp(prog, first_prog) != 0 : _stricmp(prog, a->prog) != 0) return;
    a->lin = (((uint32_t)load + a->seg) << 4) + a->off;
}

/* ------------------------------------------------------------ settings */
#define MAXEV 4096
typedef struct { double t; int sc, down; } KeyEv;
static KeyEv keys[MAXEV];
static int nkeys = 0, key_pos = 0;

typedef struct { double t; char file[260]; } Shot;
static Shot shots[256];
static int nshots = 0, shot_pos = 0;
static double shot_every = 0.0, shot_next = 0.0;
static char shot_prefix[260];
static unsigned shot_index = 0;

/* stop: 0 -log, 1 -break, 2 -poke (pa, pb, pn: where and what it writes) */
typedef struct { Addr a; int stop; int count, hits; Addr pa; uint8_t pb[16]; int pn; } Brk;
static Brk brks[BRK_MAX];
static int nbrks = 0;

static Addr watch_addr;
static int have_watch = 0;

typedef struct { Addr a; uint32_t len; } Dump;
static Dump dumps[32];
static int ndumps = 0;
static double dump_every = 0.0, dump_next = 0.0;

static const char *trace_file = NULL;
static uint64_t trace_count = 0;

/* --------------------------------------------------------------- keys */
static const struct { const char *name; int sc; } keynames[] = {
    {"esc",0x01},{"1",0x02},{"2",0x03},{"3",0x04},{"4",0x05},{"5",0x06},{"6",0x07},
    {"7",0x08},{"8",0x09},{"9",0x0A},{"0",0x0B},{"minus",0x0C},{"equals",0x0D},
    {"backspace",0x0E},{"tab",0x0F},{"q",0x10},{"w",0x11},{"e",0x12},{"r",0x13},
    {"t",0x14},{"y",0x15},{"u",0x16},{"i",0x17},{"o",0x18},{"p",0x19},{"enter",0x1C},
    {"lctrl",0x1D},{"a",0x1E},{"s",0x1F},{"d",0x20},{"f",0x21},{"g",0x22},{"h",0x23},
    {"j",0x24},{"k",0x25},{"l",0x26},{"lshift",0x2A},{"z",0x2C},{"x",0x2D},{"c",0x2E},
    {"v",0x2F},{"b",0x30},{"n",0x31},{"m",0x32},{"rshift",0x36},{"lalt",0x38},
    {"space",0x39},{"f1",0x3B},{"f2",0x3C},{"f3",0x3D},{"f4",0x3E},{"f5",0x3F},
    {"f6",0x40},{"f7",0x41},{"f8",0x42},{"f9",0x43},{"f10",0x44},{"f11",0x57},
    {"f12",0x58},{"rctrl",0xE01D},{"ralt",0xE038},{"up",0xE048},{"down",0xE050},
    {"left",0xE04B},{"right",0xE04D},{NULL,0}
};
static int key_code(const char *k){
    int i;
    unsigned v;
    char *end;
    for(i=0;keynames[i].name;i++) if(!_stricmp(k, keynames[i].name)) return keynames[i].sc;
    v = (unsigned)strtoul(k, &end, 16);
    if(*end || !*k) die("unknown key %s", k);
    return (int)v;
}
static void add_key(double t, const char *spec){
    char k[32];
    size_t n = strlen(spec);
    int mode = 0;                         /* 0 tap, 1 down, 2 up */
    if(n >= sizeof(k)) die("bad key %s", spec);
    snprintf(k, sizeof(k), "%s", spec);
    if(n > 1 && k[n-1]=='+'){ mode = 1; k[n-1] = 0; }
    else if(n > 1 && k[n-1]=='-'){ mode = 2; k[n-1] = 0; }
    if(nkeys + 2 > MAXEV) die("too many key events");
    if(mode != 2){ keys[nkeys].t = t; keys[nkeys].sc = key_code(k); keys[nkeys].down = 1; nkeys++; }
    if(mode != 1){ keys[nkeys].t = mode ? t : t + 0.15; keys[nkeys].sc = key_code(k); keys[nkeys].down = 0; nkeys++; }
}
static int key_cmp(const void *a, const void *b){
    const KeyEv *x = (const KeyEv*)a, *y = (const KeyEv*)b;
    return x->t < y->t ? -1 : x->t > y->t ? 1 : 0;
}
static void read_keys(const char *path){
    FILE *f = fopen(path, "r");
    char line[256];
    if(!f) die("cannot read %s", path);
    while(fgets(line, sizeof(line), f)){
        double t; char k[64];
        char *h = strchr(line, '#');
        if(h) *h = 0;
        if(sscanf(line, "%lf %63s", &t, k) == 2) add_key(t, k);
    }
    fclose(f);
}

/* --------------------------------------------------------------- output */
static uint32_t fb[1024*1024];
static void shot(const char *path){
    int w, h;
    vga_render(fb, &w, &h);
    if(!save_png(path, fb, w, h)) fprintf(stderr, "pddrun: cannot write %s\n", path);
    else printf("shot %s %dx%d t=%.6f\n", path, w, h, emu_now());
}
static uint64_t fnv(const uint8_t *p, size_t n){
    uint64_t h = 1469598103934665603ULL;
    size_t i;
    for(i=0;i<n;i++){ h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}
static void print_regs(void){
    printf("regs CS:IP=%04X:%04X AX=%04X BX=%04X CX=%04X DX=%04X SI=%04X DI=%04X BP=%04X SP=%04X"
           " DS=%04X ES=%04X SS=%04X F=%04X\n",
           cpu.sreg[S_CS], (unsigned)cpu.eip, REG16(R_EAX), REG16(R_EBX), REG16(R_ECX),
           REG16(R_EDX), REG16(R_ESI), REG16(R_EDI), REG16(R_EBP), REG16(R_ESP),
           cpu.sreg[S_DS], cpu.sreg[S_ES], cpu.sreg[S_SS], (unsigned)(cpu_getflags() & 0xFFFF));
}
static void print_dumps(void){
    int i;
    for(i=0;i<ndumps;i++){
        uint32_t k, lin = dumps[i].a.lin;
        if(lin == 0xFFFFFFFFu){ printf("dump: %s never loaded\n", dumps[i].a.prog); continue; }
        for(k=0;k<dumps[i].len;k+=16){
            uint32_t j;
            printf("dump %05X:", lin + k);
            for(j=k;j<k+16 && j<dumps[i].len;j++) printf(" %02X", mem_r8(lin + j));
            printf("\n");
        }
    }
}
static void write_file(const char *path, const uint8_t *p, size_t n){
    FILE *f = fopen(path, "wb");
    if(!f){ fprintf(stderr, "pddrun: cannot write %s\n", path); return; }
    fwrite(p, 1, n, f);
    fclose(f);
}

/* ------------------------------------------------------------ programs */
static void on_load(const char *dospath, uint16_t load){
    int i;
    const char *b = base_name(dospath);
    printf("load %s at %04X t=%.6f\n", dospath, load, emu_now());
    if(!first_prog[0]) snprintf(first_prog, sizeof(first_prog), "%s", b);
    for(i=0;i<nbrks;i++){
        uint32_t before = brks[i].a.lin;
        resolve(&brks[i].a, b, load);
        if(brks[i].a.lin != before) brk_lin[i] = brks[i].a.lin;
        if(brks[i].stop == 2) resolve(&brks[i].pa, b, load);
    }
    if(have_watch){ resolve(&watch_addr, b, load); memwatch_addr = watch_addr.lin; }
    for(i=0;i<ndumps;i++) resolve(&dumps[i].a, b, load);
}

static void write_sound_cfg(const char *mode){
    char host[700];
    FILE *f;
    uint8_t cfg[25];
    size_t n;
    memset(cfg, 0, sizeof(cfg));
    if(!strcmp(mode, "keep")) return;
    if(!strcmp(mode, "none")){
        /* what SETSOUND writes for NOSOUND.SDR */
        memcpy(cfg, "NOSOUND.SDR", 11); cfg[0x0E] = 0x64; n = 16;
    } else if(!strcmp(mode, "sb")){
        /* SBLASTER.SDR: base index 1 (220h), IRQ index 3 (IRQ 7), quality 0
         * (as the driver parses the file) */
        memcpy(cfg, "SBLASTER.SDR", 12); cfg[0x0E] = 1; cfg[0x11] = 3; cfg[0x14] = 0; n = 25;
    } else { die("-sound takes none, sb or keep"); return; }
    if(!dos_host_path("\\DELUXE\\SOUND.CFG", host, sizeof(host), 1) || !(f = fopen(host, "wb")))
        die("cannot write SOUND.CFG into the layer");
    fwrite(cfg, 1, n, f);
    fclose(f);
}

/* ---------------------------------------------------------------- main */
int main(int argc, char **argv){
    const char *game = NULL, *state = "build/run/state", *sound = "none";
    const char *ram_file = NULL, *vram_file = NULL, *wav_file = NULL;
    double until = 30.0;
    char prog[260] = "", tail[128] = "";
    const char *stop = "until";
    uint16_t load;
    int i, r, loadfix = 0;
    clock_t c0;

    for(i=1;i<argc;i++){
        const char *a = argv[i];
        #define NEED(k) if(i + (k) >= argc) die("%s needs %d argument(s)", a, k)
        if(prog[0]){
            /* everything after the program is its command line */
            size_t l = strlen(tail);
            snprintf(tail + l, sizeof(tail) - l, " %s", a);
        }
        else if(!strcmp(a,"-game")){ NEED(1); game = argv[++i]; }
        else if(!strcmp(a,"-state")){ NEED(1); state = argv[++i]; }
        else if(!strcmp(a,"-sound")){ NEED(1); sound = argv[++i]; }
        else if(!strcmp(a,"-until")){ NEED(1); until = atof(argv[++i]); }
        else if(!strcmp(a,"-ips")){ NEED(1); emu_ips = atof(argv[++i]); }
        else if(!strcmp(a,"-key")){ NEED(2); add_key(atof(argv[i+1]), argv[i+2]); i += 2; }
        else if(!strcmp(a,"-keys")){ NEED(1); read_keys(argv[++i]); }
        else if(!strcmp(a,"-shot")){ NEED(2);
            if(nshots == 256) die("too many -shot");
            shots[nshots].t = atof(argv[i+1]);
            snprintf(shots[nshots].file, sizeof(shots[nshots].file), "%s", argv[i+2]);
            nshots++; i += 2; }
        else if(!strcmp(a,"-shotevery")){ NEED(2); shot_every = atof(argv[i+1]);
            snprintf(shot_prefix, sizeof(shot_prefix), "%s", argv[i+2]); i += 2; }
        else if(!strcmp(a,"-break") || !strcmp(a,"-log")){ NEED(1);
            char spec[64], *hash;
            if(nbrks == BRK_MAX) die("at most %d -break/-log", BRK_MAX);
            snprintf(spec, sizeof(spec), "%s", argv[++i]);
            hash = strchr(spec, '#');
            brks[nbrks].count = 1;
            if(hash){ *hash = 0; brks[nbrks].count = atoi(hash+1); }
            brks[nbrks].a = parse_addr(spec);
            brks[nbrks].stop = !strcmp(a,"-break");
            brk_lin[nbrks] = brks[nbrks].a.lin;
            nbrks++; }
        else if(!strcmp(a,"-poke")){ NEED(3);
            char spec[64], *hash;
            const char *h;
            Brk *b = &brks[nbrks];
            if(nbrks == BRK_MAX) die("at most %d -break/-log/-poke", BRK_MAX);
            snprintf(spec, sizeof(spec), "%s", argv[++i]);
            hash = strchr(spec, '#');
            b->count = 1;
            if(hash){ *hash = 0; b->count = atoi(hash+1); }
            b->a = parse_addr(spec);
            b->stop = 2;
            b->pa = parse_addr(argv[++i]);
            for(h = argv[++i], b->pn = 0; *h; ){
                char two[3];
                if(*h == ' '){ h++; continue; }
                if(!h[1] || b->pn == 16) die("-poke: bad bytes %s", argv[i]);
                two[0] = h[0]; two[1] = h[1]; two[2] = 0;
                b->pb[b->pn++] = (uint8_t)strtoul(two, NULL, 16);
                h += 2;
            }
            brk_lin[nbrks] = b->a.lin;
            nbrks++; }
        else if(!strcmp(a,"-watch")){ NEED(1); watch_addr = parse_addr(argv[++i]); have_watch = 1;
            memwatch_addr = watch_addr.lin; }
        else if(!strcmp(a,"-trace")){ NEED(2); trace_file = argv[i+1];
            trace_count = strtoull(argv[i+2], NULL, 10); i += 2; }
        else if(!strcmp(a,"-dump")){ NEED(2);
            if(ndumps == 32) die("too many -dump");
            dumps[ndumps].a = parse_addr(argv[i+1]);
            dumps[ndumps].len = (uint32_t)strtoul(argv[i+2], NULL, 0);
            ndumps++; i += 2; }
        else if(!strcmp(a,"-dumpevery")){ NEED(1); dump_every = atof(argv[++i]);
            dump_next = dump_every; }
        else if(!strcmp(a,"-ram")){ NEED(1); ram_file = argv[++i]; }
        else if(!strcmp(a,"-vram")){ NEED(1); vram_file = argv[++i]; }
        else if(!strcmp(a,"-wav")){ NEED(1); wav_file = argv[++i]; }
        else if(!strcmp(a,"-dos")) dos_log = 1;
        else if(!strcmp(a,"-loadfix")) loadfix = 1;
        else if(!strcmp(a,"-intwatch")){ NEED(1); int_watch = (int)strtol(argv[++i], NULL, 16); }
        else if(!strcmp(a,"-prof")) prof_on = 1;
        else if(!strcmp(a,"-v")) trace_level = 1;
        else if(a[0]=='-') die("unknown option %s", a);
        else snprintf(prog, sizeof(prog), "%s", a);
        #undef NEED
    }
    if(!game) die("-game DIR (the unpacked CD) is needed");
    if(!prog[0]) die("no program given");
    if(emu_ips < 100000.0) die("-ips too small");
    emu_inv_ips = 1.0 / emu_ips;
    qsort(keys, (size_t)nkeys, sizeof(KeyEv), key_cmp);
    shot_next = 0.0;                 /* picture n is at n*DT */

    ram = (uint8_t*)calloc(RAM_ALLOC, 1);
    if(!ram) die("out of memory");
    cpu_reset();
    vga_init();
    dev_init();
    bios_init();
    dos_init(game, state);
    if(loadfix) dos_loadfix();
    dos_on_load = on_load;
    write_sound_cfg(sound);
    if(wav_file) sound_wav_open(wav_file);

    /* Park the CPU on a HLT in ROM with an IRET frame above it, the place a
     * program that leaves nothing to return to ends up. */
    ram[0xFFFF0] = 0xF4;
    set_sreg(S_CS,0xF000); cpu.eip = 0xFFF0;
    set_sreg(S_SS,0x0050); REG16(R_ESP) = 0x0100;
    set_sreg(S_DS,0x0000); set_sreg(S_ES,0x0000);
    cpu.iflag = 1;
    {
        uint32_t sp;
        REG16(R_ESP) -= 6;
        sp = cpu.sbase[S_SS] + REG16(R_ESP);
        mem_w16(sp+0, 0xFFF0);
        mem_w16(sp+2, 0xF000);
        mem_w16(sp+4, 0x0202);
    }
    r = dos_start(prog, tail[0] ? tail : "", &load);
    if(r) die("cannot start %s from %s (DOS error %d)", prog, game, r);
    for(i=0;i<nbrks;i++) if(brks[i].a.lin == 0xFFFFFFFFu)
        fprintf(stderr, "pddrun: note: %s waits for that program to be loaded\n",
                addr_str(&brks[i].a));
    if(trace_file){
        xtrace_fp = fopen(trace_file, "w");
        if(!xtrace_fp) die("cannot write %s", trace_file);
        xtrace_left = trace_count ? trace_count : ~(uint64_t)0;
    }
    {   /* the trace starts at the first hit when there are breakpoints */
        static FILE *held = NULL;
        if(xtrace_fp && nbrks){ held = xtrace_fp; xtrace_fp = NULL; }
        brk_n = nbrks;
        c0 = clock();

        while(!cpu.shutdown){
            uint64_t dl, until_c;
            double now = emu_now();
            int n, lim;
            if(now >= until) break;
            /* due events, on the emulated clock */
            while(key_pos < nkeys && keys[key_pos].t <= now){
                kbd_key(keys[key_pos].sc, keys[key_pos].down); key_pos++;
            }
            while(shot_pos < nshots && shots[shot_pos].t <= now){
                shot(shots[shot_pos].file); shot_pos++;
            }
            if(shot_every > 0.0 && shot_next <= now){
                char path[300];
                snprintf(path, sizeof(path), "%s_%05u.png", shot_prefix, shot_index++);
                shot(path);
                shot_next += shot_every;
            }
            if(dump_every > 0.0 && dump_next <= now){
                printf("at t=%.6f\n", now);
                print_dumps();
                dump_next += dump_every;
            }
            /* the next of them bounds the batch */
            {
                double next = until;
                if(key_pos < nkeys && keys[key_pos].t < next) next = keys[key_pos].t;
                if(shot_pos < nshots && shots[shot_pos].t < next) next = shots[shot_pos].t;
                if(shot_every > 0.0 && shot_next < next) next = shot_next;
                if(dump_every > 0.0 && dump_next < next) next = dump_next;
                until_c = cpu.cycles + (uint64_t)((next - now) * emu_ips) + 1;
            }
            if(cpu.halted){
                /* idle: move the clock on to the next thing that can happen */
                uint64_t step = (uint64_t)(emu_ips / 10000.0);
                dl = dev_next_deadline();
                if(dl > cpu.cycles && dl - cpu.cycles < step) step = dl - cpu.cycles;
                if(until_c > cpu.cycles && until_c - cpu.cycles < step) step = until_c - cpu.cycles;
                if(step < 1) step = 1;
                cpu.cycles += step;
                dev_tick();
            } else {
                /* up to 256 cycles (an instruction is one, a REP string
                 * instruction one per element), never past the timer's
                 * deadline: IRQ0 lands on the instruction it is due on, not
                 * up to 256 REPs later (a loop of REPE SCASB, as DDPCMAIN's
                 * key wait, got its timer interrupt 1.4 ms late) */
                uint64_t end;
                dl = dev_next_deadline();
                if(until_c < dl) dl = until_c;
                lim = 256;
                if(dl <= cpu.cycles) lim = 1;
                else if(dl - cpu.cycles < 256) lim = (int)(dl - cpu.cycles);
                for(end = cpu.cycles + (uint64_t)lim; cpu.cycles < end && !cpu.shutdown; ) cpu_step();
                dev_tick();
            }
            if(brk_hit >= 0){
                /* every -break, -log and -poke at this address; a poke is
                 * written before a stop at the same pass */
                uint32_t lin = brk_lin[brk_hit];
                Brk *stopped = NULL;
                brk_hit = -1;
                if(held){ xtrace_fp = held; held = NULL; }
                for(i=0;i<nbrks;i++){
                    Brk *b = &brks[i];
                    if(brk_lin[i] != lin) continue;
                    b->hits++;
                    if(b->stop == 2){
                        if(b->hits == b->count){
                            int k;
                            for(k=0;k<b->pn;k++) mem_w8(b->pa.lin + k, b->pb[k]);
                            printf("poke %s t=%.6f hit=%d\n", addr_str(&b->pa), emu_now(), b->hits);
                        }
                    } else if(!b->stop){
                        printf("log %s t=%.6f hit=%d ", addr_str(&b->a), emu_now(), b->hits);
                        print_regs();
                    } else if(b->hits >= b->count && !stopped)
                        stopped = b;
                }
                if(stopped){
                    stop = "break";
                    printf("break %d at %04X:%04X t=%.6f hit=%d\n", (int)(stopped - brks),
                           cpu.sreg[S_CS], (unsigned)cpu.eip, emu_now(), stopped->hits);
                    break;
                }
                /* go on: step over this instruction without stopping again */
                brk_resume = lin;
                cpu.shutdown = 0;
                continue;
            }
            if(cpu.iflag){
                int v = pic_pending();
                if(v >= 0) cpu_interrupt(v, 0);
            }
        }
    }
    dos_flush_con();
    if(dos_done) stop = "exit";
    emu_advance();

    printf("stop %s t=%.6f instructions=%llu frames=%lu host=%.2fs\n", stop, emu_now(),
           (unsigned long long)cpu.cycles, vsync_edges, (double)(clock() - c0) / CLOCKS_PER_SEC);
    if(dos_done) printf("exit code %d\n", dos_exit_code);
    print_regs();
    dev_report();
    for(i=0;i<nbrks;i++)
        printf("%s %s lin=%05X hits=%d\n", brks[i].stop == 1 ? "break" : brks[i].stop ? "poke" : "log",
               addr_str(&brks[i].a), brks[i].a.lin, brks[i].hits);
    print_dumps();
    memwatch_report();
    prof_report();
    printf("hash ram %016llx vram %016llx\n",
           (unsigned long long)fnv(ram, 0xA0000), (unsigned long long)fnv(vga_vram, sizeof(vga_vram)));
    if(ram_file) write_file(ram_file, ram, 0xA0000);
    if(vram_file) write_file(vram_file, vga_vram, sizeof(vga_vram));
    if(xtrace_fp) fclose(xtrace_fp);
    sound_wav_close();
    return 0;
}
