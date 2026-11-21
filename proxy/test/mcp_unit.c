/* mcp_unit.c -- unit tests for mc_proxy's pure helpers (host + qemu-ppc) */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "mc_proxy.h"

static int pass = 0, fail = 0;
#define CHECK(c, msg) do { if (c) pass++; else { fail++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } } while (0)

static unsigned long rng = 12345;
static unsigned long rnd(void) { rng = rng * 1103515245UL + 12345UL; return (rng >> 8) & 0xFFFFFFUL; }

/* feed a request whole, then byte by byte, then in random splits; all must agree */
static int feed_split(const char *txt, unsigned long len, McpReq *r, int mode, unsigned long *usedTotal)
{
    unsigned long at = 0, used;
    int rc = 0;
    mcp_req_init(r);
    *usedTotal = 0;
    while (at < len)
    {
        unsigned long k = len - at;
        if (mode == 1) k = 1;
        else if (mode == 2) { k = 1 + rnd() % 40; if (k > len - at) k = len - at; }
        rc = mcp_req_feed(r, txt + at, k, &used);
        *usedTotal += used;
        at += k;
        if (rc != 0) break;
    }
    return rc;
}

static void req_case(const char *name, const char *txt, int want_rc, int want_err,
                     int kind, unsigned long a, unsigned long b, const char *target)
{
    int mode, it;
    McpReq r;
    unsigned long used;
    for (mode = 0; mode < 3; mode++)
        for (it = 0; it < (mode == 2 ? 50 : 1); it++)
        {
            int rc = feed_split(txt, (unsigned long)strlen(txt), &r, mode, &used);
            char m[200];
            int ok = rc == want_rc && (want_rc != -1 || r.err == want_err);
            if (ok && want_rc == 1)
                ok = r.range_kind == kind && r.ra == a && r.rb == b
                     && (target == NULL || strcmp(r.target, target) == 0);
            sprintf(m, "req %s mode %d (rc %d err %d kind %d %lu-%lu)", name, mode, rc, r.err, r.range_kind, r.ra, r.rb);
            CHECK(ok, m);
        }
}

