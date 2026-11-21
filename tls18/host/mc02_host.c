/*
    mc02_host.c -- HOST ONLY. Runs Media Center 0.2's real fetch/play
    logic (mediacenter/mc_fetch.c + mc_core.c + H264Stream.c's moov
    rewrite + https_client.c + macTLS) against local servers, through
    the fake Toolbox / fake Open Transport of this rig.

    usage: mc02_host QUERY [download] [insecure] [confirm] [list] [file N]
      (0.3: the words after QUERY become the hook file's later lines and
       go through mc_hook_parse + mcf_hook_run, as in the app)
      env  MC_BIG_KB       big-download threshold (default 32 MB)
      env  MC_API          e.g. https://localhost:19443  (search, metadata)
           MC_DL           e.g. http://localhost:19444   (download base)
           MC_HTTP_PORT / MC_HTTPS_PORT  port pair for mcf_to_https
           CANCEL_AT_KB    "press Cmd-period" after this many KB
           OUT_DIR         where open_ref writes the reference movie
           FAKE_MAC_DIR    fake disk (App/Media Center Downloads/...)
    prints:  RESULT rc=.. mode=.. reused=.. bytes=.. msg=...
    or       mc02_host --walk FILE.mp4 OUT.mov   (walker only, random chunks)
      env  MC_DECODER_LEVEL  (0.4) host stub of the app's DecoderLevel():
           0 = Baseline-only decoder (default: -DMC_HOST_DECODER_LEVEL),
           1 = H264 Decoder 17 + AVCDecLib
*/
#define H264STREAM_HOSTTEST 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *MemAlloc(long n) { return malloc((size_t)(n ? n : 1)); }
static void  MemFree(void *p) { free(p); }
static int   NetOpen(const char *h, unsigned short p) { (void)h; (void)p; return -1; }
static long  NetSend(const char *b, long n) { (void)b; (void)n; return -1; }
static long  NetRecv(char *b, long m) { (void)b; (void)m; return -1; }
static void  NetClose(void) {}
static int   SaveAndOpenMovie(const char *name, const unsigned char *d, long n)
{ (void)name; (void)d; (void)n; return -1; }

#include "H264Stream.c"
#include "mc_fetch.h"

static McfEnv   gEnv;
static McSearch gS;
static McMeta   gM;
static char     gUrls[MC_MAXATTEMPT][MC_URLMAX];
static unsigned long gCancelKB = 0;
static int      gStatusLines = 0;

static void CbLog(void *r, const char *t) { (void)r; Say("%s", t); }
static void CbStatus(void *r, const char *l)
{ (void)r; gStatusLines++; if (gStatusLines % 20 == 1) printf("[status] %s\n", l); }
void FakeOT_Poll(void);
void OSTLS_CollectEntropy(void);
int usleep(unsigned int);
static int CbIdle(void *r, int busy)
{
    (void)r;
    FakeOT_Poll();
    OSTLS_CollectEntropy();
    if (!busy) usleep(300);
    if (gCancelKB && gEnv.dl_bytes / 1024UL >= gCancelKB) { printf("[test] Cmd-period\n"); return 1; }
    return 0;
}
/* (0.4) host stand-in for MediaCenter.c's DecoderLevel() */
#ifndef MC_HOST_DECODER_LEVEL
#define MC_HOST_DECODER_LEVEL 0
#endif
static int HostDecoderLevel(void)
{
    const char *s = getenv("MC_DECODER_LEVEL");
    return s != NULL ? atoi(s) : MC_HOST_DECODER_LEVEL;
}

static unsigned char *CbRebuild(const unsigned char *m, long l, const char *u, long *o)
{ return BuildReferenceMovie(m, l, u, o); }
static int CbOpenRef(void *r, const char *name, const unsigned char *d, long n)
{
    char p[1024]; FILE *f; const char *od = getenv("OUT_DIR");
    (void)r;
    sprintf(p, "%s/%s", od ? od : ".", name);
    f = fopen(p, "wb"); if (!f) return -1;
    fwrite(d, 1, (size_t)n, f); fclose(f);
    printf("OPEN_REF %s %ld\n", p, n);
    return 0;
}
static int CbOpenLocal(void *r, const FSSpec *s)
{
    const char *root = getenv("FAKE_MAC_DIR");
    (void)r;
    printf("OPEN_LOCAL %s/App/%s/%.*s\n", root ? root : "fakemac", MCF_DL_FOLDER,
           (int)s->name[0], (const char *)s->name + 1);
    return 0;
}

static int WalkOut(void *ref, const unsigned char *p, unsigned long n)
{ return fwrite(p, 1, (size_t)n, (FILE *)ref) != (size_t)n; }

