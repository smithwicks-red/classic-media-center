# Classic Media Center 0.1 -- build notes (host side done, guest untested)

## Files
- `mc_core.h`, `mc_core.c` -- portable core, strict C89, host-tested (`./run_tests.sh`: gcc -std=c89 -pedantic -Wall -Wextra -Werror, ASan+UBSan).
- `MediaCenter.c` -- the OS 9 app. **DRAFT, never compiled.** It `#include`s `H264Stream.c` with `H264STREAM_LIBRARY` defined.
- `H264Stream_profile_check.diff` -- must be applied to `codec-src/H264Stream.c` first (MediaCenter.c needs it). Made against an LF copy; the real file has **CR line endings**:
  `tr '\r' '\n' < H264Stream.c > t.c && patch t.c H264Stream_profile_check.diff && tr '\n' '\r' < t.c > H264Stream.c`
  (`H264Stream.patched.LF.c` is the result, LF endings, for reference.) It adds: the profile check before the reference movie; the reference movie points at the final (post-redirect) URL; https redirect = fail after the 2nd one (fixes the `.ca.` loop), or at once when `gHttpsIsFailure`; the log truncation (FSpOpenDF + SetEOF 0 before fopen); `#ifndef H264STREAM_LIBRARY` around the dialog + main; `#ifndef` around kUserAgent / kLogFileName.
  The patched file still builds in the existing host harness: `gcc -I../mediacenter tools/h264stream_hosttest.c mediacenter/mc_core.c` (checked). The H264 Stream CodeWarrior project must now also contain `mc_core.c` and have `mediacenter` on its access paths.

