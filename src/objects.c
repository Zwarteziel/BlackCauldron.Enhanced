/* Black Cauldron Enhanced - screen objects: views, motion, collision,
 * drawing. Follows the behaviour of the original AGI v2 interpreter.
 * GPL-3.0-or-later. */
#include "agi.h"
#include <stdlib.h>
#include <string.h>

static const int DX[9] = {0, 0, 1, 1, 1, 0, -1, -1, -1};
static const int DY[9] = {0, -1, -1, 0, 1, 1, 1, 0, -1};
static const int LOOP2[9] = {4, 4, 0, 0, 0, 4, 1, 1, 1};
static const int LOOP4[9] = {4, 3, 0, 0, 0, 2, 1, 1, 1};

void obj_reset(Agi *e, ScreenObj *o, int nr) {
    (void)e;
    memset(o, 0, sizeof *o);
    o->nr = nr;
    o->step_size = o->step_time = o->step_count = 1;
    o->cycle_time = o->cycle_count = 1;
    o->flags = 0;
}

static void clip_view_coords(Agi *e, ScreenObj *o) {
    if (o->x + o->xsize > PIC_W) { o->flags |= F_UPDATE_POS; o->x = PIC_W - o->xsize; }
    if (o->y - o->ysize < -1) { o->flags |= F_UPDATE_POS; o->y = o->ysize - 1; }
    if (o->y <= e->horizon && !(o->flags & F_IGNORE_HORIZON)) {
        o->flags |= F_UPDATE_POS; o->y = e->horizon + 1;
    }
}

void obj_set_cel(Agi *e, ScreenObj *o, int c) {
    View *v = &e->view[o->view];
    if (!v->loaded || o->loop >= v->nloops) return;
    Loop *l = &v->loop[o->loop];
    if (c >= l->ncels) c = l->ncels ? l->ncels - 1 : 0;
    o->cel = (uint8_t)c;
    if (l->ncels) {
        o->xsize = (int16_t)l->cel[c].w;
        o->ysize = (int16_t)l->cel[c].h;
    }
    clip_view_coords(e, o);
}

void obj_set_loop(Agi *e, ScreenObj *o, int ln) {
    View *v = &e->view[o->view];
    if (!v->loaded) return;
    if (ln >= v->nloops) ln = v->nloops ? v->nloops - 1 : 0;
    o->loop = (uint8_t)ln;
    o->ncels = (uint8_t)v->loop[ln].ncels;
    if (o->cel >= o->ncels) o->cel = 0;
    obj_set_cel(e, o, o->cel);
}

void obj_set_view(Agi *e, ScreenObj *o, int vn) {
    if (!e->view[vn].loaded) agi_load_view(e, vn);
    View *v = &e->view[vn];
    o->view = (uint8_t)vn;
    o->nloops = (uint8_t)v->nloops;
    obj_set_loop(e, o, o->loop >= o->nloops ? 0 : o->loop);
}

static int check_position(Agi *e, ScreenObj *o) {
    if (o->x < 0 || o->x + o->xsize > PIC_W || o->y - o->ysize < -1 || o->y >= PIC_H ||
        (!(o->flags & F_IGNORE_HORIZON) && o->y <= e->horizon))
        return 0;
    return 1;
}

static int check_collision(Agi *e, ScreenObj *o) {
    if (o->flags & F_IGNORE_OBJECTS) return 0;
    for (int i = 0; i < e->nobj; i++) {
        ScreenObj *c = &e->obj[i];
        if ((c->flags & (F_ANIMATED | F_DRAWN)) != (F_ANIMATED | F_DRAWN)) continue;
        if (c->flags & F_IGNORE_OBJECTS) continue;
        if (c->nr == o->nr) continue;
        if (o->x + o->xsize < c->x || o->x > c->x + c->xsize) continue;
        if (o->y == c->y) return 1;
        if ((o->y > c->y && o->yprev < c->yprev) || (o->y < c->y && o->yprev > c->yprev))
            return 1;
    }
    return 0;
}

