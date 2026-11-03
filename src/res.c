/* Black Cauldron Enhanced - resources: directories, VOL files, logics,
 * pictures and views (AGI v2 formats). GPL-3.0-or-later. */
#include "agi.h"
#include "version.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char CRYPT_KEY[] = "Avis Durgan";

static uint8_t *read_file(const char *dir, const char *name, int *len) {
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc(n + 1);
    if (buf && fread(buf, 1, n, f) != (size_t)n) { free(buf); buf = NULL; }
    fclose(f);
    if (buf) { buf[n] = 0; if (len) *len = (int)n; }
    return buf;
}

static int load_dir(Agi *e, const char *name, uint32_t *out) {
    int len;
    uint8_t *b = read_file(e->dir, name, &len);
    if (!b) return -1;
    int n = len / 3;
    if (n > 256) n = 256;
    for (int i = 0; i < 256; i++) out[i] = 0xFFFFFFFF;
    for (int i = 0; i < n; i++) {
        const uint8_t *p = b + i * 3;
        if (p[0] == 0xFF && p[1] == 0xFF && p[2] == 0xFF) continue;
        out[i] = ((uint32_t)p[0] << 16) | (p[1] << 8) | p[2];
    }
    free(b);
    return n;
}

const uint8_t *agi_res(Agi *e, uint32_t entry, int *len) {
    if (entry == 0xFFFFFFFF) return NULL;
    int v = entry >> 20;
    uint32_t off = entry & 0xFFFFF;
    if (v >= 16 || !e->vol[v] || off + 5 > (uint32_t)e->vol_len[v]) return NULL;
    const uint8_t *p = e->vol[v] + off;
    if (p[0] != 0x12 || p[1] != 0x34) return NULL;
    int l = p[3] | (p[4] << 8);
    if (off + 5 + l > (uint32_t)e->vol_len[v]) l = e->vol_len[v] - off - 5;
    *len = l;
    return p + 5;
}

int agi_load_logic(Agi *e, int n) {
    Logic *L = &e->logic[n];
    if (L->loaded) return 0;
    int len;
    const uint8_t *d = agi_res(e, e->logdir[n], &len);
    if (!d || len < 2) {
        snprintf(e->err, sizeof e->err, "logic %d not found", n);
        return -1;
    }
    L->data = malloc(len);
    memcpy(L->data, d, len);
    L->len = len;
    int clen = L->data[0] | (L->data[1] << 8);
    L->code = L->data + 2;
    L->code_len = clen;
    int mo = 2 + clen;
    L->nmsg = 0;
    if (mo < len) {
        int nm = L->data[mo];
        int base = mo + 1;
        int text = base + 2 + 2 * nm;
        /* decrypt the message text in place */
        for (int i = text, k = 0; i < len; i++, k++)
            L->data[i] ^= (uint8_t)CRYPT_KEY[k % 11];
        L->nmsg = nm;
        for (int i = 0; i < nm && i < 255; i++) {
            int po = base + 2 + 2 * i;
            if (po + 1 >= len) break;
            int ofs = L->data[po] | (L->data[po + 1] << 8);
            L->msg[i + 1] = (ofs && base + ofs < len) ? (char *)L->data + base + ofs : NULL;
        }
    }
    /* Help screen (logic 105): the joystick page becomes a page about the
     * mouse and the controller, in the same layout and colours. */
    if (n == 105 && L->nmsg >= 31 && L->msg[21] && !strcmp(L->msg[21], "JOYSTICK COMMANDS")) {
        static const char *help[11] = {
            "MOUSE & CONTROLLER",                      /* row 2, column 11 */
            "Click where you want Taran to go.",       /* row 5 */
            "Right-click to Look, Do, Use or Select.", /* row 6 */
            "Click the bar at the top for the menus.", /* row 8 */
            "Right-click or ESC closes a menu.",       /* row 9 */
            "Controller: stick or d-pad moves Taran.", /* row 11 */
            "A = Do     X = Look     Y = Use",         /* row 12 */
            "View = Select an object to use.",         /* row 13 */
            "LB / RB = previous / next object.",       /* row 15 */
            "Start = menus.  B = back to the game.",   /* row 16 */
            "In menus and windows, A means ENTER."     /* row 17 */
        };
        for (int i = 0; i < 11; i++) L->msg[21 + i] = (char *)help[i];
        /* show "Press + / - to make sound louder / softer" on the keyboard
         * page: the game shows it only on a Tandy 1000 (v20 == 2), but here
         * + and - always set the volume. if equaln(20,2) -> equaln(20,0) */
        uint8_t *c = L->data + 2 + 40;
        if (L->code_len > 48 && c[0] == 0xFF && c[1] == 0x01 && c[2] == 20 && c[3] == 2) c[3] = 0;
    }
    /* Sierra > About: add this interpreter's version under the game's own,
     * in the same style ("Version 2.00   6/14/87"): the version without its
     * second dot (0.1.8 -> 0.18) and the build date as m/d/yy. */
    if (n == 0 && L->nmsg >= 52 && L->msg[52] && !strcmp(L->msg[52], "%m56\n\nYou've been playing for \n")) {
        static char about[200];
        char ver[16];
        int vo = 0, dots = 0;
        for (const char *p = APP_VERSION; *p && vo < 15; p++) {
            if (*p == '.' && ++dots == 2) continue;
            ver[vo++] = *p;
        }
        ver[vo] = 0;
        static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
        const char *d = __DATE__;            /* "Nov  3 2026" */
        int mon = 1;
        for (int m = 0; m < 12; m++) if (!strncmp(d, months + 3 * m, 3)) mon = m + 1;
        int day = atoi(d + 4), yy = atoi(d + 7) % 100;
        snprintf(about, sizeof about,
                 "%%m56\n"
                 "  Black Cauldron Enhanced\n"
                 "\n"
                 "  Version %s   %d/%d/%02d\n"
                 "\nYou've been playing for \n",
                 ver, mon, day, yy);
        L->msg[52] = about;
    }
    /* Action menu: "New Object" becomes "Select Object", as in the mouse
     * menu. The items get two columns more so the shortcuts stay aligned. */
    if (n == 0 && L->nmsg >= 77 && L->msg[74] && !strcmp(L->msg[74], "New Object  <F3>")) {
        static const char *action[7] = {
            "Inventory    <TAB>", "See Object        ", "------------------",
            "Select Object <F3>", "Use Object    <F4>", "Do            <F6>",
            "Look          <F8>"};
        for (int i = 0; i < 7; i++) L->msg[71 + i] = (char *)action[i];
    }
    L->scan_start = 0;
    L->loaded = 1;
    return 0;
}

