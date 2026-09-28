/* pd_lost.c - state 6: the ball is lost (PD.ASM st_ball_lost to
 * release_locked_ball, count_bonus, bonus_count_step, add_booster): the
 * bonus times the multiplier counted into the score, the boosters, then
 * the next player, a locked ball back into play, or game over.  See pd.h. */
#include "pd.h"

static void copy_words(uint16_t dst, uint16_t src, int words)
{
    int i;
    for (i = 0; i < 2 * words; i += 2)
        ww((uint16_t)(dst + i), rw((uint16_t)(src + i)));
}

/* the bonus (5 bytes) from column 0Ah */
static void show_bonus(void)
{
    show_bcd((uint16_t)(rw(V(player_rec)) + 8), 0x0A, 5);
}

void show_bonus_count(void)
{
    show_bcd(V(bonus_left), 0x0A, 5);
}

/* one step of the bonus into the score every bonus_count_period frames:
 * bonus_step taken from bonus_left and added to the score while the digit
 * of bonus_left being counted is not 0, then the next digit (bonus_step
 * * 10); 1 when all is counted */
int bonus_count_step(void)
{
    uint16_t n, si = rw(V(player_rec));
    uint8_t digit;
    int i, cf;

    ww(V(wait_count), (uint16_t)(rw(V(wait_count)) - 1));
    if (rw(V(wait_count)))
        return 0;
    ww(V(wait_count), rw(V(bonus_count_period)));
    ww(V(bonus_step_ptr), V(bonus_step));
    ww(V(bonus_left_ptr), V(bonus_left));
    n = (uint16_t)(0x0F - rw(V(bonus_digits)));
    digit = rb((uint16_t)(V(bonus_left) + (n >> 1)));
    wb(V(msg_counting), (n & 1) ? (digit & 0xF0) : (digit & 0x0F));
    if (rb(V(msg_counting))) {
        wb(V(sound_request), 0x11);
        for (i = 0, cf = 0; i < 6; i++)
            wb((uint16_t)(V(bonus_left) + i),
               bcd_sbb(rb((uint16_t)(V(bonus_left) + i)), rb((uint16_t)(V(bonus_step) + i)), &cf));
        ww(V(bonus_step_ptr), (uint16_t)(V(bonus_step) + 6));
        for (i = 0, cf = 0; i < 6; i++)
            wb((uint16_t)(si + i), bcd_adc(rb((uint16_t)(si + i)), rb((uint16_t)(V(bonus_step) + i)), &cf));
        return 0;
    }
    ww(V(bonus_digits), (uint16_t)(rw(V(bonus_digits)) - 1));
    if (!rw(V(bonus_digits)))
        return 1;
    for (i = 0; i < 4; i++) {           /* bonus_step * 10h, the 64 bits a word at a time */
        int w, c = 0;
        for (w = 0; w < 8; w += 2) {
            uint16_t v = rw((uint16_t)(V(bonus_step) + w));
            ww((uint16_t)(V(bonus_step) + w), (uint16_t)(v << 1 | c));
            c = v >> 15;
        }
    }
    for (i = 0; i < 8; i += 2)
        if (rw((uint16_t)(V(bonus_left) + i))) {
            ww(V(wait_count), rw(V(bonus_count_period)));
            return 0;
        }
    return 1;
}

/* the bonus times the multiplier ("bonus x n", 30 frames a step), then
 * counted into the score digit by digit (QOL_BONUS: 20 frames a step,
 * the count twice as fast, the holds before and after it shorter) */
