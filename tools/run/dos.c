/* A small MS-DOS 5 work-alike: MCB memory chain, PSPs, EXEC, file handles.
 *
 * Memory, PSPs, EXEC and the resident-driver handling are pfemu's (see
 * pddrun.h).  The file side is this runner's own: the guest has one drive,
 * C:, whose root is the unpacked CD (game_dir) with a writable layer above
 * it (state_dir).  A path is looked up in the layer first, then on the CD;
 * a file opened for writing (or created) lives in the layer, copied up from
 * the CD on first use.  The CD is never written.  The current directory is
 * kept as DOS keeps it, because the programs rely on it: the table programs
 * open their table files relative to it and change to \DELUXE for the sound
 * set-up.
 *
 * Nothing here reads the host's clock: date and time are a fixed day plus
 * the emulated time, and file times are that same day, so two runs with the
 * same inputs see the same things.
 */
#include "pddrun.h"
#include <sys/stat.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#define mkdir_host(p) _mkdir(p)
#else
#include <dirent.h>
#include <unistd.h>
#define mkdir_host(p) mkdir((p), 0777)
#endif

#define AX REG16(R_EAX)
#define BX REG16(R_EBX)
#define CX REG16(R_ECX)
#define DX REG16(R_EDX)
#define SI REG16(R_ESI)
#define DI REG16(R_EDI)
#define AL REG8(0)
#define CL REG8(1)
#define DL REG8(2)
#define BL REG8(3)
#define AH REG8(4)
#define CH REG8(5)
#define DH REG8(6)
#define BH REG8(7)

int dos_done = 0;
int dos_exit_code = -1;
int dos_log = 0;
void (*dos_on_load)(const char *dospath, uint16_t load_seg) = NULL;

/* The fixed date the guest sees: 1 October 1994, a Saturday (the CD's
 * year; any day would do as long as it never changes). */
#define FIX_YEAR 1994
#define FIX_MON  10
#define FIX_DAY  1
#define FIX_WDAY 6
#define FIX_DOSDATE ((uint16_t)(((FIX_YEAR-1980)<<9) | (FIX_MON<<5) | FIX_DAY))

static char game_root[512], state_root[512];

#define MEM_FIRST 0x0060
#define MEM_LAST  0x9FC0

/* ------------------------------------------------------------------ MCB */
static void mcb_init(void){
    uint32_t a = MEM_FIRST*16;
    ram[a] = 'Z';
    st16u(&ram[a+1], 0);
    st16u(&ram[a+3], (uint16_t)(MEM_LAST - MEM_FIRST - 1));
    memset(&ram[a+8], 0, 8);
}
/* INT 21h AH=58h: 0 first fit, 1 best fit, 2 last fit (a program that
 * leaves a driver resident may ask for last fit so the driver lands at the
 * top of memory). */
static uint16_t dos_alloc_strategy = 0;

static uint16_t mcb_alloc(uint16_t paras, uint16_t owner, uint16_t *largest){
    uint16_t seg = MEM_FIRST;
    uint16_t chosen = 0, chosen_sz = 0;
    int strat = dos_alloc_strategy & 3;
    *largest = 0;
    for(;;){
        uint32_t a = (uint32_t)seg*16;
        uint8_t sig = ram[a];
        uint16_t own = ld16u(&ram[a+1]);
        uint16_t sz  = ld16u(&ram[a+3]);
        if(sig!='M' && sig!='Z') return 0;
        if(own==0){
            if(sz > *largest) *largest = sz;
            if(sz >= paras){
                if(!chosen) { chosen = seg; chosen_sz = sz; }
                else if(strat==1 && sz < chosen_sz){ chosen = seg; chosen_sz = sz; }
                else if(strat==2){ chosen = seg; chosen_sz = sz; }
                if(strat==0) break;
            }
        }
        if(sig=='Z') break;
        seg = (uint16_t)(seg + sz + 1);
    }
    if(!chosen) return 0;
    {
        uint32_t a = (uint32_t)chosen*16;
        uint8_t sig = ram[a];
        uint16_t sz = chosen_sz;
        if(strat==2 && sz > paras + 1){
            /* last fit: carve the block off the TOP of the free area */
            uint16_t nseg = (uint16_t)(chosen + (sz - paras - 1) + 1);
            uint32_t na = (uint32_t)nseg*16;
            ram[na] = sig;
            st16u(&ram[na+1], owner);
            st16u(&ram[na+3], paras);
            memset(&ram[na+8],0,8);
            ram[a] = 'M';
            st16u(&ram[a+3], (uint16_t)(sz - paras - 1));
            return (uint16_t)(nseg + 1);
        }
        if(sz > paras + 1){
            uint16_t nseg = (uint16_t)(chosen + paras + 1);
            uint32_t na = (uint32_t)nseg*16;
            ram[na] = sig;
            st16u(&ram[na+1], 0);
            st16u(&ram[na+3], (uint16_t)(sz - paras - 1));
            memset(&ram[na+8],0,8);
            ram[a] = 'M';
            st16u(&ram[a+3], paras);
        }
        st16u(&ram[a+1], owner);
        return (uint16_t)(chosen+1);
    }
}
static void mcb_coalesce(void){
    uint16_t seg = MEM_FIRST;
    for(;;){
        uint32_t a = (uint32_t)seg*16;
        uint8_t sig = ram[a];
        uint16_t own = ld16u(&ram[a+1]);
        uint16_t sz  = ld16u(&ram[a+3]);
        if(sig=='Z') break;
        if(own==0){
            uint16_t nseg = (uint16_t)(seg + sz + 1);
            uint32_t na = (uint32_t)nseg*16;
            if((ram[na]=='M'||ram[na]=='Z') && ld16u(&ram[na+1])==0){
                st16u(&ram[a+3], (uint16_t)(sz + ld16u(&ram[na+3]) + 1));
                ram[a] = ram[na];
                continue;
            }
        }
        seg = (uint16_t)(seg + sz + 1);
    }
}
static int mcb_valid(uint16_t seg){
    return seg > MEM_FIRST && (uint32_t)seg*16 < RAM_SIZE;
}
static int mcb_free(uint16_t seg){
    uint32_t a;
    if(!mcb_valid(seg)) return 0;
    a = (uint32_t)(seg-1)*16;
    if(ram[a]!='M' && ram[a]!='Z') return 0;
    st16u(&ram[a+1], 0);
    mcb_coalesce();
    return 1;
}
static int mcb_resize(uint16_t seg, uint16_t paras, uint16_t *avail){
    uint32_t a;
    uint16_t sz;
    uint8_t sig;
    if(!mcb_valid(seg)){ if(avail) *avail = 0; return 0; }
    a = (uint32_t)(seg-1)*16;
    sz = ld16u(&ram[a+3]);
    sig = ram[a];
    if(paras <= sz){
        if(sz > paras){
            uint16_t nseg = (uint16_t)(seg + paras);
            uint32_t na = (uint32_t)nseg*16;
            ram[na] = sig;
            st16u(&ram[na+1], 0);
            st16u(&ram[na+3], (uint16_t)(sz - paras - 1));
            ram[a] = 'M';
            st16u(&ram[a+3], paras);
            mcb_coalesce();
        }
        *avail = paras; return 1;
    }
    {   /* try to grow into the following free block */
        uint16_t nseg = (uint16_t)(seg + sz);
        uint32_t na = (uint32_t)nseg*16;
        if(sig=='M' && (ram[na]=='M'||ram[na]=='Z') && ld16u(&ram[na+1])==0){
            uint16_t total = (uint16_t)(sz + ld16u(&ram[na+3]) + 1);
            if(total >= paras){
                ram[a] = ram[na];
                st16u(&ram[a+3], total);
                return mcb_resize(seg, paras, avail);
            }
            *avail = total; return 0;
        }
    }
    *avail = sz;
    return 0;
}

