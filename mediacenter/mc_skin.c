/*
    mc_skin.c -- Classic Media Center 0.6 skin painter (U1b). See mc_skin.h.

    All geometry is integer, in 1/16 pixel ("sub") units where edges are
    anti-aliased: coverage of a pixel = how far its centre lies inside
    the shape's signed-distance edge, over a 1-pixel ramp. No floats, so
    the same pixels come out on every CPU. Hash arithmetic is masked to
    32 bits before every right shift, so a 64-bit "long" host and a
    32-bit PowerPC agree.

    Colours: ONE table, kPal below (Aqua). A second accent later = a
    second table and an index.
*/
#include <stddef.h>
#include "mc_skin.h"

#define SUB 16
#define M32 0xFFFFFFFFUL

/* ------------------------------------------------------------------
   the palette (Aqua). r,g,b
   ------------------------------------------------------------------ */
enum
{
    P_AQ_OUTLINE, P_AQ_TOP, P_AQ_MID, P_AQ_MID2, P_AQ_BOT,
    P_AQ_PR_TOP, P_AQ_PR_MID, P_AQ_PR_MID2, P_AQ_PR_BOT,
    P_PL_OUTLINE, P_PL_TOP, P_PL_MID, P_PL_MID2, P_PL_BOT,
    P_PL_PR_TOP, P_PL_PR_MID, P_PL_PR_MID2, P_PL_PR_BOT,
    P_SEL_TOP, P_SEL_BOT, P_SEL_LINE_T, P_SEL_LINE_B,
    P_SELI_TOP, P_SELI_BOT,
    P_FOCUS,
    P_LCD_TOP, P_LCD_BOT, P_LCD_EDGE_T, P_LCD_EDGE_B,
    P_BAR_EDGE, P_BAR_TRACK, P_BAR_TOP, P_BAR_BOT,
    P_DOT_GREEN, P_DOT_AMBER, P_DOT_RED, P_DOT_AQUA,
    P_COUNT
};

static const unsigned char kPal[P_COUNT][3] =
{
    { 34,  70, 138}, {130, 180, 240}, { 66, 132, 222}, { 42, 108, 210}, {150, 214, 255},
    { 60, 110, 196}, { 34,  88, 182}, { 22,  72, 168}, {104, 170, 236},
    {112, 112, 116}, {252, 252, 252}, {228, 228, 230}, {212, 212, 216}, {250, 250, 252},
    {200, 200, 204}, {176, 176, 180}, {160, 160, 166}, {214, 214, 218},
    {110, 166, 238}, { 52, 114, 214}, {150, 194, 246}, { 38,  90, 186},
    {206, 206, 210}, {172, 172, 178},
    { 92, 150, 228},
    {200, 212, 190}, {222, 231, 212}, { 92, 100,  88}, {150, 158, 144},
    {112, 122, 106}, {182, 194, 172}, { 98, 112,  94}, { 60,  72,  58},
    { 62, 172,  72}, {242, 124,  18}, {214,  52,  42}, { 64, 132, 222}
};

/* ------------------------------------------------------------------
   rects, canvas
   ------------------------------------------------------------------ */
void mc_rect_set(McRect *r, int l, int t, int rr, int b)
{
    r->l = (short)l; r->t = (short)t; r->r = (short)rr; r->b = (short)b;
}

int mc_rect_sect(const McRect *a, const McRect *b, McRect *out)
{
    McRect o;
    o.l = a->l > b->l ? a->l : b->l;
    o.t = a->t > b->t ? a->t : b->t;
    o.r = a->r < b->r ? a->r : b->r;
    o.b = a->b < b->b ? a->b : b->b;
    if (o.r <= o.l || o.b <= o.t) { mc_rect_set(out, 0, 0, 0, 0); return 0; }
    *out = o;
    return 1;
}

void mc_rect_union(McRect *a, const McRect *b)
{
    if (b->r <= b->l || b->b <= b->t) return;
    if (a->r <= a->l || a->b <= a->t) { *a = *b; return; }
    if (b->l < a->l) a->l = b->l;
    if (b->t < a->t) a->t = b->t;
    if (b->r > a->r) a->r = b->r;
    if (b->b > a->b) a->b = b->b;
}

