/* Black Cauldron Enhanced - SDL3 front end.
 * Plays the original Black Cauldron (Sierra, 1986) game files with a new
 * AGI interpreter: modern display scaling with CRT shaders, mouse and
 * gamepad controls, and PCjr/Tandy 3-voice sound. GPL-3.0-or-later. */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "agi.h"
#include "crt_gl.h"
#include "sound.h"
#include "version.h"

#define APP_NAME "Black Cauldron Enhanced"
#define INI_NAME "bc-enhanced.ini"

/* ------------------------------------------------------------------ config */
enum { PAD_A, PAD_B, PAD_X, PAD_Y, PAD_BACK, PAD_START, PAD_LB, PAD_RB, PAD_COUNT };
static const char *pad_keys[PAD_COUNT] = {"a", "b", "x", "y", "back", "start", "lb", "rb"};

typedef struct {
    char gamedir[1024];
    int fullscreen, width, height;
    int scale_mode, aspect, integer_only, shader, renderer_sdl;
    int monitor_vga;          /* 0 = EGA monitor (default), 1 = VGA monitor */
    int sound, volume;
    int arrows_hold;          /* 1 = walk while an arrow key is held */
    int walk_first[4];        /* look, do, use, select */
    int mouse;
    int pointer;              /* 0 windows, 1 agi, 2 sci, 3 agi-triangle */
    float deadzone;
    float rstick_speed;       /* pointer speed on the stick, 0.25 - 4 */
    int pointer_left;         /* 1 = left stick moves the pointer, right stick walks */
    char pad[PAD_COUNT][32];
} Config;

static void config_defaults(Config *c) {
    memset(c, 0, sizeof *c);
    c->width = 1280; c->height = 960;
    c->scale_mode = 2; c->aspect = 1; c->shader = SHADER_CRT_AUTO;
    c->sound = 3; c->volume = 60;
    c->arrows_hold = 0;
    c->walk_first[1] = c->walk_first[2] = 1;   /* do, use */
    c->mouse = 1;
    c->pointer = 1;
    c->deadzone = 0.30f;
    strcpy(c->pad[PAD_A], "click");
    strcpy(c->pad[PAD_B], "use");
    strcpy(c->pad[PAD_X], "do");
    strcpy(c->pad[PAD_Y], "select");
    strcpy(c->pad[PAD_BACK], "inventory");
    strcpy(c->pad[PAD_START], "menu");
    strcpy(c->pad[PAD_LB], "menu");
    strcpy(c->pad[PAD_RB], "look");
    c->rstick_speed = 1.0f;
}

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

static int shader_from_str(const char *v) {
    if (!SDL_strcasecmp(v, "crt-auto") || !SDL_strcasecmp(v, "crt") || !strcmp(v, "1")) return SHADER_CRT_AUTO;
    if (!SDL_strcasecmp(v, "crt-1080p")) return SHADER_CRT_1080P;
    if (!SDL_strcasecmp(v, "crt-1440p")) return SHADER_CRT_1440P;
    if (!SDL_strcasecmp(v, "crt-4k")) return SHADER_CRT_4K;
    return SHADER_NONE;
}
static const char *shader_to_str(int sh) {
    switch (sh) {
    case SHADER_CRT_AUTO: return "crt-auto";
    case SHADER_CRT_1080P: return "crt-1080p";
    case SHADER_CRT_1440P: return "crt-1440p";
    case SHADER_CRT_4K: return "crt-4k";
    default: return "none";
    }
}

static const char *ACT_NAMES[4] = {"look", "do", "use", "select"};

static void walk_first_str(const Config *c, char *out, int n) {
    out[0] = 0;
    for (int i = 0; i < 4; i++) if (c->walk_first[i]) {
        if (out[0]) SDL_strlcat(out, ",", n);
        SDL_strlcat(out, ACT_NAMES[i], n);
    }
    if (!out[0]) SDL_strlcpy(out, "none", n);
}

static void config_write(const char *path, const Config *c) {
    FILE *f = fopen(path, "w");
    if (!f) return;
    char wf[64];
    walk_first_str(c, wf, sizeof wf);
    fprintf(f,
        "; " APP_NAME " v" APP_VERSION " settings\n"
        "; Lines starting with ; are comments.\n\n"
        "[game]\n"
        "; Folder with the original Black Cauldron files (LOGDIR, VOL.0, ...).\n"
        "; Empty = the folder this program is in.\n"
        "gamedir=%s\n\n"
        "[display]\n"
        "; 1 = start in full screen (Alt+Enter or F11 switches while playing)\n"
        "fullscreen=%d\n"
        "; window size at start\n"
        "width=%d\n"
        "height=%d\n"
        "; CRT shader: crt-auto (choose by window size), crt-1080p, crt-1440p,\n"
        "; crt-4k or none. Ctrl+F6 switches it on and off while playing.\n"
        "shader=%s\n"
        "; Monitor for the CRT shader: ega (as in 1986: one picture line per\n"
        "; scanline, EGA colours) or vga (double scanned, as on a 1990s VGA\n"
        "; monitor). Ctrl+F5 switches while playing.\n"
        "monitor=%s\n"
        "; Scaling without CRT shader: sharp, nearest or linear\n"
        "scale=%s\n"
        "; 1 = show the picture in 4:3 like a 1980s monitor, 0 = square pixels\n"
        "aspect=%d\n"
        "; 1 = only whole-number scaling (without CRT shader)\n"
        "integer=%d\n"
        "; opengl (needed for the CRT shader) or sdl\n"
        "renderer=%s\n\n"
        "[sound]\n"
        "; tandy = PCjr/Tandy 1000 sound: three voices and noise (the full music)\n"
        "; pcspeaker = the PC speaker, as on most PCs in 1986 (one voice)\n"
        "; off = no sound. Ctrl+F4 switches between tandy and pcspeaker.\n"
        "sound=%s\n"
        "; volume 0 - 100. While playing, + and - change it in steps of 10.\n"
        "volume=%d\n\n"
        "[controls]\n"
        "; Mouse: left button walks to the pointer, right button opens the action\n"
        "; menu (Look, Do, Use, Select). 0 = mouse off.\n"
        "mouse=%d\n"
        "; Mouse pointer. Except for windows, it is drawn in the game picture,\n"
        "; so it gets the CRT effect too.\n"
        ";   agi           blocky arrow on the 160-pixel grid of the AGI games\n"
        ";                 (every pixel two wide, like Taran and the pictures)\n"
        ";   agi-triangle  the same blocky arrow head, without the stem\n"
        ";   sci           finer arrow, as in the later Sierra (SCI) games\n"
        ";   windows       the normal Windows pointer\n"
        "pointer=%s\n"
        "; Actions from the right-click menu for which Taran first walks to the\n"
        "; place you clicked: any of look, do, use, select (comma separated), or none.\n"
        "walk_first=%s\n"
        "; Arrow keys: classic = press once to walk, press again to stop (as in\n"
        "; 1986); hold = walk while the key is held down.\n"
        "arrow_keys=%s\n\n"
        "[gamepad]\n"
        "; The d-pad and one stick walk, the other stick moves the mouse pointer.\n"
        "; Actions for the buttons:\n"
        ";   click       left mouse click at the pointer (Taran walks there)\n"
        ";   look, do, use, select   the actions of the game (F8, F6, F4, F3)\n"
        ";   menu        the menu bar (Esc); in menus and windows: back\n"
        ";   go_back     go back to the moment you entered an earlier room\n"
        ";   cancel, enter, inventory, prev_item, next_item, save, restore,\n"
        ";   pause, none\n"
        "; In menus and windows, A chooses and B goes back.\n",
        c->gamedir, c->fullscreen, c->width, c->height, shader_to_str(c->shader),
        c->monitor_vga ? "vga" : "ega",
        c->scale_mode == 0 ? "nearest" : c->scale_mode == 1 ? "linear" : "sharp",
        c->aspect, c->integer_only, c->renderer_sdl ? "sdl" : "opengl",
        c->sound == 3 ? "tandy" : c->sound == 1 ? "pcspeaker" : "off", c->volume, c->mouse, c->pointer == 1 ? "agi" : c->pointer == 2 ? "sci" : c->pointer == 3 ? "agi-triangle" : "windows", wf, c->arrows_hold ? "hold" : "classic");
    for (int i = 0; i < PAD_COUNT; i++) fprintf(f, "%s=%s\n", pad_keys[i], c->pad[i]);
    fprintf(f, "; stick dead zone, 0.1 - 0.8\ndeadzone=%g\n", c->deadzone);
    fprintf(f, "; Which stick moves the mouse pointer: right (the left stick walks) or\n"
               "; left (the right stick walks)\npointer_stick=%s\n", c->pointer_left ? "left" : "right");
    fprintf(f, "; speed of the mouse pointer on its stick, 0.25 - 4\nrightstick_speed=%g\n", c->rstick_speed);
    fclose(f);
}

