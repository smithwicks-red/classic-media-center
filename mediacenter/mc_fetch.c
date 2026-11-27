/*
    mc_fetch.c -- Classic Media Center 0.4 (0.2: one transport for everything
    (https_client), the "can QuickTime fetch it itself?" probe, and
    download-then-play. See mc_fetch.h. Strict C89.

    THE LOCAL MOVIE (why it is built this way)
    ------------------------------------------
    QuickTime 6 does not play these .mp4 files as they are (the MPEG-4
    importer drops the avcC; the AAC sample description is in ISO form).
    For streaming, H264Stream.c rewrites the moov into a small reference
    movie. For a downloaded file we do the same rewrite, but we must not
    move a single media byte, or every chunk offset would be wrong. So
    the file is written to disk EXACTLY as it arrives, except that two
    box headers are relabelled on the way past:

        ftyp  -> free     (QuickTime's movie parser skips 'free')
        moov  -> free     (the original moov stays, as dead bytes)
        mdat     unchanged, at the same offset as on the server

    and at the end we APPEND the rewritten moov, whose data reference
    says "the media is in this file" ('alis', flags 1, as QuickTime
    itself writes) and whose chunk offsets are untouched -- they still
    point at the same bytes, because nothing before them moved. Only
    sequential writes; no seeking, no second pass, no offset arithmetic.
    It works the same for moov-at-front and moov-at-end files.

    While the original moov streams past it is compared, byte for byte,
    with the moov we checked (and rewrote) before the download started:
    if the server sent a different file, the download is refused.
*/
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <Errors.h>
#include <Files.h>
#include <Script.h>
#include <Events.h>
#include <DateTimeUtils.h>
#include "mc_fetch.h"

#define MCF_MAX_MOOV  (8UL * 1024UL * 1024UL)
#define MCF_SKIP      (512UL * 1024UL)

unsigned short mcf_test_http_port = 0, mcf_test_https_port = 0;

/* ------------------------------------------------------------------ */

static void L(McfEnv *e, const char *fmt, ...)
{
    static char buf[1400];
    va_list ap;
    va_start(ap, fmt);
    vsprintf(buf, fmt, ap);
    va_end(ap);
    if (e->log != NULL) e->log(e->ref, buf);
}

static unsigned long R32(const unsigned char *p)
{
    return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16)
         | ((unsigned long)p[2] << 8) | (unsigned long)p[3];
}

static int IsT(const unsigned char *p, const char *t)
{
    return memcmp(p, t, 4) == 0;
}

static int IsHttps(const char *u)
{
    return (u[0] == 'h' || u[0] == 'H') && (u[1] == 't' || u[1] == 'T')
        && (u[2] == 't' || u[2] == 'T') && (u[3] == 'p' || u[3] == 'P')
        && (u[4] == 's' || u[4] == 'S') && u[5] == ':';
}

static void PStr(const char *c, Str255 p)
{
    size_t n = strlen(c);
    if (n > 255) n = 255;
    p[0] = (unsigned char)n;
    memcpy(p + 1, c, n);
}

static void Copy(char *dst, const char *src, unsigned long cap)
{
    unsigned long n = (unsigned long)strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, (size_t)n);
    dst[n] = 0;
}

/* ==================================================================
   The box walker
   ================================================================== */

void mcf_walk_init(McfWalk *w, int mode, McfEnv *env)
{
    memset(w, 0, sizeof *w);
    w->mode = mode;
    w->env = env;
    w->hneed = 8;
}

void mcf_walk_free(McfWalk *w)
{
    if (w->m != NULL && w->env != NULL && w->env->release != NULL)
        w->env->release(w->m);
    w->m = NULL;
}

static int WFail(McfWalk *w, int err, const char *msg)
{
    if (w->err == 0)
    {
        w->err = err;
        Copy(w->msg, msg, sizeof w->msg);
    }
    return 1;
}

static int WBoxEnd(McfWalk *w)
{
    w->in_box = 0;
    if (w->mode == MCF_WALK_FETCH && w->kind == 1)
    {
        w->seen_moov = 1;
        w->stop = 1;
        return 1;
    }
    w->kind = 0;
    return 0;
}

/* a complete box header is in hb[0..hlen); pos is just after it */
static int WHeader(McfWalk *w)
{
    unsigned long size = R32(w->hb), body;
    unsigned long start = w->pos - (unsigned long)w->hlen;
    char t[5], m[200];
    int i;

    for (i = 0; i < 4; i++)
    {
        char c = (char)w->hb[4 + i];
        t[i] = (c >= 32 && c < 127) ? c : '?';
    }
    t[4] = 0;

    if (w->hlen == 16)
    {
        if (R32(w->hb + 8) != 0)
            return WFail(w, MCF_ERR_FORMAT, "A box is larger than 4 GB.");
        size = R32(w->hb + 12);
    }
    else if (size == 0)
    {
        sprintf(m, "Box '%s' at offset %lu runs to the end of the file; "
                "this program cannot handle that.", t, start);
        return WFail(w, MCF_ERR_FORMAT, m);
    }
    if (size < (unsigned long)w->hlen)
    {
        sprintf(m, "Corrupt box '%s' (size %lu) at offset %lu.", t, size,
                start);
        return WFail(w, MCF_ERR_FORMAT, m);
    }
    if (start + size < start)
        return WFail(w, MCF_ERR_FORMAT, "Offsets beyond 4 GB.");
    w->boxes++;
    if (w->boxes == 1 && !IsT(w->hb + 4, "ftyp") && !IsT(w->hb + 4, "moov")
        && !IsT(w->hb + 4, "mdat") && !IsT(w->hb + 4, "free")
        && !IsT(w->hb + 4, "wide") && !IsT(w->hb + 4, "skip"))
    {
        sprintf(m, "The file does not start like an MP4 (first box '%s').",
                t);
        return WFail(w, MCF_ERR_FORMAT, m);
    }
    if (w->boxes > 64 && !w->seen_moov)
        return WFail(w, MCF_ERR_FORMAT,
                     "More than 64 top-level boxes and no moov.");
    body = size - (unsigned long)w->hlen;
    w->kind = 0;

    if (w->mode == MCF_WALK_FETCH)
    {
        if (w->env != NULL)
            L(w->env, "  box '%s'  %lu bytes at offset %lu\n", t, size, start);
        if (IsT(w->hb + 4, "mdat")) w->mdat_first = 1;     /* (0.5u) */
        if (IsT(w->hb + 4, "moov"))
        {
            if (size > MCF_MAX_MOOV)
                return WFail(w, MCF_ERR_FORMAT, "The moov is over 8 MB.");
            w->m = (unsigned char *)w->env->alloc((long)size);
            if (w->m == NULL)
                return WFail(w, MCF_ERR_NOMEM,
                             "Not enough memory for the moov.");
            memcpy(w->m, w->hb, (size_t)w->hlen);
            w->mlen = (unsigned long)w->hlen;
            w->msize = size;
            w->moov_end = start + size;
            w->kind = 1;
        }
        else if (body > MCF_SKIP)
        {
            /* the media: do not read through it, ask for what follows */
            w->stop = 2;
            w->range_from = start + size;
            return 1;
        }
    }
    else
    {
        if (IsT(w->hb + 4, "moov"))
        {
            if (w->seen_moov)
                return WFail(w, MCF_ERR_FORMAT, "The file has two moov boxes.");
            w->seen_moov = 1;
            if (w->ref_moov == NULL || size != w->ref_len
                || memcmp(w->hb, w->ref_moov, (size_t)w->hlen) != 0)
                return WFail(w, MCF_ERR_CHANGED,
                    "The server sent a different file from the one that was "
                    "checked (its moov differs). Try again later.");
            w->cmp_at = (unsigned long)w->hlen;
            w->kind = 1;
            memcpy(w->hb + 4, "free", 4);
        }
        else if (IsT(w->hb + 4, "ftyp"))
            memcpy(w->hb + 4, "free", 4);
        else if (IsT(w->hb + 4, "mdat"))
            w->seen_mdat = 1;
        if (w->out != NULL
            && w->out(w->out_ref, w->hb, (unsigned long)w->hlen) != 0)
            return WFail(w, MCF_ERR_FILE, "Could not write to the disk.");
    }

    w->in_box = 1;
    w->remain = body;
    w->hlen = 0;
    w->hneed = 8;
    if (body == 0) return WBoxEnd(w);
    return 0;
}

