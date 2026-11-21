/*
 * fake_mac.c -- HOST ONLY. BSD-sockets/POSIX stand-in for exactly the
 * Open Transport and Toolbox calls used by the macTLS glue,
 * https_client.c and (for link checks) HTTPSGet.c.
 *
 * Open Transport model emulated:
 *   - OTAsyncOpenEndpoint queues T_OPENCOMPLETE (cookie = endpoint).
 *   - OTBind (async) queues T_BINDCOMPLETE.
 *   - OTConnect (async) returns kOTNoDataErr; DNS happens here (blocking
 *     getaddrinfo; a failure is reported as T_DISCONNECT); the outcome
 *     arrives later as T_CONNECT or T_DISCONNECT.
 *   - OTSnd/OTRcv are non-blocking: kOTFlowErr / kOTNoDataErr; T_GODATA
 *     and T_DATA are edge-triggered after such a return.
 *   - peer FIN -> T_ORDREL event, OTRcv returns kOTLookErr and OTLook
 *     returns T_ORDREL.  RST -> T_DISCONNECT likewise.
 * The notifier ("interrupt time" on the Mac) runs only from
 * FakeOT_Poll(), which the host driver calls from its idle callback.
 */

#include "machdr/ot_fake.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

/* ---------------- clocks ---------------- */

static double mono_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec / 1e9;
}

UInt32 TickCount(void) { return (UInt32)(mono_seconds() * 60.0); }

void Microseconds(UnsignedWide *u)
{
    unsigned long long us = (unsigned long long)(mono_seconds() * 1e6);
    u->hi = (UInt32)(us >> 32);
    u->lo = (UInt32)us;
}

void GetMouse(Point *p) { p->h = 0; p->v = 0; }

/* Mac epoch is 1904-01-01. FAKE_MAC_SECONDS overrides (clock tests). */
void GetDateTime(UInt32 *secs)
{
    const char *o = getenv("FAKE_MAC_SECONDS");
    if (o && *o) { *secs = (UInt32)strtoul(o, NULL, 10); return; }
    *secs = (UInt32)((unsigned long long)time(NULL) + 2082844800ULL);
}

void ReadLocation(MachineLocation *loc)
{
    memset(loc, 0, sizeof *loc);
}

/* ---------------- memory ---------------- */

static OSErr g_memerr = 0;
Ptr NewPtr(Size n) { Ptr p = (Ptr)malloc(n ? (size_t)n : 1); g_memerr = p ? 0 : memFullErr; return p; }
Ptr NewPtrClear(Size n) { Ptr p = (Ptr)calloc(1, n ? (size_t)n : 1); g_memerr = p ? 0 : memFullErr; return p; }
void DisposePtr(Ptr p) { free(p); }
OSErr MemError(void) { return g_memerr; }
void MaxApplZone(void) {}
void BlockMoveData(const void *s, void *d, Size n) { memmove(d, s, (size_t)n); }

/* ---------------- files ----------------
 * dirID 1 = "app folder", 2 = Preferences, 3 = Desktop. All live under
 * $FAKE_MAC_DIR (default ./fakemac). */

static const char *root_dir(void)
{
    const char *r = getenv("FAKE_MAC_DIR");
    return (r && *r) ? r : "fakemac";
}

/* (b19) sub-folders of the app folder get dirIDs 100+ */
#define MAXDIRS 16
static char g_dirname[MAXDIRS][64];
static int  g_ndirs;

static void dir_path(long dirID, char *out, size_t cap)
{
    const char *sub = dirID == 2 ? "Preferences" : dirID == 3 ? "Desktop" : "App";
    snprintf(out, cap, "%s/%s", root_dir(), sub);
    mkdir(root_dir(), 0777);
    mkdir(out, 0777);
    if (dirID >= 100 && dirID < 100 + g_ndirs) {
        size_t l = strlen(out);
        snprintf(out + l, cap - l, "/%s", g_dirname[dirID - 100]);
    }
}

static long dir_id_for(const char *name)
{
    int i;
    for (i = 0; i < g_ndirs; i++) if (strcmp(g_dirname[i], name) == 0) return 100 + i;
    if (g_ndirs >= MAXDIRS) return -1;
    snprintf(g_dirname[g_ndirs], sizeof g_dirname[0], "%s", name);
    return 100 + g_ndirs++;
}

