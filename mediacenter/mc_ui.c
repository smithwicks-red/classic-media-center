/*
    mc_ui.c -- Classic Media Center 0.6: the portable controller of the
    skinned window. See mc_ui.h. C89, no Toolbox calls, no floating
    point; strings are MacRoman (0xD2/0xD3 curly quotes, 0xE1 middle dot).
*/
#include <stdio.h>
#include <string.h>
#include "mc_ui.h"

static void cpy(char *d, const char *s, size_t n)
{
    size_t k = strlen(s);
    if (k >= n) k = n - 1;
    memcpy(d, s, k);
    d[k] = 0;
}

static int prefix(const char *s, const char *p)
{
    return strncmp(s, p, strlen(p)) == 0;
}

/* ---------------- LCD ---------------- */
static void lcd_set(McCtl *c, const char *head, const char *detail,
                    const char *right, int progress, int glyph)
{
    McUi *ui = &c->ui;
    if (head != NULL) cpy(ui->lcd_head, head, sizeof ui->lcd_head);
    if (detail != NULL) cpy(ui->lcd_detail, detail, sizeof ui->lcd_detail);
    if (right != NULL) cpy(ui->lcd_right, right, sizeof ui->lcd_right);
    ui->progress = progress;
    ui->glyph = glyph;
    mc_ui_dirty(ui, W_LCD);
}

static void save_lcd(McCtl *c)
{
    cpy(c->sv_head, c->ui.lcd_head, sizeof c->sv_head);
    cpy(c->sv_detail, c->ui.lcd_detail, sizeof c->sv_detail);
    cpy(c->sv_right, c->ui.lcd_right, sizeof c->sv_right);
    c->sv_progress = c->ui.progress;
    c->sv_glyph = c->ui.glyph;
    c->sv_mode = c->lcd_mode;
}

static void restore_lcd(McCtl *c)
{
    lcd_set(c, c->sv_head, c->sv_detail, c->sv_right, c->sv_progress,
            c->sv_glyph);
    c->lcd_mode = c->sv_mode;
}

/* While the quit warning shows, status updates go to the saved copy
   (swap in, update, swap out), so the warning stays and nothing is lost. */
static int swap_in(McCtl *c)
{
    char h[160], d[200], r[32];
    int  p, g, m;
    if (!c->ui.quit_pending) return 0;
    cpy(h, c->ui.lcd_head, sizeof h); cpy(d, c->ui.lcd_detail, sizeof d);
    cpy(r, c->ui.lcd_right, sizeof r);
    p = c->ui.progress; g = c->ui.glyph; m = c->lcd_mode;
    restore_lcd(c);
    cpy(c->sv_head, h, sizeof c->sv_head); cpy(c->sv_detail, d, sizeof c->sv_detail);
    cpy(c->sv_right, r, sizeof c->sv_right);
    c->sv_progress = p; c->sv_glyph = g; c->sv_mode = m;
    return 1;
}

static void swap_out(McCtl *c, int swapped)
{
    if (swapped) (void)swap_in(c);   /* the same swap puts the warning back */
}

static void end_warning(McCtl *c)
{
    if (!c->ui.quit_pending) return;
    c->ui.quit_pending = 0;
    restore_lcd(c);
}

/* ---------------- focus / TextEdit ---------------- */
static void sync(McCtl *c)
{
    McUi *ui = &c->ui;
    int   want;
    mc_ui_sync(ui);
    want = ui->active && ui->focus == MC_FOC_FIELD
           && (ui->flags[W_FIELD] & MC_WF_ENABLED) != 0;
    if (want != c->te_on)
    {
        c->te_on = want;
        if (c->cb.te_focus != NULL) c->cb.te_focus(c->cb.ref, want);
    }
}

static void set_focus(McCtl *c, int f)
{
    McUi *ui = &c->ui;
    if (ui->focus == f) return;
    ui->focus = f;
    mc_ui_dirty(ui, W_FIELD);
    mc_ui_dirty(ui, W_LIST);
    mc_ui_dirty(ui, W_SEARCH);
    mc_ui_dirty(ui, W_PLAY);
}

void mcu_init(McCtl *c, const McUiCb *cb)
{
    memset(c, 0, sizeof *c);
    if (cb != NULL) c->cb = *cb;
    mc_ui_init(&c->ui);
    c->ui.draw_field_text = 0;   /* TextEdit draws the field's text */
    c->te_on = 0;
    c->page = 1;
    cpy(c->ui.list_hint, "Films from the Internet Archive", sizeof c->ui.list_hint);
    sync(c);
}