int mcf_walk_feed(McfWalk *w, const unsigned char *d, unsigned long n)
{
    if (w->err != 0 || w->stop != 0) return 1;
    while (n > 0)
    {
        if (!w->in_box)
        {
            while (n > 0 && w->hlen < w->hneed)
            {
                w->hb[w->hlen++] = *d++;
                n--;
                w->pos++;
            }
            if (w->hlen < w->hneed) return 0;
            if (w->hlen == 8 && R32(w->hb) == 1)
            {
                w->hneed = 16;
                continue;
            }
            if (WHeader(w)) return 1;
        }
        else
        {
            unsigned long take = (n < w->remain) ? n : w->remain;
            if (w->mode == MCF_WALK_FETCH)
            {
                if (w->kind == 1)
                {
                    memcpy(w->m + w->mlen, d, (size_t)take);
                    w->mlen += take;
                }
            }
            else
            {
                if (w->kind == 1)
                {
                    if (memcmp(d, w->ref_moov + w->cmp_at, (size_t)take) != 0)
                        return WFail(w, MCF_ERR_CHANGED,
                            "The server sent a different file from the one "
                            "that was checked (its moov differs).");
                    w->cmp_at += take;
                }
                if (w->out != NULL && w->out(w->out_ref, d, take) != 0)
                    return WFail(w, MCF_ERR_FILE,
                                 "Could not write to the disk.");
            }
            d += take;
            n -= take;
            w->pos += take;
            w->remain -= take;
            if (w->remain == 0 && WBoxEnd(w)) return 1;
        }
    }
    return 0;
}

/* ==================================================================
   Requests
   ================================================================== */

typedef struct McfReq
{
    McfEnv       *e;
    McfWalk      *w;
    HC_BodyProc   inner;
    void         *iref;
    int           saw_https, cancelled, first, want_range, bad_body;
    unsigned long range_at;
    /* download */
    short         fref;
    OSErr         werr;
    int           disk;
    unsigned long done, total, expect, free_kb, tail_len, need_kb;
    unsigned long last_tick;
    unsigned long first_tick;     /* (0.6) body began: McfEnv.progress  */
    int           last_pct;
} McfReq;

static int ReqIdle(void *ref, int busy)
{
    McfReq *q = (McfReq *)ref;
    if (q->e->idle != NULL && q->e->idle(q->e->ref, busy) != 0)
    {
        q->cancelled = 1;
        return 1;
    }
    return 0;
}

static int ReqRedirect(void *ref, int status, const char *from,
                       const char *to)
{
    McfReq *q = (McfReq *)ref;
    (void)from;
    L(q->e, "  %d -> %.400s\n", status, to);
    if (IsHttps(to)) q->saw_https = 1;
    return 0;
}

static void SetupReq(McfEnv *e, HC_Request *rq, McfReq *q, const char *url,
                     HC_BodyProc body)
{
    memset(rq, 0, sizeof *rq);
    rq->url = url;
    rq->insecure = e->insecure;
    rq->user_agent = e->user_agent;
    rq->body = body;
    rq->idle = ReqIdle;
    rq->redirect = ReqRedirect;
    rq->ref = q;
}

static void LogResult(McfEnv *e, int rc)
{
    HC_Result *r = &e->res;
    if (rc == HC_OK)
        L(e, "  HTTP %d, %s, %lu bytes, %lu ticks\n", r->http_status,
          r->tls_version == 0x0304 ? "TLS 1.3" : r->tls_version == 0x0303
          ? "TLS 1.2" : "plain http", r->body_bytes, r->t_all);
    else if (rc != HC_ERR_ABORTED)
        L(e, "  failed: %s (HTTP %d, os %ld, BearSSL %d)\n", HC_ErrName(rc),
          r->http_status, r->os_err, r->br_err);
}

static int JsonBody(void *ref, const unsigned char *d, unsigned long n)
{
    McfReq *q = (McfReq *)ref;
    if (q->inner(q->iref, d, n) != 0)
    {
        q->bad_body = 1;
        return 1;
    }
    return 0;
}

int mcf_get(McfEnv *e, const char *url, HC_BodyProc body, void *bref)
{
    HC_Request rq;
    McfReq     q;
    int        rc;

    memset(&q, 0, sizeof q);
    q.e = e;
    q.inner = body;
    q.iref = bref;
    SetupReq(e, &rq, &q, url, JsonBody);
    L(e, "GET %.400s\n", url);
    rc = HC_Get(&rq, &e->res);
    LogResult(e, rc);
    if (q.bad_body)
    {
        Copy(e->message, "The answer was not what archive.org usually sends.",
             sizeof e->message);
        return MCF_ERR_FORMAT;
    }
    if (q.cancelled) return MCF_ERR_CANCEL;
    if (rc != HC_OK) return MCF_ERR_NET;
    return MCF_OK;
}

static int FeedS(void *c, const unsigned char *b, unsigned long n)
{
    return mc_search_feed((McSearch *)c, (const char *)b, (long)n) != 0;
}

static int FeedM(void *c, const unsigned char *b, unsigned long n)
{
    return mc_meta_feed((McMeta *)c, (const char *)b, (long)n) != 0;
}

