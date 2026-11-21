# tls18 -- HTTPS for classic Mac OS 9 apps (macTLS + BearSSL), build notes

Status: HOST-PROVEN ONLY. Nothing here has been compiled by CodeWarrior yet.
Host proof: the REAL vendored glue + all 256 BearSSL files + our code compiled
against fake UI 3.4.1 headers with `-D__MWERKS__` (so the classic code paths
are the ones tested), driven over BSD sockets by a fake Open Transport:
31/31 tests pass (x86-32), 30/30 under ASan+UBSan, 30/30 as a **32-bit
big-endian PowerPC** binary under qemu-ppc (zig cc, with sha2big.c's CW8-only
SHA-384 path enabled). See `host/run_host_tests.sh`.

## Files (tree layout)

```
tls18/
  https_prefix.h          CodeWarrior prefix file (plain text, NOT precompiled)
  ostls_classic_shim.h    Carbon "InContext" OT calls -> classic; <Threads.h>
  https_client.h/.c       our HTTP/1.1 GET client (strict C89)
  HTTPSGet.c              the test app (strict C89)
  cw_project_files.txt    the 269 .c files for the project, one per line
  bearssl_files.txt       the 256 BearSSL .c files (relative to mactls/)
  PATCHES.md, patches/    what differs from upstream macTLS 36596d3
  mactls/                 vendored macTLS subset (licences + NOTICES.txt)
    os9/                  11 glue .c + headers
    bearssl/inc, bearssl/src/...
  host/                   HOST ONLY -- never copy to the guest (it contains
                          fake Types.h, OpenTransport.h ... that would shadow
                          the real Universal Interfaces)
```

## CodeWarrior project "HTTPS Get" -- 269 source files

Exact list: `cw_project_files.txt` (paths relative to `tls18/`):

* 256 BearSSL files: `mactls/bearssl/src/**` per `bearssl_files.txt`
  (aead 3, codec 17, ec ~20, hash 9, int 40+, kdf, mac, rand, rsa, ssl, symcipher, x509).
* 11 glue files in `mactls/os9/`: `ostls_async.c ostls_log.c
  ostls_b3_anchors.c ostls_time.c ostls_entropy.c ostls_tls13_handshake.c
  ostls_tls13_record.c ostls_tls13_keysched.c ostls_ticket_cache.c
  ostls_tls13_selftest.c ostls_noverify_x509.c`.
* ours: `https_client.c`, `HTTPSGet.c`.
* NOT included: `ssl_engine_cw8.c` (identical duplicate of
  bearssl/src/ssl/ssl_engine.c), `ostls_http.c`, `ostls_fetch.c`, b1/b2/b3
  handshake probes, `ostls_provider.c`, `ostls_ot_stubs.c`.
* All file names are <= 31 characters and unique project-wide (checked).
* Suggest CodeWarrior groups "BearSSL", "macTLS", "App" so the 256 do not
  bury the two files you will edit.

Recommended creation route (what worked in session b16): Finder-copy the
`H264Stream` project folder (C Console Classic stationery, classic PPC, NOT
Carbon), `Remove Files {"H264Stream.c"}`, then `Add Files` the 269 files
(by AppleScript, in batches), set PPC Project "File Name" to `HTTPS Get`.

### Settings

* **C/C++ Language -> Prefix File: `https_prefix.h`** (REQUIRED: it makes
  `inline` empty for C89, pins BR_* config, and maps the Carbon OT calls).
  It is a text header; the old "illegal precompiled header version" problem
  was with a precompiled prefix and should not apply. If it does, as a
  fallback put `#include "https_prefix.h"` at the top of the 11 glue files
  and our 2 files, and accept that BearSSL then compiles without it
  (`inline` would be the only failure -> then enable C99 extensions).
* ANSI Strict OFF. Require Prototypes OFF (either works for our files).
  "Enable C99 extensions" ON is fine (macTLS has been built both ways
  upstream); `long long` must be available (CW8 default `#pragma longlong
  on`) because BearSSL uses `uint64_t` (with BR_64=0 only in a few places).
  `<stdint.h>` must come from MSL (CW8 MSL has it; macTLS relies on it).
