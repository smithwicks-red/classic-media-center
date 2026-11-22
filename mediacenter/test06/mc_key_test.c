/*
    mc_key_test.c -- host test of the portable key map (mc_ui_key,
    mc_ui_quit_request). C89. Exit 0 = all pass.
*/
#include <stdio.h>
#include <string.h>
#include "mc_layout.h"

static int fails = 0, checks = 0;
#define EXPECT(what, got, want) do { checks++; if ((got) != (want)) { \
    printf("FAIL %s: got %d want %d\n", what, (int)(got), (int)(want)); fails++; } } while (0)

static void results(McUi *ui)
{
    int i;
    mc_ui_init(ui);
    ui->screen = MC_SCR_RESULTS;
    for (i = 0; i < 25; i++) strcpy(ui->rows[i].text, "row");
    ui->nrows = 25; ui->sel = 0; ui->more_avail = 1;
    ui->focus = MC_FOC_LIST;
    mc_ui_sync(ui);
}

static void files(McUi *ui)
{
    results(ui);
    ui->screen = MC_SCR_FILES;
    mc_ui_sync(ui);
}

int main(void)
{
    static McUi ui;
    McRow row;

    /* Return: field = Search, list = Play */
    mc_ui_init(&ui);
    EXPECT("return in field (empty)", mc_ui_key(&ui, MC_K_RETURN, 0), MC_ACT_SEARCH);
    results(&ui);
    EXPECT("return in list = play", mc_ui_key(&ui, MC_K_RETURN, 0), MC_ACT_PLAY);
    ui.focus = MC_FOC_FIELD;
    EXPECT("return in field = search", mc_ui_key(&ui, MC_K_RETURN, 0), MC_ACT_SEARCH);
    /* Enter and Cmd-P: Play anywhere */
    EXPECT("enter in field = play", mc_ui_key(&ui, MC_K_ENTER, 0), MC_ACT_PLAY);
    EXPECT("cmd-p = play", mc_ui_key(&ui, 'p', MC_MOD_CMD), MC_ACT_PLAY);
    ui.focus = MC_FOC_LIST;
    EXPECT("enter in list = play", mc_ui_key(&ui, MC_K_ENTER, 0), MC_ACT_PLAY);
    mc_ui_init(&ui);
    EXPECT("enter with no list: nothing", mc_ui_key(&ui, MC_K_ENTER, 0), MC_ACT_NONE);
    EXPECT("cmd-p with no list: nothing", mc_ui_key(&ui, 'p', MC_MOD_CMD), MC_ACT_NONE);

    /* Tab toggles focus, only if there is a list */
    EXPECT("tab with empty list stays", (mc_ui_key(&ui, MC_K_TAB, 0), ui.focus), MC_FOC_FIELD);
    results(&ui); ui.focus = MC_FOC_FIELD;
    mc_ui_key(&ui, MC_K_TAB, 0);
    EXPECT("tab field->list", ui.focus, MC_FOC_LIST);
    mc_ui_key(&ui, MC_K_TAB, 0);
    EXPECT("tab list->field", ui.focus, MC_FOC_FIELD);
    EXPECT("aqua button follows focus (field)", mc_ui_primary(&ui), W_SEARCH);
    mc_ui_key(&ui, MC_K_TAB, 0);
    EXPECT("aqua button follows focus (list)", mc_ui_primary(&ui), W_PLAY);

    /* selection keys */
    results(&ui);
    EXPECT("down moves", mc_ui_key(&ui, MC_K_DOWN, 0), MC_ACT_MOVED);
    EXPECT("sel 1", ui.sel, 1);
    mc_ui_key(&ui, MC_K_UP, 0);
    EXPECT("up -> 0", ui.sel, 0);
    EXPECT("up at top: no move", mc_ui_key(&ui, MC_K_UP, 0), MC_ACT_NONE);
    mc_ui_key(&ui, MC_K_PGDN, 0);
    EXPECT("page down = 9 rows", ui.sel, 9);
    mc_ui_key(&ui, MC_K_PGDN, 0);
    EXPECT("page down again", ui.sel, 18);
    EXPECT("selection scrolled into view", ui.sel >= ui.top && ui.sel < ui.top + MC_LIST_ROWS, 1);
    mc_ui_key(&ui, MC_K_END, 0);
    EXPECT("end", ui.sel, 24);
    EXPECT("end: top at max", ui.top, mc_ui_max_top(&ui));
    mc_ui_key(&ui, MC_K_HOME, 0);
    EXPECT("home", ui.sel, 0);
    EXPECT("home: top 0", ui.top, 0);
    mc_ui_key(&ui, MC_K_PGUP, 0);
    EXPECT("page up at top stays 0", ui.sel, 0);
    ui.focus = MC_FOC_FIELD;
    mc_ui_key(&ui, MC_K_DOWN, 0);
    EXPECT("down in field moves selection", ui.sel, 1);
    EXPECT("home in field goes to the field", mc_ui_key(&ui, MC_K_HOME, 0), MC_ACT_TYPE);
    EXPECT("home in field keeps sel", ui.sel, 1);

    /* typing from the list jumps to the field */
    ui.focus = MC_FOC_LIST;
    EXPECT("typing = TYPE", mc_ui_key(&ui, 'a', 0), MC_ACT_TYPE);
    EXPECT("typing focuses the field", ui.focus, MC_FOC_FIELD);

    /* Esc / Cmd-. never quit */
    files(&ui);
    EXPECT("esc on files = back", mc_ui_key(&ui, MC_K_ESC, 0), MC_ACT_BACK);
    EXPECT("cmd-. on files = back", mc_ui_key(&ui, '.', MC_MOD_CMD), MC_ACT_BACK);
    results(&ui);
    EXPECT("esc on results idle: nothing (not quit)", mc_ui_key(&ui, MC_K_ESC, 0), MC_ACT_NONE);
    EXPECT("cmd-. on results idle: nothing", mc_ui_key(&ui, '.', MC_MOD_CMD), MC_ACT_NONE);
    mc_ui_init(&ui);
    EXPECT("esc on empty search: nothing", mc_ui_key(&ui, MC_K_ESC, 0), MC_ACT_NONE);
    files(&ui); ui.confirm = 1;
    EXPECT("esc cancels the big-download question", mc_ui_key(&ui, MC_K_ESC, 0), MC_ACT_CANCEL);
    EXPECT("question cleared", ui.confirm, 0);
    files(&ui); ui.busy = 1; mc_ui_sync(&ui);
    EXPECT("esc while busy = stop", mc_ui_key(&ui, MC_K_ESC, 0), MC_ACT_STOP);
    EXPECT("cmd-. while busy = stop", mc_ui_key(&ui, '.', MC_MOD_CMD), MC_ACT_STOP);
    EXPECT("busy: return ignored", mc_ui_key(&ui, MC_K_RETURN, 0), MC_ACT_NONE);
    EXPECT("busy: down ignored", mc_ui_key(&ui, MC_K_DOWN, 0), MC_ACT_NONE);
    EXPECT("busy: enter ignored", mc_ui_key(&ui, MC_K_ENTER, 0), MC_ACT_NONE);
    EXPECT("Stop enabled while busy", (ui.flags[W_STOP] & MC_WF_ENABLED) != 0, 1);
    files(&ui); ui.serving = 1; mc_ui_sync(&ui);
    EXPECT("Stop enabled while serving (not busy)", (ui.flags[W_STOP] & MC_WF_ENABLED) != 0, 1);
    EXPECT("esc while serving (not busy) = back, film keeps playing", mc_ui_key(&ui, MC_K_ESC, 0), MC_ACT_BACK);
    files(&ui);
    EXPECT("Stop disabled when idle", (ui.flags[W_STOP] & MC_WF_ENABLED) != 0, 0);

    /* Quit: Cmd-Q, idle -> quits at once */
    files(&ui);
    EXPECT("cmd-q idle = quit", mc_ui_key(&ui, 'q', MC_MOD_CMD), MC_ACT_QUIT);
    EXPECT("cmd-Q (capital) idle = quit", mc_ui_key(&ui, 'Q', MC_MOD_CMD), MC_ACT_QUIT);
    EXPECT("close button idle = quit", mc_ui_quit_request(&ui), 1);
    /* Quit while serving: warning, then second Quit */
    files(&ui); ui.serving = 1; mc_ui_sync(&ui);
    EXPECT("cmd-q serving: first = warn", mc_ui_key(&ui, 'q', MC_MOD_CMD), MC_ACT_NONE);
    EXPECT("warning shown", ui.quit_pending, 1);
    EXPECT("LCD warning glyph", ui.glyph, MC_GLYPH_WARN);
    EXPECT("LCD warning text", strstr(ui.lcd_head, "Quit again") != NULL, 1);
    EXPECT("second cmd-q quits", mc_ui_key(&ui, 'q', MC_MOD_CMD), MC_ACT_QUIT);
    files(&ui); ui.serving = 1; mc_ui_sync(&ui);
    EXPECT("close button first = warn", mc_ui_quit_request(&ui), 0);
    EXPECT("close button second = quit", mc_ui_quit_request(&ui), 1);
    files(&ui); ui.serving = 1; mc_ui_sync(&ui);
    mc_ui_quit_request(&ui);
    mc_ui_key(&ui, MC_K_DOWN, 0);
    EXPECT("another key forgets the warning", ui.quit_pending, 0);
    EXPECT("then Quit warns again", mc_ui_quit_request(&ui), 0);
    mc_ui_key(&ui, MC_K_ESC, 0);
    EXPECT("esc while serving forgets the warning (and stops)", ui.quit_pending, 0);
    files(&ui); ui.serving = 1; ui.busy = 1; mc_ui_sync(&ui);
    EXPECT("cmd-q while busy+serving warns", mc_ui_key(&ui, 'q', MC_MOD_CMD), MC_ACT_NONE);
    EXPECT("  and then quits", mc_ui_key(&ui, 'q', MC_MOD_CMD), MC_ACT_QUIT);

    /* other Cmd keys */
    files(&ui);
    EXPECT("cmd-[ = back", mc_ui_key(&ui, '[', MC_MOD_CMD), MC_ACT_BACK);
    EXPECT("cmd-c", mc_ui_key(&ui, 'c', MC_MOD_CMD), MC_ACT_COPY);
    EXPECT("cmd-v", mc_ui_key(&ui, 'v', MC_MOD_CMD), MC_ACT_PASTE);
    EXPECT("cmd-x", mc_ui_key(&ui, 'x', MC_MOD_CMD), MC_ACT_CUT);
    EXPECT("cmd-a", mc_ui_key(&ui, 'a', MC_MOD_CMD), MC_ACT_SELALL);
    results(&ui);
    EXPECT("cmd-[ on results: nothing", mc_ui_key(&ui, '[', MC_MOD_CMD), MC_ACT_NONE);

    /* act -> button flash */
    EXPECT("flash play", mc_ui_act_button(MC_ACT_PLAY), W_PLAY);
    EXPECT("flash search", mc_ui_act_button(MC_ACT_SEARCH), W_SEARCH);
    EXPECT("flash stop", mc_ui_act_button(MC_ACT_STOP), W_STOP);
    EXPECT("no flash for type", mc_ui_act_button(MC_ACT_TYPE), W_NONE);

    /* per-file note */
    memset(&row, 0, sizeof row);
    strcpy(row.mark, "plays now?"); row.dot = MC_DOT_OK_GUESS;
    EXPECT("no note: dot as is", mc_ui_row_dot(&row), MC_DOT_OK_GUESS);
    row.note = MC_NOTE_STUTTER;
    EXPECT("stutter note: orange dot", mc_ui_row_dot(&row), MC_DOT_WARN);
    EXPECT("stutter note: mark", strcmp(mc_ui_row_mark(&row), "may stutter"), 0);
    row.dot = MC_DOT_NO; strcpy(row.mark, "will not play");
    EXPECT("stutter never overrides will-not-play (dot)", mc_ui_row_dot(&row), MC_DOT_NO);
    EXPECT("stutter never overrides will-not-play (mark)", strcmp(mc_ui_row_mark(&row), "will not play"), 0);

    printf("%d checks, %d failed\n", checks, fails);
    return fails != 0;
}
