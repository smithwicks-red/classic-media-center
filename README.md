# Classic Media Center

Browse the Internet Archive from a PowerPC Mac running **Mac OS 9**, and watch its
films in **QuickTime Player 6**, including films that archive.org only serves over
**https**.

Classic Media Center is a small classic (non-Carbon) application written in C89 and
built with CodeWarrior Pro 8.3. It searches archive.org, lists an item's video files,
checks whether this Mac can play each one, and hands the film to QuickTime Player:

- **Plain http films stream directly**: Media Center fetches the movie's index (`moov`),
  rewrites it for QuickTime 6, and writes a small *reference movie* on the Desktop whose
  data points at archive.org.
- **https-only films stream through an in-app proxy.** QuickTime 6 cannot speak modern
  TLS, so Media Center runs a tiny HTTP server on `127.0.0.1:8080` and fetches the film
  over TLS 1.2/1.3 with **macTLS** (BearSSL). QuickTime reads plain http from Media
  Center; Media Center reads https from archive.org.
- **Download first** (a checkbox) fetches the whole film to disk instead.
- Main/High profile films with B-frames are rewritten so that QuickTime 6, which cannot
  reorder pictures, shows every picture on time (`AVCReorder.c`).

It needs the H.264 decoder for QuickTime 6 from
[smithwicks-red/h264-classic](https://github.com/smithwicks-red/h264-classic):
**H264 Decoder 17d + AVCDecLib** for Main/High profile (the older v15 decoder plays
Baseline only, and Media Center tells you so).

## Status: 0.5u (21 November 2026)

- Streams https-only archive.org films (tested: 299-314 MB, 640x480, from
  `*.ca.archive.org` and `*.us.archive.org`) on a **Mac mini G4** (real hardware) and
  in a QEMU/UTM Mac OS 9.2.2 VM. On the Mini macTLS fetched about 310 KB/s; the films
  need about 101 KB/s.
- Play, pause and seeking **within the part already downloaded** work. QuickTime 6
  never asks for a later part of an http movie on a seek, so you cannot jump past what
  has arrived.
- The plain dialog UI of 0.4 is still in use; a new skin is planned.

## Requirements and settings

- A PowerPC Mac with Mac OS 9.2.2, QuickTime 6.0.x, Open Transport.
- **640x480 needs a G4** (AltiVec, through AVCDecLib). G3: untested.
- **Memory (important):**
  - Media Center: Get Info > Memory, Preferred **9216 K**, Minimum **8192 K**
    (it holds up to 4 MB of the film in RAM before handing it to QuickTime).
  - QuickTime Player: Preferred about **32768 K** for 640x480 films. Its standard
    partition runs out and can freeze the whole Mac.
- Media Center must keep running while a streamed film plays (the film comes through
  it). Quitting while a film streams asks for a second Quit within 10 seconds.

## How the https streaming works (and why it is built this way)

QuickTime 6 opens a reference movie whose data is an http URL **synchronously**: inside
the open call it waits for the connection without letting any other application run.
A server that answers only from its event loop is then starved, and both wait (we saw
an 8-minute freeze, then "cannot be completely downloaded"). So Media Center:

1. fetches the film's start (up to the end of the index plus 512 KB, at most 4 MB) into
   RAM **before** it writes and opens the reference movie ("Buffering N%");
2. answers QuickTime's request from an **Open Transport notifier** (deferred-task time),
   which runs even while QuickTime holds the processor: accept, read the GET, send the
   206 head and the buffered bytes;
3. once QuickTime's open returns, continues the body from its event loop on the same
   (parked) TLS connection.

If a connection drops, QuickTime 6 reconnects with a `Range:` request and the proxy
resumes the upstream from that offset. Details: `mediacenter/BUILD_NOTES_05T.md` and
`BUILD_NOTES_05U.md`.

## Layout

| Path | What |
|---|---|
| `mediacenter/` | The app: `MediaCenter.c` (Toolbox shell), `mc_core` (archive.org search/metadata, playability verdicts), `mc_fetch` (moov probe, rewrite, download), `mc_stream` (streaming policy, pre-buffer, Stop/Quit), `mc_proxy_ot` + `mc_notify` (Open Transport server, notifier state machine), `H264Stream.c`, `AVCReorder.c`; host tests; build notes per version |
| `proxy/` | `mc_proxy`: the portable HTTP proxy core (request parsing, ranges, upstream parking/adoption), with its unit, state-machine and end-to-end tests |
| `tls18/` | `https_client` (HTTP/1.1 over macTLS or plain Open Transport, incl. `HC_Stream`), the macTLS + BearSSL tree, `HTTPSGet` test app, and the host rig that runs the whole app on Linux against local https servers |
| `guest/` | AppleScript/shell helpers used to send sources into a Mac OS 9 VM and build them |

## Building

CodeWarrior Pro 8.3, classic PPC application, no CarbonLib. Put the sources of
`mediacenter/`, `proxy/src/mc_proxy.c/.h` and `tls18/https_client.c/.h` flat in one
project folder with `tls18/mactls/` beside them; libraries: InterfaceLib, MSL C (PPC),
OpenTransportLib, OpenTptInternetLib, OpenTransportAppPPC.o, OpenTptInetPPC.o,
QuickTimeLib. The step-by-step history is in `mediacenter/BUILD_NOTES*.md`.
Tip: close every other project in CodeWarrior before a scripted Make; it acts on the
front project, and it tracks files by alias, so a stale copy elsewhere can be picked up.

Host tests (Linux, gcc, Python 3):

```sh
sh mediacenter/run_tests.sh                       # mc_core
sh mediacenter/test05/run_mc05_unit.sh            # streaming policy
sh mediacenter/test05t/run_test_mcn.sh            # notifier state machine
sh tls18/host/run_mc05_tests.sh CLIPDIR           # whole app end to end (needs ffmpeg-made clips)
```

## AI use notice

**Most of this project was written by an AI.** The source code, tests, build scripts and
documentation in this repository were written by Claude (Anthropic's AI models), working
in agentic coding sessions. smithwicks-red directed the work: he set
the goals and made the design decisions, ran every test on real Mac OS 9 hardware (a Mac
mini G4) and in his Mac OS 9.2.2 virtual machine, reported what happened, and decided
what to publish.

What that means for you:

- The code was checked by automated tests on Linux (including AddressSanitizer runs and
  big-endian PowerPC emulation) and by hands-on testing on real hardware, but it has not
  had a line-by-line human review. Treat it as you would any young hobby project: it may
  contain mistakes.
- The bundled third-party code (macTLS, BearSSL, and the TLS 1.3 code adapted from
  Certainly) comes from its own upstream authors under the licences listed below; see
  `tls18/mactls/NOTICES.txt` and `tls18/PATCHES.md` for what was changed here.
- The project's working notes, including dead ends and mistakes, are kept in the
  [h264-classic](https://github.com/smithwicks-red/h264-classic) repository's `docs/`.

Bug reports and corrections are very welcome.

## Licences

- Media Center, mc_proxy, https_client and the host rigs: MIT, see [`LICENSE`](LICENSE).
- macTLS (`tls18/mactls/os9/`): MIT, Copyright (c) 2026 Patrick Britton
  (https://github.com/mplsllc/macTLS). TLS 1.3 code adapted from Certainly: MIT,
  Copyright (c) 2026 Matt Baker. BearSSL (`tls18/mactls/bearssl/`): MIT, Copyright (c)
  2016 Thomas Pornin. Trust anchors derived from the Mozilla CA bundle. See
  `tls18/mactls/NOTICES.txt`.
- `H264Stream.c` and `AVCReorder.c` come from the h264-classic project (MIT).

Not affiliated with the Internet Archive. QuickTime and Mac OS are trademarks of Apple.
