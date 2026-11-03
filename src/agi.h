/* Black Cauldron Enhanced - native AGI v2 interpreter. GPL-3.0-or-later.
 *
 * The engine is plain C without SDL. A host (the SDL front end, or the
 * headless test tool) supplies input and timing through the AgiHost
 * callbacks and shows the 320x200 EGA frame the engine produces. */
#ifndef BCE_AGI_H
#define BCE_AGI_H
#include <stdint.h>
#include <stddef.h>

#define PIC_W 160
#define PIC_H 168
#define SCR_W 320
#define SCR_H 200
#define MAX_OBJ 64          /* screen objects (animated objects) */
#define MAX_ITEMS 256       /* inventory objects */
#define MAX_KEYMAP 64
#define MAX_MENU 16
#define MAX_MENUITEM 48

/* Screen object flags (same meaning as the original interpreter) */
enum {
    F_DRAWN          = 0x0001,
    F_IGNORE_BLOCKS  = 0x0002,
    F_FIXED_PRIORITY = 0x0004,
    F_IGNORE_HORIZON = 0x0008,
    F_UPDATE         = 0x0010,
    F_CYCLING        = 0x0020,
    F_ANIMATED       = 0x0040,
    F_MOTION         = 0x0080,
    F_ON_WATER       = 0x0100,
    F_IGNORE_OBJECTS = 0x0200,
    F_UPDATE_POS     = 0x0400,
    F_ON_LAND        = 0x0800,
    F_DONT_UPDATE    = 0x1000,
    F_FIX_LOOP       = 0x2000,
    F_DIDNT_MOVE     = 0x4000,
    F_ADD_TO_PIC     = 0x8000
};

enum { MOTION_NORMAL, MOTION_WANDER, MOTION_FOLLOW, MOTION_MOVE, MOTION_EGO };
enum { CYCLE_NORMAL, CYCLE_END_LOOP, CYCLE_REV_LOOP, CYCLE_REVERSE };

/* Well-known variables and flags */
enum {
    V_ROOM = 0, V_PREV_ROOM = 1, V_EGO_BORDER = 2, V_SCORE = 3, V_OBJ_BORDER_NR = 4,
    V_OBJ_BORDER = 5, V_EGO_DIR = 6, V_MAX_SCORE = 7, V_FREE_MEM = 8, V_NO_WORD = 9,
    V_DELAY = 10, V_SEC = 11, V_MIN = 12, V_HOUR = 13, V_DAY = 14, V_JOY = 15,
    V_EGO_VIEW = 16, V_ERROR = 17, V_ERROR2 = 18, V_KEY = 19, V_COMPUTER = 20,
    V_WIN_TIMER = 21, V_SOUNDGEN = 22, V_VOLUME = 23, V_INPUT_LEN = 24,
    V_SEL_ITEM = 25, V_MONITOR = 26
};
enum {
    FL_EGO_WATER = 0, FL_EGO_INVISIBLE = 1, FL_ENTERED = 2, FL_EGO_TRIGGER = 3,
    FL_SAID = 4, FL_NEW_ROOM = 5, FL_RESTART = 6, FL_NO_SCRIPT = 7, FL_JOY = 8,
    FL_SOUND = 9, FL_DEBUG = 10, FL_LOGIC0_FIRST = 11, FL_RESTORED = 12,
    FL_STATUS_SELECT = 13, FL_MENU = 14, FL_PRINT_MODE = 15
};

typedef struct {
    int w, h, transp, mirror_loop, mirrored_flag;
    const uint8_t *rle;     /* points into view data */
} Cel;

typedef struct {
    int ncels;
    Cel cel[32];
} Loop;

typedef struct {
    int loaded;
    int nloops;
    Loop loop[16];
    const char *desc;       /* description, or NULL */
    uint8_t *data;
    int len;
} View;

typedef struct {
    int nr;
    int16_t x, y, xprev, yprev;
    int16_t xsize, ysize;
    uint8_t view, loop, cel, nloops, ncels;
    uint8_t priority;
    uint16_t flags;
    uint8_t step_size, step_time, step_count;
    uint8_t cycle_time, cycle_count;
    uint8_t dir;
    uint8_t motion, cycle;
    uint8_t loop_flag;
    /* motion parameters */
    int16_t move_x, move_y;
    uint8_t move_step, move_flag;
    uint8_t follow_step, follow_flag, follow_count;
    uint8_t wander_count;
} ScreenObj;

