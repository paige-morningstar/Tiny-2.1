/* main.c - TINY 2.1 for the Nintendo 3DS.

   The same little transformer as TINY.C for DOS, with the DOS screen and PC
   speaker replaced by the 3DS: the conversation on the top screen, the neural
   monitor on the bottom one, the software keyboard for questions, and the
   robot voice synthesised into the DSP.

   The screens are drawn straight into the framebuffer with the 8x8 system
   font, double buffered.  libctru's console is not used: its cursor, wrapping
   and scrolling get in the way of a full redraw every frame.

   Build with `make` in this folder; `make cia` also makes an installable title.
*/

#include <3ds.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "tiny_model.h"

#define TW 50                       /* top screen is 50 x 30 characters    */
#define TH 30
#define BW 40                       /* bottom screen is 40 x 30 characters */
#define BH 30

#define CHAT_TOP 2
#define CHAT_ROWS 26
#define CHAT_W 46
#define CHAT_MAX 200
#define CHAT_LINE 64
#define ANS_MAX 160

#define PX_STRIDE 240               /* the framebuffer is 240 wide in memory */

/* the 8x8 system font that libctru's console draws with: 256 glyphs, 8 bytes each */
extern const unsigned char default_font_bin[];

#define RGB(r, g, b) ((u16)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xF8) >> 3)))

static const u16 C_BG      = RGB(3, 6, 18);
static const u16 C_RULE    = RGB(0, 34, 52);
static const u16 C_BAR     = RGB(0, 44, 96);
static const u16 C_BARCYAN = RGB(0, 96, 120);
static const u16 C_CYAN    = RGB(0, 245, 225);
static const u16 C_MINT    = RGB(85, 255, 160);
static const u16 C_PINK    = RGB(255, 90, 200);
static const u16 C_AMBER   = RGB(255, 195, 70);
static const u16 C_WHITE   = RGB(235, 245, 255);
static const u16 C_DIM     = RGB(96, 118, 148);
static const u16 C_RED     = RGB(255, 80, 80);
static const u16 C_MAG     = RGB(150, 0, 105);

typedef struct {
    u16 *fb;
    int cols, rows;
} Screen;

static Screen top_scr, bot_scr;

/* ---- drawing ------------------------------------------------------ */

static void screens_reload(void)
{
    top_scr.fb = (u16 *)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
    bot_scr.fb = (u16 *)gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
}

static void clear_scr(Screen *s, u16 bg)
{
    u32 pair = (u32)bg | ((u32)bg << 16);
    u32 *p = (u32 *)s->fb;
    int n = (s->cols * 8) * PX_STRIDE / 2, i;   /* pixels, not characters */

    for (i = 0; i < n; i++)
        p[i] = pair;
}

/* One 8x8 glyph.  This is libctru's console blit exactly: a run of eight
   pixels takes one bit from each of the eight font bytes (b8..b1), and the
   bit chosen by `mask` walks down the bytes as the run steps across. */
static void glyph(Screen *s, int cx, int cy, u16 fg, u16 bg, int ch)
{
    const unsigned char *g = default_font_bin + ((unsigned)ch & 0xFF) * 8;
    int x = cx * 8, y = cy * 8, i, k;
    u16 *dst;

    if (cx < 0 || cx >= s->cols || cy < 0 || cy >= s->rows)
        return;
    dst = s->fb + (x * PX_STRIDE) + (PX_STRIDE - 1 - (y + 7));
    for (i = 0; i < 8; i++) {
        unsigned char mask = (unsigned char)(0x80 >> i);
        for (k = 0; k < 8; k++)
            *(dst++) = (g[7 - k] & mask) ? fg : bg;
        dst += PX_STRIDE - 8;
    }
}

/* text at a cell; spaces are skipped because the screen is cleared first */
static void text(Screen *s, int cx, int cy, u16 fg, u16 bg, const char *str)
{
    for (; *str && cx < s->cols; str++, cx++)
        if (*str != ' ')
            glyph(s, cx, cy, fg, bg, *str);
}

static void textf(Screen *s, int cx, int cy, u16 fg, u16 bg, const char *fmt, ...)
{
    char buf[128];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    text(s, cx, cy, fg, bg, buf);
}

static void fill_row(Screen *s, int cy, u16 bg, int w)
{
    int i;
    if (cy < 0 || cy >= s->rows) return;
    if (w > s->cols) w = s->cols;
    for (i = 0; i < w; i++)
        glyph(s, i, cy, bg, bg, ' ');
}

static void flush_ui(void);

