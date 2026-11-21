/* test_mcn.c -- HOST ONLY (J5b). Media Center 0.5t notifier state machine
   (mc_notify.c) driven by a scripted fake notifier, plus the hand-off into
   the REAL mc_proxy core (mcp_adopt_downstream) with a fake upstream:
   the bytes on the wire must be head + file[0..total) exactly.
   cc -std=c89 -pedantic test_mcn.c ../mc_notify.c ../../proxy/src/mc_proxy.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mc_notify.h"

static int pass_n = 0, fail_n = 0;
#define CHECK(c, m) do { if (c) pass_n++; else { fail_n++; printf("FAIL %s (line %d)\n", m, __LINE__); } } while (0)

#define TOTAL 300000UL
#define PRE   65536UL
static unsigned char film[TOTAL];
static unsigned long tick = 1000;
/* fake OT for slot 0..4: scripted rx, send results */
static const char *rxs[8]; static unsigned long rxn[8]; static int rxi, rxc;
static long sendplan[64]; static int spi, spn;      /* >0 cap, 0 flow, -1 err */
#define BIGPRE 2300000UL   /* (0.5u) 1.75 MB moov + 512 KB: far over 512 KB */
#define BIGTOT 2600000UL
static unsigned char bigfilm[BIGTOT];
static unsigned char wire[BIGTOT + 4096]; static unsigned long wlen;

static unsigned long F_Ticks(void *c) { (void)c; return tick; }
static long F_Recv(void *c, int k, char *b, unsigned long n)
{
    unsigned long m;
    (void)c; (void)k;
    if (rxi >= rxc) return 0;
    m = rxn[rxi] < n ? rxn[rxi] : n;
    memcpy(b, rxs[rxi], m);
    rxi++;
    return (long)m;
}
static long F_Send(void *c, int k, const char *b, unsigned long n)
{
    long p = spi < spn ? sendplan[spi++] : 100000L;
    (void)c; (void)k;
    if (p < 0) return -1;
    if (p == 0) return 0;
    if ((unsigned long)p < n) n = (unsigned long)p;
    memcpy(wire + wlen, b, n);
    wlen += n;
    return (long)n;
}

static McnState st;
static char h206[MCN_HEAD], h200[MCN_HEAD];
static unsigned long l206, l200;
static const char *TGT = "/a/archive.org/download/X/f.mp4";
static const char QT_REQ[] = "GET /a/archive.org/download/X/f.mp4?t=9 HTTP/1.1\015\012Host: 127.0.0.1\015\012"
    "Range: bytes=0-\015\012Accept: */*\015\012User-Agent: QuickTime (qtver=6.0.2;cpu=PPC;os=Mac 9.2.2)\015\012\015\012";

static void setup(void)
{
    McnIo io;
    memset(&io, 0, sizeof io);
    io.ticks = F_Ticks; io.recv = F_Recv; io.send = F_Send;
    mcn_init(&st, &io);
    mcn_set_state(&st, 0, MCN_FREE);
    mcn_set_state(&st, 1, MCN_FREE);
    mcn_set_prebuf(&st, TGT, (const char *)film, PRE, TOTAL, h206, l206, h200, l200);
    rxi = rxc = 0; spi = spn = 0; wlen = 0;
}
static void script_req(const char *r, unsigned long n, int parts)
{
    unsigned long a = 0, step = n / (unsigned long)parts;
    int i;
    for (i = 0; i < parts; i++) { rxs[i] = r + a; rxn[i] = (i == parts - 1) ? n - a : step; a += step; }
    rxc = parts; rxi = 0;
}

