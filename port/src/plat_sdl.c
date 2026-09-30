/* plat_sdl.c - platform.h on SDL2, for macOS and Linux (the Steam Deck
 * too): a window drawn by SDL's renderer, the keyboard's scan codes from
 * SDL's (USB) scan codes, game controllers through SDL's game controller
 * API (pad.h), SDL's performance counter, an SDL audio device. */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pad.h"
#include "platform.h"
#include "sys.h"

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;
static int tex_w, tex_h;
static uint32_t *argb;
static int closed;
static SDL_Rect shown;                  /* the picture, in the renderer's pixels */
static int mouse_seen, mouse_x, mouse_y, mouse_clicks;  /* window points */

/* ---- keyboard: a queue of scan code bytes ---- */

/* SDL's scan code -> the PC's set 1 make code, E0 keys + 100h; 0: none */
static const uint16_t set1[SDL_NUM_SCANCODES] = {
    [SDL_SCANCODE_ESCAPE] = 0x01,
    [SDL_SCANCODE_1] = 0x02, [SDL_SCANCODE_2] = 0x03, [SDL_SCANCODE_3] = 0x04,
    [SDL_SCANCODE_4] = 0x05, [SDL_SCANCODE_5] = 0x06, [SDL_SCANCODE_6] = 0x07,
    [SDL_SCANCODE_7] = 0x08, [SDL_SCANCODE_8] = 0x09, [SDL_SCANCODE_9] = 0x0A,
    [SDL_SCANCODE_0] = 0x0B, [SDL_SCANCODE_MINUS] = 0x0C, [SDL_SCANCODE_EQUALS] = 0x0D,
    [SDL_SCANCODE_BACKSPACE] = 0x0E, [SDL_SCANCODE_TAB] = 0x0F,
    [SDL_SCANCODE_Q] = 0x10, [SDL_SCANCODE_W] = 0x11, [SDL_SCANCODE_E] = 0x12,
    [SDL_SCANCODE_R] = 0x13, [SDL_SCANCODE_T] = 0x14, [SDL_SCANCODE_Y] = 0x15,
    [SDL_SCANCODE_U] = 0x16, [SDL_SCANCODE_I] = 0x17, [SDL_SCANCODE_O] = 0x18,
    [SDL_SCANCODE_P] = 0x19, [SDL_SCANCODE_LEFTBRACKET] = 0x1A,
    [SDL_SCANCODE_RIGHTBRACKET] = 0x1B, [SDL_SCANCODE_RETURN] = 0x1C,
    [SDL_SCANCODE_LCTRL] = 0x1D,
    [SDL_SCANCODE_A] = 0x1E, [SDL_SCANCODE_S] = 0x1F, [SDL_SCANCODE_D] = 0x20,
    [SDL_SCANCODE_F] = 0x21, [SDL_SCANCODE_G] = 0x22, [SDL_SCANCODE_H] = 0x23,
    [SDL_SCANCODE_J] = 0x24, [SDL_SCANCODE_K] = 0x25, [SDL_SCANCODE_L] = 0x26,
    [SDL_SCANCODE_SEMICOLON] = 0x27, [SDL_SCANCODE_APOSTROPHE] = 0x28,
    [SDL_SCANCODE_GRAVE] = 0x29, [SDL_SCANCODE_LSHIFT] = 0x2A,
    [SDL_SCANCODE_BACKSLASH] = 0x2B, [SDL_SCANCODE_NONUSHASH] = 0x2B,
    [SDL_SCANCODE_Z] = 0x2C, [SDL_SCANCODE_X] = 0x2D, [SDL_SCANCODE_C] = 0x2E,
    [SDL_SCANCODE_V] = 0x2F, [SDL_SCANCODE_B] = 0x30, [SDL_SCANCODE_N] = 0x31,
    [SDL_SCANCODE_M] = 0x32, [SDL_SCANCODE_COMMA] = 0x33, [SDL_SCANCODE_PERIOD] = 0x34,
    [SDL_SCANCODE_SLASH] = 0x35, [SDL_SCANCODE_RSHIFT] = 0x36,
    [SDL_SCANCODE_KP_MULTIPLY] = 0x37, [SDL_SCANCODE_LALT] = 0x38,
    [SDL_SCANCODE_SPACE] = 0x39, [SDL_SCANCODE_CAPSLOCK] = 0x3A,
    [SDL_SCANCODE_F1] = 0x3B, [SDL_SCANCODE_F2] = 0x3C, [SDL_SCANCODE_F3] = 0x3D,
    [SDL_SCANCODE_F4] = 0x3E, [SDL_SCANCODE_F5] = 0x3F, [SDL_SCANCODE_F6] = 0x40,
    [SDL_SCANCODE_F7] = 0x41, [SDL_SCANCODE_F8] = 0x42, [SDL_SCANCODE_F9] = 0x43,
    [SDL_SCANCODE_F10] = 0x44, [SDL_SCANCODE_NUMLOCKCLEAR] = 0x45,
    [SDL_SCANCODE_SCROLLLOCK] = 0x46,
    [SDL_SCANCODE_KP_7] = 0x47, [SDL_SCANCODE_KP_8] = 0x48, [SDL_SCANCODE_KP_9] = 0x49,
    [SDL_SCANCODE_KP_MINUS] = 0x4A, [SDL_SCANCODE_KP_4] = 0x4B, [SDL_SCANCODE_KP_5] = 0x4C,
    [SDL_SCANCODE_KP_6] = 0x4D, [SDL_SCANCODE_KP_PLUS] = 0x4E, [SDL_SCANCODE_KP_1] = 0x4F,
    [SDL_SCANCODE_KP_2] = 0x50, [SDL_SCANCODE_KP_3] = 0x51, [SDL_SCANCODE_KP_0] = 0x52,
    [SDL_SCANCODE_KP_PERIOD] = 0x53, [SDL_SCANCODE_NONUSBACKSLASH] = 0x56,
    [SDL_SCANCODE_F11] = 0x57, [SDL_SCANCODE_F12] = 0x58,
    [SDL_SCANCODE_KP_ENTER] = 0x11C, [SDL_SCANCODE_RCTRL] = 0x11D,
    [SDL_SCANCODE_KP_DIVIDE] = 0x135, [SDL_SCANCODE_RALT] = 0x138,
    [SDL_SCANCODE_HOME] = 0x147, [SDL_SCANCODE_UP] = 0x148, [SDL_SCANCODE_PAGEUP] = 0x149,
    [SDL_SCANCODE_LEFT] = 0x14B, [SDL_SCANCODE_RIGHT] = 0x14D, [SDL_SCANCODE_END] = 0x14F,
    [SDL_SCANCODE_DOWN] = 0x150, [SDL_SCANCODE_PAGEDOWN] = 0x151,
    [SDL_SCANCODE_INSERT] = 0x152, [SDL_SCANCODE_DELETE] = 0x153,
    [SDL_SCANCODE_LGUI] = 0x15B, [SDL_SCANCODE_RGUI] = 0x15C,
    [SDL_SCANCODE_APPLICATION] = 0x15D,
};