/* ---------------- actions ---------------- */
static void flash(McCtl *c, int act)
{
    int w = mc_ui_act_button(act);
    if (w != W_NONE && c->cb.flash != NULL) c->cb.flash(c->cb.ref, w);
}

static int do_act(McCtl *c, int act)
{
    McUiCb *b = &c->cb;
    switch (act)
    {
    case MC_ACT_SEARCH:
        flash(c, act);
        if (b->search != NULL) b->search(b->ref, 0);
        break;
    case MC_ACT_PLAY:
        flash(c, act);
        if (b->play != NULL) b->play(b->ref);
        break;
    case MC_ACT_BACK:
        flash(c, act);
        if (b->back != NULL) b->back(b->ref);
        break;
    case MC_ACT_STOP:
        flash(c, act);
        if (b->stop != NULL) b->stop(b->ref);
        break;
    case MC_ACT_CANCEL:
        lcd_set(c, "Download cancelled.", "", "", -1, MC_GLYPH_NONE);
        c->lcd_mode = MCU_LCD_PLAIN;
        if (b->cancel != NULL) b->cancel(b->ref);
        break;
    case MC_ACT_CUT: case MC_ACT_COPY: case MC_ACT_PASTE: case MC_ACT_SELALL:
        if (c->ui.busy) return MC_ACT_NONE;
        set_focus(c, MC_FOC_FIELD);
        sync(c);
        if (b->te_edit != NULL) b->te_edit(b->ref, act);
        break;
    default:
        break;
    }
    sync(c);
    return act;
}

int mcu_quit(McCtl *c)
{
    int r = MCU_QUIT_NOW;
    end_warning(c);                 /* a second Quit: the LCD as it was */
    save_lcd(c);
    if (c->cb.quit != NULL) r = c->cb.quit(c->cb.ref);
    if (r == MCU_QUIT_NOW) return 1;
    if (r == MCU_QUIT_CLOSING)
    {
        lcd_set(c, "Closing the film in QuickTime Player, then quitting.",
                "Media Center quits as soon as QuickTime lets go of it.", "",
                -1, MC_GLYPH_BUSY);
        c->lcd_mode = MCU_LCD_PLAIN;
        sync(c);
        return 0;
    }
    /* WARNED: the LCD warning, no alert (mcs's own status line text is
       replaced; the LCD from before the Quit comes back afterwards) */
    c->ui.quit_pending = 1;
    c->warn_t = c->now;
    lcd_set(c, "Still streaming. Quit again to stop and quit.",
            "QuickTime Player is playing a film through Media Center. Quit again within 10 s, or press Stop first.",
            "", -1, MC_GLYPH_WARN);
    sync(c);
    return 0;
}

int mcu_key(McCtl *c, int ch, int mods)
{
    int act, cmd = (mods & MC_MOD_CMD) != 0;
    if (cmd && (ch == 'q' || ch == 'Q'))
        return mcu_quit(c) ? MC_ACT_QUIT : MC_ACT_NONE;
    end_warning(c);
    act = mc_ui_key(&c->ui, ch, mods);
    sync(c);
    if (act == MC_ACT_TYPE)
    {
        if (c->ui.busy || !c->te_on) return MC_ACT_NONE;
        if (c->cb.te_key != NULL) c->cb.te_key(c->cb.ref, ch, mods);
        return act;
    }
    if (act == MC_ACT_QUIT) return MC_ACT_NONE;   /* never via mc_ui_key */
    return do_act(c, act);
}

int mcu_edit(McCtl *c, int act)
{
    end_warning(c);
    if (act != MC_ACT_CUT && act != MC_ACT_COPY && act != MC_ACT_PASTE
        && act != MC_ACT_SELALL) return MC_ACT_NONE;
    return do_act(c, act);
}