/* hit count, or -1 (e->message says why) */
int mcf_search(McfEnv *e, const char *terms, int page, McSearch *s)
{
    char url[MC_URLMAX];
    char q[700];
    int  r, n, scope;

    e->message[0] = 0;
    /* (0.3) page 1: titles first; nothing there -> every field.
       Later pages keep the scope page 1 ended up with. */
    scope = (page <= 1) ? MC_SCOPE_TITLE : e->search_scope;
    for (;;)
    {
        if (mc_search_url2(terms, scope, page, url, (long)sizeof url) != 0
            || mc_search_query(terms, scope, q, (long)sizeof q) != 0)
        {
            Copy(e->message, "Type a word or two to search for (e.g. "
                 "popeye).", sizeof e->message);
            return -1;
        }
        L(e, "SEARCH q=%s page=%d\n", q, page);
        mc_search_begin(s);
        r = mcf_get(e, url, FeedS, s);
        if (r != MCF_OK)
        {
            if (r != MCF_ERR_FORMAT) mcf_explain(e, r);
            return -1;
        }
        n = mc_search_end(s);
        if (n < 0)
        {
            Copy(e->message, "archive.org sent something that is not a "
                 "search result.", sizeof e->message);
            return n;
        }
        if (n == 0 && page <= 1 && scope == MC_SCOPE_TITLE)
        {
            L(e, "No title has all these words; searching every field.\n");
            scope = MC_SCOPE_ANY;
            continue;
        }
        if (page <= 1) e->search_scope = scope;
        L(e, "SEARCH found=%ld scope=%s\n", s->numFound,
          scope == MC_SCOPE_TITLE ? "title" : "any");
        return n;
    }
}

/* mc_meta_end's code (0, -1, -2, -3), or -10 network (explained),
   -11 stopped */
int mcf_meta(McfEnv *e, const char *id, McMeta *m)
{
    char url[MC_URLMAX];
    int  r;

    if (mc_meta_url(id, url, (long)sizeof url) != 0) return -1;
    mc_meta_begin(m);
    r = mcf_get(e, url, FeedM, m);
    if (r == MCF_ERR_CANCEL) return -11;
    if (r == MCF_ERR_NET) { mcf_explain(e, r); return -10; }
    if (r != MCF_OK) return -1;
    return mc_meta_end(m);
}

/* ==================================================================
   The moov, and the probe
   ================================================================== */

static int FetchBody(void *ref, const unsigned char *d, unsigned long n)
{
    McfReq *q = (McfReq *)ref;
    if (q->first)
    {
        q->first = 0;
        if (q->want_range)
        {
            const char   *c = q->e->res.content_range;
            unsigned long first = 0;
            char          m[160];
            if (q->e->res.http_status != 206)
                return WFail(q->w, MCF_ERR_FORMAT,
                    "This file has its moov at the END and the server "
                    "ignored the Range request, so it would have to be "
                    "downloaded whole just to check it.");
            while (*c && (*c < '0' || *c > '9')) c++;
            while (*c >= '0' && *c <= '9')
                first = first * 10UL + (unsigned long)(*c++ - '0');
            if (first != q->range_at)
            {
                sprintf(m, "The server sent bytes from %lu, not %lu.", first,
                        q->range_at);
                return WFail(q->w, MCF_ERR_FORMAT, m);
            }
        }
    }
    return mcf_walk_feed(q->w, d, n);
}

static unsigned long TotalSize(const HC_Result *r, int ranged)
{
    const char *s = strchr(r->content_range, '/');
    unsigned long v = 0;
    if (s != NULL && s[1] >= '0' && s[1] <= '9')
    {
        for (s++; *s >= '0' && *s <= '9'; s++)
            v = v * 10UL + (unsigned long)(*s - '0');
        return v;
    }
    if (!ranged && r->has_length && r->http_status == 200)
        return r->content_length;
    return 0;
}

int mcf_fetch_moov(McfEnv *e, const char *url, unsigned char **moov,
                   long *moovLen, McfProbe *p)
{
    McfWalk    w;
    McfReq     q;
    HC_Request rq;
    char       cur[MC_URLMAX];
    int        tries, rc;
    unsigned long sz;

    memset(p, 0, sizeof *p);
    *moov = NULL;
    *moovLen = 0;
    if (strlen(url) >= MC_URLMAX) return MCF_ERR_FORMAT;
    Copy(p->final_url, url, sizeof p->final_url);
    p->saw_https = IsHttps(url);
    mcf_walk_init(&w, MCF_WALK_FETCH, e);

    for (tries = 0; tries < 3; tries++)
    {
        memset(&q, 0, sizeof q);
        q.e = e;
        q.w = &w;
        q.first = 1;
        q.want_range = (tries > 0);
        q.range_at = w.pos;
        Copy(cur, tries == 0 ? url : p->final_url, sizeof cur);
        SetupReq(e, &rq, &q, cur, FetchBody);
        if (q.want_range)
        {
            rq.use_range = 1;
            rq.range_start = w.pos;
            rq.range_end = 0xFFFFFFFFUL;
            L(e, "GET %.400s (bytes %lu onward)\n", cur, w.pos);
        }
        else
            L(e, "GET %.400s\n", cur);
        rc = HC_Get(&rq, &e->res);
        LogResult(e, rc);
        p->requests++;
        if (q.saw_https) p->saw_https = 1;
        if (e->res.final_url[0] != 0)
        {
            Copy(p->final_url, e->res.final_url, sizeof p->final_url);
            if (IsHttps(p->final_url)) p->saw_https = 1;
        }
        sz = TotalSize(&e->res, q.want_range);
        if (sz != 0) p->file_size = sz;

        if (w.err != 0)
        {
            L(e, "  %s\n", w.msg);
            Copy(e->message, w.msg, sizeof e->message);
            mcf_walk_free(&w);
            return w.err;
        }
        if (w.stop == 1)
        {
            L(e, "  Got the moov (%lu bytes).\n", w.mlen);
            *moov = w.m;
            *moovLen = (long)w.mlen;
            p->moov_end = w.mdat_first ? 0UL : w.moov_end;   /* (0.5u) */
            w.m = NULL;
            return MCF_OK;
        }
        if (q.cancelled)
        {
            mcf_walk_free(&w);
            return MCF_ERR_CANCEL;
        }
        if (w.stop == 2)
        {
            L(e, "  Skipping %lu KB of media: asking for bytes %lu onward.\n",
              (w.range_from - w.pos) / 1024UL, w.range_from);
            w.stop = 0;
            w.pos = w.range_from;
            w.in_box = 0;
            w.hlen = 0;
            w.hneed = 8;
            continue;
        }
        mcf_walk_free(&w);
        if (rc != HC_OK) return MCF_ERR_NET;
        Copy(e->message, "Reached the end of the file without finding a "
             "'moov'.", sizeof e->message);
        L(e, "  %s\n", e->message);
        return MCF_ERR_FORMAT;
    }
    mcf_walk_free(&w);
    Copy(e->message, "Too many requests while looking for the moov.",
         sizeof e->message);
    return MCF_ERR_FORMAT;
}

int mcf_decide(int force, const McfProbe *p, int proxy_ok)
{
    if (force) return MCF_MODE_DOWNLOAD;
    if (!p->saw_https) return MCF_MODE_STREAM;
    return proxy_ok ? MCF_MODE_PROXY : MCF_MODE_DOWNLOAD;
}

