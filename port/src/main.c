/* main.c - Pinball Dreams: a native compatibility implementation that
 * needs an installed copy of the game.
 *
 *     pdd [-game DIR] [-prog 1|2] [-table 0-3] [-record FILE]
 *
 * DIR is the unpacked CD (the folders DELUXE, DREAMS1, DREAMS2 ...):
 * -game, else $PDD_GAME, else `game` beside the program, else `game` in
 * the current directory.  -prog 1 runs PD.EXE's tables (Ignition, Steel
 * Wheel, Beat Box, Nightmare), 2 PD2.EXE's; -table picks one of the four,
 * as the command line digit did.  -record writes each byte of the
 * keyboard as the program gets it, with the number of the picture
 * (PICTURE:HEX, a line each): a game played in the window replays in the
 * headless build (PD_KEYS) and, through tools/portcmp.py --record, in the
 * original.
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
#include "pd.h"
#include "platform.h"
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

int main(int argc, char **argv)
{
    const char *given = NULL;
    char game[SYS_PATH], err[512];
    int prog = 1, table = 0, i, r;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-game") && i + 1 < argc)
            given = argv[++i];
        else if (!strcmp(argv[i], "-prog") && i + 1 < argc)
            prog = atoi(argv[++i]) == 2 ? 2 : 1;
        else if (!strcmp(argv[i], "-table") && i + 1 < argc)
            table = atoi(argv[++i]) & 3;
        else if (!strcmp(argv[i], "-record") && i + 1 < argc)
            frame_record(argv[++i]);
    }
    if (!plat_init("Pinball Dreams"))
        return 1;
    if (!find_game(given, game, sizeof game)) {
        plat_message("The game's files were not found: give their folder with -game "
                     "or in PDD_GAME.");
        plat_shutdown();
        return 1;
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
    plat_shutdown();
    return 0;
}
