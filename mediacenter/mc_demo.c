/*
    mc_demo.c -- Classic Media Center 0.6: demo scenes, the U2 font test
    scene and the dump header. See mc_demo.h. The scenes are the U1b
    mock-ups (moved here from mc_skin_render.c unchanged, so the host
    PNGs and the guest's "MC dump N" files show the same thing).
*/
#include <stdio.h>
#include <string.h>
#include "mc_demo.h"

/* ---------------- sample content ---------------- */
static const char *const kHits[] =
{
    "Star Trek: The Original Series (1966-1969)",
    "Star Trek - Where No Man Has Gone Before (pilot, 1965)",
    "Star Trek: The Animated Series",
    "Star Trek Phase II - Come What May",
    "Star Trek Continues - Pilgrim of Eternity",
    "Star Trek: The Motion Picture - trailer (1979)",
    "Leonard Nimoy on The Mike Douglas Show (1975)",
    "Star Trek blooper reel, seasons 1-2",
    "Trek Talk: a fan film documentary",
    "Star Trek Deep Space Nine - promotional reel",
    "Inside Star Trek (Columbia Records, 1976)",
    "Star Trek: New Voyages - In Harm's Way",
    "Star Trek convention, Los Angeles 1986",
    "Starship Exeter - The Savage Empire",
    "Star Trek TOS - The Cage (unaired pilot)",
    "Star Trek: The Next Generation - promo 1987",
    "Star Trek Lives! - 1974 convention film",
    "Star Trek Axanar - Prelude to Axanar",
    "Star Trek fan film - Hidden Frontier",
    "Star Trek Enterprise - launch promo",
    "Star Trek II trailer (1982)",
    "Star Trek: The Experience - Las Vegas",
    "Star Trek Communicator replica review",
    "William Shatner interview, 1968",
    "Star Trek Tribute - 40 years"
};

typedef struct { const char *label, *fmt, *mb, *mark; int dot; int note; } FileRow;

/* labels and sizes: fixtures/jimmy_files.txt (the real item's rows);
   marks: a mix to show every kind; "may stutter" comes from the
   per-file note (McRow.note = MC_NOTE_STUTTER), not from the mark text */
static const FileRow kFiles[] =
{
    { "1x01 - The Cage",                       "h.264",    "376.4", "PLAYS NOW",     MC_DOT_OK, MC_NOTE_NONE },
    { "1x02 - Where No Man Has Gone Before",   "h.264",    "298.7", "plays now?",    MC_DOT_OK_GUESS, MC_NOTE_NONE },
    { "1x03 - The Corbomite Maneuver",         "h.264",    "299.5", "plays now?",    MC_DOT_OK_GUESS, MC_NOTE_STUTTER },
    { "1x04 - Mudd's Women",                   "h.264 IA", "299.0", "will not play", MC_DOT_NO, MC_NOTE_NONE },
    { "1x05 - The Enemy Within",               "h.264",    "298.7", "plays now?",    MC_DOT_OK_GUESS, MC_NOTE_NONE },
    { "1x06 - The Man Trap",                   "h.264",    "299.2", "plays now?",    MC_DOT_OK_GUESS, MC_NOTE_NONE },
    { "1x07 - The Naked Time",                 "MPEG4",    "299.1", "may not play?", MC_DOT_NO_GUESS, MC_NOTE_NONE },
    { "1x08 - Charlie X",                      "h.264",    "299.0", "plays now?",    MC_DOT_OK_GUESS, MC_NOTE_NONE },
    { "1x09 - Balance of Terror",              "h.264",    "299.5", "plays now?",    MC_DOT_OK_GUESS, MC_NOTE_NONE },
    { "1x10 - What are Little Girls Made of",  "h.264",    "298.9", "plays now?",    MC_DOT_OK_GUESS, MC_NOTE_NONE },
    { "1x11 - Dagger of the Mind",             "h.264",    "298.9", "plays now?",    MC_DOT_OK_GUESS, MC_NOTE_NONE },
    { "1x12 - Miri",                           "h.264",    "298.5", "plays now?",    MC_DOT_OK_GUESS, MC_NOTE_NONE }
};

