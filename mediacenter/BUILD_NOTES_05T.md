# Classic Media Center 0.5t -- the proxy answers QuickTime at NOTIFIER time (J5b, 21 Nov 2026)

Status: HOST-PROVEN only. Nothing was built in CodeWarrior or run on Mac OS 9. The VM, the
device shell and the share were not touched. Tree: `b24-mediacenter-0.5t` (diff vs 0.5s:
`b23f-0.5s-to-b24-0.5t.diff`).

## Why
QuickTime Player 6.0.2 opens the "(MC).mov" reference movie SYNCHRONOUSLY and calls no
WaitNextEvent while it waits for `http://127.0.0.1:PORT/...`. 0.5s accepted connections only
from its event loop, so it would deadlock (8 min freeze, then "cannot be completely
downloaded"). QTSniff Q2 proved the fix in the VM: answer the GET at Open Transport notifier
(deferred task) time with the head plus a RAM pre-buffer; QuickTime's open returned ~2 s
later and a 640x480 High film played.

## What changed
1. **Pre-buffer before the hand-off** (`mc_stream.c` Prebuffer, `mc_proxy_ot.c` mcpo_prebuf_*).
   After PROXY mode is chosen and the token/dref exist, but BEFORE the reference movie is
   built, saved or opened, the event loop fetches the first `MCPO_PREBUF` = 512 KB (or the
   whole film if smaller) into ONE Ptr (allocated at the first use, freed at Quit).
   It goes THROUGH mc_proxy: a memory "downstream" (`gMem`) sends the core
   `GET <dref path> / Range: bytes=0-524287`; the core opens the upstream with the usual
   open-ended `Range: bytes=0-` (HC_Stream, redirects followed), never reads past 512 KB, and
   PARKS the TLS upstream at offset 524288 (`park_ticks` raised to 1800 = 30 s for this).
   The total size comes from the core's Content-Range. Status line "Buffering N%" (idle
   step = MediaCenter.c CbIdle via new `McsEnv.idle`); Stop/Cmd-period -> `mcs_proxy_url`
   returns -2 and `mc_fetch` says "Stopped."; failure -> one retry after 0.5 s, then -1 ->
   DOWNLOAD mode (as when the listener fails). The notifier heads (206 for `bytes=0-`,
   200 without Range) are built here with `mcp_build_head` (sprintf is not allowed later).
