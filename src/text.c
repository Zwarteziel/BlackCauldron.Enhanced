/* Black Cauldron Enhanced - text output: message windows, status line,
 * inventory screen, menus and text input, in the style of AGI v2 on EGA.
 * GPL-3.0-or-later. */
#include "agi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static const uint8_t FONT[256][8] = {
#include "font_ibm8x8.inc"
};

void text_draw_char(uint8_t *scr, int x, int y, int ch, int fg, int bg) {
    ch &= 0xFF;
    const uint8_t *g = FONT[ch];
    for (int r = 0; r < 8; r++) {
        int sy = y + r;
        if (sy < 0 || sy >= SCR_H) continue;
        uint8_t bits = g[r];
        for (int c = 0; c < 8; c++) {
            int sx = x + c;
            if (sx < 0 || sx >= SCR_W) continue;
            int on = (bits >> (7 - c)) & 1;
            if (on) scr[sy * SCR_W + sx] = (uint8_t)fg;
            else if (bg >= 0) scr[sy * SCR_W + sx] = (uint8_t)bg;
        }
    }
}

static void draw_string(uint8_t *scr, int x, int y, const char *s, int fg, int bg) {
    for (; *s; s++, x += 8) text_draw_char(scr, x, y, (uint8_t)*s, fg, bg);
}

static void fill_rect(uint8_t *scr, int x, int y, int w, int h, int col) {
    for (int j = 0; j < h; j++) {
        int sy = y + j;
        if (sy < 0 || sy >= SCR_H) continue;
        for (int i = 0; i < w; i++) {
            int sx = x + i;
            if (sx >= 0 && sx < SCR_W) scr[sy * SCR_W + sx] = (uint8_t)col;
        }
    }
}

/* White box with the thin red AGI frame. */
void text_draw_box(uint8_t *scr, int x, int y, int w, int h, int bg, int line) {
    fill_rect(scr, x, y, w, h, bg);
    fill_rect(scr, x + 2, y + 1, w - 4, 1, line);
    fill_rect(scr, x + 2, y + h - 2, w - 4, 1, line);
    fill_rect(scr, x + 2, y + 2, 2, h - 4, line);
    fill_rect(scr, x + w - 4, y + 2, 2, h - 4, line);
}

/* ------------------------------------------------------------ formatting */
void text_format(Agi *e, int logic, const char *in, char *out, int outlen) {
    int o = 0;
    char tmp[512];
    if (!in) in = "";
    while (*in && o < outlen - 1) {
        char c = *in++;
        if (c == '\\' && *in) { out[o++] = *in++; continue; }
        if (c != '%' || !*in) { out[o++] = c; continue; }
        char kind = *in++;
        int n = 0;
        while (isdigit((unsigned char)*in)) n = n * 10 + (*in++ - '0');
        tmp[0] = 0;
        switch (kind) {
        case 'v': {
            int digits = 0;
            if (*in == '|') { in++; while (isdigit((unsigned char)*in)) digits = digits * 10 + (*in++ - '0'); }
            if (digits) snprintf(tmp, sizeof tmp, "%0*d", digits, e->var[n & 255]);
            else snprintf(tmp, sizeof tmp, "%d", e->var[n & 255]);
            break;
        }
        case 'm':
            if (e->logic[logic].loaded && n > 0 && n <= e->logic[logic].nmsg && e->logic[logic].msg[n])
                text_format(e, logic, e->logic[logic].msg[n], tmp, sizeof tmp);
            break;
        case 'g':
            if (e->logic[0].loaded && n > 0 && n <= e->logic[0].nmsg && e->logic[0].msg[n])
                text_format(e, 0, e->logic[0].msg[n], tmp, sizeof tmp);
            break;
        case 'o': {
            int it = e->var[n & 255];
            if (it < e->nitems) snprintf(tmp, sizeof tmp, "%s", e->item_name[it]);
            break;
        }
        case 's':
            if (n < 24) snprintf(tmp, sizeof tmp, "%s", e->str[n]);
            break;
        case 'w':
            break;   /* no parser in this game */
        default:
            tmp[0] = '%'; tmp[1] = kind; tmp[2] = 0;
        }
        for (char *t = tmp; *t && o < outlen - 1; t++) out[o++] = *t;
    }
    out[o] = 0;
}