int agi_load_view(Agi *e, int n) {
    View *v = &e->view[n];
    if (v->loaded) return 0;
    int len;
    const uint8_t *d = agi_res(e, e->viewdir[n], &len);
    if (!d || len < 5) {
        snprintf(e->err, sizeof e->err, "view %d not found", n);
        return -1;
    }
    memset(v, 0, sizeof *v);
    v->data = malloc(len + 1);
    memcpy(v->data, d, len);
    v->data[len] = 0;
    v->len = len;
    const uint8_t *b = v->data;
    v->nloops = b[2];
    if (v->nloops > 16) v->nloops = 16;
    int desc = b[3] | (b[4] << 8);
    v->desc = (desc && desc < len) ? (const char *)b + desc : NULL;
    for (int l = 0; l < v->nloops; l++) {
        int lo = b[5 + 2 * l] | (b[6 + 2 * l] << 8);
        if (lo >= len) continue;
        Loop *lp = &v->loop[l];
        lp->ncels = b[lo];
        if (lp->ncels > 32) lp->ncels = 32;
        for (int c = 0; c < lp->ncels; c++) {
            int co = lo + (b[lo + 1 + 2 * c] | (b[lo + 2 + 2 * c] << 8));
            if (co + 3 > len) { lp->ncels = c; break; }
            Cel *cel = &lp->cel[c];
            cel->w = b[co];
            cel->h = b[co + 1];
            cel->transp = b[co + 2] & 0x0F;
            cel->mirrored_flag = (b[co + 2] & 0x80) != 0;
            cel->mirror_loop = (b[co + 2] >> 4) & 7;
            cel->rle = b + co + 3;
        }
    }
    v->loaded = 1;
    return 0;
}

int pri_from_y(Agi *e, int y) {
    if (y < 0) return 4;
    if (y >= PIC_H) y = PIC_H - 1;
    return e->pri_table[y];
}

/* ---------------------------------------------------------------- pictures */
typedef struct {
    Agi *e;
    int vis_on, pri_on, vcol, pcol;
} PicState;

static void pplot(PicState *s, int x, int y) {
    if (x < 0 || x >= PIC_W || y < 0 || y >= PIC_H) return;
    if (s->vis_on) s->e->pic_vis[y * PIC_W + x] = (uint8_t)s->vcol;
    if (s->pri_on) s->e->pic_pri[y * PIC_W + x] = (uint8_t)s->pcol;
}

