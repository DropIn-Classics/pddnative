/* pd_bcd.c - the BCD numbers of scores, bonus and counters: additions and
 * subtractions byte by byte with DAA and DAS as the CPU does them (flags
 * included, as the next byte and some callers use them; PD.ASM bcd_add,
 * bcd_sub and the chains written out in count_bonus, count_boosters ...).
 * See pd.h. */
#include "pd.h"

uint8_t daa(uint8_t al, int *cf, int af)
{
    uint8_t old = al;
    int oc = *cf;

    *cf = 0;
    if ((al & 0x0F) > 9 || af) {
        int t = al + 6;
        al = (uint8_t)t;
        *cf = oc | (t >> 8);
    }
    if (old > 0x99 || oc) {
        al = (uint8_t)(al + 0x60);
        *cf = 1;
    }
    return al;
}

uint8_t das(uint8_t al, int *cf, int af)
{
    uint8_t old = al;
    int oc = *cf;

    *cf = 0;
    if ((al & 0x0F) > 9 || af) {
        int t = al - 6;
        al = (uint8_t)t;
        *cf = oc | (t < 0);
    }
    if (old > 0x99 || oc) {
        al = (uint8_t)(al - 0x60);
        *cf = 1;
    }
    return al;
}

/* ADD/ADC then DAA: a + b + carry in decimal */
uint8_t bcd_adc(uint8_t a, uint8_t b, int *cf)
{
    int sum = a + b + *cf;
    int af = ((a ^ b ^ sum) >> 4) & 1;
    *cf = sum >> 8;
    return daa((uint8_t)sum, cf, af);
}

/* the 6-byte BCD number at `bp` added to the one at `di` (low byte
 * first); idle_timer restarts.  Returns ZF as the last DAA left it (1
 * when the top byte came out 0), which handlers ending here return. */
int bcd_add(uint16_t di, uint16_t bp)
{
    int i, cf = 0;
    uint8_t r = 0;

    for (i = 0; i < 6; i++) {
        r = bcd_adc(rb((uint16_t)(di + i)), rb((uint16_t)(bp + i)), &cf);
        wb((uint16_t)(di + i), r);
    }
    ww(V(idle_timer), 0x348);
    return r == 0;
}

/* ... subtracted; 1 when the result's top byte has bit 7 (below 0) */
int bcd_sub(uint16_t di, uint16_t bp)
{
    int i, cf = 0, d, af;
    uint8_t a = 0, b, r = 0;

    for (i = 0; i < 6; i++) {
        a = rb((uint16_t)(di + i));
        b = rb((uint16_t)(bp + i));
        d = a - b - cf;
        af = ((a ^ b ^ d) >> 4) & 1;
        cf = d < 0;
        r = das((uint8_t)d, &cf, af);
        wb((uint16_t)(di + i), r);
    }
    return (r & 0x80) != 0;
}

/* SUB/SBB then DAS: a - b - borrow in decimal */
uint8_t bcd_sbb(uint8_t a, uint8_t b, int *cf)
{
    int d = a - b - *cf;
    int af = ((a ^ b ^ d) >> 4) & 1;
    *cf = d < 0;
    return das((uint8_t)d, cf, af);
}

/* a word of two BCD bytes + 1 (ADD 1, DAA, ADC 0, DAA) */
void bcd_inc_word(uint16_t at)
{
    int cf = 0;
    wb(at, bcd_adc(rb(at), 1, &cf));
    wb((uint16_t)(at + 1), bcd_adc(rb((uint16_t)(at + 1)), 0, &cf));
}

