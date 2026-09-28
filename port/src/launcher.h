/* launcher.h - the setup screen shown before the game, in the style of a
 * DOS setup program (an 80x25 text screen, textmode.h, in the game's
 * window): the game's options as the menu's F10 screen sets them, the
 * sound and the window, and the start of a table.
 *
 * The game's options go into DDPCOPTN.BIN in the save folder
 * (sys_data_dir()/save/DELUXE), where the table programs read it, in the
 * 13 bytes the menu writes.  The port's own settings (volume, sound
 * shaping, full screen, the last table) go into sys_data_dir()/pdd.cfg,
 * a line "name = value" each. */
#ifndef PD_LAUNCHER_H
#define PD_LAUNCHER_H

/* pdd.cfg read and applied: the volume and shaping (hud.c), the output's
 * shaping (snd_set_fx), full screen or not */
void launcher_load_settings(void);

/* the shaping as -fx gives it, kept like one set in the launcher */
void launcher_set_fx(int bass, int treble, int oomph, int headphone);

/* pdd.cfg written, with what the sound keys and Alt+Enter left */
void launcher_save_settings(void);

/* The setup screen until a table is chosen: 1 with *prog (1 PD.EXE, 2
 * PD2.EXE) and *table (0-3), 0 to quit (or the window was closed).
 * `game` is the game's folder (shown); `note`, if not NULL, is shown
 * first in a box (why the last table ended). */
int launcher_run(const char *game, int *prog, int *table, const char *note);

#endif