static void spec_path(const FSSpec *s, char *out, size_t cap)
{
    char d[512];
    dir_path(s->parID, d, sizeof d);
    snprintf(out, cap, "%s/%.*s", d, (int)s->name[0], (const char *)s->name + 1);
}

OSErr FindFolder(short v, OSType t, Boolean create, short *fv, long *fd)
{
    (void)v; (void)create;
    *fv = -1;
    *fd = (t == kPreferencesFolderType) ? 2 : (t == kDesktopFolderType) ? 3 : 1;
    return noErr;
}

OSErr FSMakeFSSpec(short vRefNum, long dirID, ConstStr255Param name, FSSpec *spec)
{
    char p[1024];
    struct stat st;
    int n = name ? name[0] : 0;
    /* Linux gcc has no "\p": a "\pName" literal arrives as "pName"
       (warning: unknown escape). Treat a length byte > 63 as that case. */
    if (n > 63) n = (int)strnlen((const char *)name + 1, 63);
    spec->vRefNum = vRefNum;
    spec->parID = dirID;
    spec->name[0] = (unsigned char)n;
    if (n) memcpy(spec->name + 1, name + 1, (size_t)n);
    spec_path(spec, p, sizeof p);
    return stat(p, &st) == 0 ? noErr : fnfErr;
}

#define MAXF 32
static FILE *g_files[MAXF];

OSErr FSpCreate(const FSSpec *spec, OSType c, OSType t, ScriptCode s)
{
    char p[1024];
    FILE *f;
    struct stat st;
    (void)c; (void)t; (void)s;
    spec_path(spec, p, sizeof p);
    if (stat(p, &st) == 0) return -48; /* dupFNErr */
    f = fopen(p, "wb");
    if (!f) return -34;
    fclose(f);
    return noErr;
}

OSErr FSpDelete(const FSSpec *spec)
{
    char p[1024];
    spec_path(spec, p, sizeof p);
    return unlink(p) == 0 ? noErr : fnfErr;
}

OSErr FSpOpenDF(const FSSpec *spec, SInt8 perm, short *ref)
{
    char p[1024];
    int i;
    FILE *f;
    spec_path(spec, p, sizeof p);
    f = fopen(p, perm == fsRdPerm ? "rb" : "r+b");
    if (!f) return fnfErr;
    for (i = 1; i < MAXF; i++) if (!g_files[i]) { g_files[i] = f; *ref = (short)i; return noErr; }
    fclose(f);
    return -42; /* tmfoErr */
}

static FILE *ref_file(short r) { return (r > 0 && r < MAXF) ? g_files[r] : NULL; }

OSErr FSRead(short r, long *count, void *buf)
{
    FILE *f = ref_file(r);
    size_t n;
    if (!f) return -51;
    n = fread(buf, 1, (size_t)*count, f);
    if ((long)n < *count) { *count = (long)n; return eofErr; }
    return noErr;
}

OSErr FSWrite(short r, long *count, const void *buf)
{
    FILE *f = ref_file(r);
    if (!f) return -51;
    *count = (long)fwrite(buf, 1, (size_t)*count, f);
    return noErr;
}

OSErr FSClose(short r)
{
    FILE *f = ref_file(r);
    if (!f) return -51;
    fclose(f);
    g_files[r] = NULL;
    return noErr;
}

OSErr SetEOF(short r, long eof) { FILE *f = ref_file(r); if (!f) return -51; fflush(f); return ftruncate(fileno(f), eof) == 0 ? noErr : -34; }
OSErr GetEOF(short r, long *eof) { FILE *f = ref_file(r); struct stat st; if (!f) return -51; fflush(f); fstat(fileno(f), &st); *eof = (long)st.st_size; return noErr; }
OSErr SetFPos(short r, short mode, long off) { FILE *f = ref_file(r); if (!f) return -51; return fseek(f, off, mode == fsFromLEOF ? SEEK_END : SEEK_SET) == 0 ? noErr : -40; }
OSErr GetFPos(short r, long *pos) { FILE *f = ref_file(r); if (!f) return -51; *pos = ftell(f); return noErr; }
OSErr FlushVol(ConstStr63Param name, short v) { (void)name; (void)v; return noErr; }
OSErr PBHSetVolSync(WDPBRec *pb) { (void)pb; return noErr; }

