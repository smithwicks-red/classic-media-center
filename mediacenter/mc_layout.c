/*
    mc_layout.c -- Classic Media Center 0.6 UI model (U1b + U3: no
    collapse button). See mc_layout.h.
*/
#include <string.h>
#include "mc_layout.h"

/* ---------------- the widget table (l, t, r, b) at 480x360 ---------------- */
static const McRect kRects[W_COUNT] =
{
    {   0,   0, 480,  24 },     /* W_TITLE    */
    {   9,   6,  21,  18 },     /* W_CLOSE    */
    {  25,   6,  37,  18 },     /* W_SHADE    */
    {  12,  31, 382,  53 },     /* W_FIELD    */
    { 390,  30, 468,  54 },     /* W_SEARCH   */
    {  14,  58, 466,  76 },     /* W_HEADING  */
    {  12,  77, 468, 239 },     /* W_LIST     */
    {  12, 245, 468, 291 },     /* W_LCD      */
    {  14, 298, 126, 313 },     /* W_DLFIRST  (box + label) */
    { 134, 298, 356, 313 },     /* W_INSECURE */
    {  12, 321,  80, 345 },     /* W_BACK     */
    {  86, 321, 186, 345 },     /* W_MORE     */
    { 300, 321, 372, 345 },     /* W_STOP     */
    { 384, 321, 468, 345 }      /* W_PLAY     */
};

static const char *const kLabels[W_COUNT] =
{
    "Classic Media Center", "", "", "", "Search", "", "", "",
    "Download first", "Skip certificate checks (unsafe)",
    "Back", "More results", "Stop", "Play"
};

/* inside the list well: rows on the left, the scroller on the right */
#define LIST_IN_L 13
#define LIST_IN_T 78
#define LIST_IN_R 467
#define LIST_IN_B 238
#define SCROLL_W  12

/* the LCD's insides */
#define LCD_GLYPH_L 20
#define LCD_GLYPH_T 252
#define LCD_TEXT_L  22
#define LCD_TEXT_LG 42

static void copy(char *d, const char *s, size_t n)
{
    size_t k = strlen(s);
    if (k >= n) k = n - 1;
    memcpy(d, s, k);
    d[k] = 0;
}

const McRect *mc_ui_rect(int w)
{
    return &kRects[w];
}

void mc_ui_dirty(McUi *ui, int w)
{
    McRect r = kRects[w];
    /* glow and shadows reach 3 px out */
    r.l -= 4; r.t -= 4; r.r += 4; r.b += 4;
    if (w == W_TITLE) { r.l = 0; r.t = 0; r.r = MC_WIN_W; r.b = MC_TITLE_H + 2; }
    mc_rect_union(&ui->dirty, &r);
}

void mc_ui_dirty_all(McUi *ui)
{
    mc_rect_set(&ui->dirty, 0, 0, MC_WIN_W, MC_WIN_H);
}

void mc_ui_init(McUi *ui)
{
    memset(ui, 0, sizeof *ui);
    ui->screen = MC_SCR_SEARCH;
    ui->active = 1;
    ui->focus = MC_FOC_FIELD;
    ui->sel = -1;
    ui->progress = -1;
    ui->pressed = W_NONE;
    mc_ui_sync(ui);
    mc_ui_dirty_all(ui);
}

int mc_ui_primary(const McUi *ui)
{
    if (ui->screen == MC_SCR_SEARCH || ui->focus == MC_FOC_FIELD) return W_SEARCH;
    return W_PLAY;
}

static void setflag(McUi *ui, int w, int bit, int on)
{
    unsigned char old = ui->flags[w];
    if (on) ui->flags[w] |= (unsigned char)bit;
    else ui->flags[w] &= (unsigned char)~bit;
    if (old != ui->flags[w]) mc_ui_dirty(ui, w);
}