int mcu_click(McCtl *c, int x, int y, int mods, int dbl, int *w, int *part)
{
    McUi *ui = &c->ui;
    int   row, hit;
    (void)mods;
    hit = mc_ui_hit(ui, x, y, part, &row);
    *w = hit;
    if (hit != W_CLOSE) end_warning(c);
    if (hit == W_NONE || hit == W_TITLE) return MCU_CLICK_DRAG;
    if (!(ui->flags[hit] & MC_WF_ENABLED) || !ui->active)
    {
        sync(c);
        return MCU_CLICK_NONE;
    }
    switch (hit)
    {
    case W_FIELD:
        set_focus(c, MC_FOC_FIELD);
        sync(c);
        return MCU_CLICK_FIELD;
    case W_LIST:
        if (*part == MC_PART_THUMB) return MCU_CLICK_THUMB;
        if (*part != MC_PART_ROW) return MCU_CLICK_SCROLL;
        if (ui->nrows > 0) set_focus(c, MC_FOC_LIST);
        if (row >= 0)
        {
            int again = (row == ui->sel);
            mc_ui_select(ui, row);
            sync(c);
            if (dbl && again && (ui->flags[W_PLAY] & MC_WF_ENABLED))
                (void)do_act(c, MC_ACT_PLAY);
        }
        sync(c);
        return MCU_CLICK_ROW;
    default:
        ui->pressed = hit;
        sync(c);
        return MCU_CLICK_TRACK;
    }
}

void mcu_track(McCtl *c, int w, int inside)
{
    int want = inside ? w : W_NONE;
    if (c->ui.pressed != want)
    {
        c->ui.pressed = want;
        sync(c);
    }
}

int mcu_release(McCtl *c, int w, int inside)
{
    McUi *ui = &c->ui;
    ui->pressed = W_NONE;
    sync(c);
    if (!inside || !(ui->flags[w] & MC_WF_ENABLED)) return MC_ACT_NONE;
    switch (w)
    {
    case W_CLOSE:   return mcu_quit(c) ? MC_ACT_QUIT : MC_ACT_NONE;
    case W_SEARCH:
        set_focus(c, MC_FOC_FIELD);
        sync(c);
        if (c->cb.search != NULL) c->cb.search(c->cb.ref, 0);
        sync(c);
        return MC_ACT_SEARCH;
    case W_MORE:
        if (c->cb.search != NULL) c->cb.search(c->cb.ref, 1);
        sync(c);
        return MC_ACT_SEARCH;
    case W_BACK:  if (c->cb.back != NULL) c->cb.back(c->cb.ref); sync(c); return MC_ACT_BACK;
    case W_STOP:  if (c->cb.stop != NULL) c->cb.stop(c->cb.ref); sync(c); return MC_ACT_STOP;
    case W_PLAY:  if (c->cb.play != NULL) c->cb.play(c->cb.ref); sync(c); return MC_ACT_PLAY;
    case W_DLFIRST:
        ui->dl_first = !ui->dl_first;
        sync(c);
        if (c->cb.options != NULL) c->cb.options(c->cb.ref);
        return MC_ACT_MOVED;
    case W_INSECURE:
        ui->insecure = !ui->insecure;
        sync(c);
        if (c->cb.options != NULL) c->cb.options(c->cb.ref);
        return MC_ACT_MOVED;
    default:
        return MC_ACT_NONE;
    }
}

int mcu_scroll(McCtl *c, int part, int x, int y)
{
    McUi *ui = &c->ui;
    int   p, row, top = ui->top, step = MC_LIST_ROWS - 1;
    if (mc_ui_hit(ui, x, y, &p, &row) != W_LIST || p != part) return 0;
    switch (part)
    {
    case MC_PART_UP:     mc_ui_scroll(ui, top - 1); break;
    case MC_PART_DOWN:   mc_ui_scroll(ui, top + 1); break;
    case MC_PART_PAGEUP: mc_ui_scroll(ui, top - step); break;
    case MC_PART_PAGEDN: mc_ui_scroll(ui, top + step); break;
    default: return 0;
    }
    sync(c);
    return ui->top != top;
}

void mcu_thumb_begin(McCtl *c, int y)
{
    c->thumb_y0 = y;
    c->thumb_top0 = c->ui.top;
}

void mcu_thumb_move(McCtl *c, int y)
{
    McUi  *ui = &c->ui;
    McRect s, th;
    long   range, maxTop = mc_ui_max_top(ui), d, t;
    mc_ui_scroller_rect(&s);
    mc_skin_thumb(&s, ui->nrows, MC_LIST_ROWS, c->thumb_top0, &th);
    range = (long)(s.b - 2 * MC_SCROLL_ARROW - s.t) - (long)(th.b - th.t);
    if (range <= 0 || maxTop <= 0) return;
    d = (long)(y - c->thumb_y0) * maxTop;
    t = c->thumb_top0 + (d >= 0 ? (d + range / 2) / range : -((-d + range / 2) / range));
    mc_ui_scroll(ui, (int)t);
    sync(c);
}

