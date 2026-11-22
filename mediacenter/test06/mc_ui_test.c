/*
    mc_ui_test.c -- host test of the 0.6 portable controller (mc_ui.c)
    on top of mc_layout.c / mc_skin.c / mc_core.c. C89. Prints one line
    per check (the transcript is compared between x86-64 and qemu-ppc),
    then "mc_ui_test: N passed, M failed". Exit 0 = all pass.
    The fake app's quit() behaves like mc_stream.c's mcs_quit(): idle ->
    NOW; serving -> WARNED, a second Quit within 600 ticks -> CLOSING.
*/
#include <stdio.h>
#include <string.h>
#include "mc_ui.h"

static int fails = 0, checks = 0;
static void check(const char *what, int ok)
{
    checks++;
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) fails++;
}
/* got is evaluated ONCE (many checks have side effects) */
#define EQ(what, got, want) do { long g_ = (long)(got); char m_[200]; \
        sprintf(m_, "%s (got %ld want %ld)", what, g_, (long)(want)); \
        check(m_, g_ == (long)(want)); } while (0)
#define STR(what, got, want) do { const char *g_ = (got); char m_[400]; \
        sprintf(m_, "%s [%s]", what, g_); check(m_, strcmp(g_, want) == 0); } while (0)

/* ---------------- the fake app ---------------- */
typedef struct
{
    int search, more, play, back, stop, cancel, quit, opts, flashes, edits;
    int last_flash, last_edit, te_keys, last_te, te_focus_calls, te_focus;
    int serving;                 /* "mcs_streaming" */
    unsigned long now, warn_t;   /* the fake mcs clock / 10 s window */
} App;
static App A;
static McCtl C;

static void cb_search(void *r, int more) { (void)r; if (more) A.more++; else A.search++; }
static void cb_play(void *r) { (void)r; A.play++; }
static void cb_back(void *r) { (void)r; A.back++; }
static void cb_stop(void *r) { (void)r; A.stop++; }
static void cb_cancel(void *r) { (void)r; A.cancel++; }
static int  cb_quit(void *r)
{
    (void)r;
    A.quit++;
    if (!A.serving) return MCU_QUIT_NOW;
    if (A.warn_t == 0 || A.now - A.warn_t > 600UL) { A.warn_t = A.now ? A.now : 1; return MCU_QUIT_WARNED; }
    A.warn_t = 0;
    return MCU_QUIT_CLOSING;
}
static void cb_te_key(void *r, int ch, int mods) { (void)r; (void)mods; A.te_keys++; A.last_te = ch; }
static void cb_te_edit(void *r, int act) { (void)r; A.edits++; A.last_edit = act; }
static void cb_te_focus(void *r, int on) { (void)r; A.te_focus_calls++; A.te_focus = on; }
static void cb_flash(void *r, int w) { (void)r; A.flashes++; A.last_flash = w; }
static void cb_options(void *r) { (void)r; A.opts++; }

static void fresh(void)
{
    McUiCb cb;
    memset(&A, 0, sizeof A);
    memset(&cb, 0, sizeof cb);
    cb.search = cb_search; cb.play = cb_play; cb.back = cb_back; cb.stop = cb_stop;
    cb.cancel = cb_cancel; cb.quit = cb_quit; cb.te_key = cb_te_key;
    cb.te_edit = cb_te_edit; cb.te_focus = cb_te_focus; cb.flash = cb_flash;
    cb.options = cb_options;
    mcu_init(&C, &cb);
}

static McSearch gS;
static McMeta   gM;

static void results(int n)
{
    int i;
    memset(&gS, 0, sizeof gS);
    for (i = 0; i < n; i++)
    {
        sprintf(gS.hits[i].identifier, "id%d", i);
        sprintf(gS.hits[i].title, "Star Trek hit %d", i + 1);
    }
    gS.count = n;
    gS.numFound = 412;
    mcu_show_results(&C, &gS, "star trek", 1, -1);
}

