/* mc_test061.c -- HOST ONLY. (0.6.1) G3 policy in mc_core.c: the
   decode-budget table, the verdict text and note, the video trak's
   timing (fps, B-frames), the lighter-file choice. C89. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mc_core.h"

static int npass = 0, nfail = 0;
#define CHECK(c, m) do { if (c) npass++; else { nfail++; printf("  FAIL line %d: %s\n", __LINE__, m); } } while (0)

static const char *gDir = "fixtures";

/* ---------------- the verdict table ---------------- */
typedef struct { long mhz; int w, h, fps, pc, b, v; } Row;
static const Row kTable[] = {
{400,320,240,24,0,0,MC_G3_PLAYS},
{400,320,240,30,0,0,MC_G3_PLAYS},
{400,320,240,24,1,0,MC_G3_PLAYS},
{400,320,240,24,1,1,MC_G3_PLAYS},
{400,320,240,30,1,0,MC_G3_PLAYS},
{400,320,240,30,1,1,MC_G3_STUTTER},
{400,480,360,24,0,0,MC_G3_STUTTER},
{400,480,360,30,0,0,MC_G3_HEAVY},
{400,480,360,24,1,0,MC_G3_HEAVY},
{400,480,360,24,1,1,MC_G3_HEAVY},
{400,480,360,30,1,0,MC_G3_HEAVY},
{400,480,360,30,1,1,MC_G3_HEAVY},
{400,640,480,24,0,0,MC_G3_HEAVY},
{400,640,480,30,0,0,MC_G3_HEAVY},
{400,640,480,24,1,0,MC_G3_HEAVY},
{400,640,480,24,1,1,MC_G3_HEAVY},
{400,640,480,30,1,0,MC_G3_HEAVY},
{400,640,480,30,1,1,MC_G3_HEAVY},
{500,320,240,24,0,0,MC_G3_PLAYS},
{500,320,240,30,0,0,MC_G3_PLAYS},
{500,320,240,24,1,0,MC_G3_PLAYS},
{500,320,240,24,1,1,MC_G3_PLAYS},
{500,320,240,30,1,0,MC_G3_PLAYS},
{500,320,240,30,1,1,MC_G3_PLAYS},
{500,480,360,24,0,0,MC_G3_STUTTER},
{500,480,360,30,0,0,MC_G3_STUTTER},
{500,480,360,24,1,0,MC_G3_STUTTER},
{500,480,360,24,1,1,MC_G3_STUTTER},
{500,480,360,30,1,0,MC_G3_HEAVY},
{500,480,360,30,1,1,MC_G3_HEAVY},
{500,640,480,24,0,0,MC_G3_HEAVY},
{500,640,480,30,0,0,MC_G3_HEAVY},
{500,640,480,24,1,0,MC_G3_HEAVY},
{500,640,480,24,1,1,MC_G3_HEAVY},
{500,640,480,30,1,0,MC_G3_HEAVY},
{500,640,480,30,1,1,MC_G3_HEAVY},
{1250,320,240,24,0,0,MC_G3_PLAYS},
{1250,320,240,30,0,0,MC_G3_PLAYS},
{1250,320,240,24,1,0,MC_G3_PLAYS},
{1250,320,240,24,1,1,MC_G3_PLAYS},
{1250,320,240,30,1,0,MC_G3_PLAYS},
{1250,320,240,30,1,1,MC_G3_PLAYS},
{1250,480,360,24,0,0,MC_G3_PLAYS},
{1250,480,360,30,0,0,MC_G3_PLAYS},
{1250,480,360,24,1,0,MC_G3_PLAYS},
{1250,480,360,24,1,1,MC_G3_PLAYS},
{1250,480,360,30,1,0,MC_G3_PLAYS},
{1250,480,360,30,1,1,MC_G3_PLAYS},
{1250,640,480,24,0,0,MC_G3_PLAYS},
{1250,640,480,30,0,0,MC_G3_PLAYS},
{1250,640,480,24,1,0,MC_G3_STUTTER},
{1250,640,480,24,1,1,MC_G3_STUTTER},
{1250,640,480,30,1,0,MC_G3_STUTTER},
{1250,640,480,30,1,1,MC_G3_STUTTER}
};

