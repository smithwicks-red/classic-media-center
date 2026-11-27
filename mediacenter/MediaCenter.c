/*
    MediaCenter.c -- "Classic Media Center" 0.6.1
    0.6.1 (M61): a second play of a film rewrites our own "x (MC).mov"
    (or saves "x 2 (MC).mov" with the tail kept) and the STREAMING record,
    the close-window event and the log use the name really saved; the LCD
    rate is an 8 s average and says "buffered" when QuickTime is not
    fetching; rows/buttons in Geneva; "may not play?" fits; the LCD detail
    shows no debug lines; the font-smoothing lookup asks InterfaceLib.
    0.6 (U3): the skinned window. At Thousands of colours or more (decided
    once at launch) Media Center opens a fixed 480x360 borderless window
    (plainDBox: a 1-pixel outline outside the content) whose brushed
    metal, aqua buttons, list and LCD are painted by mc_skin.c/mc_layout.c
    into a 32-bit GWorld (temporary memory first, else the app heap), with
    QuickDraw text runs and ONE TextEdit record (the search field) drawn
    into the same GWorld, then CopyBits of the dirty rectangles. The
    behaviour (keys, focus, Quit-twice while streaming, the LCD) lives in
    the portable controller mc_ui.c (host-tested); this file only turns
    Toolbox events into its calls and carries out what it asks. Below
    Thousands of colours, or if the GWorld cannot be had, the 0.5u dialog
    below runs exactly as before (the functions marked "dispatcher" choose).
    Menus Apple/File/Edit, About box, Quit = Cmd-Q / File > Quit / the round
    close button / the Quit Apple event; Esc and Cmd-. never quit. Headings:
    "Apple Garamond", else "MC Garamond" (FOND+sfnt 279 in this app's own
    resource fork, MC Garamond.rsrc), else "Hoefler Text", else "Palatino".
    Debug: Apple event 'MCdb'/'dump' N (0-9 demo scenes, 50 font test,
    99 the live window) writes "MC dump N" beside the app (GUEST_STEPS_06).
    Partition 9216 K preferred / 8192 K minimum (as 0.5u).

    ---- 0.5u notes follow ----
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
        hint (0.6.1: honest -- jerky video is the CPU, Download first
        does not cure it). (0.6.1) G3 policy: decode budget per file
        (MHz from Gestalt 'pclk'), a lighter rendition auto-picked, and
        a pre-buffer slower than the film -> download first.
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
#define kUserAgent   "ClassicMediaCenter/0.6.1 (Macintosh; PPC Mac OS 9; QuickTime 6)"
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
#include <QDOffscreen.h>      /* (0.6) the skin's 32-bit GWorld         */
#include <TextEdit.h>
#include <Menus.h>
#include <Devices.h>         /* (0.6 guest fix) OpenDeskAcc */
#include <Fonts.h>
#include <Resources.h>
#include <CodeFragments.h>    /* (0.6) font smoothing query, by name    */
#include "mc_ui.h"            /* (0.6) portable controller (host-tested) */
#include "mc_demo.h"          /* (0.6) demo scenes, dump header          */

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

/* (0.6) the skinned window */
static int        gSkin = 0;          /* 1 = skin, 0 = the 0.5u dialog   */
static WindowPtr  gWin = NULL;
static GDHandle   gWinDev = NULL;
static GWorldPtr  gGW = NULL;
static TEHandle   gTE = NULL;
static McCtl      gCtl;
static int        gHeadNext = 0;      /* next UIStatus = LCD headline    */
static int        gSuspended = 0;
static Rect       gCopy;              /* GWorld area still to CopyBits   */
static short      gCaretWas = -1;

static void SkinFlush(void);
static void SkinFlash(int w);
static void SkinFieldText(char *s, long max);
static int  SkinItemWidget(short item);
static pascal OSErr AEDump(const AppleEvent *e, AppleEvent *r, long refcon);

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

    if (gSkin)
    {   /* (0.6) Tell / CbStatus -> LCD headline, other Say lines -> detail */
        if (gHeadNext) mcu_status(&gCtl, line);
        else mcu_detail(&gCtl, line);
        SkinFlush();
        return;
    }
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
    gHeadNext = 1;
    Say("%s\n", msg);
    gHeadNext = 0;
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
    GrafPtr  save;
    Rect     fr;
    CGrafPtr sp;
    GDHandle sd;

    if (gSkin && w == gWin)
    {   /* (0.6) everything is in the GWorld: copy it all */
        GetGWorld(&sp, &sd);
        SkinFlush();
        BeginUpdate(w);
        SetRect(&gCopy, 0, 0, MC_WIN_W, MC_WIN_H);
        SkinFlush();
        EndUpdate(w);
        SetGWorld(sp, sd);
        return;
    }
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
    if (gSkin) return;     /* (0.6) mc_ui_sync derives every state */
    GetDialogItem(gDlg, item, &type, &h, &r);
    HiliteControl((ControlHandle)h, (short)(on ? 0 : 255));
}

static int GetCheck(short item)
{
    short  type;
    Handle h;
    Rect   r;
    if (gSkin)
        return item == kItemDlFirst ? gCtl.ui.dl_first : gCtl.ui.insecure;
    GetDialogItem(gDlg, item, &type, &h, &r);
    return GetControlValue((ControlHandle)h) != 0;
}

static void SetCheck(short item, int on)
{
    short  type;
    Handle h;
    Rect   r;
    if (gSkin)
    {
        if (item == kItemDlFirst) mcu_set_options(&gCtl, on, gCtl.ui.insecure);
        else mcu_set_options(&gCtl, gCtl.ui.dl_first, on);
        SkinFlush();
        return;
    }
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
    if (gSkin) { SkinFieldText(s, max); return; }
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
    if (gSkin) { SkinFlash(SkinItemWidget(item)); return; }
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
    gHeadNext = 1;
    UIStatus(line);
    gHeadNext = 0;
}

/* (0.6) McfEnv.progress: the LCD's bar, KB/s and time left */
static void CbProgress(void *r, unsigned long done, unsigned long total,
                       unsigned long ticks)
{
#pragma unused(r)
    if (!gSkin) return;
    mcu_progress(&gCtl, done, total, ticks);
    SkinFlush();
}

