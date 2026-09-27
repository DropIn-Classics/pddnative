/* pd_ball.c - the ball: a frame of it (ball_frame), the physics step
 * (ball_step) through the flippers' shapes and the collision map, the
 * bounce and its fixed-point arithmetic, the flippers' movement and
 * gravity (PD.ASM from ball_frame, flipper_sweep to bounce_response).
 * See pd.h.
 *
 * The arithmetic is the original's to the bit (the 24-bit speeds with a
 * sign byte, the shift-and-add multiplications, the division with its
 * lost carry), so that a run stays the same as the original's. */
#include "pd.h"

/* ---- the arithmetic (mul_fix .. div_s) */

/* DX times the factor at `f` (a word and a sign byte after it; the word 1
 * stands for 10000h): bits 15..30 of the product, signed */
static uint16_t mul_fix(uint16_t dx, uint16_t f)
{
    uint16_t cx = rw(f), bx = 0;
    uint8_t sign = rb((uint16_t)(f + 2));
    int i, cf;

    if (cx == 1) {
        cx = (uint16_t)(dx << 1);
    } else {
        if (dx & 0x8000) {
            dx = (uint16_t)-dx;
            sign = (uint8_t)~sign;
        }
        for (i = 0; i < 16; i++) {
            cf = bx >> 15;
            bx = (uint16_t)(bx << 1);
            if (cx & 0x8000) {
                cx = (uint16_t)(cx << 1 | cf);
                if ((uint32_t)bx + dx > 0xFFFF)
                    cx++;
                bx = (uint16_t)(bx + dx);
            } else {
                cx = (uint16_t)(cx << 1 | cf);
            }
        }
        cx = (uint16_t)(cx << 1 | bx >> 15);
    }
    return (sign & 0x80) ? (uint16_t)-cx : cx;
}

/* the low word of CX * DX */
static uint16_t mul_u16(uint16_t cx, uint16_t dx)
{
    return (uint16_t)((uint32_t)cx * dx);
}

static uint16_t mul_s16(uint16_t cx, uint16_t dx)
{
    int neg = 0;
    uint16_t bx;

    if (cx & 0x8000) {
        cx = (uint16_t)-cx;
        neg ^= 1;
    }
    if (dx & 0x8000) {
        dx = (uint16_t)-dx;
        neg ^= 1;
    }
    bx = mul_u16(cx, dx);
    return neg ? (uint16_t)-bx : bx;
}

/* |BX| shifted left by `shift` + 1 over |DX|, 16 quotient bits (div_u).
 * The carry out of the remainder's shift is lost as in the original. */
static uint16_t div_u(uint16_t bx, uint16_t dx, uint8_t shift)
{
    uint8_t al = 0, n;
    uint16_t cx;
    int cf, out;

    if (bx & 0x8000)
        bx = (uint16_t)-bx;
    if (!(dx & 0x8000))
        dx = (uint16_t)-dx;
    for (; shift; shift--) {
        cf = bx >> 15;
        bx = (uint16_t)(bx << 1);
        al = (uint8_t)(al << 1 | cf);
    }
    cf = bx >> 15;
    cx = (uint16_t)(bx << 1);
    bx = al;
    for (n = 0x10; n; n--) {
        wb(V(div_count), n);
        bx = (uint16_t)(bx << 1 | cf);
        cf = (uint32_t)bx + dx > 0xFFFF;
        if (cf)
            bx = (uint16_t)(bx + dx);
        out = cx >> 15;
        cx = (uint16_t)(cx << 1 | cf);
        cf = out;
    }
    return cx;
}

static uint16_t div_s(uint16_t bx, uint16_t dx, uint8_t shift)
{
    uint16_t cx = div_u(bx, dx, shift);
    return ((dx ^ bx) & 0x8000) ? (uint16_t)-cx : cx;
}

/* a speed: DX with CL as its sign byte (24 bits) */
typedef struct {
    uint16_t dx;
    uint8_t cl;
} Speed;

static Speed speed_of(uint16_t v)
{
    Speed s = {v, (uint8_t)((v & 0x8000) ? 0xFF : 0)};
    return s;
}

/* += the signed word at `w` (add_s16) */
static void add_s16(Speed *s, uint16_t w)
{
    uint16_t v = rw(w);
    uint32_t sum = (uint32_t)s->dx + v;
    s->dx = (uint16_t)sum;
    s->cl = (uint8_t)(s->cl + ((v & 0x8000) ? 0xFF : 0) + (sum >> 16));
}

