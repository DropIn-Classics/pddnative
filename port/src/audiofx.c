/* audiofx.c - see audiofx.h.  Each frame:
 *
 *   DC blocker -> bass shelf -> treble shelf -> oomph shelf
 *   -> [headphone: an echo per ear, crossfeed] -> gain -> clip
 *
 * The constants are frequencies and times, so they hold at any rate. */
#include <math.h>
#include <string.h>
#include "audiofx.h"

#define TWO_PI 6.28318531f

#define DC_HZ 38.0f             /* the DC blocker's corner */
#define BASS_HZ 200.0f          /* the shelves' corners */
#define TREBLE_HZ 4000.0f
#define OOMPH_HZ 120.0f
#define HEADPHONE_TREBLE (-2)   /* dB: earphones bring out the 8-bit grit */

/* headphone mode: echoes of the mono signal, one per ear, and the
 * crossfeed (the other side low-passed, added in) */
#define ECHO_LEFT_MS 8
#define ECHO_RIGHT_MS 13
#define ECHO_LEVEL 0.12f
#define CROSS_HZ 700.0f
#define CROSS_LEVEL 0.30f
#define CROSS_NORM (1.0f / (1.0f + CROSS_LEVEL / 2))
#define ECHO_LEN 1024           /* 13 ms up to 78 kHz */

/* a shelving biquad (RBJ, slope 1), direct form I */
typedef struct {
    float b0, b1, b2, a1, a2;
    float x1, x2, y1, y2;
    int active;
} Shelf;

static float rate = 44100.0f;
static Shelf bass[2], treble[2], oomph[2];
static float dc_pole, dc_in[2], dc_out[2];
static float cross_k, cross_lp[2];
static float echo[ECHO_LEN];
static int echo_pos, echo_left, echo_right;

static int set_bass, set_treble, set_oomph, set_headphone, bypassed;
static int headphone;

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* new coefficients for `gain_db` at `hz`; about 0 dB switches the shelf
 * off.  The filter's history stays, so a change does not click. */
static void shelf_design(Shelf *f, int high, float hz, float gain_db)
{
    float a, sa, w, cw, alpha, a0;

    if (fabsf(gain_db) < 0.25f) {
        f->active = 0;
        return;
    }
    a = powf(10.0f, gain_db / 40.0f);
    sa = sqrtf(a);
    w = TWO_PI * hz / rate;
    cw = cosf(w);
    alpha = sinf(w) * 0.70710678f;          /* slope 1 */
    if (high) {
        a0 = (a + 1) - (a - 1) * cw + 2 * sa * alpha;
        f->b0 = a * ((a + 1) + (a - 1) * cw + 2 * sa * alpha) / a0;
        f->b1 = -2 * a * ((a - 1) + (a + 1) * cw) / a0;
        f->b2 = a * ((a + 1) + (a - 1) * cw - 2 * sa * alpha) / a0;
        f->a1 = 2 * ((a - 1) - (a + 1) * cw) / a0;
        f->a2 = ((a + 1) - (a - 1) * cw - 2 * sa * alpha) / a0;
    } else {
        a0 = (a + 1) + (a - 1) * cw + 2 * sa * alpha;
        f->b0 = a * ((a + 1) - (a - 1) * cw + 2 * sa * alpha) / a0;
        f->b1 = 2 * a * ((a - 1) - (a + 1) * cw) / a0;
        f->b2 = a * ((a + 1) - (a - 1) * cw - 2 * sa * alpha) / a0;
        f->a1 = -2 * ((a - 1) + (a + 1) * cw) / a0;
        f->a2 = ((a + 1) + (a - 1) * cw - 2 * sa * alpha) / a0;
    }
    f->active = 1;
}

static float shelf_run(Shelf *f, float x)
{
    float y;

    if (!f->active)
        return x;
    y = f->b0 * x + f->b1 * f->x1 + f->b2 * f->x2 - f->a1 * f->y1 - f->a2 * f->y2;
    f->x2 = f->x1;
    f->x1 = x;
    f->y2 = f->y1;
    f->y1 = y;
    return y;
}