void mc_ui_sync(McUi *ui)
{
    int idle = !ui->busy;
    int prim = mc_ui_primary(ui);
    int i;
    /* 0.4: UpdateButtons + Busy; while busy only Stop (CbIdle) works */
    setflag(ui, W_TITLE,   MC_WF_ENABLED, 1);
    setflag(ui, W_CLOSE,   MC_WF_ENABLED, 1);
    setflag(ui, W_SHADE,   MC_WF_ENABLED, 1);
    setflag(ui, W_HEADING, MC_WF_ENABLED, 1);
    setflag(ui, W_LCD,     MC_WF_ENABLED, 1);
    setflag(ui, W_FIELD,   MC_WF_ENABLED, idle);
    setflag(ui, W_SEARCH,  MC_WF_ENABLED, idle);
    setflag(ui, W_LIST,    MC_WF_ENABLED, idle);
    setflag(ui, W_DLFIRST, MC_WF_ENABLED, idle);
    setflag(ui, W_INSECURE, MC_WF_ENABLED, idle);
    setflag(ui, W_PLAY,    MC_WF_ENABLED, idle && ui->screen != MC_SCR_SEARCH
                                          && ui->nrows > 0);
    setflag(ui, W_MORE,    MC_WF_ENABLED, idle && ui->screen == MC_SCR_RESULTS
                                          && ui->more_avail);
    setflag(ui, W_BACK,    MC_WF_ENABLED, idle && ui->screen == MC_SCR_FILES);
    setflag(ui, W_STOP,    MC_WF_ENABLED, ui->busy || ui->serving);
    setflag(ui, W_DLFIRST, MC_WF_CHECKED, ui->dl_first);
    setflag(ui, W_INSECURE, MC_WF_CHECKED, ui->insecure);
    for (i = 0; i < W_COUNT; i++)
        setflag(ui, i, MC_WF_PRESSED, i == ui->pressed);
    /* the aqua button may have moved */
    mc_ui_dirty(ui, W_SEARCH);
    mc_ui_dirty(ui, W_PLAY);
    (void)prim;
}

/* ---------------- list maths ---------------- */
void mc_ui_list_rows_rect(McRect *r)
{
    mc_rect_set(r, LIST_IN_L, LIST_IN_T, LIST_IN_R - SCROLL_W, LIST_IN_B);
}

static void scroller_rect(McRect *r)
{
    mc_rect_set(r, LIST_IN_R - SCROLL_W, LIST_IN_T, LIST_IN_R, LIST_IN_B);
}

void mc_ui_scroller_rect(McRect *r)        /* (0.6) for thumb drags */
{
    scroller_rect(r);
}

void mc_ui_row_rect(const McUi *ui, int row, McRect *r)
{
    int k = row - ui->top;
    if (row < 0 || row >= ui->nrows || k < 0 || k >= MC_LIST_ROWS)
    {
        mc_rect_set(r, 0, 0, 0, 0);
        return;
    }
    mc_rect_set(r, LIST_IN_L, LIST_IN_T + k * MC_ROW_H,
                LIST_IN_R - SCROLL_W, LIST_IN_T + (k + 1) * MC_ROW_H);
}

int mc_ui_max_top(const McUi *ui)
{
    return ui->nrows > MC_LIST_ROWS ? ui->nrows - MC_LIST_ROWS : 0;
}

void mc_ui_scroll(McUi *ui, int newTop)
{
    if (newTop > mc_ui_max_top(ui)) newTop = mc_ui_max_top(ui);
    if (newTop < 0) newTop = 0;
    if (newTop != ui->top)
    {
        ui->top = newTop;
        mc_ui_dirty(ui, W_LIST);
    }
}

void mc_ui_select(McUi *ui, int row)
{
    McRect r;
    if (row >= ui->nrows) row = ui->nrows - 1;
    if (row < -1) row = -1;
    if (row == ui->sel) return;
    mc_ui_row_rect(ui, ui->sel, &r);
    mc_rect_union(&ui->dirty, &r);
    ui->sel = row;
    if (row >= 0)
    {
        if (row < ui->top) mc_ui_scroll(ui, row);
        else if (row >= ui->top + MC_LIST_ROWS) mc_ui_scroll(ui, row - MC_LIST_ROWS + 1);
    }
    mc_ui_row_rect(ui, ui->sel, &r);
    mc_rect_union(&ui->dirty, &r);
}

