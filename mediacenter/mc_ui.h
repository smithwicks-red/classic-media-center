/*
    mc_ui.h -- Classic Media Center 0.6: the PORTABLE controller of the
    skinned window. C89, no Toolbox calls (host-tested by
    test06/mc_ui_test.c on x86 and under qemu-ppc).

    The Mac shell (MediaCenter.c) turns Toolbox events into the abstract
    calls below (a key with modifiers, a click at x,y, the tracking of a
    pressed button, activate/deactivate, an idle tick) and feeds it the
    app's status: status lines (LCD headline), log lines (LCD detail),
    download progress (McfEnv.progress), busy/serving. The controller
    drives the McUi model (mc_layout.c: what is drawn, what is enabled,
    which button is the aqua one) and decides what happens; it asks the
    app to DO things through the McUiCb table (search, play, back, stop,
    quit, the search field's TextEdit record, flashing a button). After
    every call the shell renders ctl->ui.dirty (mc_ui_render + mc_ui_runs
    + TEUpdate + CopyBits) and clears it.

    Quit (Cmd-Q, File > Quit, the close button, the Quit Apple event):
    mcu_quit asks the app (cb.quit = MediaCenter.c's DoQuit around
    mcs_quit, so the 0.5u streaming rules are unchanged: warn, a second
    Quit within 10 s closes the movie in QuickTime Player, then quits).
    The controller only SHOWS it: the LCD warning (no alert) while the
    second Quit is awaited; any other key or click, or 10 s, puts the LCD
    back as it was. Esc and Cmd-. never quit.
*/
#ifndef MC_UI_H
#define MC_UI_H

#include "mc_layout.h"
#include "mc_core.h"

/* cb.quit results: the same values as mc_stream.h's MCS_QUIT_* */
#define MCU_QUIT_NOW     0
#define MCU_QUIT_WARNED  1
#define MCU_QUIT_CLOSING 2

/* mcu_click results: what the shell does next */
#define MCU_CLICK_NONE   0     /* disabled widget / nothing             */
#define MCU_CLICK_DRAG   1     /* metal or title: DragWindow            */
#define MCU_CLICK_TRACK  2     /* button / checkbox / close: track the
                                  press (mcu_track while StillDown), then
                                  mcu_release(w, inside)                */
#define MCU_CLICK_FIELD  3     /* the search field: TEClick             */
#define MCU_CLICK_ROW    4     /* a list row: already selected (and a
                                  double-click already played it)       */
#define MCU_CLICK_SCROLL 5     /* arrow / page part: mcu_scroll while
                                  StillDown                             */
#define MCU_CLICK_THUMB  6     /* thumb: mcu_thumb_begin, then
                                  mcu_thumb_move while StillDown        */

/* what the LCD shows (derived from the status lines) */
#define MCU_LCD_PLAIN    0
#define MCU_LCD_DOWNLOAD 1     /* McfEnv.progress: % bar, KB/s, MB left  */
#define MCU_LCD_BUFFER   2     /* "Buffering N%": the buffer gauge       */
#define MCU_LCD_STREAM   3     /* "Streaming via secure proxy: N KB/s.." */

#define MCU_WARN_TICKS   600UL /* the quit warning lasts 10 s (= mcs)   */

typedef struct McUiCb
{
    void *ref;
    void (*search)(void *ref, int more);  /* Search / More results       */
    void (*play)(void *ref);              /* Play: open item / play file
                                             (the app reads mcu_selected) */
    void (*back)(void *ref);
    void (*stop)(void *ref);              /* Stop (button, Esc, Cmd-.)    */
    void (*cancel)(void *ref);            /* Esc cleared the big-download
                                             question                    */
    int  (*quit)(void *ref);              /* MCU_QUIT_*; NULL = NOW       */
    void (*te_key)(void *ref, int ch, int mods);   /* typing           */
    void (*te_edit)(void *ref, int act);  /* MC_ACT_CUT/COPY/PASTE/SELALL */
    void (*te_focus)(void *ref, int on);  /* TEActivate / TEDeactivate   */
    void (*flash)(void *ref, int w);      /* show w pressed briefly      */
    void (*options)(void *ref);           /* a checkbox changed          */
} McUiCb;

