/* r1_sm.c -- R1 review: state-machine reproducers for mc_proxy with
   fully scripted in-memory fake ops (no sockets, controllable clock).
   H1 copy, three fixture corrections (marked "H1:"):
   - T3/T4: once the old node is stale, the origin redirects to a NEW
     node (as archive.org does); R1's origin redirected back to the stale
     node forever, which no proxy can complete.
   - fake up_read: an empty read before the end of the body returns 0,
     not EOF (T10's "momentarily empty" was an EOF).
   - T18 "Content-Length list": the request carries Range bytes=0-, so
     206 is the right answer (the point of the case is "not 502"). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mc_proxy.h"

static int npass = 0, nfail = 0;
#define CHECK(c, msg) do { if (c) npass++; else { nfail++; printf("  FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } } while (0)

/* ---------- clock ---------- */
static unsigned long g_now = 1000;
static unsigned long f_now(void *x) { (void)x; return g_now; }

/* ---------- alloc ---------- */
static long g_live = 0, g_allocs = 0, g_fail_at = -1;
static void *f_alloc(void *x, unsigned long n) { (void)x; if (g_fail_at >= 0 && g_allocs++ == g_fail_at) return NULL; if (g_fail_at < 0) g_allocs++; g_live++; return malloc(n); }
static void f_release(void *x, void *p) { (void)x; if (p) { g_live--; free(p); } }

static int g_verbose = 0;
static char g_lastlog[1024];
static void f_log(void *x, const char *l) { (void)x; strncpy(g_lastlog, l, 1023); if (g_verbose) printf("    | %s\n", l); }

/* ---------- file content (virtual, any offset) ---------- */
static unsigned char B(unsigned long o) { return (unsigned char)((o ^ (o >> 8) ^ (o >> 16) ^ (o >> 24)) * 31UL + 7UL); }

/* ---------- fake upstream ---------- */
typedef struct Up {
    int open_steps, closed, failed;
    char host[256];
    char req[4096]; unsigned long reqlen; int req_done;
    char head[4096]; unsigned long hlen;
    char *raw; unsigned long rawlen;       /* if raw: whole response after head */
    unsigned long bstart, blen;             /* virtual body */
    int has_body;
    unsigned long rpos;                     /* bytes after head delivered */
    unsigned long hpos;
    long fail_after;                        /* body bytes then ERR; -1 none */
    int eof_at_end;
    unsigned long max_read;
} Up;
static Up *g_ups[256]; static int g_nups = 0, g_up_live = 0;
typedef void (*Serve)(Up *u, const char *path, const char *range);
static Serve g_serve;
static int g_open_steps = 2;

static void *f_up_open(void *x, const char *host, unsigned short port, int tls)
{
    Up *u = (Up *)calloc(1, sizeof *u);
    (void)x; (void)port; (void)tls;
    strcpy(u->host, host);
    u->open_steps = g_open_steps; u->fail_after = -1; u->eof_at_end = 1; u->max_read = 100000;
    g_ups[g_nups++] = u; g_up_live++;
    return u;
}
static int f_up_step(void *h)
{
    Up *u = (Up *)h;
    if (u->failed) return MCP_T_FAILED;
    if (u->open_steps > 0) { u->open_steps--; return MCP_T_CONNECTING; }
    return MCP_T_OPEN;
}
static void serve_now(Up *u)
{
    char path[2048], range[256], *p, *e;
    p = u->req + 4; e = strchr(p, ' '); memcpy(path, p, (size_t)(e - p)); path[e - p] = 0;
    range[0] = 0;
    p = strstr(u->req, "\r\nRange: ");
    if (p) { p += 9; e = strstr(p, "\r\n"); memcpy(range, p, (size_t)(e - p)); range[e - p] = 0; }
    g_serve(u, path, range);
}
static long f_up_write(void *h, const char *p, unsigned long n)
{
    Up *u = (Up *)h;
    memcpy(u->req + u->reqlen, p, n); u->reqlen += n; u->req[u->reqlen] = 0;
    if (!u->req_done && strstr(u->req, "\r\n\r\n")) { u->req_done = 1; serve_now(u); }
    return (long)n;
}
static long f_up_read(void *h, char *p, unsigned long cap)
{
    Up *u = (Up *)h;
    unsigned long k = 0, total;
    if (u->failed) return MCP_IO_ERR;
    if (!u->req_done) return 0;
    if (cap > u->max_read) cap = u->max_read;
    while (k < cap && u->hpos < u->hlen) p[k++] = u->head[u->hpos++];
    total = u->raw ? u->rawlen : (u->has_body ? u->blen : 0);
    while (k < cap && u->rpos < total)
    {
        if (u->fail_after >= 0 && (long)u->rpos >= u->fail_after) { if (k) return (long)k; u->failed = 1; return MCP_IO_ERR; }
        p[k++] = u->raw ? u->raw[u->rpos] : (char)B(u->bstart + u->rpos);
        u->rpos++;
    }
    if (k) return (long)k;
    if (u->hpos < u->hlen || u->rpos < total) return 0;   /* H1: momentarily empty */
    return u->eof_at_end ? MCP_IO_EOF : 0;
}
static void f_up_close(void *h) { Up *u = (Up *)h; u->closed = 1; g_up_live--; }

