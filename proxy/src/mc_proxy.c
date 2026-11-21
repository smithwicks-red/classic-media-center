/*
    mc_proxy.c -- see mc_proxy.h. Strict C89: no // comments, no long
    long, declarations at block top. Only <string.h> and <stdio.h>
    (sprintf into bounded buffers) from the C library.

    Wire text uses \015\012 so CodeWarrior's newline mapping cannot
    change the bytes.
*/
#include <string.h>
#include <stdio.h>
#include "mc_proxy.h"

#define CRLF "\015\012"
#define MCP_HEAD_CAP  640
#define MCP_UREQ_CAP  (MCP_URL_CAP + 512)
#define MAX_U32       0xFFFFFFFFUL

/* ================================================================== */
/* small helpers                                                      */
/* ================================================================== */

static int lc(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

static int ncaseeq(const char *a, const char *b, unsigned long n)
{
    unsigned long i;
    for (i = 0; i < n; i++)
    {
        if (lc((unsigned char)a[i]) != lc((unsigned char)b[i])) return 0;
        if (a[i] == 0) return 1;
    }
    return 1;
}

static void scopy(char *d, const char *s, unsigned long cap)
{
    unsigned long n = (unsigned long)strlen(s);
    if (cap == 0) return;
    if (n >= cap) n = cap - 1;
    memcpy(d, s, n);
    d[n] = 0;
}

static int is_digit(int c) { return c >= '0' && c <= '9'; }

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* decimal digits at *pp -> *v (32-bit range, < MAX_U32). Advances *pp.
   0 ok, -1 no digit / overflow. */
static int parse_u32(const char **pp, unsigned long *v)
{
    const char *p = *pp;
    unsigned long x = 0;
    int nd = 0;
    while (is_digit((unsigned char)*p))
    {
        unsigned long d = (unsigned long)(*p - '0');
        if (x > (MAX_U32 - 1UL - d) / 10UL) return -1;
        x = x * 10UL + d;
        p++;
        nd++;
    }
    if (nd == 0) return -1;
    *v = x;
    *pp = p;
    return 0;
}

static const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

/* ================================================================== */
/* request parser                                                     */
/* ================================================================== */

void mcp_req_init(McpReq *r)
{
    memset(r, 0, sizeof *r);
}

unsigned long mcp_head_end(const char *b, unsigned long len,
                           unsigned long from)
{
    unsigned long i;
    for (i = from; i < len; i++)
    {
        if (b[i] != '\012') continue;
        if (i + 1 < len && b[i + 1] == '\012') return i + 2;
        if (i + 2 < len && b[i + 1] == '\015' && b[i + 2] == '\012')
            return i + 3;
    }
    return 0;
}

static int tchar(int c)
{
    if (c <= 32 || c >= 127) return 0;
    return strchr("()<>@,;:\\\"/[]?={}", c) == NULL;
}

/* Does the comma-separated header value contain token tok? */
static int has_token(const char *v, const char *tok)
{
    unsigned long n = (unsigned long)strlen(tok);
    const char *p = v;
    while (*p)
    {
        p = skip_ws(p);
        if (ncaseeq(p, tok, n))
        {
            const char *q = skip_ws(p + n);
            if (*q == 0 || *q == ',') return 1;
        }
        while (*p && *p != ',') p++;
        if (*p == ',') p++;
    }
    return 0;
}

int mcp_parse_range(const char *v, int *kind, unsigned long *a,
                    unsigned long *b)
{
    const char *p = skip_ws(v);
    unsigned long x, y;
    *kind = MCP_R_NONE;
    *a = *b = 0;
    if (!ncaseeq(p, "bytes", 5)) return -1;
    p = skip_ws(p + 5);
    if (*p != '=') return -1;
    p = skip_ws(p + 1);
    if (strchr(p, ',') != NULL) return -1;          /* several ranges */
    if (*p == '-')
    {
        p++;
        if (parse_u32(&p, &x) != 0) return -1;
        p = skip_ws(p);
        if (*p != 0 || x == 0) return -1;
        *kind = MCP_R_SUFFIX;
        *a = x;
        return 0;
    }
    if (parse_u32(&p, &x) != 0) return -1;
    p = skip_ws(p);
    if (*p != '-') return -1;
    p = skip_ws(p + 1);
    if (*p == 0)
    {
        *kind = MCP_R_FROM;
        *a = x;
        return 0;
    }
    if (parse_u32(&p, &y) != 0) return -1;
    p = skip_ws(p);
    if (*p != 0 || y < x) return -1;
    *kind = MCP_R_FROMTO;
    *a = x;
    *b = y;
    return 0;
}

int mcp_range_resolve(int kind, unsigned long a, unsigned long b,
                      unsigned long total, unsigned long *s,
                      unsigned long *n)
{
    if (total == MCP_UNKNOWN) return -1;
    switch (kind)
    {
    case MCP_R_FROMTO:
        if (a >= total) return 1;
        if (b >= total) b = total - 1UL;
        *s = a;
        *n = b - a + 1UL;
        return 0;
    case MCP_R_FROM:
        if (a >= total) return 1;
        *s = a;
        *n = total - a;
        return 0;
    case MCP_R_SUFFIX:
        if (a == 0 || total == 0) return 1;
        if (a >= total) { *s = 0; *n = total; }
        else { *s = total - a; *n = a; }
        return 0;
    default:
        *s = 0;
        *n = total;
        return 0;
    }
}

/* Parse the complete head r->buf[0..r->len). 0 ok, -1 (r->err set). */
static int req_parse(McpReq *r)
{
    char *p = r->buf, *end = r->buf + r->len, *eol, *q;
    unsigned long n;
    int ranges = 0, rk = MCP_R_NONE;
    unsigned long ra = 0, rb = 0;

    /* request line */
    eol = (char *)memchr(p, '\012', (size_t)(end - p));
    if (eol == NULL) { r->err = 400; return -1; }
    *eol = 0;
    if (eol > p && eol[-1] == '\015') eol[-1] = 0;
    for (q = p; *q; q++)
        if ((unsigned char)*q < 32 || (unsigned char)*q == 127)
        { r->err = 400; return -1; }
    q = p;
    n = 0;
    while (*q >= 'A' && *q <= 'Z') { q++; n++; }
    if (n == 0 || n >= sizeof r->method || *q != ' ')
    { r->err = 400; return -1; }
    memcpy(r->method, p, n);
    r->method[n] = 0;
    p = q + 1;
    q = strchr(p, ' ');
    if (q == NULL || q == p) { r->err = 400; return -1; }
    n = (unsigned long)(q - p);
    if (n >= sizeof r->target) { r->err = 414; return -1; }
    memcpy(r->target, p, n);
    r->target[n] = 0;
    p = q + 1;
    if (strncmp(p, "HTTP/", 5) != 0 || !is_digit((unsigned char)p[5])
        || p[6] != '.' || !is_digit((unsigned char)p[7]) || p[8] != 0)
    { r->err = 400; return -1; }
    r->vmajor = p[5] - '0';
    r->vminor = p[7] - '0';
    if (r->vmajor != 1) { r->err = 505; return -1; }

    /* headers */
    p = eol + 1;
    while (p < end)
    {
        char *name, *val, *t;
        eol = (char *)memchr(p, '\012', (size_t)(end - p));
        if (eol == NULL) break;
        *eol = 0;
        if (eol > p && eol[-1] == '\015') eol[-1] = 0;
        if (*p == 0) break;                    /* the blank line */
        if (*p == ' ' || *p == '\t') { r->err = 400; return -1; }
        if (++r->nheaders > 64) { r->err = 431; return -1; }
        name = p;
        t = strchr(p, ':');
        if (t == NULL || t == p) { r->err = 400; return -1; }
        for (q = name; q < t; q++)
            if (!tchar((unsigned char)*q)) { r->err = 400; return -1; }
        *t = 0;
        val = (char *)skip_ws(t + 1);
        for (q = val; *q; q++)
            if (((unsigned char)*q < 32 && *q != '\t')
                || (unsigned char)*q == 127)
            { r->err = 400; return -1; }
        /* trim trailing white space */
        q = val + strlen(val);
        while (q > val && (q[-1] == ' ' || q[-1] == '\t')) *--q = 0;
        if (ncaseeq(name, "range", 6))
        {
            ranges++;
            if (mcp_parse_range(val, &rk, &ra, &rb) != 0) rk = MCP_R_NONE;
        }
        else if (ncaseeq(name, "connection", 11))
        {
            if (has_token(val, "close")) r->conn_close = 1;
            if (has_token(val, "keep-alive")) r->conn_keep = 1;
        }
        p = eol + 1;
    }
    r->range_seen = ranges;
    if (ranges == 1 && rk != MCP_R_NONE)
    {
        r->range_kind = rk;
        r->ra = ra;
        r->rb = rb;
    }
    return 0;
}

int mcp_req_feed(McpReq *r, const char *p, unsigned long n,
                 unsigned long *used)
{
    unsigned long skipped = 0, take, end, before;
    *used = 0;
    if (r->done) return 1;
    if (r->err) return -1;
    /* RFC 9112 2.2: ignore empty lines before the request line */
    while (r->len == 0 && n > 0 && (*p == '\015' || *p == '\012'))
    {
        p++;
        n--;
        skipped++;
    }
    if (n == 0) { *used = skipped; return 0; }
    take = MCP_REQ_CAP - 1UL - r->len;
    if (take > n) take = n;
    before = r->len;
    memcpy(r->buf + r->len, p, (size_t)take);
    r->len += take;
    end = mcp_head_end(r->buf, r->len, r->scan);
    if (end == 0)
    {
        *used = skipped + take;
        r->scan = r->len >= 3 ? r->len - 3 : 0;
        if (r->len >= MCP_REQ_CAP - 1UL) { r->err = 431; return -1; }
        return 0;
    }
    *used = skipped + (end - before);
    r->len = end;
    r->buf[r->len] = 0;
    if (req_parse(r) != 0) return -1;
    r->done = 1;
    return 1;
}

/* ================================================================== */
/* response heads                                                     */
/* ================================================================== */

const char *mcp_reason(int status)
{
    switch (status)
    {
    case 200: return "OK";
    case 206: return "Partial Content";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 408: return "Request Timeout";
    case 414: return "URI Too Long";
    case 416: return "Range Not Satisfiable";
    case 431: return "Request Header Fields Too Large";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    case 504: return "Gateway Timeout";
    case 505: return "HTTP Version Not Supported";
    default:  return "Error";
    }
}

unsigned long mcp_build_head(char *out, unsigned long cap, int status,
                             int http10, unsigned long s, unsigned long n,
                             unsigned long total, const char *ctype,
                             unsigned long body_len_for_errors)
{
    char t[MCP_HEAD_CAP + 64];
    char *w = t;
    if (ctype == NULL || ctype[0] == 0) ctype = "video/quicktime";
    {
        const char *k;
        for (k = ctype; *k; k++)
            if ((unsigned char)*k < 32 || (unsigned char)*k >= 127)
            { ctype = "video/quicktime"; break; }
    }
    w += sprintf(w, "HTTP/1.%d %d %s" CRLF, http10 ? 0 : 1, status,
                 mcp_reason(status));
    if (status == 200 || status == 206)
    {
        w += sprintf(w, "Content-Type: %.60s" CRLF, ctype);
        w += sprintf(w, "Accept-Ranges: bytes" CRLF);
        if (status == 206)
            w += sprintf(w, "Content-Range: bytes %lu-%lu/%lu" CRLF, s,
                         s + n - 1UL, total);
        w += sprintf(w, "Content-Length: %lu" CRLF, n);
    }
    else
    {
        w += sprintf(w, "Content-Type: text/plain" CRLF);
        if (status == 416)
        {
            if (total != MCP_UNKNOWN)
                w += sprintf(w, "Content-Range: bytes */%lu" CRLF, total);
            w += sprintf(w, "Accept-Ranges: bytes" CRLF);
        }
        w += sprintf(w, "Content-Length: %lu" CRLF, body_len_for_errors);
    }
    w += sprintf(w, "Connection: close" CRLF "Server: MediaCenter/0.5"
                 CRLF CRLF);
    if ((unsigned long)(w - t) + 1UL > cap) return 0;
    memcpy(out, t, (size_t)(w - t) + 1);
    return (unsigned long)(w - t);
}

/* ================================================================== */
/* upstream response head                                             */
/* ================================================================== */

static int parse_crange(const char *v, McpResp *r)
{
    const char *p = skip_ws(v);
    if (!ncaseeq(p, "bytes", 5)) return -1;
    p = skip_ws(p + 5);
    if (*p == '*')
    {
        p = skip_ws(p + 1);
        if (*p != '/') return -1;
        p++;
        if (parse_u32(&p, &r->cr_total) != 0) return -1;
        r->has_crange = 2;              /* "*\/T": only the total */
        return 0;
    }
    if (parse_u32(&p, &r->cr_s) != 0 || *p != '-') return -1;
    p++;
    if (parse_u32(&p, &r->cr_e) != 0 || *p != '/') return -1;
    p++;
    if (*p == '*') r->cr_total = MCP_UNKNOWN;
    else if (parse_u32(&p, &r->cr_total) != 0) return -1;
    if (r->cr_e < r->cr_s) return -1;
    if (r->cr_total != MCP_UNKNOWN && r->cr_e >= r->cr_total) return -1;
    r->has_crange = 1;
    return 0;
}

int mcp_resp_parse(const char *h, unsigned long len, McpResp *r)
{
    char line[MCP_URL_CAP + 64];
    unsigned long i = 0, n;
    int first = 1;
    memset(r, 0, sizeof *r);
    while (i < len)
    {
        unsigned long j = i;
        while (j < len && h[j] != '\012') j++;
        n = j - i;
        if (n > 0 && h[i + n - 1] == '\015') n--;
        if (n >= sizeof line) n = sizeof line - 1;   /* long: truncated */
        memcpy(line, h + i, (size_t)n);
        line[n] = 0;
        i = j + 1;
        if (first)
        {
            const char *p = line;
            unsigned long st;
            first = 0;
            if (strncmp(p, "HTTP/1.", 7) != 0 || !is_digit((unsigned char)p[7]))
                return -1;
            r->vminor = p[7] - '0';
            p = skip_ws(p + 8);
            if (parse_u32(&p, &st) != 0 || st < 100 || st > 999) return -1;
            r->status = (int)st;
            if (r->vminor == 0) r->conn_close = 1;
            continue;
        }
        if (line[0] == 0) break;
        {
            char *c = strchr(line, ':');
            const char *v;
            char *q;
            if (c == NULL) continue;           /* tolerate junk lines */
            *c = 0;
            v = skip_ws(c + 1);
            q = (char *)v + strlen(v);
            while (q > v && (q[-1] == ' ' || q[-1] == '\t')) *--q = 0;
            if (ncaseeq(line, "content-length", 15))
            {
                /* R1 #12: "N, N" (a merged duplicate) is N */
                const char *p = v;
                unsigned long x;
                for (;;)
                {
                    if (parse_u32(&p, &x) != 0) return -1;
                    if (r->has_length && x != r->length) return -1;
                    r->has_length = 1;
                    r->length = x;
                    p = skip_ws(p);
                    if (*p == 0) break;
                    if (*p != ',') return -1;
                    p = skip_ws(p + 1);
                }
            }
            else if (ncaseeq(line, "content-range", 14))
            {
                if (parse_crange(v, r) != 0) return -1;
            }
            else if (ncaseeq(line, "transfer-encoding", 18))
            {
                if (has_token(v, "chunked")) r->chunked = 1;
                else if (!has_token(v, "identity")) return -1;
            }
            else if (ncaseeq(line, "location", 9))
            {
                if (strlen(v) >= sizeof r->location) return -1;
                scopy(r->location, v, sizeof r->location);
            }
            else if (ncaseeq(line, "content-type", 13))
            {
                /* R1 #10: never copy control characters into QuickTime's
                   head; a type that does not fit is dropped too */
                const char *k;
                int ok = strlen(v) < sizeof r->ctype;
                for (k = v; *k && ok; k++)
                    if ((unsigned char)*k < 32 || (unsigned char)*k >= 127)
                        ok = 0;
                if (ok) scopy(r->ctype, v, sizeof r->ctype);
                else r->ctype[0] = 0;
            }
            else if (ncaseeq(line, "connection", 11))
            {
                if (has_token(v, "close")) r->conn_close = 1;
            }
        }
    }
    if (first) return -1;
    if (r->chunked) r->has_length = 0;       /* RFC 9112 6.3 */
    return 0;
}

/* ================================================================== */
/* chunked decoder (in place: output never overtakes input)           */
/* ================================================================== */

enum { CK_SIZE = 0, CK_EXT, CK_DATA, CK_DCR, CK_DLF, CK_TSTART, CK_TLINE,
       CK_TLF, CK_DONE };

void mcp_chunk_init(McpChunk *c)
{
    memset(c, 0, sizeof *c);
    c->st = CK_SIZE;
}

int mcp_chunk_run(McpChunk *c, char *buf, unsigned long n,
                  unsigned long *out, unsigned long *used)
{
    unsigned long i = 0, o = 0, k;
    int ch, v;
    *out = 0;
    *used = 0;
    if (c->st == CK_DONE) return 1;
    while (i < n)
    {
        switch (c->st)
        {
        case CK_DATA:
            k = n - i;
            if (k > c->left) k = c->left;
            if (o != i) memmove(buf + o, buf + i, (size_t)k);
            o += k;
            i += k;
            c->left -= k;
            if (c->left == 0) c->st = CK_DCR;
            continue;
        default:
            break;
        }
        ch = (unsigned char)buf[i++];
        switch (c->st)
        {
        case CK_SIZE:
            v = hexval(ch);
            if (v >= 0)
            {
                if (c->left > 0x0FFFFFFFUL) return -1;
                c->left = c->left * 16UL + (unsigned long)v;
                c->digits++;
                break;
            }
            if (c->digits == 0) return -1;
            if (ch == '\012')
                c->st = c->left ? CK_DATA : CK_TSTART;
            else if (ch == ';' || ch == ' ' || ch == '\t' || ch == '\015')
            {
                c->st = CK_EXT;
                c->digits = 1;
            }
            else return -1;
            break;
        case CK_EXT:
            if (ch == '\012')
            {
                c->st = c->left ? CK_DATA : CK_TSTART;
                break;
            }
            if (++c->digits > 512) return -1;
            break;
        case CK_DCR:
            if (ch == '\015') c->st = CK_DLF;
            else if (ch == '\012') { c->st = CK_SIZE; c->digits = 0; }
            else return -1;
            break;
        case CK_DLF:
            if (ch != '\012') return -1;
            c->st = CK_SIZE;
            c->digits = 0;
            break;
        case CK_TSTART:
            if (ch == '\015') c->st = CK_TLF;
            else if (ch == '\012') c->st = CK_DONE;
            else { c->st = CK_TLINE; c->digits = 1; }
            break;
        case CK_TLINE:
            if (ch == '\012') c->st = CK_TSTART;
            else if (++c->digits > 4096) return -1;
            break;
        case CK_TLF:
            if (ch != '\012') return -1;
            c->st = CK_DONE;
            break;
        }
        if (c->st == CK_DONE)
        {
            c->done = 1;
            *out = o;
            *used = i;
            return 1;
        }
    }
    *out = o;
    *used = i;
    return 0;
}

/* ================================================================== */
/* URLs                                                               */
/* ================================================================== */

int mcp_url_split(const char *url, int *tls, char *host, unsigned long hcap,
                  unsigned short *port, char *path, unsigned long pcap)
{
    const char *p, *h;
    unsigned long n;
    if (ncaseeq(url, "https://", 8)) { *tls = 1; *port = 443; p = url + 8; }
    else if (ncaseeq(url, "http://", 7)) { *tls = 0; *port = 80; p = url + 7; }
    else return -1;
    h = p;
    while (*p && *p != ':' && *p != '/' && *p != '?' && *p != '#')
    {
        if (*p == '@' || *p == '[' || (unsigned char)*p <= 32) return -1;
        p++;
    }
    n = (unsigned long)(p - h);
    if (n == 0 || n >= hcap) return -1;
    memcpy(host, h, (size_t)n);
    host[n] = 0;
    if (*p == ':')
    {
        unsigned long v;
        p++;
        if (parse_u32(&p, &v) != 0 || v == 0 || v > 65535UL) return -1;
        *port = (unsigned short)v;
        if (*p && *p != '/' && *p != '?' && *p != '#') return -1;
    }
    if (*p == '#' || *p == 0)
    {
        if (pcap < 2) return -1;
        strcpy(path, "/");
        return 0;
    }
    n = 0;
    if (*p == '?')
    {
        if (pcap < 2) return -1;
        path[n++] = '/';
    }
    while (*p && *p != '#')
    {
        if ((unsigned char)*p <= 32) return -1;
        if (n + 1 >= pcap) return -1;
        path[n++] = *p++;
    }
    path[n] = 0;
    return 0;
}

/* RFC 3986 5.2.4: remove "." and ".." segments from the path that
   starts at p (up to '?' or the end), in place. */
static void remove_dots(char *p)
{
    char *in = p, *out = p, *q;
    char *qs = strchr(p, '?');
    char tail[MCP_URL_CAP];
    tail[0] = 0;
    if (qs != NULL) { scopy(tail, qs, sizeof tail); *qs = 0; }
    while (*in)
    {
        if (strncmp(in, "../", 3) == 0) in += 3;
        else if (strncmp(in, "./", 2) == 0) in += 2;
        else if (strncmp(in, "/./", 3) == 0) in += 2;
        else if (strcmp(in, "/.") == 0) in[1] = 0;
        else if (strncmp(in, "/../", 4) == 0 || strcmp(in, "/..") == 0)
        {
            if (in[3] == 0) { in += 2; *in = '/'; }   /* "/.." -> "/" */
            else in += 3;                              /* "/../" -> "/" */
            /* drop the last segment already written */
            while (out > p && *--out != '/') { }
        }
        else if (strcmp(in, ".") == 0 || strcmp(in, "..") == 0) in += strlen(in);
        else
        {
            /* copy one segment: optional leading '/', up to the next '/' */
            q = in;
            if (*q == '/') q++;
            while (*q && *q != '/') q++;
            memmove(out, in, (size_t)(q - in));
            out += q - in;
            in = q;
        }
    }
    *out = 0;
    if (tail[0]) strcat(p, tail);
}

int mcp_url_resolve(const char *base, const char *loc, char *out,
                    unsigned long cap)
{
    char t[MCP_URL_CAP * 2];
    const char *a, *auth_end, *bq, *k;
    char *path;
    unsigned long n, ln, w;
    loc = skip_ws(loc);
    ln = (unsigned long)strlen(loc);
    while (ln > 0 && (loc[ln - 1] == ' ' || loc[ln - 1] == '\011')) ln--;
    if (ln == 0 || ln >= MCP_URL_CAP) return -1;
    a = strstr(base, "://");
    if (a == NULL) return -1;
    auth_end = a + 3;
    while (*auth_end && *auth_end != '/' && *auth_end != '?'
           && *auth_end != '#') auth_end++;
    if ((unsigned long)strlen(base) >= MCP_URL_CAP) return -1;
    bq = auth_end;                       /* end of the base's path */
    while (*bq && *bq != '?' && *bq != '#') bq++;

    if (ncaseeq(loc, "http://", 7) || ncaseeq(loc, "https://", 8))
    {
        memcpy(t, loc, (size_t)ln);
        t[ln] = 0;
    }
    else if (loc[0] == '/' && loc[1] == '/')
    {
        n = (unsigned long)(a - base) + 1;            /* "https:" */
        memcpy(t, base, (size_t)n);
        memcpy(t + n, loc, (size_t)ln);
        t[n + ln] = 0;
    }
    else
    {
        n = (unsigned long)(auth_end - base);
        memcpy(t, base, (size_t)n);                   /* scheme://auth */
        t[n] = 0;
        if (loc[0] == '/')
        {
            memcpy(t + n, loc, (size_t)ln);
            t[n + ln] = 0;
        }
        else if (loc[0] == '?')
        {
            unsigned long pl = (unsigned long)(bq - auth_end);
            if (pl == 0) t[n++] = '/';
            memcpy(t + n, auth_end, (size_t)pl);
            memcpy(t + n + pl, loc, (size_t)ln);
            t[n + pl + ln] = 0;
        }
        else if (loc[0] == '#')
        {
            memcpy(t + n, auth_end, (size_t)(bq - auth_end));
            t[n + (bq - auth_end)] = 0;
            if (*bq == '?')
            {
                const char *e = bq;
                while (*e && *e != '#') e++;
                strncat(t, bq, (size_t)(e - bq));
            }
            if (t[n] == 0) strcat(t, "/");
        }
        else
        {
            /* merge: base path up to and including its last '/' */
            const char *last = NULL, *q;
            for (q = auth_end; q < bq; q++) if (*q == '/') last = q;
            if (last == NULL) { t[n++] = '/'; t[n] = 0; }
            else
            {
                memcpy(t + n, auth_end, (size_t)(last + 1 - auth_end));
                n += (unsigned long)(last + 1 - auth_end);
                t[n] = 0;
            }
            memcpy(t + n, loc, (size_t)ln);
            t[n + ln] = 0;
        }
    }
    /* drop a fragment, then the dot segments of the path */
    {
        char *h = strchr(t, '#');
        if (h != NULL) *h = 0;
    }
    path = strstr(t, "://") + 3;
    while (*path && *path != '/' && *path != '?') path++;
    if (*path == '/') remove_dots(path);
    /* percent-encode spaces, controls and 8-bit bytes */
    w = 0;
    for (k = t; *k; k++)
    {
        unsigned char ch = (unsigned char)*k;
        if (ch <= 32 || ch >= 127)
        {
            static const char hx[] = "0123456789ABCDEF";
            if (w + 4 > cap) return -1;
            out[w++] = '%';
            out[w++] = hx[ch >> 4];
            out[w++] = hx[ch & 15];
        }
        else
        {
            if (w + 2 > cap) return -1;
            out[w++] = (char)ch;
        }
    }
    out[w] = 0;
    return 0;
}

/* ================================================================== */
/* the server                                                         */
/* ================================================================== */

enum { CS_FREE = 0, CS_REQ, CS_UPSTART, CS_UPCONN, CS_UPHDR, CS_BODY,
       CS_CLOSE };

typedef struct McpToken
{
    int           used;
    char          tok[9];
    char          url[MCP_URL_CAP];
    char          final_url[MCP_URL_CAP];
    int           has_final;
    unsigned long final_t;
    unsigned long size;
    int           verified;      /* size confirmed by an upstream reply */
    char          ctype[64];     /* forced by the caller                */
    char          lctype[64];    /* learned from upstream (#14)         */
    unsigned long last_use;
} McpToken;

typedef struct McpPark
{
    void         *up;
    int           tok;
    unsigned long pos, uend, t;
} McpPark;

typedef struct McpConn
{
    int           state;
    unsigned long serial;
    void         *down;
    McpReq        req;
    int           tok, head_only, kind, client_range;
    unsigned long ra, rb;
    int           have_range;        /* s/n/dend known                  */
    unsigned long s, n, dend, pos;
    char          head[MCP_HEAD_CAP];
    unsigned long head_len, head_off;
    int           head_built;        /* body head built (not an error) */
    int           status_out;
    void         *up;
    char          url[MCP_URL_CAP];
    int           hops, resumes, using_cached, cache_retry_done;
    unsigned long sent_at_resume;
    char          ureq[MCP_UREQ_CAP];
    unsigned long ureq_len, ureq_off;
    char          uhdr[MCP_UHDR_CAP];
    unsigned long uhdr_len;
    McpResp       resp;
    int           chunked, u_done, u_over;
    int           park_ok;           /* unext is exactly the upstream's
                                        next file offset (R1 #1/#2)     */
    int           waiting;           /* empty read seen, since t_wait   */
    unsigned long t_wait;
    int           client_fin;        /* QuickTime half-closed (R1 #8)   */
    McpChunk      ch;
    unsigned long unext, uend;
    char         *buf;
    unsigned long boff, blen;
    unsigned long t_state, t_begin, sent;
    const char   *why;
} McpConn;

struct McpServer
{
    McpOps        ops;
    McpConfig     cfg;
    int           nconns;
    McpConn      *conns[MCP_MAX_CONNS];
    McpToken      toks[MCP_MAX_TOKENS];
    McpPark       park[MCP_MAX_PARK];
    McpStats      st;
    unsigned long seed;
    char          lb[900];
};

static unsigned long now(McpServer *s) { return s->ops.now(s->ops.ctx); }

static void say(McpServer *s, const char *line)
{
    if (s->ops.log != NULL) s->ops.log(s->ops.ctx, line);
}

#define LOGGING(s) ((s)->ops.log != NULL)

/* ---------------- tokens ---------------- */

static void hex8(unsigned long v, char *out)
{
    static const char hx[] = "0123456789abcdef";
    int i;
    for (i = 7; i >= 0; i--) { out[i] = hx[v & 15UL]; v >>= 4; }
    out[8] = 0;
}

static int tok_in_use(McpServer *s, int t)
{
    int i;
    for (i = 0; i < s->nconns; i++)
        if (s->conns[i]->state != CS_FREE && s->conns[i]->tok == t) return 1;
    for (i = 0; i < MCP_MAX_PARK; i++)
        if (s->park[i].up != NULL && s->park[i].tok == t) return 1;
    return 0;
}

int mcp_token_add(McpServer *s, const char *url, unsigned long size,
                  const char *ctype, char *tok)
{
    int i, slot = -1, tls;
    unsigned long oldest = 0, age, v;
    char host[256], path[8];
    unsigned short port;
    McpToken *t;

    if (strlen(url) >= MCP_URL_CAP) return -1;
    if (mcp_url_split(url, &tls, host, sizeof host, &port, path,
                      sizeof path) != 0)
    {
        /* path may be long: split only to validate scheme + host */
        char *big = (char *)s->ops.alloc(s->ops.ctx, MCP_URL_CAP);
        int bad;
        if (big == NULL) return -1;
        bad = mcp_url_split(url, &tls, host, sizeof host, &port, big,
                            MCP_URL_CAP);
        s->ops.release(s->ops.ctx, big);
        if (bad) return -1;
    }
    for (i = 0; i < MCP_MAX_TOKENS; i++)
        if (!s->toks[i].used) { slot = i; break; }
    if (slot < 0)
    {
        for (i = 0; i < MCP_MAX_TOKENS; i++)
        {
            if (tok_in_use(s, i)) continue;
            age = now(s) - s->toks[i].last_use;
            if (slot < 0 || age > oldest) { slot = i; oldest = age; }
        }
        if (slot < 0) return -1;
    }
    t = &s->toks[slot];
    memset(t, 0, sizeof *t);
    t->used = 1;
    scopy(t->url, url, sizeof t->url);
    t->size = size;
    if (ctype != NULL) scopy(t->ctype, ctype, sizeof t->ctype);
    t->last_use = now(s);
    for (;;)
    {
        int clash = 0;
        s->seed = s->seed * 1103515245UL + 12345UL + now(s);
        v = ((s->seed >> 8) & 0x00FFFFFFUL) | ((unsigned long)slot << 24);
        v &= 0xFFFFFFFFUL;
        hex8(v, t->tok);
        for (i = 0; i < MCP_MAX_TOKENS; i++)
            if (i != slot && s->toks[i].used
                && strcmp(s->toks[i].tok, t->tok) == 0) clash = 1;
        if (!clash) break;
    }
    if (tok != NULL) strcpy(tok, t->tok);
    return slot;
}

int mcp_token_path(const char *tok, const char *name, char *out,
                   unsigned long cap)
{
    unsigned long need = 3UL + strlen(tok) + 1UL + strlen(name) + 1UL;
    if (need > cap) return -1;
    strcpy(out, "/t/");
    strcat(out, tok);
    strcat(out, "/");
    strcat(out, name);
    return 0;
}

static int host_ok(const char *h)
{
    int n = 0;
    for (; *h; h++, n++)
    {
        int c = (unsigned char)*h;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
              || is_digit(c) || c == '.' || c == '-')) return 0;
    }
    return n > 0 && n < 256;
}

