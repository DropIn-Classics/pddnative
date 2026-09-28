/* launcher.c - see launcher.h */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gog.h"
#include "hud.h"
#include "launcher.h"
#include "pad.h"
#include "pd.h"
#include "platform.h"
#include "sound.h"
#include "sys.h"
#include "textmode.h"

/* ---- the port's settings: pdd.cfg */

static struct {
    int volume, shaping;                /* hud.c's */
    int bass, treble, oomph, headphone; /* audiofx.h's */
    int fullscreen;                     /* -1: as the platform starts (the Steam Deck: full) */
    int table;                          /* the last table started, 0-7 (F1-F8) */
    int skip_animation, quick_ball, quick_bonus, menu_box;  /* the quality of life fixes */
} cfg = { HUD_VOLUME_MAX, 1, 0, 0, 0, 0, -1, 0, 1, 1, 1, 1 };

static const struct {
    const char *name;
    int *value, lo, hi;
} cfg_keys[] = {
    { "volume", &cfg.volume, 0, HUD_VOLUME_MAX },
    { "shaping", &cfg.shaping, 0, 1 },
    { "bass", &cfg.bass, -12, 12 },
    { "treble", &cfg.treble, -12, 12 },
    { "oomph", &cfg.oomph, 0, 12 },
    { "headphone", &cfg.headphone, 0, 1 },
    { "fullscreen", &cfg.fullscreen, 0, 1 },
    { "table", &cfg.table, 0, 7 },
    { "skipanimation", &cfg.skip_animation, 0, 1 },
    { "quickball", &cfg.quick_ball, 0, 1 },
    { "quickbonus", &cfg.quick_bonus, 0, 1 },
    { "menubox", &cfg.menu_box, 0, 1 },
};
#define NCFG (int)(sizeof cfg_keys / sizeof cfg_keys[0])

static void cfg_path(char *out, size_t n)
{
    char data[SYS_PATH];
    sys_data_dir(data, sizeof data);
    sys_join(out, n, data, "pdd.cfg");
}

static void apply_fx(void)
{
    snd_set_fx(cfg.bass, cfg.treble, cfg.oomph, cfg.headphone);
}

void launcher_load_settings(void)
{
    char path[SYS_PATH], line[200], name[64];
    FILE *f;
    int v, i;

    cfg_path(path, sizeof path);
    f = fopen(path, "r");
    while (f && fgets(line, sizeof line, f))
        if (sscanf(line, " %63[a-z] = %d", name, &v) == 2) {
            for (i = 0; i < NCFG; i++)
                if (!strcmp(name, cfg_keys[i].name) && v >= cfg_keys[i].lo && v <= cfg_keys[i].hi)
                    *cfg_keys[i].value = v;
            for (i = 0; i < PAD_BUTTONS; i++)
                if (!strncmp(name, "pad", 3) && !strcmp(name + 3, pad_button_cfg_name(i)) &&
                    v >= 0 && v < PA_ACTIONS)
                    pad_map[i] = v;
        }
    if (f)
        fclose(f);
    hud_set(cfg.volume, cfg.shaping);
    apply_fx();
    if (cfg.fullscreen >= 0)
        plat_set_fullscreen(cfg.fullscreen);
}

void launcher_set_fx(int bass, int treble, int oomph, int headphone)
{
    cfg.bass = bass < -12 ? -12 : bass > 12 ? 12 : bass;
    cfg.treble = treble < -12 ? -12 : treble > 12 ? 12 : treble;
    cfg.oomph = oomph < 0 ? 0 : oomph > 12 ? 12 : oomph;
    cfg.headphone = headphone != 0;
    apply_fx();
}

int launcher_qol(void)
{
    return (cfg.quick_ball ? QOL_NEXT_BALL : 0) | (cfg.quick_bonus ? QOL_BONUS : 0);
}

int launcher_skip_animation(void)
{
    return cfg.skip_animation;
}

int launcher_menu_box(void)
{
    return cfg.menu_box;
}

void launcher_save_settings(void)
{
    char path[SYS_PATH];
    FILE *f;
    int i;

    hud_get(&cfg.volume, &cfg.shaping);
    cfg.fullscreen = plat_fullscreen();
    cfg_path(path, sizeof path);
    f = fopen(path, "w");
    if (!f)
        return;
    fprintf(f, "# pdd's settings, written by its setup screen\n");
    for (i = 0; i < NCFG; i++)
        fprintf(f, "%s = %d\n", cfg_keys[i].name, *cfg_keys[i].value);
    fprintf(f, "# a controller's buttons in a table: 0 nothing, 1 left flipper, 2 right\n"
               "# flipper, 3 plunger, 4 nudge, 5 start a game or pause, 6 Esc, 7 Y, 8 N\n");
    for (i = 0; i < PAD_BUTTONS; i++)
        fprintf(f, "pad%s = %d\n", pad_button_cfg_name(i), pad_map[i]);
    fclose(f);
}

/* ---- the game's options: DDPCOPTN.BIN, as the menu's F10 screen writes
 * it and load_options reads it: balls (1: 3, 2: 5), music (1: tunes and
 * jingles, 2: the main tune only), palette (1: grey, 2: colour), slope
 * (1-3: gravity 9, 11, 13), four key words (key_map's byte, then its bit
 * as a mask: left flipper, right flipper, nudge, plunger), screen (1:
 * 320x200, 2: 320x350).  The ranges are those the F10 screen cycles
 * through (its hit boxes at DDPCMAIN's OPTIONS:0009, a current and a
 * highest value each). */

#define OPT_SIZE 13
enum { OPT_BALLS, OPT_MUSIC, OPT_PALETTE, OPT_SLOPE, OPT_KEYS, OPT_SCREEN = 12 };

static uint8_t opt[OPT_SIZE];

/* load_options' defaults when there is no file (the CD has none) */
static const uint8_t opt_default[OPT_SIZE] = {
    1, 1, 2, 1, 0x05, 0x04, 0x06, 0x40, 0x07, 0x02, 0x1A, 0x01, 1
};
static const uint8_t opt_max[OPT_SIZE] = { 2, 2, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0, 2 };

