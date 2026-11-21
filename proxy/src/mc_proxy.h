/*
    mc_proxy.h -- Classic Media Center: a loopback HTTP server that lets
    QuickTime 6 (plain http, HTTP/1.0 era) play films that live behind
    https. QuickTime asks http://127.0.0.1:PORT/<token or /a/host/path>;
    this core fetches the bytes upstream (https, redirects, chunked
    bodies, Range) and hands them on with Content-Length, never chunked.

    Portable strict C89. No Toolbox, no Open Transport, no libc I/O: all
    I/O goes through McpOps (a non-blocking downstream "socket" for
    QuickTime's side and a non-blocking upstream transport, on the Mac
    = https_client's HC_Stream). Everything is driven by mcp_poll(),
    called from the application's event loop; nothing ever blocks.

    Text-only protocol work: no byte-order dependence.
*/
#ifndef MC_PROXY_H
#define MC_PROXY_H

#define MCP_UNKNOWN      0xFFFFFFFFUL   /* size / offset not known      */
#define MCP_MAX_CONNS    8
#define MCP_MAX_TOKENS   16
#define MCP_MAX_PARK     4
#define MCP_URL_CAP      1024
#define MCP_REQ_CAP      4096           /* request head limit (431)     */
#define MCP_UHDR_CAP     4096           /* upstream head limit          */
#define MCP_TARGET_CAP   1024

/* --- I/O results --- */
#define MCP_IO_EOF   (-1L)   /* peer closed (orderly), no more data     */
#define MCP_IO_ERR   (-2L)   /* connection failed                       */
/* --- upstream transport states (up_step) --- */
#define MCP_T_CONNECTING 0   /* DNS / TCP / TLS handshake in progress   */
#define MCP_T_OPEN       1   /* can read and write                      */
#define MCP_T_FAILED     2   /* dead: read gives MCP_IO_ERR             */

typedef struct McpOps
{
    void *ctx;
    /* upstream: open a connection to host:port (tls 1 = https).
       Non-blocking: returns at once; NULL = failed at once. */
    void *(*up_open)(void *ctx, const char *host, unsigned short port,
                     int tls);
    int   (*up_step)(void *h);                       /* MCP_T_*       */
    /* >= 0 bytes accepted (0 = not now), < 0 error */
    long  (*up_write)(void *h, const char *p, unsigned long n);
    /* > 0 bytes, 0 = nothing now, MCP_IO_EOF, MCP_IO_ERR */
    long  (*up_read)(void *h, char *p, unsigned long cap);
    void  (*up_close)(void *h);
    /* downstream (QuickTime's connection, handle from mcp_accept) */
    long  (*down_read)(void *h, char *p, unsigned long cap);  /* as up_read */
    long  (*down_write)(void *h, const char *p, unsigned long n);
    void  (*down_close)(void *h, int abort);   /* abort 1 = disconnect */
    unsigned long (*now)(void *ctx);           /* ticks, 1/60 s, wraps */
    void  (*log)(void *ctx, const char *line); /* may be NULL          */
    void *(*alloc)(void *ctx, unsigned long n);/* zeroed not required  */
    void  (*release)(void *ctx, void *p);
} McpOps;

typedef struct McpConfig
{
    int           max_conns;      /* 0 = 4; at most MCP_MAX_CONNS      */
    int           max_parked;     /* 0 = 2; -1 = never park            */
    unsigned long buf_size;       /* body buffer per conn; 0 = 16384   */
    unsigned long connect_ticks;  /* connect+handshake+head; 0 = 600   */
    unsigned long idle_ticks;     /* upstream no progress; 0 = 1200    */
    unsigned long req_ticks;      /* request head must arrive; 0 = 600 */
    unsigned long park_ticks;     /* parked upstream lifetime; 0 = 900 */
    unsigned long adopt_gap;      /* max bytes skipped to adopt; 0 = 256K */
    unsigned long url_ttl_ticks;  /* cached final URL; 0 = 30 min      */
    int           max_redirects;  /* 0 = 5                             */
    int           max_resumes;    /* 0 = 3; -1 = none                  */
    int           http10_status;  /* 1 = "HTTP/1.0 200 OK" status line */
    unsigned long per_pass;       /* bytes per conn per mcp_poll; 0 = 65536 */
    const char   *user_agent;     /* NULL = "MediaCenter/0.5"          */
    const char   *default_type;   /* NULL = upstream's, else video/quicktime */
    /* /a/<host>/<path> targets: 1 = allowed. NULL = /a/ disabled. */
    int         (*allow_host)(void *ctx, const char *host);
} McpConfig;

typedef struct McpStats
{
    unsigned long accepted, requests, refused_busy;
    unsigned long bytes_out, upstream_opens, redirects, adopted, parked;
    unsigned long resumes, errors, client_aborts;
} McpStats;

typedef struct McpServer McpServer;
struct McpReq;

McpServer *mcp_new(const McpOps *ops, const McpConfig *cfg);
void       mcp_free(McpServer *s);        /* closes everything         */

/* Register a film. url = https:// (or http://) ORIGINAL URL; size =
   known total or MCP_UNKNOWN; ctype NULL = upstream's / default.
   tok gets 8 hex chars + NUL. Returns the slot (>= 0) or -1. */
int  mcp_token_add(McpServer *s, const char *url, unsigned long size,
                   const char *ctype, char *tok);