static int check_priority(Agi *e, ScreenObj *o) {
    int water = 1, trigger = 0, ok = 1;
    if (!(o->flags & F_FIXED_PRIORITY)) o->priority = (uint8_t)pri_from_y(e, o->y);
    if (o->priority != 15) {
        int y = o->y;
        if (y < 0 || y >= PIC_H) { ok = 0; water = 0; }
        for (int i = 0; ok && i < o->xsize; i++) {
            int x = o->x + i;
            if (x < 0 || x >= PIC_W) continue;
            int p = e->pic_pri[y * PIC_W + x];
            if (p == 0) { ok = 0; break; }
            if (p == 3) continue;
            water = 0;
            if (p == 1) {
                if (o->flags & F_IGNORE_BLOCKS) continue;
                ok = 0; break;
            }
            if (p == 2) trigger = 1;
        }
        if (ok) {
            if (!water) { if (o->flags & F_ON_WATER) ok = 0; }
            else { if (o->flags & F_ON_LAND) ok = 0; }
        }
    } else {
        water = 0;
    }
    if (o->nr == 0) {
        e->flag[FL_EGO_TRIGGER] = (uint8_t)trigger;
        e->flag[FL_EGO_WATER] = (uint8_t)(water && ok);
    }
    return ok;
}

void obj_fix_position(Agi *e, ScreenObj *o) {
    if (o->y <= e->horizon && !(o->flags & F_IGNORE_HORIZON)) o->y = e->horizon + 1;
    int dir = 0, count = 1, size = 1, guard = 0;
    while ((!check_position(e, o) || check_collision(e, o) || !check_priority(e, o)) && guard++ < 40000) {
        switch (dir) {
        case 0: o->x--; if (--count) continue; dir = 1; break;
        case 1: o->y++; if (--count) continue; dir = 2; size++; break;
        case 2: o->x++; if (--count) continue; dir = 3; break;
        case 3: o->y--; if (--count) continue; dir = 0; size++; break;
        }
        count = size;
    }
}

static int check_step(int delta, int step) {
    return (-step >= delta) ? 0 : (step <= delta) ? 2 : 1;
}

static int get_direction(int ox, int oy, int dx, int dy, int step) {
    static const int tbl[9] = {8, 1, 2, 7, 0, 3, 6, 5, 4};
    return tbl[check_step(dx - ox, step) + 3 * check_step(dy - oy, step)];
}

static void move_stop(Agi *e, ScreenObj *o) {
    o->step_size = o->move_step;
    if (o->motion != MOTION_EGO) e->flag[o->move_flag] = 1;
    o->motion = MOTION_NORMAL;
    if (o->nr == 0) {
        e->player_control = 1;
        e->var[V_EGO_DIR] = 0;
    }
}

void obj_move_to(Agi *e, ScreenObj *o) {
    o->dir = (uint8_t)get_direction(o->x, o->y, o->move_x, o->move_y, o->step_size);
    if (o->nr == 0) e->var[V_EGO_DIR] = o->dir;
    if (o->dir == 0) move_stop(e, o);
}

static void motion_wander(Agi *e, ScreenObj *o) {
    uint8_t orig = o->wander_count;
    o->wander_count--;
    if (orig == 0 || (o->flags & F_DIDNT_MOVE)) {
        o->dir = (uint8_t)agi_random(e, 9);
        if (o->nr == 0) e->var[V_EGO_DIR] = o->dir;
        while (o->wander_count < 6) o->wander_count = (uint8_t)agi_random(e, 51);
    }
}

