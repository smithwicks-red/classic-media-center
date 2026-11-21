/*
    MediaCenter.c -- "Classic Media Center" 0.5u
    0.5u (J5c): the RAM pre-buffer covers [0, end of moov + 512 KB) (cap
    4 MB, else "index too large to stream" -> download): the first media
    bytes QuickTime waits for lie BEHIND the moov.
    0.5t (J5b): the proxy answers QuickTime at Open Transport NOTIFIER
    time from a 512 KB RAM pre-buffer (mc_notify.c, mc_proxy_ot.c), so
    QuickTime Player's synchronous open of the reference movie no longer
    starves it; "Buffering N%" before the movie opens; FlushVol per log line.

    0.5s (J5): https films STREAM. When the plain-http chain of a film
    insists on https (archive.org's *.ca.archive.org storage nodes),
    Media Center no longer downloads it first: it opens a loopback HTTP
    server (mc_proxy_ot.c: Open Transport listener on 127.0.0.1:8080,
    else 8081..8089, else an OT-assigned port, opened at the first such
    Play and kept until Quit) and writes the reference movie with dref
    http://127.0.0.1:PORT/a/archive.org/download/ID/FILE, saved on the
    Desktop as "<name> (MC).mov". QuickTime Player fetches plain http from
    Media Center; mc_proxy.c fetches the same bytes over https
    (https_client's HC_Stream, macTLS/BearSSL) and passes them on.
    mc_stream.c holds the policy (host-tested): mcs_poll() runs from the
    event loop AND from CbIdle; the loop sleeps 0 after bytes moved, 1
    tick while connections exist, else 15.
      * "Download first" ticked, or no listener -> the 0.4 download path.
      * Stop (enabled while a film streams) asks QuickTime Player, by an
        Apple event, to close the streaming movie (saving no), waits up
        to 2 s, then cuts whatever is left (QTCloseMovie below).
      * Quit while QuickTime is reading: status-line warning; a second
        Quit within 10 s closes the movie as for Stop, then quits.
      * Esc / Cmd-period never quit: they cancel a transfer, or go back.
      * a G3 (Gestalt) and a film above 64 KB/s average: log + status
        hint to use "Download first" (no dialog).
      * hook runs: "serve N" / "stop N" lines (mc_hook_parse); after the
        hand-off the app keeps serving (event loop) until Quit, a Stop
        (then it quits) or N seconds.
    Partition: 4096 K minimum, 5120 K preferred (BUILD_NOTES_05S.md).

    ---- 0.4 notes follow ----

    0.4 (b21e): Main and High profile H.264 (8-bit 4:2:0, B-frames
    included) when "H264 Decoder 17" (component version >= 0x00010011)
    AND its shared library "AVCDecLib" are installed: DecoderLevel()
    below checks both once at start-up and tells mc_core
    (mc_set_decoder_level). Then the profile gate (mc_avc_verdict)
    accepts profile 77/100 when the SPS says 8-bit 4:2:0 (interlaced:
    handed over, flagged "untested"), the list marks change, and every
    moov handed to QuickTime -- the streaming reference movie AND the
    moov appended to a download -- goes through AVCReorder.c inside
    BuildReferenceMovie (H264Stream.c). Without that decoder: exactly 0.3.

    0.3 (b20): search covers every movie item on archive.org (no
    "512Kb MPEG4" filter), every word required, "quoted phrases" kept,
    titles first then every field (mc_search_url2). Play on a search
    result now OPENS the item: the same list shows "files of <item>"
    (every .mp4/.m4v/.mov, natural order, size, format, and a hint
    "plays now?" / "needs newer decoder?"); Play on a file runs the
    moov fetch + Baseline check (the row becomes "PLAYS NOW" or
    "needs newer decoder") and then streams or downloads as in 0.2.
    "Back" returns to the results. A download bigger than 32 MB asks
    first: the status line gives size and time, Play again = yes.
    Hook file keywords: list, file N, confirm (besides download,
    insecure); the logic is mcf_hook_run in mc_fetch.c (host-tested).

    0.2 (b19): everything the app fetches goes through ONE transport,
    https_client (macTLS/BearSSL for https, plain Open Transport for
    http): search and metadata over https, certificates checked (unless
    "Skip certificate checks (unsafe)" is ticked). Before playing, the
    moov is fetched through the same http redirect chain QuickTime would
    follow: if every hop is plain http, QuickTime streams it as in 0.1;
    if any hop insists on https (or "Download first" is ticked), the
    whole film is downloaded over TLS into "Media Center Downloads"
    beside the app and played from the disk (see mc_fetch.c for the
    local movie layout). Cmd-period, Esc or Stop cancels a download.
    Project prefix: https_prefix.h (from the tls18 tree).

    ---- 0.1 notes follow ----

    Search the Internet Archive from Mac OS 9, pick a film, and watch
    it in QuickTime Player 6 through our H.264 decoder -- over plain
    http, exactly the way H264 Stream plays a typed URL.

    HOW IT IS PUT TOGETHER
    ----------------------
    All of H264 Stream's network and movie code is reused verbatim:
    this file #includes H264Stream.c (patched with
    H264Stream_profile_check.diff) with H264STREAM_LIBRARY defined, which
    leaves out H264 Stream's own dialog and main(). What we get from it:
      HttpGet / RxFill / gRx..  -- the Open Transport HTTP/1.0 client
      FetchMoov                 -- fetch only the 'moov' of a remote MP4
      BuildReferenceMovie       -- the tiny QuickTime reference movie
      SaveAndOpenMovie          -- save to the Desktop, open in QT Player
      Say / OpenLog / NetClose / MovieNameFromURL / CToPStr / Trim
    mc_core.c (portable, host-tested) does the archive.org parts: the
    search URL, a streaming JSON scanner, ranking an item's files, the
    list of mirror URLs, and the H.264 profile check.

    Do NOT add H264Stream.c to the CodeWarrior project as its own file:
    it is compiled as part of this one. Add mc_core.c.

    WHAT "PLAY" DOES
    ----------------
      1. GET http://archive.org/metadata/<identifier>   (streamed into
         McMeta in 16 KB pieces -- no big buffer, however many files)
      2. candidates: "512Kb MPEG4" first, then "MPEG4", then "h.264",
         "h.264 IA" last
      3. for each candidate, for each URL: archive.org/download/... and
         then each storage server, *.us.archive.org first. A redirect to
         https ends that URL ("needs HTTPS") and we try the next one.
      4. FetchMoov, then mc_avc_profile: anything but Baseline H.264
         (or MPEG-4 Part 2, which QuickTime plays itself) is refused
         BEFORE QuickTime sees it.
      5. reference movie pointing at the URL that answered -> Desktop ->
         QuickTime Player.

    CodeWarrior Pro 8.3, Universal Interfaces 3.4.1, C89, no prefix file.
*/