/* the file in the save folder, the folders made when `make` */
static void options_path(char *out, size_t n, int make)
{
    char data[SYS_PATH], save[SYS_PATH], deluxe[SYS_PATH];

    sys_data_dir(data, sizeof data);
    sys_join(save, sizeof save, data, "save");
    if (make)
        sys_mkdir(save);
    if (!sys_find(save, "DELUXE", deluxe, sizeof deluxe)) {
        sys_join(deluxe, sizeof deluxe, save, "DELUXE");
        if (make)
            sys_mkdir(deluxe);
    }
    if (!sys_find(deluxe, "DDPCOPTN.BIN", out, n))
        sys_join(out, n, deluxe, "DDPCOPTN.BIN");
}

static int key_code(int k)
{
    int index = opt[OPT_KEYS + 2 * k], mask = opt[OPT_KEYS + 2 * k + 1], bit;

    for (bit = 0; bit < 8; bit++)
        if (mask == 1 << bit)
            return index * 8 + bit;
    return -1;
}

static void set_key(int k, int code)
{
    opt[OPT_KEYS + 2 * k] = (uint8_t)(code >> 3);
    opt[OPT_KEYS + 2 * k + 1] = (uint8_t)(1 << (code & 7));
}

static void load_game_options(void)
{
    char path[SYS_PATH];
    uint8_t *data;
    size_t size;
    int i, k;

    memcpy(opt, opt_default, OPT_SIZE);
    options_path(path, sizeof path, 0);
    data = sys_load(path, &size);
    if (data && size == OPT_SIZE)
        memcpy(opt, data, OPT_SIZE);
    free(data);
    for (i = 0; i < OPT_SIZE; i++)
        if (opt_max[i] && (opt[i] < 1 || opt[i] > opt_max[i]))
            opt[i] = opt_default[i];
    for (k = 0; k < 4; k++)
        if (key_code(k) < 0 || opt[OPT_KEYS + 2 * k] >= 32) {
            opt[OPT_KEYS + 2 * k] = opt_default[OPT_KEYS + 2 * k];
            opt[OPT_KEYS + 2 * k + 1] = opt_default[OPT_KEYS + 2 * k + 1];
        }
}

static void save_game_options(void)
{
    char path[SYS_PATH];
    FILE *f;

    options_path(path, sizeof path, 1);
    f = fopen(path, "wb");
    if (!f)
        return;
    fwrite(opt, 1, OPT_SIZE, f);
    fclose(f);
}

/* ---- the keys' names (scan code set 1, E0 keys + 80h) */

static const struct { unsigned char code; const char *name; } key_names[] = {
    {0x01, "Esc"}, {0x02, "1"}, {0x03, "2"}, {0x04, "3"}, {0x05, "4"}, {0x06, "5"},
    {0x07, "6"}, {0x08, "7"}, {0x09, "8"}, {0x0A, "9"}, {0x0B, "0"}, {0x0C, "-"},
    {0x0D, "="}, {0x0E, "Backspace"}, {0x0F, "Tab"}, {0x10, "Q"}, {0x11, "W"},
    {0x12, "E"}, {0x13, "R"}, {0x14, "T"}, {0x15, "Y"}, {0x16, "U"}, {0x17, "I"},
    {0x18, "O"}, {0x19, "P"}, {0x1A, "["}, {0x1B, "]"}, {0x1C, "Enter"},
    {0x1D, "Left Ctrl"}, {0x1E, "A"}, {0x1F, "S"}, {0x20, "D"}, {0x21, "F"},
    {0x22, "G"}, {0x23, "H"}, {0x24, "J"}, {0x25, "K"}, {0x26, "L"}, {0x27, ";"},
    {0x28, "'"}, {0x29, "`"}, {0x2A, "Left Shift"}, {0x2B, "\\"}, {0x2C, "Z"},
    {0x2D, "X"}, {0x2E, "C"}, {0x2F, "V"}, {0x30, "B"}, {0x31, "N"}, {0x32, "M"},
    {0x33, ","}, {0x34, "."}, {0x35, "/"}, {0x36, "Right Shift"}, {0x37, "Keypad *"},
    {0x38, "Left Alt"}, {0x39, "Space"}, {0x3A, "Caps Lock"}, {0x3B, "F1"},
    {0x3C, "F2"}, {0x3D, "F3"}, {0x3E, "F4"}, {0x3F, "F5"}, {0x40, "F6"},
    {0x41, "F7"}, {0x42, "F8"}, {0x43, "F9"}, {0x44, "F10"}, {0x45, "Num Lock"},
    {0x46, "Scroll Lock"}, {0x47, "Keypad 7"}, {0x48, "Keypad 8"}, {0x49, "Keypad 9"},
    {0x4A, "Keypad -"}, {0x4B, "Keypad 4"}, {0x4C, "Keypad 5"}, {0x4D, "Keypad 6"},
    {0x4E, "Keypad +"}, {0x4F, "Keypad 1"}, {0x50, "Keypad 2"}, {0x51, "Keypad 3"},
    {0x52, "Keypad 0"}, {0x53, "Keypad ."}, {0x56, "< >"}, {0x57, "F11"},
    {0x58, "F12"}, {0x9C, "Keypad Enter"}, {0x9D, "Right Ctrl"}, {0xB5, "Keypad /"},
    {0xB8, "Right Alt"}, {0xC7, "Home"}, {0xC8, "Up"}, {0xC9, "Page Up"},
    {0xCB, "Left"}, {0xCD, "Right"}, {0xCF, "End"}, {0xD0, "Down"},
    {0xD1, "Page Down"}, {0xD2, "Insert"}, {0xD3, "Delete"}, {0xDB, "Left Win"},
    {0xDC, "Right Win"}, {0xDD, "Menu"},
};

static const char *key_name(int code)
{
    static char other[16];
    size_t i;

    for (i = 0; i < sizeof key_names / sizeof key_names[0]; i++)
        if (key_names[i].code == code)
            return key_names[i].name;
    snprintf(other, sizeof other, "key %02Xh", code);
    return other;
}

/* why the key cannot be a game key, or NULL: the table reads Esc, F1-F8
 * and P itself (read_game_keys, st_play), and the keypad's * - + / are
 * the sound keys, which frame.c keeps from the table */