static int clipi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void pline(PicState *s, int x1, int y1, int x2, int y2) {
    x1 = clipi(x1, 0, PIC_W - 1); x2 = clipi(x2, 0, PIC_W - 1);
    y1 = clipi(y1, 0, PIC_H - 1); y2 = clipi(y2, 0, PIC_H - 1);
    if (x1 == x2) {
        if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
        for (; y1 <= y2; y1++) pplot(s, x1, y1);
        return;
    }
    if (y1 == y2) {
        if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
        for (; x1 <= x2; x1++) pplot(s, x1, y1);
        return;
    }
    int sy = 1, dy = y2 - y1; if (dy < 0) { sy = -1; dy = -dy; }
    int sx = 1, dx = x2 - x1; if (dx < 0) { sx = -1; dx = -dx; }
    int i, det, ex, ey;
    if (dy > dx) { i = dy; det = dy; ex = dy / 2; ey = 0; }
    else { i = dx; det = dx; ex = 0; ey = dx / 2; }
    int x = x1, y = y1;
    pplot(s, x, y);
    do {
        ey += dy; if (ey >= det) { ey -= det; y += sy; }
        ex += dx; if (ex >= det) { ex -= det; x += sx; }
        pplot(s, x, y);
    } while (--i > 0);
}

static int fill_ok(PicState *s, int x, int y) {
    int i = y * PIC_W + x;
    if (!s->vis_on && !s->pri_on) return 0;
    if (!s->pri_on && s->vis_on && s->vcol != 15) return s->e->pic_vis[i] == 15;
    if (s->pri_on && !s->vis_on && s->pcol != 4) return s->e->pic_pri[i] == 4;
    return s->vis_on && s->e->pic_vis[i] == 15 && s->vcol != 15;
}

static void pfill(PicState *s, int x, int y) {
    static int16_t stack[PIC_W * PIC_H * 2];
    if (x < 0 || x >= PIC_W || y < 0 || y >= PIC_H) return;
    if (!fill_ok(s, x, y)) return;
    int sp = 0;
    stack[sp++] = (int16_t)x; stack[sp++] = (int16_t)y;
    while (sp) {
        y = stack[--sp]; x = stack[--sp];
        if (!fill_ok(s, x, y)) continue;
        /* scanline fill */
        int l = x, r = x;
        while (l > 0 && fill_ok(s, l - 1, y)) l--;
        while (r < PIC_W - 1 && fill_ok(s, r + 1, y)) r++;
        for (int xx = l; xx <= r; xx++) pplot(s, xx, y);
        for (int d = -1; d <= 1; d += 2) {
            int ny = y + d;
            if (ny < 0 || ny >= PIC_H) continue;
            int in = 0;
            for (int xx = l; xx <= r; xx++) {
                if (fill_ok(s, xx, ny)) {
                    if (!in && sp < (int)(sizeof stack / sizeof stack[0]) - 2) {
                        stack[sp++] = (int16_t)xx; stack[sp++] = (int16_t)ny;
                    }
                    in = 1;
                } else in = 0;
            }
        }
    }
}

/* Brush plotting (not used by The Black Cauldron's pictures, but part of
 * the format). Circle masks as in the original interpreter. */
static const uint16_t circle_data[] = {
    0x8000,
    0xE000, 0xE000, 0xE000,
    0x7000, 0xF800, 0xF800, 0xF800, 0x7000,
    0x3800, 0x7C00, 0xFE00, 0xFE00, 0xFE00, 0x7C00, 0x3800,
    0x1C00, 0x7F00, 0xFF80, 0xFF80, 0xFF80, 0xFF80, 0xFF80, 0x7F00, 0x1C00,
    0x0E00, 0x3F80, 0x7FC0, 0x7FC0, 0xFFE0, 0xFFE0, 0xFFE0, 0x7FC0, 0x7FC0, 0x3F80, 0x1F00, 0x0E00,
    0x0F80, 0x3FE0, 0x7FF0, 0x7FF0, 0xFFF8, 0xFFF8, 0xFFF8, 0xFFF8, 0xFFF8, 0x7FF0, 0x7FF0, 0x3FE0, 0x0F80,
    0x07C0, 0x1FF0, 0x3FF8, 0x7FFC, 0x7FFC, 0xFFFE, 0xFFFE, 0xFFFE, 0xFFFE, 0xFFFE, 0x7FFC, 0x7FFC, 0x3FF8, 0x1FF0, 0x07C0
};
static const uint8_t circle_list[] = {0, 1, 4, 9, 16, 25, 37, 50};

