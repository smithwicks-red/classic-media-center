/*
    mc_fetch.h -- Classic Media Center 0.2: everything the app fetches,
    over ONE transport (https_client: macTLS/BearSSL for https, plain
    Open Transport for http), plus "download, then play".

    Strict C89. Uses https_client.h and a handful of File Manager calls
    (FSpCreate, FSWrite, FSpRename, PBHGetVInfoSync ...). Everything
    that touches the window, the Finder or QuickTime is a callback in
    McfEnv, so the host test rig runs this very file.
*/
#ifndef MC_FETCH_H
#define MC_FETCH_H

#include <Files.h>
#include "https_client.h"
#include "mc_core.h"

#define MCF_OK          0
#define MCF_ERR_NET     1   /* see env->res (HC_ERR_*)                  */
#define MCF_ERR_CANCEL  2   /* the user stopped it                      */
#define MCF_ERR_FORMAT  3   /* not an MP4 we can handle                 */
#define MCF_ERR_NOMEM   4
#define MCF_ERR_DISK    5   /* not enough free space                    */
#define MCF_ERR_FILE    6   /* File Manager error                       */
#define MCF_ERR_CHANGED 7   /* download differs from the file we checked */
#define MCF_ERR_REFUSED 8   /* not Baseline / too big                   */
#define MCF_ERR_OPEN    9   /* could not save/open the movie            */
#define MCF_ERR_CONFIRM 10  /* (0.3) big download not confirmed         */

#define MCF_MODE_STREAM   0   /* reference movie, QuickTime fetches http */
#define MCF_MODE_DOWNLOAD 1   /* whole file over TLS, then a local movie */
#define MCF_MODE_PROXY    2   /* (0.5s) reference movie -> 127.0.0.1 proxy
                                 -> https (mc_proxy + mc_proxy_ot)       */

#define MCF_DL_FOLDER  "Media Center Downloads"
#define MCF_SPARE_KB   5120UL  /* keep 5 MB free after a download       */
/* (0.3) a download bigger than this must be confirmed first */
#define MCF_BIG_BYTES  (32UL * 1024UL * 1024UL)
#define MCF_RATE_KBS   430UL   /* the estimate's speed (KB/s)           */

typedef struct McfEnv
{
    void  *ref;                                   /* for the callbacks */
    void  (*log)(void *ref, const char *text);    /* log + status line */
    void  (*status)(void *ref, const char *line); /* status line only  */
    int   (*idle)(void *ref, int busy);           /* nonzero = stop    */
    void *(*alloc)(long n);
    void  (*release)(void *p);
    /* moov -> reference movie (url != NULL) or the moov of a
       self-contained movie (url == NULL). Block from alloc. */
    unsigned char *(*rebuild)(const unsigned char *moov, long len,
                              const char *url, long *outLen);
    /* stream: save the reference movie, open it. 0 ok */
    int   (*open_ref)(void *ref, const char *name,
                      const unsigned char *data, long n);
    /* download: open the local movie. 0 ok */
    int   (*open_local)(void *ref, const FSSpec *spec);
    /* URL -> "name.mov" (<= 31 chars) */
    void  (*movie_name)(const char *url, char *name);
    /* (0.5s) https streaming through the local proxy (NULL = never).
       proxy_url: https URL + size (0 unknown) + moov -> dref (MC_URLMAX
       + 80 bytes) and name "x.mov" -> "x (MC).mov"; 0 ok, -1 = no
       listener or pre-buffering failed (Play downloads instead), -2 =
       (0.5t) stopped while pre-buffering. proxy_opened: after open_ref.
       (0.5u) mediaFrom = end of the moov box when the moov comes BEFORE
       the mdat (the media cannot start earlier), 0 = moov at the end /
       unknown: the pre-buffer then covers [0, mediaFrom + MCPO_PREBUF). */
    int   (*proxy_url)(void *ref, const char *httpsUrl, unsigned long size,
                       const unsigned char *moov, long moovLen,
                       unsigned long mediaFrom, char *dref, char *name);
    void  (*proxy_opened)(void *ref, const char *name);

    const char *user_agent;
    int    insecure;          /* "Skip certificate checks (unsafe)"   */
    int    force_download;    /* "Download first"                     */
    short  app_vref;          /* folder holding MCF_DL_FOLDER          */
    long   app_dir;
    unsigned long spare_kb;   /* 0 = MCF_SPARE_KB                      */

    HC_Result res;            /* the last transfer                     */
    char   message[256];      /* last user-facing explanation          */
    /* filled by mcf_play_item, for tests and the log */
    int    mode;
    int    reused;
    unsigned long dl_bytes, dl_ticks;

    /* (0.3) */
    int    confirm_big;       /* the user said yes to a big download   */
    unsigned long big_bytes;  /* 0 = MCF_BIG_BYTES                     */
    const char *confirm_how;  /* "Click Play again ..." (NULL = default) */
    int    need_confirm;      /* set when MCF_ERR_CONFIRM stopped Play */
    unsigned long confirm_size;
    int    search_scope;      /* MC_SCOPE_* that page 1 used           */
} McfEnv;

