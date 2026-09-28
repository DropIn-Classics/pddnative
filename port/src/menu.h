/* menu.h - the menu, DDPCMAIN.EXE, in C: the list of the eight tables
 * (F1-F8, or the pointer moved with the arrows and Enter), the ticker
 * line, the high score show when nothing is chosen for a while, a table
 * run in the same process and the menu back after it, the history (F9:
 * the language screen, then the History of Pinball in its 16-colour
 * pictures); Esc leaves it.
 *
 * Translated from the generated source (build/DDPCMAIN.ASM) under the
 * names of src/DDPCMAIN.hints, on the program's own memory (mem.h, where
 * tools/run -loadfix loads it), so that it can be compared with runs of
 * the original.  Not translated: the intro (DDPCINTR.EXE, which `start`
 * runs first), the history's VESA pictures (no VESA BIOS here, as in
 * tools/run) and the options screen (F10; the setup screen has the
 * options): chosen, the menu fades out and comes back.  The mouse driver
 * is the port's (menu_mouse), with the keyboard behind it. */
#ifndef PD_MENU_H
#define PD_MENU_H
#include <stddef.h>

typedef struct {
    /* at the start, where the menu has DDFLIPLY play the intro's
     * animations; NULL for none.  0 when the window was closed. */
    int (*intro)(void);
    /* table `table` (0-3) of PD.EXE (prog 1) or PD2.EXE (prog 2), with
     * what comes before it (the table's animation); 1 when it ended, 0 when
     * the window was closed, -1 with a message in err after an error */
    int (*table)(int prog, int table, char *err, size_t n);
} MenuHooks;

/* The menu until Esc leaves it (0), the window is closed (0) or an error
 * ends it (-1, the message in err).  PD_STOP=where#N ends it the Nth time
 * it passes `where` (menu_loop, hiscore_show_loop), PD_TRACE prints each
 * pass, as the table programs' checkpoints do. */
int menu_run(const char *game, const MenuHooks *hooks, char *err, size_t n);

/* Not 0: the port's box in place of the pointer, a frame around the
 * entry of one table or of the history that Up and Down move (the list
 * scrolls with it) and Enter chooses; in the high score show Enter and
 * the arrows go back to the list.  The mouse moved or clicked shows the
 * pointer instead, Up, Down or Enter the box again.  0 (the default):
 * the pointer, as the original has it. */
extern int menu_box;

/* Not 0 (the default): the port's mouse driver, which the programs give
 * the pointer the keys and the computer's mouse move and which draws its
 * pointer where they have it show one (the history's 640x480 screens).
 * 0: no driver, as in tools/run (for comparisons with it). */
extern int menu_mouse;

#endif
