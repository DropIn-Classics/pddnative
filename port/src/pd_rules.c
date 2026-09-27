/* pd_rules.c - a frame of play (state 5): what the ball hit (hit
 * rectangles, lanes, holes), the scores, the event stack's objects run,
 * the lamp groups, the display (PD.ASM st_play, play_frame and what it
 * calls, bcd_add, bcd_sub).  See pd.h.
 *
 * The records are described in the hints at their users: hit rectangle
 * and group and target at test_hit_rects, lane at test_lanes_a, hole at
 * map_event_run, event object at run_events. */
#include "pd.h"
#include "sound.h"

/* the object's points: +0Eh to the bonus, +6 to the score */
static void score_object(uint16_t di)
{
    uint16_t p = rw(V(player_rec));
    bcd_add((uint16_t)(p + 8), (uint16_t)(di + 0x0E));
    bcd_add(p, (uint16_t)(di + 6));
}

/* the hits of td_count_obj (record +16h, shown in BCD at +14h; at
 * count_target the sequence of td_count_event runs and both restart) and
 * of td_count_obj2 (+12h, in BCD at +10h) */
static void count_object(uint16_t di)
{
    uint16_t si = rw(V(player_rec)), n;

    if (di == rw(V(td_count_obj))) {
        n = rw((uint16_t)(si + 0x16));
        if (n >= rw(V(count_target))) {
            run_event(rw((uint16_t)(rw(V(td_count_event)) + 2)));
            ww((uint16_t)(si + 0x14), 0);
            ww((uint16_t)(si + 0x16), 0);
        } else {
            ww((uint16_t)(si + 0x16), (uint16_t)(n + 1));
            bcd_inc_word((uint16_t)(si + 0x14));
        }
    } else if (di == rw(V(td_count_obj2))) {
        ww((uint16_t)(si + 0x12), (uint16_t)(rw((uint16_t)(si + 0x12)) + 1));
        bcd_inc_word((uint16_t)(si + 0x10));
    }
}

/* SI = a group: its sequence (+14h), if any */
static void run_object_event(uint16_t g)
{
    if (g && rw((uint16_t)(g + 0x14)))
        run_event(rw((uint16_t)(g + 0x14)));
}

/* ---- the display */

/* the hurry-up counting (up; down on table 3, its end at 0), else the
 * running message, else after a while without score the texts of
 * td_score_texts, else "player n" and the score */
void display_frame(void)
{
    uint16_t hurry = rw(V(td_hurry)), msg, bx, t;

    if (rw(V(running_object)))
        return;
    if (!rb(V(ball_held)) && (rb((uint16_t)(hurry + 0x22)) & rb(V(player_bit)))) {
        if (rb(V(table_num)) != 3) {
            bcd_add(V(hurry_value), V(hurry_step));
        } else if (bcd_sub(V(hurry_value), V(hurry_step))) {
            wb(V(jingle_request), 0);
            wb((uint16_t)(hurry + 0x22), (uint8_t)(rb((uint16_t)(hurry + 0x22)) & ~rb(V(player_bit))));
        }
        if (rb(V(message_prio)) < 0x64) {
            ww(V(message), 0);
            wb(V(message_prio), 0);
            show_text(rw(V(td_txt_hurry)));
            return;
        }
    }
    if (rw(V(message))) {
        if (!message_step(rw(V(message)))) {
            ww(V(message), 0);
            wb(V(message_prio), 0);
        }
        return;
    }
    ww(V(message), 0);
    wb(V(message_prio), 0);
    if (!(rw(V(idle_timer)) & 0x8000)) {
        ww(V(idle_timer), (uint16_t)(rw(V(idle_timer)) - 1));
        if (!(rw(V(idle_timer)) & 0x8000))
            goto score;
        ww(V(score_text), rw(V(td_score_texts)));
    }
    msg = rw(rw(V(score_text)));
    if (msg != 0xFFFF) {
        if (!message_start(msg))
            ww(V(score_text), (uint16_t)(rw(V(score_text)) + 2));
        return;
    }
    ww(V(score_text), rw(V(td_score_texts)));
    ww(V(idle_timer), 0x348);
score:
    for (bx = rw(V(td_score_texts)); !((t = rw(bx)) & 0x8000); bx = (uint16_t)(bx + 2))
        wb((uint16_t)(t + 1), 0);
    wb((uint16_t)(V(txt_player) + 7), (uint8_t)(rb(V(player)) + '1'));
    show_text(V(txt_player));
    show_bcd(rw(V(player_rec)), 8, 6);
}