/* ---------------- processes / events / init ---------------- */

QDGlobals qd;
OSErr GetCurrentProcess(ProcessSerialNumber *psn) { psn->highLongOfPSN = 0; psn->lowLongOfPSN = 2; return noErr; }
OSErr GetProcessInformation(const ProcessSerialNumber *psn, ProcessInfoRec *info)
{
    (void)psn;
    if (info->processAppSpec) {
        info->processAppSpec->vRefNum = -1;
        info->processAppSpec->parID = 1;
        memcpy(info->processAppSpec->name, "\010HTTPSGet", 9);
    }
    return noErr;
}
Boolean WaitNextEvent(UInt16 m, EventRecord *ev, UInt32 s, RgnHandle r) { (void)m; (void)r; memset(ev, 0, sizeof *ev); FakeOT_Poll(); if (s) usleep(300); return 0; }
void FlushEvents(UInt16 m, UInt16 s) { (void)m; (void)s; }
OSErr AEInstallEventHandler(UInt32 c, UInt32 i, AEEventHandlerUPP h, long rc, Boolean s) { (void)c; (void)i; (void)h; (void)rc; (void)s; return noErr; }
OSErr AEProcessAppleEvent(const EventRecord *ev) { (void)ev; return noErr; }
void InitGraf(void *p) { (void)p; }
void InitFonts(void) {}
void InitWindows(void) {}
void InitMenus(void) {}
void TEInit(void) {}
void InitDialogs(void *p) { (void)p; }
void InitCursor(void) {}
OSErr YieldToAnyThread(void) { return noErr; }
OSErr Gestalt(OSType s, long *r) { (void)s; *r = 0; return -5551; }

/* ---------------- Open Transport ---------------- */

struct OTFakeEP {
    int fd;
    int state;               /* T_* endpoint state */
    OTNotifyUPP notifier;
    void *ctx;
    int want_data;           /* arm T_DATA */
    int want_godata;         /* arm T_GODATA */
    int connecting;
    int look;                /* pending look event (T_ORDREL / T_DISCONNECT) */
    int look_notified;
    int dead;
    /* (J5) passive side */
    int listening;           /* a bound listener (qlen > 0)           */
    int accepted;            /* fd came from OTAccept: close at T_IDLE */
    int pend_fd[8];          /* accepted-by-the-kernel calls (OTListen) */
    OTSequence pend_seq[8];
    int npend;
    int want_listen;         /* (J5b) arm T_LISTEN (notifier listeners) */
    OTSequence next_seq;
    /* queued completion events */
    OTEventCode q_code[4];
    OTResult q_res[4];
    int q_n;
    struct OTFakeEP *next;
};

static struct OTFakeEP *g_eps = NULL;
static int g_ot_inited = 0;
unsigned long g_fake_ot_stats[4];   /* opens, closes, sends, recvs */

static void q_push(struct OTFakeEP *ep, OTEventCode c, OTResult r)
{
    if (ep->q_n < 4) { ep->q_code[ep->q_n] = c; ep->q_res[ep->q_n] = r; ep->q_n++; }
}

OSStatus InitOpenTransport(void) { g_ot_inited = 1; return noErr; }
void CloseOpenTransport(void) { g_ot_inited = 0; }

static struct OTConfiguration { int dummy; } g_cfg;
OTConfigurationRef OTCreateConfiguration(const char *path) { (void)path; return &g_cfg; }

static struct OTFakeEP *new_ep(void)
{
    struct OTFakeEP *ep = (struct OTFakeEP *)calloc(1, sizeof *ep);
    ep->fd = -1;
    ep->state = T_UNBND;
    ep->next = g_eps;
    g_eps = ep;
    g_fake_ot_stats[0]++;
    return ep;
}

EndpointRef OTOpenEndpoint(OTConfigurationRef cfg, OTOpenFlags f, TEndpointInfo *info, OSStatus *err)
{
    (void)cfg; (void)f;
    if (info) memset(info, 0, sizeof *info);
    *err = noErr;
    return new_ep();
}