/* Word wrap into lines of at most width chars. Returns number of lines. */
static int wrap(const char *s, int width, char lines[][41], int maxlines, int *maxw) {
    int n = 0, len = 0;
    *maxw = 0;
    lines[0][0] = 0;
    while (*s && n < maxlines) {
        if (*s == '\n') {
            lines[n][len] = 0;
            if (len > *maxw) *maxw = len;
            n++; len = 0; s++;
            if (n < maxlines) lines[n][0] = 0;
            continue;
        }
        /* next word */
        const char *w = s;
        int wl = 0;
        while (w[wl] && w[wl] != ' ' && w[wl] != '\n') wl++;
        if (len > 0 && len + wl > width) {
            /* drop trailing space */
            while (len > 0 && lines[n][len - 1] == ' ') len--;
            lines[n][len] = 0;
            if (len > *maxw) *maxw = len;
            n++; len = 0;
            if (n >= maxlines) break;
            lines[n][0] = 0;
            while (*s == ' ') s++;
            continue;
        }
        if (wl > width) wl = width;   /* very long word: hard cut */
        memcpy(lines[n] + len, s, wl);
        len += wl; s += wl;
        if (*s == ' ' && len < width) { lines[n][len++] = ' '; s++; }
        else if (*s == ' ') s++;
        lines[n][len] = 0;
    }
    if (n < maxlines && len > 0) {
        while (len > 0 && lines[n][len - 1] == ' ') len--;
        lines[n][len] = 0;
        if (len > *maxw) *maxw = len;
        n++;
    }
    return n;
}

/* The open message window, drawn over the composed screen. */
static char win_lines[22][41];
static int win_nlines, win_row, win_col, win_width;

static void draw_window(Agi *e) {
    if (!e->win_open) return;
    text_draw_box(e->screen, e->win_x, e->win_y, e->win_w, e->win_h, 15, 4);
    for (int i = 0; i < win_nlines; i++)
        draw_string(e->screen, win_col * 8, (win_row + i) * 8, win_lines[i], 0, 15);
}

void agi_frame(Agi *e) {
    compose_screen(e);
    draw_window(e);
    if (e->host && e->host->present) e->host->present(e->host);
}

void text_shake(Agi *e, int n) {
    static uint8_t buf[SCR_W * SCR_H];
    compose_screen(e);
    draw_window(e);
    memcpy(buf, e->screen, sizeof buf);
    for (int i = 0; i < n * 8 && !e->quit; i++) {
        int dx = (i & 1) ? 4 : -4, dy = (i & 2) ? 2 : -2;
        memset(e->screen, 0, sizeof e->screen);
        for (int y = 0; y < SCR_H; y++) {
            int sy = y - dy;
            if (sy < 0 || sy >= SCR_H) continue;
            for (int x = 0; x < SCR_W; x++) {
                int sx = x - dx;
                if (sx >= 0 && sx < SCR_W) e->screen[y * SCR_W + x] = buf[sy * SCR_W + sx];
            }
        }
        if (e->host->present) e->host->present(e->host);
        e->host->poll_key(e->host);
        e->host->sleep(e->host, 40);
    }
}

int text_wait_key(Agi *e) {
    e->modal = 1;
    int key = 0;
    while (!key && !e->quit) {
        agi_frame(e);
        key = e->host->poll_key(e->host);
        if (key == KEY_MOUSE_L) key = KEY_ENTER;
        else if (key == KEY_MOUSE_R) key = KEY_ESC;
        if (e->host->quit_requested(e->host)) { e->quit = 1; break; }
        if (!key) e->host->sleep(e->host, 10);
    }
    e->modal = 0;
    return key;
}