/* -= the signed word at `w` (sub_s16) */
static void sub_s16(Speed *s, uint16_t w)
{
    uint16_t v = rw(w);
    int borrow = s->dx < v;
    s->dx = (uint16_t)(s->dx - v);
    s->cl = (uint8_t)(s->cl - ((v & 0x8000) ? 0xFF : 0) - borrow);
}

/* DX / 4 (1..3 give 1), the sign from it, then -= the word at `w` */
static void quarter_sub_s16(Speed *s, uint16_t w)
{
    uint8_t low = (uint8_t)s->dx;

    s->dx = (uint16_t)((int16_t)s->dx >> 2);
    if (s->dx == 0 && (low & 3))
        s->dx = 1;
    s->cl = (uint8_t)((s->dx & 0x8000) ? 0xFF : 0);
    sub_s16(s, w);
}

/* DX kept within -0C80h..0C80h by the 24-bit value (the sign byte stays) */
static void clamp_speed(Speed *s)
{
    if (!(s->cl & 0x80)) {
        int borrow = s->dx > 0x0C80;
        if ((uint8_t)(0 - s->cl - borrow) & 0x80)
            s->dx = 0x0C80;
    } else {
        int carry = (uint32_t)s->dx + 0x0C80 > 0xFFFF;
        if ((uint8_t)(s->cl + carry) & 0x80)
            s->dx = 0xF380;
    }
}

/* the quarter-wave table (sine_table, a far pointer): the magnitude for
 * angle a (100h a turn; 40h gives 1, which mul_fix takes as 10000h) */
static uint16_t sine(uint8_t a)
{
    uint16_t seg = rw((uint16_t)(V(sine_table) + 2)), off = rw(V(sine_table));

    if (a & 0x40) {
        a = (uint8_t)(-(a & 0x3F));
        if (a == 0)
            return 1;
    }
    return frw(seg, (uint16_t)(off + 2 * (a & 0x3F)));
}

/* ---- the collision map */

/* the row index at BSS:0 for the map at the far pointer `map` (lower_map
 * or upper_map): per row the offset of the first half's x array, the
 * pixels of both halves */
void index_map(uint16_t map)
{
    uint16_t seg = rw((uint16_t)(map + 2)), bx = rw(map), di;
    int half;

    for (di = 0; di < 0x800;) {
        for (half = 0; half < 2; half++) {
            uint8_t n = 0;
            if (frb(seg, bx) == 0xFF) {
                bx++;
            } else {                    /* map_run: up to the byte with bit 7 */
                do
                    n++;
                while (!(frb(seg, bx++) & 0x80));
            }
            if (half == 0) {
                bx = (uint16_t)(bx + n);
                fww(seg, di, bx);
                fwb(seg, (uint16_t)(di + 2), n);
                di = (uint16_t)(di + 3);
                bx = (uint16_t)(bx + n);
            } else {
                fwb(seg, di++, n);
                bx = (uint16_t)(bx + 2 * n);
            }
        }
    }
}

/* the map at x, y (y < 512): 1 with the surface and its angle when the
 * pixel is one of the row's */
static int map_test(uint16_t x, uint16_t y, uint8_t *surface, uint8_t *angle)
{
    uint16_t e = (uint16_t)(4 * y), bx = frw(seg_bss, e);
    uint8_t n1 = frb(seg_bss, (uint16_t)(e + 2)), n = n1, k;

    if (x & 0x100) {
        n = frb(seg_bss, (uint16_t)(e + 3));
        bx = (uint16_t)(bx + n1 + 2 * n);
    }
    if (n == 0)
        return 0;
    for (k = n; frb(seg_bss, bx) != (uint8_t)x; bx++)
        if (--k == 0)
            return 0;
    *angle = frb(seg_bss, (uint16_t)(bx - n));
    *surface = frb(seg_bss, (uint16_t)(bx - 2 * n)) & 0x7F;
    return 1;
}

/* the angle byte of a 1Fh pixel: gravity, the level switch, or an event
 * number (returned; FFh after a code) */
static uint8_t map_code(uint8_t a)
{
    switch (a) {
    case 0xFC:
        ww(V(gravity), 0x10);
        return 0xFF;
    case 0xFE:
        ww(V(lanes_a), rw(V(td_lanes_a_upper)));
        ww(V(lanes_b), rw(V(td_lanes_b_upper)));
        ww(V(hit_rects), rw(V(td_rects_upper)));
        index_map(V(upper_map));
        wb(V(ball_level), 0xFF);
        return 0xFF;
    case 0xFD:
        ww(V(lanes_a), rw(V(td_lanes_a_lower)));
        ww(V(lanes_b), rw(V(td_lanes_b_lower)));
        ww(V(hit_rects), rw(V(td_rects_lower)));
        index_map(V(lower_map));
        wb(V(ball_level), 0x80);
        return 0xFF;
    case 0xFB:
        ww(V(gravity), 9);
        return 0xFF;
    }
    return a;
}

