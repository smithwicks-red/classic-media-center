/*
 * https_client.c -- see https_client.h. Strict C89: no // comments,
 * declarations at block top, no long long, no C99 library calls.
 *
 * Wire text is written with octal escapes (\015\012) rather than
 * "\r\n" so the bytes are right whatever CodeWarrior's newline mapping
 * ("Map newlines to CR") is set to.
 */
#include <string.h>
#include <stdlib.h>

#include <MacTypes.h>
#include <Memory.h>
#include <Events.h>
#include <OpenTransport.h>
#include <OpenTptInternet.h>

#include "ostls_async.h"
#include "https_client.h"

/* Required by ostls_async.c (see ostls_classic_shim.h). Never read on
   classic: the shim's macros discard it. */
OTClientContextPtr g_ostls_ot_context = NULL;

#define HC_CRLF "\015\012"
#define HC_HDR_CAP   8192UL
#define HC_REQ_CAP   2048UL
#define HC_IO_CAP    4096UL
#define HC_URL_CAP   1024UL
#define HC_DEFAULT_UA "HTTPSGet/0.1 (Macintosh; PPC Mac OS 9; macTLS)"
#define HC_DEFAULT_TIMEOUT (30UL * 60UL)

/* ================================================================== */
/* small string helpers (no sprintf into fixed buffers)               */
/* ================================================================== */

static int hc_lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

static int hc_ncaseeq(const char *a, const char *b, unsigned long n)
{
    unsigned long i;
    for (i = 0; i < n; i++) {
        if (hc_lower((unsigned char)a[i]) != hc_lower((unsigned char)b[i]))
            return 0;
        if (a[i] == 0) return 1;
    }
    return 1;
}

/* Append s to buf (capacity cap incl. NUL). Returns 0, or -1 if it did
   not fit (buf then left unchanged). */
static int hc_cat(char *buf, unsigned long cap, const char *s)
{
    unsigned long have, add;
    have = (unsigned long)strlen(buf);
    add = (unsigned long)strlen(s);
    if (have + add + 1 > cap) return -1;
    memcpy(buf + have, s, add + 1);
    return 0;
}

