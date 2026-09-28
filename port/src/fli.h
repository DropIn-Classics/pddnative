/* fli.h - the animations of the DELUXE folder's .FLI files (Autodesk
 * FLI, 320x200, 256 colours) played as DDFLIPLY.EXE plays them
 * (read from its code; docs/HANDOFF.md): each file's frames but the last
 * (the ring frame back to the first), the header's speed times 41h units
 * of a clock of 256 units a BIOS tick between two; a key ends the
 * playback, and with it the files still to come.
 *
 * DDFLIPLY's arguments: 0 plays SPIN21ST, INTRO_P1, INTRO_P2, INTRO_P3
 * in turn (at the menu's start), 1-8 the table's own (the menu's F1-F8,
 * before the table). */
#ifndef PD_FLI_H
#define PD_FLI_H

/* the files `names` of the game's DELUXE folder one after the other; a
 * file that is missing or cannot be read is left out.  0 when the window
 * was closed. */
int fli_play(const char *game, const char *const *names, int count);

/* the last picture shown held for `hold` frames of the menu's
 * wait_retrace, then faded to black in 32 (DDPCMAIN's fade_out_screen).
 * 0 when the window was closed. */
int fli_hold_fade(int hold);
/* before table `t` (0-7: F1-F8) as the menu runs it: its animation, the
 * last picture held for 46h frames, then faded out (DDPCMAIN's
 * run_selection).  0 when the window was closed. */
int fli_before_table(const char *game, int t);
/* at the menu's start (DDPCMAIN's `start`): the intro's animations
 * (DDFLIPLY with 0), then faded out.  0 when the window was closed. */
int fli_intro(const char *game);

#endif