void mcu_activate(McCtl *c, int on)
{
    on = on != 0;
    if (c->ui.active == on) return;
    c->ui.active = on;
    mc_ui_dirty_all(&c->ui);
    sync(c);
}

void mcu_idle(McCtl *c, unsigned long ticks)
{
    c->now = ticks;
    if (c->ui.quit_pending && ticks - c->warn_t > MCU_WARN_TICKS)
    {
        end_warning(c);
        sync(c);
    }
}

/* ---------------- app state ---------------- */
void mcu_set_busy(McCtl *c, int on)
{
    int sw;
    on = on != 0;
    if (c->ui.busy == on) return;
    c->ui.busy = on;
    if (!on)
    {
        sw = swap_in(c);
        if (c->lcd_mode == MCU_LCD_DOWNLOAD
            || (c->lcd_mode == MCU_LCD_BUFFER && !c->ui.serving))
        {
            c->ui.progress = -1;
            c->ui.lcd_right[0] = 0;
            if (c->ui.glyph == MC_GLYPH_BUSY) c->ui.glyph = MC_GLYPH_NONE;
            c->lcd_mode = MCU_LCD_PLAIN;
            mc_ui_dirty(&c->ui, W_LCD);
        }
        swap_out(c, sw);
    }
    sync(c);
}

void mcu_set_serving(McCtl *c, int on)
{
    int sw;
    on = on != 0;
    if (c->ui.serving == on) return;
    c->ui.serving = on;
    if (!on)
    {
        sw = swap_in(c);
        if (c->lcd_mode == MCU_LCD_STREAM || c->lcd_mode == MCU_LCD_BUFFER)
        {
            c->ui.progress = -1;
            c->ui.lcd_right[0] = 0;
            c->ui.glyph = MC_GLYPH_NONE;
            c->lcd_mode = MCU_LCD_PLAIN;
            mc_ui_dirty(&c->ui, W_LCD);
        }
        swap_out(c, sw);
        /* nothing streams any more: a pending quit warning is moot */
        end_warning(c);
    }
    sync(c);
}

void mcu_set_confirm(McCtl *c, int on)
{
    int sw = swap_in(c);
    c->ui.confirm = on != 0;
    if (on)
    {
        c->ui.glyph = MC_GLYPH_WARN;
        mc_ui_dirty(&c->ui, W_LCD);
    }
    swap_out(c, sw);
    sync(c);
}

void mcu_set_options(McCtl *c, int dl_first, int insecure)
{
    c->ui.dl_first = dl_first != 0;
    c->ui.insecure = insecure != 0;
    sync(c);
}

void mcu_set_field(McCtl *c, const char *text, long len)
{
    McUi *ui = &c->ui;
    int   was = ui->field[0] != 0;
    if (len < 0) len = 0;
    if (len > (long)sizeof ui->field - 1) len = (long)sizeof ui->field - 1;
    memcpy(ui->field, text, (size_t)len);
    ui->field[len] = 0;
    /* only the grey hint depends on it (TextEdit draws the text) */
    if (was != (ui->field[0] != 0)) mc_ui_dirty(ui, W_FIELD);
}

/* "https://host[:port]/..." after key in line -> host */
static void sniff_host(McCtl *c, const char *line)
{
    const char *p = strstr(line, "upstream https://");
    size_t n = 0;
    if (p != NULL) p += 17;
    else if ((p = strstr(line, "-> https://")) != NULL) p += 11;
    else return;
    while (p[n] && p[n] != '/' && p[n] != ':' && p[n] != ' ' && n < sizeof c->upstream - 1)
        n++;
    if (n == 0) return;
    memcpy(c->upstream, p, n);
    c->upstream[n] = 0;
}

static unsigned long num(const char **pp)
{
    unsigned long v = 0;
    const char *p = *pp;
    while (*p >= '0' && *p <= '9') v = v * 10UL + (unsigned long)(*p++ - '0');
    *pp = p;
    return v;
}

static void host_tail(McCtl *c, char *out, size_t cap, const char *lead)
{
    if (c->upstream[0])
        sprintf(out, "%.80s%s%.60s (https)", lead, lead[0] ? " \xE1 " : "", c->upstream);
    else
        cpy(out, lead, cap);
}

