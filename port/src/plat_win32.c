/* plat_win32.c - platform.h on Windows: a GDI window, the keyboard's scan
 * codes from WM_KEYDOWN/UP, game controllers through XInput (pad.h),
 * QueryPerformanceCounter, waveOut. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <xinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pad.h"
#include "platform.h"

#define WINDOW_CLASS "PinballDreamsWindow"

static HWND window;
static int closed;
static int fullscreen;
static WINDOWPLACEMENT windowed_place = {sizeof(WINDOWPLACEMENT)};

/* the picture to show: the last one handed to plat_present */
static struct {
    BITMAPINFOHEADER h;
    RGBQUAD colors[256];
} bmi;
static uint8_t *pixels;
static int pic_w, pic_h;

/* ---- keyboard: a queue of scan code bytes ---- */

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
        return;                         /* Windows' auto-repeat */
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

/* ---- game controllers: XInput's four, polled each pump (pad.h) */

typedef DWORD (WINAPI *GetStateFn)(DWORD user, XINPUT_STATE *state);
static GetStateFn xinput_get_state;
static uint8_t pad_down[256];           /* keys a button holds (release_shifts) */
static struct {
    int connected;
    uint32_t on;                        /* the sources down, as in set_source */
    DWORD next_look;                    /* not connected: when to look again */
} xpads[XUSER_MAX_COUNT];

static void pad_key(int code, int up)
{
    pad_down[code & 0xFF] = (uint8_t)!up;
    push_key(code & 0x7F, code & 0x80, up);
}

/* sources 0-15 are pad.h's buttons, 16-19 the left stick up, down, left,
 * right (the D-pad's buttons as well) */
static void set_source(int p, int source, int on)
{
    uint32_t bit = 1u << source;

    if (!on == !(xpads[p].on & bit))
        return;
    xpads[p].on ^= bit;
    pad_button(source < 16 ? source : PAD_UP + (source - 16), on, pad_key);
}