static void files(void)
{
    int i;
    memset(&gM, 0, sizeof gM);
    for (i = 0; i < 30; i++)
    {
        McFile *f = &gM.files[i];
        sprintf(f->name, "1x%02d-Episode.mp4", i + 1);
        sprintf(f->title, "1x%02d - Episode %d", i + 1, i + 1);
        strcpy(f->format, "h.264");
        f->size = 313256504UL;
        f->hint = MC_HINT_PLAYS;
    }
    gM.nfiles = 30;
    gM.files[2].state = MC_STATE_PLAYS; gM.files[2].note = MC_FILE_NOTE_STUTTER;
    gM.files[3].state = MC_STATE_NEWER; gM.files[3].note = MC_FILE_NOTE_STUTTER;
    gM.files[4].hint = MC_HINT_NEWER;
    mcu_show_files(&C, &gM, "Jimmy And The Federation Gang", 0);
}

static int en(int w) { return (C.ui.flags[w] & MC_WF_ENABLED) != 0; }
static void ctr(int w, int *x, int *y)
{
    const McRect *r = mc_ui_rect(w);
    *x = (r->l + r->r) / 2; *y = (r->t + r->b) / 2;
}
static int click(int w, int dbl, int *hit)
{
    int x, y, part;
    ctr(w, &x, &y);
    return mcu_click(&C, x, y, 0, dbl, hit, &part);
}
/* press and release a widget the way the shell does */
static int press(int w)
{
    int hit, r = click(w, 0, &hit);
    if (r != MCU_CLICK_TRACK) return -1;
    mcu_track(&C, hit, 1);
    return mcu_release(&C, hit, 1);
}
static int key(int ch) { return mcu_key(&C, ch, 0); }
static int cmd(int ch) { return mcu_key(&C, ch, MC_MOD_CMD); }

static void t_start(void)
{
    printf("-- start\n");
    fresh();
    EQ("screen SEARCH", C.ui.screen, MC_SCR_SEARCH);
    EQ("focus field", C.ui.focus, MC_FOC_FIELD);
    EQ("TextEdit told: active", A.te_focus, 1);
    EQ("aqua = Search", mc_ui_primary(&C.ui), W_SEARCH);
    EQ("Stop disabled", en(W_STOP), 0);
    EQ("Play disabled", en(W_PLAY), 0);
    EQ("Back disabled", en(W_BACK), 0);
    EQ("TE draws the field text", C.ui.draw_field_text, 0);
    EQ("typing 'p' -> TYPE", key('p'), MC_ACT_TYPE);
    EQ("  te_key got 'p'", A.last_te, 'p');
    EQ("Left in field -> TYPE (TE moves caret)", key(MC_K_LEFT), MC_ACT_TYPE);
    EQ("Tab with no rows stays in field", (key(MC_K_TAB), C.ui.focus), MC_FOC_FIELD);
    EQ("Return in field -> SEARCH", key(MC_K_RETURN), MC_ACT_SEARCH);
    EQ("  search(0) called", A.search, 1);
    EQ("  Search flashed", A.last_flash, W_SEARCH);
    EQ("Enter with Play disabled -> nothing", key(MC_K_ENTER), MC_ACT_NONE);
    EQ("  play not called", A.play, 0);
    EQ("Esc on empty search screen -> nothing", key(MC_K_ESC), MC_ACT_NONE);
    EQ("Cmd-. on empty search screen -> nothing", cmd('.'), MC_ACT_NONE);
    EQ("  no quit, no back", A.quit + A.back, 0);
    mcu_set_field(&C, "", 0);
    EQ("empty field: grey hint shown", C.ui.field[0], 0);
    mcu_set_field(&C, "star trek", 9);
    STR("field mirror", C.ui.field, "star trek");
    mcu_show_empty(&C, "Nothing found. Try other words.");
    STR("no hits: list hint", C.ui.list_hint, "Nothing found. Try other words.");
}