static const char *key_refused(int code)
{
    if (code == 0x01)
        return "Esc leaves the table";
    if (code >= 0x3B && code <= 0x42)
        return "F1-F8 start a game in the table";
    if (code == 0x19)
        return "P pauses the table";
    if (code == 0x37 || code == 0x4A || code == 0x4E || code == 0xB5)
        return "the keypad's * - + / are the sound keys";
    return NULL;
}

/* ---- the pages */

enum {
    I_NONE, I_PLAY, I_MENU, I_OPTIONS, I_SOUND, I_QUIT,
    I_TABLE, I_TABLE_LAST = I_TABLE + 7,
    I_BALLS, I_MUSIC, I_COLOURS, I_ANGLE, I_SCREEN,
    I_KEY, I_KEY_LAST = I_KEY + 3, I_DEFAULTS, I_BACK,
    I_VOLUME, I_SHAPING, I_BASS, I_TREBLE, I_OOMPH, I_HEADPHONE, I_FULLSCREEN,
    I_QOL, I_SKIP_ANIMATION, I_QUICK_BALL, I_QUICK_BONUS, I_MENU_BOX,
    I_PAD, I_PAD_BUTTON, I_PAD_LAST = I_PAD_BUTTON + PAD_BUTTONS - 1, I_PAD_DEFAULTS
};
enum { K_GAP, K_HEADING, K_ITEM, K_CHOICE };
enum { P_MAIN, P_TABLES, P_OPTIONS, P_SOUND, P_QOL, P_PAD };

typedef struct {
    int kind, id;
    const char *label, *help;
} Item;

static const Item main_items[] = {
    { K_ITEM, I_PLAY, "Play a table", "One of the eight tables; Esc in the table comes back here." },
    { K_ITEM, I_MENU, "Play from the menu", "The game's own menu, F1-F8 there; Esc in the menu comes back here." },
    { K_GAP, 0, NULL, NULL },
    { K_ITEM, I_OPTIONS, "Game options", "Balls, music, colours, angle, screen, keys: the menu's F10 options." },
    { K_ITEM, I_SOUND, "Sound and window", "The volume, the sound's shaping, full screen." },
    { K_ITEM, I_PAD, "Controller", "What a game controller's buttons do in a table." },
    { K_ITEM, I_QOL, "Quality of life fixes", "Each on its own: no animation, a quicker next ball and bonus, a box in the menu." },
    { K_GAP, 0, NULL, NULL },
    { K_ITEM, I_QUIT, "Quit", "Back to the system." },
};

static const char *const table_names[8] = {
    "Ignition", "Steel Wheel", "Beat Box", "Nightmare",
    "Neptune", "Safari", "Revenge of the Robot Warriors", "Stall Turn",
};

static const Item table_items[] = {
    { K_HEADING, 0, "Pinball Dreams", NULL },
    { K_ITEM, I_TABLE + 0, NULL, NULL }, { K_ITEM, I_TABLE + 1, NULL, NULL },
    { K_ITEM, I_TABLE + 2, NULL, NULL }, { K_ITEM, I_TABLE + 3, NULL, NULL },
    { K_GAP, 0, NULL, NULL },
    { K_HEADING, 0, "Pinball Dreams 2", NULL },
    { K_ITEM, I_TABLE + 4, NULL, NULL }, { K_ITEM, I_TABLE + 5, NULL, NULL },
    { K_ITEM, I_TABLE + 6, NULL, NULL }, { K_ITEM, I_TABLE + 7, NULL, NULL },
};

static const Item option_items[] = {
    { K_CHOICE, I_BALLS, "Balls a game", "How many balls a game has." },
    { K_CHOICE, I_MUSIC, "Music", "The table's tunes and jingles, or its main tune only." },
    { K_CHOICE, I_COLOURS, "Colours", "The table in colour or in grey." },
    { K_CHOICE, I_ANGLE, "Table angle", "How steep the table is: the gravity is 9, 11 or 13." },
    { K_CHOICE, I_SCREEN, "Screen", "320x200, or 350 lines, where the table scrolls less." },
    { K_GAP, 0, NULL, NULL },
    { K_CHOICE, I_KEY + 0, "Left flipper", "Enter, then the key for the left flipper." },
    { K_CHOICE, I_KEY + 1, "Right flipper", "Enter, then the key for the right flipper." },
    { K_CHOICE, I_KEY + 2, "Nudge", "Enter, then the key that nudges the table (too often: tilt)." },
    { K_CHOICE, I_KEY + 3, "Plunger", "Enter, then the key that pulls the plunger; let go to launch." },
    { K_GAP, 0, NULL, NULL },
    { K_ITEM, I_DEFAULTS, "Defaults", "All of these as the game has them without an options file." },
    { K_ITEM, I_BACK, "Back", NULL },
};

static const Item sound_items[] = {
    { K_CHOICE, I_VOLUME, "Volume", "In the game: + and - (keypad or main keys), * mutes." },
    { K_CHOICE, I_SHAPING, "Sound shaping", "Off: the sound as the game made it (/ in the game)." },
    { K_CHOICE, I_BASS, "Bass", "A shelf at 200 Hz, when the shaping is on." },
    { K_CHOICE, I_TREBLE, "Treble", "A shelf at 4 kHz, when the shaping is on." },
    { K_CHOICE, I_OOMPH, "Oomph", "More of the low bass (120 Hz), when the shaping is on." },
    { K_CHOICE, I_HEADPHONE, "Headphone mode", "The mono music a little wider, for headphones." },
    { K_GAP, 0, NULL, NULL },
    { K_CHOICE, I_FULLSCREEN, "Full screen", "The whole monitor or a window; Alt+Enter too." },
    { K_GAP, 0, NULL, NULL },
    { K_ITEM, I_BACK, "Back", NULL },
};