/* Resident children (the sound driver a program EXECs and leaves resident
 * with AH=31h) are released with the program that loaded them, and their
 * interrupt hooks undone, as pfemu does: the programs never unload their
 * driver themselves, and the next program would otherwise find its memory
 * split and the vectors pointing into whatever is loaded over the driver. */
static uint32_t ivt_snap[256];
static int ivt_snap_valid = 0;

static void dos_snapshot_ivt(void){
    int i;
    for(i=0;i<256;i++) ivt_snap[i] = mem_r32((uint32_t)i*4);
    ivt_snap_valid = 1;
}
static void ivt_unhook_range(uint16_t lo_seg, uint16_t hi_seg){
    uint32_t lo = (uint32_t)lo_seg * 16, hi = (uint32_t)hi_seg * 16 + 15;
    int i;
    if(!ivt_snap_valid) return;
    for(i=0;i<256;i++){
        uint32_t v = mem_r32((uint32_t)i*4);
        uint32_t lin = ((v >> 16) << 4) + (v & 0xFFFF);
        if(lin >= lo && lin <= hi && v != ivt_snap[i]){
            mem_w32((uint32_t)i*4, ivt_snap[i]);
            trc("[dos]   int %02X restored %08X -> %08X\n", i, v, ivt_snap[i]);
        }
    }
}
static void mcb_free_children(uint16_t parent){
    uint16_t seg = MEM_FIRST;
    for(;;){
        uint32_t a = (uint32_t)seg*16;
        uint8_t sig = ram[a];
        uint16_t own = ld16u(&ram[a+1]);
        uint16_t sz  = ld16u(&ram[a+3]);
        if(sig!='M' && sig!='Z') break;
        if(own && own != 8 && own != parent){
            uint16_t its_parent = ld16u(&ram[(uint32_t)own*16 + 0x16]);
            if(its_parent == parent){
                trc("[dos] releasing resident child %04X of %04X\n", own, parent);
                ivt_unhook_range((uint16_t)(seg+1), (uint16_t)(seg+sz));
                st16u(&ram[a+1], 0);
            }
        }
        if(sig=='Z') break;
        seg = (uint16_t)(seg + sz + 1);
    }
    mcb_coalesce();
}

/* ------------------------------------------------------------ the drive */
/* Guest paths are kept absolute and normalised: upper case, backslashes,
 * starting with a backslash, no drive letter, "." and ".." resolved.
 *
 * Every drive letter names the same tree, but each keeps its own current
 * directory, as DOS does - and the table programs depend on it: they switch
 * to C:, change to \DELUXE, load the sound driver and switch back to the
 * drive they were started from, whose current directory (their own) is
 * where the driver then finds the music.  So a program starts on D: (the
 * CD drive DELUXE.BAT runs from). */
#define START_DRIVE 3
static char cwds[26][128];
static uint8_t cur_drive = START_DRIVE;

static int drive_of(const char *path){
    if(path[0] && path[1]==':'){
        int d = (path[0] & ~0x20) - 'A';
        if(d >= 0 && d < 26) return d;
    }
    return cur_drive;
}

static int dos_normalise(const char *in, char *out, size_t n){
    char buf[260];
    const char *p = in, *cwd = cwds[drive_of(in)];
    size_t o = 0;
    if(p[0] && p[1]==':') p += 2;
    if(*p=='\\' || *p=='/') snprintf(buf, sizeof(buf), "%s", p);
    else snprintf(buf, sizeof(buf), "%s\\%s", strcmp(cwd, "\\") ? cwd : "", p);
    out[o++] = '\\';
    p = buf;
    while(*p){
        const char *e;
        size_t len, i;
        while(*p=='\\' || *p=='/') p++;
        if(!*p) break;
        for(e = p; *e && *e!='\\' && *e!='/'; e++) ;
        len = (size_t)(e - p);
        if(len==1 && p[0]=='.'){ p = e; continue; }
        if(len==2 && p[0]=='.' && p[1]=='.'){
            if(o > 1){ o--; while(o > 1 && out[o-1] != '\\') o--; if(o > 1) o--; }
            p = e; continue;
        }
        if(o > 1){ if(o+1 >= n) return 0; out[o++] = '\\'; }
        for(i=0;i<len;i++){
            char c = p[i];
            if(o+1 >= n) return 0;
            out[o++] = (char)((c>='a'&&c<='z') ? c-32 : c);
        }
        p = e;
    }
    out[o] = 0;
    return 1;
}

static int host_isfile(const char *p){
    struct stat st;
    return stat(p, &st) == 0 && !(st.st_mode & S_IFDIR);
}
static int host_isdir(const char *p){
    struct stat st;
    return stat(p, &st) == 0 && (st.st_mode & S_IFDIR);
}

/* One host directory listing (names only), for FindFirst and, on a host
 * whose file names are case sensitive, for matching a guest name. */