int mc_ui_list_key(McUi *ui, int key)
{
    int old = ui->sel, s = ui->sel;
    if (ui->nrows <= 0) return 0;
    switch (key)
    {
    case 1: s = s < 0 ? 0 : s - 1; break;
    case 2: s = s < 0 ? 0 : s + 1; break;
    case 3: s = (s < 0 ? 0 : s) - (MC_LIST_ROWS - 1); break;
    case 4: s = (s < 0 ? 0 : s) + (MC_LIST_ROWS - 1); break;
    case 5: s = 0; break;
    case 6: s = ui->nrows - 1; break;
    default: return 0;
    }
    if (s < 0) s = 0;
    if (s >= ui->nrows) s = ui->nrows - 1;
    mc_ui_select(ui, s);
    return ui->sel != old;
}

static int inrect(const McRect *r, int x, int y)
{
    return x >= r->l && x < r->r && y >= r->t && y < r->b;
}

int mc_ui_hit(const McUi *ui, int x, int y, int *part, int *row)
{
    int    w;
    McRect s, th, rr;
    *part = MC_PART_ROW;
    *row = -1;
    for (w = W_CLOSE; w < W_COUNT; w++)
    {
        if (w == W_HEADING || w == W_LCD || w == W_SHADE)
            continue;                                   /* metal: drag */
        if (!inrect(&kRects[w], x, y)) continue;
        if (w == W_LIST)
        {
            scroller_rect(&s);
            if (inrect(&s, x, y))
            {
                if (y >= s.b - MC_SCROLL_ARROW) *part = MC_PART_DOWN;
                else if (y >= s.b - 2 * MC_SCROLL_ARROW) *part = MC_PART_UP;
                else
                {
                    mc_skin_thumb(&s, ui->nrows, MC_LIST_ROWS, ui->top, &th);
                    if (th.b <= th.t) *part = MC_PART_ROW;   /* no thumb */
                    else if (y < th.t) *part = MC_PART_PAGEUP;
                    else if (y >= th.b) *part = MC_PART_PAGEDN;
                    else *part = MC_PART_THUMB;
                }
                return w;
            }
            mc_ui_list_rows_rect(&rr);
            if (inrect(&rr, x, y))
            {
                int k = ui->top + (y - LIST_IN_T) / MC_ROW_H;
                if (k < ui->nrows) *row = k;
            }
        }
        return w;
    }
    return W_NONE;
}

/* ---------------- notes -> dot and mark ---------------- */
int mc_ui_row_dot(const McRow *r)
{
    if ((r->note == MC_NOTE_STUTTER || r->note == MC_NOTE_HEAVY)
        && r->dot != MC_DOT_NO && r->dot != MC_DOT_NO_GUESS)
        return MC_DOT_WARN;
    return r->dot;
}

/* (0.6.1) the mark column is 64 px of Geneva 10: "may not play?" and
   "needs newer decoder?" were cut to "may not pl..." on the guest */
const char *mc_ui_row_mark(const McRow *r)
{
    if (r->note == MC_NOTE_HEAVY && r->dot != MC_DOT_NO && r->dot != MC_DOT_NO_GUESS)
        return "too heavy";   /* (0.6.1) G3 only; "too heavy (G3)" (14) would be cut like "may not play?" (13) */
    if (r->note == MC_NOTE_STUTTER && r->dot != MC_DOT_NO && r->dot != MC_DOT_NO_GUESS)
        return "may stutter";
    if (strcmp(r->mark, "may not play?") == 0) return "may fail?";
    if (strcmp(r->mark, "needs newer decoder?") == 0) return "old decoder?";
    if (strcmp(r->mark, "needs newer decoder") == 0) return "old decoder";
    return r->mark;
}

/* ---------------- keyboard ---------------- */
int mc_ui_act_button(int act)
{
    switch (act)
    {
    case MC_ACT_SEARCH: return W_SEARCH;
    case MC_ACT_PLAY:   return W_PLAY;
    case MC_ACT_BACK:   return W_BACK;
    case MC_ACT_STOP:   return W_STOP;
    default:            return W_NONE;
    }
}