void mcu_status(McCtl *c, const char *line)
{
    char  d[200], r[32];
    int   sw = swap_in(c);
    const char *p;

    if (prefix(line, "Buffering "))
    {
        unsigned long pct;
        char h[64];
        p = line + 10;
        pct = num(&p);
        if (pct > 100) pct = 100;
        sprintf(h, "Streaming via secure proxy \xE1 buffering %lu%%", pct);
        host_tail(c, d, sizeof d, "QuickTime Player opens it at 100%");
        lcd_set(c, h, d, "", (int)(pct * 10UL), MC_GLYPH_BUSY);
        c->lcd_mode = MCU_LCD_BUFFER;
    }
    else if (prefix(line, "Streaming via secure proxy: "))
    {
        p = line + 28;
        r[0] = 0;
        if (*p >= '0' && *p <= '9')
        {
            unsigned long kbs = num(&p);
            sprintf(r, "%lu KB/s", kbs);
            if (prefix(p, " KB/s, ")) p += 7;
        }
        else if (prefix(p, "idle, ")) { cpy(r, "idle", sizeof r); p += 6; }
        {
            char rest[120];
            cpy(rest, p, sizeof rest);
            if (c->upstream[0])
                sprintf(d, "%.60s (https) \xE1 %.110s", c->upstream, rest);
            else
                cpy(d, rest, sizeof d);
        }
        lcd_set(c, "Streaming to QuickTime Player.", d, r, -1, MC_GLYPH_BUSY);
        c->lcd_mode = MCU_LCD_STREAM;
    }
    else if (prefix(line, "Downloading: ") && c->lcd_mode == MCU_LCD_DOWNLOAD)
    {
        /* the progress callback (called right after) owns the LCD */
    }
    else
    {
        lcd_set(c, line, "", "", -1,
                c->ui.confirm ? MC_GLYPH_WARN : MC_GLYPH_NONE);
        c->lcd_mode = MCU_LCD_PLAIN;
    }
    swap_out(c, sw);
}

void mcu_detail(McCtl *c, const char *line)
{
    int sw;
    sniff_host(c, line);
    if (line[0] == 0 || prefix(line, "proxy:")) return;
    if (c->lcd_mode != MCU_LCD_PLAIN) return;   /* owned by the gauge */
    sw = swap_in(c);
    cpy(c->ui.lcd_detail, line, sizeof c->ui.lcd_detail);
    mc_ui_dirty(&c->ui, W_LCD);
    swap_out(c, sw);
}

static void mb1(char *out, unsigned long bytes)
{
    unsigned long kb = bytes / 1024UL;
    sprintf(out, "%lu.%lu", kb / 1024UL, ((kb % 1024UL) * 10UL) / 1024UL);
}

void mcu_progress(McCtl *c, unsigned long done, unsigned long total,
                  unsigned long ticks)
{
    char h[64], d[200], r[32], a[24], b[24], left[48];
    unsigned long dk = done / 1024UL, tk = total / 1024UL, kbs = 0, pm = 0;
    int sw = swap_in(c);

    if (tk > 0) pm = (dk > 4000000UL) ? dk / (tk / 1000UL ? tk / 1000UL : 1UL)
                                      : dk * 1000UL / tk;
    if (pm > 1000UL) pm = 1000UL;
    if (ticks > 0) kbs = dk / ticks * 60UL + (dk % ticks) * 60UL / ticks;
    sprintf(h, "Downloading %lu%%", pm / 10UL);
    r[0] = 0;
    if (kbs > 0) sprintf(r, "%lu KB/s", kbs);
    mb1(a, done);
    mb1(b, total);
    left[0] = 0;
    if (kbs > 0 && tk > dk)
    {
        unsigned long secs = (tk - dk) / kbs;
        if (secs < 60UL) cpy(left, " \xE1 under a minute left", sizeof left);
        else sprintf(left, " \xE1 about %lu min left", (secs + 59UL) / 60UL);
    }
    if (total > 0) sprintf(d, "%s of %s MB%s", a, b, left);
    else sprintf(d, "%s MB", a);
    lcd_set(c, h, d, r, total > 0 ? (int)pm : -1, MC_GLYPH_NONE);
    c->lcd_mode = MCU_LCD_DOWNLOAD;
    swap_out(c, sw);
}

