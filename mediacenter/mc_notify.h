/*
    mc_notify.h -- Classic Media Center 0.5t: the per-connection state
    machine that runs at Open Transport NOTIFIER (deferred task) time.
    Portable strict C89, no OT types, so host tests drive it from a fake
    notifier. Modelled on QTSniff Q2's qs_notify.c (same order: accept,
    OTRcv + mcp_req_feed, head + pre-buffer with partial sends and
    T_GODATA, hand-off to the event loop), cut down to what Media
    Center needs.

    Why: QuickTime Player 6.0.2 opens a reference movie whose dref is
    http://127.0.0.1:PORT/... SYNCHRONOUSLY and calls no WaitNextEvent
    while it waits; an event-loop server is starved (8-minute freeze,
    then "cannot be completely downloaded"). Measured fix (Q2, VM): answer
    the GET at notifier time with the head plus a RAM pre-buffer of the
    film's first bytes; QuickTime's open returns ~2 s later and the event
    loop continues the body (mc_proxy, mcp_adopt_downstream).

    Answered at notifier time: ONLY a GET for the pre-buffered target
    (query/fragment ignored) with no Range or "Range: bytes=0-" (the one
    request QuickTime 6 makes). The heads were built by the event loop
    (mcp_build_head uses sprintf). Everything else -- another target, any
    other Range, HEAD, a bad request -- is PASSED to the event loop, which
    replays the bytes into mc_proxy (the 0.5s path).

    Deferred-time rules for everything marked [N]: no Memory Manager, no
    File Manager, no QuickDraw, no sprintf, no allocation, no floating
    point; only memcpy/memcmp/str*, mc_proxy's pure request parser
    (mcp_req_feed: memcpy + byte scanning, no allocation, no sprintf),
    and the McnIo callbacks (OTRcv / OTSnd in the Mac shell). Log records
    go into a fixed RAM ring that the event loop drains (mcn_next_rec).
*/
#ifndef MC_NOTIFY_H
#define MC_NOTIFY_H

#include "mc_proxy.h"

#define MCN_SLOTS    5            /* = MCPO_SLOTS                        */
#define MCN_RX       1024         /* OTRcv scratch per slot              */
#define MCN_HEAD     640          /* = mc_proxy's MCP_HEAD_CAP           */
#define MCN_SENDMAX  32768UL      /* bytes asked per OTSnd               */
#define MCN_GUARD    64           /* sends per notifier call, then kick  */
#define MCN_RING     256          /* log records                         */
#define MCN_FLOW_LOG 30UL         /* log flow pauses longer than this    */

/* slot states */
#define MCN_OFF       0   /* not usable (closing / out of use)          */
#define MCN_FREE      1   /* idle bound endpoint, may be accepted onto  */
#define MCN_ACCEPTING 2   /* OTAccept issued, waiting for T_PASSCON     */
#define MCN_REQ       3   /* reading the request head                   */
#define MCN_SEND      4   /* sending head + pre-buffer                  */
#define MCN_READY     5   /* notifier finished: the event loop takes it */
#define MCN_LOOP      6   /* owned by the event loop (mc_proxy)         */

/* why a slot is READY */
#define MCN_WHY_SERVED 1  /* head + pre-buffer sent: adopt at offset X  */
#define MCN_WHY_PASS   2  /* not ours: replay the request to mc_proxy   */
#define MCN_WHY_FIN    3  /* peer released before a full request        */
#define MCN_WHY_DISC   4  /* peer disconnected                          */
#define MCN_WHY_ERR    5  /* send failed: hard close                    */

/* log record codes */
#define MCN_L_ACCEPT 1    /* a=ip b=port                                */
#define MCN_L_REQ    2    /* a=request bytes b=ticks after accept       */
#define MCN_L_SERVE  3    /* a=status b=pre-buffer bytes to send        */
#define MCN_L_PASS   4    /* txt=why                                    */
#define MCN_L_SENT   5    /* a=body bytes sent b=OTSnd calls            */
#define MCN_L_FIN    6    /* a=body sent b=state                        */
#define MCN_L_DISC   7    /* a=body sent b=state                        */
#define MCN_L_ERR    8    /* a=body sent                                */
#define MCN_L_FLOW   9    /* a=pause ticks b=body offset                */
#define MCN_L_NOTE   10   /* txt, a, b                                  */

