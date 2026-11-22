/*
    mc_skin_render.c -- host tool: renders the Media Center 0.6 demo
    scenes (mc_demo.c: the same code the Mac app's 'MCdb'/'dump' Apple
    event runs) with mc_layout + mc_skin into xRGB buffers.
    (0.6) + scene 50_font_test; -d OUTDIR writes "MC dump N" files the
    way the app does (header + rows, rowBytes padded to 1984 here) for
    the dump2png.py proof.

    usage: mc_skin_render OUTDIR      writes OUTDIR/<scene>.xrgb (raw
                                      480x360x4, big-endian xRGB) and
                                      OUTDIR/<scene>.runs (text runs),
                                      and prints "scene crc(raw) crc(runs)"
           mc_skin_render -t          self-test: tiled re-render == full
                                      render for every scene; prints crcs

    C89. The same source runs on x86 and (qemu-)PowerPC; the crcs must
    match (golden file: golden.txt).
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mc_demo.h"

#define W MC_WIN_W
#define H MC_WIN_H

static unsigned char gPix[W * H * 4];
static unsigned char gTile[W * H * 4];
static McRun gRuns[200];

/* ---------------- crc32 ---------------- */
static unsigned long crc32(unsigned long c, const unsigned char *p, unsigned long n)
{
    int k;
    c = (~c) & 0xFFFFFFFFUL;
    while (n--)
    {
        c ^= *p++;
        for (k = 0; k < 8; k++)
            c = (c & 1UL) ? ((c >> 1) ^ 0xEDB88320UL) : (c >> 1);
    }
    return (~c) & 0xFFFFFFFFUL;
}

/* ---------------- output ---------------- */
static int write_runs(FILE *f, int n, unsigned long *crc)
{
    int  i;
    char line[600];
    *crc = 0;
    for (i = 0; i < n; i++)
    {
        const McRun *u = &gRuns[i];
        const unsigned char *t = (const unsigned char *)u->text;
        char *p;
        sprintf(line, "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t",
                u->role, u->size, u->align, u->trunc, u->rgb[0], u->rgb[1],
                u->rgb[2], u->emboss, u->box.l, u->box.t, u->box.r, u->box.b,
                u->base);
        p = line + strlen(line);
        while (*t) { sprintf(p, "%02X", *t++); p += 2; }
        *p++ = '\n';
        *p = 0;
        *crc = crc32(*crc, (const unsigned char *)line, (unsigned long)strlen(line));
        if (f != NULL) fputs(line, f);
    }
    return 0;
}

static void paint(const McUi *ui, int n, McCanvas *cv, const McRect *r)
{
    if (n == MC_DEMO_FONTTEST) mc_demo_font_render(cv, r);
    else mc_ui_render(ui, cv, r);
}

static void render_full(const McUi *ui, int n, unsigned char *pix)
{
    McCanvas cv;
    McRect   all;
    memset(pix, 0x55, (size_t)W * H * 4);
    mc_canvas_init(&cv, pix, (long)W * 4, W, H);
    mc_rect_set(&all, 0, 0, W, H);
    paint(ui, n, &cv, &all);
}

/* odd tile sizes, so tile edges cut through every widget somewhere */
static void render_tiled(const McUi *ui, int n, unsigned char *pix)
{
    McCanvas cv;
    McRect   t;
    int      x, y;
    memset(pix, 0xAA, (size_t)W * H * 4);
    mc_canvas_init(&cv, pix, (long)W * 4, W, H);
    for (y = 0; y < H; y += 29)
        for (x = 0; x < W; x += 37)
        {
            mc_rect_set(&t, x, y, x + 37 > W ? W : x + 37, y + 29 > H ? H : y + 29);
            paint(ui, n, &cv, &t);
        }
}

#define FT_INFO1 "Heading chain: MC Garamond (host: the TTF of the 'sfnt' 279)"
#define FT_INFO2 "Host render: Hoefler Text / Palatino = DejaVu Serif Condensed stand-ins; smoothing: n/a"

static const int kList[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, MC_DEMO_FONTTEST };
#define NLIST ((int)(sizeof kList / sizeof kList[0]))

static int scene_runs(const McUi *ui, int n, const McRect *all)
{
    if (n == MC_DEMO_FONTTEST)
        return mc_demo_font_runs(all, FT_INFO1, FT_INFO2, gRuns, 200);
    return mc_ui_runs(ui, all, gRuns, 200);
}

/* -d: "MC dump N" exactly as MediaCenter.c writes it */
static int write_dump(const char *dir, int n, const unsigned char *pix)
{
    static unsigned char row[1984];
    unsigned char h[MC_DUMP_HDR];
    char  path[512];
    FILE *f;
    int   y;
    sprintf(path, "%s/MC dump %d", dir, n);
    f = fopen(path, "wb");
    if (f == NULL) { perror(path); return 1; }
    mc_dump_header(h, W, H, (long)sizeof row);
    fwrite(h, 1, sizeof h, f);
    for (y = 0; y < H; y++)
    {
        memset(row, 0xEE, sizeof row);      /* GWorld padding: junk */
        memcpy(row, pix + (long)y * W * 4, (size_t)W * 4);
        fwrite(row, 1, sizeof row, f);
    }
    fclose(f);
    return 0;
}

int main(int argc, char **argv)
{
    static McUi ui;
    McRect all;
    int    i, k, n, bad = 0;
    unsigned long cr, cu;
    int    selftest = (argc > 1 && strcmp(argv[1], "-t") == 0);
    int    dumps = (argc > 2 && strcmp(argv[1], "-d") == 0);

    if (argc < 2) { fprintf(stderr, "usage: mc_skin_render OUTDIR | -t | -d DIR\n"); return 2; }
    mc_rect_set(&all, 0, 0, W, H);
    for (i = 0; i < NLIST; i++)
    {
        k = kList[i];
        if (k != MC_DEMO_FONTTEST && mc_demo_scene(k, &ui) != 0) return 3;
        render_full(&ui, k, gPix);
        cr = crc32(0, gPix, (unsigned long)W * H * 4);
        n = scene_runs(&ui, k, &all);
        if (dumps)
        {
            if (write_dump(argv[2], k, gPix)) return 1;
            write_runs(NULL, n, &cu);
        }
        else if (selftest)
        {
            write_runs(NULL, n, &cu);
            render_tiled(&ui, k, gTile);
            if (memcmp(gPix, gTile, (size_t)W * H * 4) != 0)
            {
                printf("%s: TILED RENDER DIFFERS\n", mc_demo_name(k));
                bad = 1;
            }
        }
        else
        {
            char  path[512];
            FILE *f;
            sprintf(path, "%s/%s.xrgb", argv[1], mc_demo_name(k));
            f = fopen(path, "wb");
            if (f == NULL) { perror(path); return 1; }
            fwrite(gPix, 1, (size_t)W * H * 4, f);
            fclose(f);
            sprintf(path, "%s/%s.runs", argv[1], mc_demo_name(k));
            f = fopen(path, "w");
            if (f == NULL) { perror(path); return 1; }
            write_runs(f, n, &cu);
            fclose(f);
        }
        printf("%s %08lX %08lX\n", mc_demo_name(k), cr, cu);
    }
    return bad;
}
