/* Black Cauldron Enhanced - logic (bytecode) interpreter for AGI v2.
 * GPL-3.0-or-later. */
#include "agi.h"
#include "version.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* number of argument bytes per command (AGI 2.4xx) */
static const uint8_t CMD_ARGS[182] = {
    0,1,1,2,2,2,2,2,2,2, 2,2,1,1,1,1,1,1,1,1,   /*   0- 19 */
    1,1,1,1,1,1,0,1,1,0, 1,1,1,1,0,1,1,3,3,3,   /*  20- 39 */
    3,2,2,2,2,1,1,2,2,2, 2,2,2,2,2,2,1,2,1,1,   /*  40- 59 */
    1,1,1,1,1,1,1,1,1,3, 1,1,1,2,1,2,2,1,1,2,   /*  60- 79 */
    2,5,5,3,1,1,2,2,1,1, 4,0,1,1,1,2,2,2,1,2,   /*  80- 99 */
    0,1,1,3,3,3,0,0,1,2, 1,3,0,0,2,5,2,1,2,0,   /* 100-119 */
    0,3,7,7,0,0,0,0,0,1, 3,0,0,1,1,0,0,0,0,0,   /* 120-139 */
    0,0,1,1,1,0,0,3,3,0, 3,4,4,1,5,2,1,2,0,1,   /* 140-159 */
    1,0,1,0,0,2,2,2,2,0, 1,0,0,0,1,1,0,1,0,4,   /* 160-179 */
    2,0                                         /* 180-181 */
};
static const uint8_t TEST_ARGS[19] = {0,2,2,2,2,2,2,1,1,1,2,5,1,0,0,2,5,5,5};

static const char *msg_of(Agi *e, int lg, int n) {
    Logic *L = &e->logic[lg];
    if (n < 1 || n > L->nmsg || !L->msg[n]) return "";
    return L->msg[n];
}

static int in_box(ScreenObj *o, int x, int y, int x1, int y1, int x2, int y2) {
    (void)o;
    return x >= x1 && y >= y1 && x <= x2 && y <= y2;
}

static int compare_strings(const char *a, const char *b) {
    /* case-insensitive, ignoring spaces and punctuation */
    for (;;) {
        while (*a && (isspace((unsigned char)*a) || strchr(".,;:'!-", *a))) a++;
        while (*b && (isspace((unsigned char)*b) || strchr(".,;:'!-", *b))) b++;
        if (!*a || !*b) return !*a && !*b;
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++; b++;
    }
}

static int eval_test(Agi *e, int t, const uint8_t *p) {
    ScreenObj *o;
    switch (t) {
    case 0: return 0;
    case 1: return e->var[p[0]] == p[1];
    case 2: return e->var[p[0]] == e->var[p[1]];
    case 3: return e->var[p[0]] < p[1];
    case 4: return e->var[p[0]] < e->var[p[1]];
    case 5: return e->var[p[0]] > p[1];
    case 6: return e->var[p[0]] > e->var[p[1]];
    case 7: return e->flag[p[0]] != 0;
    case 8: return e->flag[e->var[p[0]]] != 0;
    case 9: return p[0] < e->nitems && e->item_room[p[0]] == 255;
    case 10: return p[0] < e->nitems && e->item_room[p[0]] == e->var[p[1]];
    case 11: o = &e->obj[p[0] % MAX_OBJ]; return in_box(o, o->x, o->y, p[1], p[2], p[3], p[4]);
    case 12: return e->ctrl[p[0]] != 0;
    case 13: {
        /* The original reads the keyboard directly, and scripts wait in
         * loops like "if (!have.key()) goto ...". So when no key is waiting,
         * ask the host (keyboard, mouse buttons, gamepad) and keep the
         * screen alive. */
        int k = e->key_pending;
        if (k) { e->key_pending = 0; e->haskey_waits = 0; return 1; }
        if (e->keyq_n) {
            k = e->keyq[0];
            memmove(e->keyq, e->keyq + 1, (size_t)(--e->keyq_n) * sizeof e->keyq[0]);
            e->haskey_waits = 0;
            return 1;
        }
        if (++e->haskey_waits > 1) {
            int was = e->modal;
            e->modal = 1;
            agi_frame(e);
            k = e->host->poll_key(e->host);
            if (e->host->quit_requested(e->host)) e->quit = 1;
            e->modal = was;
            if (k) { e->haskey_waits = 0; e->var[V_KEY] = (uint8_t)(k < 256 ? k : 0); return 1; }
            e->host->sleep(e->host, 10);
        }
        return 0;
    }
    case 14: return 0;   /* said: this game has no text parser */
    case 15: return compare_strings(e->str[p[0] % 24], e->str[p[1] % 24]);
    case 16: o = &e->obj[p[0] % MAX_OBJ];
        return in_box(o, o->x, o->y, p[1], p[2], p[3], p[4]) &&
               in_box(o, o->x + o->xsize - 1, o->y, p[1], p[2], p[3], p[4]);
    case 17: o = &e->obj[p[0] % MAX_OBJ];
        return in_box(o, o->x + o->xsize / 2, o->y, p[1], p[2], p[3], p[4]);
    case 18: o = &e->obj[p[0] % MAX_OBJ];
        return in_box(o, o->x + o->xsize - 1, o->y, p[1], p[2], p[3], p[4]);
    }
    return 0;
}