static void config_load(Config *c, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { config_write(path, c); return; }
    char line[512], section[32] = "";
    while (fgets(line, sizeof line, f)) {
        char *s = trim(line);
        if (!*s || *s == ';' || *s == '#') continue;
        if (*s == '[') { snprintf(section, sizeof section, "%s", s + 1); char *e = strchr(section, ']'); if (e) *e = 0; continue; }
        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = trim(s), *v = trim(eq + 1);
        for (char *p = k; *p; p++) *p = (char)tolower((unsigned char)*p);
        if (!strcmp(section, "game")) {
            if (!strcmp(k, "gamedir")) snprintf(c->gamedir, sizeof c->gamedir, "%s", v);
        } else if (!strcmp(section, "display")) {
            if (!strcmp(k, "fullscreen")) c->fullscreen = atoi(v);
            else if (!strcmp(k, "width")) c->width = atoi(v);
            else if (!strcmp(k, "height")) c->height = atoi(v);
            else if (!strcmp(k, "scale")) c->scale_mode = !strcmp(v, "nearest") ? 0 : (!strcmp(v, "linear") ? 1 : 2);
            else if (!strcmp(k, "aspect")) c->aspect = atoi(v);
            else if (!strcmp(k, "integer")) c->integer_only = atoi(v);
            else if (!strcmp(k, "shader")) c->shader = shader_from_str(v);
            else if (!strcmp(k, "monitor")) c->monitor_vga = !SDL_strcasecmp(v, "vga");
            else if (!strcmp(k, "renderer")) c->renderer_sdl = !SDL_strcasecmp(v, "sdl");
        } else if (!strcmp(section, "sound")) {
            if (!strcmp(k, "sound")) {
                if (!SDL_strcasecmp(v, "pcspeaker") || !SDL_strcasecmp(v, "pc") || !SDL_strcasecmp(v, "speaker")) c->sound = 1;
                else if (!SDL_strcasecmp(v, "off") || !strcmp(v, "0")) c->sound = 0;
                else c->sound = 3;
            }
            else if (!strcmp(k, "volume")) c->volume = atoi(v);
        } else if (!strcmp(section, "controls")) {
            if (!strcmp(k, "mouse")) c->mouse = atoi(v);
            else if (!strcmp(k, "pointer"))
                c->pointer = !SDL_strcasecmp(v, "windows") ? 0 : !SDL_strcasecmp(v, "sci") ? 2 : !SDL_strcasecmp(v, "agi-triangle") ? 3 : 1;
            else if (!strcmp(k, "arrow_keys")) c->arrows_hold = !SDL_strcasecmp(v, "hold");
            else if (!strcmp(k, "walk_first")) {
                for (int i = 0; i < 4; i++) c->walk_first[i] = 0;
                char buf[128]; snprintf(buf, sizeof buf, "%s", v);
                for (char *t = strtok(buf, ", "); t; t = strtok(NULL, ", "))
                    for (int i = 0; i < 4; i++) if (!SDL_strcasecmp(t, ACT_NAMES[i])) c->walk_first[i] = 1;
            }
        } else if (!strcmp(section, "gamepad")) {
            if (!strcmp(k, "deadzone")) c->deadzone = (float)atof(v);
            else if (!strcmp(k, "rightstick_speed")) c->rstick_speed = (float)atof(v);
            else if (!strcmp(k, "pointer_stick")) c->pointer_left = !SDL_strcasecmp(v, "left");
            else for (int i = 0; i < PAD_COUNT; i++) if (!strcmp(k, pad_keys[i])) snprintf(c->pad[i], sizeof c->pad[i], "%s", v);
        }
    }
    fclose(f);
    if (c->volume < 0) c->volume = 0;
    if (c->volume > 100) c->volume = 100;
    if (c->deadzone < 0.1f) c->deadzone = 0.1f;
    if (c->deadzone > 0.8f) c->deadzone = 0.8f;
    if (c->rstick_speed < 0.25f) c->rstick_speed = 0.25f;
    if (c->rstick_speed > 4.0f) c->rstick_speed = 4.0f;
    /* the button layout of versions before 0.2.1 becomes the new one */
    static const char *old_layout[PAD_COUNT] = {"do", "cancel", "look", "use", "select", "menu", "prev_item", "next_item"};
    int is_old = 1;
    for (int i = 0; i < PAD_COUNT; i++) if (SDL_strcasecmp(c->pad[i], old_layout[i])) is_old = 0;
    if (is_old) {
        Config d; config_defaults(&d);
        for (int i = 0; i < PAD_COUNT; i++) snprintf(c->pad[i], sizeof c->pad[i], "%s", d.pad[i]);
    }
}

