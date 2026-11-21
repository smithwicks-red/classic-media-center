/*
    HTTPSGet.c  --  classic Mac OS test tool: fetch https:// (and http://)
                    URLs with macTLS + https_client, log what happened,
                    quit by itself.

    No windows, no dialogs, no alerts: nothing can click in the guest.

    On launch it reads the text file "HTTPS Get URL" beside the
    application. One entry per line (CR, LF or CRLF line ends):

        https://example.com/                 fetch, full verification
        insecure https://self-signed.local/  fetch, certificate NOT checked
        save Movie.mp4 https://host/a.mp4    fetch, write body to the file
                                             "Movie.mp4" beside the app
        insecure save X https://...          both
        range 0-99 https://host/file         Range: bytes=0-99 (also
                                             "range 100-" open-ended);
                                             may be combined, before URL
        tls12                                (alone) offer TLS 1.2 only
                                             from here on
        debuglog                             (alone) also write macTLS's
                                             own trace "MacTLSTest.log"
                                             in the Desktop Folder
        # comment                            ignored

    Without the file it fetches two default URLs (archive.org JSON search
    and example.com). Results go to "HTTPS Get Log" (TEXT, CR line ends)
    beside the application. Then it quits. A Quit Apple event stops it
    between pump steps.

    Strict C89. Wire/log text uses \015 so CodeWarrior's newline mapping
    does not matter.
*/
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

#include <MacTypes.h>
#include <Memory.h>
#include <Files.h>
#include <Events.h>
#include <Processes.h>
#include <AppleEvents.h>
#include <Quickdraw.h>
#include <Fonts.h>
#include <Windows.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <Script.h>
#include <OpenTransport.h>

#include "ostls_async.h"
#include "ostls_time.h"
#include "ostls_entropy.h"
#include "ostls_log.h"
#include "ostls_tls13_selftest.h"
#include "https_client.h"

#define kURLFileName   "HTTPS Get URL"
#define kLogFileName   "HTTPS Get Log"
#define kMaxURLFile    16384L
#define kPreviewBytes  200
#define kAppVersion    "0.1 (tls18)"

static const char *kDefaultURLs[2] = {
    "https://archive.org/advancedsearch.php?q=popeye&fl%5B%5D=identifier&rows=3&output=json",
    "https://example.com/"
};

static short gLogRef = 0;
static int   gQuit = 0;

/* ---------------- log (FSWrite, CR line ends, bounded formatter) ------ */

static void LogRaw(const char *s, long n)
{
    long count;
    if (gLogRef == 0 || n <= 0) return;
    count = n;
    (void)FSWrite(gLogRef, &count, s);
}

static void LogS(const char *s)
{
    LogRaw(s, (long)strlen(s));
}

static void LogUL(unsigned long v, int base, int width)
{
    char tmp[16];
    int n;
    n = 0;
    do {
        tmp[n++] = "0123456789ABCDEF"[v % (unsigned long)base];
        v /= (unsigned long)base;
    } while (v != 0 && n < 15);
    while (n < width && n < 15) tmp[n++] = '0';
    while (n > 0) { n--; LogRaw(&tmp[n], 1); }
}

/* Supports %s %c %d %ld %u %lu %x %lx and %0Nx / %0Nlx; '\n' in the
   format becomes CR. */