/* responses */
static void r_head(Up *u, const char *txt) { strcpy(u->head, txt); u->hlen = strlen(txt); }
static int prange(const char *r, unsigned long size, unsigned long *a, unsigned long *b)
{   /* bytes=a-  or bytes=-n */
    if (strncmp(r, "bytes=", 6) != 0) return 0;
    if (r[6] == '-') { unsigned long n = strtoul(r + 7, 0, 10); *a = n >= size ? 0 : size - n; *b = size - 1; return 1; }
    *a = strtoul(r + 6, 0, 10); *b = size - 1; return 1;
}
static unsigned long g_size = 3000000;
static const char *g_ctype = "video/mp4";
static void serve_file(Up *u, const char *path, const char *range)
{
    unsigned long a, b; char h[1024];
    (void)path;
    if (prange(range, g_size, &a, &b))
    {
        if (a >= g_size) { sprintf(h, "HTTP/1.1 416 X\r\nContent-Range: bytes */%lu\r\nContent-Length: 0\r\n\r\n", g_size); r_head(u, h); return; }
        sprintf(h, "HTTP/1.1 206 Partial\r\nContent-Type: %s\r\nContent-Range: bytes %lu-%lu/%lu\r\nContent-Length: %lu\r\n\r\n", g_ctype, a, b, g_size, b - a + 1);
        r_head(u, h); u->bstart = a; u->blen = b - a + 1; u->has_body = 1; return;
    }
    sprintf(h, "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %lu\r\n\r\n", g_ctype, g_size);
    r_head(u, h); u->bstart = 0; u->blen = g_size; u->has_body = 1;
}

/* ---------- fake downstream (QuickTime) ---------- */
typedef struct Down {
    char in[4096]; unsigned long inlen, inpos; int eof_after_in;
    unsigned long wcap;                 /* per write; 0 = blocked */
    char head[4096]; unsigned long hl; int hdone;
    unsigned long bstart, nbody, bad, first_bad;
    long clen; int closed, aborted; int status;
} Down;
static long f_down_read(void *h, char *p, unsigned long cap)
{
    Down *d = (Down *)h; unsigned long k = d->inlen - d->inpos;
    if (k > cap) k = cap;
    if (k == 0) return d->eof_after_in ? MCP_IO_EOF : 0;
    memcpy(p, d->in + d->inpos, k); d->inpos += k; return (long)k;
}
static long f_down_write(void *h, const char *p, unsigned long n)
{
    Down *d = (Down *)h; unsigned long i = 0, k = n;
    if (d->wcap < k) k = d->wcap;
    while (i < k && !d->hdone)
    {
        d->head[d->hl++] = p[i++]; d->head[d->hl] = 0;
        if (d->hl >= 4 && memcmp(d->head + d->hl - 4, "\r\n\r\n", 4) == 0)
        {
            char *cr = strstr(d->head, "Content-Range: bytes "), *cl = strstr(d->head, "Content-Length: ");
            d->hdone = 1; d->status = atoi(d->head + 9);
            d->bstart = cr && cr[21] != '*' ? strtoul(cr + 21, 0, 10) : 0;
            d->clen = cl ? (long)strtoul(cl + 16, 0, 10) : -1;
        }
    }
    for (; i < k; i++, d->nbody++)
        if ((d->status == 200 || d->status == 206) && (unsigned char)p[i] != B(d->bstart + d->nbody)) { if (!d->bad) d->first_bad = d->nbody; d->bad++; }
    return (long)k;
}
static void f_down_close(void *h, int abort) { Down *d = (Down *)h; d->closed = 1; d->aborted = abort; }

static McpOps ops;
static McpConfig cfg;
static McpServer *mk(void)
{
    memset(&ops, 0, sizeof ops);
    ops.up_open = f_up_open; ops.up_step = f_up_step; ops.up_write = f_up_write; ops.up_read = f_up_read; ops.up_close = f_up_close;
    ops.down_read = f_down_read; ops.down_write = f_down_write; ops.down_close = f_down_close;
    ops.now = f_now; ops.log = f_log; ops.alloc = f_alloc; ops.release = f_release;
    return mcp_new(&ops, &cfg);
}
static void reset(void)
{
    int i;
    for (i = 0; i < g_nups; i++) { free(g_ups[i]->raw); free(g_ups[i]); }
    g_nups = 0; g_up_live = 0; g_serve = serve_file; g_size = 3000000; g_ctype = "video/mp4";
    memset(&cfg, 0, sizeof cfg); g_open_steps = 2;
}
static Down *req(McpServer *s, const char *txt)
{
    Down *d = (Down *)calloc(1, sizeof *d);
    strcpy(d->in, txt); d->inlen = strlen(txt); d->wcap = 1UL << 30;
    mcp_accept(s, d);
    return d;
}
static void run(McpServer *s, Down *d, int iters)
{
    int i;
    for (i = 0; i < iters && !(d && d->closed); i++) { mcp_poll(s); g_now++; }
}
static int allow(void *x, const char *h) { (void)x; (void)h; return 1; }