/* change one key in the ini file, keeping everything else */
static void ini_set(const char *path, const char *section, const char *key, const char *value) {
    FILE *f = fopen(path, "r");
    static char buf[65536]; size_t n = 0;
    if (f) { n = fread(buf, 1, sizeof buf - 1, f); fclose(f); }
    buf[n] = 0;
    static char out[70000]; size_t o = 0;
    char cur[64] = ""; int done = 0, in_sec = 0;
    char *line = buf;
    while (*line) {
        char *nl = strchr(line, '\n');
        size_t len = nl ? (size_t)(nl - line + 1) : strlen(line);
        char tmp[512]; size_t tl = len < sizeof tmp - 1 ? len : sizeof tmp - 1;
        memcpy(tmp, line, tl); tmp[tl] = 0;
        char *t = trim(tmp);
        if (*t == '[') {
            if (in_sec && !done) { o += (size_t)snprintf(out + o, sizeof out - o, "%s=%s\n", key, value); done = 1; }
            snprintf(cur, sizeof cur, "%s", t + 1); char *e = strchr(cur, ']'); if (e) *e = 0;
            in_sec = !SDL_strcasecmp(cur, section);
        } else if (in_sec && !done) {
            char *eq = strchr(t, '=');
            if (eq && *t != ';') {
                *eq = 0;
                if (!SDL_strcasecmp(trim(t), key)) {
                    o += (size_t)snprintf(out + o, sizeof out - o, "%s=%s\n", key, value); done = 1;
                    line += len; continue;
                }
            }
        }
        if (o + len < sizeof out) { memcpy(out + o, line, len); o += len; }
        line += len;
    }
    if (!done) {
        if (!in_sec) o += (size_t)snprintf(out + o, sizeof out - o, "%s[%s]\n", (o && out[o - 1] != '\n') ? "\n" : "", section);
        o += (size_t)snprintf(out + o, sizeof out - o, "%s=%s\n", key, value);
    }
    f = fopen(path, "w");
    if (f) { fwrite(out, 1, o, f); fclose(f); }
}

/* ------------------------------------------------------------------ app */
typedef struct {
    AgiHost host;
    Agi *e;
    Config cfg;
    char base[1024], ini_path[1200];
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *tex, *sharp;
    int sharp_k;
    CrtGL *gl;
    char gl_error[512];
    const char *shader_name;
    SDL_FRect dst;
    uint32_t frame[SCR_W * SCR_H];
    char status[160];
    uint64_t status_until;
    int quit;
    SDL_Gamepad *pad;
    int pad_dir;               /* current direction from stick/d-pad */
    int pad_btn[SDL_GAMEPAD_BUTTON_COUNT];
    uint64_t nav_next;         /* auto-repeat of navigation in menus */
    int stick_pointer;         /* 1 = the right stick moved the pointer last */
    float ptr_fx, ptr_fy;      /* pointer position with sub-pixel precision */
    uint64_t ptr_last_ns;
    int nav_dir;
    int arrows_down[9];
    SDL_AudioStream *audio;
    Synth synth;
    int keyq[32], keyq_n;
} App;

static const uint32_t EGA_RGBA[16] = {
    0xFF000000, 0xFFAA0000, 0xFF00AA00, 0xFFAAAA00, 0xFF0000AA, 0xFFAA00AA, 0xFF0055AA, 0xFFAAAAAA,
    0xFF555555, 0xFFFF5555, 0xFF55FF55, 0xFFFFFF55, 0xFF5555FF, 0xFFFF55FF, 0xFF55FFFF, 0xFFFFFFFF};

static void set_status(App *a, const char *msg) {
    snprintf(a->status, sizeof a->status, "%s", msg);
    a->status_until = SDL_GetTicks() + 2500;
}

static void update_title(App *a) {
    SDL_SetWindowTitle(a->win, APP_NAME " v" APP_VERSION);
}

static void gl_text_box(App *a, int x, int y, const char *txt, uint32_t col) {
    static uint32_t buf[(160 * 8 + 8) * 14];
    static uint8_t glyphs[SCR_W * 8];
    int n = (int)strlen(txt);
    if (n > 39) n = 39;
    int bw = n * 8 + 8, bh = 14;
    for (int i = 0; i < bw * bh; i++) buf[i] = 0xC8000000u;
    memset(glyphs, 0, sizeof glyphs);
    for (int c = 0; c < n; c++) text_draw_char(glyphs, c * 8, 0, (uint8_t)txt[c], 1, -1);
    for (int yy = 0; yy < 8; yy++)
        for (int xx = 0; xx < n * 8; xx++)
            if (glyphs[yy * SCR_W + xx]) buf[(3 + yy) * bw + 4 + xx] = col;
    crtgl_overlay(a->gl, buf, bw, bh, x * 2, y * 2, 2);
}

