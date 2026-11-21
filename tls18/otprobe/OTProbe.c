/*
    OTProbe.c (b18 rewrite of codec-src/OTProbe.c) -- classic (non-Carbon)
    PPC test: can a classic app OTBind a LISTENING TCP endpoint to a port
    it chooses, and accept a loopback connection from itself?

    Changes from the original (which used printf/getchar under SIOUX and
    could wait forever for Return):
      * no console, no windows, no dialogs: results go to the text file
        "OTProbe Log" beside the application; the app quits by itself.
      * every endpoint is synchronous NON-blocking; every wait is a polling
        loop bounded by TickCount (kWaitTicks), so nothing can hang.
      * new part B: loopback test inside one app. Listener bound with
        qlen 1 (127.0.0.1:8080, else 0.0.0.0:8080, else OT-chosen port),
        a client endpoint connects to 127.0.0.1:<port>, the listener
        OTListen + OTAccept onto a third endpoint, client sends "PING-b18",
        acceptor answers "PONG", both sides log what they got.
    Strict C89.
*/
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <MacTypes.h>
#include <Memory.h>
#include <Files.h>
#include <Events.h>
#include <Processes.h>
#include <Quickdraw.h>
#include <Fonts.h>
#include <Windows.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <Script.h>
#include <OpenTransport.h>
#include <OpenTptInternet.h>

#define kLogFileName "OTProbe Log"
#define kProbePort   8080
#define kLoopbackIP  0x7F000001UL
#define kWaitTicks   600L          /* 10 s per wait */

static short gLogRef = 0;

static void Log(const char *fmt, ...)
{
    char buf[600];
    va_list ap;
    long n, i;
    va_start(ap, fmt);
    vsprintf(buf, fmt, ap);
    va_end(ap);
    n = (long)strlen(buf);
    for (i = 0; i < n; i++) if (buf[i] == '\n') buf[i] = '\r';
    if (gLogRef != 0 && n > 0) (void)FSWrite(gLogRef, &n, buf);
}

static short OpenLog(void)
{
    ProcessSerialNumber psn;
    ProcessInfoRec info;
    Str255 pname;
    FSSpec app, spec;
    short ref;
    OSErr err;
    static const unsigned char kName[] = "\pOTProbe Log";

    if (GetCurrentProcess(&psn) != noErr) return 0;
    memset(&info, 0, sizeof(info));
    info.processInfoLength = sizeof(info);
    info.processName = pname;
    info.processAppSpec = &app;
    if (GetProcessInformation(&psn, &info) != noErr) return 0;
    err = FSMakeFSSpec(app.vRefNum, app.parID, kName, &spec);
    if (err == fnfErr) err = FSpCreate(&spec, 'ttxt', 'TEXT', smSystemScript);
    if (err != noErr) return 0;
    if (FSpOpenDF(&spec, fsRdWrPerm, &ref) != noErr) return 0;
    (void)SetEOF(ref, 0);
    return ref;
}

static void Pause(void)
{
    EventRecord ev;
    (void)WaitNextEvent(everyEvent, &ev, 1, NULL);
}

static const char *IPStr(UInt32 ip, char *b)
{
    sprintf(b, "%lu.%lu.%lu.%lu", (ip >> 24) & 0xFFUL, (ip >> 16) & 0xFFUL,
            (ip >> 8) & 0xFFUL, ip & 0xFFUL);
    return b;
}

static EndpointRef OpenTCP(const char *what)
{
    OSStatus err;
    EndpointRef ep;
    ep = OTOpenEndpoint(OTCreateConfiguration(kTCPName), 0, NULL, &err);
    if (ep == kOTInvalidEndpointRef || err != kOTNoError) {
        Log("    %s: OTOpenEndpoint failed, err = %ld\n", what, (long)err);
        return kOTInvalidEndpointRef;
    }
    (void)OTSetSynchronous(ep);
    (void)OTSetNonBlocking(ep);
    return ep;
}

/* Bind; returns err; boundIP and boundPort get what OT reports. */
static OSStatus DoBind(EndpointRef ep, int nullReq, int explicitAddr,
                       UInt32 ip, UInt16 port, OTQLen qlen,
                       UInt32 *boundIP, UInt16 *boundPort)
{
    TBind req, ret;
    InetAddress reqAddr, retAddr;
    OSStatus err;
    memset(&req, 0, sizeof req); memset(&ret, 0, sizeof ret);
    memset(&retAddr, 0, sizeof retAddr);
    ret.addr.buf = (UInt8 *)&retAddr;
    ret.addr.maxlen = sizeof retAddr;
    if (nullReq) {
        err = OTBind(ep, NULL, NULL);
    } else {
        if (explicitAddr) {
            OTInitInetAddress(&reqAddr, port, ip);
            req.addr.buf = (UInt8 *)&reqAddr;
            req.addr.len = sizeof reqAddr;
            req.addr.maxlen = sizeof reqAddr;
        }
        req.qlen = qlen;
        err = OTBind(ep, &req, &ret);
    }
    if (boundIP) *boundIP = retAddr.fHost;
    if (boundPort) *boundPort = retAddr.fPort;
    return err;
}

