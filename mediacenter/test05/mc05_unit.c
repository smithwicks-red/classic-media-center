/* mc05_unit.c -- HOST ONLY (J5). mc_stream.c's policy with a scripted
   clock and a STUB mc_proxy_ot (connection counts set by the test):
   " (MC)" names, mvhd rates, the dref forms, the G3 hint, Stop (event
   first, 2 s, then cancel), Quit (warn, 10 s window), sleeps, hook timers.
   gcc -std=c89 -pedantic ... mc05_unit.c ../mc_stream.c ../../proxy/src/mc_proxy.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mc_proxy.h"
#include "mc_proxy_ot.h"
#include "mc_stream.h"

static int npass = 0, nfail = 0;
#define CHECK(c, m) do { if (c) npass++; else { nfail++; printf("  FAIL line %d: %s\n", __LINE__, m); } } while (0)

/* ---- stub mc_proxy_ot ---- */
unsigned short mcpo_test_base_port = 0, mcpo_test_https_port = 0;
int mcpo_test_localhost = 0, mcpo_test_fail = 0;
static McpServer *gS = NULL;
static int g_open = 0, g_active = 0, g_parked = 0, g_cancels = 0;
static unsigned long g_now = 1000;
static char g_trace[2048];
static void tr(const char *s) { strcat(g_trace, s); strcat(g_trace, ";"); }
static unsigned long o_now(void *c) { (void)c; return g_now; }
static void *o_alloc(void *c, unsigned long n) { (void)c; return malloc(n); }
static void o_rel(void *c, void *p) { (void)c; free(p); }
int mcpo_open(void (*log)(void *, const char *), void *ref, unsigned short *port)
{
    McpOps ops;
    McpConfig cfg;
    (void)log; (void)ref;
    if (mcpo_test_fail) return -1;
    if (gS == NULL)
    {
        memset(&ops, 0, sizeof ops); memset(&cfg, 0, sizeof cfg);
        ops.now = o_now; ops.alloc = o_alloc; ops.release = o_rel;
        gS = mcp_new(&ops, &cfg);
    }
    g_open = 1; *port = 8080; return 0;
}
int mcpo_is_open(void) { return g_open; }
unsigned short mcpo_port(void) { return 8080; }
McpServer *mcpo_server(void) { return gS; }
void mcpo_set_insecure(int on) { (void)on; }
static int g_pb_tick = 0;
int mcpo_poll(void) { if (g_pb_tick) g_now++; return 0; }   /* (J5b) time passes in pre-buffer idle loops */
int mcpo_active(void) { return g_active; }
int mcpo_parked(void) { return g_parked; }
int mcpo_closing(void) { return 0; }
void mcpo_cancel_all(void) { g_active = g_parked = 0; g_cancels++; tr("cancel"); }
void mcpo_close(void) { if (gS) mcp_free(gS); gS = NULL; g_open = 0; }
/* (J5b) pre-buffer stub: start returns g_pb_start[] in turn (then 0),
   poll returns 0 g_pb_polls times, then g_pb_end; got grows 64 KB a poll */
unsigned long mcpo_test_prebuf = 0;
unsigned long mcpo_test_prebuf_max = 0;
static unsigned long g_pb_want = 0;
static int g_pb_start[4], g_pb_ns = 0, g_pb_si = 0, g_pb_polls = 2, g_pb_end = 1, g_pb_pi = 0, g_pb_cancel = 0, g_pb_calls = 0;
static char g_pb_target[300];
int mcpo_prebuf_start(const char *target, unsigned long want)
{
    g_pb_want = want; g_pb_calls++; g_pb_pi = 0; strncpy(g_pb_target, target, 299);
    return g_pb_si < g_pb_ns ? g_pb_start[g_pb_si++] : 0;
}
int mcpo_prebuf_poll(unsigned long *got, unsigned long *want, unsigned long *total)
{
    *want = 524288UL; *total = 6553600UL; *got = (unsigned long)g_pb_pi * 65536UL;
    g_now += 5;
    if (g_pb_pi++ < g_pb_polls) return 0;
    *got = 524288UL;
    return g_pb_end;
}
void mcpo_prebuf_cancel(void) { g_pb_cancel++; }