static uint8_t ror8(uint8_t v, int n)
{
    return (uint8_t)(v >> n | v << (8 - n));
}

/* a pixel of surface xFh: 1 when it acts as a wall (a one-way passage from
 * its closed side: surface 6); a 1Fh line hands its angle byte to map_code */
static int map_special(uint8_t angle)
{
    uint8_t s = rb(V(surface)) & 0xF0;

    if (s == 0) {
        uint8_t dir = rb(V(surface_angle));
        if (!(rb(V(map_flags)) & 1))
            return 0;
        wb(V(surface), 6);
        if (dir >= 3) {                 /* a y step: its bit and the angle turned */
            angle = (uint8_t)(angle - 0x40);
            dir = ror8(dir, 2);
        }
        if (!((ror8(dir, 1) ^ angle) & 0x80))
            return 1;
        wb(V(map_flags), rb(V(map_flags)) & 0xFE);
        return 0;
    }
    if (s == 0x10 && (rb(V(map_flags)) & 2)) {
        uint8_t ev = map_code(angle);
        wb(V(map_flags), rb(V(map_flags)) & 0xFD);
        wb(V(map_event), ev);
    }
    return 0;
}

/* ---- the flippers' shapes */

/* a shape record from its x field (flipper_shapes points there): +0 x,
 * +2 y, +4 width (a byte; +5 flags), +6 height, +8 a word per row from the
 * second on (where its columns start), the columns from +8 + 2 * height,
 * a second array (angles) after them.  The row of dy: its first column's
 * address and count - 1, or 0 when the row is longer than 256. */
static int shape_row(uint16_t p, uint16_t dy, uint16_t *row, uint8_t *last)
{
    uint16_t h = rw((uint16_t)(p + 6));
    uint16_t cur = dy ? rw((uint16_t)(p + 6 + 2 * dy)) : 0;
    uint16_t len = (uint16_t)(rw((uint16_t)(p + 8 + 2 * dy)) - cur - 1);

    if (len & 0xFF00)
        return 0;
    *row = (uint16_t)(p + 8 + 2 * h + cur);
    *last = (uint8_t)len;
    return 1;
}

/* the angle array's entry for the column at `col` */
static uint8_t shape_angle(uint16_t p, uint16_t col)
{
    uint16_t h = rw((uint16_t)(p + 6));
    return rb((uint16_t)(col + rw((uint16_t)(p + 6 + 2 * h))));
}

/* x, y inside the shape (between its row's first and last column): 1 and
 * the angle of the nearest column */
static int shape_test(uint16_t p, uint16_t x, uint16_t y, uint8_t *angle)
{
    uint16_t px = rw(p), py = rw((uint16_t)(p + 2)), dy, row, col;
    uint8_t dx, last, best = 0xFF;
    int i, count, bi = 0;

    if ((uint16_t)(x - px) & 0xFF00)
        return 0;
    dx = (uint8_t)(x - px);
    dy = (uint16_t)(y - py);
    if (dy & 0x8000) {
        if (y < py)
            return 0;
        *angle = 0;                     /* far below: taken as inside */
        return 1;
    }
    if (rb((uint16_t)(p + 4)) < dx || dy >= rw((uint16_t)(p + 6)))
        return 0;
    if (!shape_row(p, dy, &row, &last))
        return 0;
    if (dx < rb(row) || rb((uint16_t)(row + last)) < dx)
        return 0;
    count = last + 1;
    for (i = 0; i < count; i++) {
        uint8_t v = rb((uint16_t)(row + i)), d = v >= dx ? (uint8_t)(v - dx) : (uint8_t)(dx - v);
        if (d < best) {
            best = d;
            bi = i;
        }
    }
    col = (uint16_t)(row + count - (uint8_t)(count - bi));
    *angle = shape_angle(p, col);
    return 1;
}

/* the ball's point pushed out of the flipper's shape for `frame`, a pixel
 * at a time along the angle of the nearest column (phys_a: 1, the angle) */
static void flipper_hit(uint8_t frame, uint16_t *x, uint16_t *y)
{
    uint16_t shape = rw((uint16_t)(V(flipper_shapes) + (uint8_t)(2 * frame)));
    uint8_t a;

    while (shape_test(shape, *x, *y, &a)) {
        uint8_t t = (uint8_t)(a - 0x20);
        wb((uint16_t)(V(phys_a) + 1), a);
        if (t & 0x40)
            *y = (uint16_t)(*y + ((t & 0x80) ? 1 : -1));
        else
            *x = (uint16_t)(*x + ((t & 0x80) ? 1 : -1));
        wb(V(phys_a), 1);
    }
}