/* out must hold MC_URLMAX + 16 */
void mcf_to_https(const char *url, char *out)
{
    if (strncmp(url, "http://", 7) != 0)
    {
        Copy(out, url, MC_URLMAX + 16);
        return;
    }
    sprintf(out, "https://%.1000s", url + 7);
    if (mcf_test_http_port != 0)
    {
        char pat[16], rest[MC_URLMAX + 16];
        char *at;
        sprintf(pat, ":%u/", (unsigned)mcf_test_http_port);
        at = strstr(out, pat);
        if (at != NULL)
        {
            strcpy(rest, at + strlen(pat));
            sprintf(at, ":%u/%s", (unsigned)mcf_test_https_port, rest);
        }
    }
}

/* ==================================================================
   Download
   ================================================================== */

int mcf_downloads_dir(McfEnv *e, short *vref, long *dir)
{
    FSSpec     s;
    Str255     nm;
    CInfoPBRec pb;
    OSErr      err;
    long       created;

    PStr(MCF_DL_FOLDER, nm);
    err = FSMakeFSSpec(e->app_vref, e->app_dir, nm, &s);
    if (err == fnfErr)
    {
        err = FSpDirCreate(&s, smSystemScript, &created);
        if (err == noErr)
        {
            *vref = s.vRefNum;
            *dir = created;
            L(e, "Created the folder \"%s\" beside the application.\n",
              MCF_DL_FOLDER);
            return MCF_OK;
        }
    }
    if (err != noErr)
    {
        sprintf(e->message, "Cannot make the folder \"%s\" (error %d).",
                MCF_DL_FOLDER, (int)err);
        return MCF_ERR_FILE;
    }
    memset(&pb, 0, sizeof pb);
    pb.dirInfo.ioNamePtr = s.name;
    pb.dirInfo.ioVRefNum = s.vRefNum;
    pb.dirInfo.ioDrDirID = s.parID;
    pb.dirInfo.ioFDirIndex = 0;
    err = PBGetCatInfoSync(&pb);
    if (err != noErr || (pb.dirInfo.ioFlAttrib & ioDirMask) == 0)
    {
        sprintf(e->message, "\"%s\" is not a folder (error %d).",
                MCF_DL_FOLDER, (int)err);
        return MCF_ERR_FILE;
    }
    *vref = s.vRefNum;
    *dir = pb.dirInfo.ioDrDirID;
    return MCF_OK;
}

/* free space in KB; 0xFFFFFFFF if unknown */
static unsigned long FreeKB(short vref)
{
    HParamBlockRec pb;
    unsigned long  blocks, per;

    memset(&pb, 0, sizeof pb);
    pb.volumeParam.ioNamePtr = NULL;
    pb.volumeParam.ioVRefNum = vref;
    pb.volumeParam.ioVolIndex = 0;
    if (PBHGetVInfoSync(&pb) != noErr) return 0xFFFFFFFFUL;
    blocks = (unsigned long)pb.volumeParam.ioVFrBlk;
    per = (unsigned long)pb.volumeParam.ioVAlBlkSiz / 512UL;   /* sectors */
    if (per != 0 && blocks > 0xFFFFFFFEUL / per) return 0xFFFFFFFEUL;
    return blocks * per / 2UL;
}

static int ReuseOK(const FSSpec *s, const McfJob *j)
{
    short ref;
    long  eof, off, cnt, want;
    unsigned char hb[16], buf[2048];
    unsigned long sz;
    int   ok = 0, sawMdat = 0, boxes = 0;

    if (FSpOpenDF(s, fsRdPerm, &ref) != noErr) return 0;
    if (GetEOF(ref, &eof) != noErr) goto done;
    if (j->expect_size != 0
        && (unsigned long)eof != j->expect_size + (unsigned long)j->tailLen)
        goto done;
    if (eof < j->tailLen + 16) goto done;
    for (off = 0; off < eof; off += (long)sz)
    {
        if (++boxes > 256) goto done;
        if (SetFPos(ref, fsFromStart, off) != noErr) goto done;
        cnt = 8;
        if (FSRead(ref, &cnt, hb) != noErr || cnt != 8) goto done;
        sz = R32(hb);
        if (sz == 1)
        {
            cnt = 8;
            if (FSRead(ref, &cnt, hb + 8) != noErr || cnt != 8) goto done;
            if (R32(hb + 8) != 0) goto done;
            sz = R32(hb + 12);
        }
        if (sz < 8 || (unsigned long)(eof - off) < sz) goto done;
        if (IsT(hb + 4, "mdat")) sawMdat = 1;
        if (off + (long)sz == eof
            && (!IsT(hb + 4, "moov") || off != eof - j->tailLen))
            goto done;
    }
    if (!sawMdat) goto done;
    if (SetFPos(ref, fsFromStart, eof - j->tailLen) != noErr) goto done;
    for (off = 0; off < j->tailLen; off += want)
    {
        want = j->tailLen - off;
        if (want > (long)sizeof buf) want = (long)sizeof buf;
        cnt = want;
        if (FSRead(ref, &cnt, buf) != noErr || cnt != want) goto done;
        if (memcmp(buf, j->tail + off, (size_t)want) != 0) goto done;
    }
    ok = 1;
done:
    FSClose(ref);
    return ok;
}

static int CopyOut(void *ref, const unsigned char *p, unsigned long n)
{
    McfReq *q = (McfReq *)ref;
    long    cnt = (long)n;
    OSErr   err = FSWrite(q->fref, &cnt, p);
    if (err != noErr || cnt != (long)n)
    {
        q->werr = (err != noErr) ? err : dskFulErr;
        return 1;
    }
    return 0;
}

static int DlBody(void *ref, const unsigned char *d, unsigned long n)
{
    McfReq       *q = (McfReq *)ref;
    McfEnv       *e = q->e;
    unsigned long now;
    int           pct;

    if (q->first)
    {
        q->first = 0;
        q->total = e->res.has_length ? e->res.content_length : q->expect;
        if (e->res.has_length && q->expect != 0
            && e->res.content_length != q->expect)
            L(e, "  (the server says %lu bytes; the item list said %lu)\n",
              e->res.content_length, q->expect);
        if (q->total != 0)
        {
            q->need_kb = q->total / 1024UL + q->tail_len / 1024UL + 2UL
                       + (e->spare_kb ? e->spare_kb : MCF_SPARE_KB);
            if (q->free_kb < q->need_kb)
            {
                q->disk = 1;
                return 1;
            }
        }
        q->last_tick = TickCount();
        q->first_tick = q->last_tick;
        L(e, "  Receiving %lu KB ...\n", q->total / 1024UL);
    }
    if (mcf_walk_feed(q->w, d, n)) return 1;
    q->done += n;
    e->dl_bytes = q->done;
    pct = (q->total / 1024UL) ? (int)((q->done / 1024UL) * 100UL
                                      / (q->total / 1024UL)) : 0;
    if (pct > 100) pct = 100;
    now = TickCount();
    if (now - q->last_tick >= 30 || (q->total && q->done == q->total))
    {
        char line[120];
        q->last_tick = now;
        sprintf(line, "Downloading: %lu of %lu KB (%d%%). Cmd-period stops.",
                q->done / 1024UL, q->total / 1024UL, pct);
        if (e->status != NULL) e->status(e->ref, line);
        /* (0.6) the LCD's bar, KB/s and time left, without parsing text */
        if (e->progress != NULL)
            e->progress(e->ref, q->done, q->total, now - q->first_tick);
    }
    if (pct / 10 > q->last_pct / 10)
    {
        L(e, "  ... %lu of %lu KB (%d%%)\n", q->done / 1024UL,
          q->total / 1024UL, pct);
        q->last_pct = pct;
    }
    return 0;
}