/* "/t/<tok>/<name>" into out. 0 ok, -1 does not fit. */
int  mcp_token_path(const char *tok, const char *name, char *out,
                    unsigned long cap);

/* A new QuickTime connection (handle for down_*). Returns conn id >= 0,
   or -1 (busy: a 503 was attempted and the handle closed). */
int  mcp_accept(McpServer *s, void *down);
/* Do some work on every connection. Returns > 0 if anything moved. */
/* (0.5t) A QuickTime connection whose request (req, complete) was
   answered at Open Transport notifier time from a RAM pre-buffer: the
   head and body bytes [s, s + sent) are already on the wire, where
   [s, s + n) is req's range resolved against total. mc_proxy continues
   the body from s + sent: a parked upstream at that offset is adopted,
   else a new upstream "Range: bytes=(s+sent)-" is opened. No head is
   sent again. Returns conn id >= 0, or -1 (no free connection, unknown
   target or bad range: nothing was written; the caller closes down). */
int  mcp_adopt_downstream(McpServer *s, void *down, const struct McpReq *req,
                          unsigned long total, unsigned long sent);
int  mcp_poll(McpServer *s);
int  mcp_active(McpServer *s);       /* open QuickTime connections     */
int  mcp_parked(McpServer *s);       /* parked upstreams               */
void mcp_cancel_all(McpServer *s);   /* hard-close everything          */
const McpStats *mcp_stats(McpServer *s);

/* ==================================================================
   Pure helpers (exported for the unit tests and for QTSniff)
   ================================================================== */

/* Range kinds */
#define MCP_R_NONE   0
#define MCP_R_FROMTO 1   /* bytes=a-b */
#define MCP_R_FROM   2   /* bytes=a-  */
#define MCP_R_SUFFIX 3   /* bytes=-n  (a = n) */

typedef struct McpReq
{
    char          buf[MCP_REQ_CAP];
    unsigned long len, scan;
    int           done;          /* 1 = complete                       */
    int           err;           /* HTTP status for a bad request, or 0 */
    char          method[16];
    char          target[MCP_TARGET_CAP];
    int           vmajor, vminor;
    int           range_kind;    /* MCP_R_*                            */
    unsigned long ra, rb;
    int           range_seen;    /* a Range header was present         */
    int           conn_close, conn_keep;
    int           nheaders;
} McpReq;

void mcp_req_init(McpReq *r);
/* Feed bytes. *used = bytes consumed (the rest belongs to a following
   request). Returns 0 = need more, 1 = complete, -1 = bad (r->err). */
int  mcp_req_feed(McpReq *r, const char *p, unsigned long n,
                  unsigned long *used);

/* Range header value -> kind/a/b. 0 = one valid byte range, -1 = to be
   ignored (invalid, several ranges, other unit). */
int  mcp_parse_range(const char *v, int *kind, unsigned long *a,
                     unsigned long *b);
/* kind/a/b + total -> [*s, *s + *n). 0 ok, 1 unsatisfiable (416),
   -1 total unknown and it is needed. MCP_R_NONE -> whole file. */
int  mcp_range_resolve(int kind, unsigned long a, unsigned long b,
                       unsigned long total, unsigned long *s,
                       unsigned long *n);

/* Response head into out (NUL-terminated). status 200 / 206 / 416 /
   errors; for 206: s, n, total; for 200: n = Content-Length; for 416:
   total (or MCP_UNKNOWN). Returns length, or 0 if it did not fit. */
unsigned long mcp_build_head(char *out, unsigned long cap, int status,
                             int http10, unsigned long s, unsigned long n,
                             unsigned long total, const char *ctype,
                             unsigned long body_len_for_errors);
const char *mcp_reason(int status);

typedef struct McpResp
{
    int           status, vminor;
    int           has_length;
    unsigned long length;
    int           has_crange;     /* Content-Range: bytes a-b/T        */
    unsigned long cr_s, cr_e, cr_total;   /* cr_total MCP_UNKNOWN = '*' */
    int           chunked;
    int           conn_close;
    char          location[MCP_URL_CAP];
    char          ctype[64];
} McpResp;

/* Upstream head (status line .. blank line, len bytes). 0 ok, -1 bad. */
int  mcp_resp_parse(const char *h, unsigned long len, McpResp *r);
/* End of the head in buf: index just after the blank line, or 0. */
unsigned long mcp_head_end(const char *buf, unsigned long len,
                           unsigned long from);

/* Chunked decoder, in place */
typedef struct McpChunk
{
    int           st;
    unsigned long left;          /* data bytes left in this chunk       */
    int           digits;
    int           done;
} McpChunk;
void mcp_chunk_init(McpChunk *c);
/* Decode buf[0..n) in place. *out = decoded bytes now at buf[0..*out).
   *used = input consumed (< n only when the body ended). Returns 0 ok,
   1 = body complete, -1 = malformed. */
int  mcp_chunk_run(McpChunk *c, char *buf, unsigned long n,
                   unsigned long *out, unsigned long *used);

/* "http[s]://host[:port]/path" -> parts. 0 ok, -1 bad. */
int  mcp_url_split(const char *url, int *tls, char *host,
                   unsigned long hcap, unsigned short *port, char *path,
                   unsigned long pcap);
/* Resolve a Location against base into out. 0 ok, -1 bad/too long. */
int  mcp_url_resolve(const char *base, const char *loc, char *out,
                     unsigned long cap);

#endif