static void test_requests(void)
{
    McpReq r;
    unsigned long used;
    char big[6000];
    int i;
    req_case("plain", "GET /t/0001abcd/x.mp4 HTTP/1.0\r\n\r\n", 1, 0, MCP_R_NONE, 0, 0, "/t/0001abcd/x.mp4");
    req_case("lf-only", "GET /x HTTP/1.1\nHost: a\nRange: bytes=5-9\n\n", 1, 0, MCP_R_FROMTO, 5, 9, "/x");
    req_case("from", "GET /x HTTP/1.1\r\nRange: bytes=100-\r\n\r\n", 1, 0, MCP_R_FROM, 100, 0, "/x");
    req_case("suffix", "GET /x HTTP/1.1\r\nrange:   BYTES = -500  \r\n\r\n", 1, 0, MCP_R_SUFFIX, 500, 0, "/x");
    req_case("multi ignored", "GET /x HTTP/1.1\r\nRange: bytes=0-1,5-6\r\n\r\n", 1, 0, MCP_R_NONE, 0, 0, NULL);
    req_case("b<a ignored", "GET /x HTTP/1.1\r\nRange: bytes=9-5\r\n\r\n", 1, 0, MCP_R_NONE, 0, 0, NULL);
    req_case("unit ignored", "GET /x HTTP/1.1\r\nRange: items=0-5\r\n\r\n", 1, 0, MCP_R_NONE, 0, 0, NULL);
    req_case("suffix0 ignored", "GET /x HTTP/1.1\r\nRange: bytes=-0\r\n\r\n", 1, 0, MCP_R_NONE, 0, 0, NULL);
    req_case("overflow ignored", "GET /x HTTP/1.1\r\nRange: bytes=99999999999-\r\n\r\n", 1, 0, MCP_R_NONE, 0, 0, NULL);
    req_case("two Range headers ignored", "GET /x HTTP/1.1\r\nRange: bytes=1-2\r\nRange: bytes=3-4\r\n\r\n", 1, 0, MCP_R_NONE, 0, 0, NULL);
    req_case("leading CRLF", "\r\n\r\nGET /x HTTP/1.1\r\n\r\n", 1, 0, MCP_R_NONE, 0, 0, "/x");
    req_case("absolute-form", "GET http://127.0.0.1:8080/t/1/x HTTP/1.1\r\n\r\n", 1, 0, MCP_R_NONE, 0, 0, "http://127.0.0.1:8080/t/1/x");
    req_case("max range", "GET /x HTTP/1.1\r\nRange: bytes=0-4294967294\r\n\r\n", 1, 0, MCP_R_FROMTO, 0, 4294967294UL, NULL);
    req_case("junk binary", "\x01\x02\x03 garbage\r\n\r\n", -1, 400, 0, 0, 0, NULL);
    req_case("lower method", "get /x HTTP/1.1\r\n\r\n", -1, 400, 0, 0, 0, NULL);
    req_case("no version", "GET /x\r\n\r\n", -1, 400, 0, 0, 0, NULL);
    req_case("http2", "GET /x HTTP/2.0\r\n\r\n", -1, 505, 0, 0, 0, NULL);
    req_case("bad header", "GET /x HTTP/1.1\r\nNoColonHere\r\n\r\n", -1, 400, 0, 0, 0, NULL);
    req_case("space in name", "GET /x HTTP/1.1\r\nBad Name: v\r\n\r\n", -1, 400, 0, 0, 0, NULL);
    req_case("obs-fold", "GET /x HTTP/1.1\r\nA: b\r\n  c\r\n\r\n", -1, 400, 0, 0, 0, NULL);
    req_case("ctl in value", "GET /x HTTP/1.1\r\nA: b\x01\r\n\r\n", -1, 400, 0, 0, 0, NULL);
    req_case("incomplete", "GET /x HTTP/1.1\r\nHost: a\r\n", 0, 0, 0, 0, 0, NULL);
    /* too large -> 431 */
    strcpy(big, "GET /x HTTP/1.1\r\n");
    for (i = 0; i < 300; i++) strcat(big, "X-Pad: 0123456789\r\n");
    strcat(big, "\r\n");
    req_case("431", big, -1, 431, 0, 0, 0, NULL);
    /* long target -> 414 */
    strcpy(big, "GET /");
    for (i = 0; i < 1100; i++) strcat(big, "a");
    strcat(big, " HTTP/1.1\r\n\r\n");
    req_case("414", big, -1, 414, 0, 0, 0, NULL);
    /* 65 headers -> 431 */
    strcpy(big, "GET /x HTTP/1.1\r\n");
    for (i = 0; i < 65; i++) strcat(big, "A: b\r\n");
    strcat(big, "\r\n");
    req_case("65 headers", big, -1, 431, 0, 0, 0, NULL);
    /* used = up to end of head; pipelined bytes left over */
    {
        const char *t = "GET /a HTTP/1.1\r\n\r\nGET /b HTTP/1.1\r\n\r\n";
        mcp_req_init(&r);
        CHECK(mcp_req_feed(&r, t, (unsigned long)strlen(t), &used) == 1 && used == 19, "used stops at end of head");
    }
    /* connection tokens */
    {
        const char *t = "GET /a HTTP/1.1\r\nConnection: Keep-Alive, foo\r\n\r\n";
        mcp_req_init(&r);
        CHECK(mcp_req_feed(&r, t, (unsigned long)strlen(t), &used) == 1 && r.conn_keep && !r.conn_close, "keep-alive token");
    }
    /* fuzz: random bytes never crash and always end in 0/1/-1 with bounded len */
    for (i = 0; i < 3000; i++)
    {
        char fz[700];
        unsigned long n = rnd() % sizeof fz, j;
        int rc;
        static const char alpha[] = "GET /HTP1.0\r\n: Range=bytes-,0123456789abc\x00\x7f\xff";
        for (j = 0; j < n; j++) fz[j] = (rnd() & 1) ? alpha[rnd() % (sizeof alpha - 1)] : (char)rnd();
        if (i % 3 == 0 && n > 20) memcpy(fz, "GET /x HTTP/1.1\r\nRange: ", 24 < n ? 24 : n);
        rc = feed_split(fz, n, &r, 2, &used);
        CHECK(rc >= -1 && rc <= 1 && r.len < MCP_REQ_CAP && used <= n, "fuzz request");
    }
}