#define KEYQ 256
static uint8_t keyq[KEYQ];
static int keyq_head, keyq_tail;
static uint8_t held[256];               /* make codes down, E0 keys at + 80h */

static void push_byte(uint8_t b)
{
    int next = (keyq_tail + 1) % KEYQ;
    if (next != keyq_head) {
        keyq[keyq_tail] = b;
        keyq_tail = next;
    }
}

static void push_key(int code, int extended, int up)
{
    int idx = (code & 0x7F) | (extended ? 0x80 : 0);
    if (!up && held[idx])
        return;                         /* auto-repeat */
    if (up && !held[idx])
        return;                         /* its release was sent already */
    held[idx] = (uint8_t)!up;
    if (extended)
        push_byte(0xE0);
    push_byte((uint8_t)((code & 0x7F) | (up ? 0x80 : 0)));
}

/* everything still down goes up (the window lost the keyboard) */
static void release_all(void)
{
    int i;
    for (i = 0; i < 256; i++)
        if (held[i])
            push_key(i & 0x7F, i & 0x80, 1);
}

/* ---- game controllers: their buttons into the same queue (pad.h) */

#define MAX_PADS 8
static struct {
    SDL_GameController *gc;
    SDL_JoystickID id;
    uint32_t on;                        /* the sources down, bit = source */
} pads[MAX_PADS];