/* name.mov -> base (at most 24 chars, so "base 9.part" fits in 31) */
static void BaseName(const char *name, char *base)
{
    size_t n = strlen(name);
    if (n >= 4 && strcmp(name + n - 4, ".mov") == 0) n -= 4;
    if (n > 24) n = 24;
    if (n == 0) { strcpy(base, "film"); return; }
    memcpy(base, name, n);
    base[n] = 0;
}

int mcf_download(McfEnv *e, const McfJob *j, FSSpec *out, int *reused)
{
    short   vref, fref = 0;
    long    dir, cnt;
    char    base[32], fname[64], tname[64];
    Str255  pn, fpn;
    FSSpec  fs, ts;
    OSErr   err;
    int     i, rc, res;
    unsigned long freekb, t0, ticks, kbps;
    McfWalk w;
    McfReq  q;
    HC_Request rq;

    *reused = 0;
    rc = mcf_downloads_dir(e, &vref, &dir);
    if (rc != MCF_OK) return rc;
    BaseName(j->name, base);

    for (i = 1; i <= 9; i++)
    {
        if (i == 1) sprintf(fname, "%s.mov", base);
        else sprintf(fname, "%s %d.mov", base, i);
        PStr(fname, fpn);
        err = FSMakeFSSpec(vref, dir, fpn, &fs);
        if (err == fnfErr) break;
        if (err != noErr)
        {
            sprintf(e->message, "Bad file name \"%s\" (error %d).", fname,
                    (int)err);
            return MCF_ERR_FILE;
        }
        if (ReuseOK(&fs, j))
        {
            L(e, "Already downloaded: \"%s\" in \"%s\". Not fetching it "
              "again.\n", fname, MCF_DL_FOLDER);
            *out = fs;
            *reused = 1;
            return MCF_OK;
        }
        L(e, "  \"%s\" exists but is not this film; using another name.\n",
          fname);
    }
    if (i > 9)
    {
        sprintf(e->message, "Too many files called %s in \"%s\".", base,
                MCF_DL_FOLDER);
        return MCF_ERR_FILE;
    }
    if (i == 1) sprintf(tname, "%s.part", base);
    else sprintf(tname, "%s %d.part", base, i);

    freekb = FreeKB(vref);
    if (j->expect_size != 0)
    {
        unsigned long need = j->expect_size / 1024UL
            + (unsigned long)j->tailLen / 1024UL + 2UL
            + (e->spare_kb ? e->spare_kb : MCF_SPARE_KB);
        if (freekb < need)
        {
            sprintf(e->message, "Not enough disk space: this film needs "
                    "%lu MB (plus 5 MB spare) and only %lu MB are free.",
                    (j->expect_size / 1024UL + 1023UL) / 1024UL,
                    freekb / 1024UL);
            return MCF_ERR_DISK;
        }
    }

    PStr(tname, pn);
    err = FSMakeFSSpec(vref, dir, pn, &ts);
    if (err == noErr)
    {
        (void)FSpDelete(&ts);
        L(e, "  Removed a stale partial file \"%s\".\n", tname);
        err = FSMakeFSSpec(vref, dir, pn, &ts);
    }
    if (err != fnfErr && err != noErr)
    {
        sprintf(e->message, "Cannot use \"%s\" (error %d).", tname, (int)err);
        return MCF_ERR_FILE;
    }
    err = FSpCreate(&ts, 'TVOD', 'MooV', smSystemScript);
    if (err == noErr) err = FSpOpenDF(&ts, fsRdWrPerm, &fref);
    if (err != noErr)
    {
        sprintf(e->message, "Cannot create \"%s\" (error %d).", tname,
                (int)err);
        return MCF_ERR_FILE;
    }

    mcf_walk_init(&w, MCF_WALK_COPY, e);
    w.ref_moov = j->moov;
    w.ref_len = (unsigned long)j->moovLen;
    w.out = CopyOut;
    memset(&q, 0, sizeof q);
    w.out_ref = &q;
    q.e = e;
    q.w = &w;
    q.first = 1;
    q.fref = fref;
    q.expect = j->expect_size;
    q.free_kb = freekb;
    q.tail_len = (unsigned long)j->tailLen;
    q.last_pct = 0;
    SetupReq(e, &rq, &q, j->url, DlBody);

    L(e, "Downloading over https into \"%s\" as \"%s\":\n  %.400s\n",
      MCF_DL_FOLDER, fname, j->url);
    t0 = TickCount();
    res = HC_Get(&rq, &e->res);
    ticks = TickCount() - t0;
    LogResult(e, res);

    rc = MCF_OK;
    if (w.err != 0)
    {
        rc = w.err;
        Copy(e->message, w.msg, sizeof e->message);
    }
    else if (q.disk)
    {
        rc = MCF_ERR_DISK;
        sprintf(e->message, "Not enough disk space: this film needs %lu MB "
                "(plus 5 MB spare) and only %lu MB are free.",
                (q.total / 1024UL + 1023UL) / 1024UL, q.free_kb / 1024UL);
    }
    else if (q.werr != noErr)
    {
        rc = MCF_ERR_FILE;
        sprintf(e->message, "Could not write the download (error %d; disk "
                "full?).", (int)q.werr);
    }
    else if (q.cancelled)
        rc = MCF_ERR_CANCEL;
    else if (res != HC_OK)
        rc = MCF_ERR_NET;
    else if (w.in_box || w.hlen != 0 || !w.seen_moov || !w.seen_mdat)
    {
        rc = MCF_ERR_FORMAT;
        Copy(e->message, "The download ended in the middle of the file "
             "(or it has no moov/mdat).", sizeof e->message);
    }

    if (rc == MCF_OK)
    {
        cnt = j->tailLen;
        err = FSWrite(fref, &cnt, j->tail);
        if (err != noErr || cnt != j->tailLen)
        {
            rc = MCF_ERR_FILE;
            sprintf(e->message, "Could not write the movie header (error "
                    "%d; disk full?).", (int)err);
        }
    }
    FSClose(fref);
    if (rc != MCF_OK)
    {
        (void)FSpDelete(&ts);
        L(e, "  Partial file removed.\n");
        if (rc != MCF_ERR_NET && rc != MCF_ERR_CANCEL) L(e, "  %s\n",
                                                         e->message);
        return rc;
    }
    (void)FlushVol(NULL, vref);
    err = FSpRename(&ts, fpn);
    if (err == noErr) err = FSMakeFSSpec(vref, dir, fpn, out);
    if (err != noErr)
    {
        (void)FSpDelete(&ts);
        sprintf(e->message, "Could not rename the download (error %d).",
                (int)err);
        return MCF_ERR_FILE;
    }
    e->dl_bytes = q.done;
    e->dl_ticks = ticks;
    kbps = ticks ? (q.done / 1024UL) * 60UL / ticks : 0;
    L(e, "Downloaded %lu KB in %lu.%lu s (%lu KB/s). Saved as \"%s\" "
      "(%lu KB, self-contained).\n", q.done / 1024UL, ticks / 60UL,
      (ticks % 60UL) / 6UL, kbps, fname,
      (q.done + (unsigned long)j->tailLen) / 1024UL);
    return MCF_OK;
}

