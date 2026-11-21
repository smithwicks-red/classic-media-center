# Classic Media Center 0.2 (b19) -- build notes

Status (14 Nov 2026, ~00:10 Eastern): built in CodeWarrior Pro 8.3 in the VM (Mac OS 9.2.2,
QEMU/UTM) on the FIRST make, 0 errors. Unattended guest tests T1-T3 passed. H264 Stream was
NOT changed (still 1.2). The interactive UI (checkboxes, Stop button, Cmd-period) has never
been clicked by a human.

## What 0.2 does
* ONE transport for everything: `https_client` (macTLS/BearSSL for https, plain OT for http).
  Search and metadata go to `https://archive.org`, certificates checked. `mc_api_base` /
  `mc_dl_base` in mc_core.c hold the bases (the host tests point them at local servers).
* Play = metadata (https) -> for each candidate/URL: fetch only the moov through the SAME
  plain-http redirect chain QuickTime would follow (`http://archive.org/download/...`),
  watching every hop (new `HC_Request.redirect` callback) -> Baseline check (unchanged rule)
  -> decision `mcf_decide`: every hop http and "Download first" off -> reference movie,
  QuickTime streams (0.1 behaviour); any hop https or "Download first" on -> DOWNLOAD-THEN-PLAY.
* Download: `https://` version of the same URL into `Media Center Downloads` beside the app
  (created if missing), progress in the status line (KB / KB / %) and every 10 % in the log,
  WaitNextEvent in the idle callback, Cmd-period / Esc / Stop cancel, free-space check before
  starting (size from metadata; again from Content-Length) leaving 5 MB, partial `.part` file
  deleted on any failure, a complete `.mov` reused (verified: box walk + its trailing moov
  equals the one we would write + size), then the Finder opens it in QuickTime Player.
* Errors in plain words, e.g. "Certificate check failed (clock is set to 1956?). Set the date
  in the Date & Time control panel." / "...not signed by a known authority (BearSSL 62)." /
  "Not enough disk space: this film needs 26 MB (plus 5 MB spare) and only N MB are free."
* macTLS debug log OFF (`OSTLS_LogInit` is never called; `MacTLSTest.log` on the Desktop was
  not touched by any b19 run). Entropy: `OSTLS_CollectEntropy` in the idle callback,
  `OSTLS_SaveSeed` at exit.

## The local movie (design, proven on host and in the VM)
The file is written exactly as it arrives, except two 4-byte box types relabelled on the fly:
`ftyp`->`free`, original `moov`->`free`; `mdat` stays at the same offset. At the end the
rewritten moov (H264Stream.c `BuildReferenceMovie(moov, len, NULL)`: same rewrite as for
streaming, but the dref is one `alis` entry with flags 1 = "media in this file", QuickTime's
own self-reference) is APPENDED. Chunk offsets are untouched and still right because no byte
before them moved. Sequential writes only, no seek, works for moov-at-front and moov-at-end.
While the original moov streams past it is compared byte for byte with the moov that was
checked; a different file is refused (`MCF_ERR_CHANGED`).

## Files (LF master; CR + MacRoman on the guest)
mediacenter/: `MediaCenter.c` (0.2), `H264Stream.c` (1.2 + self-contained dref; still says
"H264 Stream 1.2"), `mc_core.c/.h` (bases, `mc_meta_url`), NEW `mc_fetch.c/.h` (box walker,
probe, download, reuse, messages, the whole Play flow), `mc_test.c` (search URL now https),
`guest_scripts/` (helper scripts as used tonight), this file.
tls18/: `https_client.c/.h` (+ `HC_RedirectProc redirect` field, called before a redirect is
followed; HTTPSGet.c unaffected because it memsets the request), unchanged macTLS tree,
`host/` rig + NEW `host/mc02_host.c`, `mc02_server.py`, `run_mc02_tests.sh`,
`build_mc02.sh`, `verify_local_movie.py`, fake Toolbox additions (PBGetCatInfoSync,
PBHGetVInfoSync with FAKE_FREE_KB, FSpDirCreate, FSpRename, sub-folder dirIDs).
NEVER copy `tls18/host/` to the guest.

## Guest project (as built)
* `Macintosh HD:mc02:MediaCenter:` = Finder duplicate of `Macintosh HD:tls18:` (guest to
  guest), renamed; `HTTPSGet.mcp` renamed `MediaCenter.mcp`; its `HTTPSGet Data` trashed.
* Sent (b19send.sh): MediaCenter.c, H264Stream.c, mc_core.c/.h, mc_fetch.c/.h,
  https_client.c/.h (into the mc02 copy only; tls18 untouched).
* `Remove Files {"HTTPSGet.c","https_client.c"}`, `Add Files` (full mc02 paths)
  https_client.c, mc_core.c, mc_fetch.c, MediaCenter.c. H264Stream.c is NOT a project file
  (#included). 272 project files + ThreadsLib + the stationery/OT libraries inherited from
  mc01's H264Stream.mcp via tls18 (same library set as mc01's Media Center project).
* Prefix `https_prefix.h` (inherited); MediaCenter.c compiles fine with it.
* PPC Project (raw codes): `«class PR02»` File Name "Media Center", `«class PR05»` minimum
  3072 K, `«class PR06»` preferred 4096 K.
  Why: code+data ~400 KB (app 401,843 bytes), TLS ~123 KB per transfer, globals ~30 KB
  (McSearch 6 KB, McMeta 3.7 KB, URL table 7 KB, H264Stream buffers 24 KB), moov +
  rewritten moov held at once during a download (Popeye 233 KB + 233 KB; 600-700 KB each
  for 80 MB films; MemAlloc falls back to temp memory), Dialog/List Manager, stack. ~2 MB
  real peak; 3 MB minimum leaves room, 4 MB preferred for very large moovs.