/* 4 BCD bytes at si as 8 digits, written backwards from di; returns di
 * after the last DEC (for the flags update_texts' DAA inherits) */
static uint16_t bcd4_to_text(uint16_t si, uint16_t di)
{
    int i;

    for (i = 0; i < 4; i++, si++) {
        uint8_t v = rb(si);
        wb(di--, (uint8_t)((v & 0x0F) + '0'));
        wb(di--, (uint8_t)((v >> 4) + '0'));
    }
    return di;
}

/* the texts with numbers in them: player and ball, the jackpots, the
 * hurry-up value, the bonus multiplier, Ignition's second count */
static void update_texts(void)
{
    uint16_t bx = rw(V(td_txt_player_ball)), di;
    uint8_t m, lo, hi;
    int cf = 0, af = 0;

    wb((uint16_t)(bx + 8), (uint8_t)(rb(V(player)) + '1'));
    wb((uint16_t)(bx + 0x12), (uint8_t)(3 - rb(V(balls_left)) + '1'));
    bcd4_to_text(V(jackpot_shown), (uint16_t)(rw(V(td_txt_jackpot)) + 0x13));
    bcd4_to_text(V(jackpot_shown), (uint16_t)(rw(V(td_txt_jackpot2)) + 0x13));
    bcd4_to_text(V(jackpot_shown), (uint16_t)(rw(V(td_txt_jackpot3)) + 0x13));
    bcd4_to_text(V(hurry_value), (uint16_t)(rw(V(td_txt_hurry)) + 0x13));
    if (rb(V(table_num)) & 2) {
        /* DAA below takes AF from bcd4_to_text's last DEC DI (on tables 0
         * and 1 from a TEST, AF 0 as in tools/run; undefined on a CPU) */
        di = bcd4_to_text(V(jackpot_shown), (uint16_t)(rw(V(td_txt_jackpot4)) + 0x13));
        af = ((di + 1) & 0x0F) == 0;
    }
    m = daa(rb(V(bonus_mult)), &cf, af);
    lo = (uint8_t)((m & 0x0F) + '0');
    hi = (uint8_t)((m >> 4) + '0');
    bx = rw(V(td_txt_bonus_x));
    wb((uint16_t)(bx + 0x0B), lo);
    wb((uint16_t)(bx + 0x0A), hi == '0' ? ' ' : hi);
    if (rb(V(table_num)) == 0) {
        uint8_t v = rb((uint16_t)(rw(V(player_rec)) + 0x10));
        wb((uint16_t)(V(txt_count2) + 5), (uint8_t)((v >> 4) + '0'));
        wb((uint16_t)(V(txt_count2) + 6), (uint8_t)((v & 0x0F) + '0'));
    }
}

/* ---- what the ball hit */

static void clear_hit_score(void)
{
    ww(V(hit_object), 0);
    ww(V(hit_score), 0);
}

static int inside(uint16_t x, uint16_t y, uint16_t r)
{
    return x >= rw(r) && y >= rw((uint16_t)(r + 2)) && x <= rw((uint16_t)(r + 4)) &&
           y <= rw((uint16_t)(r + 6));
}

/* hit_x/hit_y in a hit rectangle: its group (hit_score) and the target of
 * the group under the point not yet hit by the player (hit_object; a type
 * 7 target within its 14h frames gives nothing), the group's sounds */
static void test_hit_rects(void)
{
    uint16_t si, x = rw(V(hit_x)), y = rw(V(hit_y)), g, t, snd;
    uint8_t bit = rb(V(player_bit));

    if (rb(V(hit_lane)))
        return;
    if (rb(V(surface_type)) == 5)
        goto none;
    for (si = rw(V(hit_rects)); rw(si) != 0xFFFF; si = (uint16_t)(si + 0x0A)) {
        if (!inside(x, y, si))
            continue;
        g = rw((uint16_t)(si + 8));
        ww(V(hit_score), g);
        fww(seg_code, V(hit_sounds), rw((uint16_t)(g + 2)));
        for (t = rw(g);; t = rw((uint16_t)(t + 4))) {
            if (inside(x, y, (uint16_t)(t + 6))) {
                if (rb((uint16_t)(t + 1)) & bit)
                    goto none;
                ww(V(hit_object), t);
                if (rb(t) == 7 && rw((uint16_t)(t + 0x0E)))
                    clear_hit_score();
                snd = frw(seg_code, V(hit_sounds));
                if ((snd >> 8) != 0xFF)
                    wb(V(sound_request), (uint8_t)(snd >> 8));
                if ((snd & 0xFF) != 0xFF)
                    wb(V(jingle_request), (uint8_t)snd);
                return;
            }
            if (!rw((uint16_t)(t + 4)))
                goto none;
        }
    }
none:
    clear_hit_score();
}