#define H264STREAM_LIBRARY 1
#define kUserAgent   "ClassicMediaCenter/0.5u (Macintosh; PPC Mac OS 9; QuickTime 6)"
#define kLogFileName "Media Center Log"
#include "H264Stream.c"
#include <Lists.h>
#include <Components.h>   /* (0.4) FindNextComponent, GetComponentVersion */
#include "mc_fetch.h"
#include "ostls_entropy.h"
#include <Gestalt.h>          /* (0.5s) G3 or not                       */
#include <AppleEvents.h>      /* (0.5s) close the movie in QuickTime    */
#include "mc_proxy_ot.h"      /* (0.5s) https streaming: OT listener    */
#include "mc_stream.h"        /* (0.5s) streaming policy (host-tested)  */

/* ---------------- dialog items ---------------- */
#define kItemSearch   1      /* default button */
#define kItemQuit     2
#define kItemLabel    3
#define kItemTerms    4
#define kItemStatus   5
#define kItemList     6      /* userItem, disabled: we handle clicks */
#define kItemPlay     7
#define kItemMore     8
#define kItemDlFirst  9      /* checkbox "Download first"            */
#define kItemInsecure 10     /* checkbox "Skip certificate checks"   */
#define kItemStop     11     /* enabled only while busy              */
#define kItemBack     12     /* (0.3) files -> back to the results   */

#define kDlgW   520
#define kDlgH   400

static DialogPtr  gDlg = NULL;
static ListHandle gList = NULL;
static Rect       gListRect;          /* the userItem, scroll bar inside */
static int        gQuit = 0;
static int        gBusy = 0;

static McSearch   gSearch;            /* ~6 KB  */
static McMeta     gMeta;              /* ~26 KB (0.3: 64-file list) */
static char       gUrls[MC_MAXATTEMPT][MC_URLMAX];   /* 7 KB */
static char       gTerms[256];
static int        gPage = 1;
static int        gHook = 0;          /* 1 = unattended test run */
static McHook     gHookSpec;          /* (0.3) parsed "Media Center Query" */
static int        gListMode = 0;      /* (0.3) 0 = results, 1 = files */
static int        gItem = -1;         /* (0.3) result whose files show */
static int        gConfirmRow = -1;   /* (0.3) Play again here = yes   */
static int        gCancel = 0;
static int        gStopStream = 0;    /* (0.5s) Stop clicked during a transfer */
static int        gQuitAsk = 0;       /* (0.5s) Quit asked (menu/key/button/AE) */
static int        gStopOn = -1;       /* (0.5s) Stop button state shown */
static McfEnv     gEnv;

#define kQueryFileName "Media Center Query"

/* ==================================================================
   the status line (H264 Stream's Say() calls this for every message)
   ================================================================== */

static void UIStatus(const char *line)
{
    short   type;
    Handle  h;
    Rect    r;
    Str255  p;
    GrafPtr save;

    if (gDlg == NULL) return;
    GetDialogItem(gDlg, kItemStatus, &type, &h, &r);
    CToPStr(line, p);
    GetPort(&save);
    SetPort(gDlg);
    SetDialogItemText(h, p);
    EraseRect(&r);
    TETextBox(p + 1, p[0], &r, teFlushDefault);
    SetPort(save);
}

/* plain message: to the log and the status line */
static void Tell(const char *msg)
{
    Say("%s\n", msg);
}

/* ==================================================================
   drawing
   ================================================================== */

static void DrawDefaultRing(void)
{
    short   type;
    Handle  h;
    Rect    r;

    GetDialogItem(gDlg, kItemSearch, &type, &h, &r);
    PenSize(3, 3);
    InsetRect(&r, -4, -4);
    FrameRoundRect(&r, 16, 16);
    PenNormal();
}

static void DoUpdate(WindowPtr w)
{
    GrafPtr save;
    Rect    fr;

    GetPort(&save);
    SetPort(w);
    BeginUpdate(w);
    if (w == gDlg)
    {
        UpdateDialog(gDlg, gDlg->visRgn);
        DrawDefaultRing();
        fr = gListRect;
        InsetRect(&fr, -1, -1);
        FrameRect(&fr);
        if (gList != NULL) LUpdate(gDlg->visRgn, gList);
    }
    EndUpdate(w);
    SetPort(save);
}

/* ==================================================================
   building the window (items in code, like H264 Stream)
   ================================================================== */