static const Item qol_items[] = {
    { K_CHOICE, I_SKIP_ANIMATION, "Skip animation", "On: none before a table; off: the table's, as the menu plays it." },
    { K_CHOICE, I_QUICK_BALL, "Quick next ball", "On: before a ball only until the jingle ends; \"ball lost\" shorter." },
    { K_CHOICE, I_QUICK_BONUS, "Quick bonus", "On: the bonus counted twice as fast, shorter holds around it." },
    { K_CHOICE, I_MENU_BOX, "Menu box", "On: box for keys and controller, pointer for the mouse; off: pointer only." },
    { K_GAP, 0, NULL, NULL },
    { K_ITEM, I_BACK, "Back", NULL },
};

/* the buttons in pad.h's order; a mark before the name while one is held */
#define PAD_HELP "In a table. Here and in the menu: D-pad, A Enter, B Esc."
static const Item pad_items[] = {
    { K_CHOICE, I_PAD_BUTTON + PAD_A, "A (bottom)", PAD_HELP },
    { K_CHOICE, I_PAD_BUTTON + PAD_B, "B (right)", PAD_HELP },
    { K_CHOICE, I_PAD_BUTTON + PAD_X, "X (left)", PAD_HELP },
    { K_CHOICE, I_PAD_BUTTON + PAD_Y, "Y (top)", PAD_HELP },
    { K_CHOICE, I_PAD_BUTTON + PAD_BACK, "Back", PAD_HELP },
    { K_CHOICE, I_PAD_BUTTON + PAD_START, "Start", "In a table: F1 (a game for one player) and P (pause)." },
    { K_CHOICE, I_PAD_BUTTON + PAD_LSTICK, "Left stick press", PAD_HELP },
    { K_CHOICE, I_PAD_BUTTON + PAD_RSTICK, "Right stick press", PAD_HELP },
    { K_CHOICE, I_PAD_BUTTON + PAD_LB, "Left shoulder", PAD_HELP },
    { K_CHOICE, I_PAD_BUTTON + PAD_RB, "Right shoulder", PAD_HELP },
    { K_CHOICE, I_PAD_BUTTON + PAD_LT, "Left trigger", PAD_HELP },
    { K_CHOICE, I_PAD_BUTTON + PAD_RT, "Right trigger", PAD_HELP },
    { K_CHOICE, I_PAD_BUTTON + PAD_UP, "D-pad up", "The left stick as well. " PAD_HELP },
    { K_CHOICE, I_PAD_BUTTON + PAD_DOWN, "D-pad down", "The left stick as well. " PAD_HELP },
    { K_CHOICE, I_PAD_BUTTON + PAD_LEFT, "D-pad left", "The left stick as well. " PAD_HELP },
    { K_CHOICE, I_PAD_BUTTON + PAD_RIGHT, "D-pad right", "The left stick as well. " PAD_HELP },
    { K_ITEM, I_PAD_DEFAULTS, "Defaults", "The buttons as pdd starts with them. Esc: back." },
};

typedef struct {
    const char *title;
    const Item *items;
    int count, width, parent;
    int cursor;
} Page;

#define ITEMS(a) a, (int)(sizeof a / sizeof a[0])
static Page pages[] = {
    { "Pinball Dreams", ITEMS(main_items), 48, P_MAIN, 0 },
    { "Play a table", ITEMS(table_items), 48, P_MAIN, 1 },
    { "Game options", ITEMS(option_items), 60, P_MAIN, 0 },
    { "Sound and window", ITEMS(sound_items), 60, P_MAIN, 0 },
    { "Quality of life fixes", ITEMS(qol_items), 60, P_MAIN, 0 },
    { "Controller", ITEMS(pad_items), 60, P_MAIN, 0 },
};
static int page;

static int selectable(const Item *it)
{
    return it->kind == K_ITEM || it->kind == K_CHOICE;
}

static void move_cursor(int dir)
{
    Page *p = &pages[page];
    int i = p->cursor;

    do
        i += dir;
    while (i >= 0 && i < p->count && !selectable(&p->items[i]));
    if (i >= 0 && i < p->count)
        p->cursor = i;
}

static void cursor_to(int id)
{
    Page *p = &pages[page];
    int i;
    for (i = 0; i < p->count; i++)
        if (p->items[i].id == id)
            p->cursor = i;
}

static void go(int to)
{
    page = to;
    if (!selectable(&pages[page].items[pages[page].cursor]))
        move_cursor(1);
}

/* ---- the state of the screen: a box over the page, a note, a key being set */

static char box[400];           /* a message, lines split by '\n'; "" none */
static int capturing = -1;      /* the game key being set, 0-3 */
static int typed_sound_key;     /* the keys read now typed + - * / as well */
static char note[100];          /* instead of the help line for a while */
static uint64_t note_until;

static void show_note(const char *text)
{
    snprintf(note, sizeof note, "%s", text);
    note_until = plat_micros() + 3000000;
}

static void db(char *out, size_t n, int v)
{
    if (v)
        snprintf(out, n, "%+d dB", v);
    else
        snprintf(out, n, "0 dB");
}

