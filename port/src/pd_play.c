/* pd_play.c - a ball in play: state 3 (the ball in the plunger lane,
 * PD.ASM st_ball_start to show_player_up), the nudge, the plunger and the
 * scroll that follows the ball.  See pd.h. */
#include "pd.h"

/* a key of the options (a word: key_map's byte, the bit) down */
static int option_key(uint16_t key)
{
    uint16_t k = rw(key);
    return (rb((uint16_t)(V(key_map) + (k & 0xFF))) & (k >> 8)) != 0;
}

/* the display while the ball waits in the plunger lane: the players and
 * the ball number with the current player's digit blinking; after a lock
 * "shoot the ball, player n" blinking; for an extra ball the relaunch text */
static void show_player_up(void)
{
    uint16_t di;
    uint8_t n;
    int i;

    if (rb(V(ball_locked)) || rb(V(extra_ball))) {
        di = V(txt_blank);
        if (rb(V(player_blink))) {
            di = rb(V(ball_locked)) ? V(txt_shoot_player) : rw(V(td_txt_relaunch));
            wb((uint16_t)(di + 0x13), (uint8_t)(rb(V(player)) + '1'));
        }
        wb(V(player_blink), (uint8_t)~rb(V(player_blink)));
        show_text(di);
        return;
    }
    di = V(txt_players_ball);
    wb((uint16_t)(di + 0x0F), (uint8_t)(rb(V(ball_num)) + '1'));
    n = rb(V(last_player));
    for (i = 0;; i++) {
        wb((uint16_t)(di + i), (uint8_t)('1' + i));
        if ((int8_t)--n < 0)
            break;
    }
    wb(V(player_blink_timer), (uint8_t)(rb(V(player_blink_timer)) - 1));
    if (rb(V(player_blink_timer)) == 0) {
        wb(V(player_blink), (uint8_t)~rb(V(player_blink)));
        wb(V(player_blink_timer), 0x14);
    }
    if (rb(V(player_blink)))
        wb((uint16_t)(di + rb(V(player))), ' ');
    show_text(di);
}

/* scroll_speed toward keeping the ball's sprite near line 37h of the
 * screen: nothing within 12h lines of it, a quarter of the distance
 * beyond (in lines * 2000h) */
void scroll_follow(void)
{
    uint16_t line = (uint16_t)((rd(V(scroll)) << 3) >> 16) & 0x1FF;
    uint16_t ax = (uint16_t)(rw(V(sprite_y)) - line - 0x37), dx = 0;
    uint32_t v;

    if (ax != 0) {
        if (!(ax & 0x8000)) {
            if (ax >= 0x12)
                dx = (uint16_t)((int16_t)(ax - 0x12) >> 2);
        } else if ((uint32_t)ax + 0x12 <= 0xFFFF) {
            dx = (uint16_t)((int16_t)(ax + 0x12) >> 2);
        }
    }
    v = (uint32_t)((int32_t)((uint32_t)dx << 16) >> 3);
    ww(V(scroll_speed), (uint16_t)v);
    ww(V(scroll_speed_hi), (uint16_t)(v >> 16));
}

/* the nudge key shakes the table: nudge_push 320h for 4 frames, FF06h for
 * 2 (frame_count counts them); more than three nudges within 46h frames:
 * tilt (state 9) */
void nudge(void)
{
    uint8_t count;

    wb(V(nudge_timer), (uint8_t)(rb(V(nudge_timer)) - 1));
    if ((int8_t)rb(V(nudge_timer)) < 0) {
        wb(V(nudge_timer), 0x46);
        count = rb(V(nudge_count));
        wb(V(nudge_count), 0);
        if ((int8_t)count > 3) {
            ww(V(game_state), 9);
            return;
        }
    }
    if (rb(V(nudge_phase)) == 0) {
        if (!option_key(V(key_nudge))) {
            wb(V(nudge_key_down), 0);
            return;
        }
        if (rb(V(nudge_key_down)))
            return;
        ww(V(frame_count), 0);
        wb(V(nudge_key_down), 0xFF);
        wb(V(nudge_phase), 1);
        ww(V(nudge_push), 0x320);
        wb(V(nudge_count), (uint8_t)(rb(V(nudge_count)) + 1));
        return;
    }
    if (rb(V(nudge_phase)) == 1) {
        if ((int16_t)rw(V(frame_count)) < 4)
            return;
        wb(V(nudge_phase), 2);
        ww(V(frame_count), 0);
        ww(V(nudge_push), 0xFF06);
    }
    if (rb(V(nudge_phase)) == 2 && (int16_t)rw(V(frame_count)) >= 2) {
        wb(V(nudge_phase), 0);
        ww(V(frame_count), 0);
        ww(V(nudge_push), 0);
    }
}

/* a 24-bit coordinate of an object record (a word, a byte) * 8 as the
 * sprite's pixel */
static uint16_t object_pixel(uint16_t at)
{
    return (uint16_t)(rd(at) >> 13);
}

/* the plunger key pulls the plunger down (up to 20h frames); released, it
 * shoots: the ball's speed up by the pull when the ball lies on it; the
 * plunger's sprite */
