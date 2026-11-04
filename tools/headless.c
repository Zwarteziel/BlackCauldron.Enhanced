/* Black Cauldron Enhanced - headless test runner.
 * Usage: headless <gamedir> <script> [outdir]
 * Script lines: cycles N | key K | mkey K | type TEXT | click X Y | rclick X Y
 *               hold DIR | shot NAME | dump | quit
 * In waits for a key (message windows) without queued modal keys, ENTER is
 * pressed automatically. GPL-3.0-or-later. */
#include "../src/agi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t vt;
static int mq[256], mq_n, mq_r;
static int modal_polls;
static const char *outdir = ".";
static Agi *G;
static int verbose = 1;

static void present(AgiHost *h) { (void)h; }
static uint32_t ticks(AgiHost *h) { (void)h; return vt; }
static void hsleep(AgiHost *h, int ms) { (void)h; vt += ms; }
static int quitreq(AgiHost *h) { (void)h; return 0; }
static void shot(const char *name);
static const uint8_t EGA[16][3] = {
    {0,0,0},{0,0,170},{0,170,0},{0,170,170},{170,0,0},{170,0,170},{170,85,0},{170,170,170},
    {85,85,85},{85,85,255},{85,255,85},{85,255,255},{255,85,85},{255,85,255},{255,255,85},{255,255,255}};

static char mshot_q[8][64]; static int mshot_n, mshot_r;
static void save_screen(const char *name);
static int poll_key(AgiHost *h) {
    (void)h;
    if (!G->modal) return 0;
    if (modal_polls == 0 && mshot_r < mshot_n) save_screen(mshot_q[mshot_r++]);
    if (mq_r < mq_n) return mq[mq_r++];
    if (++modal_polls > 3) { modal_polls = 0; return KEY_ENTER; }
    return 0;
}



static void save_screen(const char *name) {
    char path[1024];
    snprintf(path, sizeof path, "%s/%s.ppm", outdir, name);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n320 200\n255\n");
    for (int i = 0; i < SCR_W * SCR_H; i++) fwrite(EGA[G->screen[i] & 15], 1, 3, f);
    fclose(f);
}

static void shot(const char *name) {
    char path[1024];
    snprintf(path, sizeof path, "%s/%s.ppm", outdir, name);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    agi_frame(G);
    fprintf(f, "P6\n320 200\n255\n");
    for (int i = 0; i < SCR_W * SCR_H; i++) fwrite(EGA[G->screen[i] & 15], 1, 3, f);
    fclose(f);
}

static void logh(Agi *e, const char *what, const char *text) {
    if (!verbose) return;
    char b[2048]; int o = 0;
    for (const char *s = text; *s && o < 2000; s++) b[o++] = *s == '\n' ? ' ' : *s;
    b[o] = 0;
    printf("  [%s] room=%d: %s\n", what, e->var[V_ROOM], b);
}

static int keyname(const char *s) {
    if (s[0] == 'F' && s[1]) return KEY_F(atoi(s + 1));
    if (!strcmp(s, "ENTER")) return KEY_ENTER;
    if (!strcmp(s, "ESC")) return KEY_ESC;
    if (!strcmp(s, "UP")) return KEY_UP;
    if (!strcmp(s, "DOWN")) return KEY_DOWN;
    if (!strcmp(s, "LEFT")) return KEY_LEFT;
    if (!strcmp(s, "RIGHT")) return KEY_RIGHT;
    if (!strcmp(s, "SPACE")) return ' ';
    if (!strcmp(s, "TAB")) return 9;
    return (unsigned char)s[0];
}