void text_close_window(Agi *e) {
    e->win_open = 0;
}

void (*agi_log_hook)(Agi *e, const char *what, const char *text);

void text_print(Agi *e, const char *msg, int row, int col, int width, int wait) {
    if (agi_log_hook) agi_log_hook(e, "print", msg);
    if (e->win_open) text_close_window(e);
    if (width <= 0) width = 30;
    if (width > 36) width = 36;
    int maxw;
    win_nlines = wrap(msg, width, win_lines, 20, &maxw);
    if (win_nlines == 0) { win_nlines = 1; win_lines[0][0] = 0; }
    if (maxw < 1) maxw = 1;
    win_width = maxw;
    if (row < 0) row = e->pic_row + (21 - win_nlines) / 2;
    if (col < 0) col = (40 - maxw) / 2;
    if (row + win_nlines > 23) row = 23 - win_nlines;
    if (row < 0) row = 0;
    if (col + maxw > 39) col = 39 - maxw;
    if (col < 1) col = 1;
    win_row = row; win_col = col;
    e->win_x = col * 8 - 8;
    e->win_y = row * 8 - 5;
    e->win_w = maxw * 8 + 16;
    e->win_h = win_nlines * 8 + 10;
    e->win_open = 1;
    if (!wait) return;
    if (e->flag[FL_PRINT_MODE]) {
        /* window stays open until the next print or close.window */
        e->flag[FL_PRINT_MODE] = 0;
        return;
    }
    if (e->var[V_WIN_TIMER]) {
        uint32_t until = e->host->ticks(e->host) + e->var[V_WIN_TIMER] * 500u;
        e->modal = 1;
        while (!e->quit && e->host->ticks(e->host) < until) {
            agi_frame(e);
            int k = e->host->poll_key(e->host);
            if (e->host->quit_requested(e->host)) { e->quit = 1; break; }
            if (k) break;
            e->host->sleep(e->host, 10);
        }
        e->modal = 0;
    } else {
        for (;;) {
            int k = text_wait_key(e);
            if (e->quit || k == KEY_ENTER || k == KEY_ESC || k == ' ' || k) break;
        }
    }
    text_close_window(e);
}

int text_confirm(Agi *e, const char *msg) {
    text_print(e, msg, -1, -1, 0, 0);
    int r = 0;
    for (;;) {
        int k = text_wait_key(e);
        if (e->quit) break;
        if (k == KEY_ENTER) { r = 1; break; }
        if (k == KEY_ESC) { r = 0; break; }
    }
    text_close_window(e);
    return r;
}

/* ------------------------------------------------------- text cell layer */
static void attr(Agi *e, int *fg, int *bg) {
    *fg = e->fg; *bg = e->bg;
    if (!e->text_mode) {
        if (*bg) { *bg = 15; if (*fg == 15) *fg = 0; }
    }
}

void text_display(Agi *e, int row, int col, const char *msg) {
    int fg, bg;
    attr(e, &fg, &bg);
    for (const char *s = msg; *s; s++) {
        if (*s == '\n' || col >= 40) { row++; col = 0; if (*s == '\n') continue; }
        if (row < 0 || row >= 25) break;
        e->text_ch[row][col] = (uint8_t)*s;
        e->text_fg[row][col] = (uint8_t)fg;
        e->text_bg[row][col] = (uint8_t)bg;
        e->text_used[row][col] = 1;
        col++;
    }
}

void text_clear_rect(Agi *e, int r1, int c1, int r2, int c2, int color) {
    if (!e->text_mode && color) color = 15;
    for (int r = r1; r <= r2 && r < 25; r++)
        for (int c = c1; c <= c2 && c < 40; c++) {
            if (r < 0 || c < 0) continue;
            /* inside the picture with colour 0: just remove text */
            if (!e->text_mode && color == 0 && r >= e->pic_row && r < e->pic_row + 21) {
                e->text_used[r][c] = 0;
                continue;
            }
            e->text_ch[r][c] = ' ';
            e->text_fg[r][c] = 0;
            e->text_bg[r][c] = (uint8_t)color;
            e->text_used[r][c] = 1;
        }
}