/* hit_x/hit_y in a lane of the list (x2, y2 not inside): its object,
 * the object's group, hit_lane, the sound; not when tilted */
static void test_lanes(uint16_t si, uint8_t sound)
{
    uint16_t x = rw(V(hit_x)), y = rw(V(hit_y)), o;

    for (; rw(si) != 0xFFFF; si = (uint16_t)(si + 0x0A)) {
        if (x < rw(si) || y < rw((uint16_t)(si + 2)) || x >= rw((uint16_t)(si + 4)) ||
            y >= rw((uint16_t)(si + 6)))
            continue;
        if (rb(V(tilted)))
            return;
        o = rw((uint16_t)(si + 8));
        ww(V(hit_object), o);
        ww(V(hit_score), rw((uint16_t)(o + 4)));
        wb(V(hit_lane), 0xFF);
        wb(V(sound_request), sound);
        return;
    }
}

/* the lamp of hit_object (+2) lit for the player */
static void light_hit_object(void)
{
    uint16_t o = rw(V(hit_object)), lamp;

    if (o && (lamp = rw((uint16_t)(o + 2))) != 0)
        wb((uint16_t)(lamp + 1), rb((uint16_t)(lamp + 1)) | rb(V(player_bit)));
}

/* hit_object of type 1 or 8: its bit for the player; type 7: its hit
 * timer (+0Eh) := 14h */
static void mark_hit_object(void)
{
    uint16_t o = rw(V(hit_object));
    uint8_t type;

    if (!o)
        return;
    type = rb(o) & 0x7F;
    if (type == 1 || type == 8)
        wb((uint16_t)(o + 1), rb((uint16_t)(o + 1)) | rb(V(player_bit)));
    else if (type == 7)
        ww((uint16_t)(o + 0x0E), 0x14);
}

/* the score record's points (+4) to the score, (+0Ch) to the bonus; while
 * a hole holds the ball (hole_period) its group's every hole_period frames,
 * hole_count times, then the ball goes (sound 8) */
static void add_hit_score(void)
{
    uint16_t bx, si;

    if (rw(V(hole_period))) {
        ww(V(hole_timer), (uint16_t)(rw(V(hole_timer)) - 1));
        if (rw(V(hole_timer)))
            return;
        bx = rw(V(hole_group));
        ww(V(hole_timer), rw(V(hole_period)));
        ww(V(hole_count), (uint16_t)(rw(V(hole_count)) - 1));
        if (!rw(V(hole_count))) {
            wb(V(ball_held), 0);
            ww(V(hole_period), 0);
            wb(V(sound_request), 8);
            return;
        }
    } else {
        bx = rw(V(hit_score));
        if (!bx)
            return;
    }
    si = rw(V(player_rec));
    bcd_add((uint16_t)(si + 8), (uint16_t)(bx + 0x0C));
    bcd_add(si, (uint16_t)(bx + 4));
}

/* the other holes' busy bytes cleared (all but `keep`) */
static void holes_not_busy(uint16_t keep)
{
    uint16_t bx, s;

    for (bx = rw(V(map_events)); (s = rw(bx)) != 0xFFFF; bx = (uint16_t)(bx + 2))
        if (s != keep)
            wb((uint16_t)(s + 1), 0);
}

/* the hole map_events[map_event] (hints at map_event_run).  Returns 1 when
 * the event has no hole (entry 0): the original then leaves play_frame. */