static void AddItem(Handle ditl, short top, short left, short bottom,
                    short right, unsigned char kind, const char *text)
{
    long           at = GetHandleSize(ditl);
    long           len = (long)strlen(text);
    long           size = 4 + 8 + 2 + len + (len & 1);
    unsigned char *p;
    short         *count;

    SetHandleSize(ditl, at + size);
    if (MemError() != noErr) return;
    p = (unsigned char *)*ditl + at;
    memset(p, 0, (size_t)size);
    p[4]  = (unsigned char)(top >> 8);    p[5]  = (unsigned char)top;
    p[6]  = (unsigned char)(left >> 8);   p[7]  = (unsigned char)left;
    p[8]  = (unsigned char)(bottom >> 8); p[9]  = (unsigned char)bottom;
    p[10] = (unsigned char)(right >> 8);  p[11] = (unsigned char)right;
    p[12] = kind;
    p[13] = (unsigned char)len;
    memcpy(p + 14, text, (size_t)len);
    count = (short *)*ditl;
    if (at > 2) (*count)++;
}

static DialogPtr MakeDialog(void)
{
    Handle ditl;
    Rect   bounds, scr;
    Str255 title;
    short  left, top;

    ditl = NewHandleClear(2);
    if (ditl == NULL) return NULL;
    AddItem(ditl,  33, 420,  53, 500, ctrlItem + btnCtrl, "Search");
    AddItem(ditl, 368, 420, 388, 500, ctrlItem + btnCtrl, "Quit");
    AddItem(ditl,  10,  20,  26, 400, statText, "Search the Internet Archive for films:");
    AddItem(ditl,  35,  23,  51, 400, editText, "");
    AddItem(ditl, 292,  20, 330, 500, statText, "");
    AddItem(ditl,  68,  21, 284, 499, userItem + itemDisable, "");
    AddItem(ditl, 368, 320, 388, 400, ctrlItem + btnCtrl, "Play");
    AddItem(ditl, 368,  20, 388, 130, ctrlItem + btnCtrl, "More results");
    AddItem(ditl, 338,  20, 356, 150, ctrlItem + chkCtrl, "Download first");
    AddItem(ditl, 338, 170, 356, 420, ctrlItem + chkCtrl,
            "Skip certificate checks (unsafe)");
    AddItem(ditl, 368, 230, 388, 300, ctrlItem + btnCtrl, "Stop");
    AddItem(ditl, 368, 140, 388, 220, ctrlItem + btnCtrl, "Back");

    scr = qd.screenBits.bounds;
    left = (short)((scr.right - scr.left - kDlgW) / 2);
    top  = (short)((scr.bottom - scr.top - kDlgH) / 3 + 20);
    if (top < 44) top = 44;
    SetRect(&bounds, left, top, left + kDlgW, top + kDlgH);
    CToPStr("Classic Media Center", title);
    return NewDialog(NULL, &bounds, title, true, movableDBoxProc,
                     (WindowPtr)-1L, false, 0, ditl);
}

static void MakeList(void)
{
    short     type;
    Handle    h;
    Rect      rView;
    ListBounds data;
    Point     cell;

    GetDialogItem(gDlg, kItemList, &type, &h, &gListRect);
    rView = gListRect;
    rView.right -= 15;                     /* room for the scroll bar */
    SetRect(&data, 0, 0, 1, 0);            /* 1 column, 0 rows */
    cell.h = 0;
    cell.v = 0;                            /* 0,0 = default cell size */
    gList = LNew(&rView, &data, cell, 0, (WindowPtr)gDlg,
                 true, false, false, true);
    if (gList != NULL) (**gList).selFlags = lOnlyOne;
}

static void EnableDlgItem(short item, int on)
{
    short  type;
    Handle h;
    Rect   r;
    GetDialogItem(gDlg, item, &type, &h, &r);
    HiliteControl((ControlHandle)h, (short)(on ? 0 : 255));
}

static int GetCheck(short item)
{
    short  type;
    Handle h;
    Rect   r;
    GetDialogItem(gDlg, item, &type, &h, &r);
    return GetControlValue((ControlHandle)h) != 0;
}

static void SetCheck(short item, int on)
{
    short  type;
    Handle h;
    Rect   r;
    GetDialogItem(gDlg, item, &type, &h, &r);
    SetControlValue((ControlHandle)h, (short)(on ? 1 : 0));
}

/* the two checkboxes -> the fetch options */
static void ReadOptions(void)
{
    gEnv.force_download = GetCheck(kItemDlFirst);
    gEnv.insecure = GetCheck(kItemInsecure);
    mcs_set_insecure(gEnv.insecure);     /* (0.5s) proxy upstreams too */
}

static void GetTerms(char *s, long max)
{
    short  type;
    Handle h;
    Rect   r;
    Str255 p;
    GetDialogItem(gDlg, kItemTerms, &type, &h, &r);
    GetDialogItemText(h, p);
    if ((long)p[0] > max - 1) p[0] = (unsigned char)(max - 1);
    memcpy(s, p + 1, p[0]);
    s[p[0]] = 0;
}

static void FlashButton(short item)
{
    short         type;
    Handle        h;
    Rect          r;
    unsigned long t;
    GetDialogItem(gDlg, item, &type, &h, &r);
    HiliteControl((ControlHandle)h, 10);
    Delay(8, &t);
    HiliteControl((ControlHandle)h, 0);
}

/* ==================================================================
   McfEnv callbacks: mc_fetch.c does the network work (0.2)
   ================================================================== */

static void Busy(int on);
static void DoUpdate(WindowPtr w);

static void CbLog(void *r, const char *t)
{
#pragma unused(r)
    Say("%s", t);
}

static void CbStatus(void *r, const char *line)
{
#pragma unused(r)
    UIStatus(line);
}

/* Between pump steps of every transfer: keep the Mac alive, redraw,
   answer Apple events, and watch for Cmd-period / Esc / Stop. */