2. **`mc_proxy_ot.c` in notifier mode** (QTSniffN.c's code and order): listener + 5 acceptors
   opened with `OTAsyncOpenEndpoint` and ONE `NewOTNotifyUPP`; wait for T_OPENCOMPLETE
   (WaitNextEvent with event mask 0, max 10 s); each bound with OTSetSynchronous,
   OTSetNonBlocking, OTBind, OTSetAsynchronous; listener bind fallbacks as 0.5s; armed inside
   OTEnterNotifier(listener). Notifier: T_LISTEN -> ListenPump (OTListen until no data,
   OTAccept onto a FREE slot, one in flight); T_ACCEPTCOMPLETE -> pump again;
   T_PASSCON/T_DATA -> OTRcv + `mcp_req_feed`; T_GODATA -> continue; T_ORDREL /
   T_DISCONNECT -> OTRcvOrderlyDisconnect / OTRcvDisconnect, flags.
   **New `mc_notify.c/.h`** (portable C89, no OT types, modelled on qs_notify.c): per-slot
   state machine OFF/FREE/ACCEPTING/REQ/SEND/READY/LOOP. A GET whose target equals the
   pre-buffered dref path (query/fragment ignored) with no Range or `bytes=0-` gets the
   prepared head + the pre-buffer (32 KB per OTSnd, 64 sends per call then an event-loop
   kick, partial sends, flow control -> T_GODATA). Anything else (other target, other
   Range, HEAD, bad request) is PASSED: the event loop replays the exact bytes received
   (raw copy; the parser rewrites its own buffer) into `mcp_accept` = the 0.5s path.
   **Request parser at notifier time:** mc_proxy's own `mcp_req_feed` (+ `mcp_parse_range`):
   CHECKED allocation-free (memcpy + byte scanning, no sprintf, no ops callbacks) -- the
   same call qs_notify.c makes at deferred time. `mcp_build_head` is NOT used there.
   **Hand-off:** READY slots are taken in the event loop (`mcn_take` inside
   OTEnterNotifier) and adopted with the NEW core API
   `mcp_adopt_downstream(srv, down, req, total, sent)`: head already sent, body continues at
   `sent`; it adopts the parked pre-buffer upstream (exact offset, skip 0, no new TLS
   handshake) or opens `Range: bytes=524288-`.
   **Event loop OT calls** (D_Read/D_Write/D_Close, PollClosing, TakeReady, Kick, HardClose,
   ListenPump re-pump) are all bracketed by OTEnterNotifier/OTLeaveNotifier. A closed slot
   waits for T_IDLE; an abortive close that is not idle after 2 s takes the slot out of use
   (deviation: QTSniffN reopened it asynchronously). A SEND slot whose peer half-closed and
   stopped reading for 5 s is disconnected. Notifier log lines go to a 256-record RAM ring
   (`proxy: [notifier t=TICK] nID slotK ACCEPT|REQ|SERVE|PASS|SENT|FIN|DISC|ERR|FLOW|NOTE`)
   flushed by `mcpo_poll`. Stop/Quit semantics of 0.5s are unchanged; `mcpo_active` now also
   counts notifier-owned slots, `mcpo_cancel_all` also cuts them.
3. **FlushVol after every log write** (H264Stream.c Say: `FlushVol(NULL, gLogVRef)`).
4. Banner / User-Agent 0.5t; MediaCenter.c sets `m.idle = CbIdle`.

## Host proofs (logs in out/logs/)
| suite | result |
|---|---|
| mc_proxy unit (ASan/UBSan) | 24959/0 |
| r1_sm | 58/0 |
| query test | 16/0, MPWNL PASS |
| proxy e2e (R1_PORTSHIFT=4000) | 44/0 x4 (run_all.sh) |
| mc05_unit (C89 -pedantic, ASan+UBSan; and qemu-ppc) | 54/0 (49 old + 5 new pre-buffer policy) |
| NEW test_mcn (fake notifier + real mc_proxy core, ASan/UBSan -O0 and -O2) | 36/0 |
| mc05 whole-app e2e (ASan+UBSan+leaks) | 52/0 in two consecutive runs (40 old + 12 new) |
| tls18 run_host_tests.sh | 30/30 |
| mc_test / mc_test04 | 266/0, 273/0 |
| mc_notify.c `-std=c89 -pedantic -Wall -Wextra -Werror` | clean |
| mc_proxy_ot.c c89 -pedantic vs fake OT headers | 0 warnings; vs Q2 UH-3.4 stubs: 0 errors from new code (only OTUnbind / kOTAddressBusyErr missing from that stub, used since 0.5s) |
| syntax_mc05.sh (MediaCenter.c + H264Stream.c) | 0 errors, 0 warnings in 0.5s/0.5t code (73 old stub-gap warnings, as 0.5s) |

test_mcn: QuickTime's GET split over 3 T_DATA; OTSnd taking 1 / rest / 0 bytes; T_GODATA
resumes; head + pre-buffer byte-exact; READY -> take -> `mcp_adopt_downstream` into the real
core with a fake 206 upstream: wire == head + film[0..300000) exactly, one upstream from
65536; FIN mid-pre-buffer (keeps sending, fin noted); FIN before the request end (replay 20
bytes); DISC mid-send; send error; PASS for another film / `bytes=1000-` / HEAD / near-miss
target / bad request with replay == bytes received; no Range -> 200 head; pre-buffer off ->
PASS; the 64-send guard + kick; film < pre-buffer; ring overflow counted; adopt refuses an
unknown token.

**The starvation simulation (e2e S/T).** In the fake OT, notifiers run only from
`FakeOT_Poll` (= OT's deferred tasks); the fake gained listener T_LISTEN, async OTAccept
(T_PASSCON to the acceptor, T_ACCEPTCOMPLETE to the listener), OTEnterNotifier (no notifier
runs while depth > 0), OTGetTimeStamp/OTElapsedMilliseconds, and a T_DISCONNECT event after
a failed OTSnd. With `MC_STARVE_SECS=N`, the host's open_ref callback (where MediaCenter.c
asks QuickTime Player to open the movie) prints OPEN_REF and then calls
`FakeOT_Starve(N)`: for N seconds ONLY FakeOT_Poll runs -- no mcs_poll, no mcp_poll, no
event-loop code -- exactly what QuickTime's synchronous open does to Media Center. The
Python "QuickTime" connects on OPEN_REF with QuickTime 6.0.2's request. Proven:
S1 "Buffering" and `PREBUF 524288 of 1950970` come BEFORE the movie is written; S2 the head
and all 524288 pre-buffer bytes arrived ~0.0 s after connecting, while the event loop was
starved (starve window 6 s); S3 after the starve the whole body is right (206, sha256 ==
source); S4 notifier ACCEPT/SERVE then `answered at notifier time ... continuing at 524288`;
S5 `adopted the parked upstream at 524288 (skip 0)` = no second TLS handshake; S6 clean Quit.
T1-T6: the same with a 100000-byte pre-buffer (adoption mid-film). In 0.5s the same
starve would have produced no byte at all (accept only happened in mcpo_poll).

Host-rig note: in ~1 of 16 runs the FIRST pre-buffer upstream, opened ~3 ms after the moov
fetch, got "upstream closed before its head" from the Python TLS server; the retry after
0.5 s always worked (`trying once more` appears once per e2e run). Cause not pinned down.

## CodeWarrior steps (guest `Macintosh HD:mc04:MediaCenter:`, the 0.4b project)
1. `AGENT=... guest/send_05t.sh m5s <unpacked tree>`: 16 files, FLAT into the project folder
   (CR, MacRoman, TEXT/CWIE): MediaCenter.c H264Stream.c mc_core.c/.h mc_fetch.c/.h
   mc_stream.c/.h mc_proxy_ot.c/.h mc_notify.c/.h mc_proxy.c/.h https_client.c/.h. Being
   flat, mc_proxy.h needs no `proxy:src:` access path.
2. `agent.sh m5b 900 < guest/build_05u.applescript`: opens `MediaCenter.mcp` (else the first
   .mcp in the folder) by full path, Remove Files `https_client.c` then Add Files
   https_client.c mc_proxy.c mc_proxy_ot.c mc_stream.c mc_notify.c from the folder, Remove
   Binaries, Make, returns `type | file | line | text` per message, sets (0.5u: see BUILD_NOTES_05U.md) the SIZE of every
   APPL in the folder to 4608 K min / 5632 K preferred. Never touches QuickTime Player.
   If `HC_StreamOpen` is undeclared/undefined, the project still sees an OLD https_client.h
   elsewhere in its access paths: remove that path or that file.
3. Libraries: as 0.5s. If the link reports OTEnterNotifier / OTLeaveNotifier /
   OTGetTimeStamp / OTElapsedMilliseconds undefined, add `OTUtilityLib` (or
   `OTClientUtilLib`) as Q2 noted. OTAsyncOpenEndpoint/NewOTNotifyUPP are already linked
   for macTLS. Prefix, "Map newlines to CR": as before (mc_notify.c uses '\015'/'\012' only).
4. (SUPERSEDED by 0.5u: 9216 K preferred / 8192 K minimum.) Partition 4608 K / 5632 K (0.5s + 512 KB pre-buffer + 61 KB notifier state).

## Attended test (about 2 minutes, a tester at the VM or the Mini)
1. Launch Media Center 0.5t. Log: `proxy:` lines appear only at the first https Play.
2. Search **star trek** (the ~300 MB episodes live on `*.ca.archive.org`, https only), or any
   item whose log says "A server on the way insists on https". `Popeye for President` is a
   plain-http CONTROL (direct streaming, no Buffering). Untick "Download first".
3. Play. Expect `Buffering 0% ... 100%` in the status line (a few seconds), then the
   QuickTime window within ~2-5 s (no freeze). Log: `proxy: LISTENING 127.0.0.1:8080
   (notifier mode ...)`, `PREBUF 524288 of N bytes in RAM`, `[notifier t=..] n1 slot0
   ACCEPT / REQ / SERVE a=206 b=524288 / SENT`, `answered at notifier time`, `adopted the
   parked upstream at 524288 (skip 0)` (or a fresh `upstream ... (524288-)` if QuickTime
   took > 30 s).
4. Play 20 s, Pause, Play, drag the slider back and forward within the downloaded (grey)
   part. Expect no new connection (QuickTime makes one), smooth seeks.
5. Stop in Media Center: QuickTime's window closes within ~2 s, no dialog. Then Quit.
6. Report the log (it survives a hard reset now: FlushVol per line). If QuickTime still
   freezes: do notifier lines appear at all (no ACCEPT = OT is not running deferred tasks
   while QuickTime spins), and does SENT appear (QuickTime not reading)?

## NOT proven (needs the guest)
CW 8.3 compile/link of mc_notify.c and the new mc_proxy_ot.c; real OT behaviour of the
async endpoints in Media Center (only QTSniffN's copy ran); T_PASSCON vs T_ACCEPTCOMPLETE
order; that OTSnd at notifier time + event-loop OTSnd under OTEnterNotifier interleave as on
the host; that QuickTime 6 returns from its open with 512 KB; parking a real macTLS
upstream for seconds while QuickTime opens; Finder SIZE units; T_IDLE after abortive close in
async mode (out-of-use fallback untested on OT).

## Notifier-time OT calls (risks)
OTListen, OTAccept (async), OTLook (listener), OTRcvDisconnect (listener + acceptors),
OTSndDisconnect (listener: reject a call), OTRcv, OTSnd, OTRcvOrderlyDisconnect,
OTElapsedMilliseconds. Nothing from the Memory/File Manager, QuickDraw, no sprintf, no FPU.
Deviation from QTSniffN: no async reopen of a stuck endpoint; the pre-buffer is fetched
through mc_proxy (not read from a file); only `bytes=0-`/no-Range GETs are answered at
notifier time (QuickTime's measured request); the status KB/s line counts the pre-buffer
bytes too.