static int map_event_run(void)
{
    uint16_t bx = (uint16_t)(rw(V(map_events)) + (uint8_t)(2 * rb(V(map_event)))), h, snd, di, si;
    uint8_t type;

    wb(V(map_event), 0xFF);
    h = rw(bx);
    if (!h) {
        wb((uint16_t)(bx + 1), 0);
        holes_not_busy(V(play_after_holes));
        return 1;
    }
    snd = rw((uint16_t)(rw((uint16_t)(h + 4)) + 2));
    if ((snd >> 8) != 0xFF)
        wb(V(sound_request), (uint8_t)(snd >> 8));
    if ((snd & 0xFF) != 0xFF)
        wb(V(jingle_request), (uint8_t)snd);
    if (rb((uint16_t)(h + 1))) {
        wb((uint16_t)(h + 1), 0);
        holes_not_busy(h);
        return 0;
    }
    type = rb(h);
    if (type == 6) {                    /* a lock */
        wb((uint16_t)(h + 1), 0xFF);
        wb(V(ball_in_hole), 0xFF);
        if (rb(V(locks_lit)))
            for (di = rw(V(td_locks)); !(rw(di) & 0x8000); di = (uint16_t)(di + 6)) {
                if (rw(di) != h)
                    continue;
                if (rw((uint16_t)(di + 4)) & 0x8000)
                    goto eject;         /* full already */
                ww((uint16_t)(di + 4), 0xFFFF);
                wb(V(ball_locked), 0xFF);
                if (rw((uint16_t)(di + 2))) {
                    uint16_t lamp = rw((uint16_t)(di + 2));
                    wb(lamp, rb(lamp) & 0xEB);
                }
                break;
            }
    } else if (type == 5) {             /* holds the ball, points given hole_count times */
        ww(V(hole_count), rw((uint16_t)(h + 8)));
        ww(V(hole_period), rw((uint16_t)(h + 6)));
        ww(V(hole_timer), 1);
    } else {
        ww(V(hit_object), h);
        si = rw((uint16_t)(h + 4));
        if (type == 0x14)
            run_object_event(si);
        ww(V(hit_score), si);
        holes_not_busy(h);
        return 0;
    }
    wb(V(ball_held), 0xFF);
    ww(V(bonus_count_period), 0);
    ww(V(hit_object), h);
    si = rw((uint16_t)(h + 4));
    ww(V(hole_group), si);
    ww(V(event_sp_ball), rw(V(event_sp)));
    run_object_event(si);
eject:
    ww(V(ball_vx), rw((uint16_t)(h + 0x0A)));
    ww(V(ball_vy), rw((uint16_t)(h + 0x0C)));
    ww(V(ball_x_hi), (uint16_t)(rw((uint16_t)(h + 0x0E)) << 2));
    ww(V(ball_y_hi), (uint16_t)(rw((uint16_t)(h + 0x10)) << 2));
    holes_not_busy(h);
    return 0;
}

/* ---- the objects */

/* while a random lamp blinks (random_lit): each of td_random_lights
 * toggles; one of them lit (its target hit): if it was the chosen one
 * (flag 2), td_random_event's sequence; then all stop */
static void random_light_check(void)
{
    uint16_t si, e, lamp;

    if (!rb(V(random_lit)))
        return;
    for (si = rw(V(td_random_lights)); !((e = rw(si)) & 0x8000);) {
        si = (uint16_t)(si + 2);
        lamp = rw((uint16_t)(e + 2));
        wb((uint16_t)(lamp + 3), (uint8_t)~rb((uint16_t)(lamp + 3)));
        if (!rb((uint16_t)(lamp + 1)))
            continue;
        if (rb(lamp) & 2)
            run_object_event(rw(rw(V(td_random_event))));
        for (si = rw(V(td_random_lights)); !((e = rw(si)) & 0x8000); si = (uint16_t)(si + 2)) {
            lamp = rw((uint16_t)(e + 2));
            wb(lamp, rb(lamp) & 0xFD);
            wb((uint16_t)(lamp + 3), 0);
        }
        wb(V(random_lit), 0);
        return;
    }
}

static void restart_timer(uint16_t di)
{
    ww((uint16_t)(di + 0x26), rw((uint16_t)(di + 0x24)));
}

/* a message (at `msg`) shown if its priority is not below the running one's */
static void offer_message(uint16_t msg, uint8_t prio)
{
    if (!msg || prio < rb(V(message_prio)))
        return;
    wb(V(message_prio), prio);
    ww(V(message), msg);
    wb((uint16_t)(msg + 1), 0);
}

static void request_sounds(uint16_t w)
{
    if ((w >> 8) != 0xFF)
        wb(V(sound_request), (uint8_t)(w >> 8));
    if ((w & 0xFF) != 0xFF)
        wb(V(jingle_request), (uint8_t)w);
}