typedef struct { char name[64]; int dir; } Entry;
static int host_list(const char *dir, Entry *out, int max){
    int n = 0;
#ifdef _WIN32
    char pat[600];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    snprintf(pat, sizeof(pat), "%s\\*", dir);
    h = FindFirstFileA(pat, &fd);
    if(h == INVALID_HANDLE_VALUE) return 0;
    do {
        if(!strcmp(fd.cFileName,".") || !strcmp(fd.cFileName,"..")) continue;
        if(n < max && strlen(fd.cFileName) < sizeof(out[n].name)){
            snprintf(out[n].name, sizeof(out[n].name), "%s", fd.cFileName);
            out[n].dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            n++;
        }
    } while(FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir);
    struct dirent *e;
    if(!d) return 0;
    while((e = readdir(d))){
        char full[700];
        if(!strcmp(e->d_name,".") || !strcmp(e->d_name,"..")) continue;
        if(n < max && strlen(e->d_name) < sizeof(out[n].name)){
            snprintf(out[n].name, sizeof(out[n].name), "%s", e->d_name);
            snprintf(full, sizeof(full), "%s/%s", dir, e->d_name);
            out[n].dir = host_isdir(full);
            n++;
        }
    }
    closedir(d);
#endif
    return n;
}

/* Host path of a normalised guest path under one layer root.  On Windows
 * the file system already ignores case; elsewhere each component is looked
 * up without regard to case (a missing one is kept as spelled, so a create
 * makes it in upper case). */
static void layer_path(const char *root, const char *gpath, char *out, size_t n){
#ifdef _WIN32
    size_t i;
    snprintf(out, n, "%s%s", root, gpath);
    for(i=0;out[i];i++) if(out[i]=='/') out[i]='\\';
#else
    char comp[64];
    const char *p = gpath;
    static Entry ents[512];
    snprintf(out, n, "%s", root);
    while(*p){
        const char *e;
        size_t len;
        int i, k;
        while(*p=='\\') p++;
        if(!*p) break;
        for(e = p; *e && *e!='\\'; e++) ;
        len = (size_t)(e - p); if(len >= sizeof(comp)) len = sizeof(comp)-1;
        memcpy(comp, p, len); comp[len] = 0;
        k = host_list(out, ents, 512);
        for(i=0;i<k;i++) if(!_stricmp(ents[i].name, comp)){ snprintf(comp, sizeof(comp), "%s", ents[i].name); break; }
        { size_t l = strlen(out); snprintf(out + l, n - l, "/%s", comp); }
        p = e;
    }
#endif
}

static void mkdirs_for(const char *hostfile){
    char tmp[700];
    size_t i;
    snprintf(tmp, sizeof(tmp), "%s", hostfile);
    for(i = strlen(state_root) + 1; tmp[i]; i++){
        if(tmp[i]=='\\' || tmp[i]=='/'){
            char c = tmp[i]; tmp[i] = 0; mkdir_host(tmp); tmp[i] = c;
        }
    }
}
static int copy_file(const char *src, const char *dst){
    FILE *a = fopen(src, "rb"), *b;
    static char buf[32768];
    size_t got;
    if(!a) return 0;
    b = fopen(dst, "wb");
    if(!b){ fclose(a); return 0; }
    while((got = fread(buf, 1, sizeof(buf), a)) > 0) fwrite(buf, 1, got, b);
    fclose(a); fclose(b);
    return 1;
}

/* Guest path (as the guest wrote it) -> host file.  for_write: the file
 * must end up in the layer (copied up from the CD when it is there).
 * Returns 1 when the host file exists (or, for_write, can be made). */
int dos_host_path(const char *dospath, char *out, size_t n, int for_write){
    char g[260], lay[700], cd[700];
    if(!dos_normalise(dospath, g, sizeof(g))) return 0;
    layer_path(state_root, g, lay, sizeof(lay));
    if(host_isfile(lay)){ snprintf(out, n, "%s", lay); return 1; }
    layer_path(game_root, g, cd, sizeof(cd));
    if(!for_write){ snprintf(out, n, "%s", cd); return host_isfile(cd); }
    mkdirs_for(lay);
    if(host_isfile(cd)) copy_file(cd, lay);
    snprintf(out, n, "%s", lay);
    return 1;
}
static int guest_isdir(const char *g){
    char h[700];
    if(!strcmp(g, "\\")) return 1;
    layer_path(state_root, g, h, sizeof(h));
    if(host_isdir(h)) return 1;
    layer_path(game_root, g, h, sizeof(h));
    return host_isdir(h);
}

static void read_dosstr(uint32_t a, char *out, int n){
    int i;
    for(i=0;i<n-1;i++){ uint8_t c = mem_r8(a+i); if(!c) break; out[i]=(char)c; }
    out[i]=0;
}

/* -------------------------------------------------------------- handles */
typedef struct { FILE *f; int used; char name[260]; } DFile;
static DFile fh[64];
static uint16_t cur_psp;
static uint16_t dta_seg, dta_off;
static uint16_t last_retcode = 0;
static int oa_active = 0;             /* INT 21h AH=0Ah line in progress */

static int alloc_handle(void){
    int i; for(i=5;i<64;i++) if(!fh[i].used) return i; return -1;
}

/* ----------------------------------------------------------- EXEC / load */
typedef struct {
    uint16_t psp, parent_psp, parent_ss, parent_sp, env;
    /* The caller's registers across EXEC: DOS hands them back when the child
     * ends, and programs rely on it (pfemu: TABLE1.PRG uses DS after
     * EXECing its driver without reloading it). */
    uint16_t r_ax, r_bx, r_cx, r_dx, r_si, r_di, r_bp, r_ds, r_es;
} Proc;
static Proc procs[8];
static int nproc = 0;

static void make_psp(uint16_t seg, uint16_t memtop, uint16_t parent, uint16_t env,
                     const char *tail){
    uint32_t a = (uint32_t)seg*16;
    int i;
    memset(&ram[a], 0, 256);
    ram[a+0]=0xCD; ram[a+1]=0x20;
    st16u(&ram[a+2], memtop);
    ram[a+5]=0xCD; ram[a+6]=0x21; ram[a+7]=0xCB;
    st32u(&ram[a+0x0A], mem_r32(0x22*4));
    st32u(&ram[a+0x0E], mem_r32(0x23*4));
    st32u(&ram[a+0x12], mem_r32(0x24*4));
    st16u(&ram[a+0x16], parent);
    for(i=0;i<20;i++) ram[a+0x18+i] = (i<5)?(uint8_t)i:0xFF;
    st16u(&ram[a+0x2C], env);
    st16u(&ram[a+0x32], 20);
    st32u(&ram[a+0x34], ((uint32_t)seg<<16) | 0x18);
    ram[a+0x50]=0xCD; ram[a+0x51]=0x21; ram[a+0x52]=0xCB;
    {
        int n = tail ? (int)strlen(tail) : 0;
        if(n>126) n=126;
        ram[a+0x80] = (uint8_t)n;
        if(n) memcpy(&ram[a+0x81], tail, (size_t)n);
        ram[a+0x81+n] = 0x0D;
    }
}

