/* pd_keys.c - the keyboard: PD.EXE's INT 9 handler (kbd_int) and the
 * waits on it.  See pd.h. */
#include "pd.h"

/* INT 9: key_map bit n = scancode n down; E0-prefixed keys + 80h, the E0
 * shifts (2Ah, 36h after E0) dropped */
void kbd_int(uint8_t b)
{
    uint8_t code, bit;
    uint16_t at;

    if (b == 0xE0) {
        fwb(seg_code, V(kbd_e0), 0xE0);
        return;
    }
    code = b & 0x7F;
    if (frb(seg_code, V(kbd_e0))) {
        fwb(seg_code, V(kbd_e0), 0);
        if (code == 0x2A || code == 0x36)
            return;
        code |= 0x80;
    }
    bit = (uint8_t)(1 << (code & 7));
    at = (uint16_t)(V(key_map) + (code >> 3));
    if (b & 0x80)
        wb(at, (uint8_t)(rb(at) & ~bit));
    else
        wb(at, (uint8_t)(rb(at) | bit));
}

int key_down(int code)
{
    return (rb((uint16_t)(V(key_map) + (code >> 3))) >> (code & 7)) & 1;
}

/* until no key is down */
void wait_keys_up(void)
{
    for (;;) {
        int i, any = 0;
        for (i = 0; i < 0x20; i++)
            any |= rb((uint16_t)(V(key_map) + i));
        if (!any)
            return;
        pump_frame();
    }
}