/* ---- McsEnv ---- */
static int g_closes = 0, g_hint = 0;
static char g_status[256];
static unsigned long e_now(void *r) { (void)r; return g_now; }
static char g_logall[8192];
static void e_log(void *r, const char *t) { (void)r; if (strstr(t, "G3 HINT")) g_hint++; if (strlen(g_logall) + strlen(t) < sizeof g_logall - 1) strcat(g_logall, t); }
static void e_status(void *r, const char *l) { (void)r; strncpy(g_status, l, 255); }
static int e_idle_stop(void *r, int busy) { (void)r; (void)busy; return 1; }
static int e_close(void *r, const char *n) { char t[64]; (void)r; g_closes++; sprintf(t, "close %.40s", n); tr(t); return 0; }

static void env(int g3)
{
    McsEnv e;
    memset(&e, 0, sizeof e);
    e.now = e_now; e.log = e_log; e.status = e_status; e.qt_close = e_close; e.cpu_g3 = g3;
    mcs_init(&e);
    g_trace[0] = 0; g_closes = g_hint = g_cancels = 0; g_active = g_parked = 0;
}

static void name_is(const char *in, const char *want)
{
    char b[64];
    strcpy(b, in);
    mcs_mc_name(b);
    if (strcmp(b, want) != 0) printf("  name \"%s\" -> \"%s\" (want \"%s\")\n", in, b, want);
    CHECK(strcmp(b, want) == 0 && strlen(b) <= 31, "mc name");
}

static void be32(unsigned char *p, unsigned long v) { p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16); p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v; }
static long mk_moov(unsigned char *m, int v1, unsigned long scale, unsigned long dur)
{
    memset(m, 0, 200);
    be32(m, 200); memcpy(m + 4, "moov", 4);
    be32(m + 8, 120); memcpy(m + 12, "mvhd", 4);
    m[16] = (unsigned char)v1;
    if (v1) { be32(m + 36, scale); be32(m + 40, 0); be32(m + 44, dur); }
    else { be32(m + 28, scale); be32(m + 32, dur); }
    return 200;
}

static void t_names(void)
{
    printf("names\n");
    name_is("httpsonly_512kb.mov", "httpsonly_512kb (MC).mov");
    name_is("abcdefghijklmnopqrstuvwxyz1.mov", "abcdefghijklmnopqrstuv (MC).mov");
    name_is("abcdefghijklmnopqrstuv (MC).mov", "abcdefghijklmnopqrstuv (MC).mov");
    name_is("Film.MOV", "Film (MC).mov");
    name_is("abcdefghijklmnopqrstu  xyz.mov", "abcdefghijklmnopqrstu (MC).mov");
    name_is(".mov", "film (MC).mov");
    name_is("noext", "noext (MC).mov");
}

static void t_rates(void)
{
    unsigned char m[200];
    long n;
    printf("rates\n");
    n = mk_moov(m, 0, 600, 600UL * 30UL);
    CHECK(mcs_avg_kbs(m, n, 1950970UL) == 1950970UL / 1024UL / 30UL, "mvhd v0");
    n = mk_moov(m, 1, 90000, 90000UL * 100UL);
    CHECK(mcs_avg_kbs(m, n, 10240000UL) == 100, "mvhd v1");
    n = mk_moov(m, 0, 600, 0);
    CHECK(mcs_avg_kbs(m, n, 1000000UL) == 0, "zero duration -> unknown");
    n = mk_moov(m, 0, 600, 6000);
    CHECK(mcs_avg_kbs(m, n, MCP_UNKNOWN) == 0, "unknown size -> unknown");
    CHECK(mcs_avg_kbs(m, 20, 1000000UL) == 0, "truncated moov -> unknown");
}

