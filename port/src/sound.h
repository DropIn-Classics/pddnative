/* sound.h - the sound driver's functions (INT 66h) that the table programs
 * call, played with modplay (micromod).  The functions and their
 * arguments are those PD.EXE passes (see pd_sound.c); what the driver
 * does with an argument is only taken as far as it is established for
 * Dreams, the rest is marked. */
#ifndef PD_SOUND_H
#define PD_SOUND_H

/* opens the audio output (once) */
void snd_init(void);

/* AL=12h: loads the module file at `path`; 0, or nonzero when it cannot */
int snd_load_module(const char *path);
/* AL=4: the music plays */
void snd_play(void);
/* AL=0: the music stops, the module is gone */
void snd_stop(void);
/* AL=6: the volume, CX = 0-100h (music_fade's steps; presumably 100h is
 * full, not checked) */
void snd_volume(int cx);
/* AL=10h: the module goes on at order position BX after the row it plays
 * (SBLASTER.SDR: the position counter BX - 1, a jump pending); returns in
 * AL the position it was playing (PD.EXE keeps it in music_pos) */
int snd_position(int bx);
/* AL=13h: the program's routine the driver calls at a pattern jump (Bxx)
 * with AL = AH = the jump's target; the module goes on at the position in
 * AL it returns (SBLASTER.SDR; not called while a jump is pending) */
void snd_jump_callback(int (*callback)(int target));
/* AL=11h: a sound effect with BL, BH, CL, DL from the table's effect
 * list, a note put at once into a channel of the module (SBLASTER.SDR):
 * DL the channel (from 1), BL the note (1-36, the driver's periods 856 to
 * 113), CL the sample (from 1), BH the volume (as effect C; 0: the
 * sample's own).  The module's next row on that channel follows as
 * usual. */
void snd_effect(int bl, int bh, int cl, int dl);

#endif