void mc_canvas_init(McCanvas *cv, unsigned char *base, long rowBytes,
                    int w, int h)
{
    cv->base = base;
    cv->rowBytes = rowBytes;
    cv->w = (short)w;
    cv->h = (short)h;
    mc_rect_set(&cv->clip, 0, 0, w, h);
}

/* the part of r (grown by g pixels) that is inside clip and canvas */
static int area(const McCanvas *cv, const McRect *r, int g, McRect *o)
{
    McRect a, c;
    mc_rect_set(&a, r->l - g, r->t - g, r->r + g, r->b + g);
    if (!mc_rect_sect(&a, &cv->clip, &c)) return 0;
    mc_rect_set(&a, 0, 0, cv->w, cv->h);
    return mc_rect_sect(&c, &a, o);
}

/* blend (r,g,b) at alpha a (0..256) */
static void blend(McCanvas *cv, int x, int y, int r, int g, int b, int a)
{
    unsigned char *p;
    if (a <= 0) return;
    p = cv->base + (long)y * cv->rowBytes + (long)x * 4L;
    if (a >= 256)
    {
        p[0] = 0;
        p[1] = (unsigned char)r; p[2] = (unsigned char)g; p[3] = (unsigned char)b;
        return;
    }
    p[0] = 0;
    p[1] = (unsigned char)((p[1] * (256 - a) + r * a + 128) >> 8);
    p[2] = (unsigned char)((p[2] * (256 - a) + g * a + 128) >> 8);
    p[3] = (unsigned char)((p[3] * (256 - a) + b * a + 128) >> 8);
}

static int clampi(long v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : (int)v);
}