static void t_table(void)
{
    unsigned i;
    char m[120];
    long pct;
    for (i = 0; i < sizeof kTable / sizeof kTable[0]; i++)
    {
        const Row *r = &kTable[i];
        int v = mc_g3_verdict(r->mhz, r->w, r->h, (long)r->fps * 100L, r->pc,
                              r->b, &pct);
        sprintf(m, "table: %ld MHz %dx%d %d fps %s%s -> %d (%ld%%), want %d",
                r->mhz, r->w, r->h, r->fps, r->pc ? "Main" : "Base",
                r->b ? "+B" : "", v, pct, r->v);
        CHECK(v == r->v, m);
    }
    /* anchors by hand (Pismo log: 640x480 Main+B = slideshow) */
    CHECK(mc_g3_verdict(500, 640, 480, 2997L, MC_PCLASS_MAIN, 1, &pct) == MC_G3_HEAVY
          && pct == 264, "anchor: Pismo 500, 640x480 Main+B 29.97 fps = 264% -> too heavy");
    CHECK(mc_g3_verdict(500, 320, 240, 3000L, MC_PCLASS_BASE, 0, &pct) == MC_G3_PLAYS
          && pct == 45, "anchor: Pismo 500, 320x240 Baseline 30 fps = 45% -> plays");
    CHECK(mc_g3_verdict(0, 320, 240, 0L, MC_PCLASS_MAIN, 0, &pct) == MC_G3_PLAYS
          && pct == 75, "anchor: MHz unknown (400) + fps unknown (30): 320x240 Main = 75% -> plays");
    CHECK(mc_mbs100(321, 241, 0L) == 21UL * 16UL * 3000UL, "mbs: ceil(321/16)*ceil(241/16)*30");
    CHECK(mc_mbs100(0, 240, 3000L) == 0UL, "mbs: no width -> 0");
}

/* ---------------- verdict text + note per CPU ---------------- */
static void info_main(McAvcInfo *i, int w, int h, long fps100, int b)
{
    memset(i, 0, sizeof *i);
    strcpy(i->codec, "avc1");
    i->profile = 77; i->level = 30; i->width = w; i->height = h;
    i->sps = 1; i->chroma = 1; i->depth_luma = i->depth_chroma = 8;
    i->frame_mbs_only = 1; i->fps100 = fps100; i->bframes = b;
}

static void t_verdict(void)
{
    McAvcInfo i;
    char msg[320];
    int r;

    mc_set_decoder_level(MC_DECODER_MAINHIGH);
    mc_set_cpu(1, 500L);
    CHECK(mc_cpu_g3() == 1 && mc_cpu_mhz() == 500L, "cpu: G3 500");
    info_main(&i, 640, 480, 2397L, 1);
    r = mc_avc_verdict(&i, 0, msg, sizeof msg);
    CHECK(r == MC_PLAY_OK && mc_avc_last_note() == MC_FILE_NOTE_HEAVY
          && mc_avc_last_stutter() == 1, "G3 500: 640x480 Main+B 23.97 -> plays, note HEAVY");
    CHECK(strstr(msg, "G3 500 MHz: 23.97 fps+B, 211% of budget: too heavy (G3).") != NULL, msg);
    info_main(&i, 480, 360, 0L, 0);
    r = mc_avc_verdict(&i, 0, msg, sizeof msg);
    CHECK(r == MC_PLAY_OK && strstr(msg, "fps unknown (30 assumed)") != NULL
          && mc_avc_last_note() == MC_FILE_NOTE_HEAVY, "G3 500: 480x360 Main, fps unknown -> 30 assumed, HEAVY");
    info_main(&i, 480, 360, 2400L, 0);
    (void)mc_avc_verdict(&i, 0, msg, sizeof msg);
    CHECK(mc_avc_last_note() == MC_FILE_NOTE_STUTTER && strstr(msg, ": may stutter.") != NULL,
          "G3 500: 480x360 Main 24 fps -> may stutter");
    info_main(&i, 320, 240, 2997L, 1);
    (void)mc_avc_verdict(&i, 0, msg, sizeof msg);
    CHECK(mc_avc_last_note() == MC_FILE_NOTE_NONE && strstr(msg, ": plays.") != NULL,
          "G3 500: 320x240 Main+B -> plays, no note");
    info_main(&i, 320, 240, 3000L, 0); i.profile = 66;
    (void)mc_avc_verdict(&i, 0, msg, sizeof msg);
    CHECK(mc_avc_last_note() == MC_FILE_NOTE_NONE && strstr(msg, "Baseline H.264") != NULL
          && strstr(msg, "G3 500 MHz: 30.00 fps, 45% of budget: plays.") != NULL, msg);
    memset(&i, 0, sizeof i); strcpy(i.codec, "mp4v"); i.width = 320; i.height = 240;
    r = mc_avc_verdict(&i, 0, msg, sizeof msg);
    CHECK(r == MC_PLAY_NATIVE && mc_avc_last_note() == MC_FILE_NOTE_NONE
          && strstr(msg, "G3") == NULL, "G3: MPEG-4 Part 2 (mp4v) -> QuickTime-native, no budget, no note");
    mc_set_cpu(1, 400L);
    info_main(&i, 320, 240, 3000L, 1);
    (void)mc_avc_verdict(&i, 0, msg, sizeof msg);
    CHECK(mc_avc_last_note() == MC_FILE_NOTE_STUTTER, "G3 400: 320x240 Main+B 30 -> may stutter (83%)");
    mc_set_cpu(1, 0L);
    CHECK(mc_cpu_mhz() == MC_G3_MHZ_UNKNOWN, "G3, Gestalt no clock -> 400 assumed");
    mc_set_cpu(1, 99999L);
    CHECK(mc_cpu_mhz() == MC_G3_MHZ_UNKNOWN, "G3, absurd clock -> 400 assumed");

    /* G4 / unknown: 0.4b size rule only */
    mc_set_cpu(0, 1250L);
    info_main(&i, 640, 480, 2997L, 1);
    (void)mc_avc_verdict(&i, 0, msg, sizeof msg);
    CHECK(mc_avc_last_note() == MC_FILE_NOTE_NONE && strstr(msg, "G3") == NULL,
          "G4 1250: 640x480 Main+B -> plays, no note, no G3 text (unchanged)");
    info_main(&i, 854, 480, 2997L, 1);
    (void)mc_avc_verdict(&i, 0, msg, sizeof msg);
    CHECK(mc_avc_last_note() == MC_FILE_NOTE_STUTTER && strstr(msg, "Large for this Mac") != NULL
          && strstr(msg, "G3") == NULL, "G4 1250: 854x480 -> size rule 'may stutter' (unchanged)");
    mc_set_cpu(0, 0L);
    mc_set_decoder_level(MC_DECODER_BASELINE);
}