/* request target -> token slot, or -1 */
static int target_token(McpServer *s, const char *t)
{
    int i;
    if (ncaseeq(t, "http://", 7))
    {
        t += 7;
        while (*t && *t != '/') t++;
        if (*t == 0) return -1;
    }
    if (strncmp(t, "/t/", 3) == 0)
    {
        t += 3;
        for (i = 0; i < 8; i++) if (hexval((unsigned char)t[i]) < 0) return -1;
        if (t[8] != 0 && t[8] != '/' && t[8] != '?') return -1;
        for (i = 0; i < MCP_MAX_TOKENS; i++)
            if (s->toks[i].used && strncmp(s->toks[i].tok, t, 8) == 0)
                return i;
        return -1;
    }
    if (strncmp(t, "/a/", 3) == 0 && s->cfg.allow_host != NULL)
    {
        char host[256], url[MCP_URL_CAP];
        const char *slash = strchr(t + 3, '/');
        unsigned long hn, pn;
        if (slash == NULL) return -1;
        hn = (unsigned long)(slash - (t + 3));
        if (hn == 0 || hn >= sizeof host) return -1;
        memcpy(host, t + 3, (size_t)hn);
        host[hn] = 0;
        if (!host_ok(host) || !s->cfg.allow_host(s->ops.ctx, host)) return -1;
        /* (J5) QuickTime caches http movies by URL, so a reference movie
           may add "?t=2": the query (and a fragment) is IGNORED here --
           same film, same token, not sent upstream. */
        pn = 0;
        while (slash[pn] != 0 && slash[pn] != '?' && slash[pn] != '#') pn++;
        if (8UL + hn + pn + 1UL > sizeof url) return -1;
        strcpy(url, "https://");
        strcat(url, host);
        memcpy(url + 8 + hn, slash, (size_t)pn);
        url[8 + hn + pn] = 0;
        for (i = 0; i < MCP_MAX_TOKENS; i++)
            if (s->toks[i].used && strcmp(s->toks[i].url, url) == 0)
                return i;
        return mcp_token_add(s, url, MCP_UNKNOWN, NULL, NULL);
    }
    return -1;
}

