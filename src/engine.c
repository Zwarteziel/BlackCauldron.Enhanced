/* Black Cauldron Enhanced - main cycle, input, modern controls, save and
 * restore. GPL-3.0-or-later. */
#include "agi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int agi_random(Agi *e, int n) {
    if (n <= 0) return 0;
    e->rng = e->rng * 1103515245u + 12345u;
    return (int)((e->rng >> 16) % (uint32_t)n);
}

void agi_sound_done(Agi *e) {
    if (e->sound_flag >= 0) e->flag[e->sound_flag] = 1;
    e->sound_flag = -1;
}

static void default_pri_table(Agi *e) {
    int y = 0;
    for (int p = 1; p < 15; p++)
        for (int s = 0; s < 12 && y < PIC_H; s++) e->pri_table[y++] = p < 4 ? 4 : p;
}

void agi_init_state(Agi *e) {
    memset(e->var, 0, sizeof e->var);
    memset(e->flag, 0, sizeof e->flag);
    memset(e->str, 0, sizeof e->str);
    memset(e->ctrl, 0, sizeof e->ctrl);
    for (int i = 0; i < MAX_OBJ; i++) obj_reset(e, &e->obj[i], i);
    for (int i = 0; i < e->nitems && e->objfile; i++) e->item_room[i] = e->objfile[3 + i * 3 + 2];
    for (int i = 0; i < 256; i++) {
        if (e->logic[i].loaded) { free(e->logic[i].data); memset(&e->logic[i], 0, sizeof e->logic[i]); }
    }
    memset(e->pic_vis, 15, sizeof e->pic_vis);
    memset(e->pic_pri, 4, sizeof e->pic_pri);
    memset(e->shown_vis, 0, sizeof e->shown_vis);
    memset(e->text_used, 0, sizeof e->text_used);
    e->pic_shown = 0;
    default_pri_table(e);
    e->horizon = 36;
    e->block_on = 0;
    e->player_control = 1;
    e->status_on = 0;
    e->input_on = 0;
    e->pic_row = 1; e->input_row = 22; e->status_row = 0;
    e->fg = 15; e->bg = 0;
    e->text_mode = 0;
    e->win_open = 0;
    e->nkeymap = 0;
    e->menu_request = 0;
    e->exit_all = 0;
    e->restart = 0;
    e->key_pending = 0;
    e->sound_flag = -1;
    e->walk_active = 0;
    e->ego_hold_dir = 0;
    e->keyq_n = 0;

    e->var[V_COMPUTER] = 0;          /* IBM PC */
    e->var[V_SOUNDGEN] = (uint8_t)(e->sound_mode == 1 ? 1 : 3);   /* PC speaker or PCjr/Tandy */
    e->var[V_MONITOR] = 3;           /* EGA */
    e->var[V_FREE_MEM] = 180;
    e->var[V_INPUT_LEN] = 38;
    e->var[V_VOLUME] = 15;
    e->flag[FL_SOUND] = 1;
    e->flag[FL_NEW_ROOM] = 1;
    e->flag[FL_LOGIC0_FIRST] = 1;
    agi_load_logic(e, 0);
}

void agi_key_event(Agi *e, int key) {
    if (e->keyq_n < 32) e->keyq[e->keyq_n++] = key;
}

static int key_dir(int key) {
    switch (key) {
    case KEY_UP: return 1;
    case KEY_PGUP: return 2;
    case KEY_RIGHT: return 3;
    case KEY_PGDN: return 4;
    case KEY_DOWN: return 5;
    case KEY_END: return 6;
    case KEY_LEFT: return 7;
    case KEY_HOME: return 8;
    case KEY_CENTER: return 9;
    }
    return 0;
}

static void cancel_walk(Agi *e) {
    if (!e->walk_active) return;
    ScreenObj *ego = &e->obj[0];
    if (ego->motion == MOTION_EGO) {
        ego->motion = MOTION_NORMAL;
        ego->step_size = ego->move_step;
    }
    e->walk_active = 0;
}

static void handle_key(Agi *e, int key) {
    for (int i = 0; i < e->nkeymap; i++) {
        if (e->keymap[i].key == key) { e->ctrl[e->keymap[i].ctrl] = 1; return; }
    }
    if (key == 26) { e->go_back_request = 1; return; }   /* Ctrl+Z (not used by the game) */
    if (key == KEY_MOUSE_L) {
        /* a click on the status bar opens the menus (as on the Amiga and ST) */
        int m = text_menu_bar_hit(e, e->mouse_x, e->mouse_y);
        if (m >= 0) { e->menu_request = 1; e->menu_start = m; return; }
        if (e->status_on && e->mouse_y >= e->status_row * 8 && e->mouse_y < e->status_row * 8 + 8) return;
        if (!e->player_control || !(e->obj[0].flags & F_DRAWN)) {
            /* Taran can't walk now (intro, cut scene): the click counts as
             * a key press, as on the versions with a mouse */
            e->key_pending = KEY_ENTER;
            e->var[V_KEY] = KEY_ENTER;
            return;
        }
        agi_walk_to(e, e->mouse_x, e->mouse_y, 0);
        return;
    }
    int d = key_dir(key);
    if (d) {
        if (e->player_control) {
            cancel_walk(e);
            if (d == 9) d = 0;
            if (e->obj[0].dir == d) e->var[V_EGO_DIR] = 0;
            else e->var[V_EGO_DIR] = (uint8_t)d;
        }
        return;
    }
    e->key_pending = key;
    e->var[V_KEY] = (uint8_t)(key < 256 ? key : 0);
}

/* Margins (in AGI pixels) in which a click means "walk out of the room". */
#define EXIT_MARGIN_X 5
#define EXIT_MARGIN_Y 4