/* sources 0-15 are pad.h's buttons, 16-19 the left stick up, down, left,
 * right (the D-pad's buttons as well) */
static const int sdl_buttons[SDL_CONTROLLER_BUTTON_MAX] = {
    [SDL_CONTROLLER_BUTTON_A] = PAD_A + 1, [SDL_CONTROLLER_BUTTON_B] = PAD_B + 1,
    [SDL_CONTROLLER_BUTTON_X] = PAD_X + 1, [SDL_CONTROLLER_BUTTON_Y] = PAD_Y + 1,
    [SDL_CONTROLLER_BUTTON_BACK] = PAD_BACK + 1, [SDL_CONTROLLER_BUTTON_START] = PAD_START + 1,
    [SDL_CONTROLLER_BUTTON_LEFTSTICK] = PAD_LSTICK + 1,
    [SDL_CONTROLLER_BUTTON_RIGHTSTICK] = PAD_RSTICK + 1,
    [SDL_CONTROLLER_BUTTON_LEFTSHOULDER] = PAD_LB + 1,
    [SDL_CONTROLLER_BUTTON_RIGHTSHOULDER] = PAD_RB + 1,
    [SDL_CONTROLLER_BUTTON_DPAD_UP] = PAD_UP + 1, [SDL_CONTROLLER_BUTTON_DPAD_DOWN] = PAD_DOWN + 1,
    [SDL_CONTROLLER_BUTTON_DPAD_LEFT] = PAD_LEFT + 1, [SDL_CONTROLLER_BUTTON_DPAD_RIGHT] = PAD_RIGHT + 1,
};

static void pad_key(int code, int up)
{
    push_key(code & 0x7F, code & 0x80, up);
}

static void set_source(int p, int source, int on)
{
    uint32_t bit = 1u << source;

    if (!on == !(pads[p].on & bit))
        return;
    pads[p].on ^= bit;
    pad_button(source < 16 ? source : PAD_UP + (source - 16), on, pad_key);
}

static int pad_index(SDL_JoystickID id)
{
    int p;
    for (p = 0; p < MAX_PADS; p++)
        if (pads[p].gc && pads[p].id == id)
            return p;
    return -1;
}

static void pad_added(int device)
{
    int p;
    SDL_GameController *gc;

    if (pad_index(SDL_JoystickGetDeviceInstanceID(device)) >= 0)
        return;                         /* opened already */
    for (p = 0; p < MAX_PADS && pads[p].gc; p++)
        ;
    if (p == MAX_PADS || !(gc = SDL_GameControllerOpen(device)))
        return;
    pads[p].gc = gc;
    pads[p].id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gc));
    pads[p].on = 0;
}

static void pad_removed(SDL_JoystickID id)
{
    int p = pad_index(id), s;

    if (p < 0)
        return;
    for (s = 0; s < 20; s++)
        set_source(p, s, 0);
    SDL_GameControllerClose(pads[p].gc);
    pads[p].gc = NULL;
}

/* a trigger or the left stick, with some hysteresis */
static void pad_axis(SDL_JoystickID id, int axis, int v)
{
    int p = pad_index(id);

    if (p < 0)
        return;
    switch (axis) {
    case SDL_CONTROLLER_AXIS_TRIGGERLEFT:
    case SDL_CONTROLLER_AXIS_TRIGGERRIGHT: {
        int s = axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ? PAD_LT : PAD_RT;
        if (v > 16000 || v < 8000)
            set_source(p, s, v > 16000);
        break;
    }
    case SDL_CONTROLLER_AXIS_LEFTY:
    case SDL_CONTROLLER_AXIS_LEFTX: {
        int s = axis == SDL_CONTROLLER_AXIS_LEFTY ? 16 : 18;  /* up/down, left/right */
        if (v < -20000 || v > -12000)
            set_source(p, s, v < -20000);
        if (v > 20000 || v < 12000)
            set_source(p, s + 1, v > 20000);
        break;
    }
    }
}

/* the sound keys, from SDL's text input */
static int controls[16], ctl_head, ctl_tail;

static void push_control(char c)
{
    int v = c == '+' ? PLAT_VOLUME_UP : c == '-' ? PLAT_VOLUME_DOWN :
            c == '*' ? PLAT_MUTE : c == '/' ? PLAT_EQ : 0;
    int next = (ctl_tail + 1) % 16;
    if (v && next != ctl_head) {
        controls[ctl_tail] = v;
        ctl_tail = next;
    }
}