static uint16_t make_env(const char *gpath){
    static const char *vars[] = { "PATH=C:\\", "COMSPEC=C:\\COMMAND.COM", "PROMPT=$p$g", NULL };
    char buf[512]; int i, n = 0;
    uint16_t paras, seg, largest;
    for(i=0;vars[i];i++){ int l=(int)strlen(vars[i]); memcpy(buf+n, vars[i], (size_t)l+1); n += l+1; }
    buf[n++] = 0;
    st16u(&buf[n], 1); n += 2;
    n += snprintf(buf+n, sizeof(buf)-(size_t)n, "C:%s", gpath) + 1;
    paras = (uint16_t)((n + 15)/16);
    seg = mcb_alloc(paras, 8, &largest);
    if(!seg) return 0;
    memcpy(&ram[(uint32_t)seg*16], buf, (size_t)n);
    return seg;
}

/* Read an MZ header and image; relocate by `reloc`.  Loads at `load`
 * (a segment); returns the header fields the caller needs. */
typedef struct { uint16_t cs, ip, ss, sp, minalloc, maxalloc; uint32_t imglen; } MzInfo;
static int mz_read(FILE *f, MzInfo *m){
    uint8_t hdr[32];
    long fsize;
    uint32_t imgend;
    fseek(f,0,SEEK_END); fsize = ftell(f); fseek(f,0,SEEK_SET);
    if(fread(hdr,1,32,f)!=32 || hdr[0]!='M' || hdr[1]!='Z') return 0;
    {
        uint16_t cblp = ld16u(&hdr[2]), cp = ld16u(&hdr[4]), cparhdr = ld16u(&hdr[8]);
        imgend = cblp ? ((uint32_t)(cp-1)*512 + cblp) : ((uint32_t)cp*512);
        if(imgend > (uint32_t)fsize) imgend = (uint32_t)fsize;
        m->imglen = imgend - (uint32_t)cparhdr*16;
    }
    m->minalloc = ld16u(&hdr[10]); m->maxalloc = ld16u(&hdr[12]);
    m->ss = ld16u(&hdr[14]); m->sp = ld16u(&hdr[16]);
    m->ip = ld16u(&hdr[20]); m->cs = ld16u(&hdr[22]);
    return 1;
}
static void mz_load(FILE *f, uint16_t load, uint16_t reloc){
    uint8_t hdr[32];
    uint32_t imgend, hdrsize, imglen;
    uint16_t crlc, lfarlc;
    int i;
    fseek(f,0,SEEK_SET);
    if(fread(hdr,1,32,f)!=32) return;
    {
        uint16_t cblp = ld16u(&hdr[2]), cp = ld16u(&hdr[4]);
        long fsize;
        fseek(f,0,SEEK_END); fsize = ftell(f);
        hdrsize = (uint32_t)ld16u(&hdr[8])*16;
        imgend = cblp ? ((uint32_t)(cp-1)*512 + cblp) : ((uint32_t)cp*512);
        if(imgend > (uint32_t)fsize) imgend = (uint32_t)fsize;
        imglen = imgend - hdrsize;
    }
    crlc = ld16u(&hdr[6]); lfarlc = ld16u(&hdr[24]);
    fseek(f, (long)hdrsize, SEEK_SET);
    if(fread(&ram[(uint32_t)load*16], 1, imglen, f) != imglen){ }
    fseek(f, lfarlc, SEEK_SET);
    for(i=0;i<crlc;i++){
        uint8_t rb[4]; uint32_t a;
        if(fread(rb,1,4,f)!=4) break;
        a = (uint32_t)(load + ld16u(&rb[2]))*16 + ld16u(&rb[0]);
        st16u(&ram[a], (uint16_t)(ld16u(&ram[a]) + reloc));
    }
}

/* Load a program for EXEC AL=00.  Returns 0 or a DOS error code. */
static int load_program(const char *host, const char *gpath, uint16_t env, const char *tail,
                        uint16_t *out_cs, uint16_t *out_ip, uint16_t *out_ss,
                        uint16_t *out_sp, uint16_t *out_psp){
    FILE *f = fopen(host, "rb");
    MzInfo m;
    uint16_t paras, seg, psp, load, largest;
    if(!f) return 2;
    if(!mz_read(f, &m)){
        /* a .COM: all free memory, the file at PSP:0100, all four segments
         * the PSP */
        long fsize;
        fseek(f,0,SEEK_END); fsize = ftell(f); fseek(f,0,SEEK_SET);
        seg = mcb_alloc(0xFFFF, 0, &largest);
        if(!seg) seg = mcb_alloc(largest, 0, &largest);
        if(!seg){ fclose(f); return 8; }
        paras = ld16u(&ram[(uint32_t)(seg-1)*16 + 3]);
        psp = seg;
        st16u(&ram[(uint32_t)(seg-1)*16 + 1], psp);
        make_psp(psp, (uint16_t)(psp + paras), cur_psp ? cur_psp : psp, env, tail);
        if(fsize > 0xFF00) fsize = 0xFF00;
        if(fread(&ram[(uint32_t)psp*16 + 0x100], 1, (size_t)fsize, f) != (size_t)fsize){ }
        fclose(f);
        *out_cs = psp; *out_ip = 0x0100;
        *out_ss = psp; *out_sp = 0xFFFE;
        mem_w16((uint32_t)psp*16 + 0xFFFE, 0x0000);   /* the INT 20h return word */
        *out_psp = psp;
        if(dos_on_load) dos_on_load(gpath, psp);
        return 0;
    }
    {
        uint32_t need = (m.imglen + 15)/16 + 16 + m.minalloc;
        if(m.maxalloc > m.minalloc){
            uint32_t want = (m.imglen + 15)/16 + 16 + m.maxalloc;
            need = want > 0xFFFF ? 0xFFFF : want;
        }
        paras = (uint16_t)(need > 0xFFFF ? 0xFFFF : need);
        seg = mcb_alloc(paras, 0, &largest);
        if(!seg){
            if(largest < (m.imglen+15)/16 + 16 + m.minalloc){ fclose(f); return 8; }
            paras = largest;
            seg = mcb_alloc(paras, 0, &largest);
            if(!seg){ fclose(f); return 8; }
        }
    }
    psp = seg;
    load = (uint16_t)(seg + 16);
    st16u(&ram[(uint32_t)(seg-1)*16 + 1], psp);
    make_psp(psp, (uint16_t)(psp + paras), cur_psp ? cur_psp : psp, env, tail);
    mz_load(f, load, load);
    fclose(f);
    *out_cs = (uint16_t)(load + m.cs);
    *out_ip = m.ip;
    *out_ss = (uint16_t)(load + m.ss);
    *out_sp = m.sp;
    *out_psp = psp;
    if(dos_on_load) dos_on_load(gpath, load);
    return 0;
}