/* ---- sound -------------------------------------------------------- */

#define SR 22050
#define AMP 2600
#define VOICE_FRAMES (SR * 6)       /* six seconds of robot voice */
#define FX_FRAMES    (SR / 2)

static int snd_ok, dsp_on;
static s16 *vox;                    /* the answer's voice, or a chime */
static s16 *fx;                     /* scratch for short blips       */
static ndspWaveBuf vox_wb, fx_wb;

static int vpos;                    /* frames written to vox */
static int vox_cue[ANS_MAX];        /* when each letter starts, in ms */
static int vox_n;

static int sound_on = 1, voice_on = 1;

static void snd_channel(int ch)
{
    float mix[12];

    ndspChnSetInterp(ch, NDSP_INTERP_LINEAR);
    ndspChnSetRate(ch, (float)SR);
    ndspChnSetFormat(ch, NDSP_FORMAT_MONO_PCM16);
    memset(mix, 0, sizeof mix);
    mix[0] = 1.0f;
    mix[1] = 1.0f;
    ndspChnSetMix(ch, mix);
}

static void snd_init(void)
{
    snd_ok = 0;
    if (R_FAILED(ndspInit()))
        return;                      /* no DSP: TINY runs silently */
    dsp_on = 1;
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    snd_channel(0);
    snd_channel(1);
    vox = (s16 *)linearAlloc(VOICE_FRAMES * sizeof(s16));
    fx = (s16 *)linearAlloc(FX_FRAMES * sizeof(s16));
    if (vox == NULL || fx == NULL)
        return;
    snd_ok = 1;
}

static void snd_exit(void)
{
    if (dsp_on) {
        ndspChnSetPaused(0, true);
        ndspChnSetPaused(1, true);
        ndspChnWaveBufClear(0);
        ndspChnWaveBufClear(1);
        ndspExit();
        dsp_on = 0;
    }
    if (vox) linearFree(vox);
    if (fx) linearFree(fx);
    vox = NULL;
    fx = NULL;
    snd_ok = 0;
}

static int ms_frames(int ms) { return ms * SR / 1000; }

static void vox_put(int v)
{
    if (vpos < VOICE_FRAMES)
        vox[vpos++] = (s16)v;
}

static void vox_silence(int ms)
{
    int n = ms_frames(ms);
    while (n-- > 0)
        vox_put(0);
}

static void vox_square(u32 hz, int ms, int amp, u32 shift)
{
    int n = ms_frames(ms), i;
    u32 period;

    hz = (u32)((unsigned long long)hz * shift / 100);
    if (hz < 30) hz = 30;
    period = SR / hz;
    if (period < 2) period = 2;
    for (i = 0; i < n; i++)
        vox_put((i % period) < period / 2 ? amp : -amp);
}

static void vox_buzz(u32 f1, u32 f2, int ms, u32 shift)
{
    int t;
    for (t = 0; t < ms; t += 4) {
        vox_square(f1, 2, AMP, shift);
        vox_square(f2, 2, AMP, shift);
    }
}

static void vox_hiss(int ms)
{
    int n = ms_frames(ms), i;
    for (i = 0; i < n; i++)
        vox_put((rand() % 20001) - 10000);
}

/* Not real speech: every letter becomes a little sound with the rhythm of
   speech.  Vowels buzz between two pitches, s and f hiss, p and t click,
   m and n hum.  The pitch slowly falls through a sentence. */
static void vox_letter(int c, u32 shift)
{
    if (vox_n < ANS_MAX)
        vox_cue[vox_n] = vpos * 1000 / SR;
    vox_n++;

    if (c >= 'A' && c <= 'Z') c += 32;
    switch (c) {
    case 'a': vox_buzz(730, 1090, 64, shift); break;
    case 'e': vox_buzz(530, 1840, 56, shift); break;
    case 'i': case 'y': vox_buzz(300, 2290, 52, shift); break;
    case 'o': vox_buzz(570, 840, 64, shift); break;
    case 'u': vox_buzz(320, 870, 56, shift); break;
    case 's': case 'z': case 'c': case 'x': vox_hiss(46); break;
    case 'f': case 'h': case 'v': case 'j': vox_hiss(30); break;
    case 'p': case 't': case 'k': case 'q':
        vox_square(3200, 2, AMP, 100); vox_silence(18); break;
    case 'b': case 'd': case 'g':
        vox_buzz(180, 260, 12, shift); vox_silence(10); break;
    case 'm': case 'n': vox_buzz(210, 300, 40, shift); break;
    case 'l': case 'r': case 'w': vox_buzz(400, 1100, 36, shift); break;
    case ' ': vox_silence(45); break;
    case ',': case '-': vox_silence(110); break;
    case '.': case '!': case '?': case ':': vox_silence(200); break;
    default:
        if (c >= '0' && c <= '9')
            vox_buzz((u32)(420 + (c - '0') * 60),
                     (u32)(900 + (c - '0') * 90), 70, shift);
        else
            vox_silence(20);
        break;
    }
}