typedef struct McfProbe
{
    int  saw_https;           /* some hop of the chain was https      */
    int  requests;
    unsigned long file_size;  /* total size, if a header told us; 0   */
    unsigned long moov_end;   /* (0.5u) offset just after the moov when it
                                 precedes the mdat; 0 = at the end/unknown */
    char final_url[MC_URLMAX];
} McfProbe;

/* --- the box walker (exposed for the tests) --- */
#define MCF_WALK_FETCH 0      /* find and capture the moov            */
#define MCF_WALK_COPY  1      /* pass everything through, ftyp/moov -> 'free' */

typedef int (*McfOut)(void *ref, const unsigned char *p, unsigned long n);

typedef struct McfWalk
{
    int            mode;
    McfEnv        *env;
    unsigned long  pos;       /* file offset of the next byte         */
    unsigned char  hb[16];
    int            hlen, hneed;
    int            in_box;
    int            kind;      /* 0 pass/discard, 1 moov               */
    unsigned long  remain;
    int            boxes;
    int            seen_moov;
    int            seen_mdat;
    int            mdat_first;   /* (0.5u) fetch: an mdat came before the moov */
    unsigned long  moov_end;     /* (0.5u) fetch: offset after the moov  */
    /* fetch */
    unsigned char *m;
    unsigned long  mlen, msize;
    int            stop;      /* 1 = moov complete, 2 = need a Range  */
    unsigned long  range_from;
    /* copy */
    const unsigned char *ref_moov;
    unsigned long  ref_len, cmp_at;
    McfOut         out;
    void          *out_ref;
    int            err;       /* MCF_ERR_* once something is wrong    */
    char           msg[200];
} McfWalk;

void mcf_walk_init(McfWalk *w, int mode, McfEnv *env);
/* 0 = go on, 1 = stop (see w->stop / w->err) */
int  mcf_walk_feed(McfWalk *w, const unsigned char *d, unsigned long n);
void mcf_walk_free(McfWalk *w);

/* --- fetching --- */
int  mcf_get(McfEnv *e, const char *url, HC_BodyProc body, void *bref);
int  mcf_search(McfEnv *e, const char *terms, int page, McSearch *s);
int  mcf_meta(McfEnv *e, const char *id, McMeta *m);
int  mcf_fetch_moov(McfEnv *e, const char *url, unsigned char **moov,
                    long *moovLen, McfProbe *p);
/* (0.5s) plain-http chain -> STREAM; "Download first" -> DOWNLOAD; an
   https chain -> PROXY if proxy_ok (a listener can be had), else DOWNLOAD */
int  mcf_decide(int force, const McfProbe *p, int proxy_ok);
void mcf_to_https(const char *url, char *out);
/* test hook: http port -> https port when making the download URL */
extern unsigned short mcf_test_http_port, mcf_test_https_port;

typedef struct McfJob
{
    const char          *url;        /* https URL of the .mp4          */
    const unsigned char *moov;       /* as checked (original bytes)    */
    long                 moovLen;
    const unsigned char *tail;       /* self-contained moov to append  */
    long                 tailLen;
    unsigned long        expect_size;/* .mp4 size if known, else 0     */
    const char          *name;       /* "something.mov"                */
} McfJob;

int  mcf_downloads_dir(McfEnv *e, short *vref, long *dir);
int  mcf_download(McfEnv *e, const McfJob *job, FSSpec *out, int *reused);

/* (0.3) open a hit: fetch its metadata, log its video files
   ("FILES n id" then one "FILE k ..." line each). 0 ok, -1 failed
   (e->message says why; also when the item has no video file). */
int  mcf_open_item(McfEnv *e, const McHit *hit, McMeta *m);
/* (0.3) log one file row / the whole list in the grep-able format */
void mcf_log_file(McfEnv *e, const McMeta *m, int fi);
void mcf_log_files(McfEnv *e, const char *id, const McMeta *m);
/* (0.3) play file fi (0-based) of an opened item: moov fetch +
   Baseline check (re-marks m->files[fi].state, logs "MARK ..."),
   then stream or download-then-play. 0 = handed to QuickTime. */
int  mcf_play_file(McfEnv *e, const char *id, McMeta *m, int fi,
                   char urls[][MC_URLMAX]);
/* (0.3) the hook file: search, then list / file N / play hit 1.
   Sets force_download/insecure/confirm_big from the hook first. */
int  mcf_hook_run(McfEnv *e, const McHook *h, McSearch *s, McMeta *m,
                  char urls[][MC_URLMAX]);

/* the whole "Play" of one search hit. 0 = handed to QuickTime. */
int  mcf_play_item(McfEnv *e, const McHit *hit, McMeta *meta,
                   char urls[][MC_URLMAX]);

/* user-facing text for an error, into e->message (also returned) */
const char *mcf_explain(McfEnv *e, int mcfErr);

#endif