static void value_text(int id, char *out, size_t n)
{
    int volume, shaping, i;

    hud_get(&volume, &shaping);
    switch (id) {
    case I_BALLS:   snprintf(out, n, "%d", opt[OPT_BALLS] == 1 ? 3 : 5); break;
    case I_MUSIC:   snprintf(out, n, "%s", opt[OPT_MUSIC] == 1 ? "Tunes and jingles" : "Main tune only"); break;
    case I_COLOURS: snprintf(out, n, "%s", opt[OPT_PALETTE] == 1 ? "Grey" : "Colour"); break;
    case I_ANGLE:   snprintf(out, n, "%d  %s", opt[OPT_SLOPE],
                             opt[OPT_SLOPE] == 1 ? "(flat)" : opt[OPT_SLOPE] == 3 ? "(steep)" : ""); break;
    case I_SCREEN:  snprintf(out, n, "%s", opt[OPT_SCREEN] == 2 ? "320x350" : "320x200"); break;
    case I_VOLUME:
        for (i = 0; i < HUD_VOLUME_MAX && i + 1 < (int)n; i++)
            out[i] = (char)(i < volume ? TM_SQUARE : TM_MIDDLE_DOT);
        snprintf(out + i, n - (size_t)i, "  %d", volume);
        break;
    case I_SHAPING:    snprintf(out, n, "%s", shaping ? "On" : "Off"); break;
    case I_BASS:       db(out, n, cfg.bass); break;
    case I_TREBLE:     db(out, n, cfg.treble); break;
    case I_OOMPH:      db(out, n, cfg.oomph); break;
    case I_HEADPHONE:  snprintf(out, n, "%s", cfg.headphone ? "On" : "Off"); break;
    case I_FULLSCREEN: snprintf(out, n, "%s", plat_fullscreen() ? "On" : "Off"); break;
    case I_SKIP_ANIMATION: snprintf(out, n, "%s", cfg.skip_animation ? "On" : "Off"); break;
    case I_QUICK_BALL:     snprintf(out, n, "%s", cfg.quick_ball ? "On" : "Off"); break;
    case I_QUICK_BONUS:    snprintf(out, n, "%s", cfg.quick_bonus ? "On" : "Off"); break;
    case I_MENU_BOX:       snprintf(out, n, "%s", cfg.menu_box ? "On" : "Off"); break;
    default:
        if (id >= I_KEY && id <= I_KEY_LAST)
            snprintf(out, n, "%s", key_name(key_code(id - I_KEY)));
        else if (id >= I_PAD_BUTTON && id <= I_PAD_LAST)
            snprintf(out, n, "%s", pad_action_name(pad_map[id - I_PAD_BUTTON]));
        else
            out[0] = 0;
    }
}

/* v + dir kept in lo..hi; `wrap` goes round */
static int step(int v, int dir, int lo, int hi, int wrap)
{
    v += dir;
    if (v < lo)
        return wrap ? hi : lo;
    if (v > hi)
        return wrap ? lo : hi;
    return v;
}

/* Left and Right (dir -1, 1) on a choice; Enter is 1 with wrap */
static void change(int id, int dir, int wrap)
{
    int volume, shaping;

    hud_get(&volume, &shaping);
    switch (id) {
    case I_BALLS:   opt[OPT_BALLS] = (uint8_t)step(opt[OPT_BALLS], dir, 1, 2, 1); break;
    case I_MUSIC:   opt[OPT_MUSIC] = (uint8_t)step(opt[OPT_MUSIC], dir, 1, 2, 1); break;
    case I_COLOURS: opt[OPT_PALETTE] = (uint8_t)step(opt[OPT_PALETTE], dir, 1, 2, 1); break;
    case I_ANGLE:   opt[OPT_SLOPE] = (uint8_t)step(opt[OPT_SLOPE], dir, 1, 3, wrap); break;
    case I_SCREEN:  opt[OPT_SCREEN] = (uint8_t)step(opt[OPT_SCREEN], dir, 1, 2, 1); break;
    case I_VOLUME:     hud_set(step(volume, dir, 0, HUD_VOLUME_MAX, wrap), shaping); break;
    case I_SHAPING:    hud_set(volume, !shaping); break;
    case I_BASS:       cfg.bass = step(cfg.bass, dir, -12, 12, wrap); apply_fx(); break;
    case I_TREBLE:     cfg.treble = step(cfg.treble, dir, -12, 12, wrap); apply_fx(); break;
    case I_OOMPH:      cfg.oomph = step(cfg.oomph, dir, 0, 12, wrap); apply_fx(); break;
    case I_HEADPHONE:  cfg.headphone = !cfg.headphone; apply_fx(); break;
    case I_FULLSCREEN: plat_set_fullscreen(!plat_fullscreen()); break;
    case I_SKIP_ANIMATION: cfg.skip_animation = !cfg.skip_animation; break;
    case I_QUICK_BALL:     cfg.quick_ball = !cfg.quick_ball; break;
    case I_QUICK_BONUS:    cfg.quick_bonus = !cfg.quick_bonus; break;
    case I_MENU_BOX:       cfg.menu_box = !cfg.menu_box; break;
    default:
        if (id < I_PAD_BUTTON || id > I_PAD_LAST)
            return;
        pad_map[id - I_PAD_BUTTON] = step(pad_map[id - I_PAD_BUTTON], dir, 0, PA_ACTIONS - 1, 1);
    }
    if (id >= I_BALLS && id <= I_SCREEN)
        save_game_options();
    else
        launcher_save_settings();
}

enum { GO_ON, START, QUIT };

static int start_table(int t, int *prog, int *table)
{
    cfg.table = t;
    launcher_save_settings();
    *prog = t < 4 ? 1 : 2;
    *table = t & 3;
    return START;
}

/* Enter on the item under the cursor */
static int activate(int *prog, int *table)
{
    const Item *it = &pages[page].items[pages[page].cursor];
    int id = it->id;

    if (id >= I_TABLE && id <= I_TABLE_LAST)
        return start_table(id - I_TABLE, prog, table);
    if (id >= I_KEY && id <= I_KEY_LAST) {
        capturing = id - I_KEY;
        return GO_ON;
    }
    switch (id) {
    case I_PLAY:
        go(P_TABLES);
        cursor_to(I_TABLE + cfg.table);
        break;
    case I_MENU:
        launcher_save_settings();
        *prog = 0;
        return START;
    case I_OPTIONS: go(P_OPTIONS); break;
    case I_SOUND:   go(P_SOUND); break;
    case I_QOL:     go(P_QOL); break;
    case I_PAD:     go(P_PAD); break;
    case I_PAD_DEFAULTS:
        pad_default_map();
        launcher_save_settings();
        show_note("The controller's buttons are as pdd starts with them again.");
        break;
    case I_QUIT:    return QUIT;
    case I_BACK:    go(pages[page].parent); break;
    case I_DEFAULTS:
        memcpy(opt, opt_default, OPT_SIZE);
        save_game_options();
        show_note("The game options are the game's defaults again.");
        break;
    default:
        if (it->kind == K_CHOICE)
            change(id, 1, 1);
    }
    return GO_ON;
}

