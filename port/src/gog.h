/* gog.h - the game's files from the player's GOG release of Pinball
 * Dreams Deluxe.  GOG ships the CD's data track as game.gog, a raw image
 * of 2352-byte sectors (Mode 1, or Mode 2 Form 1: 2048 bytes of data
 * each) with an ISO 9660 file system; it is unpacked into a folder as
 * tools/gogx.py does (the files as they are, names without ";1").  The
 * CD audio tracks beside it are not used. */
#ifndef PD_GOG_H
#define PD_GOG_H

#include <stddef.h>

/* The installed release's game.gog: where GOG's installers put it (on
 * Windows also where GOG's registry entry for the game says it is),
 * beside the program and in the current directory; only an image with
 * the game's DREAMS1\PD.EXE in it counts.  1 if found. */
int gog_find(char *out, size_t n);

/* The files of `image` unpacked into `dir`, which must not exist: into
 * `dir`.part first, renamed when complete.  `progress` (may be NULL) is
 * called after each file with the bytes written so far and in all; a
 * nonzero return stops the unpacking.  0 on success, else -1 with the
 * reason in err. */
int gog_unpack(const char *image, const char *dir,
               int (*progress)(void *ctx, const char *file, long done, long total), void *ctx,
               char *err, size_t n);

#endif