static void Log(const char *fmt, ...)
{
    va_list ap;
    const char *p;
    int is_long, width;
    long sv;
    unsigned long uv;
    char c;

    va_start(ap, fmt);
    for (p = fmt; *p; p++) {
        if (*p == '\n') { LogRaw("\015", 1); continue; }
        if (*p != '%') { LogRaw(p, 1); continue; }
        p++;
        width = 0;
        if (*p == '0') {
            p++;
            while (*p >= '0' && *p <= '9') { width = width * 10 + (*p - '0'); p++; }
        }
        is_long = 0;
        if (*p == 'l') { is_long = 1; p++; }
        switch (*p) {
        case 's': LogS(va_arg(ap, const char *)); break;
        case 'c': c = (char)va_arg(ap, int); LogRaw(&c, 1); break;
        case 'd':
            sv = is_long ? va_arg(ap, long) : (long)va_arg(ap, int);
            if (sv < 0) { LogRaw("-", 1); uv = (unsigned long)(-sv); }
            else uv = (unsigned long)sv;
            LogUL(uv, 10, width);
            break;
        case 'u':
            uv = is_long ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned int);
            LogUL(uv, 10, width);
            break;
        case 'x':
            uv = is_long ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned int);
            LogUL(uv, 16, width);
            break;
        case '%': LogRaw("%", 1); break;
        case 0: p--; break;
        default: LogRaw(p, 1); break;
        }
    }
    va_end(ap);
}

/* ---------------- files beside the application ---------------- */

static int GetAppSpec(FSSpec *app)
{
    ProcessSerialNumber psn;
    ProcessInfoRec      info;
    Str255              pname;

    if (GetCurrentProcess(&psn) != noErr) return -1;
    memset(&info, 0, sizeof(info));
    info.processInfoLength = sizeof(info);
    info.processName = pname;
    info.processAppSpec = app;
    return (GetProcessInformation(&psn, &info) == noErr) ? 0 : -1;
}

static void CToPStr(const char *c, Str255 p)
{
    long n;
    n = (long)strlen(c);
    if (n > 255) n = 255;
    p[0] = (unsigned char)n;
    memcpy(p + 1, c, (size_t)n);
}

/* Create (if needed), open for writing and truncate a file beside the
   app. Returns a refNum, or 0. */
static short OpenOutFile(const char *name, OSType creator, OSType type)
{
    FSSpec app, spec;
    Str255 pname;
    short  ref;
    OSErr  err;

    if (GetAppSpec(&app) != 0) return 0;
    CToPStr(name, pname);
    err = FSMakeFSSpec(app.vRefNum, app.parID, pname, &spec);
    if (err == fnfErr)
        err = FSpCreate(&spec, creator, type, smSystemScript);
    if (err != noErr) return 0;
    if (FSpOpenDF(&spec, fsRdWrPerm, &ref) != noErr) return 0;
    (void)SetEOF(ref, 0);
    return ref;
}

/* Read the URL file into a NewPtr'd NUL-terminated buffer, or NULL. */
static char *ReadURLFile(void)
{
    FSSpec app, spec;
    Str255 pname;
    short  ref;
    long   n;
    char  *buf;

    if (GetAppSpec(&app) != 0) return NULL;
    CToPStr(kURLFileName, pname);
    if (FSMakeFSSpec(app.vRefNum, app.parID, pname, &spec) != noErr) return NULL;
    if (FSpOpenDF(&spec, fsRdPerm, &ref) != noErr) return NULL;
    buf = NewPtrClear(kMaxURLFile + 1);
    if (buf == NULL) { FSClose(ref); return NULL; }
    n = kMaxURLFile;
    (void)FSRead(ref, &n, buf);
    FSClose(ref);
    if (n < 0) n = 0;
    buf[n] = 0;
    return buf;
}

/* ---------------- Apple events ---------------- */

static pascal OSErr AEQuit(const AppleEvent *e, AppleEvent *r, long refcon)
{
#pragma unused(e, r, refcon)
    gQuit = 1;
    return noErr;
}

static pascal OSErr AEIgnore(const AppleEvent *e, AppleEvent *r, long refcon)
{
#pragma unused(e, r, refcon)
    return noErr;
}

static void InstallAEHandlers(void)
{
    (void)AEInstallEventHandler(kCoreEventClass, kAEQuitApplication,
            NewAEEventHandlerUPP(AEQuit), 0, false);
    (void)AEInstallEventHandler(kCoreEventClass, kAEOpenApplication,
            NewAEEventHandlerUPP(AEIgnore), 0, false);
    (void)AEInstallEventHandler(kCoreEventClass, kAEOpenDocuments,
            NewAEEventHandlerUPP(AEIgnore), 0, false);
    (void)AEInstallEventHandler(kCoreEventClass, kAEPrintDocuments,
            NewAEEventHandlerUPP(AEIgnore), 0, false);
}