/* ==================================================================
   Messages
   ================================================================== */

static unsigned long MacYear(void)
{
    UInt32 s;
    GetDateTime(&s);
    return 1904UL + (unsigned long)s / 31556952UL;
}

const char *mcf_explain(McfEnv *e, int err)
{
    char      *m = e->message;
    HC_Result *r = &e->res;

    switch (err)
    {
    case MCF_OK:
        m[0] = 0;
        break;
    case MCF_ERR_CANCEL:
        strcpy(m, "Stopped.");
        break;
    case MCF_ERR_NOMEM:
        strcpy(m, "Not enough memory. Give Media Center more memory in "
               "Get Info.");
        break;
    case MCF_ERR_NET:
        switch (r->err)
        {
        case HC_ERR_CLOCK:
            sprintf(m, "Certificate check failed (clock is set to %lu?). "
                    "Set the date in the Date & Time control panel.",
                    MacYear());
            break;
        case HC_ERR_CERT:
            if (r->br_err == 54)
                sprintf(m, "Certificate check failed: not valid on the Mac's "
                        "date (clock says %lu?) (BearSSL 54).", MacYear());
            else if (r->br_err == 56)
                sprintf(m, "Certificate check failed: the certificate is for "
                        "another site (BearSSL 56).");
            else if (r->br_err == 62)
                sprintf(m, "Certificate check failed: not signed by a known "
                        "authority (BearSSL 62).");
            else
                sprintf(m, "Certificate check failed (BearSSL error %d).",
                        r->br_err);
            break;
        case HC_ERR_HANDSHAKE:
            sprintf(m, "The secure (TLS) connection failed (error %ld/%d).",
                    r->os_err, r->br_err);
            break;
        case HC_ERR_CONNECT:
            sprintf(m, "Could not connect to the server (error %ld).",
                    r->ot_err ? r->ot_err : r->os_err);
            break;
        case HC_ERR_TIMEOUT:
            strcpy(m, "The server stopped answering (timeout).");
            break;
        case HC_ERR_HTTP_STATUS:
            sprintf(m, "The server answered %d.", r->http_status);
            break;
        case HC_ERR_TRUNCATED:
        case HC_ERR_IO:
            strcpy(m, "The connection broke before the end.");
            break;
        default:
            sprintf(m, "Network error: %s.", HC_ErrName(r->err));
            break;
        }
        break;
    default:
        if (m[0] == 0) sprintf(m, "Failed (code %d).", err);
        break;
    }
    return m;
}

static int IsCertErr(const McfEnv *e)
{
    return e->res.err == HC_ERR_CERT || e->res.err == HC_ERR_CLOCK;
}

/* ==================================================================
   Play one search hit
   ================================================================== */

static int Fail(McfEnv *e, const char *msg)
{
    if (msg != e->message) Copy(e->message, msg, sizeof e->message);
    L(e, "%s\n", e->message);
    return -1;
}

/* (0.3) One candidate file: every URL, moov, Baseline check, then
   stream or download. (0.4) The check is mc_avc_verdict at the decoder
   level the app set: with the Main/High decoder it also passes 8-bit
   4:2:0 Main/High (MC_PLAY_UNTESTED = interlaced, handed over too), and
   e->rebuild (BuildReferenceMovie) then runs the B-frame rewrite.
   *checked: 1 Baseline/QT-native (0.4: or Main/High), -1 refused,
   0 never got as far as the moov. refusal[] gets the refusal text.
   Returns 0 played, 1 go on with the next candidate, -1 failed (Fail
   already called). */