static int CbIdle(void *r, int busy)
{
#pragma unused(r)
    EventRecord ev;
    OSTLS_CollectEntropy();
    (void)mcs_poll();          /* (0.5s) QuickTime is never starved */
    if (WaitNextEvent(everyEvent, &ev, busy ? 0 : 1, NULL))
    {
        switch (ev.what)
        {
        case kHighLevelEvent:
            (void)AEProcessAppleEvent(&ev);
            break;
        case updateEvt:
            DoUpdate((WindowPtr)ev.message);
            break;
        case keyDown:
        case autoKey:
            {
                char c = (char)(ev.message & charCodeMask);
                if (((ev.modifiers & cmdKey) && c == '.') || c == 0x1B)
                    gCancel = 1;
            }
            break;
        case mouseDown:
            {
                WindowPtr w;
                short     part = FindWindow(ev.where, &w);
                if (part == inDrag && w == gDlg)
                    DragWindow(w, ev.where, &qd.screenBits.bounds);
                else if (part == inContent && w == gDlg)
                {
                    short  type;
                    Handle h;
                    Rect   rr;
                    Point  pt = ev.where;
                    GrafPtr save;
                    GetPort(&save);
                    SetPort(gDlg);
                    GlobalToLocal(&pt);
                    SetPort(save);
                    GetDialogItem(gDlg, kItemStop, &type, &h, &rr);
                    if (PtInRect(pt, &rr))
                    {   /* (0.5s) Stop also ends streaming, after this transfer */
                        gCancel = 1;
                        if (mcs_streaming()) gStopStream = 1;
                    }
                }
            }
            break;
        }
    }
    if (gQuit || gQuitAsk) gCancel = 1;
    return gCancel;
}

static void *CbAlloc(long n) { return MemAlloc(n); }
static void  CbRelease(void *p) { MemFree(p); }

static unsigned char *CbRebuild(const unsigned char *moov, long len,
                                const char *url, long *outLen)
{
    return BuildReferenceMovie(moov, len, url, outLen);
}

static int CbOpenRef(void *r, const char *name, const unsigned char *d,
                     long n)
{
#pragma unused(r)
    return SaveAndOpenMovie(name, d, n);
}

static int CbOpenLocal(void *r, const FSSpec *spec)
{
#pragma unused(r)
    OSErr err = OpenWithFinder(spec);
    if (err != noErr)
    {
        Say("  Could not ask the Finder to open it (err %d). Double-click\n"
            "  it in the \"%s\" folder instead.\n", (int)err, MCF_DL_FOLDER);
        return 0;
    }
    Say("Asked the Finder to open the downloaded film in QuickTime "
        "Player.\n");
    return 0;
}

/* ==================================================================
   (0.5s) streaming glue
   ================================================================== */

/* four-char codes spelled out: no AERegistry.h / AEObjects.h needed */
#define kMC_QTSig      'TVOD'     /* QuickTime Player                  */
#define kMC_CoreSuite  'core'     /* kAECoreSuite                      */
#define kMC_Close      'clos'     /* kAEClose                          */
#define kMC_Window     'cwin'     /* cWindow                           */
#define kMC_FormName   'name'     /* formName                          */
#define kMC_Want       'want'     /* keyAEDesiredClass                 */
#define kMC_From       'from'     /* keyAEContainer                    */
#define kMC_Form       'form'     /* keyAEKeyForm                      */
#define kMC_Seld       'seld'     /* keyAEKeyData                      */
#define kMC_Savo       'savo'     /* keyAESaveOptions                  */
#define kMC_No         'no  '     /* kAENo                             */
#define kMC_TypeObj    'obj '     /* typeObjectSpecifier               */

/* "close window <name> saving no" to QuickTime Player. The object
   specifier is built by hand (an AERecord coerced to 'obj '), so no
   ObjectSupportLib is needed. kAENoReply + kAENeverInteract: this never
   waits and never lets QuickTime show a dialog for us. A window that is
   not there is QuickTime's error, not ours: we only learn whether the
   event was delivered (procNotFound = QuickTime Player not running). */
static OSErr QTCloseWindowNamed(const char *name)
{
    AEAddressDesc target;
    AppleEvent    ae, reply;
    AEDesc        rec, spec, nul;
    OSType        sig = kMC_QTSig, savo = kMC_No;
    DescType      cls = kMC_Window, form = kMC_FormName;
    OSErr         err;

    target.descriptorType = typeNull; target.dataHandle = NULL;
    ae = target; reply = target; rec = target; spec = target; nul = target;
    err = AECreateDesc(typeApplSignature, &sig, sizeof sig, &target);
    if (err == noErr)
        err = AECreateAppleEvent(kMC_CoreSuite, kMC_Close, &target,
                                 kAutoGenerateReturnID, kAnyTransactionID,
                                 &ae);
    if (err == noErr) err = AECreateList(NULL, 0, true, &rec);
    if (err == noErr) err = AEPutKeyPtr(&rec, kMC_Want, typeType, &cls, sizeof cls);
    if (err == noErr) err = AEPutKeyDesc(&rec, kMC_From, &nul);
    if (err == noErr) err = AEPutKeyPtr(&rec, kMC_Form, typeEnumerated, &form, sizeof form);
    if (err == noErr) err = AEPutKeyPtr(&rec, kMC_Seld, typeChar, name, (Size)strlen(name));
    if (err == noErr) err = AECoerceDesc(&rec, kMC_TypeObj, &spec);
    if (err == noErr) err = AEPutParamDesc(&ae, keyDirectObject, &spec);
    if (err == noErr) err = AEPutParamPtr(&ae, kMC_Savo, typeEnumerated, &savo, sizeof savo);
    if (err == noErr)
        err = AESend(&ae, &reply, kAENoReply | kAENeverInteract,
                     kAENormalPriority, kAEDefaultTimeout, NULL, NULL);
    (void)AEDisposeDesc(&spec);
    (void)AEDisposeDesc(&rec);
    (void)AEDisposeDesc(&ae);
    (void)AEDisposeDesc(&reply);
    (void)AEDisposeDesc(&target);
    return err;
}

