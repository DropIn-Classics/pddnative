/* mem.c - see mem.h */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mem.h"
#include "sha256.h"
#include "sys.h"

uint8_t mem[MEM_SIZE];
int prog_id;
uint16_t seg_psp, seg_code, seg_tdata, seg_data, seg_xdata, seg_bss;
PdNames nm;

#define PDN_PD1(seg, name, a1, a2) a1,
#define PDN_PD2(seg, name, a1, a2) a2,
static const PdNames names_pd1 = {PDN_NAMES(PDN_PD1)};
static const PdNames names_pd2 = {PDN_NAMES(PDN_PD2)};

/* Where pddrun (tools/run) puts the program: its PSP at 0067h, the
 * image 10h paragraphs above.  The program keeps 1A40h paragraphs from
 * its PSP on (PD2.EXE 1A4Ah; INT 21h 4Ah at `start`); what it allocates
 * comes after, each block behind a paragraph of MCB, as DOS does it. */
#define LOAD_PSP 0x0067u
#define PROG_PARAS (prog_id == 1 ? 0x1A40u : 0x1A4Au)
#define MEM_TOP 0xA000u

static uint16_t sw(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

int mem_load(int id, const char *game_dir, char *err, size_t n)
{
    const char *exe = id == 1 ? PD1_EXE : PD2_EXE;
    const char *want = id == 1 ? PD1_SHA256 : PD2_SHA256;
    char path[SYS_PATH], found[SYS_PATH], hex[65];
    uint8_t digest[32];
    size_t size, hdr, image, i;
    uint8_t *f;
    uint16_t nrel, relofs;

    /* the name on the CD, DREAMS1/PD.EXE: the folder, then the file */
    snprintf(path, sizeof path, "%s", exe);
    path[strcspn(path, "/")] = 0;
    if (!sys_find(game_dir, path, found, sizeof found) ||
        !sys_find(found, strchr(exe, '/') + 1, path, sizeof path)) {
        snprintf(err, n, "%s is not in %s.", exe, game_dir);
        return -1;
    }
    f = sys_load(path, &size);
    if (!f) {
        snprintf(err, n, "%s cannot be read.", path);
        return -1;
    }
    sha256(f, size, digest);
    for (i = 0; i < 32; i++)
        snprintf(hex + 2 * i, 3, "%02x", digest[i]);
    if (size != (id == 1 ? PD1_SIZE : PD2_SIZE) || strcmp(hex, want) != 0) {
        snprintf(err, n, "%s is not the file of the GOG release (SHA-256 %s).", path, hex);
        free(f);
        return -1;
    }

    memset(mem, 0, sizeof mem);
    prog_id = id;
    nm = id == 1 ? names_pd1 : names_pd2;
    seg_psp = LOAD_PSP;
    seg_code = (uint16_t)(LOAD_PSP + 0x10);
    seg_tdata = (uint16_t)(seg_code + (id == 1 ? PD1_TDATA : PD2_TDATA));
    seg_data = (uint16_t)(seg_code + (id == 1 ? PD1_DATA : PD2_DATA));
    seg_xdata = (uint16_t)(seg_code + (id == 1 ? PD1_XDATA : PD2_XDATA));
    seg_bss = (uint16_t)(seg_code + (id == 1 ? PD1_BSS : PD2_BSS));

    /* the image: from the header's end to the size in the header */
    hdr = (size_t)sw(f + 8) * 16;
    image = (size_t)(sw(f + 4) - 1) * 512 + (sw(f + 2) ? sw(f + 2) : 512) - hdr;
    memcpy(mem + (size_t)seg_code * 16, f + hdr, image);
    nrel = sw(f + 6);
    relofs = sw(f + 0x18);
    for (i = 0; i < nrel; i++) {
        const uint8_t *r = f + relofs + 4 * i;
        uint16_t off = sw(r), seg = (uint16_t)(sw(r + 2) + seg_code);
        fww(seg, off, (uint16_t)(frw(seg, off) + seg_code));
    }
    free(f);

    /* the arena: one free block from the program's end to video memory */
    {
        uint16_t mcb = (uint16_t)(LOAD_PSP + PROG_PARAS);
        fwb(mcb, 0, 'Z');
        fww(mcb, 1, 0);
        fww(mcb, 3, (uint16_t)(MEM_TOP - mcb - 1));
    }
    return 0;
}

/* The MCB chain as DOS keeps it: 'M' or 'Z' (the last), the owner (0 =
 * free), the size in paragraphs.  First fit, as DOS's default strategy. */
static uint16_t first_mcb(void) { return (uint16_t)(LOAD_PSP + PROG_PARAS); }

uint16_t dos_alloc(uint16_t paras)
{
    uint16_t mcb = first_mcb();
    for (;;) {
        uint8_t kind = frb(mcb, 0);
        uint16_t owner = frw(mcb, 1), size = frw(mcb, 3);
        if (owner == 0 && size >= paras) {
            if (size > paras) {                 /* the rest stays free */
                uint16_t rest = (uint16_t)(mcb + 1 + paras);
                fwb(rest, 0, kind);
                fww(rest, 1, 0);
                fww(rest, 3, (uint16_t)(size - paras - 1));
                fwb(mcb, 0, 'M');
                fww(mcb, 3, paras);
            }
            fww(mcb, 1, seg_psp);
            return (uint16_t)(mcb + 1);
        }
        if (kind == 'Z')
            return 0;
        mcb = (uint16_t)(mcb + 1 + size);
    }
}

int dos_free(uint16_t seg)
{
    uint16_t mcb = first_mcb();
    for (;;) {
        uint8_t kind = frb(mcb, 0);
        uint16_t size = frw(mcb, 3);
        if ((uint16_t)(mcb + 1) == seg) {
            fww(mcb, 1, 0);
            break;
        }
        if (kind == 'Z')
            return -1;
        mcb = (uint16_t)(mcb + 1 + size);
    }
    /* join free neighbours, as DOS does at the next allocation */
    for (mcb = first_mcb(); frb(mcb, 0) == 'M';) {
        uint16_t next = (uint16_t)(mcb + 1 + frw(mcb, 3));
        if (frw(mcb, 1) == 0 && frw(next, 1) == 0) {
            fwb(mcb, 0, frb(next, 0));
            fww(mcb, 3, (uint16_t)(frw(mcb, 3) + 1 + frw(next, 3)));
        } else {
            mcb = next;
        }
    }
    return 0;
}