static void t_dref(void)
{
    char d[1200], nm[64];
    unsigned char m[200];
    long n;
    printf("dref + G3 hint\n");
    env(1);
    n = mk_moov(m, 0, 1, 100);                          /* 100 s */
    strcpy(nm, "f x.mov");
    CHECK(mcs_proxy_url(NULL, "https://archive.org/download/ID/f%20x.mp4", 6553600UL, m, n, 0, d, nm) == 0, "proxy_url ok");
    CHECK(strcmp(d, "http://127.0.0.1:8080/a/archive.org/download/ID/f%20x.mp4") == 0, "archive.org /a/ form");
    CHECK(strcmp(nm, "f x (MC).mov") == 0, "name (MC)");
    CHECK(g_hint == 0, "64 KB/s on a G3: no hint (threshold is 'above')");
    CHECK(mcs_proxy_url(NULL, "https://ia800.us.archive.org/1/items/x/y.mp4", 10240000UL, m, n, 0, d, nm) == 0
          && strcmp(d, "http://127.0.0.1:8080/a/ia800.us.archive.org/1/items/x/y.mp4") == 0, "storage node /a/ form");
    CHECK(g_hint == 1 && strstr(g_status, "G3 HINT") != NULL, "100 KB/s on a G3: hint in log + status");
    CHECK(mcs_proxy_url(NULL, "https://example.com/v/f.mp4", 0, m, n, 0, d, nm) == 0
          && strncmp(d, "http://127.0.0.1:8080/t/", 24) == 0 && strcmp(d + 32, "/f.mp4") == 0, "other host -> /t/token/f.mp4");
    CHECK(mcs_proxy_url(NULL, "https://archive.org:8443/download/ID/f.mp4", 0, m, n, 0, d, nm) == 0
          && strncmp(d, "http://127.0.0.1:8080/t/", 24) == 0, "other port -> /t/ token");
    env(0);
    CHECK(mcs_proxy_url(NULL, "https://ia800.us.archive.org/1/items/x/y.mp4", 10240000UL, m, n, 0, d, nm) == 0 && g_hint == 0,
          "not a G3: no hint");
    mcpo_test_fail = 1;
    CHECK(mcs_proxy_url(NULL, "https://archive.org/download/ID/f.mp4", 0, m, n, 0, d, nm) == -1, "no listener -> -1");
    mcpo_test_fail = 0;
    /* (J5b) pre-buffer policy */
    g_pb_tick = 1;
    env(0); g_pb_calls = 0;
    CHECK(mcs_proxy_url(NULL, "https://archive.org/download/ID/f.mp4", 0, m, n, 0, d, nm) == 0
          && strcmp(g_pb_target, "/a/archive.org/download/ID/f.mp4") == 0 && g_pb_calls == 1,
          "pre-buffer: target = the dref's path, before returning 0");
    CHECK(strcmp(g_status, "Buffering 100%") == 0, "status ends with Buffering 100%");
    env(0); g_pb_calls = 0; g_pb_end = -1;
    CHECK(mcs_proxy_url(NULL, "https://archive.org/download/ID/f.mp4", 0, m, n, 0, d, nm) == -1 && g_pb_calls == 2,
          "pre-buffer fails twice (one retry) -> -1 (download)");
    env(0); g_pb_calls = 0; g_pb_end = 1; g_pb_ns = 2; g_pb_si = 0; g_pb_start[0] = 1; g_pb_start[1] = 1;
    CHECK(mcs_proxy_url(NULL, "https://archive.org/download/ID/f.mp4", 0, m, n, 0, d, nm) == 0 && g_pb_calls == 3,
          "busy pre-buffer (start = 1) is retried, then works");
    {   /* Stop while buffering -> -2, cancel called */
        McsEnv e;
        memset(&e, 0, sizeof e);
        e.now = e_now; e.log = e_log; e.status = e_status; e.qt_close = e_close; e.idle = e_idle_stop;
        mcs_init(&e);
        g_pb_ns = 0; g_pb_cancel = 0; g_pb_polls = 5;
        CHECK(mcs_proxy_url(NULL, "https://archive.org/download/ID/f.mp4", 0, m, n, 0, d, nm) == -2 && g_pb_cancel == 1,
              "Stop while buffering -> -2 and the fetch is cancelled");
        g_pb_polls = 2;
    }
    /* (0.5u) pre-buffer size = [0, mediaFrom + 512 KB), at most 4 MB */
    env(0); g_pb_calls = 0; g_pb_end = 1; g_pb_ns = 0; g_pb_want = 0;
    CHECK(mcs_proxy_url(NULL, "https://archive.org/download/ID/f.mp4", 0, m, n, 0, d, nm) == 0
          && g_pb_want == MCPO_PREBUF, "moov at the end / unknown (mediaFrom 0): 512 KB");
    CHECK(mcs_proxy_url(NULL, "https://archive.org/download/ID/f.mp4", 313256504UL, m, n, 1750144UL, d, nm) == 0
          && g_pb_want == 1750144UL + MCPO_PREBUF, "Jimmy and the Federation Gang: 1750144 + 512 KB");
    CHECK(strstr(g_logall, "PREBUF 2274432 bytes (moov ends at 1750144)") != NULL, "log: PREBUF N bytes (moov ends at M)");
    CHECK(mcs_proxy_url(NULL, "https://archive.org/download/ID/f.mp4", 0, m, n, 3UL * 1024UL * 1024UL, d, nm) == 0
          && g_pb_want == 3UL * 1024UL * 1024UL + MCPO_PREBUF, "3 MB moov: 3.5 MB, under the cap");
    g_pb_calls = 0;
    CHECK(mcs_proxy_url(NULL, "https://archive.org/download/ID/f.mp4", 0, m, n, 3700UL * 1024UL, d, nm) == -1
          && g_pb_calls == 0, "3.7 MB moov: over the 4 MB cap -> -1 (download), no pre-buffer started");
    CHECK(strstr(g_logall, "index too large to stream") != NULL, "log: index too large to stream");
    mcpo_test_prebuf_max = 1000000UL; mcpo_test_prebuf = 100000UL;
    CHECK(mcs_proxy_url(NULL, "https://archive.org/download/ID/f.mp4", 0, m, n, 900000UL, d, nm) == 0
          && g_pb_want == 1000000UL, "exactly the cap: allowed");
    CHECK(mcs_proxy_url(NULL, "https://archive.org/download/ID/f.mp4", 0, m, n, 900001UL, d, nm) == -1,
          "one byte over the cap: download");
    mcpo_test_prebuf_max = 0; mcpo_test_prebuf = 0;
    CHECK(mcs_proxy_url(NULL, "https://archive.org/download/ID/f.mp4", 100000UL, m, n, 20000UL, d, nm) == 0
          && g_pb_want == 100000UL, "tiny film: pre-buffer clipped to the file size");
    g_pb_tick = 0;
}

