/* gog.c - see gog.h */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gog.h"
#include "sys.h"

/* ---- finding game.gog */

static int is_dreams(const char *image);

/* `path` if it is an image of this game's CD: other GOG releases of DOS
 * games ship a game.gog of the same format */
static int take(const char *path, char *out, size_t n)
{
    if (!sys_is_file(path) || !is_dreams(path))
        return 0;
    snprintf(out, n, "%s", path);
    return 1;
}

/* dir/game.gog */
static int take_in(const char *dir, char *out, size_t n)
{
    char path[SYS_PATH];
    sys_join(path, sizeof path, dir, "game.gog");
    return take(path, out, n);
}

#ifdef _WIN32
/* GOG's installers keep a key per game under GOG.com\Games, named by
 * the game's product ID (1207664093 for this one, as the Mac release's
 * goggame-1207664093.info says), with the folder in its value "path"
 * (not checked on a Windows installation) */
static int from_registry(const char *key, char *out, size_t n)
{
    char dir[MAX_PATH];
    DWORD size = sizeof dir;

    if (RegGetValueA(HKEY_LOCAL_MACHINE, key, "path", RRF_RT_REG_SZ, NULL, dir, &size) !=
        ERROR_SUCCESS)
        return 0;
    return take_in(dir, out, n);
}
#endif

int gog_find(char *out, size_t n)
{
    char dir[SYS_PATH], path[SYS_PATH];

    sys_exe_dir(dir, sizeof dir);
    if (take_in(dir, out, n) || take("game.gog", out, n))
        return 1;
#ifdef _WIN32
    {
        char drive;
        const char *pf = getenv("ProgramFiles(x86)");

        if (from_registry("SOFTWARE\\WOW6432Node\\GOG.com\\Games\\1207664093", out, n) ||
            from_registry("SOFTWARE\\GOG.com\\Games\\1207664093", out, n))
            return 1;
        /* the installer's default folder, on any drive; GOG Galaxy's */
        for (drive = 'C'; drive <= 'Z'; drive++) {
            snprintf(path, sizeof path, "%c:\\GOG Games\\Pinball Dreams Deluxe", drive);
            if (take_in(path, out, n))
                return 1;
        }
        snprintf(path, sizeof path, "%s\\GOG Galaxy\\Games\\Pinball Dreams Deluxe",
                 pf ? pf : "C:\\Program Files (x86)");
        if (take_in(path, out, n))
            return 1;
    }
#else
    {
        /* On a Mac the release is an application with a Boxer (DOSBox)
         * bundle inside, the image in its CD folder */
        static const char *const bundle =
            "Pinball Dreams Deluxe.app/Contents/Resources/game/Pinball Dreams.app/Contents/"
            "Resources/Pinball Dreams.boxer/game.cdmedia/game.gog";
        /* Elsewhere (the Windows release under Wine, Heroic, Lutris):
         * guesses at the usual folders, not checked */
        static const char *const home_dirs[] = {
            "Applications", "GOG Games/Pinball Dreams Deluxe",
            "Games/Heroic/Pinball Dreams Deluxe",
            ".wine/drive_c/GOG Games/Pinball Dreams Deluxe",
        };
        char home[SYS_PATH];
        size_t i;

        snprintf(path, sizeof path, "/Applications/%s", bundle);
        if (take(path, out, n))
            return 1;
        sys_home_dir(home, sizeof home);
        for (i = 0; i < sizeof home_dirs / sizeof home_dirs[0]; i++) {
            sys_join(dir, sizeof dir, home, home_dirs[i]);
            if (i == 0) {
                sys_join(path, sizeof path, dir, bundle);
                if (take(path, out, n))
                    return 1;
            } else if (take_in(dir, out, n)) {
                return 1;
            }
        }
    }
#endif
    return 0;
}

/* ---- the image */

#define RAW 2352
#define DATA 2048
#define MAX_DEPTH 8

typedef struct {
    FILE *f;
    long total, done;           /* bytes */
    int writing, has_program;
    int (*progress)(void *ctx, const char *file, long done, long total);
    void *ctx;
    char *err;
    size_t n;
} Unpack;