void agi_walk_to(Agi *e, int sx, int sy, int then_key) {
    ScreenObj *ego = &e->obj[0];
    int px = sx / 2, py = sy - e->pic_row * 8;
    /* a plain walk click below the picture means: out at the bottom */
    if (!then_key && py >= PIC_H && py < PIC_H + 24) py = PIC_H - 1;
    if (!e->player_control || !(ego->flags & F_DRAWN) || py < 0 || py >= PIC_H) {
        if (then_key) agi_key_event(e, then_key);
        return;
    }
    int tx = px - ego->xsize / 2;
    if (tx < 0) tx = 0;
    if (tx + ego->xsize > PIC_W) tx = PIC_W - ego->xsize;
    int ty = py;
    int observe_horizon = !(ego->flags & F_IGNORE_HORIZON);
    if (ty <= e->horizon && observe_horizon) ty = e->horizon + 1;
    if (!then_key) {
        /* Rooms are left by touching an edge: the game only changes the room
         * when Taran tries to walk past it. So for a click near an edge (or
         * above the horizon), aim just beyond it; Taran then walks on until
         * he touches the edge, as with the arrow keys. */
        if (px < EXIT_MARGIN_X) tx = -1;
        else if (px >= PIC_W - EXIT_MARGIN_X) tx = PIC_W - ego->xsize + 1;
        if (py >= PIC_H - EXIT_MARGIN_Y) ty = PIC_H;
        else if (observe_horizon && py <= e->horizon) ty = e->horizon;
        else if (!observe_horizon && py < EXIT_MARGIN_Y + ego->ysize) ty = ego->ysize - 2;
    }
    if (ego->motion != MOTION_EGO) ego->move_step = ego->step_size;
    ego->motion = MOTION_EGO;
    ego->move_x = (int16_t)tx;
    ego->move_y = (int16_t)ty;
    ego->flags |= F_UPDATE;
    e->walk_active = 1;
    e->walk_stuck = 0;
    e->walk_then_key = then_key;
    e->walk_tx = tx; e->walk_ty = ty;
}

void agi_set_hold_dir(Agi *e, int dir) {
    if (dir == e->ego_hold_dir) return;
    e->ego_hold_dir = dir;
    if (!e->player_control) return;
    cancel_walk(e);
    e->var[V_EGO_DIR] = (uint8_t)dir;
}

static void walk_update(Agi *e) {
    if (!e->walk_active) return;
    ScreenObj *ego = &e->obj[0];
    int fire = 0;
    if (ego->motion != MOTION_EGO) {
        fire = 1;                      /* arrived (or touched an edge) */
    } else if (ego->flags & F_DIDNT_MOVE) {
        if (++e->walk_stuck > 3 * (ego->step_time ? ego->step_time : 1)) {
            ego->motion = MOTION_NORMAL;
            ego->step_size = ego->move_step;
            ego->dir = 0;
            e->var[V_EGO_DIR] = 0;
            fire = 1;
        }
    } else e->walk_stuck = 0;
    if (fire) {
        e->walk_active = 0;
        if (e->walk_then_key) handle_key(e, e->walk_then_key);
        e->walk_then_key = 0;
    }
}

/* ------------------------------------------------- right-click action menu */
static const char *ACT_NAME[4] = {"Look", "Do", "Use", "Select"};
static const int ACT_KEY[4] = {8, 6, 4, 3};    /* function keys of the original */
/* order of the rows in the menu: Select, Use, Do, Look (ids as above) */
static const int ACT_ORDER[4] = {3, 2, 1, 0};

int agi_action_menu(Agi *e, int sx, int sy, const int walk_first[4]) {
    const int rows = 4;
    /* BC keeps the object chosen with "New Object" in variable 42 */
    char labels[4][40];
    int it = e->var[42];
    for (int i = 0; i < 4; i++) {
        int id = ACT_ORDER[i];
        if (id == 2 && it > 0 && it < e->nitems && e->item_room[it] == 255)
            snprintf(labels[i], sizeof labels[i], "Use %.20s", e->item_name[it]);
        else snprintf(labels[i], sizeof labels[i], "%s", ACT_NAME[id]);
    }
    int w = 0;
    for (int i = 0; i < 4; i++) { int l = (int)strlen(labels[i]) + 4; if (l > w) w = l; }
    int bw = w * 8 + 16, bh = rows * 8 + 10;
    int bx = sx + 4, by = sy - 8;
    if (bx + bw > SCR_W) bx = sx - bw - 4;
    if (bx < 0) bx = 0;
    if (by + bh > SCR_H) by = SCR_H - bh;
    if (by < 0) by = 0;
    int sel = -1, ksel = 0, use_keys = 0;
    e->modal = 1;
    int result = -1;
    for (;;) {
        compose_screen(e);
        text_draw_box(e->screen, bx, by, bw, bh, 15, 4);
        int mx = e->mouse_x, my = e->mouse_y;
        sel = -1;
        if (!use_keys && mx >= bx && mx < bx + bw && my >= by + 5 && my < by + 5 + rows * 8)
            sel = (my - by - 5) / 8;
        if (use_keys) sel = ksel;
        for (int i = 0; i < rows; i++) {
            char line[48];
            snprintf(line, sizeof line, "%-*sF%d", w - 2, labels[i], ACT_KEY[ACT_ORDER[i]]);
            int inv = i == sel;
            int x = bx + 8, y = by + 5 + i * 8;
            for (int k = 0; line[k]; k++) text_draw_char(e->screen, x + k * 8, y, (uint8_t)line[k], inv ? 15 : 0, inv ? 0 : 15);
        }
        if (e->host->present) e->host->present(e->host);
        int k = e->host->poll_key(e->host);
        if (e->host->quit_requested(e->host)) { e->quit = 1; break; }
        if (!k) { e->host->sleep(e->host, 10); continue; }
        if (k == KEY_ESC || k == KEY_MOUSE_R) break;
        if (k == KEY_UP) { if (!use_keys) { use_keys = 1; ksel = sel < 0 ? 0 : sel; } ksel = (ksel + rows - 1) % rows; continue; }
        if (k == KEY_DOWN) { if (!use_keys) { use_keys = 1; ksel = sel < 0 ? 0 : sel; } ksel = (ksel + 1) % rows; continue; }
        if (k == KEY_MOUSE_L) { use_keys = 0; mx = e->mouse_x; my = e->mouse_y;
            if (mx >= bx && mx < bx + bw && my >= by + 5 && my < by + 5 + rows * 8) { result = (my - by - 5) / 8; }
            break; }
        if (k == KEY_ENTER) { result = sel >= 0 ? sel : -1; break; }
    }
    e->modal = 0;
    if (result >= 0) {
        result = ACT_ORDER[result];
        int key = KEY_F(ACT_KEY[result]);
        if (walk_first && walk_first[result]) agi_walk_to(e, sx, sy, key);
        else agi_key_event(e, key);
    }
    return result;
}