static void t_stop(void)
{
    unsigned long t0;
    printf("stop\n");
    env(0);
    mcs_proxy_opened(NULL, "a (MC).mov");
    g_active = 1;
    CHECK(mcs_streaming() && mcs_serving(), "streaming + serving");
    CHECK(mcs_sleep(1, 15) == 0 && mcs_sleep(0, 15) == 1, "sleep 0 after bytes, 1 while serving");
    t0 = g_now; mcs_stop();
    CHECK(g_closes == 1 && g_cancels == 0 && mcs_stopping(), "Stop: Apple event first, nothing cut yet");
    g_now = t0 + MCS_STOP_WAIT - 1; mcs_poll();
    CHECK(g_cancels == 0, "still waiting at 119 ticks");
    g_now = t0 + MCS_STOP_WAIT; mcs_poll();
    CHECK(g_cancels == 1 && !mcs_stopping() && strcmp(g_trace, "close a (MC).mov;cancel;") == 0, "cut at 120 ticks, after the event");
    CHECK(!mcs_streaming() && !mcs_quit_ready(), "after Stop: not streaming; a manual Stop does not quit");
    CHECK(mcs_sleep(0, 15) == 15, "idle sleep again");
    /* QuickTime obeys: its connection goes away at once */
    env(0);
    mcs_proxy_opened(NULL, "b (MC).mov");
    g_active = 2; t0 = g_now; mcs_stop();
    g_now += 10; g_active = 0; mcs_poll();
    CHECK(g_cancels == 1 && !mcs_stopping(), "QuickTime let go: finished before the 2 s");
    /* nothing to stop */
    env(0);
    mcs_stop();
    CHECK(g_closes == 0 && !mcs_stopping(), "Stop with nothing streaming: no event");
}

