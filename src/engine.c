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

static int pick_slot(Agi *e, const char *title, int need_existing) {
    char descs[SAVE_SLOTS][32];
    int exist[SAVE_SLOTS];
    for (int i = 0; i < SAVE_SLOTS; i++) exist[i] = slot_desc(e, i, descs[i]);
    int sel = 0;
    e->modal = 1;
    const int bx = 4 * 8 - 8, by = 3 * 8 - 5, bw = 32 * 8 + 16, bh = (SAVE_SLOTS + 3) * 8 + 10;
    for (;;) {
        compose_screen(e);
        text_draw_box(e->screen, bx, by, bw, bh, 15, 4);
        for (int k = 0; title[k]; k++) text_draw_char(e->screen, (4 + k) * 8, 3 * 8, (uint8_t)title[k], 0, 15);
        for (int i = 0; i < SAVE_SLOTS; i++) {
            char line[40];
            snprintf(line, sizeof line, "%2d. %-27s", i + 1, exist[i] ? descs[i] : "-");
            int inv = i == sel;
            for (int k = 0; line[k] && k < 31; k++)
                text_draw_char(e->screen, (4 + k) * 8, (5 + i) * 8, (uint8_t)line[k], inv ? 15 : 0, inv ? 0 : 15);
        }
        if (e->host->present) e->host->present(e->host);
        int k = e->host->poll_key(e->host);
        if (e->host->quit_requested(e->host)) { e->quit = 1; break; }
        if (!k) { e->host->sleep(e->host, 10); continue; }
        if (k == KEY_MOUSE_L) {
            int r = e->mouse_y / 8 - 5;
            if (r >= 0 && r < SAVE_SLOTS && e->mouse_x >= bx && e->mouse_x < bx + bw) { sel = r; k = KEY_ENTER; }
            else continue;
        }
        if (k == KEY_ESC || k == KEY_MOUSE_R) break;
        if (k == KEY_UP) sel = (sel + SAVE_SLOTS - 1) % SAVE_SLOTS;
        if (k == KEY_DOWN) sel = (sel + 1) % SAVE_SLOTS;
        if (k == KEY_ENTER) {
            if (need_existing && !exist[sel]) continue;
            e->modal = 0;
            return sel;
        }
    }
    e->modal = 0;
    return -1;
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

void agi_save_game(Agi *e) {
    int slot = pick_slot(e, "Save the game in which slot?", 0);
    if (slot < 0) return;
    char desc[41] = {0};
    char old[32];
    slot_desc(e, slot, old);
    if (!text_get_string(e, "How would you like to describe this saved game?", -1, -1, 30, desc)) return;
    if (!desc[0]) snprintf(desc, sizeof desc, "%s", old[0] ? old : "Saved game");
    SaveState *s = calloc(1, sizeof *s);
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
    for (int i = 0; i < e->nmitem; i++) s->item_enabled[i] = (uint8_t)e->mitem[i].enabled;
    for (int i = 0; i < 256; i++) { s->logic_loaded[i] = (uint8_t)e->logic[i].loaded; s->scan_start[i] = e->logic[i].scan_start; }
    char path[1024];
    slot_path(e, slot, path, sizeof path);
    FILE *f = fopen(path, "wb");
    int ok = 0;
    if (f) {
        int ver = SAVE_VERSION;
        char d32[32] = {0};
        snprintf(d32, sizeof d32, "%s", desc);
        ok = fwrite(SAVE_MAGIC, 1, 8, f) == 8 && fwrite(&ver, 4, 1, f) == 1 &&
             fwrite(d32, 1, 32, f) == 32 && fwrite(s, sizeof *s, 1, f) == 1;
        ok = (fclose(f) == 0) && ok;
    }
    free(s);
    if (!ok) text_print(e, "The game could not be saved.", -1, -1, 0, 1);
}

int agi_restore_game(Agi *e) {
    int slot = pick_slot(e, "Restore which saved game?", 1);
    if (slot < 0) return 0;
    char path[1024];
    slot_path(e, slot, path, sizeof path);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    SaveState *s = calloc(1, sizeof *s);
    char hdr[8], d32[32];
    int ver = 0;
    int ok = fread(hdr, 1, 8, f) == 8 && !memcmp(hdr, SAVE_MAGIC, 8) &&
             fread(&ver, 4, 1, f) == 1 && ver == SAVE_VERSION &&
             fread(d32, 1, 32, f) == 32 && fread(s, sizeof *s, 1, f) == 1;
    fclose(f);
    if (!ok) { free(s); text_print(e, "This saved game cannot be read.", -1, -1, 0, 1); return 0; }
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
    for (int i = 0; i < e->nmitem; i++) e->mitem[i].enabled = s->item_enabled[i];
    for (int i = 1; i < 256; i++) {
        if (e->logic[i].loaded) { free(e->logic[i].data); memset(&e->logic[i], 0, sizeof e->logic[i]); }
    }
    for (int i = 0; i < 256; i++) {
        if (s->logic_loaded[i]) { agi_load_logic(e, i); e->logic[i].scan_start = s->scan_start[i]; }
    }
    for (int i = 0; i < MAX_OBJ; i++)
        if (e->obj[i].flags & (F_ANIMATED | F_DRAWN)) agi_load_view(e, e->obj[i].view);
    free(s);
    e->walk_active = 0;
    e->win_open = 0;
    e->sound_flag = -1;
    if (e->host->sound_stop) e->host->sound_stop(e->host);
    e->flag[FL_RESTORED] = 1;
    e->exit_all = 1;
    return 1;
}

/* ------------------------------------------------------------ main cycle */
static void do_restart(Agi *e) {
    if (e->host->sound_stop) e->host->sound_stop(e->host);
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
    ego->dir = e->var[V_EGO_DIR];
    e->var[V_OBJ_BORDER_NR] = 0;
    e->var[V_OBJ_BORDER] = 0;
    e->flag[FL_NEW_ROOM] = 0;
    e->flag[FL_RESTART] = 0;
    e->flag[FL_RESTORED] = 0;
    e->flag[FL_LOGIC0_FIRST] = 0;
    obj_update_all(e);
    if (e->menu_request) {
        e->menu_request = 0;
        text_menu(e, e->menu_start);
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