/* ---------------- parking ---------------- */

static int park_limit(McpServer *s)
{
    return s->cfg.max_parked < 0 ? 0 : s->cfg.max_parked;
}

static void park_up(McpServer *s, void *up, int tok, unsigned long pos,
                    unsigned long uend)
{
    int i, slot = -1, lim = park_limit(s);
    unsigned long oldest = 0, age;
    if (lim == 0 || (uend != MCP_UNKNOWN && pos >= uend))
    {
        s->ops.up_close(up);
        return;
    }
    for (i = 0; i < lim; i++)
        if (s->park[i].up == NULL) { slot = i; break; }
    if (slot < 0)
    {
        for (i = 0; i < lim; i++)
        {
            age = now(s) - s->park[i].t;
            if (slot < 0 || age > oldest) { slot = i; oldest = age; }
        }
        s->ops.up_close(s->park[slot].up);
        s->park[slot].up = NULL;
    }
    s->park[slot].up = up;
    s->park[slot].tok = tok;
    s->park[slot].pos = pos;
    s->park[slot].uend = uend;
    s->park[slot].t = now(s);
    s->st.parked++;
    if (LOGGING(s))
    {
        sprintf(s->lb, "proxy: upstream parked at %lu (token %s)", pos,
                s->toks[tok].tok);
        say(s, s->lb);
    }
}