/* one entry of the event stack popped and run (hints at run_events): armed
 * (FFFFh) or fired (0); a fired object with flag 1 becomes the running
 * object, whose handler then runs each frame, instead, until it returns ZF */
static void run_events(void)
{
    uint16_t bx, di, cx, lamp;

    if (!rw(V(running_object))) {
        bx = rw(V(event_sp));
        if (bx == V(event_stack))
            return;
        ww(V(event_sp), (uint16_t)(bx - 4));
        di = rw((uint16_t)(bx - 2));
        cx = rw((uint16_t)(bx - 4));
        if (cx) {                       /* armed */
            request_sounds(rw((uint16_t)(di + 0x1C)));
            lamp = rw((uint16_t)(di + 0x1E));
            if (lamp && rb((uint16_t)(di + 1)) != 0xFE)
                wb((uint16_t)(lamp + 1), rb((uint16_t)(lamp + 1)) | rb(V(player_bit)));
            offer_message(rw((uint16_t)(di + 0x20)), rb((uint16_t)(di + 3)));
            if (rb((uint16_t)(di + 0x23)) & 4)
                restart_timer(di);
            return;
        }
        score_object(di);               /* fired */
        request_sounds(rw((uint16_t)(di + 4)));
        count_object(di);
        if ((lamp = rw((uint16_t)(di + 0x18))) != 0)
            wb((uint16_t)(lamp + 1), rb((uint16_t)(lamp + 1)) | rb(V(player_bit)));
        if ((lamp = rw((uint16_t)(di + 0x1E))) != 0)
            wb((uint16_t)(lamp + 1), (uint8_t)(rb((uint16_t)(lamp + 1)) & ~rb(V(player_bit))));
        if (!(rb((uint16_t)(di + 0x23)) & 1)) {
            if (rb((uint16_t)(di + 0x23)) & 2)
                restart_timer(di);
            if (rw((uint16_t)(di + 0x16)))
                call_handler(rw((uint16_t)(di + 0x16)), di);
            offer_message(rw((uint16_t)(di + 0x1A)), rb((uint16_t)(di + 2)));
            return;
        }
        ww(V(running_object), di);
    }
    di = rw(V(running_object));
    cx = rw((uint16_t)(di + 0x16));
    if (!cx || call_handler(cx, di))
        end_running();
}

/* td_timed: each running timer (+26h) counts down; at 0 the object is
 * disarmed for the player; its lamp (+1Eh) is out while it does not run */
static void object_timers(void)
{
    uint16_t si = rw(V(td_timed)), di, lamp;
    uint8_t keep = (uint8_t)~rb(V(player_bit));

    for (; !((di = rw(si)) & 0x8000); si = (uint16_t)(si + 2)) {
        if (rw((uint16_t)(di + 0x26))) {
            ww((uint16_t)(di + 0x26), (uint16_t)(rw((uint16_t)(di + 0x26)) - 1));
            if (rw((uint16_t)(di + 0x26)))
                continue;
            wb((uint16_t)(di + 0x22), rb((uint16_t)(di + 0x22)) & keep);
        }
        if ((lamp = rw((uint16_t)(di + 0x1E))) != 0)
            wb((uint16_t)(lamp + 1), rb((uint16_t)(lamp + 1)) & keep);
    }
}

/* ---- the groups */

/* all targets of a hit rectangle's group hit: the group's sequence, then
 * they go out one by one (clear_rect_group) */
static void rect_group_done(void)
{
    uint16_t si, t, g;
    uint8_t bit = rb(V(player_bit));

    if (rw(V(rect_clear_timer)))
        return;
    for (si = rw(V(hit_rects)); rw(si) != 0xFFFF; si = (uint16_t)(si + 0x0A)) {
        for (t = rw(rw((uint16_t)(si + 8))); rb((uint16_t)(t + 1)) & bit; t = rw((uint16_t)(t + 4)))
            if (!rw((uint16_t)(t + 4))) {
                ww(V(rect_clear_timer), 0x28);
                g = rw((uint16_t)(si + 8));
                ww(V(rect_clear_target), rw(g));
                run_object_event(g);
                return;
            }
    }
}

/* a light group of td_lights with every lamp lit for the player (a lamp
 * with flag 8 puts its group out of it): its sequence, its lamps out, the
 * group blinks for 46h frames (blink_light_group) */
