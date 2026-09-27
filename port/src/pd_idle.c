/* pd_idle.c - state 2: the table waits for a game (PD.ASM st_idle to
 * idle_scroll): the attract show (the idle texts with the table scrolling
 * up and down; on Ignition a tour of the table after them), F1-F8 start a
 * game for 1-8 players, Esc asks to quit.  See pd.h. */
#include "pd.h"

static uint16_t first_message(uint16_t list_entry)
{
    return rw(rw(list_entry));
}

/* F1-F8: the number of players - 1 into last_player; Esc: quit_request */
static void read_game_keys(void)
{
    int i;

    if (key_down(0x01)) {
        wb(V(quit_request), 0xFF);
        return;
    }
    for (i = 0; i < 8; i++)
        if (key_down(0x3B + i)) {
            wb(V(last_player), (uint8_t)i);
            return;
        }
}

/* "really quit <y/n>": 1 for Y.  The original polls the keys in a loop
 * the keyboard interrupt updates; here each round is a picture. */
static int ask_quit(void)
{
    show_text(V(txt_quit));
    for (;;) {
        if (key_down(0x15)) {
            wait_keys_up();
            return 1;
        }
        if (key_down(0x31)) {
            wait_keys_up();
            show_text(V(txt_blank));
            wb(V(quit_request), 0);
            return 0;
        }
        pump_frame();
    }
}

static void scroll_add(uint32_t d)
{
    wd(V(scroll), rd(V(scroll)) + d);
}

/* one scroll step toward the line tour_line / 8 */
static void scroll_toward(void)
{
    uint16_t dx = rw(V(tour_line)), hi = rw(V(scroll_hi));

    if (rb(V(opt_screen)) == 2 && !((uint16_t)(dx - 0x7E) & 0x8000))
        dx = (uint16_t)(dx - 0x7E);
    dx >>= 3;
    if (dx != hi)
        scroll_add(((uint16_t)(dx - hi) & 0x8000) ? (uint32_t)-0x2000 : 0x2000);
    scroll_redraw();
}

/* one scroll step down to the table's end, then up to its top (idle_dir) */
static void idle_scroll(void)
{
    uint16_t hi = rw(V(scroll_hi));

    if (!rb(V(idle_dir))) {
        uint16_t end = rb(V(opt_screen)) == 2 ? 0x17 : 0x29;
        if ((uint16_t)(hi - end) & 0x8000) {
            scroll_add(0x2000);
            scroll_redraw();
            return;
        }
    } else if (hi != 0) {
        scroll_add((uint32_t)-0x2000);
        scroll_redraw();
        return;
    }
    wb(V(idle_dir), (uint8_t)~rb(V(idle_dir)));
    scroll_redraw();
}

/* the attract show, a frame: the idle texts (td_idle_texts) while the
 * table scrolls; on Ignition then the tour (td_idle_scroll: a message, a
 * light group, a line to scroll to per step) */
static void idle_show(void)
{
    uint16_t bx;

    if (rb(V(table_num)) != 0 || !rb(V(tour_on))) {
        idle_scroll();
        lights_frame();
        bx = rw(V(idle_text));
        if (message_start(rw(bx)))
            return;
        if (rw((uint16_t)(bx + 2)) & 0x8000) {
            ww(V(idle_text), rw(V(td_idle_texts)));
            wb(V(tour_on), 0xFF);
            lights_off_all();
            wb(V(tour_wait), 0x10);
        } else {
            ww(V(idle_text), (uint16_t)(rw(V(idle_text)) + 2));
        }
        wb((uint16_t)(first_message(V(idle_text)) + 1), 0);
        return;
    }
    if (rb(V(tour_wait))) {
        wb(V(tour_wait), (uint8_t)(rb(V(tour_wait)) - 1));
        scroll_toward();
        wb(V(attract_mode), 0);
        lights_frame();
        wb(V(attract_mode), 0xFF);
        return;
    }
    bx = rw(V(tour_step));
    ww(V(tour_lamps), rw((uint16_t)(bx + 2)));
    ww(V(tour_line), rw((uint16_t)(bx + 4)));
    if (!message_start(rw(bx))) {
        if (!(rw((uint16_t)(bx + 6)) & 0x8000)) {
            ww(V(tour_step), (uint16_t)(rw(V(tour_step)) + 6));
            lights_off_all();
            wb(V(tour_wait), 0x10);
            wb((uint16_t)(first_message(V(tour_step)) + 1), 0);
            return;
        }
        ww(V(tour_step), rw(V(td_idle_scroll)));
        wb(V(tour_on), 0);
        wb((uint16_t)(first_message(V(tour_step)) + 1), 0);
    }
    scroll_toward();
    idle_lights();
}

/* the ball's object and sprite: not shown */
static void hide_ball(void)
{
    ww(V(ball_obj), rw(V(ball_obj)) & 0xFFFE);
    wb(V(sprites), (uint8_t)((rb(V(sprites)) & 0xFE) | 2));
}

/* state 2: the attract show until F1-F8 (then the table scrolls down and
 * state 3) or Esc and Y (state 0) */
void st_idle(void)
{
    hide_ball();
    lights_off_all();
    wd(V(scroll_speed), 0);
    index_map(V(lower_map));
    wb(V(ball_level), 0x80);
    wb(V(attract_mode), 0xFF);
    wb(V(quit_request), 0);
    wb(V(tour_on), 0);
    wb(V(player_blink_timer), 1);
    wb(V(player), 0);
    wb(V(last_player), 0xFF);
    wb(V(player_bit), 1);
    ww(V(idle_text), rw(V(td_idle_texts)));
    ww(V(tour_step), rw(V(td_idle_scroll)));
    wb((uint16_t)(first_message(V(idle_text)) + 1), 0);
    wb(V(flippers_off), 0xFF);
    hide_ball();
    for (;;) {
        checkpoint("idle_loop");        /* CODE:016A */
        read_game_keys();
        ball_frame();
        draw_flippers();
        draw_sprites();
        idle_show();
        if (rb(V(quit_request)) && ask_quit()) {
            wb(V(tune_request), 0x80);
            stop_sound();
            ww(V(game_state), 0);
            return;
        }
        if (rb(V(last_player)) != 0xFF)
            break;
    }
    wb(V(idle_dir), 0);
    show_text(V(txt_blank));
    wb(V(jingle_request), 0x80);
    wait_frames(5);
    wb(V(jingle_request), 2);
    wait_frames(0x23);
    game_scroll_down();
}

/* the table scrolls down to its end, then the game starts: state 3 (also
 * where a new game from the game over state goes on) */
void game_scroll_down(void)
{
    int32_t end = rb(V(opt_screen)) == 2 ? 0x172000 : 0x29E000;
    int i;

    do {
        lights_frame();
        wd(V(scroll_speed), 0xC000);
        scroll_step();
    } while ((int32_t)rd(V(scroll)) < end);
    wb(V(tour_on), 0);
    wb(V(attract_mode), 0);
    for (i = 0; i < 4; i++)
        lights_frame();
    new_game();
    reset_ball();
    ww(V(game_state), 3);
}
