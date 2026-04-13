#include "audio.h"
#include <SDL.h>
#ifdef _WIN32
#include <SDL_syswm.h>
#endif
#include <math.h>
#include <stdio.h>
#include <string.h>

#define WINDOW_W 700
#define WINDOW_H 400
#define MARGIN   25

#define WAVE_X   MARGIN
#define WAVE_Y   60
#define WAVE_W   (WINDOW_W - 2 * MARGIN)
#define WAVE_H   100

#define BTN_X    MARGIN
#define BTN_Y    190
#define BTN_W    140
#define BTN_H    35

#define SLIDER_X 250
#define SLIDER_W (WINDOW_W - SLIDER_X - 60)
#define SLIDER_H 8
#define KNOB_R   7

#define BPM_Y    240
#define CUTOFF_Y 275
#define RESO_Y   310

/* ---- Minimal 5x7 bitmap font ---- */
static const uint8_t FONT_5X7[128][7] = {
    ['0'] = {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},
    ['1'] = {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    ['2'] = {0x0E,0x11,0x01,0x06,0x08,0x10,0x1F},
    ['3'] = {0x0E,0x11,0x01,0x06,0x01,0x11,0x0E},
    ['4'] = {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
    ['5'] = {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
    ['6'] = {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},
    ['7'] = {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    ['8'] = {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
    ['9'] = {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
    ['A'] = {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},
    ['B'] = {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    ['C'] = {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
    ['D'] = {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},
    ['E'] = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
    ['F'] = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    ['G'] = {0x0E,0x11,0x10,0x17,0x11,0x11,0x0E},
    ['H'] = {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    ['I'] = {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
    ['K'] = {0x11,0x12,0x14,0x18,0x14,0x12,0x11},
    ['L'] = {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    ['M'] = {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},
    ['N'] = {0x11,0x19,0x15,0x13,0x11,0x11,0x11},
    ['O'] = {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
    ['P'] = {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    ['R'] = {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    ['S'] = {0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E},
    ['T'] = {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    ['U'] = {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
    ['V'] = {0x11,0x11,0x11,0x11,0x0A,0x0A,0x04},
    ['W'] = {0x11,0x11,0x11,0x15,0x15,0x1B,0x11},
    ['X'] = {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    ['Y'] = {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},
    ['Z'] = {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
    [':'] = {0x00,0x04,0x04,0x00,0x04,0x04,0x00},
    ['.'] = {0x00,0x00,0x00,0x00,0x00,0x00,0x04},
    ['?'] = {0x0E,0x11,0x01,0x02,0x04,0x00,0x04},
    ['+'] = {0x00,0x04,0x04,0x1F,0x04,0x04,0x00},
    ['-'] = {0x00,0x00,0x00,0x1F,0x00,0x00,0x00},
    ['/'] = {0x01,0x02,0x02,0x04,0x08,0x08,0x10},
    ['='] = {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00},
    ['v'] = {0x00,0x00,0x11,0x11,0x0A,0x0A,0x04},
    [' '] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
};

static SDL_Renderer *g_ren;

static void set_color(int r, int g, int b, int a)
{
    SDL_SetRenderDrawColor(g_ren, (Uint8)r, (Uint8)g, (Uint8)b, (Uint8)a);
}

static void fill_rect(int x, int y, int w, int h)
{
    SDL_Rect rc = {x, y, w, h};
    SDL_RenderFillRect(g_ren, &rc);
}

static void draw_text(const char *text, int x, int y, int scale, int r, int g, int b, int a)
{
    set_color(r, g, b, a);
    for (const char *p = text; *p; p++) {
        int ch = (unsigned char)*p;
        if (ch >= 128) ch = ' ';
        const uint8_t *glyph = FONT_5X7[ch];
        for (int row = 0; row < 7; row++) {
            uint8_t bits = glyph[row];
            for (int col = 0; col < 5; col++) {
                if (bits & (0x10 >> col)) {
                    fill_rect(x + col * scale, y + row * scale, scale, scale);
                }
            }
        }
        x += 6 * scale;
    }
}

static int text_width(const char *text, int scale)
{
    int len = (int)strlen(text);
    return len > 0 ? len * 6 * scale - scale : 0;
}

static int point_in_rect(int px, int py, int rx, int ry, int rw, int rh)
{
    return px >= rx && px < rx + rw && py >= ry && py < ry + rh;
}

/* ---- Slider state ---- */
static int dragging_bpm = 0, dragging_cutoff = 0, dragging_reso = 0, dragging_vol = 0;
static float flash_alpha = 0.0f;
static float tap_flash = 0.0f;  /* flash when tap registers */
static int show_help = 0;

/* ---- Tap tempo state ---- */
#define TAP_MAX 4
static Uint32 tap_times[TAP_MAX];
static int tap_count = 0;

/* ---- Save recording with unique filename ---- */
static void save_recording(void)
{
    char fname[128];
    snprintf(fname, sizeof(fname), "narina_out.wav");
    for (int n = 1; n < 9999; n++) {
        FILE *test = fopen(fname, "rb");
        if (!test) break;
        fclose(test);
        snprintf(fname, sizeof(fname), "narina_out(%d).wav", n);
    }
    audio_rec_stop(fname);
}

/* ---- Toggle play/stop (handles rec cleanup) ---- */
static void toggle_play_stop(void)
{
    g_seq.playing = !g_seq.playing;
}

/* ---- Process tap tempo ---- */
static float process_tap(Uint32 now)
{
    /* Shift old taps */
    if (tap_count >= TAP_MAX) {
        for (int i = 0; i < TAP_MAX - 1; i++) tap_times[i] = tap_times[i+1];
        tap_count = TAP_MAX - 1;
    }
    tap_times[tap_count++] = now;

    if (tap_count < 2) return 0.0f;

    /* Average interval over all taps */
    Uint32 total = tap_times[tap_count-1] - tap_times[0];
    float avg_ms = (float)total / (float)(tap_count - 1);
    if (avg_ms < 1.0f) return 0.0f;

    float bpm = 60000.0f / avg_ms;
    if (bpm < 80.0f) bpm = 80.0f;
    if (bpm > 300.0f) bpm = 300.0f;

    /* Reset if gap > 2 seconds (user stopped tapping) */
    if (now - tap_times[tap_count-2] > 2000) {
        tap_count = 1;
        tap_times[0] = now;
        return 0.0f;
    }

    return bpm;
}

static float draw_slider(const char *label, float value, float min_val, float max_val,
                          int x, int y, int w, int *dragging, int mx, int my, int mdown, int mpress)
{
    /* Label */
    if (label) draw_text(label, MARGIN, y - 3, 2, 120, 120, 140, 255);

    /* Value text */
    char vbuf[16];
    if (max_val > 2.0f)
        snprintf(vbuf, sizeof(vbuf), "%d", (int)value);
    else
        snprintf(vbuf, sizeof(vbuf), "%.2f", value);
    draw_text(vbuf, x + w + 10, y - 3, 2, 0, 255, 100, 255);

    /* Track */
    set_color(30, 30, 40, 255);
    fill_rect(x, y, w, SLIDER_H);

    /* Filled portion */
    float norm = (value - min_val) / (max_val - min_val);
    int fill_w = (int)(norm * w);
    set_color(0, 255, 100, 255);
    fill_rect(x, y, fill_w, SLIDER_H);

    /* Knob */
    int kx = x + fill_w, ky = y + SLIDER_H / 2;
    for (int dy = -(KNOB_R+2); dy <= KNOB_R+2; dy++)
        for (int dx = -(KNOB_R+2); dx <= KNOB_R+2; dx++)
            if (dx*dx+dy*dy <= (KNOB_R+2)*(KNOB_R+2)) {
                SDL_SetRenderDrawColor(g_ren, 0, 255, 100, 30);
                SDL_RenderDrawPoint(g_ren, kx+dx, ky+dy);
            }
    for (int dy = -KNOB_R; dy <= KNOB_R; dy++)
        for (int dx = -KNOB_R; dx <= KNOB_R; dx++)
            if (dx*dx+dy*dy <= KNOB_R*KNOB_R) {
                SDL_SetRenderDrawColor(g_ren, 0, 255, 100, 255);
                SDL_RenderDrawPoint(g_ren, kx+dx, ky+dy);
            }

    /* Hit detection (generous vertical area for easy grabbing) */
    if (mpress && point_in_rect(mx, my, x-KNOB_R, y-15, w+KNOB_R*2, SLIDER_H+30)) {
        *dragging = 1;
    }
    if (!mdown) {
        *dragging = 0;
    }
    if (*dragging) {
        float nn = (float)(mx - x) / (float)w;
        if (nn < 0.0f) nn = 0.0f;
        if (nn > 1.0f) nn = 1.0f;
        value = min_val + nn * (max_val - min_val);
    }
    return value;
}

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);

    SDL_Window *win = SDL_CreateWindow("N A R I N A",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_W, WINDOW_H, SDL_WINDOW_BORDERLESS);
    g_ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_BLEND);

    /* Set taskbar icon */
#ifdef _WIN32
    {
        SDL_SysWMinfo wminfo;
        SDL_VERSION(&wminfo.version);
        if (SDL_GetWindowWMInfo(win, &wminfo)) {
            HWND hwnd = wminfo.info.win.window;
            HICON icon = LoadIconA(GetModuleHandleA(NULL), "IDI_ICON1");
            if (icon) {
                SendMessageA(hwnd, WM_SETICON, ICON_BIG, (LPARAM)icon);
                SendMessageA(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)icon);
            }
        }
    }
#endif

    audio_init();

    float bpm = g_seq.bpm;
    float cutoff = g_dsp.base_cutoff;
    float reso = g_dsp.flt_resonance;
    float vol = g_seq.volume;

    int running = 1;
    int dragging_window = 0;
    int drag_ox = 0, drag_oy = 0;
    int prev_mdown = 1;  /* suppress phantom first-click from window focus */
    Uint32 last_tick = SDL_GetTicks();

    while (running) {
        Uint32 now = SDL_GetTicks();
        float dt = (now - last_tick) / 1000.0f;
        last_tick = now;

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = 0;
            if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_LEFT)
                dragging_window = 0;
            if (ev.type == SDL_KEYDOWN && !ev.key.repeat) {
                switch (ev.key.keysym.sym) {
                case SDLK_1: g_seq.kick_enabled  = !g_seq.kick_enabled;  break;
                case SDLK_2:
                    g_seq.hihat_enabled = !g_seq.hihat_enabled;
                    if (g_seq.hihat_enabled) audio_generate_hihat_pattern();
                    break;
                case SDLK_3: g_seq.clap_enabled  = !g_seq.clap_enabled;  break;
                case SDLK_r:
                    if (g_rec.recording) save_recording();
                    else audio_rec_start();
                    break;
                case SDLK_SPACE: toggle_play_stop(); break;
                case SDLK_RETURN: audio_generate_pattern(); flash_alpha = 1.0f;
                    cutoff = g_dsp.base_cutoff; reso = g_dsp.flt_resonance; break;
                case SDLK_t: {
                    float tb = process_tap(now);
                    if (tb > 0.0f) { bpm = tb; audio_set_bpm(bpm); }
                } break;
                case SDLK_h: show_help = !show_help; break;
                case SDLK_ESCAPE: show_help = 0; break;
                }
            }
        }

        int mx, my;
        Uint32 mstate = SDL_GetMouseState(&mx, &my);
        int mdown = (mstate & SDL_BUTTON(1)) != 0;
        int mpress = mdown && !prev_mdown;  /* edge detect: just pressed this frame */
        prev_mdown = mdown;

        /* Flash decay */
        if (flash_alpha > 0.0f) {
            flash_alpha -= dt * 3.0f;
            if (flash_alpha < 0.0f) flash_alpha = 0.0f;
        }

        /* Window drag (top 50px) */
        if (mpress && my < 50 && mx < WINDOW_W - 40) {
            dragging_window = 1;
            drag_ox = mx; drag_oy = my;
        }
        if (dragging_window && mdown) {
            int wx, wy;
            SDL_GetWindowPosition(win, &wx, &wy);
            int gmx, gmy;
            SDL_GetGlobalMouseState(&gmx, &gmy);
            SDL_SetWindowPosition(win, gmx - drag_ox, gmy - drag_oy);
        }

        /* Close button */
        int close_hover = point_in_rect(mx, my, WINDOW_W-35, 5, 28, 28);
        if (close_hover && mpress) running = 0;

        /* ---- Draw ---- */
        /* Background gradient (approximated with bands) */
        for (int y = 0; y < WINDOW_H; y++) {
            float t = (float)y / WINDOW_H;
            int r = (int)(12 + (8 - 12) * t);
            int g = (int)(12 + (8 - 12) * t);
            int b = (int)(20 + (12 - 20) * t);
            set_color(r, g, b, 255);
            SDL_RenderDrawLine(g_ren, 0, y, WINDOW_W, y);
        }

        /* Border */
        set_color(40, 40, 50, 255);
        SDL_Rect border = {0, 0, WINDOW_W, WINDOW_H};
        SDL_RenderDrawRect(g_ren, &border);

        /* Close button X */
        {
            int cr = close_hover ? 255 : 100, cg = close_hover ? 60 : 100, cb = close_hover ? 60 : 120;
            int cx = WINDOW_W - 35 + 14, cy = 5 + 14;
            set_color(cr, cg, cb, 255);
            SDL_RenderDrawLine(g_ren, cx-6, cy-6, cx+6, cy+6);
            SDL_RenderDrawLine(g_ren, cx+6, cy-6, cx-6, cy+6);
            SDL_RenderDrawLine(g_ren, cx-6, cy-5, cx+6, cy+7);
            SDL_RenderDrawLine(g_ren, cx+6, cy-5, cx-6, cy+7);
        }

        /* Help button ? */
        {
            int hx = WINDOW_W - 65, hy = 8, hs = 22;
            int hhover = point_in_rect(mx, my, hx, hy, hs, hs);
            set_color(hhover ? 60 : 40, hhover ? 60 : 40, hhover ? 75 : 50, 255);
            fill_rect(hx, hy, hs, hs);
            draw_text("?", hx + 5, hy + 4, 2, hhover ? 200 : 100, hhover ? 200 : 100, hhover ? 220 : 120, 255);
            if (hhover && mpress) { show_help = !show_help; mpress = 0; }
        }

        /* Title */
        draw_text("N A R I N A", MARGIN, 15, 3, 220, 20, 60, 255);
        draw_text("v0.1", MARGIN + text_width("N A R I N A", 3) + 12, 22, 2, 220, 20, 60, 255);

        /* Waveform box */
        set_color(15, 15, 22, 255);
        fill_rect(WAVE_X, WAVE_Y, WAVE_W, WAVE_H);
        set_color(0, 60, 25, 255);
        SDL_Rect wbox = {WAVE_X, WAVE_Y, WAVE_W, WAVE_H};
        SDL_RenderDrawRect(g_ren, &wbox);

        /* Flash */
        if (flash_alpha > 0.01f) {
            set_color(0, 255, 100, (int)(flash_alpha * 180));
            fill_rect(WAVE_X+1, WAVE_Y+1, WAVE_W-2, WAVE_H-2);
        }

        /* Waveform line */
        {
            int rp = (g_wave.write_pos - WAVE_W + WAVE_BUF_LEN) & (WAVE_BUF_LEN - 1);
            float cy = WAVE_Y + WAVE_H / 2.0f;
            set_color(0, 255, 100, 255);
            int prev_y = (int)cy;
            for (int i = 0; i < WAVE_W; i++) {
                float s = g_wave.data[(rp + i) & (WAVE_BUF_LEN - 1)];
                if (s > 1.0f) s = 1.0f;
                if (s < -1.0f) s = -1.0f;
                int sy = (int)(cy - s * (WAVE_H / 2.0f - 4));
                if (i > 0) SDL_RenderDrawLine(g_ren, WAVE_X+i-1, prev_y, WAVE_X+i, sy);
                prev_y = sy;
            }
        }

        /* Generate button */
        {
            int hover = point_in_rect(mx, my, BTN_X, BTN_Y, BTN_W, BTN_H);
            if (hover) { set_color(0, 255, 100, 30); fill_rect(BTN_X-3, BTN_Y-3, BTN_W+6, BTN_H+6); }
            if (hover) set_color(0, 255, 100, 255); else set_color(0, 128, 50, 255);
            fill_rect(BTN_X, BTN_Y, BTN_W, BTN_H);
            int tw = text_width("RANDOMIZE", 2);
            if (hover) draw_text("RANDOMIZE", BTN_X+(BTN_W-tw)/2, BTN_Y+(BTN_H-14)/2, 2, 10,10,15,255);
            else       draw_text("RANDOMIZE", BTN_X+(BTN_W-tw)/2, BTN_Y+(BTN_H-14)/2, 2, 200,255,220,255);
            if (hover && mpress) {
                audio_generate_pattern();
                flash_alpha = 1.0f;
                cutoff = g_dsp.base_cutoff;
                reso = g_dsp.flt_resonance;
            }
        }

        /* Style selector - above RANDOMIZE */
        {
            static const char *STYLE_NAMES[] = {
                "RANDOM", "DARK ACID", "TRANCE", "HARDCORE", "HYPNOTIC", "HARD ACID"
            };
            int sel = g_seq.style_select + 1;  /* -1 maps to 0 (RANDOM) */
            int sy = BTN_Y - 22;

            draw_text("STYLE:", BTN_X, sy, 2, 120, 120, 140, 255);

            int lx = BTN_X + text_width("STYLE:", 2) + 8;
            int lw = text_width(STYLE_NAMES[sel], 2) + 8;
            int lh = 16;
            int shover = point_in_rect(mx, my, lx - 14, sy - 2, lw + 28, lh + 4);

            draw_text("<", lx - 14, sy, 2, shover ? 0 : 50, shover ? 200 : 50, shover ? 80 : 60, 255);
            draw_text(STYLE_NAMES[sel], lx, sy, 2,
                      shover ? 0 : 80, shover ? 255 : 200, shover ? 100 : 100, 255);
            draw_text(">", lx + lw, sy, 2, shover ? 0 : 50, shover ? 200 : 50, shover ? 80 : 60, 255);

            if (shover && mpress) {
                g_seq.style_select++;
                if (g_seq.style_select >= 5) g_seq.style_select = -1;
                mpress = 0;
            }
        }

        /* Time signature + shuffle - small buttons, right side above drums */
        {
            int tw2 = 36, tgap = 4;
            int th = 16;
            int total_tw = 3*(tw2+tgap) + 4 + 54;  /* 3 time sigs + gap + shuffle */
            int dx = WINDOW_W - MARGIN - total_tw;
            int ty = BTN_Y - 20;
            static const char *TS_LABELS[] = {"4/4", "3/4", "6/8"};
            for (int t = 0; t < 3; t++) {
                int tx = dx + t * (tw2 + tgap);
                int thover = point_in_rect(mx, my, tx, ty, tw2, th);
                int active = (g_seq.time_sig == t);
                if (active) set_color(220, 220, 230, 255);
                else if (thover) set_color(80, 80, 90, 255);
                else set_color(50, 50, 60, 255);
                fill_rect(tx, ty, tw2, th);
                int tlw = text_width(TS_LABELS[t], 1);
                if (active) draw_text(TS_LABELS[t], tx+(tw2-tlw)/2, ty+4, 1, 15,15,20,255);
                else        draw_text(TS_LABELS[t], tx+(tw2-tlw)/2, ty+4, 1, 150,150,160,255);
                if (thover && mpress) { audio_set_time_sig(t); mpress = 0; }
            }
            /* Shuffle button */
            {
                int sx2 = dx + 3 * (tw2 + tgap) + 4;
                int sw2 = 54;
                int shov = point_in_rect(mx, my, sx2, ty, sw2, th);
                int son = g_seq.shuffle;
                if (son) set_color(220, 220, 230, 255);
                else if (shov) set_color(80, 80, 90, 255);
                else set_color(50, 50, 60, 255);
                fill_rect(sx2, ty, sw2, th);
                int slw = text_width("SHFL", 1);
                if (son) draw_text("SHFL", sx2+(sw2-slw)/2, ty+4, 1, 15,15,20,255);
                else     draw_text("SHFL", sx2+(sw2-slw)/2, ty+4, 1, 150,150,160,255);
                if (shov && mpress) { g_seq.shuffle = !g_seq.shuffle; mpress = 0; }
            }
        }

        /* Drum toggles - right side */
        {
            int dw = 58, dgap = 6;
            int total_dw = 3*(dw+dgap) - dgap;
            int dx = WINDOW_W - MARGIN - total_dw;
            struct { const char *label; int *enabled; } drums[] = {
                {"KICK", &g_seq.kick_enabled}, {"HH", &g_seq.hihat_enabled}, {"CLAP", &g_seq.clap_enabled},
            };
            for (int d = 0; d < 3; d++) {
                int bx = dx + d*(dw+dgap);
                int dh_h = BTN_H - 4;
                int dy = BTN_Y + 2;
                int dh = point_in_rect(mx, my, bx, dy, dw, dh_h);
                int on = *drums[d].enabled;
                if (on) set_color(220,20,60,255);
                else if (dh) set_color(60,60,70,255);
                else set_color(40,40,50,255);
                fill_rect(bx, dy, dw, dh_h);
                int tw = text_width(drums[d].label, 2);
                if (on) draw_text(drums[d].label, bx+(dw-tw)/2, dy+(dh_h-14)/2, 2, 255,255,255,255);
                else    draw_text(drums[d].label, bx+(dw-tw)/2, dy+(dh_h-14)/2, 2, 120,120,140,255);
                if (dh && mpress) {
                    *drums[d].enabled = !*drums[d].enabled;
                    if (d == 1 && *drums[d].enabled) audio_generate_hihat_pattern();
                }
            }
        }

        /* Sync sliders with DSP state when not dragging (audio thread may have changed them) */
        if (!dragging_cutoff) cutoff = g_dsp.base_cutoff;
        if (!dragging_reso)   reso = g_dsp.flt_resonance;

        /* BPM slider with tap tempo on label */
        {
            /* Tap tempo: click on "BPM:" label area */
            int lbl_w = text_width("BPM:", 2) + 4;
            int lbl_hover = point_in_rect(mx, my, MARGIN, BPM_Y - 8, lbl_w, 20);
            if (lbl_hover && mpress) {
                float tb = process_tap(now);
                if (tb > 0.0f) { bpm = tb; audio_set_bpm(bpm); }
                tap_flash = 1.0f;
                mpress = 0;  /* consume so slider doesn't grab */
            }
            /* Decay tap flash */
            if (tap_flash > 0.0f) tap_flash -= dt * 4.0f;
            if (tap_flash < 0.0f) tap_flash = 0.0f;

            /* Draw label with flash color */
            int lr = (int)(120 + tap_flash * 135);
            int lg = (int)(120 + tap_flash * 135);
            int lb = (int)(140 - tap_flash * 40);
            draw_text(lbl_hover ? "TAP:" : "BPM:", MARGIN, BPM_Y - 3, 2, lr, lg, lb, 255);
        }
        bpm = draw_slider(NULL, bpm, 80, 300, SLIDER_X, BPM_Y, SLIDER_W, &dragging_bpm, mx, my, mdown, mpress);
        audio_set_bpm(bpm);
        cutoff = draw_slider("CUTOFF:", cutoff, 0, 1, SLIDER_X, CUTOFF_Y, SLIDER_W, &dragging_cutoff, mx, my, mdown, mpress);
        g_dsp.base_cutoff = cutoff;
        reso = draw_slider("RESONANCE:", reso, 0, 1, SLIDER_X, RESO_Y, SLIDER_W, &dragging_reso, mx, my, mdown, mpress);
        g_dsp.flt_resonance = reso;

        /* Play/Stop button - bottom left */
        {
            int bs = 28;
            int bx = MARGIN, by = WINDOW_H - MARGIN - bs/2 - 2;
            int bhover = point_in_rect(mx, my, bx, by, bs, bs);

            if (bhover) set_color(50, 50, 65, 255);
            else set_color(35, 35, 45, 255);
            fill_rect(bx, by, bs, bs);

            int cx = bx + bs/2, cy = by + bs/2;
            if (g_seq.playing) {
                set_color(160, 160, 170, 255);
                fill_rect(cx - 5, cy - 5, 10, 10);
            } else {
                set_color(0, 200, 80, 255);
                for (int row = -6; row <= 6; row++) {
                    int half = 6 - (row < 0 ? -row : row);
                    SDL_RenderDrawLine(g_ren, cx-4, cy+row, cx-4+half+6, cy+row);
                }
            }

            if (bhover && mpress) { toggle_play_stop(); mpress = 0; }
        }

        /* Rec button - right of play/stop */
        {
            int rs = 28;
            int rx = MARGIN + 28 + 6, ry = WINDOW_H - MARGIN - rs/2 - 2;
            int rhover = point_in_rect(mx, my, rx, ry, rs, rs);
            int rec_on = g_rec.recording;

            if (rhover) set_color(60, 30, 30, 255);
            else set_color(35, 35, 45, 255);
            fill_rect(rx, ry, rs, rs);

            /* Red circle - dim when off, bright+blinking when on */
            int cx = rx + rs/2, cy = ry + rs/2;
            int visible = rec_on ? ((now / 400) % 2 == 0) : 1;
            if (visible) {
                int cr = rec_on ? 255 : 100, cg = rec_on ? 40 : 20, cb = rec_on ? 40 : 20;
                for (int dy2 = -6; dy2 <= 6; dy2++)
                    for (int dx2 = -6; dx2 <= 6; dx2++)
                        if (dx2*dx2 + dy2*dy2 <= 36) {
                            SDL_SetRenderDrawColor(g_ren, cr, cg, cb, 255);
                            SDL_RenderDrawPoint(g_ren, cx+dx2, cy+dy2);
                        }
            }

            if (rhover && mpress) {
                if (rec_on) {
                    save_recording();
                } else {
                    audio_rec_start();
                }
            }
        }

        /* Rec timer - right of rec button */
        {
            int tx = MARGIN + 28 + 6 + 28 + 8;
            int ty = WINDOW_H - MARGIN - 6;
            int rec_on = g_rec.recording;
            int secs = rec_on ? g_rec.pos / SAMPLE_RATE : 0;
            int mins = secs / 60;
            secs = secs % 60;
            char tbuf[8];
            snprintf(tbuf, sizeof(tbuf), "%02d:%02d", mins, secs);
            if (rec_on)
                draw_text(tbuf, tx, ty, 2, 220, 220, 50, 255);
            else
                draw_text(tbuf, tx, ty, 2, 40, 40, 50, 120);
        }

        /* Volume slider */
        {
            int vx = WINDOW_W - 150 - MARGIN, vy = WINDOW_H - MARGIN;
            draw_text("VOL", vx-30, vy-3, 1, 60,60,75,255);
            set_color(25, 25, 35, 255); fill_rect(vx, vy, 120, 4);
            int vfill = (int)(vol * 120);
            set_color(0, 120, 50, 180); fill_rect(vx, vy, vfill, 4);
            /* knob */
            for (int dy=-4; dy<=4; dy++)
                for (int dx=-4; dx<=4; dx++)
                    if (dx*dx+dy*dy<=16) {
                        SDL_SetRenderDrawColor(g_ren, 0, 180, 70, 200);
                        SDL_RenderDrawPoint(g_ren, vx+vfill+dx, vy+2+dy);
                    }
            if (mpress && point_in_rect(mx, my, vx-5, vy-8, 130, 20)) dragging_vol = 1;
            if (!mdown) dragging_vol = 0;
            if (dragging_vol) {
                float nn = (float)(mx - vx) / 120.0f;
                if (nn < 0.0f) nn = 0.0f;
                if (nn > 1.0f) nn = 1.0f;
                vol = nn; audio_set_volume(vol);
            }
        }

        /* Help overlay */
        if (show_help) {
            set_color(180, 180, 190, 255);
            fill_rect(MARGIN, 55, WINDOW_W - 2*MARGIN, 220);
            set_color(120, 120, 130, 255);
            SDL_Rect hb = {MARGIN, 55, WINDOW_W - 2*MARGIN, 220};
            SDL_RenderDrawRect(g_ren, &hb);

            int hx = MARGIN + 15, hy = 65;
            draw_text("HOTKEYS", hx, hy, 2, 180, 20, 50, 255); hy += 22;
            draw_text("SPACE = PLAY/STOP", hx, hy, 2, 30, 30, 40, 255); hy += 18;
            draw_text("ENTER = RANDOMIZE", hx, hy, 2, 30, 30, 40, 255); hy += 18;
            draw_text("R     = REC START/STOP", hx, hy, 2, 30, 30, 40, 255); hy += 18;
            draw_text("T     = TAP TEMPO", hx, hy, 2, 30, 30, 40, 255); hy += 18;
            draw_text("1     = KICK TOGGLE", hx, hy, 2, 30, 30, 40, 255); hy += 18;
            draw_text("2     = HIHAT TOGGLE", hx, hy, 2, 30, 30, 40, 255); hy += 18;
            draw_text("3     = CLAP TOGGLE", hx, hy, 2, 30, 30, 40, 255); hy += 18;
            hy += 8;
            draw_text("CLICK BPM: LABEL = TAP TEMPO", hx, hy, 2, 80, 80, 90, 255);

            /* Close help on any click outside help button */
            if (mpress && !point_in_rect(mx, my, WINDOW_W-65, 8, 22, 22)) {
                show_help = 0;
            }
        }

        SDL_RenderPresent(g_ren);
    }

    audio_shutdown();
    SDL_DestroyRenderer(g_ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