static void light_group_done(void)
{
    uint16_t si, g, lamp;
    uint8_t bit = rb(V(player_bit));

    if (rw(V(light_group_timer)))
        return;
    for (si = rw(V(td_lights)); !((g = rw(si)) & 0x8000);) {
        si = (uint16_t)(si + 2);
        for (lamp = rw(g);; lamp = rw((uint16_t)(lamp + 6))) {
            if ((rb(lamp) & 8) || !(rb((uint16_t)(lamp + 1)) & bit))
                break;
            if (!rw((uint16_t)(lamp + 6))) {
                ww(V(light_group_timer), 0x46);
                ww(V(done_light_group), g);
                for (lamp = rw(g); lamp; lamp = rw((uint16_t)(lamp + 6))) {
                    wb((uint16_t)(lamp + 1), (uint8_t)(rb((uint16_t)(lamp + 1)) & ~bit));
                    wb(lamp, rb(lamp) & 0xFD);
                }
                run_object_event(g);
                return;
            }
        }
    }
}

/* the targets of a completed hit rectangle group go out one by one, one
 * after rect_clear_timer's 28h frames, the rest a frame each */
static void clear_rect_group(void)
{
    uint16_t t = rw(V(rect_clear_target));

    if (!t)
        return;
    ww(V(rect_clear_timer), (uint16_t)(rw(V(rect_clear_timer)) - 1));
    if (rw(V(rect_clear_timer)))
        return;
    wb((uint16_t)(t + 1), (uint8_t)(rb((uint16_t)(t + 1)) & ~rb(V(player_bit))));
    ww(V(rect_clear_target), rw((uint16_t)(t + 4)));
    if (rw(V(rect_clear_target)))
        ww(V(rect_clear_timer), 1);
}

/* the completed light group blinks (4-frame phases, the lamps alternating)
 * until light_group_timer runs out */
static void blink_light_group(void)
{
    uint16_t g = rw(V(done_light_group)), lamp, t;
    uint8_t al;

    if (!g)
        return;
    t = (uint16_t)(rw(V(light_group_timer)) - 1);
    ww(V(light_group_timer), t);
    if (!t) {
        for (lamp = rw(g); lamp; lamp = rw((uint16_t)(lamp + 6)))
            wb(lamp, rb(lamp) & 0xFD);
        ww(V(done_light_group), 0);
        return;
    }
    al = (t & 4) ? 0 : 0xFF;
    for (lamp = rw(g); lamp; lamp = rw((uint16_t)(lamp + 6))) {
        wb((uint16_t)(lamp + 3), al);
        wb(lamp, rb(lamp) | 2);
        al = (uint8_t)~al;
    }
}

/* a flipper was pressed: the lit states of each group of td_rotating move
 * one lamp on */
static void rotate_lights(void)
{
    uint16_t bx, g, si, di;
    uint8_t first;

    if (rb(V(flipper_pressed)))
        for (bx = rw(V(td_rotating)); !((g = rw(bx)) & 0x8000);) {
            bx = (uint16_t)(bx + 2);
            si = rw(g);
            first = rb((uint16_t)(si + 1));
            for (di = si; rw((uint16_t)(di + 6)); si = di) {
                di = rw((uint16_t)(di + 6));
                wb((uint16_t)(si + 1), rb((uint16_t)(di + 1)));
            }
            wb((uint16_t)(si + 1), first);
        }
    wb(V(flipper_pressed), 0);
}

/* the hit timers (+0Eh) of the type 7 targets at the start of each of the
 * level's hit rectangle groups count down */
static void object_timers7(void)
{
    uint16_t si, g, di;

    for (si = rw(V(hit_rects)); !((g = rw((uint16_t)(si + 8))) & 0x8000); si = (uint16_t)(si + 0x0A))
        for (di = rw(g); di && rb(di) == 7; di = rw((uint16_t)(di + 4)))
            if (rw((uint16_t)(di + 0x0E)))
                ww((uint16_t)(di + 0x0E), (uint16_t)(rw((uint16_t)(di + 0x0E)) - 1));
}

/* a ball in a hole of type 6 or taken by a lock stays held while a message
 * or the events it started run; then it goes (sound 8) */
