# GUEST_FIXES.md -- changes made while building in CodeWarrior Pro 8.3 (session b18)

1. `ostls_classic_shim.h`: added `#include <Files.h>` before `<OpenTransport.h>`.
   Why: with only MacTypes.h included first (prefix), UI 3.4.1
   OpenTransportProviders.h:2333 (`FSSpec modemScript;`, via OpenTptInternet.h)
   gave "declaration syntax error" in every file.

Build result after fix 1: HTTPS Get linked, 0 errors, 2 warnings
(prf.c:44 "possible unwanted ';'", linker "ignored 'errno' (data) in OTXTILib").

## OTProbe (new file otprobe/OTProbe.c, rewrite of codec-src/OTProbe.c)
2. Rewritten so it cannot hang or raise UI: no printf/SIOUX/getchar, log file
   "OTProbe Log" beside the app, sync NON-blocking endpoints, every wait
   bounded (600 ticks), quits by itself. Added part B: loopback listen +
   connect + OTAccept + PING/PONG exchange inside one app.
3. otprobe/OTProbe.c: a synchronous OTConnect returned noErr immediately on
   loopback (already connected), so the following OTRcvConnect gave -3155
   (kOTOutStateErr). Now OTRcvConnect is polled only when OTConnect said
   kOTNoDataErr.
Guest project: copy of mc01 H264Stream.mcp; Remove Files of H264Stream.c /
mc_core.c only worked after empty placeholder files of those names were put
beside the project (CW must find a file to remove it).
