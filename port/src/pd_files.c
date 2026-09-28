/* pd_files.c - files (INT 21h): the table's files, the high scores, the
 * options.  See pd.h.
 *
 * A DOS name is looked up as the program would see it: "C:\DELUXE\X" from
 * the root of the game's folder, a name without a folder in the program's
 * own folder (DREAMS1 or DREAMS2, where it starts; DELUXE for the menu).
 * Files the program writes go into the save folder (sys_data_dir()/save,
 * the same tree), and a name found there is taken before the game's. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pd.h"
#include "sys.h"

/* `dos` relative to the game's root, with '\' between the parts */
static void dos_rel(uint16_t seg, uint16_t off, char *rel, size_t n)
{
    char name[128];
    const char *p = name;
    size_t i;

    for (i = 0; i < sizeof name - 1 && frb(seg, (uint16_t)(off + i)); i++)
        name[i] = (char)frb(seg, (uint16_t)(off + i));
    name[i] = 0;
    if (p[0] && p[1] == ':')
        p += 2;
    if (*p == '\\')
        snprintf(rel, n, "%s", p + 1);
    else
        snprintf(rel, n, "%s\\%s", prog_id == 1 ? "DREAMS1" : prog_id == 2 ? "DREAMS2" : "DELUXE", p);
}

/* each part of `rel` found in `root` ignoring case; 1 when all are */
static int find_rel(const char *root, const char *rel, char *out, size_t n)
{
    char part[128], dir[SYS_PATH];
    const char *p = rel;

    snprintf(dir, sizeof dir, "%s", root);
    while (*p) {
        size_t k = strcspn(p, "\\");
        if (k >= sizeof part)
            return 0;
        memcpy(part, p, k);
        part[k] = 0;
        if (!sys_find(dir, part, out, n))
            return 0;
        snprintf(dir, sizeof dir, "%s", out);
        p += k;
        if (*p)
            p++;
    }
    return 1;
}

static void save_root(char *out, size_t n)
{
    char data[SYS_PATH];
    sys_data_dir(data, sizeof data);
    sys_join(out, n, data, "save");
}

int dos_path(uint16_t seg, uint16_t off, char *out, size_t n)
{
    char rel[256], root[SYS_PATH];

    dos_rel(seg, off, rel, sizeof rel);
    save_root(root, sizeof root);
    if (find_rel(root, rel, out, n) || find_rel(pd_game_dir, rel, out, n))
        return 0;
    return -1;
}

/* the path to write the file named at seg:off to (folders made) */
static void dos_write_path(uint16_t seg, uint16_t off, char *out, size_t n)
{
    char rel[256], dir[SYS_PATH], part[128];
    const char *p = rel;

    dos_rel(seg, off, rel, sizeof rel);
    save_root(dir, sizeof dir);
    sys_mkdir(dir);
    for (;;) {
        size_t k = strcspn(p, "\\");
        snprintf(part, sizeof part, "%.*s", (int)k, p);
        sys_join(out, n, dir, part);
        if (!p[k])
            return;
        sys_mkdir(out);
        snprintf(dir, sizeof dir, "%s", out);
        p += k + 1;
    }
}

/* the file named at DS:DX (DATA) into seg:di; 1 on failure */
int load_file(uint16_t name, uint16_t seg, uint16_t di)
{
    char path[SYS_PATH];
    uint8_t *data;
    size_t size, i;
    uint32_t a = lin(seg, di);

    if (dos_path(seg_data, name, path, sizeof path))
        return 1;
    data = sys_load(path, &size);
    if (!data)
        return 1;
    if (V(load_handle) != NONE)
        fww(seg_code, V(load_handle), 5);           /* DOS's first free handle */
    for (i = 0; i < size && a + i < MEM_SIZE; i++)
        mem[a + i] = data[i];
    free(data);
    return 0;
}

/* creates the file named at DS:DX and writes [file_size] bytes from
 * seg:di; 1 on failure */
int write_file(uint16_t name, uint16_t seg, uint16_t di)
{
    char path[SYS_PATH];
    FILE *f;
    uint16_t i, size = rw(V(file_size));

    dos_write_path(seg_data, name, path, sizeof path);
    f = fopen(path, "wb");
    if (!f)
        return 1;
    if (prog_id == 1)
        ww(V(file_handle), 5);          /* DOS's first free handle */
    for (i = 0; i < size; i++)
        fputc(frb(seg, (uint16_t)(di + i)), f);
    return fclose(f) != 0;
}

/* C:\DELUXE\HISCORES.PD1 into hiscores, or the defaults (hiscores_default) */
void load_hiscores(void)
{
    uint16_t i;

    if (!load_file(V(hiscore_file), seg_data, V(hiscores)))
        return;
    for (i = 0; i < 0x90; i++)
        wb((uint16_t)(V(hiscores) + i), rb((uint16_t)(V(hiscores_default) + i)));
}

void save_hiscores(void)
{
    ww(V(file_size), 0x90);
    write_file(V(hiscore_file), seg_data, V(hiscores));
}

/* C:\DELUXE\DDPCOPTN.BIN into the options, or the defaults when it is
 * missing; opt_balls 1 becomes 3 balls, else 5 */
void load_options(void)
{
    uint16_t di = V(opt_balls);

    if (load_file(V(options_file), seg_data, di)) {
        wb(di, 1);
        wb((uint16_t)(di + 1), 1);
        wb((uint16_t)(di + 2), 2);
        wb((uint16_t)(di + 3), 1);
        ww((uint16_t)(di + 4), 0x0405);
        ww((uint16_t)(di + 6), 0x4006);
        ww((uint16_t)(di + 8), 0x0207);
        ww((uint16_t)(di + 0x0A), 0x011A);
        ww((uint16_t)(di + 0x0C), 1);
    }
    wb(di, rb(di) == 1 ? 3 : 5);
}