static void count_bonus(void)
{
    uint16_t bx = rw(V(player_rec)), cx, di = V(txt_bonus_x);
    uint8_t dl = 1;

    ww(V(wait_count), (uint16_t)qol_frames(QOL_BONUS, 0x46, 0x1E));
    ww(V(bonus_digits), 0x0F);
    ww(V(bonus_count_period), (uint16_t)qol_frames(QOL_BONUS, 4, 2));
    copy_words(V(bonus_step), V(bonus_unit), 4);
    copy_words(V(bonus_left), (uint16_t)(bx + 8), 4);
    wb((uint16_t)(di + 8), '0');
    wb((uint16_t)(di + 9), '1');
    show_text(di);
    cx = (uint16_t)(rb(V(bonus_mult)) - 1);
    if (cx) {
        for (;;) {
            int cf = 0;
            wb(V(sound_request), 0x10);
            dl = bcd_adc(dl, 1, &cf);
            show_bonus();
            lights_frame();
            wait_frames(qol_frames(QOL_BONUS, 0x1E, 0x14));
            if (!cx)
                break;
            wb((uint16_t)(di + 8), (uint8_t)((dl >> 4) + '0'));
            wb((uint16_t)(di + 9), (uint8_t)((dl & 0x0F) + '0'));
            show_text(di);
            bcd_add((uint16_t)(bx + 8), V(bonus_left));
            cx--;
            if (cx & 0x8000)
                break;
        }
        copy_words(V(bonus_left), (uint16_t)(rw(V(player_rec)) + 8), 4);
    }
    do {
        wait_frame();
        show_bonus_count();
        lights_frame();
    } while (!bonus_count_step());
    wait_frames(qol_frames(QOL_BONUS, 0x1E, 0x0F));
    show_bonus_count();
    wait_frames(qol_frames(QOL_BONUS, 0x1E, 0x0F));
}

/* booster_value (6 bytes BCD) added at player record +4 (see the hints:
 * it lands above the digits shown) */
static void add_booster(void)
{
    uint16_t di = (uint16_t)(rw(V(player_rec)) + 4);
    int i, cf = 0;

    for (i = 0; i < 6; i++)
        wb((uint16_t)(di + i), bcd_adc(rb((uint16_t)(di + i)), rb((uint16_t)(V(booster_value) + i)), &cf));
}

/* extra_ball: the same player again; else the next player, after the last
 * one the next ball, after the last ball game over (state 7) */
static void next_player(void)
{
    if (!rb(V(extra_ball))) {
        uint8_t next = (uint8_t)(rb(V(player)) + 1);
        wb(V(player_bit), (uint8_t)(rb(V(player_bit)) << 1));
        wb(V(player), next);
        if (rb(V(last_player)) < next) {
            wb(V(ball_num), (uint8_t)(rb(V(ball_num)) + 1));
            wb(V(balls_left), (uint8_t)(rb(V(balls_left)) - 1));
            if (rb(V(ball_num)) == rb(V(balls_per_game))) {
                ww(V(game_state), 7);
                return;
            }
            wb(V(player), 0);
            wb(V(player_bit), 1);
        }
    }
    reset_ball();
    ww(V(game_state), 3);
}

/* the player's boosters (record +10h, BCD) one by one: shown in
 * td_txt_boosters, add_booster, minus booster_step; then next_player */
static void count_boosters(void)
{
    for (;;) {
        uint16_t di, si, ax;
        int dx, cf;

        wait_frames(5);
        if (!rw(V(boosters)))
            break;
        wb(V(sound_request), 0x12);
        di = rw(V(td_txt_boosters));
        si = (uint16_t)(di + rw(V(booster_col)));
        ax = rw(V(boosters));
        for (dx = 1; dx >= 0 && ax; dx--) {     /* at most two digits */
            wb(--si, (uint8_t)((ax & 0x0F) + '0'));
            ax >>= 4;
        }
        wb(--si, ' ');
        show_text(di);
        add_booster();
        cf = 0;
        wb(V(boosters), bcd_sbb(rb(V(boosters)), rb(V(booster_step)), &cf));
        wb((uint16_t)(V(boosters) + 1),
           bcd_sbb(rb((uint16_t)(V(boosters) + 1)), rb((uint16_t)(V(booster_step) + 1)), &cf));
    }
    wait_frames(qol_frames(QOL_BONUS, 0x23, 0x0A));
    next_player();
}

/* "ball lost" for 46h double frames (QOL_NEXT_BALL: 23h), then (unless
 * tilted) the bonus, the held bonus again, the boosters; then the next
 * player */
