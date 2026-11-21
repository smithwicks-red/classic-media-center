/* mc_test04.c -- (0.4) host tests for the decoder level, the SPS reader
   and the Main/High gate in mc_core.c (gcc, C89, ASan+UBSan).
   Fixtures: fixtures/avc/NAME.moov = the 'moov' of tiny libx264 clips (real
   SPS bytes): Baseline, Main (B-frames / bf=0 / fake-interlaced), High
   (mp4, mov, custom scaling matrix, 1280x720, 1920x1080), High 10,
   High 4:2:2, High 4:4:4, and a PAFF/MBAFF interlaced High clip.
   Run: ./run_tests.sh (after mc_test) or ./mc_test04 fixtures */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mc_core.h"

static int gPass = 0, gFail = 0;
#define CHECK(c) do { if (c) gPass++; else { gFail++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECKSTR(a, b) do { if (strcmp((a), (b)) == 0) gPass++; else { \
    gFail++; printf("FAIL %s:%d: got \"%s\" want \"%s\"\n", __FILE__, \
    __LINE__, (a), (b)); } } while (0)

static unsigned char gBuf[65536];
static long gLen;
static const char *gDir;

static int load(const char *name)
{
    char path[512];
    FILE *f;
    sprintf(path, "%s/avc/%s.moov", gDir, name);
    f = fopen(path, "rb");
    if (f == NULL) { printf("missing %s\n", path); gFail++; return -1; }
    gLen = (long)fread(gBuf, 1, sizeof gBuf, f);
    fclose(f);
    return 0;
}

typedef struct
{
    const char *name;
    int profile, chroma, depth, fmo;   /* expected from the SPS */
    int v0, v1;                        /* verdict at level 0 / 1 */
    const char *msg1;                  /* exact sentence at level 1 */
} Case;

static const Case kCases[] = {
  { "base",       66, 1, 8, 1, MC_PLAY_OK, MC_PLAY_OK,
    "Baseline H.264, level 1.3, 320x240." },
  { "main",       77, 1, 8, 1, MC_PLAY_NOT_BASE, MC_PLAY_OK,
    "Main H.264, level 1.3, 320x240." },
  { "main_bf0",   77, 1, 8, 1, MC_PLAY_NOT_BASE, MC_PLAY_OK,
    "Main H.264, level 1.3, 320x240." },
  /* x264 fake-interlaced: progressive pictures, but the SPS says
     frame_mbs_only_flag 0 (checked with ffmpeg trace_headers) */
  { "fakeint",    77, 1, 8, 0, MC_PLAY_NOT_BASE, MC_PLAY_UNTESTED,
    "Main H.264, level 2.1, 320x240, interlaced: untested -- it may not play correctly." },
  /* hand-made SPS (work/mksps.py, cross-checked with ffmpeg
     trace_headers): scaling matrices in the SPS, pic_order_cnt_type 1
     with a 2^22 offset, an emulation-prevention byte, MBAFF */
  { "hand_sps",  100, 1, 8, 0, MC_PLAY_NOT_BASE, MC_PLAY_UNTESTED,
    "High H.264, level 1.3, 320x240, interlaced: untested -- it may not play correctly." },
  { "high",      100, 1, 8, 1, MC_PLAY_NOT_BASE, MC_PLAY_OK,
    "High H.264, level 1.3, 320x240." },
  { "high_mov",  100, 1, 8, 1, MC_PLAY_NOT_BASE, MC_PLAY_OK,
    "High H.264, level 1.3, 320x240." },
  { "high_cqm",  100, 1, 8, 1, MC_PLAY_NOT_BASE, MC_PLAY_OK,
    "High H.264, level 1.3, 320x240." },
  { "hd720",     100, 1, 8, 1, MC_PLAY_NOT_BASE, MC_PLAY_TOO_BIG,
    "This High H.264 video is 1280x720; Main/High plays up to 854x480 here." },
  { "big1080",   100, 1, 8, 1, MC_PLAY_NOT_BASE, MC_PLAY_TOO_BIG,
    "This High H.264 video is 1920x1080; Main/High plays up to 854x480 here." },
  { "high10",    110, 1, 10, 1, MC_PLAY_NOT_BASE, MC_PLAY_NOT_BASE,
    "This video is 10-bit (High 10 profile); this decoder plays 8-bit video only." },
  { "high422",   122, 2, 8, 1, MC_PLAY_NOT_BASE, MC_PLAY_NOT_BASE,
    "This video is 4:2:2 colour (High 4:2:2 profile); this decoder plays 4:2:0 colour only." },
  { "high444",   244, 3, 8, 1, MC_PLAY_NOT_BASE, MC_PLAY_NOT_BASE,
    "This video is 4:4:4 colour (High 4:4:4 profile); this decoder plays 4:2:0 colour only." },
  { "interlaced",100, 1, 8, 0, MC_PLAY_NOT_BASE, MC_PLAY_UNTESTED,
    "High H.264, level 2.1, 320x240, interlaced: untested -- it may not play correctly." },
  { NULL, 0, 0, 0, 0, 0, 0, NULL }
};