static void hc_utoa(unsigned long v, char *out)
{
    char tmp[12];
    int n, i;
    n = 0;
    do {
        tmp[n++] = (char)('0' + (v % 10UL));
        v /= 10UL;
    } while (v != 0 && n < 11);
    for (i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
    out[n] = 0;
}

static void hc_copy(char *dst, const char *src, unsigned long cap)
{
    unsigned long n;
    n = (unsigned long)strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}

/* ================================================================== */
/* URL handling                                                       */
/* ================================================================== */

int HC_ParseURL(const char *url, int *is_https, char *host,
                unsigned short *port, char *path)
{
    const char *p, *h, *hend, *pend;
    unsigned long hl, pl, v;

    while (*url == ' ' || *url == '\t') url++;
    if (hc_ncaseeq(url, "https://", 8)) {
        *is_https = 1; *port = 443; p = url + 8;
    } else if (hc_ncaseeq(url, "http://", 7)) {
        *is_https = 0; *port = 80; p = url + 7;
    } else {
        return HC_ERR_URL;
    }
    h = p;
    while (*p && *p != '/' && *p != '?' && *p != '#' && *p != ':') {
        if (*p == '@' || *p == '[' || *p == ' ') return HC_ERR_URL;
        p++;
    }
    hend = p;
    hl = (unsigned long)(hend - h);
    if (hl == 0 || hl > 255) return HC_ERR_URL;
    memcpy(host, h, hl);
    host[hl] = 0;
    if (*p == ':') {
        p++;
        v = 0;
        if (*p < '0' || *p > '9') return HC_ERR_URL;
        while (*p >= '0' && *p <= '9') {
            v = v * 10UL + (unsigned long)(*p - '0');
            if (v > 65535UL) return HC_ERR_URL;
            p++;
        }
        if (v == 0) return HC_ERR_URL;
        *port = (unsigned short)v;
        if (*p && *p != '/' && *p != '?' && *p != '#') return HC_ERR_URL;
    }
    /* path + query, fragment dropped */
    pend = p;
    while (*pend && *pend != '#' && *pend != ' ' && *pend != '\015'
           && *pend != '\012') pend++;
    pl = (unsigned long)(pend - p);
    if (pl == 0) {
        path[0] = '/'; path[1] = 0;
    } else if (*p == '?') {
        if (pl + 2 > HC_URL_CAP) return HC_ERR_URL;
        path[0] = '/';
        memcpy(path + 1, p, pl);
        path[pl + 1] = 0;
    } else {
        if (pl + 1 > HC_URL_CAP) return HC_ERR_URL;
        memcpy(path, p, pl);
        path[pl] = 0;
    }
    return HC_OK;
}

int HC_ResolveLocation(const char *base, const char *loc, char *out,
                       unsigned long cap)
{
    const char *s, *auth_end, *q;
    unsigned long n;

    while (*loc == ' ' || *loc == '\t') loc++;
    if (*loc == 0) return HC_ERR_URL;
    out[0] = 0;
    if (hc_ncaseeq(loc, "http://", 7) || hc_ncaseeq(loc, "https://", 8)) {
        if ((unsigned long)strlen(loc) + 1 > cap) return HC_ERR_URL;
        hc_copy(out, loc, cap);
        return HC_OK;
    }
    /* find end of scheme://authority in base */
    s = strstr(base, "://");
    if (s == NULL) return HC_ERR_URL;
    if (loc[0] == '/' && loc[1] == '/') {
        /* scheme-relative */
        n = (unsigned long)(s + 1 - base);          /* "https:" */
        if (n + strlen(loc) + 1 > cap) return HC_ERR_URL;
        memcpy(out, base, n);
        out[n] = 0;
        return hc_cat(out, cap, loc) == 0 ? HC_OK : HC_ERR_URL;
    }
    auth_end = s + 3;
    while (*auth_end && *auth_end != '/' && *auth_end != '?'
           && *auth_end != '#') auth_end++;
    if (loc[0] == '/') {
        n = (unsigned long)(auth_end - base);
    } else if (loc[0] == '?') {
        q = auth_end;
        while (*q && *q != '?' && *q != '#') q++;
        n = (unsigned long)(q - base);
    } else {
        /* relative path: keep base up to and including the last '/' of
           its path (before any query) */
        q = auth_end;
        n = (unsigned long)(auth_end - base);
        while (*q && *q != '?' && *q != '#') {
            if (*q == '/') n = (unsigned long)(q + 1 - base);
            q++;
        }
        if (n == (unsigned long)(auth_end - base)) {
            /* base had no path: add one */
            if (n + 2 + strlen(loc) > cap) return HC_ERR_URL;
            memcpy(out, base, n);
            out[n] = '/';
            out[n + 1] = 0;
            return hc_cat(out, cap, loc) == 0 ? HC_OK : HC_ERR_URL;
        }
    }
    if (n + strlen(loc) + 1 > cap) return HC_ERR_URL;
    memcpy(out, base, n);
    out[n] = 0;
    return hc_cat(out, cap, loc) == 0 ? HC_OK : HC_ERR_URL;
}

/* ================================================================== */
/* transports                                                         */
/* ================================================================== */

/* transport status from step() */
enum { HC_T_CONNECTING = 0, HC_T_HANDSHAKING, HC_T_OPEN, HC_T_EOF,
       HC_T_FAILED };

typedef struct HC_Conn HC_Conn;
typedef struct HC_Ops {
    int  (*start)(HC_Conn *c, const char *host, unsigned short port,
                  int insecure, unsigned long timeout);
    int  (*step)(HC_Conn *c, int *progress);   /* returns HC_T_* */
    int  (*write)(HC_Conn *c, const char *buf, unsigned long len,
                  unsigned long *done);       /* 0 or HC_ERR_* */
    int  (*read)(HC_Conn *c, char *buf, unsigned long cap,
                 unsigned long *got);         /* 0 or HC_ERR_* */
    void (*finish)(HC_Conn *c, HC_Result *res); /* fill err detail */
    void (*close)(HC_Conn *c);
} HC_Ops;

struct HC_Conn {
    const HC_Ops *ops;
    /* tls */
    OSTLSConnection *tls;
    /* plain OT */
    EndpointRef  ep;
    TEndpointInfo info;
    DNSAddress   dns;
    TCall        call;
    char         hostport[300];
    int          phase;           /* plain connect phase */
    unsigned long start_ticks;
    unsigned long timeout;
    long         ot_err;
    int          fail_code;       /* HC_ERR_* for plain */
    /* notifier flags (set at "interrupt time") */
    volatile int nf_open, nf_bind, nf_connect, nf_data, nf_ordrel,
                 nf_disconnect;
    volatile OTResult nf_result;
};

/* ------------------------------ TLS ------------------------------ */

static int tls_start(HC_Conn *c, const char *host, unsigned short port,
                     int insecure, unsigned long timeout)
{
    OSTLSConfig cfg;
    OSErr err;

    memset(&cfg, 0, sizeof cfg);
    cfg.host = host;
    cfg.port = port;
    cfg.server_name = host;
    cfg.connect_timeout_ticks = timeout;
    cfg.handshake_timeout_ticks = timeout;
    cfg.user_refcon = c;
    cfg.insecure_skip_verify = insecure;
    err = OSTLS_New(&c->tls, &cfg);
    if (err != noErr || c->tls == NULL) {
        c->tls = NULL;
        return err == (OSErr)kOSTLSAsync_NoMemory ? HC_ERR_NOMEM
                                                  : HC_ERR_URL;
    }
    err = OSTLS_Start(c->tls);
    if (err == (OSErr)kOSTLSAsync_ClockBefore2000) return HC_ERR_CLOCK;
    if (err != noErr) return HC_ERR_CONNECT;
    return HC_OK;
}

static int tls_step(HC_Conn *c, int *progress)
{
    OSTLSEvent ev;
    OSTLSState st;
    OSTLSDiagnostics d1, d2;

    OSTLS_GetDiagnostics(c->tls, &d1);
    (void)OSTLS_Pump(c->tls, 8, &ev);
    OSTLS_GetDiagnostics(c->tls, &d2);
    *progress = (ev != kOSTLSEventNone)
             || d1.ot_recv_bytes != d2.ot_recv_bytes
             || d1.ot_send_bytes != d2.ot_send_bytes
             || d1.state != d2.state;
    st = OSTLS_GetState(c->tls);
    switch (st) {
    case kOSTLSStateIdle:
    case kOSTLSStateConnecting:  return HC_T_CONNECTING;
    case kOSTLSStateHandshaking: return HC_T_HANDSHAKING;
    case kOSTLSStateOpen:        return HC_T_OPEN;
    case kOSTLSStateClosing:
    case kOSTLSStateClosed:      return HC_T_EOF;
    default:                     return HC_T_FAILED;
    }
}

static int tls_write(HC_Conn *c, const char *buf, unsigned long len,
                     unsigned long *done)
{
    UInt32 w;
    OSErr err;
    w = 0;
    err = OSTLS_Write(c->tls, buf, (UInt32)len, &w);
    *done = (unsigned long)w;
    return err == noErr ? HC_OK : HC_ERR_IO;
}

static int tls_read(HC_Conn *c, char *buf, unsigned long cap,
                    unsigned long *got)
{
    UInt32 r;
    OSErr err;
    r = 0;
    err = OSTLS_Read(c->tls, buf, (UInt32)cap, &r);
    *got = (unsigned long)r;
    return err == noErr ? HC_OK : HC_ERR_IO;
}

static void tls_finish(HC_Conn *c, HC_Result *res)
{
    OSTLSDiagnostics d;
    int oe;

    OSTLS_GetDiagnostics(c->tls, &d);
    oe = (int)OSTLS_GetLastError(c->tls);
    res->os_err = (long)oe;
    res->ot_err = (long)d.ot_err;
    res->br_err = d.br_err;
    res->cipher_suite = (unsigned int)OSTLS_GetCipherSuite(c->tls);
    if (res->cipher_suite != 0) {
        res->tls_version = (res->cipher_suite >= 0x1301U
                            && res->cipher_suite <= 0x1305U)
                           ? 0x0304 : 0x0303;
    }
    res->cert_err_ignored = OSTLS_GetIgnoredCertError(c->tls);
    if (d.state != kOSTLSStateFailed) return;
    switch (oe) {
    case kOSTLSAsync_ClockBefore2000:
        res->err = HC_ERR_CLOCK; break;
    case kOSTLSAsync_ConnectTimeout:
    case kOSTLSAsync_HandshakeTimeout:
        res->err = HC_ERR_TIMEOUT; break;
    case kOSTLSAsync_OTConfigFail:
    case kOSTLSAsync_OTOpenFail:
    case kOSTLSAsync_OTBindFail:
    case kOSTLSAsync_OTConnectFail:
    case kOSTLSAsync_OTNotifierFail:
        res->err = HC_ERR_CONNECT; break;
    case kOSTLSAsync_NoMemory:
        res->err = HC_ERR_NOMEM; break;
    case kOSTLSAsync_OTSndFail:
    case kOSTLSAsync_OTRcvFail:
        /* macTLS reports any T_DISCONNECT as OTSndFail, including a
           refused connect or failed DNS lookup: classify by phase. */
        res->err = res->t_handshake ? HC_ERR_IO
                 : res->t_connect ? HC_ERR_HANDSHAKE : HC_ERR_CONNECT;
        break;
    default:
        /* BearSSL X.509 errors are 32..63 */
        if (d.br_err >= 32 && d.br_err <= 63)
            res->err = HC_ERR_CERT;
        else
            res->err = res->t_handshake ? HC_ERR_TLS : HC_ERR_HANDSHAKE;
        break;
    }
}

static void tls_close(HC_Conn *c)
{
    if (c->tls != NULL) {
        OSTLS_Close(c->tls);
        OSTLS_Dispose(c->tls);
        c->tls = NULL;
    }
}

static const HC_Ops hc_tls_ops = {
    tls_start, tls_step, tls_write, tls_read, tls_finish, tls_close
};

/* ---------------------------- plain OT --------------------------- */

enum { PL_OPENING = 0, PL_BINDING, PL_CONNECTING, PL_OPEN, PL_EOF,
       PL_FAILED };

static pascal void plain_notifier(void *context, OTEventCode code,
                                  OTResult result, void *cookie)
{
    HC_Conn *c;
    c = (HC_Conn *)context;
    if (c == NULL) return;
    c->nf_result = result;
    switch (code) {
    case T_OPENCOMPLETE:
        c->ep = (EndpointRef)cookie;
        c->nf_open = 1;
        break;
    case T_BINDCOMPLETE: c->nf_bind = 1; break;
    case T_CONNECT:      c->nf_connect = 1; break;
    case T_DATA:         c->nf_data = 1; break;
    case T_ORDREL:       c->nf_ordrel = 1; break;
    case T_DISCONNECT:   c->nf_disconnect = 1; break;
    default: break;
    }
}

static OTNotifyUPP g_plain_upp = NULL;

static int plain_start(HC_Conn *c, const char *host, unsigned short port,
                       int insecure, unsigned long timeout)
{
    OSStatus err;
    char num[12];

    (void)insecure;
    c->hostport[0] = 0;
    if (hc_cat(c->hostport, sizeof c->hostport, host) != 0)
        return HC_ERR_URL;
    hc_utoa((unsigned long)port, num);
    (void)hc_cat(c->hostport, sizeof c->hostport, ":");
    (void)hc_cat(c->hostport, sizeof c->hostport, num);
    c->timeout = timeout;
    c->start_ticks = (unsigned long)TickCount();
    if (g_plain_upp == NULL) g_plain_upp = NewOTNotifyUPP(plain_notifier);
    c->phase = PL_OPENING;
    err = OTAsyncOpenEndpoint(OTCreateConfiguration(kTCPName), 0,
                              &c->info, g_plain_upp, c);
    if (err != noErr) {
        c->ot_err = (long)err;
        c->phase = PL_FAILED;
        c->fail_code = HC_ERR_CONNECT;
        return HC_ERR_CONNECT;
    }
    return HC_OK;
}

static int plain_fail(HC_Conn *c, int code, long ot_err)
{
    c->phase = PL_FAILED;
    c->fail_code = code;
    c->ot_err = ot_err;
    return HC_T_FAILED;
}

static int plain_step(HC_Conn *c, int *progress)
{
    OSStatus err;
    OTByteCount n;

    *progress = 0;
    if (c->phase == PL_FAILED) return HC_T_FAILED;
    if (c->phase == PL_EOF) return HC_T_EOF;
    if (c->phase == PL_OPEN) {
        if (c->nf_disconnect) {
            c->nf_disconnect = 0;
            (void)OTRcvDisconnect(c->ep, NULL);
            return plain_fail(c, HC_ERR_IO, (long)c->nf_result);
        }
        return HC_T_OPEN;
    }
    if ((unsigned long)TickCount() - c->start_ticks > c->timeout)
        return plain_fail(c, HC_ERR_TIMEOUT, 0);
    if (c->nf_disconnect)
        return plain_fail(c, HC_ERR_CONNECT, (long)c->nf_result);
    switch (c->phase) {
    case PL_OPENING:
        if (!c->nf_open) return HC_T_CONNECTING;
        c->nf_open = 0;
        if (c->nf_result != kOTNoError || c->ep == NULL)
            return plain_fail(c, HC_ERR_CONNECT, (long)c->nf_result);
        /* async (completion by notifier) + non-blocking Snd/Rcv */
        (void)OTSetAsynchronous(c->ep);
        (void)OTSetNonBlocking(c->ep);
        err = OTBind(c->ep, NULL, NULL);
        if (err != kOTNoError) return plain_fail(c, HC_ERR_CONNECT, (long)err);
        c->phase = PL_BINDING;
        *progress = 1;
        return HC_T_CONNECTING;
    case PL_BINDING:
        if (!c->nf_bind) return HC_T_CONNECTING;
        c->nf_bind = 0;
        if (c->nf_result != kOTNoError)
            return plain_fail(c, HC_ERR_CONNECT, (long)c->nf_result);
        memset(&c->call, 0, sizeof c->call);
        n = OTInitDNSAddress(&c->dns, c->hostport);
        c->call.addr.buf = (UInt8 *)&c->dns;
        c->call.addr.len = n;
        err = OTConnect(c->ep, &c->call, NULL);
        if (err != kOTNoError && err != kOTNoDataErr)
            return plain_fail(c, HC_ERR_CONNECT, (long)err);
        c->phase = PL_CONNECTING;
        *progress = 1;
        return HC_T_CONNECTING;
    case PL_CONNECTING:
        if (!c->nf_connect) return HC_T_CONNECTING;
        c->nf_connect = 0;
        if (c->nf_result != kOTNoError)
            return plain_fail(c, HC_ERR_CONNECT, (long)c->nf_result);
        err = OTRcvConnect(c->ep, NULL);
        if (err != kOTNoError)
            return plain_fail(c, HC_ERR_CONNECT, (long)err);
        c->phase = PL_OPEN;
        *progress = 1;
        return HC_T_OPEN;
    default:
        return HC_T_FAILED;
    }
}

static int plain_write(HC_Conn *c, const char *buf, unsigned long len,
                       unsigned long *done)
{
    OTResult r;
    *done = 0;
    if (c->phase != PL_OPEN) return HC_ERR_IO;
    r = OTSnd(c->ep, (void *)buf, (OTByteCount)len, 0);
    if (r >= 0) { *done = (unsigned long)r; return HC_OK; }
    if (r == kOTFlowErr) return HC_OK;
    if (r == kOTLookErr && OTLook(c->ep) == T_ORDREL) return HC_ERR_IO;
    c->ot_err = (long)r;
    return HC_ERR_IO;
}

static int plain_read(HC_Conn *c, char *buf, unsigned long cap,
                      unsigned long *got)
{
    OTResult r, look;
    OTFlags flags;
    *got = 0;
    if (c->phase == PL_EOF) return HC_OK;
    if (c->phase != PL_OPEN) return HC_ERR_IO;
    flags = 0;
    r = OTRcv(c->ep, buf, (OTByteCount)cap, &flags);
    if (r > 0) { *got = (unsigned long)r; return HC_OK; }
    if (r == kOTNoDataErr || r == 0) return HC_OK;
    if (r == kOTLookErr) {
        look = OTLook(c->ep);
        if (look == T_ORDREL) {
            (void)OTRcvOrderlyDisconnect(c->ep);
            c->phase = PL_EOF;
            return HC_OK;
        }
        if (look == T_DISCONNECT) {
            (void)OTRcvDisconnect(c->ep, NULL);
            c->ot_err = (long)look;
            c->phase = PL_FAILED;
            c->fail_code = HC_ERR_IO;
            return HC_ERR_IO;
        }
        return HC_OK;   /* some other event (T_GODATA...) */
    }
    c->ot_err = (long)r;
    return HC_ERR_IO;
}

static void plain_finish(HC_Conn *c, HC_Result *res)
{
    res->ot_err = c->ot_err;
    res->os_err = c->ot_err;
    if (c->phase == PL_FAILED && res->err == HC_OK) res->err = c->fail_code;
}

static void plain_close(HC_Conn *c)
{
    if (c->ep != NULL) {
        (void)OTSndDisconnect(c->ep, NULL);
        (void)OTCloseProvider(c->ep);
        c->ep = NULL;
    }
}

static const HC_Ops hc_plain_ops = {
    plain_start, plain_step, plain_write, plain_read, plain_finish,
    plain_close
};

/* ================================================================== */
/* HTTP                                                               */
/* ================================================================== */

typedef struct HC_Ctx {
    HC_Conn  conn;
    char     url[HC_URL_CAP];
    char     next_url[HC_URL_CAP];
    char     host[256];
    char     path[HC_URL_CAP];
    char     location[HC_URL_CAP];
    char     req[HC_REQ_CAP];
    char     hdr[HC_HDR_CAP + 1];
    char     io[HC_IO_CAP];
    unsigned long hdr_len;
    /* body decoder */
    int      body_mode;          /* 0 until close, 1 length, 2 chunked */
    unsigned long remaining;     /* length / chunk bytes left */
    int      ch_state;           /* chunked sub-state */
    unsigned long ch_size;
    int      ch_digits;
    int      body_done;
    int      deliver;            /* pass body to callback? */
} HC_Ctx;

enum { CH_SIZE = 0, CH_EXT, CH_SIZE_LF, CH_DATA, CH_DATA_CR, CH_DATA_LF,
       CH_TRAILER, CH_TRAILER_LF, CH_DONE };

static int hc_emit(HC_Ctx *x, const HC_Request *req, HC_Result *res,
                   const char *p, unsigned long n)
{
    if (n == 0) return HC_OK;
    if (!x->deliver) return HC_OK;
    res->body_bytes += n;
    if (req->body != NULL && req->body(req->ref,
                                       (const unsigned char *)p, n) != 0)
        return HC_ERR_ABORTED;
    return HC_OK;
}

static int hex_val(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = hc_lower(c);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* Feed body bytes through the decoder. */
static int hc_body(HC_Ctx *x, const HC_Request *req, HC_Result *res,
                   const char *p, unsigned long n)
{
    unsigned long take;
    int e, c, h;

    if (x->body_mode == 0) return hc_emit(x, req, res, p, n);
    if (x->body_mode == 1) {
        take = n < x->remaining ? n : x->remaining;
        x->remaining -= take;
        if (x->remaining == 0) x->body_done = 1;
        return hc_emit(x, req, res, p, take);
    }
    while (n > 0 && x->ch_state != CH_DONE) {
        c = (unsigned char)*p;
        switch (x->ch_state) {
        case CH_SIZE:
            h = hex_val(c);
            if (h >= 0) {
                if (x->ch_digits >= 7) return HC_ERR_PROTOCOL; /* >256MB */
                x->ch_size = x->ch_size * 16UL + (unsigned long)h;
                x->ch_digits++;
            } else if (c == ';' || c == ' ' || c == '\t') {
                if (x->ch_digits == 0) return HC_ERR_PROTOCOL;
                x->ch_state = CH_EXT;
            } else if (c == '\015') {
                if (x->ch_digits == 0) return HC_ERR_PROTOCOL;
                x->ch_state = CH_SIZE_LF;
            } else if (c == '\012') {
                if (x->ch_digits == 0) return HC_ERR_PROTOCOL;
                x->ch_state = x->ch_size ? CH_DATA : CH_TRAILER;
                x->remaining = x->ch_size;
            } else {
                return HC_ERR_PROTOCOL;
            }
            p++; n--;
            break;
        case CH_EXT:
            if (c == '\012') {
                x->ch_state = x->ch_size ? CH_DATA : CH_TRAILER;
                x->remaining = x->ch_size;
            } else if (c == '\015') {
                x->ch_state = CH_SIZE_LF;
            }
            p++; n--;
            break;
        case CH_SIZE_LF:
            if (c != '\012') return HC_ERR_PROTOCOL;
            x->ch_state = x->ch_size ? CH_DATA : CH_TRAILER;
            x->remaining = x->ch_size;
            p++; n--;
            break;
        case CH_DATA:
            take = n < x->remaining ? n : x->remaining;
            e = hc_emit(x, req, res, p, take);
            if (e != HC_OK) return e;
            p += take; n -= take;
            x->remaining -= take;
            if (x->remaining == 0) x->ch_state = CH_DATA_CR;
            break;
        case CH_DATA_CR:
            if (c == '\015') { x->ch_state = CH_DATA_LF; p++; n--; break; }
            if (c != '\012') return HC_ERR_PROTOCOL;
            /* bare LF tolerated */
            /* fall through */
        case CH_DATA_LF:
            if (c != '\012') return HC_ERR_PROTOCOL;
            x->ch_state = CH_SIZE;
            x->ch_size = 0;
            x->ch_digits = 0;
            p++; n--;
            break;
        case CH_TRAILER:
            /* at line start: empty line ends the message */
            if (c == '\015') { x->ch_state = CH_TRAILER_LF; }
            else if (c == '\012') { x->ch_state = CH_DONE; }
            else { x->ch_state = CH_EXT + 100; }   /* skip trailer line */
            p++; n--;
            break;
        case CH_TRAILER_LF:
            if (c != '\012') return HC_ERR_PROTOCOL;
            x->ch_state = CH_DONE;
            p++; n--;
            break;
        default:   /* inside a trailer header line */
            if (c == '\012') x->ch_state = CH_TRAILER;
            p++; n--;
            break;
        }
    }
    if (x->ch_state == CH_DONE) x->body_done = 1;
    return HC_OK;
}

/* Find the end of the header block. Returns offset just past it, or 0. */
static unsigned long hc_hdr_end(const char *h, unsigned long n)
{
    unsigned long i;
    for (i = 0; i + 1 < n; i++) {
        if (h[i] == '\012') {
            if (h[i + 1] == '\012') return i + 2;
            if (i + 2 < n && h[i + 1] == '\015' && h[i + 2] == '\012')
                return i + 3;
        }
    }
    return 0;
}

/* Copy header value of `name` (case-insensitive) into out. 1 if found. */
static int hc_header(const char *h, const char *name, char *out,
                     unsigned long cap)
{
    const char *line, *v, *e;
    unsigned long nl, vl;

    nl = (unsigned long)strlen(name);
    line = h;
    while (*line) {
        if (hc_ncaseeq(line, name, nl) && line[nl] == ':') {
            v = line + nl + 1;
            while (*v == ' ' || *v == '\t') v++;
            e = v;
            while (*e && *e != '\015' && *e != '\012') e++;
            while (e > v && (e[-1] == ' ' || e[-1] == '\t')) e--;
            vl = (unsigned long)(e - v);
            if (vl >= cap) vl = cap - 1;
            memcpy(out, v, vl);
            out[vl] = 0;
            return 1;
        }
        while (*line && *line != '\012') line++;
        if (*line) line++;
    }
    return 0;
}

static int hc_parse_ulong(const char *s, unsigned long *out)
{
    unsigned long v, d;
    if (*s < '0' || *s > '9') return 0;
    v = 0;
    while (*s >= '0' && *s <= '9') {
        d = (unsigned long)(*s - '0');
        if (v > (0xFFFFFFFFUL - d) / 10UL) return 0;
        v = v * 10UL + d;
        s++;
    }
    *out = v;
    return 1;
}

static int hc_build_request(HC_Ctx *x, const HC_Request *req, int https,
                            unsigned short port)
{
    char num[12];
    char *r;
    unsigned long cap;
    int bad;

    r = x->req;
    cap = HC_REQ_CAP;
    r[0] = 0;
    bad = 0;
    bad |= hc_cat(r, cap, "GET ");
    bad |= hc_cat(r, cap, x->path);
    bad |= hc_cat(r, cap, " HTTP/1.1" HC_CRLF "Host: ");
    bad |= hc_cat(r, cap, x->host);
    if ((https && port != 443) || (!https && port != 80)) {
        hc_utoa((unsigned long)port, num);
        bad |= hc_cat(r, cap, ":");
        bad |= hc_cat(r, cap, num);
    }
    bad |= hc_cat(r, cap, HC_CRLF "User-Agent: ");
    bad |= hc_cat(r, cap, req->user_agent ? req->user_agent : HC_DEFAULT_UA);
    bad |= hc_cat(r, cap, HC_CRLF "Accept: */*" HC_CRLF
                          "Accept-Encoding: identity" HC_CRLF);
    if (req->use_range) {
        hc_utoa(req->range_start, num);
        bad |= hc_cat(r, cap, "Range: bytes=");
        bad |= hc_cat(r, cap, num);
        bad |= hc_cat(r, cap, "-");
        if (req->range_end != 0xFFFFFFFFUL) {
            hc_utoa(req->range_end, num);
            bad |= hc_cat(r, cap, num);
        }
        bad |= hc_cat(r, cap, HC_CRLF);
    }
    bad |= hc_cat(r, cap, "Connection: close" HC_CRLF HC_CRLF);
    return bad ? HC_ERR_URL : HC_OK;
}

/* Process the complete header block in x->hdr[0..end). Returns HC_OK
   and sets *redirect when a Location should be followed. */
static int hc_process_headers(HC_Ctx *x, HC_Result *res, unsigned long end,
                              int *redirect)
{
    char save, val[64];
    const char *s;
    unsigned long v;

    *redirect = 0;
    save = x->hdr[end];
    x->hdr[end] = 0;
    s = x->hdr;
    if (!hc_ncaseeq(s, "HTTP/1.", 7) || s[8] != ' ') {
        x->hdr[end] = save;
        return HC_ERR_PROTOCOL;
    }
    if (!hc_parse_ulong(s + 9, &v) || v < 100 || v > 999) {
        x->hdr[end] = save;
        return HC_ERR_PROTOCOL;
    }
    res->http_status = (int)v;
    res->content_type[0] = 0;
    res->content_range[0] = 0;
    (void)hc_header(s, "Content-Type", res->content_type,
                    sizeof res->content_type);
    (void)hc_header(s, "Content-Range", res->content_range,
                    sizeof res->content_range);
    res->chunked = 0;
    res->has_length = 0;
    res->content_length = 0;
    x->body_mode = 0;
    if (hc_header(s, "Transfer-Encoding", val, sizeof val)) {
        /* last coding must be chunked for us to frame it */
        unsigned long L = (unsigned long)strlen(val);
        if (L >= 7 && hc_ncaseeq(val + L - 7, "chunked", 7)) {
            res->chunked = 1;
            x->body_mode = 2;
            x->ch_state = CH_SIZE;
            x->ch_size = 0;
            x->ch_digits = 0;
        }
    }
    if (!res->chunked && hc_header(s, "Content-Length", val, sizeof val)) {
        if (!hc_parse_ulong(val, &v)) { x->hdr[end] = save; return HC_ERR_PROTOCOL; }
        res->has_length = 1;
        res->content_length = v;
        x->body_mode = 1;
        x->remaining = v;
        if (v == 0) x->body_done = 1;
    }
    if (res->http_status == 204 || res->http_status == 304)
        x->body_done = 1;
    x->location[0] = 0;
    if ((res->http_status == 301 || res->http_status == 302
         || res->http_status == 303 || res->http_status == 307
         || res->http_status == 308)
        && hc_header(s, "Location", x->location, sizeof x->location))
        *redirect = 1;
    x->deliver = (res->http_status >= 200 && res->http_status < 300);
    x->hdr[end] = save;
    return HC_OK;
}

/* One hop. Returns HC_OK, or HC_ERR_*. *redirect set if we should go on
   to x->next_url. */
static int hc_hop(HC_Ctx *x, const HC_Request *req, HC_Result *res,
                  int *redirect)
{
    int https, st, progress, e, sent_all, have_hdr;
    unsigned short port;
    unsigned long t0, last, sent, done, got, end, timeout, now;

    *redirect = 0;
    e = HC_ParseURL(x->url, &https, x->host, &port, x->path);
    if (e != HC_OK) return e;
    e = hc_build_request(x, req, https, port);
    if (e != HC_OK) return e;
    timeout = req->timeout_ticks ? req->timeout_ticks : HC_DEFAULT_TIMEOUT;

    memset(&x->conn, 0, sizeof x->conn);
    x->conn.ops = https ? &hc_tls_ops : &hc_plain_ops;
    x->hdr_len = 0;
    x->body_done = 0;
    x->deliver = 0;
    res->t_connect = res->t_handshake = res->t_first_byte = 0;
    res->http_status = 0;
    res->tls_version = 0;
    res->cipher_suite = 0;
    res->cert_err_ignored = -1;
    res->os_err = res->ot_err = 0;
    res->br_err = 0;
    res->err = HC_OK;
    t0 = (unsigned long)TickCount();
    last = t0;
    sent = 0;
    sent_all = 0;
    have_hdr = 0;

    e = x->conn.ops->start(&x->conn, x->host, port, req->insecure, timeout);
    if (e != HC_OK) {
        if (x->conn.tls != NULL || x->conn.ep != NULL) {
            x->conn.ops->finish(&x->conn, res);
            if (res->err != HC_OK) e = res->err;
        }
        x->conn.ops->close(&x->conn);
        return e;
    }
    e = HC_OK;
    for (;;) {
        progress = 0;
        st = x->conn.ops->step(&x->conn, &progress);
        now = (unsigned long)TickCount();
        if (st == HC_T_FAILED) {
            x->conn.ops->finish(&x->conn, res);
            e = res->err != HC_OK ? res->err : HC_ERR_IO;
            if (have_hdr && e == HC_ERR_TLS && !x->body_done)
                e = HC_ERR_TRUNCATED;
            break;
        }
        if (st >= HC_T_HANDSHAKING && res->t_connect == 0)
            res->t_connect = (now - t0) ? (now - t0) : 1;
        if (st >= HC_T_OPEN && res->t_handshake == 0) {
            if (res->t_connect == 0) res->t_connect = (now - t0) ? (now - t0) : 1;
            res->t_handshake = (now - t0) ? (now - t0) : 1;
        }
        if (st == HC_T_OPEN && !sent_all) {
            done = 0;
            e = x->conn.ops->write(&x->conn, x->req + sent,
                                   (unsigned long)strlen(x->req) - sent,
                                   &done);
            if (e != HC_OK) break;
            if (done) { sent += done; progress = 1; }
            if (sent >= (unsigned long)strlen(x->req)) sent_all = 1;
        }
        if (st == HC_T_OPEN || st == HC_T_EOF) {
            /* drain everything the transport has */
            for (;;) {
                got = 0;
                if (!have_hdr) {
                    e = x->conn.ops->read(&x->conn, x->hdr + x->hdr_len,
                                          HC_HDR_CAP - x->hdr_len, &got);
                } else {
                    e = x->conn.ops->read(&x->conn, x->io, HC_IO_CAP, &got);
                }
                if (e != HC_OK || got == 0) break;
                progress = 1;
                if (res->t_first_byte == 0)
                    res->t_first_byte = (now - t0) ? (now - t0) : 1;
                if (!have_hdr) {
                    x->hdr_len += got;
                    end = hc_hdr_end(x->hdr, x->hdr_len);
                    if (end == 0) {
                        if (x->hdr_len >= HC_HDR_CAP) { e = HC_ERR_PROTOCOL; break; }
                        continue;
                    }
                    e = hc_process_headers(x, res, end, redirect);
                    if (e != HC_OK) break;
                    if (res->http_status >= 100 && res->http_status < 200) {
                        /* interim response: drop it, keep reading */
                        memmove(x->hdr, x->hdr + end, x->hdr_len - end);
                        x->hdr_len -= end;
                        continue;
                    }
                    have_hdr = 1;
                    if (*redirect) break;
                    e = hc_body(x, req, res, x->hdr + end, x->hdr_len - end);
                } else {
                    e = hc_body(x, req, res, x->io, got);
                }
                if (e != HC_OK || x->body_done) break;
            }
            if (e != HC_OK || *redirect || x->body_done) break;
            if (st == HC_T_EOF) {
                /* nothing more will come */
                if (!have_hdr) e = x->hdr_len ? HC_ERR_PROTOCOL : HC_ERR_IO;
                else if (x->body_mode != 0) e = HC_ERR_TRUNCATED;
                else x->body_done = 1;
                break;
            }
        }
        if (progress) last = now;
        else if (now - last > timeout) { e = HC_ERR_TIMEOUT; break; }
        if (req->idle != NULL && req->idle(req->ref, progress) != 0) {
            e = HC_ERR_ABORTED;
            break;
        }
    }
    {
        int keep;
        keep = res->err;
        x->conn.ops->finish(&x->conn, res);
        res->err = keep;
    }
    x->conn.ops->close(&x->conn);
    res->t_total = (unsigned long)TickCount() - t0;
    if (e != HC_OK) return e;
    if (*redirect) {
        e = HC_ResolveLocation(x->url, x->location, x->next_url, HC_URL_CAP);
        return e;
    }
    if (res->http_status >= 300) return HC_ERR_HTTP_STATUS;
    return HC_OK;
}

int HC_Get(const HC_Request *req, HC_Result *res)
{
    HC_Ctx *x;
    int e, redirect, maxr;
    unsigned long t0;

    memset(res, 0, sizeof *res);
    res->cert_err_ignored = -1;
    if (req == NULL || req->url == NULL) { res->err = HC_ERR_URL; return HC_ERR_URL; }
    if ((unsigned long)strlen(req->url) >= HC_URL_CAP) { res->err = HC_ERR_URL; return HC_ERR_URL; }
    x = (HC_Ctx *)NewPtrClear((Size)sizeof(HC_Ctx));
    if (x == NULL) { res->err = HC_ERR_NOMEM; return HC_ERR_NOMEM; }
    maxr = req->max_redirects == 0 ? 5 : req->max_redirects;
    hc_copy(x->url, req->url, HC_URL_CAP);
    t0 = (unsigned long)TickCount();
    for (;;) {
        hc_copy(res->final_url, x->url, sizeof res->final_url);
        e = hc_hop(x, req, res, &redirect);
        if (e != HC_OK || !redirect) break;
        if (req->redirect != NULL
            && req->redirect(req->ref, res->http_status, x->url,
                             x->next_url) != 0) {
            e = HC_ERR_ABORTED;
            break;
        }
        if (maxr < 0) { e = HC_ERR_HTTP_STATUS; break; }
        if (res->redirects >= maxr) { e = HC_ERR_REDIRECTS; break; }
        res->redirects++;
        hc_copy(x->url, x->next_url, HC_URL_CAP);
        res->body_bytes = 0;
    }
    res->t_all = (unsigned long)TickCount() - t0;
    res->err = e;
    DisposePtr((Ptr)x);
    return e;
}

const char *HC_ErrName(int err)
{
    switch (err) {
    case HC_OK:              return "ok";
    case HC_ERR_URL:         return "bad URL";
    case HC_ERR_NOMEM:       return "out of memory";
    case HC_ERR_CONNECT:     return "DNS/connect failed";
    case HC_ERR_HANDSHAKE:   return "TLS handshake failed";
    case HC_ERR_CERT:        return "certificate rejected";
    case HC_ERR_CLOCK:       return "Mac clock is before 2000";
    case HC_ERR_TIMEOUT:     return "timed out";
    case HC_ERR_HTTP_STATUS: return "HTTP error status";
    case HC_ERR_PROTOCOL:    return "bad HTTP response";
    case HC_ERR_TRUNCATED:   return "connection closed mid-body";
    case HC_ERR_REDIRECTS:   return "too many redirects";
    case HC_ERR_ABORTED:     return "aborted";
    case HC_ERR_IO:          return "network I/O error";
    case HC_ERR_TLS:         return "TLS error after handshake";
    default:                 return "?";
    }
}

const char *HC_CipherName(unsigned int s)
{
    switch (s) {
    case 0x1301: return "TLS_AES_128_GCM_SHA256";
    case 0x1302: return "TLS_AES_256_GCM_SHA384";
    case 0x1303: return "TLS_CHACHA20_POLY1305_SHA256";
    case 0xC02B: return "ECDHE_ECDSA_AES_128_GCM_SHA256";
    case 0xC02C: return "ECDHE_ECDSA_AES_256_GCM_SHA384";
    case 0xC02F: return "ECDHE_RSA_AES_128_GCM_SHA256";
    case 0xC030: return "ECDHE_RSA_AES_256_GCM_SHA384";
    case 0xCCA8: return "ECDHE_RSA_CHACHA20_POLY1305";
    case 0xCCA9: return "ECDHE_ECDSA_CHACHA20_POLY1305";
    case 0xC027: return "ECDHE_RSA_AES_128_CBC_SHA256";
    case 0xC013: return "ECDHE_RSA_AES_128_CBC_SHA";
    case 0xC014: return "ECDHE_RSA_AES_256_CBC_SHA";
    case 0x009C: return "RSA_AES_128_GCM_SHA256";
    case 0x002F: return "RSA_AES_128_CBC_SHA";
    case 0:      return "(none)";
    default:     return "(other)";
    }
}

/* ---- HC_Stream (public step-driven stream over HC_Ops) ---- */
struct HC_Stream {
    HC_Conn conn;
    int     last;          /* last HC_T_* from step                 */
    int     err;           /* HC_ERR_* once failed                  */
};

HC_Stream *HC_StreamOpen(const char *host, unsigned short port, int https,
                         int insecure, unsigned long timeout_ticks)
{
    HC_Stream *s;
    int e;
    s = (HC_Stream *)NewPtrClear((Size)sizeof(HC_Stream));
    if (s == NULL) return NULL;
    s->conn.ops = https ? &hc_tls_ops : &hc_plain_ops;
    s->last = HC_T_CONNECTING;
    e = s->conn.ops->start(&s->conn, host, port, insecure,
                           timeout_ticks ? timeout_ticks : HC_DEFAULT_TIMEOUT);
    if (e != HC_OK) {
        s->conn.ops->close(&s->conn);
        DisposePtr((Ptr)s);
        return NULL;
    }
    return s;
}

int HC_StreamStep(HC_Stream *s)
{
    int progress = 0;
    if (s->last == HC_T_FAILED) return HC_S_FAILED;
    s->last = s->conn.ops->step(&s->conn, &progress);
    if (s->last == HC_T_FAILED) {
        HC_Result r;
        memset(&r, 0, sizeof r);
        s->conn.ops->finish(&s->conn, &r);
        s->err = r.err != HC_OK ? r.err : HC_ERR_IO;
        return HC_S_FAILED;
    }
    if (s->last == HC_T_OPEN || s->last == HC_T_EOF) return HC_S_OPEN;
    return HC_S_CONNECTING;
}

long HC_StreamWrite(HC_Stream *s, const char *p, unsigned long n)
{
    unsigned long done = 0;
    if (s->last != HC_T_OPEN) return s->last == HC_T_FAILED ? -2L : 0L;
    if (s->conn.ops->write(&s->conn, p, n, &done) != HC_OK) {
        s->err = HC_ERR_IO;
        s->last = HC_T_FAILED;
        return -2L;
    }
    return (long)done;
}

long HC_StreamRead(HC_Stream *s, char *p, unsigned long cap)
{
    unsigned long got = 0;
    int progress = 0;
    if (s->last == HC_T_FAILED) return -2L;
    if (s->last != HC_T_OPEN && s->last != HC_T_EOF) return 0L;
    if (s->conn.ops->read(&s->conn, p, cap, &got) != HC_OK) {
        s->err = HC_ERR_IO;
        s->last = HC_T_FAILED;
        return -2L;
    }
    if (got > 0) return (long)got;
    /* nothing: has the peer finished? */
    s->last = s->conn.ops->step(&s->conn, &progress);
    if (s->last == HC_T_EOF) return -1L;
    if (s->last == HC_T_FAILED) return -2L;
    return 0L;
}

void HC_StreamClose(HC_Stream *s)
{
    if (s == NULL) return;
    s->conn.ops->close(&s->conn);
    DisposePtr((Ptr)s);
}

int HC_StreamError(HC_Stream *s) { return s->err; }