/* ---- the core with a fake upstream ---- */
typedef struct Up { unsigned long from; char out[1024 + 1]; unsigned long olen, opos, fpos; int headed; } Up;
static int up_opens = 0;
static void *U_Open(void *c, const char *h, unsigned short p, int tls) { Up *u = calloc(1, sizeof *u); (void)c; (void)h; (void)p; (void)tls; up_opens++; return u; }
static int U_Step(void *h) { (void)h; return MCP_T_OPEN; }
static long U_Write(void *h, const char *p, unsigned long n)
{
    Up *u = (Up *)h;
    const char *r = strstr(p, "Range: bytes=");
    if (r) u->from = strtoul(r + 13, NULL, 10);
    return (long)n;
}
static long U_Read(void *h, char *p, unsigned long cap)
{
    Up *u = (Up *)h;
    unsigned long n;
    if (!u->headed)
    {
        u->olen = (unsigned long)sprintf(u->out, "HTTP/1.1 206 Partial Content\015\012Content-Type: video/mp4\015\012"
            "Content-Range: bytes %lu-%lu/%lu\015\012Content-Length: %lu\015\012\015\012", u->from, TOTAL - 1, TOTAL, TOTAL - u->from);
        u->headed = 1; u->fpos = u->from;
    }
    if (u->opos < u->olen) { n = u->olen - u->opos; if (n > cap) n = cap; memcpy(p, u->out + u->opos, n); u->opos += n; return (long)n; }
    if (u->fpos >= TOTAL) return MCP_IO_EOF;
    n = TOTAL - u->fpos; if (n > cap) n = cap; if (n > 7000) n = 7000;
    memcpy(p, film + u->fpos, n); u->fpos += n;
    return (long)n;
}
static void U_Close(void *h) { free(h); }
static long D_Read(void *h, char *p, unsigned long cap) { (void)h; (void)p; (void)cap; return 0; }
static int dclosed = 0;
static long D_Write(void *h, const char *p, unsigned long n) { (void)h; if (n > 3000) n = 3000; memcpy(wire + wlen, p, n); wlen += n; return (long)n; }
static void D_Close(void *h, int a) { (void)h; dclosed = a ? 2 : 1; }
static unsigned long P_Now(void *c) { (void)c; return tick; }
static void *P_Alloc(void *c, unsigned long n) { (void)c; return malloc(n); }
static void P_Rel(void *c, void *p) { (void)c; free(p); }
static int P_Allow(void *c, const char *h) { (void)c; (void)h; return 1; }

