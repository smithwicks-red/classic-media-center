/*
 * https_client.h -- small HTTP/1.1 GET client for classic Mac OS apps,
 * over macTLS (https) or plain Open Transport TCP (http).
 *
 * Strict C89. No dependence on H264Stream.c. One request at a time per
 * HC_Get call; the call does not return until the transfer is finished,
 * but it calls req->idle between every pump step so the application
 * can run WaitNextEvent, update a progress bar, or cancel.
 *
 * The application must have called InitOpenTransport() first.
 */
#ifndef HTTPS_CLIENT_H
#define HTTPS_CLIENT_H

/* ---- result codes (HC_Result.err and the return value of HC_Get) ---- */
enum {
    HC_OK = 0,
    HC_ERR_URL = 1,              /* bad / unsupported URL (or too long)  */
    HC_ERR_NOMEM = 2,
    HC_ERR_CONNECT = 3,          /* DNS lookup or TCP connect failed     */
    HC_ERR_HANDSHAKE = 4,        /* TLS handshake failed (not the cert)  */
    HC_ERR_CERT = 5,             /* certificate rejected; see br_err     */
    HC_ERR_CLOCK = 6,            /* Mac clock is before 2000-01-01       */
    HC_ERR_TIMEOUT = 7,          /* no progress for timeout_ticks        */
    HC_ERR_HTTP_STATUS = 8,      /* server answered >= 400 (or odd 3xx)  */
    HC_ERR_PROTOCOL = 9,         /* malformed HTTP response              */
    HC_ERR_TRUNCATED = 10,       /* connection closed before body end    */
    HC_ERR_REDIRECTS = 11,       /* more than max_redirects              */
    HC_ERR_ABORTED = 12,         /* body or idle callback said stop      */
    HC_ERR_IO = 13,              /* send/receive failed after connect    */
    HC_ERR_TLS = 14              /* TLS error after the handshake        */
};

/* Return 0 to continue, nonzero to abort the transfer (HC_ERR_ABORTED). */
typedef int (*HC_BodyProc)(void *ref, const unsigned char *data,
                           unsigned long len);
/* Called between pump steps. busy = 1 if the last step made progress
   (so the app may want WaitNextEvent with sleep 0), 0 if idle (sleep 1).
   Return nonzero to cancel. */
typedef int (*HC_IdleProc)(void *ref, int busy);
/* (b19) Called before a redirect is followed: from = the URL that
   answered 3xx, to = the resolved Location. Return nonzero to stop
   (HC_Get then returns HC_ERR_ABORTED). */
typedef int (*HC_RedirectProc)(void *ref, int status, const char *from,
                               const char *to);

typedef struct HC_Request {
    const char   *url;           /* http:// or https://                  */
    int           insecure;      /* 1 = do not verify the certificate    */
    int           use_range;     /* 1 = send Range: bytes=start-[end]    */
    unsigned long range_start;
    unsigned long range_end;     /* inclusive; 0xFFFFFFFF = open-ended   */
    const char   *user_agent;    /* NULL = default                       */
    unsigned long timeout_ticks; /* no-progress timeout; 0 = 30 s        */
    int           max_redirects; /* 0 = default 5; -1 = do not follow    */
    HC_BodyProc   body;          /* may be NULL (body discarded)         */
    HC_IdleProc   idle;          /* may be NULL                          */
    void         *ref;           /* passed to both callbacks             */
    HC_RedirectProc redirect;    /* (b19) may be NULL                    */
} HC_Request;

typedef struct HC_Result {
    int           err;           /* HC_OK or HC_ERR_*                    */
    int           http_status;   /* last status received, 0 if none      */
    long          os_err;        /* macTLS kOSTLSAsync_* or OT error     */
    long          ot_err;        /* underlying OT error, if any          */
    int           br_err;        /* BearSSL error number, if any         */
    int           cert_err_ignored; /* insecure: error that was ignored;
                                     0 = chain would have passed;
                                     -1 = n/a                           */
    int           tls_version;   /* 0x0304, 0x0303, or 0 for plain http  */
    unsigned int  cipher_suite;
    int           redirects;     /* how many were followed               */
    int           chunked;       /* last response used chunked coding    */
    int           has_length;    /* Content-Length was present           */
    unsigned long content_length;
    unsigned long body_bytes;    /* bytes delivered to req->body         */
    /* ticks, measured from the start of the LAST hop */
    unsigned long t_connect;     /* DNS + TCP connect                    */
    unsigned long t_handshake;   /* TLS handshake done (= connect, http) */
    unsigned long t_first_byte;  /* first response byte                  */
    unsigned long t_total;       /* whole hop                            */
    unsigned long t_all;         /* whole HC_Get, all hops               */
    char          final_url[1024];
    char          content_type[64];
    char          content_range[64];
} HC_Result;

int         HC_Get(const HC_Request *req, HC_Result *res);
const char *HC_ErrName(int err);
const char *HC_CipherName(unsigned int suite);

/* Exposed for tests: parse "http[s]://host[:port]/path". Returns HC_OK
   or HC_ERR_URL. host must hold 256 bytes, path 1024. */
int HC_ParseURL(const char *url, int *is_https, char *host,
                unsigned short *port, char *path);
/* Exposed for tests: resolve a Location against the current URL into
   out (cap bytes). Returns HC_OK or HC_ERR_URL. */
int HC_ResolveLocation(const char *base, const char *loc, char *out,
                       unsigned long cap);

/* ---- HC_Stream: non-blocking byte stream over the same transports
   HC_Get uses (macTLS for https, plain OT for http). No HTTP in here.
   Nothing blocks; call HC_StreamStep before reading/writing. ---- */
typedef struct HC_Stream HC_Stream;

#define HC_S_CONNECTING 0
#define HC_S_OPEN       1
#define HC_S_FAILED     2

/* NULL = failed at once (bad host string, no memory, clock < 2000). */
HC_Stream *HC_StreamOpen(const char *host, unsigned short port, int https,
                         int insecure, unsigned long timeout_ticks);
int   HC_StreamStep(HC_Stream *s);                       /* HC_S_*   */
/* >= 0 bytes accepted, -2 error */
long  HC_StreamWrite(HC_Stream *s, const char *p, unsigned long n);
/* > 0 bytes, 0 none now, -1 orderly end, -2 error */
long  HC_StreamRead(HC_Stream *s, char *p, unsigned long cap);
void  HC_StreamClose(HC_Stream *s);
int   HC_StreamError(HC_Stream *s);  /* HC_ERR_* detail after FAILED  */

#endif