/* --------------------------------------------------------- save / restore */
#define SAVE_MAGIC "BCE-SAVE"
#define SAVE_VERSION 1
#define SAVE_SLOTS 12

static void slot_path(Agi *e, int slot, char *out, int n) {
    snprintf(out, n, "%s/bce-save-%02d.sav", e->dir, slot + 1);
}

static int slot_desc(Agi *e, int slot, char *desc) {
    char path[1024];
    slot_path(e, slot, path, sizeof path);
    desc[0] = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char hdr[8];
    int ok = fread(hdr, 1, 8, f) == 8 && !memcmp(hdr, SAVE_MAGIC, 8);
    int ver = 0;
    if (ok) ok = fread(&ver, 4, 1, f) == 1 && fread(desc, 1, 32, f) == 32;
    fclose(f);
    desc[31] = 0;
    if (!ok) desc[0] = 0;
    return ok;
}

/* Slot picker. Rows 0..11 are our own slots; when ndos > 0, saved games of
 * the original DOS version follow (return value 12 + i). */
static int pick_slot(Agi *e, const char *title, int need_existing, int ndos, char dosdesc[][32]) {
    char descs[SAVE_SLOTS][32];
    int exist[SAVE_SLOTS];
    for (int i = 0; i < SAVE_SLOTS; i++) exist[i] = slot_desc(e, i, descs[i]);
    int nrows = SAVE_SLOTS + (ndos ? ndos + 1 : 0);   /* +1: heading line */
    int sel = 0;
    e->modal = 1;
    const int bx = 4 * 8 - 8, by = 2 * 8 - 5, bw = 32 * 8 + 16, bh = (nrows + 3) * 8 + 10;
    for (;;) {
        compose_screen(e);
        text_draw_box(e->screen, bx, by, bw, bh, 15, 4);
        for (int k = 0; title[k]; k++) text_draw_char(e->screen, (4 + k) * 8, 2 * 8, (uint8_t)title[k], 0, 15);
        for (int r = 0; r < nrows; r++) {
            char line[40];
            int item = r;                         /* item index, -1 = heading */
            if (r < SAVE_SLOTS) snprintf(line, sizeof line, "%2d. %-27s", r + 1, exist[r] ? descs[r] : "-");
            else if (r == SAVE_SLOTS) { snprintf(line, sizeof line, "Other saved games:"); item = -1; }
            else { item = r - 1; snprintf(line, sizeof line, "    %-27s", dosdesc[r - SAVE_SLOTS - 1]); }
            int inv = item >= 0 && item == sel;
            for (int k = 0; line[k] && k < 31; k++)
                text_draw_char(e->screen, (4 + k) * 8, (4 + r) * 8, (uint8_t)line[k], inv ? 15 : 0, inv ? 0 : 15);
        }
        int nitems = SAVE_SLOTS + ndos;
        if (e->host->present) e->host->present(e->host);
        int k = e->host->poll_key(e->host);
        if (e->host->quit_requested(e->host)) { e->quit = 1; break; }
        if (!k) { e->host->sleep(e->host, 10); continue; }
        if (k == KEY_MOUSE_L) {
            int r = e->mouse_y / 8 - 4;
            if (r >= 0 && r < nrows && r != SAVE_SLOTS && e->mouse_x >= bx && e->mouse_x < bx + bw) {
                sel = r < SAVE_SLOTS ? r : r - 1; k = KEY_ENTER;
            } else continue;
        }
        if (k == KEY_ESC || k == KEY_MOUSE_R) break;
        if (k == KEY_UP) sel = (sel + nitems - 1) % nitems;
        if (k == KEY_DOWN) sel = (sel + 1) % nitems;
        if (k == KEY_ENTER) {
            if (need_existing && sel < SAVE_SLOTS && !exist[sel]) continue;
            e->modal = 0;
            return sel;
        }
    }
    e->modal = 0;
    return -1;
}

/* ---------------------------------------------- original DOS saved games */
#define MAX_DOS_SAVES 6

static void dos_save_path(Agi *e, int n, char *out, int len) {
    snprintf(out, len, "%s/BCSG.%d", e->dir, n);
    FILE *f = fopen(out, "rb");
    if (f) { fclose(f); return; }
    snprintf(out, len, "%s/bcsg.%d", e->dir, n);
}

/* Finds BCSG.1 .. BCSG.12 and reads their descriptions. */
static int find_dos_saves(Agi *e, int nums[], char desc[][32]) {
    int n = 0;
    for (int i = 1; i <= 12 && n < MAX_DOS_SAVES; i++) {
        char path[1100];
        dos_save_path(e, i, path, sizeof path);
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        char d[32] = {0};
        if (fread(d, 1, 31, f) == 31) {
            d[31] = 0;
            for (int k = 0; d[k]; k++) if ((unsigned char)d[k] < 32 || (unsigned char)d[k] > 126) d[k] = ' ';
            snprintf(desc[n], 32, "%s", d[0] ? d : "(no description)");
            nums[n++] = i;
        }
        fclose(f);
    }
    return n;
}

static int rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }

/* Loads a saved game of the original interpreter (AGI 2.4xx format):
 * a 31-byte description, then five blocks with a 16-bit length each:
 * game state, animated objects (43 bytes each), inventory, script buffer
 * (the commands that built the current picture) and logic scan starts. */
