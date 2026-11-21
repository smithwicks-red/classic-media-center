# Classic Media Center 0.3 (b20) -- build notes

Status (14 Nov 2026): HOST PROOF ONLY. Not yet compiled by CodeWarrior, never run on
the guest. H264 Stream unchanged (1.2). tls18/ TLS code unchanged (only the host rig).

## What 0.3 changes
* Search (mc_core.c `mc_search_query` / `mc_search_url2`, mc_fetch.c `mcf_search`):
  every movie item (`mediatype:movies`, no `512Kb MPEG4` filter); every word REQUIRED
  (`a AND b`); `"quoted phrases"` kept; plain words lower-cased (and/or/not are words,
  never operators); a word with anything but letters/digits (`c++`, `x-men`, `don't`)
  is sent as a one-word phrase. NO backslash escaping: archive.org answers
  `[BACKEND_ERROR] Invalid or no response from Elasticsearch` to `c\+\+` (seen).
  Order: archive.org advancedsearch has NO relevance order (without sort[] the
  order is index/identifier order, e.g. `2022-12-05_...`, `220411-...`; `sort[]=score
  desc` -> `[UNSUPPORTED_SORT]`; both seen). So page 1 searches `title:(...)` first and,
  if no title has all the words, every field `(...)`; both `sort[]=downloads desc`.
  "More results" keeps the scope page 1 used. Logged: `SEARCH q=... page=N`,
  `SEARCH found=N scope=title|any`.
* Item file list (McMeta.files, 64 rows, ~22 KB more in McMeta = 25.4 KB): every
  .mp4/.m4v/.mov of the item, natural order (1x2 < 1x10, case-blind), file `title`
  or name without folders/extension, format, size in MB (one decimal), hint
  `plays now?` (formats `h.264`, `512Kb MPEG4`) or `needs newer decoder?` (all other
  labels). .avi/.ogv/.mpeg etc. are not listed. The 0.2 ranked candidate list
  (8 entries) is still filled the 0.2 way for the old "play hit 1" path.