/* the drawn frame (at f + 1) stepped toward the wanted one (f), the ball
 * tested at each */
static void flipper_sweep(uint16_t f, uint16_t *x, uint16_t *y)
{
    ww(V(phys_a), 0);
    for (;;) {
        uint8_t to = rb(f), fr = rb((uint16_t)(f + 1));
        if (to == fr)
            return;
        fr = (uint8_t)(fr + (to < fr ? -1 : 1));
        wb((uint16_t)(f + 1), fr);
        flipper_hit(fr, x, y);
    }
}

/* a flipper shape against the ball's next pixel (ball_collide's own test:
 * only the listed columns are its edge): 1 with the surface and angle */
static int shape_edge(uint16_t p, uint16_t x, uint16_t y, int right, uint8_t *angle)
{
    uint16_t dy, row;
    uint8_t dx, flags, last;
    int i;

    if ((uint16_t)(x - rw(p)) & 0xFF00)
        return 0;
    dx = (uint8_t)(x - rw(p));
    dy = (uint16_t)(y - rw((uint16_t)(p + 2)));
    if (dy & 0x8000)
        return 0;
    if (rb((uint16_t)(p + 4)) < dx || dy >= rw((uint16_t)(p + 6)))
        return 0;
    flags = rb((uint16_t)(p + 5));
    wb(V(surface), flags);
    if (flags & 0x80) {                 /* the whole box, straight up or down */
        wb(V(surface), flags & 0x3F);
        *angle = (uint8_t)((flags & 0x40) << 1 | 0x40);
        return 1;
    }
    if (!shape_row(p, dy, &row, &last))
        return 0;
    for (i = 0; i <= last; i++)
        if (rb((uint16_t)(row + i)) == dx) {
            uint8_t a = shape_angle(p, (uint16_t)(row + i));
            wb(V(surface), right ? 2 : 1);
            *angle = right ? (uint8_t)-a : a;
            return 1;
        }
    return 0;
}

enum { FREE, BLOCKED, HIT };

/* the ball's next pixel from x, y in the direction `code` (bit 0: y, bit
 * 1: backwards) against the flippers and the map.  FREE: x, y moved.
 * BLOCKED: the direction's bit is in *cl (it hit there before).  HIT: the
 * surface and surface_angle set, the direction's bit added to *cl.  (The
 * original also walks a list of shapes here that nothing reaches.) */
static int ball_collide(uint16_t *x, uint16_t *y, uint8_t code, uint8_t *cl)
{
    uint16_t nx = *x, ny = *y, mirror;
    uint8_t mv, s, a, angle;
    int f;

    wb(V(phys_c), 0);
    if (!(code & 1)) {
        uint8_t sx = rb(V(step_sx)), h = (uint8_t)((int8_t)sx >> 1);
        if (!(code & 2)) {
            nx = (uint16_t)(nx + (int8_t)sx);
            mv = (uint8_t)(h + 2);
        } else {
            nx = (uint16_t)(nx - (int8_t)sx);
            mv = (uint8_t)(~h + 2);
        }
    } else {
        uint8_t sy = rb(V(step_sy)), h = (uint8_t)((int8_t)sy >> 1);
        if (!(code & 2)) {
            ny = (uint16_t)(ny + (int8_t)sy);
            mv = (uint8_t)(h + 2);
        } else {
            ny = (uint16_t)(ny - (int8_t)sy);
            mv = (uint8_t)(~h + 2);
        }
        mv = (uint8_t)(mv << 2);
    }
    wb(V(surface_angle), mv);
    if (mv & *cl)
        return BLOCKED;
    ww(V(phys_a), nx);
    ww(V(phys_b), ny);

    for (f = 1; f <= 2; f++) {
        uint8_t frame = rb(f == 1 ? V(lflipper_frame) : V(rflipper_frame));
        uint16_t shape = rw((uint16_t)(V(flipper_shapes) + 2 * frame)), sx = nx;
        if (f == 2) {
            mirror = rw(V(flipper_mirror_x));
            if (mirror < nx)
                continue;
            sx = (uint16_t)(mirror - nx);
        }
        if (shape_edge(shape, sx, ny, f == 2, &angle))
            goto hit;
        wb(V(phys_c), (uint8_t)(rb(V(phys_c)) + 1));
    }

    if ((ny >> 8) >= 2 || !map_test(nx, ny, &s, &a)) {
        wb(V(map_flags), 0xFF);
        goto free;
    }
    wb(V(surface), s);
    angle = a;
    if ((s & 0x0F) == 0x0F && !map_special(a))
        goto free;
hit:
    *cl |= rb(V(surface_angle));
    wb(V(surface_angle), angle);
    return HIT;
free:
    *x = nx;
    *y = ny;
    return FREE;
}

