/*
    mc_layout.h -- Classic Media Center 0.6 (U1b skin): the UI model, the widget
    table (fixed 480x360 window), hit-testing, list maths, and the text
    runs the platform draws. Portable C89, no Toolbox calls.

    The Mac shell owns an McUi, changes it in its event handlers, marks
    what changed dirty (mc_ui_dirty), and at the end of each event
    renders ui->dirty: mc_ui_render (pixels) + mc_ui_runs (text, drawn
    with QuickDraw into the same GWorld), then CopyBits. The host tool
    does the same with a byte buffer and a Python TTF renderer.
*/
#ifndef MC_LAYOUT_H
#define MC_LAYOUT_H

#include "mc_skin.h"

#define MC_WIN_W   480
#define MC_WIN_H   360
#define MC_TITLE_H 24

enum
{
    W_TITLE, W_CLOSE, W_SHADE /* 0.6: not drawn, not hit: metal */, W_FIELD, W_SEARCH, W_HEADING, W_LIST,
    W_LCD, W_DLFIRST, W_INSECURE, W_BACK, W_MORE, W_STOP, W_PLAY,
    W_COUNT
};
#define W_NONE (-1)                 /* the metal: drag the window      */

/* per-widget flags */
#define MC_WF_ENABLED 1
#define MC_WF_PRESSED 2
#define MC_WF_CHECKED 4

/* screens (derived from 0.4's gListMode + list contents) */
#define MC_SCR_SEARCH  0            /* nothing searched yet / no hits  */
#define MC_SCR_RESULTS 1            /* gListMode 0                     */
#define MC_SCR_FILES   2            /* gListMode 1                     */

#define MC_FOC_FIELD 0
#define MC_FOC_LIST  1

/* list geometry */
/* per-file notes (McRow.note): the mark and dot are derived from them */
#define MC_NOTE_NONE    0
#define MC_NOTE_STUTTER 1           /* orange dot + "may stutter"      */
#define MC_NOTE_HEAVY   2           /* (0.6.1) G3: orange dot + "too heavy" */

#define MC_ROW_H     16
#define MC_LIST_ROWS 10
#define MC_UI_MAXROWS 64            /* = MC_MAXFILES; results use 25   */

/* scroller parts (mc_ui_hit *part for W_LIST) */
#define MC_PART_ROW    0            /* *row = the row hit (or -1)      */
#define MC_PART_UP     1
#define MC_PART_DOWN   2
#define MC_PART_PAGEUP 3
#define MC_PART_PAGEDN 4
#define MC_PART_THUMB  5

/* text roles: the platform maps them to fonts */
#define MC_F_TITLE  0    /* window title: the heading serif            */
#define MC_F_HEAD   1    /* headings: the heading serif                */
#define MC_F_UI     2    /* buttons, rows: Charcoal / Geneva           */
#define MC_F_UIBOLD 3
#define MC_F_SMALL  4    /* Geneva 9-10                                */
#define MC_F_LCD    5    /* LCD text: Geneva                           */
#define MC_F_LCDB   6    /* LCD figures: Geneva bold                   */
/* (0.6) the U2 font test scene only (mc_demo.c) */
#define MC_F_TEST_MCG  7 /* "MC Garamond" by name, whatever the chain says */
#define MC_F_TEST_HOEF 8 /* "Hoefler Text"                             */
#define MC_F_TEST_PAL  9 /* "Palatino"                                 */
#define MC_F_BTN       10 /* (0.6.1) button labels: Charcoal (system font), plain */

#define MC_AL_LEFT   0
#define MC_AL_CENTER 1
#define MC_AL_RIGHT  2
#define MC_TR_END    0
#define MC_TR_MIDDLE 1

typedef struct
{
    char   text[128];
    McRect box;            /* clip and truncation width                */
    short  base;           /* baseline y                               */
    unsigned char role, size, align, trunc;
    unsigned char rgb[3];
    unsigned char emboss;  /* draw a light copy 1 px lower first       */
} McRun;

typedef struct
{
    char text[100];        /* title, or the file's readable label      */
    char fmt[24];          /* files: "h.264"                           */
    char size[16];         /* files: "298.7 MB"                        */
    char mark[24];         /* files: "plays now?" ...                  */
    unsigned char dot;     /* MC_DOT_*                                 */
    unsigned char note;    /* MC_NOTE_*: a per-file verdict note; a
                              STUTTER note turns an OK dot orange and the
                              mark into "may stutter" (never overrides a
                              red "will not play")                     */
} McRow;

