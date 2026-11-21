/* mcp_query_test.c -- HOST ONLY (J5). QuickTime 6 caches http movies BY
   URL, so a reference movie may carry "?t=2" (or any query) to force a
   fresh fetch. On /a/<host>/<path> targets the core must IGNORE the query:
   same token (same learned size / cached final URL / parked upstream), and
   the upstream request path must not carry it. Reuses r1_sm.c's fake ops
   (its main() renamed away). Against the H1 core (query kept) this fails:
   a second token is made and the upstream asks "...f.mp4?t=2". */
#define main r1_sm_main
#include "r1_sm.c"
#undef main

static int count_tokens_seen(McpServer *s)
{
    (void)s;
    return 0;
}

static void t_query(const char *q)
{
    McpServer *s; Down *d1, *d2; char r[512]; int i, withq = 0;
    printf("Q /a/ target with query \"%s\": same film, query not sent upstream\n", q);
    reset(); cfg.allow_host = allow; s = mk();
    d1 = req(s, GET("/a/archive.org/download/x/f.mp4", "Range: bytes=0-\r\n"));
    run(s, d1, 4000);
    CHECK(d1->closed && d1->status == 206 && d1->bad == 0 && d1->nbody == g_size, "plain /a/ GET whole file");
    sprintf(r, "GET /a/archive.org/download/x/f.mp4%s HTTP/1.1\r\nHost: 127.0.0.1\r\nRange: bytes=0-\r\n\r\n", q);
    d2 = req(s, r);
    run(s, d2, 4000);
    CHECK(d2->closed && d2->status == 206 && d2->bad == 0 && d2->nbody == g_size, "query /a/ GET whole file");
    for (i = 0; i < g_nups; i++)
        if (strchr(g_ups[i]->req, '?') != NULL || strchr(g_ups[i]->req, '#') != NULL)
        { withq++; printf("  upstream asked: %.60s\n", g_ups[i]->req); }
    CHECK(withq == 0, "no upstream request carries the query");
    {   /* the same URL must map to ONE token: register it and look it up by /t/ */
        char tok[9], tok2[9];
        int a = mcp_token_add(s, "https://archive.org/download/x/f.mp4", MCP_UNKNOWN, NULL, tok);
        int b = mcp_token_add(s, "https://archive.org/download/x/f.mp4", MCP_UNKNOWN, NULL, tok2);
        printf("  token slots for the plain URL: %d %d\n", a, b);
        CHECK(a == 1 && b == 2, "plain and query /a/ targets shared ONE token (slot 0)");
    }
    mcp_free(s); free(d1); free(d2);
    (void)count_tokens_seen;
}

int main(void)
{
    t_query("?t=2");
    t_query("?t=2&x=%20y");
    t_query("#frag");
    t_query("?");
    printf("query: %d passed, %d failed\n", npass, nfail);
    return nfail != 0;
}