static void test_fixtures(void)
{
    McAvcInfo info;
    char msg[200], want0[200];
    int i, r;

    for (i = 0; kCases[i].name != NULL; i++)
    {
        const Case *c = &kCases[i];
        if (load(c->name) != 0) continue;
        r = mc_avc_profile(gBuf, gLen, &info);
        CHECK(r == 0);
        CHECK(info.profile == c->profile);
        CHECK(info.sps == 1);
        CHECK(info.chroma == c->chroma);
        CHECK(info.depth_luma == c->depth && info.depth_chroma == c->depth);
        CHECK(info.frame_mbs_only == c->fmo);
        /* level 0: exactly the 0.3 rule and sentence */
        CHECK(mc_avc_verdict_at(&info, r, 0, msg, sizeof msg) == c->v0);
        if (c->profile != 66)
        {
            sprintf(want0, "This video is %s profile (%d); this decoder "
                    "plays Baseline only. Install H264 Decoder 17c and "
                    "AVCDecLib to play it.", c->profile == 77 ? "Main"
                    : c->profile == 100 ? "High" : c->profile == 110
                    ? "High 10" : c->profile == 122 ? "High 4:2:2"
                    : "High 4:4:4", c->profile);
            CHECKSTR(msg, want0);
        }
        /* level 1 */
        CHECK(mc_avc_verdict_at(&info, r, 1, msg, sizeof msg) == c->v1);
        CHECKSTR(msg, c->msg1);
        /* the setter drives mc_avc_verdict */
        mc_set_decoder_level(1);
        CHECK(mc_avc_verdict(&info, r, msg, sizeof msg) == c->v1);
        mc_set_decoder_level(0);
        CHECK(mc_avc_verdict(&info, r, msg, sizeof msg) == c->v0);
        printf("  %-10s L0=%d L1=%d  %s\n", c->name, c->v0, c->v1, c->msg1);
    }
}

/* edit the avcC of high.moov in place (offset of its payload) */
static long avcc_at(void)
{
    long i;
    for (i = 0; i + 4 < gLen; i++)
        if (memcmp(gBuf + i, "avcC", 4) == 0) return i + 4;
    return -1;
}

static void test_synthetic(void)
{
    McAvcInfo info;
    char msg[200];
    long a;
    int r;

    /* avc3 Main at level 1: refused (decoder + rewriter are avc1-only) */
    if (load("main") == 0)
    {
        long i;
        for (i = 0; i + 4 < gLen; i++)
            if (memcmp(gBuf + i, "avc1", 4) == 0) { gBuf[i + 3] = '3'; break; }
        r = mc_avc_profile(gBuf, gLen, &info);
        CHECK(r == 0);
        CHECK(mc_avc_verdict_at(&info, r, 1, msg, sizeof msg) == MC_PLAY_NOT_BASE);
        CHECKSTR(msg, "This video is Main profile (77) in an 'avc3' track; "
                 "this decoder plays Main and High from 'avc1' tracks only.");
        CHECK(mc_avc_verdict_at(&info, r, 0, msg, sizeof msg) == MC_PLAY_NOT_BASE);
    }
    /* profile 88 (Extended) at level 1 */
    if (load("main") == 0 && (a = avcc_at()) > 0)
    {
        gBuf[a + 1] = 88;
        r = mc_avc_profile(gBuf, gLen, &info);
        CHECK(mc_avc_verdict_at(&info, r, 1, msg, sizeof msg) == MC_PLAY_NOT_BASE);
        CHECKSTR(msg, "This video is Extended profile (88); this decoder "
                 "plays Baseline, Main and High only.");
    }
    /* no SPS in avcC (numOfSPS = 0): High refused as unknown at level 1 */
    if (load("high") == 0 && (a = avcc_at()) > 0)
    {
        gBuf[a + 5] = 0xE0;
        r = mc_avc_profile(gBuf, gLen, &info);
        CHECK(r == 0 && info.sps == 0 && info.chroma == 1);
        CHECK(mc_avc_verdict_at(&info, r, 1, msg, sizeof msg) == MC_PLAY_UNKNOWN);
        CHECKSTR(msg, "This High profile video's settings (its SPS) could "
                 "not be read.");
        CHECK(mc_avc_verdict_at(&info, r, 0, msg, sizeof msg) == MC_PLAY_NOT_BASE);
    }
    /* SPS length cut to 3 bytes: not read, no crash */
    if (load("high") == 0 && (a = avcc_at()) > 0)
    {
        gBuf[a + 6] = 0; gBuf[a + 7] = 3;
        r = mc_avc_profile(gBuf, gLen, &info);
        CHECK(r == 0 && info.sps == 0);
    }
    /* level setter clamps, hints and marks */
    mc_set_decoder_level(7);
    CHECK(mc_decoder_level() == MC_DECODER_MAINHIGH);
    CHECK(mc_format_hint("h.264 IA") == MC_HINT_PLAYS);
    CHECK(mc_format_hint("h.264") == MC_HINT_PLAYS);
    CHECK(mc_format_hint("MPEG4") == MC_HINT_NEWER);
    CHECK(mc_format_hint("h.264 HD") == MC_HINT_NEWER);
    {
        McFile f;
        memset(&f, 0, sizeof f);
        f.hint = MC_HINT_NEWER; f.state = MC_STATE_HINT;
        CHECKSTR(mc_file_mark(&f), "may not play?");
        CHECKSTR(mc_file_token(&f), "hint:may-not-play");
        f.state = MC_STATE_NEWER;
        CHECKSTR(mc_file_mark(&f), "will not play");
        CHECKSTR(mc_file_token(&f), "fact:will-not-play");
        f.state = MC_STATE_PLAYS;
        CHECKSTR(mc_file_mark(&f), "PLAYS NOW");
        f.state = MC_STATE_HINT; f.hint = MC_HINT_PLAYS;
        CHECKSTR(mc_file_token(&f), "hint:plays-now");
        mc_set_decoder_level(-3);
        CHECK(mc_decoder_level() == MC_DECODER_BASELINE);
        CHECK(mc_format_hint("h.264 IA") == MC_HINT_NEWER);
        f.hint = MC_HINT_NEWER;
        CHECKSTR(mc_file_mark(&f), "needs newer decoder?");
        f.state = MC_STATE_NEWER;
        CHECKSTR(mc_file_token(&f), "fact:needs-newer-decoder");
    }
}

