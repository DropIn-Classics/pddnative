/* pd_over.c - state 7: game over (PD.ASM st_game_over, check_hiscores to
 * enter_initials): each player's score into the table's high scores with
 * the initials entered, then the players' scores in turn; F1-F8 start a
 * new game, else back to state 2.  See pd.h. */
#include "pd.h"

/* the 6 bytes at a and b from the highest: 1 when a is higher (cmp_score's
 * CX 1; lower or equal is 2) */
static int score_higher(uint16_t a, uint16_t b)
{
    int i;

    for (i = 0; i < 6; i++) {
        uint8_t x = rb((uint16_t)(a + i)), y = rb((uint16_t)(b + i));
        if (x != y)
            return x > y;
    }
    return 0;
}

/* the flipper keys choose a letter, the nudge key takes it (three); Esc
 * leaves them blank.  The letter blinks at display column 10h.. */
static void enter_initials(void)
{
    uint16_t letters = V(initial_letters);
    uint8_t dl;

    show_text(V(txt_highscore));
    wb(V(initial_index), 0);
    wb(V(display_col), 0x10);
    for (;;) {
        if (key_down(0x01)) {
            int i;
            wait_keys_up();
            for (i = 0; i < 3; i++)
                wb((uint16_t)(V(initials) + i), ' ');
            return;
        }
        if (rb((uint16_t)(V(key_map) + (rw(V(key_lflipper)) & 0xFF))) & (rw(V(key_lflipper)) >> 8)) {
            dl = (uint8_t)(rb(V(initial_index)) - 1);
            wb(V(initial_index), (dl & 0x80) ? 0x24 : dl);
        } else if (rb((uint16_t)(V(key_map) + (rw(V(key_rflipper)) & 0xFF))) & (rw(V(key_rflipper)) >> 8)) {
            dl = (uint8_t)(rb(V(initial_index)) + 1);
            wb(V(initial_index), (int8_t)dl > 0x24 ? 0 : dl);
        } else if (rb((uint16_t)(V(key_map) + (rw(V(key_nudge)) & 0xFF))) & (rw(V(key_nudge)) >> 8)) {
            wait_keys_up();
            dl = rb((uint16_t)(letters + rb(V(initial_index))));
            draw_char(dl);
            wb((uint16_t)(V(initials) + rb(V(display_col)) - 0x10), dl);
            if ((int8_t)rb(V(display_col)) >= 0x12)
                return;
            wb(V(display_col), (uint8_t)(rb(V(display_col)) + 1));
            wb(V(initial_index), 0);
        }
        dl = rb((uint16_t)(letters + rb(V(initial_index))));
        wait_frames(3);
        draw_char(dl);
        wait_frames(3);
        draw_char(' ');
    }
}

/* score_msb into the table's four high scores where it is higher, the
 * lower ones moved down, the initials entered (see the hints for the
 * third place) */
static void insert_hiscore(void)
{
    uint16_t table = (uint16_t)(V(hiscores) + rb(V(table_num)) * 0x24), si = (uint16_t)(table + 3);
    uint16_t bx, n, src, dst;
    int i;

    if (score_higher(V(score_msb), si)) {
        bx = 0;
    } else if (score_higher(V(score_msb), si = (uint16_t)(si + 9))) {
        bx = 1;
    } else if (score_higher(V(score_msb), si = (uint16_t)(si + 9))) {
        bx = 3;                         /* sic: SI stays at the third */
    } else if (score_higher(V(score_msb), si = (uint16_t)(si + 9))) {
        bx = 3;
    } else {
        return;
    }
    if (!rb(V(hiscore_jingled))) {
        wb(V(jingle_request), 0x28);
        while (rb(V(jingle_request)) != 0xFF)       /* the driver's tick takes it */
            pump_frame();
        wb(V(hiscore_jingled), (uint8_t)~rb(V(hiscore_jingled)));
    }
    fww(seg_xdata, V(tune_playing), 1);
    for (n = (uint16_t)(3 - bx), src = (uint16_t)(table + 2 * 9 + 8), dst = (uint16_t)(src + 9); n; n--)
        for (i = 0; i < 9; i++)         /* STD, REP MOVSB: an entry down, from its end */
            wb(dst--, rb(src--));
    enter_initials();
    for (i = 0; i < 3; i++)
        wb((uint16_t)(si - 3 + i), rb((uint16_t)(V(initials) + i)));
    for (i = 0; i < 6; i++)
        wb((uint16_t)(si + i), rb((uint16_t)(V(score_msb) + i)));
}

/* SI = a player's score (6 bytes BCD, low first) into score_msb (from the
 * highest), then insert_hiscore */