static int adopt(McpServer *s, McpConn *c)
{
    int i;
    for (i = 0; i < MCP_MAX_PARK; i++)
    {
        McpPark *p = &s->park[i];
        if (p->up == NULL || p->tok != c->tok) continue;
        if (p->pos > c->pos || c->pos - p->pos > s->cfg.adopt_gap) continue;
        if (p->uend != MCP_UNKNOWN && p->uend <= c->pos) continue;
        if (s->ops.up_step(p->up) != MCP_T_OPEN) continue;
        c->up = p->up;
        c->unext = p->pos;
        c->uend = p->uend;
        c->chunked = 0;
        c->u_done = c->u_over = 0;
        c->park_ok = 1;
        p->up = NULL;
        s->st.adopted++;
        if (LOGGING(s))
        {
            sprintf(s->lb, "proxy #%lu: adopted the parked upstream at %lu"
                    " (skip %lu)", c->serial, c->unext, c->pos - c->unext);
            say(s, s->lb);
        }
        return 1;
    }
    return 0;
}

static void expire_parked(McpServer *s)
{
    int i;
    for (i = 0; i < MCP_MAX_PARK; i++)
        if (s->park[i].up != NULL
            && now(s) - s->park[i].t > s->cfg.park_ticks)
        {
            s->ops.up_close(s->park[i].up);
            s->park[i].up = NULL;
        }
}