static void RunBindCase(const char *label, int nullReq, int explicitAddr,
                        UInt32 ip, UInt16 port, OTQLen qlen)
{
    EndpointRef ep;
    OSStatus err;
    UInt32 bip = 0;
    UInt16 bport = 0;
    char b1[20];
    Log("\n--- %s\n", label);
    ep = OpenTCP("probe");
    if (ep == kOTInvalidEndpointRef) return;
    err = DoBind(ep, nullReq, explicitAddr, ip, port, qlen, &bip, &bport);
    if (err == kOTNoError) {
        Log("    OTBind -> OK, bound to %s:%u\n", IPStr(bip, b1), (unsigned)bport);
        if (explicitAddr && port != 0) Log("    *** chosen port accepted ***\n");
        (void)OTUnbind(ep);
    } else {
        Log("    OTBind -> FAILED, err = %ld%s\n", (long)err,
            err == -3150 ? "  (kOTBadAddressErr, same as CarbonLib)" : "");
    }
    (void)OTCloseProvider(ep);
}

/* Part B: loopback connect + accept + exchange inside this app. */
static void LoopbackTest(void)
{
    EndpointRef lis, acc, cli;
    OSStatus err;
    UInt32 bip = 0, t0;
    UInt16 bport = 0, port;
    TCall call, sndcall;
    InetAddress callAddr, dst;
    char b1[20], rbuf[64];
    OTFlags flags;
    OTResult r;
    long got;
    int stage, connected;
    OSStatus err0;

    Log("\n=== B. Loopback: listen, connect from same app, accept, exchange ===\n");
    lis = OpenTCP("listener");
    acc = OpenTCP("acceptor");
    cli = OpenTCP("client");
    if (lis == kOTInvalidEndpointRef || acc == kOTInvalidEndpointRef ||
        cli == kOTInvalidEndpointRef) goto done;

    err = DoBind(lis, 0, 1, kLoopbackIP, kProbePort, 1, &bip, &bport);
    Log("  listener bind 127.0.0.1:%u qlen 1 -> %ld\n", (unsigned)kProbePort, (long)err);
    if (err != kOTNoError) {
        err = DoBind(lis, 0, 1, 0, kProbePort, 1, &bip, &bport);
        Log("  listener bind 0.0.0.0:%u qlen 1 -> %ld\n", (unsigned)kProbePort, (long)err);
    }
    if (err != kOTNoError) {
        err = DoBind(lis, 0, 0, 0, 0, 1, &bip, &bport);
        Log("  listener bind OT-chosen qlen 1 -> %ld\n", (long)err);
    }
    if (err != kOTNoError) { Log("  RESULT: no listener could be bound.\n"); goto done; }
    Log("  listener bound to %s:%u\n", IPStr(bip, b1), (unsigned)bport);
    port = bport;

    err = DoBind(acc, 1, 0, 0, 0, 0, NULL, NULL);
    Log("  acceptor bind (NULL) -> %ld\n", (long)err);
    err = DoBind(cli, 1, 0, 0, 0, 0, NULL, NULL);
    Log("  client bind (NULL) -> %ld\n", (long)err);
    if (err != kOTNoError) goto done;

    memset(&sndcall, 0, sizeof sndcall);
    OTInitInetAddress(&dst, port, kLoopbackIP);
    sndcall.addr.buf = (UInt8 *)&dst;
    sndcall.addr.len = sizeof dst;
    err = OTConnect(cli, &sndcall, NULL);
    err0 = err;
    Log("  client OTConnect 127.0.0.1:%u -> %ld (-3162 = in progress, normal)\n", (unsigned)port, (long)err);
    if (err != kOTNoError && err != kOTNoDataErr) goto done;

    /* wait for the call on the listener */
    stage = 0;
    t0 = TickCount();
    while (TickCount() - t0 < (UInt32)kWaitTicks) {
        memset(&call, 0, sizeof call);
        memset(&callAddr, 0, sizeof callAddr);
        call.addr.buf = (UInt8 *)&callAddr;
        call.addr.maxlen = sizeof callAddr;
        err = OTListen(lis, &call);
        if (err == kOTNoError) { stage = 1; break; }
        if (err == kOTLookErr) Log("  listener OTLook = 0x%lx\n", (unsigned long)OTLook(lis));
        else if (err != kOTNoDataErr) break;
        Pause();
    }
    Log("  listener OTListen -> %ld after %lu ticks%s\n", (long)err,
        (unsigned long)(TickCount() - t0), stage ? "" : "  (no call arrived)");
    if (!stage) goto done;
    Log("  call from %s:%u\n", IPStr(callAddr.fHost, b1), (unsigned)callAddr.fPort);

    connected = (err0 == kOTNoError);   /* sync OTConnect may finish at once */
    err = OTAccept(lis, acc, &call);
    Log("  OTAccept(listener -> acceptor) -> %ld\n", (long)err);
    if (err != kOTNoError) goto done;

    t0 = TickCount();
    while (!connected && TickCount() - t0 < (UInt32)kWaitTicks) {
        err = OTRcvConnect(cli, NULL);
        if (err != kOTNoDataErr) { connected = (err == kOTNoError); break; }
        Pause();
    }
    if (err0 == kOTNoError) err = kOTNoError;
    else Log("  client OTRcvConnect -> %ld after %lu ticks\n", (long)err, (unsigned long)(TickCount() - t0));
    if (err != kOTNoError) goto done;

    r = OTSnd(cli, (void *)"PING-b18", 8, 0);
    Log("  client OTSnd 8 bytes -> %ld\n", (long)r);
    got = 0;
    t0 = TickCount();
    while (TickCount() - t0 < (UInt32)kWaitTicks && got < 8) {
        flags = 0;
        r = OTRcv(acc, rbuf + got, (OTByteCount)(sizeof rbuf - 1 - got), &flags);
        if (r > 0) got += r;
        else if (r != kOTNoDataErr) { Log("  acceptor OTRcv -> %ld\n", (long)r); break; }
        else Pause();
    }
    rbuf[got] = 0;
    Log("  acceptor received %ld bytes: \"%s\"\n", got, rbuf);

    r = OTSnd(acc, (void *)"PONG", 4, 0);
    Log("  acceptor OTSnd 4 bytes -> %ld\n", (long)r);
    got = 0;
    t0 = TickCount();
    while (TickCount() - t0 < (UInt32)kWaitTicks && got < 4) {
        flags = 0;
        r = OTRcv(cli, rbuf + got, (OTByteCount)(sizeof rbuf - 1 - got), &flags);
        if (r > 0) got += r;
        else if (r != kOTNoDataErr) { Log("  client OTRcv -> %ld\n", (long)r); break; }
        else Pause();
    }
    rbuf[got] = 0;
    Log("  client received %ld bytes: \"%s\"\n", got, rbuf);
    if (got == 4 && memcmp(rbuf, "PONG", 4) == 0)
        Log("  RESULT: LOOPBACK OK (listen on port %u, accept, two-way exchange)\n", (unsigned)port);
    else
        Log("  RESULT: loopback exchange incomplete\n");

    (void)OTSndDisconnect(cli, NULL);
done:
    if (cli != kOTInvalidEndpointRef) (void)OTCloseProvider(cli);
    if (acc != kOTInvalidEndpointRef) (void)OTCloseProvider(acc);
    if (lis != kOTInvalidEndpointRef) (void)OTCloseProvider(lis);
}