static void present_sdl(App *a) {
    int ww, wh;
    SDL_GetRenderOutputSize(a->ren, &ww, &wh);
    double src_w = 320, src_h = a->cfg.aspect ? 240 : 200;
    double s = SDL_min(ww / src_w, wh / src_h);
    if (a->cfg.integer_only && s >= 1) s = SDL_floor(s);
    a->dst.w = (float)(src_w * s); a->dst.h = (float)(src_h * s);
    a->dst.x = (float)((ww - a->dst.w) / 2); a->dst.y = (float)((wh - a->dst.h) / 2);
    if (!a->tex) {
        a->tex = SDL_CreateTexture(a->ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, SCR_W, SCR_H);
        SDL_SetTextureScaleMode(a->tex, a->cfg.scale_mode == 1 ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
    }
    SDL_UpdateTexture(a->tex, NULL, a->frame, SCR_W * 4);
    SDL_SetRenderDrawColor(a->ren, 0, 0, 0, 255);
    SDL_RenderClear(a->ren);
    if (a->cfg.scale_mode == 2) {
        int k = (int)SDL_ceil(SDL_max(a->dst.w / SCR_W, a->dst.h / SCR_H));
        if (k < 1) k = 1;
        if (k > 8) k = 8;
        if (!a->sharp || a->sharp_k != k) {
            if (a->sharp) SDL_DestroyTexture(a->sharp);
            a->sharp = SDL_CreateTexture(a->ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_TARGET, SCR_W * k, SCR_H * k);
            SDL_SetTextureScaleMode(a->sharp, SDL_SCALEMODE_LINEAR);
            a->sharp_k = k;
        }
        SDL_SetRenderTarget(a->ren, a->sharp);
        SDL_RenderTexture(a->ren, a->tex, NULL, NULL);
        SDL_SetRenderTarget(a->ren, NULL);
        SDL_RenderTexture(a->ren, a->sharp, NULL, &a->dst);
    } else {
        SDL_RenderTexture(a->ren, a->tex, NULL, &a->dst);
    }
    if (a->status[0] && SDL_GetTicks() < a->status_until) {
        SDL_SetRenderScale(a->ren, 2, 2);
        SDL_SetRenderDrawColor(a->ren, 0, 0, 0, 200);
        SDL_FRect r = {4, 4, (float)(strlen(a->status) * 8 + 8), 14};
        SDL_RenderFillRect(a->ren, &r);
        SDL_SetRenderDrawColor(a->ren, 255, 230, 120, 255);
        SDL_RenderDebugText(a->ren, 8, 7, a->status);
        SDL_SetRenderScale(a->ren, 1, 1);
    }
    SDL_RenderPresent(a->ren);
}

static void draw_pointer(App *a);

static void host_present(AgiHost *h) {
    App *a = (App *)h->user;
    for (int i = 0; i < SCR_W * SCR_H; i++) a->frame[i] = EGA_RGBA[a->e->screen[i] & 15];
    if (a->cfg.mouse && a->cfg.pointer) draw_pointer(a);
    if (a->gl) {
        const char *nm = crtgl_frame(a->gl, a->frame, SCR_W, SCR_H, a->cfg.shader, a->cfg.scale_mode,
                                     a->cfg.aspect, a->cfg.integer_only, &a->dst);
        if (nm[0] && nm != a->shader_name) {
            if (a->shader_name && a->cfg.shader != SHADER_NONE) {
                char msg[96]; snprintf(msg, sizeof msg, "Display: %s", nm);
                set_status(a, msg);
            }
            a->shader_name = nm;
        }
        if (a->status[0] && SDL_GetTicks() < a->status_until) gl_text_box(a, 4, 4, a->status, 0xFF78E6FFu);
        /* test hook: BCE_DEBUG_GLSHOT=<ms> saves the window contents once */
        static double glshot = -1;
        if (glshot < 0) { const char *s = getenv("BCE_DEBUG_GLSHOT"); glshot = s ? atof(s) : 0; }
        if (glshot > 0 && SDL_GetTicks() > glshot) {
            int ww, wh; crtgl_output_size(a->gl, &ww, &wh);
            uint8_t *px = malloc((size_t)ww * wh * 4);
            crtgl_read(a->gl, px, ww, wh);
            char fn[1200]; snprintf(fn, sizeof fn, "%sglshot.ppm", a->base);
            FILE *f = fopen(fn, "wb");
            if (f) {
                fprintf(f, "P6 %d %d 255\n", ww, wh);
                for (int i = 0; i < ww * wh; i++) fwrite(px + i * 4, 1, 3, f);
                fclose(f);
            }
            free(px);
            fprintf(stderr, "[glshot] %dx%d %s (%s)\n", ww, wh, a->shader_name ? a->shader_name : "?", crtgl_info(a->gl));
            glshot = 0;
        }
        crtgl_swap(a->gl);
    } else {
        present_sdl(a);
    }
}

/* Pointers: white with a black edge, hotspot at the tip.
 * AGI: on the 160-pixel grid of the AGI games (every pixel two wide). */
static const char *POINTER_AGI[] = {
    "X     ",
    "XX    ",
    "XX    ",
    "XWX   ",
    "XWX   ",
    "XWWX  ",
    "XWWX  ",
    "XWWWX ",
    "XWWWX ",
    "XWWWWX",
    "XWWXXX",
    "XXWX  ",
    "  XWX ",
    "  XWX ",
    "   X  ",
    NULL};
/* AGI triangle: the same arrow head without a stem */
static const char *POINTER_TRI[] = {
    "X     ",
    "XX    ",
    "XX    ",
    "XWX   ",
    "XWX   ",
    "XWWX  ",
    "XWWX  ",
    "XWWWX ",
    "XWWWX ",
    "XWWWWX",
    "XXXXXX",
    NULL};
/* SCI: the finer arrow of the later Sierra games (320-pixel grid). */
static const char *POINTER[] = {
    "X          ",
    "XX         ",
    "XWX        ",
    "XWWX       ",
    "XWWWX      ",
    "XWWWWX     ",
    "XWWWWWX    ",
    "XWWWWWWX   ",
    "XWWWWWWWX  ",
    "XWWWWWWWWX ",
    "XWWWWWXXXXX",
    "XWWXWWX    ",
    "XWX XWWX   ",
    "XX  XWWX   ",
    "X    XWWX  ",
    "     XWWX  ",
    "      XX   ",
    NULL};

static void draw_pointer(App *a) {
    int mx = a->e->mouse_x, my = a->e->mouse_y;
    if (mx < 0 || my < 0) return;
    int agi = a->cfg.pointer == 1 || a->cfg.pointer == 3;
    const char **shape = a->cfg.pointer == 3 ? POINTER_TRI : agi ? POINTER_AGI : POINTER;
    int pw = agi ? 2 : 1;
    if (agi) mx &= ~1;                 /* snap to the AGI pixel grid */
    for (int r = 0; shape[r]; r++) {
        int y = my + r;
        if (y >= SCR_H) break;
        for (int c = 0; shape[r][c]; c++) {
            char p = shape[r][c];
            if (p != 'X' && p != 'W') continue;
            for (int k = 0; k < pw; k++) {
                int x = mx + c * pw + k;
                if (x < SCR_W) a->frame[y * SCR_W + x] = EGA_RGBA[p == 'X' ? 0 : 15];
            }
        }
    }
}

static uint32_t host_ticks(AgiHost *h) { (void)h; return (uint32_t)SDL_GetTicks(); }
static void host_sleep(AgiHost *h, int ms) { (void)h; SDL_Delay((Uint32)ms); }
static int host_quit(AgiHost *h) { return ((App *)h->user)->quit; }

static void host_sound_play(AgiHost *h, const uint8_t *d, int len) {
    App *a = (App *)h->user;
    if (!a->audio) return;
    SDL_LockAudioStream(a->audio);
    synth_start(&a->synth, d, len);
    SDL_UnlockAudioStream(a->audio);
}
static void host_sound_stop(AgiHost *h) {
    App *a = (App *)h->user;
    if (!a->audio) return;
    SDL_LockAudioStream(a->audio);
    synth_stop(&a->synth);
    SDL_UnlockAudioStream(a->audio);
}

static void SDLCALL audio_cb(void *ud, SDL_AudioStream *s, int additional, int total) {
    (void)total;
    App *a = (App *)ud;
    static int16_t buf[4096];
    while (additional > 0) {
        int n = additional / 2;
        if (n > 4096) n = 4096;
        synth_render(&a->synth, buf, n);
        SDL_PutAudioStreamData(s, buf, n * 2);
        additional -= n * 2;
    }
}

/* --------------------------------------------------------------- input */
static void push_key(App *a, int k) { if (a->keyq_n < 32) a->keyq[a->keyq_n++] = k; }

static int xt_letter(SDL_Keycode k) {
    static const char *rows = "qwertyuiop\0asdfghjkl\0zxcvbnm";
    static const int base[3] = {0x10, 0x1E, 0x2C};
    int r = 0, i = 0;
    for (const char *p = rows; r < 3; p++) {
        if (!*p) { r++; i = 0; continue; }
        if (*p == (char)k) return base[r] + i;
        i++;
    }
    return 0;
}

static void screenshot(App *a) {
    static uint32_t shot[SCR_W * SCR_H];
    for (int i = 0; i < SCR_W * SCR_H; i++) shot[i] = EGA_RGBA[a->e->screen[i] & 15];
    SDL_Surface *s = SDL_CreateSurfaceFrom(SCR_W, SCR_H, SDL_PIXELFORMAT_ABGR8888, shot, SCR_W * 4);
    if (!s) return;
    char fn[1200];
    for (int i = 1; i < 10000; i++) {
        snprintf(fn, sizeof fn, "%sbce%04d.bmp", a->base, i);
        FILE *f = fopen(fn, "rb");
        if (f) { fclose(f); continue; }
        break;
    }
    SDL_SaveBMP(s, fn);
    SDL_DestroySurface(s);
    set_status(a, "Screenshot saved");
}

static void toggle_fullscreen(App *a) {
    a->cfg.fullscreen = !(SDL_GetWindowFlags(a->win) & SDL_WINDOW_FULLSCREEN);
    SDL_SetWindowFullscreen(a->win, a->cfg.fullscreen);
}

/* window pixel -> 320x200 screen */
static void to_screen(App *a, float wx, float wy, int *sx, int *sy) {
    float d = SDL_GetWindowPixelDensity(a->win);
    float px = wx * d, py = wy * d;
    float x = (px - a->dst.x) / (a->dst.w > 0 ? a->dst.w : 1) * SCR_W;
    float y = (py - a->dst.y) / (a->dst.h > 0 ? a->dst.h : 1) * SCR_H;
    *sx = (int)SDL_clamp(x, 0, SCR_W - 1);
    *sy = (int)SDL_clamp(y, 0, SCR_H - 1);
}

/* master volume: perceived loudness, so the gain goes with the square */
static void set_volume(App *a, int v) {
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    a->cfg.volume = v;
    if (a->audio) SDL_SetAudioStreamGain(a->audio, (v / 100.0f) * (v / 100.0f));
    char buf[16]; snprintf(buf, sizeof buf, "%d", v);
    ini_set(a->ini_path, "sound", "volume", buf);
    char msg[48]; snprintf(msg, sizeof msg, "Volume: %d%%", v);
    set_status(a, msg);
}

/* BC keeps the item chosen with "New Object" in variable 42 */
static void cycle_item(App *a, int step) {
    Agi *e = a->e;
    int items[MAX_ITEMS], n = 0, cur = -1;
    for (int i = 1; i < e->nitems; i++) if (e->item_room[i] == 255) {
        if (i == e->var[42]) cur = n;
        items[n++] = i;
    }
    if (!n) { set_status(a, "You are not carrying anything"); return; }
    cur = cur < 0 ? (step > 0 ? 0 : n - 1) : (cur + step + n) % n;
    e->var[42] = (uint8_t)items[cur];
    char msg[96];
    snprintf(msg, sizeof msg, "Use object: %s", e->item_name[items[cur]]);
    set_status(a, msg);
}

/* a left mouse click at the pointer, as if the mouse was used */
static void pointer_click(App *a) {
    Agi *e = a->e;
    if (e->mouse_x < 0 || e->mouse_y < 0) { e->mouse_x = SCR_W / 2; e->mouse_y = SCR_H / 2; }
    push_key(a, KEY_MOUSE_L);
}

static void do_action(App *a, int button, const char *act, int down) {
    Agi *e = a->e;
    if (!down) return;
    if (e->modal) {
        /* in menus and windows: A chooses, B and the menu buttons go back */
        if (!SDL_strcasecmp(act, "none")) return;
        if (button == PAD_B || !SDL_strcasecmp(act, "cancel") || !SDL_strcasecmp(act, "menu")) push_key(a, KEY_ESC);
        else if (!SDL_strcasecmp(act, "click") && a->stick_pointer) pointer_click(a);
        else push_key(a, KEY_ENTER);
        return;
    }
    if (!SDL_strcasecmp(act, "click")) { pointer_click(a); return; }
    if (!SDL_strcasecmp(act, "look")) push_key(a, KEY_F(8));
    else if (!SDL_strcasecmp(act, "do")) push_key(a, KEY_F(6));
    else if (!SDL_strcasecmp(act, "use")) push_key(a, KEY_F(4));
    else if (!SDL_strcasecmp(act, "select")) push_key(a, KEY_F(3));
    else if (!SDL_strcasecmp(act, "menu")) push_key(a, KEY_ESC);
    else if (!SDL_strcasecmp(act, "cancel")) push_key(a, KEY_ESC);
    else if (!SDL_strcasecmp(act, "enter")) push_key(a, KEY_ENTER);
    else if (!SDL_strcasecmp(act, "inventory")) push_key(a, 9);
    else if (!SDL_strcasecmp(act, "go_back")) push_key(a, 26);
    else if (!SDL_strcasecmp(act, "save")) push_key(a, KEY_F(5));
    else if (!SDL_strcasecmp(act, "restore")) push_key(a, KEY_F(7));
    else if (!SDL_strcasecmp(act, "pause")) push_key(a, 27);
    else if (!SDL_strcasecmp(act, "prev_item")) cycle_item(a, -1);
    else if (!SDL_strcasecmp(act, "next_item")) cycle_item(a, 1);
}

static int pad_button_index(SDL_GamepadButton b) {
    switch (b) {
    case SDL_GAMEPAD_BUTTON_SOUTH: return PAD_A;
    case SDL_GAMEPAD_BUTTON_EAST: return PAD_B;
    case SDL_GAMEPAD_BUTTON_WEST: return PAD_X;
    case SDL_GAMEPAD_BUTTON_NORTH: return PAD_Y;
    case SDL_GAMEPAD_BUTTON_BACK: return PAD_BACK;
    case SDL_GAMEPAD_BUTTON_START: return PAD_START;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return PAD_LB;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return PAD_RB;
    default: return -1;
    }
}

static void open_first_pad(App *a) {
    if (a->pad) return;
    int n = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&n);
    if (ids && n > 0) {
        a->pad = SDL_OpenGamepad(ids[0]);
        if (a->pad) {
            char msg[128];
            snprintf(msg, sizeof msg, "Gamepad: %s", SDL_GetGamepadName(a->pad));
            set_status(a, msg);
        }
    }
    SDL_free(ids);
}