OSStatus OTAsyncOpenEndpoint(OTConfigurationRef cfg, OTOpenFlags f, TEndpointInfo *info, OTNotifyUPP n, void *ctx)
{
    struct OTFakeEP *ep;
    (void)cfg; (void)f;
    if (!g_ot_inited) return -3201;
    if (info) memset(info, 0, sizeof *info);
    ep = new_ep();
    ep->notifier = n;
    ep->ctx = ctx;
    q_push(ep, T_OPENCOMPLETE, 0);
    return noErr;
}

static OSStatus bind_listener(EndpointRef ep, TBind *req, TBind *ret);   /* (J5) */
OSStatus OTBind(EndpointRef ep, TBind *req, TBind *ret)
{
    if (req && req->qlen > 0) return bind_listener(ep, req, ret);   /* (J5) */
    ep->state = T_IDLE;
    if (ep->notifier) q_push(ep, T_BINDCOMPLETE, 0);
    return noErr;
}

OTByteCount OTInitDNSAddress(DNSAddress *a, char *str)
{
    size_t n = strlen(str);
    if (n > 255) n = 255;
    a->fAddressType = AF_DNS;
    memcpy(a->fName, str, n);
    a->fName[n] = 0;
    return (OTByteCount)(sizeof(OTAddressType) + n + 1);
}