int main(void)
{
    OSStatus err;

    MaxApplZone();
    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();
    FlushEvents(everyEvent, 0);

    gLogRef = OpenLog();
    Log("OTProbe b18 -- classic PPC passive bind + loopback test\n");
#if defined(TARGET_API_MAC_CARBON) && TARGET_API_MAC_CARBON
    Log("!!! BUILT AGAINST CARBON -- TEST INVALID !!!\n");
#else
    Log("Build: classic PPC (not Carbon).\n");
#endif
    err = InitOpenTransport();
    if (err != kOTNoError) {
        Log("InitOpenTransport failed, err = %ld. Quitting.\n", (long)err);
        goto out;
    }
    Log("InitOpenTransport OK.\n\n=== A. Bind matrix ===\n");
    RunBindCase("1. OTBind(ep, NULL, NULL)", 1, 0, 0, 0, 0);
    RunBindCase("2. OTBind(addr = NULL, qlen = 1)", 0, 0, 0, 0, 1);
    RunBindCase("3. OTBind(127.0.0.1:8080, qlen = 1)", 0, 1, kLoopbackIP, kProbePort, 1);
    RunBindCase("4. OTBind(0.0.0.0:8080, qlen = 1)", 0, 1, 0, kProbePort, 1);
    RunBindCase("5. OTBind(0.0.0.0:0, qlen = 1)", 0, 1, 0, 0, 1);
    LoopbackTest();
    CloseOpenTransport();
out:
    Log("\nDone.\n");
    if (gLogRef) { (void)FSClose(gLogRef); gLogRef = 0; }
    return 0;
}