/* ---------------- the list ---------------- */
static void clear_rows(McUi *ui)
{
    memset(ui->rows, 0, sizeof ui->rows);
    ui->nrows = 0;
    ui->top = 0;
    ui->sel = -1;
    mc_ui_dirty(ui, W_LIST);
    mc_ui_dirty(ui, W_HEADING);
}

void mcu_show_empty(McCtl *c, const char *hint)
{
    McUi *ui = &c->ui;
    clear_rows(ui);
    ui->screen = MC_SCR_SEARCH;
    ui->more_avail = 0;
    ui->heading[0] = 0;
    ui->head_right[0] = 0;
    cpy(ui->list_hint, hint != NULL ? hint : "", sizeof ui->list_hint);
    if (ui->focus == MC_FOC_LIST) set_focus(c, MC_FOC_FIELD);
    sync(c);
}

void mcu_show_results(McCtl *c, const McSearch *s, const char *terms,
                      int page, int sel)
{
    McUi *ui = &c->ui;
    int   i, n = s->count, first;
    if (n > MC_UI_MAXROWS) n = MC_UI_MAXROWS;
    clear_rows(ui);
    ui->screen = MC_SCR_RESULTS;
    for (i = 0; i < n; i++)
        cpy(ui->rows[i].text, s->hits[i].title[0] ? s->hits[i].title
                                                 : s->hits[i].identifier,
            sizeof ui->rows[i].text);
    ui->nrows = n;
    ui->more_avail = (s->count == MC_MAXHITS);
    c->page = page;
    sprintf(ui->heading, "Results for \xD2%.70s\xD3", terms);
    first = (page - 1) * MC_MAXHITS + 1;
    if (s->numFound >= 0)
        sprintf(ui->head_right, "%ld items \xE1 %d-%d", s->numFound, first, first + n - 1);
    else
        sprintf(ui->head_right, "%d-%d", first, first + n - 1);
    if (sel >= 0) mc_ui_select(ui, sel);
    sync(c);
}

int mcu_file_dot(const McFile *f)
{
    if (f->state == MC_STATE_PLAYS) return MC_DOT_OK;
    if (f->state == MC_STATE_NEWER) return MC_DOT_NO;
    return f->hint == MC_HINT_PLAYS ? MC_DOT_OK_GUESS : MC_DOT_NO_GUESS;
}

static void file_row(McRow *r, const McFile *f)
{
    char mb[24];
    mc_file_label(f, r->text, (long)sizeof r->text);
    cpy(r->fmt, f->format, sizeof r->fmt);
    mc_file_mb(f, mb);
    sprintf(r->size, "%.10s MB", mb);
    cpy(r->mark, mc_file_mark(f), sizeof r->mark);
    r->dot = (unsigned char)mcu_file_dot(f);
    r->note = (unsigned char)(f->note == MC_FILE_NOTE_STUTTER ? MC_NOTE_STUTTER
                                                             : MC_NOTE_NONE);
}

void mcu_show_files(McCtl *c, const McMeta *m, const char *title, int sel)
{
    McUi *ui = &c->ui;
    int   i, n = m->nfiles;
    if (n > MC_UI_MAXROWS) n = MC_UI_MAXROWS;
    clear_rows(ui);
    ui->screen = MC_SCR_FILES;
    for (i = 0; i < n; i++) file_row(&ui->rows[i], &m->files[i]);
    ui->nrows = n;
    ui->more_avail = 0;
    cpy(ui->heading, title, sizeof ui->heading);
    sprintf(ui->head_right, "%d video file%s%s", m->nfiles, m->nfiles == 1 ? "" : "s",
            m->droppedFiles ? " (first 64)" : "");
    if (sel >= 0) mc_ui_select(ui, sel);
    sync(c);
}

void mcu_update_file(McCtl *c, const McMeta *m, int i)
{
    McRect r;
    if (i < 0 || i >= c->ui.nrows || i >= m->nfiles || c->ui.screen != MC_SCR_FILES)
        return;
    file_row(&c->ui.rows[i], &m->files[i]);
    mc_ui_row_rect(&c->ui, i, &r);
    mc_rect_union(&c->ui.dirty, &r);
    sync(c);
}

int mcu_selected(const McCtl *c)
{
    if (c->ui.sel < 0 || c->ui.sel >= c->ui.nrows) return -1;
    return c->ui.sel;
}