/* ---------------- connection helpers ---------------- */

static void conn_free(McpServer *s, McpConn *c)
{
    if (c->up != NULL) { s->ops.up_close(c->up); c->up = NULL; }
    c->state = CS_FREE;
    c->down = NULL;
}

static void log_end(McpServer *s, McpConn *c, const char *how)
{
    if (!LOGGING(s)) return;
    sprintf(s->lb, "proxy #%lu: %s, %lu bytes sent in %lu ticks", c->serial,
            how, c->sent, now(s) - c->t_begin);
    say(s, s->lb);
}

/* the connection is finished: hand the handle back */
static void conn_end(McpServer *s, McpConn *c, int abort, const char *how)
{
    log_end(s, c, how);
    if (c->down != NULL) s->ops.down_close(c->down, abort);
    conn_free(s, c);
}

static int parkable(McpServer *s, McpConn *c)
{
    return c->up != NULL && c->park_ok && !c->chunked && !c->u_over
        && !c->u_done && c->tok >= 0 && s->ops.up_step(c->up) == MCP_T_OPEN;
}

static void release_up(McpServer *s, McpConn *c)
{
    if (c->up == NULL) return;
    if (parkable(s, c)) park_up(s, c->up, c->tok, c->unext, c->uend);
    else s->ops.up_close(c->up);
    c->up = NULL;
}

/* error answer (before any body head was sent), then close */
static void send_error(McpServer *s, McpConn *c, int status,
                       unsigned long total)
{
    char body[80];
    unsigned long bl, hl;
    if (c->up != NULL) release_up(s, c);
    sprintf(body, "%d %s" CRLF, status, mcp_reason(status));
    bl = (unsigned long)strlen(body);
    hl = mcp_build_head(c->head, sizeof c->head - bl - 1, status,
                        s->cfg.http10_status, 0, 0, total, NULL, bl);
    if (c->head_only) bl = 0;
    memcpy(c->head + hl, body, (size_t)bl);
    c->head_len = hl + bl;
    c->head_off = 0;
    c->state = CS_CLOSE;
    c->t_state = now(s);
    c->status_out = status;
    if (status >= 500) s->st.errors++;
    if (LOGGING(s))
    {
        sprintf(s->lb, "proxy #%lu: -> %d %s%s%s", c->serial, status,
                mcp_reason(status), c->why ? " (" : "", c->why ? c->why : "");
        if (c->why) strcat(s->lb, ")");
        say(s, s->lb);
    }
}

static const char *ctype_for(McpServer *s, McpConn *c)
{
    McpToken *t = &s->toks[c->tok];
    if (t->ctype[0]) return t->ctype;
    if (s->cfg.default_type != NULL) return s->cfg.default_type;
    if (c->resp.ctype[0]) return c->resp.ctype;
    if (t->lctype[0]) return t->lctype;
    return "video/quicktime";
}

/* build the 200/206 head for [s, s+n) of total */
static void build_body_head(McpServer *s, McpConn *c, unsigned long total)
{
    int st = c->client_range ? 206 : 200;
    c->status_out = st;
    c->head_len = mcp_build_head(c->head, sizeof c->head, st,
                                 s->cfg.http10_status, c->s, c->n, total,
                                 ctype_for(s, c), 0);
    c->head_off = 0;
    c->head_built = 1;
    if (LOGGING(s))
    {
        if (st == 206)
            sprintf(s->lb, "proxy #%lu: -> 206 bytes %lu-%lu/%lu", c->serial,
                    c->s, c->s + c->n - 1UL, total);
        else
            sprintf(s->lb, "proxy #%lu: -> 200 length %lu", c->serial, c->n);
        say(s, s->lb);
    }
}

static void resume(McpServer *s, McpConn *c, const char *why);

/* failure on the way to the upstream head */
static void up_fail(McpServer *s, McpConn *c, int status, const char *why)
{
    if (c->up != NULL) { s->ops.up_close(c->up); c->up = NULL; }
    if (c->using_cached && !c->cache_retry_done)
    {
        /* the cached storage-node URL went bad: from the top, once */
        s->toks[c->tok].has_final = 0;
        c->cache_retry_done = 1;
        c->hops = 0;
        if (LOGGING(s))
        {
            sprintf(s->lb, "proxy #%lu: cached URL failed (%s); "
                    "starting again from the original URL", c->serial, why);
            say(s, s->lb);
        }
        c->state = CS_UPSTART;
        c->url[0] = 0;
        return;
    }
    if (c->head_built) { resume(s, c, why); return; }
    c->why = why;
    send_error(s, c, status, MCP_UNKNOWN);
}

static void resume(McpServer *s, McpConn *c, const char *why)
{
    if (c->up != NULL) { s->ops.up_close(c->up); c->up = NULL; }
    if (c->sent != c->sent_at_resume) c->resumes = 0;  /* progress made */
    c->sent_at_resume = c->sent;
    c->resumes++;
    if (s->cfg.max_resumes < 0 || c->resumes > s->cfg.max_resumes)
    {
        s->st.errors++;
        if (LOGGING(s))
        {
            sprintf(s->lb, "proxy #%lu: upstream lost (%s); giving up at "
                    "offset %lu", c->serial, why, c->pos);
            say(s, s->lb);
        }
        conn_end(s, c, 1, "aborted");
        return;
    }
    s->st.resumes++;
    if (LOGGING(s))
    {
        sprintf(s->lb, "proxy #%lu: upstream lost (%s); resuming at %lu "
                "(try %d)", c->serial, why, c->pos, c->resumes);
        say(s, s->lb);
    }
    c->boff = c->blen = 0;
    c->hops = 0;
    c->url[0] = 0;
    c->state = CS_UPSTART;
}

/* ---------------- states ---------------- */

