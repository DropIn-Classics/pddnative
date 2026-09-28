/* main.c - Pinball Dreams: a native compatibility implementation that
 * needs an installed copy of the game.
 *
 *     pdd [-game DIR | -gog FILE] [-prog 1|2] [-table 0-3] [-menu] [-qol]
 *         [-record FILE] [-fx BASS,TREBLE,OOMPH,HEADPHONE]
 *
 * DIR is the unpacked CD (the folders DELUXE, DREAMS1, DREAMS2 ...):
 * -game, else $PDD_GAME, else the first folder `game` with a DREAMS1 in
 * it beside the program, in the current directory or in the data folder
 * (sys_data_dir).  When there is none, the window offers to unpack the
 * installed GOG release's image into the data folder's `game` (gog.c;
 * -gog names the image, game.gog, instead of looking for it).  Without -prog and -table the window shows the
 * setup screen (launcher.c), which starts the tables and comes back after
 * each.  -prog 1 runs PD.EXE's tables (Ignition, Steel Wheel, Beat Box,
 * Nightmare), 2 PD2.EXE's; -table picks one of the four, as the command
 * line digit did: with either, that table runs once, without the setup
 * screen (as the headless build always does), and as the original runs
 * it unless -qol gives it the quality of life fixes (pd.h: pd_qol).  -menu
 * starts the game's menu (menu.h) instead, without the setup screen and
 * the animations, its tables as with -prog/-table; Esc there ends the
 * program.  From the setup screen (its "Play from the menu" too) its
 * settings decide: the engine's fixes each, and
 * unless the animation is skipped, the table's animation before it as the
 * menu plays it (fli.h).  -record writes each byte of
 * the keyboard as the program gets it, with the number of the picture
 * (PICTURE:HEX, a line each): a game played in the window replays in the
 * headless build (PD_KEYS) and, through tools/portcmp.py --record, in the
 * original (the pictures are counted from the program's start: with
 * -prog/-table).  -fx shapes the sound (audiofx.h): bass and treble -12
 * to 12 dB, oomph 0 to 12 dB, headphone 0 or 1; kept as if set in the
 * setup screen.
 *
 * For comparing with the original in tools/run: when the program stops,
 * PD_RAM names a file for memory 0-A0000h and PD_VRAM one for the 256 KB
 * of video memory (byte 4 * offset + plane), as the runner's -ram and
 * -vram write them.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fli.h"
#include "frame.h"
#include "launcher.h"
#include "menu.h"
#include "pd.h"
#include "platform.h"
#include "sound.h"
#include "sys.h"
#include "vga.h"

static void dump(void)
{
    const char *ram = getenv("PD_RAM"), *vram = getenv("PD_VRAM");
    FILE *f;
    long a;

    if (ram && (f = fopen(ram, "wb")) != NULL) {
        fwrite(mem, 1, 0xA0000, f);
        fclose(f);
    }
    if (vram && (f = fopen(vram, "wb")) != NULL) {
        for (a = 0; a < 4 * 0x10000; a++)
            fputc(vga_plane_byte((int)(a & 3), (uint16_t)(a >> 2)), f);
        fclose(f);
    }
}

/* a folder with the CD's files: one with DREAMS1 in it */
static int has_game(const char *dir)
{
    char sub[SYS_PATH];
    return sys_is_dir(dir) && sys_find(dir, "DREAMS1", sub, sizeof sub) && sys_is_dir(sub);
}

static int find_game(const char *given, char *out, size_t n)
{
    char dir[SYS_PATH];
    const char *env = getenv("PDD_GAME");

    if (given) {
        snprintf(out, n, "%s", given);
        return sys_is_dir(out);
    }
    if (env && *env) {
        snprintf(out, n, "%s", env);
        return sys_is_dir(out);
    }
    sys_exe_dir(dir, sizeof dir);
    sys_join(out, n, dir, "game");
    if (has_game(out))
        return 1;
    snprintf(out, n, "game");
    if (has_game(out))
        return 1;
    sys_data_dir(dir, sizeof dir);
    sys_join(out, n, dir, "game");
    return has_game(out);
}

static const char *game_dir;
static int with_setup;                  /* started from the setup screen */

/* a table: its animation first (from the setup screen, unless skipped),
 * then the table; 1 when it ended, 0 when the window was closed, -1 with
 * a message in err */