/* ---- the response */

/* the surface's five constants into surface_params_v; kickers score; a
 * flipper's speed at the hit point into surface_vx/vy */
static void surface_params(void)
{
    uint8_t s = rb(V(surface)), t = s & 0x0F;
    uint16_t bx = (uint16_t)(V(surface_table) + 10 * (t ? t - 1 : 0));
    int i;

    ww(V(surface_vx), 0);
    ww(V(surface_vy), 0);
    wb(V(surface_type), s);
    for (i = 0; i < 5; i++)
        ww((uint16_t)(V(surface_params_v) + 2 * i), rw((uint16_t)(bx + 2 * i)));
    if (s == 1) {
        uint16_t dx = (uint16_t)(rw(V(hit_x)) - rw(V(lflipper_pivot_x))), cx = rw(V(lflipper));
        if (dx & 0x8000)
            return;
        ww(V(surface_vy), (uint16_t)-mul_s16(cx, dx));
        ww(V(surface_vx), mul_s16(cx, (uint16_t)(rw(V(hit_y)) - rw(V(lflipper_pivot_y)))));
    } else if (s == 2) {
        uint16_t dx = (uint16_t)(rw(V(hit_x)) - rw(V(rflipper_pivot_x))), cx = rw(V(rflipper));
        if (!(dx & 0x8000))
            return;
        ww(V(surface_vy), mul_s16(cx, dx));
        ww(V(surface_vx), (uint16_t)-mul_s16(cx, (uint16_t)(rw(V(hit_y)) - rw(V(rflipper_pivot_y)))));
    } else if ((s & 0xF0) == 0x10 || (s & 0xF0) == 0x20) {
        uint16_t k = (s & 0xF0) == 0x10 ? V(kicker_score1) : V(kicker_score2);
        uint8_t off;
        if (rb(V(tilted)))
            return;
        wb(V(hit_lane), 0xFF);
        ww(V(hit_object), k);
        off = rb((uint16_t)(k + 1));
        ww(V(hit_score), off ? (uint16_t)(k + off) : 0);
    }
}

static void clear_hit_score(void)
{
    ww(V(hit_score), 0);
    wb(V(hit_lane), 0);
    ww(V(hit_object), 0);
}

/* the ball's speed turned into the surface's frame (along it, across it),
 * the speed across reflected and damped by the surface's constants, the
 * push of flippers, nudge and kickers, and turned back.  `code` is the
 * step's direction (bit 0: y); the position is set to the pixel's edge */