static void cpy(char *d, const char *s, size_t n)
{
    size_t k = strlen(s);
    if (k >= n) k = n - 1;
    memcpy(d, s, k);
    d[k] = 0;
}

static void lcd(McUi *ui, const char *head, const char *detail, const char *right,
                int progress, int glyph)
{
    cpy(ui->lcd_head, head, sizeof ui->lcd_head);
    cpy(ui->lcd_detail, detail, sizeof ui->lcd_detail);
    cpy(ui->lcd_right, right, sizeof ui->lcd_right);
    ui->progress = progress;
    ui->glyph = glyph;
}

static void results(McUi *ui)
{
    int i, n = (int)(sizeof kHits / sizeof kHits[0]);
    ui->screen = MC_SCR_RESULTS;
    cpy(ui->field, "star trek", sizeof ui->field);
    /* MacRoman: 0xD2/0xD3 curly quotes, 0xE1 middle dot */
    cpy(ui->heading, "Results for \xD2star trek\xD3", sizeof ui->heading);
    cpy(ui->head_right, "412 items \xE1 1-25", sizeof ui->head_right);
    for (i = 0; i < n; i++) cpy(ui->rows[i].text, kHits[i], sizeof ui->rows[i].text);
    ui->nrows = n;
    ui->sel = 0;
    ui->more_avail = 1;
    ui->focus = MC_FOC_LIST;
    lcd(ui, "412 items found, showing 1-25.",
        "Pick one and click Play (or press Enter) to see its videos.", "", -1, 0);
}

static void files(McUi *ui)
{
    int i, n = (int)(sizeof kFiles / sizeof kFiles[0]);
    ui->screen = MC_SCR_FILES;
    cpy(ui->field, "star trek", sizeof ui->field);
    cpy(ui->heading, "Star Trek: The Original Series", sizeof ui->heading);
    cpy(ui->head_right, "30 video files", sizeof ui->head_right);
    for (i = 0; i < n; i++)
    {
        McRow *r = &ui->rows[i];
        cpy(r->text, kFiles[i].label, sizeof r->text);
        cpy(r->fmt, kFiles[i].fmt, sizeof r->fmt);
        sprintf(r->size, "%s MB", kFiles[i].mb);
        cpy(r->mark, kFiles[i].mark, sizeof r->mark);
        r->dot = (unsigned char)kFiles[i].dot;
        r->note = (unsigned char)kFiles[i].note;
    }
    ui->nrows = 30;           /* 30 in the item; 12 sample rows + ... */
    for (i = n; i < 30; i++)
    {
        McRow *r = &ui->rows[i];
        sprintf(r->text, "1x%02d - (episode %d)", i + 1, i + 1);
        cpy(r->fmt, "h.264", sizeof r->fmt);
        cpy(r->size, "299.0 MB", sizeof r->size);
        cpy(r->mark, "plays now?", sizeof r->mark);
        r->dot = MC_DOT_OK_GUESS;
    }
    ui->sel = 1;
    ui->focus = MC_FOC_LIST;
    lcd(ui, "30 video files. Pick one and click Play.",
        "\xD2plays now?\xD3 is a guess until you try it; the mark then becomes a fact.",
        "", -1, 0);
}

static const char *const kScene[MC_DEMO_SCENES] =
{
    "1_search", "2_results", "2b_results_play_pressed", "3_files",
    "4_download", "5_confirm", "5b_error", "6_streaming", "6b_quit_warning", "7_inactive"
};