/* McsEnv.qt_close: the window title may or may not keep ".mov" (to be
   seen on the guest, J6), so ask for both; only our " (MC)" movies match */
static int CbQtClose(void *r, const char *name)
{
    char  base[64];
    OSErr e1, e2 = noErr;
    long  n = (long)strlen(name);
#pragma unused(r)
    e1 = QTCloseWindowNamed(name);
    if (n > 4 && n < 64 && strcmp(name + n - 4, ".mov") == 0)
    {
        memcpy(base, name, (size_t)(n - 4));
        base[n - 4] = 0;
        e2 = QTCloseWindowNamed(base);
    }
    Say("  Apple event to QuickTime Player: close window \"%s\" saving no "
        "-> %d / %d (without .mov)\n", name, (int)e1, (int)e2);
    return e1 != noErr ? e1 : e2;
}

static unsigned long CbTicks(void *r)
{
#pragma unused(r)
    return (unsigned long)TickCount();
}

/* gestaltNativeCPUtype ('cput'): 0x0108 = 750 (G3), 0x0120 = 750FX */
static int IsG3(long *cpu)
{
    *cpu = 0;
    if (Gestalt('cput', cpu) != noErr) return 0;
    return *cpu == 0x0108L || *cpu == 0x0120L;
}

static void SetupStreaming(void)
{
    McsEnv m;
    long   cpu;
    memset(&m, 0, sizeof m);
    m.now = CbTicks;
    m.log = CbLog;
    m.status = CbStatus;
    m.qt_close = CbQtClose;
    m.idle = CbIdle;                 /* (0.5t) pre-buffer steps */
    m.cpu_g3 = IsG3(&cpu);
    mcs_init(&m);
    Say("CPU type 0x%04lX (%s).\n", (unsigned long)cpu,
        m.cpu_g3 ? "G3: films above 64 KB/s get a \"Download first\" hint"
                 : "not a G3");
}

/* the Stop button: enabled while Media Center works OR a film streams */
static void UpdateStop(void)
{
    int on = gBusy || mcs_streaming();
    if (on != gStopOn) { EnableDlgItem(kItemStop, on); gStopOn = on; }
}

static void DoStopStreaming(void)
{
    gStopStream = 0;
    if (mcs_streaming()) mcs_stop();
}

static void DoQuit(void)
{
    gQuitAsk = 0;
    if (mcs_quit() == MCS_QUIT_NOW) gQuit = 1;
}

static void SetupEnv(void)
{
    FSSpec app;
    memset(&gEnv, 0, sizeof gEnv);
    gEnv.log = CbLog;
    gEnv.status = CbStatus;
    gEnv.idle = CbIdle;
    gEnv.alloc = CbAlloc;
    gEnv.release = CbRelease;
    gEnv.rebuild = CbRebuild;
    gEnv.open_ref = CbOpenRef;
    gEnv.open_local = CbOpenLocal;
    gEnv.movie_name = MovieNameFromURL;
    gEnv.proxy_url = mcs_proxy_url;          /* (0.5s) */
    gEnv.proxy_opened = mcs_proxy_opened;
    gEnv.user_agent = kUserAgent;
    gEnv.confirm_how = "Click Play again to download it.";
    if (GetAppSpec(&app) == 0)
    {
        gEnv.app_vref = app.vRefNum;
        gEnv.app_dir = app.parID;
    }
}

/* ==================================================================
   (0.4) Which H.264 decoder is installed?
   ==================================================================
   1 (Main/High) only if BOTH:
     a. an image decompressor component 'imdc'/'avc1' answers the
        Version selector with >= 0x00010011 ("H264 Decoder 17"). The
        decoder is registered by its startup extension with
        RegisterComponent and has NO 'thng' resource, so the version is
        only available from an open instance: OpenComponent +
        GetComponentVersion + CloseComponent, for every match (the
        highest wins). Opening it allocates its globals only; AVCDecLib
        is not loaded until the first frame.
     b. a file named "AVCDecLib" is in the Extensions folder or the
        System Folder: exactly the two places the decoder's own fallback
        (H264LoadAvc: GetDiskFragment) looks, and where the installer
        puts it. A file check (FindFolder + FSMakeFSSpec) loads no code
        and runs no CFM init routine. Not checked: the library's own
        version (r2 = 0x00080F02), a damaged file, or a copy that CFM
        would find by name somewhere else (e.g. beside QuickTime
        Player): such a Mac is reported as Baseline only, i.e. 0.3.
   Anything else -> 0: 0.3 behaviour. */
#define kAvcDecoderMainHigh 0x00010011UL

static int DecoderLevel(long *version, int *libFound)
{
    ComponentDescription cd;
    Component            c = NULL;
    ComponentInstance    ci;
    long                 v, best = 0;
    OSType               where[2];
    short                vRef;
    long                 dirID;
    FSSpec               spec;
    Str255               name;
    int                  w;

    cd.componentType         = 'imdc';  /* decompressorComponentType */
    cd.componentSubType      = 'avc1';
    cd.componentManufacturer = 0;       /* any */
    cd.componentFlags        = 0;
    cd.componentFlagsMask    = 0;
    while ((c = FindNextComponent(c, &cd)) != NULL)
    {
        ci = OpenComponent(c);
        if (ci == NULL) continue;
        v = GetComponentVersion(ci);
        (void)CloseComponent(ci);
        if (v > best) best = v;
    }
    *version = best;

    *libFound = 0;
    CToPStr("AVCDecLib", name);
    where[0] = kExtensionFolderType;
    where[1] = kSystemFolderType;
    for (w = 0; w < 2 && !*libFound; w++)
        if (FindFolder(kOnSystemDisk, where[w], kDontCreateFolder,
                       &vRef, &dirID) == noErr
            && FSMakeFSSpec(vRef, dirID, name, &spec) == noErr)
            *libFound = 1;

    if (best >= (long)kAvcDecoderMainHigh && *libFound)
        return MC_DECODER_MAINHIGH;
    return MC_DECODER_BASELINE;
}