static int do_exec(const char *dospath, const char *tail, uint16_t env){
    char host[700], g[260];
    uint16_t cs,ip,ss,sp,psp;
    int r;
    if(!dos_normalise(dospath, g, sizeof(g)) || !dos_host_path(dospath, host, sizeof(host), 0))
        return 2;
    if(!env) env = make_env(g);
    r = load_program(host, g, env, tail, &cs,&ip,&ss,&sp,&psp);
    if(r) return r;
    if(nproc < 8){
        procs[nproc].psp = psp;
        procs[nproc].parent_psp = cur_psp;
        procs[nproc].parent_ss = cpu.sreg[S_SS];
        procs[nproc].parent_sp = REG16(R_ESP);
        procs[nproc].env = env;
        procs[nproc].r_ax = AX; procs[nproc].r_bx = BX;
        procs[nproc].r_cx = CX; procs[nproc].r_dx = DX;
        procs[nproc].r_si = SI; procs[nproc].r_di = DI;
        procs[nproc].r_bp = REG16(R_EBP);
        procs[nproc].r_ds = cpu.sreg[S_DS]; procs[nproc].r_es = cpu.sreg[S_ES];
        nproc++;
    }
    cur_psp = psp;
    oa_active = 0;
    dos_snapshot_ivt();
    /* DS/ES point at the PSP, SS:SP and CS:IP from the header */
    set_sreg(S_DS, psp); set_sreg(S_ES, psp);
    set_sreg(S_SS, ss);  REG16(R_ESP) = sp;
    set_sreg(S_CS, cs);  cpu.eip = ip;
    AX = 0; BX = 0; CX = 0; DX = 0; SI = ip; DI = sp;
    REG16(R_EBP) = 0x091C;
    trc("[dos] exec %s -> cs=%04X ip=%04X ss=%04X sp=%04X psp=%04X tail=\"%s\"\n",
        g, cs, ip, ss, sp, psp, tail);
    return 0;
}

/* The first program: the current directory is the program's own, as if it
 * had been started from there. */
int dos_start(const char *dospath, const char *tail, uint16_t *load_seg){
    char g[260], *slash;
    int r;
    if(!dos_normalise(dospath, g, sizeof(g))) return 2;
    {
        char *cwd = cwds[cur_drive];
        snprintf(cwd, sizeof(cwds[0]), "%s", g);
        slash = strrchr(cwd, '\\');
        if(slash == cwd) cwd[1] = 0; else if(slash) *slash = 0;
    }
    r = do_exec(g, tail, 0);
    if(!r && load_seg) *load_seg = (uint16_t)(cur_psp + 16);
    return r;
}

/* keep_paras < 0 => normal exit (free everything); >= 0 => TSR */
static void dos_terminate2(uint16_t code, int keep_paras){
    last_retcode = code;
    if(nproc == 0){ dos_done = 1; dos_exit_code = code; cpu.shutdown = 1; cpu_no_iret(); return; }
    nproc--;
    {
        Proc *p = &procs[nproc];
        uint32_t a = (uint32_t)p->psp*16;
        if(keep_paras < 0){
            /* restore the vectors the PSP saved */
            st32u(&ram[0x22*4], ld32u(&ram[a+0x0A]));
            st32u(&ram[0x23*4], ld32u(&ram[a+0x0E]));
            st32u(&ram[0x24*4], ld32u(&ram[a+0x12]));
            if(p->env) mcb_free(p->env);
            mcb_free(p->psp);
            mcb_free_children(p->psp);
        } else {
            uint16_t avail;
            if(keep_paras < 17) keep_paras = 17;
            mcb_resize(p->psp, (uint16_t)keep_paras, &avail);
            if(p->env) mcb_free(p->env);      /* DOS releases the environment */
            trc("[dos] TSR: psp %04X stays resident, %d paragraphs\n", p->psp, keep_paras);
        }
        cur_psp = p->parent_psp;
        if(nproc == 0 && keep_paras < 0){
            /* the first program has ended: nothing to return to */
            dos_done = 1; dos_exit_code = code; cpu.shutdown = 1; cpu_no_iret();
            return;
        }
        set_sreg(S_SS, p->parent_ss);
        REG16(R_ESP) = p->parent_sp;
        /* hand the parent its registers back (see Proc) */
        BX = p->r_bx; CX = p->r_cx; DX = p->r_dx;
        SI = p->r_si; DI = p->r_di; REG16(R_EBP) = p->r_bp;
        set_sreg(S_DS, p->r_ds); set_sreg(S_ES, p->r_es);
        /* the parent's pending IRET frame is now on top of its stack */
        {   uint32_t sp32 = cpu.sbase[S_SS] + REG16(R_ESP);
            uint16_t fl = mem_r16(sp32+4);
            mem_w16(sp32+4, (uint16_t)(fl & ~1u));       /* CF = 0: EXEC succeeded */
        }
        AX = 0;
    }
}

/* ------------------------------------------------------- FindFirst/Next */
/* The union of both layers' entries of one directory, sorted by name so the
 * order does not depend on the host, matched against the pattern. */
static Entry find_ents[512];
static int find_n = 0, find_pos = 0;
static char find_pat[16];
static char find_dir[260];            /* the searched directory, "" = root */
static uint8_t find_attr;

