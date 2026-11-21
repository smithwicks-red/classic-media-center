/*
    mc_proxy_ot.c -- Classic Media Center 0.5t: Open Transport listener +
    downstream adapter + HC_Stream upstream adapter for mc_proxy.c. See
    mc_proxy_ot.h.

    0.5t: NOTIFIER MODE (QTSniff Q2's QTSniffN.c, proven in the VM with
    QuickTime Player 6.0.2). The listener and the acceptors are
    ASYNCHRONOUS endpoints (OTAsyncOpenEndpoint + one notifier UPP),
    bound synchronously (OTSetSynchronous, OTSetNonBlocking, OTBind,
    OTSetAsynchronous). At deferred-task time the notifier does T_LISTEN
    -> OTListen/OTAccept, T_PASSCON/T_DATA -> OTRcv + mcp_req_feed, and
    for QuickTime's GET of the pre-buffered film the head + the RAM
    pre-buffer (mc_notify.c), T_GODATA continues. The event loop then
    adopts the connection into mc_proxy (mcp_adopt_downstream) or, for
    any other request, replays its bytes into mc_proxy (mcp_accept). Every
    event-loop OT call on an endpoint the notifier also uses is bracketed
    by OTEnterNotifier / OTLeaveNotifier. Notifier log records go into a
    RAM ring that the event loop flushes (FlushRing).

    The pre-buffer itself is fetched by the event loop THROUGH mc_proxy:
    a memory "downstream" (gMem) asks the core for "Range: bytes=0-
    (MCPO_PREBUF-1)"; the core opens the upstream with "bytes=0-"
    (HC_Stream), stops reading at MCPO_PREBUF and PARKS the upstream at
    that offset, so the adopted QuickTime connection continues on the
    same TLS connection (no new handshake) if it comes within park_ticks.

    Strict C89, no newline escapes (CodeWarrior "Map newlines to CR").
*/
#include <stdio.h>
#include <string.h>

#include <MacTypes.h>
#include <Memory.h>
#include <Events.h>
#include <OpenTransport.h>
#include <OpenTptInternet.h>

#include "https_client.h"
#include "mc_notify.h"
#include "mc_proxy_ot.h"

#define kLoopbackIP 0x7F000001UL
#define kLisCookie  100L
#define MCPO_SEND_STALL 300UL   /* ticks: half-closed peer not reading */

unsigned short mcpo_test_base_port = 0;
unsigned short mcpo_test_https_port = 0;
int            mcpo_test_localhost = 0;
int            mcpo_test_fail = 0;
unsigned long  mcpo_test_prebuf = 0;
unsigned long  mcpo_test_prebuf_max = 0;

#define SL_FREE    0          /* notifier side (mc_notify state)       */
#define SL_BUSY    1          /* owned by the core                     */
#define SL_CLOSING 2          /* orderly release sent, waiting for it  */
#define SL_ABORT   3          /* abortive disconnect sent: wait T_IDLE */
#define SL_DEAD    4          /* out of use                            */

typedef struct Slot
{
    EndpointRef    ep;
    int            st, idx, mem;
    volatile int   opened;
    volatile long  openErr;
    int            fin_told;
    unsigned long  t0;
    TCall          call;       /* OTAccept's call, kept until done      */
    InetAddress    addr;
    const char    *rp;         /* request bytes to replay (PASS / FIN)  */
    unsigned long  rpn, rpo;
    const char    *xp;
    unsigned long  xpn, xpo;
} Slot;

static EndpointRef     gLis = kOTInvalidEndpointRef;
static Slot            gSlot[MCPO_SLOTS];
static TCall           gCall[MCPO_CALLS];
static InetAddress     gCallAddr[MCPO_CALLS];
static int             gNCalls = 0;
static volatile int    gWaitLogged = 0;
static int             gAnyAddr = 0;     /* bound to 0.0.0.0 (last resort) */
static unsigned short  gPort = 0;
static McpServer      *gSrv = NULL;
static int             gInsecure = 0;
static void          (*gLog)(void *ref, const char *line) = NULL;
static void           *gLogRef = NULL;
static char            gDrain[512];
/* notifier mode */
static McnState       *gN = NULL;
static OTNotifyUPP     gUPP = NULL;
static volatile int    gArmed = 0, gQuitting = 0;
static volatile int    gAcceptBusy = 0, gAcceptSlot = -1;
static volatile int    gLisOpened = 0;
static volatile long   gLisOpenErr = 0;
static OTTimeStamp     gStamp0;
static unsigned long   gTick0 = 0, gDropLogged = 0;
/* pre-buffer (memory downstream into the core) */
static Slot            gMem;
static char           *gPre = NULL;
static unsigned long   gPreCap = 0;      /* bytes wanted for this film */
static unsigned long   gPreAlloc = 0;    /* size of the Ptr (0.5u)     */
static char            gMemReq[MCP_TARGET_CAP + 200];
static char            gMemTarget[MCP_TARGET_CAP];
static char            gMemHead[MCP_UHDR_CAP];
static unsigned long   gMemReqLen = 0, gMemHeadLen = 0, gMemGot = 0;
static unsigned long   gMemExpect = 0, gMemTotal = 0;
static int             gMemInCore = 0, gMemHeadDone = 0, gMemAbort = 0;
static int             gMemDone = 0;     /* 0 running, 1 ok, -1 failed  */
static McpResp         gMemResp;

static void Log(const char *line)
{
    if (gLog != NULL) gLog(gLogRef, line);
}

static void Logf1(const char *fmt, long a, long b, long c)
{
    char t[160];
    sprintf(t, fmt, a, b, c);
    Log(t);
}