/* ---------------- callbacks ---------------- */

typedef struct Fetch {
    unsigned char preview[kPreviewBytes];
    unsigned long preview_len;
    short         save_ref;       /* 0 = not saving */
    OSErr         save_err;
    unsigned long idle_calls;
} Fetch;

static int BodyProc(void *ref, const unsigned char *data, unsigned long len)
{
    Fetch *f;
    unsigned long take;
    long count;

    f = (Fetch *)ref;
    if (f->preview_len < kPreviewBytes) {
        take = kPreviewBytes - f->preview_len;
        if (take > len) take = len;
        memcpy(f->preview + f->preview_len, data, (size_t)take);
        f->preview_len += take;
    }
    if (f->save_ref != 0) {
        count = (long)len;
        f->save_err = FSWrite(f->save_ref, &count, data);
        if (f->save_err != noErr || count != (long)len) return 1;
    }
    return 0;
}

static int IdleProc(void *ref, int busy)
{
    Fetch *f;
    EventRecord ev;

    f = (Fetch *)ref;
    f->idle_calls++;
    OSTLS_CollectEntropy();
    /* Give time to other processes (and OT's deferred tasks). Sleep 0
       while data flows, 1 tick when idle. */
    if (WaitNextEvent(everyEvent, &ev, busy ? 0 : 1, NULL)) {
        if (ev.what == kHighLevelEvent)
            (void)AEProcessAppleEvent(&ev);
    }
    return gQuit;
}

/* ---------------- reporting ---------------- */

static void LogClock(void)
{
    OSTLSTimeDetails d;
    unsigned long days, secs, z, era, doe, yoe, y, doy, mp, dd, mm;

    memset(&d, 0, sizeof d);
    (void)OSTLS_GetBearSSLTimeDetails(&d);
    Log("Mac clock: local secs since 1904 = %lu, gmtDelta = %ld s\n",
        (unsigned long)d.local_mac_seconds, (long)d.gmt_delta_seconds);
    /* civil date from unix days (Howard Hinnant's algorithm), UTC */
    days = (unsigned long)d.unix_seconds / 86400UL;
    secs = (unsigned long)d.unix_seconds % 86400UL;
    z = days + 719468UL;
    era = z / 146097UL;
    doe = z - era * 146097UL;
    yoe = (doe - doe / 1460UL + doe / 36524UL - doe / 146096UL) / 365UL;
    y = yoe + era * 400UL;
    doy = doe - (365UL * yoe + yoe / 4UL - yoe / 100UL);
    mp = (5UL * doy + 2UL) / 153UL;
    dd = doy - (153UL * mp + 2UL) / 5UL + 1UL;
    mm = mp < 10UL ? mp + 3UL : mp - 9UL;
    if (mm <= 2UL) y++;
    Log("Mac clock as UTC: %04lu-%02lu-%02lu %02lu:%02lu:%02lu  (BearSSL days=%lu) %s\n",
        y, mm, dd, secs / 3600UL, (secs / 60UL) % 60UL, secs % 60UL,
        (unsigned long)d.bearssl_days,
        d.result == 0 ? "OK" : "BEFORE 2000 -- TLS WILL REFUSE");
}

static void LogPreview(const Fetch *f)
{
    unsigned long i;
    unsigned char c;
    char ch;

    Log("  first %lu bytes: ", f->preview_len);
    for (i = 0; i < f->preview_len; i++) {
        c = f->preview[i];
        if (c == 0x0D) LogS("\\r");
        else if (c == 0x0A) LogS("\\n");
        else if (c < 0x20 || c > 0x7E) LogS(".");
        else { ch = (char)c; LogRaw(&ch, 1); }
    }
    Log("\n");
}