/* direction 1..8 (AGI: 1 = up, clockwise) from the stick or d-pad */
static int pad_direction(App *a) {
    if (!a->pad) return 0;
    int up = SDL_GetGamepadButton(a->pad, SDL_GAMEPAD_BUTTON_DPAD_UP);
    int dn = SDL_GetGamepadButton(a->pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
    int lf = SDL_GetGamepadButton(a->pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
    int rt = SDL_GetGamepadButton(a->pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
    int pl = a->cfg.pointer_left;   /* walking stick: left, or right when swapped */
    float x = SDL_GetGamepadAxis(a->pad, pl ? SDL_GAMEPAD_AXIS_RIGHTX : SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;
    float y = SDL_GetGamepadAxis(a->pad, pl ? SDL_GAMEPAD_AXIS_RIGHTY : SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f;
    if (SDL_sqrtf(x * x + y * y) >= a->cfg.deadzone) {
        float ang = SDL_atan2f(x, -y) * 180.0f / SDL_PI_F;   /* 0 = up, clockwise */
        if (ang < 0) ang += 360;
        return 1 + ((int)((ang + 22.5f) / 45.0f)) % 8;
    }
    int dx = rt - lf, dy = dn - up;
    static const int tbl[3][3] = {{8, 1, 2}, {7, 0, 3}, {6, 5, 4}};
    return tbl[dy + 1][dx + 1];
}

/* The right stick moves the mouse pointer; the speed grows with the
 * deflection (full deflection crosses the screen in about 1.3 seconds). */
static void move_pointer_with_stick(App *a) {
    Agi *e = a->e;
    uint64_t now = SDL_GetTicksNS();
    double dt = a->ptr_last_ns ? (now - a->ptr_last_ns) / 1e9 : 0;
    a->ptr_last_ns = now;
    if (dt > 0.1) dt = 0.1;
    int pl = a->cfg.pointer_left;   /* pointer stick: right, or left when swapped */
    float x = SDL_GetGamepadAxis(a->pad, pl ? SDL_GAMEPAD_AXIS_LEFTX : SDL_GAMEPAD_AXIS_RIGHTX) / 32767.0f;
    float y = SDL_GetGamepadAxis(a->pad, pl ? SDL_GAMEPAD_AXIS_LEFTY : SDL_GAMEPAD_AXIS_RIGHTY) / 32767.0f;
    float m = SDL_sqrtf(x * x + y * y);
    const float dz = 0.2f;
    if (m < dz) return;
    float k = (m - dz) / (1 - dz);
    if (k > 1) k = 1;
    float speed = 250.0f * a->cfg.rstick_speed * k * k / m;   /* screen pixels per second */
    if (!a->stick_pointer || e->mouse_x < 0) {
        a->ptr_fx = e->mouse_x >= 0 ? (float)e->mouse_x : SCR_W / 2.0f;
        a->ptr_fy = e->mouse_y >= 0 ? (float)e->mouse_y : SCR_H / 2.0f;
    }
    a->stick_pointer = 1;
    a->ptr_fx = SDL_clamp(a->ptr_fx + x * speed * (float)dt, 0, SCR_W - 1);
    a->ptr_fy = SDL_clamp(a->ptr_fy + y * speed * (float)dt * 0.85f, 0, SCR_H - 1);
    e->mouse_x = (int)a->ptr_fx;
    e->mouse_y = (int)a->ptr_fy;
    if (!a->cfg.pointer) {
        /* normal Windows pointer: move it to the same place */
        float dens = SDL_GetWindowPixelDensity(a->win);
        if (dens <= 0) dens = 1;
        SDL_WarpMouseInWindow(a->win, (a->dst.x + (e->mouse_x + 0.5f) * a->dst.w / SCR_W) / dens,
                              (a->dst.y + (e->mouse_y + 0.5f) * a->dst.h / SCR_H) / dens);
    }
}

static void poll_pad(App *a) {
    if (!a->pad) return;
    move_pointer_with_stick(a);
    int d = pad_direction(a);
    Agi *e = a->e;
    if (e->modal) {
        /* navigate menus with auto-repeat */
        static const int nav_key[9] = {0, KEY_UP, KEY_UP, KEY_RIGHT, KEY_DOWN, KEY_DOWN, KEY_DOWN, KEY_LEFT, KEY_UP};
        uint64_t now = SDL_GetTicks();
        if (d != a->nav_dir) {
            a->nav_dir = d;
            if (d) { push_key(a, nav_key[d]); a->nav_next = now + 350; a->stick_pointer = 0; }
        } else if (d && now >= a->nav_next) {
            push_key(a, nav_key[d]); a->nav_next = now + 120;
        }
        if (a->pad_dir) { a->pad_dir = 0; agi_set_hold_dir(e, 0); }
        return;
    }
    a->nav_dir = d;
    if (d != a->pad_dir) {
        a->pad_dir = d;
        agi_set_hold_dir(e, d);
    }
}

static int arrow_dir(SDL_Keycode k) {
    switch (k) {
    case SDLK_UP: case SDLK_KP_8: return 1;
    case SDLK_PAGEUP: case SDLK_KP_9: return 2;
    case SDLK_RIGHT: case SDLK_KP_6: return 3;
    case SDLK_PAGEDOWN: case SDLK_KP_3: return 4;
    case SDLK_DOWN: case SDLK_KP_2: return 5;
    case SDLK_END: case SDLK_KP_1: return 6;
    case SDLK_LEFT: case SDLK_KP_4: return 7;
    case SDLK_HOME: case SDLK_KP_7: return 8;
    }
    return 0;
}
static const int DIR_KEY[9] = {0, KEY_UP, KEY_PGUP, KEY_RIGHT, KEY_PGDN, KEY_DOWN, KEY_END, KEY_LEFT, KEY_HOME};

static void held_arrows(App *a) {
    int d = 0;
    /* combine held arrows into one direction */
    int up = a->arrows_down[1], dn = a->arrows_down[5], lf = a->arrows_down[7], rt = a->arrows_down[3];
    for (int i = 2; i <= 8; i += 2) if (a->arrows_down[i]) d = i;
    if (!d) {
        int dx = rt - lf, dy = dn - up;
        static const int tbl[3][3] = {{8, 1, 2}, {7, 0, 3}, {6, 5, 4}};
        d = tbl[dy + 1][dx + 1];
    }
    if (!a->pad_dir) agi_set_hold_dir(a->e, d);
}

static void handle_event(App *a, SDL_Event *ev) {
    Agi *e = a->e;
    switch (ev->type) {
    case SDL_EVENT_QUIT: a->quit = 1; break;
    case SDL_EVENT_GAMEPAD_ADDED: open_first_pad(a); break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        if (a->pad && ev->gdevice.which == SDL_GetGamepadID(a->pad)) {
            SDL_CloseGamepad(a->pad); a->pad = NULL; a->pad_dir = 0; agi_set_hold_dir(e, 0);
            open_first_pad(a);
        }
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN: case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        int i = pad_button_index((SDL_GamepadButton)ev->gbutton.button);
        if (i >= 0) do_action(a, i, a->cfg.pad[i], ev->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
        break;
    }
    case SDL_EVENT_MOUSE_MOTION:
        if (a->cfg.mouse) to_screen(a, ev->motion.x, ev->motion.y, &e->mouse_x, &e->mouse_y);
        if (!a->cfg.pointer && a->stick_pointer) break;   /* our own warp */
        a->stick_pointer = 0;
        break;
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        e->mouse_x = e->mouse_y = -1;
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (!a->cfg.mouse) break;
        to_screen(a, ev->button.x, ev->button.y, &e->mouse_x, &e->mouse_y);
        if (ev->button.button == SDL_BUTTON_LEFT) push_key(a, KEY_MOUSE_L);
        else if (ev->button.button == SDL_BUTTON_RIGHT) push_key(a, KEY_MOUSE_R);
        break;
    case SDL_EVENT_TEXT_INPUT: {
        const unsigned char *t = (const unsigned char *)ev->text.text;
        for (; *t; t++) {
            if (!e->modal && (*t == '+' || *t == '=' || *t == '-')) {
                set_volume(a, a->cfg.volume + (*t == '-' ? -10 : 10));
                continue;
            }
            if (*t >= 32 && *t < 127) push_key(a, *t);
        }
        break;
    }
    case SDL_EVENT_KEY_UP: {
        int d = arrow_dir(ev->key.key);
        if (d) { a->arrows_down[d] = 0; if (a->cfg.arrows_hold && !e->modal) held_arrows(a); }
        break;
    }
    case SDL_EVENT_KEY_DOWN: {
        SDL_Keycode k = ev->key.key;
        SDL_Keymod m = ev->key.mod;
        int alt = (m & SDL_KMOD_ALT) != 0, ctrl = (m & SDL_KMOD_CTRL) != 0;
        if ((k == SDLK_RETURN && alt) || k == SDLK_F11) { if (!ev->key.repeat) toggle_fullscreen(a); break; }
        if (k == SDLK_F12) { if (!ev->key.repeat) screenshot(a); break; }
        if (k == SDLK_F6 && ctrl) {
            if (ev->key.repeat) break;
            a->cfg.shader = a->cfg.shader == SHADER_NONE ? SHADER_CRT_AUTO : SHADER_NONE;
            ini_set(a->ini_path, "display", "shader", shader_to_str(a->cfg.shader));
            set_status(a, a->cfg.shader == SHADER_NONE ? "CRT shader off" : "CRT shader on");
            break;
        }
        if (k == SDLK_F4 && ctrl) {
            if (ev->key.repeat || !a->cfg.sound) break;
            a->cfg.sound = a->cfg.sound == 3 ? 1 : 3;
            if (a->audio) SDL_LockAudioStream(a->audio);
            a->synth.pcspeaker = a->cfg.sound == 1;
            if (a->audio) SDL_UnlockAudioStream(a->audio);
            e->sound_mode = a->cfg.sound;
            e->var[V_SOUNDGEN] = (uint8_t)a->cfg.sound;
            ini_set(a->ini_path, "sound", "sound", a->cfg.sound == 1 ? "pcspeaker" : "tandy");
            set_status(a, a->cfg.sound == 1 ? "Sound: PC speaker" : "Sound: PCjr / Tandy");
            break;
        }
        if (k == SDLK_F5 && ctrl) {
            if (ev->key.repeat) break;
            a->cfg.monitor_vga = !a->cfg.monitor_vga;
            crtgl_set_monitor(a->gl, !a->cfg.monitor_vga);
            ini_set(a->ini_path, "display", "monitor", a->cfg.monitor_vga ? "vga" : "ega");
            set_status(a, a->cfg.monitor_vga ? "Monitor: VGA" : "Monitor: EGA");
            break;
        }
        int d = arrow_dir(k);
        if (d) {
            if (e->modal) { push_key(a, DIR_KEY[d]); break; }
            if (a->cfg.arrows_hold) {
                if (!ev->key.repeat) { a->arrows_down[d] = 1; held_arrows(a); }
            } else if (!ev->key.repeat) push_key(a, DIR_KEY[d]);
            break;
        }
        if (k == SDLK_KP_5) { if (!e->modal && !ev->key.repeat) push_key(a, KEY_CENTER); break; }
        if (k >= SDLK_F1 && k <= SDLK_F10) { push_key(a, KEY_F(1 + (int)(k - SDLK_F1))); break; }
        if (k == SDLK_RETURN || k == SDLK_KP_ENTER) { push_key(a, KEY_ENTER); break; }
        if (k == SDLK_ESCAPE) { push_key(a, KEY_ESC); break; }
        if (k == SDLK_BACKSPACE) { push_key(a, KEY_BACKSPACE); break; }
        if (k == SDLK_TAB) { push_key(a, 9); break; }
        if (k >= 'a' && k <= 'z') {
            if (ctrl) push_key(a, (int)(k - 'a' + 1));
            else if (alt) push_key(a, xt_letter(k) << 8);
            break;
        }
        break;
    }
    }
}

static int host_poll_key(AgiHost *h) {
    App *a = (App *)h->user;
    SDL_Event ev;
    /* test hooks: BCE_TEST_QUIT=<ms> quits after that time, saving a screenshot */
    static int test_mouse = 0;
    if (!test_mouse) { const char *m = getenv("BCE_TEST_MOUSE"); test_mouse = 1;
        if (m) sscanf(m, "%d,%d", &a->e->mouse_x, &a->e->mouse_y); }
    /* test hook: BCE_TEST_VPAD=1 drives a virtual gamepad (see tests/) */
    static int vpad_init = 0;
    static SDL_Joystick *vjoy = NULL;
    if (!vpad_init) {
        vpad_init = 1;
        if (getenv("BCE_TEST_VPAD")) {
            SDL_VirtualJoystickDesc desc;
            SDL_INIT_INTERFACE(&desc);
            desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
            desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
            desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
            desc.name = "Test pad";
            SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
            vjoy = SDL_OpenJoystick(id);
            fprintf(stderr, "[vpad] id %u joy %p gamepad %d err %s\n", (unsigned)id, (void *)vjoy, SDL_IsGamepad(id), SDL_GetError());
        }
    }
    if (vjoy) {
        uint64_t ms = SDL_GetTicks();
        int a_down = (ms > 2000 && ms < 2100) || (ms > 3000 && ms < 3100) || (ms > 4000 && ms < 4100) ||
                     (ms > 5000 && ms < 5100) || (ms > 9000 && ms < 9100);
        SDL_SetJoystickVirtualButton(vjoy, SDL_GAMEPAD_BUTTON_SOUTH, a_down);
        SDL_SetJoystickVirtualAxis(vjoy, SDL_GAMEPAD_AXIS_RIGHTX, (ms > 7000 && ms < 7400) ? -32767 : 0);
        SDL_SetJoystickVirtualAxis(vjoy, SDL_GAMEPAD_AXIS_RIGHTY, (ms > 7000 && ms < 7300) ? 32767 : 0);
        static int reported = 0;
        if (ms > 8000 && !reported) { reported = 1;
            fprintf(stderr, "[vpad] room %d pointer %d,%d ego %d,%d\n", a->e->var[0], a->e->mouse_x, a->e->mouse_y, a->e->obj[0].x, a->e->obj[0].y); }
        if (ms > 13500 && reported == 1) { reported = 2;
            fprintf(stderr, "[vpad] room %d pointer %d,%d ego %d,%d (after A)\n", a->e->var[0], a->e->mouse_x, a->e->mouse_y, a->e->obj[0].x, a->e->obj[0].y); }
    }
    static double test_quit = -1;
    if (test_quit < 0) { const char *q = getenv("BCE_TEST_QUIT"); test_quit = q ? atof(q) : 0; }
    if (test_quit > 0 && SDL_GetTicks() > test_quit) { screenshot(a); a->quit = 1; }
    while (SDL_PollEvent(&ev)) handle_event(a, &ev);
    poll_pad(a);
    if (!a->keyq_n) return 0;
    int k = a->keyq[0];
    memmove(a->keyq, a->keyq + 1, (size_t)(--a->keyq_n) * sizeof(int));
    return k;
}

/* ------------------------------------------------------------------ main */
int main(int argc, char **argv) {
    static App app;
    static Agi eng;
    App *a = &app;
    a->e = &eng;
    config_defaults(&a->cfg);
    const char *bp = SDL_GetBasePath();
    snprintf(a->base, sizeof a->base, "%s", bp ? bp : "./");
    snprintf(a->ini_path, sizeof a->ini_path, "%s%s", a->base, INI_NAME);
    config_load(&a->cfg, a->ini_path);
    {
        /* rewrite an ini file of an older version in the current layout */
        FILE *f = fopen(a->ini_path, "r");
        char first[128] = "";
        if (f) {
            if (!fgets(first, sizeof first, f)) first[0] = 0;
            fclose(f);
            size_t fl = strlen(first);
            while (fl && (first[fl - 1] == '\n' || first[fl - 1] == '\r')) first[--fl] = 0;
            if (strcmp(first, "; " APP_NAME " v" APP_VERSION " settings")) {
                char bak[1300]; snprintf(bak, sizeof bak, "%s.bak", a->ini_path);
                remove(bak); rename(a->ini_path, bak);
                config_write(a->ini_path, &a->cfg);
            }
        }
    }
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--fullscreen")) a->cfg.fullscreen = 1;
        else if (!strcmp(argv[i], "--window")) a->cfg.fullscreen = 0;
        else if (!strcmp(argv[i], "--gamedir") && i + 1 < argc) snprintf(a->cfg.gamedir, sizeof a->cfg.gamedir, "%s", argv[++i]);
    }
    if (!a->cfg.gamedir[0]) snprintf(a->cfg.gamedir, sizeof a->cfg.gamedir, "%s", a->base);
    size_t gl = strlen(a->cfg.gamedir);
    while (gl > 1 && (a->cfg.gamedir[gl - 1] == '/' || a->cfg.gamedir[gl - 1] == '\\')) a->cfg.gamedir[--gl] = 0;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    int loaded = agi_load(&eng, a->cfg.gamedir) == 0;
    char base_ns[1024];
    snprintf(base_ns, sizeof base_ns, "%s", a->base);
    { size_t bl = strlen(base_ns); while (bl > 1 && (base_ns[bl - 1] == '/' || base_ns[bl - 1] == '\\')) base_ns[--bl] = 0; }
    if (!loaded && !strcmp(a->cfg.gamedir, base_ns)) {
        /* also accept the game files one folder up */
        char up[1100];
        snprintf(up, sizeof up, "%s..", a->base);
        if (agi_load(&eng, up) == 0) { loaded = 1; snprintf(a->cfg.gamedir, sizeof a->cfg.gamedir, "%s", up); }
    }
    if (!loaded) {
        char msg[2000];
        snprintf(msg, sizeof msg,
                 "The original Black Cauldron files were not found in:\n%s\n\n"
                 "Put BC-Enhanced.exe in the folder with LOGDIR, PICDIR, VIEWDIR,\n"
                 "SNDDIR, OBJECT, WORDS.TOK and VOL.0 - VOL.2, or set gamedir= in %s.",
                 a->cfg.gamedir, INI_NAME);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, APP_NAME, msg, NULL);
        return 1;
    }

    SDL_WindowFlags wflags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (!a->cfg.renderer_sdl) {
        crtgl_set_attributes();
        a->win = SDL_CreateWindow(APP_NAME, a->cfg.width, a->cfg.height, wflags | SDL_WINDOW_OPENGL);
        if (a->win) {
            a->gl = crtgl_create(a->win, a->gl_error, sizeof a->gl_error);
            crtgl_set_monitor(a->gl, !a->cfg.monitor_vga);
            if (!a->gl) { SDL_DestroyWindow(a->win); a->win = NULL; }
        } else {
            snprintf(a->gl_error, sizeof a->gl_error, "no OpenGL window: %s", SDL_GetError());
        }
        char lp[1200]; snprintf(lp, sizeof lp, "%sbc-enhanced-gl.log", a->base);
        if (!a->gl) {
            FILE *lf = fopen(lp, "w");
            if (lf) { fprintf(lf, APP_NAME " v" APP_VERSION ": OpenGL not available, CRT shader off.\n%s\n", a->gl_error); fclose(lf); }
        } else remove(lp);
    }
    if (!a->win) {
        a->win = SDL_CreateWindow(APP_NAME, a->cfg.width, a->cfg.height, wflags);
        if (!a->win) { SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, APP_NAME, SDL_GetError(), NULL); return 1; }
        a->ren = SDL_CreateRenderer(a->win, NULL);
        if (!a->ren) { SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, APP_NAME, SDL_GetError(), NULL); return 1; }
        SDL_SetRenderVSync(a->ren, 1);
    }
    update_title(a);
    {
        static const uint8_t icon[32 * 32 * 4] = {
#include "icon32.inc"
        };
        SDL_Surface *is = SDL_CreateSurfaceFrom(32, 32, SDL_PIXELFORMAT_RGBA32, (void *)icon, 32 * 4);
        if (is) { SDL_SetWindowIcon(a->win, is); SDL_DestroySurface(is); }
    }
    if (a->cfg.fullscreen) SDL_SetWindowFullscreen(a->win, true);
    SDL_StartTextInput(a->win);
    if (a->cfg.mouse && a->cfg.pointer) SDL_HideCursor();
    open_first_pad(a);

    synth_init(&a->synth, 44100);
    a->synth.pcspeaker = a->cfg.sound == 1;
    eng.sound_mode = a->cfg.sound == 1 ? 1 : 3;
    if (a->cfg.sound) {
        SDL_AudioSpec spec = {SDL_AUDIO_S16, 1, 44100};
        a->audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audio_cb, a);
        if (a->audio) {
            SDL_SetAudioStreamGain(a->audio, (a->cfg.volume / 100.0f) * (a->cfg.volume / 100.0f));
            SDL_ResumeAudioStreamDevice(a->audio);
        }
    }

    a->host.user = a;
    a->host.present = host_present;
    a->host.poll_key = host_poll_key;
    a->host.ticks = host_ticks;
    a->host.sleep = host_sleep;
    a->host.quit_requested = host_quit;
    a->host.sound_play = host_sound_play;
    a->host.sound_stop = host_sound_stop;
    eng.host = &a->host;
    for (int i = 0; i < 4; i++) eng.walk_first[i] = a->cfg.walk_first[i];
    eng.pad_swap = a->cfg.pointer_left;
    eng.rng = (uint32_t)SDL_GetTicks() ^ 0x5EED1986u;
    if (!a->gl && !a->cfg.renderer_sdl && a->cfg.shader != SHADER_NONE)
        set_status(a, "OpenGL 3.3 not available: CRT shader off");

    agi_init_state(&eng);
    agi_run(&eng);

    if (a->audio) SDL_DestroyAudioStream(a->audio);
    if (a->pad) SDL_CloseGamepad(a->pad);
    if (a->gl) crtgl_destroy(a->gl);
    if (a->ren) SDL_DestroyRenderer(a->ren);
    SDL_DestroyWindow(a->win);
    SDL_Quit();
    agi_free(&eng);
    return 0;
}