static void hold_ball_check(void)
{
    if (!rb(V(ball_in_hole))) {
        if (rb(V(lock_pending)))
            wb(V(ball_held), 0xFF);
        return;
    }
    if (rw(V(message))) {
        wb(V(ball_held), 0xFF);
        return;
    }
    if (rw(V(event_sp)) != rw(V(event_sp_ball))) {
        wb(V(ball_in_hole), 0xFF);
        wb(V(ball_held), 0xFF);
        return;
    }
    if (rb(V(lock_pending))) {
        wb(V(ball_held), 0xFF);
        return;
    }
    wb(V(sound_request), 8);
    wb(V(ball_held), 0);
    wb(V(ball_in_hole), 0);
}

/* hit_score, hit_object, hit_x/hit_y and hit_lane cleared, unless the ball
 * is held */
static void clear_hit(void)
{
    if (rb(V(ball_held)))
        return;
    ww(V(hit_score), 0);
    ww(V(hit_object), 0);
    ww(V(hit_x), 0);
    ww(V(hit_y), 0);
    wb(V(hit_lane), 0);
}

/* ---- the frame */

static void play_frame(void)
{
    update_texts();
    object_timers();
    display_frame();
    clear_hit_score();
    test_hit_rects();
    if (rw(V(hit_object))) {
        light_hit_object();
        mark_hit_object();
        add_hit_score();
    }
    clear_hit_score();
    test_lanes(rw(V(lanes_a)), 2);
    if (rw(V(hit_object))) {
        light_hit_object();
        add_hit_score();
    }
    clear_hit_score();
    test_lanes(rw(V(lanes_b)), 6);
    if (rw(V(hit_object))) {
        light_hit_object();
        add_hit_score();
    }
    clear_hit_score();
    if (rb(V(map_event)) != 0xFF && map_event_run())
        return;
    if (rw(V(hit_object))) {
        light_hit_object();
        add_hit_score();
    }
    random_light_check();
    run_events();
    lights_frame();
    rect_group_done();
    light_group_done();
    clear_rect_group();
    blink_light_group();
    rotate_lights();
    object_timers7();
    hold_ball_check();
    clear_hit();
}

/* state 5: a frame of the game; the ball drained: state 6; Esc: state 2 */
void st_play(void)
{
    checkpoint("st_play");
    ww(V(frame_count), (uint16_t)(rw(V(frame_count)) + 1));
    scroll_follow();
    ball_frame();
    nudge();
    plunger();
    play_frame();
    scroll_step();
    if (rb(V(ball_drained)))
        ww(V(game_state), 6);
    if (key_down(0x01)) {
        wait_keys_up();
        wb(V(tune_request), 0);
        ww(V(game_state), 2);
    }
}

/* state 8 (key P): the music at volume 1, "game paused" until a key (Esc
 * asks to quit); then state 5.  The original polls the keys in a loop;
 * here each round is a picture. */
void st_pause(void)
{
    int i, any;

    wait_keys_up();
    snd_volume(1);
    for (;;) {
        show_text(V(txt_paused));
        if (key_down(0x01) && ask_quit()) {
            stop_sound();
            ww(V(game_state), 0);
            return;
        }
        for (i = 0, any = 0; i < 0x10; i++)
            any |= rb((uint16_t)(V(key_map) + i));
        if (any)
            break;
        pump_frame();
    }
    wait_keys_up();
    ww(V(game_state), 5);
    snd_volume(0x100);
}

/* state 9: tilt: flippers off, the ball's bonus and counts cleared,
 * "tilt" until the ball drains (only the lanes still tested), state 6 */
void st_tilt(void)
{
    uint16_t bx = rw(V(player_rec));
    int i;

    checkpoint("st_tilt");
    wb(V(jingle_request), 6);
    wb(V(nudge_phase), 0);
    ww(V(nudge_push), 0);
    wb(V(flippers_off), 0xFF);
    wb(V(ball_held), 0);
    wb(V(tilted), 0xFF);
    lights_off_all();
    for (i = 0x0C; i <= 0x12; i += 2)
        ww((uint16_t)(bx + i), 0);
    do {
        ww(V(frame_count), (uint16_t)(rw(V(frame_count)) + 1));
        scroll_follow();
        ball_frame();
        plunger();
        scroll_step();
        test_lanes(rw(V(lanes_a)), 2);
        test_lanes(rw(V(lanes_b)), 6);
        show_text(V(txt_tilt));
        lights_frame();
        object_timers7();
    } while (!rb(V(ball_drained)));
    ww(V(game_state), 6);
}