## CodeWarrior project ("Classic Media Center")
Same as H264 Stream (from its header comment): "C Console Classic" stationery (Mac OS PPC, NOT Carbon), remove HelloWorld.c, then add:
- `MediaCenter.c`, `mc_core.c` (NOT H264Stream.c -- it is #included)
- OpenTransportLib, OpenTptInternetLib (stubs), OpenTransportAppPPC.o, OpenTptInetPPC.o (static glue)
- the stationery's InterfaceLib / MSL C / MathLib as they are. List Manager is in InterfaceLib; nothing else new.
- Access paths: `codec-src` and `mediacenter`. No prefix file. Require Prototypes off.
- Memory: the app needs ~20 KB of globals (McSearch 6 KB, McMeta 3.7 KB, URL table 7 KB) + H264 Stream's 16 KB RX buffer and 8 KB header buffer. moov blocks come from MemAlloc (app heap, then temp memory) as before. Same partition as H264 Stream is plenty.

## Toolbox calls I am unsure of (check first if it does not compile)
- `LNew(&rView, &dataBounds, cSize, 0, (WindowPtr)gDlg, true, false, false, true)` -- arg order per Lists.h 3.4.1; `ListBounds` typedef; cSize 0,0 = default.
- `(**gList).selFlags = lOnlyOne;`, `LDelRow(0, 0, gList)` (0 = delete all), `LAddRow`, `LSetCell(ptr, len, cell, list)`, `LGetSelect(true, &cell, list)`, `LClick(pt, ev->modifiers, list)` (double-click = Play), `LUpdate(gDlg->visRgn, list)`, `LActivate`, `LSetDrawingMode`, `LDispose`.
- List is drawn over a *disabled userItem* with no user-item proc; clicks are caught before DialogSelect (GlobalToLocal + PtInRect). If DialogSelect erases it, install a UserItemUPP instead.
- `HiliteControl(h, 255)` on dialog buttons to disable Play / More results.
- `#pragma unused(...)` in the AE handlers; `NewAEEventHandlerUPP` (3.4.1 name; older: NewAEEventHandlerProc).
- `InvalRect` (pre-Carbon name) after refilling the list.
- `activateEvt` handling with LActivate + DialogSelect.

## Guest test plan (next agent)
1. Apply the diff, rebuild H264 Stream, run it with `http://archive.org/download/Popeye_forPresident/Popeye_forPresident_512kb.mp4`: must still play; log must say "Baseline H.264, level ..., 320x240" and the log file must not have a stale tail (run once with a long URL, then a short failing one; check the end of the log).
2. H264 Stream with `http://archive.org/download/vietnam-travel-guide-hanoi-ha-long-bay-ho-chi-minh-1_202101/Vietnam%20Travel%20Guide_%20Hanoi%2C%20Ha%20Long%20Bay%2C%20Ho%20Chi%20Minh1.ia.mp4`: must stop with "This video is High profile (100); this decoder plays Baseline only." and QuickTime Player must NOT open.
3. Build Classic Media Center. Search `popeye president` -> 9 results, first "Popeye for President". Select, Play -> QuickTime Player plays (log: metadata fetch, 1 candidate `Popeye_forPresident_512kb.mp4`, 2 URLs, Baseline).
4. Search `popeye` -> 25 rows, "More results" enabled (151 found on 2026-11-13); More -> page 2.
5. Vietnam item is not in 512Kb-filtered search results; to test refusal in the app, temporarily search `vietnam travel guide hanoi` after removing the format clause, or test via H264 Stream (step 2). Expected: candidates MPEG4 (original) then h.264 IA, both refused as High -> status shows the High-profile message.
6. Repeat Play on the same item a few times: if the download 302 lands on a `.ca.` host, the log must show "needs HTTPS" for that URL and then success via `dn8012xx.us.archive.org`.
7. Edge: empty search (beep + hint), Play with nothing selected (beep), Cmd-Q / Esc quit, double-click a row.

## Guest build results (2026-11-13, unattended agent session b16)

### Change to the patch after review (session-15 fact)
The reference movie stores the ORIGINAL URL again, not the post-redirect one. A ref movie
holding a storage-node URL failed an hour later with -3162; QuickTime follows the
archive.org/download 302 itself. `H264Stream_profile_check.diff` and `H264Stream.patched.LF.c`
were regenerated (the diff is now against the CR original converted to LF). Host check:
`gcc -std=c89 -Wall -Imediacenter -Icodec-src tools/h264stream_hosttest.c mediacenter/mc_core.c`,
then `./hst dv_quicktime_h264_512kb.mp4 redirect http://a.example/r/x/y.mp4 o.mov` ->
"Baseline H.264, level 1.3, 360x240", o.mov holds only `http://a.example/r/x/y.mp4`.
`run_tests.sh`: 164 passed, 0 failed.
Media Center stores `gUrls[ui]` -- the URL it asked for (archive.org/download/... when that
attempt worked, else the storage-server URL that did).

### Other source changes
- `mediacenter/H264Stream.c` = patched 1.2 copy (codec-src/H264Stream.c untouched). Startup line now "H264 Stream 1.2".
- `MediaCenter.c`: `EnableItem` renamed `EnableDlgItem` (clashed with Menus.h EnableItem -- the only compile error).
- `MediaCenter.c`: test hook. File `Media Center Query` beside the app (first line = search) -> search,
  log every hit as `identifier | title`, PlayItem on hit 0, log Done./FAILED., quit. No alerts anywhere
  (none existed in either source). Normal mode logs "Event loop." when it enters the event loop.
  DoSearch logs the hit list in normal mode too. MakeDialog failure now logs "No dialog.".

### Guest layout
- `Macintosh HD:mc01:H264Stream:` -- Finder copy of the desktop `H264 Classic Project Folder:H264Stream:`,
  sources replaced, `mc_core.c` added by `Add Files` (works by script, returns 0). App `H264 Stream` (1.2).
- `Macintosh HD:mc01:MediaCenter:` -- copy of the above; `Remove Files {"H264Stream.c"}`,
  `Add Files {MediaCenter.c}`, PPC Project `File Name` set to "Media Center" (record field `File Name` works).
  H264STREAM_LIBRARY comes from MediaCenter.c's own #define; no project define needed.
  Project file is still named `H264Stream.mcp` in that folder.
- Note: the project folder is `Macintosh HD:Desktop Folder:H264 Classic Project Folder:`, not on the root.
- Copying files: Finder `duplicate` from `share:` gave -110 and left 0-byte files; reading the share file in
  6000-byte chunks with `open for access` and writing them (then Finder set TEXT/CWIE) works
  (`~/b16copy.sh`). Reading 68 KB in one go -> -108 in the agent.
- Error read-back: `~/b16make.sh` (Remove Binaries + Make, prints «class ErrT»/«class ErrS»/file/line).

### Test evidence
- Both builds: 0 errors; 1 linker warning "ignored 'errno' (data) in OTXTILib" (harmless).
- H264 Stream 1.2, hook `H264 Stream URL` = Popeye_forPresident_512kb.mp4: log fresh, starts
  "H264 Stream 1.2", "Baseline H.264, level 1.3, 320x240.", "Done."; QuickTime played 8.04 s;
  decoder log "pictures shown 40", "decode errors survived 0".
- Media Center 0.1, hook query "popeye president": 9 hits, first `Popeye_forPresident | Popeye for President`;
  "1 MP4 candidate(s); 1 server(s)", 302 -> dn801208.us, Baseline 320x240, ref movie
  "Popeye_forPresident_512k 10.mov", "Done.", "Exit." (quit by itself); QuickTime played 8.12 s;
  decoder log pictures shown 40, errors 0.
- Media Center without hook: process running, log (157 bytes, i.e. truncated from 1672) ends "Event loop.".
  Left running (no quit sent).

### Untested
Interactive UI (typing, List clicks/double-click, Play/More buttons, HiliteControl, LUpdate/activate
redraw, Cmd-Q/Esc), More results/page 2, High-profile refusal (Vietnam), the https-redirect paths,
multi-candidate/multi-server fallback, the stale-tail case of the log in H264 Stream with a short run.
