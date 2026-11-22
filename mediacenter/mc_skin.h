/*
    mc_skin.h -- Classic Media Center 0.6 (U1b skin): the "QuickTime 6" skin painter.

    Portable C89, no Toolbox calls, no floating point (bit-identical on
    x86 and PowerPC). Paints into an McCanvas: 32-bit xRGB pixels in
    big-endian byte order (byte 0 = x, 1 = R, 2 = G, 3 = B), i.e. exactly
    a Mac 32-bit GWorld PixMap. Pixels are written byte by byte, never
    through a word cast, so the host's endianness does not matter.

    Every primitive clips to cv->clip, so any rect can be re-rendered
    on its own and gives the same pixels as a full redraw.
*/
#ifndef MC_SKIN_H
#define MC_SKIN_H

typedef struct { short l, t, r, b; } McRect;

typedef struct
{
    unsigned char *base;      /* pixel (0,0)                          */
    long           rowBytes;  /* may be > 4*w (GWorld padding)        */
    short          w, h;
    McRect         clip;      /* paint only inside this               */
} McCanvas;

/* button looks */
#define MC_BTN_PLAIN    0     /* clear/white gel                      */
#define MC_BTN_PRIMARY  1     /* aqua gel (the default button)        */
/* button states */
#define MC_ST_NORMAL    0
#define MC_ST_PRESSED   1
#define MC_ST_DISABLED  2

/* list row status dots */
#define MC_DOT_NONE      0
#define MC_DOT_OK_GUESS  1    /* "plays now?"     hollow green        */
#define MC_DOT_OK        2    /* "PLAYS NOW"      green               */
#define MC_DOT_WARN      3    /* "may stutter"    orange              */
#define MC_DOT_NO_GUESS  4    /* "may not play?"  hollow red          */
#define MC_DOT_NO        5    /* "will not play"  red                 */

/* LCD glyphs */
#define MC_GLYPH_NONE   0
#define MC_GLYPH_WARN   1     /* amber disc, "!" is a text run        */
#define MC_GLYPH_ERROR  2     /* red disc                             */
#define MC_GLYPH_BUSY   3     /* aqua disc                            */

void mc_rect_set(McRect *r, int l, int t, int rr, int b);
int  mc_rect_sect(const McRect *a, const McRect *b, McRect *out); /* 0 = empty */
void mc_rect_union(McRect *a, const McRect *b);   /* empty a = b      */

void mc_canvas_init(McCanvas *cv, unsigned char *base, long rowBytes,
                    int w, int h);

/* the window background: brushed metal, a pure function of (x, y) */
void mc_skin_metal(McCanvas *cv, const McRect *r);
/* 1-px bevel of the whole window + a faint line under the title strip */
void mc_skin_frame(McCanvas *cv, int titleH);

void mc_skin_button(McCanvas *cv, const McRect *r, int look, int state);
void mc_skin_round_button(McCanvas *cv, const McRect *r, int glyph,
                          int state);          /* glyph 0 close, 1 shade */
void mc_skin_field(McCanvas *cv, const McRect *r, int focused,
                   int enabled);
void mc_skin_list_well(McCanvas *cv, const McRect *r, int focused);
void mc_skin_selection(McCanvas *cv, const McRect *row, int active);
void mc_skin_scroller(McCanvas *cv, const McRect *r, long total,
                      long visible, long top, int enabled);
void mc_skin_lcd(McCanvas *cv, const McRect *r);
void mc_skin_progress(McCanvas *cv, const McRect *r, int permille);
void mc_skin_checkbox(McCanvas *cv, const McRect *box, int checked,
                      int state);
void mc_skin_dot(McCanvas *cv, int cx, int cy, int kind);
void mc_skin_caret(McCanvas *cv, int x, int t, int b);   /* host TE stand-in */
void mc_skin_glyph(McCanvas *cv, const McRect *r, int glyph);

/* scroller geometry (shared with mc_layout's hit-testing) */
#define MC_SCROLL_ARROW 11
void mc_skin_thumb(const McRect *track, long total, long visible,
                   long top, McRect *thumb);

#endif
