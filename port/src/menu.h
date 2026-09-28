/* menu.h - the menu, DDPCMAIN.EXE, in C: the list of the eight tables
 * (F1-F8, or the pointer moved with the arrows and Enter), the ticker
 * line, the high score show when nothing is chosen for a while, a table
 * run in the same process and the menu back after it; Esc leaves it.
 *
 * Translated from the generated source (build/DDPCMAIN.ASM) under the
 * names of src/DDPCMAIN.hints, on the program's own memory (mem.h, where
 * tools/run -loadfix loads it), so that it can be compared with runs of
 * the original.  Not translated: the intro (DDPCINTR.EXE, which `start`
 * runs first), the history viewer (F9) and the options screen (F10; the
 * setup screen has the options): chosen, the menu fades out and comes
 * back, as when the language screen before the history is left with Esc.
 * No mouse: the program sees none, as without a mouse driver. */
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

#endif