static void vox_build(const char *a)
{
    int i, n = (int)strlen(a);

    vpos = 0;
    vox_n = 0;
    for (i = 0; i < n; i++)
        vox_letter(a[i], (u32)(115 - 30L * i / (n + 1)));
}

static int vox_play(void)
{
    if (!snd_ok || !sound_on || vpos <= 0)
        return 0;
    DSP_FlushDataCache(vox, (u32)vpos * sizeof(s16));
    ndspChnSetPaused(0, true);
    ndspChnWaveBufClear(0);
    memset(&vox_wb, 0, sizeof vox_wb);
    vox_wb.data_pcm16 = vox;
    vox_wb.nsamples = (u32)vpos;
    vox_wb.looping = false;
    ndspChnWaveBufAdd(0, &vox_wb);
    ndspChnSetPaused(0, false);
    return 1;
}

static void vox_stop(void)
{
    if (!snd_ok)
        return;
    ndspChnSetPaused(0, true);
    ndspChnWaveBufClear(0);
}

static void chime(void)
{
    static const u32 notes[] = { 392, 523, 659, 784, 1047, 1319 };
    int i;

    if (!snd_ok || !sound_on)
        return;
    vpos = 0;
    vox_n = 0;
    for (i = 0; i < (int)(sizeof notes / sizeof notes[0]); i++) {
        vox_square(notes[i], 55, AMP / 2, 100);
        vox_silence(6);
    }
    vox_square(1568, 140, AMP / 2, 100);
    vox_play();
}

static void beep(u32 hz, int ms)
{
    int n, i;
    u32 period;

    if (!snd_ok || !sound_on)
        return;
    n = ms_frames(ms);
    if (n > FX_FRAMES) n = FX_FRAMES;
    if (n <= 0) return;
    if (hz < 30) hz = 30;
    period = SR / hz;
    if (period < 2) period = 2;
    for (i = 0; i < n; i++)
        fx[i] = ((i % period) < period / 2) ? AMP / 2 : -AMP / 2;
    DSP_FlushDataCache(fx, (u32)n * sizeof(s16));
    ndspChnSetPaused(1, true);
    ndspChnWaveBufClear(1);
    memset(&fx_wb, 0, sizeof fx_wb);
    fx_wb.data_pcm16 = fx;
    fx_wb.nsamples = (u32)n;
    ndspChnWaveBufAdd(1, &fx_wb);
    ndspChnSetPaused(1, false);
}

static void sweep(u32 from, u32 to, int ms)
{
    int n, i;
    u32 period;

    if (!snd_ok || !sound_on)
        return;
    n = ms_frames(ms);
    if (n > FX_FRAMES) n = FX_FRAMES;
    if (n <= 0) return;
    for (i = 0; i < n; i++) {
        u32 hz = (u32)((long)from + ((long)to - (long)from) * i / n);
        if (hz < 30) hz = 30;
        period = SR / hz;
        if (period < 2) period = 2;
        fx[i] = ((i % period) < period / 2) ? AMP / 2 : -AMP / 2;
    }
    DSP_FlushDataCache(fx, (u32)n * sizeof(s16));
    ndspChnSetPaused(1, true);
    ndspChnWaveBufClear(1);
    memset(&fx_wb, 0, sizeof fx_wb);
    fx_wb.data_pcm16 = fx;
    fx_wb.nsamples = (u32)n;
    ndspChnWaveBufAdd(1, &fx_wb);
    ndspChnSetPaused(1, false);
}

/* ---- the conversation --------------------------------------------- */

enum { R_SYS, R_YOU, R_TINY, R_HELP };

static char chat[CHAT_MAX][CHAT_LINE];
static unsigned char chat_role[CHAT_MAX];
static int chat_n;

/* where the answer letters landed on screen, so the voice can light them up */
static int ans_base, ans_n;
static int ans_off[ANS_MAX], ans_col[ANS_MAX];
static int drawn_first;

static int speak_letter = -1;
static int speak_line, speak_col;

static int reveal_active;
static int rev_line = -1, rev_col = -1;