/* ------------------------------------------------------------------ */
/* McpOps: clock, memory, log                                          */
/* ------------------------------------------------------------------ */

static unsigned long P_Now(void *ctx)
{
    (void)ctx;
    return (unsigned long)TickCount();
}

static void P_Log(void *ctx, const char *line)
{
    char t[200];
    (void)ctx;
    strcpy(t, "proxy: ");
    strncat(t, line, sizeof t - 8);
    Log(t);
}

static void *P_Alloc(void *ctx, unsigned long n)
{
    (void)ctx;
    return (void *)NewPtr((Size)(n ? n : 1));
}

static void P_Release(void *ctx, void *p)
{
    (void)ctx;
    if (p != NULL) DisposePtr((Ptr)p);
}

static int P_Allow(void *ctx, const char *host)
{
    unsigned long n = (unsigned long)strlen(host);
    (void)ctx;
    if (strcmp(host, "archive.org") == 0) return 1;
    if (n > 12 && strcmp(host + n - 12, ".archive.org") == 0) return 1;
    if (mcpo_test_localhost && strcmp(host, "localhost") == 0) return 1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* upstream: HC_Stream                                                 */
/* ------------------------------------------------------------------ */

static void *U_Open(void *ctx, const char *host, unsigned short port, int tls)
{
    (void)ctx;
    if (mcpo_test_https_port != 0 && tls && port == 443)
        port = mcpo_test_https_port;
    return (void *)HC_StreamOpen(host, port, tls, gInsecure, 600);
}

static int U_Step(void *h)
{
    int s = HC_StreamStep((HC_Stream *)h);
    if (s == HC_S_OPEN) return MCP_T_OPEN;
    if (s == HC_S_FAILED) return MCP_T_FAILED;
    return MCP_T_CONNECTING;
}

static long U_Write(void *h, const char *p, unsigned long n)
{
    long r = HC_StreamWrite((HC_Stream *)h, p, n);
    return r < 0 ? MCP_IO_ERR : r;
}

static long U_Read(void *h, char *p, unsigned long cap)
{
    long r = HC_StreamRead((HC_Stream *)h, p, cap);
    if (r == -1) return MCP_IO_EOF;
    if (r < -1) return MCP_IO_ERR;
    return r;
}

static void U_Close(void *h)
{
    HC_StreamClose((HC_Stream *)h);
}

static OSStatus BindListener(UInt32 ip, UInt16 port, int explicitAddr,
                             UInt16 *bport)
{
    TBind       req, ret;
    InetAddress reqAddr, retAddr;
    OSStatus    err;
    memset(&req, 0, sizeof req);
    memset(&ret, 0, sizeof ret);
    memset(&retAddr, 0, sizeof retAddr);
    ret.addr.buf = (UInt8 *)&retAddr;
    ret.addr.maxlen = sizeof retAddr;
    if (explicitAddr)
    {
        OTInitInetAddress(&reqAddr, port, ip);
        req.addr.buf = (UInt8 *)&reqAddr;
        req.addr.len = sizeof reqAddr;
        req.addr.maxlen = sizeof reqAddr;
    }
    req.qlen = MCPO_CALLS;
    err = OTBind(gLis, &req, &ret);
    *bport = retAddr.fPort;
    if (err == kOTNoError && port != 0 && retAddr.fPort != port)
    {   /* XTI may bind ANOTHER address when the one asked is taken */
        (void)OTUnbind(gLis);
        return kOTAddressBusyErr;
    }
    return err;
}

/* ------------------------------------------------------------------ */
/* [N] notifier time                                                   */
/* ------------------------------------------------------------------ */

/* [N] ticks at deferred time: OT's time stamp in TickCount's base,
   integer only (no FPU at deferred time) */
static unsigned long N_Ticks(void *ctx)
{
    UInt32 ms;
    (void)ctx;
    ms = OTElapsedMilliseconds(&gStamp0);
    return gTick0 + ((unsigned long)ms * 3UL) / 50UL;
}

/* [N] OTRcv at notifier time */
static long N_Recv(void *ctx, int k, char *buf, unsigned long n)
{
    OTFlags  flags = 0;
    OTResult r = OTRcv(gSlot[k].ep, buf, (OTByteCount)n, &flags);
    (void)ctx;
    if (r > 0) return (long)r;
    if (r == kOTNoDataErr || r == kOTLookErr) return 0;   /* event follows */
    mcn_log(gN, k, MCN_L_NOTE, (unsigned long)r, 0, "OTRcv failed");
    return -1;
}

/* [N] OTSnd; 0 = flow controlled: T_GODATA (or T_DISCONNECT) follows */
static long N_Send(void *ctx, int k, const char *buf, unsigned long n)
{
    OTResult r = OTSnd(gSlot[k].ep, (void *)buf, (OTByteCount)n, 0);
    (void)ctx;
    if (r > 0) return (long)r;
    if (r == 0 || r == kOTFlowErr || r == kOTLookErr) return 0;
    mcn_log(gN, k, MCN_L_NOTE, (unsigned long)r, 0, "OTSnd failed");
    return -1;
}

static void DropCall(int i)
{
    int k;
    for (k = i; k + 1 < gNCalls; k++)
    {
        gCall[k] = gCall[k + 1];
        gCallAddr[k] = gCallAddr[k + 1];
        gCall[k].addr.buf = (UInt8 *)&gCallAddr[k];
    }
    gNCalls--;
}

/* [N] the peer withdrew a queued call */
static void ListenerDiscon(void)
{
    TDiscon d;
    int     i;
    memset(&d, 0, sizeof d);
    if (OTRcvDisconnect(gLis, &d) != kOTNoError) return;
    for (i = 0; i < gNCalls; i++)
        if (gCall[i].sequence == d.sequence) { DropCall(i); return; }
}

/* [N] OTListen until no data, then OTAccept the first call onto a free
   acceptor. One OTAccept in flight at a time; T_ACCEPTCOMPLETE pumps
   again. From the listener's notifier, or from the event loop inside
   OTEnterNotifier(gLis). (QTSniffN.c ListenPump.) */
static void ListenPump(void)
{
    OSStatus err;
    OTResult ev;
    TCall   *c;
    int      k, f = -1;

    if (!gArmed || gQuitting || gAcceptBusy) return;
    while (gNCalls < MCPO_CALLS)
    {
        c = &gCall[gNCalls];
        memset(c, 0, sizeof *c);
        memset(&gCallAddr[gNCalls], 0, sizeof gCallAddr[0]);
        c->addr.buf = (UInt8 *)&gCallAddr[gNCalls];
        c->addr.maxlen = sizeof gCallAddr[0];
        err = OTListen(gLis, c);
        if (err == kOTNoError) { gNCalls++; continue; }
        if (err == kOTNoDataErr) break;
        if (err == kOTLookErr)
        {
            ev = OTLook(gLis);
            if (ev == T_DISCONNECT) { ListenerDiscon(); continue; }
            mcn_log(gN, -1, MCN_L_NOTE, (unsigned long)ev, 0,
                    "listener: OTListen look event");
            break;
        }
        mcn_log(gN, -1, MCN_L_NOTE, (unsigned long)err, 0,
                "listener: OTListen error");
        break;
    }
    while (gNCalls > 0 && gAnyAddr
           && ((gCallAddr[0].fHost >> 24) & 0xFFUL) != 127UL)
    {   /* bound to every address: only this Mac may use us */
        mcn_log(gN, -1, MCN_L_NOTE, gCallAddr[0].fHost, 0,
                "listener: refused a call from a non-local address");
        (void)OTSndDisconnect(gLis, &gCall[0]);
        DropCall(0);
    }
    if (gNCalls == 0) return;
    for (k = 0; k < MCPO_SLOTS; k++)
        if (gN->s[k].st == MCN_FREE && gSlot[k].ep != kOTInvalidEndpointRef)
        { f = k; break; }
    if (f < 0)
    {
        if (!gWaitLogged)
            mcn_log(gN, -1, MCN_L_NOTE, (unsigned long)gNCalls, 0,
                    "listener: call(s) waiting, every endpoint is busy");
        gWaitLogged = 1;
        return;
    }
    gWaitLogged = 0;
    gSlot[f].call = gCall[0];
    gSlot[f].addr = gCallAddr[0];
    gSlot[f].call.addr.buf = (UInt8 *)&gSlot[f].addr;
    gSlot[f].fin_told = 0;
    mcn_accepting(gN, f, gSlot[f].addr.fHost, gSlot[f].addr.fPort);
    gAcceptBusy = 1;
    gAcceptSlot = f;
    err = OTAccept(gLis, gSlot[f].ep, &gSlot[f].call);
    if (err == kOTNoError) { DropCall(0); return; }   /* T_ACCEPTCOMPLETE */
    gAcceptBusy = 0;
    gAcceptSlot = -1;
    mcn_set_state(gN, f, MCN_FREE);
    if (err == kOTLookErr)
    {
        ev = OTLook(gLis);
        mcn_log(gN, f, MCN_L_NOTE, (unsigned long)ev, 0,
                "OTAccept look event: retry later");
        if (ev == T_DISCONNECT) ListenerDiscon();
        return;
    }
    mcn_log(gN, f, MCN_L_NOTE, (unsigned long)err, 0,
            "OTAccept failed: rejecting the call");
    (void)OTSndDisconnect(gLis, &gCall[0]);
    DropCall(0);
}

/* [N] THE notifier: one for all endpoints */
static pascal void Notifier(void *context, OTEventCode code, OTResult result,
                            void *cookie)
{
    long who = (long)context;
    int  k, x;
    if (gQuitting || gN == NULL) return;       /* endpoints being closed */
    if (who == kLisCookie)
    {
        switch (code)
        {
        case T_OPENCOMPLETE:
            gLis = (EndpointRef)cookie;
            gLisOpenErr = (long)result;
            gLisOpened = 1;
            break;
        case T_LISTEN:
            ListenPump();
            break;
        case T_ACCEPTCOMPLETE:
            k = gAcceptSlot;
            gAcceptBusy = 0;
            gAcceptSlot = -1;
            if (result != kOTNoError && k >= 0)
            {
                mcn_log(gN, k, MCN_L_NOTE, (unsigned long)result, 0,
                        "T_ACCEPTCOMPLETE error: slot recycled");
                mcn_set_state(gN, k, MCN_OFF);
                gSlot[k].st = SL_ABORT;          /* event loop: idle -> FREE */
                gSlot[k].t0 = gTick0;
            }
            ListenPump();
            break;
        case T_DISCONNECT:
            ListenerDiscon();
            break;
        default:
            break;
        }
        return;
    }
    if (who < 1 || who > MCPO_SLOTS) return;
    k = (int)who - 1;
    x = gN->s[k].st;
    switch (code)
    {
    case T_OPENCOMPLETE:
        gSlot[k].ep = (EndpointRef)cookie;
        gSlot[k].openErr = (long)result;
        gSlot[k].opened = 1;
        break;
    case T_PASSCON:
        if (x == MCN_ACCEPTING || x == MCN_REQ) mcn_passcon(gN, k);
        break;
    case T_DATA:
        if (mcn_owns_events(gN, k)) mcn_on_data(gN, k);
        break;
    case T_GODATA:
        if (mcn_owns_events(gN, k)) mcn_on_godata(gN, k);
        break;
    case T_ORDREL:
        if (x >= MCN_ACCEPTING && x <= MCN_LOOP
            && OTRcvOrderlyDisconnect(gSlot[k].ep) == kOTNoError)
            mcn_on_fin(gN, k);
        break;
    case T_DISCONNECT:
        if (x >= MCN_ACCEPTING && x <= MCN_LOOP
            && OTRcvDisconnect(gSlot[k].ep, NULL) == kOTNoError)
            mcn_on_disc(gN, k);
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* event loop: the ring, READY slots, kicks                            */
/* ------------------------------------------------------------------ */

static void FlushRing(void)
{
    McnRec r;
    char   t[200];
    static const char *names[] = { "?", "ACCEPT", "REQ", "SERVE", "PASS",
        "SENT", "FIN", "DISC", "ERR", "FLOW", "NOTE" };
    if (gN == NULL) return;
    while (mcn_next_rec(gN, &r))
    {
        sprintf(t, "proxy: [notifier t=%lu] n%d slot%d %s a=%lu b=%lu%s%s",
                r.tick, r.id, r.slot,
                (r.code >= 1 && r.code <= MCN_L_NOTE) ? names[r.code] : "?",
                r.a, r.b, r.txt ? ": " : "", r.txt ? r.txt : "");
        Log(t);
    }
    if (gN->dropped != gDropLogged)
    {
        gDropLogged = gN->dropped;
        Logf1("proxy: notifier log ring dropped %ld records so far",
              (long)gDropLogged, 0, 0);
    }
}

static void FreeSlot(Slot *s)
{
    s->st = SL_FREE;
    s->rp = s->xp = NULL;
    s->rpn = s->rpo = s->xpn = s->xpo = 0;
    s->fin_told = 0;
    if (gN != NULL) mcn_set_state(gN, s->idx, MCN_FREE);
}

/* abortive disconnect (if connected); the loop then waits for T_IDLE */
static void HardClose(Slot *s)
{
    OTResult st;
    OSStatus e = kOTNoError;
    (void)OTEnterNotifier(s->ep);
    st = OTGetEndpointState(s->ep);
    if (st == T_DATAXFER || st == T_OUTREL || st == T_INREL || st == T_INCON)
        e = OTSndDisconnect(s->ep, NULL);
    mcn_set_state(gN, s->idx, MCN_OFF);
    OTLeaveNotifier(s->ep);
    if (e != kOTNoError)
        Logf1("proxy: slot%ld close: state %ld, OTSndDisconnect -> %ld",
              (long)s->idx, (long)st, (long)e);
    s->st = SL_ABORT;
    s->t0 = (unsigned long)TickCount();
}

static int TakeReady(void)
{
    int     k, ok, any = 0;
    McnTake t;
    for (k = 0; k < MCPO_SLOTS; k++)
    {
        Slot *s = &gSlot[k];
        if (gN->s[k].st != MCN_READY) continue;
        (void)OTEnterNotifier(s->ep);
        ok = mcn_take(gN, k, &t);
        OTLeaveNotifier(s->ep);
        if (!ok) continue;
        any = 1;
        FlushRing();
        s->rp = s->xp = NULL;
        s->rpn = s->rpo = s->xpn = s->xpo = 0;
        s->fin_told = 0;
        if (t.why == MCN_WHY_DISC || t.why == MCN_WHY_ERR)
        {
            Logf1(t.why == MCN_WHY_DISC
                  ? "proxy: n%ld: QuickTime disconnected at notifier time, "
                    "%ld body bytes sent"
                  : "proxy: n%ld: send failed at notifier time, %ld body "
                    "bytes sent", (long)t.id, (long)t.body_sent, 0);
            HardClose(s);
            continue;
        }
        s->st = SL_BUSY;
        if (t.why == MCN_WHY_SERVED)
        {
            if (mcp_adopt_downstream(gSrv, (void *)s, t.req, t.total,
                                     t.body_sent) < 0)
            {
                Logf1("proxy: n%ld could not be adopted: hard close",
                      (long)t.id, 0, 0);
                HardClose(s);
            }
            continue;
        }
        /* PASS / FIN: mc_proxy reads the same request bytes (0.5s path) */
        s->rp = t.replay;
        s->rpn = t.nreplay;
        (void)mcp_accept(gSrv, (void *)s);   /* -1: 503 sent, D_Close done */
    }
    return any;
}

/* re-pump slots that hit MCN_GUARD; drop a slot whose peer half-closed
   and stopped reading (no T_GODATA will ever come) */
static void Kick(void)
{
    int k;
    for (k = 0; k < MCPO_SLOTS; k++)
    {
        McnSlot *q = &gN->s[k];
        if (q->st != MCN_SEND) continue;
        if (q->need_kick)
        {
            (void)OTEnterNotifier(gSlot[k].ep);
            mcn_kick(gN, k);
            OTLeaveNotifier(gSlot[k].ep);
        }
        if (q->st == MCN_SEND && q->fin && q->in_flow
            && (unsigned long)TickCount() - q->t_flow > MCPO_SEND_STALL)
        {
            Logf1("proxy: n%ld: QuickTime closed and stopped reading at body "
                  "%ld: disconnecting", (long)q->id, (long)q->body_sent, 0);
            HardClose(&gSlot[k]);
        }
    }
}

/* ------------------------------------------------------------------ */
/* the memory downstream (pre-buffer fetch through the core)           */
/* ------------------------------------------------------------------ */

static long M_Read(char *p, unsigned long cap)
{
    unsigned long n;
    if (gMemAbort) return MCP_IO_ERR;
    n = gMemReqLen - gMem.rpo;
    if (n > cap) n = cap;
    memcpy(p, gMemReq + gMem.rpo, (size_t)n);
    gMem.rpo += n;
    return (long)n;
}

static long M_Write(const char *p, unsigned long n)
{
    unsigned long used = 0, end, k;
    if (gMemAbort) return MCP_IO_ERR;
    if (!gMemHeadDone)
    {
        k = sizeof gMemHead - 1UL - gMemHeadLen;
        if (k > n) k = n;
        memcpy(gMemHead + gMemHeadLen, p, (size_t)k);
        end = mcp_head_end(gMemHead, gMemHeadLen + k, 0);
        if (end == 0)
        {
            gMemHeadLen += k;
            return gMemHeadLen >= sizeof gMemHead - 1UL ? MCP_IO_ERR : (long)n;
        }
        used = end - gMemHeadLen;
        gMemHeadLen = end;
        gMemHeadDone = 1;
        if (mcp_resp_parse(gMemHead, end, &gMemResp) != 0) return MCP_IO_ERR;
        if (gMemResp.status == 206 && gMemResp.has_crange == 1
            && gMemResp.cr_total != MCP_UNKNOWN)
        {
            gMemTotal = gMemResp.cr_total;
            gMemExpect = gMemResp.cr_e - gMemResp.cr_s + 1UL;
        }
        else if (gMemResp.status == 200 && gMemResp.has_length)
            gMemTotal = gMemExpect = gMemResp.length;
        else
        {
            Logf1("proxy: pre-buffer request answered %ld", (long)gMemResp.status, 0, 0);
            return MCP_IO_ERR;
        }
        if (gMemExpect > gPreCap) gMemExpect = gPreCap;
    }
    k = n - used;
    if (gMemGot + k > gPreCap) return MCP_IO_ERR;
    memcpy(gPre + gMemGot, p + used, (size_t)k);
    gMemGot += k;
    return (long)n;
}

static void M_Close(int abortive)
{
    gMemInCore = 0;
    gMemDone = (!abortive && !gMemAbort && gMemHeadDone && gMemExpect > 0
                && gMemGot == gMemExpect) ? 1 : -1;
}

/* ------------------------------------------------------------------ */
/* downstream (QuickTime's connection, or gMem) for the core           */
/* ------------------------------------------------------------------ */

static long D_Read(void *h, char *p, unsigned long cap)
{
    Slot    *s = (Slot *)h;
    OTFlags  flags = 0;
    OTResult r;
    long     out, n;
    if (s->mem) return M_Read(p, cap);
    if (s->rpo < s->rpn)                       /* replay (PASS / FIN) */
    {
        n = (long)(s->rpn - s->rpo);
        if ((unsigned long)n > cap) n = (long)cap;
        memcpy(p, s->rp + s->rpo, (size_t)n);
        s->rpo += (unsigned long)n;
        return n;
    }
    if (s->xpo < s->xpn)
    {
        n = (long)(s->xpn - s->xpo);
        if ((unsigned long)n > cap) n = (long)cap;
        memcpy(p, s->xp + s->xpo, (size_t)n);
        s->xpo += (unsigned long)n;
        return n;
    }
    (void)OTEnterNotifier(s->ep);
    if (gN->s[s->idx].disc) out = MCP_IO_ERR;
    else if (gN->s[s->idx].fin) { s->fin_told = 1; out = MCP_IO_EOF; }
    else
    {
        r = OTRcv(s->ep, p, (OTByteCount)cap, &flags);
        if (r > 0) out = (long)r;
        else if (r == kOTNoDataErr) out = 0;
        else if (r == kOTLookErr)
        {
            OTResult ev = OTLook(s->ep);
            out = 0;
            if (ev == T_ORDREL && OTRcvOrderlyDisconnect(s->ep) == kOTNoError)
            { gN->s[s->idx].fin = 1; s->fin_told = 1; out = MCP_IO_EOF; }
            else if (ev == T_DISCONNECT && OTRcvDisconnect(s->ep, NULL) == kOTNoError)
            { gN->s[s->idx].disc = 1; out = MCP_IO_ERR; }
        }
        else out = -1000L + (long)r;           /* logged below */
    }
    OTLeaveNotifier(s->ep);
    if (out < -999L)
    {
        Logf1("proxy: slot%ld OTRcv -> %ld (state %ld)", (long)s->idx,
              out + 1000L, (long)OTGetEndpointState(s->ep));
        out = MCP_IO_ERR;
    }
    return out;
}

static long D_Write(void *h, const char *p, unsigned long n)
{
    Slot    *s = (Slot *)h;
    OTResult r;
    long     out;
    if (s->mem) return M_Write(p, n);
    (void)OTEnterNotifier(s->ep);
    if (gN->s[s->idx].disc) out = MCP_IO_ERR;
    else
    {
        r = OTSnd(s->ep, (void *)p, (OTByteCount)n, 0);
        if (r >= 0) out = (long)r;
        else if (r == kOTFlowErr || r == kOTLookErr) out = 0;
        else out = -1000L + (long)r;
    }
    OTLeaveNotifier(s->ep);
    if (out < -999L)
    {
        Logf1("proxy: slot%ld OTSnd -> %ld (state %ld)", (long)s->idx,
              out + 1000L, (long)OTGetEndpointState(s->ep));
        out = MCP_IO_ERR;
    }
    return out;
}

static void D_Close(void *h, int abortive)
{
    Slot    *s = (Slot *)h;
    OTResult st;
    OSStatus e = kOTNoError;
    if (s->mem) { M_Close(abortive); return; }
    if (abortive) { HardClose(s); return; }
    (void)OTEnterNotifier(s->ep);
    st = OTGetEndpointState(s->ep);
    if (st == T_DATAXFER || st == T_INREL) e = OTSndOrderlyDisconnect(s->ep);
    OTLeaveNotifier(s->ep);
    if ((st != T_DATAXFER && st != T_INREL && st != T_IDLE) || e != kOTNoError)
    { HardClose(s); return; }
    s->st = SL_CLOSING;                 /* T_OUTREL: wait for QuickTime's */
    s->t0 = (unsigned long)TickCount();
}

/* finish closes: drain, take QuickTime's release, wait for T_IDLE */
static int PollClosing(void)
{
    int      i, k, any = 0;
    OTFlags  flags;
    OTResult r, st;
    for (i = 0; i < MCPO_SLOTS; i++)
    {
        Slot *s = &gSlot[i];
        if (s->st != SL_CLOSING && s->st != SL_ABORT) continue;
        (void)OTEnterNotifier(s->ep);
        for (k = 0; k < 4 && s->st == SL_CLOSING; k++)
        {
            if (OTGetEndpointState(s->ep) == T_IDLE) break;
            flags = 0;
            r = OTRcv(s->ep, gDrain, (OTByteCount)sizeof gDrain, &flags);
            if (r > 0) { any = 1; continue; }
            if (r == kOTLookErr)
            {
                OTResult ev = OTLook(s->ep);
                if (ev == T_ORDREL) (void)OTRcvOrderlyDisconnect(s->ep);
                else if (ev == T_DISCONNECT) (void)OTRcvDisconnect(s->ep, NULL);
                continue;
            }
            break;
        }
        st = OTGetEndpointState(s->ep);
        OTLeaveNotifier(s->ep);
        if (st == T_IDLE) { FreeSlot(s); any = 1; continue; }
        if (s->st == SL_CLOSING
            && (unsigned long)TickCount() - s->t0 > MCPO_LINGER)
        {
            Logf1("proxy: slot%ld: no orderly release from QuickTime in %ld "
                  "ticks: disconnecting", (long)s->idx, (long)MCPO_LINGER, 0);
            HardClose(s);
        }
        else if (s->st == SL_ABORT
                 && (unsigned long)TickCount() - s->t0 > 120UL)
        {
            Logf1("proxy: slot%ld state %ld 2 s after its disconnect: out of "
                  "use", (long)s->idx, (long)st, 0);
            s->st = SL_DEAD;
            mcn_set_state(gN, s->idx, MCN_OFF);
        }
    }
    return any;
}

/* ------------------------------------------------------------------ */
/* opening / closing                                                   */
/* ------------------------------------------------------------------ */

static void CloseEndpoints(void)
{
    int i;
    gQuitting = 1;                     /* the notifier ignores everything */
    gArmed = 0;
    if (gLis != kOTInvalidEndpointRef)
        for (i = 0; i < gNCalls; i++) (void)OTSndDisconnect(gLis, &gCall[i]);
    gNCalls = 0;
    for (i = 0; i < MCPO_SLOTS; i++)
    {
        if (gSlot[i].ep != kOTInvalidEndpointRef) (void)OTCloseProvider(gSlot[i].ep);
        gSlot[i].ep = kOTInvalidEndpointRef;
        gSlot[i].st = SL_DEAD;
    }
    if (gLis != kOTInvalidEndpointRef) (void)OTCloseProvider(gLis);
    gLis = kOTInvalidEndpointRef;
    if (gN != NULL) { DisposePtr((Ptr)gN); gN = NULL; }
}

/* sync bind of an async-opened acceptor (QTSniffN.c NSetupAcceptor) */
static int SetupAcceptor(Slot *s)
{
    OSStatus e;
    (void)OTSetSynchronous(s->ep);
    (void)OTSetNonBlocking(s->ep);
    e = OTBind(s->ep, NULL, NULL);
    (void)OTSetAsynchronous(s->ep);
    if (e != kOTNoError)
    {
        Logf1("proxy: slot%ld OTBind -> %ld", (long)s->idx, (long)e, 0);
        return 0;
    }
    s->st = SL_FREE;
    mcn_set_state(gN, s->idx, MCN_FREE);
    return 1;
}

static int OpenNotifierEndpoints(void)
{
    McnIo       io;
    OSStatus    err;
    EventRecord ev;
    unsigned long t0;
    int         i, all = 0;

    gN = (McnState *)NewPtr((Size)sizeof(McnState));
    if (gN == NULL)
    {
        Logf1("proxy: NewPtr(%ld) for the notifier state failed",
              (long)sizeof(McnState), 0, 0);
        return -1;
    }
    memset(&io, 0, sizeof io);
    io.ticks = N_Ticks;
    io.recv = N_Recv;
    io.send = N_Send;
    OTGetTimeStamp(&gStamp0);
    gTick0 = (unsigned long)TickCount();
    mcn_init(gN, &io);
    gQuitting = 0;
    gArmed = 0;
    gAcceptBusy = 0;
    gAcceptSlot = -1;
    gLisOpened = 0;
    if (gUPP == NULL) gUPP = NewOTNotifyUPP(Notifier);
    err = OTAsyncOpenEndpoint(OTCreateConfiguration(kTCPName), 0, NULL, gUPP,
                              (void *)kLisCookie);
    if (err != kOTNoError)
    {
        Logf1("proxy: listener OTAsyncOpenEndpoint -> %ld", (long)err, 0, 0);
        return -1;
    }
    for (i = 0; i < MCPO_SLOTS; i++)
    {
        gSlot[i].opened = 0;
        err = OTAsyncOpenEndpoint(OTCreateConfiguration(kTCPName), 0, NULL,
                                  gUPP, (void *)(long)(i + 1));
        if (err != kOTNoError)
        {
            Logf1("proxy: slot%ld OTAsyncOpenEndpoint -> %ld", (long)i,
                  (long)err, 0);
            return -1;
        }
    }
    t0 = (unsigned long)TickCount();
    for (;;)                                   /* wait for T_OPENCOMPLETE */
    {
        all = gLisOpened;
        for (i = 0; i < MCPO_SLOTS; i++) all = all && gSlot[i].opened;
        if (all || (unsigned long)TickCount() - t0 > 600UL) break;
        (void)WaitNextEvent(0, &ev, 1, NULL);  /* no event is taken */
    }
    if (!all) { Log("proxy: T_OPENCOMPLETE did not arrive within 10 s"); return -1; }
    if (gLisOpenErr != kOTNoError || gLis == kOTInvalidEndpointRef)
    {
        Logf1("proxy: listener open -> %ld", gLisOpenErr, 0, 0);
        return -1;
    }
    for (i = 0; i < MCPO_SLOTS; i++)
        if (gSlot[i].openErr != kOTNoError || gSlot[i].ep == kOTInvalidEndpointRef
            || !SetupAcceptor(&gSlot[i]))
        {
            Logf1("proxy: acceptor slot%ld open %ld / bind failed", (long)i,
                  gSlot[i].openErr, 0);
            return -1;
        }
    return 0;
}

int mcpo_open(void (*log)(void *ref, const char *line), void *ref,
              unsigned short *port)
{
    McpOps    ops;
    McpConfig cfg;
    OSStatus  err = kOTNoError;
    UInt16    base, p, bport = 0;
    int       i;
    char      t[160];

    gLog = log;
    gLogRef = ref;
    if (gSrv != NULL) { if (port) *port = gPort; return 0; }
    if (mcpo_test_fail) { Log("proxy: (test) listener disabled"); return -1; }
    for (i = 0; i < MCPO_SLOTS; i++)
    {
        memset(&gSlot[i], 0, sizeof gSlot[i]);
        gSlot[i].ep = kOTInvalidEndpointRef;
        gSlot[i].st = SL_DEAD;
        gSlot[i].idx = i;
    }
    memset(&gMem, 0, sizeof gMem);
    gMem.mem = 1;
    gMem.idx = -1;
    gNCalls = 0;
    gAnyAddr = 0;
    gLis = kOTInvalidEndpointRef;
    if (OpenNotifierEndpoints() != 0) goto fail;
    (void)OTSetSynchronous(gLis);
    (void)OTSetNonBlocking(gLis);
    base = (UInt16)(mcpo_test_base_port ? mcpo_test_base_port : MCPO_BASE_PORT);
    err = kOTAddressBusyErr;
    for (p = base; p < base + MCPO_NPORTS && err != kOTNoError; p++)
    {
        err = BindListener(kLoopbackIP, p, 1, &bport);
        if (err != kOTNoError)
            Logf1("proxy: 127.0.0.1:%ld is taken (%ld)", (long)p, (long)err, 0);
    }
    if (err != kOTNoError)
    {
        err = BindListener(kLoopbackIP, 0, 1, &bport);
        Logf1("proxy: 127.0.0.1:OT-assigned -> %ld", (long)err, 0, 0);
    }
    if (err != kOTNoError)
    {
        err = BindListener(0, 0, 0, &bport);
        Logf1("proxy: any address, OT-assigned -> %ld (non-local calls will "
              "be refused)", (long)err, 0, 0);
        if (err == kOTNoError) gAnyAddr = 1;
    }
    (void)OTSetAsynchronous(gLis);
    if (err != kOTNoError || bport == 0) goto fail;

    memset(&ops, 0, sizeof ops);
    ops.up_open = U_Open;
    ops.up_step = U_Step;
    ops.up_write = U_Write;
    ops.up_read = U_Read;
    ops.up_close = U_Close;
    ops.down_read = D_Read;
    ops.down_write = D_Write;
    ops.down_close = D_Close;
    ops.now = P_Now;
    ops.log = P_Log;
    ops.alloc = P_Alloc;
    ops.release = P_Release;
    memset(&cfg, 0, sizeof cfg);
    cfg.max_conns = MCPO_MAX_CONNS;
    cfg.park_ticks = MCPO_PARK_TICKS;
    cfg.allow_host = P_Allow;
    gSrv = mcp_new(&ops, &cfg);
    if (gSrv == NULL) { Log("proxy: out of memory for the proxy core"); goto fail; }
    gPort = bport;
    if (port) *port = bport;
    (void)OTEnterNotifier(gLis);
    gArmed = 1;
    ListenPump();                       /* a call that came in already */
    OTLeaveNotifier(gLis);
    sprintf(t, "proxy: LISTENING 127.0.0.1:%u (notifier mode: %d async "
            "acceptors, %lu bytes of state)", (unsigned)bport, MCPO_SLOTS,
            (unsigned long)sizeof(McnState));
    Log(t);
    if (bport != MCPO_BASE_PORT && !mcpo_test_base_port)
        Log("proxy: (streaming movies made on another port will not play "
            "this session)");
    return 0;
fail:
    CloseEndpoints();
    Log("proxy: no listener: https films will be downloaded first");
    return -1;
}

int mcpo_is_open(void) { return gSrv != NULL; }
unsigned short mcpo_port(void) { return gSrv != NULL ? gPort : 0; }
McpServer *mcpo_server(void) { return gSrv; }
void mcpo_set_insecure(int on) { gInsecure = on; }

int mcpo_poll(void)
{
    int any = 0, k, freed = 0;
    if (gSrv == NULL) return 0;
    FlushRing();
    any |= TakeReady();
    Kick();
    any |= mcp_poll(gSrv) > 0;
    freed = PollClosing();
    any |= freed;
    if (freed || gNCalls > 0)
    {   /* a slot came free: calls may be waiting */
        (void)OTEnterNotifier(gLis);
        ListenPump();
        OTLeaveNotifier(gLis);
    }
    for (k = 0; k < MCPO_SLOTS; k++) if (mcn_busy(gN, k)) any = 1;
    FlushRing();
    return any;
}

int mcpo_active(void)
{
    int k, n;
    if (gSrv == NULL) return 0;
    n = mcp_active(gSrv) - (gMemInCore ? 1 : 0);
    for (k = 0; k < MCPO_SLOTS; k++) if (mcn_busy(gN, k)) n++;
    return n;
}

int mcpo_parked(void) { return gSrv != NULL ? mcp_parked(gSrv) : 0; }

int mcpo_closing(void)
{
    int i, n = 0;
    for (i = 0; i < MCPO_SLOTS; i++)
        if (gSrv != NULL && (gSlot[i].st == SL_CLOSING || gSlot[i].st == SL_ABORT)) n++;
    return n;
}

void mcpo_cancel_all(void)
{
    int i;
    if (gSrv == NULL) return;
    gMemAbort = 1;
    mcp_cancel_all(gSrv);
    for (i = 0; i < MCPO_SLOTS; i++)
        if (gSlot[i].st == SL_CLOSING || (gSlot[i].st == SL_FREE && mcn_busy(gN, i)))
            HardClose(&gSlot[i]);
}

void mcpo_close(void)
{
    if (gSrv == NULL) return;
    mcpo_cancel_all();
    mcp_free(gSrv);
    gSrv = NULL;
    FlushRing();
    CloseEndpoints();
    if (gPre != NULL) { DisposePtr((Ptr)gPre); gPre = NULL; gPreAlloc = 0; }
    Log("proxy: closed");
}

/* ------------------------------------------------------------------ */
/* pre-buffer API                                                      */
/* ------------------------------------------------------------------ */

int mcpo_prebuf_start(const char *target, unsigned long want)
{
    char *w;
    if (gSrv == NULL) return -1;
    mcn_prebuf_off(gN);                 /* new calls go to the event loop */
    if (gMemInCore || mcn_prebuf_busy(gN)) return 1;      /* try again */
    if (strlen(target) + 1 > sizeof gMemTarget) return -1;
    if (want == 0) want = mcpo_test_prebuf ? mcpo_test_prebuf : MCPO_PREBUF;
    if (gPre != NULL && gPreAlloc < want)
    {   /* (0.5u) a bigger film: nothing sends from the old Ptr (busy
           was checked above) */
        DisposePtr((Ptr)gPre);
        gPre = NULL;
        gPreAlloc = 0;
    }
    if (gPre == NULL)
    {
        gPre = (char *)NewPtr((Size)want);
        if (gPre == NULL)
        {
            Logf1("proxy: NewPtr(%ld) for the pre-buffer failed: raise the "
                  "partition", (long)want, 0, 0);
            return -1;
        }
        gPreAlloc = want;
    }
    gPreCap = want;
    strcpy(gMemTarget, target);
    w = gMemReq;
    w += sprintf(w, "GET %s HTTP/1.1\015\012Host: 127.0.0.1\015\012Range: "
                 "bytes=0-%lu\015\012\015\012", target, gPreCap - 1UL);
    gMemReqLen = (unsigned long)(w - gMemReq);
    gMem.rpo = 0;
    gMemHeadLen = gMemGot = gMemExpect = gMemTotal = 0;
    gMemHeadDone = gMemAbort = 0;
    gMemDone = 0;
    gMemInCore = 1;
    if (mcp_accept(gSrv, (void *)&gMem) < 0)
    {
        gMemInCore = 0;
        gMemDone = -1;
    }
    return 0;
}

int mcpo_prebuf_poll(unsigned long *got, unsigned long *want,
                     unsigned long *total)
{
    char h206[MCN_HEAD], h200[MCN_HEAD];
    unsigned long l206, l200;
    const char *ct;
    *got = gMemGot;
    *want = gMemExpect ? gMemExpect : gPreCap;
    *total = gMemTotal;
    if (gMemDone == 2) return 1;
    if (gMemDone <= 0) return gMemDone;
    ct = gMemResp.ctype[0] ? gMemResp.ctype : "video/quicktime";
    l206 = mcp_build_head(h206, sizeof h206, 206, 0, 0, gMemTotal, gMemTotal, ct, 0);
    l200 = mcp_build_head(h200, sizeof h200, 200, 0, 0, gMemTotal, gMemTotal, ct, 0);
    if (l206 == 0 || l200 == 0) return -1;
    mcn_set_prebuf(gN, gMemTarget, gPre, gMemGot, gMemTotal, h206, l206,
                   h200, l200);
    {
        char t[200];
        sprintf(t, "proxy: PREBUF %lu of %lu bytes in RAM: QuickTime's "
                "GET is answered at notifier time", gMemGot, gMemTotal);
        Log(t);
    }
    gMemDone = 2;                       /* published once */
    return 1;
}

void mcpo_prebuf_cancel(void)
{
    if (gMemInCore) gMemAbort = 1;
    if (gN != NULL) mcn_prebuf_off(gN);
}