static void t_results(void)
{
    int i;
    printf("-- results: focus, aqua button, list keys at the edges\n");
    fresh();
    results(25);
    EQ("screen RESULTS", C.ui.screen, MC_SCR_RESULTS);
    EQ("25 rows", C.ui.nrows, 25);
    EQ("nothing selected", mcu_selected(&C), -1);
    STR("heading", C.ui.heading, "Results for \xD2star trek\xD3");
    STR("head right", C.ui.head_right, "412 items \xE1 1-25");
    EQ("More enabled (full page)", en(W_MORE), 1);
    EQ("Play enabled", en(W_PLAY), 1);
    EQ("aqua = Search while the field has focus", mc_ui_primary(&C.ui), W_SEARCH);
    EQ("Tab -> list", (key(MC_K_TAB), C.ui.focus), MC_FOC_LIST);
    EQ("  first row selected", mcu_selected(&C), 0);
    EQ("  TextEdit deactivated", A.te_focus, 0);
    EQ("  aqua = Play", mc_ui_primary(&C.ui), W_PLAY);
    EQ("Up at row 0: no move", key(MC_K_UP), MC_ACT_NONE);
    for (i = 0; i < 3; i++) key(MC_K_DOWN);
    EQ("Down x3 -> row 3", mcu_selected(&C), 3);
    EQ("Up -> row 2", (key(MC_K_UP), mcu_selected(&C)), 2);
    EQ("End -> last row", (key(MC_K_END), mcu_selected(&C)), 24);
    EQ("  scrolled: top = 15", C.ui.top, 15);
    EQ("Down at the last row: no move", key(MC_K_DOWN), MC_ACT_NONE);
    EQ("PgDn at the end: no move", key(MC_K_PGDN), MC_ACT_NONE);
    EQ("PgUp -> 15", (key(MC_K_PGUP), mcu_selected(&C)), 15);
    EQ("PgUp -> 6", (key(MC_K_PGUP), mcu_selected(&C)), 6);
    EQ("  top follows", C.ui.top, 6);
    EQ("PgUp -> 0 (clamped)", (key(MC_K_PGUP), mcu_selected(&C)), 0);
    EQ("PgUp at the top: no move", key(MC_K_PGUP), MC_ACT_NONE);
    EQ("PgDn -> 9", (key(MC_K_PGDN), mcu_selected(&C)), 9);
    EQ("Home -> 0", (key(MC_K_HOME), mcu_selected(&C)), 0);
    EQ("  top 0", C.ui.top, 0);
    EQ("Return in list -> PLAY", key(MC_K_RETURN), MC_ACT_PLAY);
    EQ("  play() called, Play flashed", A.play * 100 + A.last_flash, 100 + W_PLAY);
    EQ("Enter -> PLAY", key(MC_K_ENTER), MC_ACT_PLAY);
    EQ("Cmd-P -> PLAY", cmd('p'), MC_ACT_PLAY);
    EQ("Cmd-Shift-P (upper case) -> PLAY", mcu_key(&C, 'P', MC_MOD_CMD | MC_MOD_SHIFT), MC_ACT_PLAY);
    EQ("  play() x4", A.play, 4);
    EQ("Esc on results -> nothing (never quit)", key(MC_K_ESC), MC_ACT_NONE);
    EQ("Cmd-[ with Back disabled -> nothing", cmd('['), MC_ACT_NONE);
    EQ("typing in the list -> focus field + TYPE", key('k'), MC_ACT_TYPE);
    EQ("  focus field", C.ui.focus, MC_FOC_FIELD);
    EQ("  TE active again", A.te_focus, 1);
    EQ("  aqua = Search", mc_ui_primary(&C.ui), W_SEARCH);
    EQ("Enter from the field -> PLAY (anywhere)", key(MC_K_ENTER), MC_ACT_PLAY);
    EQ("Down from the field moves the list", (key(MC_K_DOWN), mcu_selected(&C)), 1);
    EQ("  focus stays in the field", C.ui.focus, MC_FOC_FIELD);
    EQ("Return in the field -> SEARCH", key(MC_K_RETURN), MC_ACT_SEARCH);
    EQ("Tab, Tab -> back in field", (key(MC_K_TAB), key(MC_K_TAB), C.ui.focus), MC_FOC_FIELD);
    EQ("Cmd-C -> COPY to TE", cmd('c'), MC_ACT_COPY);
    EQ("Cmd-V -> PASTE", cmd('v'), MC_ACT_PASTE);
    EQ("Cmd-X -> CUT", cmd('x'), MC_ACT_CUT);
    EQ("Cmd-A -> SELECT ALL", cmd('a'), MC_ACT_SELALL);
    EQ("  te_edit x4", A.edits, 4);
    EQ("Edit menu Copy from the list -> focus field", (key(MC_K_TAB), mcu_edit(&C, MC_ACT_COPY), C.ui.focus), MC_FOC_FIELD);
    EQ("More results button -> search(1)", press(W_MORE), MC_ACT_SEARCH);
    EQ("  more = 1", A.more, 1);
    mcu_show_results(&C, &gS, "star trek", 2, -1);
    STR("page 2 head right", C.ui.head_right, "412 items \xE1 26-50");
    results(7);
    EQ("short page: More disabled", en(W_MORE), 0);
    EQ("Tab to a 7-row list, End -> 6, top 0", (key(MC_K_TAB), key(MC_K_END), C.ui.top * 100 + mcu_selected(&C)), 6);
}