* Selecting a file (`mcf_play_file`): same moov fetch + Baseline check as 0.2
  (unchanged rule, unchanged code path, now `TryCand`); the row becomes `PLAYS NOW`
  or `needs newer decoder`; log `MARK n fact:plays-now|fact:needs-newer-decoder`.
  Nothing non-Baseline reaches QuickTime (MPEG-4 Part 2 still plays, as in 0.2:
  QuickTime's own codec).
* Big download: a download (forced by https or by "Download first") of a file over
  32 MB (MCF_BIG_BYTES) stops BEFORE any byte: log
  `CONFIRM needed: 298.7 MB, about 12 min at 430 KB/s`; status "This film is 298 MB:
  about 12 minutes to download (at 430 KB/s). Click Play again to download it."
  Play again on the same row = yes. Hook mode: needs the keyword `confirm`
  (`download` alone is not enough). Not checked: an already complete download in
  "Media Center Downloads" is asked about too (reuse is decided later).
* Hook file `Media Center Query`: parsed by `mc_hook_parse` (portable), run by
  `mcf_hook_run` (portable; the host rig runs the same code). Line 1 = search;
  later lines, case-blind: `download`, `insecure`, `confirm`, `list`, `file N`.
  No list/file -> exactly the 0.2 behaviour (play hit 1). Log lines:
  `HOOK query=... +list +file 2`, `FILES 30 <id>`, then per file
  `FILE <n> <hint:plays-now|hint:needs-newer-decoder|fact:...> mb=<MB> fmt=<format> name=<file> label=<readable>`.

## UI (for the human tester)
One new button, nothing else moves: **Back** at (368,140)-(388,220), between
"More results" and "Stop" (window still 520x400). Flow: type words, Return ->
results list ("N items found"). Select an item, Play (or double-click, Enter,
Cmd-P) -> the SAME list now shows that item's videos, one row each:
`1x02 - Where No Man Has Gone Before   [h.264, 298.7 MB]  plays now?`.
"More results" is disabled while files show. Select a file, Play -> check ->
row re-marked, then stream / download as in 0.2. Back (or Cmd-[) -> the results
again, the opened item selected. Big download: beep + status line text; Play
again on that row downloads it (any other click forgets the question).

## Files changed (all LF here; CR + MacRoman on the guest; all pure ASCII)
mediacenter/: `MediaCenter.c`, `mc_core.c`, `mc_core.h`, `mc_fetch.c`, `mc_fetch.h`,
`mc_test.c`, `run_tests.sh`, NEW `fixtures/jimmy_files.txt` (compact projection of
the real item's 30 .mp4 rows), NEW `fixtures/gen_meta_jimmy.py` (rebuilds a 425-file
metadata JSON of the real shape: 137,720 bytes vs real 139,112; and a ~700 KB one).
tls18/host/: `mc02_host.c` (argv -> hook text -> `mcf_hook_run`; env MC_BIG_KB),
`mc02_server.py` (new q= shape; items JimmyAndTheFederationGang, jimmyhttps, iahigh,
notitle), `run_mc02_tests.sh` (T15-T26; T5 no longer pins a clip byte offset).
Unchanged: H264Stream.c, https_client.c/.h, everything else in tls18/.

## Host results
* `sh mediacenter/run_tests.sh`: 266/266 (was 164) under ASan+UBSan (gcc C89
  -pedantic -Werror); same 266/266 big-endian (zig cc powerpc-linux-musleabihf,
  qemu-ppc).
* `sh tls18/host/run_mc02_tests.sh asan CLIPDIR`: 26/26 (T1-T14 old + T15-T26 new),
  x86-32 ASan+UBSan. `BIG=0 RUNNER=qemu-ppc ... ppc`: 25/25 big-endian.
  zcc.sh = `exec python3 -m ziglang cc -target powerpc-linux-musleabihf "$@"`
  (pip ziglang 0.17.1; plain `powerpc-linux-musl` has no libc in that zig).
* `tls18/host/run_host_tests.sh asan`: 30/30.
* MediaCenter.c: only a stub-header syntax check (gcc -std=c89 -pedantic
  -Wdeclaration-after-statement, fake Toolbox) -- no errors; the real compile is
  CodeWarrior's. New Toolbox calls: LSetSelect, LAutoScroll (List Manager, InterfaceLib).
  New dialog item 12 (Back) appended LAST so items 1-11 keep their numbers.

## Guest build (agent), starting from `Macintosh HD:mc02:`
1. Sanity: `echo 'return (current date) as string' | agent.sh ID 30`.
2. Back up 0.2: Finder `duplicate` the five files MediaCenter.c, mc_core.c, mc_core.h,
   mc_fetch.c, mc_fetch.h of `Macintosh HD:mc02:MediaCenter:` into a new folder
   `Macintosh HD:mc02:src-0.2:` (and the app `Media Center` -> `Media Center 0.2`).
3. Send the five 0.3 files with b19send.sh (it writes CR/MacRoman TEXT/CWIE into
   `Macintosh HD:mc02:MediaCenter:`): put them in ~/b19w/ (or edit the script's
   `$HOME/b19w`), then `b19send.sh ID MediaCenter.c mc_core.c mc_core.h mc_fetch.c mc_fetch.h`.
   No Add/Remove Files: they are already project members.
4. `Compile Files` mc_core.c, mc_fetch.c, MediaCenter.c first (expect 0 messages), then
   `b18make.sh ID rb` (Remove Binaries + Make, ~220 s). Expect the two known b18
   warnings only. Memory partition: keep 3072 K min / 4096 K preferred (McMeta +22 KB).
5. Rename `Media Center Query.off` away; for each test write the hook file (lines
   separated by CR), launch `Macintosh HD:mc02:MediaCenter:Media Center`, poll with
   `b19poll.sh ID` only, save the log with b19savelog.sh.

## Hook tests (expected `Media Center Log` lines)
* **T1** `jimmy and the federation gang` CR `list`:
  `SEARCH q=title:(jimmy AND and AND the AND federation AND gang) AND mediatype:movies page=1`,
  `SEARCH found=1 scope=title`, `FILES 30 JimmyAndTheFederationGang`, 30 lines
  `FILE n hint:plays-now mb=... fmt=h.264 name=1x..`, FILE 1 = `1x01-TheCage.mp4 ...
  label=1x01 - The Cage`, FILE 30 = `1x30-OperationAnnihilate.mp4`; `Done.`; no MARK,
  no QuickTime.
* **T2** same + CR `file 2`: `Trying 1x02-WhereNoManHasGoneBefore.mp4 (h.264, 305914 KB)`,
  the verdict line (Baseline), `MARK 2 fact:plays-now`, then EITHER `Every hop was plain
  http ... (streaming)` + `Done.` (QuickTime Player opens the reference movie; play 15 s,
  640x480) OR, if the node forces https, `CONFIRM needed: 298.7 MB, about 12 min at
  430 KB/s` + `FAILED.` with no file in Media Center Downloads. (Do NOT add `confirm`:
  it would fetch 299 MB.)
* **T3** `vietnam travel guide hanoi` CR `file 1`: FILE 1 =
  `...Ho Chi Minh1.ia.mp4` (`hint:needs-newer-decoder`, fmt=h.264 IA),
  `MARK 1 fact:needs-newer-decoder`, the refusal (High/Main) and `FAILED.`; no QuickTime,
  no download. If hit 1 is another item, use `comologar` CR `file 1` (comologar.ia.mp4).
* **T4** `popeye president` (no keyword lines): `SEARCH found=12 scope=title`,
  `Item: ... (Popeye_forPresident)`, `Trying Popeye_forPresident_512kb.mp4`,
  `Every hop was plain http`, `Done.` -> QuickTime plays as in b19 T1.