static void start_upstream(McpServer *s, McpConn *c)
{
    McpToken *t = &s->toks[c->tok];
    char host[256], *path;
    unsigned short port;
    int tls;
    unsigned long from = 0;
    char *w;
    int suffix = 0;

    c->park_ok = 0;
    c->waiting = 0;
    if (c->url[0] == 0)
    {
        if (t->has_final && !c->cache_retry_done
            && now(s) - t->final_t < s->cfg.url_ttl_ticks
            && strcmp(t->final_url, t->url) != 0)
        {
            scopy(c->url, t->final_url, sizeof c->url);
            c->using_cached = 1;
        }
        else
        {
            scopy(c->url, t->url, sizeof c->url);
            c->using_cached = 0;
        }
    }
    if (c->have_range) from = c->pos;
    else if (c->kind == MCP_R_SUFFIX) suffix = 1;
    else if (c->kind == MCP_R_FROM || c->kind == MCP_R_FROMTO) from = c->ra;

    path = c->uhdr;                     /* scratch: free at this point */
    if (mcp_url_split(c->url, &tls, host, sizeof host, &port, path,
                      MCP_UHDR_CAP) != 0)
    {
        up_fail(s, c, 502, "bad upstream URL");
        return;
    }
    if (strlen(path) + strlen(host) + 300 > MCP_UREQ_CAP)
    {
        up_fail(s, c, 502, "upstream URL too long");
        return;
    }
    w = c->ureq;
    w += sprintf(w, "GET %.1000s HTTP/1.1" CRLF "Host: %.255s", path, host);
    if ((tls && port != 443) || (!tls && port != 80))
        w += sprintf(w, ":%u", (unsigned)port);
    w += sprintf(w, CRLF "User-Agent: %.100s" CRLF "Accept: */*" CRLF
                 "Accept-Encoding: identity" CRLF, s->cfg.user_agent);
    if (suffix)
        w += sprintf(w, "Range: bytes=-%lu" CRLF, c->ra);
    else
        w += sprintf(w, "Range: bytes=%lu-" CRLF, from);
    w += sprintf(w, "Connection: close" CRLF CRLF);
    c->ureq_len = (unsigned long)(w - c->ureq);
    c->ureq_off = 0;
    c->uhdr_len = 0;
    c->up = s->ops.up_open(s->ops.ctx, host, port, tls);
    s->st.upstream_opens++;
    if (LOGGING(s))
    {
        sprintf(s->lb, "proxy #%lu: upstream %.500s (%s%lu-)%s", c->serial,
                c->url, suffix ? "suffix " : "", suffix ? c->ra : from,
                c->using_cached ? " [cached URL]" : "");
        say(s, s->lb);
    }
    if (c->up == NULL)
    {
        up_fail(s, c, 502, "connect failed");
        return;
    }
    c->state = CS_UPCONN;
    c->t_state = now(s);
}

/* decoded bytes in buf[0..n) that start at file offset o = c->unext */
static void place(McpConn *c, unsigned long n)
{
    unsigned long o = c->unext, k = 0;
    c->unext += n;
    if (c->uend != MCP_UNKNOWN && c->unext >= c->uend) c->u_done = 1;
    if (o < c->pos)
    {
        k = c->pos - o;
        if (k > n) k = n;
    }
    c->boff = k;
    c->blen = n;
    if (o + n > c->dend)
    {
        c->blen = (c->dend > o) ? c->dend - o : 0;
        c->u_over = 1;
        if (c->boff > c->blen) c->boff = c->blen;
    }
}

/* raw upstream bytes in buf[0..n): decode (if chunked) and place */
static int ingest(McpServer *s, McpConn *c, unsigned long n)
{
    unsigned long out = n, used;
    (void)s;
    if (c->chunked)
    {
        int rc = mcp_chunk_run(&c->ch, c->buf, n, &out, &used);
        if (rc < 0) return -1;
        if (rc == 1) c->u_done = 1;
    }
    place(c, out);
    return 0;
}

static void upstream_head(McpServer *s, McpConn *c, unsigned long end)
{
    McpResp *r = &c->resp;
    McpToken *t = &s->toks[c->tok];
    unsigned long total, upos, uend, left;
    int st, explicit_total;

    if (mcp_resp_parse(c->uhdr, end, r) != 0)
    {
        up_fail(s, c, 502, "bad upstream head");
        return;
    }
    st = r->status;
    if (st >= 100 && st < 200)
    {
        memmove(c->uhdr, c->uhdr + end, (size_t)(c->uhdr_len - end));
        c->uhdr_len -= end;
        return;                                /* interim: keep reading */
    }
    if (st == 301 || st == 302 || st == 303 || st == 307 || st == 308)
    {
        char next[MCP_URL_CAP];
        if (r->location[0] == 0) { up_fail(s, c, 502, "redirect without Location"); return; }
        if (++c->hops > s->cfg.max_redirects) { up_fail(s, c, 502, "too many redirects"); return; }
        if (mcp_url_resolve(c->url, r->location, next, sizeof next) != 0)
        { up_fail(s, c, 502, "bad Location"); return; }
        s->ops.up_close(c->up);
        c->up = NULL;
        s->st.redirects++;
        if (LOGGING(s))
        {
            sprintf(s->lb, "proxy #%lu: upstream %d -> %.500s", c->serial, st,
                    next);
            say(s, s->lb);
        }
        scopy(c->url, next, sizeof c->url);
        start_upstream(s, c);
        return;
    }
    if (st == 416 && !c->head_built)
    {
        total = (r->has_crange == 2) ? r->cr_total : t->size;
        if (r->has_crange == 2) { t->size = r->cr_total; t->verified = 1; }
        c->park_ok = 0;
        c->why = "upstream 416";
        send_error(s, c, 416, total);
        return;
    }
    if (st != 200 && st != 206)
    {
        char why[40];
        if (c->using_cached && !c->cache_retry_done)
        {
            up_fail(s, c, 502, "upstream error with cached URL");
            return;
        }
        if (c->head_built) { resume(s, c, "upstream error status"); return; }
        sprintf(why, "upstream %d", st);
        c->why = (st == 404 || st == 410) ? "upstream 404" : "upstream error";
        (void)why;
        send_error(s, c, (st == 404 || st == 410) ? 404 : 502, MCP_UNKNOWN);
        return;
    }
    /* 200 / 206 */
    if (st == 206)
    {
        if (r->has_crange != 1) { up_fail(s, c, 502, "206 without Content-Range"); return; }
        upos = r->cr_s;
        uend = r->cr_e + 1UL;
        total = r->cr_total;
    }
    else
    {
        upos = 0;
        total = (r->has_length && !r->chunked) ? r->length : MCP_UNKNOWN;
        uend = total;
    }
    explicit_total = (total != MCP_UNKNOWN);
    /* R1 #4: a reply from the CACHED storage URL must be the same file */
    if (c->using_cached && !c->cache_retry_done && t->verified
        && (!explicit_total || total != t->size))
    {
        up_fail(s, c, 502, "cached URL answered a different file");
        return;
    }
    if (total == MCP_UNKNOWN) total = t->size;
    if (total == MCP_UNKNOWN && !c->head_built)
    {
        up_fail(s, c, 502, "upstream size unknown");
        return;
    }
    if (t->size != MCP_UNKNOWN && total != t->size)
    {
        if (c->head_built || t->verified)
        {
            /* R1 #9: the film changed under QuickTime: stop at once */
            if (LOGGING(s))
            {
                sprintf(s->lb, "proxy #%lu: file size changed (%lu, was %lu);"
                        " giving up", c->serial, total, t->size);
                say(s, s->lb);
            }
            s->st.errors++;
            if (c->head_built) { conn_end(s, c, 1, "aborted"); return; }
            c->why = "file size changed";
            send_error(s, c, 502, MCP_UNKNOWN);
            return;
        }
        if (LOGGING(s))
        {
            sprintf(s->lb, "proxy #%lu: size is %lu, not %lu as registered",
                    c->serial, total, t->size);
            say(s, s->lb);
        }
        /* R1 #3: the range was resolved against the wrong size */
        c->have_range = 0;
    }
    t->size = total;
    if (explicit_total) t->verified = 1;
    if (!t->has_final || strcmp(t->final_url, c->url) != 0)
    {
        /* R1 #4: final_t is set when the URL is LEARNED, so the TTL ends */
        scopy(t->final_url, c->url, sizeof t->final_url);
        t->has_final = 1;
        t->final_t = now(s);
    }
    if (r->ctype[0]) scopy(t->lctype, r->ctype, sizeof t->lctype);
    c->chunked = r->chunked;
    if (c->chunked) { mcp_chunk_init(&c->ch); uend = MCP_UNKNOWN; }
    c->unext = upos;
    c->uend = uend;
    c->u_done = c->u_over = 0;
    left = c->uhdr_len - end;
    /* R1 #1/#2: only an upstream nothing was read from may be parked
       before the body loop runs */
    c->park_ok = (left == 0);

    if (!c->have_range)
    {
        int rr = mcp_range_resolve(c->kind, c->ra, c->rb, total, &c->s, &c->n);
        if (rr != 0)
        {
            c->why = "range";
            send_error(s, c, 416, total);
            return;
        }
        c->have_range = 1;
        c->pos = c->s;
        c->dend = c->s + c->n;
    }
    if (upos > c->pos)
    {
        up_fail(s, c, 502, "upstream started after the wanted offset");
        return;
    }
    if (!c->head_built) build_body_head(s, c, total);
    if (c->head_only || c->n == 0)
    {
        release_up(s, c);
        c->state = CS_BODY;               /* sends the head, then ends */
        return;
    }
    memcpy(c->buf, c->uhdr + end, (size_t)left);
    c->park_ok = 1;      /* ingest() accounts for every byte from here on */
    c->boff = c->blen = 0;
    c->state = CS_BODY;
    c->t_state = now(s);
    if (left > 0 && ingest(s, c, left) != 0)
        resume(s, c, "bad chunked body");
}

