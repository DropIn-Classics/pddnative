/* pd_handlers.c - the event objects' handlers: the routines the objects of
 * the tables name at +16h (PD.ASM advance_object to lock_ball3), run by
 * run_events with DI = the object and BP = player_rec.  See pd.h.
 *
 * A handler returns ZF (1 here), which matters when its object runs every
 * frame (flag 1): ZF ends it.  Both callers have ZF clear when they call,
 * so a handler that sets no flag returns 0; others return what their last
 * flag-setting instruction left, as the original does. */
#include "pd.h"

static int always_0(void) { return rb(V(always_0)) == 0; }
static int always_ff(void) { return rb(V(always_ff)) == 0; }

static void copy_words(uint16_t dst, uint16_t src, int words)
{
    int i;
    for (i = 0; i < 2 * words; i += 2)
        ww((uint16_t)(dst + i), rw((uint16_t)(src + i)));
}

static int obj_nop(uint16_t di)
{
    (void)di;
    return 0;
}

int obj_nop1(uint16_t di) { return obj_nop(di); }
int obj_nop2(uint16_t di) { return obj_nop(di); }
int obj_nop3(uint16_t di) { return obj_nop(di); }
int obj_nop4(uint16_t di) { return obj_nop(di); }
int obj_nop5(uint16_t di) { return obj_nop(di); }
int obj_nop6(uint16_t di) { return obj_nop(di); }

/* the bonus multiplier */
static int set_mult(uint8_t m)
{
    wb(V(bonus_mult), m);
    return 0;
}

int mult_2(uint16_t di) { (void)di; return set_mult(2); }
int mult_3(uint16_t di) { (void)di; return set_mult(3); }
int mult_4(uint16_t di) { (void)di; return set_mult(4); }
int mult_5(uint16_t di) { (void)di; return set_mult(5); }
int mult_6(uint16_t di) { (void)di; return set_mult(6); }
int mult_7(uint16_t di) { (void)di; return set_mult(7); }
int mult_8(uint16_t di) { (void)di; return set_mult(8); }
int mult_10(uint16_t di) { (void)di; return set_mult(0x0A); }

/* (2Eh-byte objects) the sequence +28h one step on for the player (not
 * onto a negative word), the first lamp not lit of the light group +2Ah
 * lit (not one with flag 8), the Ignition timer +2Ch restarted */
int advance_object(uint16_t di)
{
    uint16_t cx, si, step;
    uint8_t bit;

    cx = rw((uint16_t)(di + 0x28));
    if (cx) {
        step = (uint16_t)(cx + rb(V(player)) + 2);
        wb(step, (uint8_t)(rb(step) + 1));
        if (rw((uint16_t)(cx + 2 * rb(step) + 0x0A)) & 0x8000)
            wb(step, (uint8_t)(rb(step) - 1));
    }
    cx = rw((uint16_t)(di + 0x2A));
    if (cx) {
        bit = rb(V(player_bit));
        for (si = rw(cx); si; si = rw((uint16_t)(si + 6)))
            if (!(rb((uint16_t)(si + 1)) & bit)) {
                if (!(rb(si) & 8))
                    wb((uint16_t)(si + 1), rb((uint16_t)(si + 1)) | bit);
                break;
            }
    }
    cx = rw((uint16_t)(di + 0x2C));
    if (!cx)
        return 1;
    ww(cx, rw((uint16_t)(cx + 2)));
    return 0;
}

/* td_extra_ball's lamp (+1 FFh), extra_ball + 1 */
int extra_ball_award(uint16_t di)
{
    (void)di;
    wb((uint16_t)(rw(V(td_extra_ball)) + 1), 0xFF);
    wb(V(extra_ball), (uint8_t)(rb(V(extra_ball)) + 1));
    return rb(V(extra_ball)) == 0;
}

/* hurry_value to the score */
int add_hurry_value(uint16_t di)
{
    (void)di;
    return bcd_add(rw(V(player_rec)), V(hurry_value));
}

/* the score (+0) or the bonus (+8) doubled */
static int double_at(uint16_t at)
{
    copy_words(V(double_tmp), at, 4);
    return bcd_add(at, V(double_tmp));
}

int double_score(uint16_t di)
{
    (void)di;
    return double_at(rw(V(player_rec)));
}

int double_bonus(uint16_t di)
{
    (void)di;
    return double_at((uint16_t)(rw(V(player_rec)) + 8));
}