/* ==================================================================
   Search
   ================================================================== */

static void ClearList(void)
{
    if (gList == NULL) return;
    LSetDrawingMode(false, gList);
    LDelRow(0, 0, gList);                 /* 0 rows = delete them all */
    LSetDrawingMode(true, gList);
}

/* (0.3) the one list shows either the results or the item's files */
static void ListInval(void)
{
    GrafPtr save;
    GetPort(&save);
    SetPort(gDlg);
    InvalRect(&gListRect);
    SetPort(save);
}

static void SelectRow(int row)
{
    Cell c;
    if (row < 0) return;
    c.h = 0;
    c.v = (short)row;
    LSetSelect(true, c, gList);
    LAutoScroll(gList);
}

static void FillResults(int select)
{
    int  i;
    Cell c;
    ClearList();
    LSetDrawingMode(false, gList);
    (void)LAddRow((short)gSearch.count, 0, gList);
    for (i = 0; i < gSearch.count; i++)
    {
        c.h = 0;
        c.v = (short)i;
        LSetCell(gSearch.hits[i].title,
                 (short)strlen(gSearch.hits[i].title), c, gList);
    }
    SelectRow(select);
    LSetDrawingMode(true, gList);
    ListInval();
}

static void SetFileRow(int i)
{
    char row[200];
    Cell c;
    mc_file_row(&gMeta.files[i], row, (long)sizeof row);
    c.h = 0;
    c.v = (short)i;
    LSetCell(row, (short)strlen(row), c, gList);
}

static void FillFiles(void)
{
    int i;
    ClearList();
    LSetDrawingMode(false, gList);
    (void)LAddRow((short)gMeta.nfiles, 0, gList);
    for (i = 0; i < gMeta.nfiles; i++) SetFileRow(i);
    SelectRow(0);
    LSetDrawingMode(true, gList);
    ListInval();
}

static void DoSearch(int nextPage)
{
    char  url[MC_URLMAX];
    char  msg[160];
    int   i, n, r;

    if (!nextPage)
    {
        GetTerms(gTerms, sizeof(gTerms));
        Trim(gTerms);
        gPage = 1;
    }
    else gPage++;

    r = mc_search_url(gTerms, gPage, url, sizeof(url));
    if (r != 0)
    {
        SysBeep(10);
        Tell("Type a word or two to search for (e.g. popeye).");
        return;
    }
    Say("\nSearch: %s (page %d)\n", gTerms, gPage);

    ReadOptions();
    gCancel = 0;
    n = mcf_search(&gEnv, gTerms, gPage, &gSearch);
    if (n < 0)
    {
        if (gEnv.message[0] != 0) Tell(gEnv.message);
        if (nextPage) gPage--;
        return;
    }

    ClearList();
    gListMode = 0;
    gItem = -1;
    gConfirmRow = -1;
    if (n == 0)
    {
        Tell(nextPage ? "No more results." : "Nothing found. Try other words.");
        if (nextPage) gPage--;
        return;
    }
    for (i = 0; i < n; i++)
        Say("  %s | %s\n", gSearch.hits[i].identifier, gSearch.hits[i].title);
    FillResults(-1);
    sprintf(msg, "%ld items found; showing %d-%d. Pick one and click Play "
            "to see its videos.",
            gSearch.numFound, (gPage - 1) * MC_MAXHITS + 1,
            (gPage - 1) * MC_MAXHITS + n);
    Tell(msg);
}

/* ==================================================================
   Play
   ================================================================== */

/* -1 when nothing is selected */
static int SelectedRow(void)
{
    Cell c;
    c.h = 0;
    c.v = 0;
    if (gList == NULL || !LGetSelect(true, &c, gList)) return -1;
    if (c.v < 0) return -1;
    if (gListMode == 0 && c.v >= gSearch.count) return -1;
    if (gListMode == 1 && c.v >= gMeta.nfiles) return -1;
    return c.v;
}

/* 0 = playing, -1 = failed (status line already says why).
   0.2: mc_fetch.c does it all: metadata over https, the http probe,
   the Baseline check, then stream or download-then-play. */
static int PlayItem(const McHit *hit)
{
    ReadOptions();
    gCancel = 0;
    return mcf_play_item(&gEnv, hit, &gMeta, gUrls);
}

static void Busy(int on)
{
    if (on)
    {
        SetCursor(*GetCursor(watchCursor));
        gBusy = 1;
        UpdateStop();
    }
    else
    {
        gBusy = 0;
        UpdateStop();
        InitCursor();
        FlushEvents(mDownMask | mUpMask | keyDownMask | keyUpMask
                    | autoKeyMask, 0);
    }
}

static void UpdateButtons(void)
{
    EnableDlgItem(kItemPlay, gListMode ? gMeta.nfiles > 0
                                       : gSearch.count > 0);
    EnableDlgItem(kItemMore, gListMode == 0 && gSearch.count == MC_MAXHITS);
    EnableDlgItem(kItemBack, gListMode == 1);
}

/* (0.3) results -> the item's video files */
static void OpenItem(int row)
{
    char msg[200];
    int  rc;
    Busy(1);
    ReadOptions();
    gCancel = 0;
    rc = mcf_open_item(&gEnv, &gSearch.hits[row], &gMeta);
    Busy(0);
    if (rc != 0) { SysBeep(10); return; }
    gListMode = 1;
    gItem = row;
    gConfirmRow = -1;
    FillFiles();
    UpdateButtons();
    sprintf(msg, "%d video file(s)%s. Pick one, click Play. \"plays now?\" "
            "is a guess until you do.", gMeta.nfiles,
            gMeta.droppedFiles ? " (first 64 shown)" : "");
    Tell(msg);
}