/* (0.4b) Main/High size rule in 16x16 macroblocks, level 1:
   <= 1200 plays, <= 1620 plays + "may stutter", more = TOO_BIG.
   1201 and 1621 are prime, so the only frames with exactly those MB
   counts are 16-pixel strips; those are past the 1280x1024 fence
   that is kept as well, so they come out TOO_BIG. The nearest real
   frames above 1200 (641x480 = 1230, 448x688 = 1204) are checked. */
typedef struct { int w, h, prof, fmo; long mbs; int v; const char *msg; } MbCase;
static const MbCase kMb[] = {
  {  640,  480, 100, 1, 1200, MC_PLAY_OK, "High H.264, level 3.1, 640x480." },
  {  640,  472, 100, 1, 1200, MC_PLAY_OK, "High H.264, level 3.1, 640x472." },
  {   16,19216, 100, 1, 1201, MC_PLAY_TOO_BIG,
    "This High H.264 video is 16x19216; Main/High plays up to 854x480 here." },
  {  448,  688, 100, 1, 1204, MC_PLAY_OK,
    "High H.264, level 3.1, 448x688. Large for this Mac: it may stutter." },
  {  641,  480,  77, 1, 1230, MC_PLAY_OK,
    "Main H.264, level 3.1, 641x480. Large for this Mac: it may stutter." },
  {  720,  480, 100, 1, 1350, MC_PLAY_OK,
    "High H.264, level 3.1, 720x480. Large for this Mac: it may stutter." },
  {  854,  480, 100, 1, 1620, MC_PLAY_OK,
    "High H.264, level 3.1, 854x480. Large for this Mac: it may stutter." },
  {  720,  576, 100, 1, 1620, MC_PLAY_OK,
    "High H.264, level 3.1, 720x576. Large for this Mac: it may stutter." },
  {  720,  576, 100, 0, 1620, MC_PLAY_UNTESTED,
    "High H.264, level 3.1, 720x576, interlaced: untested -- it may not "
    "play correctly. Large for this Mac: it may stutter." },
  {   16,25936, 100, 1, 1621, MC_PLAY_TOO_BIG,
    "This High H.264 video is 16x25936; Main/High plays up to 854x480 here." },
  {  855,  480, 100, 1, 1620, MC_PLAY_OK,
    "High H.264, level 3.1, 855x480. Large for this Mac: it may stutter." },
  {  865,  480, 100, 1, 1650, MC_PLAY_TOO_BIG,
    "This High H.264 video is 865x480; Main/High plays up to 854x480 here." },
  {  720,  577, 100, 1, 1665, MC_PLAY_TOO_BIG,
    "This High H.264 video is 720x577; Main/High plays up to 854x480 here." },
  { 1280,  720, 100, 1, 3600, MC_PLAY_TOO_BIG,
    "This High H.264 video is 1280x720; Main/High plays up to 854x480 here." },
  { 1280,  720,  77, 1, 3600, MC_PLAY_TOO_BIG,
    "This Main H.264 video is 1280x720; Main/High plays up to 854x480 here." },
  {    0,    0, 0, 0, 0, 0, NULL }
};