void text_clear_lines(Agi *e, int top, int bottom, int color) {
    text_clear_rect(e, top, 0, bottom, 39, color);
}


/* ------------------------------------------------------------- inventory */
void text_inventory(Agi *e) {
    int items[MAX_ITEMS], n = 0;
    for (int i = 0; i < e->nitems; i++)
        if (e->item_room[i] == 255) items[n++] = i;
    int sel = 0, selecting = e->flag[FL_STATUS_SELECT] != 0;
    e->modal = 1;
    for (;;) {
        memset(e->screen, 15, sizeof e->screen);
        const char *title = "You are carrying:";
        draw_string(e->screen, (40 - (int)strlen(title)) / 2 * 8, 0, title, 0, 15);
        if (n == 0) {
            draw_string(e->screen, (40 - 7) / 2 * 8, 2 * 8, "nothing", 0, 15);
        }
        for (int k = 0; k < n; k++) {
            const char *nm = e->item_name[items[k]];
            int row = 2 + k / 2;
            int col = (k & 1) ? 39 - (int)strlen(nm) : 1;
            int inv = selecting && k == sel;
            draw_string(e->screen, col * 8, row * 8, nm, inv ? 15 : 0, inv ? 0 : 15);
        }
        const char *foot = selecting ? "Press ENTER to select, ESC to cancel"
                                     : "Press a key to return to the game";
        draw_string(e->screen, (40 - (int)strlen(foot)) / 2 * 8, 24 * 8, foot, 0, 15);
        if (e->host->present) e->host->present(e->host);
        int k = e->host->poll_key(e->host);
        if (e->host->quit_requested(e->host)) { e->quit = 1; break; }
        if (!k) { e->host->sleep(e->host, 10); continue; }
        if (k == KEY_MOUSE_R) k = KEY_ESC;
        if (k == KEY_MOUSE_L) {
            /* mouse: pick the item under the pointer */
            int mr = e->mouse_y / 8, mc = e->mouse_x / 8;
            int hit = -1;
            for (int j = 0; j < n; j++) {
                const char *nm = e->item_name[items[j]];
                int row = 2 + j / 2, col = (j & 1) ? 39 - (int)strlen(nm) : 1;
                if (mr == row && mc >= col && mc < col + (int)strlen(nm)) hit = j;
            }
            if (!selecting) break;
            if (hit >= 0) { sel = hit; k = KEY_ENTER; }
            else continue;
        }
        if (!selecting) break;
        if (k == KEY_ESC) { e->var[V_SEL_ITEM] = 255; break; }
        if (k == KEY_ENTER) { e->var[V_SEL_ITEM] = n ? (uint8_t)items[sel] : 255; break; }
        if (n == 0) continue;
        if (k == KEY_LEFT && sel > 0) sel--;
        else if (k == KEY_RIGHT && sel < n - 1) sel++;
        else if (k == KEY_UP && sel >= 2) sel -= 2;
        else if (k == KEY_DOWN && sel + 2 < n) sel += 2;
    }
    e->modal = 0;
}

