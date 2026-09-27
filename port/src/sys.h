/* sys.h - files and directories, on Windows and (for tests) POSIX */
#ifndef PD_SYS_H
#define PD_SYS_H

#include <stddef.h>
#include <stdint.h>

#define SYS_PATH 1024

/* the directory the program runs from, without a trailing separator */
void sys_exe_dir(char *out, size_t n);

/* where the settings and the GOG import go: beside the program on Windows;
 * elsewhere too if that can be written, else the user's data directory */
void sys_data_dir(char *out, size_t n);

/* dir + separator + name; name alone if dir is empty */
void sys_join(char *out, size_t n, const char *dir, const char *name);

/* the folder above path (out may be path); 0 if there is none.  On
 * Windows the drive's root has the empty path above it, the drive list. */
int sys_parent(const char *path, char *out, size_t n);

/* calls fn for each entry of dir but . and .. (is_dir for folders); on
 * Windows the empty path lists the drives, "C:\" and so on.  0, or -1 if
 * dir cannot be read. */
int sys_list_dir(const char *dir, void (*fn)(void *ctx, const char *name, int is_dir), void *ctx);

/* the user's home folder (on Windows the profile folder) */
void sys_home_dir(char *out, size_t n);

int sys_is_dir(const char *path);
int sys_is_file(const char *path);
int sys_mkdir(const char *path);        /* 0 on success */
int sys_rmdir(const char *path);
int sys_rename(const char *from, const char *to);

/* The path of `name` in `dir`, ignoring case (the game's file names are
 * upper case on the disc, mixed case in its own strings).  1 if found. */
int sys_find(const char *dir, const char *name, char *out, size_t n);

/* a whole file in a malloc'ed buffer, NULL if it cannot be read */
uint8_t *sys_load(const char *path, size_t *size);

int sys_stricmp(const char *a, const char *b);

/* 1 on a Steam Deck (the game starts fullscreen there): Steam says so in
 * SteamDeck=1, else the firmware names Valve's Jupiter (LCD) or Galileo
 * (OLED), under SteamOS as under Windows */
int sys_steam_deck(void);

#endif