static char last_q[TINY_MAXQ + 1];
static char status_msg[48];
static int speed10;
static int answer_letters;
static long answer_ms;

static float act_max[8];
static int heat[18], heat_n;

static void status(const char *s)
{
    snprintf(status_msg, sizeof status_msg, "%s", s);
}

static int chat_add(int role, const char *s)
{
    if (chat_n == CHAT_MAX) {
        memmove(chat[0], chat[1], sizeof(chat[0]) * (CHAT_MAX - 1));
        memmove(chat_role, chat_role + 1, CHAT_MAX - 1);
        chat_n--;
        if (ans_base > 0) ans_base--;
    }
    snprintf(chat[chat_n], CHAT_LINE, "%s", s);
    chat_role[chat_n] = (unsigned char)role;
    return chat_n++;
}

static void chat_wrap(int role, const char *lead, const char *text_in, int indent)
{
    char buf[CHAT_LINE];
    int n, i;

    snprintf(buf, sizeof buf, "%s", lead);
    n = (int)strlen(buf);
    for (i = 0; text_in[i]; i++) {
        if (n >= CHAT_W) {
            chat_add(role, buf);
            memset(buf, ' ', indent);
            buf[indent] = 0;
            n = indent;
        }
        buf[n++] = text_in[i];
        buf[n] = 0;
    }
    chat_add(role, buf);
}

static int chat_char(int role, int c, int indent)
{
    char *s = chat[chat_n - 1];
    int n = (int)strlen(s);

    if (n >= CHAT_W) {
        char buf[CHAT_LINE];
        memset(buf, ' ', indent);
        buf[indent] = 0;
        chat_add(role, buf);
        s = chat[chat_n - 1];
        n = indent;
    }
    s[n] = (char)c;
    s[n + 1] = 0;
    return n;
}

static u16 role_color(int role)
{
    switch (role) {
    case R_YOU:  return C_PINK;
    case R_TINY: return C_MINT;
    case R_HELP: return C_CYAN;
    default:     return C_DIM;
    }
}

static void meter(char *out, int w, int v)
{
    int i, n = (int)((long)v * w / 1000);
    if (n < 0) n = 0;
    if (n > w) n = w;
    for (i = 0; i < w; i++)
        out[i] = i < n ? '#' : '.';
    out[w] = 0;
}

static void draw_top(void)
{
    int i, first;
    Screen *s = &top_scr;

    clear_scr(s, C_BG);

    fill_row(s, 0, C_BAR, TW);
    text(s, 1, 0, C_WHITE, C_BAR, "T I N Y  2.1");
    text(s, TW - 20, 0, C_WHITE, C_BAR, "NEURAL INTERFACE 3DS");
    fill_row(s, 1, C_RULE, TW);

    first = chat_n > CHAT_ROWS ? chat_n - CHAT_ROWS : 0;
    drawn_first = first;
    for (i = first; i < chat_n; i++) {
        int y = CHAT_TOP + (i - first);
        text(s, 0, y, role_color(chat_role[i]), C_BG, chat[i]);
    }

    /* the letters that are being shown off: newest one, or the spoken one */
    if (reveal_active && rev_line >= first && rev_line < chat_n && rev_col >= 0) {
        int y = CHAT_TOP + (rev_line - first);
        int ch = (unsigned char)chat[rev_line][rev_col];
        if (ch)
            glyph(s, rev_col, y, C_BG, C_CYAN, ch);
    }
    if (speak_letter >= 0 && speak_line >= first && speak_line < chat_n && speak_col >= 0) {
        int y = CHAT_TOP + (speak_line - first);
        int ch = (unsigned char)chat[speak_line][speak_col];
        if (ch)
            glyph(s, speak_col, y, C_WHITE, C_MAG, ch);
    }

    text(s, 1, 28, C_DIM, C_BG, "A:ask X:snd Y:voice L/R:creat SEL:help START:quit");
    fill_row(s, 29, C_BARCYAN, TW);
    text(s, 1, 29, C_WHITE, C_BARCYAN, status_msg[0] ? status_msg : "ready");
}

static void monitor_tick(void)
{
    int l;
    for (l = 0; l < 8; l++) {
        float a = tiny_layer_act(l);
        if (a > act_max[l]) act_max[l] = a;
        act_max[l] *= 0.98f;
    }
}

