/*
    mc05_host.c -- HOST ONLY (J5, Media Center 0.5s; J5b 0.5t). mc02_host.c + https
    STREAMING: the app's own mc_stream.c (policy) + mc_proxy_ot.c (the
    Open Transport listener/downstream/HC_Stream adapter, run here over the
    fake OT, which has a POSIX listener since J5) + mc_proxy.c. After the
    hook run, if a film was handed over through the proxy, the rig runs the
    same serve loop as MediaCenter.c's EventLoop (mcs_poll, mcs_status_tick,
    mcs_sleep, Stop/Quit/Esc handlers), with commands read from a control
    file instead of mouse and keys:
      env  MC_CTL        control file: lines "stop", "quit", "esc"
           MC_QT_EVENTS  the stubbed Apple event to QuickTime Player is
                         appended here ("close NAME") and printed
                         (QT_CLOSE_EVENT NAME) -- Python plays QuickTime
           MC_PROXY_PORT instead of 8080 (fallback +1..+9, then OT-assigned)
           MC_PROXY_FAIL 1 = the listener cannot open (-> download)
           MC_CPU_G3     1 = pretend Gestalt said G3;  MC_G3_KBS threshold
    ---- mc02_host.c's notes ----
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
#include "mc_proxy_ot.h"     /* (J5) */
#include "mc_stream.h"       /* (J5) */

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
    (void)mcs_poll();                  /* (J5) the proxy never starves */
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
    fflush(stdout);
    {   /* (J5b) QuickTime Player 6 opens the movie SYNCHRONOUSLY: no event
           loop pass for N s; only Open Transport's deferred tasks run */
        const char *sv = getenv("MC_STARVE_SECS");
        if (sv != NULL && atof(sv) > 0) {
            printf("STARVE begin %.1f s (event loop not polled)\n", atof(sv)); fflush(stdout);
            FakeOT_Starve(atof(sv));
            printf("STARVE end\n"); fflush(stdout);
        }
    }
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

/* ---------------- (J5) streaming glue, as in MediaCenter.c ---------------- */
static unsigned long HNow(void *r) { (void)r; return (unsigned long)TickCount(); }
static void HLog(void *r, const char *t) { (void)r; Say("%s", t); }
static void HStatus(void *r, const char *l) { (void)r; printf("[status] %s\n", l); fflush(stdout); }
/* stub of MediaCenter.c's QTCloseMovie (the Apple event): log it, tell "QuickTime" */
static int HQtClose(void *r, const char *name)
{
    const char *p = getenv("MC_QT_EVENTS");
    (void)r;
    printf("QT_CLOSE_EVENT %s\n", name);
    fflush(stdout);
    if (p != NULL) { FILE *f = fopen(p, "a"); if (f) { fprintf(f, "close %s\n", name); fclose(f); } }
    return 0;
}
static long gCtlAt = 0;
/* next complete line of the control file, or "" */
static void ReadCtl(char *cmd, size_t cap)
{
    const char *p = getenv("MC_CTL");
    FILE *f;
    cmd[0] = 0;
    if (p == NULL || (f = fopen(p, "r")) == NULL) return;
    if (fseek(f, gCtlAt, SEEK_SET) == 0 && fgets(cmd, (int)cap, f) != NULL)
    {
        size_t n = strlen(cmd);
        if (n > 0 && cmd[n - 1] == '\n') { gCtlAt += (long)n; cmd[n - 1] = 0; }
        else cmd[0] = 0;               /* not a complete line yet */
    }
    fclose(f);
}
/* the app's handlers (MediaCenter.c DoStopStreaming / DoQuit / Esc) */
static int gQuit = 0;
static void DoStopStreaming(void) { mcs_stop(); }
static void DoQuit(void) { if (mcs_quit() == MCS_QUIT_NOW) gQuit = 1; }
static void DoEsc(void) { Say("ESC: cancel/back only (Media Center never quits on Esc or Cmd-period).\n"); }
static void ServeLoop(void)
{
    char cmd[64];
    int moved = 0;
    unsigned long passes = 0;
    printf("SERVE_LOOP start\n"); fflush(stdout);
    while (!gQuit)
    {
        long slp = mcs_sleep(moved, 15);
        FakeOT_Poll();                          /* = WaitNextEvent's slice */
        if (slp) usleep((unsigned)(slp * 16667));
        passes++;
        ReadCtl(cmd, sizeof cmd);
        if (strcmp(cmd, "stop") == 0) { printf("CTL stop\n"); DoStopStreaming(); }
        else if (strcmp(cmd, "quit") == 0) { printf("CTL quit\n"); DoQuit(); }
        else if (strcmp(cmd, "esc") == 0) { printf("CTL esc\n"); DoEsc(); }
        fflush(stdout);
        moved = mcs_poll();
        mcs_status_tick();
        if (mcs_quit_ready()) gQuit = 1;
    }
    {
        const McpStats *st = mcp_stats(mcpo_server());
        printf("SERVE_END passes=%lu accepted=%lu requests=%lu bytes_out=%lu upstream_opens=%lu "
               "redirects=%lu adopted=%lu parked=%lu errors=%lu client_aborts=%lu busy=%lu\n",
               passes, st->accepted, st->requests, st->bytes_out, st->upstream_opens, st->redirects,
               st->adopted, st->parked, st->errors, st->client_aborts, st->refused_busy);
    }
}