static void ball_lost_count(void)
{
    ww(V(nudge_push), 0);
    lights_off_player();
    ww(V(wait_count), (uint16_t)qol_frames(QOL_NEXT_BALL, 0x46, 0x23));
    wb(V(flippers_off), 0xFF);
    wb(V(ball_held), 0xFF);
    show_text(rw(V(td_txt_ball_lost)));
    do {
        wait_frames(1);
        wait_frame();
        ww(V(wait_count), (uint16_t)(rw(V(wait_count)) - 1));
    } while (rw(V(wait_count)));
    if (rb(V(tilted))) {
        next_player();
        return;
    }
    for (;;) {
        count_bonus();
        show_bonus_count();
        wait_frames(qol_frames(QOL_BONUS, 0x14, 0));
        if (!rb(V(bonus_held)))
            break;
        show_text(V(txt_bonus_held));
        wait_frames(qol_frames(QOL_BONUS, 0x46, 0x28));
        wb(V(bonus_held), 0);
        copy_words((uint16_t)(rw(V(player_rec)) + 8), V(bonus_saved), 4);
    }
    ww(V(boosters), rw((uint16_t)(rw(V(player_rec)) + 0x10)));
    count_boosters();
}

/* the first full lock of td_locks puts its ball back into play (position
 * and speed from its hole), ball_locked stays set while another lock is
 * full; state 5 */
static void release_locked_ball(void)
{
    uint16_t bx, h, lamp;
    uint8_t any = 0;

    for (bx = rw(V(td_locks)); !(rw((uint16_t)(bx + 4)) & 0x8000); bx = (uint16_t)(bx + 6))
        ;
    ww((uint16_t)(bx + 4), 0);
    if ((lamp = rw((uint16_t)(bx + 2))) != 0) {
        wb((uint16_t)(lamp + 1), (uint8_t)(rb((uint16_t)(lamp + 1)) & ~rb(V(player_bit))));
        wb(lamp, rb(lamp) & 0xEB);
    }
    h = rw(bx);
    ww(V(ball_vx), rw((uint16_t)(h + 0x0A)));
    ww(V(ball_vy), rw((uint16_t)(h + 0x0C)));
    ww(V(ball_x_hi), (uint16_t)(rw((uint16_t)(h + 0x0E)) << 2));
    ww(V(ball_y_hi), (uint16_t)(rw((uint16_t)(h + 0x10)) << 2));
    wb(V(locks_lit), 0);
    for (bx = rw(V(td_locks));; bx = (uint16_t)(bx + 6)) {
        uint16_t full = rw((uint16_t)(bx + 4));
        if (full == 0)
            continue;
        if (!(full & 0x8000))
            break;                      /* past the last lock */
        any = 0xFF;
    }
    wb(V(ball_locked), any);
    wb(V(flippers_off), 0);
    wb(V(ball_held), 0);
    wb(V(lock_pending), 0);
    ww(V(ball_obj), rw(V(ball_obj)) | 1);
    wb(V(sprites), rb(V(sprites)) | 3);
    wb(V(ball_drained), 0);
    ww(V(game_state), 5);
    wb(V(jingle_request), 0);
}

/* state 6: the ball is lost: 17 more frames of the table scrolling down
 * after it, 19h frames of silence (none with QOL_NEXT_BALL), then a locked
 * ball back into play, or the bonus and the next player */
void st_ball_lost(void)
{
    int i;

    checkpoint("st_ball_lost");
    wb(V(jingle_request), 0x80);
    ww(V(ball_obj), rw(V(ball_obj)) & 0xFFFE);
    wb(V(sprites), (uint8_t)((rb(V(sprites)) & 0xFE) | 2));
    wb(V(flippers_off), 0xFF);
    for (i = 0; i < 17; i++) {
        ball_frame();
        scroll_step();
    }
    copy_words(V(bonus_saved), (uint16_t)(rw(V(player_rec)) + 8), 4);
    wait_frames(qol_frames(QOL_NEXT_BALL, 0x19, 0));
    wb(V(jingle_request), 7);
    if (!rb(V(tilted)) && rb(V(ball_locked)))
        release_locked_ball();
    else
        ball_lost_count();
}