#define GET(path, extra) "GET " path " HTTP/1.0\r\nHost: 127.0.0.1\r\n" extra "\r\n"

/* ================= tests ================= */

static void t_head_then_adopt(void)
{
    McpServer *s; Down *d1, *d2;
    printf("T1 HEAD (size unknown) parks an upstream whose body bytes were already read; GET adopts it\n");
    reset(); cfg.allow_host = allow; s = mk();
    d1 = req(s, "HEAD /a/archive.org/download/x/f.mp4 HTTP/1.0\r\n\r\n");
    run(s, d1, 200);
    CHECK(d1->closed && d1->status == 200, "HEAD 200");
    printf("  parked after HEAD: %d\n", mcp_parked(s));
    d2 = req(s, GET("/a/archive.org/download/x/f.mp4", ""));
    run(s, d2, 2000);
    printf("  GET: status %d clen %ld body %lu bad %lu first_bad %lu adopted %lu opens %lu\n", d2->status, d2->clen,
           d2->nbody, d2->bad, d2->first_bad, mcp_stats(s)->adopted, mcp_stats(s)->upstream_opens);
    CHECK(d2->bad == 0, "GET after HEAD delivers the right bytes");
    mcp_free(s); free(d1); free(d2);
}

/* upstream ignores Range (200, full file); client range beyond the end, size unknown */
static void serve_ignore_range(Up *u, const char *path, const char *range)
{ (void)range; serve_file(u, path, ""); }
static void t_416_park(void)
{
    McpServer *s; Down *d1, *d2; char tok[9], p[64];
    printf("T2 range past end, size unknown, upstream ignores Range: 416 path parks a consumed upstream\n");
    reset(); g_serve = serve_ignore_range; s = mk();
    mcp_token_add(s, "https://h.example/f.mp4", MCP_UNKNOWN, NULL, tok);
    mcp_token_path(tok, "f.mp4", p, sizeof p);
    { char r[256]; sprintf(r, "GET %s HTTP/1.0\r\nRange: bytes=9000000-\r\n\r\n", p); d1 = req(s, r); }
    run(s, d1, 200);
    printf("  first: status %d, parked %d\n", d1->status, mcp_parked(s));
    { char r[256]; sprintf(r, "GET %s HTTP/1.0\r\nRange: bytes=0-99999\r\n\r\n", p); d2 = req(s, r); }
    run(s, d2, 2000);
    printf("  second: status %d body %lu bad %lu adopted %lu\n", d2->status, d2->nbody, d2->bad, mcp_stats(s)->adopted);
    CHECK(d1->status == 416, "416");
    CHECK(d2->bad == 0, "bytes after 416-park adoption are right");
    mcp_free(s); free(d1); free(d2);
}