static void t_quit(void)
{
    printf("quit\n");
    env(0);
    CHECK(mcs_quit() == MCS_QUIT_NOW, "nothing streams: quit at once");
    mcs_proxy_opened(NULL, "c (MC).mov");
    g_active = 1;
    CHECK(mcs_quit() == MCS_QUIT_WARNED && g_closes == 0 && strstr(g_status, "Quit again") != NULL, "serving: warn only");
    g_now += MCS_QUIT_AGAIN - 1;
    CHECK(mcs_quit() == MCS_QUIT_CLOSING && g_closes == 1, "second Quit within 10 s: close event, wait");
    CHECK(!mcs_quit_ready(), "not ready while QuickTime has 2 s");
    g_now += MCS_STOP_WAIT; mcs_poll();
    CHECK(mcs_quit_ready() && g_cancels == 1, "then cut and ready to quit");
    env(0);
    mcs_proxy_opened(NULL, "d (MC).mov");
    g_active = 1;
    CHECK(mcs_quit() == MCS_QUIT_WARNED, "warn");
    g_now += MCS_QUIT_AGAIN + 1; mcs_poll();
    CHECK(mcs_quit() == MCS_QUIT_WARNED && g_closes == 0, "a Quit after 10 s warns again");
    g_active = 0;
    env(0);
    mcs_proxy_opened(NULL, "e (MC).mov");                /* open, not reading */
    CHECK(mcs_quit() == MCS_QUIT_CLOSING && g_closes == 1, "not serving, movie open: close it, no warning");
    mcs_poll();
    CHECK(mcs_quit_ready(), "ready at the next pass");
    env(0);
    mcs_proxy_opened(NULL, "f (MC).mov");
    g_active = 1; mcs_stop();
    CHECK(mcs_quit() == MCS_QUIT_CLOSING && !mcs_quit_ready(), "Quit during a Stop: waits for it");
    g_now += MCS_STOP_WAIT; mcs_poll();
    CHECK(mcs_quit_ready(), "then quits");
}

static void t_hook(void)
{
    printf("hook timers\n");
    env(0);
    mcs_proxy_opened(NULL, "g (MC).mov");
    g_active = 1;
    mcs_hook_timers(0, 2);
    g_now += 119; mcs_poll();
    CHECK(g_closes == 0, "stop 2: not yet at 119 ticks");
    g_now += 1; mcs_poll();
    CHECK(g_closes == 1, "stop 2: Stop at 120 ticks");
    g_now += MCS_STOP_WAIT; mcs_poll();
    CHECK(mcs_quit_ready(), "hook run quits after its Stop");
    env(0);
    mcs_proxy_opened(NULL, "h (MC).mov");
    mcs_hook_timers(3, 0);
    g_now += 180; mcs_poll(); mcs_poll();
    CHECK(g_closes == 1 && mcs_quit_ready(), "serve 3: closes the movie and quits");
    env(0);
    mcs_proxy_opened(NULL, "i (MC).mov");
    mcs_hook_timers(0, 0);
    g_now += 600UL * 60UL; mcs_poll(); mcs_poll();
    CHECK(mcs_quit_ready(), "hook run never serves forever (600 s)");
}

static void t_status(void)
{
    char s[200];
    printf("status\n");
    env(0);
    mcs_proxy_opened(NULL, "j (MC).mov");
    g_active = 1;
    mcs_status_text(s, sizeof s);
    CHECK(strncmp(s, "Streaming via secure proxy: ", 28) == 0 && (strstr(s, "KB/s") || strstr(s, "buffered")) && strstr(s, "1 connection."), "status text");
    g_active = 0;
    mcs_status_text(s, sizeof s);
    CHECK(strstr(s, "idle") != NULL && strstr(s, "Stop ends") != NULL, "idle status text");
}