/* ----------------------------------------------------------- text input */
int text_get_string(Agi *e, const char *prompt, int row, int col, int maxlen, char *out) {
    char buf[41] = {0};
    int len = 0;
    if (maxlen > 40) maxlen = 40;
    if (maxlen < 1) maxlen = 1;
    int boxed = row < 0;
    e->modal = 1;
    for (;;) {
        compose_screen(e);
        if (boxed) {
            int maxw;
            char lines[22][41];
            char full[512];
            snprintf(full, sizeof full, "%s", prompt);
            int nl = wrap(full, 30, lines, 18, &maxw);
            if (maxw < maxlen + 1) maxw = maxlen + 1;
            if (maxw > 36) maxw = 36;
            int r = e->pic_row + (21 - (nl + 2)) / 2, c = (40 - maxw) / 2;
            text_draw_box(e->screen, c * 8 - 8, r * 8 - 5, maxw * 8 + 16, (nl + 2) * 8 + 10, 15, 4);
            for (int i = 0; i < nl; i++) draw_string(e->screen, c * 8, (r + i) * 8, lines[i], 0, 15);
            draw_string(e->screen, c * 8, (r + nl + 1) * 8, buf, 0, 15);
            text_draw_char(e->screen, (c + len) * 8, (r + nl + 1) * 8, '_', 0, 15);
        } else {
            int fg = 15, bg = 0;
            fill_rect(e->screen, 0, row * 8, SCR_W, 8, bg);
            draw_string(e->screen, col * 8, row * 8, prompt, fg, bg);
            int pc = col + (int)strlen(prompt);
            draw_string(e->screen, pc * 8, row * 8, buf, fg, bg);
            text_draw_char(e->screen, (pc + len) * 8, row * 8, '_', fg, bg);
        }
        if (e->host->present) e->host->present(e->host);
        int k = e->host->poll_key(e->host);
        if (e->host->quit_requested(e->host)) { e->quit = 1; break; }
        if (!k) { e->host->sleep(e->host, 10); continue; }
        if (k == KEY_MOUSE_L) k = KEY_ENTER;
        if (k == KEY_MOUSE_R) k = KEY_ESC;
        if (k == KEY_ENTER) { memcpy(out, buf, len + 1); e->modal = 0; return 1; }
        if (k == KEY_ESC) { out[0] = 0; e->modal = 0; return 0; }
        if (k == KEY_BACKSPACE) { if (len) buf[--len] = 0; continue; }
        if (k >= 32 && k < 127 && len < maxlen) { buf[len++] = (char)k; buf[len] = 0; }
    }
    e->modal = 0;
    out[0] = 0;
    return 0;
}

/* ------------------------------------------------------------------ menu */
static int menu_available(Agi *e) {
    return e->nmenu && e->flag[FL_MENU] && !e->text_mode;
}

/* Index of the menu title under a screen position on the status bar, or -1
 * outside the bar. A click on the bar but between titles opens menu 0. */
int text_menu_bar_hit(Agi *e, int sx, int sy) {
    if (!menu_available(e) || !e->status_on || sx < 0) return -1;
    if (sy < e->status_row * 8 || sy >= e->status_row * 8 + 8) return -1;
    int mc = sx / 8;
    for (int i = 0; i < e->nmenu; i++)
        if (mc >= e->menu[i].col - 1 && mc <= e->menu[i].col + (int)strlen(e->menu[i].text) - 1) return i;
    return 0;
}

static void draw_menu_bar(Agi *e, int y, int sel) {
    fill_rect(e->screen, 0, y, SCR_W, 8, 15);
    for (int i = 0; i < e->nmenu; i++)
        draw_string(e->screen, e->menu[i].col * 8, y, e->menu[i].text, i == sel ? 15 : 0, i == sel ? 0 : 15);
}

/* Status line; while the mouse is over it, the menu bar (as in the later
 * Sierra games), so a click there opens the menus. */
void text_status_line(Agi *e) {
    int y = e->status_row * 8;
    if (!e->modal && e->mouse_y >= y && e->mouse_y < y + 8 && menu_available(e)) {
        int m = text_menu_bar_hit(e, e->mouse_x, e->mouse_y);
        int mc = e->mouse_x / 8, over = -1;
        if (m >= 0 && mc >= e->menu[m].col && mc < e->menu[m].col + (int)strlen(e->menu[m].text)) over = m;
        draw_menu_bar(e, y, over);
        return;
    }
    fill_rect(e->screen, 0, y, SCR_W, 8, 15);
    char buf[64];
    snprintf(buf, sizeof buf, " Score:%d of %-3d", e->var[V_SCORE], e->var[V_MAX_SCORE]);
    draw_string(e->screen, 0, y, buf, 0, 15);
    snprintf(buf, sizeof buf, "Sound:%s", e->flag[FL_SOUND] ? "on" : "off");
    draw_string(e->screen, 30 * 8, y, buf, 0, 15);
}