int main(int argc, char **argv)
{
    int i, n, rc;
    const char *s;
    McHook hook;
    McsEnv senv;

    setvbuf(stdout, NULL, _IOLBF, 0);
    memset(&hook, 0, sizeof hook);

    mc_set_decoder_level(HostDecoderLevel());          /* (0.4) */
    if (argc >= 4 && strcmp(argv[1], "--walk") == 0) return WalkTest(argv[2], argv[3]);
    if (argc < 2) { fprintf(stderr, "usage\n"); return 2; }
    if ((s = getenv("MC_API")) != NULL) mc_api_base = s;
    if ((s = getenv("MC_DL")) != NULL) mc_dl_base = s;
    if ((s = getenv("MC_HTTP_PORT")) != NULL) mcf_test_http_port = (unsigned short)atoi(s);
    if ((s = getenv("MC_HTTPS_PORT")) != NULL) mcf_test_https_port = (unsigned short)atoi(s);
    if ((s = getenv("CANCEL_AT_KB")) != NULL) gCancelKB = strtoul(s, NULL, 10);

    InitOpenTransport();
    /* (J5) streaming */
    if ((s = getenv("MC_PROXY_PORT")) != NULL) mcpo_test_base_port = (unsigned short)atoi(s);
    if ((s = getenv("MC_HTTPS_PORT")) != NULL) mcpo_test_https_port = (unsigned short)atoi(s);
    if ((s = getenv("MC_PROXY_FAIL")) != NULL) mcpo_test_fail = atoi(s);
    mcpo_test_localhost = 1;
    memset(&senv, 0, sizeof senv);
    senv.now = HNow; senv.log = HLog; senv.status = HStatus; senv.qt_close = HQtClose;
    senv.idle = CbIdle;                                   /* (J5b) */
    if ((s = getenv("MC_PREBUF_TEST")) != NULL) mcpo_test_prebuf = strtoul(s, NULL, 10);
    senv.cpu_g3 = (s = getenv("MC_CPU_G3")) != NULL && atoi(s) == 1;
    if ((s = getenv("MC_G3_KBS")) != NULL) senv.g3_kbs = strtoul(s, NULL, 10);
    mcs_init(&senv);
    gEnv.proxy_url = mcs_proxy_url;
    gEnv.proxy_opened = mcs_proxy_opened;
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
        mcs_set_insecure(h.insecure);
        rc = mcf_hook_run(&gEnv, &h, &gS, &gM, gUrls);
        hook = h;
        n = gS.count;
        for (i = 0; i < n && i < 5; i++) Say("  %s | %s\n", gS.hits[i].identifier, gS.hits[i].title);
    }
    printf("RESULT rc=%d mode=%d reused=%d bytes=%lu confirm=%d msg=%s\n", rc, gEnv.mode,
           gEnv.reused, gEnv.dl_bytes, gEnv.need_confirm, gEnv.message);
    if (mcs_streaming())                     /* (J5) as HookRun -> EventLoop */
    {
        mcs_hook_timers((unsigned long)hook.serve, (unsigned long)hook.stop);
        ServeLoop();
    }
    mcs_shutdown();
    printf("EXIT\n");
    return rc == 0 ? 0 : 1;
}