static void draw_monitor(void)
{
    int l, i, v;
    char m[40];
    Screen *s = &bot_scr;

    clear_scr(s, C_BG);

    fill_row(s, 0, C_BARCYAN, BW);
    text(s, 1, 0, C_BG, C_BARCYAN, "TINY 2.1");
    text(s, BW - 14, 0, C_BG, C_BARCYAN, "NEURAL MONITOR");

    text(s, 1, 2, C_DIM, C_BG, "LAYER ACTIVITY");
    for (l = 0; l < tiny_layers() && l < 6; l++) {
        float a = tiny_layer_act(l);
        v = act_max[l] > 0.0f ? (int)(1000.0f * a / act_max[l]) : 0;
        meter(m, 20, v);
        textf(s, 1, 3 + l, C_CYAN, C_BG, "L%d", l + 1);
        text(s, 5, 3 + l, l % 2 ? C_CYAN : C_MINT, C_BG, m);
    }

    v = (int)(tiny_conf() * 1000.0f);
    meter(m, 18, v);
    text(s, 1, 8, C_DIM, C_BG, "CONFIDENCE");
    text(s, 1, 9, v > 800 ? C_MINT : v > 500 ? C_AMBER : C_RED, C_BG, m);
    textf(s, 20, 9, C_WHITE, C_BG, "%3d%%", v / 10);

    text(s, 1, 11, C_DIM, C_BG, "LAST LETTERS");
    for (i = 0; i < 18; i++) {
        if (i >= heat_n) {
            glyph(s, 1 + i, 12, C_RULE, C_BG, '.');
            continue;
        }
        v = heat[i];
        glyph(s, 1 + i, 12, v > 800 ? C_MINT : v > 500 ? C_AMBER : C_RED, C_BG,
              v > 900 ? '#' : v > 700 ? '=' : v > 450 ? '+' : ':');
    }

    text(s, 1, 14, C_DIM, C_BG, "SPEED");
    if (answer_letters > 0)
        textf(s, 1, 15, C_WHITE, C_BG, "%d.%d letters/s  %d ltr",
              speed10 / 10, speed10 % 10, answer_letters);
    else
        text(s, 1, 15, C_DIM, C_BG, "idle");

    text(s, 1, 17, C_DIM, C_BG, "NETWORK");
    textf(s, 1, 18, C_CYAN, C_BG, "%d layers x %d wide", tiny_layers(), tiny_width());

    text(s, 1, 20, C_DIM, C_BG, "MODE");
    textf(s, 1, 21, C_WHITE, C_BG, "creativity %d", tiny_creativity());
    textf(s, 1, 22, C_WHITE, C_BG, "sound %s  voice %s",
          sound_on ? "ON" : "OFF", voice_on ? "ON" : "OFF");

    textf(s, 1, 24, C_DIM, C_BG, "%d weights, 8-bit", tiny_weights());
    textf(s, 1, 26, C_DIM, C_BG, "%ld ms to answer", answer_ms);
    text(s, 1, 28, C_DIM, C_BG, "synthesised PC-speaker voice");
}

static void render_all(void)
{
    draw_top();
    draw_monitor();
}

static void flush_ui(void)
{
    render_all();
    gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();
    screens_reload();
}

/* ---- asking ------------------------------------------------------- */

static u32 keys_down(void)
{
    hidScanInput();
    return hidKeysDown();
}

static int quit_now;
static int abort_answer;

static int poll_abort(void)
{
    u32 k;

    if (!aptMainLoop()) {
        quit_now = 1;
        return 1;
    }
    k = keys_down();
    if (k & KEY_START) {
        quit_now = 1;
        return 1;
    }
    if (k & KEY_B)
        abort_answer = 1;
    return abort_answer;
}

static void ui_step(int i, int n)
{
    monitor_tick();
    if (n > 0)
        snprintf(status_msg, sizeof status_msg, "reading the question %d%%",
                 100 * (i + 1) / n);
    else
        snprintf(status_msg, sizeof status_msg, "thinking");
    if (i % 2 == 0)
        beep((u32)(2400 + (rand() % 8) * 150), 3);
    flush_ui();
}

static void ui_letter(int c)
{
    int col = chat_char(R_TINY, c, 6);

    if (ans_n < ANS_MAX) {
        ans_off[ans_n] = chat_n - 1 - ans_base;
        ans_col[ans_n] = col;
        ans_n++;
    }
    rev_line = chat_n - 1;
    rev_col = col;

    if (heat_n < 18)
        heat[heat_n++] = (int)(tiny_conf() * 1000.0f);
    else {
        int k;
        for (k = 0; k < 17; k++) heat[k] = heat[k + 1];
        heat[17] = (int)(tiny_conf() * 1000.0f);
    }
    monitor_tick();
    if (!voice_on)
        beep((u32)(900 + (c % 16) * 70), 6);
    flush_ui();
}