/* (0.6.1) second play: names, reuse, the record */
static void t_second_play(void)
{
    char           o[64], big[64];
    int            i, ok = 1;
    unsigned short port;
    (void)mcpo_open(NULL, NULL, &port);   /* the stub listener is "open" */
    printf("second play (0.6.1)\n");
    CHECK(mcs_is_mc_name("Film (MC).mov") && mcs_is_mc_name("film (mc).MOV") && !mcs_is_mc_name("Film.mov")
          && !mcs_is_mc_name("(MC).mov"), "our names: ' (MC).mov' tail");
    mcs_try_name("Film (MC).mov", 1, o);
    CHECK(strcmp(o, "Film (MC).mov") == 0, "first play: the name as is");
    mcs_try_name("Film (MC).mov", 2, o);
    CHECK(strcmp(o, "Film 2 (MC).mov") == 0, "second try keeps the (MC) tail: 'Film 2 (MC).mov' (0.6: '(M 2.mov')");
    mcs_try_name("Film (MC).mov", 10, o);
    CHECK(strcmp(o, "Film 10 (MC).mov") == 0, "try 10");
    mcs_try_name("Plain.mov", 2, o);
    CHECK(strcmp(o, "Plain 2.mov") == 0, "a name without our tail: 'Plain 2.mov'");
    strcpy(big, "ABCDEFGHIJKLMNOPQRSTUV (MC).mov");
    CHECK(strlen(big) == 31, "test name is 31 chars");
    mcs_try_name(big, 1, o);
    CHECK(strcmp(o, big) == 0, "31 chars, first play: unchanged");
    for (i = 2; i <= 99; i++)
    {
        mcs_try_name(big, i, o);
        if (strlen(o) > 31 || !mcs_is_mc_name(o) || strncmp(o, "ABCDEFGHIJ", 10) != 0) ok = 0;
    }
    CHECK(ok, "31-char name, tries 2..99: <= 31 chars, always ends ' (MC).mov'");
    mcs_try_name(big, 2, o);
    CHECK(strcmp(o, "ABCDEFGHIJKLMNOPQRST 2 (MC).mov") == 0 && strlen(o) == 31, "31-char name, second: base cut to 20, exactly 31");
    mcs_try_name("ABCDEFGHIJKLMNOPQRS  (MC).mov", 2, o);
    CHECK(strstr(o, "S 2 (MC).mov") != NULL && strstr(o, "  ") == NULL, "no double space where the base was cut");
    /* the record and the close use the name really saved */
    env(0);
    mcs_saved_as("Film 2 (MC).mov");
    mcs_proxy_opened(NULL, "Film (MC).mov");
    CHECK(strstr(g_logall, "STREAMING \"Film 2 (MC).mov\"") != NULL, "STREAMING line: the saved name");
    g_active = 1; mcs_stop();
    CHECK(strcmp(g_trace, "close Film 2 (MC).mov;") == 0, "Stop's Apple event names the saved window");
    /* in use: no rewrite while streaming; free after Stop */
    env(0);
    CHECK(!mcs_movie_in_use("Film (MC).mov"), "nothing streaming: not in use (reuse the file)");
    mcs_proxy_opened(NULL, "Film (MC).mov");
    CHECK(mcs_movie_in_use("Film (MC).mov") && mcs_movie_in_use("FILM (MC).MOV") && !mcs_movie_in_use("Other (MC).mov"),
          "streaming: in use (uniquify instead)");
    g_active = 1; mcs_stop(); g_now += MCS_STOP_WAIT; mcs_poll();
    CHECK(!mcs_movie_in_use("Film (MC).mov"), "after Stop: free again (second play rewrites it)");
    mcs_proxy_opened(NULL, "Film (MC).mov");
    CHECK(strstr(g_logall, "STREAMING \"Film (MC).mov\"") != NULL && mcs_movie_in_use("Film (MC).mov"), "second play, same name recorded");
}

