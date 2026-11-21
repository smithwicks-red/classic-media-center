# Classic Media Center 0.5s -- https STREAMING (J5, 20 Nov 2026)

Status: HOST-PROVEN only. Nothing here has been built in CodeWarrior or run on
Mac OS 9 / QuickTime 6. The VM, the device shell and the share were not touched.

## What changed for the user
* An https-only film (archive.org chain 301s to https, e.g. *.ca.archive.org)
  now STREAMS by default: Media Center opens a loopback HTTP server
  (127.0.0.1:8080, else 8081..8089, else an OT-assigned port; opened at the
  first such Play, kept until Quit) and saves the Desktop reference movie
  "<name> (MC).mov" (31-char limit kept: the title part is trimmed to 22) with
  dref `http://127.0.0.1:PORT/a/archive.org/download/ID/FILE`. QuickTime
  Player fetches plain http from Media Center; mc_proxy fetches the same bytes
  over https (HC_Stream / macTLS / BearSSL), redirects followed in the proxy.
* "Download first" ticked, or no listener -> the 0.4 download path, unchanged.
  Plain-http chains -> 0.4 direct streaming, unchanged (no listener opened).
* Stop is enabled while a film streams. Stop = (1) Apple event to QuickTime
  Player "close window <name> saving no" (both "x (MC).mov" and "x (MC)"),
  (2) poll the proxy up to 120 ticks, ending early when QuickTime's connection
  is gone, (3) mcp_cancel_all. Stop pressed during Media Center's own transfer
  cancels that transfer AND then ends streaming (Esc / Cmd-period only cancel).
* Quit while QuickTime has an open connection: status-line + log warning, no
  alert; a second Quit within 10 s (600 ticks) closes the movie as for Stop,
  then quits. Quit with a streaming movie open but idle: closes it, then quits.
  Applies to Cmd-Q, the Quit button and the Quit Apple event (which always
  replies noErr: a script must send quit twice while a film is being read).
* Esc and Cmd-period never quit: they cancel a transfer (CbIdle) or go Back.
* Status line (idle loop, once a second): "Streaming via secure proxy: N KB/s,
  X.Y MB sent, 1 connection." / "... idle, X.Y MB sent. Stop ends streaming."
* G3 hint: Gestalt 'cput' 0x0108 (750) or 0x0120 (750FX) and a film averaging
  > 64 KB/s (size / mvhd duration) -> log + status "G3 HINT: ... tick
  \"Download first\" ..." (no dialog). Log always has "Film average: N KB/s".
* Hook file: new lines `serve N` (keep serving N s, then quit closing the movie
  first; default 600 s) and `stop N` (press Stop N s after the hand-off; a hook
  run quits after any completed Stop). After a streaming hook run the app stays
  in its event loop until then (0.4 quit right after the hook).

## Files (diff: out/b23f-0.4b-to-0.5s.diff)
mediacenter/
* NEW `mc_proxy_ot.c/.h` -- Mac-only OT side (QTSniff/OTProbe code): listener +
  5 pre-opened acceptor endpoints (sync, non-blocking, NO notifier), OTListen/
  OTAccept polling, OTRcv/OTSnd/OTLook -> McpOps down_*, orderly release both
  ways (T_OUTREL slots drained until QuickTime's T_ORDREL, 300-tick limit, then
  OTSndDisconnect), HC_Stream upstream adapter, allow-list archive.org /
  *.archive.org, bind fallback, refuses non-127.x peers if it ever had to bind
  0.0.0.0. Functions: mcpo_open/poll/active/parked/closing/cancel_all/close.
* NEW `mc_stream.c/.h` -- portable C89 policy (no Toolbox): mcs_proxy_url,
  mcs_proxy_opened, mcs_mc_name, mcs_avg_kbs, mcs_poll, mcs_sleep,
  mcs_streaming/serving, mcs_stop, mcs_quit/quit_ready, mcs_hook_timers,
  mcs_status_text/status_tick, mcs_shutdown. The 0.5 skin should call only
  these (plus UpdateStop/DoStopStreaming/DoQuit in MediaCenter.c).
* `mc_fetch.c/.h` -- MCF_MODE_PROXY (2); `mcf_decide(force, probe, proxy_ok)`;
  McfEnv.proxy_url / proxy_opened; TryCand's PROXY branch (falls back to
  DOWNLOAD when proxy_url fails).
* `mc_core.c/.h` -- McHook.serve / .stop parsed by mc_hook_parse.
* `MediaCenter.c` -- QTCloseWindowNamed (the Apple event), CbQtClose, IsG3,
  SetupStreaming, UpdateStop, DoStopStreaming, DoQuit, StreamPass; CbIdle
  polls the proxy; EventLoop sleeps mcs_sleep(moved, 15) (0 / 1 / 15);
  HookRun keeps serving; mcs_shutdown before OSTLS_SaveSeed; banner/UA 0.5s.