static int TryCand(McfEnv *e, const char *id, McMeta *m, const McCand *c,
                   char urls[][MC_URLMAX], char *refusal, int *sawHttps,
                   int *checked, int g3probe)
{
    char           why[200], name[64];
    char           dl[MC_URLMAX + 16];
    int            r, ui, nurl;
    unsigned char *moov, *out;
    long           moovLen, outLen;
    unsigned long  big;
    McAvcInfo      info;
    McfProbe       probe;
    McfJob         job;
    FSSpec         spec;

    *checked = 0;
    L(e, "Trying %.200s (%s, %lu KB)\n", c->name, c->format,
      c->size / 1024UL);
    if (c->width > MC_MAX_WIDTH || c->height > MC_MAX_HEIGHT)
    {
        sprintf(refusal, "This video is %dx%d; the limit is %dx%d.",
                c->width, c->height, MC_MAX_WIDTH, MC_MAX_HEIGHT);
        *checked = -1;
        return 1;
    }
    nurl = mc_attempt_urls(id, m, c, urls, MC_MAXATTEMPT);
    for (ui = 0; ui < nurl; ui++)
    {
        r = mcf_fetch_moov(e, urls[ui], &moov, &moovLen, &probe);
        if (probe.saw_https) *sawHttps = 1;
        if (r == MCF_ERR_CANCEL) return Fail(e, "Stopped.");
        if (r == MCF_ERR_NOMEM) return Fail(e, e->message);
        if (r == MCF_ERR_NET && IsCertErr(e))
            return Fail(e, mcf_explain(e, r));
        if (r == MCF_ERR_NET) mcf_explain(e, r);
        if (r != MCF_OK) continue;

        /* can our decoder play it? (unchanged rule) */
        r = mc_avc_verdict(&info, mc_avc_profile(moov, moovLen, &info),
                           why, sizeof why);
        L(e, "  %s\n", why);
        if (r < 0)
        {
            e->release(moov);
            strcpy(refusal, why);
            *checked = -1;
            return 1;
        }
        *checked = 1;
        /* (0.6.1) G3: a heavy file may have a lighter sibling: ask first */
        if (g3probe && mc_avc_last_note() != MC_FILE_NOTE_NONE)
        {
            e->release(moov);
            return 2;
        }

        e->mode = mcf_decide(e->force_download, &probe,
                             e->proxy_url != NULL);
        if (e->mode == MCF_MODE_PROXY)
        {
            /* (0.5s) QuickTime fetches http://127.0.0.1:PORT/a/host/path;
               Media Center fetches it over https (redirects followed in
               the proxy). The dref keeps the ORIGINAL archive.org path,
               never the storage node (session 15). */
            static char dref[MC_URLMAX + 80];
            unsigned long fsize = (c->size != 0 && c->size != 0xFFFFFFFFUL)
                                ? c->size : probe.file_size;
            mcf_to_https(urls[ui], dl);
            e->movie_name(urls[ui], name);
            r = e->proxy_url(e->ref, dl, fsize, moov, moovLen,
                             probe.moov_end, dref, name);
            if (r == -2)
            {   /* (0.5t) Stop / Cmd-period while pre-buffering */
                e->release(moov);
                return Fail(e, "Stopped.");
            }
            if (r == 0)
            {
                L(e, "A server on the way insists on https: Media Center "
                  "streams it to QuickTime Player through its local proxy "
                  "(%.300s).\n", dref);
                out = e->rebuild(moov, moovLen, dref, &outLen);
                e->release(moov);
                if (out == NULL)
                    return Fail(e, "Could not build the reference movie.");
                r = e->open_ref(e->ref, name, out, outLen);
                e->release(out);
                if (r != 0) return Fail(e, "Could not save or open the "
                                        "reference movie.");
                if (e->proxy_opened != NULL) e->proxy_opened(e->ref, name);
                return 0;
            }
            if (r == -3)
            {   /* (0.6.1) the measured network is slower than the film */
                L(e, "The network is slower than this film needs: "
                  "downloading it first instead of streaming.\n");
                if (e->g3note[0] == 0)
                    Copy(e->g3note, "Network too slow to stream this "
                         "film: downloaded first.", sizeof e->g3note);
            }
            else
                L(e, "The local streaming proxy is not available.\n");
            e->mode = MCF_MODE_DOWNLOAD;
        }
        if (e->mode == MCF_MODE_STREAM)
        {
            L(e, "Every hop was plain http: QuickTime Player will fetch "
              "the media itself (streaming).\n");
            out = e->rebuild(moov, moovLen, urls[ui], &outLen);
            e->release(moov);
            if (out == NULL)
                return Fail(e, "Could not build the reference movie.");
            e->movie_name(probe.final_url, name);
            r = e->open_ref(e->ref, name, out, outLen);
            e->release(out);
            if (r != 0) return Fail(e, "Could not save or open the "
                                    "reference movie.");
            return 0;
        }

        if (e->force_download)
            L(e, "\"Download first\" is on: fetching the whole film over "
              "https, then playing it from the disk.\n");
        else
            L(e, "A server on the way insists on https, which QuickTime "
              "6 cannot fetch: downloading the whole film over https "
              "first.\n");

        /* (0.3) a big download needs a yes first */
        job.expect_size = (c->size != 0 && c->size != 0xFFFFFFFFUL)
                        ? c->size : probe.file_size;
        big = e->big_bytes ? e->big_bytes : MCF_BIG_BYTES;
        if (job.expect_size > big && !e->confirm_big)
        {
            unsigned long kb = job.expect_size / 1024UL;
            unsigned long min = (kb / MCF_RATE_KBS + 59UL) / 60UL;
            e->release(moov);
            e->mode = -1;
            e->need_confirm = 1;
            e->confirm_size = job.expect_size;
            L(e, "CONFIRM needed: %lu.%lu MB, about %lu min at %lu KB/s\n",
              kb / 1024UL, ((kb % 1024UL) * 10UL) / 1024UL, min,
              MCF_RATE_KBS);
            sprintf(why, "This film is %lu MB: about %lu minutes to "
                    "download (at %lu KB/s). %s", kb / 1024UL, min,
                    MCF_RATE_KBS, e->confirm_how != NULL ? e->confirm_how
                    : "Confirm to download it.");
            return Fail(e, why);
        }

        out = e->rebuild(moov, moovLen, NULL, &outLen);
        if (out == NULL)
        {
            e->release(moov);
            return Fail(e, "Could not build the local movie header.");
        }
        mcf_to_https(urls[ui], dl);
        {
            unsigned long keep = job.expect_size;
            memset(&job, 0, sizeof job);
            job.expect_size = keep;
        }
        job.url = dl;
        job.moov = moov;
        job.moovLen = moovLen;
        job.tail = out;
        job.tailLen = outLen;
        e->movie_name(urls[ui], name);
        job.name = name;
        r = mcf_download(e, &job, &spec, &e->reused);
        e->release(out);
        e->release(moov);
        if (r == MCF_OK)
        {
            if (e->open_local(e->ref, &spec) != 0)
                return Fail(e, "Could not open the downloaded movie.");
            return 0;
        }
        mcf_explain(e, r);
        if (r == MCF_ERR_NET && !IsCertErr(e))
        {
            L(e, "  %s Trying the next server.\n", e->message);
            continue;
        }
        return Fail(e, e->message);
    }
    return 1;
}

static void Reset(McfEnv *e)
{
    e->message[0] = 0;
    e->mode = -1;
    e->reused = 0;
    e->dl_bytes = e->dl_ticks = 0;
    e->need_confirm = 0;
    e->confirm_size = 0;
    e->g3note[0] = 0;                                     /* (0.6.1) */
}

/* (0.6.1) status line only */
static void Status(McfEnv *e, const char *line)
{
    if (e->status != NULL) e->status(e->ref, line);
}

/* metadata of a hit -> m. 0 ok, -1 Fail()ed */
static int OpenMeta(McfEnv *e, const McHit *hit, McMeta *m)
{
    int r;
    L(e, "\nItem: %.96s (%.104s)\n", hit->title, hit->identifier);
    r = mcf_meta(e, hit->identifier, m);
    if (r == -11) return Fail(e, "Stopped.");
    if (r == -10) return Fail(e, e->message);
    if (r == -2) return Fail(e, "archive.org has no item by that name.");
    if (r == -3) return Fail(e, "This item has been withdrawn (dark).");
    if (r != 0) return Fail(e, "Could not read this item's file list.");
    return 0;
}

void mcf_log_file(McfEnv *e, const McMeta *m, int fi)
{
    const McFile *f = &m->files[fi];
    char label[MC_TITLEMAX], mb[16];
    mc_file_label(f, label, (long)sizeof label);
    mc_file_mb(f, mb);
    L(e, "FILE %d %s mb=%s fmt=%s name=%s label=%s\n", fi + 1,
      mc_file_token(f), mb, f->format, f->name, label);
}

void mcf_log_files(McfEnv *e, const char *id, const McMeta *m)
{
    int i;
    L(e, "FILES %d %s%s\n", m->nfiles, id,
      m->droppedFiles ? " (more files did not fit)" : "");
    for (i = 0; i < m->nfiles; i++) mcf_log_file(e, m, i);
}

int mcf_open_item(McfEnv *e, const McHit *hit, McMeta *m)
{
    Reset(e);
    if (OpenMeta(e, hit, m) != 0) return -1;
    mcf_log_files(e, hit->identifier, m);
    if (m->nfiles == 0)
        return Fail(e, "No MP4/MOV video in this item (only other "
                    "formats).");
    return 0;
}