static int load_dos_save(Agi *e, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    static uint8_t buf[65536];
    int len = (int)fread(buf, 1, sizeof buf, f);
    fclose(f);
    const uint8_t *blk[5]; int blen[5];
    int p = 31;
    for (int i = 0; i < 5; i++) {
        if (p + 2 > len) return 0;
        blen[i] = rd16(buf + p);
        blk[i] = buf + p + 2;
        p += 2 + blen[i];
        if (p > len) return 0;
    }
    const uint8_t *st = blk[0];
    if (blen[0] < 330 || blen[1] % 43) return 0;

    /* game state: 7-byte game id, 256 variables, 256 flags (bit 7 first) */
    uint8_t keep20 = e->var[V_COMPUTER], keep22 = e->var[V_SOUNDGEN], keep26 = e->var[V_MONITOR];
    memcpy(e->var, st + 7, 256);
    for (int i = 0; i < 256; i++) e->flag[i] = (st[263 + i / 8] >> (7 - i % 8)) & 1;
    e->horizon = rd16(st + 299);
    e->block_on = rd16(st + 301) != 0;
    e->bx1 = rd16(st + 303); e->by1 = rd16(st + 305); e->bx2 = rd16(st + 307); e->by2 = rd16(st + 309);
    e->player_control = rd16(st + 311) != 0;
    /* this interpreter's machine (the save may come from a Tandy) */
    e->var[V_COMPUTER] = keep20; e->var[V_SOUNDGEN] = keep22; e->var[V_MONITOR] = keep26;

    /* animated objects */
    int nrec = blen[1] / 43;
    if (nrec > MAX_OBJ) nrec = MAX_OBJ;
    if (nrec > e->nobj) e->nobj = nrec;
    for (int i = 0; i < nrec; i++) {
        const uint8_t *r = blk[1] + i * 43;
        ScreenObj *o = &e->obj[i];
        obj_reset(e, o, i);
        o->step_count = r[0]; o->step_time = r[1];
        o->x = (int16_t)rd16(r + 3); o->y = (int16_t)rd16(r + 5);
        o->view = r[7]; o->loop = r[10]; o->nloops = r[11]; o->cel = r[14]; o->ncels = r[15];
        o->xprev = (int16_t)rd16(r + 22); o->yprev = (int16_t)rd16(r + 24);
        o->xsize = (int16_t)rd16(r + 26); o->ysize = (int16_t)rd16(r + 28);
        o->step_size = r[30]; o->cycle_time = r[31]; o->cycle_count = r[32];
        o->dir = r[33]; o->motion = r[34]; o->cycle = r[35]; o->priority = r[36];
        o->flags = (uint16_t)rd16(r + 37);
        const uint8_t *pm = r + 39;
        switch (o->motion) {
        case MOTION_WANDER: o->wander_count = pm[0]; break;
        case MOTION_FOLLOW: o->follow_step = pm[0]; o->follow_flag = pm[1]; o->follow_count = pm[2]; break;
        case MOTION_MOVE: o->move_x = pm[0]; o->move_y = pm[1]; o->move_step = pm[2]; o->move_flag = pm[3]; break;
        }
        if (o->cycle == CYCLE_END_LOOP || o->cycle == CYCLE_REV_LOOP) o->loop_flag = pm[3];
        if (o->flags & (F_ANIMATED | F_DRAWN)) {
            agi_load_view(e, o->view);
            View *v = &e->view[o->view];
            if (v->loaded && o->loop < v->nloops && o->cel < v->loop[o->loop].ncels) {
                o->xsize = (int16_t)v->loop[o->loop].cel[o->cel].w;
                o->ysize = (int16_t)v->loop[o->loop].cel[o->cel].h;
            }
        }
    }

    /* inventory: 3 bytes per object (name offset, room) */
    for (int i = 0; i < e->nitems && i * 3 + 2 < blen[2]; i++) e->item_room[i] = blk[2][i * 3 + 2];

    /* unload all logics but 0; the scan start table says which were loaded */
    for (int i = 1; i < 256; i++)
        if (e->logic[i].loaded) { free(e->logic[i].data); memset(&e->logic[i], 0, sizeof e->logic[i]); }

    /* script buffer: rebuild the picture as the original did */
    memset(e->pic_vis, 15, sizeof e->pic_vis);
    memset(e->pic_pri, 4, sizeof e->pic_pri);
    const uint8_t *sb = blk[3];
    int nent = rd16(st + 321);
    if (nent * 2 > blen[3]) nent = blen[3] / 2;
    for (int i = 0; i < nent; i++) {
        int type = sb[i * 2], num = sb[i * 2 + 1];
        switch (type) {
        case 0: agi_load_logic(e, num); break;
        case 1: agi_load_view(e, num); break;
        case 4: pic_draw(e, num, 1); break;
        case 8: pic_draw(e, num, 0); break;
        case 5:
            if (i + 3 < nent) {
                const uint8_t *a = sb + (i + 1) * 2;
                obj_add_to_pic(e, a[0], a[1], a[2], a[3], a[4], a[5] & 15, a[5] >> 4);
                i += 3;
            }
            break;
        default: break;   /* load.pic, load.sound, discard.*: nothing to do */
        }
    }
    memcpy(e->shown_vis, e->pic_vis, sizeof e->shown_vis);
    e->pic_shown = 1;

    /* logic scan starts: (logic, offset) pairs, 0xFFFF ends */
    for (int i = 0; i + 3 < blen[4]; i += 4) {
        int ln = rd16(blk[4] + i), off = rd16(blk[4] + i + 2);
        if (ln == 0xFFFF) break;
        if (ln > 255) continue;
        agi_load_logic(e, ln);
        if (e->logic[ln].loaded && off < e->logic[ln].code_len) e->logic[ln].scan_start = off;
    }
    memset(e->text_used, 0, sizeof e->text_used);
    return 1;
}

typedef struct {
    uint8_t var[256], flag[256];
    char str[24][41];
    uint8_t item_room[MAX_ITEMS];
    ScreenObj obj[MAX_OBJ];
    uint8_t pic_vis[PIC_W * PIC_H], pic_pri[PIC_W * PIC_H], shown_vis[PIC_W * PIC_H];
    int32_t pic_shown, horizon, block_on, bx1, by1, bx2, by2, player_control;
    int32_t status_on, input_on, pic_row, input_row, status_row, fg, bg, text_mode;
    uint8_t text_ch[25][40], text_fg[25][40], text_bg[25][40], text_used[25][40];
    KeyMap keymap[MAX_KEYMAP]; int32_t nkeymap;
    uint8_t item_enabled[MAX_MENUITEM];
    int32_t scan_start[256];
    uint8_t logic_loaded[256];
} SaveState;

/* Our own menu item (File > Go Back) is not part of the game's menu, so it
 * is left out of the saved enable states: saved games stay compatible. */