int mc_demo_scene(int k, McUi *ui)
{
    if (k < 0 || k >= MC_DEMO_SCENES) return -1;
    mc_ui_init(ui);
    ui->draw_field_text = 1;
    switch (k)
    {
    case 0:
        cpy(ui->list_hint, "Films from the Internet Archive", sizeof ui->list_hint);
        lcd(ui, "Type a word or two (e.g. popeye) and press Return.",
            "Decoder 17 + AVCDecLib found: Baseline, Main and High films play.",
            "", -1, 0);
        break;
    case 1: results(ui); break;
    case 2: results(ui); ui->pressed = W_PLAY; break;
    case 3: files(ui); ui->dl_first = 1; break;
    case 4:
        files(ui);
        ui->busy = 1;
        lcd(ui, "Downloading 41%",
            "1x02 - Where No Man Has Gone Before.mov \xE1 122.5 of 298.7 MB \xE1 about 7 min left",
            "426 KB/s", 410, 0);
        break;
    case 5:
        files(ui);
        lcd(ui, "This film is 298 MB: about 12 minutes to download.",
            "At 430 KB/s. Click Play again to download it; anything else cancels.",
            "", -1, MC_GLYPH_WARN);
        break;
    case 6:
        cpy(ui->field, "popeye", sizeof ui->field);
        ui->focus = MC_FOC_FIELD;
        cpy(ui->list_hint, "No results", sizeof ui->list_hint);
        lcd(ui, "Could not connect to the server (error -3155).",
            "archive.org did not answer. Check the network, then Search again.",
            "", -1, MC_GLYPH_ERROR);
        break;
    case 7:
        files(ui);
        ui->busy = 1;           /* buffering: busy AND serving; Stop ends both */
        ui->serving = 1;
        lcd(ui, "Streaming via secure proxy \xE1 buffering 64%",
            "QuickTime Player opens it at 100% \xE1 ia601304.us.archive.org (https)",
            "212 KB/s", 640, MC_GLYPH_BUSY);
        break;
    case 8:
        files(ui);              /* streaming, QuickTime already playing: idle but serving */
        ui->serving = 1;
        mc_ui_sync(ui);
        lcd(ui, "Streaming to QuickTime Player.", "ia601304.us.archive.org (https)",
            "212 KB/s", -1, MC_GLYPH_BUSY);
        mc_ui_quit_request(ui);      /* first Quit: the LCD warning */
        break;
    case 9:
        results(ui);
        ui->active = 0;
        break;
    }
    mc_ui_sync(ui);
    mc_ui_dirty_all(ui);
    return 0;
}

const char *mc_demo_name(int n)
{
    if (n >= 0 && n < MC_DEMO_SCENES) return kScene[n];
    if (n == MC_DEMO_FONTTEST) return "50_font_test";
    if (n == MC_DEMO_LIVE) return "99_live";
    return NULL;
}

/* ---------------- the U2 font test ---------------- */
#define FT_TOP    30
#define FT_ROW_H  38
#define FT_LEFT_L 14
#define FT_LEFT_R 236
#define FT_WELL_L 244
#define FT_WELL_R 468

typedef struct { int role, size; const char *label; } FtRow;
static const FtRow kFt[MC_FT_ROWS] =
{
    { MC_F_HEAD,      18, "heading chain, 18 pt" },
    { MC_F_TEST_MCG,  14, "MC Garamond 14 pt" },
    { MC_F_TEST_MCG,  18, "MC Garamond 18 pt" },
    { MC_F_TEST_MCG,  20, "MC Garamond 20 pt" },
    { MC_F_TEST_MCG,  24, "MC Garamond 24 pt" },
    { MC_F_TEST_HOEF, 18, "Hoefler Text 18 pt" },
    { MC_F_TEST_PAL,  18, "Palatino 18 pt" }
};

static void ft_well(McRect *r)
{
    mc_rect_set(r, FT_WELL_L, FT_TOP - 2, FT_WELL_R, FT_TOP + MC_FT_ROWS * FT_ROW_H + 2);
}

