/*
    mc_stream.c -- Classic Media Center 0.5s: streaming policy. See
    mc_stream.h. Strict C89; no Toolbox calls (McsEnv supplies them).
    Text for the log only: "\n" is the log's line end, as in mc_fetch.c
    (no protocol bytes are written here).
*/
#include <stdio.h>
#include <string.h>

#include "mc_proxy.h"
#include "mc_proxy_ot.h"
#include "mc_stream.h"

static McsEnv        gE;
static int           gInit = 0;
static char          gMovie[MCS_MAX_MOVIES][MCS_NAME_MAX + 1];
static int           gNMovies = 0;
static int           gStopping = 0;
static unsigned long gStopT0 = 0;
static int           gQuitAfterStop = 0;
static int           gQuitReady = 0;
static unsigned long gQuitWarnT = 0;
static int           gHook = 0;
static unsigned long gHookServe = 0, gHookStop = 0;   /* deadlines (ticks) */
static int           gInsecure = 0;
static unsigned long gRateT = 0, gKBs = 0;
/* (0.6.1) rate = bytes over the last MCS_RATE_N seconds (ring of samples
   taken every MCS_STATUS_EVERY ticks), not over the last second alone:
   QuickTime fetches in bursts and then rests while its buffer is full */
static unsigned long gRt[MCS_RATE_N + 1], gRb[MCS_RATE_N + 1];
static int           gRn = 0;
static char          gLastStatus[160];
static char          gSaved[MCS_NAME_MAX + 1];   /* (0.6.1) real saved name */

static unsigned long Now(void)
{
    return gE.now != NULL ? gE.now(gE.ref) : 0UL;
}

static void Log(const char *t)
{
    if (gE.log != NULL) gE.log(gE.ref, t);
}

static void Status(const char *line)
{
    strncpy(gLastStatus, line, sizeof gLastStatus - 1);
    gLastStatus[sizeof gLastStatus - 1] = 0;
    if (gE.status != NULL) gE.status(gE.ref, line);
}

/* mc_proxy_ot's lines have no line end */
static void ProxyLine(void *ref, const char *line)
{
    char t[240];
    (void)ref;
    strncpy(t, line, sizeof t - 2);
    t[sizeof t - 2] = 0;
    strcat(t, "\n");
    Log(t);
}

void mcs_init(const McsEnv *env)
{
    gE = *env;
    gInit = 1;
    gNMovies = 0;
    gStopping = gQuitAfterStop = gQuitReady = gHook = 0;
    gQuitWarnT = gHookServe = gHookStop = 0;
    gRateT = gKBs = 0;
    gRn = 0;
    gLastStatus[0] = 0;
    gSaved[0] = 0;
}

void mcs_set_insecure(int on) { gInsecure = on; }

/* ------------------------------------------------------------------ */
/* names, rates                                                        */
/* ------------------------------------------------------------------ */

static int LowerEq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
    {
        int x = (unsigned char)*a, y = (unsigned char)*b;
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return 0;
    }
    return *a == 0 && *b == 0;
}

void mcs_mc_name(char *name)
{
    static const char kTail[] = " (MC).mov";
    unsigned long n = (unsigned long)strlen(name);
    unsigned long room = MCS_NAME_MAX - (sizeof kTail - 1);   /* 22 */
    if (n >= 4 && LowerEq(name + n - 4, ".mov")) n -= 4;
    if (n >= 5 && strncmp(name + n - 5, " (MC)", 5) == 0) n -= 5;
    if (n > room) n = room;
    while (n > 0 && (name[n - 1] == ' ' || name[n - 1] == '.')) n--;
    if (n == 0) { strcpy(name, "film"); n = 4; }
    name[n] = 0;
    strcat(name, kTail);
}

/* (0.6.1) "x (MC).mov" is one of OUR reference movies */
int mcs_is_mc_name(const char *name)
{
    static const char kTail[] = " (MC).mov";
    unsigned long n = (unsigned long)strlen(name), k = sizeof kTail - 1;
    return n > k && LowerEq(name + n - k, kTail);
}

/* (0.6.1) the name for try number attempt (1 = name itself, 2.. = a
   number before our " (MC)" tail and ".mov": "x 2 (MC).mov"), at most 31
   chars. The 0.5u/0.6 loop cut the END of the name, which turned
   "... (MC).mov" into "... (M 2.mov". Same name rule as the Finder's. */