static void t_files(void)
{
    McRow *r;
    printf("-- files: Back, Esc, stutter mark\n");
    fresh();
    results(25);
    key(MC_K_TAB);
    files();
    EQ("screen FILES", C.ui.screen, MC_SCR_FILES);
    STR("heading = item title", C.ui.heading, "Jimmy And The Federation Gang");
    STR("head right", C.ui.head_right, "30 video files");
    EQ("Back enabled", en(W_BACK), 1);
    EQ("More disabled", en(W_MORE), 0);
    EQ("row 0 selected", mcu_selected(&C), 0);
    r = &C.ui.rows[2];
    EQ("checked + stutter note: orange dot", mc_ui_row_dot(r), MC_DOT_WARN);
    STR("  mark", mc_ui_row_mark(r), "may stutter");
    r = &C.ui.rows[3];
    EQ("will-not-play + stutter: stays red", mc_ui_row_dot(r), MC_DOT_NO);
    STR("  mark", mc_ui_row_mark(r), "needs newer decoder");
    EQ("guess row: hollow green", mc_ui_row_dot(&C.ui.rows[0]), MC_DOT_OK_GUESS);
    EQ("newer-decoder guess: hollow red", mc_ui_row_dot(&C.ui.rows[4]), MC_DOT_NO_GUESS);
    STR("row text = label", C.ui.rows[0].text, "1x01 - Episode 1");
    STR("row size", C.ui.rows[0].size, "298.7 MB");
    gM.files[0].state = MC_STATE_PLAYS; gM.files[0].note = MC_FILE_NOTE_STUTTER;
    mcu_update_file(&C, &gM, 0);
    EQ("Play verdict arrives: row 0 turns orange", mc_ui_row_dot(&C.ui.rows[0]), MC_DOT_WARN);
    gM.files[1].state = MC_STATE_PLAYS;
    mcu_update_file(&C, &gM, 1);
    EQ("plain PLAYS NOW: green", mc_ui_row_dot(&C.ui.rows[1]), MC_DOT_OK);
    STR("  mark", mc_ui_row_mark(&C.ui.rows[1]), "PLAYS NOW");
    EQ("Esc on files -> BACK", key(MC_K_ESC), MC_ACT_BACK);
    EQ("Cmd-. on files -> BACK", cmd('.'), MC_ACT_BACK);
    EQ("Cmd-[ -> BACK", cmd('['), MC_ACT_BACK);
    EQ("  back() x3, no quit", A.back * 10 + A.quit, 30);
    EQ("Back button -> back()", press(W_BACK), MC_ACT_BACK);
    mcu_set_confirm(&C, 1);
    mcu_status(&C, "This film is 298 MB: about 12 minutes to download. Click Play again to download it.");
    EQ("big-download question: warn glyph", C.ui.glyph, MC_GLYPH_WARN);
    EQ("Esc -> CANCEL the question", key(MC_K_ESC), MC_ACT_CANCEL);
    EQ("  cancel() called, confirm cleared", A.cancel * 10 + C.ui.confirm, 10);
    EQ("  glyph gone", C.ui.glyph, MC_GLYPH_NONE);
    EQ("Esc again -> BACK", key(MC_K_ESC), MC_ACT_BACK);
}

