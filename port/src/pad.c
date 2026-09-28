/* pad.c - see pad.h */
#include <string.h>
#include "pad.h"

static int context = PAD_TEXT;
static int game_keys[4] = { 0x2A, 0x36, 0x39, 0xD0 };  /* load_options' defaults */
static int presses[PAD_BUTTONS];        /* controllers holding the button */
static int pressed[PAD_BUTTONS][2];     /* the keys it put down, 0: none */
static int key_holds[256];              /* buttons holding the key */

#define DEFAULT_MAP { \
    [PAD_A] = PA_PLUNGER, [PAD_B] = PA_NO, [PAD_X] = PA_NUDGE, [PAD_Y] = PA_YES, \
    [PAD_BACK] = PA_ESC, [PAD_START] = PA_START, \
    [PAD_LB] = PA_LEFT_FLIPPER, [PAD_LT] = PA_LEFT_FLIPPER, \
    [PAD_RB] = PA_RIGHT_FLIPPER, [PAD_RT] = PA_RIGHT_FLIPPER, \
    [PAD_UP] = PA_NUDGE, [PAD_DOWN] = PA_PLUNGER, \
}

int pad_map[PAD_BUTTONS] = DEFAULT_MAP;
static const int default_map[PAD_BUTTONS] = DEFAULT_MAP;

void pad_default_map(void)
{
    memcpy(pad_map, default_map, sizeof pad_map);
}

int pad_set_context(int c)
{
    int was = context;
    context = c;
    return was;
}

void pad_set_game_keys(const int codes[4])
{
    memcpy(game_keys, codes, sizeof game_keys);
}

int pad_held(int button)
{
    return button >= 0 && button < PAD_BUTTONS && presses[button] > 0;
}

/* the keys the button stands for now: up to two into out; how many */
static int keys_for(int button, int out[2])
{
    if (context == PAD_TEXT || context == PAD_MENU) {
        switch (button) {
        case PAD_UP:    out[0] = 0xC8; return 1;
        case PAD_DOWN:  out[0] = 0xD0; return 1;
        case PAD_LEFT:  out[0] = 0xCB; return 1;
        case PAD_RIGHT: out[0] = 0xCD; return 1;
        case PAD_A: case PAD_START: out[0] = 0x1C; return 1;
        case PAD_B: case PAD_BACK:  out[0] = 0x01; return 1;
        }
        return 0;
    }
    if (context != PAD_TABLE)
        return 0;
    switch (pad_map[button]) {
    case PA_LEFT_FLIPPER:  out[0] = game_keys[0]; return 1;
    case PA_RIGHT_FLIPPER: out[0] = game_keys[1]; return 1;
    case PA_NUDGE:         out[0] = game_keys[2]; return 1;
    case PA_PLUNGER:       out[0] = game_keys[3]; return 1;
    /* F1 starts a game (1 player) where the table waits for one, P
     * pauses one being played; neither is read where the other is */
    case PA_START: out[0] = 0x3B; out[1] = 0x19; return 2;
    case PA_ESC:   out[0] = 0x01; return 1;
    case PA_YES:   out[0] = 0x15; return 1;
    case PA_NO:    out[0] = 0x31; return 1;
    }
    return 0;
}

void pad_button(int button, int down, void (*key)(int code, int up))
{
    int codes[2], n, i;

    if (button < 0 || button >= PAD_BUTTONS)
        return;
    if (down) {
        if (presses[button]++)
            return;                     /* another controller holds it already */
        n = keys_for(button, codes);
        for (i = 0; i < 2; i++) {
            pressed[button][i] = i < n ? codes[i] & 0xFF : 0;
            if (i < n && key_holds[codes[i] & 0xFF]++ == 0)
                key(codes[i] & 0xFF, 0);
        }
        return;
    }
    if (presses[button] == 0 || --presses[button])
        return;
    /* the keys it put down, even where the context changed since */
    for (i = 0; i < 2; i++) {
        int c = pressed[button][i];
        if (c && --key_holds[c] == 0)
            key(c, 1);
        pressed[button][i] = 0;
    }
}

const char *pad_button_cfg_name(int button)
{
    static const char *const names[PAD_BUTTONS] = {
        "a", "b", "x", "y", "back", "start", "leftstick", "rightstick",
        "leftshoulder", "rightshoulder", "lefttrigger", "righttrigger",
        "up", "down", "left", "right",
    };
    return names[button];
}

const char *pad_action_name(int action)
{
    static const char *const names[PA_ACTIONS] = {
        "-", "Left flipper", "Right flipper", "Plunger", "Nudge",
        "Start game / pause", "Esc", "Yes (Y)", "No (N)",
    };
    return action >= 0 && action < PA_ACTIONS ? names[action] : "-";
}