static void state_capture(Agi *e, SaveState *s) {
    memset(s, 0, sizeof *s);
    memcpy(s->var, e->var, 256); memcpy(s->flag, e->flag, 256);
    memcpy(s->str, e->str, sizeof s->str);
    memcpy(s->item_room, e->item_room, sizeof s->item_room);
    memcpy(s->obj, e->obj, sizeof s->obj);
    memcpy(s->pic_vis, e->pic_vis, sizeof s->pic_vis);
    memcpy(s->pic_pri, e->pic_pri, sizeof s->pic_pri);
    memcpy(s->shown_vis, e->shown_vis, sizeof s->shown_vis);
    s->pic_shown = e->pic_shown; s->horizon = e->horizon; s->block_on = e->block_on;
    s->bx1 = e->bx1; s->by1 = e->by1; s->bx2 = e->bx2; s->by2 = e->by2;
    s->player_control = e->player_control; s->status_on = e->status_on; s->input_on = e->input_on;
    s->pic_row = e->pic_row; s->input_row = e->input_row; s->status_row = e->status_row;
    s->fg = e->fg; s->bg = e->bg; s->text_mode = e->text_mode;
    memcpy(s->text_ch, e->text_ch, sizeof s->text_ch); memcpy(s->text_fg, e->text_fg, sizeof s->text_fg);
    memcpy(s->text_bg, e->text_bg, sizeof s->text_bg); memcpy(s->text_used, e->text_used, sizeof s->text_used);
    memcpy(s->keymap, e->keymap, sizeof s->keymap); s->nkeymap = e->nkeymap;
    for (int i = 0, k = 0; i < e->nmitem; i++)
        if (e->mitem[i].ctrl != CTRL_GO_BACK) s->item_enabled[k++] = (uint8_t)e->mitem[i].enabled;
    for (int i = 0; i < 256; i++) { s->logic_loaded[i] = (uint8_t)e->logic[i].loaded; s->scan_start[i] = e->logic[i].scan_start; }
}

static void state_apply(Agi *e, const SaveState *s) {
    memcpy(e->var, s->var, 256); memcpy(e->flag, s->flag, 256);
    memcpy(e->str, s->str, sizeof s->str);
    memcpy(e->item_room, s->item_room, sizeof s->item_room);
    memcpy(e->obj, s->obj, sizeof s->obj);
    memcpy(e->pic_vis, s->pic_vis, sizeof s->pic_vis);
    memcpy(e->pic_pri, s->pic_pri, sizeof s->pic_pri);
    memcpy(e->shown_vis, s->shown_vis, sizeof s->shown_vis);
    e->pic_shown = s->pic_shown; e->horizon = s->horizon; e->block_on = s->block_on;
    e->bx1 = s->bx1; e->by1 = s->by1; e->bx2 = s->bx2; e->by2 = s->by2;
    e->player_control = s->player_control; e->status_on = s->status_on; e->input_on = s->input_on;
    e->pic_row = s->pic_row; e->input_row = s->input_row; e->status_row = s->status_row;
    e->fg = s->fg; e->bg = s->bg; e->text_mode = s->text_mode;
    memcpy(e->text_ch, s->text_ch, sizeof s->text_ch); memcpy(e->text_fg, s->text_fg, sizeof s->text_fg);
    memcpy(e->text_bg, s->text_bg, sizeof s->text_bg); memcpy(e->text_used, s->text_used, sizeof s->text_used);
    memcpy(e->keymap, s->keymap, sizeof s->keymap); e->nkeymap = s->nkeymap;
    for (int i = 0, k = 0; i < e->nmitem; i++)
        if (e->mitem[i].ctrl != CTRL_GO_BACK) e->mitem[i].enabled = s->item_enabled[k++];
    for (int i = 1; i < 256; i++) {
        if (e->logic[i].loaded) { free(e->logic[i].data); memset(&e->logic[i], 0, sizeof e->logic[i]); }
    }
    for (int i = 0; i < 256; i++) {
        if (s->logic_loaded[i]) { agi_load_logic(e, i); e->logic[i].scan_start = s->scan_start[i]; }
    }
    for (int i = 0; i < MAX_OBJ; i++)
        if (e->obj[i].flags & (F_ANIMATED | F_DRAWN)) agi_load_view(e, e->obj[i].view);
    /* continue as after a restore */
    e->walk_active = 0;
    e->win_open = 0;
    e->sound_flag = -1;
    if (e->host->sound_stop) e->host->sound_stop(e->host);
    e->flag[FL_RESTORED] = 1;
}

static int state_write(const char *path, const char *desc, const SaveState *s) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    int ver = SAVE_VERSION;
    char d32[32] = {0};
    snprintf(d32, sizeof d32, "%s", desc);
    int ok = fwrite(SAVE_MAGIC, 1, 8, f) == 8 && fwrite(&ver, 4, 1, f) == 1 &&
             fwrite(d32, 1, 32, f) == 32 && fwrite(s, sizeof *s, 1, f) == 1;
    return (fclose(f) == 0) && ok;
}

static int state_read(const char *path, SaveState *s) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char hdr[8], d32[32];
    int ver = 0;
    int ok = fread(hdr, 1, 8, f) == 8 && !memcmp(hdr, SAVE_MAGIC, 8) &&
             fread(&ver, 4, 1, f) == 1 && ver == SAVE_VERSION &&
             fread(d32, 1, 32, f) == 32 && fread(s, sizeof *s, 1, f) == 1;
    fclose(f);
    return ok;
}

static void autosave_path(Agi *e, char *out, int n) {
    snprintf(out, n, "%s/bce-autosave.sav", e->dir);
}

void agi_save_game(Agi *e) {
    int slot = pick_slot(e, "Save the game in which slot?", 0, 0, NULL);
    if (slot < 0) return;
    char desc[41] = {0};
    char old[32];
    slot_desc(e, slot, old);
    if (!text_get_string(e, "How would you like to describe this saved game?", -1, -1, 30, desc)) return;
    if (!desc[0]) snprintf(desc, sizeof desc, "%s", old[0] ? old : "Saved game");
    SaveState *s = malloc(sizeof *s);
    state_capture(e, s);
    char path[1024];
    slot_path(e, slot, path, sizeof path);
    int ok = state_write(path, desc, s);
    free(s);
    if (!ok) text_print(e, "The game could not be saved.", -1, -1, 0, 1);
}