* "Map newlines to CR": irrelevant for our files (wire and log text use
  `\015\012` octal escapes).
* Access paths, USER, each NON-recursive (host/ must never be found):
  `{Project}` (= tls18), `tls18:mactls:os9:`, `tls18:mactls:bearssl:inc:`,
  `tls18:mactls:bearssl:src:`. System paths: as the stationery
  (`{Compiler}MacOS Support`, recursive). "Always search user paths" ON
  (BearSSL's `#include "inner.h"` is found via `bearssl:src:`).
* Libraries (classic, no CarbonLib): as H264Stream.mcp -- `OpenTransportLib`,
  `OpenTptInternetLib` (stubs), `OpenTransportAppPPC.o`, `OpenTptInetPPC.o`
  (static glue), the stationery's `InterfaceLib`, `MSL C`, `MathLib`
  (MSL runtime) -- PLUS **`ThreadsLib`** (stub library in `MacOS Support:
  Libraries:Stubs`) for `YieldToAnyThread`, which macTLS calls from its
  notifier on kOTSyncIdleEvent. (If ThreadsLib is not found, weak-link it or
  `#define YieldToAnyThread() 0` at the end of `https_prefix.h` -- the event
  never fires on our async/non-blocking endpoints.)
* Memory (heap, all NewPtr): per transfer ~123 KB peak =
  `OSTLSConnection` 67.3 KB (BearSSL bidirectional I/O buffer 33 KB + ring
  buffers) + TLS 1.3 context 35.8 KB (kept until Dispose) + our `HC_Ctx`
  19.4 KB; plus the URL file buffer 16 KB and 1 KB per fetch. Globals:
  trust anchors ~64 KB code/const + ~4 KB, session/ticket caches small.
  Code: BearSSL+glue measured ~410 KB text for the whole object set on host
  (x86) / ~450 KB clang-PPC; the linker keeps ~95 of the 256 BearSSL objects
  (host archive link of the whole test program: 285 KB text) -- expect
  ~350-450 KB on CW8. **SIZE: preferred 3072 KB, minimum 2048 KB** (generous;
  trim later). Downloads stream to disk through the callback: a 20 MB body
  ran in constant memory on host (max RSS +28 KB vs. an 81-byte GET).
* Stack: worst TLS path (RSA-2048 verify, P-384/SHA-384 chain, TLS 1.3)
  ran with <= 8 KB of stack above process baseline on x86-32; largest
  frames: br_rsa_i31_public 2.3 KB, x509/keysched ~1 KB, OSTLS_Pump 0.5 KB.
  PPC frames are larger; the OS 9 default for PPC apps should suffice. If a
  handshake crashes in RSA/EC code, enlarge the stack before MaxApplZone:
  `SetApplLimit(GetApplLimit() - 65536L);` (UNVERIFIED need).

## Guest test steps

1. Copy `tls18/` WITHOUT `host/` to the guest (e.g. `Macintosh HD:mc01:
   HTTPSGet:`), converting `.c/.h` LF -> CR, type TEXT creator CWIE (the b16
   chunked copy script works). Non-ASCII bytes exist only inside comments
   (UTF-8 dashes/arrows in macTLS); `iconv -f UTF-8 -t MACINTOSH//TRANSLIT`
   or leaving them is harmless.
2. Build as above. Expect 0 errors; possibly warnings about unused static
   functions (from `inline` -> `static`), multichar constants.
3. Make sure the guest's clock is right (TLS refuses a pre-2000 clock and
   certificates need a current date).