static void PlayFile(int row)
{
    int rc;
    ReadOptions();
    gCancel = 0;
    gEnv.confirm_big = (row == gConfirmRow);
    gConfirmRow = -1;
    Busy(1);
    rc = mcf_play_file(&gEnv, gSearch.hits[gItem].identifier, &gMeta, row,
                       gUrls);
    Busy(0);
    gEnv.confirm_big = 0;
    LSetDrawingMode(false, gList);
    SetFileRow(row);                       /* hint -> fact */
    LSetDrawingMode(true, gList);
    ListInval();
    if (rc == 0)
    {
        Say("Done.\n");
        Tell("Opened in QuickTime Player. Press Play there.");
        return;
    }
    if (gEnv.need_confirm) gConfirmRow = row; /* status says "Play again" */
    SysBeep(10);
}

static void DoBack(void)
{
    if (gListMode != 1) return;
    gListMode = 0;
    gConfirmRow = -1;
    FillResults(gItem);
    UpdateButtons();
    Tell("Pick an item and click Play to see its videos.");
}

static void DoPlay(void)
{
    int row = SelectedRow();
    if (row < 0)
    {
        SysBeep(10);
        Tell(gListMode ? "Pick a file in the list first."
                       : "Pick an item in the list first.");
        return;
    }
    if (gListMode == 0) OpenItem(row);
    else PlayFile(row);
}

static void DoSearchBusy(int more)
{
    Busy(1);
    if (more && gListMode != 0) { Busy(0); return; }
    DoSearch(more);
    Busy(0);
    UpdateButtons();
}

/* ==================================================================
   events
   ================================================================== */

static pascal OSErr AEQuit(const AppleEvent *e, AppleEvent *r, long refcon)
{
#pragma unused(e, r, refcon)
    gQuitAsk = 1;              /* (0.5s) the loop decides (DoQuit) */
    return noErr;
}

static pascal OSErr AEIgnore(const AppleEvent *e, AppleEvent *r, long refcon)
{
#pragma unused(e, r, refcon)
    return noErr;
}

static void InstallAEHandlers(void)
{
    (void)AEInstallEventHandler(kCoreEventClass, kAEOpenApplication,
                                NewAEEventHandlerUPP(AEIgnore), 0, false);
    (void)AEInstallEventHandler(kCoreEventClass, kAEOpenDocuments,
                                NewAEEventHandlerUPP(AEIgnore), 0, false);
    (void)AEInstallEventHandler(kCoreEventClass, kAEPrintDocuments,
                                NewAEEventHandlerUPP(AEIgnore), 0, false);
    (void)AEInstallEventHandler(kCoreEventClass, kAEQuitApplication,
                                NewAEEventHandlerUPP(AEQuit), 0, false);
}

static void DoKey(EventRecord *ev)
{
    char      c = (char)(ev->message & charCodeMask);
    short     item;
    DialogPtr d;

    if (ev->modifiers & cmdKey)
    {
        if (c == 'q' || c == 'Q') { DoQuit(); return; }
        if (c == '.') { DoBack(); return; }   /* (0.5s) never quits */
        if (c == 'x' || c == 'X')
        { DialogCut(gDlg); (void)ZeroScrap(); (void)TEToScrap(); return; }
        if (c == 'c' || c == 'C')
        { DialogCopy(gDlg); (void)ZeroScrap(); (void)TEToScrap(); return; }
        if (c == 'v' || c == 'V')
        { (void)TEFromScrap(); DialogPaste(gDlg); return; }
        if (c == 'a' || c == 'A')
        { SelectDialogItemText(gDlg, kItemTerms, 0, 32767); return; }
        if (c == 'p' || c == 'P') { FlashButton(kItemPlay); DoPlay(); return; }
        if (c == '[') { FlashButton(kItemBack); DoBack(); return; }
        return;
    }
    if (c == 0x0D) { FlashButton(kItemSearch); DoSearchBusy(0); return; }
    if (c == 0x03) { FlashButton(kItemPlay); DoPlay(); return; } /* Enter */
    if (c == 0x1B) { DoBack(); return; }      /* (0.5s) Esc: back, never quit */
    (void)DialogSelect(ev, &d, &item);
}

static void DoMouse(EventRecord *ev)
{
    WindowPtr w;
    DialogPtr d;
    short     item;
    Point     pt;

    switch (FindWindow(ev->where, &w))
    {
    case inDrag:
        DragWindow(w, ev->where, &qd.screenBits.bounds);
        break;
    case inContent:
        if (w != FrontWindow()) { SelectWindow(w); break; }
        SetPort(gDlg);
        pt = ev->where;
        GlobalToLocal(&pt);
        if (gList != NULL && PtInRect(pt, &gListRect))
        {
            if (LClick(pt, ev->modifiers, gList)) DoPlay();  /* dbl-click */
            break;
        }
        if (DialogSelect(ev, &d, &item))
        {
            if (item == kItemSearch) DoSearchBusy(0);
            if (item == kItemMore)   DoSearchBusy(1);
            if (item == kItemPlay)   DoPlay();
            if (item == kItemBack)   DoBack();
            if (item == kItemQuit)   DoQuit();
            if (item == kItemStop)   DoStopStreaming();   /* (0.5s) */
            if (item == kItemDlFirst || item == kItemInsecure)
                SetCheck(item, !GetCheck(item));
        }
        break;
    case inSysWindow:
        SystemClick(ev, w);
        break;
    }
}

/* (0.5s) one pass of streaming work after every event or null event */
static int StreamPass(void)
{
    int moved = mcs_poll();
    mcs_status_tick();
    if (gStopStream) DoStopStreaming();
    if (gQuitAsk) DoQuit();
    if (mcs_quit_ready()) gQuit = 1;
    UpdateStop();
    return moved;
}