int hold_bonus(uint16_t di)
{
    (void)di;
    wb(V(bonus_held), 0xFF);
    return 0;
}

/* the message of td_count_texts for the count (player record +16h), at
 * the object's priority (+2) */
int count_message(uint16_t di)
{
    uint16_t n = (uint16_t)((rw((uint16_t)(rw(V(player_rec)) + 0x16)) - 1) << 1);
    uint16_t msg = rw((uint16_t)(rw(V(td_count_texts)) + n));
    uint8_t prio = rb((uint16_t)(di + 2));
    int same;

    if (msg & 0x8000)
        return 0;
    if (prio < rb(V(message_prio)))     /* set_message */
        return 0;
    same = prio == rb(V(message_prio));
    wb(V(message_prio), prio);
    ww(V(message), msg);
    wb((uint16_t)(msg + 1), 0);
    return same;
}

/* the jackpot to the score, then back to jackpot_base */
int collect_jackpot(uint16_t di)
{
    int zf;

    (void)di;
    copy_words(V(jackpot_shown), V(jackpot), 4);
    zf = bcd_add(rw(V(player_rec)), V(jackpot));
    copy_words(V(jackpot), V(jackpot_base), 4);
    return zf;
}

/* jackpot += jackpot_step */
int raise_jackpot(uint16_t di)
{
    int zf;

    (void)di;
    zf = bcd_add(V(jackpot), V(jackpot_step));
    copy_words(V(jackpot_shown), V(jackpot), 4);
    return zf;
}

/* the jackpot to the score, once more for each of the first two locks the
 * test finds full (it reads [lock + map_flags' address] where the lock's
 * state was presumably meant; see the hints), the count into
 * td_txt_jackpot4, the jackpot back to jackpot_base, the locks emptied */
int lock_jackpot(uint16_t di)
{
    uint16_t p = rw(V(player_rec)), bx = rw(V(td_locks));
    uint8_t n = 1;
    int i;

    (void)di;
    copy_words(V(jackpot_shown), V(jackpot), 4);
    bcd_add(p, V(jackpot));
    for (i = 0; i < 2; i++, bx = (uint16_t)(bx + 6))
        if (rb((uint16_t)(bx + V(map_flags)))) {
            n++;
            bcd_add(p, V(jackpot));
        }
    wb((uint16_t)(rw(V(td_txt_jackpot4)) + 8), (uint8_t)(n + '0'));
    copy_words(V(jackpot), V(jackpot_base), 4);
    reset_locks();
    wb(V(locks_lit), 0);
    wb(V(ball_locked), 0);
    return 0;
}

/* the current player's score added to the last of the other players'
 * (from player 2 on) that no word of it exceeds, compared word by word
 * from the lowest (as the code reads; not checked by running) */
int score_to_best(uint16_t di)
{
    uint16_t bx = rw(V(player_rec)), si = V(player_1), best = si;
    int8_t n = (int8_t)rb(V(last_player));
    int w;

    (void)di;
    copy_words(V(score_copy), bx, 4);
    while (--n >= 0) {
        si = (uint16_t)(si + 0x98);
        for (w = 0; w < 8; w += 2)
            if (rw((uint16_t)(V(score_copy) + w)) > rw((uint16_t)(si + w)))
                break;
        if (w == 8)
            best = si;
    }
    return bcd_add(best, bx);
}

/* jingle 11h, game_object_3 armed and switch_object_3 disarmed for the
 * player */
int nightmare_switch(uint16_t di)
{
    uint16_t o = (uint16_t)(V(switch_object_3) + 0x22);
    uint8_t bit = rb(V(player_bit));

    (void)di;
    wb(V(jingle_request), 0x11);
    wb((uint16_t)(V(game_object_3) + 0x22), rb((uint16_t)(V(game_object_3) + 0x22)) | bit);
    wb(o, (uint8_t)(rb(o) & ~bit));
    return rb(o) == 0;
}

/* the bonus counted into the score now (td_txt_countdown), then waits
 * (pd_qol: counted twice as fast, shorter waits) */
int countdown(uint16_t di)
{
    (void)di;
    ww(V(wait_count), (uint16_t)qol_frames(0x46, 0x1E));
    ww(V(bonus_digits), 0x0F);
    ww(V(bonus_count_period), (uint16_t)qol_frames(4, 2));
    copy_words(V(bonus_step), V(bonus_unit), 4);
    copy_words(V(bonus_left), (uint16_t)(rw(V(player_rec)) + 8), 4);
    show_text(rw(V(td_txt_countdown)));
    do {
        wait_frame();
        show_bonus_count();
        lights_frame();
    } while (!bonus_count_step());
    wait_frames(qol_frames(0x1E, 0x0F));
    show_bonus_count();
    wait_frames(qol_frames(0x28, 0x0F));
    return 1;
}