static void LogResult(const HC_Result *r, const Fetch *f)
{
    Log("  result: %d (%s)\n", r->err, HC_ErrName(r->err));
    Log("  HTTP status %d, redirects %d, final URL %s\n",
        r->http_status, r->redirects, r->final_url);
    if (r->tls_version != 0)
        Log("  TLS 1.%d, cipher suite 0x%04x %s\n",
            r->tls_version == 0x0304 ? 3 : 2, r->cipher_suite,
            HC_CipherName(r->cipher_suite));
    else if (r->final_url[4] == 's' || r->final_url[4] == 'S')
        Log("  TLS not established\n");
    else
        Log("  plain http (no TLS)\n");
    if (r->cert_err_ignored >= 0)
        Log("  INSECURE: certificate %s (BearSSL would have said %d)\n",
            r->cert_err_ignored == 0 ? "would have passed" : "NOT VALID",
            r->cert_err_ignored);
    if (r->err != 0)
        Log("  errors: macTLS/OT %ld, OT %ld, BearSSL %d\n",
            r->os_err, r->ot_err, r->br_err);
    Log("  ticks: connect %lu, handshake %lu, first byte %lu, hop total %lu, all %lu\n",
        r->t_connect, r->t_handshake, r->t_first_byte, r->t_total, r->t_all);
    Log("  body: %lu bytes%s%s, Content-Length %s%lu, type \"%s\"",
        r->body_bytes, r->chunked ? " (chunked)" : "", "",
        r->has_length ? "" : "none/", r->content_length, r->content_type);
    if (r->content_range[0]) Log(", range \"%s\"", r->content_range);
    Log("\n  idle callbacks: %lu\n", f->idle_calls);
    LogPreview(f);
}

/* ---------------- one line of the URL file ---------------- */

static char *SkipWord(char *s, const char *w)
{
    unsigned long n;
    n = (unsigned long)strlen(w);
    if (strncmp(s, w, (size_t)n) == 0 && (s[n] == ' ' || s[n] == '\t')) {
        s += n;
        while (*s == ' ' || *s == '\t') s++;
        return s;
    }
    return NULL;
}

static void DoLine(char *line, int index)
{
    HC_Request rq;
    HC_Result  rs;
    Fetch     *f;
    char      *p, *q, *save_name;
    unsigned long sv;

    memset(&rq, 0, sizeof rq);
    save_name = NULL;
    p = line;
    for (;;) {
        if ((q = SkipWord(p, "insecure")) != NULL) { rq.insecure = 1; p = q; continue; }
        if ((q = SkipWord(p, "save")) != NULL) {
            save_name = q;
            while (*q && *q != ' ' && *q != '\t') q++;
            if (*q) *q++ = 0;
            while (*q == ' ' || *q == '\t') q++;
            p = q;
            continue;
        }
        if ((q = SkipWord(p, "range")) != NULL) {
            rq.use_range = 1;
            sv = 0;
            while (*q >= '0' && *q <= '9') sv = sv * 10UL + (unsigned long)(*q++ - '0');
            rq.range_start = sv;
            rq.range_end = 0xFFFFFFFFUL;
            if (*q == '-') {
                q++;
                if (*q >= '0' && *q <= '9') {
                    sv = 0;
                    while (*q >= '0' && *q <= '9') sv = sv * 10UL + (unsigned long)(*q++ - '0');
                    rq.range_end = sv;
                }
            }
            while (*q == ' ' || *q == '\t') q++;
            p = q;
            continue;
        }
        break;
    }
    f = (Fetch *)NewPtrClear((Size)sizeof(Fetch));
    if (f == NULL) { Log("\n[%d] out of memory\n", index); return; }
    Log("\n[%d] GET %s%s\n", index, p, rq.insecure ? "   (INSECURE: no certificate check)" : "");
    if (save_name != NULL) {
        f->save_ref = OpenOutFile(save_name, 'TVOD', 'MooV');
        Log("  saving body to \"%s\" %s\n", save_name, f->save_ref ? "" : "-- COULD NOT CREATE FILE");
        if (f->save_ref == 0) { DisposePtr((Ptr)f); return; }
    }
    rq.url = p;
    rq.body = BodyProc;
    rq.idle = IdleProc;
    rq.ref = f;
    (void)HC_Get(&rq, &rs);
    if (f->save_ref != 0) {
        (void)FSClose(f->save_ref);
        if (f->save_err != noErr) Log("  file write error %d\n", (int)f->save_err);
    }
    LogResult(&rs, f);
    DisposePtr((Ptr)f);
}