/* (0.6.1) KB/s over the last 8 samples; 'buffered' when nothing moved */
static void t_rate(void)
{
    unsigned long t = 1000, b = 0;
    int           i;
    char          s[200];
    printf("rate (0.6.1)\n");
    mcs_rate_reset();
    CHECK(mcs_rate_feed(t, b) == 0, "first sample: no rate yet");
    t += 60; b += 100UL * 1024UL;
    CHECK(mcs_rate_feed(t, b) == 100, "100 KB in 1 s = 100 KB/s");
    /* a burst then silence: a 1 s window would say 0, the 8 s window keeps the average */
    for (i = 0; i < 4; i++) { t += 60; (void)mcs_rate_feed(t, b); }
    CHECK(mcs_rate_feed(t, b) <= 100 && mcs_rate_feed(t + 60, b) > 0, "burst + 4 s idle: still > 0 (average, not instant)");
    for (i = 0; i < 12; i++) { t += 60; (void)mcs_rate_feed(t, b); }
    CHECK(mcs_rate_feed(t + 60, b) == 0, "no bytes for the whole 8 s window: 0");
    /* steady 200 KB/s */
    mcs_rate_reset(); t = 5000; b = 0;
    for (i = 0; i < 20; i++) { (void)mcs_rate_feed(t, b); t += 60; b += 200UL * 1024UL; }
    CHECK(mcs_rate_feed(t, b) == 200, "steady 200 KB/s, 20 samples (ring wrapped)");
    CHECK(mcs_rate_feed(t + 30, b + 100UL * 1024UL) == 200, "half-second step still 200");
    /* big transfer: no overflow */
    mcs_rate_reset(); t = 0; b = 0;
    (void)mcs_rate_feed(t, b); t += 480; b += 400UL * 1024UL * 1024UL;
    CHECK(mcs_rate_feed(t, b) == 51200, "400 MB in 8 s = 51200 KB/s, no overflow");
    /* the status text: buffered (no 0 KB/s) vs rate */
    env(0);
    mcs_proxy_opened(NULL, "r (MC).mov");
    g_active = 1;
    (void)mcs_rate_feed(100, 0); (void)mcs_rate_feed(160, 0);
    mcs_status_text(s, sizeof s);
    CHECK(strncmp(s, "Streaming via secure proxy: buffered, ", 38) == 0 && strstr(s, "0 KB/s") == NULL && strstr(s, "1 connection."), "connection open, nothing fetched for the window: 'buffered', never '0 KB/s'");
    (void)mcs_rate_feed(220, 300UL * 1024UL);
    mcs_status_text(s, sizeof s);
    CHECK(strstr(s, "KB/s,") != NULL && strstr(s, "buffered") == NULL, "bytes moved: a KB/s figure");
    /* (0.6.1 F1) a trickle that rounds to 0 KB/s is a slow link, not "buffered" */
    mcs_rate_reset();
    (void)mcs_rate_feed(100, 0); (void)mcs_rate_feed(160, 500UL);
    mcs_status_text(s, sizeof s);
    CHECK(strstr(s, "buffered") == NULL && strstr(s, "0 KB/s,") != NULL, "500 bytes in the window (rate rounds to 0): '0 KB/s', not 'buffered'");
    mcs_rate_reset();
    (void)mcs_rate_feed(100, 700UL); (void)mcs_rate_feed(160, 700UL);
    mcs_status_text(s, sizeof s);
    CHECK(strncmp(s, "Streaming via secure proxy: buffered, ", 38) == 0, "zero bytes in the window (non-zero total): 'buffered'");
}

/* (0.6.1) G3 policy: stream vs download from the pre-buffer's rate */
static void t_netrule(void)
{
    unsigned long k;
    /* the Pismo log: 557,712 B in 335 ticks = 97 KB/s; film 93 KB/s */
    CHECK(mcs_net_download(557712UL, 335UL, 93UL, 0L, &k) == 1 && k == 97UL,
          "net: Pismo star-trek 97 KB/s < 93 x 1.25 -> download");
    CHECK(mcs_net_download(557712UL, 335UL, 64UL, 0L, &k) == 0 && k == 97UL,
          "net: 97 KB/s >= 64 x 1.25 (512Kb MPEG4) -> stream");
    CHECK(mcs_net_download(557712UL, 335UL, 78UL, 0L, &k) == 0,
          "net: edge: 97.55 KB/s >= 78 x 1.25 = 97.5 -> stream");
    CHECK(mcs_net_download(557712UL, 335UL, 79UL, 0L, &k) == 1,
          "net: edge: 97.55 < 79 x 1.25 = 98.75 -> download");
    CHECK(mcs_net_download(557712UL, 335UL, 93UL, 100L, &k) == 0,
          "net: margin 100%: 97 >= 93 -> stream");
    CHECK(mcs_net_download(100000UL, 335UL, 500UL, 0L, &k) == 0 && k == 0,
          "net: < 128 KB pre-buffered: not judged (stream)");
    CHECK(mcs_net_download(557712UL, 10UL, 5000UL, 0L, &k) == 0 && k == 0,
          "net: < 30 ticks (fast LAN / host loopback): not judged");
    CHECK(mcs_net_download(557712UL, 335UL, 0UL, 0L, &k) == 0 && k == 97UL,
          "net: film rate unknown: stream (rate still reported)");
    CHECK(MCS_STREAM_MARGIN_PCT == 125L, "net: MCS_STREAM_MARGIN_PCT 125");
}

int main(void)
{
    t_netrule();
    t_second_play(); t_rate();
    t_names(); t_rates(); t_dref(); t_stop(); t_quit(); t_hook(); t_status();
    mcs_shutdown();
    printf("mc05_unit: %d passed, %d failed\n", npass, nfail);
    return nfail != 0;
}