* NEW `test05/mc05_unit.c` (policy, scripted clock, stub OT side).
proxy/ (NEW in this tree) `src/mc_proxy.c/.h` = H1's reviewed core + ONE change
(proven by NEW `test/mcp_query_test.c`: 9/7 on the H1 core, 16/0 after):
`/a/` targets ignore `?query` and `#fragment` (same token, not sent upstream;
QuickTime caches by URL). `test/` = H1's harness, run_all.sh pointed at the
merged tls18 (`test/hc_stream.h` = the merged declarations, for mcp_host.c).
tls18/ = J2's tree (https_client.c/.h with HC_Stream merged) plus host-only:
* `host/fake_mac.c`, `host/machdr/ot_fake.h`: a POSIX passive side for the fake
  OT (OTBind with qlen > 0 -> listening socket on 127.0.0.1, kOTAddressBusyErr
  when taken; OTListen/OTAccept/OTUnbind/OTInitInetAddress/TDiscon; accepted
  endpoints close their socket at T_IDLE, abortive close = RST, SO_SNDBUF 32 KB
  so a paused QuickTime exerts back-pressure; OTRcv out of state in T_INREL).
  Client-side (macTLS) behaviour unchanged.
* NEW `host/mc05_host.c`, `build_mc05.sh`, `run_mc05_tests.sh`, `mc05_e2e.py`,
  `syntax_mc05.sh`, `macstub05/` (types for the MediaCenter.c syntax check).

## CodeWarrior project (copy of the 0.4 Media Center project)
1. Replace `https_client.c/.h` with this tree's tls18 copies (J2 merge: HC_Stream).
2. Add files: `mc_proxy.c` (proxy/src), `mc_proxy_ot.c`, `mc_stream.c`
   (mediacenter). Access path: add `proxy:src:` (user path) for mc_proxy.h.
   H264Stream.c stays #included, not a project file.
3. No new library. AE calls (AECreateDesc, AECreateAppleEvent, AECreateList,
   AEPutKeyPtr/Desc, AECoerceDesc, AEPutParamDesc/Ptr, AESend, AEDisposeDesc)
   and Gestalt are InterfaceLib; OTListen/OTAccept/OTUnbind/OTInitInetAddress
   are in OpenTransportLib/OpenTptInternetLib, already linked for macTLS. The
   object specifier is built by hand (AERecord coerced to 'obj '), so
   ObjectSupportLib is NOT needed. Includes: <Gestalt.h>, <AppleEvents.h>.