OSStatus OTConnect(EndpointRef ep, TCall *snd, TCall *rcv)
{
    DNSAddress *a;
    char host[256], *colon, portstr[8];
    struct addrinfo hints, *res = NULL;
    int fd, rc, one = 1;
    (void)rcv;
    a = (DNSAddress *)(void *)snd->addr.buf;
    if (a->fAddressType != AF_DNS) return kOTBadAddressErr;
    snprintf(host, sizeof host, "%s", a->fName);
    colon = strrchr(host, ':');
    if (!colon) return kOTBadAddressErr;
    *colon = 0;
    snprintf(portstr, sizeof portstr, "%s", colon + 1);
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    ep->state = T_OUTCON;
    if (getaddrinfo(host, portstr, &hints, &res) != 0 || !res) {
        q_push(ep, T_DISCONNECT, kOTBadNameErr);
        ep->dead = 1;
        ep->look = T_DISCONNECT;
        return kOTNoDataErr;
    }
    fd = socket(AF_INET, SOCK_STREAM, 0);
    fcntl(fd, F_SETFL, O_NONBLOCK);
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    rc = connect(fd, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    ep->fd = fd;
    if (rc != 0 && errno != EINPROGRESS) {
        q_push(ep, T_DISCONNECT, -3260);
        ep->dead = 1;
        ep->look = T_DISCONNECT;
        return kOTNoDataErr;
    }
    ep->connecting = 1;
    return kOTNoDataErr;
}

OSStatus OTRcvConnect(EndpointRef ep, TCall *call) { (void)call; ep->state = T_DATAXFER; ep->want_data = 1; return noErr; }

OTResult OTSnd(EndpointRef ep, void *buf, OTByteCount n, OTFlags flags)
{
    ssize_t r;
    (void)flags;
    if (ep->look) return kOTLookErr;
    if (ep->state != T_DATAXFER && ep->state != T_INREL) return kOTOutStateErr;
    g_fake_ot_stats[2]++;
    r = send(ep->fd, buf, n, MSG_NOSIGNAL);
    if (r >= 0) return (OTResult)r;
    if (errno == EAGAIN || errno == EWOULDBLOCK) { ep->want_godata = 1; return kOTFlowErr; }
    ep->look = T_DISCONNECT;
    if (ep->accepted && ep->notifier && !ep->look_notified) {   /* (J5b) real OT delivers it */
        ep->look_notified = 1;
        q_push(ep, T_DISCONNECT, 0);
    }
    return kOTLookErr;
}

OTResult OTRcv(EndpointRef ep, void *buf, OTByteCount n, OTFlags *flags)
{
    ssize_t r;
    if (flags) *flags = 0;
    if (ep->look) return kOTLookErr;
    if (ep->fd < 0) return kOTOutStateErr;
    if (ep->accepted && ep->state != T_DATAXFER && ep->state != T_OUTREL) return kOTOutStateErr;   /* (J5) */
    g_fake_ot_stats[3]++;
    r = recv(ep->fd, buf, n, 0);
    if (r > 0) return (OTResult)r;
    if (r == 0) { ep->look = T_ORDREL; return kOTLookErr; }
    if (errno == EAGAIN || errno == EWOULDBLOCK) { ep->want_data = 1; return kOTNoDataErr; }
    ep->look = T_DISCONNECT;
    return kOTLookErr;
}

OTResult OTLook(EndpointRef ep) { return ep->look; }
OTResult OTGetEndpointState(EndpointRef ep) { return ep->state; }

/* (J5) an accepted connection that reached T_IDLE gives its socket back
   (real OT: the acceptor endpoint is idle and reusable) */
static void idle_release(EndpointRef ep)
{
    if (ep->accepted && ep->state == T_IDLE && ep->fd >= 0)
    { close(ep->fd); ep->fd = -1; ep->accepted = 0; }
}

OSStatus OTSndOrderlyDisconnect(EndpointRef ep)
{
    if (ep->accepted) {   /* (J5) real OT's checks, passive side only */
        if (ep->look == T_DISCONNECT) return kOTLookErr;
        if (ep->state != T_DATAXFER && ep->state != T_INREL) return kOTOutStateErr;
    }
    if (ep->fd >= 0) shutdown(ep->fd, SHUT_WR);
    ep->state = (ep->state == T_INREL) ? T_IDLE : T_OUTREL;
    idle_release(ep);
    return noErr;
}

OSStatus OTRcvOrderlyDisconnect(EndpointRef ep)
{
    if (ep->look != T_ORDREL) return kOTNoDataErr;
    ep->look = 0;
    ep->state = (ep->state == T_OUTREL) ? T_IDLE : T_INREL;
    idle_release(ep);
    return noErr;
}

static void close_pending(EndpointRef lis, OTSequence seq);
OSStatus OTSndDisconnect(EndpointRef ep, TCall *c)
{
    if (ep->listening) {             /* (J5) reject a pending call */
        if (c) close_pending(ep, c->sequence);
        return noErr;
    }
    if (ep->accepted && ep->fd >= 0) {   /* abortive: the peer sees a RST */
        struct linger l; l.l_onoff = 1; l.l_linger = 0;
        setsockopt(ep->fd, SOL_SOCKET, SO_LINGER, &l, sizeof l);
    }
    if (ep->fd >= 0) shutdown(ep->fd, SHUT_RDWR);
    ep->state = T_IDLE; ep->look = 0;
    idle_release(ep);
    return noErr;
}
OSStatus OTRcvDisconnect(EndpointRef ep, void *d)
{
    if (d) memset(d, 0, sizeof(TDiscon));
    ep->look = 0; ep->state = T_IDLE;
    idle_release(ep);
    return noErr;
}

/* ---------------- (J5) listener ---------------- */
int g_fake_ot_bind_any = 0;
void OTInitInetAddress(InetAddress *a, InetPort port, InetHost host)
{
    memset(a, 0, sizeof *a);
    a->fAddressType = AF_INET_OT;
    a->fPort = port;
    a->fHost = host;
}

static void close_pending(EndpointRef lis, OTSequence seq)
{
    int i;
    for (i = 0; i < lis->npend; i++)
        if (lis->pend_seq[i] == seq) {
            close(lis->pend_fd[i]);
            memmove(lis->pend_fd + i, lis->pend_fd + i + 1, sizeof(int) * (size_t)(lis->npend - i - 1));
            memmove(lis->pend_seq + i, lis->pend_seq + i + 1, sizeof(OTSequence) * (size_t)(lis->npend - i - 1));
            lis->npend--;
            return;
        }
}

/* req->qlen > 0: a listener. Real OT answers kOTAddressBusyErr for a TCP
   port in use (OTProbe/QTSniff fall back on any error). */
static OSStatus bind_listener(EndpointRef ep, TBind *req, TBind *ret)
{
    struct sockaddr_in a;
    socklen_t al = sizeof a;
    InetAddress *ia = (req->addr.len >= sizeof(InetAddress)) ? (InetAddress *)(void *)req->addr.buf : NULL;
    int fd = socket(AF_INET, SOCK_STREAM, 0), one = 1;
    if (fd < 0) return -3211;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(ia ? ia->fPort : 0);
    a.sin_addr.s_addr = htonl((ia && (ia->fHost != 0 || g_fake_ot_bind_any)) ? ia->fHost : 0x7F000001UL);
    if (bind(fd, (struct sockaddr *)&a, sizeof a) != 0) { close(fd); return kOTAddressBusyErr; }
    if (listen(fd, (int)req->qlen) != 0) { close(fd); return kOTAddressBusyErr; }
    fcntl(fd, F_SETFL, O_NONBLOCK);
    getsockname(fd, (struct sockaddr *)&a, &al);
    ep->fd = fd;
    ep->listening = 1;
    ep->want_listen = 1;
    ep->state = T_IDLE;
    if (ret) {
        ret->qlen = req->qlen;
        if (ret->addr.buf && ret->addr.maxlen >= sizeof(InetAddress)) {
            OTInitInetAddress((InetAddress *)(void *)ret->addr.buf, ntohs(a.sin_port), ntohl(a.sin_addr.s_addr));
            ret->addr.len = sizeof(InetAddress);
        }
    }
    return noErr;
}

OSStatus OTUnbind(EndpointRef ep)
{
    if (ep->state != T_IDLE) return kOTOutStateErr;
    while (ep->npend > 0) close_pending(ep, ep->pend_seq[0]);
    if (ep->fd >= 0) { close(ep->fd); ep->fd = -1; }
    ep->listening = 0;
    ep->state = T_UNBND;
    return noErr;
}

OSStatus OTListen(EndpointRef ep, TCall *call)
{
    struct sockaddr_in a;
    socklen_t al = sizeof a;
    int fd;
    if (!ep->listening) return kOTOutStateErr;
    if (ep->npend < 8) {
        fd = accept(ep->fd, (struct sockaddr *)&a, &al);
        if (fd >= 0) {
            ep->pend_fd[ep->npend] = fd;
            ep->pend_seq[ep->npend] = ++ep->next_seq;
            ep->npend++;
            if (call) {
                call->sequence = ep->next_seq;
                if (call->addr.buf && call->addr.maxlen >= sizeof(InetAddress)) {
                    OTInitInetAddress((InetAddress *)(void *)call->addr.buf, ntohs(a.sin_port), ntohl(a.sin_addr.s_addr));
                    call->addr.len = sizeof(InetAddress);
                }
            }
            return noErr;
        }
    }
    ep->want_listen = 1;
    return kOTNoDataErr;
}

OSStatus OTAccept(EndpointRef lis, EndpointRef res, TCall *call)
{
    int i, one = 1;
    if (!lis->listening || !call) return kOTOutStateErr;
    if (res->state != T_IDLE || res->fd >= 0 || res->listening) return kOTOutStateErr;
    for (i = 0; i < lis->npend; i++)
        if (lis->pend_seq[i] == call->sequence) {
            res->fd = lis->pend_fd[i];
            memmove(lis->pend_fd + i, lis->pend_fd + i + 1, sizeof(int) * (size_t)(lis->npend - i - 1));
            memmove(lis->pend_seq + i, lis->pend_seq + i + 1, sizeof(OTSequence) * (size_t)(lis->npend - i - 1));
            lis->npend--;
            fcntl(res->fd, F_SETFL, O_NONBLOCK);
            setsockopt(res->fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
            {   /* OT's TCP send buffering is small; Linux would autotune to
                   MBs and hide back-pressure (a paused QuickTime) */
                int sb = 32768;
                setsockopt(res->fd, SOL_SOCKET, SO_SNDBUF, &sb, sizeof sb);
            }
            res->state = T_DATAXFER;
            res->accepted = 1;
            res->look = 0; res->look_notified = 0; res->dead = 0;
            res->want_data = 1;
            if (lis->notifier) {     /* (J5b) async: real OT order unknown */
                q_push(res, T_PASSCON, 0);
                q_push(lis, T_ACCEPTCOMPLETE, 0);
            }
            return noErr;
        }
    return -3156;   /* kOTBadSequenceErr */
}

OSStatus OTCloseProvider(ProviderRef ep)
{
    struct OTFakeEP **pp;
    if (!ep) return -3150;
    for (pp = &g_eps; *pp; pp = &(*pp)->next) if (*pp == ep) { *pp = ep->next; break; }
    while (ep->npend > 0) close_pending(ep, ep->pend_seq[0]);   /* (J5) */
    if (ep->fd >= 0) close(ep->fd);
    free(ep);
    g_fake_ot_stats[1]++;
    return noErr;
}

OSStatus OTSetAsynchronous(ProviderRef ep) { (void)ep; return noErr; }
OSStatus OTSetSynchronous(ProviderRef ep) { (void)ep; return noErr; }
OSStatus OTSetNonBlocking(ProviderRef ep) { (void)ep; return noErr; }
OSStatus OTSetBlocking(ProviderRef ep) { (void)ep; return noErr; }
OSStatus OTUseSyncIdleEvents(ProviderRef ep, Boolean b) { (void)ep; (void)b; return noErr; }
OSStatus OTOptionManagement(EndpointRef ep, TOptMgmt *req, TOptMgmt *ret)
{
    (void)req; (void)ret;
    if (ep->notifier) q_push(ep, T_OPTMGMTCOMPLETE, 0);
    return noErr;
}
void OTMemzero(void *p, OTByteCount n) { memset(p, 0, n); }

static void fire(struct OTFakeEP *ep, OTEventCode c, OTResult r, void *cookie)
{
    if (ep->notifier) ep->notifier(ep->ctx, c, r, cookie);
}

/* (J5b) OTEnterNotifier: in this single-threaded fake, notifiers run only
   from FakeOT_Poll, so "deferring" them = not polling while depth > 0 */
unsigned long g_fake_ot_enter_depth = 0, g_fake_ot_starve_notifies = 0;
static int g_in_poll = 0;
Boolean OTEnterNotifier(ProviderRef ep) { (void)ep; g_fake_ot_enter_depth++; return 1; }
void OTLeaveNotifier(ProviderRef ep) { (void)ep; if (g_fake_ot_enter_depth) g_fake_ot_enter_depth--; }
void OTGetTimeStamp(OTTimeStamp *t) { double s = mono_seconds(); t->hi = (UInt32)(s / 4294.967296); t->lo = (UInt32)((s - (double)t->hi * 4294.967296) * 1e6); }
UInt32 OTElapsedMilliseconds(OTTimeStamp *t) { double s = (double)t->hi * 4294.967296 + (double)t->lo / 1e6; return (UInt32)((mono_seconds() - s) * 1000.0); }

void FakeOT_Poll(void)
{
    struct OTFakeEP *ep, *next;
    if (g_fake_ot_enter_depth > 0 || g_in_poll) return;
    g_in_poll = 1;
    for (ep = g_eps; ep; ep = next) {
        next = ep->next;
        while (ep->q_n > 0) {
            OTEventCode c = ep->q_code[0];
            OTResult r = ep->q_res[0];
            memmove(ep->q_code, ep->q_code + 1, sizeof ep->q_code[0] * 3);
            memmove(ep->q_res, ep->q_res + 1, sizeof ep->q_res[0] * 3);
            ep->q_n--;
            fire(ep, c, r, c == T_OPENCOMPLETE ? (void *)ep : NULL);
        }
        if (ep->listening && ep->notifier && ep->want_listen && ep->fd >= 0) {   /* (J5b) */
            struct pollfd lp;
            lp.fd = ep->fd; lp.events = POLLIN; lp.revents = 0;
            if (poll(&lp, 1, 0) > 0 && (lp.revents & POLLIN)) { ep->want_listen = 0; fire(ep, T_LISTEN, 0, NULL); }
        }
        if (ep->fd < 0 || ep->dead || ep->listening) continue;   /* (J5) listeners: OTListen polls */
        {
            struct pollfd pf;
            pf.fd = ep->fd;
            pf.events = POLLIN | (ep->connecting || ep->want_godata ? POLLOUT : 0);
            pf.revents = 0;
            if (poll(&pf, 1, 0) <= 0) continue;
            if (ep->connecting && (pf.revents & (POLLOUT | POLLERR | POLLHUP))) {
                int err = 0;
                socklen_t len = sizeof err;
                getsockopt(ep->fd, SOL_SOCKET, SO_ERROR, &err, &len);
                ep->connecting = 0;
                if (err == 0) fire(ep, T_CONNECT, 0, NULL);
                else { ep->dead = 1; ep->look = T_DISCONNECT; fire(ep, T_DISCONNECT, -3260, NULL); }
                continue;
            }
            if (ep->want_godata && (pf.revents & POLLOUT)) { ep->want_godata = 0; fire(ep, T_GODATA, 0, NULL); }
            if (pf.revents & (POLLIN | POLLHUP | POLLERR)) {
                char c;
                ssize_t pr = recv(ep->fd, &c, 1, MSG_PEEK | MSG_DONTWAIT);
                if (pr > 0) {
                    if (ep->want_data) { ep->want_data = 0; fire(ep, T_DATA, 0, NULL); }
                } else if (!ep->look_notified && (pr == 0 || (errno != EAGAIN && errno != EWOULDBLOCK))) {
                    ep->look_notified = 1;
                    ep->look = (pr == 0) ? T_ORDREL : T_DISCONNECT;
                    fire(ep, pr == 0 ? T_ORDREL : T_DISCONNECT, 0, NULL);
                }
            }
        }
    }
    g_in_poll = 0;
}

/* (J5b) the synchronous open of QuickTime Player 6: Media Center's event
   loop gets no time, only Open Transport's deferred tasks run */
void FakeOT_Starve(double secs)
{
    double end = mono_seconds() + secs;
    while (mono_seconds() < end) { FakeOT_Poll(); g_fake_ot_starve_notifies++; usleep(500); }
}

/* ---------------- (b19) catalog, volume, rename, folders ---------------- */
OSErr PBGetCatInfoSync(CInfoPBPtr pb)
{
    FSSpec s; char p[1024]; struct stat st;
    s.vRefNum = pb->dirInfo.ioVRefNum; s.parID = pb->dirInfo.ioDrDirID;
    memcpy(s.name, pb->dirInfo.ioNamePtr, (size_t)pb->dirInfo.ioNamePtr[0] + 1);
    spec_path(&s, p, sizeof p);
    if (stat(p, &st) != 0) return fnfErr;
    if (S_ISDIR(st.st_mode)) {
        char nm[64];
        if (s.parID != 1) return paramErr;      /* only app-folder subdirs */
        snprintf(nm, sizeof nm, "%.*s", (int)s.name[0], (const char *)s.name + 1);
        pb->dirInfo.ioFlAttrib = (SInt8)ioDirMask;
        pb->dirInfo.ioDrDirID = dir_id_for(nm);
    } else {
        pb->hFileInfo.ioFlAttrib = 0;
        pb->hFileInfo.ioFlLgLen = (long)st.st_size;
    }
    return noErr;
}

/* FAKE_FREE_KB = free space to report (default 1 GB). */
OSErr PBHGetVInfoSync(HParmBlkPtr pb)
{
    const char *f = getenv("FAKE_FREE_KB");
    unsigned long kb = (f && *f) ? strtoul(f, NULL, 10) : 1048576UL;
    pb->volumeParam.ioVAlBlkSiz = 32768;
    pb->volumeParam.ioVFrBlk = (unsigned short)(kb / 32 > 65535 ? 65535 : kb / 32);
    return noErr;
}

OSErr FSpDirCreate(const FSSpec *spec, ScriptCode script, long *created)
{
    char p[1024], nm[64];
    (void)script;
    if (spec->parID != 1) return paramErr;
    spec_path(spec, p, sizeof p);
    if (mkdir(p, 0777) != 0) return dupFNErr;
    snprintf(nm, sizeof nm, "%.*s", (int)spec->name[0], (const char *)spec->name + 1);
    *created = dir_id_for(nm);
    return noErr;
}

OSErr FSpRename(const FSSpec *spec, ConstStr255Param newName)
{
    char a[1024], b[1024]; FSSpec t; struct stat st;
    spec_path(spec, a, sizeof a);
    t = *spec;
    memcpy(t.name, newName, (size_t)newName[0] + 1);
    spec_path(&t, b, sizeof b);
    if (stat(b, &st) == 0) return dupFNErr;
    return rename(a, b) == 0 ? noErr : fnfErr;
}