static void bounce(uint8_t code)
{
    uint16_t p = V(surface_params_v), pos, n, t, q, sum, k, cx, bx, w, a2, b2;
    uint8_t a;
    Speed vx, vy;
    int neg;

    if (!(code & 1)) {
        pos = rw(V(ball_x));
        ww(V(ball_x), (uint16_t)((pos & 0xFC00) | (uint8_t)~((int8_t)rb(V(step_sx)) >> 1)));
    } else {
        pos = rw(V(ball_y));
        ww(V(ball_y), (uint16_t)((pos & 0xFC00) | (uint8_t)~((int8_t)rb(V(step_sy)) >> 1)));
    }
    surface_params();
    a = (uint8_t)(~rb(V(surface_angle)) + 0x40);
    ww(V(phys_a), sine((uint8_t)(a + 0x40)));         /* the cosine */
    wb(V(phys_b), (uint8_t)(a + 0x40));
    ww(V(phys_c), sine(a));
    wb(V(phys_c_sign), a);

    vx = speed_of(rw(V(ball_vx)));
    add_s16(&vx, V(surface_vx));
    clamp_speed(&vx);
    vy = speed_of(rw(V(ball_vy)));
    add_s16(&vy, V(surface_vy));
    add_s16(&vy, V(nudge_push));
    clamp_speed(&vy);

    n = (uint16_t)(mul_fix(vx.dx, V(phys_c)) + mul_fix(vy.dx, V(phys_a)));
    t = (uint16_t)(mul_fix(vy.dx, V(phys_c)) - mul_fix(vx.dx, V(phys_a)));
    if (!(t & 0x8000)) {                /* moving away from the surface */
        clear_hit_score();
        return;
    }
    ww(V(bounce_along), n);
    q = div_u(n, t, 4);
    if (!((uint16_t)(q - rw((uint16_t)(p + 8))) & 0x8000) ||
        !((uint16_t)(t - rw((uint16_t)(p + 6))) & 0x8000))
        t = 0;
    ww(V(kick_speed), 0);
    k = 0;
    if (t != 0 && rb(V(hit_lane)) != 0) {
        k = 0xE4A8;
        if ((rb(V(surface)) & 0x0F) == 3) {
            k = ((uint16_t)(0xFC18 - t) & 0x8000) ? 0 : 0xF63C;
        }
    }
    if (k)
        ww(V(kick_speed), k);
    else
        clear_hit_score();

    sum = (uint16_t)(t + rw(V(kick_speed)));
    neg = (sum & 0x8000) != 0;
    if (neg)
        sum = (uint16_t)-sum;
    t = (uint16_t)(sum - div_u(sum, rw((uint16_t)(p + 4)), 8));
    if (neg)
        t = (uint16_t)-t;

    cx = rw(p);
    bx = rw((uint16_t)(p + 2));
    if (!((uint16_t)(t - 0xFC01) & 0x8000)) {
        uint16_t f = (uint16_t)((((uint16_t)-t) >> 6) + 1);
        cx = mul_u16(cx, f);
        bx = mul_u16(bx, f);
    }
    w = (uint16_t)(rw(V(table_push)) + rw(V(nudge_push)) - rw(V(bounce_along)));
    ww(V(table_push), (uint16_t)(rw(V(table_push)) - div_s(w, bx, 8)));
    n = div_s((uint16_t)(rw(V(bounce_along)) + div_s(w, cx, 8)), 0x801, 0x0B);

    a2 = (uint16_t)(mul_fix(n, V(phys_c)) + mul_fix(t, V(phys_a)));
    b2 = (uint16_t)(mul_fix(n, V(phys_a)) - mul_fix(t, V(phys_c)));
    vy.dx = b2;
    quarter_sub_s16(&vy, V(surface_vy));
    sub_s16(&vy, V(nudge_push));
    clamp_speed(&vy);
    ww(V(ball_vy), vy.dx);
    vx.dx = a2;
    quarter_sub_s16(&vx, V(surface_vx));
    clamp_speed(&vx);
    ww(V(ball_vx), vx.dx ? vx.dx : 1);
}

/* the pixels this step goes in x and y; returns their sum */
static uint8_t ball_step_size(void)
{
    uint16_t a;
    int8_t h;

    wb(V(step_err), 0);
    a = (uint16_t)((rw(V(ball_x)) & 0x3FF) + rw(V(ball_vx)));
    h = (int8_t)((int8_t)(a >> 8) >> 2);
    wb(V(step_dx), (uint8_t)(h < 0 ? -h : h));
    wb(V(step_sx), h < 0 ? 0xFF : 1);
    a = (uint16_t)((rw(V(ball_y)) & 0x3FF) + rw(V(ball_vy)));
    h = (int8_t)((int8_t)(a >> 8) >> 2);
    wb(V(step_dy), (uint8_t)(h < 0 ? -h : h));
    wb(V(step_sy), h < 0 ? 0xFF : 1);
    return (uint8_t)(rb(V(step_dx)) + rb(V(step_dy)));
}

/* every surface's response (response_table): bounce, then the steps again;
 * step_count the smaller of what was left and the new sum */
static void bounce_response(uint8_t code)
{
    uint8_t steps;

    bounce(code);
    steps = ball_step_size();
    if (rb(V(step_count)) >= steps)
        wb(V(step_count), steps);
}

/* the hit point, then the surface's routine of response_table */
static void collision_response(uint16_t x, uint16_t y, uint8_t code)
{
    uint8_t t = rb(V(surface)) & 0x0F;
    uint16_t fn;

    ww(V(hit_x), x);
    ww(V(hit_y), y);
    if (t >= 8)
        return;
    fn = rw((uint16_t)(V(response_table) + 2 * t));
    if (fn == V(bounce_response))
        bounce_response(code);
    else
        call_code(fn);
}

/* ---- the flippers and gravity */

void draw_flippers(void)
{
    ww(V(lflipper_last), rw(V(lflipper_angle)));
    ww(V(rflipper_last), rw(V(rflipper_angle)));
    set_sprite_frame((uint16_t)(rb(V(lflipper_frame)) >> 2), 0x20);
    set_sprite_frame((uint16_t)((rb(V(rflipper_frame)) >> 2) + 5), 0x40);
}

/* a flipper (f: +0 angular speed, +2 angle, +4 the frame wanted): pressed
 * speeds it up, released lets it fall back; the frame from the angle */