static void redesign(void)
{
    int b = set_bass, t = set_treble, o = set_oomph, c;

    headphone = set_headphone && !bypassed;
    if (bypassed)
        b = t = o = 0;
    if (headphone)
        t += HEADPHONE_TREBLE;
    for (c = 0; c < 2; c++) {
        shelf_design(&bass[c], 0, BASS_HZ, (float)b);
        shelf_design(&treble[c], 1, TREBLE_HZ, (float)t);
        shelf_design(&oomph[c], 0, OOMPH_HZ, (float)o);
    }
}

void audiofx_init(long r)
{
    rate = r > 0 ? (float)r : 44100.0f;
    memset(bass, 0, sizeof bass);
    memset(treble, 0, sizeof treble);
    memset(oomph, 0, sizeof oomph);
    memset(dc_in, 0, sizeof dc_in);
    memset(dc_out, 0, sizeof dc_out);
    memset(cross_lp, 0, sizeof cross_lp);
    memset(echo, 0, sizeof echo);
    echo_pos = 0;
    dc_pole = expf(-TWO_PI * DC_HZ / rate);
    cross_k = 1.0f - expf(-TWO_PI * CROSS_HZ / rate);
    echo_left = clampi((int)(rate * ECHO_LEFT_MS / 1000), 1, ECHO_LEN - 1);
    echo_right = clampi((int)(rate * ECHO_RIGHT_MS / 1000), 1, ECHO_LEN - 1);
    redesign();
}

void audiofx_set(int b, int t, int o, int hp)
{
    set_bass = clampi(b, -AUDIOFX_EQ_MAX, AUDIOFX_EQ_MAX);
    set_treble = clampi(t, -AUDIOFX_EQ_MAX, AUDIOFX_EQ_MAX);
    set_oomph = clampi(o, 0, AUDIOFX_OOMPH_MAX);
    set_headphone = hp != 0;
    redesign();
}

void audiofx_set_bypass(int on)
{
    bypassed = on != 0;
    redesign();
}

int audiofx_bypass(void)
{
    return bypassed;
}

static int16_t to_sample(float v)
{
    v = v * 32767.0f + (v >= 0 ? 0.5f : -0.5f);
    return (int16_t)(v > 32767.0f ? 32767 : v < -32768.0f ? -32768 : (long)v);
}

void audiofx_process(int16_t *buf, int frames, float gain)
{
    int i, c;

    for (i = 0; i < frames; i++) {
        float s[2];

        for (c = 0; c < 2; c++) {
            float x = buf[2 * i + c] * (1.0f / 32768.0f);
            float v = x - dc_in[c] + dc_pole * dc_out[c];

            dc_in[c] = x;
            dc_out[c] = v;
            v = shelf_run(&bass[c], v);
            v = shelf_run(&treble[c], v);
            s[c] = shelf_run(&oomph[c], v);
        }
        if (headphone) {
            float l = s[0] + ECHO_LEVEL * echo[(echo_pos + ECHO_LEN - echo_left) % ECHO_LEN];
            float r = s[1] + ECHO_LEVEL * echo[(echo_pos + ECHO_LEN - echo_right) % ECHO_LEN];

            echo[echo_pos] = (s[0] + s[1]) * 0.5f;
            echo_pos = (echo_pos + 1) % ECHO_LEN;
            cross_lp[0] += cross_k * (l - cross_lp[0]);
            cross_lp[1] += cross_k * (r - cross_lp[1]);
            s[0] = (l + CROSS_LEVEL * cross_lp[1]) * CROSS_NORM;
            s[1] = (r + CROSS_LEVEL * cross_lp[0]) * CROSS_NORM;
        }
        buf[2 * i] = to_sample(s[0] * gain);
        buf[2 * i + 1] = to_sample(s[1] * gain);
    }
}