typedef struct { uint16_t key; uint8_t ctrl; } KeyMap;

typedef struct { char text[41]; int ctrl; int enabled; int menu; } MenuItem;
typedef struct { char text[41]; int first, count; int col; } Menu;

typedef struct {
    uint8_t *data; int len;     /* raw logic resource */
    const uint8_t *code; int code_len;
    int nmsg;
    char *msg[256];             /* decrypted messages (1-based) */
    int scan_start;
    int loaded;
} Logic;

/* Host interface ------------------------------------------------------ */
typedef struct AgiHost {
    void *user;
    /* Present the current frame (eng->screen, 320x200 EGA indices). */
    void (*present)(struct AgiHost *h);
    /* Process host events; returns a key code (AGI style: ascii, or scan<<8)
     * or 0. Must not block. */
    int  (*poll_key)(struct AgiHost *h);
    /* Milliseconds since start. */
    uint32_t (*ticks)(struct AgiHost *h);
    /* Sleep a little (ms); the host should keep its window alive. */
    void (*sleep)(struct AgiHost *h, int ms);
    /* Nonzero when the user wants to quit (window closed). */
    int  (*quit_requested)(struct AgiHost *h);
    /* Sound: play resource data, stop. Host sets flag via agi_sound_done. */
    void (*sound_play)(struct AgiHost *h, const uint8_t *data, int len);
    void (*sound_stop)(struct AgiHost *h);
} AgiHost;

/* Engine state -------------------------------------------------------- */
typedef struct Agi {
    char dir[512];
    AgiHost *host;

    uint8_t *vol[16]; int vol_len[16];
    uint32_t logdir[256], picdir[256], viewdir[256], snddir[256];
    int nlog, npic, nview, nsnd;

    Logic logic[256];
    View view[256];
    int pic_loaded[256];

    uint8_t var[256];
    uint8_t flag[256];
    char str[24][41];
    uint8_t ctrl[256];         /* controller events this cycle */

    /* inventory */
    int nitems;
    char *item_name[MAX_ITEMS];
    uint8_t item_room[MAX_ITEMS];
    uint8_t *objfile;

    ScreenObj obj[MAX_OBJ];
    int nobj;                   /* number of screen object slots */

    /* picture buffers (160x168) */
    uint8_t pic_vis[PIC_W * PIC_H];
    uint8_t pic_pri[PIC_W * PIC_H];
    uint8_t shown_vis[PIC_W * PIC_H];  /* last shown picture */
    int pic_shown;
    int pri_table[PIC_H];

    /* composited screen */
    uint8_t screen[SCR_W * SCR_H];
    uint8_t text_ch[25][40], text_fg[25][40], text_bg[25][40];
    uint8_t text_used[25][40];

    int horizon;
    int block_on, bx1, by1, bx2, by2;
    int player_control;
    int status_on, input_on;
    int pic_row, input_row, status_row;
    int fg, bg;
    int text_mode;
    int cursor_char;

    /* message window */
    int win_open;
    int win_x, win_y, win_w, win_h;   /* in screen pixels */

    KeyMap keymap[MAX_KEYMAP]; int nkeymap;

    Menu menu[MAX_MENU]; int nmenu;
    MenuItem mitem[MAX_MENUITEM]; int nmitem;
    int menu_submitted;
    int menu_request;
    int menu_start;           /* menu to open first (mouse click on the bar) */

    /* execution */
    int new_room_pending;
    int exit_all;
    int quit;
    int restart;
    int call_depth;
    uint32_t last_cycle_ms;
    uint32_t clock_ms;
    int key_pending;          /* key for have.key / var 19 */
    int sound_flag;           /* flag to set when sound ends, or -1 */
    uint32_t sound_end_ms;    /* when the current sound ends */
    int shake;

    /* host interaction */
    int modal;                /* engine waits for a key (window, menu...) */
    int mouse_x, mouse_y;     /* last mouse position, 320x200 screen pixels */
    int keyq[32]; int keyq_n;

    /* enhanced: modern controls */
    int walk_active;
    int walk_stuck;
    int walk_then_key;        /* key to send after walking, or 0 */
    int walk_first[4];        /* look, do, use, select: walk to the click first */
    int pad_swap;             /* 1 = left stick moves the pointer (help text) */
    int sound_mode;           /* 1 = PC speaker, 3 = PCjr/Tandy (value for var 22) */
    int haskey_waits;         /* consecutive have.key polls without a key */
    int walk_tx, walk_ty;
    int ego_hold_dir;         /* held direction from gamepad/held keys */

    uint32_t rng;
    char gameid[16];
    char err[256];
} Agi;