static void pbrush(PicState *s, int code, int texture, int x, int y) {
    int size = code & 7;
    const uint16_t *cp = &circle_data[circle_list[size]];
    int px = x * 2 - size;
    if (px < 0) px = 0;
    int lim = PIC_W * 2 - 2 * size;
    if (px >= lim) px = lim;
    px /= 2;
    int fx = px;
    int py = y - size;
    if (py < 0) py = 0;
    lim = PIC_H - 1 - 2 * size;
    if (py >= lim) py = lim;
    int ey = py + size * 2 + 1;
    int width = (size * 2 + 1) * 2;
    uint8_t t = (uint8_t)(texture | 1);
    for (; py < ey; py++) {
        uint16_t word = *cp++;
        for (int c = 0; c <= width; c += 4) {
            if ((code & 0x10) || (word & (0x8000 >> (c >> 1)))) {
                int draw = 1;
                if (code & 0x20) {
                    int b = t & 1;
                    t >>= 1;
                    if (b) t ^= 0xB8;
                    draw = (t & 3) == 2;
                }
                if (draw) pplot(s, px, py);
            }
            px++;
        }
        px = fx;
    }
}

void pic_draw(Agi *e, int n, int clear) {
    if (clear) {
        memset(e->pic_vis, 15, sizeof e->pic_vis);
        memset(e->pic_pri, 4, sizeof e->pic_pri);
    }
    int len;
    const uint8_t *d = agi_res(e, e->picdir[n], &len);
    if (!d) return;
    PicState s = {e, 0, 0, 15, 4};
    int i = 0, pen = 0;
#define NEXT_ARG(v) (i < len && d[i] < 0xF0 ? ((v) = d[i++], 1) : 0)
    while (i < len) {
        int c = d[i++];
        if (c == 0xFF) break;
        int x, y, a, b;
        switch (c) {
        case 0xF0: s.vcol = d[i++] & 15; s.vis_on = 1; break;
        case 0xF1: s.vis_on = 0; break;
        case 0xF2: s.pcol = d[i++] & 15; s.pri_on = 1; break;
        case 0xF3: s.pri_on = 0; break;
        case 0xF4: case 0xF5: {
            int turn_x = (c == 0xF5);
            if (!NEXT_ARG(x) || !NEXT_ARG(y)) break;
            pplot(&s, x, y);
            while (NEXT_ARG(a)) {
                if (turn_x) { pline(&s, x, y, a, y); x = a; }
                else { pline(&s, x, y, x, a); y = a; }
                turn_x = !turn_x;
            }
            break;
        }
        case 0xF6:
            if (!NEXT_ARG(x) || !NEXT_ARG(y)) break;
            pplot(&s, x, y);
            while (NEXT_ARG(a)) {
                if (!NEXT_ARG(b)) break;
                pline(&s, x, y, a, b); x = a; y = b;
            }
            break;
        case 0xF7:
            if (!NEXT_ARG(x) || !NEXT_ARG(y)) break;
            pplot(&s, x, y);
            while (NEXT_ARG(a)) {
                int dx = (a >> 4) & 7, dy = a & 7;
                if (a & 0x80) dx = -dx;
                if (a & 0x08) dy = -dy;
                pline(&s, x, y, x + dx, y + dy);
                x += dx; y += dy;
            }
            break;
        case 0xF8:
            while (NEXT_ARG(x)) {
                if (!NEXT_ARG(y)) break;
                pfill(&s, x, y);
            }
            break;
        case 0xF9: pen = d[i++]; break;
        case 0xFA: {
            int tex = 0;
            for (;;) {
                if (pen & 0x20) { if (!NEXT_ARG(tex)) break; tex = (tex >> 1) & 0x7F; }
                if (!NEXT_ARG(x) || !NEXT_ARG(y)) break;
                pbrush(&s, pen, tex, x, y);
            }
            break;
        }
        default:
            while (i < len && d[i] < 0xF0) i++;
            break;
        }
    }
#undef NEXT_ARG
}

/* Draw one cel with priority handling. x = left, y = bottom line.
 * Writes visual and (where no control line) priority. */
