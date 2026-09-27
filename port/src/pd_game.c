/* pd_game.c - a new game and a new ball: the players' records, the lamps,
 * the objects and sequences reset (PD.ASM new_game, clear_player,
 * reset_ball and the routines it calls, reset_targets to restore_lights,
 * pick_random_light, table_ball_events, table_game_objects).  See pd.h. */
#include "pd.h"

#define PLAYER_SIZE 0x98u

/* the ball at its start: the 32-bit ball_start_x/y (pixels * 2000h) / 8
 * into the 24-bit position */
static void ball_to_start(void)
{
    uint32_t x = (uint32_t)((int32_t)rd(V(ball_start_x)) >> 3);
    uint32_t y = (uint32_t)((int32_t)rd(V(ball_start_y)) >> 3);

    ww(V(ball_x), (uint16_t)x);
    wb((uint16_t)(V(ball_x) + 2), (uint8_t)(x >> 16));
    ww(V(ball_y), (uint16_t)y);
    wb((uint16_t)(V(ball_y) + 2), (uint8_t)(y >> 16));
}

/* the words +0..+16h of a player's record: score, bonus, counts */
static void clear_player(uint16_t p)
{
    int i;
    for (i = 0; i < 0x18; i += 2)
        ww((uint16_t)(p + i), 0);
}

/* the record's lamp bytes (+18h, 80h of them) */
static void clear_player_lights(uint16_t p)
{
    int i;
    for (i = 0; i < 0x80; i++)
        wb((uint16_t)(p + 0x18 + i), 0);
}

/* td_timed's timers stopped */
void clear_object_timers(void)
{
    uint16_t si = rw(V(td_timed)), o;

    while (!((o = rw(si)) & 0x8000)) {
        si = (uint16_t)(si + 2);
        ww((uint16_t)(o + 0x26), 0);
    }
}

/* the event objects of td_objects disarmed (type 1Fh armed for all; with
 * `kept_too` 0 only those whose +1 is 0), their timers stopped; the
 * sequences of td_events (not type 2Ah; not kept unless kept_too) back to
 * step 0 for every player */
static void reset_object_lists(int kept_too)
{
    uint16_t si = rw(V(td_objects)), o;
    int i;

    while (!((o = rw(si)) & 0x8000)) {
        si = (uint16_t)(si + 2);
        if (kept_too || rb((uint16_t)(o + 1)) == 0)
            wb((uint16_t)(o + 0x22), 0);
        if (rb(o) == 0x1F)
            wb((uint16_t)(o + 0x22), 0xFF);
        if (rb((uint16_t)(o + 0x23)) & 6)
            ww((uint16_t)(o + 0x26), 0);
    }
    si = rw(V(td_events));
    while (!((o = rw(si)) & 0x8000)) {
        si = (uint16_t)(si + 2);
        if ((kept_too || rb((uint16_t)(o + 1)) == 0) && rb(o) != 0x2A)
            for (i = 2; i <= 8; i += 2)
                ww((uint16_t)(o + i), 0);
    }
}

/* per table at a new game: table 3's game_object_3 armed for all */
static void table_game_objects(void)
{
    if (rb(V(table_num)) == 3)
        wb((uint16_t)(V(game_object_3) + 0x22), 0xFF);
}

/* per table at a new ball: table 1 runs ball_event_1, table 3 ball_event_3
 * and disarms ball_object_3 for the player */
static void table_ball_events(void)
{
    if (rb(V(table_num)) == 1) {
        run_event(V(ball_event_1));
    } else if (rb(V(table_num)) == 3) {
        uint16_t o = (uint16_t)(V(ball_object_3) + 0x22);
        run_event(V(ball_event_3));
        wb(o, (uint8_t)(rb(o) & ~rb(V(player_bit))));
    }
}

/* the eight players' lamp bytes cleared, all lamps out, every object and
 * sequence reset, the timers stopped */