static void SkinBusyEvent(EventRecord *ev);

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
        if (gSkin) SkinBusyEvent(&ev);      /* (0.6) */
        else
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
    if (gSkin)
    {
        mcu_set_serving(&gCtl, mcs_streaming());
        mcu_idle(&gCtl, (unsigned long)TickCount());
        SkinFlush();
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

/* (0.6.1) the clock, for the G3 decode budget (mc_core.h G3 POLICY):
   gestaltProcClkSpeed ('pclk', Hz) is in every PowerPC Mac OS since
   7.x and fits a long up to 2.1 GHz (every G3/G4 that runs OS 9);
   'mclk' (gestaltProcClkSpeedMHz, MHz) is newer (CarbonLib/OS X era)
   and only the fallback. 0 = unknown (mc_core then assumes
   MC_G3_MHZ_UNKNOWN on a G3). NEEDS GUEST: the Pismo's value. */
static long CpuMHz(void)
{
    long v = 0;
    if (Gestalt('pclk', &v) == noErr && v >= 50000000L)
        return (v + 500000L) / 1000000L;
    v = 0;
    if (Gestalt('mclk', &v) == noErr && v >= 50L && v <= 5000L)
        return v;
    return 0;
}

static void SetupStreaming(void)
{
    McsEnv m;
    long   cpu, mhz;
    memset(&m, 0, sizeof m);
    m.now = CbTicks;
    m.log = CbLog;
    m.status = CbStatus;
    m.qt_close = CbQtClose;
    m.idle = CbIdle;                 /* (0.5t) pre-buffer steps */
    m.cpu_g3 = IsG3(&cpu);
    mhz = CpuMHz();
    mc_set_cpu(m.cpu_g3, mhz);                     /* (0.6.1) G3 policy */
    mcs_init(&m);
    Say("CPU type 0x%04lX, %ld MHz (%s).\n", (unsigned long)cpu, mhz,
        m.cpu_g3 ? "G3: decode budget per file, lighter file auto-picked, "
                   "slow network -> download first"
                 : "not a G3");
}

/* the Stop button: enabled while Media Center works OR a film streams */
static void UpdateStop(void)
{
    int on = gBusy || mcs_streaming();
    if (gSkin) { mcu_set_serving(&gCtl, mcs_streaming()); return; }
    if (on != gStopOn) { EnableDlgItem(kItemStop, on); gStopOn = on; }
}

static void DoStopStreaming(void)
{
    gStopStream = 0;
    if (mcs_streaming()) mcs_stop();
}

static int DoQuit(void)
{
    int r;
    gQuitAsk = 0;
    r = mcs_quit();
    if (r == MCS_QUIT_NOW) gQuit = 1;
    return r;                  /* (0.6) the controller shows it */
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
    gEnv.progress = CbProgress;              /* (0.6) */
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
    if (gSkin) { mcu_show_empty(&gCtl, "No results"); return; }
    if (gList == NULL) return;
    LSetDrawingMode(false, gList);
    LDelRow(0, 0, gList);                 /* 0 rows = delete them all */
    LSetDrawingMode(true, gList);
}

/* (0.3) the one list shows either the results or the item's files */
static void ListInval(void)
{
    GrafPtr save;
    if (gSkin) { SkinFlush(); return; }
    GetPort(&save);
    SetPort(gDlg);
    InvalRect(&gListRect);
    SetPort(save);
}

static void SelectRow(int row)
{
    Cell c;
    if (row < 0) return;
    if (gSkin) { mc_ui_select(&gCtl.ui, row); mc_ui_sync(&gCtl.ui); return; }
    c.h = 0;
    c.v = (short)row;
    LSetSelect(true, c, gList);
    LAutoScroll(gList);
}

static void FillResults(int select)
{
    int  i;
    Cell c;
    if (gSkin)
    {
        mcu_set_confirm(&gCtl, 0);
        mcu_show_results(&gCtl, &gSearch, gTerms, gPage, select);
        SkinFlush();
        return;
    }
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
    if (gSkin) { mcu_update_file(&gCtl, &gMeta, i); return; }
    mc_file_row(&gMeta.files[i], row, (long)sizeof row);
    c.h = 0;
    c.v = (short)i;
    LSetCell(row, (short)strlen(row), c, gList);
}

static void FillFiles(void)
{
    int i;
    if (gSkin)
    {
        mcu_set_confirm(&gCtl, 0);
        mcu_show_files(&gCtl, &gMeta, gSearch.hits[gItem].title, 0);
        SkinFlush();
        return;
    }
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
    if (gSkin)
    {
        int r = mcu_selected(&gCtl);
        if (gListMode == 0 && r >= gSearch.count) return -1;
        if (gListMode == 1 && r >= gMeta.nfiles) return -1;
        return r;
    }
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
        if (gSkin) { mcu_set_busy(&gCtl, 1); SkinFlush(); }
        UpdateStop();
    }
    else
    {
        gBusy = 0;
        if (gSkin) { mcu_set_busy(&gCtl, 0); SkinFlush(); }
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
    if (!gSkin) LSetDrawingMode(false, gList);   /* gList is NULL in skin mode */
    SetFileRow(row);                       /* hint -> fact */
    if (!gSkin) LSetDrawingMode(true, gList);
    ListInval();
    if (rc == 0)
    {
        Say("Done.\n");
        if (gEnv.g3note[0] != 0) Tell(gEnv.g3note);   /* (0.6.1) */
        else Tell("Opened in QuickTime Player. Press Play there.");
        return;
    }
    if (gEnv.need_confirm) gConfirmRow = row; /* status says "Play again" */
    if (gSkin) { mcu_set_confirm(&gCtl, gEnv.need_confirm); SkinFlush(); }
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
    /* (0.6) debug dumps for the unattended guest run (GUEST_STEPS_06.md) */
    (void)AEInstallEventHandler('MCdb', 'dump',
                                NewAEEventHandlerUPP(AEDump), 0, false);
}

/* ==================================================================
   (0.6) THE SKIN: the Toolbox layer of the skinned window
   ==================================================================
   Pixels: mc_skin.c / mc_layout.c paint into the 32-bit GWorld gGW
   (xRGB, big-endian, any rowBytes). Text: QuickDraw runs (mc_ui_runs)
   drawn into the same GWorld, then TextEdit's search field (its inPort
   IS the GWorld, clipped to the white field interior). SkinFlush copies
   what changed to the window with CopyBits. Behaviour: mc_ui.c.
   Port convention: the window is the current port; anything that draws
   into the GWorld goes through PortGW() ... PortWin(). */

static int  StreamPass(void);
static void SkinOsEvent(EventRecord *ev);

#define kFieldL 20            /* TextEdit's view: inside the white field */
#define kFieldT 34
#define kFieldR 374
#define kFieldB 50
#define kFieldMax 255         /* gTerms is 256                           */

static short gFontHead = 0, gFontUI = 1, gFontSmall = 1, gFontBtn = 0;
static short gFontTest[3];    /* MC Garamond, Hoefler Text, Palatino     */
static short gChainId[4];
static char  gFontHeadName[40] = "Geneva (no serif found)";
static short gAppRes = -1;
static int   gMcgFromApp = 0;
static McRun gRuns[200];
static McUi  gDemo;           /* the 'MCdb'/'dump' scenes (12 KB)        */
static char  gSmooth[64] = "font smoothing: not asked";
static MenuHandle gMenuApple = NULL, gMenuFile = NULL, gMenuEdit = NULL;
static unsigned long gLastClick = 0;
static Point gLastPt;

static const char *const kHeadChain[4] =
    { "Apple Garamond", "MC Garamond", "Hoefler Text", "Palatino" };

static void PortGW(void)  { SetGWorld(gGW, NULL); }
static void PortWin(void) { if (gWin != NULL) SetGWorld((CGrafPtr)gWin, gWinDev); }

static void FieldRect(Rect *r) { SetRect(r, kFieldL, kFieldT, kFieldR, kFieldB); }

static void AddCopy(short l, short t, short r, short b)
{
    if (l < 0) l = 0;
    if (t < 0) t = 0;
    if (r > MC_WIN_W) r = MC_WIN_W;
    if (b > MC_WIN_H) b = MC_WIN_H;
    if (r <= l || b <= t) return;
    if (gCopy.right <= gCopy.left || gCopy.bottom <= gCopy.top)
        SetRect(&gCopy, l, t, r, b);
    else
    {
        if (l < gCopy.left) gCopy.left = l;
        if (t < gCopy.top) gCopy.top = t;
        if (r > gCopy.right) gCopy.right = r;
        if (b > gCopy.bottom) gCopy.bottom = b;
    }
}

static short FontNamed(const char *name)
{
    Str255 p;
    short  id = 0;
    CToPStr(name, p);
    GetFNum(p, &id);
    return id;
}

/* the heading chain, the UI fonts, and where MC Garamond came from */
static void SkinFonts(void)
{
    int    i;
    Str255 p;
    Handle h;
    gFontHead = 0;
    for (i = 0; i < 4; i++)
    {
        gChainId[i] = FontNamed(kHeadChain[i]);
        if (gChainId[i] != 0 && gFontHead == 0)
        {
            gFontHead = gChainId[i];
            strcpy(gFontHeadName, kHeadChain[i]);
        }
    }
    if (gFontHead == 0) gFontHead = applFont;
    gFontTest[0] = gChainId[1];
    gFontTest[1] = gChainId[2];
    gFontTest[2] = gChainId[3];
    /* (0.6.1) rows, search field, hint and checkbox labels in Geneva (0.6
       used Charcoal, heavy on the guest); button labels in Charcoal, PLAIN
       (no synthetic bold), as standard OS 9 buttons: MC_F_BTN. GetFNum
       gives 0 when "Charcoal" is absent: 0 is the system font itself.
       (The host mock-ups are DejaVu stand-ins.) No style leaked:
       SkinDrawRuns sets font, size, face and mode before every run. */
    gFontBtn = FontNamed("Charcoal");
    gFontSmall = FontNamed("Geneva");
    if (gFontSmall == 0) gFontSmall = applFont;
    gFontUI = gFontSmall;
    CToPStr("MC Garamond", p);
    SetResLoad(false);
    h = GetNamedResource('FOND', p);
    SetResLoad(true);
    gMcgFromApp = (h != NULL && HomeResFile(h) == gAppRes);
}

/* (0.6.1) IsAntiAliasedTextEnabled is a QuickDraw-text call (Mac OS 8.5
   and later), exported by InterfaceLib; 0.6 asked AppearanceLib for it and
   got -2802 (cfragNoSymbolErr: the library is there, the symbol is not).
   Looked up by name so no library is added to the project; if it is not
   there either, the line just says "n/a" (no error number in the log). */
typedef pascal Boolean (*McAAProc)(SInt16 *minSize);
static void SkinSmoothing(void)
{
    static const char *const libs[2] = { "InterfaceLib", "AppearanceLib" };
    CFragConnectionID conn;
    Ptr               mainAddr, sym = NULL;
    Str255            lib, name, errName;
    CFragSymbolClass  cls;
    SInt16            minSize = 0;
    OSErr             err = -1;
    int               i;
    CToPStr("IsAntiAliasedTextEnabled", name);
    for (i = 0; i < 2 && (err != noErr || sym == NULL); i++)
    {
        sym = NULL;
        CToPStr(libs[i], lib);
        err = GetSharedLibrary(lib, kPowerPCCFragArch, kFindCFrag, &conn,
                               &mainAddr, errName);
        if (err != noErr)
            err = GetSharedLibrary(lib, kPowerPCCFragArch, kReferenceCFrag,
                                   &conn, &mainAddr, errName);
        if (err == noErr) err = FindSymbol(conn, name, &sym, &cls);
    }
    if (err != noErr || sym == NULL)
    {
        strcpy(gSmooth, "font smoothing: n/a");
        return;
    }
    if (((McAAProc)sym)(&minSize))
        sprintf(gSmooth, "font smoothing: ON at %d pt and up", (int)minSize);
    else
        strcpy(gSmooth, "font smoothing: OFF");
}

static short RoleFont(int role, short *face)
{
    *face = 0;
    switch (role)
    {
    case MC_F_TITLE: case MC_F_HEAD: return gFontHead;
    case MC_F_UIBOLD: *face = bold; return gFontUI;
    case MC_F_UI: return gFontUI;
    case MC_F_BTN: return gFontBtn;   /* (0.6.1) plain Charcoal */
    case MC_F_LCDB: *face = bold; return gFontSmall;
    case MC_F_TEST_MCG:  return gFontTest[0] ? gFontTest[0] : applFont;
    case MC_F_TEST_HOEF: return gFontTest[1] ? gFontTest[1] : applFont;
    case MC_F_TEST_PAL:  return gFontTest[2] ? gFontTest[2] : applFont;
    default: return gFontSmall;
    }
}

/* the text runs of the dirty rect, into the GWorld (port = GWorld) */
static void SkinDrawRuns(int n, const Rect *dirty)
{
    int      i;
    RGBColor c;
    for (i = 0; i < n; i++)
    {
        const McRun *u = &gRuns[i];
        Rect   clip, box;
        char   buf[130];
        short  len, face, w, x;
        SetRect(&box, u->box.l, u->box.t, u->box.r, u->box.b + (u->emboss ? 1 : 0));
        if (!SectRect(&box, dirty, &clip)) continue;
        ClipRect(&clip);
        TextFont(RoleFont(u->role, &face));
        TextSize(u->size);
        TextFace(face);
        TextMode(srcOr);
        len = (short)strlen(u->text);
        memcpy(buf, u->text, (size_t)len);
        w = (short)(u->box.r - u->box.l);
        if (TextWidth(buf, 0, len) > w)
            (void)TruncText(w, buf, &len, u->trunc == MC_TR_MIDDLE ? truncMiddle : truncEnd);
        x = u->box.l;
        if (u->align == MC_AL_CENTER) x = (short)(u->box.l + (w - TextWidth(buf, 0, len)) / 2);
        if (u->align == MC_AL_RIGHT) x = (short)(u->box.r - TextWidth(buf, 0, len));
        if (u->emboss)
        {
            c.red = c.green = 0xEC00; c.blue = 0xEE00;
            RGBForeColor(&c);
            MoveTo(x, (short)(u->base + 1));
            DrawText(buf, 0, len);
        }
        c.red = (unsigned short)(u->rgb[0] * 257);
        c.green = (unsigned short)(u->rgb[1] * 257);
        c.blue = (unsigned short)(u->rgb[2] * 257);
        RGBForeColor(&c);
        MoveTo(x, u->base);
        DrawText(buf, 0, len);
    }
    c.red = c.green = c.blue = 0;
    RGBForeColor(&c);
    TextFace(0);
}

static void FontTestInfo(char *i1, char *i2)
{
    sprintf(i1, "Heading chain -> %s (id %d); MC Garamond id %d%s",
            gFontHeadName, (int)gFontHead, (int)gChainId[1],
            gChainId[1] ? (gMcgFromApp ? ", from the app" : ", NOT from the app") : "");
    sprintf(i2, "Apple Garamond %d, Hoefler Text %d, Palatino %d (0 = none); %s",
            (int)gChainId[0], (int)gChainId[2], (int)gChainId[3], gSmooth);
}

/* paint ui (or the font test) inside d into the GWorld */
static void SkinPaint(const McUi *ui, int fontTest, const McRect *d)
{
    PixMapHandle pm = GetGWorldPixMap(gGW);
    McCanvas     cv;
    Rect         dr, fr, sect;
    RGBColor     black = { 0, 0, 0 }, white = { 0xFFFF, 0xFFFF, 0xFFFF };
    int          n;
    char         i1[160], i2[200];
    CGrafPtr     savePort;
    GDHandle     saveDev;

    GetGWorld(&savePort, &saveDev);
    if (!LockPixels(pm)) return;
    mc_canvas_init(&cv, (unsigned char *)GetPixBaseAddr(pm),
                   (long)((**pm).rowBytes & 0x3FFF), MC_WIN_W, MC_WIN_H);
    if (fontTest)
    {
        mc_demo_font_render(&cv, d);
        FontTestInfo(i1, i2);
        n = mc_demo_font_runs(d, i1, i2, gRuns, 200);
    }
    else
    {
        mc_ui_render(ui, &cv, d);
        n = mc_ui_runs(ui, d, gRuns, 200);
    }
    SetRect(&dr, d->l, d->t, d->r, d->b);
    PortGW();
    RGBForeColor(&black);
    RGBBackColor(&white);
    SkinDrawRuns(n, &dr);
    FieldRect(&fr);
    if (!fontTest && ui == &gCtl.ui && !ui->draw_field_text && gTE != NULL
        && SectRect(&fr, &dr, &sect))
    {
        ClipRect(&fr);
        TEUpdate(&fr, gTE);
    }
    ClipRect(&(**pm).bounds);
    SetGWorld(savePort, saveDev);
}

static void SkinCopy(void)
{
    RGBColor black = { 0, 0, 0 }, white = { 0xFFFF, 0xFFFF, 0xFFFF };
    CGrafPtr savePort;
    GDHandle saveDev;
    if (gWin == NULL || gCopy.right <= gCopy.left || gCopy.bottom <= gCopy.top) return;
    GetGWorld(&savePort, &saveDev);
    PortWin();
    RGBForeColor(&black);
    RGBBackColor(&white);
    CopyBits((BitMap *)*GetGWorldPixMap(gGW), &gWin->portBits,
             &gCopy, &gCopy, srcCopy, NULL);
    SetRect(&gCopy, 0, 0, 0, 0);
    SetGWorld(savePort, saveDev);
}

static void SkinFlush(void)
{
    McRect d;
    if (!gSkin || gGW == NULL) return;
    d = gCtl.ui.dirty;
    if (d.l < 0) d.l = 0;
    if (d.t < 0) d.t = 0;
    if (d.r > MC_WIN_W) d.r = MC_WIN_W;
    if (d.b > MC_WIN_H) d.b = MC_WIN_H;
    mc_rect_set(&gCtl.ui.dirty, 0, 0, 0, 0);
    if (d.r > d.l && d.b > d.t)
    {
        SkinPaint(&gCtl.ui, 0, &d);
        AddCopy(d.l, d.t, d.r, d.b);
    }
    SkinCopy();
}

static int SkinItemWidget(short item)
{
    switch (item)
    {
    case kItemSearch: return W_SEARCH;
    case kItemPlay:   return W_PLAY;
    case kItemBack:   return W_BACK;
    case kItemMore:   return W_MORE;
    case kItemStop:   return W_STOP;
    default:          return W_NONE;
    }
}

static void SkinFlash(int w)
{
    unsigned long t;
    if (!gSkin || w == W_NONE) return;
    gCtl.ui.pressed = w;
    mc_ui_sync(&gCtl.ui);
    SkinFlush();
    Delay(8, &t);
    gCtl.ui.pressed = W_NONE;
    mc_ui_sync(&gCtl.ui);
    SkinFlush();
}

/* ---------------- the search field (TextEdit) ---------------- */
static void SkinFieldText(char *s, long max)
{
    long n = 0;
    if (gTE != NULL)
    {
        n = (**gTE).teLength;
        if (n > max - 1) n = max - 1;
        memcpy(s, *(**gTE).hText, (size_t)n);
    }
    s[n] = 0;
}

static void FieldChanged(void)
{
    Rect fr;
    if (gTE == NULL) return;
    HLock((**gTE).hText);
    mcu_set_field(&gCtl, *(**gTE).hText, (long)(**gTE).teLength);
    HUnlock((**gTE).hText);
    FieldRect(&fr);
    AddCopy(fr.left, fr.top, fr.right, fr.bottom);
}

/* TextEdit always draws into the GWorld, clipped to the white field */
static void TEBegin(void)
{
    Rect fr;
    PortGW();
    FieldRect(&fr);
    ClipRect(&fr);
}

static void TEEnd(void)
{
    ClipRect(&(**GetGWorldPixMap(gGW)).bounds);
    PortWin();
    FieldChanged();
}

static void CbTeKey(void *r, int ch, int mods)
{
    short s0, s1, len;
    (void)r; (void)mods;
    if (gTE == NULL) return;
    s0 = (**gTE).selStart; s1 = (**gTE).selEnd; len = (**gTE).teLength;
    TEBegin();
    if (ch == MC_K_HOME) TESetSelect(0, 0, gTE);
    else if (ch == MC_K_END) TESetSelect(len, len, gTE);
    else if (ch == 0x7F)                  /* forward delete */
    {
        if (s0 == s1 && s1 < len) TESetSelect(s0, s0 + 1, gTE);
        TEDelete(gTE);
    }
    else if (ch >= 0x20 && len - (s1 - s0) >= kFieldMax) SysBeep(1);
    else if (ch >= 0x20 || ch == 0x08 || (ch >= MC_K_LEFT && ch <= MC_K_DOWN))
        TEKey((char)ch, gTE);
    TEEnd();
}

static void CbTeEdit(void *r, int act)
{
    (void)r;
    if (gTE == NULL) return;
    TEBegin();
    switch (act)
    {
    case MC_ACT_CUT:  TECut(gTE);  (void)ZeroScrap(); (void)TEToScrap(); break;
    case MC_ACT_COPY: TECopy(gTE); (void)ZeroScrap(); (void)TEToScrap(); break;
    case MC_ACT_PASTE:
        (void)TEFromScrap();
        if ((**gTE).teLength - ((**gTE).selEnd - (**gTE).selStart)
            + TEGetScrapLength() <= kFieldMax) TEPaste(gTE);
        else SysBeep(1);
        break;
    case MC_ACT_SELALL: TESetSelect(0, 32767, gTE); break;
    }
    TEEnd();
}

static void CbTeFocus(void *r, int on)
{
    (void)r;
    if (gTE == NULL) return;
    TEBegin();
    if (on) TEActivate(gTE); else TEDeactivate(gTE);
    TEEnd();
}

/* while TEClick tracks a drag-selection (it draws into the window port):
   keep serving; save and restore the port around the poll */
static pascal Boolean SkinClickLoop(TEPtr te)
{
    CGrafPtr p;
    GDHandle g;
    (void)te;
    GetGWorld(&p, &g);
    (void)mcs_poll();
    SetGWorld(p, g);
    return true;
}

/* ---------------- the controller's requests ---------------- */
static void CbSearch(void *r, int more) { (void)r; DoSearchBusy(more); }
static void CbPlay(void *r)   { (void)r; DoPlay(); }
static void CbBack(void *r)   { (void)r; DoBack(); }
static void CbCancel(void *r) { (void)r; gConfirmRow = -1; }
static void CbFlash(void *r, int w) { (void)r; SkinFlash(w); }

/* Stop: during a transfer as 0.5u's CbIdle (cancel; end the stream after
   it), otherwise as 0.5u's Stop button (DoStopStreaming) */
static void CbStop(void *r)
{
    (void)r;
    if (gBusy)
    {
        gCancel = 1;
        if (mcs_streaming()) gStopStream = 1;
    }
    else DoStopStreaming();
}

/* Quit: during a transfer as 0.5u's Quit Apple event (cancel, then the
   loop's DoQuit decides); otherwise DoQuit = mcs_quit's rules */
static int CbQuit(void *r)
{
    (void)r;
    if (gBusy) { gQuitAsk = 1; return MCU_QUIT_NOW; }
    return DoQuit();
}

/* ---------------- About ---------------- */
/* About (chosen: its own WaitNextEvent loop, not ModalDialog). Every pass
   does what SkinEventLoop does (same sleep, StreamPass on every event and
   null event), so QuickTime is never starved; update events go to our
   window or the dialog; Return/Enter/Esc/Cmd-. press OK; the dialog's own
   events go through IsDialogEvent/DialogSelect until OK. */
static void SkinAbout(void)
{
    Handle      ditl = NewHandleClear(2);
    DialogPtr   d;
    Rect        b, scr = qd.screenBits.bounds;
    Str255      title;
    short       item = 0, left, top, hit;
    EventRecord ev;
    DialogPtr   dd;
    int         moved = 0, done = 0, ch;
    if (ditl == NULL) return;
    AddItem(ditl, 128, 300, 148, 370, ctrlItem + btnCtrl, "OK");
    AddItem(ditl,  14,  20,  32, 370, statText, "Classic Media Center 0.6.1");
    AddItem(ditl,  36,  20,  52, 370, statText, "by smithwicks-red");
    AddItem(ditl,  56,  20,  72, 370, statText, "http://smithwicks.freeshell.org");
    AddItem(ditl,  84,  20, 118, 370, statText,
            "MC Garamond is based on EB Garamond (SIL Open Font License)");
    left = (short)((scr.right - scr.left - 390) / 2);
    top = (short)((scr.bottom - scr.top - 160) / 3);
    SetRect(&b, left, top, left + 390, top + 160);
    CToPStr("About Classic Media Center", title);
    d = NewDialog(NULL, &b, title, true, dBoxProc, (WindowPtr)-1L, false, 0, ditl);
    if (d == NULL) return;
    (void)SetDialogDefaultItem(d, 1);
    (void)item;
    while (!done && !gQuit)
    {
        if (!WaitNextEvent(everyEvent, &ev, mcs_sleep(moved, 15), NULL))
        {
            OSTLS_CollectEntropy();
            moved = StreamPass();
            continue;
        }
        if (ev.what == updateEvt && (WindowPtr)ev.message == (WindowPtr)gWin)
            DoUpdate(gWin);
        else if (ev.what == kHighLevelEvent) (void)AEProcessAppleEvent(&ev);
        else if (ev.what == osEvt) SkinOsEvent(&ev);
        else if (ev.what == keyDown || ev.what == autoKey)
        {
            ch = (int)(ev.message & charCodeMask);
            if (ch == 13 || ch == 3 || ch == 27
                || (ch == '.' && (ev.modifiers & cmdKey) != 0))
                done = 1;
        }
        else if (IsDialogEvent(&ev))
        {
            if (DialogSelect(&ev, &dd, &hit) && dd == d && hit == 1) done = 1;
        }
        moved = StreamPass();
    }
    DisposeDialog(d);
    PortWin();
}

/* ---------------- menus ---------------- */
#define kMenuApple 128
#define kMenuFile  129
#define kMenuEdit  130

static void SkinMenus(void)
{
    Str255 p;
    p[0] = 1; p[1] = 0x14;                     /* the apple */
    gMenuApple = NewMenu(kMenuApple, p);
    CToPStr("About Classic Media Center\311", p); /* MacRoman ellipsis */
    AppendMenu(gMenuApple, p);
    CToPStr("(-", p);
    AppendMenu(gMenuApple, p);
    AppendResMenu(gMenuApple, 'DRVR');
    CToPStr("File", p);
    gMenuFile = NewMenu(kMenuFile, p);
    CToPStr("Quit/Q", p);
    AppendMenu(gMenuFile, p);
    CToPStr("Edit", p);
    gMenuEdit = NewMenu(kMenuEdit, p);
    CToPStr("Undo/Z;(-;Cut/X;Copy/C;Paste/V;Clear;Select All/A", p);
    AppendMenu(gMenuEdit, p);
    InsertMenu(gMenuApple, 0);
    InsertMenu(gMenuFile, 0);
    InsertMenu(gMenuEdit, 0);
    DrawMenuBar();
}

static void SkinMenuState(void)
{
    short i;
    int   da = FrontWindow() != gWin;      /* a desk accessory is in front */
    if (gMenuEdit == NULL) return;
    if (da) EnableItem(gMenuEdit, 1); else DisableItem(gMenuEdit, 1);
    for (i = 3; i <= 7; i++)
        if (da || !gBusy) EnableItem(gMenuEdit, i); else DisableItem(gMenuEdit, i);
}

static void SkinMenu(long mr)
{
    short  menu = (short)(mr >> 16), item = (short)(mr & 0xFFFF);
    Str255 name;
    switch (menu)
    {
    case kMenuApple:
        if (item == 1) SkinAbout();
        else
        {
            GetMenuItemText(gMenuApple, item, name);
            (void)OpenDeskAcc(name);
            PortWin();
        }
        break;
    case kMenuFile:
        if (item == 1) (void)mcu_quit(&gCtl);   /* CbQuit -> DoQuit */
        break;
    case kMenuEdit:
        if (SystemEdit((short)(item - 1))) break;
        if (item == 3) (void)mcu_edit(&gCtl, MC_ACT_CUT);
        if (item == 4) (void)mcu_edit(&gCtl, MC_ACT_COPY);
        if (item == 5) (void)mcu_edit(&gCtl, MC_ACT_PASTE);
        if (item == 7) (void)mcu_edit(&gCtl, MC_ACT_SELALL);
        if (item == 6 && !gBusy && gTE != NULL) { TEBegin(); TEDelete(gTE); TEEnd(); }
        break;
    }
    HiliteMenu(0);
    SkinFlush();
}

/* ---------------- events ---------------- */
static int Mods(const EventRecord *ev)
{
    return ((ev->modifiers & cmdKey) ? MC_MOD_CMD : 0)
         | ((ev->modifiers & shiftKey) ? MC_MOD_SHIFT : 0);
}

static void SkinKey(EventRecord *ev)
{
    int c = (int)(ev->message & charCodeMask);
    if (ev->modifiers & cmdKey)
    {
        long mr;
        SkinMenuState();
        mr = MenuKey((char)c);
        if ((mr >> 16) != 0) { SkinMenu(mr); return; }
    }
    /* Quit: DoQuit (via the controller's cb.quit) sets gQuit itself */
    (void)mcu_key(&gCtl, c, Mods(ev));
    SkinFlush();
}

/* a pressed button: follow the mouse, keep serving, act on release */
static void SkinTrack(int w)
{
    const McRect *k = mc_ui_rect(w);
    Rect  r;
    Point pt;
    int   inside = 1, in;
    SetRect(&r, k->l, k->t, k->r, k->b);
    SkinFlush();
    while (StillDown())
    {
        GetMouse(&pt);
        in = PtInRect(pt, &r);
        if (in != inside) { inside = in; mcu_track(&gCtl, w, in); SkinFlush(); }
        (void)mcs_poll();
    }
    (void)mcu_release(&gCtl, w, inside);      /* Quit: see CbQuit */
    SkinFlush();
}

static void SkinScrollTrack(int part)
{
    Point         pt;
    unsigned long t;
    GetMouse(&pt);
    do
    {
        (void)mcu_scroll(&gCtl, part, pt.h, pt.v);
        SkinFlush();
        Delay(part == MC_PART_UP || part == MC_PART_DOWN ? 3 : 8, &t);
        (void)mcs_poll();
        GetMouse(&pt);
    } while (StillDown());
}

static void SkinThumbTrack(Point start)
{
    Point pt;
    mcu_thumb_begin(&gCtl, start.v);
    while (StillDown())
    {
        GetMouse(&pt);
        mcu_thumb_move(&gCtl, pt.v);
        SkinFlush();
        (void)mcs_poll();
    }
}

static void SkinMouse(EventRecord *ev, int busy)
{
    WindowPtr w;
    short     part = FindWindow(ev->where, &w);
    Point     pt;
    Rect      fr;
    RgnHandle sv;
    int       r, wid, prt, dbl;
    if (part == inMenuBar)
    {
        if (busy) return;          /* MenuSelect would stop the transfer */
        SkinMenuState();
        SkinMenu(MenuSelect(ev->where));
        return;
    }
    if (part == inSysWindow) { if (!busy) SystemClick(ev, w); return; }
    if (w != gWin || part == inDesk) return;
    if (w != FrontWindow()) { SelectWindow(w); return; }
    PortWin();
    pt = ev->where;
    GlobalToLocal(&pt);
    dbl = (unsigned long)ev->when - gLastClick <= (unsigned long)GetDblTime()
          && pt.h - gLastPt.h < 5 && gLastPt.h - pt.h < 5
          && pt.v - gLastPt.v < 5 && gLastPt.v - pt.v < 5;
    gLastClick = (unsigned long)ev->when;
    gLastPt = pt;
    r = mcu_click(&gCtl, pt.h, pt.v, Mods(ev), dbl, &wid, &prt);
    SkinFlush();
    switch (r)
    {
    case MCU_CLICK_DRAG:
        DragWindow(w, ev->where, &qd.screenBits.bounds);
        break;
    case MCU_CLICK_TRACK:
        SkinTrack(wid);
        break;
    case MCU_CLICK_FIELD:
        if (gTE != NULL)
        {   /* TEClick tracks with GetMouse = local coordinates of the
               current port: run it in the window port (the GWorld's
               bounds start at 0,0 too, so the field rect is the same),
               clipped to the field, then repaint from the GWorld */
            FieldRect(&fr);
            sv = NewRgn();
            (**gTE).inPort = (GrafPtr)gWin;
            PortWin();
            if (sv != NULL) GetClip(sv);
            ClipRect(&fr);
            TEClick(pt, (ev->modifiers & shiftKey) != 0, gTE);
            if (sv != NULL) { SetClip(sv); DisposeRgn(sv); }
            else ClipRect(&(**GetGWorldPixMap(gGW)).bounds);
            (**gTE).inPort = (GrafPtr)gGW;
            mc_ui_dirty(&gCtl.ui, W_FIELD);
            AddCopy(fr.left, fr.top, fr.right, fr.bottom);
        }
        break;
    case MCU_CLICK_SCROLL:
        SkinScrollTrack(prt);
        break;
    case MCU_CLICK_THUMB:
        SkinThumbTrack(pt);
        break;
    default:
        break;
    }
    SkinFlush();
}

static void SkinActivate(int on)
{
    if (on) (void)TEFromScrap();
    mcu_activate(&gCtl, on);
    SkinFlush();
}

static void SkinOsEvent(EventRecord *ev)
{
    if (((ev->message >> 24) & 0xFF) == suspendResumeMessage)
    {
        int on = (ev->message & resumeFlag) != 0;
        gSuspended = !on;
        SkinActivate(on);
    }
}

/* caret blink: TEIdle draws into the GWorld; copy when it changed */
static void SkinNull(void)
{
    Rect fr;
    if (gTE == NULL || !gCtl.te_on) return;
    TEBegin();
    TEIdle(gTE);
    ClipRect(&(**GetGWorldPixMap(gGW)).bounds);
    PortWin();
    if ((**gTE).caretState != gCaretWas)
    {
        gCaretWas = (**gTE).caretState;
        FieldRect(&fr);
        AddCopy(fr.left, fr.top, fr.right, fr.bottom);
        SkinCopy();
    }
}

/* CbIdle (a transfer is running): only Stop, Quit, Esc/Cmd-., drag,
   updates and Apple events -- the controller's busy rules decide */
static void SkinBusyEvent(EventRecord *ev)
{
    switch (ev->what)
    {
    case kHighLevelEvent: (void)AEProcessAppleEvent(ev); break;
    case updateEvt:       DoUpdate((WindowPtr)ev->message); break;
    case activateEvt:     if ((WindowPtr)ev->message == gWin)
                              SkinActivate((ev->modifiers & activeFlag) != 0);
                          break;
    case osEvt:           SkinOsEvent(ev); break;
    case keyDown:
    case autoKey:
        /* Esc / Cmd-. -> CbStop (gCancel); Cmd-Q -> CbQuit (gQuitAsk) */
        (void)mcu_key(&gCtl, (int)(ev->message & charCodeMask), Mods(ev));
        break;
    case mouseDown:       SkinMouse(ev, 1); break;
    }
}

static void SkinEventLoop(void)
{
    EventRecord ev;
    int         moved = 0;
    while (!gQuit)
    {
        /* the sleep is 0.5u's: 0 after bytes moved, 1 while QuickTime
           has connections, else 15 (caret blink too), suspended or not */
        if (!WaitNextEvent(everyEvent, &ev, mcs_sleep(moved, 15), NULL))
        {
            SkinNull();
            moved = StreamPass();
            continue;
        }
        switch (ev.what)
        {
        case kHighLevelEvent: (void)AEProcessAppleEvent(&ev); break;
        case updateEvt:       DoUpdate((WindowPtr)ev.message); break;
        case activateEvt:     if ((WindowPtr)ev.message == gWin)
                                  SkinActivate((ev.modifiers & activeFlag) != 0);
                              break;
        case osEvt:           SkinOsEvent(&ev); break;
        case keyDown:
        case autoKey:         SkinKey(&ev); break;
        case mouseDown:       SkinMouse(&ev, 0); break;
        }
        moved = StreamPass();
    }
}

/* ---------------- the 'MCdb'/'dump' Apple event ---------------- */
static OSErr WriteDump(int n)
{
    PixMapHandle  pm = GetGWorldPixMap(gGW);
    FSSpec        app, spec;
    Str255        pname;
    char          name[32];
    unsigned char h[MC_DUMP_HDR];
    long          rb = (long)((**pm).rowBytes & 0x3FFF), cnt, y;
    short         ref;
    OSErr         err;
    Ptr           base;

    if (GetAppSpec(&app) != 0) return fnfErr;
    sprintf(name, "MC dump %d", n);
    CToPStr(name, pname);
    err = FSMakeFSSpec(app.vRefNum, app.parID, pname, &spec);
    if (err == fnfErr) err = FSpCreate(&spec, (OSType)0x3F3F3F3FUL /* '????' without a trigraph */, 'BINA', smSystemScript);
    if (err != noErr) return err;
    err = FSpOpenDF(&spec, fsRdWrPerm, &ref);
    if (err != noErr) return err;
    (void)SetEOF(ref, 0);
    if (!LockPixels(pm)) { FSClose(ref); return memFullErr; }
    base = GetPixBaseAddr(pm);
    mc_dump_header(h, MC_WIN_W, MC_WIN_H, rb);
    cnt = MC_DUMP_HDR;
    err = FSWrite(ref, &cnt, h);
    for (y = 0; y < MC_WIN_H && err == noErr; y++)
    {
        cnt = rb;
        err = FSWrite(ref, &cnt, base + y * rb);
    }
    FSClose(ref);
    (void)FlushVol(NULL, app.vRefNum);
    return err;
}

static int MakeGWorld(void)
{
    Rect  r;
    OSErr err;
    SetRect(&r, 0, 0, MC_WIN_W, MC_WIN_H);
    err = NewGWorld(&gGW, 32, &r, NULL, NULL, useTempMem);
    if (err != noErr || gGW == NULL)
    {
        gGW = NULL;
        err = NewGWorld(&gGW, 32, &r, NULL, NULL, 0);
    }
    if (err != noErr || gGW == NULL) { gGW = NULL; return 0; }
    (void)LockPixels(GetGWorldPixMap(gGW));
    return 1;
}

static OSErr DumpScene(int n)
{
    McRect      all;
    OSErr       err;
    CGrafPtr    sp;
    GDHandle    sd;
    const char *what = mc_demo_name(n);
    if (what == NULL) return paramErr;
    if (gGW == NULL && (gSkin || !MakeGWorld())) return memFullErr;
    if (n == MC_DEMO_LIVE && !gSkin)
    {
        Say("DUMP 99: the 0.5u dialog is showing (below Thousands of colours): nothing to dump.\n");
        return errAEEventNotHandled;
    }
    GetGWorld(&sp, &sd);
    mc_rect_set(&all, 0, 0, MC_WIN_W, MC_WIN_H);
    if (n == MC_DEMO_LIVE) SkinFlush();
    else if (n == MC_DEMO_FONTTEST) SkinPaint(NULL, 1, &all);
    else
    {
        (void)mc_demo_scene(n, &gDemo);
        SkinPaint(&gDemo, 0, &all);
    }
    err = WriteDump(n);
    if (gSkin && n != MC_DEMO_LIVE)
    {   /* back to the live window */
        mc_ui_dirty_all(&gCtl.ui);
        SkinFlush();
    }
    SetGWorld(sp, sd);
    Say("DUMP %d %s -> \"MC dump %d\" err %d; heading font \"%s\" (id %d)%s\n",
        n, what, n, (int)err, gFontHeadName, (int)gFontHead,
        gSkin ? "" : " [dialog mode: temporary GWorld]");
    if (n == MC_DEMO_FONTTEST)
    {
        char i1[160], i2[200];
        FontTestInfo(i1, i2);
        Say("FONTTEST %s\nFONTTEST %s\n", i1, i2);
    }
    return err;
}

static pascal OSErr AEDump(const AppleEvent *e, AppleEvent *r, long refcon)
{
    long     n = -1;
    DescType t;
    Size     sz;
    OSErr    err;
    (void)r; (void)refcon;
    err = AEGetParamPtr(e, keyDirectObject, typeLongInteger, &t, &n,
                        (Size)sizeof n, &sz);
    if (err != noErr) return err;
    return DumpScene((int)n);
}

/* ---------------- open / close ---------------- */
static int DepthOK(void)
{
    GDHandle gd = GetMainDevice();
    return gd != NULL && (**(**gd).gdPMap).pixelSize >= 16;
}

/* 1 = the skin is up; 0 = use the 0.5u dialog (logged why) */
static int SkinOpen(void)
{
    static McUiCb cb;
    Rect   b, scr = qd.screenBits.bounds, dest, view;
    Str255 title;
    short  left, top, proc;
    int    depth = 0;
    GDHandle gd = GetMainDevice();

    gAppRes = CurResFile();
    if (gd != NULL) depth = (**(**gd).gdPMap).pixelSize;
    if (!DepthOK())
    {
        Say("Window: the 0.5u dialog (screen at %d bits: the skin needs "
            "Thousands of colours or more).\n", depth);
        return 0;
    }
    if (!MakeGWorld())
    {
        Say("Window: the 0.5u dialog (no memory for the 480x360 GWorld).\n");
        return 0;
    }
    SkinFonts();
    SkinSmoothing();
    left = (short)((scr.right - scr.left - MC_WIN_W) / 2);
    top  = (short)((scr.bottom - scr.top - MC_WIN_H) / 3 + 20);
    if (top < 44) top = 44;
    SetRect(&b, left, top, left + MC_WIN_W, top + MC_WIN_H);
    proc = plainDBox;
    CToPStr("Classic Media Center", title);
    gWin = NewCWindow(NULL, &b, title, false, proc, (WindowPtr)-1L, false, 0);
    if (gWin == NULL)
    {
        DisposeGWorld(gGW);
        gGW = NULL;
        Say("Window: the 0.5u dialog (NewCWindow failed).\n");
        return 0;
    }
    SetPort(gWin);
    gWinDev = GetGDevice();
    SetRect(&gCopy, 0, 0, 0, 0);

    /* the search field: one TextEdit record whose port is the GWorld */
    PortGW();
    TextFont(gFontSmall);
    TextSize(12);
    TextFace(0);
    FieldRect(&view);
    dest = view;
    dest.right = (short)(dest.left + 3000);   /* one line, never wraps */
    gTE = TENew(&dest, &view);
    if (gTE != NULL)
    {
        TEAutoView(true, gTE);
        TESetClickLoop(NewTEClickLoopUPP(SkinClickLoop), gTE);
    }
    PortWin();

    cb.search = CbSearch; cb.play = CbPlay; cb.back = CbBack; cb.stop = CbStop;
    cb.cancel = CbCancel; cb.quit = CbQuit; cb.te_key = CbTeKey;
    cb.te_edit = CbTeEdit; cb.te_focus = CbTeFocus; cb.flash = CbFlash;
    cb.options = NULL;
    mcu_init(&gCtl, &cb);
    gSkin = 1;
    SkinMenus();
    SkinFlush();                     /* paint the GWorld before showing */
    ShowWindow(gWin);
    SelectWindow(gWin);
    Say("Window: skin 480x360 (%d-bit screen, WDEF %d, GWorld rowBytes %d); "
        "headings \"%s\" (id %d; MC Garamond id %d%s); %s.\n", depth,
        (int)proc, (int)((**GetGWorldPixMap(gGW)).rowBytes & 0x3FFF),
        gFontHeadName, (int)gFontHead, (int)gChainId[1],
        gChainId[1] ? (gMcgFromApp ? " from the app" : " NOT from the app") : "",
        gSmooth);
    return 1;
}

static void SkinClose(void)
{
    if (gTE != NULL) { TEDispose(gTE); gTE = NULL; }
    if (gWin != NULL) { DisposeWindow(gWin); gWin = NULL; }
    if (gGW != NULL) { DisposeGWorld(gGW); gGW = NULL; }
    gSkin = 0;
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
    if (gQuitAsk)
    {
        if (gSkin) { gQuitAsk = 0; (void)mcu_quit(&gCtl); }  /* (0.6) LCD warning */
        else DoQuit();
    }
    if (mcs_quit_ready()) gQuit = 1;
    UpdateStop();
    if (gSkin)
    {
        mcu_idle(&gCtl, (unsigned long)TickCount());
        SkinFlush();
    }
    return moved;
}

static void EventLoop(void)
{
    EventRecord ev;
    DialogPtr   d;
    short       item;
    int         moved = 0;

    if (gSkin) { SkinEventLoop(); return; }     /* (0.6) */
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
    Say("Classic Media Center 0.6.1 -- Internet Archive films in "
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

    /* (0.6) the skin at Thousands of colours or more, else 0.5u's dialog */
    gSkin = SkinOpen();
    if (!gSkin)
    {
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
    }
    gHook = ReadQueryFile(&gHookSpec);
    if (gHook) HookRun();
    else
    {
        Tell("Type a word or two (e.g. popeye) and press Return.");
        Say("Event loop.\n");
        EventLoop();
    }

    if (gSkin) SkinClose();
    else
    {
    LDispose(gList);
    gList = NULL;
    DisposeDialog(gDlg);
    gDlg = NULL;
    }
    mcs_shutdown();            /* (0.5s) listener, connections, upstreams */
    Say("Exit.\n");
    OSTLS_SaveSeed();
    if (gOTReady) CloseOpenTransport();
    if (gLog) { fclose(gLog); gLog = NULL; }
    return 0;
}