static void handle_request(McpServer *s, McpConn *c)
{
    McpReq *q = &c->req;
    McpToken *t;
    int rr;

    s->st.requests++;
    if (LOGGING(s))
    {
        sprintf(s->lb, "proxy #%lu: %s %.400s HTTP/%d.%d", c->serial,
                q->method, q->target, q->vmajor, q->vminor);
        if (q->range_kind != MCP_R_NONE)
            sprintf(s->lb + strlen(s->lb), " range %d:%lu-%lu", q->range_kind,
                    q->ra, q->rb);
        say(s, s->lb);
    }
    c->head_only = strcmp(q->method, "HEAD") == 0;
    if (!c->head_only && strcmp(q->method, "GET") != 0)
    {
        send_error(s, c, 405, MCP_UNKNOWN);
        return;
    }
    c->tok = target_token(s, q->target);
    if (c->tok < 0)
    {
        c->why = "unknown token";
        send_error(s, c, 404, MCP_UNKNOWN);
        return;
    }
    t = &s->toks[c->tok];
    t->last_use = now(s);
    c->kind = q->range_kind;
    c->ra = q->ra;
    c->rb = q->rb;
    c->client_range = (c->kind != MCP_R_NONE);
    if (t->size != MCP_UNKNOWN && t->verified)
    {
        rr = mcp_range_resolve(c->kind, c->ra, c->rb, t->size, &c->s, &c->n);
        if (rr != 0)
        {
            send_error(s, c, 416, t->size);
            return;
        }
        c->have_range = 1;
        c->pos = c->s;
        c->dend = c->s + c->n;
        if (c->head_only || c->n == 0)
        {
            build_body_head(s, c, t->size);
            c->state = CS_BODY;
            return;
        }
        if (adopt(s, c))
        {
            build_body_head(s, c, t->size);
            c->state = CS_BODY;
            c->t_state = now(s);
            return;
        }
    }
    c->state = CS_UPSTART;
}

/* one connection, one pass. Returns bytes/progress moved. */
static long conn_step(McpServer *s, McpConn *c)
{
    long moved = 0, r;
    int st;
    char tmp[256];

    switch (c->state)
    {
    case CS_REQ:
        r = s->ops.down_read(c->down, c->buf, MCP_REQ_CAP);
        if (r > 0)
        {
            unsigned long used;
            int rc = mcp_req_feed(&c->req, c->buf, (unsigned long)r, &used);
            moved += r;
            if (rc < 0) { send_error(s, c, c->req.err, MCP_UNKNOWN); return moved; }
            if (rc == 1) handle_request(s, c);
            return moved;
        }
        if (r < 0)
        {
            conn_end(s, c, 0, c->req.len ? "client left mid-request"
                                          : "client left without a request");
            return 1;
        }
        if (now(s) - c->t_state > s->cfg.req_ticks)
        {
            send_error(s, c, 408, MCP_UNKNOWN);
            return 1;
        }
        return 0;

    case CS_UPSTART:
        start_upstream(s, c);
        return 1;

    case CS_UPCONN:
        st = s->ops.up_step(c->up);
        if (st == MCP_T_FAILED) { up_fail(s, c, 502, "upstream connection failed"); return 1; }
        if (st == MCP_T_OPEN && c->ureq_off < c->ureq_len)
        {
            r = s->ops.up_write(c->up, c->ureq + c->ureq_off,
                                c->ureq_len - c->ureq_off);
            if (r < 0) { up_fail(s, c, 502, "upstream write failed"); return 1; }
            c->ureq_off += (unsigned long)r;
            moved += r;
            if (c->ureq_off >= c->ureq_len) c->state = CS_UPHDR;
        }
        if (c->state == CS_UPCONN
            && now(s) - c->t_state > s->cfg.connect_ticks)
            up_fail(s, c, 504, "upstream connect timeout");
        return moved;

    case CS_UPHDR:
        st = s->ops.up_step(c->up);
        if (st == MCP_T_FAILED) { up_fail(s, c, 502, "upstream failed before its head"); return 1; }
        r = s->ops.up_read(c->up, c->uhdr + c->uhdr_len,
                           MCP_UHDR_CAP - 1UL - c->uhdr_len);
        if (r > 0)
        {
            unsigned long from = c->uhdr_len >= 3 ? c->uhdr_len - 3 : 0, end;
            c->uhdr_len += (unsigned long)r;
            moved += r;
            end = mcp_head_end(c->uhdr, c->uhdr_len, from);
            if (end != 0)
            {
                upstream_head(s, c, end);
                /* an interim 1xx leaves more head bytes behind */
                while (c->state == CS_UPHDR && c->uhdr_len > 0
                       && (end = mcp_head_end(c->uhdr, c->uhdr_len, 0)) != 0)
                    upstream_head(s, c, end);
            }
            else if (c->uhdr_len >= MCP_UHDR_CAP - 1UL)
                up_fail(s, c, 502, "upstream head too large");
            return moved;
        }
        if (r < 0) { up_fail(s, c, 502, "upstream closed before its head"); return 1; }
        if (now(s) - c->t_state > s->cfg.connect_ticks)
            up_fail(s, c, 504, "upstream head timeout");
        return 0;

    case CS_BODY:
        /* has QuickTime gone away? (anything it sends now is ignored).
           R1 #8: an orderly EOF may be a half-close -- keep sending
           until the body is done or a write fails. */
        r = c->client_fin ? 0 : s->ops.down_read(c->down, tmp, sizeof tmp);
        if (r == MCP_IO_EOF) c->client_fin = 1;
        else if (r < 0)
        {
            s->st.client_aborts++;
            release_up(s, c);
            conn_end(s, c, 0, "client closed");
            return 1;
        }
        while (moved < (long)s->cfg.per_pass)
        {
            if (c->head_off < c->head_len)
            {
                r = s->ops.down_write(c->down, c->head + c->head_off,
                                      c->head_len - c->head_off);
                if (r < 0) { s->st.client_aborts++; release_up(s, c); conn_end(s, c, 0, "client closed"); return moved + 1; }
                c->head_off += (unsigned long)r;
                moved += r;
                if (c->head_off < c->head_len) break;
            }
            if (c->head_only || c->n == 0)
            {
                release_up(s, c);
                conn_end(s, c, 0, "done");
                return moved + 1;
            }
            if (c->boff < c->blen)
            {
                r = s->ops.down_write(c->down, c->buf + c->boff,
                                      c->blen - c->boff);
                if (r < 0) { s->st.client_aborts++; release_up(s, c); conn_end(s, c, 0, "client closed"); return moved + 1; }
                if (r == 0) { c->waiting = 0; break; }   /* flow control */
                c->boff += (unsigned long)r;
                c->pos += (unsigned long)r;
                c->sent += (unsigned long)r;
                s->st.bytes_out += (unsigned long)r;
                moved += r;
                continue;
            }
            c->boff = c->blen = 0;
            if (c->pos >= c->dend)
            {
                release_up(s, c);
                conn_end(s, c, 0, "done");
                return moved + 1;
            }
            if (c->up == NULL || c->u_done)
            {
                resume(s, c, "upstream body ended early");
                return moved + 1;
            }
            st = s->ops.up_step(c->up);
            if (st == MCP_T_FAILED) { resume(s, c, "upstream failed"); return moved + 1; }
            {
                unsigned long want = s->cfg.buf_size;
                if (!c->chunked)
                {
                    unsigned long rem = c->dend - c->unext;
                    if (c->uend != MCP_UNKNOWN && c->uend - c->unext < rem)
                        rem = c->uend - c->unext;
                    if (rem < want) want = rem;
                }
                r = s->ops.up_read(c->up, c->buf, want);
            }
            if (r > 0)
            {
                moved += r;
                c->t_state = now(s);
                c->waiting = 0;
                if (ingest(s, c, (unsigned long)r) != 0)
                {
                    resume(s, c, "bad chunked body");
                    return moved;
                }
                continue;
            }
            if (r < 0) { resume(s, c, r == MCP_IO_EOF ? "upstream closed early" : "upstream read error"); return moved + 1; }
            /* R1 #7: the stall clock starts at the FIRST empty read with
               nothing waiting for QuickTime, not at the last data (a
               long pause in QuickTime is not an upstream stall) */
            if (!c->waiting) { c->waiting = 1; c->t_wait = now(s); }
            else if (now(s) - c->t_wait > s->cfg.idle_ticks)
            {
                resume(s, c, "upstream stalled");
                return moved + 1;
            }
            break;
        }
        return moved;

    case CS_CLOSE:
        if (c->head_off < c->head_len)
        {
            r = s->ops.down_write(c->down, c->head + c->head_off,
                                  c->head_len - c->head_off);
            if (r < 0) { conn_end(s, c, 0, "client closed"); return 1; }
            c->head_off += (unsigned long)r;
            moved += r;
        }
        if (c->head_off >= c->head_len)
        {
            conn_end(s, c, 0, "error sent");
            return moved + 1;
        }
        if (now(s) - c->t_state > s->cfg.req_ticks)
            conn_end(s, c, 1, "client never read the error");
        return moved;
    }
    return 0;
}