typedef struct McnRec
{
    unsigned long tick;
    int           id, slot, code;
    unsigned long a, b;
    const char   *txt;            /* string literal or NULL              */
} McnRec;

typedef struct McnIo
{
    void *ctx;
    unsigned long (*ticks)(void *ctx);                /* deferred-safe  */
    /* >0 bytes, 0 = nothing now (no data / look event), <0 error */
    long (*recv)(void *ctx, int slot, char *buf, unsigned long n);
    /* >0 bytes taken, 0 = flow controlled (T_GODATA follows), <0 error */
    long (*send)(void *ctx, int slot, const char *buf, unsigned long n);
} McnIo;

/* the film the event loop pre-buffered (mcn_set_prebuf) */
typedef struct McnPre
{
    volatile int  on;
    char          target[MCP_TARGET_CAP];  /* no query / fragment      */
    unsigned long tlen;
    const char   *buf;                     /* film bytes [0, len)      */
    unsigned long len, total;
    char          h206[MCN_HEAD];          /* "Range: bytes=0-"        */
    char          h200[MCN_HEAD];          /* no Range                 */
    unsigned long l206, l200;
} McnPre;

typedef struct McnSlot
{
    volatile int  st;
    int           why, id, status;
    unsigned long ip, port, t_accept, t_flow;
    McpReq        req;
    char          rx[MCN_RX];
    char          raw[MCP_REQ_CAP];        /* bytes as received: the
                                              parser rewrites req.buf */
    unsigned long rawlen;
    const char   *head;
    unsigned long hlen, hsent;
    unsigned long n, total;                /* reply body = [0, n)       */
    unsigned long pre_len, body_sent, sends;
    volatile int  fin, disc, need_kick, in_flow;
} McnSlot;

typedef struct McnState
{
    McnIo                  io;
    McnPre                 pre;
    int                    next_id;
    McnSlot                s[MCN_SLOTS];
    McnRec                 ring[MCN_RING];
    volatile unsigned long rhead, rtail, dropped;
} McnState;

/* what the event loop needs to adopt a READY slot */
typedef struct McnTake
{
    int           why, id, status, fin, disc;
    unsigned long body_sent, n, total;
    const McpReq *req;                     /* parsed request           */
    const char   *replay;                  /* bytes received (PASS/FIN) */
    unsigned long nreplay;
} McnTake;

/* ---- event loop ---- */
void mcn_init(McnState *st, const McnIo *io);
/* call with pre.on == 0 and no slot in MCN_SEND/MCN_READY using it
   (mcn_prebuf_busy); target without query; heads from mcp_build_head */
void mcn_set_prebuf(McnState *st, const char *target, const char *buf,
                    unsigned long len, unsigned long total,
                    const char *h206, unsigned long l206,
                    const char *h200, unsigned long l200);
void mcn_prebuf_off(McnState *st);
int  mcn_prebuf_busy(McnState *st);       /* a slot is sending from it */
int  mcn_take(McnState *st, int slot, McnTake *t); /* READY -> LOOP; 1 ok */
void mcn_kick(McnState *st, int slot);    /* after MCN_GUARD sends      */
void mcn_set_state(McnState *st, int slot, int state);
int  mcn_next_rec(McnState *st, McnRec *out);      /* 1 = got one      */
/* 1 if the slot holds a QuickTime connection not yet owned by mc_proxy */
int  mcn_busy(McnState *st, int slot);

/* ---- [N] notifier time (or event loop inside OTEnterNotifier) ---- */
void mcn_log(McnState *st, int slot, int code, unsigned long a,
             unsigned long b, const char *txt);
void mcn_accepting(McnState *st, int slot, unsigned long ip,
                   unsigned long port);
void mcn_passcon(McnState *st, int slot);
void mcn_on_data(McnState *st, int slot);
void mcn_on_godata(McnState *st, int slot);
/* after OTRcvOrderlyDisconnect / OTRcvDisconnect returned noErr */
void mcn_on_fin(McnState *st, int slot);
void mcn_on_disc(McnState *st, int slot);
/* 1 if the notifier should handle T_DATA / T_GODATA for the slot */
int  mcn_owns_events(McnState *st, int slot);

#endif