static void set_focus(McUi *ui, int f)
{
    if (ui->focus == f) return;
    ui->focus = f;
    mc_ui_dirty(ui, W_FIELD);
    mc_ui_dirty(ui, W_LIST);
    mc_ui_dirty(ui, W_SEARCH);
    mc_ui_dirty(ui, W_PLAY);
}

static void forget_quit(McUi *ui)
{
    if (ui->quit_pending)
    {
        ui->quit_pending = 0;
        ui->glyph = MC_GLYPH_NONE;
        ui->lcd_head[0] = 0;       /* the shell restores its status line */
        ui->lcd_detail[0] = 0;
        mc_ui_dirty(ui, W_LCD);
    }
}

int mc_ui_quit_request(McUi *ui)
{
    if (ui->serving && !ui->quit_pending)
    {
        ui->quit_pending = 1;
        copy(ui->lcd_head, "Still streaming. Quit again to stop and quit.", sizeof ui->lcd_head);
        copy(ui->lcd_detail,
             "Quitting now cuts QuickTime off mid-film. Press Stop first, or Quit again.",
             sizeof ui->lcd_detail);
        ui->lcd_right[0] = 0;
        ui->progress = -1;
        ui->glyph = MC_GLYPH_WARN;
        mc_ui_dirty(ui, W_LCD);
        return 0;
    }
    return 1;
}

void mc_ui_note_stop(McUi *ui)
{
    forget_quit(ui);
    ui->confirm = 0;
}

int mc_ui_key(McUi *ui, int ch, int mods)
{
    int cmd = (mods & MC_MOD_CMD) != 0;
    int act;
    int running = ui->busy;   /* serving only: Esc/Cmd-. = Back, as 0.5u */

    if (cmd)
    {
        if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
        if (ch == 'q')
            return mc_ui_quit_request(ui) ? MC_ACT_QUIT : MC_ACT_NONE;
    }
    /* any key but a second Quit forgets the quit warning */
    forget_quit(ui);

    /* cancel: Esc, Cmd-. : stop / cancel the question / back. Never quit */
    if ((!cmd && ch == MC_K_ESC) || (cmd && ch == '.'))
    {
        if (running) { mc_ui_note_stop(ui); return MC_ACT_STOP; }
        if (ui->confirm) { ui->confirm = 0; return MC_ACT_CANCEL; }
        if (ui->screen == MC_SCR_FILES && (ui->flags[W_BACK] & MC_WF_ENABLED))
            return MC_ACT_BACK;
        return MC_ACT_NONE;
    }
    /* while busy only Stop (and Quit, above) work */
    if (ui->busy) return MC_ACT_NONE;

    if (cmd)
    {
        switch (ch)
        {
        case 'p': act = (ui->flags[W_PLAY] & MC_WF_ENABLED) ? MC_ACT_PLAY : MC_ACT_NONE; return act;
        case '[': return (ui->flags[W_BACK] & MC_WF_ENABLED) ? MC_ACT_BACK : MC_ACT_NONE;
        case 'x': return MC_ACT_CUT;
        case 'c': return MC_ACT_COPY;
        case 'v': return MC_ACT_PASTE;
        case 'a': return MC_ACT_SELALL;
        default:  return MC_ACT_NONE;
        }
    }
    switch (ch)
    {
    case MC_K_ENTER:
        return (ui->flags[W_PLAY] & MC_WF_ENABLED) ? MC_ACT_PLAY : MC_ACT_NONE;
    case MC_K_RETURN:
        if (ui->focus == MC_FOC_LIST)
            return (ui->flags[W_PLAY] & MC_WF_ENABLED) ? MC_ACT_PLAY : MC_ACT_NONE;
        return MC_ACT_SEARCH;
    case MC_K_TAB:
        if (ui->nrows > 0 || ui->focus == MC_FOC_LIST)
            set_focus(ui, ui->focus == MC_FOC_FIELD ? MC_FOC_LIST : MC_FOC_FIELD);
        if (ui->focus == MC_FOC_LIST && ui->sel < 0 && ui->nrows > 0)
            mc_ui_select(ui, 0);
        return MC_ACT_MOVED;
    case MC_K_UP:   return mc_ui_list_key(ui, 1) ? MC_ACT_MOVED : MC_ACT_NONE;
    case MC_K_DOWN: return mc_ui_list_key(ui, 2) ? MC_ACT_MOVED : MC_ACT_NONE;
    case MC_K_PGUP: return mc_ui_list_key(ui, 3) ? MC_ACT_MOVED : MC_ACT_NONE;
    case MC_K_PGDN: return mc_ui_list_key(ui, 4) ? MC_ACT_MOVED : MC_ACT_NONE;
    case MC_K_HOME:
    case MC_K_END:
        if (ui->focus == MC_FOC_LIST)
            return mc_ui_list_key(ui, ch == MC_K_HOME ? 5 : 6) ? MC_ACT_MOVED : MC_ACT_NONE;
        return MC_ACT_TYPE;            /* the field: line start / end  */
    case MC_K_LEFT: case MC_K_RIGHT:
        if (ui->focus == MC_FOC_LIST) return MC_ACT_NONE;
        return MC_ACT_TYPE;
    default:
        break;
    }
    /* anything else is typing: it goes to the field */
    set_focus(ui, MC_FOC_FIELD);
    return MC_ACT_TYPE;
}