/* "russian roulette" (runs each frame while roulette_on): the first call
 * picks the start (td_roulette_pick by the frame counter), then the
 * lamps of td_roulette are lit in turn, slowing down, each with its text;
 * at the end the last one's sequence runs */
int roulette(uint16_t di)
{
    uint16_t si, cx, bp, lim;
    uint8_t bit = rb(V(player_bit));

    (void)di;
    if (!rb(V(roulette_on))) {
        wb(V(roulette_on), 0xFF);
        si = (uint16_t)(rw(V(td_roulette)) +
                        6 * rb((uint16_t)(rw(V(td_roulette_pick)) + (rw(V(frame_count)) & 0x7F))));
        ww(V(roulette_pos), si);
        ww(V(roulette_delay), 0x0A);
        ww(V(roulette_period), 0x0A);
        ww(V(roulette_delay), (uint16_t)(rw(V(roulette_delay)) >> 2));
        return always_ff();
    }
    for (si = rw(V(td_roulette));; si = (uint16_t)(si + 6)) {
        cx = rw((uint16_t)(si + 2));
        if (cx & 0x8000)
            break;
        if (cx)
            wb((uint16_t)(cx + 1), (uint8_t)(rb((uint16_t)(cx + 1)) & ~bit));
    }
    si = rw(V(td_roulette));
    for (;;) {
        di = rw(V(roulette_pos));
        cx = rw((uint16_t)(di + 2));
        if (!(cx & 0x8000))
            break;
        ww(V(roulette_pos), si);        /* past the end: from the first again */
    }
    if (cx)
        wb((uint16_t)(cx + 1), rb((uint16_t)(cx + 1)) | bit);
    bp = rw((uint16_t)(di + 4)) ? rw((uint16_t)(di + 4)) : V(txt_roulette);
    show_text(bp);
    ww(V(roulette_delay), (uint16_t)(rw(V(roulette_delay)) - 1));
    if (!(rw(V(roulette_delay)) & 0x8000)) {
        ww(V(roulette_pos), di);
        return always_ff();
    }
    ww(V(roulette_period), (uint16_t)(rw(V(roulette_period)) + 1));
    lim = rb(V(table_num)) == 3 ? 0x40 : 0x32;
    if (!((uint16_t)(rw(V(roulette_period)) - lim) & 0x8000)) {
        run_event(rw(di));
        wb(V(roulette_on), 0);
        return always_0();
    }
    ww(V(roulette_delay), (uint16_t)(rw(V(roulette_period)) >> 2));
    ww(V(roulette_pos), (uint16_t)(di + 6));
    return always_ff();
}

/* unless a ball is locked: locks_lit, each lock's lamp lit and blinking
 * (flags 14h) */
int light_locks(uint16_t di)
{
    uint16_t si, lamp;

    (void)di;
    if (!rb(V(ball_locked))) {
        wb(V(locks_lit), 0xFF);
        for (si = rw(V(td_locks)); !(rw(si) & 0x8000); si = (uint16_t)(si + 6))
            if ((lamp = rw((uint16_t)(si + 2))) != 0) {
                wb(lamp, rb(lamp) | 0x14);
                wb((uint16_t)(lamp + 1), rb((uint16_t)(lamp + 1)) | rb(V(player_bit)));
            }
    }
    return always_0();
}

/* locks lit: the ball is held and a new one comes (state 4); lock_ball2
 * and lock_ball3 also mark their lock used */
static int lock(uint16_t used)
{
    if (rb(V(locks_lit))) {
        wb(V(jingle_request), 0x0E);
        wb(V(ball_locked), 0xFF);
        ww(V(game_state), 4);
        wb(V(ball_held), 0xFF);
        wb(V(lock_pending), 0xFF);
        if (used != NONE)
            ww(used, 1);
    }
    return always_0();
}

int lock_ball(uint16_t di) { (void)di; return lock(NONE); }
int lock_ball2(uint16_t di) { (void)di; return lock(V(lock2_used)); }
int lock_ball3(uint16_t di) { (void)di; return lock(V(lock3_used)); }