static void flipper_move(uint16_t f, int pressed)
{
    uint8_t c;
    uint16_t v, q;

    if (pressed) {
        if (!rb(V(lflipper_frame)) && !rb(V(rflipper_frame))) {
            wb(V(sound_request), 3);
            wb(V(flipper_pressed), rb(V(flipper_pressed)) | 0x80);
        }
        c = (uint8_t)(rb(f) - 9);
        if ((int8_t)(c + 0x73) < 0)
            c = 0x8D;
    } else {
        c = (uint8_t)(rb(f) + 6);
    }
    v = (uint16_t)(int8_t)c;
    ww(f, v);
    ww((uint16_t)(f + 2), (uint16_t)(rw((uint16_t)(f + 2)) - v));
    q = (uint16_t)(div_s(rw((uint16_t)(f + 2)), 0x37, 0) - 1);
    if (q & 0x8000) {
        q = 0;
        ww((uint16_t)(f + 2), 0x37);
        ww(f, 0);
    } else if (++q >= 0x10) {
        q = 0x10;
        ww((uint16_t)(f + 2), 0x370);
        ww(f, 0);
    }
    wb((uint16_t)(f + 4), (uint8_t)q);
}


/* a flipper key (a word: key_map's byte, the bit), unless flippers_off */
static int flipper_key(uint16_t key)
{
    uint16_t k = rw(key);
    return rb(V(flippers_off)) != 0xFF && (rb((uint16_t)(V(key_map) + (k & 0xFF))) & (k >> 8));
}

/* the flippers moved by their keys, their sprites; gravity added to the
 * speed; table_push back toward 0 by 1 */
static void flippers_gravity(void)
{
    uint16_t tp;

    flipper_move(V(lflipper), flipper_key(V(key_lflipper)));
    flipper_move(V(rflipper), flipper_key(V(key_rflipper)));
    draw_flippers();
    ww(V(ball_vx), (uint16_t)(rw(V(ball_vx)) + rw(V(gravity_x))));
    ww(V(ball_vy), (uint16_t)(rw(V(ball_vy)) + rw(V(gravity))));
    tp = rw(V(table_push));
    if (tp & 0x8000)
        tp++;
    else if (tp)
        tp--;
    ww(V(table_push), tp);
}

/* ---- the step */

/* one physics step (three a frame): the flippers swept against the ball,
 * the ball walked pixel by pixel toward position + speed (a line through
 * the pixels: step_err), each surface met answered by a bounce; then the
 * position (the pixel walked to, the fraction of position + speed), the
 * flippers and gravity */