static void t_busy(void)
{
    int hit;
    printf("-- busy: only Stop (and Quit) work\n");
    fresh();
    results(25);
    key(MC_K_TAB);
    mcu_set_busy(&C, 1);
    EQ("busy: Stop enabled", en(W_STOP), 1);
    EQ("busy: Search/Play/More/field/list disabled",
       en(W_SEARCH) + en(W_PLAY) + en(W_MORE) + en(W_FIELD) + en(W_LIST), 0);
    EQ("busy: typing ignored", key('z'), MC_ACT_NONE);
    EQ("  te_key not called", A.te_keys, 0);
    EQ("busy: Return ignored", key(MC_K_RETURN), MC_ACT_NONE);
    EQ("busy: Down ignored", key(MC_K_DOWN), MC_ACT_NONE);
    EQ("busy: Cmd-V ignored", cmd('v'), MC_ACT_NONE);
    EQ("busy: Edit menu ignored", mcu_edit(&C, MC_ACT_PASTE), MC_ACT_NONE);
    EQ("busy: Esc -> STOP", key(MC_K_ESC), MC_ACT_STOP);
    EQ("busy: Cmd-. -> STOP", cmd('.'), MC_ACT_STOP);
    EQ("  stop() x2, quit 0", A.stop * 10 + A.quit, 20);
    EQ("busy: click Play -> nothing", click(W_PLAY, 0, &hit), MCU_CLICK_NONE);
    EQ("busy: click list -> nothing", click(W_LIST, 0, &hit), MCU_CLICK_NONE);
    EQ("busy: click field -> nothing", click(W_FIELD, 0, &hit), MCU_CLICK_NONE);
    EQ("busy: click metal -> drag", mcu_click(&C, 240, 318, 0, 0, &hit, &hit), MCU_CLICK_DRAG);
    EQ("busy: Stop tracks", click(W_STOP, 0, &hit), MCU_CLICK_TRACK);
    mcu_track(&C, W_STOP, 1);
    EQ("  pressed look", C.ui.pressed, W_STOP);
    mcu_track(&C, W_STOP, 0);
    EQ("  dragged off: not pressed", C.ui.pressed, W_NONE);
    EQ("  released outside: nothing", mcu_release(&C, W_STOP, 0), MC_ACT_NONE);
    EQ("  stop still 2", A.stop, 2);
    EQ("busy: Stop pressed+released -> STOP", press(W_STOP), MC_ACT_STOP);
    EQ("  stop() 3", A.stop, 3);
    mcu_progress(&C, 52428800UL, 104857600UL, 600UL);
    STR("download head", C.ui.lcd_head, "Downloading 50%");
    EQ("  bar 500", C.ui.progress, 500);
    STR("  KB/s", C.ui.lcd_right, "5120 KB/s");
    STR("  detail", C.ui.lcd_detail, "50.0 of 100.0 MB \xE1 under a minute left");
    mcu_progress(&C, 10485760UL, 314572800UL, 600UL);
    STR("  slower film: minutes left", C.ui.lcd_detail, "10.0 of 300.0 MB \xE1 about 5 min left");
    mcu_status(&C, "Downloading: 10240 of 307200 KB (3%). Cmd-period stops.");
    STR("  status line does not override the gauge", C.ui.lcd_head, "Downloading 3%");
    mcu_detail(&C, "  Receiving 307200 KB ...");
    STR("  log lines do not override the gauge", C.ui.lcd_detail, "10.0 of 300.0 MB \xE1 about 5 min left");
    mcu_set_busy(&C, 0);
    EQ("idle again: bar gone", C.ui.progress, -1);
    STR("  KB/s gone", C.ui.lcd_right, "");
    EQ("  Search enabled", en(W_SEARCH), 1);
    EQ("  Stop disabled", en(W_STOP), 0);
    EQ("  list focus kept: TE stays off", A.te_focus, 0);
    EQ("busy Cmd-Q, nothing streams -> QUIT", (mcu_set_busy(&C, 1), cmd('q')), MC_ACT_QUIT);
    EQ("  quit() once", A.quit, 1);
}