static void clear_players_lights(void)
{
    int i;

    for (i = 0; i < 8; i++)
        clear_player_lights((uint16_t)(V(player_1) + i * PLAYER_SIZE));
    lights_off_all();
    reset_object_lists(1);
    clear_object_timers();
    table_game_objects();
}

/* the eight players' records and the jackpots cleared; player 1; the
 * balls from the options */
void new_game(void)
{
    int i;

    ww(V(lock2_used), 0);
    ww(V(lock3_used), 0);
    clear_players_lights();
    for (i = 0; i < 8; i += 2)
        ww((uint16_t)(V(txt_players_ball) + i), 0x2020);
    for (i = 0; i < 8; i += 2) {
        uint16_t w = rw((uint16_t)(V(jackpot_base) + i));
        ww((uint16_t)(V(jackpot) + i), w);
        ww((uint16_t)(V(jackpot_shown) + i), w);
    }
    for (i = 0; i < 8; i++)
        clear_player((uint16_t)(V(player_1) + i * PLAYER_SIZE));
    wb(V(player_bit), 1);
    ww(V(player_rec), V(player_1));
    wb(V(player), 0);
    wb(V(unused_9718), (uint8_t)rw(V(frame_count)));
    wb(V(unused_9719), (uint8_t)~rw(V(frame_count)));
    wb(V(balls_left), rb(V(opt_balls)));
    wb(V(balls_per_game), rb(V(opt_balls)));
    wb(V(extra_ball), 0);
    wb(V(ball_num), 0);
    wb(V(quit_request), 0);
    wb(V(ball_locked), 0);
}

/* DI = hit rectangles (0Ah each, +8 the group; FFFFh ends): the player's
 * bit of their targets cleared, following the target chain while the
 * targets are of type 1 or 8 (a lights_frame after each of those) */
static void reset_rect_targets(uint16_t di)
{
    uint16_t g, si;
    uint8_t keep = (uint8_t)~rb(V(player_bit)), type;

    while ((g = rw((uint16_t)(di + 8))) != 0xFFFF) {
        si = rw(g);
        for (;;) {
            wb((uint16_t)(si + 1), rb((uint16_t)(si + 1)) & keep);
            type = rb(si) & 0x7F;
            if (type != 1 && type != 8)
                break;
            lights_frame();
            if (!rw((uint16_t)(si + 4)))
                break;
            si = rw((uint16_t)(si + 4));
        }
        di = (uint16_t)(di + 0x0A);
    }
}

/* each lock empty, its lamp's flags 14h cleared */
void reset_locks(void)
{
    uint16_t si = rw(V(td_locks)), lamp;

    for (; !(rw(si) & 0x8000); si = (uint16_t)(si + 6)) {
        ww((uint16_t)(si + 4), 0);
        lamp = rw((uint16_t)(si + 2));
        if (lamp)
            wb(lamp, rb(lamp) & 0xEB);
    }
}

/* the ball to its start and the per-ball state: messages, locks, the
 * bonus (unless held), multiplier 1, the event stack, the lower level */
static void reset_ball_objects(void)
{
    uint16_t bx = rw(V(player_rec));
    int i;

    ball_to_start();
    ww(V(message), 0);
    wb(V(message_prio), 0);
    ww(V(running_object), 0);
    ww(V(ball_vx), 0);
    ww(V(ball_vy), 0);
    wb(V(ball_held), 0);
    wb(V(ball_in_hole), 0);
    wb(V(lock_pending), 0);
    reset_locks();
    if (!rb(V(bonus_held)))
        for (i = 8; i < 0x10; i += 2)
            ww((uint16_t)(bx + i), 0);
    wb(V(unused_8A9E), 0);
    wb(V(unused_8A9B), 0);
    wb(V(bonus_held), 0);
    ww(V(done_light_group), 0);
    ww(V(light_group_timer), 0);
    ww(V(bonus_count_period), 0);
    wb(V(locks_lit), 0);
    wb(V(ball_locked), 0);
    wb(V(bonus_mult), 1);
    ww(V(event_sp), V(event_stack));
    ww(V(event_sp_ball), V(event_stack));
    ww(V(lanes_a), rw(V(td_lanes_a_lower)));
    ww(V(lanes_b), rw(V(td_lanes_b_lower)));
    ww(V(hit_rects), rw(V(td_rects_lower)));
}