/* ---------------- pixels ---------------- */
static int st(const McUi *ui, int w)
{
    if (!(ui->flags[w] & MC_WF_ENABLED)) return MC_ST_DISABLED;
    if (ui->flags[w] & MC_WF_PRESSED) return MC_ST_PRESSED;
    return MC_ST_NORMAL;
}

static int touches(const McRect *clip, int w)
{
    McRect r = kRects[w], o;
    r.l -= 4; r.t -= 4; r.r += 4; r.b += 4;
    return mc_rect_sect(&r, clip, &o);
}

static void checkbox_box(int w, McRect *b)
{
    mc_rect_set(b, kRects[w].l, kRects[w].t + 1, kRects[w].l + 13, kRects[w].t + 14);
}

static void render_list(const McUi *ui, McCanvas *cv)
{
    McRect s, r;
    int    i, en = (ui->flags[W_LIST] & MC_WF_ENABLED) != 0;
    mc_skin_list_well(cv, &kRects[W_LIST], ui->focus == MC_FOC_LIST && ui->active && en);
    for (i = ui->top; i < ui->nrows && i < ui->top + MC_LIST_ROWS; i++)
    {
        mc_ui_row_rect(ui, i, &r);
        if (i == ui->sel) mc_skin_selection(cv, &r, ui->active && en);
        if (ui->screen == MC_SCR_FILES && mc_ui_row_dot(&ui->rows[i]))
            mc_skin_dot(cv, 383, r.t + 8, mc_ui_row_dot(&ui->rows[i]));
    }
    scroller_rect(&s);
    mc_skin_scroller(cv, &s, ui->nrows, MC_LIST_ROWS, ui->top, en);
}