/* ================================================================== */
/* public                                                             */
/* ================================================================== */

McpServer *mcp_new(const McpOps *ops, const McpConfig *cfg)
{
    McpServer *s;
    int i;
    unsigned long csz;
    s = (McpServer *)ops->alloc(ops->ctx, (unsigned long)sizeof(McpServer));
    if (s == NULL) return NULL;
    memset(s, 0, sizeof *s);
    s->ops = *ops;
    if (cfg != NULL) s->cfg = *cfg;
    if (s->cfg.max_conns <= 0) s->cfg.max_conns = 4;
    if (s->cfg.max_conns > MCP_MAX_CONNS) s->cfg.max_conns = MCP_MAX_CONNS;
    if (s->cfg.max_parked == 0) s->cfg.max_parked = 2;
    if (s->cfg.max_parked > MCP_MAX_PARK) s->cfg.max_parked = MCP_MAX_PARK;
    if (s->cfg.buf_size == 0) s->cfg.buf_size = 16384UL;
    if (s->cfg.buf_size < MCP_UHDR_CAP) s->cfg.buf_size = MCP_UHDR_CAP;
    if (s->cfg.buf_size < MCP_REQ_CAP) s->cfg.buf_size = MCP_REQ_CAP;
    if (s->cfg.connect_ticks == 0) s->cfg.connect_ticks = 600;
    if (s->cfg.idle_ticks == 0) s->cfg.idle_ticks = 1200;
    if (s->cfg.req_ticks == 0) s->cfg.req_ticks = 600;
    if (s->cfg.park_ticks == 0) s->cfg.park_ticks = 900;
    if (s->cfg.adopt_gap == 0) s->cfg.adopt_gap = 262144UL;
    if (s->cfg.url_ttl_ticks == 0) s->cfg.url_ttl_ticks = 30UL * 3600UL;
    if (s->cfg.max_redirects <= 0) s->cfg.max_redirects = 5;
    if (s->cfg.max_resumes == 0) s->cfg.max_resumes = 3;
    if (s->cfg.per_pass == 0) s->cfg.per_pass = 65536UL;
    if (s->cfg.user_agent == NULL) s->cfg.user_agent = "MediaCenter/0.5";
    s->seed = now(s) ^ 0x5DEECE66UL;
    csz = (unsigned long)sizeof(McpConn) + s->cfg.buf_size;
    for (i = 0; i < s->cfg.max_conns; i++)
    {
        McpConn *c = (McpConn *)ops->alloc(ops->ctx, csz);
        if (c == NULL) { mcp_free(s); return NULL; }
        memset(c, 0, sizeof *c);
        c->buf = (char *)(c + 1);
        s->conns[i] = c;
        s->nconns++;
    }
    return s;
}

void mcp_cancel_all(McpServer *s)
{
    int i;
    for (i = 0; i < s->nconns; i++)
        if (s->conns[i]->state != CS_FREE)
            conn_end(s, s->conns[i], 1, "cancelled");
    for (i = 0; i < MCP_MAX_PARK; i++)
        if (s->park[i].up != NULL)
        {
            s->ops.up_close(s->park[i].up);
            s->park[i].up = NULL;
        }
}

void mcp_free(McpServer *s)
{
    int i;
    if (s == NULL) return;
    mcp_cancel_all(s);
    for (i = 0; i < s->nconns; i++) s->ops.release(s->ops.ctx, s->conns[i]);
    s->ops.release(s->ops.ctx, s);
}

int mcp_accept(McpServer *s, void *down)
{
    static const char busy[] =
        "HTTP/1.1 503 Service Unavailable" CRLF
        "Content-Type: text/plain" CRLF "Content-Length: 6" CRLF
        "Retry-After: 1" CRLF "Connection: close" CRLF CRLF "busy" CRLF;
    int i;
    for (i = 0; i < s->nconns; i++)
    {
        McpConn *c = s->conns[i];
        char *b;
        if (c->state != CS_FREE) continue;
        b = c->buf;
        memset(c, 0, sizeof *c);
        c->buf = b;
        c->state = CS_REQ;
        c->down = down;
        c->tok = -1;
        c->serial = ++s->st.accepted;
        c->t_begin = c->t_state = now(s);
        mcp_req_init(&c->req);
        return i;
    }
    s->st.accepted++;
    s->st.refused_busy++;
    (void)s->ops.down_write(down, busy, (unsigned long)(sizeof busy - 1));
    s->ops.down_close(down, 0);
    say(s, "proxy: busy, a connection got 503");
    return -1;
}

int mcp_adopt_downstream(McpServer *s, void *down, const McpReq *req,
                         unsigned long total, unsigned long sent)
{
    int i;
    McpConn *c = NULL;
    char *b;
    for (i = 0; i < s->nconns; i++)
        if (s->conns[i]->state == CS_FREE) { c = s->conns[i]; break; }
    if (c == NULL || !req->done || total == MCP_UNKNOWN) return -1;
    b = c->buf;
    memset(c, 0, sizeof *c);
    c->buf = b;
    c->req = *req;
    c->tok = target_token(s, c->req.target);
    if (c->tok < 0) { c->state = CS_FREE; return -1; }
    c->kind = c->req.range_kind;
    c->ra = c->req.ra;
    c->rb = c->req.rb;
    c->client_range = (c->kind != MCP_R_NONE);
    if (mcp_range_resolve(c->kind, c->ra, c->rb, total, &c->s, &c->n) != 0
        || sent > c->n)
    { c->state = CS_FREE; return -1; }
    c->down = down;
    c->serial = ++s->st.accepted;
    s->st.requests++;
    c->t_begin = c->t_state = now(s);
    s->toks[c->tok].last_use = now(s);
    c->have_range = 1;
    c->dend = c->s + c->n;
    c->pos = c->s + sent;
    c->sent = sent;
    s->st.bytes_out += sent;
    c->head_built = 1;                 /* sent at notifier time */
    c->status_out = c->client_range ? 206 : 200;
    c->head_len = c->head_off = 0;
    if (LOGGING(s))
    {
        sprintf(s->lb, "proxy #%lu: %.400s answered at notifier time (%d, "
                "%lu of %lu body bytes sent); continuing at %lu", c->serial,
                c->req.target, c->status_out, sent, c->n, c->pos);
        say(s, s->lb);
    }
    if (c->pos >= c->dend || adopt(s, c))
    {
        c->state = CS_BODY;
        return i;
    }
    c->state = CS_UPSTART;
    return i;
}

int mcp_poll(McpServer *s)
{
    int i;
    long moved = 0;
    for (i = 0; i < s->nconns; i++)
    {
        McpConn *c = s->conns[i];
        int guard;
        /* a few state changes per pass, so a request can get going */
        for (guard = 0; guard < 4 && c->state != CS_FREE; guard++)
        {
            int before = c->state;
            long m = conn_step(s, c);
            moved += m;
            if (c->state == before && m == 0) break;
            if (c->state == CS_BODY && before == CS_BODY) break;
        }
    }
    expire_parked(s);
    return moved > 0;
}

int mcp_active(McpServer *s)
{
    int i, n = 0;
    for (i = 0; i < s->nconns; i++) if (s->conns[i]->state != CS_FREE) n++;
    return n;
}

int mcp_parked(McpServer *s)
{
    int i, n = 0;
    for (i = 0; i < MCP_MAX_PARK; i++) if (s->park[i].up != NULL) n++;
    return n;
}

const McpStats *mcp_stats(McpServer *s) { return &s->st; }
