/* pd_events.c - the event stack: sequences push event objects onto it to
 * be armed or fired (PD.ASM run_event).  See pd.h.
 *
 * A sequence (hints at run_event): type 2Ah, a list of words up to FFFFh,
 * all of them pushed; types 28h and 29h, a step per player (+2..+9) and a
 * word per step from +0Ah.  A word is an event object; +8000h arms it,
 * +4000h runs that sequence instead.  The stack (event_sp) holds pairs:
 * FFFFh (arm) or 0 (fire), and the object. */
#include "pd.h"

static uint16_t push_event(uint16_t si, uint16_t what, uint16_t obj)
{
    ww(si, what);
    ww((uint16_t)(si + 2), obj);
    return (uint16_t)(si + 4);
}

/* the hurry-up's value from its start (4 words) when td_hurry is armed */
static void start_hurry(void)
{
    int i;
    for (i = 0; i < 8; i += 2)
        ww((uint16_t)(V(hurry_value) + i), rw((uint16_t)(V(hurry_base) + i)));
}

void run_event(uint16_t di)
{
    uint16_t si = rw(V(event_sp)), cx, bx, step, a;
    uint8_t bit;

    ww(V(score_copy), 0);
    ww(V(event_arm), 0);
    ww(V(event_player_bit), 0);
    wb(V(event_player_bit), rb(V(player_bit)));
    bit = rb(V(event_player_bit));

    if (rb(di) == 0x2A) {
        for (di = (uint16_t)(di + 2);; ) {
            cx = rw(di);
            di = (uint16_t)(di + 2);
            if (cx == 0xFFFF)
                break;
            if (cx & 0x4000) {
                ww(V(event_sp), si);
                run_event(cx & 0xBFFF);
                si = rw(V(event_sp));   /* the nested sequence's pushes stay */
                continue;
            }
            if (cx & 0x8000) {
                bx = cx & 0x7FFF;
                wb((uint16_t)(bx + 0x22), rb((uint16_t)(bx + 0x22)) | bit);
                if (bx == rw(V(td_hurry)))
                    start_hurry();
                si = push_event(si, 0xFFFF, bx);
                continue;
            }
            bx = cx;
            if (rb(bx) != 0x1F) {
                if (!(rb((uint16_t)(bx + 0x22)) & bit))
                    continue;           /* not armed for the player */
                if (rb((uint16_t)(bx + 1)) != 0xFE)
                    wb((uint16_t)(bx + 0x22), (uint8_t)(rb((uint16_t)(bx + 0x22)) & ~bit));
            }
            si = push_event(si, 0, bx);
        }
        ww(V(event_sp), si);
        return;
    }

    /* types 28h, 29h: the player's step */
    step = (uint16_t)(di + rb(V(player)) + 2);
    for (;;) {
        ww(V(event_arm), 0);
        a = (uint8_t)(rb(step) << 1);
        wb(V(event_arm), (uint8_t)a);
        cx = rw((uint16_t)(di + a + 0x0A));
        if (cx == 0xFFFF)
            return;
        if (cx == 0xFFFE)
            wb(step, 0);
        else if (cx == 0xFFFD)
            wb(step, (uint8_t)(rb(step) - 1));
        else if (cx == 0xFFFC)
            wb(step, rb((uint16_t)(di + a + 0x0C)));
        else
            break;
    }
    ww(V(event_arm), 0);
    if (rb(di) != 0x29)
        wb(step, (uint8_t)(rb(step) + 1));
    if (cx & 0x4000) {
        ww(V(event_sp), si);
        run_event(cx & 0xBFFF);
        return;
    }
    if (cx & 0x8000) {
        ww(V(event_arm), 0xFFFF);
        bx = cx & 0x7FFF;
        wb((uint16_t)(bx + 0x22), rb((uint16_t)(bx + 0x22)) | bit);
    } else {
        bx = cx;
        if (rb(bx) == 0x1E) {
            if (!(rb((uint16_t)(bx + 0x22)) & bit)) {
                wb(step, (uint8_t)(rb(step) - 1));      /* not armed: the step again next time */
                return;
            }
            if (rb((uint16_t)(bx + 1)) != 0xFE)
                wb((uint16_t)(bx + 0x22), (uint8_t)(rb((uint16_t)(bx + 0x22)) & ~bit));
        } else if (rb(bx) != 0x1F) {
            return;
        }
    }
    si = push_event(si, rw(V(event_arm)), bx);
    ww(V(event_sp), si);
}