void mcs_try_name(const char *name, int attempt, char *out)
{
    char          num[12], base[MCS_NAME_MAX + 1];
    const char   *tail = "";
    unsigned long n = (unsigned long)strlen(name), room;
    if (attempt < 2)
    {
        if (n > MCS_NAME_MAX) n = MCS_NAME_MAX;
        memcpy(out, name, (size_t)n);
        out[n] = 0;
        return;
    }
    if (n >= 4 && LowerEq(name + n - 4, ".mov")) n -= 4;
    if (n >= 5 && strncmp(name + n - 5, " (MC)", 5) == 0)
    {
        n -= 5;
        tail = " (MC)";
    }
    sprintf(num, " %d", attempt);
    room = MCS_NAME_MAX - 4UL - strlen(tail) - strlen(num);
    if (n > room) n = room;
    memcpy(base, name, (size_t)n);
    while (n > 0 && (base[n - 1] == ' ' || base[n - 1] == '.')) n--;
    base[n] = 0;
    sprintf(out, "%s%s%s.mov", base, num, tail);
}

/* (0.6.1) is a movie of this name being streamed right now? (its file
   must not be rewritten then) */
int mcs_movie_in_use(const char *name)
{
    int i;
    if (!mcs_streaming()) return 0;
    for (i = 0; i < gNMovies; i++)
        if (LowerEq(gMovie[i], name)) return 1;
    return 0;
}

/* (0.6.1) the file's real name, when open_ref saved it under another one
   than asked for; mcs_proxy_opened records and logs THIS one, so the
   Apple event of Stop/Quit names the window that really exists */
void mcs_saved_as(const char *actual)
{
    strncpy(gSaved, actual, MCS_NAME_MAX);
    gSaved[MCS_NAME_MAX] = 0;
}

static unsigned long Be32(const unsigned char *p)
{
    return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16)
         | ((unsigned long)p[2] << 8) | (unsigned long)p[3];
}