static void motion_follow(Agi *e, ScreenObj *o) {
    ScreenObj *ego = &e->obj[0];
    int ex = ego->x + ego->xsize / 2, ey = ego->y;
    int ox = o->x + o->xsize / 2, oy = o->y;
    int dir = get_direction(ox, oy, ex, ey, o->follow_step);
    if (dir == 0) {
        o->dir = 0;
        o->motion = MOTION_NORMAL;
        e->flag[o->follow_flag] = 1;
        return;
    }
    if (o->follow_count == 255) {
        o->follow_count = 0;
    } else if (o->flags & F_DIDNT_MOVE) {
        do { o->dir = (uint8_t)agi_random(e, 9); } while (o->dir == 0);
        int d = (abs(ey - oy) + abs(ex - ox)) / 2 + 1;
        if (d <= o->step_size) { o->follow_count = o->step_size; return; }
        do { o->follow_count = (uint8_t)agi_random(e, d + 1); } while (o->follow_count < o->step_size);
        return;
    }
    if (o->follow_count != 0) {
        int k = o->follow_count - o->step_size;
        o->follow_count = (uint8_t)(k < 0 ? 0 : k);
    } else {
        o->dir = (uint8_t)dir;
    }
}

static int check_block(Agi *e, int x, int y) {
    if (x <= e->bx1 || x >= e->bx2) return 0;
    if (y <= e->by1 || y >= e->by2) return 0;
    return 1;
}

static void change_pos(Agi *e, ScreenObj *o) {
    int inside = check_block(e, o->x, o->y);
    int x = o->x + o->step_size * DX[o->dir];
    int y = o->y + o->step_size * DY[o->dir];
    if (check_block(e, x, y) == inside) {
        o->flags &= ~F_MOTION;
    } else {
        o->flags |= F_MOTION;
        o->dir = 0;
        if (o->nr == 0) e->var[V_EGO_DIR] = 0;
    }
}

void obj_check_all_motions(Agi *e) {
    for (int i = 0; i < e->nobj; i++) {
        ScreenObj *o = &e->obj[i];
        if ((o->flags & (F_ANIMATED | F_UPDATE | F_DRAWN)) != (F_ANIMATED | F_UPDATE | F_DRAWN)) continue;
        if (o->step_count != 1) continue;
        switch (o->motion) {
        case MOTION_WANDER: motion_wander(e, o); break;
        case MOTION_FOLLOW: motion_follow(e, o); break;
        case MOTION_MOVE: case MOTION_EGO: obj_move_to(e, o); break;
        }
        if (e->block_on && !(o->flags & F_IGNORE_BLOCKS) && o->dir) change_pos(e, o);
    }
}

static void update_view_cycle(Agi *e, ScreenObj *o) {
    if (o->flags & F_DONT_UPDATE) { o->flags &= ~F_DONT_UPDATE; return; }
    int cel = o->cel, last = o->ncels - 1;
    switch (o->cycle) {
    case CYCLE_NORMAL:
        if (++cel > last) cel = 0;
        break;
    case CYCLE_END_LOOP:
        if (cel < last) { if (++cel != last) break; }
        e->flag[o->loop_flag] = 1;
        o->flags &= ~F_CYCLING;
        o->dir = 0;
        o->cycle = CYCLE_NORMAL;
        break;
    case CYCLE_REV_LOOP:
        if (cel) { if (--cel) break; }
        e->flag[o->loop_flag] = 1;
        o->flags &= ~F_CYCLING;
        o->dir = 0;
        o->cycle = CYCLE_NORMAL;
        break;
    case CYCLE_REVERSE:
        if (cel == 0) cel = last; else cel--;
        break;
    }
    obj_set_cel(e, o, cel < 0 ? 0 : cel);
}

