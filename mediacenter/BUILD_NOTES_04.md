# Classic Media Center 0.4 (b21e) -- build notes

Status (16 Nov 2026): HOST PROOF ONLY (Linux x86_64, gcc 13, ASan+UBSan; plus
big-endian PowerPC under qemu-ppc for the portable parts). Not compiled by
CodeWarrior, never run on the guest or a real Mac. The Mac-only code is quoted
in full at the end.

## What 0.4 changes
* **Decoder level** (new, mc_core.c): `mc_set_decoder_level(int)` /
  `mc_decoder_level()`, default 0 = Baseline only. The app sets it once at
  start-up from `DecoderLevel()` (MediaCenter.c, Mac only): 1 = Main/High when
  an `'imdc'/'avc1'` component answers Version >= 0x00010011 AND a file
  `AVCDecLib` is in the Extensions folder or the System Folder. Logged as
  `Decoder: 'avc1' version 0x00010011, AVCDecLib found -> Main/High (B-frame
  rewrite on).` A setter, not a new argument: one machine-wide fact read once,
  needed by four places (TryCand's gate, H264 Stream's StreamURL gate, the list
  hints/marks, BuildReferenceMovie's rewrite); every 0.3 caller and test keeps
  its signature, and a program that never calls the setter (the standalone
  H264 Stream app, the 0.3 tests) is exactly 0.3. Tests use the explicit
  `mc_avc_verdict_at(info, found, level, msg, max)`.
* **Gate** (`mc_avc_verdict`, level 1 only; level 0 is the 0.3 code and
  sentences, unchanged): `mc_avc_profile` now also reads the first SPS in avcC
  (exp-Golomb, 00 00 03 skipped, scaling lists, poc type 0/1/2, every read
  bounded; failure = `sps 0`, never a different return code). New verdict code
  `MC_PLAY_UNTESTED 2` (>= 0 = playable for every existing caller).

  | level 1, avc1 track | verdict | sentence |
  |---|---|---|
  | profile 66 | 0.3 rule | `Baseline H.264, level x.y, WxH.` |
  | 77/100, 8-bit 4:2:0, progressive, <= 1280x1024 | OK 0 | `High H.264, level 3.0, 640x480.` (+ ` Large for Main/High: may not keep up.` above 848x480) |
  | same, frame_mbs_only_flag 0 | UNTESTED 2 (plays) | `... interlaced: untested -- it may not play correctly.` |
  | bit depth > 8 (any profile) | NOT_BASE -1 | `This video is 10-bit (High 10 profile); this decoder plays 8-bit video only.` |
  | chroma != 4:2:0 | NOT_BASE -1 | `This video is 4:2:2 colour (High 4:2:2 profile); this decoder plays 4:2:0 colour only.` |
  | other profile (88, 110/122/244 at 8-bit 4:2:0 ...) | NOT_BASE -1 | `This video is Extended profile (88); this decoder plays Baseline, Main and High only.` |
  | 77/100, SPS unreadable | UNKNOWN -4 | `This High profile video's settings (its SPS) could not be read.` |
  | 77/100 too big | TOO_BIG -2 | 0.3 sentence, same 1280x1024 limit (the app has no machine check) |
  | 77/100 in an 'avc3' entry | NOT_BASE -1 | decoder registers 'avc1' only; AVCReorder handles avc1 only |

  Interlaced is let through (flagged) because every caller treats >= 0 as
  "hand it over" and the existing positive code MC_PLAY_NATIVE already means
  "plays, with a different sentence"; the row is marked PLAYS NOW and the log/
  status line carries the warning.
* **List marks** at level 1: `h.264 IA` now guessed `plays now?`; the "no"
  marks read `may not play?` / `will not play` (tokens `hint:may-not-play`,
  `fact:will-not-play`) because a newer decoder is no longer the reason. Level 0:
  0.3 strings.
* **B-frame rewrite**: H264Stream.c `BuildReferenceMovie` ends with
  `AVCR_Rewrite` (inPlace 0, fileSize 0xFFFFFFFF), only at level 1. That one
  function makes BOTH moovs QuickTime opens: (1) streaming, `rebuild(moov, len,
  url)` -> reference movie; (2) download-then-play, `rebuild(moov, len, NULL)` ->
  the moov APPENDED after the downloaded bytes (ftyp/moov relabelled 'free',
  nothing moves; reuse of an existing download compares against this same
  tail). inPlace 0 is right for both: no byte that an offset points at moves.
  AVCR NOCHANGE or error: the 0.3 moov, a log line `B-frame rewrite not done
  (...); movie left as it was.`
* Version strings: banner and User-Agent 0.4; mc_fetch.c header.

## Files (all LF, ASCII, as 0.3; CR + MacRoman on the guest)
mediacenter/: `MediaCenter.c`, `H264Stream.c` (still "H264 Stream 1.2" inside),
`mc_core.c`, `mc_core.h`, `mc_fetch.c` (comments only), NEW `AVCReorder.c`,
`AVCReorder.h` (b21c r3, unmodified), NEW `mc_test04.c`, `run_tests.sh`, NEW
`mc04_build_hosttest.c`, NEW `fixtures/avc/` (13 moovs + generators).
Unchanged: `mc_fetch.h`, `mc_test.c`, everything else.
tls18/host/: `mc02_host.c` (env `MC_DECODER_LEVEL`, default `-DMC_HOST_DECODER_LEVEL`
= 0), `build_mc02.sh` (+AVCReorder.o), `mc02_server.py` (items xb0.. from env
`MC02_XFILES`), NEW `run_mc04_tests.sh`, `mc04_proof_b.py`,
`run_mc04_builder_tests.sh`, `mc04_edge_check.py`.

## CodeWarrior project
Add `AVCReorder.c` (put it beside H264Stream.c in `MediaCenter:`, H264Stream.c
#includes "AVCReorder.h") as its OWN file; it has no MSL calls, its statics
(RD32, ISTYPE ...) do not clash with H264Stream.c's. Do not add H264Stream.c.
No new library: FindNextComponent / OpenComponent / GetComponentVersion /
CloseComponent / FindFolder / FSMakeFSSpec are all in InterfaceLib. New
#include <Components.h> (Universal Interfaces 3.4.1). Then as in 0.3: Compile
mc_core.c, AVCReorder.c, mc_fetch.c, MediaCenter.c (expect 0 messages), then
b18make.sh rb. If H264 Stream (the standalone app) is rebuilt from this
H264Stream.c it also needs AVCReorder.c (or `#define kReorderBFrames 0`); it
never sets the level, so it stays Baseline-only.
Memory: during a rewrite the reference moov and its rewritten copy coexist
(the rewritten one is usually smaller); keep 3072 K min / 4096 K preferred.

## Host proof (commands)
* `sh mediacenter/run_tests.sh`: mc_test 266/266 + mc_test04 194/194.
* `CC="gcc -idirafter HOSTINC" sh tls18/host/run_mc02_tests.sh asan CLIPDIR`: 26/26
  (level 0). HOSTINC = a dir holding `asm -> /usr/include/x86_64-linux-gnu/asm`
  (this host's -m32 headers lack asm/; nothing else changed).
* `sh tls18/host/run_host_tests.sh asan`: 30/30.
* `sh tls18/host/run_mc04_tests.sh asan CLIPDIR /home/claude/s5/sim/files EXTRADIR RESDIR`
  then `python3 tls18/host/mc04_proof_b.py /home/claude/s5/sim/files RESDIR`.
* `sh tls18/host/run_mc04_builder_tests.sh DIR03 RIG03 OUT "NONB" "EDGE"`.

## Guest tests (hook file `Media Center Query`, log `Media Center Log`)
G1 Decoder 17 + AVCDecLib installed: first lines `Decoder: 'avc1' version
0x00010011, AVCDecLib found -> Main/High (B-frame rewrite on).` If it says
`0x00010000`, decoder 17 still answers the old Version constant: fix
kH264CodecVersion in the decoder (do NOT lower the app's threshold).
G2 `vietnam travel guide hanoi` CR `file 1` (.ia.mp4, High): `High H.264 ...`,
`B-frames: reorder delay N = ..` (if it has B-frames), `MARK 1 fact:plays-now`,
QuickTime plays 15 s: pictures in order, no jump at the end, lips in sync.
G3 same + `download` CR `confirm` on a SMALL High file: local movie plays, and
a second run reuses it (`Already downloaded`).
G4 `list` on that item: `.ia.mp4` rows `hint:plays-now`, MPEG4 rows `hint:may-not-play`.
G5 Move AVCDecLib to Extensions (Disabled), restart: `-> Baseline only`; G2 is
refused exactly as 0.3 (`... plays Baseline only.`, `fact:needs-newer-decoder`).
G6 Decoder 16 instead of 17: same as G5. G7 an interlaced (1080i-derived)
item: `interlaced: untested`, watch what decoder 17 does (black? fields?).
Real Mac: Mac mini G4 640x480 High B-frames (expect smooth); Pismo G3 320x240.

## Mac-only code (not compiled here)
New include in MediaCenter.c: `#include <Components.h>`. Calls: FindNextComponent,
OpenComponent, GetComponentVersion, CloseComponent (Component Manager),
FindFolder (kOnSystemDisk, kExtensionFolderType, kSystemFolderType,
kDontCreateFolder), FSMakeFSSpec, and the existing CToPStr. Constants: 'imdc'
(= decompressorComponentType, ImageCompression.h), 'avc1', 0x00010011.
Checked only by a gcc -std=c89 -pedantic syntax/type check against hand-written
UI 3.4.1 prototypes (out/logs/f_declevel_syntax.txt: 0 warnings).

```c
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
```

In main(), right after the banner:

```c
    Say("Classic Media Center 0.4 -- Internet Archive films in "
        "QuickTime Player 6. https via macTLS (BearSSL).\n");
    {
        long ver = 0;
        int  lib = 0;
        int  lvl = DecoderLevel(&ver, &lib);
        mc_set_decoder_level(lvl);
        Say("Decoder: 'avc1' version 0x%08lX, AVCDecLib %s -> %s.\n",
            (unsigned long)ver, lib ? "found" : "not found",
            lvl ? "Main/High (B-frame rewrite on)" : "Baseline only");
    }
```