static void ft_info(McRect *r)
{
    mc_rect_set(r, 12, 304, 468, 352);
}

void mc_demo_font_render(McCanvas *cv, const McRect *clip)
{
    McRect c, r;
    McRect save = cv->clip;
    if (!mc_rect_sect(clip, &cv->clip, &c)) return;
    cv->clip = c;
    mc_skin_metal(cv, &c);
    mc_skin_frame(cv, MC_TITLE_H);
    ft_well(&r);
    mc_skin_list_well(cv, &r, 0);
    ft_info(&r);
    mc_skin_lcd(cv, &r);
    cv->clip = save;
}

static void ft_run(McRun *u, const char *text, int l, int t, int r, int b,
                   int base, int role, int size, int v, int emboss)
{
    size_t k = strlen(text);
    if (k >= sizeof u->text) k = sizeof u->text - 1;
    memcpy(u->text, text, k);
    u->text[k] = 0;
    mc_rect_set(&u->box, l, t, r, b);
    u->base = (short)base;
    u->role = (unsigned char)role;
    u->size = (unsigned char)size;
    u->align = (unsigned char)(role == MC_F_TITLE ? MC_AL_CENTER : MC_AL_LEFT);
    u->trunc = MC_TR_END;
    u->rgb[0] = u->rgb[1] = (unsigned char)v;
    u->rgb[2] = (unsigned char)(v + 2 > 255 ? 255 : v + 2);
    u->emboss = (unsigned char)emboss;
}

int mc_demo_font_runs(const McRect *clip, const char *info1,
                      const char *info2, McRun *out, int max)
{
    McRun  tmp;
    McRect o;
    int    n = 0, i, side;
    /* MacRoman 0xD2/0xD3: curly quotes */
    const char *sample = "Results for \xD2star trek\xD3";

#define FT_ADD(args) do { ft_run args; \
        if (n < max && mc_rect_sect(&tmp.box, clip, &o)) out[n++] = tmp; } while (0)

    FT_ADD((&tmp, "U2 font test", 44, 0, 436, MC_TITLE_H, 17, MC_F_TITLE, 16, 34, 1));
    for (i = 0; i < MC_FT_ROWS; i++)
    {
        int y0 = FT_TOP + i * FT_ROW_H;
        for (side = 0; side < 2; side++)
        {
            int l = side ? FT_WELL_L + 8 : FT_LEFT_L;
            int r = side ? FT_WELL_R - 6 : FT_LEFT_R;
            FT_ADD((&tmp, kFt[i].label, l, y0, r, y0 + 12, y0 + 10, MC_F_SMALL, 9,
                    side ? 110 : 70, 0));
            FT_ADD((&tmp, sample, l, y0 + 12, r, y0 + FT_ROW_H, y0 + 33,
                    kFt[i].role, kFt[i].size, side ? 20 : 30, side ? 0 : 1));
        }
    }
    if (info1 != NULL)
        FT_ADD((&tmp, info1, 22, 306, 458, 326, 321, MC_F_LCD, 12, 34, 0));
    if (info2 != NULL)
        FT_ADD((&tmp, info2, 22, 328, 458, 346, 341, MC_F_SMALL, 10, 70, 0));
#undef FT_ADD
    return n;
}

/* ---------------- dump header ---------------- */
static void be32(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)((v >> 24) & 0xFFUL);
    p[1] = (unsigned char)((v >> 16) & 0xFFUL);
    p[2] = (unsigned char)((v >> 8) & 0xFFUL);
    p[3] = (unsigned char)(v & 0xFFUL);
}

void mc_dump_header(unsigned char *h, long width, long height, long rowBytes)
{
    h[0] = 'M'; h[1] = 'C'; h[2] = 'D'; h[3] = '1';
    be32(h + 4, (unsigned long)width);
    be32(h + 8, (unsigned long)height);
    be32(h + 12, (unsigned long)rowBytes);
}