/* sector `lba`'s 2048 bytes of data */
static int sector(Unpack *u, uint32_t lba, uint8_t *out)
{
    uint8_t raw[RAW];

    if (fseek(u->f, (long)lba * RAW, SEEK_SET) != 0 || fread(raw, 1, RAW, u->f) != RAW)
        return -1;
    if (raw[15] == 1)
        memcpy(out, raw + 16, DATA);
    else if (raw[15] == 2)
        memcpy(out, raw + 24, DATA);    /* after the 8-byte subheader */
    else
        return -1;
    return 0;
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int fail(Unpack *u, const char *what)
{
    snprintf(u->err, u->n, "%s", what);
    return -1;
}

/* a name the CD may have: letters, digits and the few signs DOS allows */
static int plain_name(const char *s)
{
    if (!*s || !strcmp(s, ".") || !strcmp(s, ".."))
        return 0;
    for (; *s; s++)
        if (!((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') ||
              strchr("_-.~!#$%&'()@^{}", *s)))
            return 0;
    return 1;
}

/* a file of `size` bytes from sector `lba` into `path` */
static int copy_file(Unpack *u, uint32_t lba, uint32_t size, const char *path)
{
    uint8_t buf[DATA];
    FILE *out = fopen(path, "wb");
    uint32_t left = size;

    if (!out)
        return fail(u, "A file could not be written.");
    while (left) {
        uint32_t k = left < DATA ? left : DATA;
        if (sector(u, lba++, buf) || fwrite(buf, 1, k, out) != k) {
            fclose(out);
            return fail(u, "The image could not be read to its end, or a file not written.");
        }
        left -= k;
    }
    return fclose(out) == 0 ? 0 : fail(u, "A file could not be written.");
}

/* each entry of the directory at `lba` (`size` bytes); `dir` the folder
 * it goes to (writing), `rel` its path on the CD */
static int walk(Unpack *u, uint32_t lba, uint32_t size, const char *dir, const char *rel, int depth)
{
    uint8_t *data;
    uint32_t i, s, sectors = (size + DATA - 1) / DATA;
    int r = 0;

    if (depth > MAX_DEPTH || size > 0x100000)
        return fail(u, "The image's folders are not as a CD's are.");
    data = (uint8_t *)malloc((size_t)sectors * DATA);
    if (!data)
        return fail(u, "No memory.");
    for (s = 0; s < sectors; s++)
        if (sector(u, lba + s, data + s * DATA)) {
            free(data);
            return fail(u, "The image cannot be read.");
        }
    for (i = 0; r == 0 && i < size;) {
        uint8_t len = data[i];
        char name[64], path[SYS_PATH], sub[256];
        int is_dir;
        uint32_t elba, esize;

        if (len == 0) {                 /* records never cross a sector */
            i = (i / DATA + 1) * DATA;
            continue;
        }
        if (len < 34 || i + len > size || 33u + data[i + 32] > len) {
            r = fail(u, "The image's folders are not as a CD's are.");
            break;
        }
        snprintf(name, sizeof name, "%.*s", data[i + 32], (const char *)data + i + 33);
        elba = le32(data + i + 2);
        esize = le32(data + i + 10);
        is_dir = (data[i + 25] & 2) != 0;
        i += len;
        if (name[0] == 0 || name[0] == 1)
            continue;                   /* the folder itself, its parent */
        name[strcspn(name, ";")] = 0;
        if (!plain_name(name)) {
            r = fail(u, "The image has a file name no CD of the game has.");
            break;
        }
        snprintf(sub, sizeof sub, "%s%s%s", rel, *rel ? "/" : "", name);
        if (u->writing)
            sys_join(path, sizeof path, dir, name);
        if (is_dir) {
            if (u->writing && sys_mkdir(path) != 0)
                r = fail(u, "A folder could not be made.");
            else
                r = walk(u, elba, esize, path, sub, depth + 1);
        } else if (u->writing) {
            r = copy_file(u, elba, esize, path);
            u->done += (long)esize;
            if (r == 0 && u->progress && u->progress(u->ctx, sub, u->done, u->total))
                r = fail(u, "Stopped.");
        } else {
            u->total += (long)esize;
            if (!sys_stricmp(sub, "DREAMS1/PD.EXE"))
                u->has_program = 1;
        }
    }
    free(data);
    return r;
}

/* the folder and everything in it (only ever our own `dir`.part) */
static void remove_entry(void *ctx, const char *name, int is_dir);

static void remove_tree(const char *dir)
{
    sys_list_dir(dir, remove_entry, (void *)dir);
    sys_rmdir(dir);
}

static void remove_entry(void *ctx, const char *name, int is_dir)
{
    char path[SYS_PATH];
    sys_join(path, sizeof path, (const char *)ctx, name);
    if (is_dir)
        remove_tree(path);
    else
        remove(path);
}

/* 1 if `image` holds a CD file system with DREAMS1\PD.EXE in it */
static int is_dreams(const char *image)
{
    Unpack u;
    uint8_t pvd[DATA];
    char err[128];

    memset(&u, 0, sizeof u);
    u.err = err;
    u.n = sizeof err;
    u.f = fopen(image, "rb");
    if (!u.f)
        return 0;
    if (sector(&u, 16, pvd) || pvd[0] != 1 || memcmp(pvd + 1, "CD001", 5) != 0 ||
        walk(&u, le32(pvd + 156 + 2), le32(pvd + 156 + 10), NULL, "", 0) != 0)
        u.has_program = 0;
    fclose(u.f);
    return u.has_program;
}

int gog_unpack(const char *image, const char *dir,
               int (*progress)(void *ctx, const char *file, long done, long total), void *ctx,
               char *err, size_t n)
{
    Unpack u;
    uint8_t pvd[DATA];
    char part[SYS_PATH];
    int r;

    memset(&u, 0, sizeof u);
    u.progress = progress;
    u.ctx = ctx;
    u.err = err;
    u.n = n;
    if (sys_is_dir(dir) || sys_is_file(dir))
        return fail(&u, "The folder for the game's files is there already.");
    u.f = fopen(image, "rb");
    if (!u.f)
        return fail(&u, "The image cannot be opened.");
    /* the primary volume descriptor, sector 16: the root directory's record at 156 */
    if (sector(&u, 16, pvd) || pvd[0] != 1 || memcmp(pvd + 1, "CD001", 5) != 0) {
        fclose(u.f);
        return fail(&u, "The image holds no CD file system.");
    }
    r = walk(&u, le32(pvd + 156 + 2), le32(pvd + 156 + 10), NULL, "", 0);
    if (r == 0 && !u.has_program)
        r = fail(&u, "The image is not Pinball Dreams Deluxe's CD (no DREAMS1\\PD.EXE).");
    if (r == 0) {
        snprintf(part, sizeof part, "%s.part", dir);
        if (sys_is_dir(part))
            remove_tree(part);          /* left by an unpacking that was stopped */
        u.writing = 1;
        if (sys_mkdir(part) != 0)
            r = fail(&u, "The folder for the game's files cannot be made.");
        else
            r = walk(&u, le32(pvd + 156 + 2), le32(pvd + 156 + 10), part, "", 0);
        if (r == 0 && sys_rename(part, dir) != 0)
            r = fail(&u, "The unpacked files could not be moved to their folder.");
        if (r != 0)
            remove_tree(part);
    }
    fclose(u.f);
    return r;
}