static int wild_match(const char *pat, const char *name){
    /* DOS 8.3 matching: name and extension separately; '*' fills the rest
     * of its part, '?' matches one character or none at the end */
    char pn[9], pe[4], nn[9], ne[4];
    const char *d;
    int i;
    #define SPLIT(s, b, e) do { size_t l_; d = strchr(s, '.'); \
        l_ = d ? (size_t)(d - s) : strlen(s); if(l_ > 8) l_ = 8; \
        memcpy(b, s, l_); b[l_] = 0; \
        snprintf(e, 4, "%s", d ? d+1 : ""); } while(0)
    SPLIT(pat, pn, pe); SPLIT(name, nn, ne);
    #undef SPLIT
    for(i=0;i<8;i++){
        char p = pn[i], c = nn[i];
        if(p=='*') break;
        if(!p){ if(c) return 0; break; }
        if(p=='?') { if(!c) break; continue; }
        if(p != c) return 0;
    }
    for(i=0;i<3;i++){
        char p = pe[i], c = ne[i];
        if(p=='*') break;
        if(!p){ if(c) return 0; break; }
        if(p=='?') { if(!c) break; continue; }
        if(p != c) return 0;
    }
    return 1;
}
static int ent_cmp(const void *a, const void *b){
    return strcmp(((const Entry*)a)->name, ((const Entry*)b)->name);
}
static int find_fill(void){
    uint32_t d = (uint32_t)dta_seg*16 + dta_off;
    while(find_pos < find_n){
        Entry *e = &find_ents[find_pos++];
        char host[700];
        long size = 0;
        int i;
        if(e->dir && !(find_attr & 0x10)) continue;
        if(!wild_match(find_pat, e->name)) continue;
        if(!e->dir){
            FILE *f;
            char gp[340];
            snprintf(gp, sizeof(gp), "%s\\%s", find_dir, e->name);
            if(dos_host_path(gp, host, sizeof(host), 0) && (f = fopen(host, "rb"))){
                fseek(f, 0, SEEK_END); size = ftell(f); fclose(f);
            }
        }
        mem_w8(d+0x15, (uint8_t)(e->dir ? 0x10 : 0x20));
        mem_w16(d+0x16, 0);
        mem_w16(d+0x18, FIX_DOSDATE);
        mem_w16(d+0x1A, (uint16_t)size);
        mem_w16(d+0x1C, (uint16_t)(size>>16));
        for(i=0;i<13;i++) mem_w8(d + 0x1E + i, (uint8_t)(i<(int)strlen(e->name) ? e->name[i] : 0));
        return 1;
    }
    return 0;
}
static void find_first(const char *spec, uint8_t attr){
    char g[260], dir[260], h[700], *slash;
    static Entry tmp[512];
    int i, j, k;
    find_n = find_pos = 0;
    find_attr = attr;
    if(!dos_normalise(spec, g, sizeof(g))) return;
    slash = strrchr(g, '\\');
    snprintf(find_pat, sizeof(find_pat), "%s", slash + 1);
    snprintf(dir, sizeof(dir), "%.*s", (int)(slash - g), g);
    if(!dir[0]) snprintf(dir, sizeof(dir), "\\");
    for(k=0;k<2;k++){
        layer_path(k ? game_root : state_root, strcmp(dir,"\\") ? dir : "", h, sizeof(h));
        {
            int n = host_list(h, tmp, 512);
            for(i=0;i<n;i++){
                char up[64];
                for(j=0;tmp[i].name[j] && j<63;j++){ char c = tmp[i].name[j]; up[j] = (char)((c>='a'&&c<='z')?c-32:c); }
                up[j] = 0;
                for(j=0;j<find_n;j++) if(!strcmp(find_ents[j].name, up)) break;
                if(j==find_n && find_n < 512){
                    snprintf(find_ents[find_n].name, sizeof(find_ents[find_n].name), "%s", up);
                    find_ents[find_n].dir = tmp[i].dir;
                    find_n++;
                }
            }
        }
    }
    qsort(find_ents, (size_t)find_n, sizeof(Entry), ent_cmp);
    snprintf(find_dir, sizeof(find_dir), "%s", strcmp(dir, "\\") ? dir : "");
}

/* --------------------------------------------------------- console I/O */
/* What the programs print through DOS goes to the screen (BIOS TTY) and,
 * line by line, to the run's output as "con: ...". */
static char con_line[256];
static int con_n = 0;
static void con_out(uint8_t c){
    bios_tty(c);
    if(c=='\n' || c=='\r'){
        if(con_n){ con_line[con_n] = 0; printf("con: %s\n", con_line); con_n = 0; }
        return;
    }
    if(c >= 0x20 && con_n < (int)sizeof(con_line)-1) con_line[con_n++] = (char)c;
}
void dos_flush_con(void){
    if(con_n){ con_line[con_n] = 0; printf("con: %s\n", con_line); con_n = 0; }
}

/* Wait for a key: rewind onto the INT stub and idle (the loop in main.c
 * advances the clock while halted; the next keyboard IRQ wakes the CPU and
 * the call runs again). */
static void block_for_key(void){
    uint32_t sp = cpu.sbase[S_SS] + REG16(R_ESP);
    cpu.iflag = (mem_r16(sp+4) >> 9) & 1;
    cpu.eip -= 3; cpu_no_iret(); cpu.halted = 1;
}

