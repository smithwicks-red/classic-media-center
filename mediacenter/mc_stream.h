/*
    mc_stream.h -- Classic Media Center 0.5s: the streaming POLICY around
    the proxy (mc_proxy.c core, mc_proxy_ot.c Open Transport side):
    the localhost dref, " (MC)" movie names, the loop sleep, the status
    line with KB/s, Stop (close the movie in QuickTime Player first, wait,
    then cut), Quit while serving (warn, second Quit within 10 s), the G3
    hint, and the hook-run timers. Strict C89, no Toolbox calls: the app
    supplies the clock, the log, the status line and the Apple event to
    QuickTime Player (McsEnv), so the host tests run this very file.
    The skin of 0.5 calls only these functions for its UI.
*/
#ifndef MC_STREAM_H
#define MC_STREAM_H

#define MCS_STOP_WAIT   120UL   /* ticks QuickTime gets to close it    */
#define MCS_QUIT_AGAIN  600UL   /* second Quit within 10 s              */
#define MCS_RATE_N       8      /* (0.6.1) KB/s = average of the last 8 samples */
#define MCS_STATUS_EVERY 60UL   /* status line refresh (ticks)          */
#define MCS_G3_KBS      64UL    /* G3 hint above this average (KB/s)    */
/* (0.6.1) G3 policy, stream vs download (see mc_core.h "G3 POLICY"):
   the pre-buffer's measured KB/s (bytes / ticks, TLS handshakes
   included) must reach the film's average KB/s * MARGIN / 100, else
   Play downloads first. Pismo log: 557,712 B in 335 ticks = 97 KB/s
   for a 93 KB/s film -> 97 < 116: download */
#define MCS_STREAM_MARGIN_PCT 125L
#define MCS_NET_MIN_BYTES (128UL * 1024UL) /* trust a rate only after ... */
#define MCS_NET_MIN_TICKS 30UL             /* ... this many bytes and ticks */
#define MCS_PREBUF_WAIT 600UL   /* (0.5t) ticks to wait for a busy pre-buffer */
#define MCS_MAX_MOVIES  4
#define MCS_NAME_MAX    31      /* Mac file name limit                  */

typedef struct McsEnv
{
    void *ref;
    unsigned long (*now)(void *ref);                 /* ticks           */
    void (*log)(void *ref, const char *text);        /* log file (+ \n) */
    void (*status)(void *ref, const char *line);     /* status line     */
    /* Apple event "close <movie window> saving no" to QuickTime Player.
       0 = sent (QuickTime may still refuse), else an OSErr. */
    int  (*qt_close)(void *ref, const char *movieName);
    /* (0.5t) between pre-buffer steps: keep the Mac alive and poll the
       proxy (MediaCenter.c CbIdle); nonzero = Stop / Cmd-period. NULL =
       mcs_poll only (tests) */
    int  (*idle)(void *ref, int busy);
    int  cpu_g3;              /* 1 = a G3 (Gestalt gestaltNativeCPUtype) */
    unsigned long g3_kbs;     /* 0 = MCS_G3_KBS                        */
    long stream_margin;       /* (0.6.1) 0 = MCS_STREAM_MARGIN_PCT, <0 =
                                 never switch to download (host tests)  */
} McsEnv;

void mcs_init(const McsEnv *env);

/* McfEnv.proxy_url: https URL of the film + its size (0 = unknown) + its
   moov -> dref "http://127.0.0.1:PORT/a/host/path" (cap >= 1100), and
   name "x.mov" -> "x (MC).mov". 0 = stream through the proxy, -1 = no
   listener (the caller downloads instead). (0.5t) Before it returns 0
   it PRE-BUFFERS the film's first MCPO_PREBUF bytes ("Buffering N%"),
   so QuickTime's synchronous open is answered at notifier time.
   (0.5u) The pre-buffer is [0, mediaFrom + MCPO_PREBUF) (mediaFrom = end
   of the moov when it precedes the mdat, else 0 = 512 KB), at most
   MCPO_PREBUF_MAX (4 MB): a bigger index -> -1 (download). A
   failed pre-buffer -> -1 (download), Stop while buffering -> -2. */
int  mcs_proxy_url(void *ref, const char *httpsUrl, unsigned long size,
                   const unsigned char *moov, long moovLen,
                   unsigned long mediaFrom, char *dref, char *name);
/* McfEnv.proxy_opened: the reference movie is open in QuickTime Player */
void mcs_proxy_opened(void *ref, const char *name);
void mcs_set_insecure(int on);
/* (0.6.1) names of our reference movies on the Desktop */
int  mcs_is_mc_name(const char *name);                 /* "x (MC).mov"      */
void mcs_try_name(const char *name, int attempt, char *out); /* 1 = as is; 2.. = "x 2 (MC).mov", <= 31 */
int  mcs_movie_in_use(const char *name);               /* being streamed now */
void mcs_saved_as(const char *actual);                 /* open_ref saved it under this name */

/* (0.6.1) stream vs download: 1 = download first (measured rate below
   film_kbs * margin_pct / 100). 0 = stream, or too little data to judge
   (bytes < MCS_NET_MIN_BYTES or ticks < MCS_NET_MIN_TICKS) or film_kbs
   unknown (0). margin_pct 0 = MCS_STREAM_MARGIN_PCT. *net_kbs = the
   measured rate (0 = not judged) */
int  mcs_net_download(unsigned long bytes, unsigned long ticks,
                      unsigned long film_kbs, long margin_pct,
                      unsigned long *net_kbs);
/* proxy_url returns -3 on a G3 when mcs_net_download said so */

/* "x.mov" -> "x (MC).mov", trimming x so the result has <= 31 chars */
void mcs_mc_name(char *name);
/* average byte rate of a film: size / mvhd duration. 0 = unknown */
unsigned long mcs_avg_kbs(const unsigned char *moov, long len,
                          unsigned long size);

/* call every event-loop pass AND from CbIdle. >0 = bytes moved */
int  mcs_poll(void);
/* WaitNextEvent sleep: 0 after bytes moved, 1 while connections or
   parked upstreams exist, else idle (15) */
long mcs_sleep(int moved, long idle);
/* the Stop button should be enabled for streaming */
int  mcs_streaming(void);
/* QuickTime has an open connection right now */
int  mcs_serving(void);

/* Stop: Apple event to QuickTime Player for every streaming movie,
   up to MCS_STOP_WAIT ticks of polling, then cancel everything left. */
void mcs_stop(void);
#define MCS_QUIT_NOW     0    /* nothing streams: quit at once           */
#define MCS_QUIT_WARNED  1    /* serving: told the user to Quit again    */
#define MCS_QUIT_CLOSING 2    /* closing the movie(s) first: keep looping */
int  mcs_quit(void);
/* 1 once the app may quit (after MCS_QUIT_CLOSING, or a hook timer) */
int  mcs_quit_ready(void);
int  mcs_stopping(void);

/* unattended (hook) runs: stop after stopSecs, quit (closing the movies)
   after serveSecs, quit once a Stop is complete. 0 = not set */
void mcs_hook_timers(unsigned long serveSecs, unsigned long stopSecs);

/* "Streaming via secure proxy: 412 KB/s, 12.3 MB sent, 1 connection." */
unsigned long mcs_rate_feed(unsigned long t, unsigned long bytes);   /* (0.6.1) sample -> KB/s */
void mcs_rate_reset(void);
void mcs_status_text(char *out, unsigned long cap);
/* idle loop only: refresh KB/s and push the status line when it changes */
void mcs_status_tick(void);
void mcs_shutdown(void);      /* at exit: close the listener, free all  */

#endif