* Make: `b18make.sh ID rb` (Remove Binaries + Make), 218 s. Result: 0 errors, 2 warnings
  (prf.c:44 "possible unwanted ';'", linker "ignored 'errno' (data) in OTXTILib"), both
  known from b18. `Compile Files` on mc_fetch.c and MediaCenter.c alone first: 0 messages.
* Guest fixes needed: NONE (no source change after the host proof).

## Test hook
File `Media Center Query` beside the app: line 1 = search; any later line may say
`download` (= tick "Download first") and/or `insecure` (= tick "Skip certificate checks
(unsafe)"). The hook searches, plays hit 0, logs to `Media Center Log`, quits. Currently
renamed `Media Center Query.off`.

## Guest test recipes (scripts in guest_scripts/)
* Sanity: `echo 'return (current date) as string' | agent.sh id 30`.
* Launch: write the hook file (open for access / write / close), Finder `open file
  "Macintosh HD:mc02:MediaCenter:Media Center"`. While it runs: ONLY `b19poll.sh ID` (process
  list + log tail); never `tell` Media Center.
* Playback check: `tell application "QuickTime Player"`: `play movie 1`, `delay 15`,
  `current time of movie 1`, `stop movie 1`, `close movie 1 saving no` -> decoder statistics
  appended to `share:h264log.txt` (`STATS pictures shown`, `STATS decode errors survived`).
  Then `tell application "QuickTime Player" to quit` before the next test.
* Note: Finder `free space of disk "Macintosh HD"` gave -1700 in the agent (number too
  big?) -- do not use it in a script that must go on to launch something.

## Results (VM; ticks/KB/s say nothing about a G3/G4)
* T1 "popeye president": search over https (TLS 1.3, 65 ticks), metadata https, moov via
  `http://archive.org/download/...` 302 -> `http://dn801208.us...` (plain chain) -> "Every hop
  was plain http" -> reference movie on the Desktop -> QuickTime: 15.0 s, 445 pictures,
  0 decode errors (`b19 T1 h264log.txt`, last block).
* T2 same + `download`: 26,989,036 bytes over TLS 1.3 from dn801208.us in 45.3 s
  (581 KB/s), 10 % progress lines, saved `Media Center Downloads:Popeye_forPresident_512k.mov`
  (26,583 KB, self-contained) -> QuickTime: duration 370.3 s (whole film), 15 s played,
  449 pictures, 0 errors (`b19 T2 Media Center Log.txt`).
* T3 rerun: "Already downloaded ... Not fetching it again." -> played 6 s, 181 pictures,
  0 errors (`b19 T3 Media Center Log.txt`).
* T4 (H264 Stream 1.3): not done (see below).

## Host proof (`sh tls18/host/run_mc02_tests.sh asan CLIPDIR`)
CLIPDIR: ffmpeg clips 320x240 Baseline + AAC: base.mp4 (faststart, 1.9 MB), end.mp4 (moov
at end), high.mp4 (High), other.mp4 (same as base, different moov), big.mp4 (19.5 MB):
`ffmpeg -f lavfi -i testsrc2=size=320x240:rate=25:duration=30 -f lavfi -i sine=...:duration=30
 -c:v libx264 -profile:v baseline -pix_fmt yuv420p -g 50 -b:v 450k -x264-params ref=1
 -c:a aac -b:a 64k -ac 2 [-movflags +faststart] base.mp4`.
14/14 under ASan+UBSan (x86-32), 13/13 big-endian PowerPC under qemu-ppc (zig cc;
`CC=.../zcc.sh ARCHFLAGS= sh host/build_mc02.sh ppc`, `BIG=0 RUNNER=qemu-ppc ...`):
T1 http chain -> stream; T2 http->https chain -> download; T3 forced download; T4 reuse;
T5 moov-at-end (Range probe) + download; T6 High refused before download; T7 changed file
refused, partial removed; T8 server drop -> no partial; T9 Cmd-period -> no partial;
T10 disk full refused; T11 untrusted cert message; T12 `insecure` works; T13 1956 clock
message; T14 19.5 MB. `verify_local_movie.py` per download: layout, all original bytes
identical except the two relabelled types, all stco offsets identical and inside mdat,
dref = alis self-ref, audio = QuickTime v1 + wave, ffmpeg framemd5 identical (video 750 /
audio 1292 frames). tls18 `run_host_tests.sh asan`: 30/30 still pass. `mc_test`: 164/164.

## Toolbox calls: status
Compiled and exercised in the VM: FSpDirCreate, PBGetCatInfoSync (folder dirID),
FSMakeFSSpec, FSpCreate, FSpOpenDF, FSWrite, FSRead/GetEOF/SetFPos (reuse check), FSpRename,
FlushVol, OpenWithFinder, TickCount, GetDateTime, InitOpenTransport at start.
Compiled, NOT exercised in the VM: PBHGetVInfoSync free-space numbers (HFS+ reports values
pinned to 2 GB; only refusal matters), the cancel path (CbIdle: Cmd-period, Esc, a click in
the Stop button's rect, DragWindow while busy), GetControlValue/SetControlValue on the two
checkboxes, HiliteControl of Stop, FSpDelete of a partial file on failure.

## Not done / next
* H264 Stream 1.3: not built. It would need its own copy of the 269-file TLS project plus
  `StreamURL` rewritten onto `mcf_fetch_moov` / `mcf_download` (all the logic is in
  mc_fetch.c, so the code change is small; it is one more ~4-minute build + test).
* By hand: the UI (see below), a Cmd-period during a real download, an item whose storage
  node really forces https (`.ca.` hosts), a big film (CC605 Apple II Forever, 120 MB).
* Streaming over https (in-app loopback proxy) remains the next stage.