void view_draw_cel(Agi *e, int vn, int ln, int cn, int x, int y, int pri,
                   uint8_t *vis, uint8_t *pribuf, int to_pic) {
    View *v = &e->view[vn];
    if (!v->loaded || ln >= v->nloops || cn >= v->loop[ln].ncels) return;
    const Cel *c = &v->loop[ln].cel[cn];
    int mirror = c->mirrored_flag && c->mirror_loop != ln;
    const uint8_t *p = c->rle;
    const uint8_t *end = v->data + v->len;
    int top = y - c->h + 1;
    for (int row = 0; row < c->h && p < end; row++) {
        int col = 0;
        int sy = top + row;
        for (;;) {
            if (p >= end) break;
            uint8_t b = *p++;
            if (b == 0) break;
            int color = b >> 4, cnt = b & 15;
            for (int k = 0; k < cnt; k++, col++) {
                if (color == c->transp) continue;
                int cx = mirror ? (c->w - 1 - col) : col;
                int sx = x + cx;
                if (sx < 0 || sx >= PIC_W || sy < 0 || sy >= PIC_H) continue;
                int idx = sy * PIC_W + sx;
                int sp = pribuf[idx];
                int visible;
                if (sp < 3) {
                    /* control line: use the first real priority below it */
                    int yy = sy, cp = 15;
                    visible = 1;
                    while (++yy < PIC_H) {
                        cp = pribuf[yy * PIC_W + sx];
                        if (cp > 2) { visible = cp <= pri; break; }
                    }
                } else visible = sp <= pri;
                if (!visible) continue;
                vis[idx] = (uint8_t)color;
                if (sp >= 3 || to_pic) {
                    if (sp >= 3) pribuf[idx] = (uint8_t)pri;
                }
            }
        }
    }
}

/* ---------------------------------------------------------------- loading */
int agi_load(Agi *e, const char *dir) {
    memset(e, 0, sizeof *e);
    snprintf(e->dir, sizeof e->dir, "%s", dir);
    e->mouse_x = e->mouse_y = -1;
    e->sound_mode = 3;
    e->nlog = load_dir(e, "LOGDIR", e->logdir);
    e->npic = load_dir(e, "PICDIR", e->picdir);
    e->nview = load_dir(e, "VIEWDIR", e->viewdir);
    e->nsnd = load_dir(e, "SNDDIR", e->snddir);
    if (e->nlog < 0 || e->npic < 0 || e->nview < 0) {
        snprintf(e->err, sizeof e->err,
                 "The game files (LOGDIR, PICDIR, VIEWDIR, VOL.*) were not found in\n%s", dir);
        return -1;
    }
    for (int i = 0; i < 16; i++) {
        char name[16];
        snprintf(name, sizeof name, "VOL.%d", i);
        e->vol[i] = read_file(dir, name, &e->vol_len[i]);
    }
    if (!e->vol[0]) {
        snprintf(e->err, sizeof e->err, "VOL.0 not found in %s", dir);
        return -1;
    }
    /* inventory objects (OBJECT, encrypted) */
    int olen;
    uint8_t *ob = read_file(dir, "OBJECT", &olen);
    if (ob) {
        for (int i = 0; i < olen; i++) ob[i] ^= (uint8_t)CRYPT_KEY[i % 11];
        int names = ob[0] | (ob[1] << 8);
        e->nobj = ob[2] ? ob[2] : 16;
        if (e->nobj > MAX_OBJ) e->nobj = MAX_OBJ;
        int n = names / 3;
        if (n > MAX_ITEMS) n = MAX_ITEMS;
        e->nitems = n;
        for (int i = 0; i < n; i++) {
            int so = 3 + i * 3;
            if (so + 2 >= olen) { e->nitems = i; break; }
            int off = (ob[so] | (ob[so + 1] << 8)) + 3;
            e->item_room[i] = ob[so + 2];
            e->item_name[i] = (off < olen) ? (char *)ob + off : (char *)"";
        }
        e->objfile = ob;
    } else {
        e->nobj = 16;
    }
    return 0;
}

void agi_free(Agi *e) {
    for (int i = 0; i < 16; i++) free(e->vol[i]);
    for (int i = 0; i < 256; i++) {
        free(e->logic[i].data);
        free(e->view[i].data);
    }
    free(e->objfile);
}

/* Length of an AGI v2 sound resource in milliseconds (longest voice). */
int sound_duration_ms(const uint8_t *d, int len) {
    long best = 0;
    for (int v = 0; v < 4; v++) {
        if (2 * v + 1 >= len) break;
        int off = d[2 * v] | (d[2 * v + 1] << 8);
        long ticks = 0;
        for (int p = off; p + 4 < len; p += 5) {
            int dur = d[p] | (d[p + 1] << 8);
            if (dur == 0xFFFF) break;
            ticks += dur;
        }
        if (ticks > best) best = ticks;
    }
    return (int)(best * 1000 / 60);
}