/* ---------------- main ---------------- */

int main(void)
{
    char *urls, *line, *e;
    int index, kat, t13, ent;
    unsigned long fp;
    OSStatus oterr;

    MaxApplZone();
    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();
    FlushEvents(everyEvent, 0);
    InstallAEHandlers();

    gLogRef = OpenOutFile(kLogFileName, 'ttxt', 'TEXT');
    Log("HTTPS Get %s -- macTLS (BearSSL) over classic Open Transport\n", kAppVersion);
    Log("TickCount at start: %lu\n", (unsigned long)TickCount());
    LogClock();

    Log("\nSelf-tests:\n");
    kat = OSTLS_SHA384_KAT();
    Log("  SHA-384 KAT: %s (%d)\n", kat == 0 ? "PASS" : "FAIL", kat);
    t13 = OSTLS_TLS13_SelfTest();
    Log("  TLS 1.3 key schedule/record self-test: %s (%d failures)\n", t13 == 0 ? "PASS" : "FAIL", t13);
    fp = 0;
    ent = OSTLS_EntropySelfTest(&fp);
    Log("  entropy self-test: %s (%d), fingerprint %08lx (must differ every launch)\n",
        ent == 0 ? "PASS" : "FAIL", ent, fp);

    oterr = InitOpenTransport();
    if (oterr != noErr) {
        Log("\nInitOpenTransport failed (%ld). Is TCP/IP set up? Quitting.\n", (long)oterr);
        if (gLogRef) FSClose(gLogRef);
        return 1;
    }

    index = 0;
    urls = ReadURLFile();
    if (urls == NULL) {
        Log("\n(no \"%s\" file: using the two default URLs)\n", kURLFileName);
        for (index = 0; index < 2 && !gQuit; index++) {
            char tmp[256];
            strcpy(tmp, kDefaultURLs[index]);
            DoLine(tmp, index + 1);
        }
    } else {
        Log("\n(URLs from \"%s\")\n", kURLFileName);
        line = urls;
        while (*line && !gQuit) {
            e = line;
            while (*e && *e != '\015' && *e != '\012') e++;
            if (*e) *e++ = 0;
            while (*line == ' ' || *line == '\t') line++;
            {
                char *t;
                t = line + strlen(line);
                while (t > line && (t[-1] == ' ' || t[-1] == '\t')) *--t = 0;
            }
            if (line[0] == 0 || line[0] == '#') { line = e; continue; }
            if (strcmp(line, "tls12") == 0) {
                OSTLS_SetTryTLS13(0);
                Log("\n(TLS 1.3 off from here on)\n");
            } else if (strcmp(line, "debuglog") == 0) {
                Log("\n(macTLS trace -> MacTLSTest.log on the Desktop: %d)\n", (int)OSTLS_LogInit());
            } else {
                index++;
                DoLine(line, index);
            }
            line = e;
        }
        DisposePtr(urls);
    }
    if (gQuit) Log("\nQuit Apple event received; stopping.\n");
    Log("\nDone. %d URL(s).\n", index);

    OSTLS_SaveSeed();
    OSTLS_LogClose();
    CloseOpenTransport();
    if (gLogRef) { FSClose(gLogRef); gLogRef = 0; }
    return 0;
}
