/* platform.h - what the program needs from the operating system: a window
 * that shows an indexed picture, PC keyboard scan codes, a clock and an
 * audio stream.
 *
 * plat_win32.c implements it with the Windows SDK alone (user32, gdi32,
 * winmm), plat_sdl.c with SDL2 on macOS and Linux; plat_null.c is a
 * headless stand-in for tests and scripted runs.
 */
#ifndef PD_PLATFORM_H
#define PD_PLATFORM_H

#include <stdint.h>

int plat_init(const char *title);
void plat_shutdown(void);

/* a message for the player: a message box on Windows, stderr elsewhere */
void plat_message(const char *text);

/* handles window messages; 0 once the window was closed */
int plat_pump(void);

/* shows width x height palette indexes with a 0x00RRGGBB palette, in the
 * 4:3 shape of a VGA screen; Alt+Enter switches to the whole monitor */
void plat_present(const uint8_t *pixels, int width, int height, const uint32_t palette[256]);

/* The keyboard as the PC's port 60h gave it (scan code set 1): make code,
 * break code = make | 80h, E0h before the extended keys.  -1 when none is
 * left. */
int plat_read_scancode(void);

/* monotonic clock in microseconds */
uint64_t plat_micros(void);
void plat_sleep_ms(int ms);

/* Audio: `fill` is called from the audio thread for `frames` stereo
 * 16-bit frames at `rate` Hz.  Lock around data the callback reads. */
typedef void (*PlatAudioFill)(int16_t *out, int frames, void *user);
int plat_audio_start(int rate, PlatAudioFill fill, void *user);
void plat_audio_lock(void);
void plat_audio_unlock(void);

#endif