static void test_ranges(void)
{
    unsigned long s, n;
    int k;
    unsigned long a, b;
    CHECK(mcp_range_resolve(MCP_R_NONE, 0, 0, 1000, &s, &n) == 0 && s == 0 && n == 1000, "none");
    CHECK(mcp_range_resolve(MCP_R_NONE, 0, 0, 0, &s, &n) == 0 && n == 0, "none empty");
    CHECK(mcp_range_resolve(MCP_R_FROMTO, 10, 19, 1000, &s, &n) == 0 && s == 10 && n == 10, "a-b");
    CHECK(mcp_range_resolve(MCP_R_FROMTO, 990, 5000, 1000, &s, &n) == 0 && s == 990 && n == 10, "a-b clipped");
    CHECK(mcp_range_resolve(MCP_R_FROMTO, 999, 999, 1000, &s, &n) == 0 && s == 999 && n == 1, "last byte");
    CHECK(mcp_range_resolve(MCP_R_FROMTO, 1000, 1001, 1000, &s, &n) == 1, "a>=T 416");
    CHECK(mcp_range_resolve(MCP_R_FROM, 0, 0, 1000, &s, &n) == 0 && n == 1000, "0-");
    CHECK(mcp_range_resolve(MCP_R_FROM, 1000, 0, 1000, &s, &n) == 1, "T- 416");
    CHECK(mcp_range_resolve(MCP_R_FROM, 0, 0, 0, &s, &n) == 1, "0- of empty 416");
    CHECK(mcp_range_resolve(MCP_R_SUFFIX, 100, 0, 1000, &s, &n) == 0 && s == 900 && n == 100, "-100");
    CHECK(mcp_range_resolve(MCP_R_SUFFIX, 5000, 0, 1000, &s, &n) == 0 && s == 0 && n == 1000, "-5000 whole");
    CHECK(mcp_range_resolve(MCP_R_SUFFIX, 1, 0, 0, &s, &n) == 1, "suffix empty 416");
    CHECK(mcp_range_resolve(MCP_R_FROM, 5, 0, MCP_UNKNOWN, &s, &n) == -1, "unknown total");
    CHECK(mcp_range_resolve(MCP_R_FROMTO, 0, 4294967294UL, 4294967295UL - 1UL, &s, &n) == 0 && n == 4294967294UL, "4 GB edge");
    CHECK(mcp_parse_range("bytes=0-0", &k, &a, &b) == 0 && k == MCP_R_FROMTO && a == 0 && b == 0, "0-0");
    CHECK(mcp_parse_range("bytes=4294967295-", &k, &a, &b) == -1, "u32 max refused");
    CHECK(mcp_parse_range("bytes=1-2 x", &k, &a, &b) == -1, "trailing junk");
    CHECK(mcp_parse_range("bytes=-", &k, &a, &b) == -1, "bare dash");
    CHECK(mcp_parse_range("bytes 1-2", &k, &a, &b) == -1, "no =");
    /* random consistency: resolved range always within file */
    {
        int i;
        for (i = 0; i < 20000; i++)
        {
            unsigned long T = rnd() % 5000, x = rnd() % 6000, y = x + rnd() % 3000;
            int kind = 1 + (int)(rnd() % 3), rc = mcp_range_resolve(kind, kind == 3 ? x : x, y, T, &s, &n);
            if (rc == 0) CHECK(s + n <= T && (n > 0 || T == 0), "resolve within file");
            else CHECK(rc == 1, "resolve 416 only");
        }
    }
}

static void test_heads(void)
{
    char h[700];
    unsigned long n;
    n = mcp_build_head(h, sizeof h, 206, 0, 100, 50, 1000, "video/mp4", 0);
    CHECK(n > 0 && strstr(h, "HTTP/1.1 206 Partial Content\r\n") == h, "206 status");
    CHECK(strstr(h, "Content-Range: bytes 100-149/1000\r\n") && strstr(h, "Content-Length: 50\r\n"), "206 fields");
    CHECK(strstr(h, "Accept-Ranges: bytes\r\n") && strstr(h, "Connection: close\r\n") && strstr(h, "Content-Type: video/mp4\r\n"), "206 misc");
    CHECK(strcmp(h + n - 4, "\r\n\r\n") == 0 && strstr(h, "chunked") == NULL, "206 end, no chunked");
    n = mcp_build_head(h, sizeof h, 200, 1, 0, 1000, 1000, NULL, 0);
    CHECK(strstr(h, "HTTP/1.0 200 OK\r\n") == h && strstr(h, "Content-Length: 1000\r\n") && strstr(h, "Content-Type: video/quicktime") && !strstr(h, "Content-Range"), "200 http10");
    n = mcp_build_head(h, sizeof h, 416, 0, 0, 0, 1000, NULL, 7);
    CHECK(strstr(h, "Content-Range: bytes */1000\r\n") && strstr(h, "Content-Length: 7\r\n"), "416");
    n = mcp_build_head(h, sizeof h, 404, 0, 0, 0, MCP_UNKNOWN, NULL, 15);
    CHECK(strstr(h, "HTTP/1.1 404 Not Found\r\n") == h, "404");
    CHECK(mcp_build_head(h, 50, 200, 0, 0, 1, 1, NULL, 0) == 0, "too small cap");
}

