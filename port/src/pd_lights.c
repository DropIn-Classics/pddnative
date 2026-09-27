/* pd_lights.c - the table's lamps: blinking, what the current player's
 * lamps show into the palette, Ignition's timed lights, the chains (PD.ASM
 * blink_lights to light_chains, light_on, light_off, lights_off_all).
 * See pd.h.
 *
 * A lamp (8 bytes; hints at update_lights): +0 flags, +1 lit (a bit per
 * player), +2 blink phase, +3 the state shown with flag 2, +4 its light
 * (a record of `lights`, whose +22h is the palette slot), +6 the next lamp
 * of its group.  A group's first word is its first lamp; the groups'
 * lists (td_lights ...) end with a negative word. */
#include "pd.h"

/* the light's palette slot gets its "on" (or "off") colours from TDATA */
static void light_set(uint16_t light, uint16_t pal)
{
    uint16_t slot = (uint16_t)(3 * rw((uint16_t)(light + 0x22))), src = (uint16_t)(rw(pal) + slot);
    int i;

    for (i = 0; i < 3; i++)
        wb((uint16_t)(V(light_palette) + slot + i), frb(seg_tdata, (uint16_t)(src + i)));
}

void light_on(uint16_t light)
{
    light_set(light, V(lights_on_pal));
}

void light_off(uint16_t light)
{
    light_set(light, V(lights_off_pal));
}

/* every lamp out for every player, flag 2 cleared */
void lights_off_all(void)
{
    uint16_t bx = rw(V(td_lights)), g;

    while (!((g = rw(bx)) & 0x8000)) {
        uint16_t si = rw(g);
        bx = (uint16_t)(bx + 2);
        for (;;) {
            wb(si, rb(si) & 0xFD);
            wb((uint16_t)(si + 1), 0);
            if (!rw((uint16_t)(si + 6)))
                break;
            si = rw((uint16_t)(si + 6));
        }
    }
}

/* each group every +17h frames (+16h the counter): lamps with flag 40h
 * change their blink phase; with 20h a chase moves on (+18h its phase).
 * Outside the attract mode only groups whose lamps have flag 4. */
static void blink_lights(void)
{
    uint16_t di = rw(V(td_lights)), si, ax, bx;

    while (!((si = rw(di)) & 0x8000)) {
        uint8_t cl, dl;
        di = (uint16_t)(di + 2);
        ax = rw(si);
        if (rb((uint16_t)(si + 0x16)) != 0) {
            wb((uint16_t)(si + 0x16), (uint8_t)(rb((uint16_t)(si + 0x16)) - 1));
            continue;
        }
        wb((uint16_t)(si + 0x16), rb((uint16_t)(si + 0x17)));
        for (;;) {
            bx = ax;
            cl = rb(bx);
            if (!rb(V(attract_mode)) && !(cl & 4))
                break;
            if (cl & 0x40) {
                wb((uint16_t)(bx + 2), (uint8_t)~rb((uint16_t)(bx + 2)));
                ax = rw((uint16_t)(bx + 6));
                if (ax)
                    continue;
                wb((uint16_t)(si + 0x16), (uint8_t)(rb((uint16_t)(si + 0x16)) - 1));
                break;
            }
            if (cl & 0x20) {
                dl = rb((uint16_t)(si + 0x18));
                for (;;) {
                    bx = ax;
                    if (dl != rb((uint16_t)(bx + 2))) {
                        wb((uint16_t)(bx + 2), dl);
                        break;
                    }
                    ax = rw((uint16_t)(bx + 6));
                    if (!ax) {
                        wb((uint16_t)(si + 0x18), (uint8_t)~rb((uint16_t)(si + 0x18)));
                        break;
                    }
                }
            }
            break;
        }
    }
}

/* the current player's lamps into the palette (player record +18h keeps a
 * byte per lamp: what it shows), at most ten changes a frame */
static void update_lights(void)
{
    uint16_t di = rw(V(td_lights)), si = (uint16_t)(rw(V(player_rec)) + 0x18), g, bx;
    uint8_t dl = rb(V(player_bit)), dh, f;
    int changes = 10;

    while (!((g = rw(di)) & 0x8000)) {
        di = (uint16_t)(di + 2);
        bx = rw(g);
        if (!bx)
            continue;
        for (;; bx = rw((uint16_t)(bx + 6))) {
            f = rb(bx);
            if (f & 2) {
                dh = rb((uint16_t)(bx + 3));
            } else {
                dh = rb((uint16_t)(bx + 1)) & dl;
                if (f & 0x10) {
                    if (!dh)
                        goto off;
                    dh ^= dl;
                }
                if (rb(V(attract_mode)) || (f & 4))
                    dh |= rb((uint16_t)(bx + 2));
            }
            if (dh && !rb(V(tilted))) {
                if (!rb(si)) {
                    wb(si, 0xFF);
                    if (rw((uint16_t)(bx + 4)))
                        light_on(rw((uint16_t)(bx + 4)));
                    si++;
                    if (--changes == 0)
                        return;
                } else {
                    si++;
                }
            } else {
            off:
                if (rb(si)) {
                    wb(si, 0);
                    if (rw((uint16_t)(bx + 4)))
                        light_off(rw((uint16_t)(bx + 4)));
                    si++;
                    if (--changes == 0)
                        return;
                } else {
                    si++;
                }
            }
            if (!rw((uint16_t)(bx + 6)))
                break;
        }
    }
}

