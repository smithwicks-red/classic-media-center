/*
    mc_core.c -- portable core of Classic Media Center 0.1 (now 0.4).
    See mc_core.h. Strict C89, no Mac headers, no malloc, 32-bit only.
*/
#include <string.h>
#include <stdio.h>
#include "mc_core.h"

/* (b19) Where the app talks to. Search and metadata go over https;
   the download URL handed to QuickTime stays plain http (QuickTime 6
   has no TLS). The host tests point these at a local server. */
const char *mc_api_base = "https://archive.org";
const char *mc_dl_base  = "http://archive.org";

int mc_meta_url(const char *id, char *out, long outMax)
{
    char esc[MC_URLMAX / 2];
    if (mc_path_escape(id, esc, (long)sizeof(esc)) != 0) return -1;
    if ((long)(strlen(mc_api_base) + strlen(esc) + 16) > outMax) return -1;
    sprintf(out, "%s/metadata/%s", mc_api_base, esc);
    return 0;
}

/* ==================================================================
   small helpers
   ================================================================== */

static int mc_lower(int c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A' + 'a';
    return c;
}

static int mc_streqi(const char *a, const char *b)
{
    while (*a && *b)
    {
        if (mc_lower((unsigned char)*a) != mc_lower((unsigned char)*b))
            return 0;
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

static int mc_startsi(const char *s, const char *prefix)
{
    while (*prefix)
    {
        if (mc_lower((unsigned char)*s) != mc_lower((unsigned char)*prefix))
            return 0;
        s++;
        prefix++;
    }
    return 1;
}

static int mc_endsi(const char *s, const char *suffix)
{
    long ls = (long)strlen(s), lx = (long)strlen(suffix);
    if (lx > ls) return 0;
    return mc_streqi(s + ls - lx, suffix);
}

/* copy with truncation; dst always terminated */
static void mc_copy(char *dst, const char *src, long max)
{
    long n = (long)strlen(src);
    if (n > max - 1) n = max - 1;
    memcpy(dst, src, (size_t)n);
    dst[n] = 0;
}

/* decimal text -> unsigned long, clamped; stops at first non-digit
   (so "26989036" and 26989036 and "370.3" all work) */
static unsigned long mc_atoul(const char *s)
{
    unsigned long v = 0;
    while (*s == ' ') s++;
    while (*s >= '0' && *s <= '9')
    {
        unsigned long d = (unsigned long)(*s - '0');
        if (v > (0xFFFFFFFFUL - d) / 10UL) return 0xFFFFFFFFUL;
        v = v * 10UL + d;
        s++;
    }
    return v;
}

static unsigned long mc_rd32(const unsigned char *p)
{
    return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16)
         | ((unsigned long)p[2] << 8)  |  (unsigned long)p[3];
}

static unsigned int mc_rd16(const unsigned char *p)
{
    return ((unsigned int)p[0] << 8) | (unsigned int)p[1];
}

static const char kHex[] = "0123456789ABCDEF";

/* ==================================================================
   a. URL building
   ================================================================== */

static int mc_unreserved(int c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
        || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_'
        || c == '~';
}

static int mc_escape(const char *s, char *out, long outMax, int isPath)
{
    long at = 0;
    const unsigned char *p = (const unsigned char *)s;

    for (; *p; p++)
    {
        int c = *p;
        if (mc_unreserved(c) || (isPath && c == '/'))
        {
            if (at + 1 >= outMax) return -1;
            out[at++] = (char)c;
        }
        else if (!isPath && c == ' ')
        {
            if (at + 1 >= outMax) return -1;
            out[at++] = '+';
        }
        else
        {
            if (at + 3 >= outMax) return -1;
            out[at++] = '%';
            out[at++] = kHex[(c >> 4) & 15];
            out[at++] = kHex[c & 15];
        }
    }
    if (at >= outMax) return -1;
    out[at] = 0;
    return 0;
}

int mc_query_escape(const char *s, char *out, long outMax)
{
    return mc_escape(s, out, outMax, 0);
}

int mc_path_escape(const char *s, char *out, long outMax)
{
    return mc_escape(s, out, outMax, 1);
}

static int mc_isalnum(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9');
}

/* One term (word or phrase) of the user's text, already cleaned to
   printable ASCII without '"' or '\', appended to q[*at..] as
   "AND"-joined Lucene. 0 ok, -1 too long. */
static int mc_add_term(char *q, long *at, long max, const char *t, long n,
                       int phrase)
{
    long i, k = 0, need;
    int  quote = phrase;

    while (n > 0 && t[0] == ' ') { t++; n--; }
    while (n > 0 && t[n - 1] == ' ') n--;
    for (i = 0; i < n; i++)
    {
        if (mc_isalnum(t[i])) k++;
        else quote = 1;
    }
    if (k == 0) return 0;                 /* nothing searchable: drop */
    need = n + (quote ? 2 : 0) + (*at > 0 ? 5 : 0);
    if (*at + need + 1 > max) return -1;
    if (*at > 0) { memcpy(q + *at, " AND ", 5); *at += 5; }
    if (quote) q[(*at)++] = '"';
    for (i = 0; i < n; i++)
    {
        int c = t[i];
        if (c == ' ' && i > 0 && t[i - 1] == ' ') continue;
        q[(*at)++] = (char)(quote ? c : mc_lower(c));
    }
    if (quote) q[(*at)++] = '"';
    q[*at] = 0;
    return 0;
}

int mc_search_query(const char *terms, int scope, char *out, long outMax)
{
    char  body[620];
    char  t[204];
    long  at = 0, n;
    const unsigned char *p = (const unsigned char *)terms;
    const unsigned char *end;
    int   inPhrase;

    body[0] = 0;
    end = p + strlen(terms);
    if (end - p > 200) end = p + 200;     /* as 0.2: 200 chars at most */
    while (p < end)
    {
        if (*p == '"')
        {
            p++;
            inPhrase = 1;
        }
        else inPhrase = 0;
        n = 0;
        while (p < end)
        {
            int c = *p;
            if (inPhrase && c == '"') { p++; break; }
            if (!inPhrase && c == '"') break;
            p++;
            if (c < 0x21 || c > 0x7E || c == '\\')
            {
                if (!inPhrase) break;     /* a word ends here */
                c = ' ';
            }
            if (n < 200) t[n++] = (char)c;
        }
        if (mc_add_term(body, &at, (long)sizeof(body), t, n, inPhrase) != 0)
            return -2;
    }
    if (at == 0) return -1;
    if (scope == MC_SCOPE_ANY)
    {
        if (at + 40 > outMax) return -2;
        sprintf(out, "(%s) AND mediatype:movies", body);
    }
    else
    {
        if (at + 46 > outMax) return -2;
        sprintf(out, "title:(%s) AND mediatype:movies", body);
    }
    return 0;
}

int mc_search_url2(const char *terms, int scope, int page, char *out,
                   long outMax)
{
    char q[700];
    char enc[MC_URLMAX];
    int  r;

    r = mc_search_query(terms, scope, q, (long)sizeof(q));
    if (r != 0) return r;
    if (page < 1) page = 1;
    if (page > 9999) page = 9999;
    if (mc_query_escape(q, enc, (long)sizeof(enc)) != 0) return -2;
    if ((long)strlen(enc) + (long)strlen(mc_api_base) + 120 > outMax)
        return -2;
    sprintf(out,
        "%s/advancedsearch.php?q=%s"
        "&fl%%5B%%5D=identifier%%2Ctitle&sort%%5B%%5D=downloads+desc"
        "&rows=%d&page=%d&output=json", mc_api_base, enc, MC_MAXHITS, page);
    return 0;
}

int mc_search_url(const char *terms, int page, char *out, long outMax)
{
    return mc_search_url2(terms, MC_SCOPE_TITLE, page, out, outMax);
}

/* ==================================================================
   b. push JSON scanner
   ================================================================== */

enum
{
    ST_VALUE = 0, ST_ARR_FIRST, ST_OBJ_FIRST, ST_OBJ_KEY, ST_COLON,
    ST_AFTER, ST_STR, ST_ESC, ST_HEX, ST_LIT, ST_DONE, ST_ERR
};

/* U+00C0..U+00FF folded to ASCII */
static const char kLatin[] =
    "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTs"
    "aaaaaaaceeeeiiiidnooooo/ouuuuyty";

static void js_put(McJson *j, int c)
{
    if (j->slen < MC_JSON_STRMAX - 1) j->str[j->slen++] = (char)c;
    else j->truncated = 1;
}

static void js_put_code(McJson *j, unsigned long cp)
{
    int c = '?';
    if (cp < 0x20) c = ' ';
    else if (cp < 0x7F) c = (int)cp;
    else if (cp == 0xA0) c = ' ';
    else if (cp >= 0xC0 && cp <= 0xFF) c = kLatin[cp - 0xC0];
    else if (cp == 0x2018 || cp == 0x2019 || cp == 0xB4) c = '\'';
    else if (cp == 0x201C || cp == 0x201D || cp == 0xAB || cp == 0xBB)
        c = '"';
    else if (cp == 0x2013 || cp == 0x2014 || cp == 0x2010) c = '-';
    else if (cp == 0x2026) c = '.';
    js_put(j, c);
}

void mc_json_init(McJson *j, McJsonEvent cb, void *user)
{
    memset(j, 0, sizeof(*j));
    j->state = ST_VALUE;
    j->cb = cb;
    j->user = user;
}

static void js_emit(McJson *j, int kind, const char *v, long n)
{
    if (j->cb != NULL) j->cb(j->user, j, kind, v, n);
}

static void js_value_done(McJson *j)
{
    j->state = (j->depth == 0) ? ST_DONE : ST_AFTER;
}

static int js_fail(McJson *j)
{
    j->err = 1;
    j->state = ST_ERR;
    return -1;
}

static int js_open(McJson *j, int c)
{
    if (j->depth >= MC_JSON_DEPTH) return js_fail(j);
    j->kind[j->depth] = (char)(c == '{' ? 'o' : 'a');
    j->key[j->depth][0] = 0;
    j->index[j->depth] = 0;
    j->depth++;
    js_emit(j, c == '{' ? MC_J_OBJ_BEGIN : MC_J_ARR_BEGIN, NULL, 0);
    j->state = (c == '{') ? ST_OBJ_FIRST : ST_ARR_FIRST;
    return 0;
}

static int js_close(McJson *j, int c)
{
    char want = (char)(c == '}' ? 'o' : 'a');
    if (j->depth < 1 || j->kind[j->depth - 1] != want) return js_fail(j);
    js_emit(j, c == '}' ? MC_J_OBJ_END : MC_J_ARR_END, NULL, 0);
    j->depth--;
    js_value_done(j);
    return 0;
}

static void js_begin_str(McJson *j, int isKey)
{
    j->isKey = isKey;
    j->slen = 0;
    j->truncated = 0;
    j->utfNeed = 0;
    j->state = ST_STR;
}

static int js_is_litchar(int c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z')
        || (c >= 'A' && c <= 'Z') || c == '+' || c == '-' || c == '.';
}

static int js_end_lit(McJson *j)
{
    j->str[j->slen] = 0;
    if (strcmp(j->str, "true") == 0 || strcmp(j->str, "false") == 0
        || strcmp(j->str, "null") == 0)
    {
        js_emit(j, MC_J_LIT, j->str, j->slen);
    }
    else
    {
        /* -?(0|[1-9][0-9]*)(.[0-9]+)?([eE][+-]?[0-9]+)? */
        const char *q = j->str;
        if (*q == '-') q++;
        if (*q == '0') q++;
        else if (*q >= '1' && *q <= '9')
        { while (*q >= '0' && *q <= '9') q++; }
        else return js_fail(j);
        if (*q == '.')
        {
            q++;
            if (!(*q >= '0' && *q <= '9')) return js_fail(j);
            while (*q >= '0' && *q <= '9') q++;
        }
        if (*q == 'e' || *q == 'E')
        {
            q++;
            if (*q == '+' || *q == '-') q++;
            if (!(*q >= '0' && *q <= '9')) return js_fail(j);
            while (*q >= '0' && *q <= '9') q++;
        }
        if (*q != 0) return js_fail(j);
        js_emit(j, MC_J_NUM, j->str, j->slen);
    }
    js_value_done(j);
    return 0;
}

/* a value may start here (ST_VALUE / ST_ARR_FIRST) */
static int js_value_start(McJson *j, int c)
{
    if (c == '{' || c == '[') return js_open(j, c);
    if (c == '"') { js_begin_str(j, 0); return 0; }
    if (c == '-' || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z'))
    {
        j->slen = 0;
        j->str[j->slen++] = (char)c;
        j->state = ST_LIT;
        return 0;
    }
    return js_fail(j);
}

static int js_end_str(McJson *j)
{
    if (j->utfNeed > 0) { js_put(j, '?'); j->utfNeed = 0; }
    j->str[j->slen] = 0;
    if (j->isKey)
    {
        mc_copy(j->key[j->depth - 1], j->str, MC_JSON_KEYMAX);
        j->state = ST_COLON;
    }
    else
    {
        js_emit(j, MC_J_STR, j->str, j->slen);
        js_value_done(j);
    }
    return 0;
}

static int js_byte(McJson *j, int c)
{
    int ws = (c == ' ' || c == '\t' || c == '\r' || c == '\n');

    switch (j->state)
    {
    case ST_ERR:
        return -1;

    case ST_DONE:
        return ws ? 0 : js_fail(j);

    case ST_VALUE:
        if (ws) return 0;
        return js_value_start(j, c);

    case ST_ARR_FIRST:
        if (ws) return 0;
        if (c == ']') return js_close(j, c);
        return js_value_start(j, c);

    case ST_OBJ_FIRST:
        if (ws) return 0;
        if (c == '}') return js_close(j, c);
        if (c == '"') { js_begin_str(j, 1); return 0; }
        return js_fail(j);

    case ST_OBJ_KEY:
        if (ws) return 0;
        if (c == '"') { js_begin_str(j, 1); return 0; }
        return js_fail(j);

    case ST_COLON:
        if (ws) return 0;
        if (c == ':') { j->state = ST_VALUE; return 0; }
        return js_fail(j);

    case ST_AFTER:
        if (ws) return 0;
        if (c == '}' || c == ']') return js_close(j, c);
        if (c == ',')
        {
            if (j->kind[j->depth - 1] == 'o') j->state = ST_OBJ_KEY;
            else { j->index[j->depth - 1]++; j->state = ST_VALUE; }
            return 0;
        }
        return js_fail(j);

    case ST_STR:
        if (c >= 0x80)
        {
            if ((c & 0xC0) == 0x80)             /* continuation byte */
            {
                if (j->utfNeed > 0)
                {
                    j->utfCode = (j->utfCode << 6) | (unsigned long)(c & 0x3F);
                    if (--j->utfNeed == 0) js_put_code(j, j->utfCode);
                }
                else js_put(j, '?');
                return 0;
            }
            if (j->utfNeed > 0) js_put(j, '?');  /* broken sequence */
            if (c >= 0xC0 && c <= 0xDF)
            { j->utfNeed = 1; j->utfCode = (unsigned long)(c & 0x1F); }
            else if (c >= 0xE0 && c <= 0xEF)
            { j->utfNeed = 2; j->utfCode = (unsigned long)(c & 0x0F); }
            else if (c >= 0xF0 && c <= 0xF7)
            { j->utfNeed = 3; j->utfCode = (unsigned long)(c & 0x07); }
            else { j->utfNeed = 0; js_put(j, '?'); }
            return 0;
        }
        if (j->utfNeed > 0) { js_put(j, '?'); j->utfNeed = 0; }
        if (c == '"') return js_end_str(j);
        if (c == '\\') { j->state = ST_ESC; return 0; }
        if (c < 0x20) return js_fail(j);
        js_put(j, c);
        return 0;

    case ST_ESC:
        j->state = ST_STR;
        switch (c)
        {
        case '"': case '\\': case '/': js_put(j, c); return 0;
        case 'b': case 'f': case 'n': case 'r': case 't':
            js_put(j, ' ');
            return 0;
        case 'u':
            j->hexN = 0;
            j->hexV = 0;
            j->state = ST_HEX;
            return 0;
        }
        return js_fail(j);

    case ST_HEX:
        {
            int d;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else return js_fail(j);
            j->hexV = (j->hexV << 4) | (unsigned long)d;
            if (++j->hexN < 4) return 0;
            j->state = ST_STR;
            if (j->hexV >= 0xDC00 && j->hexV <= 0xDFFF) return 0; /* low half */
            if (j->hexV >= 0xD800 && j->hexV <= 0xDBFF) { js_put(j, '?'); return 0; }
            js_put_code(j, j->hexV);
            return 0;
        }

    case ST_LIT:
        if (js_is_litchar(c))
        {
            if (j->slen >= 40) return js_fail(j);
            j->str[j->slen++] = (char)c;
            return 0;
        }
        if (js_end_lit(j) != 0) return -1;
        return js_byte(j, c);       /* reprocess the terminator (once) */
    }
    return js_fail(j);
}

int mc_json_feed(McJson *j, const char *buf, long n)
{
    long i;
    if (j->err) return -1;
    for (i = 0; i < n; i++)
    {
        if (js_byte(j, (unsigned char)buf[i]) != 0) return -1;
        j->bytes++;
    }
    return 0;
}

int mc_json_finish(McJson *j)
{
    if (j->err) return -1;
    if (j->state == ST_LIT && j->depth == 0)
    {
        if (js_end_lit(j) != 0) return -1;
    }
    return (j->state == ST_DONE) ? 0 : -1;
}

/* path test: are we at depth d with these keys? keys[i]==NULL means
   "any" (array level). */
static int js_at(const McJson *j, int d, const char *k0, const char *k1)
{
    if (j->depth != d) return 0;
    if (d >= 1 && k0 != NULL &&
        (j->kind[0] != 'o' || strcmp(j->key[0], k0) != 0)) return 0;
    if (d >= 2 && k1 != NULL &&
        (j->kind[1] != 'o' || strcmp(j->key[1], k1) != 0)) return 0;
    return 1;
}

/* ==================================================================
   search results: { "response": { "numFound": N,
                       "docs": [ { "identifier": "...",
                                   "title": "..." or ["...",..] } ] } }
   ================================================================== */

static void search_cb(void *user, const McJson *j, int kind,
                      const char *val, long len)
{
    McSearch *s = (McSearch *)user;
    (void)len;

    if (kind == MC_J_OBJ_BEGIN && js_at(j, 4, "response", "docs")
        && j->kind[2] == 'a')
    {
        memset(&s->tmp, 0, sizeof(s->tmp));
        s->titleSeen = 0;
        s->cur = 1;
        return;
    }
    if (kind == MC_J_OBJ_END && s->cur && j->depth == 4)
    {
        s->cur = 0;
        if (s->tmp.identifier[0] != 0 && s->count < MC_MAXHITS)
        {
            if (s->tmp.title[0] == 0)
                mc_copy(s->tmp.title, s->tmp.identifier, MC_TITLEMAX);
            s->hits[s->count++] = s->tmp;
        }
        return;
    }
    if (kind == MC_J_NUM && js_at(j, 2, "response", "numFound"))
    {
        s->numFound = (long)mc_atoul(val);
        return;
    }
    if (kind != MC_J_STR || !s->cur) return;

    if (j->depth == 4 && j->kind[3] == 'o')
    {
        if (strcmp(j->key[3], "identifier") == 0)
        {
            /* a cut identifier is useless: leave it empty -> skipped */
            if (!j->truncated && len < MC_IDMAX)
                mc_copy(s->tmp.identifier, val, MC_IDMAX);
        }
        else if (strcmp(j->key[3], "title") == 0 && !s->titleSeen)
        {
            mc_copy(s->tmp.title, val, MC_TITLEMAX);
            s->titleSeen = 1;
        }
    }
    else if (j->depth == 5 && j->kind[3] == 'o' && j->kind[4] == 'a'
             && strcmp(j->key[3], "title") == 0 && !s->titleSeen)
    {
        mc_copy(s->tmp.title, val, MC_TITLEMAX);
        s->titleSeen = 1;
    }
}

void mc_search_begin(McSearch *s)
{
    memset(s, 0, sizeof(*s));
    s->numFound = -1;
    mc_json_init(&s->j, search_cb, s);
}

int mc_search_feed(McSearch *s, const char *buf, long n)
{
    return mc_json_feed(&s->j, buf, n);
}

int mc_search_end(McSearch *s)
{
    if (mc_json_finish(&s->j) != 0) return -1;
    if (s->numFound < 0) return -1;    /* not a search response */
    return s->count;
}

/* ==================================================================
   c. item metadata
   ================================================================== */

static int mc_format_rank(const char *fmt)
{
    if (strcmp(fmt, "512Kb MPEG4") == 0) return MC_RANK_512KB;
    if (strcmp(fmt, "MPEG4") == 0) return MC_RANK_MPEG4;
    if (mc_streqi(fmt, "h.264 IA")) return MC_RANK_H264IA;
    if (mc_startsi(fmt, "h.264")) return MC_RANK_H264;
    return 0;
}

static void meta_add_server(McMeta *m, const char *s, long len, int cut)
{
    int i;
    if (cut || len == 0 || len >= MC_SRVMAX) return;
    for (i = 0; i < m->nsrv; i++)
        if (mc_streqi(m->server[i], s)) return;
    if (m->nsrv >= MC_MAXSRV) return;
    mc_copy(m->server[m->nsrv++], s, MC_SRVMAX);
}

/* (0.6.1) same-film key: hash of the name without its extension and
   without IA's derivative suffixes ".ia" / "_512kb" (case-blind), so
   "x.avi", "x.ia.mp4" and "x_512kb.mp4" share one key */
static unsigned long mc_stem_key(const char *name)
{
    const char *s = name, *dot = NULL, *p;
    long n, i;
    unsigned long h = 2166136261UL;

    for (p = name; *p; p++)
    {
        if (*p == '/') { s = p + 1; dot = NULL; }
        else if (*p == '.') dot = p;
    }
    n = dot != NULL && dot != s ? (long)(dot - name) : (long)strlen(name);
    if (n >= 3 && name[n - 3] == '.' && (name[n - 2] | 32) == 'i'
        && (name[n - 1] | 32) == 'a')
        n -= 3;
    else if (n >= 6 && name[n - 6] == '_' && name[n - 5] == '5'
             && name[n - 4] == '1' && name[n - 3] == '2'
             && (name[n - 2] | 32) == 'k' && (name[n - 1] | 32) == 'b')
        n -= 6;
    for (i = 0; i < n; i++)
    {
        unsigned char c = (unsigned char)name[i];
        if (c >= 'A' && c <= 'Z') c = (unsigned char)(c + 32);
        h = ((h ^ c) * 16777619UL) & 0xFFFFFFFFUL;
    }
    return h ? h : 1UL;
}

/* (0.3) every .mp4/.m4v/.mov goes into the file list, natural order */
static void meta_add_file(McMeta *m)
{
    McCand *c = &m->tmp;
    McFile *f;
    int     i, at;

    if (!(mc_endsi(c->name, ".mp4") || mc_endsi(c->name, ".m4v")
          || mc_endsi(c->name, ".mov")))
        return;
    if (m->tmpBad || c->name[0] == 0) { m->droppedFiles++; return; }
    at = m->nfiles;
    while (at > 0 && mc_natcmp(m->files[at - 1].name, c->name) > 0) at--;
    if (at >= MC_MAXFILES) { m->droppedFiles++; return; }
    if (m->nfiles == MC_MAXFILES) { m->nfiles--; m->droppedFiles++; }
    for (i = m->nfiles; i > at; i--) m->files[i] = m->files[i - 1];
    f = &m->files[at];
    memset(f, 0, sizeof(*f));
    mc_copy(f->name, c->name, MC_NAMEMAX);
    mc_copy(f->title, m->tmpTitle, MC_TITLEMAX);
    mc_copy(f->format, c->format, MC_FMTMAX);
    f->size = c->size;
    f->width = c->width;
    f->height = c->height;
    f->hint = mc_format_hint(c->format);
    f->state = MC_STATE_HINT;
    f->skey = mc_stem_key(c->name);                       /* (0.6.1) */
    f->key = m->tmpOrig ? m->tmpOrig : f->skey;
    m->nfiles++;
}

static void meta_commit(McMeta *m)
{
    McCand *c = &m->tmp;
    int i, at;

    meta_add_file(m);

    c->rank = mc_format_rank(c->format);
    if (c->rank == 0) return;
    if (!(mc_endsi(c->name, ".mp4") || mc_endsi(c->name, ".m4v")
          || mc_endsi(c->name, ".mov")))
        return;
    if (m->tmpBad || c->name[0] == 0) { m->droppedCand++; return; }

    /* stable insert: after every entry of the same or better rank */
    at = m->ncand;
    while (at > 0 && m->cand[at - 1].rank > c->rank) at--;
    if (at >= MC_MAXCAND) { m->droppedCand++; return; }
    if (m->ncand == MC_MAXCAND) { m->ncand--; m->droppedCand++; }
    for (i = m->ncand; i > at; i--) m->cand[i] = m->cand[i - 1];
    m->cand[at] = *c;
    m->ncand++;
}

static void meta_cb(void *user, const McJson *j, int kind,
                    const char *val, long len)
{
    McMeta *m = (McMeta *)user;
    int scalar = (kind == MC_J_STR || kind == MC_J_NUM || kind == MC_J_LIT);

    if ((scalar && j->depth == 1) ||
        ((kind == MC_J_OBJ_BEGIN || kind == MC_J_ARR_BEGIN) && j->depth == 2))
        m->topKeys++;

    /* files[i] objects */
    if (kind == MC_J_OBJ_BEGIN && js_at(j, 3, "files", NULL)
        && j->kind[1] == 'a')
    {
        memset(&m->tmp, 0, sizeof(m->tmp));
        m->tmpTitle[0] = 0;
        m->tmpBad = 0;
        m->tmpOrig = 0;
        return;
    }
    if (kind == MC_J_OBJ_END && js_at(j, 3, "files", NULL)
        && j->kind[1] == 'a')
    {
        meta_commit(m);
        return;
    }
    if (scalar && js_at(j, 3, "files", NULL) && j->kind[1] == 'a'
        && j->kind[2] == 'o')
    {
        const char *k = j->key[2];
        if (kind == MC_J_STR && strcmp(k, "name") == 0)
        {
            if (j->truncated || len >= MC_NAMEMAX) m->tmpBad = 1;
            else mc_copy(m->tmp.name, val, MC_NAMEMAX);
        }
        else if (kind == MC_J_STR && strcmp(k, "format") == 0)
            mc_copy(m->tmp.format, val, MC_FMTMAX);
        else if (kind == MC_J_STR && strcmp(k, "title") == 0)
            mc_copy(m->tmpTitle, val, MC_TITLEMAX);
        else if (kind == MC_J_STR && strcmp(k, "original") == 0
                 && !j->truncated)
            m->tmpOrig = mc_stem_key(val);            /* (0.6.1) */
        else if (strcmp(k, "size") == 0)
            m->tmp.size = mc_atoul(val);
        else if (strcmp(k, "width") == 0)
            m->tmp.width = (int)(mc_atoul(val) & 0x7FFF);
        else if (strcmp(k, "height") == 0)
            m->tmp.height = (int)(mc_atoul(val) & 0x7FFF);
        return;
    }

    if (kind == MC_J_STR && j->depth == 1 && j->kind[0] == 'o')
    {
        const char *k = j->key[0];
        if (strcmp(k, "server") == 0 || strcmp(k, "d1") == 0
            || strcmp(k, "d2") == 0)
            meta_add_server(m, val, len, j->truncated);
        else if (strcmp(k, "dir") == 0 && !j->truncated && len < MC_DIRMAX)
            mc_copy(m->dir, val, MC_DIRMAX);
        return;
    }
    if (kind == MC_J_LIT && js_at(j, 1, "is_dark", NULL)
        && strcmp(val, "true") == 0)
    {
        m->isDark = 1;
        return;
    }
    if (kind == MC_J_STR && js_at(j, 2, "workable_servers", NULL)
        && j->kind[1] == 'a')
    {
        meta_add_server(m, val, len, j->truncated);
        return;
    }
    if (kind == MC_J_STR && !m->titleSeen &&
        (js_at(j, 2, "metadata", "title") ||
         (js_at(j, 3, "metadata", "title") && j->kind[2] == 'a')))
    {
        mc_copy(m->title, val, MC_TITLEMAX);
        m->titleSeen = 1;
    }
}

void mc_meta_begin(McMeta *m)
{
    memset(m, 0, sizeof(*m));
    mc_json_init(&m->j, meta_cb, m);
}

int mc_meta_feed(McMeta *m, const char *buf, long n)
{
    return mc_json_feed(&m->j, buf, n);
}

int mc_meta_end(McMeta *m)
{
    if (mc_json_finish(&m->j) != 0) return -1;
    if (m->j.kind[0] != 'o') return -1;          /* top level not {} */
    if (m->topKeys == 0) return -2;
    if (m->isDark) return -3;
    return 0;
}

/* ==================================================================
   (0.3) the item's file list
   ================================================================== */

int mc_natcmp(const char *a, const char *b)
{
    const unsigned char *p = (const unsigned char *)a;
    const unsigned char *q = (const unsigned char *)b;

    while (*p && *q)
    {
        if (*p >= '0' && *p <= '9' && *q >= '0' && *q <= '9')
        {
            const unsigned char *p0, *q0;
            long lp, lq;
            while (*p == '0' && p[1] >= '0' && p[1] <= '9') p++;
            while (*q == '0' && q[1] >= '0' && q[1] <= '9') q++;
            p0 = p; q0 = q;
            while (*p >= '0' && *p <= '9') p++;
            while (*q >= '0' && *q <= '9') q++;
            lp = (long)(p - p0);
            lq = (long)(q - q0);
            if (lp != lq) return lp < lq ? -1 : 1;
            while (p0 < p)
            {
                if (*p0 != *q0) return *p0 < *q0 ? -1 : 1;
                p0++; q0++;
            }
        }
        else
        {
            int x = mc_lower(*p), y = mc_lower(*q);
            if (x != y) return x < y ? -1 : 1;
            p++; q++;
        }
    }
    if (*p) return 1;
    if (*q) return -1;
    return strcmp(a, b) < 0 ? -1 : (strcmp(a, b) > 0 ? 1 : 0);
}

int mc_format_hint(const char *format)
{
    /* a survey of 16 files (Nov 2026): IA's "h.264" and "512Kb MPEG4"
       derivatives were always Constrained Baseline */
    if (mc_streqi(format, "h.264") || mc_streqi(format, "512Kb MPEG4"))
        return MC_HINT_PLAYS;
    /* (0.4) IA's ".ia.mp4" derivative ("h.264 IA") is 8-bit 4:2:0
       Main/High: a guess until the moov is checked */
    if (mc_decoder_level() >= MC_DECODER_MAINHIGH
        && mc_streqi(format, "h.264 IA"))
        return MC_HINT_PLAYS;
    return MC_HINT_NEWER;
}

void mc_file_label(const McFile *f, char *out, long outMax)
{
    const char *s, *dot;
    long n;

    if (outMax < 1) return;
    if (f->title[0] != 0) { mc_copy(out, f->title, outMax); return; }
    s = strrchr(f->name, '/');
    s = (s != NULL) ? s + 1 : f->name;
    dot = strrchr(s, '.');
    n = (dot != NULL && dot != s) ? (long)(dot - s) : (long)strlen(s);
    if (n > outMax - 1) n = outMax - 1;
    memcpy(out, s, (size_t)n);
    out[n] = 0;
}

const char *mc_file_mark(const McFile *f)
{
    int hi = (mc_decoder_level() >= MC_DECODER_MAINHIGH);
    if (f->state == MC_STATE_PLAYS) return "PLAYS NOW";
    if (f->state == MC_STATE_NEWER)
        return hi ? "will not play" : "needs newer decoder";
    if (f->hint == MC_HINT_PLAYS) return "plays now?";
    return hi ? "may not play?" : "needs newer decoder?";
}

const char *mc_file_token(const McFile *f)
{
    int hi = (mc_decoder_level() >= MC_DECODER_MAINHIGH);
    if (f->state == MC_STATE_PLAYS) return "fact:plays-now";
    if (f->state == MC_STATE_NEWER)
        return hi ? "fact:will-not-play" : "fact:needs-newer-decoder";
    if (f->hint == MC_HINT_PLAYS) return "hint:plays-now";
    return hi ? "hint:may-not-play" : "hint:needs-newer-decoder";
}

void mc_file_mb(const McFile *f, char *out)
{
    unsigned long kb = f->size / 1024UL;
    unsigned long mb = kb / 1024UL;
    unsigned long tenth = ((kb % 1024UL) * 10UL) / 1024UL;
    sprintf(out, "%lu.%lu", mb, tenth);
}

void mc_file_row(const McFile *f, char *out, long outMax)
{
    char label[MC_TITLEMAX];
    char mb[16];
    char row[MC_TITLEMAX + MC_FMTMAX + 64];

    mc_file_label(f, label, 60);          /* the list is ~470 px wide */
    mc_file_mb(f, mb);
    sprintf(row, "%s   [%s, %s MB]  %s", label, f->format, mb,
            mc_file_mark(f));
    mc_copy(out, row, outMax);
}

void mc_file_cand(const McFile *f, McCand *c)
{
    memset(c, 0, sizeof(*c));
    mc_copy(c->name, f->name, MC_NAMEMAX);
    mc_copy(c->format, f->format, MC_FMTMAX);
    c->size = f->size;
    c->width = f->width;
    c->height = f->height;
    c->rank = mc_format_rank(f->format);
}

/* ==================================================================
   (0.3) the test hook file
   ================================================================== */

/* the word w (lower case) at the start of s, followed by a non-letter */
static int mc_word_at(const char *s, const char *w)
{
    long n = (long)strlen(w);
    long i;
    for (i = 0; i < n; i++)
        if (mc_lower((unsigned char)s[i]) != w[i]) return 0;
    return !mc_isalnum((unsigned char)s[n]);
}

int mc_hook_parse(const char *text, McHook *h)
{
    const char *p = text, *e;
    long n;
    int  line = 0;

    memset(h, 0, sizeof(*h));
    while (*p)
    {
        e = p;
        while (*e && *e != '\r' && *e != '\n') e++;
        if (line == 0)
        {
            n = (long)(e - p);
            while (n > 0 && (*p == ' ' || *p == '\t')) { p++; n--; }
            while (n > 0 && (p[n - 1] == ' ' || p[n - 1] == '\t')) n--;
            if (n > (long)sizeof(h->query) - 1)
                n = (long)sizeof(h->query) - 1;
            memcpy(h->query, p, (size_t)n);
            h->query[n] = 0;
        }
        else
        {
            const char *s;
            for (s = p; s < e; s++)
            {
                if (s > p && mc_isalnum((unsigned char)s[-1])) continue;
                if (mc_word_at(s, "download")) h->download = 1;
                else if (mc_word_at(s, "insecure")) h->insecure = 1;
                else if (mc_word_at(s, "confirm")) h->confirm = 1;
                else if (mc_word_at(s, "list")) h->list = 1;
                else if (mc_word_at(s, "serve") || mc_word_at(s, "stop"))
                {   /* (0.5s) "serve N" / "stop N" (seconds) */
                    int isStop = mc_word_at(s, "stop");
                    const char *d = s + (isStop ? 4 : 5);
                    long v = 0;
                    while (d < e && (*d == ' ' || *d == '\t')) d++;
                    while (d < e && *d >= '0' && *d <= '9' && v < 100000L)
                        v = v * 10 + (*d++ - '0');
                    if (v > 0 && v < 100000L)
                    {
                        if (isStop) h->stop = v; else h->serve = v;
                    }
                }
                else if (mc_word_at(s, "file"))
                {
                    const char *d = s + 4;
                    long v = 0;
                    while (d < e && (*d == ' ' || *d == '\t')) d++;
                    while (d < e && *d >= '0' && *d <= '9' && v < 100000L)
                        v = v * 10 + (*d++ - '0');
                    if (v > 0 && v < 100000L) h->file = (int)v;
                }
            }
        }
        line++;
        p = e;
        if (*p == '\r') p++;
        if (*p == '\n') p++;
    }
    return h->query[0] != 0;
}

/* ==================================================================
   d. URL attempts
   ================================================================== */

int mc_attempt_urls(const char *id, const McMeta *m, const McCand *c,
                    char urls[][MC_URLMAX], int maxUrls)
{
    char escName[MC_URLMAX];
    char escId[MC_URLMAX / 4];
    int  n = 0, pass, i;

    if (maxUrls < 1) return -1;
    if (mc_path_escape(c->name, escName, (long)sizeof(escName)) != 0)
        return -1;
    if (mc_path_escape(id, escId, (long)sizeof(escId)) != 0) return -1;
    if (strlen(escName) + strlen(escId) + 32 > MC_URLMAX) return -1;
    if (strlen(mc_dl_base) + strlen(escName) + strlen(escId) + 32 > MC_URLMAX)
        return -1;
    sprintf(urls[n++], "%s/download/%s/%s", mc_dl_base, escId, escName);

    if (m->dir[0] != '/') return n;
    for (pass = 0; pass < 2; pass++)
    {
        for (i = 0; i < m->nsrv && n < maxUrls; i++)
        {
            int us = strstr(m->server[i], ".us.") != NULL;
            if (us != (pass == 0)) continue;
            if (strlen(m->server[i]) + strlen(m->dir) + strlen(escName) + 10
                > MC_URLMAX) continue;
            sprintf(urls[n++], "http://%s%s/%s", m->server[i], m->dir,
                    escName);
        }
    }
    return n;
}

/* ==================================================================
   e. what is in the moov
   ================================================================== */

/* Box at off within [off,end): header length and total size.
   0 ok, -1 malformed. 64-bit sizes accepted only if the high half
   is 0 (as in H264Stream.c). */
static int mc_box(const unsigned char *b, long off, long end,
                  long *hdr, long *size)
{
    unsigned long s;
    if (off < 0 || end - off < 8) return -1;
    s = mc_rd32(b + off);
    *hdr = 8;
    if (s == 1)
    {
        if (end - off < 16) return -1;
        if (mc_rd32(b + off + 8) != 0) return -1;
        s = mc_rd32(b + off + 12);
        *hdr = 16;
    }
    else if (s == 0)
    {
        s = (unsigned long)(end - off);
    }
    if (s < (unsigned long)*hdr || s > (unsigned long)(end - off)) return -1;
    *size = (long)s;
    return 0;
}

static int mc_is(const unsigned char *t, const char *four)
{
    return t[0] == (unsigned char)four[0] && t[1] == (unsigned char)four[1]
        && t[2] == (unsigned char)four[2] && t[3] == (unsigned char)four[3];
}

static int mc_visual(const unsigned char *t)
{
    static const char *const kVis[] =
    {
        "avc1", "avc3", "mp4v", "hvc1", "hev1", "vp08", "vp09", "av01",
        "s263", "h263", "jpeg", "mjpa", "SVQ1", "SVQ3", "cvid", "encv",
        NULL
    };
    int i;
    for (i = 0; kVis[i] != NULL; i++)
        if (mc_is(t, kVis[i])) return 1;
    return 0;
}

/* ------------------------------------------------------------------
   (0.4) A small SPS reader: just far enough to learn chroma format,
   bit depths and frame_mbs_only. Exp-Golomb bits, emulation-prevention
   bytes (00 00 03) skipped on the fly, every read bounded: a short or
   garbled SPS sets err and the caller keeps the defaults (sps = 0).
   32-bit arithmetic only.
   ------------------------------------------------------------------ */
typedef struct
{
    const unsigned char *b;
    long len, pos;
    int  bit;             /* next bit in b[pos], 0 = msb */
    int  zeros;           /* zero bytes just consumed (for 00 00 03) */
    int  err;
} McBits;

static int mc_bit(McBits *r)
{
    int v;
    if (r->err) return 0;
    if (r->bit == 0)
    {
        if (r->pos < r->len && r->zeros >= 2 && r->b[r->pos] == 3)
        {
            r->pos++;               /* emulation prevention byte */
            r->zeros = 0;
        }
        if (r->pos >= r->len) { r->err = 1; return 0; }
    }
    v = (r->b[r->pos] >> (7 - r->bit)) & 1;
    if (++r->bit == 8)
    {
        r->zeros = (r->b[r->pos] == 0) ? r->zeros + 1 : 0;
        r->bit = 0;
        r->pos++;
    }
    return v;
}

static unsigned long mc_bits(McBits *r, int n)
{
    unsigned long v = 0;
    while (n-- > 0) v = (v << 1) | (unsigned long)mc_bit(r);
    return v;
}

static unsigned long mc_ue(McBits *r)
{
    int lz = 0;
    while (!r->err && mc_bit(r) == 0)
        if (++lz > 31) { r->err = 1; return 0; }
    if (r->err) return 0;
    if (lz == 0) return 0;
    return ((1UL << lz) - 1UL) + mc_bits(r, lz);
}

static long mc_se(McBits *r)
{
    unsigned long k = mc_ue(r);
    if (k > 0x7FFFFFFEUL) { r->err = 1; return 0; }
    return (k & 1UL) ? (long)((k + 1UL) / 2UL) : -(long)(k / 2UL);
}

static void mc_scaling_list(McBits *r, int size)
{
    long last = 8, next = 8, delta;
    int  j;
    for (j = 0; j < size && !r->err; j++)
    {
        if (next != 0)
        {
            delta = mc_se(r);
            if (delta < -128 || delta > 127) { r->err = 1; return; }
            next = (last + delta + 256) % 256;
        }
        if (next != 0) last = next;
    }
}

/* a = avcC payload (version byte first), n bytes */
static void mc_avcc_sps(const unsigned char *a, long n, McAvcInfo *info)
{
    McBits r;
    unsigned long prof, v, i, cnt;
    int chroma = 1, dl = 8, dc = 8;
    long sl;

    if (n < 8 || (a[5] & 0x1F) == 0) return;
    sl = (long)mc_rd16(a + 6);
    if (sl < 4 || 8 + sl > n || (a[8] & 0x1F) != 7) return;
    r.b = a + 9; r.len = sl - 1; r.pos = 0; r.bit = 0; r.zeros = 0;
    r.err = 0;
    prof = mc_bits(&r, 8);
    (void)mc_bits(&r, 16);                  /* constraint flags, level */
    if (mc_ue(&r) > 31) return;             /* seq_parameter_set_id */
    if (prof == 100 || prof == 110 || prof == 122 || prof == 244
        || prof == 44 || prof == 83 || prof == 86 || prof == 118
        || prof == 128 || prof == 138 || prof == 139 || prof == 134
        || prof == 135)
    {
        v = mc_ue(&r);
        if (v > 3) return;
        chroma = (int)v;
        if (chroma == 3) (void)mc_bit(&r);  /* separate_colour_plane */
        v = mc_ue(&r);
        if (v > 6) return;
        dl = 8 + (int)v;
        v = mc_ue(&r);
        if (v > 6) return;
        dc = 8 + (int)v;
        (void)mc_bit(&r);                   /* qpprime_y_zero_bypass */
        if (mc_bit(&r))                     /* seq_scaling_matrix */
        {
            cnt = (chroma != 3) ? 8UL : 12UL;
            for (i = 0; i < cnt && !r.err; i++)
                if (mc_bit(&r)) mc_scaling_list(&r, i < 6 ? 16 : 64);
        }
    }
    if (mc_ue(&r) > 12) return;             /* log2_max_frame_num - 4 */
    v = mc_ue(&r);                          /* pic_order_cnt_type */
    if (v == 0)
    {
        if (mc_ue(&r) > 12) return;
    }
    else if (v == 1)
    {
        (void)mc_bit(&r);
        (void)mc_se(&r);
        (void)mc_se(&r);
        cnt = mc_ue(&r);
        if (cnt > 255) return;
        for (i = 0; i < cnt && !r.err; i++) (void)mc_se(&r);
    }
    else if (v != 2) return;
    (void)mc_ue(&r);                        /* max_num_ref_frames */
    (void)mc_bit(&r);                       /* gaps_in_frame_num */
    (void)mc_ue(&r);                        /* pic_width_in_mbs - 1 */
    (void)mc_ue(&r);                        /* pic_height_in_map_units - 1 */
    v = (unsigned long)mc_bit(&r);          /* frame_mbs_only_flag */
    if (r.err) return;
    info->sps = 1;
    info->chroma = chroma;
    info->depth_luma = dl;
    info->depth_chroma = dc;
    info->frame_mbs_only = (int)v;
}

/* 1 found, 0 not here, -1 malformed */
static int mc_stsd(const unsigned char *b, long off, long end,
                   McAvcInfo *info)
{
    unsigned long count, i;
    long e, hdr, size;

    if (end - off < 8) return -1;
    count = mc_rd32(b + off + 4);
    e = off + 8;
    for (i = 0; i < count && e < end; i++)
    {
        if (mc_box(b, e, end, &hdr, &size) != 0) return -1;
        if (mc_visual(b + e + 4))
        {
            long v = e + hdr, c, ch, cs;
            memcpy(info->codec, b + e + 4, 4);
            info->codec[4] = 0;
            if (size - hdr < 78) return -1;
            info->width  = (int)mc_rd16(b + v + 24);
            info->height = (int)mc_rd16(b + v + 26);
            for (c = v + 78; c < e + size; c += cs)
            {
                if (mc_box(b, c, e + size, &ch, &cs) != 0) return -1;
                if (mc_is(b + c + 4, "avcC") && cs - ch >= 4)
                {
                    info->profile = b[c + ch + 1];
                    info->compat  = b[c + ch + 2];
                    info->level   = b[c + ch + 3];
                    mc_avcc_sps(b + c + ch, cs - ch, info);   /* (0.4) */
                    break;
                }
            }
            return 1;
        }
        e += size;
    }
    return 0;
}

static int mc_walk(const unsigned char *b, long off, long end,
                   McAvcInfo *info, int depth)
{
    long hdr, size;
    int r;

    if (depth > 8) return -1;
    while (off < end)
    {
        if (mc_box(b, off, end, &hdr, &size) != 0) return -1;
        if (mc_is(b + off + 4, "trak") || mc_is(b + off + 4, "mdia")
            || mc_is(b + off + 4, "minf") || mc_is(b + off + 4, "stbl"))
        {
            r = mc_walk(b, off + hdr, off + size, info, depth + 1);
            if (r != 0) return r;
        }
        else if (mc_is(b + off + 4, "stsd"))
        {
            r = mc_stsd(b, off + hdr, off + size, info);
            if (r != 0) return r;
        }
        off += size;
    }
    return 0;
}

/* (0.6.1) one trak's timing: hdlr type, mdhd timescale/duration, stts
   sample count, ctts present */
typedef struct
{
    int vide;
    unsigned long scale, dur, samples;
    int ctts;
} McTrakT;

static void mc_trak_t(const unsigned char *b, long off, long end,
                      McTrakT *t, int depth)
{
    long hdr, size, p, n;
    unsigned long cnt, i;

    if (depth > 8) return;
    while (off < end)
    {
        if (mc_box(b, off, end, &hdr, &size) != 0) return;
        p = off + hdr;
        n = size - hdr;
        if (mc_is(b + off + 4, "mdia") || mc_is(b + off + 4, "minf")
            || mc_is(b + off + 4, "stbl"))
            mc_trak_t(b, p, off + size, t, depth + 1);
        else if (mc_is(b + off + 4, "hdlr") && n >= 12)
            { if (mc_is(b + p + 8, "vide")) t->vide = 1; }
        else if (mc_is(b + off + 4, "mdhd") && n >= 24)
        {
            if (b[p] == 1)
            {
                if (n >= 36 && mc_rd32(b + p + 24) == 0)
                {
                    t->scale = mc_rd32(b + p + 20);
                    t->dur = mc_rd32(b + p + 28);
                }
            }
            else
            {
                t->scale = mc_rd32(b + p + 12);
                t->dur = mc_rd32(b + p + 16);
            }
        }
        else if (mc_is(b + off + 4, "stts") && n >= 8)
        {
            cnt = mc_rd32(b + p + 4);
            t->samples = 0;
            for (i = 0; i < cnt && 8L + (long)i * 8L + 8L <= n; i++)
                t->samples += mc_rd32(b + p + 8 + (long)i * 8L);
        }
        else if (mc_is(b + off + 4, "ctts"))
            t->ctts = 1;
        off += size;
    }
}

static void mc_timing(const unsigned char *b, long off, long end,
                      McAvcInfo *info)
{
    long hdr, size;
    McTrakT t;

    while (off < end)
    {
        if (mc_box(b, off, end, &hdr, &size) != 0) return;
        if (mc_is(b + off + 4, "trak"))
        {
            memset(&t, 0, sizeof t);
            mc_trak_t(b, off + hdr, off + size, &t, 0);
            if (t.vide)
            {
                if (t.scale != 0 && t.dur != 0)
                {
                    double sec = (double)t.dur / (double)t.scale;
                    info->dur_ms = (unsigned long)(sec * 1000.0);
                    if (t.samples > 1 && sec > 0.0)
                    {
                        double f = (double)t.samples * 100.0 / sec;
                        if (f >= 100.0 && f <= 12000.0)   /* 1..120 fps */
                            info->fps100 = (long)(f + 0.5);
                    }
                }
                info->bframes = t.ctts;
                return;
            }
        }
        off += size;
    }
}

int mc_avc_profile(const unsigned char *moov, long len, McAvcInfo *info)
{
    long hdr, size;
    int r;

    memset(info, 0, sizeof(*info));
    info->profile = -1;
    info->chroma = 1;                 /* (0.4) defaults until an SPS says */
    info->depth_luma = info->depth_chroma = 8;
    info->frame_mbs_only = 1;
    if (moov == NULL || mc_box(moov, 0, len, &hdr, &size) != 0) return -2;
    if (!mc_is(moov + 4, "moov")) return -2;
    r = mc_walk(moov, hdr, size, info, 0);
    if (r < 0) return -2;
    if (r == 1) mc_timing(moov, hdr, size, info);         /* (0.6.1) */
    return r == 1 ? 0 : -1;
}

static const char *mc_profile_name(int p)
{
    switch (p)
    {
    case 66:  return "Baseline";
    case 77:  return "Main";
    case 88:  return "Extended";
    case 100: return "High";
    case 110: return "High 10";
    case 122: return "High 4:2:2";
    case 244: return "High 4:4:4";
    }
    return "an unknown";
}

/* ------------------------------------------------------------------
   (0.4) the decoder level, set once by the app at start-up
   ------------------------------------------------------------------ */
static int mc_level = MC_DECODER_BASELINE;

void mc_set_decoder_level(int level)
{
    mc_level = (level >= MC_DECODER_MAINHIGH) ? MC_DECODER_MAINHIGH
                                              : MC_DECODER_BASELINE;
}

int mc_decoder_level(void)
{
    return mc_level;
}

static const char *mc_chroma_name(int c)
{
    switch (c)
    {
    case 0: return "monochrome (4:0:0)";
    case 2: return "4:2:2 colour";
    case 3: return "4:4:4 colour";
    }
    return "4:2:0 colour";
}

/* (0.6) the last verdict's "may stutter" (Large for this Mac);
   (0.6.1) MC_FILE_NOTE_*: also the G3 budget's STUTTER / HEAVY */
static int gLastStutter = 0;

int mc_avc_last_stutter(void)
{
    return gLastStutter != MC_FILE_NOTE_NONE;
}

int mc_avc_last_note(void)
{
    return gLastStutter;
}

/* ------------------------------------------------------------------
   (0.6.1) G3 policy (constants: mc_core.h)
   ------------------------------------------------------------------ */
static int  mc_g3 = 0;
static long mc_mhz = 0;

void mc_set_cpu(int g3, long mhz)
{
    mc_g3 = g3 != 0;
    mc_mhz = (mhz >= 100L && mhz <= 5000L) ? mhz : 0L;
}

int mc_cpu_g3(void)
{
    return mc_g3;
}

long mc_cpu_mhz(void)
{
    if (mc_mhz == 0 && mc_g3) return MC_G3_MHZ_UNKNOWN;
    return mc_mhz;
}

unsigned long mc_mbs100(int w, int h, long fps100)
{
    unsigned long mb;
    if (w <= 0 || h <= 0) return 0;
    if (fps100 <= 0) fps100 = MC_G3_FPS_UNKNOWN * 100L;
    mb = ((unsigned long)w + 15UL) / 16UL * (((unsigned long)h + 15UL) / 16UL);
    return mb * (unsigned long)fps100;
}

int mc_g3_verdict(long mhz, int w, int h, long fps100, int pclass,
                  int bframes, long *pct)
{
    double need, budget, p;

    if (mhz <= 0) mhz = MC_G3_MHZ_UNKNOWN;
    need = (double)mc_mbs100(w, h, fps100) / 100.0;
    if (bframes) need = need * (double)MC_G3_BFRAME_PCT / 100.0;
    budget = (double)mhz * (double)(pclass == MC_PCLASS_BASE
                                    ? MC_G3_MBS_PER_MHZ_BASE
                                    : MC_G3_MBS_PER_MHZ_MAIN);
    p = need * 100.0 / budget;
    if (pct != NULL) *pct = (long)(p + 0.5);
    if (p <= (double)MC_G3_PLAYS_PCT) return MC_G3_PLAYS;
    if (p <= (double)MC_G3_STUTTER_PCT) return MC_G3_STUTTER;
    return MC_G3_HEAVY;
}

static int mc_is_512kb(const McFile *f)
{
    return strcmp(f->format, "512Kb MPEG4") == 0;
}

int mc_g3_lighter(const McMeta *m, int fi, long mhz)
{
    const McFile *f, *g;
    int  i, best = -1, mp4 = -1;
    unsigned long fmb, gmb, bestmb = 0;
    int  fw, fh;

    if (m == NULL || fi < 0 || fi >= m->nfiles) return -1;
    f = &m->files[fi];
    if (mc_is_512kb(f)) return -1;                /* already the light one */
    fw = f->width;
    fh = f->height;
    fmb = (fw > 0 && fh > 0) ? mc_mbs100(fw, fh, 100L) : 0xFFFFFFFFUL;
    for (i = 0; i < m->nfiles; i++)
    {
        if (i == fi) continue;
        g = &m->files[i];
        if (!(g->key == f->key || g->key == f->skey || g->skey == f->key))
            continue;                             /* another film */
        if (g->state == MC_STATE_NEWER) continue; /* known not to play */
        if (mc_is_512kb(g))
        {
            if (mp4 < 0) mp4 = i;
            continue;
        }
        if (!mc_startsi(g->format, "h.264") || g->width <= 0
            || g->height <= 0)
            continue;
        gmb = mc_mbs100(g->width, g->height, 100L);
        if (gmb >= fmb) continue;                 /* not lighter */
        if (mc_g3_verdict(mhz, g->width, g->height, 0L, MC_PCLASS_MAIN, 0,
                          NULL) != MC_G3_PLAYS)
            continue;
        if (best < 0 || gmb > bestmb) { best = i; bestmb = gmb; }
    }
    return best >= 0 ? best : mp4;
}

/* (0.6.1) a G3 sentence for an H.264 verdict line; sets gLastStutter */
static void mc_g3_line(const McAvcInfo *info, char *line)
{
    long mhz = mc_cpu_mhz(), pct;
    int  v, pc = info->profile == 66 ? MC_PCLASS_BASE : MC_PCLASS_MAIN;
    char t[120];

    v = mc_g3_verdict(mhz, info->width, info->height, info->fps100, pc,
                      info->bframes, &pct);
    if (info->fps100 > 0)
        sprintf(t, " G3 %ld MHz: %ld.%02ld fps%s, %ld%% of budget",
                mhz, info->fps100 / 100L, info->fps100 % 100L,
                info->bframes ? "+B" : "", pct);
    else
        sprintf(t, " G3 %ld MHz: fps unknown (30 assumed)%s, %ld%% of "
                "budget", mhz, info->bframes ? "+B" : "", pct);
    if (strlen(line) + strlen(t) + 30 < 320) strcat(line, t);
    if (v == MC_G3_HEAVY)
    {
        if (strlen(line) + 30 < 320) strcat(line, ": too heavy (G3).");
        gLastStutter = MC_FILE_NOTE_HEAVY;
    }
    else if (v == MC_G3_STUTTER)
    {
        if (strlen(line) + 30 < 320) strcat(line, ": may stutter.");
        if (gLastStutter < MC_FILE_NOTE_STUTTER)
            gLastStutter = MC_FILE_NOTE_STUTTER;
    }
    else if (strlen(line) + 2 < 320) strcat(line, ": plays.");
}

/* (0.4b) Main/High size limits in 16x16 macroblocks */
#define MC_MH_MB_SMOOTH 1200L   /* 640x480: plays                       */
#define MC_MH_MB_MAX    1620L   /* 854x480: plays, may stutter; more = -2 */

/* (0.4) Decoder 17 + AVCDecLib, any profile but 66 (Baseline keeps the
   0.3 rule). It decodes profile_idc 77 and 100 when the stream is
   8-bit 4:2:0; profiles 110/122/244, bit depth > 8 or chroma other
   than 4:2:0 are refused by the decoder (black picture), so refuse
   them here with a sentence. Interlaced (frame_mbs_only_flag 0) is
   untested: handed over, flagged. (0.4b) Size: see the MB rule below. */
static int mc_verdict_mainhigh(const McAvcInfo *info, char *line)
{
    const char *pn = mc_profile_name(info->profile);
    long mbs;
    int  big;

    if (!mc_is((const unsigned char *)info->codec, "avc1"))
    {
        /* the decoder registers for 'avc1' only, and the B-frame
           rewrite (AVCReorder.c) handles 'avc1' entries only */
        sprintf(line, "This video is %s profile (%d) in an '%.4s' track; "
                "this decoder plays Main and High from 'avc1' tracks only.",
                pn, info->profile, info->codec);
        return MC_PLAY_NOT_BASE;
    }
    if (info->sps && (info->depth_luma > 8 || info->depth_chroma > 8))
    {
        sprintf(line, "This video is %d-bit (%s profile); this decoder "
                "plays 8-bit video only.", info->depth_luma > 8
                ? info->depth_luma : info->depth_chroma, pn);
        return MC_PLAY_NOT_BASE;
    }
    if (info->sps && info->chroma != 1)
    {
        sprintf(line, "This video is %s (%s profile); this decoder plays "
                "4:2:0 colour only.", mc_chroma_name(info->chroma), pn);
        return MC_PLAY_NOT_BASE;
    }
    if (info->profile != 77 && info->profile != 100)
    {
        sprintf(line, "This video is %s profile (%d); this decoder plays "
                "Baseline, Main and High only.", pn, info->profile);
        return MC_PLAY_NOT_BASE;
    }
    if (!info->sps)
    {
        sprintf(line, "This %s profile video's settings (its SPS) could "
                "not be read.", pn);
        return MC_PLAY_UNKNOWN;
    }
    /* (0.4b) size rule, counted in 16x16 macroblocks (what the decoder
       actually works through per picture):
         MB <= 1200 (640x480)              plays
         MB <= 1620 (854x480; PAL 720x576 = 1620, NTSC 720x480 = 1350)
                                           plays, "may stutter"
         more                              TOO_BIG
       The Baseline 1280x1024 limit stays as a second fence (a very wide
       or very tall strip can be few MBs). */
    mbs = ((long)info->width + 15L) / 16L * (((long)info->height + 15L) / 16L);
    if (info->width <= 0 || info->height <= 0 || mbs > MC_MH_MB_MAX
        || info->width > MC_MAX_WIDTH || info->height > MC_MAX_HEIGHT)
    {
        sprintf(line, "This %s H.264 video is %dx%d; Main/High plays up "
                "to 854x480 here.", pn, info->width, info->height);
        return MC_PLAY_TOO_BIG;
    }
    big = mbs > MC_MH_MB_SMOOTH;
    gLastStutter = big ? MC_FILE_NOTE_STUTTER : MC_FILE_NOTE_NONE;
                                  /* (0.6) kept per file by mc_fetch.c */
    if (!info->frame_mbs_only)
    {
        sprintf(line, "%s H.264, level %d.%d, %dx%d, interlaced: untested "
                "-- it may not play correctly.%s", pn, info->level / 10,
                info->level % 10, info->width, info->height,
                big ? " Large for this Mac: it may stutter." : "");
        return MC_PLAY_UNTESTED;
    }
    sprintf(line, "%s H.264, level %d.%d, %dx%d.%s", pn, info->level / 10,
            info->level % 10, info->width, info->height,
            big ? " Large for this Mac: it may stutter." : "");
    return MC_PLAY_OK;
}

int mc_avc_verdict(const McAvcInfo *info, int found, char *msg, long msgMax)
{
    return mc_avc_verdict_at(info, found, mc_decoder_level(), msg, msgMax);
}

int mc_avc_verdict_at(const McAvcInfo *info, int found, int level,
                      char *msg, long msgMax)
{
    char line[320];
    int  r;

    gLastStutter = 0;
    if (found == -2)
    { strcpy(line, "The movie header is damaged."); r = MC_PLAY_UNKNOWN; }
    else if (found != 0)
    { strcpy(line, "This file has no video track."); r = MC_PLAY_NO_VIDEO; }
    else if (mc_is((const unsigned char *)info->codec, "avc1")
             || mc_is((const unsigned char *)info->codec, "avc3"))
    {
        if (info->profile < 0)
        {
            strcpy(line, "This H.264 video has no avcC settings box.");
            r = MC_PLAY_UNKNOWN;
        }
        else if (level >= MC_DECODER_MAINHIGH && info->profile != 66)
            r = mc_verdict_mainhigh(info, line);
        else if (info->profile != 66)
        {
            /* h264bsd parses the SPS as Baseline syntax only (no
               chroma_format_idc etc.) and has no CABAC, B-slices or
               interlace: profile 66 is the only safe answer. */
            sprintf(line, "This video is %s profile (%d); this decoder "
                    "plays Baseline only. Install H264 Decoder 17c and "
                    "AVCDecLib to play it.", mc_profile_name(info->profile),
                    info->profile);
            r = MC_PLAY_NOT_BASE;
        }
        else if (info->width > MC_MAX_WIDTH || info->height > MC_MAX_HEIGHT)
        {
            sprintf(line, "This video is %dx%d; the limit is %dx%d.",
                    info->width, info->height, MC_MAX_WIDTH, MC_MAX_HEIGHT);
            r = MC_PLAY_TOO_BIG;
        }
        else
        {
            sprintf(line, "Baseline H.264, level %d.%d, %dx%d.",
                    info->level / 10, info->level % 10,
                    info->width, info->height);
            r = MC_PLAY_OK;
        }
    }
    else if (mc_is((const unsigned char *)info->codec, "mp4v"))
    {
        sprintf(line, "MPEG-4 video, %dx%d (QuickTime plays this itself).",
                info->width, info->height);
        r = MC_PLAY_NATIVE;
    }
    else
    {
        sprintf(line, "This video is '%.4s', which QuickTime 6 cannot play.",
                info->codec);
        r = MC_PLAY_UNKNOWN;
    }
    /* (0.6.1) a G3 also gets its decode budget (any H.264 that plays) */
    if (mc_g3 && (r == MC_PLAY_OK || r == MC_PLAY_UNTESTED)
        && (mc_is((const unsigned char *)info->codec, "avc1")
            || mc_is((const unsigned char *)info->codec, "avc3")))
        mc_g3_line(info, line);
    if (msg != NULL && msgMax > 0) mc_copy(msg, line, msgMax);
    return r;
}