static void test_resp(void)
{
    McpResp r;
    const char *t;
    t = "HTTP/1.1 206 Partial Content\r\nContent-Range: bytes 10-19/100\r\nContent-Length: 10\r\nContent-Type: video/mp4\r\n\r\n";
    CHECK(mcp_head_end(t, strlen(t), 0) == strlen(t), "head_end");
    CHECK(mcp_resp_parse(t, strlen(t), &r) == 0 && r.status == 206 && r.has_crange == 1 && r.cr_s == 10 && r.cr_e == 19 && r.cr_total == 100 && r.length == 10, "206 parse");
    t = "HTTP/1.1 302 Found\r\nLocation: https://dn1.ca.archive.org/0/items/x/y.mp4\r\n\r\n";
    CHECK(mcp_resp_parse(t, strlen(t), &r) == 0 && r.status == 302 && strcmp(r.location, "https://dn1.ca.archive.org/0/items/x/y.mp4") == 0, "302");
    t = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nContent-Length: 5\r\n\r\n";
    CHECK(mcp_resp_parse(t, strlen(t), &r) == 0 && r.chunked && !r.has_length, "chunked wins");
    t = "HTTP/1.1 416 x\r\nContent-Range: bytes */777\r\n\r\n";
    CHECK(mcp_resp_parse(t, strlen(t), &r) == 0 && r.has_crange == 2 && r.cr_total == 777, "416 */T");
    t = "HTTP/1.1 206 x\r\nContent-Range: bytes 0-9/*\r\n\r\n";
    CHECK(mcp_resp_parse(t, strlen(t), &r) == 0 && r.cr_total == MCP_UNKNOWN, "206 /*");
    t = "HTTP/1.1 206 x\r\nContent-Range: bytes 9-0/100\r\n\r\n";
    CHECK(mcp_resp_parse(t, strlen(t), &r) == -1, "bad crange");
    t = "HTTP/1.1 200 x\r\nContent-Length: 5\r\nContent-Length: 6\r\n\r\n";
    CHECK(mcp_resp_parse(t, strlen(t), &r) == -1, "conflicting CL");
    t = "HTTP/1.1 200 x\r\nTransfer-Encoding: gzip\r\n\r\n";
    CHECK(mcp_resp_parse(t, strlen(t), &r) == -1, "gzip TE refused");
    t = "ICY 200 OK\r\n\r\n";
    CHECK(mcp_resp_parse(t, strlen(t), &r) == -1, "not http");
    t = "HTTP/1.0 200 OK\nContent-Length: 3\n\n";
    CHECK(mcp_head_end(t, strlen(t), 0) == strlen(t) && mcp_resp_parse(t, strlen(t), &r) == 0 && r.conn_close && r.length == 3, "1.0 LF");
}

/* chunk decoder: encode random data, decode in random splits */
static void test_chunks(void)
{
    static char src[20000], enc[40000], work[40000], dec[20000];
    int it;
    for (it = 0; it < 400; it++)
    {
        unsigned long sn = rnd() % sizeof src, i, en = 0, dn = 0, at = 0;
        McpChunk c;
        int rc = 0, ext = (int)(it & 1);
        for (i = 0; i < sn; i++) src[i] = (char)rnd();
        i = 0;
        while (i < sn)
        {
            unsigned long k = 1 + rnd() % 3000;
            if (k > sn - i) k = sn - i;
            en += (unsigned long)sprintf(enc + en, ext ? "%lX;foo=bar\r\n" : "%lx\r\n", k);
            memcpy(enc + en, src + i, k);
            en += k;
            enc[en++] = '\r'; enc[en++] = '\n';
            i += k;
        }
        en += (unsigned long)sprintf(enc + en, ext ? "0\r\nX-Trailer: 1\r\n\r\nJUNK" : "0\r\n\r\n");
        mcp_chunk_init(&c);
        while (at < en && rc == 0)
        {
            unsigned long k = 1 + rnd() % 700, out, used;
            if (it % 7 == 0) k = 1;
            if (k > en - at) k = en - at;
            memcpy(work, enc + at, k);
            rc = mcp_chunk_run(&c, work, k, &out, &used);
            memcpy(dec + dn, work, out);
            dn += out;
            at += used;
            if (rc == 0 && used != k) { rc = -2; break; }
        }
        CHECK(rc == 1 && dn == sn && memcmp(dec, src, sn) == 0, "chunk roundtrip");
        if (ext) CHECK(en - at == 4, "chunk stops before JUNK");
    }
    {
        McpChunk c;
        char b[64];
        unsigned long o, u;
        strcpy(b, "zz\r\n"); mcp_chunk_init(&c);
        CHECK(mcp_chunk_run(&c, b, 4, &o, &u) == -1, "bad hex");
        strcpy(b, "3\r\nabcX\r\n"); mcp_chunk_init(&c);
        CHECK(mcp_chunk_run(&c, b, 8, &o, &u) == -1, "missing CRLF after data");
        strcpy(b, "FFFFFFFFF\r\n"); mcp_chunk_init(&c);
        CHECK(mcp_chunk_run(&c, b, 11, &o, &u) == -1, "chunk size overflow");
    }
}