int plat_read_control(void)
{
    int v;
    if (ctl_head == ctl_tail)
        return -1;
    v = controls[ctl_head];
    ctl_head = (ctl_head + 1) % 16;
    return v;
}

int plat_read_scancode(void)
{
    int b;
    if (keyq_head == keyq_tail)
        return -1;
    b = keyq[keyq_head];
    keyq_head = (keyq_head + 1) % KEYQ;
    return b;
}

/* ---- the window ---- */

int plat_has_window(void)
{
    return 1;
}

/* the keys held with Enter to switch full screen (platform.h) */
#ifdef __APPLE__
#define FULLSCREEN_MODS (KMOD_ALT | KMOD_GUI)
#else
#define FULLSCREEN_MODS KMOD_ALT
#endif

/* full screen as asked for: on macOS the window's flags say so only when
 * the animation to or from full screen has ended, most of a second later */
static int fullscreen;

static int window_fullscreen(void)
{
    return (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP;
}

int plat_fullscreen(void)
{
    return fullscreen;
}

void plat_set_fullscreen(int on)
{
    on = on != 0;
    if (on == fullscreen)
        return;
    fullscreen = on;
    /* the keys down go up now: the key that switched could lose its
     * release in the animation and repeat, switching back */
    release_all();
    SDL_SetWindowFullscreen(window, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

static void toggle_fullscreen(void)
{
    plat_set_fullscreen(!plat_fullscreen());
}

int plat_init(const char *title)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 0;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");        /* sharp pixels */
    window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 960, 720,
                              SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI |
                              (sys_steam_deck() ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return 0;
    }
    fullscreen = window_fullscreen();
    /* no vsync: frame.c paces the pictures by the driver's tick */
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer)
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        return 0;
    }
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_ShowCursor(SDL_DISABLE);
    SDL_RenderPresent(renderer);
    SDL_StartTextInput();               /* the sound keys come as text */
    return 1;
}

void plat_shutdown(void)
{
    if (texture)
        SDL_DestroyTexture(texture);
    if (renderer)
        SDL_DestroyRenderer(renderer);
    if (window)
        SDL_DestroyWindow(window);
    texture = NULL;
    renderer = NULL;
    window = NULL;
    free(argb);
    argb = NULL;
    SDL_Quit();
}

void plat_message(const char *text)
{
    if (SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Pinball Dreams", text, window) != 0)
        fprintf(stderr, "%s\n", text);
}

int plat_pump(void)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            closed = 1;
            break;
        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
                release_all();
            /* switched by the window's own button or the system's keys too */
            if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                fullscreen = window_fullscreen();
            break;
        case SDL_CONTROLLERDEVICEADDED:        /* also those there at the start */
            pad_added(e.cdevice.which);
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            pad_removed(e.cdevice.which);
            break;
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP: {
            int p = pad_index(e.cbutton.which), b = e.cbutton.button;
            if (p >= 0 && b >= 0 && b < SDL_CONTROLLER_BUTTON_MAX && sdl_buttons[b])
                set_source(p, sdl_buttons[b] - 1, e.type == SDL_CONTROLLERBUTTONDOWN);
            break;
        }
        case SDL_MOUSEMOTION:
            mouse_seen = 1;
            mouse_x = e.motion.x;
            mouse_y = e.motion.y;
            break;
        case SDL_MOUSEBUTTONDOWN:
            mouse_seen = 1;
            mouse_x = e.button.x;
            mouse_y = e.button.y;
            mouse_clicks |= e.button.button == SDL_BUTTON_LEFT ? 1 : e.button.button == SDL_BUTTON_RIGHT ? 2 : 0;
            break;
        case SDL_CONTROLLERAXISMOTION:
            pad_axis(e.caxis.which, e.caxis.axis, e.caxis.value);
            break;
        case SDL_TEXTINPUT: {
            const char *t;
            for (t = e.text.text; *t; t++)
                push_control(*t);
            break;
        }
        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            SDL_Scancode sc = e.key.keysym.scancode;
            int up = e.type == SDL_KEYUP, code;
            if (!up && sc == SDL_SCANCODE_RETURN && (e.key.keysym.mod & FULLSCREEN_MODS)) {
                if (!e.key.repeat)
                    toggle_fullscreen();
                break;
            }
            if (!up && sc == SDL_SCANCODE_F4 && (e.key.keysym.mod & KMOD_ALT)) {
                closed = 1;
                break;
            }
            code = (unsigned)sc < SDL_NUM_SCANCODES ? set1[sc] : 0;
            if (code)
                push_key(code & 0x7F, code & 0x100, up);
            break;
        }
        }
    }
    return !closed;
}

