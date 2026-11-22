/*
    mc_demo.h -- Classic Media Center 0.6: the demo scenes (the U1b
    mock-up screens with placeholder data, no network), the U2 font
    test scene, and the "MC dump N" file header. Portable C89, no
    Toolbox calls: the host renderer (test06/mc_skin_render.c) and the
    Mac app's 'MCdb'/'dump' Apple event (MediaCenter.c) build the very
    same McUi, so a guest dump can be compared with the host PNGs.
*/
#ifndef MC_DEMO_H
#define MC_DEMO_H

#include "mc_layout.h"

#define MC_DEMO_SCENES   10   /* N = 0..9                              */
#define MC_DEMO_FONTTEST 50   /* N = 50: the U2 font test              */
#define MC_DEMO_LIVE     99   /* N = 99: the live window (shell only)  */

/* "1_search", ... "7_inactive", "50_font_test"; NULL if unknown */
const char *mc_demo_name(int n);
/* fill ui with demo scene n (0..9): 0 ok, -1 no such scene. Sets
   draw_field_text = 1 (the field text is a run, not TextEdit). */
int  mc_demo_scene(int n, McUi *ui);

/* the font test (n = 50): metal + a white panel, seven sample rows
   (label + "Results for <star trek>") drawn with the heading chain,
   MC Garamond 14/18/20/24, Hoefler Text 18, Palatino 18 (roles
   MC_F_HEAD / MC_F_TEST_*), each on metal (left) and on white (right),
   and an info panel with two lines the shell supplies (which fonts
   the chain found, font smoothing). */
#define MC_FT_ROWS 7
void mc_demo_font_render(McCanvas *cv, const McRect *clip);
int  mc_demo_font_runs(const McRect *clip, const char *info1,
                       const char *info2, McRun *out, int max);

/* "MC dump N": 16-byte header "MCD1", width, height, rowBytes (each a
   big-endian uint32, written byte by byte), then height rows of
   rowBytes bytes (32-bit xRGB, big-endian, i.e. the GWorld as is) */
#define MC_DUMP_HDR 16
void mc_dump_header(unsigned char *h, long width, long height,
                    long rowBytes);

#endif
