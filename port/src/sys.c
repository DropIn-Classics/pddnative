/* sys.c - see sys.h */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sys.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define SEP '\\'
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#define SEP '/'
#endif
#ifdef __APPLE__
#include <limits.h>
#include <mach-o/dyld.h>
#endif

int sys_stricmp(const char *a, const char *b)
{
    while (*a && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
        a++;
        b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

void sys_join(char *out, size_t n, const char *dir, const char *name)
{
    size_t len = strlen(dir);
    if (len == 0 || dir[len - 1] == '\\' || dir[len - 1] == '/')
        snprintf(out, n, "%s%s", dir, name);
    else
        snprintf(out, n, "%s%c%s", dir, SEP, name);
}

static int is_sep(char c)
{
    return c == '\\' || c == '/';
}

int sys_parent(const char *path, char *out, size_t n)
{
    char p[SYS_PATH];
    size_t len;

    snprintf(p, sizeof p, "%s", path);
    len = strlen(p);
    while (len > 1 && is_sep(p[len - 1]))           /* no trailing separator but the root's */
        p[--len] = 0;
#ifdef _WIN32
    if (len == 0)
        return 0;
    if (p[1] == ':' && (len == 2 || len == 3)) {    /* C: or C:\ - the drive list above */
        snprintf(out, n, "%s", "");
        return 1;
    }
#else
    if (len == 0 || !strcmp(p, "/"))
        return 0;
#endif
    while (len && !is_sep(p[len - 1]))
        len--;
    if (len == 0)
        return 0;                                   /* a bare name: nothing known above */
    if (len > 1 && !(len == 3 && p[1] == ':'))      /* keep "/" and "C:\" */
        len--;
    p[len] = 0;
    snprintf(out, n, "%s", p);
    return 1;
}

#ifdef _WIN32

void sys_exe_dir(char *out, size_t n)
{
    char path[SYS_PATH];
    char *sep;
    DWORD len = GetModuleFileNameA(NULL, path, sizeof path);
    if (len == 0 || len >= sizeof path) {
        snprintf(out, n, ".");
        return;
    }
    sep = strrchr(path, '\\');
    if (sep)
        *sep = 0;
    snprintf(out, n, "%s", path);
}

/* PD_DATA_DIR if set, else beside the program, as ever on Windows */
void sys_data_dir(char *out, size_t n)
{
    const char *env = getenv("PD_DATA_DIR");

    if (env && *env) {
        snprintf(out, n, "%s", env);
        sys_mkdir(out);
        return;
    }
    sys_exe_dir(out, n);
}

int sys_is_dir(const char *path)
{
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

int sys_is_file(const char *path)
{
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

int sys_mkdir(const char *path)
{
    return CreateDirectoryA(path, NULL) ? 0 : -1;
}

int sys_rmdir(const char *path)
{
    return RemoveDirectoryA(path) ? 0 : -1;
}

int sys_rename(const char *from, const char *to)
{
    return MoveFileA(from, to) ? 0 : -1;
}

/* Windows file names ignore case already */
int sys_find(const char *dir, const char *name, char *out, size_t n)
{
    sys_join(out, n, dir, name);
    return sys_is_file(out) || sys_is_dir(out);
}

int sys_list_dir(const char *dir, void (*fn)(void *ctx, const char *name, int is_dir), void *ctx)
{
    char pattern[SYS_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;

    if (!*dir) {
        DWORD drives = GetLogicalDrives();
        int i;
        char root[4] = "A:\\";
        for (i = 0; i < 26; i++) {
            if (!(drives >> i & 1))
                continue;
            root[0] = (char)('A' + i);
            fn(ctx, root, 1);
        }
        return 0;
    }
    sys_join(pattern, sizeof pattern, dir, "*");
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return -1;
    do {
        if (strcmp(fd.cFileName, ".") && strcmp(fd.cFileName, ".."))
            fn(ctx, fd.cFileName, (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return 0;
}

void sys_home_dir(char *out, size_t n)
{
    const char *home = getenv("USERPROFILE");
    snprintf(out, n, "%s", home && *home ? home : "C:\\");
}

#else

void sys_exe_dir(char *out, size_t n)
{
    char path[SYS_PATH];
    char *sep;
#ifdef __APPLE__
    char link[SYS_PATH];
    uint32_t size = sizeof link;
    if (_NSGetExecutablePath(link, &size) != 0 || !realpath(link, path)) {
        snprintf(out, n, ".");
        return;
    }
#else
    ssize_t len = readlink("/proc/self/exe", path, sizeof path - 1);
    if (len <= 0) {
        snprintf(out, n, ".");
        return;
    }
    path[len] = 0;
#endif
    sep = strrchr(path, '/');
    if (sep)
        *sep = 0;
    snprintf(out, n, "%s", path);
}

/* the program's own folder can hold the settings and the import unless it
 * is inside an AppImage (mounted read-only) or a Mac app bundle (which
 * would lose its signature; Gatekeeper may also run it from a read-only
 * copy) */
static int packaged(const char *exe_dir)
{
#ifdef __APPLE__
    const char *tail = ".app/Contents/MacOS";
    size_t len = strlen(exe_dir), k = strlen(tail);
    if (len >= k && !strcmp(exe_dir + len - k, tail))
        return 1;
#endif
    (void)exe_dir;
    return getenv("APPIMAGE") != NULL;
}

/* PD_DATA_DIR if set; the program's directory if it can be written and is
 * not packaged; else ~/Library/Application Support/Pinball Dreams on a
 * Mac, $XDG_DATA_HOME/pinball-dreams or ~/.local/share/pinball-dreams
 * elsewhere */
void sys_data_dir(char *out, size_t n)
{
    const char *env = getenv("PD_DATA_DIR"), *home;
    char base[SYS_PATH];

    if (env && *env) {
        snprintf(out, n, "%s", env);
        sys_mkdir(out);
        return;
    }
    sys_exe_dir(out, n);
    if (!packaged(out) && access(out, W_OK) == 0)
        return;
    home = getenv("HOME");
#ifdef __APPLE__
    snprintf(base, sizeof base, "%s/Library/Application Support", home ? home : ".");
    sys_mkdir(base);
    sys_join(out, n, base, "Pinball Dreams");
#else
    {
        const char *xdg = getenv("XDG_DATA_HOME");
        if (xdg && *xdg) {
            snprintf(base, sizeof base, "%s", xdg);
        } else {
            snprintf(base, sizeof base, "%s/.local", home ? home : ".");
            sys_mkdir(base);
            sys_join(base, sizeof base, base, "share");
        }
    }
    sys_mkdir(base);
    sys_join(out, n, base, "pinball-dreams");
#endif
    sys_mkdir(out);
}

int sys_is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int sys_is_file(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

int sys_mkdir(const char *path)
{
    return mkdir(path, 0777);
}

int sys_rmdir(const char *path)
{
    return rmdir(path);
}

int sys_rename(const char *from, const char *to)
{
    return rename(from, to);
}

int sys_find(const char *dir, const char *name, char *out, size_t n)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    int found = 0;
    if (!d)
        return 0;
    while (!found && (e = readdir(d)) != NULL) {
        if (sys_stricmp(e->d_name, name) == 0) {
            sys_join(out, n, dir, e->d_name);
            found = sys_is_file(out);
        }
    }
    closedir(d);
    return found;
}

int sys_list_dir(const char *dir, void (*fn)(void *ctx, const char *name, int is_dir), void *ctx)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    char path[SYS_PATH];

    if (!d)
        return -1;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        sys_join(path, sizeof path, dir, e->d_name);
        fn(ctx, e->d_name, sys_is_dir(path));       /* links to folders count as folders */
    }
    closedir(d);
    return 0;
}

void sys_home_dir(char *out, size_t n)
{
    const char *home = getenv("HOME");
    snprintf(out, n, "%s", home && *home ? home : "/");
}

#endif

uint8_t *sys_load(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    uint8_t *buf;
    long len;

    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    buf = (uint8_t *)malloc(len ? (size_t)len : 1);
    if (!buf || fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    if (size)
        *size = (size_t)len;
    return buf;
}

static int deck_model(const char *vendor, const char *product)
{
    return !strncmp(vendor, "Valve", 5)
           && (!strncmp(product, "Jupiter", 7) || !strncmp(product, "Galileo", 7));
}

#ifdef _WIN32

/* Windows keeps the firmware's names in the registry */
static int firmware_deck(void)
{
    static const char key[] = "HARDWARE\\DESCRIPTION\\System\\BIOS";
    char vendor[64], product[64];
    DWORD len = sizeof vendor;

    if (RegGetValueA(HKEY_LOCAL_MACHINE, key, "SystemManufacturer", RRF_RT_REG_SZ, NULL,
                     vendor, &len) != ERROR_SUCCESS)
        return 0;
    len = sizeof product;
    if (RegGetValueA(HKEY_LOCAL_MACHINE, key, "SystemProductName", RRF_RT_REG_SZ, NULL,
                     product, &len) != ERROR_SUCCESS)
        return 0;
    return deck_model(vendor, product);
}

#else

static void read_line(const char *path, char *out, size_t n)
{
    FILE *f = fopen(path, "r");
    out[0] = 0;
    if (f) {
        if (!fgets(out, (int)n, f))
            out[0] = 0;
        fclose(f);
    }
}

static int firmware_deck(void)
{
    char vendor[64], product[64];
    read_line("/sys/class/dmi/id/sys_vendor", vendor, sizeof vendor);
    read_line("/sys/class/dmi/id/product_name", product, sizeof product);
    return deck_model(vendor, product);
}

#endif

int sys_steam_deck(void)
{
    const char *env = getenv("SteamDeck");
    if (env && *env)
        return atoi(env) != 0;
    return firmware_deck();
}