/* play the answer's voice and light up each letter as it is spoken */
static void speak(void)
{
    int i, total;
    u64 t0;

    if (!vox_play())
        return;
    total = vpos * 1000 / SR;
    t0 = svcGetSystemTick();
    for (;;) {
        int el = (int)((svcGetSystemTick() - t0) * 1000ULL / (u64)SYSCLOCK_ARM11);
        int k = -1;
        u32 pressed;

        for (i = 0; i < vox_n && i < ans_n; i++)
            if (vox_cue[i] <= el) k = i;
        if (k >= 0) {
            speak_letter = k;
            speak_line = ans_base + ans_off[k];
            speak_col = ans_col[k];
        }
        flush_ui();
        if (el > total + 40)
            break;
        if (!aptMainLoop()) {
            quit_now = 1;
            break;
        }
        pressed = keys_down();
        if (pressed & (KEY_B | KEY_START)) {
            if (pressed & KEY_START)
                quit_now = 1;
            break;
        }
        svcSleepThread(4000000LL);
    }
    vox_stop();
    speak_letter = -1;
}

static void ask(const char *q)
{
    static char out[ANS_MAX];
    long ms = 0;
    int letters;

    if (chat_n > 0)
        chat_add(R_SYS, "");
    chat_wrap(R_YOU, "YOU>  ", q, 6);

    chat_add(R_TINY, "TINY> ");
    ans_base = chat_n - 1;
    ans_n = 0;
    heat_n = 0;
    rev_line = -1;
    rev_col = -1;
    memset(act_max, 0, sizeof act_max);
    reveal_active = 1;
    abort_answer = 0;
    speak_letter = -1;

    status("thinking...");
    flush_ui();

    tiny_set_abort(poll_abort);
    letters = tiny_answer(q, out, &ms, ui_step, ui_letter);
    tiny_set_abort(NULL);
    reveal_active = 0;

    if (letters == 0) {
        int col = chat_char(R_TINY, '.', 6);
        rev_line = chat_n - 1;
        rev_col = col;
        out[0] = '.';
        out[1] = 0;
    }

    answer_letters = letters;
    answer_ms = ms;
    speed10 = ms > 0 ? (int)(100L * letters / ms) : 0;

    sprintf(status_msg, "%d letters in %ld.%lds", letters, ms / 1000, (ms % 1000) / 100);
    flush_ui();

    if (!quit_now && letters > 0) {
        if (voice_on && sound_on) {
            vox_build(out);
            flush_ui();
            speak();
        } else {
            beep(1400, 30);
        }
    }
    rev_line = -1;
    rev_col = -1;
    flush_ui();
}

static void show_help(void)
{
    chat_add(R_SYS, "");
    chat_wrap(R_HELP, " HELP  ", "Press A and type a question, then Enter.", 8);
    chat_wrap(R_HELP, "       ", "Try: capital of peru / is a whale a fish", 8);
    chat_wrap(R_HELP, "       ", "     what is 7 times 8 / spell elephant", 8);
    chat_wrap(R_HELP, "       ", "     which countries border estonia", 8);
    chat_wrap(R_HELP, "       ", "     how do you say thank you in spanish", 8);
    chat_wrap(R_HELP, " KEYS  ", "X sound  Y robot voice  L/R creativity", 8);
    chat_wrap(R_HELP, "       ", "UP repeats a question, B stops the voice", 8);
    chat_wrap(R_HELP, " WORDS ", "HELP, CLS and QUIT work as questions.", 8);
}

/* ---- boot --------------------------------------------------------- */

static const char *logo[5] = {
    "#####  ###  #   #  #   #",
    "  #     #   ##  #   # # ",
    "  #     #   # # #    #  ",
    "  #     #   #  ##    #  ",
    "  #    ###  #   #    #  "
};

static const u16 logo_col[5] = { 0, 1, 2, 3, 4 };

static char rain_grid[TH][TW + 1];