static void load_xinput(void)
{
    static const char *const dlls[] = { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll" };
    size_t i;

    for (i = 0; i < sizeof dlls / sizeof dlls[0] && !xinput_get_state; i++) {
        HMODULE m = LoadLibraryA(dlls[i]);
        if (m)
            xinput_get_state = (GetStateFn)(void (*)(void))GetProcAddress(m, "XInputGetState");
    }
}

/* the thumb stick's value past `on` sets the source, back inside `off`
 * clears it */
static void stick(int p, int source, int v, int on, int off)
{
    if (v > on || v < off)
        set_source(p, source, v > on);
}

static void poll_pads(void)
{
    static const struct { WORD mask; int button; } buttons[] = {
        { XINPUT_GAMEPAD_A, PAD_A }, { XINPUT_GAMEPAD_B, PAD_B },
        { XINPUT_GAMEPAD_X, PAD_X }, { XINPUT_GAMEPAD_Y, PAD_Y },
        { XINPUT_GAMEPAD_BACK, PAD_BACK }, { XINPUT_GAMEPAD_START, PAD_START },
        { XINPUT_GAMEPAD_LEFT_THUMB, PAD_LSTICK }, { XINPUT_GAMEPAD_RIGHT_THUMB, PAD_RSTICK },
        { XINPUT_GAMEPAD_LEFT_SHOULDER, PAD_LB }, { XINPUT_GAMEPAD_RIGHT_SHOULDER, PAD_RB },
        { XINPUT_GAMEPAD_DPAD_UP, PAD_UP }, { XINPUT_GAMEPAD_DPAD_DOWN, PAD_DOWN },
        { XINPUT_GAMEPAD_DPAD_LEFT, PAD_LEFT }, { XINPUT_GAMEPAD_DPAD_RIGHT, PAD_RIGHT },
    };
    DWORD now = GetTickCount();
    XINPUT_STATE st;
    int p, s;
    size_t i;

    if (!xinput_get_state || GetForegroundWindow() != window)
        return;                         /* another window has the player */
    for (p = 0; p < XUSER_MAX_COUNT; p++) {
        /* an empty slot is slow to ask: every two seconds */
        if (!xpads[p].connected && (LONG)(now - xpads[p].next_look) < 0)
            continue;
        if (xinput_get_state((DWORD)p, &st) != ERROR_SUCCESS) {
            for (s = 0; s < 20; s++)
                set_source(p, s, 0);    /* unplugged: all it held goes up */
            xpads[p].connected = 0;
            xpads[p].next_look = now + 2000;
            continue;
        }
        xpads[p].connected = 1;
        for (i = 0; i < sizeof buttons / sizeof buttons[0]; i++)
            set_source(p, buttons[i].button, (st.Gamepad.wButtons & buttons[i].mask) != 0);
        if (st.Gamepad.bLeftTrigger > 64 || st.Gamepad.bLeftTrigger < 32)
            set_source(p, PAD_LT, st.Gamepad.bLeftTrigger > 64);
        if (st.Gamepad.bRightTrigger > 64 || st.Gamepad.bRightTrigger < 32)
            set_source(p, PAD_RT, st.Gamepad.bRightTrigger > 64);
        stick(p, 16, st.Gamepad.sThumbLY, 20000, 12000);        /* up: positive here */
        stick(p, 17, -st.Gamepad.sThumbLY, 20000, 12000);
        stick(p, 18, -st.Gamepad.sThumbLX, 20000, 12000);
        stick(p, 19, st.Gamepad.sThumbLX, 20000, 12000);
    }
}

/* the sound keys, from WM_CHAR */
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

static void set_fullscreen(int on)
{
    DWORD style = (DWORD)GetWindowLongA(window, GWL_STYLE);
    if (on == fullscreen)
        return;
    fullscreen = on;
    if (on) {
        MONITORINFO mi = {sizeof mi};
        GetWindowPlacement(window, &windowed_place);
        GetMonitorInfoA(MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY), &mi);
        SetWindowLongA(window, GWL_STYLE, (LONG)(style & ~WS_OVERLAPPEDWINDOW) | WS_POPUP);
        SetWindowPos(window, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    } else {
        SetWindowLongA(window, GWL_STYLE, (LONG)((style & ~WS_POPUP) | WS_OVERLAPPEDWINDOW));
        SetWindowPlacement(window, &windowed_place);
        SetWindowPos(window, NULL, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
}

int plat_has_window(void)
{
    return 1;
}

void plat_set_fullscreen(int on)
{
    set_fullscreen(on != 0);
}

int plat_fullscreen(void)
{
    return fullscreen;
}

/* the picture in the largest 4:3 rectangle of the client area, black around */
static void paint(HDC dc)
{
    RECT r;
    int cw, ch, w, h, x, y;
    HBRUSH black = (HBRUSH)GetStockObject(BLACK_BRUSH);

    GetClientRect(window, &r);
    cw = r.right;
    ch = r.bottom;
    if (cw * 3 > ch * 4) {
        h = ch;
        w = ch * 4 / 3;
    } else {
        w = cw;
        h = cw * 3 / 4;
    }
    x = (cw - w) / 2;
    y = (ch - h) / 2;
    if (x > 0) {
        RECT a = {0, 0, x, ch}, b = {x + w, 0, cw, ch};
        FillRect(dc, &a, black);
        FillRect(dc, &b, black);
    }
    if (y > 0) {
        RECT a = {0, 0, cw, y}, b = {0, y + h, cw, ch};
        FillRect(dc, &a, black);
        FillRect(dc, &b, black);
    }
    if (!pixels) {
        RECT a = {x, y, x + w, y + h};
        FillRect(dc, &a, black);
        return;
    }
    SetStretchBltMode(dc, COLORONCOLOR);
    StretchDIBits(dc, x, y, w, h, 0, 0, pic_w, pic_h, pixels, (BITMAPINFO *)&bmi,
                  DIB_RGB_COLORS, SRCCOPY);
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CLOSE:
        closed = 1;
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        paint(dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_KILLFOCUS:
        release_all();
        break;
    case WM_CHAR:
        push_control((char)wp);
        return 0;
    case WM_SYSKEYDOWN:
        if (wp == VK_RETURN && (lp & (1 << 29))) {          /* Alt+Enter */
            if (!(lp & (1 << 30)))
                set_fullscreen(!fullscreen);
            return 0;
        }
        if (wp == VK_F4 && (lp & (1 << 29))) {
            closed = 1;
            return 0;
        }
        /* fall through: F10 and the Alt keys are keys for the game */
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP: {
        int code = (int)((lp >> 16) & 0xFF), ext = (int)((lp >> 24) & 1);
        int up = msg == WM_KEYUP || msg == WM_SYSKEYUP;
        if (code)
            push_key(code, ext, up);
        return 0;                       /* no menu on Alt or F10 */
    }
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

int plat_init(const char *title)
{
    WNDCLASSA wc;
    RECT r = {0, 0, 960, 720};
    HINSTANCE inst = GetModuleHandleA(NULL);

    SetProcessDPIAware();
    timeBeginPeriod(1);
    load_xinput();
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(inst, MAKEINTRESOURCE(1));
    wc.lpszClassName = WINDOW_CLASS;
    if (!RegisterClassA(&wc))
        return 0;
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    window = CreateWindowA(WINDOW_CLASS, title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                           r.right - r.left, r.bottom - r.top, NULL, NULL, inst, NULL);
    if (!window)
        return 0;
    ShowWindow(window, SW_SHOW);
    return 1;
}

void plat_shutdown(void)
{
    if (window)
        DestroyWindow(window);
    window = NULL;
    free(pixels);
    pixels = NULL;
    timeEndPeriod(1);
}

void plat_message(const char *text)
{
    MessageBoxA(window, text, "Pinball Dreams", MB_OK | MB_ICONINFORMATION);
}

/* With both Shift keys down, Windows sends no WM_KEYUP for the one let go
 * first (only when the second goes up): the flippers are the Shift keys,
 * so each one's release is looked for in the keyboard's state */
static void release_shifts(void)
{
    if (held[0x2A] && !pad_down[0x2A] && !(GetAsyncKeyState(VK_LSHIFT) & 0x8000))
        push_key(0x2A, 0, 1);
    if (held[0x36] && !pad_down[0x36] && !(GetAsyncKeyState(VK_RSHIFT) & 0x8000))
        push_key(0x36, 0, 1);
}

int plat_pump(void)
{
    MSG m;
    while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
    release_shifts();
    poll_pads();
    return !closed;
}

void plat_present(const uint8_t *src, int width, int height, const uint32_t palette[256])
{
    int i, y, stride = (width + 3) & ~3;
    HDC dc;

    if (width != pic_w || height != pic_h || !pixels) {
        free(pixels);
        pixels = (uint8_t *)malloc((size_t)stride * (size_t)height);
        if (!pixels)
            return;
        pic_w = width;
        pic_h = height;
    }
    memset(&bmi.h, 0, sizeof bmi.h);
    bmi.h.biSize = sizeof bmi.h;
    bmi.h.biWidth = width;
    bmi.h.biHeight = -height;           /* top-down */
    bmi.h.biPlanes = 1;
    bmi.h.biBitCount = 8;
    bmi.h.biCompression = BI_RGB;
    bmi.h.biClrUsed = 256;
    for (i = 0; i < 256; i++) {
        bmi.colors[i].rgbRed = (BYTE)(palette[i] >> 16);
        bmi.colors[i].rgbGreen = (BYTE)(palette[i] >> 8);
        bmi.colors[i].rgbBlue = (BYTE)palette[i];
        bmi.colors[i].rgbReserved = 0;
    }
    for (y = 0; y < height; y++)
        memcpy(pixels + (size_t)y * (size_t)stride, src + (size_t)y * (size_t)width, (size_t)width);
    dc = GetDC(window);
    paint(dc);
    ReleaseDC(window, dc);
}

/* ---- the clock ---- */

uint64_t plat_micros(void)
{
    static LARGE_INTEGER freq;
    LARGE_INTEGER now;
    if (!freq.QuadPart)
        QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return (uint64_t)(now.QuadPart / freq.QuadPart * 1000000 +
                      now.QuadPart % freq.QuadPart * 1000000 / freq.QuadPart);
}

void plat_sleep_ms(int ms)
{
    Sleep((DWORD)ms);
}

/* ---- audio: waveOut, a few short buffers refilled by a thread ---- */

#define AUDIO_BUFS 4
#define AUDIO_FRAMES 1024               /* about 23 ms each at 44.1 kHz */

static CRITICAL_SECTION audio_cs;
static HWAVEOUT wave;
static HANDLE audio_event;
static WAVEHDR hdrs[AUDIO_BUFS];
static int16_t bufs[AUDIO_BUFS][AUDIO_FRAMES * 2];
static PlatAudioFill audio_fill;
static void *audio_user;

static void fill_and_queue(WAVEHDR *h)
{
    EnterCriticalSection(&audio_cs);
    audio_fill((int16_t *)h->lpData, AUDIO_FRAMES, audio_user);
    LeaveCriticalSection(&audio_cs);
    waveOutWrite(wave, h, sizeof *h);
}

static DWORD WINAPI audio_thread(LPVOID arg)
{
    int i;
    (void)arg;
    for (;;) {
        WaitForSingleObject(audio_event, INFINITE);
        for (i = 0; i < AUDIO_BUFS; i++)
            if (hdrs[i].dwFlags & WHDR_DONE)
                fill_and_queue(&hdrs[i]);
    }
}

int plat_audio_start(int rate, PlatAudioFill fill, void *user)
{
    WAVEFORMATEX fmt;
    int i;

    InitializeCriticalSection(&audio_cs);
    audio_fill = fill;
    audio_user = user;
    memset(&fmt, 0, sizeof fmt);
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 2;
    fmt.nSamplesPerSec = (DWORD)rate;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = 4;
    fmt.nAvgBytesPerSec = (DWORD)rate * 4;
    audio_event = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (waveOutOpen(&wave, WAVE_MAPPER, &fmt, (DWORD_PTR)audio_event, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR)
        return 0;
    for (i = 0; i < AUDIO_BUFS; i++) {
        memset(&hdrs[i], 0, sizeof hdrs[i]);
        hdrs[i].lpData = (LPSTR)bufs[i];
        hdrs[i].dwBufferLength = sizeof bufs[i];
        waveOutPrepareHeader(wave, &hdrs[i], sizeof hdrs[i]);
        fill_and_queue(&hdrs[i]);
    }
    CreateThread(NULL, 0, audio_thread, NULL, 0, NULL);
    return 1;
}

void plat_audio_lock(void)
{
    if (wave)
        EnterCriticalSection(&audio_cs);
}

void plat_audio_unlock(void)
{
    if (wave)
        LeaveCriticalSection(&audio_cs);
}