void mc_ui_render(const McUi *ui, McCanvas *cv, const McRect *clip)
{
    McRect c, b;
    int    prim = mc_ui_primary(ui);
    int    w;
    if (!mc_rect_sect(clip, &cv->clip, &c)) return;
    {
        McRect save = cv->clip;
        cv->clip = c;
        mc_skin_metal(cv, &c);
        mc_skin_frame(cv, MC_TITLE_H);
        for (w = 0; w < W_COUNT; w++)
        {
            if (!touches(&c, w)) continue;
            switch (w)
            {
            case W_CLOSE:
                mc_skin_round_button(cv, &kRects[w], 0, ui->active ? st(ui, w) : MC_ST_DISABLED);
                break;
            case W_SHADE:
                /* 0.6: no collapse button (owner decision, U3): metal only */
                break;
            case W_FIELD:
                mc_skin_field(cv, &kRects[w], ui->focus == MC_FOC_FIELD && ui->active
                              && (ui->flags[w] & MC_WF_ENABLED),
                              (ui->flags[w] & MC_WF_ENABLED) != 0);
                if (ui->draw_field_text && ui->focus == MC_FOC_FIELD && ui->active
                    && !ui->busy && ui->field[0] == 0)
                {
                    /* the host's stand-in for TextEdit's caret */
                    mc_skin_caret(cv, 23, 36, 49);
                }
                break;
            case W_SEARCH: case W_BACK: case W_MORE: case W_STOP: case W_PLAY:
                mc_skin_button(cv, &kRects[w],
                               (w == prim && ui->active && st(ui, w) != MC_ST_DISABLED)
                               ? MC_BTN_PRIMARY : MC_BTN_PLAIN, st(ui, w));
                break;
            case W_LIST:
                render_list(ui, cv);
                break;
            case W_LCD:
                mc_skin_lcd(cv, &kRects[w]);
                if (ui->glyph != MC_GLYPH_NONE)
                {
                    mc_rect_set(&b, LCD_GLYPH_L, LCD_GLYPH_T, LCD_GLYPH_L + 16, LCD_GLYPH_T + 16);
                    mc_skin_glyph(cv, &b, ui->glyph);
                }
                mc_rect_set(&b, 22, 279, 446, 286);
                mc_skin_progress(cv, &b, ui->progress);
                break;
            case W_DLFIRST: case W_INSECURE:
                checkbox_box(w, &b);
                mc_skin_checkbox(cv, &b, (ui->flags[w] & MC_WF_CHECKED) != 0, st(ui, w));
                break;
            default:
                break;
            }
        }
        cv->clip = save;
    }
}

/* ---------------- text runs ---------------- */
typedef struct { McRun *out; int n, max; const McRect *clip; } Runs;

static void run(Runs *R, const char *text, int l, int t, int r, int b, int base,
                int role, int size, int align, int v0, int v1, int v2, int emboss)
{
    McRun *u;
    McRect box, o;
    if (text == NULL || text[0] == 0 || R->n >= R->max) return;
    mc_rect_set(&box, l, t, r, b);
    if (!mc_rect_sect(&box, R->clip, &o)) return;
    u = &R->out[R->n++];
    copy(u->text, text, sizeof u->text);
    u->box = box;
    u->base = (short)base;
    u->role = (unsigned char)role;
    u->size = (unsigned char)size;
    u->align = (unsigned char)align;
    u->trunc = MC_TR_END;
    u->rgb[0] = (unsigned char)v0; u->rgb[1] = (unsigned char)v1; u->rgb[2] = (unsigned char)v2;
    u->emboss = (unsigned char)emboss;
}

static void grey(int en, int *v) { *v = en ? 20 : 150; }