/* ------------------------------------------------------------- INT 21h  */
static void dos_int21(void){
    if(dos_log){
        uint32_t sp = cpu.sbase[S_SS] + REG16(R_ESP);
        char s[80] = "";
        if(AH==0x3C || AH==0x3D || AH==0x41 || AH==0x43 || AH==0x4B || AH==0x4E || AH==0x3B)
            read_dosstr(cpu.sbase[S_DS] + DX, s, sizeof(s));
        printf("int21 AX=%04X BX=%04X CX=%04X DX=%04X DS=%04X ES=%04X from %04X:%04X t=%.6f %s\n",
               AX,BX,CX,DX,cpu.sreg[S_DS],cpu.sreg[S_ES],
               mem_r16(sp+2), mem_r16(sp), emu_now(), s);
    }
    switch(AH){
    case 0x00: dos_terminate2(0, -1); return;
    case 0x01: case 0x07: case 0x08: {
        uint16_t k;
        if(!bios_kbuf_get(&k)){ block_for_key(); return; }
        AL = (uint8_t)k;
        if(AH==0x01) con_out(AL);
        break; }
    case 0x02: con_out(DL); break;
    case 0x06:
        if(DL != 0xFF) con_out(DL);
        else {
            uint16_t k;
            if(bios_kbuf_get(&k)){ AL = (uint8_t)k; cpu.zf = 0; }
            else { AL = 0; cpu.zf = 1; }
            { uint32_t sp = cpu.sbase[S_SS] + REG16(R_ESP);
              uint16_t f = mem_r16(sp+4);
              mem_w16(sp+4, (uint16_t)(cpu.zf ? (f|0x40) : (f&~0x40))); }
        }
        break;
    case 0x09: {
        uint32_t a = cpu.sbase[S_DS] + DX;
        int i;
        for(i=0;i<2000;i++){ uint8_t c = mem_r8(a+i); if(c=='$') break; con_out(c); }
        AL = '$';
        break; }
    case 0x0A: {
        /* Buffered line input, DS:DX: [0]=max chars, [1]=count (out), chars
         * from [2], CR-terminated.  An empty queue blocks like AH=01h; the
         * part already typed sits in the guest buffer and oa_active says to
         * go on with it. */
        uint32_t a = cpu.sbase[S_DS] + DX;
        uint8_t maxlen = mem_r8(a);
        uint8_t n;
        if(!oa_active) mem_w8(a+1, 0);
        n = mem_r8(a+1);
        if(n > maxlen) n = maxlen;
        for(;;){
            uint16_t k;
            if(!bios_kbuf_get(&k)){ oa_active = 1; mem_w8(a+1, n); block_for_key(); return; }
            { uint8_t asc = (uint8_t)(k & 0xFF);
              uint8_t sc = (uint8_t)(k >> 8);
              if(asc == 0x0D){ mem_w8(a+2+n, 0x0D); con_out(0x0D); con_out(0x0A); break; }
              else if(asc == 0x08){ if(n){ n--; bios_tty(0x08); bios_tty(' '); bios_tty(0x08); } }
              else if(asc == 0){ if(n+2 <= maxlen){ mem_w8(a+2+n,0); mem_w8(a+2+n+1,sc); n+=2; } }
              else if(asc >= 0x20){ if(n < maxlen){ mem_w8(a+2+n,asc); n++; con_out(asc); } } }
            mem_w8(a+1, n);
        }
        mem_w8(a+1, n);
        oa_active = 0;
        break; }
    case 0x0B: { uint16_t k; AL = bios_kbuf_peek(&k) ? 0xFF : 0x00; break; }
    case 0x0C: {
        uint16_t k;
        while(bios_kbuf_get(&k)) ;
        if(AL==0x01 || AL==0x06 || AL==0x07 || AL==0x08 || AL==0x0A){
            AH = AL; dos_int21(); return;
        }
        break; }
    case 0x0D: break;
    case 0x0E: if(DL < 26) cur_drive = DL; AL = 26; break;
    case 0x19: AL = cur_drive; break;
    case 0x1A: dta_seg = cpu.sreg[S_DS]; dta_off = DX; break;
    case 0x25: st32u(&ram[(uint32_t)AL*4], ((uint32_t)cpu.sreg[S_DS]<<16) | DX); break;
    case 0x2A: CX = FIX_YEAR; DH = FIX_MON; DL = FIX_DAY; AL = FIX_WDAY; break;
    case 0x2C: {
        /* noon plus the emulated time */
        uint32_t cs = (uint32_t)(emu_now() * 100.0) + 12u*360000u;
        CH = (uint8_t)((cs / 360000u) % 24); CL = (uint8_t)((cs / 6000u) % 60);
        DH = (uint8_t)((cs / 100u) % 60);    DL = (uint8_t)(cs % 100u);
        break; }
    case 0x2F: set_sreg(S_ES, dta_seg); BX = dta_off; break;
    case 0x30: AL = 5; AH = 0; BH = 0xFF; BL = 0; CX = 0; break;
    case 0x33: if(AL==0) DL=0; break;
    case 0x35: { uint32_t v = ld32u(&ram[(uint32_t)AL*4]);
        set_sreg(S_ES, (uint16_t)(v>>16)); BX = (uint16_t)v; break; }
    case 0x36: AX = 4; BX = 20000; CX = 512; DX = 30000; break;
    case 0x3B: {
        char name[260], g[260];
        read_dosstr(cpu.sbase[S_DS] + DX, name, sizeof(name));
        if(dos_normalise(name, g, sizeof(g)) && guest_isdir(g)){
            snprintf(cwds[drive_of(name)], sizeof(cwds[0]), "%s", g); bios_set_cf(0);
        } else { AX = 3; bios_set_cf(1); }
        trc("[dos] chdir '%s' -> %s\n", name, g);
        break; }
    case 0x3C: case 0x3D: {
        char name[260], host[700];
        int h = alloc_handle();
        int writing = (AH==0x3C) || ((AL & 3) != 0);
        read_dosstr(cpu.sbase[S_DS] + DX, name, sizeof(name));
        if(h<0){ AX = 4; bios_set_cf(1); break; }
        if(!dos_host_path(name, host, sizeof(host), writing)){
            trc("[dos] open '%s': not found\n", name);
            AX = 2; bios_set_cf(1); break;
        }
        if(AH==0x3C) fh[h].f = fopen(host, "w+b");
        else fh[h].f = fopen(host, writing ? "r+b" : "rb");
        if(!fh[h].f){ trc("[dos] open '%s' (%s) failed\n", name, host); AX = 2; bios_set_cf(1); break; }
        fh[h].used = 1;
        snprintf(fh[h].name, sizeof(fh[h].name), "%s", name);
        AX = (uint16_t)h; bios_set_cf(0);
        trc("[dos] open '%s' -> handle %d (%s)\n", name, h, host);
        break; }
    case 0x3E: {
        int h = BX;
        if(h>=5 && h<64 && fh[h].used){ fclose(fh[h].f); fh[h].used=0; }
        bios_set_cf(0); break; }
    case 0x3F: {
        int h = BX; uint32_t a = cpu.sbase[S_DS] + DX; uint16_t n = CX;
        if(h<5){ AX = 0; bios_set_cf(0); break; }
        if(h>=64 || !fh[h].used){ AX=6; bios_set_cf(1); break; }
        {
            static uint8_t buf[65536];
            size_t got = fread(buf, 1, n, fh[h].f);
            uint32_t i;
            for(i=0;i<got;i++) mem_w8(a+i, buf[i]);
            AX = (uint16_t)got; bios_set_cf(0);
        }
        break; }
    case 0x40: {
        int h = BX; uint32_t a = cpu.sbase[S_DS] + DX; uint16_t n = CX;
        if(h<5){ uint16_t i; for(i=0;i<n;i++) con_out(mem_r8(a+i)); AX = n; bios_set_cf(0); break; }
        if(h>=64 || !fh[h].used){ AX=6; bios_set_cf(1); break; }
        {
            static uint8_t buf[65536];
            uint16_t i;
            for(i=0;i<n;i++) buf[i] = mem_r8(a+i);
            AX = (uint16_t)fwrite(buf,1,n,fh[h].f); bios_set_cf(0);
        }
        break; }
    case 0x41: {
        /* only a file in the layer can go; one on the CD is read-only */
        char name[260], g[260], lay[700];
        read_dosstr(cpu.sbase[S_DS] + DX, name, sizeof(name));
        dos_normalise(name, g, sizeof(g));
        layer_path(state_root, g, lay, sizeof(lay));
        if(host_isfile(lay)){ bios_set_cf(remove(lay) ? 1 : 0); if(cpu.cf) AX = 5; }
        else if(dos_host_path(name, lay, sizeof(lay), 0)){ AX = 5; bios_set_cf(1); }
        else { AX = 2; bios_set_cf(1); }
        break; }
    case 0x42: {
        int h = BX; long off = (long)(int32_t)(((uint32_t)CX<<16) | DX);
        if(h<5 || h>=64 || !fh[h].used){ AX=6; bios_set_cf(1); break; }
        fseek(fh[h].f, off, AL==0?SEEK_SET:(AL==1?SEEK_CUR:SEEK_END));
        { long p = ftell(fh[h].f); AX=(uint16_t)p; DX=(uint16_t)(p>>16); }
        bios_set_cf(0); break; }
    case 0x43: {
        char name[260], host[700], g[260];
        read_dosstr(cpu.sbase[S_DS] + DX, name, sizeof(name));
        if(dos_host_path(name, host, sizeof(host), 0)){ CX = 0x20; bios_set_cf(0); }
        else if(dos_normalise(name, g, sizeof(g)) && guest_isdir(g)){ CX = 0x10; bios_set_cf(0); }
        else { AX = 2; bios_set_cf(1); }
        break; }
    case 0x44:
        if(AL==0){ DX = (BX<5) ? 0x80D3 : 0x0000; bios_set_cf(0); }
        else bios_set_cf(0);
        break;
    case 0x45: { int h = alloc_handle(); AX = (uint16_t)(h<0?4:h); bios_set_cf(h<0); break; }
    case 0x47: {
        /* current directory without drive and leading backslash */
        uint32_t a = cpu.sbase[S_DS] + SI;
        const char *p = cwds[DL ? (DL - 1) % 26 : cur_drive] + 1;
        int i;
        for(i=0;p[i] && i<63;i++) mem_w8(a+i, (uint8_t)p[i]);
        mem_w8(a+i, 0);
        bios_set_cf(0); break; }
    case 0x48: { uint16_t largest, s = mcb_alloc(BX, cur_psp, &largest);
        trc("[dos] alloc %u paras -> %04X\n", (unsigned)BX, s);
        if(s){ AX = s; bios_set_cf(0); } else { AX = 8; BX = largest; bios_set_cf(1); }
        break; }
    case 0x49: trc("[dos] free block %04X\n", cpu.sreg[S_ES]);
        bios_set_cf(mcb_free(cpu.sreg[S_ES]) ? 0 : 1); if(cpu.cf) AX = 9; break;
    case 0x4A: { uint16_t avail;
        trc("[dos] resize %04X to %u paras\n", cpu.sreg[S_ES], (unsigned)BX);
        if(mcb_resize(cpu.sreg[S_ES], BX, &avail)) bios_set_cf(0);
        else { AX = 8; BX = avail; bios_set_cf(1); }
        break; }
    case 0x4B: {
        char name[260], host[700], tail[130];
        uint32_t pb = cpu.sbase[S_ES] + BX;
        int r, n, i;
        read_dosstr(cpu.sbase[S_DS] + DX, name, sizeof(name));
        if(AL == 3){
            /* load overlay: image at [pb], relocated by [pb+2]; no PSP */
            FILE *f;
            if(!dos_host_path(name, host, sizeof(host), 0) || !(f = fopen(host, "rb"))){ AX = 2; bios_set_cf(1); break; }
            { MzInfo m;
              if(mz_read(f, &m)) mz_load(f, mem_r16(pb), mem_r16(pb+2));
              else { long sz; fseek(f,0,SEEK_END); sz = ftell(f); fseek(f,0,SEEK_SET);
                     if(fread(&ram[(uint32_t)mem_r16(pb)*16], 1, (size_t)sz, f) != (size_t)sz){ } } }
            fclose(f);
            trc("[dos] overlay '%s' at %04X\n", name, mem_r16(pb));
            bios_set_cf(0); break;
        }
        if(AL != 0){ AX = 1; bios_set_cf(1); break; }
        {   uint16_t toff = mem_r16(pb+2), tseg = mem_r16(pb+4);
            n = mem_r8((uint32_t)tseg*16 + toff);
            if(n>126) n=126;
            for(i=0;i<n;i++) tail[i] = (char)mem_r8((uint32_t)tseg*16 + toff + 1 + i);
            tail[n] = 0; }
        r = do_exec(name, tail, mem_r16(pb));
        if(r){ AX = (uint16_t)r; bios_set_cf(1); trc("[dos] exec '%s' failed (%d)\n", name, r); break; }
        cpu_no_iret();
        return; }
    case 0x31: dos_terminate2(AL, (int)DX); return;
    case 0x4C: dos_terminate2(AL, -1); return;
    case 0x4D: AX = last_retcode; break;
    case 0x4E: {
        char name[260];
        read_dosstr(cpu.sbase[S_DS] + DX, name, sizeof(name));
        find_first(name, (uint8_t)CX);
        if(find_fill()){ AX = 0; bios_set_cf(0); } else { AX = 18; bios_set_cf(1); }
        break; }
    case 0x4F:
        if(find_fill()){ AX = 0; bios_set_cf(0); } else { AX = 18; bios_set_cf(1); }
        break;
    case 0x50: cur_psp = BX; break;
    case 0x51: case 0x62: BX = cur_psp; break;
    case 0x54: AL = 0; break;
    case 0x58:
        switch(AL){
        case 0x00: AX = dos_alloc_strategy; bios_set_cf(0); break;
        case 0x01: dos_alloc_strategy = BX; AX = 0; bios_set_cf(0); break;
        case 0x02: AX = 0; bios_set_cf(0); break;      /* UMB link state: off */
        case 0x03: AX = 0; bios_set_cf(0); break;
        default:   AX = 1; bios_set_cf(1); break;
        }
        break;
    case 0x59: AX = 0; break;
    default:
        printf("dos: unimplemented INT 21h AH=%02X AL=%02X at t=%.6f\n", AH, AL, emu_now());
        bios_set_cf(1); AX = 1;
        break;
    }
}

static void dos_int20(void){ dos_terminate2(0, -1); }
static void dos_int27(void){ dos_terminate2(0, -1); }
static void dos_nop(void){ }

void dos_init(const char *game_dir, const char *state_dir){
    size_t l;
    snprintf(game_root, sizeof(game_root), "%s", game_dir);
    snprintf(state_root, sizeof(state_root), "%s", state_dir);
    l = strlen(game_root); if(l && (game_root[l-1]=='/' || game_root[l-1]=='\\')) game_root[l-1]=0;
    l = strlen(state_root); if(l && (state_root[l-1]=='/' || state_root[l-1]=='\\')) state_root[l-1]=0;
    mkdir_host(state_root);
    memset(fh,0,sizeof(fh));
    mcb_init();
    cur_psp = 0; nproc = 0;
    dta_seg = 0; dta_off = 0x80;
    for(l=0;l<26;l++) snprintf(cwds[l], sizeof(cwds[0]), "\\");
    cur_drive = START_DRIVE;
    cb_table[0x20] = dos_int20;
    cb_table[0x21] = dos_int21;
    cb_table[0x27] = dos_int27;
    cb_table[0x28] = dos_nop;
    cb_table[0x2F] = dos_nop;
}