int text_menu(Agi *e, int start) {
    if (!e->nmenu) return 0;
    int m = (start >= 0 && start < e->nmenu) ? start : 0, it = 0;
    int last_mx = e->mouse_x, last_my = e->mouse_y;
    e->modal = 1;
    for (;;) {
        Menu *mn = &e->menu[m];
        int w = 0;
        for (int i = 0; i < mn->count; i++) {
            int l = (int)strlen(e->mitem[mn->first + i].text);
            if (l > w) w = l;
        }
        int col = mn->col;
        if (col + w > 39) col = 39 - w;
        if (col < 1) col = 1;
        /* the mouse moves the highlight */
        if (e->mouse_x != last_mx || e->mouse_y != last_my) {
            last_mx = e->mouse_x; last_my = e->mouse_y;
            int mr = e->mouse_y / 8, mc = e->mouse_x / 8;
            if (e->mouse_y >= 0 && mr == 0) {
                for (int i = 0; i < e->nmenu; i++)
                    if (i != m && mc >= e->menu[i].col && mc < e->menu[i].col + (int)strlen(e->menu[i].text)) { m = i; it = 0; }
                continue;
            }
            if (mr >= 2 && mr < 2 + mn->count && mc >= col - 1 && mc <= col + w) it = mr - 2;
        }
        compose_screen(e);
        draw_menu_bar(e, 0, m);
        text_draw_box(e->screen, col * 8 - 8, 8 + 3, w * 8 + 16, mn->count * 8 + 10, 15, 0);
        for (int i = 0; i < mn->count; i++) {
            MenuItem *mi = &e->mitem[mn->first + i];
            int inv = i == it;
            int fg = mi->enabled ? 0 : 8;
            draw_string(e->screen, col * 8, (2 + i) * 8, mi->text, inv ? 15 : fg, inv ? 0 : 15);
        }
        if (e->host->present) e->host->present(e->host);
        int k = e->host->poll_key(e->host);
        if (e->host->quit_requested(e->host)) { e->quit = 1; break; }
        if (!k) { e->host->sleep(e->host, 10); continue; }
        if (k == KEY_MOUSE_R) k = KEY_ESC;
        if (k == KEY_MOUSE_L) {
            int mr = e->mouse_y / 8, mc = e->mouse_x / 8;
            if (mr == 0) {
                int hit = -1;
                for (int i = 0; i < e->nmenu; i++)
                    if (mc >= e->menu[i].col && mc < e->menu[i].col + (int)strlen(e->menu[i].text)) hit = i;
                if (hit < 0 || hit == m) break;     /* click on the open title closes */
                m = hit; it = 0;
                continue;
            }
            if (mr >= 2 && mr < 2 + mn->count && mc >= col - 1 && mc <= col + w) { it = mr - 2; k = KEY_ENTER; }
            else k = KEY_ESC;
        }
        if (k == KEY_ESC) break;
        if (k == KEY_ENTER) {
            MenuItem *mi = &e->mitem[mn->first + it];
            if (mi->enabled) {
                if (mi->ctrl == CTRL_GO_BACK) e->go_back_request = 1;
                else if (mi->ctrl == CTRL_HUNGER) agi_toggle_hunger(e);
                else e->ctrl[mi->ctrl] = 1;
                e->modal = 0;
                return 1;
            }
            continue;
        }
        if (k == KEY_LEFT) { m = (m + e->nmenu - 1) % e->nmenu; it = 0; }
        else if (k == KEY_RIGHT) { m = (m + 1) % e->nmenu; it = 0; }
        else if (k == KEY_UP) it = (it + mn->count - 1) % mn->count;
        else if (k == KEY_DOWN) it = (it + 1) % mn->count;
    }
    e->modal = 0;
    return 0;
}