4. First run WITHOUT a URL file: default URLs (archive.org JSON search,
   example.com). Then a file `HTTPS Get URL` beside the app, e.g.
   ```
   https://example.com/
   https://archive.org/advancedsearch.php?q=popeye&fl%5B%5D=identifier&rows=3&output=json
   http://example.com/
   https://expired.badssl.com/
   insecure https://self-signed.badssl.com/
   https://wrong.host.badssl.com/
   range 0-99 https://archive.org/download/Popeye_forPresident/Popeye_forPresident_512kb.mp4
   save Popeye.mp4 https://archive.org/download/Popeye_forPresident/Popeye_forPresident_512kb.mp4
   tls12
   https://example.com/
   ```
   (archive.org/download 302s to a storage node: exercises redirects.)
   `debuglog` on its own line additionally writes macTLS's trace to
   `MacTLSTest.log` in the Desktop Folder.
5. Read `HTTPS Get Log` (TEXT, CR line ends) beside the app.
   Side effects: macTLS creates `macTLS Entropy Seed` in the Preferences
   folder on the first TLS connection (and rewrites it at exit).

### What a good log looks like (host run, abridged; `host/sample_HTTPSGet_Log.txt`)

```
HTTPS Get 0.1 (tls18) -- macTLS (BearSSL) over classic Open Transport
TickCount at start: 104886
Mac clock: local secs since 1904 = 3877454886, gmtDelta = 0 s
Mac clock as UTC: 2026-11-13 22:48:06  (BearSSL days=740298) OK

Self-tests:
  SHA-384 KAT: PASS (0)
  TLS 1.3 key schedule/record self-test: PASS (0 failures)
  entropy self-test: PASS (0), fingerprint 9F88EAD9 (must differ every launch)

[1] GET https://example.com/
  result: 0 (ok)
  HTTP status 200, redirects 0, final URL https://example.com/
  TLS 1.3, cipher suite 0x1303 TLS_CHACHA20_POLY1305_SHA256
  ticks: connect 9, handshake 95, first byte 110, hop total 112, all 112
  body: 1256 bytes, Content-Length 1256, type "text/html"
  idle callbacks: ...
  first 200 bytes: <!doctype html>\n<html>...

[5] GET https://self-signed.badssl.com/   (INSECURE: no certificate check)
  result: 0 (ok) ...
  INSECURE: certificate NOT VALID (BearSSL would have said 62)

[6] GET https://wrong.host.badssl.com/
  result: 5 (certificate rejected)
  errors: macTLS/OT 2012, OT 0, BearSSL 56
```
(guest tick counts will be much larger: a G3/G4 handshake is seconds.)
BearSSL errors to expect: 56 BAD_SERVER_NAME, 62 NOT_TRUSTED, 54 EXPIRED.
SHA-384 KAT must PASS on the guest: it is the CW8 path (fixes411) and it
also passes on big-endian qemu-ppc here.

## Our API in one paragraph