static void test_urls(void)
{
    char host[256], path[1024], out[1024];
    unsigned short port;
    int tls;
    CHECK(mcp_url_split("https://archive.org/download/a/b.mp4", &tls, host, sizeof host, &port, path, sizeof path) == 0
          && tls && port == 443 && strcmp(host, "archive.org") == 0 && strcmp(path, "/download/a/b.mp4") == 0, "split https");
    CHECK(mcp_url_split("HTTP://h:8080?q=1#f", &tls, host, sizeof host, &port, path, sizeof path) == 0
          && !tls && port == 8080 && strcmp(path, "/?q=1") == 0, "split port+query");
    CHECK(mcp_url_split("http://h", &tls, host, sizeof host, &port, path, sizeof path) == 0 && strcmp(path, "/") == 0, "split no path");
    CHECK(mcp_url_split("ftp://h/", &tls, host, sizeof host, &port, path, sizeof path) == -1, "split ftp");
    CHECK(mcp_url_split("http://u@h/", &tls, host, sizeof host, &port, path, sizeof path) == -1, "split userinfo");
    CHECK(mcp_url_split("http://h:0/", &tls, host, sizeof host, &port, path, sizeof path) == -1, "split port 0");
    CHECK(mcp_url_split("http://h:70000/", &tls, host, sizeof host, &port, path, sizeof path) == -1, "split port big");
    CHECK(mcp_url_split("http:///x", &tls, host, sizeof host, &port, path, sizeof path) == -1, "split empty host");
    CHECK(mcp_url_resolve("http://archive.org/download/x/y.mp4", "https://dn1.ca.archive.org/0/items/x/y.mp4", out, sizeof out) == 0
          && strcmp(out, "https://dn1.ca.archive.org/0/items/x/y.mp4") == 0, "resolve absolute");
    CHECK(mcp_url_resolve("https://a.org/d/x/y.mp4", "//b.org/z", out, sizeof out) == 0 && strcmp(out, "https://b.org/z") == 0, "resolve //");
    CHECK(mcp_url_resolve("https://a.org:444/d/x/y.mp4?q", "/z/w", out, sizeof out) == 0 && strcmp(out, "https://a.org:444/z/w") == 0, "resolve /");
    CHECK(mcp_url_resolve("https://a.org/d/x/y.mp4", "w.mp4", out, sizeof out) == 0 && strcmp(out, "https://a.org/d/x/w.mp4") == 0, "resolve relative");
    CHECK(mcp_url_resolve("https://a.org", "w", out, sizeof out) == 0 && strcmp(out, "https://a.org/w") == 0, "resolve relative no path");
    CHECK(mcp_url_resolve("https://a.org/", "", out, sizeof out) == -1, "resolve empty");
    CHECK(mcp_url_resolve("https://a.org/", "/abcdef", out, 10) == -1, "resolve too long");
    CHECK(mcp_token_path("0001abcd", "x.mp4", out, sizeof out) == 0 && strcmp(out, "/t/0001abcd/x.mp4") == 0, "token path");
}

int main(void)
{
    test_requests();
    test_ranges();
    test_heads();
    test_resp();
    test_chunks();
    test_urls();
    printf("mcp_unit: %d passed, %d failed\n", pass, fail);
    return fail != 0;
}