static void update_positions(Agi *e) {
    e->var[V_OBJ_BORDER_NR] = 0;
    e->var[V_EGO_BORDER] = 0;
    e->var[V_OBJ_BORDER] = 0;
    for (int i = 0; i < e->nobj; i++) {
        ScreenObj *o = &e->obj[i];
        if ((o->flags & (F_ANIMATED | F_UPDATE | F_DRAWN)) != (F_ANIMATED | F_UPDATE | F_DRAWN)) continue;
        if (o->step_count != 0) { if (--o->step_count != 0) continue; }
        o->step_count = o->step_time;
        int x = o->x, y = o->y, ox = x, oy = y, border = 0;
        if (!(o->flags & F_UPDATE_POS)) {
            x += o->step_size * DX[o->dir];
            y += o->step_size * DY[o->dir];
        }
        if (x < 0) { x = 0; border = 4; }
        else if (x + o->xsize > PIC_W) { x = PIC_W - o->xsize; border = 2; }
        if (y - o->ysize < -1) { y = o->ysize - 1; border = 1; }
        else if (y > PIC_H - 1) { y = PIC_H - 1; border = 3; }
        else if (!(o->flags & F_IGNORE_HORIZON) && y <= e->horizon) { y = e->horizon + 1; border = 1; }
        o->x = (int16_t)x; o->y = (int16_t)y;
        if (check_collision(e, o) || !check_priority(e, o)) {
            o->x = (int16_t)ox; o->y = (int16_t)oy;
            border = 0;
            obj_fix_position(e, o);
        }
        if (border) {
            if (o->nr == 0) e->var[V_EGO_BORDER] = (uint8_t)border;
            else { e->var[V_OBJ_BORDER_NR] = (uint8_t)o->nr; e->var[V_OBJ_BORDER] = (uint8_t)border; }
            if (o->motion == MOTION_MOVE || o->motion == MOTION_EGO) move_stop(e, o);
        }
        o->flags &= ~F_UPDATE_POS;
    }
}

void obj_update_all(Agi *e) {
    int changed = 0;
    for (int i = 0; i < e->nobj; i++) {
        ScreenObj *o = &e->obj[i];
        if ((o->flags & (F_ANIMATED | F_UPDATE | F_DRAWN)) != (F_ANIMATED | F_UPDATE | F_DRAWN)) continue;
        changed++;
        int ln = 4;
        if (!(o->flags & F_FIX_LOOP)) {
            if (o->nloops == 2 || o->nloops == 3) ln = LOOP2[o->dir];
            else if (o->nloops == 4) ln = LOOP4[o->dir];
        }
        if (ln != 4 && ln != o->loop && o->step_count == 1) obj_set_loop(e, o, ln);
        if (o->flags & F_CYCLING) {
            if (o->cycle_count) {
                if (--o->cycle_count == 0) {
                    update_view_cycle(e, o);
                    o->cycle_count = o->cycle_time;
                }
            }
        }
    }
    if (changed) {
        update_positions(e);
        for (int i = 0; i < e->nobj; i++) {
            ScreenObj *o = &e->obj[i];
            if ((o->flags & (F_ANIMATED | F_UPDATE | F_DRAWN)) != (F_ANIMATED | F_UPDATE | F_DRAWN)) continue;
            if (o->x == o->xprev && o->y == o->yprev) o->flags |= F_DIDNT_MOVE;
            else { o->xprev = o->x; o->yprev = o->y; o->flags &= ~F_DIDNT_MOVE; }
        }
    }
}

