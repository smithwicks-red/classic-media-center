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

## Status: 0.6 (22 November 2026)

![Media Center 0.6, every screen as drawn by the app itself on Mac OS 9](docs/screenshots/mc06-screens-os9.png)

0.6 replaces the plain dialog of 0.4/0.5 with a window in the style of QuickTime
Player 6: brushed metal, Aqua gel buttons, an inset list, a grey-green "LCD" status
display with a progress bar, and Garamond-style headings. Mac OS 9 has no brushed-metal
or Aqua controls, so the app draws all of it itself: a portable C89 painter
(`mc_skin`, `mc_layout`) paints into a 32-bit offscreen GWorld, QuickDraw adds the text,
and only the changed rectangles are copied to the window.

- **Headings** use *MC Garamond*, a condensed EB Garamond bundled in the application's
  resource fork (SIL Open Font License, see `font/`). If Apple Garamond is installed it
  is used instead; the fallbacks are Hoefler Text, then Palatino.
- **The status display** shows search and download progress, the streaming buffer,
  KB/s and the upstream server, and warnings (for example the second-Quit warning)
  without modal alerts.
- **Keys:** Return searches from the field and plays from the list; Enter and Cmd-P
  play; Tab moves between the field and the list; arrows, Page Up/Down and Home/End move
  in the list; Esc and Cmd-. cancel a search or download, or go back, and never quit.
  Quit is Cmd-Q, File > Quit or the round close button.
- Files that this Mac may not play smoothly get an orange "may stutter" mark.
- Below Thousands of colours (or if there is not enough memory for the offscreen image)
  the 0.5 dialog is used.

Tested on a **Mac mini G4** (real hardware) and in a QEMU/UTM Mac OS 9.2.2 VM: search,
Main-profile B-frame films over plain http, and an https-only 306 MB 640x480 film
streamed through the in-app proxy (about 159 MB served at about 390 KB/s on the Mini),
Stop, and Quit while streaming.

Streaming itself is unchanged from 0.5u:

- https-only archive.org films stream through the in-app proxy; plain http films stream
  directly from archive.org to QuickTime.
- Play, pause and seeking **within the part already downloaded** work. QuickTime 6
  never asks for a later part of an http movie on a seek, so you cannot jump past what
  has arrived.

Known issues in 0.6:

- Playing the same film twice leaves the first reference movie on the Desktop; the
  second gets a shortened name, and Stop/Quit then cannot ask QuickTime to close it by
  name, so Media Center cuts the connection instead. Delete old "(MC).mov" files from
  the Desktop for now.
- The KB/s figure can read 0 while a film is still being served.
- In the VM (not on real hardware) QuickTime ignores Stop's request to close the window.

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
| `mediacenter/` | The app: `MediaCenter.c` (Toolbox shell and the skin's Mac layer), `mc_ui` (portable UI controller: keys, clicks, focus, the LCD), `mc_layout` + `mc_skin` (portable widget layout and painter), `mc_demo` (demo scenes and the font test), `mc_core` (archive.org search/metadata, playability verdicts), `mc_fetch` (moov probe, rewrite, download), `mc_stream` (streaming policy, pre-buffer, Stop/Quit), `mc_proxy_ot` + `mc_notify` (Open Transport server, notifier state machine), `H264Stream.c`, `AVCReorder.c`; host tests; build notes per version |
| `proxy/` | `mc_proxy`: the portable HTTP proxy core (request parsing, ranges, upstream parking/adoption), with its unit, state-machine and end-to-end tests |
| `tls18/` | `https_client` (HTTP/1.1 over macTLS or plain Open Transport, incl. `HC_Stream`), the macTLS + BearSSL tree, `HTTPSGet` test app, and the host rig that runs the whole app on Linux against local https servers |
| `font/` | MC Garamond: the bundled heading font (`MC Garamond.rsrc`, `MC Garamond.r` for Rez, the TrueType file), `OFL.txt`, `FONTLOG.txt` |
| `docs/screenshots/` | Screens drawn by the app on Mac OS 9 |
| `guest/` | AppleScript/shell helpers used to send sources into a Mac OS 9 VM and build them |

## Building

CodeWarrior Pro 8.3, classic PPC application, no CarbonLib. Put the sources of
`mediacenter/`, `proxy/src/mc_proxy.c/.h` and `tls18/https_client.c/.h` flat in one
project folder with `tls18/mactls/` beside them, and add `font/MC Garamond.rsrc`
(file type `rsrc`) or `font/MC Garamond.r` to the project so the font's `FOND`/`sfnt`
resources (id 279) end up in the application; libraries: InterfaceLib, MSL C (PPC),
OpenTransportLib, OpenTptInternetLib, OpenTransportAppPPC.o, OpenTptInetPPC.o,
QuickTimeLib. Set the partition to 9216 K preferred / 8192 K minimum. The 0.6 steps are
in `mediacenter/BUILD_NOTES_06.md`, the history in `mediacenter/BUILD_NOTES*.md`.
Tip: close every other project in CodeWarrior before a scripted Make; it acts on the
front project, and it tracks files by alias, so a stale copy elsewhere can be picked up.

Host tests (Linux, gcc, Python 3):

```sh
sh mediacenter/run_tests.sh                       # mc_core
sh mediacenter/test05/run_mc05_unit.sh            # streaming policy
sh mediacenter/test05t/run_test_mcn.sh            # notifier state machine
sh tls18/host/run_mc05_tests.sh CLIPDIR           # whole app end to end (needs ffmpeg-made clips)
sh mediacenter/test06/run_ui_test.sh              # 0.6 UI controller (x86 + qemu-ppc)
sh mediacenter/test06/run_proof.sh                # skin painter: key map, renders, big-endian check
sh run_all_06.sh CLIPDIR OLD_MEDIACENTER_C        # everything above plus the Toolbox syntax gate
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
  contain mistakes. Such mistakes would be much appreciated in their reporting.
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
- MC Garamond (`font/`): SIL Open Font License 1.1. It is a modified (condensed,
  renamed) version of EB Garamond by Georg A. Duffner; see `font/OFL.txt` and
  `font/FONTLOG.txt`. The font may be used and redistributed under the OFL only; it is
  not covered by the MIT licence.

Not affiliated with the Internet Archive. QuickTime and Mac OS are trademarks of Apple.