/* a key pressed for the game key being set */
static void capture(int code)
{
    const char *why;
    int k;

    if (code == 0x01) {                 /* Esc: as it was */
        capturing = -1;
        return;
    }
    why = key_refused(code);
    if (!why && typed_sound_key)
        why = "it types a sound key (+ - * /) as well";
    for (k = 0; !why && k < 4; k++)
        if (k != capturing && key_code(k) == code)
            why = "it is set for another of the four already";
    if (why) {
        char msg[100];
        snprintf(msg, sizeof msg, "Not %s: %s.", key_name(code), why);
        show_note(msg);
        return;
    }
    set_key(capturing, code);
    save_game_options();
    capturing = -1;
}

/* a key (make code, E0 keys + 80h) pressed or repeated */
static int key_press(int code, int *prog, int *table)
{
    Page *p = &pages[page];
    const Item *it = &p->items[p->cursor];

    if (capturing >= 0) {
        capture(code);
        return GO_ON;
    }
    if (box[0]) {
        if (code == 0x1C || code == 0x9C || code == 0x01 || code == 0x39)
            box[0] = 0;
        return GO_ON;
    }
    switch (code) {
    case 0x48: case 0xC8: move_cursor(-1); break;
    case 0x50: case 0xD0: move_cursor(1); break;
    case 0x47: case 0xC7: case 0x49: case 0xC9:        /* Home, Page Up: the first */
        p->cursor = -1;
        move_cursor(1);
        break;
    case 0x4F: case 0xCF: case 0x51: case 0xD1:        /* End, Page Down: the last */
        p->cursor = p->count;
        move_cursor(-1);
        break;
    case 0x4B: case 0xCB: change(it->id, -1, 0); break;
    case 0x4D: case 0xCD: change(it->id, 1, 0); break;
    case 0x1C: case 0x9C: case 0x39:
        return activate(prog, table);
    case 0x01:
        if (page == P_MAIN)
            cursor_to(I_QUIT);
        else
            go(p->parent);
        break;
    default:
        if (code >= 0x3B && code <= 0x42 && (page == P_MAIN || page == P_TABLES))
            return start_table(code - 0x3B, prog, table);
    }
    return GO_ON;
}

/* ---- drawing */

#define A_SCREEN  TM_ATTR(TM_WHITE, TM_BLUE)
#define A_WINDOW  TM_ATTR(TM_WHITE, TM_BLUE)
#define A_TITLE   TM_ATTR(TM_YELLOW, TM_BLUE)
#define A_HEADING TM_ATTR(TM_LIGHTCYAN, TM_BLUE)
#define A_LABEL   TM_ATTR(TM_LIGHTGREY, TM_BLUE)
#define A_VALUE   TM_ATTR(TM_YELLOW, TM_BLUE)
#define A_OFF     TM_ATTR(TM_DARKGREY, TM_BLUE)
#define A_CURSOR  TM_ATTR(TM_BLACK, TM_CYAN)
#define A_BAR     TM_ATTR(TM_BLACK, TM_LIGHTGREY)
#define A_BAR_KEY TM_ATTR(TM_RED, TM_LIGHTGREY)
#define A_BOX     TM_ATTR(TM_WHITE, TM_RED)
#define A_NOTE    TM_ATTR(TM_YELLOW, TM_RED)

/* a bar of "KEY text" pairs */
static void help_bar(int y, const char *const *parts)
{
    int x = 1;

    tm_fill(0, y, TM_COLS, 1, ' ', A_BAR);
    for (; parts[0]; parts += 2) {
        x = tm_text(x, y, parts[0], A_BAR_KEY);
        x = tm_text(x + 1, y, parts[1], A_BAR) + 3;
    }
}

/* text centred in w columns from x */
static void centred(int x, int w, int y, const char *s, uint8_t attr)
{
    tm_text(x + (w - (int)strlen(s)) / 2, y, s, attr);
}

static void draw_page(void)
{
    const Page *p = &pages[page];
    int w = p->width, h = p->count + 4, x = (TM_COLS - w) / 2, y = 2 + (19 - h) / 2, i;
    char title[64], text[80], value[64];

    tm_fill(x, y, w, h, ' ', A_WINDOW);
    tm_frame(x, y, w, h, A_WINDOW);
    snprintf(title, sizeof title, " %s ", p->title);
    centred(x, w, y, title, A_TITLE);
    tm_shadow(x, y, w, h);
    for (i = 0; i < p->count; i++) {
        const Item *it = &p->items[i];
        int row = y + 2 + i, cur = i == p->cursor && capturing < 0 && !box[0];
        uint8_t label = cur ? A_CURSOR : selectable(it) ? A_LABEL : A_OFF;

        if (it->kind == K_GAP)
            continue;
        if (it->kind == K_HEADING) {
            tm_text(x + 3, row, it->label, A_HEADING);
            continue;
        }
        if (cur)
            tm_fill(x + 2, row, w - 4, 1, ' ', A_CURSOR);
        if (it->id >= I_TABLE && it->id <= I_TABLE_LAST) {
            snprintf(text, sizeof text, "F%d", it->id - I_TABLE + 1);
            tm_text(x + 4, row, text, cur ? A_CURSOR : A_HEADING);
            tm_text(x + 9, row, table_names[it->id - I_TABLE], cur ? A_CURSOR : TM_ATTR(TM_WHITE, TM_BLUE));
            continue;
        }
        if (it->id >= I_PAD_BUTTON && it->id <= I_PAD_LAST && pad_held(it->id - I_PAD_BUTTON))
            tm_put(x + 3, row, TM_RIGHT_TRIANGLE, cur ? A_CURSOR : A_VALUE);
        tm_text(x + 4, row, it->label, label);
        if (it->kind == K_CHOICE) {
            int vx = x + 24 > x + 8 + (int)strlen(it->label) ? x + 24 : x + 8 + (int)strlen(it->label);
            value_text(it->id, value, sizeof value);
            if (i == p->cursor && capturing >= 0 && capturing == it->id - I_KEY)
                snprintf(value, sizeof value, "press a key ...");
            if (cur) {
                tm_put(vx - 2, row, TM_LEFT_TRIANGLE, A_CURSOR);
                tm_put(vx + (int)strlen(value) + 1, row, TM_RIGHT_TRIANGLE, A_CURSOR);
            }
            tm_text(vx, row, value, cur ? A_CURSOR : A_VALUE);
        }
    }
}