static void check_hiscore(uint16_t si)
{
    int i;

    for (i = 0; i < 6; i++)
        wb((uint16_t)(V(score_msb) + 5 - i), rb((uint16_t)(si + i)));
    insert_hiscore();
}

/* each player's score through check_hiscore, the player's digit in the
 * entry's text */
static void check_hiscores(void)
{
    uint8_t p;

    wb(V(hiscore_jingled), 0);
    for (p = 0; p <= rb(V(last_player)); p++) {
        wb(V(txt_hiscore_player), (uint8_t)(p + '1'));
        check_hiscore(rw((uint16_t)(V(player_recs) + 2 * p)));
    }
}

/* one frame of the game over show: 1 when it ends the state (quit asked
 * and confirmed: state 0), 2 when F1-F8 start a game */
static int over_frame(void)
{
    read_game_keys();
    draw_flippers();
    lights_frame();
    draw_sprites();
    idle_scroll();
    if (rb(V(quit_request)) && ask_quit()) {
        stop_sound();
        ww(V(game_state), 0);
        return 1;
    }
    return rb(V(last_player)) != 0xFF ? 2 : 0;
}

/* state 7 */
void st_game_over(void)
{
    uint16_t n;
    int r;

    checkpoint("st_game_over");
    check_hiscores();
    format_hiscores();
    if (!rb(V(hiscore_jingled)))
        wb(V(jingle_request), 5);
    wb(V(hiscore_jingled), 0);
    lights_off_all();
    wb(V(ball_num), 0);
    wb(V(attract_mode), 0xFF);
    ww(V(ball_obj), rw(V(ball_obj)) & 0xFFFE);
    wb(V(sprites), (uint8_t)((rb(V(sprites)) & 0xFE) | 2));
    wb(V(over_last), rb(V(last_player)));
    wb(V(last_player), 0xFF);
    for (ww(V(over_rounds), 4); rw(V(over_rounds)); ww(V(over_rounds), (uint16_t)(rw(V(over_rounds)) - 1))) {
        ww(V(over_player), (uint16_t)(rb(V(over_last)) + 1));
        for (; rw(V(over_player)); ww(V(over_player), (uint16_t)(rw(V(over_player)) - 1))) {
            n = rw(V(over_player));
            ww(V(player_rec), rw((uint16_t)(V(player_recs) + 2 * (n - 1))));
            wb((uint16_t)(V(txt_player2) + 7), (uint8_t)(n + '0'));
            show_text(V(txt_player2));
            show_bcd(rw(V(player_rec)), 8, 6);
            for (ww(V(wait_count), 0x46);; ) {
                if ((r = over_frame()) == 1)
                    return;
                if (r == 2)
                    goto new_game;
                ww(V(wait_count), (uint16_t)(rw(V(wait_count)) - 1));
                if (!rw(V(wait_count)))
                    break;
            }
            show_text(rw(V(td_txt_game_over)));
            for (ww(V(wait_count), 0x11);; ) {
                if ((r = over_frame()) == 1)
                    return;
                if (r == 2)
                    goto new_game;
                ww(V(wait_count), (uint16_t)(rw(V(wait_count)) - 1));
                if (!rw(V(wait_count)))
                    break;
            }
        }
    }
    ww(V(game_state), 2);
    return;
new_game:
    ww(V(ball_obj), rw(V(ball_obj)) | 1);
    wb(V(sprites), rb(V(sprites)) | 3);
    show_text(V(txt_blank));
    wb(V(jingle_request), 2);
    wb(V(attract_mode), 0);
    game_scroll_down();
}

/* 6 BCD bytes at si (low first) as 12 digits at di, the highest first */
static void bcd6_to_text(uint16_t si, uint16_t di)
{
    int i;

    for (i = 5; i >= 0; i--, di = (uint16_t)(di + 2)) {
        uint8_t v = rb((uint16_t)(si + i));
        wb(di, (uint8_t)((v >> 4) + '0'));
        wb((uint16_t)(di + 1), (uint8_t)((v & 0x0F) + '0'));
    }
}

/* state 12: player 1's score in a message shown to its end, "game over",
 * then state 11.  Nothing sets state 12 (left over, presumably). */
void st_12(void)
{
    uint16_t msg = V(msg_player1_score);

    bcd6_to_text(V(player_1), (uint16_t)(rw((uint16_t)(msg + 4)) + 0x24));
    ww(V(message), msg);
    do {
        wait_frames(1);
        display_frame();
    } while (rw(V(message)));
    show_text(V(txt_game_over2));
    wait_frames(0xD2);
    stop_sound();
    ww(V(game_state), 0x0B);
}