static void boot_rain(int frames)
{
    int col[TW], spd[TW], f, x, y, i;

    memset(rain_grid, ' ', sizeof rain_grid);
    for (x = 0; x < TW; x++) {
        col[x] = -(rand() % 24);
        spd[x] = 1 + rand() % 2;
    }
    for (f = 0; f < frames; f++) {
        gspWaitForVBlank();
        for (x = 0; x < TW - 1; x += 2) {
            y = col[x];
            if (y >= 0 && y < TH) rain_grid[y][x] = (char)(33 + rand() % 90);
            if (y + 1 >= 0 && y + 1 < TH) rain_grid[y + 1][x] = (char)(33 + rand() % 90);
            if (y - 6 >= 0 && y - 6 < TH) rain_grid[y - 6][x] = ' ';
            col[x] += spd[x];
            if (col[x] > TH + 6)
                col[x] = -(rand() % 8);
        }
        clear_scr(&top_scr, C_BG);
        for (y = 0; y < TH; y++)
            for (i = 0; i < TW; i++)
                if (rain_grid[y][i] != ' ')
                    glyph(&top_scr, i, y, i == col[y % TW] ? C_WHITE : C_MINT, C_BG,
                          rain_grid[y][i]);
        gfxFlushBuffers();
        gfxSwapBuffers();
        screens_reload();
        if (f > 3 && f % 3 == 0) {
            if (keys_down())
                break;
            beep((u32)(2400 + rand() % 1500), 3);
        }
    }
}

static void boot_progress(int done, int total)
{
    int i, n = 30 * done / total;
    char b[36];

    for (i = 0; i < 30; i++)
        b[i] = i < n ? '#' : '.';
    b[30] = 0;

    fill_row(&bot_scr, 17, C_BG, BW);
    textf(&bot_scr, 1, 17, C_CYAN, C_BG, "[%s] %3d%%", b, 100 * done / total);
    text(&bot_scr, 1, 18, C_DIM, C_BG, "loading neural weights from TINY.BIN");
    gfxFlushBuffers();
    gfxSwapBuffers();
    screens_reload();
}

static void boot(void)
{
    int y, i;
    Screen *s = &top_scr;

    clear_scr(&top_scr, C_BG);
    clear_scr(&bot_scr, C_BG);
    boot_rain(20);

    clear_scr(s, C_BG);
    for (y = 0; y < 5; y++)
        for (i = 0; i < 24; i++)
            if (logo[y][i] == '#')
                glyph(s, 13 + i, 3 + y,
                      logo_col[y] == 2 ? C_CYAN : logo_col[y] >= 3 ? C_PINK : C_MINT,
                      C_BG, '#');
    text(s, 10, 10, C_WHITE, C_BG, "N E U R A L   I N T E R F A C E");
    text(s, 11, 11, C_DIM, C_BG, "version 2.1  for the Nintendo 3DS");
    fill_row(s, 13, C_RULE, 40);

    clear_scr(&bot_scr, C_BG);
    fill_row(&bot_scr, 2, C_BARCYAN, 12);
    text(&bot_scr, 1, 2, C_BG, C_BARCYAN, "TINY 2.1");
    text(&bot_scr, 1, 4, C_DIM, C_BG, "a neural network in your pocket");

    chime();
    gfxFlushBuffers();
    gfxSwapBuffers();
    screens_reload();
}

/* ---- main --------------------------------------------------------- */

static const char *find_model(void)
{
    static const char *cand[] = {
        "sdmc:/3ds/TINY/TINY.BIN",
        "romfs:/TINY.BIN"
    };
    FILE *fp;
    int i;

    for (i = 0; i < (int)(sizeof cand / sizeof cand[0]); i++) {
        fp = fopen(cand[i], "rb");
        if (fp) {
            fclose(fp);
            return cand[i];
        }
    }
    return NULL;
}