/* cached final URL now answers 200 with an HTML page */
static int g_stale = 0;
static void serve_redirect_then_stale(Up *u, const char *path, const char *range)
{
    if (strcmp(u->host, "orig.example") == 0)
    {   /* H1: after the old node went stale the origin points at a new one */
        r_head(u, g_stale ? "HTTP/1.1 302 Found\r\nLocation: https://node2.example/f.mp4\r\nContent-Length: 0\r\n\r\n"
                          : "HTTP/1.1 302 Found\r\nLocation: https://node.example/f.mp4\r\nContent-Length: 0\r\n\r\n");
        return;
    }
    if (g_stale && strcmp(u->host, "node.example") == 0) { r_head(u, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 300\r\n\r\n"); u->bstart = 0; u->blen = 300; u->has_body = 1; return; }
    serve_file(u, path, range);
}
static void t_cached_url_stale_html(void)
{
    McpServer *s; Down *d1, *d2; char tok[9], p[64], r[256];
    printf("T3 cached final URL turns into a 200 HTML page (size differs)\n");
    reset(); g_serve = serve_redirect_then_stale; g_stale = 0; s = mk();
    mcp_token_add(s, "https://orig.example/f.mp4", g_size, NULL, tok);
    mcp_token_path(tok, "f.mp4", p, sizeof p);
    sprintf(r, "GET %s HTTP/1.0\r\nRange: bytes=0-999\r\n\r\n", p); d1 = req(s, r); run(s, d1, 500);
    g_stale = 1;
    g_now += 2000;   /* parked upstream expired, cache (30 min) still valid */
    mcp_poll(s);
    sprintf(r, "GET %s HTTP/1.0\r\nRange: bytes=1000000-1999999\r\n\r\n", p); d2 = req(s, r); run(s, d2, 2000);
    printf("  second: status %d clen %ld body %lu bad %lu; log: %s\n", d2->status, d2->clen, d2->nbody, d2->bad, g_lastlog);
    CHECK(d2->status == 206 && d2->bad == 0 && d2->nbody == 1000000, "stale cached URL falls back to the original URL");
    mcp_free(s); free(d1); free(d2);
}
/* same, mid-film: the upstream drops, the resume hits the stale cached URL */
static void serve_drop_then_stale(Up *u, const char *path, const char *range)
{
    serve_redirect_then_stale(u, path, range);
    if (strcmp(u->host, "node.example") == 0 && !g_stale) { u->fail_after = 200000; g_stale = 1; }
}
static void t_cached_url_stale_midfilm(void)
{
    McpServer *s; Down *d1; char tok[9], p[64], r[256];
    printf("T4 mid-film: upstream drops, resume uses the cached URL which now gives 200 HTML\n");
    reset(); g_serve = serve_drop_then_stale; g_stale = 0; s = mk();
    mcp_token_add(s, "https://orig.example/f.mp4", g_size, NULL, tok);
    mcp_token_path(tok, "f.mp4", p, sizeof p);
    sprintf(r, "GET %s HTTP/1.0\r\n\r\n", p); d1 = req(s, r); run(s, d1, 5000);
    printf("  status %d body %lu of %ld bad %lu aborted %d opens %lu; log: %s\n", d1->status, d1->nbody, d1->clen, d1->bad, d1->aborted, mcp_stats(s)->upstream_opens, g_lastlog);
    CHECK(d1->nbody == g_size && d1->bad == 0, "resume falls back to the original URL, film completes");
    mcp_free(s); free(d1);
}

static void serve_ctype_cr(Up *u, const char *path, const char *range)
{ g_ctype = "video/mp4\rSet-Cookie: x=1"; serve_file(u, path, range); }
static void t_ctype_cr(void)
{
    McpServer *s; Down *d; char tok[9], p[64], r[256];
    printf("T5 upstream Content-Type with a bare CR is copied into QuickTime's head\n");
    reset(); g_serve = serve_ctype_cr; s = mk();
    mcp_token_add(s, "https://h.example/f.mp4", MCP_UNKNOWN, NULL, tok); mcp_token_path(tok, "f", p, sizeof p);
    sprintf(r, "GET %s HTTP/1.0\r\nRange: bytes=0-9\r\n\r\n", p); d = req(s, r); run(s, d, 500);
    CHECK(strchr(d->head, '\r') == NULL || strstr(d->head, "mp4\rSet") == NULL, "no bare CR in downstream head");
    mcp_free(s); free(d);
}

static const char *g_loc;
static char g_seen_path[2048];
static void serve_loc(Up *u, const char *path, const char *range)
{
    char h[1200];
    if (strcmp(path, "/dl/f.mp4?x=1") == 0) { sprintf(h, "HTTP/1.1 302 Found\r\nLocation: %s\r\n\r\n", g_loc); r_head(u, h); return; }
    strcpy(g_seen_path, path); serve_file(u, path, range);
}
static void t_location(const char *loc, const char *want)
{
    McpServer *s; Down *d; char tok[9], p[64], r[256];
    printf("T6 Location '%s' from https://orig.example/dl/f.mp4?x=1\n", loc);
    reset(); g_serve = serve_loc; g_loc = loc; g_seen_path[0] = 0; s = mk();
    mcp_token_add(s, "https://orig.example/dl/f.mp4?x=1", MCP_UNKNOWN, NULL, tok); mcp_token_path(tok, "f", p, sizeof p);
    sprintf(r, "GET %s HTTP/1.0\r\nRange: bytes=0-9\r\n\r\n", p); d = req(s, r); run(s, d, 500);
    printf("  status %d, next-hop path '%s'\n", d->status, g_seen_path);
    CHECK(d->status == 206 && strcmp(g_seen_path, want) == 0, "next hop as RFC 3986 says");
    mcp_free(s); free(d);
}

static void t_size_changed(void)
{
    McpServer *s; Down *d; char tok[9], p[64], r[256]; unsigned long opens;
    printf("T7 size changes on resume: how many upstream opens before giving up\n");
    reset(); s = mk(); g_serve = serve_drop_then_stale; g_stale = 0;
    mcp_token_add(s, "https://node.example/f.mp4", g_size, NULL, tok); mcp_token_path(tok, "f", p, sizeof p);
    sprintf(r, "GET %s HTTP/1.0\r\n\r\n", p); d = req(s, r); run(s, d, 5000);
    opens = mcp_stats(s)->upstream_opens;
    printf("  opens %lu aborted %d body %lu\n", opens, d->aborted, d->nbody);
    CHECK(opens <= 2, "gives up at the first size change (no wasted reconnect)");
    mcp_free(s); free(d);
}

static void t_tick_wrap(void)
{
    McpServer *s; Down *d1, *d2; char tok[9], p[64], r[256];
    printf("T8 TickCount wraps during a session (park, adopt, url TTL, timeouts)\n");
    reset(); g_now = 0xFFFFFFFFUL - 8UL; s = mk();
    mcp_token_add(s, "https://h.example/f.mp4", g_size, NULL, tok); mcp_token_path(tok, "f", p, sizeof p);
    sprintf(r, "GET %s HTTP/1.0\r\nRange: bytes=0-199999\r\n\r\n", p); d1 = req(s, r); run(s, d1, 2000);
    g_now += 100;  /* now past the wrap */
    sprintf(r, "GET %s HTTP/1.0\r\nRange: bytes=200000-399999\r\n\r\n", p); d2 = req(s, r); run(s, d2, 2000);
    printf("  now=%lu  d1 %d/%lu  d2 %d/%lu bad %lu adopted %lu parked %d\n", g_now, d1->status, d1->nbody, d2->status, d2->nbody, d2->bad, mcp_stats(s)->adopted, mcp_parked(s));
    CHECK(d1->nbody == 200000 && d2->nbody == 200000 && d2->bad == 0 && mcp_stats(s)->adopted == 1, "wrap-safe adopt");
    g_now += 899; mcp_poll(s); CHECK(mcp_parked(s) == 1, "park survives < 900 ticks across the wrap");
    g_now += 2; mcp_poll(s); CHECK(mcp_parked(s) == 0, "park expires at 900 ticks across the wrap");
    g_now = 1000;
    mcp_free(s); free(d1); free(d2);
}

static void t_big(unsigned long size, const char *range, int want_status, const char *want_cr, unsigned long want_len)
{
    McpServer *s; Down *d; char tok[9], p[64], r[256], cl[64];
    printf("T9 >2 GB: size %lu range '%s'\n", size, range);
    reset(); g_size = size; s = mk();
    mcp_token_add(s, "https://h.example/f.mp4", MCP_UNKNOWN, NULL, tok); mcp_token_path(tok, "f", p, sizeof p);
    if (*range) sprintf(r, "GET %s HTTP/1.0\r\nRange: %s\r\n\r\n", p, range); else sprintf(r, "GET %s HTTP/1.0\r\n\r\n", p);
    d = req(s, r);
    /* let it stream ~1 MB, then QuickTime closes */
    run(s, d, 60);
    { char *cr = strstr(d->head, "Content-Range"); int L = cr ? (int)strcspn(cr, "\r") : 1;
      printf("  status %d head '%.*s' clen %ld body %lu bad %lu\n", d->status, L, cr ? cr : "-", d->clen, d->nbody, d->bad); }
    sprintf(cl, "%lu", want_len);
    CHECK(d->status == want_status, "status");
    CHECK(want_len == 0 || (unsigned long)d->clen == want_len, "Content-Length");
    CHECK(d->bad == 0, "bytes right at > 2 GB offsets");
    (void)want_cr;
    mcp_free(s); free(d);
}

static void t_pause_spurious_resume(void)
{
    McpServer *s; Down *d; char tok[9], p[64], r[256]; Up *u;
    printf("T10 QuickTime pauses longer than idle_ticks, then reads; upstream healthy but momentarily empty\n");
    reset(); s = mk();
    mcp_token_add(s, "https://h.example/f.mp4", g_size, NULL, tok); mcp_token_path(tok, "f", p, sizeof p);
    sprintf(r, "GET %s HTTP/1.0\r\n\r\n", p); d = req(s, r);
    run(s, d, 20);
    d->wcap = 0;               /* paused */
    run(s, d, 30);
    g_now += 5000;             /* 83 s pause */
    u = g_ups[0];
    u->max_read = 0;           /* next read: nothing yet (TLS record in flight) */
    d->wcap = 1UL << 30;
    mcp_poll(s);
    u->max_read = 100000;
    run(s, d, 3000);
    printf("  opens %lu resumes %lu body %lu bad %lu\n", mcp_stats(s)->upstream_opens, mcp_stats(s)->resumes, d->nbody, d->bad);
    CHECK(mcp_stats(s)->resumes == 0, "no resume of a healthy upstream after a pause");
    mcp_free(s); free(d);
}

static void t_halfclose(void)
{
    McpServer *s; Down *d; char tok[9], p[64], r[256];
    printf("T11 QuickTime half-closes (FIN) after its request\n");
    reset(); s = mk();
    mcp_token_add(s, "https://h.example/f.mp4", g_size, NULL, tok); mcp_token_path(tok, "f", p, sizeof p);
    sprintf(r, "GET %s HTTP/1.0\r\nRange: bytes=0-99999\r\n\r\n", p); d = req(s, r); d->eof_after_in = 1;
    run(s, d, 500);
    printf("  status %d body %lu\n", d->status, d->nbody);
    CHECK(d->nbody == 100000, "body still delivered after a half-close");
    mcp_free(s); free(d);
}

static void t_cancel_upconn(void)
{
    McpServer *s; Down *d; char tok[9], p[64], r[256];
    printf("T12 cancel during UPCONN; then free: handles all closed, no leak\n");
    reset(); g_open_steps = 1000; s = mk();
    mcp_token_add(s, "https://h.example/f.mp4", g_size, NULL, tok); mcp_token_path(tok, "f", p, sizeof p);
    sprintf(r, "GET %s HTTP/1.0\r\n\r\n", p); d = req(s, r); run(s, d, 5);
    mcp_cancel_all(s);
    CHECK(d->closed && d->aborted && g_up_live == 0 && mcp_active(s) == 0, "cancel closes both sides");
    mcp_free(s); CHECK(g_live == 0, "no leak"); free(d);
}

static void t_503(void)
{
    McpServer *s; Down *d[6]; char tok[9], p[64], r[256]; int i; long before;
    printf("T13 503 when full\n");
    reset(); g_open_steps = 1000; s = mk();
    mcp_token_add(s, "https://h.example/f.mp4", g_size, NULL, tok); mcp_token_path(tok, "f", p, sizeof p);
    sprintf(r, "GET %s HTTP/1.0\r\n\r\n", p);
    for (i = 0; i < 4; i++) d[i] = req(s, r);
    before = g_live;
    d[4] = req(s, r);
    CHECK(d[4]->closed && d[4]->status == 503 && g_live == before, "503 static, no alloc");
    mcp_free(s); CHECK(g_live == 0 && g_up_live == 0, "no leak");
    for (i = 0; i < 5; i++) free(d[i]);
}

static void t_alloc_fail(void)
{
    int k; McpServer *s;
    printf("T14 allocation failure at every call of mcp_new\n");
    for (k = 0; k < 8; k++)
    {
        reset(); g_allocs = 0; g_fail_at = k; g_live = 0;
        s = mk();
        if (s) mcp_free(s);
        CHECK(g_live == 0, "no leak on mcp_new failure");
    }
    g_fail_at = -1;
}

/* chunked upstream drops mid-chunk */
static void serve_chunked_drop(Up *u, const char *path, const char *range)
{
    unsigned long a = 0, b, o, w = 0, k;
    static int n = 0;
    (void)path;
    prange(range, g_size, &a, &b);
    u->raw = (char *)malloc(g_size * 2 + 1000);
    for (o = a; o < g_size; o += k)
    {
        k = 7777; if (k > g_size - o) k = g_size - o;
        w += (unsigned long)sprintf(u->raw + w, "%lx\r\n", k);
        { unsigned long i; for (i = 0; i < k; i++) u->raw[w++] = (char)B(o + i); }
        u->raw[w++] = '\r'; u->raw[w++] = '\n';
    }
    w += (unsigned long)sprintf(u->raw + w, "0\r\n\r\n");
    u->rawlen = w;
    { char h[256]; sprintf(h, "HTTP/1.1 206 P\r\nTransfer-Encoding: chunked\r\nContent-Range: bytes %lu-%lu/%lu\r\n\r\n", a, g_size - 1, g_size); r_head(u, h); }
    if (n++ < 3) u->fail_after = 123457 + n * 1000;
}
static void t_chunk_resume(void)
{
    McpServer *s; Down *d; char tok[9], p[64], r[256];
    printf("T15 chunked upstream lost mid-chunk, resumed 3x\n");
    reset(); g_size = 500000; g_serve = serve_chunked_drop; s = mk();
    mcp_token_add(s, "https://h.example/f.mp4", MCP_UNKNOWN, NULL, tok); mcp_token_path(tok, "f", p, sizeof p);
    sprintf(r, "GET %s HTTP/1.0\r\n\r\n", p); d = req(s, r); run(s, d, 5000);
    printf("  status %d body %lu bad %lu resumes %lu\n", d->status, d->nbody, d->bad, mcp_stats(s)->resumes);
    CHECK(d->nbody == g_size && d->bad == 0, "complete and right");
    mcp_free(s); free(d);
}

/* 200 close-delimited HTTP/1.0, size known */
static void serve_http10(Up *u, const char *path, const char *range)
{ (void)path; (void)range; r_head(u, "HTTP/1.0 200 OK\r\n\r\n"); u->bstart = 0; u->blen = g_size; u->has_body = 1; }
static void t_http10(unsigned long known, int want)
{
    McpServer *s; Down *d; char tok[9], p[64], r[256];
    printf("T16 HTTP/1.0 close-delimited upstream, size %s\n", known == MCP_UNKNOWN ? "unknown" : "known");
    reset(); g_size = 400000; g_serve = serve_http10; s = mk();
    mcp_token_add(s, "http://h.example/f.mp4", known, NULL, tok); mcp_token_path(tok, "f", p, sizeof p);
    sprintf(r, "GET %s HTTP/1.0\r\nRange: bytes=1000-\r\n\r\n", p); d = req(s, r); run(s, d, 5000);
    printf("  status %d body %lu bad %lu\n", d->status, d->nbody, d->bad);
    CHECK(d->status == want && (want != 206 || (d->nbody == g_size - 1000 && d->bad == 0)), "as expected");
    mcp_free(s); free(d);
}

static void t_odd_requests(void)
{
    McpServer *s; Down *d; char tok[9], p[64], r[512];
    printf("T17 odd QuickTime requests\n");
    reset(); s = mk(); g_size = 100000;
    mcp_token_add(s, "https://h.example/f.mp4", MCP_UNKNOWN, NULL, tok); mcp_token_path(tok, "f", p, sizeof p);
    sprintf(r, "GET http://127.0.0.1:8080%s HTTP/1.0\r\nrange: bytes=0-\r\n\r\n", p); d = req(s, r); run(s, d, 500);
    CHECK(d->status == 206 && d->nbody == g_size, "absolute URI + lowercase range + bytes=0- on unknown size"); free(d);
    sprintf(r, "GET %s HTTP/1.0\r\nRange: bytes=0-9\r\nRange: bytes=5-9\r\n\r\n", p); d = req(s, r); run(s, d, 500);
    CHECK(d->status == 200 && d->nbody == g_size, "two Range headers -> ignored -> 200"); free(d);
    sprintf(r, "GET %s HTTP/1.0\r\nRange: bytes=-10\r\n\r\n", p); d = req(s, r); run(s, d, 500);
    CHECK(d->status == 206 && d->nbody == 10 && d->bstart == g_size - 10 && d->bad == 0, "suffix"); free(d);
    mcp_free(s);
}

/* hostile heads */
static const char *g_rawhead;
static void serve_raw(Up *u, const char *path, const char *range)
{ unsigned long a = 0, b; (void)path; prange(range, g_size, &a, &b); r_head(u, g_rawhead); u->bstart = a; u->blen = g_size - a; u->has_body = 1; }
static void t_hostile(const char *name, const char *head, int want)
{
    McpServer *s; Down *d; char tok[9], p[64], r[256];
    reset(); g_size = 100000; g_serve = serve_raw; g_rawhead = head; s = mk();
    mcp_token_add(s, "https://h.example/f.mp4", MCP_UNKNOWN, NULL, tok); mcp_token_path(tok, "f", p, sizeof p);
    sprintf(r, "GET %s HTTP/1.0\r\nRange: bytes=0-\r\n\r\n", p); d = req(s, r); run(s, d, 500);
    printf("T18 %-40s -> %d (body %lu bad %lu)\n", name, d->status, d->nbody, d->bad);
    CHECK(d->status == want && d->bad == 0, name);
    mcp_free(s); free(d);
}


static void t_wrong_registered_size(const char *range)
{
    McpServer *s; Down *d; char tok[9], p[64], r[256]; char *cr;
    printf("T19 registered size 3000000, real file 2000000, '%s'\n", range);
    reset(); g_size = 2000000; s = mk();
    mcp_token_add(s, "https://h.example/f.mp4", 3000000UL, NULL, tok); mcp_token_path(tok, "f", p, sizeof p);
    sprintf(r, "GET %s HTTP/1.0\r\nRange: %s\r\n\r\n", p, range); d = req(s, r); run(s, d, 5000);
    cr = strstr(d->head, "Content-Range");
    printf("  status %d '%.*s' clen %ld body %lu aborted %d opens %lu\n", d->status, cr ? (int)strcspn(cr, "\r") : 1, cr ? cr : "-", d->clen, d->nbody, d->aborted, mcp_stats(s)->upstream_opens);
    CHECK(!d->aborted && (d->status == 416 || (long)d->nbody == d->clen), "consistent answer (valid 206 or 416), no short body");
    mcp_free(s); free(d);
}
static void serve_early_start(Up *u, const char *path, const char *range)
{   /* answers any range from offset 1000 lower than asked */
    unsigned long a, b; char h[256]; (void)path;
    prange(range, g_size, &a, &b); a = a >= 1000 ? a - 1000 : 0;
    sprintf(h, "HTTP/1.1 206 P\r\nContent-Range: bytes %lu-%lu/%lu\r\nContent-Length: %lu\r\n\r\n", a, g_size - 1, g_size, g_size - a);
    r_head(u, h); u->bstart = a; u->blen = g_size - a; u->has_body = 1;
}
static void t_early_start(void)
{
    McpServer *s; Down *d; char tok[9], p[64], r[256];
    printf("T20 206 that starts 1000 bytes before the wanted offset\n");
    reset(); g_size = 100000; g_serve = serve_early_start; s = mk();
    mcp_token_add(s, "https://h.example/f.mp4", MCP_UNKNOWN, NULL, tok); mcp_token_path(tok, "f", p, sizeof p);
    sprintf(r, "GET %s HTTP/1.0\r\nRange: bytes=5000-5999\r\n\r\n", p); d = req(s, r); run(s, d, 500);
    CHECK(d->status == 206 && d->bstart == 5000 && d->nbody == 1000 && d->bad == 0, "skips to the wanted offset");
    mcp_free(s); free(d);
}
static void t_lru(void)
{
    McpServer *s; Down *d1, *d2; char tok[17][9], p[64], r[256]; int i, slot;
    printf("T21 token LRU never recycles a token held by an active conn or a parked upstream\n");
    reset(); g_open_steps = 0; s = mk();
    for (i = 0; i < 16; i++) { mcp_token_add(s, "https://h.example/f.mp4", g_size, NULL, tok[i]); g_now += 10; }
    /* token 0 (oldest): parked upstream; token 1: active conn */
    mcp_token_path(tok[0], "f", p, sizeof p);
    sprintf(r, "GET %s HTTP/1.0\r\nRange: bytes=0-199999\r\n\r\n", p); d1 = req(s, r); run(s, d1, 100);
    mcp_token_path(tok[1], "f", p, sizeof p);
    sprintf(r, "GET %s HTTP/1.0\r\n\r\n", p); d2 = req(s, r); d2->wcap = 0; run(s, d2, 10);
    g_now -= 5000;   /* make tokens 0 and 1 the least recently used */
    for (i = 2; i < 16; i++) { mcp_token_path(tok[i], "f", p, sizeof p); }
    g_now += 10000;
    slot = mcp_token_add(s, "https://h.example/g.mp4", g_size, NULL, tok[16]);
    printf("  parked %d active %d new slot %d\n", mcp_parked(s), mcp_active(s), slot);
    CHECK(mcp_parked(s) == 1 && mcp_active(s) == 1 && slot != 0 && slot != 1, "slots 0/1 protected");
    mcp_free(s); free(d1); free(d2);
}
int main(void)
{
    t_wrong_registered_size("bytes=0-");
    t_wrong_registered_size("bytes=2500000-");
    t_early_start();
    t_lru();
    t_head_then_adopt();
    t_416_park();
    t_cached_url_stale_html();
    t_cached_url_stale_midfilm();
    t_ctype_cr();
    t_location("/dl/a%20b.mp4", "/dl/a%20b.mp4");
    t_location("/dl/a b.mp4", "/dl/a%20b.mp4");
    t_location("?y=2", "/dl/f.mp4?y=2");
    t_location("../g.mp4", "/g.mp4");
    t_size_changed();
    t_tick_wrap();
    t_big(3000000000UL, "bytes=2500000000-", 206, "", 500000000UL);
    t_big(3000000000UL, "", 200, "", 3000000000UL);
    t_big(4294967294UL, "bytes=4294000000-", 206, "", 967294UL);
    t_big(4294967295UL, "bytes=0-", 502, "", 0);
    t_pause_spurious_resume();
    t_halfclose();
    t_cancel_upconn();
    t_503();
    t_alloc_fail();
    t_chunk_resume();
    t_http10(400000, 206);
    t_http10(MCP_UNKNOWN, 502);
    t_odd_requests();
    t_hostile("dup equal Content-Length", "HTTP/1.1 206 P\r\nContent-Range: bytes 0-99999/100000\r\nContent-Length: 100000\r\nContent-Length: 100000\r\n\r\n", 206);
    t_hostile("conflicting Content-Length", "HTTP/1.1 206 P\r\nContent-Range: bytes 0-99999/100000\r\nContent-Length: 100000\r\nContent-Length: 5\r\n\r\n", 502);
    t_hostile("Content-Range without total, size unknown", "HTTP/1.1 206 P\r\nContent-Range: bytes 0-99999/*\r\nContent-Length: 100000\r\n\r\n", 502);
    t_hostile("206 starting after wanted", "HTTP/1.1 206 P\r\nContent-Range: bytes 10-99999/100000\r\n\r\n", 502);
    t_hostile("200 to ranged request, no length", "HTTP/1.1 200 OK\r\n\r\n", 502);
    t_hostile("206 without Content-Range", "HTTP/1.1 206 P\r\nContent-Length: 100000\r\n\r\n", 502);
    t_hostile("Content-Length list '100000, 100000'", "HTTP/1.1 200 OK\r\nContent-Length: 100000, 100000\r\n\r\n", 206);  /* H1: ranged request */
    printf("r1_sm: %d passed, %d failed\n", npass, nfail);
    return 0;
}