static void EventLoop(void)
{
    EventRecord ev;
    DialogPtr   d;
    short       item;
    int         moved = 0;

    while (!gQuit)
    {
        if (!WaitNextEvent(everyEvent, &ev, mcs_sleep(moved, 15), NULL))
        {
            if (FrontWindow() == gDlg) (void)DialogSelect(&ev, &d, &item);
            moved = StreamPass();
            continue;
        }
        switch (ev.what)
        {
        case kHighLevelEvent:
            (void)AEProcessAppleEvent(&ev);
            break;
        case updateEvt:
            DoUpdate((WindowPtr)ev.message);
            break;
        case activateEvt:
            if (gList != NULL)
                LActivate((ev.modifiers & activeFlag) != 0, gList);
            (void)DialogSelect(&ev, &d, &item);
            break;
        case keyDown:
        case autoKey:
            DoKey(&ev);
            break;
        case mouseDown:
            DoMouse(&ev);
            break;
        case osEvt:
            if (((ev.message >> 24) & 0xFF) == suspendResumeMessage)
            {
                EventRecord a = ev;
                int on = (ev.message & resumeFlag) != 0;
                a.what = activateEvt;
                a.message = (long)gDlg;
                a.modifiers = (short)(on ? activeFlag : 0);
                if (on) (void)TEFromScrap();
                if (gList != NULL) LActivate(on, gList);
                (void)DialogSelect(&a, &d, &item);
            }
            break;
        default:
            if (IsDialogEvent(&ev)) (void)DialogSelect(&ev, &d, &item);
            break;
        }
        moved = StreamPass();
    }
}

/* Test hook: a file "Media Center Query" beside the app. Line 1 = the
   search; later lines: download, insecure, confirm, list, file N
   (mc_hook_parse). 1 = found and has a query. */
static int ReadQueryFile(McHook *h)
{
    FSSpec app, spec;
    Str255 pname;
    short  ref;
    long   n;
    static char text[1024];

    if (GetAppSpec(&app) != 0) return 0;
    CToPStr(kQueryFileName, pname);
    if (FSMakeFSSpec(app.vRefNum, app.parID, pname, &spec) != noErr) return 0;
    if (FSpOpenDF(&spec, fsRdPerm, &ref) != noErr) return 0;
    n = (long)sizeof(text) - 1;
    (void)FSRead(ref, &n, text);
    FSClose(ref);
    if (n < 0) n = 0;
    text[n] = 0;
    return mc_hook_parse(text, h);
}

/* Unattended run (mcf_hook_run): search; then "list" (log the files of
   hit 1), "file N" (play file N of hit 1) or, as in 0.2, play hit 1.
   Never an alert; a big download needs the keyword "confirm". */
static void HookRun(void)
{
    int rc;
    Say("(Query taken from the file \"%s\")\n", kQueryFileName);
    SetCheck(kItemDlFirst, gHookSpec.download);
    SetCheck(kItemInsecure, gHookSpec.insecure);
    gEnv.confirm_how = "Add a line \"confirm\" to the query file to "
                       "allow it.";
    Busy(1);
    gCancel = 0;
    mcs_set_insecure(gHookSpec.insecure);
    rc = mcf_hook_run(&gEnv, &gHookSpec, &gSearch, &gMeta, gUrls);
    Busy(0);
    Say(rc == 0 ? "\nDone.\n" : "\nFAILED.\n");
    if (mcs_streaming())
    {   /* (0.5s) QuickTime reads the film from us: keep serving */
        Say("Serving the film to QuickTime Player (hook: serve %ld s, "
            "stop %ld s; Quit twice or Stop ends it).\n",
            gHookSpec.serve, gHookSpec.stop);
        mcs_hook_timers((unsigned long)gHookSpec.serve,
                        (unsigned long)gHookSpec.stop);
        EventLoop();
    }
}

int main(void)
{
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
    OpenLog();                  /* truncates the old log (see the diff) */
    Say("Classic Media Center 0.5u -- Internet Archive films in "
        "QuickTime Player 6. https via macTLS (BearSSL), streamed through "
        "a local proxy.\n");
    {
        long ver = 0;
        int  lib = 0;
        int  lvl = DecoderLevel(&ver, &lib);
        mc_set_decoder_level(lvl);
        Say("Decoder: 'avc1' version 0x%08lX, AVCDecLib %s -> %s.\n",
            (unsigned long)ver, lib ? "found" : "not found",
            lvl ? "Main/High (B-frame rewrite on)" : "Baseline only");
    }
    if (InitOpenTransport() != noErr)
    {
        Say("Open Transport is not available.\n");
        SysBeep(10);
        return 1;
    }
    gOTReady = 1;
    SetupEnv();
    SetupStreaming();          /* (0.5s) */

    gDlg = MakeDialog();
    if (gDlg == NULL) { Say("No dialog.\n"); SysBeep(10); return 1; }
    SetPort(gDlg);
    MakeList();
    if (gList == NULL) { SysBeep(10); return 1; }
    EnableDlgItem(kItemPlay, 0);
    EnableDlgItem(kItemMore, 0);
    EnableDlgItem(kItemStop, 0);
    EnableDlgItem(kItemBack, 0);
    SelectDialogItemText(gDlg, kItemTerms, 0, 32767);
    gHook = ReadQueryFile(&gHookSpec);
    if (gHook) HookRun();
    else
    {
        Tell("Type a word or two (e.g. popeye) and press Return.");
        Say("Event loop.\n");
        EventLoop();
    }

    LDispose(gList);
    gList = NULL;
    DisposeDialog(gDlg);
    gDlg = NULL;
    mcs_shutdown();            /* (0.5s) listener, connections, upstreams */
    Say("Exit.\n");
    OSTLS_SaveSeed();
    if (gOTReady) CloseOpenTransport();
    if (gLog) { fclose(gLog); gLog = NULL; }
    return 0;
}