/* optional trace hook (messages, room changes) */
extern void (*agi_log_hook)(Agi *e, const char *what, const char *text);

/* res.c */
int  agi_load(Agi *e, const char *dir);
void agi_free(Agi *e);
int  agi_load_logic(Agi *e, int n);
int  agi_load_view(Agi *e, int n);
const uint8_t *agi_res(Agi *e, uint32_t entry, int *len);
void pic_draw(Agi *e, int n, int clear);
int  sound_duration_ms(const uint8_t *d, int len);
void view_draw_cel(Agi *e, int view, int loop, int cel, int x, int y, int pri,
                   uint8_t *vis, uint8_t *pribuf, int to_pic);
int  pri_from_y(Agi *e, int y);

/* objects.c */
void obj_reset(Agi *e, ScreenObj *o, int nr);
void obj_set_view(Agi *e, ScreenObj *o, int v);
void obj_set_loop(Agi *e, ScreenObj *o, int l);
void obj_set_cel(Agi *e, ScreenObj *o, int c);
void obj_fix_position(Agi *e, ScreenObj *o);
void obj_check_all_motions(Agi *e);
void obj_update_all(Agi *e);
void obj_move_to(Agi *e, ScreenObj *o);
void obj_add_to_pic(Agi *e, int view, int loop, int cel, int x, int y, int pri, int margin);
void compose_screen(Agi *e);

/* text.c */
void text_print(Agi *e, const char *msg, int row, int col, int width, int wait);
void text_close_window(Agi *e);
void text_display(Agi *e, int row, int col, const char *msg);
void text_clear_lines(Agi *e, int top, int bottom, int color);
void text_clear_rect(Agi *e, int r1, int c1, int r2, int c2, int color);
void text_format(Agi *e, int logic, const char *in, char *out, int outlen);
void text_status_line(Agi *e);
void text_inventory(Agi *e);
int  text_get_string(Agi *e, const char *prompt, int row, int col, int maxlen, char *out);
int  text_menu(Agi *e, int start);
int  text_menu_bar_hit(Agi *e, int sx, int sy);
int  text_wait_key(Agi *e);
void text_draw_char(uint8_t *scr, int x, int y, int ch, int fg, int bg);
void text_draw_box(uint8_t *scr, int x, int y, int w, int h, int bg, int line);
int  text_confirm(Agi *e, const char *msg);

/* logic.c */
int  agi_run_logic(Agi *e, int n);
void agi_new_room(Agi *e, int n);

/* engine.c */
void agi_init_state(Agi *e);
int  agi_cycle(Agi *e);          /* run one interpreter cycle; 0 = quit */
void agi_run(Agi *e);            /* main loop until quit */
void agi_key_event(Agi *e, int key);
void agi_frame(Agi *e);          /* compose + present */
void agi_pump(Agi *e);           /* poll host & keep time while waiting */
int  agi_random(Agi *e, int n);
void agi_fire_controller(Agi *e, int ctrl);
void agi_walk_to(Agi *e, int x, int y, int then_ctrl);
void agi_set_hold_dir(Agi *e, int dir);
void agi_sound_done(Agi *e);
void agi_save_game(Agi *e);
int  agi_restore_game(Agi *e);
int  agi_action_menu(Agi *e, int sx, int sy, const int walk_first[4]);
void text_shake(Agi *e, int n);

#define KEY_UP     (0x48 << 8)
#define KEY_DOWN   (0x50 << 8)
#define KEY_LEFT   (0x4B << 8)
#define KEY_RIGHT  (0x4D << 8)
#define KEY_HOME   (0x47 << 8)
#define KEY_PGUP   (0x49 << 8)
#define KEY_END    (0x4F << 8)
#define KEY_PGDN   (0x51 << 8)
#define KEY_CENTER (0x4C << 8)
#define KEY_F(n)   ((0x3A + (n)) << 8)
#define KEY_ENTER  13
#define KEY_ESC    27
#define KEY_BACKSPACE 8
#define KEY_MOUSE_L 0xF001
#define KEY_MOUSE_R 0xF002

#endif