/* Permanently add a cel to the picture, with an optional control box. */
void obj_add_to_pic(Agi *e, int vn, int ln, int cn, int x, int y, int pri, int margin) {
    if (!e->view[vn].loaded && agi_load_view(e, vn) < 0) return;
    View *v = &e->view[vn];
    if (ln >= v->nloops) ln = v->nloops - 1;
    if (ln < 0 || cn >= v->loop[ln].ncels) cn = 0;
    if (ln < 0 || v->loop[ln].ncels == 0) return;
    const Cel *c = &v->loop[ln].cel[cn];
    /* keep it on screen like the original does */
    if (x + c->w > PIC_W) x = PIC_W - c->w;
    if (y - c->h < -1) y = c->h - 1;
    if (y >= PIC_H) y = PIC_H - 1;
    if (x < 0) x = 0;
    if (pri == 0) pri = pri_from_y(e, y);
    view_draw_cel(e, vn, ln, cn, x, y, pri, e->pic_vis, e->pic_pri, 1);
    if (pri == 15 || margin > 3) {
        if (e->pic_shown) memcpy(e->shown_vis, e->pic_vis, sizeof e->shown_vis);
        return;
    }
    /* control box: bottom line and sides, up to where the priority band ends */
    int h = 1;
    int band = pri_from_y(e, y);
    while (h < c->h && y - h >= 0 && pri_from_y(e, y - h) == band) h++;
    for (int i = 0; i < c->w; i++) {
        int px = x + i;
        if (px < PIC_W && y >= 0 && y < PIC_H) e->pic_pri[y * PIC_W + px] = (uint8_t)margin;
    }
    for (int j = 1; j < h; j++) {
        int py = y - j;
        if (py < 0) break;
        e->pic_pri[py * PIC_W + x] = (uint8_t)margin;
        if (x + c->w - 1 < PIC_W) e->pic_pri[py * PIC_W + x + c->w - 1] = (uint8_t)margin;
    }
    if (e->pic_shown) memcpy(e->shown_vis, e->pic_vis, sizeof e->shown_vis);
}

static int sort_y(Agi *e, ScreenObj *o) {
    if (o->flags & F_FIXED_PRIORITY) {
        int p = o->priority;
        for (int y = 0; y < PIC_H; y++) if (e->pri_table[y] >= p) return y;
        return PIC_H;
    }
    return o->y;
}

/* Builds e->screen (indices) from the picture, objects and text layers. */
void compose_screen(Agi *e) {
    static uint8_t wvis[PIC_W * PIC_H], wpri[PIC_W * PIC_H];
    memcpy(wvis, e->pic_shown ? e->pic_vis : e->shown_vis, sizeof wvis);
    memcpy(wpri, e->pic_pri, sizeof wpri);
    if (e->pic_shown) {
        ScreenObj *list[MAX_OBJ];
        for (int pass = 0; pass < 2; pass++) {
            int n = 0;
            for (int i = 0; i < e->nobj; i++) {
                ScreenObj *o = &e->obj[i];
                if ((o->flags & (F_ANIMATED | F_DRAWN)) != (F_ANIMATED | F_DRAWN)) continue;
                int upd = (o->flags & F_UPDATE) != 0;
                if (upd != pass) continue;
                list[n++] = o;
            }
            /* insertion sort by y */
            for (int i = 1; i < n; i++) {
                ScreenObj *t = list[i];
                int k = sort_y(e, t), j = i - 1;
                while (j >= 0 && sort_y(e, list[j]) > k) { list[j + 1] = list[j]; j--; }
                list[j + 1] = t;
            }
            for (int i = 0; i < n; i++) {
                ScreenObj *o = list[i];
                int pri = o->priority;
                if (!(o->flags & F_FIXED_PRIORITY)) pri = pri_from_y(e, o->y);
                view_draw_cel(e, o->view, o->loop, o->cel, o->x, o->y, pri, wvis, wpri, 0);
            }
        }
    }
    memset(e->screen, 0, sizeof e->screen);
    if (!e->text_mode) {
        int oy = e->pic_row * 8;
        for (int y = 0; y < PIC_H; y++) {
            int sy = y + oy;
            if (sy < 0 || sy >= SCR_H) continue;
            uint8_t *d = e->screen + sy * SCR_W;
            const uint8_t *s = wvis + y * PIC_W;
            for (int x = 0; x < PIC_W; x++) d[2 * x] = d[2 * x + 1] = s[x];
        }
    }
    /* text cells (display, clear.lines, status line, input line) */
    for (int r = 0; r < 25; r++)
        for (int c = 0; c < 40; c++)
            if (e->text_used[r][c])
                text_draw_char(e->screen, c * 8, r * 8, e->text_ch[r][c], e->text_fg[r][c], e->text_bg[r][c]);
    if (e->status_on && !e->text_mode) text_status_line(e);
}