void plunger(void)
{
    uint16_t si = V(plunger_obj), di = V(ball_obj), ax, dx;
    uint32_t sum;

    if (option_key(V(key_plunger))) {
        if ((int16_t)rw(V(plunger_pull)) < 0x20) {
            sum = (uint32_t)rw((uint16_t)(si + 6)) + 0x1000;
            ww((uint16_t)(si + 6), (uint16_t)sum);
            wb((uint16_t)(si + 8), (uint8_t)(rb((uint16_t)(si + 8)) + (sum >> 16)));
            ww(V(plunger_pull), (uint16_t)(rw(V(plunger_pull)) + 1));
        }
    } else {
        ax = rw(V(plunger_pull));
        ww(V(plunger_pull), 0);
        if (ax == 0)
            return;
        wb(V(sound_request), 7);
        ax = (uint16_t)(ax << 4);
        {
            uint16_t w = rw((uint16_t)(si + 7));
            ww((uint16_t)(si + 7), (uint16_t)(w - ax));
            wb((uint16_t)(si + 9), (uint8_t)(rb((uint16_t)(si + 9)) - (w < ax)));
        }
        dx = (uint16_t)(rw(V(ball_start_x_hi)) - 1);
        if ((int16_t)rw((uint16_t)(di + 4)) >= (int16_t)dx &&
            (int16_t)rw((uint16_t)(di + 8)) >= (int16_t)rw(V(ball_start_y_hi)))
            ww(V(ball_vy), (uint16_t)(rw(V(ball_vy)) - (uint16_t)(ax << 3)));
    }
    ww((uint16_t)(V(sprites) + 0x64), object_pixel((uint16_t)(si + 6)));
    ww((uint16_t)(V(sprites) + 0x62), object_pixel((uint16_t)(si + 2)));
    wb((uint16_t)(V(sprites) + 0x60), rb((uint16_t)(V(sprites) + 0x60)) | 3);
}

/* state 3: a new ball for the current player, then ball_start */
void st_ball_start(void)
{
    checkpoint("st_ball_start");
    wb(V(sprites_in_irq), 0xFF);
    reset_ball();
    ball_start();
}

/* "player n" and the jingles, the ball shown; then the ball is played in
 * the plunger lane until it leaves it (sprite x below lane_exit_x): state 5.
 * Esc: back to state 2 */
void ball_start(void)
{
    ww(V(player_rec), rw((uint16_t)(V(player_recs) + 2 * rb(V(player)))));
    wb(V(ball_held), 0);
    wait_frames(0x69);
    wb(V(jingle_request), 3);
    wait_frames(0xB4);
    wb(V(sound_request), 5);
    wait_frames(0x2D);
    wb(V(jingle_request), 1);
    ww(V(wait_count), 0x46);
    do {                                /* (no frame waited in between) */
        show_player_up();
        lights_frame();
        ww(V(wait_count), (uint16_t)(rw(V(wait_count)) - 1));
    } while (rw(V(wait_count)));
    wait_frames(0x0F);
    ww(V(ball_obj), rw(V(ball_obj)) | 1);
    wb(V(sprites), rb(V(sprites)) | 3);
    wb(V(flippers_off), 0xFF);
    ball_frame();
    for (;;) {
        checkpoint("ball_start_loop");
        ww(V(frame_count), (uint16_t)(rw(V(frame_count)) + 1));
        scroll_follow();
        ball_frame();
        plunger();
        lights_frame();
        nudge();
        wb(V(nudge_count), 0);
        show_player_up();
        scroll_step();
        if (key_down(0x01)) {
            wait_keys_up();
            wb(V(tune_request), 0);
            ww(V(game_state), 2);
            ww(V(ball_obj), rw(V(ball_obj)) & 0xFFFE);
            wb(V(sprites), (uint8_t)((rb(V(sprites)) & 0xFE) | 2));
            return;
        }
        if ((uint16_t)(rw(V(sprite_x)) - rw(V(lane_exit_x))) & 0x8000)
            break;
    }
    wb(V(extra_ball), 0);
    wb(V(jingle_request), 0);
    ww(V(game_state), 5);
    wb(V(flippers_off), 0);
}

/* state 4: a ball was locked (lock_ball): a new ball to the plunger, the
 * table scrolls down, then ball_start.  In the 320x200 mode the scroll
 * loop does not end (the original's two tests run one after the other;
 * see the hints at st_ball_locked). */
void st_ball_locked(void)
{
    int32_t s;

    checkpoint("st_ball_locked");
    ww(V(ball_obj), rw(V(ball_obj)) & 0xFFFE);
    wb(V(sprites), (uint8_t)((rb(V(sprites)) & 0xFE) | 2));
    ball_to_start();
    ww(V(message), 0);
    wb(V(message_prio), 0);
    ww(V(running_object), 0);
    ww(V(ball_vx), 0);
    ww(V(ball_vy), 0);
    wb(V(ball_held), 0);
    wb(V(ball_in_hole), 0);
    wb(V(lock_pending), 0);
    ww(V(event_sp), V(event_stack));
    ww(V(event_sp_ball), V(event_stack));
    ww(V(lanes_a), rw(V(td_lanes_a_lower)));
    ww(V(lanes_b), rw(V(td_lanes_b_lower)));
    ww(V(hit_rects), rw(V(td_rects_lower)));
    do {
        checkpoint("ball_locked_loop");
        lights_frame();
        wd(V(scroll_speed), 0x4000);
        scroll_step();
        s = (int32_t)rd(V(scroll));
        if (rb(V(opt_screen)) != 2)
            s -= 0x29E000;
    } while (s < 0x172000);
    ball_start();
}