void agi_new_room(Agi *e, int n) {
    if (agi_log_hook) { char b[32]; snprintf(b, sizeof b, "%d", n); agi_log_hook(e, "room", b); }
    for (int i = 0; i < e->nobj; i++) {
        ScreenObj *o = &e->obj[i];
        o->flags &= ~(F_ANIMATED | F_DRAWN);
        o->flags |= F_UPDATE;
        o->step_time = o->step_count = 1;
        o->cycle_time = o->cycle_count = 1;
        o->step_size = 1;
    }
    /* unload all logics but 0 (resets their scan starts) */
    for (int i = 1; i < 256; i++) {
        if (e->logic[i].loaded) {
            free(e->logic[i].data);
            memset(&e->logic[i], 0, sizeof e->logic[i]);
        }
    }
    e->player_control = 1;
    e->block_on = 0;
    e->horizon = 36;
    e->var[V_PREV_ROOM] = e->var[V_ROOM];
    e->var[V_ROOM] = (uint8_t)n;
    e->var[V_OBJ_BORDER_NR] = 0;
    e->var[V_OBJ_BORDER] = 0;
    e->var[V_EGO_VIEW] = e->obj[0].view;
    agi_load_logic(e, n);
    ScreenObj *ego = &e->obj[0];
    switch (e->var[V_EGO_BORDER]) {
    case 1: ego->y = PIC_H - 1; break;
    case 2: ego->x = 0; break;
    case 3: ego->y = 36 + 1; break;
    case 4: ego->x = PIC_W - ego->xsize; break;
    }
    e->var[V_EGO_BORDER] = 0;
    e->flag[FL_NEW_ROOM] = 1;
    e->exit_all = 1;
    e->walk_active = 0;
    /* clear text from the play area */
    for (int r = 0; r < 25; r++) memset(e->text_used[r], 0, 40);
}

static void cmd_print(Agi *e, int lg, const char *raw, int row, int col, int width) {
    char buf[1024];
    text_format(e, lg, raw, buf, sizeof buf);
    text_print(e, buf, row, col, width, 1);
}

static void show_obj(Agi *e, int vn) {
    if (agi_load_view(e, vn) < 0) return;
    View *v = &e->view[vn];
    if (!v->nloops || !v->loop[0].ncels) return;
    const Cel *c = &v->loop[0].cel[0];
    /* draw the close-up view in the middle of the picture, then the text */
    static uint8_t savev[PIC_W * PIC_H], savep[PIC_W * PIC_H];
    memcpy(savev, e->pic_vis, sizeof savev);
    memcpy(savep, e->pic_pri, sizeof savep);
    int x = (PIC_W - c->w) / 2, y = PIC_H - 1;
    view_draw_cel(e, vn, 0, 0, x, y, 15, e->pic_vis, e->pic_pri, 1);
    text_print(e, v->desc ? v->desc : "", -1, -1, 0, 1);
    memcpy(e->pic_vis, savev, sizeof savev);
    memcpy(e->pic_pri, savep, sizeof savep);
}