/* ---------------- timing from a synthetic moov ---------------- */
static unsigned char gB[2048];
static long gN;
static void p32(unsigned long v) { gB[gN++] = (unsigned char)(v >> 24); gB[gN++] = (unsigned char)(v >> 16); gB[gN++] = (unsigned char)(v >> 8); gB[gN++] = (unsigned char)v; }
static long box(const char *t) { long at = gN; p32(0); memcpy(gB + gN, t, 4); gN += 4; return at; }
static void end(long at) { unsigned long s = (unsigned long)(gN - at); gB[at] = (unsigned char)(s >> 24); gB[at + 1] = (unsigned char)(s >> 16); gB[at + 2] = (unsigned char)(s >> 8); gB[at + 3] = (unsigned char)s; }
static void pz(int n) { while (n-- > 0) gB[gN++] = 0; }

static void trak(int video, unsigned long ts, unsigned long dur,
                 unsigned long n, unsigned long delta, int ctts, int v1)
{
    long t = box("trak"), md = box("mdia"), x, mi, st, sd, e, a;
    x = box("mdhd");
    if (v1) { p32(0x01000000UL); pz(16); p32(ts); p32(0); p32(dur); pz(4); }
    else { p32(0); pz(8); p32(ts); p32(dur); pz(4); }
    end(x);
    x = box("hdlr"); p32(0); p32(0); memcpy(gB + gN, video ? "vide" : "soun", 4); gN += 4; pz(13); end(x);
    mi = box("minf"); st = box("stbl");
    sd = box("stsd"); p32(0); p32(1);
    e = box(video ? "avc1" : "mp4a");
    if (video)
    {
        pz(24); gB[gN++] = 2; gB[gN++] = 128; gB[gN++] = 1; gB[gN++] = 224;  /* 640x480 */
        pz(50);
        a = box("avcC"); gB[gN++] = 1; gB[gN++] = 77; gB[gN++] = 64; gB[gN++] = 30; end(a);
    }
    else pz(28);
    end(e); end(sd);
    x = box("stts"); p32(0); p32(2); p32(n - 1); p32(delta); p32(1); p32(delta); end(x);
    if (ctts) { x = box("ctts"); p32(0); p32(0); end(x); }
    end(st); end(mi); end(md); end(t);
}

