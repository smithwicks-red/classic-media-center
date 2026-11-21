# tls18 -- changes to the vendored macTLS / BearSSL subset

Source: standalone MIT macTLS, https://github.com/mplsllc/macTLS, commit
36596d3 (not MacSurf's copy). Everything under `mactls/` is byte-identical to
that commit except what is listed here. Unified diffs against upstream are in
`patches/` (`ostls_async.c.diff`, `ostls_async.h.diff`).

## PATCH 1 -- per-connection "insecure / don't verify certificates" option

Default OFF (full validation). Works on BOTH TLS 1.3 and TLS 1.2 paths (host
tests B4a/B4b), so insecure mode does not need to force 1.2.

* NEW `os9/ostls_noverify_x509.h`, `os9/ostls_noverify_x509.c` (strict C89,
  ~120 lines): a `br_x509_class` that forwards every call to the real
  `br_x509_minimal` context (so the error full validation WOULD have given is
  recorded), decodes the leaf certificate with `br_x509_decoder`, and returns
  the leaf key with `end_chain() == 0`. It fails only if the leaf cannot be
  parsed at all.
* `os9/ostls_async.h`:
  * `OSTLSConfig` gains a LAST field `int insecure_skip_verify;` (0 = verify).
    A zero-filled config keeps today's behaviour. (ABI note: the struct grew;
    everything is compiled together, so no issue.)
  * New `int OSTLS_GetIgnoredCertError(OSTLSConnection *)`: for an insecure
    connection, the BR_ERR_X509_* code that was ignored (0 = chain would have
    passed, -1 = not insecure / not known yet).
* `os9/ostls_async.c` (all hunks marked `/* tls18 PATCH 1 */`):
  * include `ostls_noverify_x509.h`; two fields in `struct OSTLSConnection`
    (`int insecure; ostls_noverify_x509 nv;` = +1132 bytes per connection).
  * `OSTLS_New`: copy `config->insecure_skip_verify`.
  * `ostls_setup_bearssl`: when insecure, wrap `conn->xc` and install the
    wrapper with `br_ssl_engine_set_x509` (1.2 path) and give it to
    `hs13->x509_ctx` (1.3 path).
  * Resumption is disabled for insecure connections in both directions: no
    1.3 ticket offered (TicketCacheGet skipped) or stored (TicketCachePut
    skipped), no 1.2 session offered (sess_cache_get) or stored
    (sess_cache_put). So a session negotiated without verification can never
    be resumed later by a verifying connection to the same host.
  * `OSTLS_GetIgnoredCertError` appended at the end of the file.

## PATCH 2 -- BearSSL file list: 3 files added, 1 glue file excluded

`tools/bearssl_cw8_files.txt` (253 files) does not link on its own:
undefined `br_prng_seeder_system` (from `ssl_engine.c`) and
`br_sslrec_out_ccm_vtable` (from `ssl_engine_default_aesccm.c`). The CW8
linker would report the same. Added (they are in upstream's own
`os9/macmake.toml` list): `bearssl/src/rand/sysrng.c` (with BR_USE_URANDOM=0
etc. it compiles to a "no system seeder" stub; macTLS injects entropy
itself), `bearssl/src/ssl/ssl_rec_ccm.c`,
`bearssl/src/symcipher/poly1305_ctmulq.c`. Result: 256 BearSSL files, see
`bearssl_files.txt`.

`os9/ssl_engine_cw8.c` is NOT vendored: it is byte-identical to
`bearssl/src/ssl/ssl_engine.c` (which is in the list); compiling both would
give duplicate symbols.

## PATCH 3 -- OTOptionManagement buffers made static

`ostls_async.c`, `pump_connect_step`, the TCP_NODELAY block: `TOption opt`,
`TOptMgmt req`, `TOptMgmt ret` changed from automatic to `static`. The
endpoint is asynchronous, so OTOptionManagement returns at once and OT writes
the reply into `ret` (and the option buffer it points at) later, at
T_OPTMGMTCOMPLETE -- after the stack frame is gone, i.e. into whatever the
app's stack holds by then. Static storage makes that late write harmless.
Shipping MacSurf has the automatic version and evidently survives (the write
is ~20 bytes into a dead area), so this is insurance against a heisenbug,
not a fix for an observed crash. Three words changed plus a comment.

## Not patched (deliberately)

* `ostls_cw8_prefix.h`, all of BearSSL: untouched. Configuration comes from
  our prefix `https_prefix.h` (includes `<MacTypes.h>`, then
  `ostls_cw8_prefix.h`, then `ostls_classic_shim.h`).
* The Carbon `...InContext` calls: handled by `ostls_classic_shim.h` macros.
* `YieldToAnyThread` implicit declaration: handled by `#include <Threads.h>`
  in the shim.
* `OSTLS_HTTP_FormatGet` (sprintf overflow on long paths): `ostls_http.c` is
  not vendored at all; `https_client.c` builds requests with bounded appends.
* Known upstream behaviours kept as they are (see BUILD_NOTES_TLS.md): no
  TLS_AES_256_GCM_SHA384 offer in TLS 1.3; a 1.3 attempt that meets a
  1.2-only server costs a second TCP connection; Pump maps every
  T_DISCONNECT to kOSTLSAsync_OTSndFail (https_client.c classifies by phase).