4. Partition (PPC Project panel / SIZE): minimum 4096 K, preferred 5120 K
   (plan section 3: proxy peak ~770 KB = 4 x 27 KB connection blocks + up to
   6 TLS streams x ~104 KB, on top of 0.4's ~2 MB peak).
5. Prefix `https_prefix.h` as before. Keep "Map newlines to CR" as before:
   mc_proxy.c and mc_proxy_ot.c contain no `\n`/`\r` escapes (wire bytes are
   '\015\012'); mc_stream.c's "\n" are log text, like mc_fetch.c's.
6. Compile mc_proxy.c, mc_proxy_ot.c, mc_stream.c, mc_fetch.c, mc_core.c,
   MediaCenter.c one by one (expect 0 errors; `#pragma unused` is used only in
   MediaCenter.c), then b18make.sh rb.

## The Apple event (for checking by hand)
`tell application "QuickTime Player" to close window "NAME (MC).mov" saving no`
= class 'core', id 'clos', target typeApplSignature 'TVOD', direct object
{want:'cwin', from:null, form:'name', seld:"NAME (MC).mov"} as 'obj ', 'savo'
= enum 'no  ', sent kAENoReply | kAENeverInteract. Log line: `Apple event to
QuickTime Player: close window "..." saving no -> e1 / e2 (without .mov)`
(-600 procNotFound = QuickTime Player not running). Which title form QuickTime
6 uses (with or without ".mov") is UNKNOWN: both are sent.

## Host proofs (all on Linux; logs in out/logs/)
* proxy core vs the merged tls18: unit 24959/0 (ASan+UBSan) and 24959/0
  (qemu-ppc); r1_sm 58/0 (-m32 ASan) and 58/0 (qemu-ppc); MPWNL PASS; c89
  -pedantic 0 warnings; NEW query test 16/0; e2e (R1_PORTSHIFT=4000) 44/0 in
  6 of 6 runs (two full run_all.sh passes); 4 KB-buffer e2e 44/0 in 5 of 6
  runs -- 1 run died inside the harness (its byte-by-byte request in the
  busy-503 case got ECONNRESET: the "503 written then closed unread" race
  R1 #15 already lists; mcp_host uses POSIX sockets downstream, untouched).
* tls18 run_host_tests.sh 30/30. Media Center: run_tests.sh mc_test 266/0,
  mc_test04 273/0 (= 0.4b); run_mc02_tests.sh 26/26 (clips regenerated with the
  BUILD_NOTES_02 ffmpeg recipe); run_mc04_tests.sh 62 passed / 4 failed on BOTH
  0.4b and 0.5s, the same PASS/FAIL lines (the 4 = s5/sim/files wild_bear720/
  wild_download; EXTRADIR regenerated with ffmpeg since the original is gone).
* NEW mc05_unit (policy): 49/0 (C89 -pedantic, ASan+UBSan) and 49/0 qemu-ppc.
* NEW mc05 end to end (`sh tls18/host/run_mc05_tests.sh CLIPDIR`), whole app
  flow, ASan+UBSan+leak check, 40/0 in two runs: https-only item -> PROXY,
  "(MC).mov" name, dref 127.0.0.1; Python "QuickTime" (exact 6.0.2 request)
  fetches the dref while the serve loop polls: sha256 == source; `?t=2`; two
  connections at once; client close mid-body; Range 1000000-; 404 for other
  hosts; Esc does not quit; Quit when idle closes the movie first; Stop with a
  QuickTime that ignores the event (close event logged BEFORE the cancel, cut
  after 1.99 s); Stop with a QuickTime that obeys (no cut); Quit while serving
  (warning, still serving, second Quit -> close event -> cancel -> exit); a
  Quit after 10.8 s warns again; Download first; listener failure -> download;
  port fallback 18080 -> 18081; all 10 ports taken -> OT-assigned; plain http
  stays direct; hook `stop 2` / `serve 3`; untrusted cert refused; `insecure`
  also reaches the proxy's upstream.
* `sh tls18/host/syntax_mc05.sh`: MediaCenter.c (+H264Stream.c) -fsyntax-only
  against host/machdr + macstub05: 0 errors, 0 warnings in 0.5s code (73
  warnings = stub gaps in 0.4 code). mc_proxy_ot.c is not just syntax-checked:
  it is COMPILED (-std=c89 -pedantic, 0 warnings) and RUN by the e2e over the
  fake OT.

## Guest test plan J6 (VM first, then the Mini)
Setup: CW project as above; QuickTime 6.0.2 + decoder 17 kit; a local https
test server reachable from the VM (mc02_server.py on the host side, its test
CA in the guest's anchor list or `insecure` in the hook), or a real
*.ca.archive.org film. Keep every unattended film SMALL (m05-size, <= 2 MB) so
QuickTime has it all within seconds: then no connection is open when Media
Center ends serving, and nothing can raise QuickTime's modal "cannot be
completely downloaded" dialog (it froze the VM this morning).
Unattended (hook file "Media Center Query" + AppleScript):
* H1 https-only small film, hook `<query>` / `file 1` / `serve 60`. Expect
  log: `proxy: LISTENING 127.0.0.1:8080`, `STREAMING "... (MC).mov"`, proxy
  request lines; Desktop file "... (MC).mov"; QuickTime opens it and plays
  (AppleScript: play, delay 20, current time to 70 %, delay 5 -- the seek
  measurement planned this afternoon). At 60 s: `HOOK: serve time is up`,
  `Apple event to QuickTime Player: close window ... -> 0 / 0`; then
  AppleScript `count windows` of QuickTime Player: the movie window must be
  GONE (proves the AE and which title form matched). `STOP: QuickTime Player
  let go of the film` expected (no cut).
* H2 the same film with `stop 30`: same, Stop path.
* H3 `download` line: 0.4 download path (mode 1), no LISTENING line.
* H4 plain-http item: direct streaming as 0.4, no LISTENING line.
* H5 QTSniff (or any app) holding 8080 first: `127.0.0.1:8080 is taken`,
  LISTENING on 8081, dref has 8081, film plays.
* H6 G3 check in the log: `CPU type 0x....` (VM = G4 -> "not a G3").
ATTENDED (someone watches the screen; a QuickTime dialog is possible):
* A1 a 20-40 MB https film, play 30 s, click Stop in Media Center. Expect the
  QuickTime window to close within ~2 s and NO dialog. If QuickTime shows
  "cannot be completely downloaded": click OK, note which AE result the log
  shows, and report (the window title form then needs fixing).
* A2 same film: Cmd-Q in Media Center -> status warning, still playing; Cmd-Q
  again within 10 s -> movie closes, Media Center quits. Then Quit once more
  after 11 s to see the warning again.
* A3 Esc and Cmd-period while playing: nothing quits.
* A4 Media Center in the BACKGROUND (QuickTime in front) for 5 min: watch the
  status KB/s (bring Media Center forward briefly) and for stutter.
* A5 Pismo (J8): a > 64 KB/s film -> `G3 HINT` in log + status.

## NOT proven (needs the guest)
Real OT listener/acceptor behaviour inside Media Center (only QTSniff's copy
of this code ran in the VM); the window title QuickTime 6 uses (AE match);
whether QuickTime closes cleanly on the AE while reading; QuickTime's seek
behaviour through the proxy; background throughput and CPU on G3/G4;
HC_Stream on real macTLS with several streams; memory figures (estimates);
the 4096/5120 K partition; behaviour of a Quit Apple event from the Finder at
Shut Down while serving (Media Center will not quit on the first one).