int main(int argc, char **argv)
{
    char line[128], q[TINY_MAXQ + 1];
    const char *model;
    int i;

    (void)argc;
    (void)argv;

    gfxInitDefault();
    gfxSetScreenFormat(GFX_TOP, GSP_RGB565_OES);
    gfxSetScreenFormat(GFX_BOTTOM, GSP_RGB565_OES);
    gfxSetDoubleBuffering(GFX_TOP, true);
    gfxSetDoubleBuffering(GFX_BOTTOM, true);

    top_scr.cols = TW;
    top_scr.rows = TH;
    bot_scr.cols = BW;
    bot_scr.rows = BH;
    screens_reload();

    srand((unsigned)svcGetSystemTick());
    snd_init();
    fsInit();
    romfsInit();

    boot();

    model = find_model();
    if (model == NULL || tiny_load(model, boot_progress) != 0) {
        clear_scr(&top_scr, C_BG);
        text(&top_scr, 2, 4, C_RED, C_BG, "TINY cannot start.");
        textf(&top_scr, 2, 6, C_WHITE, C_BG, "%s",
              model ? tiny_error() : "TINY.BIN was not found.");
        text(&top_scr, 2, 8, C_DIM, C_BG, "Put it at sdmc:/3ds/TINY/TINY.BIN");
        text(&top_scr, 2, 9, C_DIM, C_BG, "or keep it in the app's RomFS.");
        text(&top_scr, 2, 12, C_WHITE, C_BG, "Press START to exit.");
        gfxFlushBuffers();
        gfxSwapBuffers();
        screens_reload();
        while (aptMainLoop()) {
            gspWaitForVBlank();
            if (keys_down() & KEY_START)
                break;
        }
        snd_exit();
        romfsExit();
        fsExit();
        gfxExit();
        return 0;
    }

    clear_scr(&bot_scr, C_BG);
    fill_row(&bot_scr, 2, C_BARCYAN, 12);
    text(&bot_scr, 1, 2, C_BG, C_BARCYAN, "TINY 2.1");
    text(&bot_scr, 1, 17, C_MINT, C_BG, "SYSTEM READY");
    textf(&bot_scr, 1, 18, C_DIM, C_BG, "%d layers, %d wide", tiny_layers(), tiny_width());
    textf(&bot_scr, 1, 19, C_DIM, C_BG, "%d weights, 8-bit", tiny_weights());
    text(&top_scr, 11, 16, C_WHITE, C_BG, "press any button to begin");
    gfxFlushBuffers();
    gfxSwapBuffers();
    screens_reload();
    beep(1760, 70);

    chat_add(R_SYS, "Hello! I am TINY, a neural network in your 3DS.");
    chat_add(R_SYS, "Press A to ask about countries, animals,");
    chat_add(R_SYS, "space, words or sums. SELECT lists the keys.");
    status("ready");

    while (aptMainLoop()) {
        gspWaitForVBlank();
        if (keys_down())
            break;
    }
    flush_ui();

    while (!quit_now) {
        u32 k;

        if (!aptMainLoop())
            break;
        k = keys_down();

        if (k & KEY_START)
            break;
        if (k & KEY_SELECT) {
            beep(1800, 2);
            show_help();
            flush_ui();
            continue;
        }
        if (k & KEY_X) {
            sound_on = !sound_on;
            beep(1800, 2);
            status(sound_on ? "sound on" : "sound off");
            flush_ui();
            continue;
        }
        if (k & KEY_Y) {
            voice_on = !voice_on;
            beep(1800, 2);
            status(voice_on ? "robot voice on" : "robot voice off");
            flush_ui();
            continue;
        }
        if (k & KEY_L) {
            tiny_set_creativity(tiny_creativity() - 1);
            beep(1200, 2);
            flush_ui();
            continue;
        }
        if (k & KEY_R) {
            tiny_set_creativity(tiny_creativity() + 1);
            beep(1600, 2);
            flush_ui();
            continue;
        }
        if (k & KEY_UP) {
            beep(1800, 2);
            if (last_q[0] == 0) {
                status("no question yet");
                flush_ui();
                continue;
            }
            ask(last_q);
            status("ready");
            continue;
        }
        if (k & KEY_A) {
            static SwkbdState sw;
            SwkbdButton b;

            line[0] = 0;
            swkbdInit(&sw, SWKBD_TYPE_NORMAL, 2, -1);
            swkbdSetHintText(&sw, "Ask TINY a question");
            swkbdSetInitialText(&sw, last_q);
            swkbdSetFeatures(&sw, SWKBD_PREDICTIVE_INPUT);
            gfxFlushBuffers();
            gfxSwapBuffers();
            b = swkbdInputText(&sw, line, sizeof line);
            screens_reload();
            if (b != SWKBD_BUTTON_RIGHT || tiny_clean(line, q) == 0) {
                flush_ui();
                continue;
            }
            if (tiny_is_word(q, "quit") || tiny_is_word(q, "exit"))
                break;
            if (tiny_is_word(q, "cls")) {
                chat_n = 0;
                ans_base = 0;
                ans_n = 0;
                heat_n = 0;
                rev_line = rev_col = -1;
                answer_letters = 0;
                status("cleared");
                flush_ui();
                continue;
            }
            if (tiny_is_word(q, "help")) {
                show_help();
                flush_ui();
                continue;
            }
            for (i = 0; q[i]; i++)
                last_q[i] = q[i];
            last_q[i] = 0;
            ask(q);
            status("ready");
            continue;
        }

        flush_ui();
    }

    status("goodbye");
    flush_ui();
    sweep(1600, 200, 320);
    for (i = 0; i < 10; i++)
        gspWaitForVBlank();

    snd_exit();
    romfsExit();
    fsExit();
    gfxExit();
    return 0;
}