int agi_restore_game(Agi *e) {
    /* other saved games: the autosave, then saves of the original version */
    int dosnum[MAX_DOS_SAVES];
    char dosdesc[MAX_DOS_SAVES][32];
    int ndos = find_dos_saves(e, dosnum, dosdesc);
    char extra[MAX_DOS_SAVES + 1][32];
    int kind[MAX_DOS_SAVES + 1], nextra = 0;   /* kind: -1 autosave, else DOS index */
    char apath[1100];
    autosave_path(e, apath, sizeof apath);
    FILE *af = fopen(apath, "rb");
    if (af) { fclose(af); snprintf(extra[nextra], 32, "Autosave: last room entered"); kind[nextra++] = -1; }
    for (int i = 0; i < ndos; i++) { snprintf(extra[nextra], 32, "DOS: %.26s", dosdesc[i]); kind[nextra++] = i; }
    int slot = pick_slot(e, "Restore which saved game?", 1, nextra, extra);
    if (slot < 0) return 0;
    if (slot >= SAVE_SLOTS && kind[slot - SAVE_SLOTS] >= 0) {
        char dpath[1100];
        dos_save_path(e, dosnum[kind[slot - SAVE_SLOTS]], dpath, sizeof dpath);
        if (!load_dos_save(e, dpath)) {
            text_print(e, "This saved game cannot be read.", -1, -1, 0, 1);
            return 0;
        }
        e->walk_active = 0;
        e->win_open = 0;
        e->sound_flag = -1;
        if (e->host->sound_stop) e->host->sound_stop(e->host);
        e->flag[FL_RESTORED] = 1;
        e->exit_all = 1;
        return 1;
    }
    char path[1100];
    if (slot >= SAVE_SLOTS) snprintf(path, sizeof path, "%s", apath);
    else slot_path(e, slot, path, sizeof path);
    SaveState *s = malloc(sizeof *s);
    if (!state_read(path, s)) { free(s); text_print(e, "This saved game cannot be read.", -1, -1, 0, 1); return 0; }
    state_apply(e, s);
    free(s);
    e->exit_all = 1;
    return 1;
}

/* ------------------------------------------- go back: room entry snapshots */
/* Every time Taran enters a room, the state after the room's first cycle is
 * kept (the last GO_BACK_MAX rooms) and written to bce-autosave.sav. After a
 * fatal mistake, or with File > Go Back (Ctrl+Z), the player can go back to
 * one of these moments. Nothing of the game logic changes. */

static void go_back_clear(Agi *e) {
    for (int i = 0; i < e->go_back_n; i++) free(e->go_back[i]);
    e->go_back_n = 0;
}

static void go_back_push(Agi *e, SaveState *s) {
    if (e->go_back_n == GO_BACK_MAX) { free(e->go_back[GO_BACK_MAX - 1]); e->go_back_n--; }
    memmove(e->go_back + 1, e->go_back, (size_t)e->go_back_n * sizeof e->go_back[0]);
    e->go_back[0] = s;               /* newest first */
    e->go_back_n++;
}

/* The Black Cauldron: variable 102 is 13 once the game is lost. */
#define BC_GAME_OVER(e) ((e)->var[102] == 13)

static void go_back_capture(Agi *e) {
    if (e->var[V_ROOM] == 0 || !(e->obj[0].flags & F_DRAWN) || BC_GAME_OVER(e) || e->text_mode) return;
    SaveState *s = malloc(sizeof *s);
    if (!s) return;
    state_capture(e, s);
    go_back_push(e, s);
    char path[1100];
    autosave_path(e, path, sizeof path);
    state_write(path, "Last room entered", s);
}

/* Show what a snapshot looked like: swap the display state in and out. */
static void swap_display(Agi *e, SaveState *s) {
    static uint8_t tmp[PIC_W * PIC_H];
    static ScreenObj tobj[MAX_OBJ];
#define SWAPA(a, b) do { memcpy(tmp, a, sizeof a); memcpy(a, b, sizeof a); memcpy(b, tmp, sizeof a); } while (0)
    SWAPA(e->pic_vis, s->pic_vis);
    SWAPA(e->pic_pri, s->pic_pri);
    SWAPA(e->shown_vis, s->shown_vis);
    memcpy(tobj, e->obj, sizeof tobj); memcpy(e->obj, s->obj, sizeof tobj); memcpy(s->obj, tobj, sizeof tobj);
    SWAPA(e->var, s->var);
    SWAPA(e->flag, s->flag);
    SWAPA(e->text_ch, s->text_ch); SWAPA(e->text_fg, s->text_fg);
    SWAPA(e->text_bg, s->text_bg); SWAPA(e->text_used, s->text_used);
#undef SWAPA
    int32_t t;
    t = e->pic_shown; e->pic_shown = s->pic_shown; s->pic_shown = t;
    t = e->status_on; e->status_on = s->status_on; s->status_on = t;
    t = e->text_mode; e->text_mode = s->text_mode; s->text_mode = t;
}

static void draw_lines_box(Agi *e, const char **lines, int n, int row, int *bx, int *by, int *bw, int *bh) {
    int w = 0;
    for (int i = 0; i < n; i++) { int l = (int)strlen(lines[i]); if (l > w) w = l; }
    int col = (40 - w) / 2;
    *bx = col * 8 - 8; *by = row * 8 - 5; *bw = w * 8 + 16; *bh = n * 8 + 10;
    text_draw_box(e->screen, *bx, *by, *bw, *bh, 15, 4);
    for (int i = 0; i < n; i++) {
        int c = col + (w - (int)strlen(lines[i])) / 2;
        for (int k = 0; lines[i][k]; k++) text_draw_char(e->screen, (c + k) * 8, (row + i) * 8, (uint8_t)lines[i][k], 0, 15);
    }
}

/* Browse the snapshots (newest first) with the picture of each room.
 * Returns 1 when the player went back. */