static void dump(void) {
    ScreenObj *o = &G->obj[0];
    printf("  room=%d ego=(%d,%d) view=%d loop=%d dir=%d pc=%d score=%d flags=%04x\n",
           G->var[V_ROOM], o->x, o->y, o->view, o->loop, o->dir, G->player_control, G->var[V_SCORE], o->flags);
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: headless gamedir script [outdir]\n"); return 1; }
    if (argc > 3) outdir = argv[3];
    static Agi eng;
    G = &eng;
    AgiHost host = {0};
    host.present = present; host.poll_key = poll_key; host.ticks = ticks;
    host.sleep = hsleep; host.quit_requested = quitreq;
    if (agi_load(&eng, argv[1]) < 0) { fprintf(stderr, "%s\n", eng.err); return 1; }
    eng.host = &host;
    eng.walk_first[1] = eng.walk_first[2] = 1;
    agi_log_hook = logh;
    agi_init_state(&eng);
    FILE *sf = fopen(argv[2], "r");
    if (!sf) { perror(argv[2]); return 1; }
    char line[512];
    long total = 0;
    while (fgets(line, sizeof line, sf) && !eng.quit) {
        line[strcspn(line, "\r\n")] = 0;
        char cmd[32] = {0}, a1[256] = {0}, a2[64] = {0};
        sscanf(line, "%31s %255s %63s", cmd, a1, a2);
        if (!cmd[0] || cmd[0] == '#') continue;
        printf("> %s\n", line);
        if (!strcmp(cmd, "cycles")) {
            int n = atoi(a1);
            for (int i = 0; i < n && !eng.quit; i++) {
                if (!agi_cycle(&eng)) break;
                int d = eng.var[V_DELAY] * 50; if (d < 25) d = 25;
                vt += d; total++;
                eng.var[V_SEC] = (uint8_t)((vt / 1000) % 60);
            }
        } else if (!strcmp(cmd, "key")) agi_key_event(&eng, keyname(a1));
        else if (!strcmp(cmd, "mkey")) mq[mq_n++] = keyname(a1);
        else if (!strcmp(cmd, "type")) { for (char *s = line + 5; *s; s++) mq[mq_n++] = *s; mq[mq_n++] = KEY_ENTER; }
        else if (!strcmp(cmd, "mouse")) { eng.mouse_x = atoi(a1); eng.mouse_y = atoi(a2); }
        else if (!strcmp(cmd, "click")) { eng.mouse_x = atoi(a1); eng.mouse_y = atoi(a2); agi_key_event(&eng, KEY_MOUSE_L); }
        else if (!strcmp(cmd, "act")) {   /* act X Y N : right-click menu choice N */
            int n = atoi(line + strlen("act ") + strlen(a1) + strlen(a2) + 2);
            eng.mouse_x = atoi(a1); eng.mouse_y = atoi(a2);
            mq[mq_n++] = KEY_DOWN; for (int i = 0; i < n; i++) mq[mq_n++] = KEY_DOWN;
            mq[mq_n++] = KEY_UP; mq[mq_n++] = KEY_ENTER;
            agi_action_menu(&eng, eng.mouse_x, eng.mouse_y, eng.walk_first);
        }
        else if (!strcmp(cmd, "hold")) agi_set_hold_dir(&eng, atoi(a1));
        else if (!strcmp(cmd, "shot")) shot(a1);
        else if (!strcmp(cmd, "mshot") && mshot_n < 8) snprintf(mshot_q[mshot_n++], 64, "%s", a1);
        else if (!strcmp(cmd, "dump")) dump();
        else if (!strcmp(cmd, "var")) printf("  v%d=%d\n", atoi(a1), eng.var[atoi(a1)]);
        else if (!strcmp(cmd, "setvar")) eng.var[atoi(a1) & 255] = (uint8_t)atoi(a2);
        else if (!strcmp(cmd, "ctrlz")) agi_key_event(&eng, 26);
        else if (!strcmp(cmd, "goback")) printf("  go back points: %d\n", eng.go_back_n);
        else if (!strcmp(cmd, "flag")) printf("  f%d=%d\n", atoi(a1), eng.flag[atoi(a1)]);
        else if (!strcmp(cmd, "quiet")) verbose = 0;
        else if (!strcmp(cmd, "loud")) verbose = 1;
        else if (!strcmp(cmd, "quit")) break;
    }
    printf("done: %ld cycles, room %d\n", total, eng.var[V_ROOM]);
    return 0;
}