`HC_Get(&req, &res)`: `req.url`, `req.insecure`, `req.use_range/range_start/
range_end`, `req.user_agent`, `req.timeout_ticks` (no-progress, default
1800), `req.max_redirects` (0 = 5, -1 = none), `req.body(ref, p, n)` (return
nonzero to abort), `req.idle(ref, busy)` called between every pump step
(call WaitNextEvent there; return nonzero to cancel). Result: `err`
(HC_OK / URL / NOMEM / CONNECT (DNS or TCP) / HANDSHAKE / CERT (+br_err) /
CLOCK / TIMEOUT / HTTP_STATUS / PROTOCOL / TRUNCATED / REDIRECTS / ABORTED /
IO / TLS), status, TLS version + suite, timings (connect, handshake, first
byte, total), Content-Length/chunked, bytes, final URL, Content-Range.
Requests are `GET`, HTTP/1.1, `Host`, `User-Agent`, `Accept`,
`Accept-Encoding: identity`, optional `Range`, `Connection: close`.
Bodies: Content-Length, chunked (extensions, trailers), or until close. Only
2xx bodies are delivered to the callback. Plain `http://` uses our own async
OT endpoint (same pattern as macTLS's). Location: absolute,
scheme-relative, absolute-path, relative, query-only (dot segments are
passed through unnormalised).

## Toolbox / OT calls I am not sure of under UI 3.4.1 (check first)

1. `OTAsyncOpenEndpoint(cfg, 0, &info, upp, ctx)` -- classic 5-argument form,
   via the shim macro (macTLS) and directly (https_client.c plain path).
2. `NewOTNotifyUPP` + `static pascal void notifier(void*, OTEventCode,
   OTResult, void*)`. If 3.4.1 declares OTNotifyProcPtr as C (not pascal)
   and CW complains, drop `pascal` in https_client.c (macTLS's own notifier
   has the same shape and compiled under CW8 Carbon headers).
3. `OTClientContextPtr` type in classic OpenTransport.h (for
   `g_ostls_ot_context`, defined in https_client.c). Fallback typedef in the
   shim comment.
4. `OTInitDNSAddress(DNSAddress*, char*)` returns OTByteCount; we store it in
   `TNetbuf.len`.
5. `OTRcvDisconnect(ep, NULL)`, `OTSndDisconnect(ep, NULL)`,
   `OTRcvOrderlyDisconnect`, `OTLook` -- standard, but our plain-http EOF
   path depends on OTRcv returning kOTLookErr + OTLook == T_ORDREL.
6. `YieldToAnyThread` / ThreadsLib (see Libraries).
7. `AEProcessAppleEvent` for kHighLevelEvent from the idle callback, and
   `WaitNextEvent(everyEvent, &ev, 0 or 1, NULL)` with no windows.
8. `#pragma unused(...)` (CW-only; gcc warns, harmless).
9. HTTPSGet uses only File Manager calls already proven in H264Stream.c
   (GetProcessInformation, FSMakeFSSpec, FSpCreate, FSpOpenDF, SetEOF,
   FSRead/FSWrite/FSClose); FSWrite instead of MSL fopen for the log.
10. Real OT may deliver T_DATA/T_ORDREL differently from the fake (e.g.
    T_ORDREL while data is still queued). Both https_client and macTLS read
    until OTRcv says no more, then honour the look event; unverified on OT.

## Known behaviours / limits (upstream macTLS, kept)

* TLS 1.3 never offers TLS_AES_256_GCM_SHA384 (0x1302) by design (offers
  CHACHA20 0x1303, AES128 0x1301). A 1.3 server that ONLY allows 0x1302
  fails (no 1.2 fallback on that path). Real sites all allow 0x1301.
* Against a 1.2-only server the 1.3 attempt is abandoned and macTLS
  RECONNECTS (second TCP connection + 1.2 handshake). Costs time on a G3;
  HTTPSGet's `tls12` line avoids it.
* Pump reports any T_DISCONNECT as kOSTLSAsync_OTSndFail (2008), even a
  refused connect or DNS failure; https_client.c classifies by phase
  (before TCP connect -> HC_ERR_CONNECT).
* Resumption caches are process-global (good: repeat GETs to one host are
  cheaper); insecure connections never use or feed them (PATCH 1).
* `OSTLSDiagnostics.ot_err` reads 0x7FFFFFFF on success (initial value);
  only meaningful after a failure.

## Host proof (how to rerun)

`sh host/build_host.sh [plain|asan]` then `sh host/run_host_tests.sh
[plain|asan]` (gcc with -m32 multilib, openssl, python3). Big-endian:
`CC="zig cc -target powerpc-linux-musleabihf" ARCHFLAGS= KEEP_MWERKS_SHA=1
sh host/build_host.sh ppc && RUNNER=qemu-ppc sh host/run_host_tests.sh ppc`.
Test CA: generated per run; injected only through `host/test_anchor.c`
(HOST_TEST_CA), which overrides `OSTLS_B3_GetAnchors`; the real 121-anchor
table is compiled and linked unchanged (renamed at compile time with
`-DOSTLS_B3_GetAnchors=OSTLS_B3_GetAnchors_real`, host build only). Host
deviations from the Mac: `-D__MWERKS__` with fake headers; on x86 sha2big.c
uses BearSSL's stock path (its CW8 path is 32-bit big-endian only; it runs in
the ppc build); `"\p"` strings arrive as `"p..."` (fake FSMakeFSSpec copes).