static unsigned long isqrt(unsigned long n)
{
    unsigned long r = 0, bit = 1UL << 30;
    n &= M32;
    while (bit > n) bit >>= 2;
    while (bit != 0)
    {
        if (n >= r + bit) { n -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return r;
}

/* coverage (0..256) of pixel x,y by rect r with corner radius rad (sub) */
static int rr_cov(const McRect *r, long rad, int x, int y)
{
    long px = (long)x * SUB + SUB / 2, py = (long)y * SUB + SUB / 2;
    long l = (long)r->l * SUB, t = (long)r->t * SUB;
    long rr = (long)r->r * SUB, b = (long)r->b * SUB;
    long dx = 0, dy = 0, d, m;
    if (px < l + rad) dx = l + rad - px; else if (px > rr - rad) dx = px - (rr - rad);
    if (py < t + rad) dy = t + rad - py; else if (py > b - rad) dy = py - (b - rad);
    if (dx == 0 && dy == 0)
    {
        m = px - l;
        if (rr - px < m) m = rr - px;
        if (py - t < m) m = py - t;
        if (b - py < m) m = b - py;
        d = -m;
    }
    else d = (long)isqrt((unsigned long)(dx * dx + dy * dy)) - rad;
    return clampi((SUB / 2 - d) * (256 / SUB), 0, 256);
}

/* ---------------- shaders ---------------- */
typedef struct
{
    int  n;
    int  pos[6];                 /* 0..256 down the rect              */
    unsigned char c[6][3];
    int  from, len;              /* the axis span in pixels           */
    int  horiz;                  /* 1 = left -> right                 */
} Grad;

static void grad_init(Grad *g, const McRect *r, int horiz)
{
    g->n = 0;
    g->horiz = horiz;
    g->from = horiz ? r->l : r->t;
    g->len = horiz ? r->r - r->l : r->b - r->t;
    if (g->len < 1) g->len = 1;
}

static void grad_stop(Grad *g, int pos, int r, int gg, int b)
{
    if (g->n >= 6) return;
    g->pos[g->n] = pos;
    g->c[g->n][0] = (unsigned char)r;
    g->c[g->n][1] = (unsigned char)gg;
    g->c[g->n][2] = (unsigned char)b;
    g->n++;
}

static void grad_pal(Grad *g, int pos, int idx)
{
    grad_stop(g, pos, kPal[idx][0], kPal[idx][1], kPal[idx][2]);
}

static void grad_at(const Grad *g, int x, int y, int *rgb)
{
    long p = (((long)((g->horiz ? x : y) - g->from)) * 256L + 128L) / g->len;
    int  i, k;
    if (p <= g->pos[0] || g->n == 1)
    {
        for (k = 0; k < 3; k++) rgb[k] = g->c[0][k];
        return;
    }
    for (i = 1; i < g->n; i++)
        if (p <= g->pos[i])
        {
            long span = g->pos[i] - g->pos[i - 1];
            long f = span > 0 ? ((p - g->pos[i - 1]) * 256L) / span : 256L;
            for (k = 0; k < 3; k++)
                rgb[k] = (int)((g->c[i - 1][k] * (256L - f) + g->c[i][k] * f
                                + 128L) >> 8);
            return;
        }
    for (k = 0; k < 3; k++) rgb[k] = g->c[g->n - 1][k];
}

/* fill a rounded rect with a gradient (g != NULL) or a flat colour,
   at overall alpha (0..256); galpha: optional alpha gradient (uses
   channel 0 of an alpha Grad as 0..255) */
static void rr_paint(McCanvas *cv, const McRect *r, long rad, const Grad *g,
                     int fr, int fg, int fb, int alpha, const Grad *ga)
{
    McRect a;
    int    x, y, cov, rgb[3], al[3];
    if (r->r <= r->l || r->b <= r->t) return;
    if (!area(cv, r, 0, &a)) return;
    rgb[0] = fr; rgb[1] = fg; rgb[2] = fb;
    for (y = a.t; y < a.b; y++)
        for (x = a.l; x < a.r; x++)
        {
            cov = rr_cov(r, rad, x, y);
            if (cov <= 0) continue;
            if (g != NULL) grad_at(g, x, y, rgb);
            cov = (cov * alpha) >> 8;
            if (ga != NULL)
            {
                grad_at(ga, x, y, al);
                cov = (cov * (al[0] + 1)) >> 8;
            }
            blend(cv, x, y, rgb[0], rgb[1], rgb[2], cov);
        }
}

static void rr_flat(McCanvas *cv, const McRect *r, long rad, int c, int alpha)
{
    rr_paint(cv, r, rad, NULL, kPal[c][0], kPal[c][1], kPal[c][2], alpha, NULL);
}

static void rr_rgb(McCanvas *cv, const McRect *r, long rad, int v, int alpha)
{
    rr_paint(cv, r, rad, NULL, v, v, v, alpha, NULL);
}

/* an anti-aliased line segment (sub units), half width hw (sub) */
static void seg(McCanvas *cv, long x0, long y0, long x1, long y1, long hw,
                int r, int g, int b, int alpha)
{
    McRect bb, a;
    long   vx = x1 - x0, vy = y1 - y0, len2 = vx * vx + vy * vy, len;
    int    x, y, cov;
    mc_rect_set(&bb, (int)((x0 < x1 ? x0 : x1) - hw) / SUB - 1,
                (int)((y0 < y1 ? y0 : y1) - hw) / SUB - 1,
                (int)((x0 > x1 ? x0 : x1) + hw) / SUB + 2,
                (int)((y0 > y1 ? y0 : y1) + hw) / SUB + 2);
    if (!area(cv, &bb, 0, &a)) return;
    len = (long)isqrt((unsigned long)len2);
    for (y = a.t; y < a.b; y++)
        for (x = a.l; x < a.r; x++)
        {
            long px = (long)x * SUB + SUB / 2 - x0, py = (long)y * SUB + SUB / 2 - y0;
            long t = px * vx + py * vy, d;
            if (len2 == 0 || t <= 0) d = (long)isqrt((unsigned long)(px * px + py * py));
            else if (t >= len2)
            {
                long qx = px - vx, qy = py - vy;
                d = (long)isqrt((unsigned long)(qx * qx + qy * qy));
            }
            else
            {
                d = px * vy - py * vx;
                if (d < 0) d = -d;
                d = d / len;
            }
            cov = clampi((hw + SUB / 2 - d) * (256 / SUB), 0, 256);
            blend(cv, x, y, r, g, b, (cov * alpha) >> 8);
        }
}

/* ------------------------------------------------------------------
   metal
   ------------------------------------------------------------------ */
static unsigned long hash2(unsigned long a, unsigned long b)
{
    unsigned long h = (a * 0x9E3779B1UL + b * 0x85EBCA77UL + 0x165667B1UL) & M32;
    h ^= h >> 15;
    h = (h * 0x2C1B3C6DUL) & M32;
    h ^= h >> 12;
    h = (h * 0x297A2D39UL) & M32;
    h ^= h >> 15;
    return h & M32;
}

/* signed noise -128..127 */
static int hn(unsigned long a, unsigned long b)
{
    return (int)((hash2(a, b) >> 8) & 255UL) - 128;
}

/* one horizontal streak layer: value noise along x, per row, segment
   length 1 << sh pixels, amplitude amp (output in 1/256 grey levels) */
static long streak(int x, int y, unsigned long seed, int sh, long amp)
{
    long s = (long)(x >> sh);
    long f = (long)(x & ((1 << sh) - 1));
    long n0 = hn((unsigned long)y * 7919UL + seed, (unsigned long)s);
    long n1 = hn((unsigned long)y * 7919UL + seed, (unsigned long)(s + 1));
    long v = (n0 * ((1L << sh) - f) + n1 * f) >> sh;   /* -128..127 */
    return (v * amp) >> 7;
}

static void metal_px(int x, int y, int h, int *rgb)
{
    long v;
    /* vertical gradient: 218 at the top to 190 at the bottom, with the
       top fifth a touch lighter (the polished band of QT 6's window) */
    v = (218L << 8) - ((long)y * (28L << 8)) / (h > 0 ? h : 1);
    if (y < h / 5) v += ((long)(h / 5 - y) * (4L << 8)) / (h / 5 > 0 ? h / 5 : 1);
    v += streak(x, y, 11UL, 7, 4L << 8);          /* long, faint        */
    v += streak(x, y, 23UL, 5, 3L << 8);    /* medium             */
    v += streak(x, y >> 1, 37UL, 3, 1L << 8);     /* short, 2-row thick */
    v += ((long)hn((unsigned long)x, (unsigned long)y + 99991UL) * 3L) / 2L;
    v = (v + 128) >> 8;
    rgb[0] = clampi(v - 1, 0, 255);
    rgb[1] = clampi(v, 0, 255);
    rgb[2] = clampi(v + 2, 0, 255);
}

void mc_skin_metal(McCanvas *cv, const McRect *r)
{
    McRect a;
    int    x, y, rgb[3];
    if (!area(cv, r, 0, &a)) return;
    for (y = a.t; y < a.b; y++)
        for (x = a.l; x < a.r; x++)
        {
            metal_px(x, y, cv->h, rgb);
            blend(cv, x, y, rgb[0], rgb[1], rgb[2], 256);
        }
}

static void hline(McCanvas *cv, int x0, int x1, int y, int v, int a)
{
    McRect r;
    mc_rect_set(&r, x0, y, x1, y + 1);
    rr_rgb(cv, &r, 0, v, a);
}

static void vline(McCanvas *cv, int x, int y0, int y1, int v, int a)
{
    McRect r;
    mc_rect_set(&r, x, y0, x + 1, y1);
    rr_rgb(cv, &r, 0, v, a);
}

void mc_skin_frame(McCanvas *cv, int titleH)
{
    int w = cv->w, h = cv->h;
    /* outer dark edge */
    hline(cv, 0, w, 0, 96, 256);
    hline(cv, 0, w, h - 1, 64, 256);
    vline(cv, 0, 0, h, 84, 256);
    vline(cv, w - 1, 0, h, 72, 256);
    /* inner bevel */
    hline(cv, 1, w - 1, 1, 255, 190);
    vline(cv, 1, 1, h - 1, 255, 120);
    hline(cv, 1, w - 1, h - 2, 0, 50);
    vline(cv, w - 2, 1, h - 1, 0, 40);
    /* engraved line under the title strip */
    if (titleH > 0)
    {
        hline(cv, 2, w - 2, titleH, 0, 30);
        hline(cv, 2, w - 2, titleH + 1, 255, 110);
    }
}

/* ------------------------------------------------------------------
   gel buttons
   ------------------------------------------------------------------ */
static void gel(McCanvas *cv, const McRect *r, long rad, int look, int state)
{
    McRect b, hi;
    Grad   g, ga;
    int    base, h = r->b - r->t;
    long   hrad;

    /* soft drop shadow on the metal */
    b = *r; b.t += 1; b.b += 1;
    rr_rgb(cv, &b, rad, 0, state == MC_ST_DISABLED ? 20 : 46);
    b.t += 1; b.b += 1; b.l += 1; b.r -= 1;
    rr_rgb(cv, &b, rad, 0, state == MC_ST_DISABLED ? 6 : 16);

    if (state == MC_ST_DISABLED)
    {
        rr_flat(cv, r, rad, P_PL_OUTLINE, 120);
        b = *r; b.l++; b.t++; b.r--; b.b--;
        grad_init(&g, &b, 0);
        grad_stop(&g, 0, 240, 240, 242);
        grad_stop(&g, 256, 226, 226, 230);
        rr_paint(cv, &b, rad - SUB, &g, 0, 0, 0, 170, NULL);
        return;
    }

    if (look == MC_BTN_PRIMARY)
        base = state == MC_ST_PRESSED ? P_AQ_PR_TOP : P_AQ_TOP;
    else
        base = state == MC_ST_PRESSED ? P_PL_PR_TOP : P_PL_TOP;

    rr_flat(cv, r, rad, look == MC_BTN_PRIMARY ? P_AQ_OUTLINE : P_PL_OUTLINE, 256);
    b = *r; b.l++; b.t++; b.r--; b.b--;
    grad_init(&g, &b, 0);
    grad_pal(&g, 0, base);
    grad_pal(&g, 118, base + 1);
    grad_pal(&g, 134, base + 2);
    grad_pal(&g, 256, base + 3);
    rr_paint(cv, &b, rad - SUB, &g, 0, 0, 0, 256, NULL);

    /* the glossy highlight: a smaller pill over the top half */
    hi = b;
    hi.l += (short)(rad > 6 * SUB ? 4 : 2);
    hi.r -= (short)(rad > 6 * SUB ? 4 : 2);
    hi.t += 1;
    hi.b = (short)(b.t + (h * 11) / 24);
    hrad = rad - 3 * SUB;
    if (hrad > ((long)(hi.b - hi.t) * SUB) / 2) hrad = ((long)(hi.b - hi.t) * SUB) / 2;
    if (hrad < 2 * SUB) hrad = 2 * SUB;
    grad_init(&ga, &hi, 0);
    grad_stop(&ga, 0, 235, 0, 0);
    grad_stop(&ga, 256, 70, 0, 0);
    rr_paint(cv, &hi, hrad, NULL, 255, 255, 255,
             state == MC_ST_PRESSED ? 150 : 256, &ga);
}

void mc_skin_button(McCanvas *cv, const McRect *r, int look, int state)
{
    gel(cv, r, ((long)(r->b - r->t) * SUB) / 2, look, state);
}

void mc_skin_round_button(McCanvas *cv, const McRect *r, int glyph, int state)
{
    McRect b;
    Grad   g, ga;
    long   rad = ((long)(r->r - r->l) * SUB) / 2;
    long   cx = ((long)r->l + r->r) * SUB / 2, cy = ((long)r->t + r->b) * SUB / 2;
    int    a = state == MC_ST_DISABLED ? 110 : 256;

    b = *r; b.t += 1; b.b += 1;
    rr_rgb(cv, &b, rad, 255, 150);           /* engraved: light below */
    rr_rgb(cv, r, rad, 70, a);
    b = *r; b.l++; b.t++; b.r--; b.b--;
    grad_init(&g, &b, 0);
    if (state == MC_ST_PRESSED)
    {
        grad_stop(&g, 0, 120, 120, 124);
        grad_stop(&g, 256, 170, 170, 176);
    }
    else
    {
        grad_stop(&g, 0, 150, 150, 156);
        grad_stop(&g, 140, 118, 118, 124);
        grad_stop(&g, 256, 214, 214, 220);
    }
    rr_paint(cv, &b, rad - SUB, &g, 0, 0, 0, a, NULL);
    b.l += 2; b.r -= 2; b.b = (short)(b.t + 4);
    grad_init(&ga, &b, 0);
    grad_stop(&ga, 0, 230, 0, 0);
    grad_stop(&ga, 256, 60, 0, 0);
    rr_paint(cv, &b, 2 * SUB, NULL, 255, 255, 255, a, &ga);
    if (state == MC_ST_DISABLED) return;
    if (glyph == 0)
    {
        seg(cv, cx - 40, cy - 40 + 4, cx + 40, cy + 40 + 4, 10, 40, 40, 44, 200);
        seg(cv, cx + 40, cy - 40 + 4, cx - 40, cy + 40 + 4, 10, 40, 40, 44, 200);
    }
    else
        seg(cv, cx - 44, cy + 4, cx + 44, cy + 4, 11, 40, 40, 44, 200);
}

/* ------------------------------------------------------------------
   wells
   ------------------------------------------------------------------ */
static void focus_glow(McCanvas *cv, const McRect *r, long rad)
{
    McRect g;
    int    i;
    static const int al[3] = { 50, 80, 130 };
    for (i = 0; i < 3; i++)
    {
        g = *r;
        g.l -= (short)(3 - i); g.t -= (short)(3 - i);
        g.r += (short)(3 - i); g.b += (short)(3 - i);
        rr_flat(cv, &g, rad + (3 - i) * SUB, P_FOCUS, al[i]);
    }
}

/* an inset white well: light emboss below, dark-to-mid edge, inner
   top shadow, white body */
static void inset_well(McCanvas *cv, const McRect *r, long rad, int shadowRows)
{
    McRect b;
    Grad   g;
    b = *r; b.t += 1; b.b += 1;
    rr_rgb(cv, &b, rad, 255, 170);
    grad_init(&g, r, 0);
    grad_stop(&g, 0, 98, 98, 100);
    grad_stop(&g, 256, 150, 150, 154);
    rr_paint(cv, r, rad, &g, 0, 0, 0, 256, NULL);
    b = *r; b.l++; b.t++; b.r--; b.b--;
    grad_init(&g, &b, 0);
    grad_stop(&g, 0, 206, 206, 208);
    grad_stop(&g, (int)((shadowRows * 256L) / (b.b - b.t)), 255, 255, 255);
    grad_stop(&g, 256, 255, 255, 255);
    rr_paint(cv, &b, rad > SUB ? rad - SUB : 0, &g, 0, 0, 0, 256, NULL);
}

void mc_skin_field(McCanvas *cv, const McRect *r, int focused, int enabled)
{
    long rad = ((long)(r->b - r->t) * SUB) / 2;
    if (focused) focus_glow(cv, r, rad);
    inset_well(cv, r, rad, 3);
    if (!enabled) rr_rgb(cv, r, rad, 214, 110);
}

void mc_skin_list_well(McCanvas *cv, const McRect *r, int focused)
{
    if (focused) focus_glow(cv, r, 2 * SUB);
    inset_well(cv, r, SUB, 2);
}

void mc_skin_selection(McCanvas *cv, const McRect *row, int active)
{
    Grad g;
    grad_init(&g, row, 0);
    grad_pal(&g, 0, active ? P_SEL_TOP : P_SELI_TOP);
    grad_pal(&g, 256, active ? P_SEL_BOT : P_SELI_BOT);
    rr_paint(cv, row, 0, &g, 0, 0, 0, 256, NULL);
    if (active)
    {
        McRect l = *row;
        l.b = (short)(l.t + 1);
        rr_flat(cv, &l, 0, P_SEL_LINE_T, 256);
        l = *row;
        l.t = (short)(l.b - 1);
        rr_flat(cv, &l, 0, P_SEL_LINE_B, 256);
    }
}

void mc_skin_thumb(const McRect *track, long total, long visible, long top,
                   McRect *thumb)
{
    long len = (track->b - track->t) - 2 * MC_SCROLL_ARROW;
    long th, pos;
    *thumb = *track;
    if (total <= visible || len < 8) { thumb->b = thumb->t; return; }
    th = (len * visible) / total;
    if (th < 18) th = 18;
    if (th > len) th = len;
    pos = ((len - th) * top) / (total - visible);
    thumb->t = (short)(track->t + pos);
    thumb->b = (short)(thumb->t + th);
}

static void arrow(McCanvas *cv, const McRect *c, int up, int enabled)
{
    long cx = ((long)c->l + c->r) * SUB / 2, cy = ((long)c->t + c->b) * SUB / 2;
    int  v = enabled ? 50 : 150;
    long d = up ? -1 : 1;
    seg(cv, cx - 40, cy - d * 18, cx, cy + d * 22, 9, v, v, v + 4, 256);
    seg(cv, cx + 40, cy - d * 18, cx, cy + d * 22, 9, v, v, v + 4, 256);
}

void mc_skin_scroller(McCanvas *cv, const McRect *r, long total, long visible,
                      long top, int enabled)
{
    McRect t, th, c;
    Grad   g, ga;
    long   rad;
    /* track: a groove, darker on the left */
    grad_init(&g, r, 1);
    grad_stop(&g, 0, 196, 196, 200);
    grad_stop(&g, 70, 226, 226, 230);
    grad_stop(&g, 256, 240, 240, 244);
    rr_paint(cv, r, 0, &g, 0, 0, 0, 256, NULL);
    vline(cv, r->l, r->t, r->b, 150, 256);
    /* the two arrows together at the bottom (Aqua style) */
    c = *r; c.l++; c.t = (short)(r->b - 2 * MC_SCROLL_ARROW); c.b = (short)(c.t + MC_SCROLL_ARROW);
    hline(cv, c.l, c.r, c.t, 160, 256);
    arrow(cv, &c, 1, enabled && top > 0);
    c.t = (short)(c.t + MC_SCROLL_ARROW); c.b = (short)(c.b + MC_SCROLL_ARROW);
    hline(cv, c.l, c.r, c.t, 180, 256);
    arrow(cv, &c, 0, enabled && top + visible < total);
    t = *r;
    mc_skin_thumb(&t, total, visible, top, &th);
    if (th.b <= th.t || !enabled) return;
    th.l += 1; th.r -= 0; th.t += 1; th.b -= 1;
    rad = ((long)(th.r - th.l) * SUB) / 2;
    rr_flat(cv, &th, rad, P_AQ_OUTLINE, 256);
    th.l++; th.r--; th.t++; th.b--;
    grad_init(&g, &th, 1);
    grad_pal(&g, 0, P_AQ_MID);
    grad_pal(&g, 128, P_AQ_MID2);
    grad_pal(&g, 256, P_AQ_BOT);
    rr_paint(cv, &th, rad - SUB, &g, 0, 0, 0, 256, NULL);
    c = th; c.r = (short)(c.l + (th.r - th.l) / 2 + 1); c.t += 2; c.b -= 2;
    grad_init(&ga, &c, 1);
    grad_stop(&ga, 0, 200, 0, 0);
    grad_stop(&ga, 256, 60, 0, 0);
    rr_paint(cv, &c, (long)(c.r - c.l) * SUB / 2, NULL, 255, 255, 255, 256, &ga);
}

void mc_skin_lcd(McCanvas *cv, const McRect *r)
{
    McRect b;
    Grad   g;
    long   rad = 5 * SUB;
    b = *r; b.t += 1; b.b += 1;
    rr_rgb(cv, &b, rad, 255, 180);
    grad_init(&g, r, 0);
    grad_pal(&g, 0, P_LCD_EDGE_T);
    grad_pal(&g, 256, P_LCD_EDGE_B);
    rr_paint(cv, r, rad, &g, 0, 0, 0, 256, NULL);
    b = *r; b.l++; b.t++; b.r--; b.b--;
    grad_init(&g, &b, 0);
    grad_stop(&g, 0, 168, 180, 160);
    grad_pal(&g, (int)(3L * 256L / (b.b - b.t)), P_LCD_TOP);
    grad_pal(&g, 256, P_LCD_BOT);
    rr_paint(cv, &b, rad - SUB, &g, 0, 0, 0, 256, NULL);
}

void mc_skin_progress(McCanvas *cv, const McRect *r, int permille)
{
    McRect b;
    Grad   g;
    long   w;
    if (permille < 0) return;
    if (permille > 1000) permille = 1000;
    rr_flat(cv, r, 3 * SUB, P_BAR_EDGE, 256);
    b = *r; b.l++; b.t++; b.r--; b.b--;
    rr_flat(cv, &b, 2 * SUB, P_BAR_TRACK, 256);
    w = ((long)(b.r - b.l) * permille + 500L) / 1000L;
    if (w <= 0) return;
    b.r = (short)(b.l + w);
    grad_init(&g, &b, 0);
    grad_pal(&g, 0, P_BAR_TOP);
    grad_pal(&g, 256, P_BAR_BOT);
    rr_paint(cv, &b, 2 * SUB, &g, 0, 0, 0, 256, NULL);
    b.b = (short)(b.t + 1);
    b.l++; if (b.r > b.l + 1) b.r--;
    rr_rgb(cv, &b, 0, 255, 60);
}

void mc_skin_checkbox(McCanvas *cv, const McRect *box, int checked, int state)
{
    long cx = (long)box->l * SUB, cy = (long)box->t * SUB;
    int  v = state == MC_ST_DISABLED ? 140 : 24;
    gel(cv, box, 3 * SUB, checked ? MC_BTN_PRIMARY : MC_BTN_PLAIN, state);
    if (!checked) return;
    seg(cv, cx + 50, cy + 104, cx + 86, cy + 152, 17, v, v, v, 256);
    seg(cv, cx + 86, cy + 152, cx + 168, cy + 34, 17, v, v, v, 256);
}

void mc_skin_dot(McCanvas *cv, int cx, int cy, int kind)
{
    McRect r, h;
    int    c, hollow;
    Grad   ga;
    if (kind == MC_DOT_NONE) return;
    c = (kind == MC_DOT_OK || kind == MC_DOT_OK_GUESS) ? P_DOT_GREEN
      : (kind == MC_DOT_WARN ? P_DOT_AMBER : P_DOT_RED);
    hollow = (kind == MC_DOT_OK_GUESS || kind == MC_DOT_NO_GUESS);
    mc_rect_set(&r, cx - 4, cy - 4, cx + 5, cy + 5);
    rr_flat(cv, &r, 72, c, 256);
    if (hollow)
    {
        mc_rect_set(&h, cx - 2, cy - 2, cx + 3, cy + 3);
        rr_rgb(cv, &h, 40, 255, 256);
        return;
    }
    mc_rect_set(&h, cx - 2, cy - 3, cx + 3, cy);
    grad_init(&ga, &h, 0);
    grad_stop(&ga, 0, 220, 0, 0);
    grad_stop(&ga, 256, 40, 0, 0);
    rr_paint(cv, &h, 40, NULL, 255, 255, 255, 256, &ga);
}

void mc_skin_glyph(McCanvas *cv, const McRect *r, int glyph)
{
    McRect b, h;
    Grad   g, ga;
    int    c;
    long   rad = ((long)(r->r - r->l) * SUB) / 2;
    if (glyph == MC_GLYPH_NONE) return;
    c = glyph == MC_GLYPH_WARN ? P_DOT_AMBER
      : (glyph == MC_GLYPH_ERROR ? P_DOT_RED : P_DOT_AQUA);
    b = *r; b.t += 1; b.b += 1;
    rr_rgb(cv, &b, rad, 255, 150);
    rr_rgb(cv, r, rad, 70, 200);
    b = *r; b.l++; b.t++; b.r--; b.b--;
    grad_init(&g, &b, 0);
    grad_stop(&g, 0, (kPal[c][0] * 3) / 4, (kPal[c][1] * 3) / 4, (kPal[c][2] * 3) / 4);
    grad_pal(&g, 256, c);
    rr_paint(cv, &b, rad - SUB, &g, 0, 0, 0, 256, NULL);
    h = b; h.l += 2; h.r -= 2; h.b = (short)(h.t + (b.b - b.t) / 2);
    grad_init(&ga, &h, 0);
    grad_stop(&ga, 0, 200, 0, 0);
    grad_stop(&ga, 256, 30, 0, 0);
    rr_paint(cv, &h, ((long)(h.b - h.t) * SUB) / 2, NULL, 255, 255, 255, 256, &ga);
}

void mc_skin_caret(McCanvas *cv, int x, int t, int b)
{
    vline(cv, x, t, b, 0, 256);
}