/* the box's lines centred on the screen */
static void draw_box(const char *text, const char *footer)
{
    char lines[8][80];
    int n = 0, w = (int)strlen(footer), x, y, i;
    const char *s = text;

    while (*s && n < 8) {
        size_t k = strcspn(s, "\n");
        snprintf(lines[n], sizeof lines[n], "%.*s", (int)(k < 74 ? k : 74), s);
        if ((int)strlen(lines[n]) > w)
            w = (int)strlen(lines[n]);
        n++;
        s += k;
        if (*s)
            s++;
    }
    w += 6;
    x = (TM_COLS - w) / 2;
    y = (TM_ROWS - (n + 5)) / 2;
    tm_fill(x, y, w, n + 5, ' ', A_BOX);
    tm_frame(x, y, w, n + 5, A_BOX);
    tm_shadow(x, y, w, n + 5);
    for (i = 0; i < n; i++)
        tm_text(x + 3, y + 2 + i, lines[i], A_BOX);
    centred(x, w, y + n + 3, footer, TM_ATTR(TM_YELLOW, TM_RED));
}

/* the blue screen with its title bar */
static void backdrop(void)
{
    tm_clear(' ', A_SCREEN);
    tm_fill(0, 0, TM_COLS, 1, ' ', A_BAR);
    tm_text(1, 0, "Pinball Dreams Setup", A_BAR);
    tm_text(TM_COLS - 10, 0, "pddnative", A_BAR);
}

static void draw(const char *game)
{
    static const char *const help_page[] = {
        "\x18\x19", "Select", "\x1B\x1A", "Change", "Enter", "Choose", "Esc", "Back", NULL
    };
    static const char *const help_main[] = {
        "\x18\x19", "Select", "Enter", "Choose", "F1-F8", "Play that table", "Esc", "To Quit", NULL
    };
    static const char *const help_play[] = {
        "\x18\x19", "Select", "Enter", "Play", "F1-F8", "Play that table", "Esc", "Back", NULL
    };
    static const char *const help_key[] = { "Any key", "Set it", "Esc", "Keep the key", NULL };
    static const char *const help_box[] = { "Enter", "Go on", NULL };
    const Page *p = &pages[page];
    const Item *it = &p->items[p->cursor];
    char line[TM_COLS + 1];
    int n;

    backdrop();
    draw_page();

    if (note[0] && plat_micros() < note_until) {
        tm_fill(0, 22, TM_COLS, 1, ' ', A_NOTE);
        centred(0, TM_COLS, 22, note, A_NOTE);
    } else if (it->help || (it->id >= I_TABLE && it->id <= I_TABLE_LAST)) {
        if (it->help)
            snprintf(line, sizeof line, "%s", it->help);
        else
            snprintf(line, sizeof line, "%s, from %s. Esc in the table comes back here.",
                     table_names[it->id - I_TABLE],
                     it->id - I_TABLE < 4 ? "Pinball Dreams" : "Pinball Dreams 2");
        centred(0, TM_COLS, 22, line, TM_ATTR(TM_LIGHTCYAN, TM_BLUE));
    }
    /* the game's folder, the start of a long path left out */
    n = (int)strlen(game);
    snprintf(line, sizeof line, "Your Pinball Dreams: %s%s", n > 52 ? "..." : "", game + (n > 52 ? n - 52 : 0));
    centred(0, TM_COLS, 23, line, A_LABEL);

    if (box[0]) {
        draw_box(box, "Enter: go on");
        help_bar(24, help_box);
    } else if (capturing >= 0) {
        static const char *const what[4] = { "the left flipper", "the right flipper", "nudging", "the plunger" };
        snprintf(line, sizeof line, "Press the key for %s.\nEsc keeps %s.", what[capturing],
                 key_name(key_code(capturing)));
        draw_box(line, "");
        help_bar(24, help_key);
    } else {
        help_bar(24, page == P_MAIN ? help_main : page == P_TABLES ? help_play : help_page);
    }
}

/* ---- the loop */

#define REPEAT_DELAY 400000     /* microseconds before a held key repeats */
#define REPEAT_EVERY 60000

void launcher_show_options(void)
{
    go(P_OPTIONS);
}

int launcher_run(const char *game, int *prog, int *table, const char *note_text)
{
    static uint8_t pixels[TM_WIDTH * TM_HEIGHT];
    static uint32_t palette[256];
    int e0 = 0, held = 0, b, r = GO_ON;
    uint64_t repeat_at = 0;

    load_game_options();
    pad_set_context(PAD_TEXT);
    if (note_text && *note_text)
        snprintf(box, sizeof box, "%s", note_text);
    while (r == GO_ON) {
        pad_set_context(capturing >= 0 ? PAD_OFF : PAD_TEXT);  /* a game key: the keyboard's */
        if (!plat_pump())
            return 0;
        /* the sound keys act in the game only; here they tell a key that
         * types one of them (the key and its character come in the same
         * pump) */
        typed_sound_key = 0;
        while (plat_read_control() >= 0)
            typed_sound_key = 1;
        while (r == GO_ON && (b = plat_read_scancode()) >= 0) {
            int code;
            if (b == 0xE0) {
                e0 = 1;
                continue;
            }
            code = (b & 0x7F) | (e0 ? 0x80 : 0);
            e0 = 0;
            if (code == 0xAA || code == 0xB6)
                continue;                       /* the shifts' E0 fake codes */
            if (b & 0x80) {
                if (code == held)
                    held = 0;
                continue;
            }
            held = code;
            repeat_at = plat_micros() + REPEAT_DELAY;
            r = key_press(code, prog, table);
        }
        if (r == GO_ON && held && capturing < 0 && plat_micros() >= repeat_at) {
            static const int repeats[] = { 0x48, 0xC8, 0x50, 0xD0, 0x4B, 0xCB, 0x4D, 0xCD };
            size_t i;
            for (i = 0; i < sizeof repeats / sizeof repeats[0]; i++)
                if (repeats[i] == held)
                    r = key_press(held, prog, table);
            repeat_at += REPEAT_EVERY;
        }
        draw(game);
        tm_render(pixels, palette);
        plat_present(pixels, TM_WIDTH, TM_HEIGHT, palette);
        plat_sleep_ms(15);
    }
    return r == START;
}