void ball_step(void)
{
    uint16_t x, y, x0, y0, m, mx;
    uint8_t steps, cl = 0, code, a, b, e, side = 0, ang = 0, left;
    int r;

    if (rb(V(ball_held)))
        return;
    clear_hit_score();
    steps = ball_step_size();
    x = (uint16_t)((int16_t)rw(V(ball_x_hi)) >> 2);
    y = (uint16_t)((int16_t)rw(V(ball_y_hi)) >> 2);

    flipper_sweep(V(lflipper_frame_to), &x, &y);
    if (rb(V(phys_a))) {
        side = 1;
        ang = rb((uint16_t)(V(phys_a) + 1));
    } else {
        m = rw(V(flipper_mirror_x));
        if (m < x) {
            wb(V(rflipper_frame), rb(V(rflipper_frame_to)));
        } else {
            mx = (uint16_t)(m - x);
            flipper_sweep(V(rflipper_frame_to), &mx, &y);
            x = (uint16_t)(m - mx);
            if (rb(V(phys_a))) {
                side = 2;
                ang = (uint8_t)-rb((uint16_t)(V(phys_a) + 1));
            }
        }
    }
    if (side) {                         /* a flipper swept into the ball */
        wb(V(surface), side);
        wb(V(surface_angle), ang);
        wb(V(phys_c), 0);
        wb(V(step_count), steps);
        collision_response(x, y, 1);
        if (!rb(V(step_count)))
            goto move;
    } else {
        if (!steps)
            goto move;
        wb(V(step_count), steps);
    }

    for (;;) {
        e = rb(V(step_err));
        a = (uint8_t)(e + rb(V(step_dx)));
        if (a & 0x80)
            a = (uint8_t)-a;
        b = (uint8_t)(e - rb(V(step_dy)));
        if (b & 0x80)
            b = (uint8_t)-b;
        code = b < a ? 0 : 1;           /* 0: an x pixel, 1: a y pixel */
        cl &= 0x7F;
        r = ball_collide(&x, &y, code, &cl);
        if (r == HIT) {
            collision_response(x, y, code);
            if (!rb(V(step_count)))
                break;
            continue;
        }
        if (r == BLOCKED) {
            /* the way was hit before: try the other axis, then both
             * together, then the other axis backwards */
            uint8_t c0;
            x0 = x;
            y0 = y;
            code ^= 1;
            if (ball_collide(&x, &y, code, &cl) == FREE) {
                c0 = 0;
                code ^= 1;
                if (ball_collide(&x, &y, code, &c0) == FREE)
                    goto diagonal;
                code ^= 1;
            }
            code ^= 6;
            if (!rb((code & 1) ? V(step_dy) : V(step_dx))) {
                x = x0;
                y = y0;
                if (ball_collide(&x, &y, code, &cl) == FREE) {
                    c0 = 0;
                    code ^= 3;
                    if (ball_collide(&x, &y, code, &c0) == FREE)
                        goto diagonal;
                    code ^= 3;
                }
            }
            /* stuck: this axis given up for the rest of the step */
            x = x0;
            y = y0;
            wb(V(step_err), 0);
            if (!(code & 1)) {
                wb(V(step_dy), 0);
                left = rb(V(step_dx));
            } else {
                wb(V(step_dx), 0);
                left = rb(V(step_dy));
            }
            if (rb(V(step_count)) >= left)
                wb(V(step_count), left);
            if (!rb(V(step_count)))
                break;
            continue;
        diagonal:
            cl = 0;
            {
                uint8_t t = (uint8_t)(code >> 1), d = rb(V(step_dx));
                if (code & 1) {
                    d = rb(V(step_dy));
                    t ^= 2;
                }
                e = rb(V(step_err));
                wb(V(step_err), (uint8_t)((t & 2) ? e - d : e + d));
            }
        }
        /* a pixel gone */
        e = rb(V(step_err));
        wb(V(step_err), (uint8_t)((code & 1) ? e + rb(V(step_dx)) : e - rb(V(step_dy))));
        cl = 0;
        wb(V(step_count), (uint8_t)(rb(V(step_count)) - 1));
        if (!rb(V(step_count)))
            break;
    }

move:
    ww(V(ball_x), (uint16_t)(rw(V(ball_x)) + rw(V(ball_vx))));
    ww(V(ball_x_hi), (uint16_t)(x << 2 | (rb(V(ball_x_hi)) & 3)));
    ww(V(ball_y), (uint16_t)(rw(V(ball_y)) + rw(V(ball_vy))));
    ww(V(ball_y_hi), (uint16_t)(y << 2 | (rb(V(ball_y_hi)) & 3)));
    flippers_gravity();
}

/* ---- the frame */

/* P: pause (state 8); three steps; the ball's object record (position and
 * speed * 8 as 32 bits) and sprite; ball_drained once it is below the table */
void ball_frame(void)
{
    uint16_t si = V(ball_obj);
    uint32_t v;
    int i;

    if (rb((uint16_t)(V(key_map) + 3)) & 2)
        ww(V(game_state), 8);
    clear_hit_score();
    for (i = 0; i < 3; i++)
        ball_step();
    ww((uint16_t)(si + 2), (uint16_t)(rw(V(ball_x)) << 3));
    ww((uint16_t)(si + 4), (uint16_t)((int16_t)rw(V(ball_x_hi)) >> 5));
    v = rd((uint16_t)(si + 2)) - 0xE000;
    wd((uint16_t)(si + 2), v);
    ww(V(sprite_x), (uint16_t)(v >> 13));
    ww((uint16_t)(si + 6), (uint16_t)(rw(V(ball_y)) << 3));
    ww((uint16_t)(si + 8), (uint16_t)((int16_t)rw(V(ball_y_hi)) >> 5));
    v = rd((uint16_t)(si + 6)) - 0xE000;
    wd((uint16_t)(si + 6), v);
    ww(V(sprite_y), (uint16_t)(v >> 13));
    wb(V(sprites), rb(V(sprites)) | 2);
    ww((uint16_t)(si + 0x12), (uint16_t)(rw(V(ball_vx)) << 3));
    ww((uint16_t)(si + 0x14), (uint16_t)((int8_t)rb(V(ball_vx_hi)) >> 5));
    ww((uint16_t)(si + 0x16), (uint16_t)(rw(V(ball_vy)) << 3));
    ww((uint16_t)(si + 0x18), (uint16_t)((int8_t)rb(V(ball_vy_hi)) >> 5));
    if ((int16_t)rw((uint16_t)(si + 8)) >= 0x40)
        wb(V(ball_drained), 0xFF);
}