/* walker only: original file -> local movie, fed in random pieces */
static int WalkTest(const char *in, const char *outp)
{
    FILE *f = fopen(in, "rb"), *o;
    unsigned char *buf, *moov = NULL, *tail;
    long len, at = 0, tl;
    McfWalk w;
    unsigned long ml;
    if (!f) return 2;
    fseek(f, 0, SEEK_END); len = ftell(f); fseek(f, 0, SEEK_SET);
    buf = malloc((size_t)len); if (fread(buf, 1, (size_t)len, f) != (size_t)len) return 2; fclose(f);
    gEnv.alloc = MemAlloc; gEnv.release = MemFree; gEnv.log = CbLog;
    /* pass 1: fetch walker (with Range jumps simulated) */
    mcf_walk_init(&w, MCF_WALK_FETCH, &gEnv);
    srand(7);
    while (at < len) {
        long n = 1 + rand() % 3000; if (n > len - at) n = len - at;
        if (mcf_walk_feed(&w, buf + at, (unsigned long)n)) {
            if (w.stop == 2) { at = (long)w.range_from; w.stop = 0; w.pos = w.range_from; w.in_box = 0; w.hlen = 0; w.hneed = 8; continue; }
            break;
        }
        at += n;
    }
    if (w.stop != 1) { printf("WALK fetch failed: %d %s\n", w.err, w.msg); return 1; }
    moov = w.m; w.m = NULL; ml = w.mlen;
    tail = BuildReferenceMovie(moov, (long)ml, NULL, &tl);
    o = fopen(outp, "wb");
    mcf_walk_init(&w, MCF_WALK_COPY, &gEnv);
    w.ref_moov = moov; w.ref_len = ml;
    w.out = WalkOut; w.out_ref = o;
    for (at = 0; at < len; ) {
        long n = 1 + rand() % 5000; if (n > len - at) n = len - at;
        if (mcf_walk_feed(&w, buf + at, (unsigned long)n)) { printf("WALK copy failed: %d %s\n", w.err, w.msg); return 1; }
        at += n;
    }
    if (w.in_box || w.hlen || !w.seen_moov || !w.seen_mdat) { printf("WALK incomplete\n"); return 1; }
    fwrite(tail, 1, (size_t)tl, o); fclose(o);
    printf("WALK ok: %ld + %ld bytes\n", len, tl);
    free(buf); free(moov); free(tail);
    return 0;
}

int main(int argc, char **argv)
{
    int i, n, rc;
    const char *s;

    mc_set_decoder_level(HostDecoderLevel());          /* (0.4) */
    if (argc >= 4 && strcmp(argv[1], "--walk") == 0) return WalkTest(argv[2], argv[3]);
    if (argc < 2) { fprintf(stderr, "usage\n"); return 2; }
    if ((s = getenv("MC_API")) != NULL) mc_api_base = s;
    if ((s = getenv("MC_DL")) != NULL) mc_dl_base = s;
    if ((s = getenv("MC_HTTP_PORT")) != NULL) mcf_test_http_port = (unsigned short)atoi(s);
    if ((s = getenv("MC_HTTPS_PORT")) != NULL) mcf_test_https_port = (unsigned short)atoi(s);
    if ((s = getenv("CANCEL_AT_KB")) != NULL) gCancelKB = strtoul(s, NULL, 10);

    InitOpenTransport();
    gEnv.log = CbLog; gEnv.status = CbStatus; gEnv.idle = CbIdle;
    gEnv.alloc = MemAlloc; gEnv.release = MemFree; gEnv.rebuild = CbRebuild;
    gEnv.open_ref = CbOpenRef; gEnv.open_local = CbOpenLocal;
    gEnv.movie_name = MovieNameFromURL;
    gEnv.user_agent = "ClassicMediaCenter/0.4 (host test)";
    gEnv.app_vref = -1; gEnv.app_dir = 1;
    if ((s = getenv("MC_BIG_KB")) != NULL) gEnv.big_bytes = strtoul(s, NULL, 10) * 1024UL;
    {
        static char text[2048];
        McHook h;
        strcpy(text, argv[1]);
        for (i = 2; i < argc; i++) { strcat(text, "\r"); strcat(text, argv[i]);
            if (strcmp(argv[i], "file") == 0 && i + 1 < argc) { strcat(text, " "); strcat(text, argv[++i]); } }
        if (!mc_hook_parse(text, &h)) { printf("RESULT rc=-1 mode=-1 reused=0 bytes=0 msg=empty\n"); return 1; }
        rc = mcf_hook_run(&gEnv, &h, &gS, &gM, gUrls);
        n = gS.count;
        for (i = 0; i < n && i < 5; i++) Say("  %s | %s\n", gS.hits[i].identifier, gS.hits[i].title);
    }
    printf("RESULT rc=%d mode=%d reused=%d bytes=%lu confirm=%d msg=%s\n", rc, gEnv.mode,
           gEnv.reused, gEnv.dl_bytes, gEnv.need_confirm, gEnv.message);
    return rc == 0 ? 0 : 1;
}
