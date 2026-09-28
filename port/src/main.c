/* main.c - Pinball Dreams: a native compatibility implementation that
 * needs an installed copy of the game.
 *
 *     pdd [-game DIR] [-prog 1|2] [-table 0-3] [-record FILE]
 *         [-fx BASS,TREBLE,OOMPH,HEADPHONE]
 *
 * DIR is the unpacked CD (the folders DELUXE, DREAMS1, DREAMS2 ...):
 * -game, else $PDD_GAME, else `game` beside the program, else `game` in
 * the current directory.  Without -prog and -table the window shows the
 * setup screen (launcher.c), which starts the tables and comes back after
 * each.  -prog 1 runs PD.EXE's tables (Ignition, Steel Wheel, Beat Box,
 * Nightmare), 2 PD2.EXE's; -table picks one of the four, as the command
 * line digit did: with either, that table runs once, without the setup
 * screen (as the headless build always does).  -record writes each byte of
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
#include "frame.h"
#include "launcher.h"
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

static int find_game(const char *given, char *out, size_t n)
{
    char exe[SYS_PATH];
    const char *env = getenv("PDD_GAME");

    if (given) {
        snprintf(out, n, "%s", given);
        return sys_is_dir(out);
    }
    if (env && *env) {
        snprintf(out, n, "%s", env);
        return sys_is_dir(out);
    }
    sys_exe_dir(exe, sizeof exe);
    sys_join(out, n, exe, "game");
    if (sys_is_dir(out))
        return 1;
    snprintf(out, n, "game");
    return sys_is_dir(out);
}

/* The setup screen, the table chosen there, back to the screen: until it
 * is left or the window closed.  What went wrong in a table is shown on
 * the screen. */
static int with_launcher(const char *game)
{
    char err[512] = "";
    int prog, table;

    while (launcher_run(game, &prog, &table, err)) {
        err[0] = 0;
        if (mem_load(prog, game, err, sizeof err) != 0)
            continue;
        pd_run(table, game, err, sizeof err);
        snd_stop();                     /* the table's music, if it still plays */
        launcher_save_settings();       /* what the sound keys and Alt+Enter set */
        if (!plat_pump())
            break;                      /* the window was closed */
    }
    launcher_save_settings();
    plat_shutdown();
    return 0;
}

int main(int argc, char **argv)
{
    const char *given = NULL;
    char game[SYS_PATH], err[512];
    int prog = 1, table = 0, direct = 0, fx_given = 0, i, r;
    int fx[4] = { 0, 0, 0, 0 };

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-game") && i + 1 < argc)
            given = argv[++i];
        else if (!strcmp(argv[i], "-prog") && i + 1 < argc) {
            prog = atoi(argv[++i]) == 2 ? 2 : 1;
            direct = 1;
        } else if (!strcmp(argv[i], "-table") && i + 1 < argc) {
            table = atoi(argv[++i]) & 3;
            direct = 1;
        } else if (!strcmp(argv[i], "-record") && i + 1 < argc)
            frame_record(argv[++i]);
        else if (!strcmp(argv[i], "-fx") && i + 1 < argc) {
            sscanf(argv[++i], "%d,%d,%d,%d", &fx[0], &fx[1], &fx[2], &fx[3]);
            fx_given = 1;
        }
    }
    if (!plat_init("Pinball Dreams"))
        return 1;
    if (!find_game(given, game, sizeof game)) {
        plat_message("The game's files were not found: give their folder with -game "
                     "or in PDD_GAME.");
        plat_shutdown();
        return 1;
    }
    if (plat_has_window())
        launcher_load_settings();
    if (fx_given)
        launcher_set_fx(fx[0], fx[1], fx[2], fx[3]);
    if (plat_has_window() && !direct)
        return with_launcher(game);

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
