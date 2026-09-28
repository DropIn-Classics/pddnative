/* hud.h - the sound keys during the game and what they show: + and -
 * the volume, * mute, / the output's shaping (audiofx) on and off.  A
 * short box at the top of the picture says what was set.
 *
 * None of this reaches the game's memory: the keys are taken before the
 * program's keyboard handler, the box is drawn into the picture after
 * vga_render. */
#ifndef PD_HUD_H
#define PD_HUD_H

#include "vga.h"

#define HUD_VOLUME_MAX 10

/* a key from plat_read_control() */
void hud_control(int control);

/* the volume (0 to HUD_VOLUME_MAX, 3 dB a step) and the shaping on or off,
 * as the keys leave them; hud_set applies them (not muted) */
void hud_set(int volume, int shaping);
void hud_get(int *volume, int *shaping);

/* the box over the picture, while it is shown */
void hud_draw(VgaFrame *picture);

#endif