static int go_back_pick(Agi *e, const char *title, const char *esc_text) {
    if (!e->go_back_n) {
        text_print(e, "There is no earlier room to go back to yet.", -1, -1, 0, 1);
        return 0;
    }
    /* the clickable buttons and their columns within the line */
    static const char buttons[] = "[<older]   [ENTER]   [newer>]";
    static const int bstart[3] = {0, 11, 21}, bend[3] = {8, 18, 29};
    static const int bkey[3] = {KEY_LEFT, KEY_ENTER, KEY_RIGHT};
    const int top = 19;
    int sel = 0, result = -1;
    e->modal = 1;
    for (;;) {
        SaveState *s = (SaveState *)e->go_back[sel];
        for (int i = 0; i < MAX_OBJ; i++)
            if (s->obj[i].flags & (F_ANIMATED | F_DRAWN)) agi_load_view(e, s->obj[i].view);
        swap_display(e, s);
        compose_screen(e);
        swap_display(e, s);
        char l2[48];
        snprintf(l2, sizeof l2, "%s  (%d of %d)", sel == 0 ? "The room you entered last" :
                 sel == 1 ? "One room earlier" : "Earlier room", sel + 1, e->go_back_n);
        const char *lines[4] = {title, l2, buttons, esc_text};
        int bx, by, bw, bh;
        draw_lines_box(e, lines, 4, top, &bx, &by, &bw, &bh);
        /* button under the mouse: shown inverted */
        int w = 0;
        for (int i = 0; i < 4; i++) { int l = (int)strlen(lines[i]); if (l > w) w = l; }
        int bcol = (40 - w) / 2 + (w - (int)strlen(buttons)) / 2;
        int hover = -1;
        if (e->mouse_y >= (top + 2) * 8 && e->mouse_y < (top + 3) * 8 && e->mouse_x >= 0) {
            int mc = e->mouse_x / 8 - bcol;
            for (int b = 0; b < 3; b++) if (mc >= bstart[b] && mc < bend[b]) hover = b;
        }
        for (int b = 0; b < 3; b++) {
            int dim = (b == 0 && sel == e->go_back_n - 1) || (b == 2 && sel == 0);
            for (int k = bstart[b]; k < bend[b]; k++)
                text_draw_char(e->screen, (bcol + k) * 8, (top + 2) * 8, (uint8_t)buttons[k],
                               b == hover ? 15 : dim ? 8 : 0, b == hover ? 0 : 15);
        }
        if (e->host->present) e->host->present(e->host);
        int k = e->host->poll_key(e->host);
        if (e->host->quit_requested(e->host)) { e->quit = 1; break; }
        if (!k) { e->host->sleep(e->host, 10); continue; }
        if (k == KEY_MOUSE_L) {
            if (hover < 0) continue;
            k = bkey[hover];
        }
        if (k == KEY_ESC || k == KEY_MOUSE_R) break;
        if (k == KEY_LEFT && sel < e->go_back_n - 1) sel++;
        else if (k == KEY_RIGHT && sel > 0) sel--;
        else if (k == KEY_ENTER) { result = sel; break; }
    }
    e->modal = 0;
    if (result < 0) return 0;
    state_apply(e, (SaveState *)e->go_back[result]);
    return 1;
}

/* After a fatal mistake: the new choice next to the original ones. The
 * original choices are sent as the game's own keys, so the game handles
 * them exactly as before (with its own questions). */
static void game_over_menu(Agi *e) {
    static const char *items[4] = {
        "Go back to an earlier room   <^Z>",
        "Restore a saved game         <F7>",
        "Restart the game             <F9>",
        "Quit                      <Alt-Z>"};
    const char *title = "What would you like to do?";
    const int n = 4, top = 9;
    int sel = e->go_back_n ? 0 : 1;
    for (;;) {
        int w = (int)strlen(items[0]);
        int col = (40 - w) / 2;
        int bx = col * 8 - 8, by = (top - 2) * 8 - 5, bw = w * 8 + 16, bh = (n + 2) * 8 + 10;
        e->modal = 1;
        int choice = -1;
        for (;;) {
            compose_screen(e);
            text_draw_box(e->screen, bx, by, bw, bh, 15, 4);
            int tc = col + (w - (int)strlen(title)) / 2;
            for (int k = 0; title[k]; k++) text_draw_char(e->screen, (tc + k) * 8, (top - 2) * 8, (uint8_t)title[k], 0, 15);
            if (e->mouse_x >= bx && e->mouse_x < bx + bw && e->mouse_y >= top * 8 && e->mouse_y < (top + n) * 8) {
                static int lx = -1, ly = -1;
                if (e->mouse_x != lx || e->mouse_y != ly) { sel = e->mouse_y / 8 - top; lx = e->mouse_x; ly = e->mouse_y; }
            }
            for (int i = 0; i < n; i++) {
                int dim = i == 0 && !e->go_back_n;
                for (int k = 0; items[i][k]; k++)
                    text_draw_char(e->screen, (col + k) * 8, (top + i) * 8, (uint8_t)items[i][k],
                                   i == sel ? 15 : dim ? 8 : 0, i == sel ? 0 : 15);
            }
            if (e->host->present) e->host->present(e->host);
            int k = e->host->poll_key(e->host);
            if (e->host->quit_requested(e->host)) { e->quit = 1; break; }
            if (!k) { e->host->sleep(e->host, 10); continue; }
            if (k == KEY_MOUSE_L) {
                if (e->mouse_x >= bx && e->mouse_x < bx + bw && e->mouse_y >= top * 8 && e->mouse_y < (top + n) * 8) {
                    sel = e->mouse_y / 8 - top; k = KEY_ENTER;
                } else continue;
            }
            if (k == KEY_ESC || k == KEY_MOUSE_R) break;
            if (k == KEY_UP) sel = (sel + n - 1) % n;
            if (k == KEY_DOWN) sel = (sel + 1) % n;
            if (k == KEY_ENTER) { if (sel == 0 && !e->go_back_n) continue; choice = sel; break; }
        }
        e->modal = 0;
        switch (choice) {
        case 0:
            if (go_back_pick(e, "Go back in time, Taran?", "ESC: the other choices")) return;
            continue;                                   /* back to these choices */
        case 1: agi_key_event(e, KEY_F(7)); return;
        case 2: agi_key_event(e, KEY_F(9)); return;
        case 3: agi_key_event(e, 0x2C << 8); return;    /* Alt+Z */
        default: return;                                /* ESC: stay */
        }
    }
}