typedef struct McCtl
{
    McUi   ui;
    McUiCb cb;
    int    lcd_mode;           /* MCU_LCD_*                             */
    int    te_on;              /* what te_focus was last told           */
    char   upstream[64];       /* https host the proxy fetches from     */
    /* the LCD as it was before the quit warning */
    char   sv_head[160], sv_detail[200], sv_right[32];
    int    sv_progress, sv_glyph, sv_mode;
    unsigned long warn_t;      /* tick of the warning                   */
    unsigned long now;         /* last mcu_idle tick                    */
    int    thumb_y0, thumb_top0;
    int    page;               /* results page shown                    */
} McCtl;

void mcu_init(McCtl *c, const McUiCb *cb);

/* ---- input ---- */
/* ch = the Mac character code (MC_K_*), mods = MC_MOD_*. Returns the
   MC_ACT_* that was carried out (MC_ACT_QUIT = quit now). */
int  mcu_key(McCtl *c, int ch, int mods);
/* mouse down at x,y (window coordinates). dbl = a double-click (the
   shell compares GetDblTime and the last click). *w = the widget. */
int  mcu_click(McCtl *c, int x, int y, int mods, int dbl, int *w, int *part);
void mcu_track(McCtl *c, int w, int inside);     /* pressed look       */
/* the mouse went up: inside = over w. Returns the MC_ACT_* done. */
int  mcu_release(McCtl *c, int w, int inside);
/* one step of an arrow / page part, if (x,y) is still on it: 1 = moved */
int  mcu_scroll(McCtl *c, int part, int x, int y);
void mcu_thumb_begin(McCtl *c, int y);
void mcu_thumb_move(McCtl *c, int y);
/* Edit menu (cut/copy/paste/clear/select all): MC_ACT_* or MC_ACT_NONE
   when the field cannot take it now */
int  mcu_edit(McCtl *c, int act);
/* Quit from anywhere: 1 = the app quits now */
int  mcu_quit(McCtl *c);
void mcu_activate(McCtl *c, int on);
/* every event-loop pass (ticks = TickCount): ends the quit warning */
void mcu_idle(McCtl *c, unsigned long ticks);

/* ---- app state -> the window ---- */
void mcu_set_busy(McCtl *c, int on);
void mcu_set_serving(McCtl *c, int on);
void mcu_set_confirm(McCtl *c, int on);
void mcu_set_options(McCtl *c, int dl_first, int insecure);
/* the search field's text (TextEdit's, mirrored so the grey hint hides) */
void mcu_set_field(McCtl *c, const char *text, long len);
/* status line -> LCD headline (patterns: Buffering, Streaming via ...,
   Downloading: ...); everything else is a plain headline */
void mcu_status(McCtl *c, const char *line);
/* log line -> LCD detail (not "proxy:" lines; they only reveal the
   upstream host) */
void mcu_detail(McCtl *c, const char *line);
/* McfEnv.progress: bytes done/total, ticks since the transfer began */
void mcu_progress(McCtl *c, unsigned long done, unsigned long total,
                  unsigned long ticks);

/* ---- the list ---- */
void mcu_show_empty(McCtl *c, const char *hint);
void mcu_show_results(McCtl *c, const McSearch *s, const char *terms,
                      int page, int sel);
void mcu_show_files(McCtl *c, const McMeta *m, const char *title, int sel);
void mcu_update_file(McCtl *c, const McMeta *m, int i);
/* the selected row, -1 = none */
int  mcu_selected(const McCtl *c);

/* MC_DOT_* of an McFile (state, hint) */
int  mcu_file_dot(const McFile *f);

#endif