/* ---- the game's files from the GOG release (gog.c) */

static uint8_t shown_pixels[TM_WIDTH * TM_HEIGHT];
static uint32_t shown_palette[256];

static void present(void)
{
    tm_render(shown_pixels, shown_palette);
    plat_present(shown_pixels, TM_WIDTH, TM_HEIGHT, shown_palette);
}

/* `s` in at most `w` columns: the start left out */
static const char *tail(const char *s, int w, char *buf, size_t n)
{
    int len = (int)strlen(s);
    if (len <= w)
        return s;
    snprintf(buf, n, "...%s", s + len - (w - 3));
    return buf;
}

/* a window of text lines (NULL: an empty line) and below them the
 * choices, `cursor` highlighted (-1: none) */
static void draw_import(const char *const *lines, int nlines, const char *const *choices,
                        int nchoices, int cursor)
{
    int w = 70, h = nlines + nchoices + 5, x = (TM_COLS - w) / 2, y = 2 + (20 - h) / 2, i;

    tm_fill(x, y, w, h, ' ', A_WINDOW);
    tm_frame(x, y, w, h, A_WINDOW);
    centred(x, w, y, " The game's files ", A_TITLE);
    tm_shadow(x, y, w, h);
    for (i = 0; i < nlines; i++)
        if (lines[i])
            tm_text(x + 3, y + 2 + i, lines[i], lines[i][0] == ' ' ? A_VALUE : A_LABEL);
    for (i = 0; i < nchoices; i++) {
        int row = y + 3 + nlines + i;
        if (i == cursor)
            tm_fill(x + 2, row, w - 4, 1, ' ', A_CURSOR);
        tm_text(x + 4, row, choices[i], i == cursor ? A_CURSOR : A_LABEL);
    }
}

typedef struct {
    uint64_t drawn;
    int closed;
} Copying;

static int copy_progress(void *ctx, const char *file, long done, long total)
{
    static const char *const help[] = { "", "Copying ...", NULL };
    Copying *c = (Copying *)ctx;
    char bar[64], text[80];
    int width = 50, full = total > 0 ? (int)((double)done / (double)total * width) : 0, i;
    const char *lines[4];

    if (plat_micros() - c->drawn < 30000 && done < total)
        return 0;
    c->drawn = plat_micros();
    for (i = 0; i < width; i++)
        bar[i] = (char)(i < full ? TM_BLOCK : TM_SHADE_LIGHT);
    bar[width] = 0;
    snprintf(text, sizeof text, " %3d %%   %s", total > 0 ? (int)(100.0 * (double)done / (double)total) : 0, file);
    lines[0] = "Copying the game's files from your GOG release:";
    lines[1] = NULL;
    lines[2] = bar;
    lines[3] = text;
    backdrop();
    draw_import(lines, 4, NULL, 0, -1);
    help_bar(24, help);
    present();
    if (!plat_pump())
        c->closed = 1;
    return c->closed;
}

/* the lines of text until Enter (or Esc: the last choice); the choice */
static int ask(const char *const *lines, int nlines, const char *const *choices, int nchoices)
{
    static const char *const help[] = { "\x18\x19", "Select", "Enter", "Choose", NULL };
    int cursor = 0, e0 = 0, b;

    for (;;) {
        if (!plat_pump())
            return nchoices - 1;
        while (plat_read_control() >= 0)
            ;
        while ((b = plat_read_scancode()) >= 0) {
            int code;
            if (b == 0xE0) {
                e0 = 1;
                continue;
            }
            code = (b & 0x7F) | (e0 ? 0x80 : 0);
            e0 = 0;
            if (b & 0x80)
                continue;
            if ((code == 0x48 || code == 0xC8) && cursor > 0)
                cursor--;
            else if ((code == 0x50 || code == 0xD0) && cursor < nchoices - 1)
                cursor++;
            else if (code == 0x1C || code == 0x9C || code == 0x39)
                return cursor;
            else if (code == 0x01)
                return nchoices - 1;
        }
        backdrop();
        draw_import(lines, nlines, choices, nchoices, cursor);
        help_bar(24, help);
        present();
        plat_sleep_ms(15);
    }
}

int launcher_import(const char *image, const char *dir)
{
    static const char *const copy_or_quit[] = { "Copy the files", "Quit" };
    static const char *const quit[] = { "Quit" };
    char found[SYS_PATH], err[200], from[80], to[80], why[80];
    const char *lines[10];
    Copying c;

    if (image)
        snprintf(found, sizeof found, "%s", image);
    else if (!gog_find(found, sizeof found)) {
        lines[0] = "pdd runs Pinball Dreams with the files of your own copy of the";
        lines[1] = "game, the GOG release of Pinball Dreams Deluxe. Neither its files";
        lines[2] = "nor an installed GOG release were found.";
        lines[3] = NULL;
        lines[4] = "Install the game from GOG and start pdd again, or start it with";
        lines[5] = "-gog FILE (the release's game.gog) or -game FOLDER (the files of";
        lines[6] = "its CD).";
        ask(lines, 7, quit, 1);
        return 0;
    }
    snprintf(from, sizeof from, "  %s", tail(found, 62, err, sizeof err));
    snprintf(to, sizeof to, "  %s", tail(dir, 62, why, sizeof why));
    lines[0] = "pdd runs Pinball Dreams with the files of your own copy of the";
    lines[1] = "game. They are not here yet; your GOG release is:";
    lines[2] = from;
    lines[3] = NULL;
    lines[4] = "Its files can be copied from there into:";
    lines[5] = to;
    if (ask(lines, 6, copy_or_quit, 2) != 0)
        return 0;
    memset(&c, 0, sizeof c);
    if (gog_unpack(found, dir, copy_progress, &c, err, sizeof err) == 0)
        return 1;
    if (c.closed)
        return 0;
    snprintf(why, sizeof why, "  %s", err);
    lines[0] = "The files could not be copied:";
    lines[1] = why;
    lines[2] = NULL;
    lines[3] = "From:";
    lines[4] = from;
    ask(lines, 5, quit, 1);
    return 0;
}