int main(void)
{
    unsigned long i, hl;
    McnTake t;
    McnRec rec;
    int k, recs = 0;
    for (i = 0; i < TOTAL; i++) film[i] = (unsigned char)((i * 7919UL) >> 3);
    l206 = mcp_build_head(h206, sizeof h206, 206, 0, 0, TOTAL, TOTAL, "video/mp4", 0);
    l200 = mcp_build_head(h200, sizeof h200, 200, 0, 0, TOTAL, TOTAL, "video/mp4", 0);

    /* 1: QuickTime's GET split over 3 T_DATA, partial sends, flow, T_GODATA */
    setup();
    mcn_accepting(&st, 0, 0x7F000001UL, 1234);
    CHECK(st.s[0].st == MCN_ACCEPTING && mcn_busy(&st, 0), "accepting");
    script_req(QT_REQ, sizeof QT_REQ - 1, 3);
    rxc = 1;                                     /* only part 1 arrived */
    mcn_passcon(&st, 0);
    CHECK(st.s[0].st == MCN_REQ, "REQ after part 1");
    rxc = 3;
    sendplan[0] = 1; sendplan[1] = 4096; sendplan[2] = 0; spn = 3;
    mcn_on_data(&st, 0);
    CHECK(st.s[0].st == MCN_SEND && st.s[0].in_flow, "flow controlled after 1 + 4096 bytes");
    CHECK(wlen == l206, "head went out in 2 partial sends (1 + rest), then flow control");
    for (k = 0; k < 40 && st.s[0].st == MCN_SEND; k++) { tick += 40; mcn_on_godata(&st, 0); if (st.s[0].need_kick) mcn_kick(&st, 0); }
    CHECK(st.s[0].st == MCN_READY && st.s[0].why == MCN_WHY_SERVED, "READY: served the pre-buffer");
    CHECK(wlen == l206 + PRE && memcmp(wire, h206, l206) == 0 && memcmp(wire + l206, film, PRE) == 0, "wire == head206 + film[0..PRE)");
    CHECK(mcn_prebuf_busy(&st), "pre-buffer busy while READY");
    while (mcn_next_rec(&st, &rec)) recs++;
    CHECK(recs >= 5, "log records (ACCEPT REQ SERVE FLOW SENT)");

    /* 2: hand-off + adopt into the real core: bytes == head + film */
    {
        McpOps ops; McpConfig cfg; McpServer *srv; char tok[9];
        memset(&ops, 0, sizeof ops);
        ops.up_open = U_Open; ops.up_step = U_Step; ops.up_write = U_Write; ops.up_read = U_Read; ops.up_close = U_Close;
        ops.down_read = D_Read; ops.down_write = D_Write; ops.down_close = D_Close;
        ops.now = P_Now; ops.alloc = P_Alloc; ops.release = P_Rel;
        memset(&cfg, 0, sizeof cfg); cfg.allow_host = P_Allow;
        srv = mcp_new(&ops, &cfg);
        CHECK(mcp_token_add(srv, "https://archive.org/download/X/f.mp4", TOTAL, NULL, tok) >= 0, "token");
        CHECK(mcn_take(&st, 0, &t) == 1 && st.s[0].st == MCN_LOOP && t.body_sent == PRE && t.total == TOTAL, "take");
        CHECK(mcp_adopt_downstream(srv, (void *)1, t.req, t.total, t.body_sent) >= 0, "adopt");
        for (k = 0; k < 2000 && !dclosed; k++) { tick++; mcp_poll(srv); }
        CHECK(dclosed == 1, "orderly close after the body");
        CHECK(wlen == l206 + TOTAL && memcmp(wire + l206, film, TOTAL) == 0, "bytes on the wire == head + film[0..TOTAL) exactly");
        CHECK(up_opens == 1 && mcp_stats(srv)->bytes_out == TOTAL, "one upstream from PRE, bytes_out == TOTAL");
        /* bad: unknown target */
        {
            static const char bad[] = "GET /t/deadbeef/x.mp4 HTTP/1.1\015\012\015\012";
            McpReq q; unsigned long used;
            mcp_req_init(&q);
            mcp_req_feed(&q, bad, (unsigned long)(sizeof bad - 1), &used);
            CHECK(mcp_adopt_downstream(srv, (void *)2, &q, TOTAL, 10) == -1, "adopt refuses an unknown target");
        }
        mcp_free(srv);
    }

    /* 3: FIN in the middle of the pre-buffer: keeps sending, then READY */
    setup();
    mcn_accepting(&st, 1, 1, 2);
    script_req(QT_REQ, sizeof QT_REQ - 1, 1);
    sendplan[0] = 20000; sendplan[1] = 0; spn = 2;
    mcn_passcon(&st, 1);
    mcn_on_fin(&st, 1);
    CHECK(st.s[1].st == MCN_SEND && st.s[1].fin, "FIN mid-pre-buffer: still sending");
    mcn_on_godata(&st, 1);
    CHECK(st.s[1].st == MCN_READY && st.s[1].why == MCN_WHY_SERVED && mcn_take(&st, 1, &t) && t.fin == 1, "then READY with fin noted");

    /* 4: disconnect mid-send */
    setup();
    mcn_accepting(&st, 0, 1, 2);
    script_req(QT_REQ, sizeof QT_REQ - 1, 2);
    sendplan[0] = 5000; sendplan[1] = 0; spn = 2;
    mcn_passcon(&st, 0);
    mcn_on_disc(&st, 0);
    CHECK(st.s[0].st == MCN_READY && st.s[0].why == MCN_WHY_DISC, "DISC mid-send -> READY DISC");
    /* 5: send error */
    setup();
    mcn_accepting(&st, 0, 1, 2);
    script_req(QT_REQ, sizeof QT_REQ - 1, 1);
    sendplan[0] = -1; spn = 1;
    mcn_passcon(&st, 0);
    CHECK(st.s[0].st == MCN_READY && st.s[0].why == MCN_WHY_ERR, "send error -> READY ERR");

    /* 6: PASS: other target, other Range, HEAD, bad request; replay bytes == received */
    {
        static const char *reqs[] = {
            "GET /a/archive.org/download/Y/g.mp4 HTTP/1.1\015\012Range: bytes=0-\015\012\015\012",
            "GET /a/archive.org/download/X/f.mp4 HTTP/1.1\015\012Range: bytes=1000-\015\012\015\012",
            "HEAD /a/archive.org/download/X/f.mp4 HTTP/1.1\015\012\015\012",
            "GET /a/archive.org/download/X/f.mp4x HTTP/1.1\015\012\015\012",
            "BAD\015\012\015\012" };
        for (k = 0; k < 5; k++)
        {
            setup();
            mcn_accepting(&st, 0, 1, 2);
            script_req(reqs[k], strlen(reqs[k]), 2);
            mcn_passcon(&st, 0);
            CHECK(st.s[0].st == MCN_READY && st.s[0].why == MCN_WHY_PASS && wlen == 0, "PASS to the event loop, nothing sent");
            CHECK(mcn_take(&st, 0, &t) && t.nreplay == strlen(reqs[k]) && memcmp(t.replay, reqs[k], t.nreplay) == 0, "replay bytes == bytes received");
        }
    }
    /* 7: no Range -> 200 head; pre.on = 0 -> PASS */
    setup();
    mcn_accepting(&st, 0, 1, 2);
    { static const char r[] = "GET /a/archive.org/download/X/f.mp4 HTTP/1.0\015\012\015\012";
      script_req(r, sizeof r - 1, 1); mcn_passcon(&st, 0); }
    CHECK(st.s[0].st == MCN_READY && wlen == l200 + PRE && memcmp(wire, h200, l200) == 0, "no Range -> 200 head + pre-buffer");
    setup();
    mcn_prebuf_off(&st);
    mcn_accepting(&st, 0, 1, 2);
    script_req(QT_REQ, sizeof QT_REQ - 1, 1);
    mcn_passcon(&st, 0);
    CHECK(st.s[0].why == MCN_WHY_PASS, "pre-buffer off -> PASS");
    /* 8: FIN before a full request -> READY FIN with the partial bytes */
    setup();
    mcn_accepting(&st, 0, 1, 2);
    script_req(QT_REQ, 20, 1);
    mcn_passcon(&st, 0);
    mcn_on_fin(&st, 0);
    CHECK(st.s[0].why == MCN_WHY_FIN && mcn_take(&st, 0, &t) && t.nreplay == 20, "FIN before the request end -> FIN, 20 bytes to replay");
    /* 9: the 64-send guard and the event loop's kick */
    setup();
    mcn_accepting(&st, 0, 1, 2);
    script_req(QT_REQ, sizeof QT_REQ - 1, 1);
    for (k = 0; k < 64; k++) sendplan[k] = 100;
    spn = 64;
    mcn_passcon(&st, 0);
    CHECK(st.s[0].st == MCN_SEND && st.s[0].need_kick, "guard: 64 sends, then need_kick");
    mcn_kick(&st, 0);
    CHECK(st.s[0].st == MCN_READY && wlen == l206 + PRE, "kick finishes the pre-buffer");
    /* 10: film smaller than the pre-buffer */
    setup();
    mcn_set_prebuf(&st, TGT, (const char *)film, PRE, 50000UL, h206, l206, h200, l200);
    CHECK(st.pre.len == 50000UL, "pre.len clipped to the film");
    /* 11: ring overflow counted */
    setup();
    for (k = 0; k < MCN_RING + 10; k++) mcn_log(&st, 0, MCN_L_NOTE, 0, 0, "x");
    CHECK(st.dropped == 10, "ring overflow counted");

    /* 12 (0.5u): a pre-buffer far over 512 KB (moov 1.75 MB + 512 KB media):
       head + the WHOLE pre-buffer leave from the notifier alone (no event
       loop here: only mcn_* calls), byte for byte */
    {
        char b206[MCN_HEAD], b200[MCN_HEAD];
        unsigned long bl206, bl200;
        for (i = 0; i < BIGTOT; i++) bigfilm[i] = (unsigned char)((i * 31UL + (i >> 9)) >> 1);
        bl206 = mcp_build_head(b206, sizeof b206, 206, 0, 0, BIGTOT, BIGTOT, "video/mp4", 0);
        bl200 = mcp_build_head(b200, sizeof b200, 200, 0, 0, BIGTOT, BIGTOT, "video/mp4", 0);
        setup();
        mcn_set_prebuf(&st, TGT, (const char *)bigfilm, BIGPRE, BIGTOT, b206, bl206, b200, bl200);
        CHECK(st.pre.len == BIGPRE, "big pre-buffer: pre.len kept (not clipped to 512 KB)");
        mcn_accepting(&st, 0, 0x7F000001UL, 1234);
        script_req(QT_REQ, sizeof QT_REQ - 1, 1);
        rxc = 1;
        mcn_passcon(&st, 0);
        mcn_on_data(&st, 0);
        for (k = 0; k < 20000 && st.s[0].st == MCN_SEND; k++)
        {
            tick += 40; mcn_on_godata(&st, 0);
            if (st.s[0].need_kick) mcn_kick(&st, 0);
        }
        CHECK(st.s[0].st == MCN_READY && st.s[0].why == MCN_WHY_SERVED, "big: READY, served");
        CHECK(wlen == bl206 + BIGPRE && memcmp(wire, b206, bl206) == 0
              && memcmp(wire + bl206, bigfilm, BIGPRE) == 0,
              "big: wire == head206 + bigfilm[0..2300000) (the whole larger pre-buffer)");
    }

    printf("test_mcn: %d passed, %d failed\n", pass_n, fail_n);
    return fail_n != 0;
}