typedef struct
{
    int  screen;
    int  busy;             /* a transfer runs: only Stop works         */
    int  serving;          /* local streaming server running: Stop is
                              enabled while busy OR serving            */
    int  confirm;          /* the big-download question is showing     */
    int  quit_pending;     /* "Streaming: Quit again to stop" is showing */
    int  active;           /* window is frontmost                      */
    int  focus;            /* MC_FOC_*                                 */
    int  more_avail;       /* a full page of results: More enabled     */
    int  dl_first, insecure;   /* the two checkboxes                   */
    int  pressed;          /* W_* drawn pressed, or W_NONE             */
    int  draw_field_text;  /* host: 1 (no TextEdit); Mac: 0            */

    char field[128];       /* host only (Mac: TextEdit holds it)       */
    char heading[96];
    char head_right[64];
    char list_hint[96];    /* shown in an empty list                   */

    McRow rows[MC_UI_MAXROWS];
    int   nrows, top, sel; /* sel -1 = none                            */

    char lcd_head[160];
    char lcd_detail[200];
    char lcd_right[32];    /* "426 KB/s"                               */
    int  progress;         /* 0..1000, -1 = no bar                     */
    int  glyph;            /* MC_GLYPH_*                               */

    unsigned char flags[W_COUNT];
    McRect dirty;
} McUi;

void mc_ui_init(McUi *ui);
/* recompute enabled/checked flags from the state (0.4's UpdateButtons
   and Busy rules); marks changed widgets dirty */
void mc_ui_sync(McUi *ui);
const McRect *mc_ui_rect(int w);
/* which widget is the aqua (Return) button now */
int  mc_ui_primary(const McUi *ui);

void mc_ui_dirty(McUi *ui, int w);
void mc_ui_dirty_all(McUi *ui);

/* W_* or W_NONE; for W_LIST *part = MC_PART_*, *row = row index */
int  mc_ui_hit(const McUi *ui, int x, int y, int *part, int *row);

/* list maths */
void mc_ui_list_rows_rect(McRect *r);       /* the rows area           */
void mc_ui_scroller_rect(McRect *r);        /* (0.6) the scroll bar    */
void mc_ui_row_rect(const McUi *ui, int row, McRect *r); /* empty if hidden */
int  mc_ui_max_top(const McUi *ui);
void mc_ui_scroll(McUi *ui, int newTop);
void mc_ui_select(McUi *ui, int row);       /* + scroll into view      */
/* arrow/page keys: 1 = up, 2 = down, 3 = page up, 4 = page down,
   5 = home, 6 = end. Returns 1 if the selection moved */
int  mc_ui_list_key(McUi *ui, int key);

/* what a row shows once its note is applied */
int  mc_ui_row_dot(const McRow *r);
const char *mc_ui_row_mark(const McRow *r);

/* ---- keyboard: portable key map. ch = the Mac event's character code
   (the MC_K_* values are the classic Mac ones), mods = MC_MOD_*.
   Returns an MC_ACT_* the shell carries out (FlashButton for the ones
   that have a button, see mc_ui_act_button). Esc and Cmd-. NEVER quit:
   they stop (busy or serving), cancel the big-download question, or go
   Back. Quit = Cmd-Q or the close button, via mc_ui_quit_request. ---- */
#define MC_K_HOME   0x01
#define MC_K_ENTER  0x03
#define MC_K_END    0x04
#define MC_K_PGUP   0x0B
#define MC_K_PGDN   0x0C
#define MC_K_TAB    0x09
#define MC_K_RETURN 0x0D
#define MC_K_ESC    0x1B
#define MC_K_LEFT   0x1C
#define MC_K_RIGHT  0x1D
#define MC_K_UP     0x1E
#define MC_K_DOWN   0x1F
#define MC_MOD_CMD   1
#define MC_MOD_SHIFT 2

#define MC_ACT_NONE    0   /* swallowed / nothing to do                */
#define MC_ACT_SEARCH  1   /* run the search (Return in the field)     */
#define MC_ACT_PLAY    2   /* Play (Return in list, Enter, Cmd-P)      */
#define MC_ACT_BACK    3   /* Cmd-[ or Esc/Cmd-. on the files screen   */
#define MC_ACT_STOP    4   /* Esc/Cmd-. while busy or serving: the
                              shell does what Stop does (see 4)        */
#define MC_ACT_CANCEL  5   /* Esc/Cmd-. cleared the big-download question */
#define MC_ACT_QUIT    6   /* really quit now                          */
#define MC_ACT_TYPE    7   /* hand the key to the search field (focus
                              was moved to it)                         */
#define MC_ACT_CUT     8
#define MC_ACT_COPY    9
#define MC_ACT_PASTE  10
#define MC_ACT_SELALL 11
#define MC_ACT_MOVED  12   /* selection or focus changed (redraw only) */

int  mc_ui_key(McUi *ui, int ch, int mods);
/* the button to flash for an action, or W_NONE */
int  mc_ui_act_button(int act);
/* close button / Cmd-Q / Quit Apple event: 1 = quit now. While
   ui->serving the first request shows the LCD warning (returns 0), the
   second one quits. Any other action forgets the warning. */
int  mc_ui_quit_request(McUi *ui);
/* Stop pressed (button or key): clears pending warnings */
void mc_ui_note_stop(McUi *ui);

/* pixels for everything inside clip (and the canvas' own clip) */
void mc_ui_render(const McUi *ui, McCanvas *cv, const McRect *clip);
/* text runs intersecting clip; returns the count (<= max) */
int  mc_ui_runs(const McUi *ui, const McRect *clip, McRun *out, int max);

#endif