/* the picture in the largest 4:3 rectangle of the window, black around */
void plat_present(const uint8_t *src, int width, int height, const uint32_t palette[256])
{
    SDL_Rect dst;
    int ow, oh, i;

    if (width != tex_w || height != tex_h || !texture) {
        if (texture)
            SDL_DestroyTexture(texture);
        free(argb);
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                    SDL_TEXTUREACCESS_STREAMING, width, height);
        argb = (uint32_t *)malloc((size_t)width * (size_t)height * sizeof *argb);
        if (!texture || !argb)
            return;
        tex_w = width;
        tex_h = height;
    }
    for (i = 0; i < width * height; i++)
        argb[i] = 0xFF000000u | palette[src[i]];
    SDL_UpdateTexture(texture, NULL, argb, width * (int)sizeof *argb);

    SDL_GetRendererOutputSize(renderer, &ow, &oh);
    if (ow * 3 > oh * 4) {
        dst.h = oh;
        dst.w = oh * 4 / 3;
    } else {
        dst.w = ow;
        dst.h = ow * 3 / 4;
    }
    dst.x = (ow - dst.w) / 2;
    dst.y = (oh - dst.h) / 2;
    shown = dst;
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, &dst);
    SDL_RenderPresent(renderer);
}

/* the window's points to the renderer's pixels (more on a Retina screen) */
int plat_mouse(int *x, int *y, int *clicks)
{
    int ww, wh, ow, oh, px, py;

    *clicks = mouse_clicks;
    mouse_clicks = 0;
    if (!mouse_seen || shown.w <= 0 || shown.h <= 0)
        return 0;
    SDL_GetWindowSize(window, &ww, &wh);
    SDL_GetRendererOutputSize(renderer, &ow, &oh);
    px = ww > 0 ? mouse_x * ow / ww - shown.x : 0;
    py = wh > 0 ? mouse_y * oh / wh - shown.y : 0;
    px = px < 0 ? 0 : px >= shown.w ? shown.w - 1 : px;
    py = py < 0 ? 0 : py >= shown.h ? shown.h - 1 : py;
    *x = (int)((long)px * 65536 / shown.w);
    *y = (int)((long)py * 65536 / shown.h);
    return 1;
}

/* ---- the clock ---- */

uint64_t plat_micros(void)
{
    static uint64_t freq;
    uint64_t now = SDL_GetPerformanceCounter();
    if (!freq)
        freq = SDL_GetPerformanceFrequency();
    return now / freq * 1000000 + now % freq * 1000000 / freq;
}

void plat_sleep_ms(int ms)
{
    SDL_Delay((Uint32)(ms > 0 ? ms : 0));
}

/* ---- audio: SDL's callback, which runs with the device's lock held ---- */

static SDL_AudioDeviceID audio_dev;
static PlatAudioFill audio_fill;
static void *audio_user;

static void SDLCALL audio_callback(void *user, Uint8 *stream, int len)
{
    (void)user;
    audio_fill((int16_t *)stream, len / 4, audio_user);
}

int plat_audio_start(int rate, PlatAudioFill fill, void *user)
{
    SDL_AudioSpec want, have;

    audio_fill = fill;
    audio_user = user;
    memset(&want, 0, sizeof want);
    want.freq = rate;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;                /* about 23 ms at 44.1 kHz */
    want.callback = audio_callback;
    audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!audio_dev)
        return 0;
    SDL_PauseAudioDevice(audio_dev, 0);
    return 1;
}

void plat_audio_lock(void)
{
    if (audio_dev)
        SDL_LockAudioDevice(audio_dev);
}

void plat_audio_unlock(void)
{
    if (audio_dev)
        SDL_UnlockAudioDevice(audio_dev);
}
