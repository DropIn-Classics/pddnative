/* sys.h - files and directories, on Windows and (for tests) POSIX */
#ifndef PD_SYS_H
#define PD_SYS_H

#include <stddef.h>
#include <stdint.h>

#define SYS_PATH 1024

/* the directory the program runs from, without a trailing separator */
void sys_exe_dir(char *out, size_t n);

/* where settings, saves and the GOG import go, made if missing:
 * $PD_DATA_DIR if set, else the user's data folder: %LOCALAPPDATA%\Pinball
 * Dreams on Windows, ~/Library/Application Support/Pinball Dreams on a Mac,
 * $XDG_DATA_HOME/pinball-dreams or ~/.local/share/pinball-dreams elsewhere.
 * Never the program's folder, so that a newer release can take its place. */
void sys_data_dir(char *out, size_t n);

/* What an earlier version wrote beside the program moved into
 * sys_data_dir: each of `names` (files or folders; NULL ends the list)
 * that is beside the program and not yet in the data folder.  Renamed
 * where that works, else copied (another drive; the old one left).  The
 * number moved. */
int sys_data_migrate(const char *const *names);

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