unsigned long mcs_avg_kbs(const unsigned char *moov, long len,
                          unsigned long size)
{
    long          i;
    unsigned long scale, dur, secs;
    if (moov == NULL || size == 0 || size == MCP_UNKNOWN) return 0;
    for (i = 4; i + 36 <= len && i < 4096; i++)
    {
        if (memcmp(moov + i, "mvhd", 4) != 0) continue;
        if (moov[i + 4] == 1)
        {   /* version 1: 64-bit times; the high half of duration must be 0 */
            if (i + 36 > len) return 0;
            scale = Be32(moov + i + 24);
            if (Be32(moov + i + 28) != 0) return 0;
            dur = Be32(moov + i + 32);
        }
        else
        {
            scale = Be32(moov + i + 16);
            dur = Be32(moov + i + 20);
        }
        if (scale == 0 || dur == 0) return 0;
        secs = dur / scale;
        if (secs == 0) secs = 1;
        return size / 1024UL / secs;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* the play hand-off                                                   */
/* ------------------------------------------------------------------ */

static int Allowed(const char *host)
{
    unsigned long n = (unsigned long)strlen(host);
    if (strcmp(host, "archive.org") == 0) return 1;
    if (n > 12 && strcmp(host + n - 12, ".archive.org") == 0) return 1;
    return mcpo_test_localhost && strcmp(host, "localhost") == 0;
}

/* (0.5t) one idle step while pre-buffering: 1 = stopped */
static int PreIdle(void)
{
    if (gE.idle != NULL) return gE.idle(gE.ref, 1) != 0;
    (void)mcs_poll();
    return 0;
}

/* (0.5t) the film's first bytes into RAM through the proxy core, BEFORE
   the reference movie exists: 0 ok, -1 failed, -2 stopped.
   (0.5u) want = [0, want): the whole index plus MCPO_PREBUF of media */
static int Prebuffer1(const char *target, unsigned long need);

static int Prebuffer(const char *target, unsigned long want)
{
    int r = Prebuffer1(target, want);
    if (r == -1)
    {   /* one more try: a TLS upstream can fail right after the moov's */
        unsigned long t0 = Now();
        Log("proxy: pre-buffering failed; trying once more in 0.5 s.\n");
        while (Now() - t0 < 30UL)
            if (PreIdle()) return -2;
        r = Prebuffer1(target, want);
    }
    return r;
}

/* (0.6.1) the last pre-buffer: bytes and ticks (network rate) */
static unsigned long gPreGot = 0, gPreTicks = 0;

int mcs_net_download(unsigned long bytes, unsigned long ticks,
                     unsigned long film_kbs, long margin_pct,
                     unsigned long *net_kbs)
{
    double kbs;
    if (net_kbs != NULL) *net_kbs = 0;
    if (bytes < MCS_NET_MIN_BYTES || ticks < MCS_NET_MIN_TICKS) return 0;
    kbs = (double)bytes / 1024.0 * 60.0 / (double)ticks;
    if (net_kbs != NULL) *net_kbs = (unsigned long)kbs;
    if (film_kbs == 0) return 0;
    if (margin_pct <= 0) margin_pct = MCS_STREAM_MARGIN_PCT;
    return kbs * 100.0 < (double)film_kbs * (double)margin_pct;
}

static int Prebuffer1(const char *target, unsigned long need)
{
    unsigned long tw = Now(), t0, got = 0, want = 0, total = 0, pct, last = 999;
    int  r;
    char t[160];
    gPreGot = gPreTicks = 0;
    if (target == NULL) return -1;
    for (;;)
    {
        r = mcpo_prebuf_start(target, need);
        if (r != 1) break;
        if (Now() - tw > MCS_PREBUF_WAIT)
        {
            Log("proxy: the previous film's pre-buffer is still being sent.\n");
            return -1;
        }
        if (PreIdle()) return -2;
    }
    if (r < 0) return -1;
    /* (0.6.1 F1) the rate timer starts after the wait for the previous
       pre-buffer, so that wait is not counted as slow link */
    t0 = Now();
    Status("Buffering 0%");
    for (;;)
    {
        r = mcpo_prebuf_poll(&got, &want, &total);
        if (r != 0) break;
        pct = want != 0 ? (got / 1024UL) * 100UL / (want / 1024UL + 1UL) : 0;
        if (pct > 99) pct = 99;
        if (pct != last)
        {
            sprintf(t, "Buffering %lu%%", pct);
            Status(t);
            last = pct;
        }
        if (PreIdle())
        {
            mcpo_prebuf_cancel();
            Log("Buffering stopped.\n");
            return -2;
        }
    }
    if (r < 0) return -1;
    gPreGot = got;
    gPreTicks = Now() - t0;
    sprintf(t, "Buffered the first %lu KB of %lu KB in %lu ticks.\n",
            got / 1024UL, total / 1024UL, gPreTicks);
    Log(t);
    Status("Buffering 100%");
    return 0;
}

int mcs_proxy_url(void *ref, const char *httpsUrl, unsigned long size,
                  const unsigned char *moov, long moovLen,
                  unsigned long mediaFrom, char *dref, char *name)
{
    static char    host[256], path[MCP_URL_CAP], url[MCP_URL_CAP + 300];
    char           tok[9], t[300];
    unsigned short port = 0, uport = 0;
    int            tls = 0, defport, r;
    unsigned long  kbs, lim, base, cap, want, net;
    int            dl;
    const char    *fname;
    (void)ref;

    if (mcpo_open(ProxyLine, NULL, &port) != 0) return -1;
    mcpo_set_insecure(gInsecure);
    if (mcp_url_split(httpsUrl, &tls, host, sizeof host, &uport, path,
                      sizeof path) != 0)
    {
        Log("proxy: cannot split the film's URL: downloading instead.\n");
        return -1;
    }
    if (size == 0) size = MCP_UNKNOWN;
    defport = uport == 443 || (mcpo_test_https_port != 0
                               && uport == mcpo_test_https_port);
    if (tls && defport && Allowed(host)
        && 40UL + strlen(host) + strlen(path) < 1100UL)
    {   /* stateless form: a Desktop movie keeps working next session */
        sprintf(url, "https://%s%s", host, path);
        (void)mcp_token_add(mcpo_server(), url, size, NULL, tok);
        sprintf(dref, "http://127.0.0.1:%u/a/%s%s", (unsigned)port, host, path);
    }
    else
    {   /* session token (another host or port) */
        if (mcp_token_add(mcpo_server(), httpsUrl, size, NULL, tok) < 0)
            return -1;
        fname = strrchr(path, '/');
        fname = fname != NULL ? fname + 1 : path;
        if (strlen(fname) > 200 || mcp_token_path(tok, fname, url, 300) != 0)
            (void)mcp_token_path(tok, "film.mp4", url, 300);
        sprintf(dref, "http://127.0.0.1:%u%s", (unsigned)port, url);
    }
    /* (0.5u) QuickTime opens the reference movie and then WAITS (event
       loop starved) for the first media bytes, which lie behind the
       moov: the pre-buffer must cover [0, mediaFrom + MCPO_PREBUF) */
    base = mcpo_test_prebuf ? mcpo_test_prebuf : MCPO_PREBUF;
    cap = mcpo_test_prebuf_max ? mcpo_test_prebuf_max : MCPO_PREBUF_MAX;
    want = mediaFrom != 0 ? mediaFrom + base : base;
    if (want > cap)
    {
        sprintf(t, "proxy: index too large to stream (moov ends at %lu, "
                "%lu bytes to pre-buffer, limit %lu): downloading "
                "instead.\n", mediaFrom, want, cap);
        Log(t);
        return -1;
    }
    if (size != MCP_UNKNOWN && size != 0 && want > size) want = size;
    sprintf(t, "proxy: PREBUF %lu bytes (moov ends at %lu)\n", want,
            mediaFrom);
    Log(t);
    r = Prebuffer(strchr(dref + 7, '/'), want);
    if (r == -2) return -2;
    if (r != 0)
    {
        Log("proxy: pre-buffering failed: downloading instead.\n");
        return -1;
    }
    mcs_mc_name(name);
    kbs = mcs_avg_kbs(moov, moovLen, size);
    lim = gE.g3_kbs ? gE.g3_kbs : MCS_G3_KBS;
    if (kbs != 0)
    {
        sprintf(t, "Film average: %lu KB/s (%s).\n", kbs,
                gE.cpu_g3 ? "G3" : "not a G3");
        Log(t);
    }
    /* (0.6.1) stream vs download, decided HERE: right after the
       pre-buffer (its bytes/ticks are the only network measurement we
       get before QuickTime opens the reference movie) and before the
       reference movie exists, so -3 costs no second moov fetch: the
       caller downloads with the moov it holds (mc_fetch.c TryCand). The
       pre-buffered RAM is dropped; the download starts at byte 0. */
    dl = mcs_net_download(gPreGot, gPreTicks, kbs, gE.stream_margin,
                          &net);
    if (net != 0)
    {
        sprintf(t, "NET: pre-buffer %lu KB/s, film %lu KB/s (need x%ld.%02ld): "
                "%s.\n", net, kbs, (gE.stream_margin > 0 ? gE.stream_margin
                : MCS_STREAM_MARGIN_PCT) / 100L, (gE.stream_margin > 0
                ? gE.stream_margin : MCS_STREAM_MARGIN_PCT) % 100L,
                dl ? "too slow to stream" : "fast enough");
        Log(t);
    }
    if (gE.cpu_g3 && dl && gE.stream_margin >= 0)
    {
        mcpo_prebuf_cancel();
        return -3;
    }
    if (gE.cpu_g3 && kbs > lim)
    {   /* (0.6.1) honest: Download first cures a slow NETWORK only; the
           network was measured (or not judged) above, so any jerkiness
           left is the G3 decoding */
        sprintf(t, "G3 HINT: this film averages %lu KB/s%s. Jerky video "
                "on a G3 is the CPU decoding, not the network: Download "
                "first will not fix it; a smaller file will.", kbs,
                net ? " and the network keeps up" : "");
        Log(t);
        Log("\n");
        Status("G3 HINT: jerky video = CPU; try a smaller file.");
    }
    return 0;
}

void mcs_proxy_opened(void *ref, const char *name)
{
    int  i;
    char t[120];
    (void)ref;
    if (gSaved[0] != 0) name = gSaved;   /* (0.6.1) the name really saved */
    for (i = 0; i < gNMovies; i++) if (LowerEq(gMovie[i], name)) break;
    if (i == gNMovies)
    {
        if (gNMovies == MCS_MAX_MOVIES)
        {
            memmove(gMovie[0], gMovie[1], sizeof gMovie[0] * (MCS_MAX_MOVIES - 1));
            gNMovies--;
        }
        strncpy(gMovie[gNMovies], name, MCS_NAME_MAX);
        gMovie[gNMovies][MCS_NAME_MAX] = 0;
        gNMovies++;
    }
    sprintf(t, "STREAMING \"%.31s\" via 127.0.0.1:%u (Stop ends it).\n", name,
            (unsigned)mcpo_port());
    Log(t);
    gSaved[0] = 0;
    mcs_rate_reset();   /* (0.6.1) a new film: a new rate window */
}

/* ------------------------------------------------------------------ */
/* the loop                                                            */
/* ------------------------------------------------------------------ */

int mcs_streaming(void)
{
    return mcpo_is_open()
        && (gNMovies > 0 || mcpo_active() > 0 || mcpo_parked() > 0
            || gStopping);
}

int mcs_serving(void) { return mcpo_active() > 0; }
int mcs_stopping(void) { return gStopping; }
int mcs_quit_ready(void) { return gQuitReady; }

long mcs_sleep(int moved, long idle)
{
    if (moved) return 0;
    if (mcpo_active() > 0 || mcpo_parked() > 0 || mcpo_closing() > 0
        || gStopping)
        return 1;
    return idle;
}

static void FinishStop(const char *why)
{
    char t[160];
    sprintf(t, "STOP: %s\n", why);
    Log(t);
    sprintf(t, "STOP: cancelling every proxy connection (%d open, %d parked).\n",
            mcpo_active(), mcpo_parked());
    Log(t);
    mcpo_cancel_all();
    mcs_rate_reset();
    gNMovies = 0;
    gStopping = 0;
    Status("Streaming stopped.");
    if (gQuitAfterStop || gHook) gQuitReady = 1;
}

void mcs_stop(void)
{
    int  i, r;
    char t[160];
    if (gStopping) return;
    if (!mcpo_is_open() || (gNMovies == 0 && mcpo_active() == 0
                            && mcpo_parked() == 0))
    {
        if (gQuitAfterStop || gHook) gQuitReady = 1;
        return;
    }
    for (i = 0; i < gNMovies; i++)
    {
        r = gE.qt_close != NULL ? gE.qt_close(gE.ref, gMovie[i]) : -1;
        sprintf(t, "STOP: asked QuickTime Player to close \"%.31s\" "
                "(saving no) -> %d\n", gMovie[i], r);
        Log(t);
    }
    if (gNMovies == 0)
        Log("STOP: no streaming movie of ours is known to be open.\n");
    gStopping = 1;
    gStopT0 = Now();
    Status("Stopping: asking QuickTime Player to close the film...");
}

int mcs_quit(void)
{
    unsigned long t = Now();
    if (gStopping) { gQuitAfterStop = 1; return MCS_QUIT_CLOSING; }
    if (!mcs_streaming()) return MCS_QUIT_NOW;
    if (mcs_serving() && (gQuitWarnT == 0 || t - gQuitWarnT > MCS_QUIT_AGAIN))
    {
        gQuitWarnT = t;
        Log("QUIT: QuickTime Player is playing a film through Media Center: "
            "Quit again within 10 seconds to close it and quit.\n");
        Status("QuickTime Player is playing a film through Media Center. "
               "Quit again within 10 seconds to close it and quit.");
        return MCS_QUIT_WARNED;
    }
    gQuitWarnT = 0;
    gQuitAfterStop = 1;
    Log("QUIT: closing the streaming movie(s) in QuickTime Player first.\n");
    mcs_stop();
    return gQuitReady ? MCS_QUIT_NOW : MCS_QUIT_CLOSING;
}

void mcs_hook_timers(unsigned long serveSecs, unsigned long stopSecs)
{
    unsigned long t = Now();
    gHook = 1;
    gHookServe = serveSecs ? t + serveSecs * 60UL : 0;
    gHookStop = stopSecs ? t + stopSecs * 60UL : 0;
    if (gHookServe == 0) gHookServe = t + 600UL * 60UL;  /* never forever */
}

static int Due(unsigned long deadline, unsigned long t)
{
    return deadline != 0 && (long)(t - deadline) >= 0;
}

int mcs_poll(void)
{
    int           moved;
    unsigned long t;
    if (!gInit) return 0;
    moved = mcpo_poll();
    t = Now();
    if (gStopping)
    {
        if (mcpo_active() == 0)
            FinishStop("QuickTime Player let go of the film (no open "
                       "connection).");
        else if (t - gStopT0 >= MCS_STOP_WAIT)
            FinishStop("QuickTime Player did not close the film within 2 s: "
                       "cutting the connection.");
    }
    if (gHook && Due(gHookStop, t))
    {
        gHookStop = 0;
        Log("HOOK: stop time: pressing Stop.\n");
        mcs_stop();
    }
    if (gHook && Due(gHookServe, t))
    {
        gHookServe = 0;
        Log("HOOK: serve time is up: quitting (closing the movie first).\n");
        gQuitAfterStop = 1;
        mcs_stop();
    }
    if (gQuitWarnT != 0 && t - gQuitWarnT > MCS_QUIT_AGAIN) gQuitWarnT = 0;
    return moved;
}

/* (0.6.1) one sample (time in ticks, total bytes sent so far) -> KB/s
   over the samples kept (the last MCS_RATE_N intervals) */
unsigned long mcs_rate_feed(unsigned long t, unsigned long bytes)
{
    int           i;
    unsigned long dt, db;
    if (gRn == MCS_RATE_N + 1)
    {
        for (i = 1; i < gRn; i++) { gRt[i - 1] = gRt[i]; gRb[i - 1] = gRb[i]; }
        gRn--;
    }
    gRt[gRn] = t; gRb[gRn] = bytes; gRn++;
    gKBs = 0;
    if (gRn >= 2)
    {
        dt = gRt[gRn - 1] - gRt[0];
        db = gRb[gRn - 1] - gRb[0];
        if (dt != 0) gKBs = (db / 1024UL) * 60UL / dt;
    }
    return gKBs;
}

/* (0.6.1) forget the samples (a new stream, or none) */
void mcs_rate_reset(void)
{
    gRn = 0;
    gKBs = 0;
    gRateT = 0;
}

void mcs_status_text(char *out, unsigned long cap)
{
    const McpStats *st;
    unsigned long   b, mb10;
    int             n = mcpo_active();
    char            t[200];
    if (!mcpo_is_open()) { if (cap) out[0] = 0; return; }
    st = mcp_stats(mcpo_server());
    b = st->bytes_out;
    mb10 = b / (1024UL * 1024UL / 10UL);
    if (n > 0 && gRn >= 2 && gRb[gRn - 1] == gRb[0])
        /* (0.6.1) nothing went out for the whole window while QuickTime
           holds the connection: it is not fetching (buffer full), which
           is not "0 KB/s of a slow link" */
        sprintf(t, "Streaming via secure proxy: buffered, %lu.%lu MB sent, "
                "%d connection%s.", mb10 / 10UL, mb10 % 10UL, n,
                n == 1 ? "" : "s");
    else if (n > 0)
        sprintf(t, "Streaming via secure proxy: %lu KB/s, %lu.%lu MB sent, "
                "%d connection%s.", gKBs, mb10 / 10UL, mb10 % 10UL, n,
                n == 1 ? "" : "s");
    else
        sprintf(t, "Streaming via secure proxy: idle, %lu.%lu MB sent. "
                "Stop ends streaming.", mb10 / 10UL, mb10 % 10UL);
    strncpy(out, t, cap - 1);
    out[cap - 1] = 0;
}

/* call from the idle loop only (not during Media Center's own
   transfers, which own the status line then) */
void mcs_status_tick(void)
{
    unsigned long t = Now();
    char          s[200];
    if (!mcpo_is_open() || gStopping) return;
    if (gRateT != 0 && t - gRateT < MCS_STATUS_EVERY) return;
    if (!mcs_streaming())
    {
        gRn = 0; gKBs = 0; gRateT = t;
        return;
    }
    (void)mcs_rate_feed(t, mcp_stats(mcpo_server())->bytes_out);
    gRateT = t;
    mcs_status_text(s, sizeof s);
    if (strcmp(s, gLastStatus) != 0) Status(s);
}

void mcs_shutdown(void)
{
    mcpo_close();
    gNMovies = 0;
}
