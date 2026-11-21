# Classic Media Center 0.5u -- pre-buffer covers the moov (J5c, 21 Nov 2026)

Status: HOST-PROVEN only (CodeWarrior / Mac OS 9 not touched). Everything in BUILD_NOTES_05T.md
still applies except what is listed here.

## Bug (measured in the VM, archive.org JimmyAndTheFederationGang 1x02, 313,256,504 bytes)
ftyp 32 bytes, moov 1,750,112 bytes at offset 32, so media starts at ~1,750,144. The 0.5t
pre-buffer held bytes 0..524287 = moov only. QuickTime opens the reference movie, then waits
synchronously for the first media bytes; Media Center's event loop is starved: deadlock.

## Fix
- mc_fetch.c: the box walker records the end of the moov (`McfProbe.moov_end`); 0 when an
  mdat came BEFORE the moov (moov at the end). `McfEnv.proxy_url` gets it as `mediaFrom`.
- mc_stream.c `mcs_proxy_url`: pre-buffer = [0, mediaFrom + MCPO_PREBUF) (MCPO_PREBUF =
  512 KB media bytes); mediaFrom 0 -> 512 KB as in 0.5t; clipped to the file size.
  Over MCPO_PREBUF_MAX (4 MB) -> log "index too large to stream ..." and return -1 (DOWNLOAD).
  Logs `proxy: PREBUF N bytes (moov ends at M)`.
- mc_proxy_ot.c `mcpo_prebuf_start(target, want)`: Ptr allocated at `want`; a later film
  needing more -> DisposePtr + NewPtr; a smaller one reuses the Ptr.
- Notifier (mc_notify.c) unchanged: it sends head + any pre-buffer length (tested to 2.3 MB).

## Partition (replaces step 4 of 0.5t)
**9216 K preferred / 8192 K minimum** (0.5s ~4 MB + up to 4 MB pre-buffer Ptr + 61 KB notifier
state). 0.5t's 4608 K / 5632 K is too small for a 4 MB pre-buffer. `guest/build_05u.applescript`
sets these sizes (renamed from build_05t.applescript; send_05t.sh unchanged apart from that name).
If NewPtr fails the log says "raise the partition" and Play downloads instead.

## Files changed vs 0.5t (send these 7 to the guest, flat)
MediaCenter.c mc_fetch.c mc_fetch.h mc_stream.c mc_stream.h mc_proxy_ot.c mc_proxy_ot.h
(+ guest/build_05u.applescript, run on the host side; BUILD_NOTES_05U.md; tests below.)

## Tests (host)
test05t 39/0 (was 36; +3 big pre-buffer 2.3 MB through the notifier), test05 mc05_unit 63/0
(was 54; +9 sizing/cap/clip), e2e 59/0 (was 52; M1-M6 = film with a 1,750,112-byte moov,
C1 = 4,000,000-byte moov -> download), proxy suite unchanged and green. Fixtures:
tls18/host/moovpad.py (pads moov with a 'free' child, shifts stco/co64), mc02_server.py items
`bigmoov`, `hugemoov`. e2e K2 ("serve 3") timing-flaked once under load; green on re-run.