/* Runs a logic. Returns 0 when all logics must stop (new room, quit). */
int agi_run_logic(Agi *e, int n) {
    if (!e->logic[n].loaded && agi_load_logic(e, n) < 0) return 1;
    if (++e->call_depth > 64) { e->call_depth--; return 1; }
    Logic *L = &e->logic[n];
    const uint8_t *code = L->code;
    int len = L->code_len;
    int ip = L->scan_start;
    int guard = 0;
    while (ip < len) {
        if (e->exit_all || e->quit) { e->call_depth--; return 0; }
        if (e->haskey_waits > 1) guard = 0;   /* waiting for the player */
        if (++guard > 200000) break;   /* runaway loop safety */
        int c = code[ip++];
        if (c == 0xFF) {
            int and_r = 1, or_mode = 0, or_r = 0, neg = 0;
            while (ip < len) {
                int t = code[ip++];
                if (t == 0xFF) break;
                if (t == 0xFC) {
                    if (!or_mode) { or_mode = 1; or_r = 0; }
                    else { or_mode = 0; and_r = and_r && or_r; }
                    continue;
                }
                if (t == 0xFD) { neg = !neg; continue; }
                int r;
                if (t == 14) {
                    int k = code[ip];
                    r = eval_test(e, 14, code + ip);
                    ip += 1 + 2 * k;
                } else if (t < 19) {
                    r = eval_test(e, t, code + ip);
                    ip += TEST_ARGS[t];
                } else r = 0;
                if (neg) { r = !r; neg = 0; }
                if (or_mode) or_r = or_r || r;
                else and_r = and_r && r;
            }
            int skip = code[ip] | (code[ip + 1] << 8);
            ip += 2;
            if (!and_r) ip += skip;
            continue;
        }
        if (c == 0xFE) {
            int16_t off = (int16_t)(code[ip] | (code[ip + 1] << 8));
            ip += 2 + off;
            continue;
        }
        if (c >= 182) break;
        const uint8_t *p = code + ip;
        ip += CMD_ARGS[c];
        uint8_t *v = e->var;
        ScreenObj *o = &e->obj[p[0] % MAX_OBJ];
        switch (c) {
        case 0: e->call_depth--; return 1;
        case 1: if (v[p[0]] < 255) v[p[0]]++; break;
        case 2: if (v[p[0]] > 0) v[p[0]]--; break;
        case 3: v[p[0]] = p[1]; break;
        case 4: v[p[0]] = v[p[1]]; break;
        case 5: v[p[0]] += p[1]; break;
        case 6: v[p[0]] += v[p[1]]; break;
        case 7: v[p[0]] -= p[1]; break;
        case 8: v[p[0]] -= v[p[1]]; break;
        case 9: v[v[p[0]]] = v[p[1]]; break;
        case 10: v[p[0]] = v[v[p[1]]]; break;
        case 11: v[v[p[0]]] = p[1]; break;
        case 12: e->flag[p[0]] = 1; break;
        case 13: e->flag[p[0]] = 0; break;
        case 14: e->flag[p[0]] = !e->flag[p[0]]; break;
        case 15: e->flag[v[p[0]]] = 1; break;
        case 16: e->flag[v[p[0]]] = 0; break;
        case 17: e->flag[v[p[0]]] = !e->flag[v[p[0]]]; break;
        case 18: agi_new_room(e, p[0]); e->call_depth--; return 0;
        case 19: agi_new_room(e, v[p[0]]); e->call_depth--; return 0;
        case 20: agi_load_logic(e, p[0]); break;
        case 21: agi_load_logic(e, v[p[0]]); break;
        case 22: case 23: {
            int ln = c == 22 ? p[0] : v[p[0]];
            if (!agi_run_logic(e, ln)) { e->call_depth--; return 0; }
            /* the called logic may have been unloaded/reloaded */
            code = L->code; len = L->code_len;
            break;
        }
        case 24: e->pic_loaded[v[p[0]]] = 1; break;
        case 25:
            pic_draw(e, v[p[0]], 1);
            e->pic_shown = 0;
            break;
        case 26:
            e->flag[FL_PRINT_MODE] = 0;
            text_close_window(e);
            memcpy(e->shown_vis, e->pic_vis, sizeof e->shown_vis);
            e->pic_shown = 1;
            for (int r = e->pic_row; r < e->pic_row + 21 && r < 25; r++) memset(e->text_used[r], 0, 40);
            break;
        case 27: break;
        case 28: pic_draw(e, v[p[0]], 0); break;
        case 29: break;
        case 30: agi_load_view(e, p[0]); break;
        case 31: agi_load_view(e, v[p[0]]); break;
        case 32: break;
        case 33:
            if (!(o->flags & F_ANIMATED)) {
                o->flags = F_ANIMATED | F_UPDATE | F_CYCLING;
                o->motion = MOTION_NORMAL;
                o->cycle = CYCLE_NORMAL;
                o->dir = 0;
            }
            break;
        case 34:
            for (int i = 0; i < e->nobj; i++) e->obj[i].flags &= ~(F_ANIMATED | F_DRAWN);
            break;
        case 35:
            if (!(o->flags & F_DRAWN)) {
                o->flags |= F_UPDATE;
                obj_set_loop(e, o, o->loop);
                obj_set_cel(e, o, o->cel);
                obj_fix_position(e, o);
                o->xprev = o->x; o->yprev = o->y;
                o->flags |= F_DRAWN;
            }
            break;
        case 36: o->flags &= ~F_DRAWN; break;
        case 37: o->x = o->xprev = p[1]; o->y = o->yprev = p[2]; break;
        case 38: o->x = o->xprev = v[p[1]]; o->y = o->yprev = v[p[2]]; break;
        case 39: v[p[1]] = (uint8_t)o->x; v[p[2]] = (uint8_t)o->y; break;
        case 40: {
            int dx = (int8_t)v[p[1]], dy = (int8_t)v[p[2]];
            o->flags |= F_UPDATE_POS;
            int nx = o->x + dx, ny = o->y + dy;
            o->x = (int16_t)(nx < 0 ? 0 : nx);
            o->y = (int16_t)(ny < 0 ? 0 : ny);
            obj_fix_position(e, o);
            break;
        }
        case 41: obj_set_view(e, o, p[1]); break;
        case 42: obj_set_view(e, o, v[p[1]]); break;
        case 43: obj_set_loop(e, o, p[1]); break;
        case 44: obj_set_loop(e, o, v[p[1]]); break;
        case 45: o->flags |= F_FIX_LOOP; break;
        case 46: o->flags &= ~F_FIX_LOOP; break;
        case 47: obj_set_cel(e, o, p[1]); o->flags &= ~F_DONT_UPDATE; break;
        case 48: obj_set_cel(e, o, v[p[1]]); o->flags &= ~F_DONT_UPDATE; break;
        case 49: v[p[1]] = (uint8_t)(o->ncels ? o->ncels - 1 : 0); break;
        case 50: v[p[1]] = o->cel; break;
        case 51: v[p[1]] = o->loop; break;
        case 52: v[p[1]] = o->view; break;
        case 53: v[p[1]] = o->nloops; break;
        case 54: o->priority = p[1]; o->flags |= F_FIXED_PRIORITY; break;
        case 55: o->priority = v[p[1]]; o->flags |= F_FIXED_PRIORITY; break;
        case 56: o->flags &= ~F_FIXED_PRIORITY; break;
        case 57: v[p[1]] = o->priority; break;
        case 58: o->flags &= ~F_UPDATE; break;
        case 59: o->flags |= F_UPDATE; break;
        case 60: break;
        case 61: o->flags |= F_IGNORE_HORIZON; break;
        case 62: o->flags &= ~F_IGNORE_HORIZON; break;
        case 63: e->horizon = p[0]; break;
        case 64: o->flags |= F_ON_WATER; break;
        case 65: o->flags |= F_ON_LAND; break;
        case 66: o->flags &= ~(F_ON_WATER | F_ON_LAND); break;
        case 67: o->flags |= F_IGNORE_OBJECTS; break;
        case 68: o->flags &= ~F_IGNORE_OBJECTS; break;
        case 69: {
            ScreenObj *o2 = &e->obj[p[1] % MAX_OBJ];
            int d = 255;
            if ((o->flags & F_DRAWN) && (o2->flags & F_DRAWN)) {
                d = abs((o->x + o->xsize / 2) - (o2->x + o2->xsize / 2)) + abs(o->y - o2->y);
                if (d > 254) d = 254;
            }
            v[p[2]] = (uint8_t)d;
            break;
        }
        case 70: o->flags &= ~F_CYCLING; break;
        case 71: o->flags |= F_CYCLING; break;
        case 72: o->cycle = CYCLE_NORMAL; o->flags |= F_CYCLING; break;
        case 73: o->cycle = CYCLE_END_LOOP; o->flags |= F_UPDATE | F_CYCLING;
                 o->loop_flag = p[1]; e->flag[p[1]] = 0; break;
        case 74: o->cycle = CYCLE_REVERSE; o->flags |= F_CYCLING; break;
        case 75: o->cycle = CYCLE_REV_LOOP; o->flags |= F_UPDATE | F_CYCLING;
                 o->loop_flag = p[1]; e->flag[p[1]] = 0; break;
        case 76: o->cycle_time = o->cycle_count = v[p[1]]; break;
        case 77:
            o->dir = 0; o->motion = MOTION_NORMAL;
            if (o->nr == 0) { v[V_EGO_DIR] = 0; e->player_control = 0; e->walk_active = 0; }
            break;
        case 78:
            o->motion = MOTION_NORMAL;
            if (o->nr == 0) { v[V_EGO_DIR] = 0; e->player_control = 1; }
            break;
        case 79: o->step_size = v[p[1]]; break;
        case 80: o->step_time = o->step_count = v[p[1]]; break;
        case 81: case 82: {
            int mx = c == 81 ? p[1] : v[p[1]];
            int my = c == 81 ? p[2] : v[p[2]];
            int st = c == 81 ? p[3] : v[p[3]];
            o->motion = MOTION_MOVE;
            o->move_x = (int16_t)mx; o->move_y = (int16_t)my;
            o->move_step = o->step_size;
            o->move_flag = p[4];
            if (st) o->step_size = (uint8_t)st;
            e->flag[p[4]] = 0;
            o->flags |= F_UPDATE;
            if (o->nr == 0) { e->player_control = 0; e->walk_active = 0; }
            obj_move_to(e, o);
            break;
        }
        case 83:
            o->motion = MOTION_FOLLOW;
            o->follow_step = p[1] <= o->step_size ? o->step_size : p[1];
            o->follow_flag = p[2];
            o->follow_count = 255;
            e->flag[p[2]] = 0;
            o->flags |= F_UPDATE;
            break;
        case 84:
            o->motion = MOTION_WANDER;
            o->flags |= F_UPDATE;
            if (o->nr == 0) { e->player_control = 0; e->walk_active = 0; }
            break;
        case 85: o->motion = MOTION_NORMAL; break;
        case 86: o->dir = v[p[1]]; break;
        case 87: v[p[1]] = o->dir; break;
        case 88: o->flags |= F_IGNORE_BLOCKS; break;
        case 89: o->flags &= ~F_IGNORE_BLOCKS; break;
        case 90: e->block_on = 1; e->bx1 = p[0]; e->by1 = p[1]; e->bx2 = p[2]; e->by2 = p[3]; break;
        case 91: e->block_on = 0; break;
        case 92: if (p[0] < e->nitems) e->item_room[p[0]] = 255; break;
        case 93: if (v[p[0]] < e->nitems) e->item_room[v[p[0]]] = 255; break;
        case 94: if (p[0] < e->nitems) e->item_room[p[0]] = 0; break;
        case 95: if (p[0] < e->nitems) e->item_room[p[0]] = v[p[1]]; break;
        case 96: if (v[p[0]] < e->nitems) e->item_room[v[p[0]]] = v[p[1]]; break;
        case 97: v[p[1]] = v[p[0]] < e->nitems ? e->item_room[v[p[0]]] : 0; break;
        case 98: break;
        case 99: {
            int len2;
            const uint8_t *d = agi_res(e, e->snddir[p[0]], &len2);
            e->flag[p[1]] = 0;
            if (e->sound_flag >= 0) e->flag[e->sound_flag] = 1;
            e->sound_flag = p[1];
            if (d && e->flag[FL_SOUND]) {
                if (e->host->sound_play) e->host->sound_play(e->host, d, len2);
                e->sound_end_ms = e->host->ticks(e->host) + (uint32_t)sound_duration_ms(d, len2);
            } else agi_sound_done(e);
            break;
        }
        case 100:
            if (e->host->sound_stop) e->host->sound_stop(e->host);
            agi_sound_done(e);
            break;
        case 101: cmd_print(e, n, msg_of(e, n, p[0]), -1, -1, 0); break;
        case 102: cmd_print(e, n, msg_of(e, n, v[p[0]]), -1, -1, 0); break;
        case 103: case 104: {
            int row = c == 103 ? p[0] : v[p[0]];
            int col = c == 103 ? p[1] : v[p[1]];
            int m = c == 103 ? p[2] : v[p[2]];
            char buf[1024];
            text_format(e, n, msg_of(e, n, m), buf, sizeof buf);
            text_display(e, row, col, buf);
            break;
        }
        case 105: text_clear_lines(e, p[0], p[1], p[2]); break;
        case 106:
            /* text screen: the whole screen in the current background colour */
            e->text_mode = 1;
            e->win_open = 0;
            for (int r = 0; r < 25; r++)
                for (int cc = 0; cc < 40; cc++) {
                    e->text_ch[r][cc] = ' ';
                    e->text_fg[r][cc] = (uint8_t)e->fg;
                    e->text_bg[r][cc] = (uint8_t)e->bg;
                    e->text_used[r][cc] = 1;
                }
            break;
        case 107: e->text_mode = 0; for (int r = 0; r < 25; r++) memset(e->text_used[r], 0, 40); break;
        case 108: e->cursor_char = (uint8_t)msg_of(e, n, p[0])[0]; break;
        case 109: e->fg = p[0]; e->bg = p[1]; break;
        case 110: text_shake(e, p[0]); break;
        case 111: e->pic_row = p[0]; e->input_row = p[1]; e->status_row = p[2]; break;
        case 112: e->status_on = 1; break;
        case 113: e->status_on = 0; break;
        case 114: snprintf(e->str[p[0] % 24], 41, "%s", msg_of(e, n, p[1])); break;
        case 115: {
            char pr[512];
            text_format(e, n, msg_of(e, n, p[1]), pr, sizeof pr);
            int row = p[2], col = p[3];
            if (row > 24) row = -1;
            text_get_string(e, pr, row, col, p[4], e->str[p[0] % 24]);
            break;
        }
        case 116: e->str[p[0] % 24][0] = 0; break;
        case 117: break;
        case 118: {
            char pr[512], ans[41];
            text_format(e, n, msg_of(e, n, p[0]), pr, sizeof pr);
            text_get_string(e, pr, -1, -1, 3, ans);
            v[p[1]] = (uint8_t)atoi(ans);
            break;
        }
        case 119: e->input_on = 0; break;
        case 120: e->input_on = 1; break;
        case 121: {
            int key = p[0] ? p[0] : (p[1] << 8);
            if (e->nkeymap < MAX_KEYMAP) {
                e->keymap[e->nkeymap].key = (uint16_t)key;
                e->keymap[e->nkeymap].ctrl = p[2];
                e->nkeymap++;
            }
            break;
        }
        case 122: case 123: {
            int a[7];
            for (int i = 0; i < 7; i++) a[i] = c == 122 ? p[i] : v[p[i]];
            obj_add_to_pic(e, a[0], a[1], a[2], a[3], a[4], a[5] & 15, a[6]);
            break;
        }
        case 124: text_inventory(e); break;
        case 125: agi_save_game(e); break;
        case 126:
            if (agi_restore_game(e)) { e->call_depth--; return 0; }
            break;
        case 127: break;
        case 128:
            if (text_confirm(e, "Press ENTER to restart\nthe game.\n\nPress ESC to continue\nthis game.")) {
                e->restart = 1; e->exit_all = 1; e->call_depth--; return 0;
            }
            break;
        case 129: show_obj(e, p[0]); break;
        case 130: {
            int lo = p[0], hi = p[1];
            if (hi < lo) { int t = lo; lo = hi; hi = t; }
            v[p[2]] = (uint8_t)(lo + agi_random(e, hi - lo + 1));
            break;
        }
        case 131: e->player_control = 0; e->walk_active = 0; break;
        case 132: e->player_control = 1; break;
        case 133: break;
        case 134:
            if (p[0] == 1 || text_confirm(e, "Press ENTER to quit.\nPress ESC to keep playing.")) {
                e->quit = 1; e->call_depth--; return 0;
            }
            break;
        case 135: break;
        case 136:
            text_print(e, "      Game paused.\nPress Enter to continue.", -1, -1, 0, 1);
            break;
        case 137: case 138: case 139: case 140: break;
        case 141:
            text_print(e, "Black Cauldron Enhanced " APP_VERSION "\n\nA new interpreter for\nSierra's AGI version 2.439", -1, -1, 0, 1);
            break;
        case 142: case 143: case 144: break;
        case 145: L->scan_start = ip; break;
        case 146: L->scan_start = 0; break;
        case 147: o->x = p[1]; o->y = p[2]; o->flags |= F_UPDATE_POS; obj_fix_position(e, o); break;
        case 148: o->x = v[p[1]]; o->y = v[p[2]]; o->flags |= F_UPDATE_POS; obj_fix_position(e, o); break;
        case 149: case 150: break;
        case 151: cmd_print(e, n, msg_of(e, n, p[0]), p[1], p[2], p[3]); break;
        case 152: cmd_print(e, n, msg_of(e, n, v[p[0]]), p[1], p[2], p[3]); break;
        case 153: break;
        case 154: text_clear_rect(e, p[0], p[1], p[2], p[3], p[4]); break;
        case 155: break;
        case 156:
            if (e->nmenu < MAX_MENU && !e->menu_submitted) {
                Menu *m = &e->menu[e->nmenu];
                snprintf(m->text, sizeof m->text, "%s", msg_of(e, n, p[0]));
                m->first = e->nmitem; m->count = 0;
                m->col = e->nmenu ? e->menu[e->nmenu - 1].col + (int)strlen(e->menu[e->nmenu - 1].text) + 1 : 1;
                e->nmenu++;
            }
            break;
        case 157:
            if (e->nmenu && e->nmitem < MAX_MENUITEM && !e->menu_submitted) {
                MenuItem *mi = &e->mitem[e->nmitem++];
                snprintf(mi->text, sizeof mi->text, "%s", msg_of(e, n, p[0]));
                mi->ctrl = p[1]; mi->enabled = 1; mi->menu = e->nmenu - 1;
                e->menu[e->nmenu - 1].count++;
            }
            break;
        case 158: e->menu_submitted = 1; break;
        case 159: case 160:
            for (int i = 0; i < e->nmitem; i++) if (e->mitem[i].ctrl == p[0]) e->mitem[i].enabled = c == 159;
            break;
        case 161: if (e->flag[FL_MENU]) { e->menu_request = 1; e->menu_start = 0; } break;
        case 162: show_obj(e, v[p[0]]); break;
        case 163: case 164: break;
        case 165: v[p[0]] = (uint8_t)(v[p[0]] * p[1]); break;
        case 166: v[p[0]] = (uint8_t)(v[p[0]] * v[p[1]]); break;
        case 167: if (p[1]) v[p[0]] = (uint8_t)(v[p[0]] / p[1]); break;
        case 168: if (v[p[1]]) v[p[0]] = (uint8_t)(v[p[0]] / v[p[1]]); break;
        case 169: text_close_window(e); break;
        default: break;
        }
    }
    e->call_depth--;
    return 1;
}
