/* pad.h - game controllers: their buttons become keys.
 *
 * The platform reports the buttons of every controller it knows (plugged
 * in before the start or while the game runs) as pad_button calls; each
 * becomes the scan codes of keys, pushed into the same queue as the
 * keyboard's, so the programs read them as keys and -record writes them
 * like typed ones.  What a button stands for depends on where the player
 * is (pad_set_context): in the setup screen and the menu the D-pad (and the
 * left stick) are the arrow keys, A and Start Enter, B and Back Esc; in a
 * table each button has one of the actions below (pdd.cfg, the setup
 * screen's "Controller" page), the flippers, nudge and plunger being the
 * keys the game options set (pad_set_game_keys). */
#ifndef PD_PAD_H
#define PD_PAD_H

/* the buttons, named by where they sit (the Xbox names) */
enum {
    PAD_A, PAD_B, PAD_X, PAD_Y, PAD_BACK, PAD_START,
    PAD_LSTICK, PAD_RSTICK, PAD_LB, PAD_RB, PAD_LT, PAD_RT,
    PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT,
    PAD_BUTTONS
};

/* what a button does in a table */
enum {
    PA_NONE, PA_LEFT_FLIPPER, PA_RIGHT_FLIPPER, PA_PLUNGER, PA_NUDGE,
    PA_START, PA_ESC, PA_YES, PA_NO,
    PA_ACTIONS
};

enum { PAD_OFF, PAD_TEXT, PAD_MENU, PAD_TABLE };

/* A button pressed (down 1) or let go on some controller; `key` is called
 * with a make code (E0 keys + 80h) and up 0 or 1 for each key that goes
 * down or up by it.  A key two buttons hold goes up with the last. */
void pad_button(int button, int down, void (*key)(int code, int up));

/* where the buttons go from now on (PAD_OFF: nowhere); the one before */
int pad_set_context(int context);

/* the table's four keys (make codes, E0 keys + 80h): left flipper, right
 * flipper, nudge, plunger */
void pad_set_game_keys(const int codes[4]);

/* the table's button -> action map, and the defaults */
extern int pad_map[PAD_BUTTONS];
void pad_default_map(void);

/* 1 while some controller holds the button */
int pad_held(int button);

/* the button's name in pdd.cfg ("pad" + a lower case word), the action's
 * in the setup screen */
const char *pad_button_cfg_name(int button);
const char *pad_action_name(int action);

#endif