static void t_stream(void)
{
    int hit;
    printf("-- streaming: LCD gauge, Quit twice, Esc never quits\n");
    fresh();
    results(25);
    key(MC_K_TAB);
    files();
    mcu_detail(&C, "proxy: proxy #1: upstream https://archive.org/download/X/1x02.mp4 (0-)");
    mcu_detail(&C, "proxy: proxy #1: upstream https://dn721300.ca.archive.org/0/items/X/1x02.mp4 (0-)");
    STR("upstream host sniffed from proxy lines", C.upstream, "dn721300.ca.archive.org");
    STR("  proxy lines not shown", C.ui.lcd_detail, "");
    mcu_set_busy(&C, 1);
    mcu_set_serving(&C, 1);
    A.serving = 1;
    mcu_status(&C, "Buffering 64%");
    STR("buffer head", C.ui.lcd_head, "Streaming via secure proxy \xE1 buffering 64%");
    EQ("  gauge 640", C.ui.progress, 640);
    EQ("  busy glyph", C.ui.glyph, MC_GLYPH_BUSY);
    STR("  detail + host", C.ui.lcd_detail, "QuickTime Player opens it at 100% \xE1 dn721300.ca.archive.org (https)");
    mcu_detail(&C, "proxy: PREBUF 2274432 bytes");
    mcu_detail(&C, "Buffered the first 2221 KB of 305914 KB in 309 ticks.");
    STR("  log lines do not disturb the gauge", C.ui.lcd_detail, "QuickTime Player opens it at 100% \xE1 dn721300.ca.archive.org (https)");
    mcu_set_busy(&C, 0);
    EQ("hand-off done, still serving: Stop enabled", en(W_STOP), 1);
    EQ("  Play enabled again", en(W_PLAY), 1);
    mcu_status(&C, "Streaming via secure proxy: 212 KB/s, 12.3 MB sent, 1 connection.");
    STR("stream head", C.ui.lcd_head, "Streaming to QuickTime Player.");
    STR("  KB/s right", C.ui.lcd_right, "212 KB/s");
    STR("  detail", C.ui.lcd_detail, "dn721300.ca.archive.org (https) \xE1 12.3 MB sent, 1 connection.");
    EQ("  no bar", C.ui.progress, -1);
    EQ("serving (not busy): Esc -> BACK (film keeps playing), not quit", key(MC_K_ESC), MC_ACT_BACK);
    EQ("serving (not busy): Cmd-. -> BACK, not quit", cmd('.'), MC_ACT_BACK);
    EQ("  quit() never called", A.quit, 0);
    /* Quit #1 */
    mcu_idle(&C, 1000UL); A.now = 1000UL;
    EQ("Cmd-Q #1 while streaming -> no quit", cmd('q'), MC_ACT_NONE);
    EQ("  warning shown", C.ui.quit_pending, 1);
    STR("  LCD warning", C.ui.lcd_head, "Still streaming. Quit again to stop and quit.");
    EQ("  warn glyph", C.ui.glyph, MC_GLYPH_WARN);
    mcu_status(&C, "Streaming via secure proxy: 230 KB/s, 14.0 MB sent, 1 connection.");
    STR("  status tick during the warning: warning stays", C.ui.lcd_head, "Still streaming. Quit again to stop and quit.");
    EQ("Esc during the warning -> BACK, never quit", key(MC_K_ESC), MC_ACT_BACK);
    EQ("  warning dropped", C.ui.quit_pending, 0);
    STR("  LCD restored with the newest figure", C.ui.lcd_right, "230 KB/s");
    EQ("  quit() once so far", A.quit, 1);
    /* the close button = Quit (mcs's 10 s window from Quit #1 is over);
       then a 2nd Quit within 10 s */
    A.now = 1700UL; mcu_idle(&C, 1700UL);
    EQ("close button #1 -> no quit (warned)", press(W_CLOSE), MC_ACT_NONE);
    EQ("  warning", C.ui.quit_pending, 1);
    A.now = 1800UL; mcu_idle(&C, 1800UL);
    EQ("Cmd-Q #2 within 10 s -> closing, not yet quit", cmd('q'), MC_ACT_NONE);
    STR("  LCD says closing", C.ui.lcd_head, "Closing the film in QuickTime Player, then quitting.");
    EQ("  quit() 3 times", A.quit, 3);
    /* warning expiry */
    A.now = 5000UL; mcu_idle(&C, 5000UL);
    EQ("Quit again later -> warned again", (mcu_quit(&C), C.ui.quit_pending), 1);
    mcu_idle(&C, 5000UL + 601UL);
    EQ("10 s later the warning ends by itself", C.ui.quit_pending, 0);
    EQ("Quit warning, then a click on metal clears it",
       (mcu_quit(&C), mcu_click(&C, 240, 318, 0, 0, &hit, &hit), C.ui.quit_pending), 0);
    EQ("Quit warning, then a typed key clears it", (mcu_quit(&C), key('a'), C.ui.quit_pending), 0);
    mcu_quit(&C);
    mcu_set_serving(&C, 0); A.serving = 0;
    EQ("streaming ends: warning moot", C.ui.quit_pending, 0);
    STR("  LCD back to plain, no KB/s", C.ui.lcd_right, "");
    EQ("  Stop disabled", en(W_STOP), 0);
    EQ("not serving: Cmd-Q quits at once", cmd('q'), MC_ACT_QUIT);
    EQ("not serving: close button quits at once", press(W_CLOSE), MC_ACT_QUIT);
    mcu_status(&C, "Streaming via secure proxy: idle, 56.3 MB sent. Stop ends streaming.");
    STR("idle stream status: right 'idle'", C.ui.lcd_right, "idle");
}