/* the player's bits of every lamp cleared, but lamps with flag 80h; the
 * flags 2 cleared */
void lights_off_player(void)
{
    uint16_t bx = rw(V(td_lights)), g, si;

    while (!((g = rw(bx)) & 0x8000)) {
        bx = (uint16_t)(bx + 2);
        for (si = rw(g);; si = rw((uint16_t)(si + 6))) {
            wb(si, rb(si) & 0xFD);
            if (!(rb(si) & 0x80))
                wb((uint16_t)(si + 1), (uint8_t)(rb((uint16_t)(si + 1)) & ~rb(V(player_bit))));
            if (!rw((uint16_t)(si + 6)))
                break;
        }
    }
}

/* one of td_random_lights (the (frame_count AND 7)th, 0 standing for
 * 65536, the list taken round and round) gets its lamp showing +3 (flag 2) */
static void pick_random_light(void)
{
    uint32_t n = rw(V(frame_count)) & 7;
    uint16_t bx = rw(V(td_random_lights)), dx, lamp;

    if (n == 0)
        n = 0x10000;
    for (;;) {
        dx = rw(bx);
        bx = (uint16_t)(bx + 2);
        if (dx & 0x8000) {
            bx = rw(V(td_random_lights));
            continue;
        }
        if (--n == 0)
            break;
    }
    lamp = rw((uint16_t)(dx + 2));
    wb(lamp, rb(lamp) | 2);
    wb((uint16_t)(lamp + 1), 0);
    wb((uint16_t)(lamp + 3), 0);
    wb(V(random_lit), 0xFF);
}

/* the lights' palette from the off colours, then the current player's lit
 * lamps on */
static void restore_lights(void)
{
    uint16_t src = rw(V(lights_off_pal)), n = (uint16_t)(3 * rw(V(light_count))), i, bx, g, si;

    for (i = 0; i < n; i++)
        wb((uint16_t)(V(light_palette) + i), frb(seg_tdata, (uint16_t)(src + i)));
    bx = rw(V(td_lights));
    while (!((g = rw(bx)) & 0x8000)) {
        bx = (uint16_t)(bx + 2);
        for (si = rw(g);; si = rw((uint16_t)(si + 6))) {
            if (rb((uint16_t)(si + 1)) & rb(V(player_bit)))
                light_on(rw((uint16_t)(si + 4)));
            if (!rw((uint16_t)(si + 6)))
                break;
        }
    }
}

/* the ball to its start, speed 0, gravity from opt_slope, flippers on, the
 * table's objects for a new ball */
void reset_ball(void)
{
    ball_to_start();
    ww(V(ball_vx), 0);
    ww(V(ball_vy), 0);
    ww(V(gravity_x), 0);
    ww(V(gravity), (uint16_t)(9 + 2 * (uint8_t)(rb(V(opt_slope)) - 1)));
    wb(V(flipper_pressed), 0);
    wb(V(map_event), 0xFF);
    wb(V(flippers_off), 0);
    wb(V(ball_drained), 0);
    wb(V(nudge_phase), 0);
    ww(V(nudge_push), 0);
    wb(V(tilted), 0);
    wb(V(nudge_count), 0);
    reset_rect_targets(rw(V(td_rects_lower)));
    reset_rect_targets(rw(V(td_rects_upper)));
    reset_ball_objects();
    lights_off_player();
    reset_object_lists(0);
    pick_random_light();
    table_ball_events();
    clear_object_timers();
    restore_lights();
}