static int play_table(int prog, int table, char *err, size_t n)
{
    int r;

    if (with_setup) {
        pd_qol = launcher_qol();
        if (!launcher_skip_animation() && !fli_before_table(game_dir, 4 * (prog - 1) + table))
            return 0;                   /* the window was closed */
    }
    if (mem_load(prog, game_dir, err, n) != 0)
        return -1;
    r = pd_run(table, game_dir, err, n);
    snd_stop();                         /* the table's music, if it still plays */
    if (plat_has_window())
        launcher_save_settings();       /* what the sound keys and Alt+Enter set */
    if (r != 0)
        return -1;
    return plat_pump();
}

/* the intro's animations before the menu, unless skipped */
static int menu_intro(void)
{
    return launcher_skip_animation() || fli_intro(game_dir);
}

/* The setup screen, the table or the menu chosen there, back to the
 * screen: until it is left or the window closed.  What went wrong in a
 * table is shown on the screen. */
static int with_launcher(void)
{
    MenuHooks hooks = { menu_intro, play_table };
    char err[512] = "";
    int prog, table, r;

    with_setup = 1;
    while (launcher_run(game_dir, &prog, &table, err)) {
        err[0] = 0;
        r = prog == 0 ? menu_run(game_dir, &hooks, err, sizeof err)
                      : play_table(prog, table, err, sizeof err);
        if (r == 0 && !plat_pump())
            break;                      /* the window was closed */
    }
    launcher_save_settings();
    plat_shutdown();
    return 0;
}

int main(int argc, char **argv)
{
    const char *given = NULL, *image = NULL;
    char game[SYS_PATH], err[512];
    int prog = 1, table = 0, direct = 0, menu = 0, fx_given = 0, i, r;
    int fx[4] = { 0, 0, 0, 0 };

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-game") && i + 1 < argc)
            given = argv[++i];
        else if (!strcmp(argv[i], "-gog") && i + 1 < argc)
            image = argv[++i];
        else if (!strcmp(argv[i], "-prog") && i + 1 < argc) {
            prog = atoi(argv[++i]) == 2 ? 2 : 1;
            direct = 1;
        } else if (!strcmp(argv[i], "-table") && i + 1 < argc) {
            table = atoi(argv[++i]) & 3;
            direct = 1;
        } else if (!strcmp(argv[i], "-menu"))
            menu = 1;
        else if (!strcmp(argv[i], "-qol"))
            pd_qol = QOL_NEXT_BALL | QOL_BONUS;
        else if (!strcmp(argv[i], "-record") && i + 1 < argc)
            frame_record(argv[++i]);
        else if (!strcmp(argv[i], "-fx") && i + 1 < argc) {
            sscanf(argv[++i], "%d,%d,%d,%d", &fx[0], &fx[1], &fx[2], &fx[3]);
            fx_given = 1;
        }
    }
    if (!plat_init("Pinball Dreams"))
        return 1;
    if (plat_has_window())
        launcher_load_settings();
    if (!find_game(given, game, sizeof game)) {
        /* found by neither -game nor PDD_GAME: the GOG release's, copied */
        if (!given && !getenv("PDD_GAME") && plat_has_window()) {
            char data[SYS_PATH];
            sys_data_dir(data, sizeof data);
            sys_join(game, sizeof game, data, "game");
            if (!launcher_import(image, game)) {
                launcher_save_settings();
                plat_shutdown();
                return 0;
            }
        } else {
            plat_message("The game's files were not found: give their folder with -game "
                         "or in PDD_GAME.");
            plat_shutdown();
            return 1;
        }
    }
    if (fx_given)
        launcher_set_fx(fx[0], fx[1], fx[2], fx[3]);
    game_dir = game;
    if (plat_has_window() && !direct && !menu)
        return with_launcher();

    if (menu) {
        MenuHooks hooks = { NULL, play_table };
        r = menu_run(game, &hooks, err, sizeof err);
        dump();
        if (r != 0)
            plat_message(err);
        if (plat_has_window())
            launcher_save_settings();
        plat_shutdown();
        return r != 0;
    }
    if (mem_load(prog, game, err, sizeof err) != 0) {
        plat_message(err);
        plat_shutdown();
        return 1;
    }
    r = pd_run(table, game, err, sizeof err);
    dump();
    if (r != 0) {
        frame_wait();                   /* what it had drawn stays on the screen */
        plat_message(err);
        plat_shutdown();
        return 1;
    }
    if (plat_has_window())
        launcher_save_settings();
    plat_shutdown();
    return 0;
}