static void t_mouse(void)
{
    int hit, part, x, y, r;
    printf("-- mouse: drag on metal, field, rows, scroller, checkboxes\n");
    fresh();
    EQ("title strip -> drag", mcu_click(&C, 240, 10, 0, 0, &hit, &part), MCU_CLICK_DRAG);
    EQ("where the collapse button was -> drag", mcu_click(&C, 31, 12, 0, 0, &hit, &part), MCU_CLICK_DRAG);
    EQ("heading -> drag", mcu_click(&C, 200, 66, 0, 0, &hit, &part), MCU_CLICK_DRAG);
    EQ("LCD -> drag", mcu_click(&C, 200, 270, 0, 0, &hit, &part), MCU_CLICK_DRAG);
    EQ("bottom metal -> drag", mcu_click(&C, 240, 352, 0, 0, &hit, &part), MCU_CLICK_DRAG);
    EQ("close button -> track", click(W_CLOSE, 0, &hit), MCU_CLICK_TRACK);
    mcu_release(&C, W_CLOSE, 0);
    EQ("  released outside: no quit", A.quit, 0);
    results(25);
    key(MC_K_TAB);
    EQ("field click -> FIELD", click(W_FIELD, 0, &hit), MCU_CLICK_FIELD);
    EQ("  focus field, TE on", C.ui.focus * 10 + A.te_focus, MC_FOC_FIELD * 10 + 1);
    EQ("row 3 click -> ROW", mcu_click(&C, 100, 78 + 3 * MC_ROW_H + 5, 0, 0, &hit, &part), MCU_CLICK_ROW);
    EQ("  row 3 selected, list focus", mcu_selected(&C) * 10 + C.ui.focus, 31);
    EQ("  TE off", A.te_focus, 0);
    EQ("double-click row 3 -> plays", (mcu_click(&C, 100, 78 + 3 * MC_ROW_H + 5, 0, 1, &hit, &part), A.play), 1);
    EQ("double-click on another row only selects", (mcu_click(&C, 100, 78 + 5 * MC_ROW_H + 5, 0, 1, &hit, &part), A.play * 10 + mcu_selected(&C)), 15);
    x = 461; y = 237;
    EQ("down arrow -> SCROLL", mcu_click(&C, x, y, 0, 0, &hit, &part), MCU_CLICK_SCROLL);
    EQ("  part DOWN", part, MC_PART_DOWN);
    for (r = 0; r < 30 && mcu_scroll(&C, MC_PART_DOWN, x, y); r++) ;
    EQ("  held: stops at the end (top 15)", C.ui.top, 15);
    EQ("  14 steps... then no more", r, 15);
    EQ("  selection untouched by scrolling", mcu_selected(&C), 5);
    EQ("scroll with the mouse moved off the arrow -> nothing", mcu_scroll(&C, MC_PART_UP, 200, 100), 0);
    y = 237 - MC_SCROLL_ARROW;
    EQ("up arrow part", (mcu_click(&C, x, y, 0, 0, &hit, &part), part), MC_PART_UP);
    for (r = 0; r < 30 && mcu_scroll(&C, MC_PART_UP, x, y); r++) ;
    EQ("  held: stops at the top", C.ui.top, 0);
    EQ("page-down area", (mcu_click(&C, x, 200, 0, 0, &hit, &part), part), MC_PART_PAGEDN);
    EQ("  one page", (mcu_scroll(&C, MC_PART_PAGEDN, x, 200), C.ui.top), 9);
    mcu_scroll(&C, MC_PART_PAGEUP, x, 100);
    EQ("thumb (top 0: thumb at the top of the track)", mcu_click(&C, x, 100, 0, 0, &hit, &part), MCU_CLICK_THUMB);
    mcu_thumb_begin(&C, 100);
    mcu_thumb_move(&C, 400);
    EQ("  dragged far down: top = max", C.ui.top, 15);
    mcu_thumb_move(&C, -300);
    EQ("  dragged far up: top = 0", C.ui.top, 0);
    mcu_thumb_move(&C, 100 + 42);
    EQ("  half the free track down: top 8 (rounded)", C.ui.top, 8);
    mcu_thumb_move(&C, 100);
    EQ("  back to the start: top 0", C.ui.top, 0);
    EQ("checkbox Download first", press(W_DLFIRST), MC_ACT_MOVED);
    EQ("  ticked, options()", C.ui.dl_first * 10 + A.opts, 11);
    EQ("checkbox again: unticked", (press(W_DLFIRST), C.ui.dl_first), 0);
    EQ("checkbox Skip certificate checks", (press(W_INSECURE), C.ui.insecure), 1);
    mcu_set_options(&C, 1, 0);
    EQ("app sets the options (hook)", C.ui.dl_first * 10 + C.ui.insecure, 10);
    EQ("Search button from the list -> focus field", (press(W_SEARCH), C.ui.focus), MC_FOC_FIELD);
    EQ("  search()", A.search, 1);
    EQ("Play button", press(W_PLAY), MC_ACT_PLAY);
    EQ("disabled Back click -> nothing", click(W_BACK, 0, &hit), MCU_CLICK_NONE);
    ctr(W_STOP, &x, &y);
    EQ("disabled Stop click -> nothing", mcu_click(&C, x, y, 0, 0, &hit, &part), MCU_CLICK_NONE);
}