static void test_mbrule(void)
{
    McAvcInfo info;
    char msg[200];
    int i, r;

    for (i = 0; kMb[i].msg != NULL; i++)
    {
        const MbCase *c = &kMb[i];
        memset(&info, 0, sizeof info);
        strcpy(info.codec, "avc1");
        info.profile = c->prof; info.level = 31;
        info.width = c->w; info.height = c->h;
        info.sps = 1; info.chroma = 1;
        info.depth_luma = info.depth_chroma = 8;
        info.frame_mbs_only = c->fmo;
        CHECK(((long)(c->w + 15) / 16) * ((c->h + 15) / 16) == c->mbs);
        r = mc_avc_verdict_at(&info, 0, 1, msg, sizeof msg);
        CHECK(r == c->v);
        CHECKSTR(msg, c->msg);
        /* level 0: refused as 0.3, with the install hint */
        r = mc_avc_verdict_at(&info, 0, 0, msg, sizeof msg);
        CHECK(r == MC_PLAY_NOT_BASE);
        CHECK(strstr(msg, "plays Baseline only. Install H264 Decoder 17c "
                      "and AVCDecLib to play it.") != NULL);
        printf("  MB %5ld %5dx%-5d L1=%d  %s\n", c->mbs, c->w, c->h, c->v, c->msg);
    }
    /* Baseline at level 1 keeps the 0.3 1280x1024 rule */
    memset(&info, 0, sizeof info);
    strcpy(info.codec, "avc1");
    info.profile = 66; info.level = 31; info.width = 1280; info.height = 720;
    info.sps = 1; info.chroma = 1; info.depth_luma = info.depth_chroma = 8;
    info.frame_mbs_only = 1;
    CHECK(mc_avc_verdict_at(&info, 0, 1, msg, sizeof msg) == MC_PLAY_OK);
    CHECKSTR(msg, "Baseline H.264, level 3.1, 1280x720.");
    info.width = 1281;
    CHECK(mc_avc_verdict_at(&info, 0, 1, msg, sizeof msg) == MC_PLAY_TOO_BIG);
    CHECKSTR(msg, "This video is 1281x720; the limit is 1280x1024.");
}

/* the reader on corrupted / truncated SPS bytes: any answer, no crash */
static void test_sps_fuzz(void)
{
    static const char *kNames[] = { "high", "high_cqm", "interlaced",
                                    "high444", "main", NULL };
    unsigned char save[65536];
    unsigned long seed = 99;
    McAvcInfo info;
    char msg[200];
    long a, i, k, len;
    int j, r, bad = 0;

    for (j = 0; kNames[j] != NULL; j++)
    {
        if (load(kNames[j]) != 0) continue;
        a = avcc_at();
        if (a < 0) continue;
        memcpy(save, gBuf, (size_t)gLen);
        len = gLen;
        for (i = 0; i < 20000; i++)
        {
            memcpy(gBuf, save, (size_t)len);
            for (k = 0; k < 1 + (long)(i % 4); k++)
            {
                seed = seed * 1103515245UL + 12345UL;
                gBuf[a + 8 + (long)((seed >> 16) % 24)] =
                    (unsigned char)(seed >> 8);
            }
            r = mc_avc_profile(gBuf, len, &info);
            if (r != 0) { bad++; continue; }
            r = mc_avc_verdict_at(&info, r, 1, msg, sizeof msg);
            if (strlen(msg) >= sizeof msg) bad++;
            if (info.sps && (info.chroma < 0 || info.chroma > 3
                || info.depth_luma > 14 || info.depth_chroma > 14)) bad++;
        }
        /* SPS length claims more than the box holds, each value */
        for (i = 0; i < 256; i++)
        {
            memcpy(gBuf, save, (size_t)len);
            gBuf[a + 7] = (unsigned char)i;
            r = mc_avc_profile(gBuf, len, &info);
            if (r == 0) (void)mc_avc_verdict_at(&info, r, 1, msg, sizeof msg);
        }
    }
    CHECK(bad == 0);
}

int main(int argc, char **argv)
{
    gDir = argc > 1 ? argv[1] : "fixtures";
    test_fixtures();
    test_synthetic();
    test_mbrule();
    test_sps_fuzz();
    printf("mc_test04: %d passed, %d failed\n", gPass, gFail);
    return gFail ? 1 : 0;
}