/* the idle tour's lamps (tour_lamps): each shows its blink phase, at most
 * ten a frame */
static void idle_update_lights(void)
{
    uint16_t di = V(tour_lamps), g, bx;
    int n = 10;

    while (!((g = rw(di)) & 0x8000)) {
        di = (uint16_t)(di + 2);
        bx = rw(g);
        if (!bx)
            continue;
        for (;;) {
            uint16_t light = rw((uint16_t)(bx + 4));
            if (light) {
                if (rb((uint16_t)(bx + 2)))
                    light_on(light);
                else
                    light_off(light);
            }
            if (--n == 0)
                return;
            if (!rw((uint16_t)(bx + 6)))
                break;
            bx = rw((uint16_t)(bx + 6));
        }
    }
}

/* Ignition only: each timed light record of td_ign_timers (+0 counter,
 * +2 its period, +4 a group, +6 a per-player count, +8 sound and jingle):
 * when the counter runs out, the last lit lamp of the lit run at the
 * group's start goes out (with the sound), and the player's count drops */
static void ign_timers(void)
{
    uint16_t bx, si, cx, di, bp;
    uint8_t dl;

    if (rb(V(table_num)) != 0)
        return;
    for (bx = rw(V(td_ign_timers));; ) {
        cx = rw(bx);
        bx = (uint16_t)(bx + 2);
        if (cx & 0x8000)
            return;
        si = cx;
        if (rw(si) == 0) {
            ww(si, rw((uint16_t)(si + 2)));
            bp = V(lamp_stack);
            cx = rw((uint16_t)(si + 4));
            if (cx) {
                cx = rw(cx);
                for (;;) {              /* the lit lamps from the first on */
                    di = cx;
                    dl = rb(V(player_bit));
                    if (!(rb((uint16_t)(di + 1)) & dl))
                        break;
                    ww(bp, di);
                    bp = (uint16_t)(bp + 2);
                    cx = rw((uint16_t)(di + 6));
                    if (!cx)
                        break;
                }
                bp = (uint16_t)(bp - 2);
                cx = rw(bp);
                if (cx) {
                    dl = rb(V(player_bit));
                    if (rb((uint16_t)(cx + 1)) & dl) {
                        uint16_t snd = rw((uint16_t)(si + 8));
                        wb((uint16_t)(cx + 1), (uint8_t)(rb((uint16_t)(cx + 1)) & ~dl));
                        if (snd) {
                            wb(V(sound_request), (uint8_t)(snd >> 8));
                            wb(V(jingle_request), (uint8_t)snd);
                        }
                    }
                }
            }
            cx = rw((uint16_t)(si + 6));
            if (cx) {
                uint16_t c = (uint16_t)(cx + 2 + rb(V(player)));
                if (rb(c))
                    wb(c, (uint8_t)(rb(c) - 1));
            }
        }
        ww(si, (uint16_t)(rw(si) - 1));
    }
}

/* td_chains' groups: the first lamp not lit (for the player) blinks, the
 * ones after it are cleared */
static void light_chains(void)
{
    uint16_t di = rw(V(td_chains)), g, si;
    uint8_t bit;

    while (!((g = rw(di)) & 0x8000)) {
        di = (uint16_t)(di + 2);
        si = rw(g);
        while (rb((uint16_t)(si + 1)) & rb(V(player_bit))) {
            si = rw((uint16_t)(si + 6));
            if (!si)
                goto next;
        }
        wb(si, rb(si) | 0x40);
        while ((si = rw((uint16_t)(si + 6))) != 0) {
            wb(si, rb(si) & 0xBF);
            bit = rb(V(player_bit));
            wb((uint16_t)(si + 1), (uint8_t)(rb((uint16_t)(si + 1)) & ~bit));
            wb((uint16_t)(si + 2), 0);
        }
    next:;
    }
}

void lights_frame(void)
{
    blink_lights();
    update_lights();
    ign_timers();
    light_chains();
}

void idle_lights(void)
{
    blink_lights();
    idle_update_lights();
}