/* Adds File > Go Back to the game's menu, after Restore. */
void agi_menu_add_go_back(Agi *e) {
    if (e->nmitem >= MAX_MENUITEM) return;
    for (int m = 0; m < e->nmenu; m++) {
        if (strncmp(e->menu[m].text, "File", 4)) continue;
        Menu *mn = &e->menu[m];
        int at = mn->first + mn->count;      /* default: at the end */
        for (int i = 0; i < mn->count; i++)
            if (!strncmp(e->mitem[mn->first + i].text, "Restore", 7)) { at = mn->first + i + 1; break; }
        memmove(&e->mitem[at + 1], &e->mitem[at], (size_t)(e->nmitem - at) * sizeof e->mitem[0]);
        e->nmitem++;
        MenuItem *mi = &e->mitem[at];
        int w = (int)strlen(e->mitem[mn->first].text);
        if (w < 13 || w > 30) w = 13;
        snprintf(mi->text, sizeof mi->text, "%-*s<^Z>", w - 4, "Go Back");   /* like "Restore  <F7>" */
        mi->ctrl = CTRL_GO_BACK; mi->enabled = 1; mi->menu = m;
        mn->count++;
        for (int k = m + 1; k < e->nmenu; k++) e->menu[k].first++;
        return;
    }
}

/* Seeds the list with the autosave of an earlier session. */
static void go_back_seed(Agi *e) {
    if (e->go_back_n) return;
    char path[1100];
    autosave_path(e, path, sizeof path);
    SaveState *s = malloc(sizeof *s);
    if (s && state_read(path, s)) go_back_push(e, s);
    else free(s);
}

/* ------------------------------------------------------------ main cycle */
static void do_restart(Agi *e) {
    if (e->host->sound_stop) e->host->sound_stop(e->host);
    go_back_clear(e);
    int sound = e->flag[FL_SOUND];
    agi_init_state(e);
    e->flag[FL_SOUND] = (uint8_t)sound;
    e->flag[FL_RESTART] = 1;
    e->var[V_DELAY] = 2;
}

int agi_cycle(Agi *e) {
    ScreenObj *ego = &e->obj[0];
    e->var[V_KEY] = 0;
    e->key_pending = 0;
    int nq = e->keyq_n;
    e->keyq_n = 0;
    for (int i = 0; i < nq; i++) handle_key(e, e->keyq[i]);
    walk_update(e);
    if (e->sound_flag >= 0 && (int32_t)(e->host->ticks(e->host) - e->sound_end_ms) >= 0) agi_sound_done(e);
    if (e->player_control && e->ego_hold_dir && !e->walk_active)
        e->var[V_EGO_DIR] = (uint8_t)e->ego_hold_dir;

    if (!e->player_control) e->var[V_EGO_DIR] = ego->dir;
    else ego->dir = e->var[V_EGO_DIR];
    obj_check_all_motions(e);

    e->exit_all = 0;
    e->call_depth = 0;
    e->haskey_waits = 0;
    while (!agi_run_logic(e, 0)) {
        if (e->quit) return 0;
        if (e->restart) { do_restart(e); }
        e->var[V_NO_WORD] = 0;
        e->var[V_OBJ_BORDER_NR] = 0;
        e->var[V_OBJ_BORDER] = 0;
        e->flag[FL_ENTERED] = 0;
        e->exit_all = 0;
        e->call_depth = 0;
        memset(e->ctrl, 0, sizeof e->ctrl);
    }
    memset(e->ctrl, 0, sizeof e->ctrl);
    int entered = e->flag[FL_NEW_ROOM] && !e->restart;
    ego->dir = e->var[V_EGO_DIR];
    e->var[V_OBJ_BORDER_NR] = 0;
    e->var[V_OBJ_BORDER] = 0;
    e->flag[FL_NEW_ROOM] = 0;
    e->flag[FL_RESTART] = 0;
    e->flag[FL_RESTORED] = 0;
    e->flag[FL_LOGIC0_FIRST] = 0;
    obj_update_all(e);
    if (entered) go_back_capture(e);
    if (e->menu_request) {
        e->menu_request = 0;
        text_menu(e, e->menu_start);
    }
    /* after a fatal mistake (the game's own message has been shown), offer
     * to go back; with File > Go Back or Ctrl+Z at any time */
    if (BC_GAME_OVER(e)) {
        if (!e->game_over_offered && e->var[87] <= 1 && !e->win_open) {
            e->game_over_offered = 1;
            e->go_back_request = 2;
        }
    } else e->game_over_offered = 0;
    if (e->go_back_request) {
        int over = e->go_back_request == 2;
        e->go_back_request = 0;
        if (over) game_over_menu(e);
        else go_back_pick(e, "Go back to an earlier room?", "ESC: stay where you are");
    }
    return !e->quit;
}

void agi_pump(Agi *e) {
    int k;
    while ((k = e->host->poll_key(e->host)) != 0) {
        if (k == KEY_MOUSE_R) agi_action_menu(e, e->mouse_x, e->mouse_y, e->walk_first);
        else agi_key_event(e, k);
    }
    if (e->host->quit_requested(e->host)) e->quit = 1;
}

static void update_clock(Agi *e, uint32_t now) {
    while (now - e->clock_ms >= 1000) {
        e->clock_ms += 1000;
        if (++e->var[V_SEC] >= 60) {
            e->var[V_SEC] = 0;
            if (++e->var[V_MIN] >= 60) {
                e->var[V_MIN] = 0;
                if (++e->var[V_HOUR] >= 24) { e->var[V_HOUR] = 0; e->var[V_DAY]++; }
            }
        }
    }
}

void agi_run(Agi *e) {
    go_back_seed(e);
    uint32_t now = e->host->ticks(e->host);
    e->clock_ms = now;
    uint32_t next = now;
    while (!e->quit) {
        agi_pump(e);
        if (e->quit) break;
        now = e->host->ticks(e->host);
        update_clock(e, now);
        if ((int32_t)(now - next) >= 0) {
            if (!agi_cycle(e)) break;
            int delay = e->var[V_DELAY] * 50;
            if (delay < 25) delay = 25;
            next += delay;
            now = e->host->ticks(e->host);
            if ((int32_t)(now - next) > 250) next = now;   /* don't try to catch up */
        }
        agi_frame(e);
        e->host->sleep(e->host, 2);
    }
}