int mc_ui_runs(const McUi *ui, const McRect *clip, McRun *out, int max)
{
    Runs R;
    int  w, v, i, en;
    McRect r, o;
    R.out = out; R.n = 0; R.max = max; R.clip = clip;

    /* title strip */
    run(&R, kLabels[W_TITLE], 44, 0, 436, MC_TITLE_H, 17, MC_F_TITLE, 16,
        MC_AL_CENTER, ui->active ? 34 : 120, ui->active ? 34 : 120,
        ui->active ? 36 : 122, 1);

    /* search field (host only; on the Mac TextEdit draws it) */
    if (ui->field[0] && ui->draw_field_text)
        run(&R, ui->field, 23, 34, 372, 51, 46, MC_F_UI, 12, MC_AL_LEFT, 0, 0, 0, 0);
    else if (ui->field[0] == 0)
        run(&R, "Search the Internet Archive for films", 27, 34, 372, 51, 46,
            MC_F_UI, 12, MC_AL_LEFT, 158, 158, 162, 0);

    /* heading */
    run(&R, ui->heading, 15, 56, 320, 77, 72, MC_F_HEAD, 16, MC_AL_LEFT, 30, 30, 32, 1);
    run(&R, ui->head_right, 300, 58, 465, 77, 72, MC_F_SMALL, 10, MC_AL_RIGHT, 70, 70, 74, 1);

    /* buttons */
    for (w = W_SEARCH; w < W_COUNT; w++)
    {
        const McRect *k = &kRects[w];
        if (w == W_HEADING || w == W_LIST || w == W_LCD) continue;
        en = (ui->flags[w] & MC_WF_ENABLED) != 0;
        grey(en, &v);
        if (w == W_DLFIRST || w == W_INSECURE)
            run(&R, kLabels[w], k->l + 18, k->t, k->r, k->b, k->t + 11, MC_F_UI, 11,
                MC_AL_LEFT, v, v, v, en);
        else
            run(&R, kLabels[w], k->l + 4, k->t, k->r - 4, k->b, k->t + 16, MC_F_BTN, 12,
                MC_AL_CENTER, v, v, v, 0);
    }

    /* list */
    en = (ui->flags[W_LIST] & MC_WF_ENABLED) != 0;
    if (ui->nrows == 0)
        run(&R, ui->list_hint, LIST_IN_L, LIST_IN_T, LIST_IN_R - SCROLL_W, LIST_IN_B,
            (LIST_IN_T + LIST_IN_B) / 2 + 4, MC_F_HEAD, 14, MC_AL_CENTER, 168, 168, 172, 0);
    for (i = ui->top; i < ui->nrows && i < ui->top + MC_LIST_ROWS; i++)
    {
        int sel = (i == ui->sel);
        /* busy: the list stays readable, only its selection greys */
        int hi = sel && ui->active && en;
        int tv = hi ? 255 : (ui->active ? 0 : 90);
        int sv = hi ? 236 : (ui->active ? 96 : 130);
        mc_ui_row_rect(ui, i, &r);
        if (!mc_rect_sect(&r, clip, &o)) continue;
        if (ui->screen != MC_SCR_FILES)
        {
            run(&R, ui->rows[i].text, r.l + 7, r.t, r.r - 6, r.b, r.t + 12, MC_F_UI, 12,
                MC_AL_LEFT, tv, tv, tv, 0);
            continue;
        }
        run(&R, ui->rows[i].text, r.l + 7, r.t, 262, r.b, r.t + 12, MC_F_UI, 12,
            MC_AL_LEFT, tv, tv, tv, 0);
        run(&R, ui->rows[i].fmt, 268, r.t, 318, r.b, r.t + 12, MC_F_SMALL, 10,
            MC_AL_LEFT, sv, sv, sv, 0);
        run(&R, ui->rows[i].size, 318, r.t, 372, r.b, r.t + 12, MC_F_SMALL, 10,
            MC_AL_RIGHT, sv, sv, sv, 0);
        run(&R, mc_ui_row_mark(&ui->rows[i]), 390, r.t, r.r - 2, r.b, r.t + 12, MC_F_SMALL, 10,
            MC_AL_LEFT, tv, tv, tv, 0);
    }

    /* LCD */
    {
        int tl = ui->glyph != MC_GLYPH_NONE ? LCD_TEXT_LG : LCD_TEXT_L;
        int rr = ui->lcd_right[0] ? 372 : 446;
        int hb = ui->progress >= 0 ? 261 : 263;
        if (ui->glyph == MC_GLYPH_WARN || ui->glyph == MC_GLYPH_ERROR)
            run(&R, "!", LCD_GLYPH_L, LCD_GLYPH_T, LCD_GLYPH_L + 16, LCD_GLYPH_T + 16,
                LCD_GLYPH_T + 12, MC_F_LCDB, 11, MC_AL_CENTER, 255, 255, 255, 0);
        run(&R, ui->lcd_head, tl, 247, rr, 266, hb, MC_F_LCD, 12, MC_AL_LEFT, 34, 42, 32, 0);
        run(&R, ui->lcd_right, 372, 247, 446, 266, hb, MC_F_LCDB, 12, MC_AL_RIGHT, 34, 42, 32, 0);
        run(&R, ui->lcd_detail, tl, 266, 446, 280, ui->progress >= 0 ? 275 : 279,
            MC_F_SMALL, 10, MC_AL_LEFT, 70, 82, 66, 0);
        if (R.n > 0 && ui->lcd_detail[0] && strcmp(R.out[R.n - 1].text, ui->lcd_detail) == 0)
            R.out[R.n - 1].trunc = MC_TR_MIDDLE;
    }
    return R.n;
}