static void t_timing(void)
{
    McAvcInfo i;
    long m;

    gN = 0; m = box("moov"); trak(0, 44100, 441000, 430, 1024, 0, 0);
    trak(1, 24000, 240240, 240, 1001, 1, 0); end(m);
    CHECK(mc_avc_profile(gB, gN, &i) == 0 && i.profile == 77 && i.width == 640,
          "timing: moov parsed (sound trak first)");
    CHECK(i.fps100 == 2398L && i.bframes == 1 && i.dur_ms == 10010UL,
          "timing: video trak 240 frames / 10.01 s = 23.98 fps, ctts -> B-frames");
    gN = 0; m = box("moov"); trak(1, 600, 6000, 300, 20, 0, 1); end(m);
    CHECK(mc_avc_profile(gB, gN, &i) == 0 && i.fps100 == 3000L && i.bframes == 0
          && i.dur_ms == 10000UL, "timing: mdhd v1, 30 fps, no ctts");
    gN = 0; m = box("moov"); trak(1, 600, 0, 300, 20, 0, 0); end(m);
    CHECK(mc_avc_profile(gB, gN, &i) == 0 && i.fps100 == 0L && i.dur_ms == 0UL,
          "timing: duration 0 -> fps unknown");
    /* (0.6.1 F1) a real QuickTime .mov: minf holds a second hdlr ('dhlr')
       that must not clear the video flag */
    {
        char path[300];
        FILE *f;
        long n;
        static unsigned char mv[65536];
        sprintf(path, "%s/avc/high_mov.moov", gDir);
        f = fopen(path, "rb");
        CHECK(f != NULL, "high_mov.moov present");
        if (f != NULL)
        {
            n = (long)fread(mv, 1, sizeof mv, f);
            fclose(f);
            CHECK(mc_avc_profile(mv, n, &i) == 0 && i.fps100 == 2500L
                  && i.bframes == 1, "timing: high_mov.moov (QuickTime hdlr in minf) 25 fps, B-frames");
        }
    }
}

/* ---------------- lighter-file choice ---------------- */
static int meta(const char *name, McMeta *m)
{
    char path[300], *b;
    long n;
    FILE *f;
    sprintf(path, "%s/%s", gDir, name);
    f = fopen(path, "rb");
    if (f == NULL) return -1;
    b = (char *)malloc(1 << 16);
    n = (long)fread(b, 1, 1 << 16, f);
    fclose(f);
    mc_meta_begin(m);
    (void)mc_meta_feed(m, b, n);
    free(b);
    return mc_meta_end(m);
}

static int fidx(const McMeta *m, const char *name)
{
    int i;
    for (i = 0; i < m->nfiles; i++) if (strcmp(m->files[i].name, name) == 0) return i;
    return -99;
}

static McMeta gM;

static void t_lighter(void)
{
    int a;
    CHECK(meta("meta_g3_512.json", &gM) == 0 && gM.nfiles == 3, "lighter: 512 fixture, 3 video files");
    a = mc_g3_lighter(&gM, fidx(&gM, "voyager.ia.mp4"), 500L);
    CHECK(a == fidx(&gM, "voyager_512kb.mp4"), "lighter: voyager.ia.mp4 -> voyager_512kb.mp4 (same original)");
    CHECK(mc_g3_lighter(&gM, fidx(&gM, "voyager_512kb.mp4"), 500L) == -1, "lighter: 512Kb itself -> none");
    CHECK(mc_g3_lighter(&gM, fidx(&gM, "other.ia.mp4"), 500L) == -1, "lighter: other film has no light sibling -> none");
    CHECK(mc_g3_lighter(&gM, 99, 500L) == -1, "lighter: bad index -> none");

    CHECK(meta("meta_g3_h264.json", &gM) == 0 && gM.nfiles == 4, "lighter: h264 fixture, 4 video files");
    a = mc_g3_lighter(&gM, fidx(&gM, "film.ia.mp4"), 500L);
    CHECK(a == fidx(&gM, "film_small.mp4"), "lighter: G3 500 640x480 -> 320x240 h.264 (480x360 is over budget)");
    a = mc_g3_lighter(&gM, fidx(&gM, "film.ia.mp4"), 1000L);
    CHECK(a == fidx(&gM, "film.mp4"), "lighter: a 1 GHz G3 -> the largest that fits, 480x360");
    a = mc_g3_lighter(&gM, fidx(&gM, "film_small.mp4"), 500L);
    CHECK(a == -1, "lighter: the smallest already -> none");
    gM.files[fidx(&gM, "film_small.mp4")].state = MC_STATE_NEWER;
    CHECK(mc_g3_lighter(&gM, fidx(&gM, "film.ia.mp4"), 500L) == -1, "lighter: a sibling known not to play is skipped");

    CHECK(meta("meta_g3_none.json", &gM) == 0 && gM.nfiles == 3, "lighter: episodes fixture");
    CHECK(mc_g3_lighter(&gM, fidx(&gM, "ep1.mp4"), 500L) == -1, "lighter: ep1 never gets ep2's 512Kb");
    CHECK(mc_g3_lighter(&gM, fidx(&gM, "ep2.mp4"), 500L) == fidx(&gM, "ep2_512kb.mp4"),
          "lighter: ep2.mp4 (no 'original') -> ep2_512kb.mp4 (original ep2.mp4)");
}

int main(int argc, char **argv)
{
    if (argc > 1) gDir = argv[1];
    t_table(); t_verdict(); t_timing(); t_lighter();
    printf("mc_test061: %d passed, %d failed\n", npass, nfail);
    return nfail != 0;
}