int mcf_play_file(McfEnv *e, const char *id, McMeta *m, int fi,
                  char urls[][MC_URLMAX])
{
    char    refusal[220];
    int     r, sawHttps = 0, checked = 0, alt;
    McCand  c;
    McFile *f;
    int     confirm = e->confirm_big;

    Reset(e);
    e->confirm_big = confirm;
    refusal[0] = 0;
    if (fi < 0 || fi >= m->nfiles)
        return Fail(e, "There is no such file in this item.");
    f = &m->files[fi];
    mc_file_cand(f, &c);
    /* (0.6.1) on a G3, a file over its decode budget with a lighter
       rendition of the same film (mc_g3_lighter) plays that one */
    alt = mc_cpu_g3() ? mc_g3_lighter(m, fi, mc_cpu_mhz()) : -1;
    r = TryCand(e, id, m, &c, urls, refusal, &sawHttps, &checked,
                alt >= 0);
    if (checked != 0)
    {
        f->state = checked > 0 ? MC_STATE_PLAYS : MC_STATE_NEWER;
        /* (0.6) the verdict's note: "may stutter" + orange dot;
           (0.6.1) G3: "too heavy" */
        f->note = checked > 0 ? mc_avc_last_note() : MC_FILE_NOTE_NONE;
        L(e, "MARK %d %s%s\n", fi + 1, mc_file_token(f),
          f->note == MC_FILE_NOTE_HEAVY ? " note:too-heavy"
          : f->note == MC_FILE_NOTE_STUTTER ? " note:may-stutter" : "");
    }
    if (r == 2)
    {
        McFile *g = &m->files[alt];
        int     cs = 0, heavy = f->note == MC_FILE_NOTE_HEAVY;
        McCand  ac;
        char    dim[24];
        if (g->width > 0 && g->height > 0)
            sprintf(dim, "%dx%d ", g->width, g->height);
        else if (strcmp(g->format, "512Kb MPEG4") == 0)
            sprintf(dim, "%dx%d ", MC_G3_MPEG4_W, MC_G3_MPEG4_H);
        else
            dim[0] = 0;
        L(e, "G3 %ld MHz: file %d %s; playing file %d (%.40s) instead.\n",
          mc_cpu_mhz(), fi + 1, heavy ? "is too heavy" : "may stutter",
          alt + 1, g->format);
        sprintf(e->g3note, "G3: playing the %s%s version.", dim,
                strcmp(g->format, "512Kb MPEG4") == 0 ? "MPEG-4" : "H.264");
        Status(e, e->g3note);
        refusal[0] = 0;
        e->message[0] = 0;
        mc_file_cand(g, &ac);
        r = TryCand(e, id, m, &ac, urls, refusal, &sawHttps, &cs, 0);
        if (cs != 0)
        {
            g->state = cs > 0 ? MC_STATE_PLAYS : MC_STATE_NEWER;
            g->note = cs > 0 ? mc_avc_last_note() : MC_FILE_NOTE_NONE;
            L(e, "MARK %d %s\n", alt + 1, mc_file_token(g));
        }
        if (r != 0 && !e->need_confirm
            && !(r == -1 && strcmp(e->message, "Stopped.") == 0))
        {   /* the lighter one failed (refused, no server, or a hard
               error such as TLS / rebuild / open_ref): the chosen file
               after all. Not after the user's Stop, nor while a big
               download waits for the user's confirm. (0.6.1 F1) */
            L(e, "G3: the lighter file did not play; trying file %d.\n",
              fi + 1);
            e->g3note[0] = 0;
            refusal[0] = 0;
            e->message[0] = 0;
            r = TryCand(e, id, m, &c, urls, refusal, &sawHttps, &checked,
                        0);
        }
        else if (r != 0)
            e->g3note[0] = 0;
    }
    else if (r == 0 && mc_cpu_g3() && f->note == MC_FILE_NOTE_HEAVY
             && e->g3note[0] == 0)   /* (F1) keep "Network too slow ..." */
        Copy(e->g3note, "G3: too heavy for this Mac; expect a slideshow.",
             sizeof e->g3note);
    else if (r == 0 && mc_cpu_g3() && f->note == MC_FILE_NOTE_STUTTER
             && e->g3note[0] == 0)
        Copy(e->g3note, "G3: near this Mac's limit; it may stutter.",
             sizeof e->g3note);
    if (r == 0) return 0;
    if (r < 0) return -1;
    /* r == 1: refused, or no server answered */
    if (refusal[0] != 0) return Fail(e, refusal);
    if (sawHttps && e->message[0] == 0)
        return Fail(e, "This item needs HTTPS and no server delivered it.");
    if (e->message[0] != 0) return Fail(e, e->message);
    return Fail(e, "Could not reach any server for this item.");
}

int mcf_hook_run(McfEnv *e, const McHook *h, McSearch *s, McMeta *m,
                 char urls[][MC_URLMAX])
{
    int n;

    e->force_download = h->download;
    e->insecure = h->insecure;
    e->confirm_big = h->confirm;
    if (e->confirm_how == NULL)
        e->confirm_how = "Add a line \"confirm\" to the query file to "
                         "allow it.";
    L(e, "HOOK query=%s%s%s%s%s", h->query, h->download ? " +download" : "",
      h->insecure ? " +insecure" : "", h->confirm ? " +confirm" : "",
      h->list ? " +list" : "");
    if (h->file > 0) L(e, " +file %d", h->file);
    L(e, "\n");
    n = mcf_search(e, h->query, 1, s);
    if (n <= 0)
    {
        if (n == 0) Copy(e->message, "Nothing found.", sizeof e->message);
        return Fail(e, e->message[0] ? e->message : "Search failed.");
    }
    if (!h->list && h->file <= 0)
        return mcf_play_item(e, &s->hits[0], m, urls);   /* 0.2 way */
    if (mcf_open_item(e, &s->hits[0], m) != 0) return -1;
    if (h->list && h->file <= 0) return 0;
    if (h->file > m->nfiles)
    {
        char t[80];
        sprintf(t, "There is no file %d: this item lists %d.", h->file,
                m->nfiles);
        return Fail(e, t);
    }
    return mcf_play_file(e, s->hits[0].identifier, m, h->file - 1, urls);
}

int mcf_play_item(McfEnv *e, const McHit *hit, McMeta *m,
                  char urls[][MC_URLMAX])
{
    char refusal[220];
    int  r, ci, sawHttps = 0, checked;
    int  confirm = e->confirm_big;

    refusal[0] = 0;
    Reset(e);
    e->confirm_big = confirm;
    if (OpenMeta(e, hit, m) != 0) return -1;
    if (m->ncand == 0) return Fail(e, "No MP4 in this item.");
    L(e, "  %d MP4 candidate(s); %d server(s).\n", m->ncand, m->nsrv);

    for (ci = 0; ci < m->ncand; ci++)
    {
        r = TryCand(e, hit->identifier, m, &m->cand[ci], urls, refusal,
                    &sawHttps, &checked, 0);
        if (r == 0) return 0;
        if (r < 0) return -1;
    }

    if (refusal[0] != 0) return Fail(e, refusal);
    if (sawHttps && e->message[0] == 0) return Fail(e, "This item needs HTTPS and no server "
                              "delivered it.");
    if (e->message[0] != 0) return Fail(e, e->message);
    return Fail(e, "Could not reach any server for this item.");
}