static void t_activate(void)
{
    printf("-- activate / deactivate\n");
    fresh();
    results(25);
    mcu_activate(&C, 0);
    EQ("inactive: TE off", A.te_focus, 0);
    EQ("  whole window dirty", C.ui.dirty.r - C.ui.dirty.l, MC_WIN_W);
    EQ("inactive: clicks on the list do nothing (the shell activates first)",
       (mcu_click(&C, 100, 90, 0, 0, &C.thumb_y0, &C.thumb_top0)), MCU_CLICK_NONE);
    mcu_activate(&C, 1);
    EQ("active again with field focus: TE on", A.te_focus, 1);
    EQ("focus calls are edge-triggered (3)", A.te_focus_calls, 3);
}

/* mc_core's per-file verdict note (0.6): the size rule's "may stutter" */
static int verdict(int w, int h, int interlaced, int baseline)
{
    McAvcInfo in;
    char      why[220];
    memset(&in, 0, sizeof in);
    strcpy(in.codec, "avc1");
    in.profile = baseline ? 66 : 100; in.level = 31; in.width = w; in.height = h;
    in.sps = 1; in.chroma = 1; in.depth_luma = in.depth_chroma = 8;
    in.frame_mbs_only = !interlaced;
    (void)mc_avc_verdict(&in, 0, why, (long)sizeof why);
    return mc_avc_last_stutter();
}

static void t_verdict(void)
{
    printf("-- mc_core: the stutter note follows the verdict\n");
    mc_set_decoder_level(MC_DECODER_MAINHIGH);
    EQ("High 640x480: no note", verdict(640, 480, 0, 0), 0);
    EQ("High 854x480: may stutter", verdict(854, 480, 0, 0), 1);
    EQ("High 720x576 interlaced: may stutter", verdict(720, 576, 1, 0), 1);
    EQ("High 1280x720 (too big): no note", verdict(1280, 720, 0, 0), 0);
    EQ("Baseline 854x480: no note (0.3 rule)", verdict(854, 480, 0, 1), 0);
    EQ("note reset by the next verdict", (verdict(854, 480, 0, 0), verdict(320, 240, 0, 0)), 0);
    mc_set_decoder_level(MC_DECODER_BASELINE);
}

int main(void)
{
    t_start();
    t_results();
    t_files();
    t_busy();
    t_stream();
    t_mouse();
    t_activate();
    t_verdict();
    printf("mc_ui_test: %d passed, %d failed\n", checks - fails, fails);
    return fails != 0;
}
