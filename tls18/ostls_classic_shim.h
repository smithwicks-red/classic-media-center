/*
    ostls_classic_shim.h  --  build macTLS into a classic (non-Carbon)
                              PowerPC application without editing it

    Session 18 revision (tls18). Changes from the v15 copy:
      - #include <Threads.h> (ostls_async.c calls YieldToAnyThread but,
        on the classic path, includes no header that declares it; CW8
        would otherwise see an implicit declaration).
      - Call-site counts below re-taken against the vendored subset in
        tls18/mactls/os9 (they were taken against the whole upstream
        os9/ tree before, including files we do not compile).
      - The definition example uses a C89 comment.
      - Notes on prefix-file order and the host build added.

    ------------------------------------------------------------------
    The problem this solves
    ------------------------------------------------------------------
    macTLS is written for Carbon. Every Open Transport endpoint it opens
    goes through the Carbon-only "InContext" spellings, which take an
    extra OTClientContextPtr argument that classic Open Transport does
    not have:

        OTOpenEndpointInContext(cfg, flags, info, &err, ctx)
        OTAsyncOpenEndpointInContext(cfg, flags, info, notifier,
                                     contextPtr, ctx)

    We stay classic on purpose: a Carbon CFM application cannot bind a
    listening OT endpoint to a port of its choosing (OTBind -3150 for an
    explicit address); a classic one can (see OTProbe.c). Staying
    classic keeps the local-proxy architecture possible.

    ------------------------------------------------------------------
    Why a header and not a patch
    ------------------------------------------------------------------
    In the files we actually compile (tls18/mactls/os9, the async
    engine and its helpers) the Carbon surface is exactly:

        OTAsyncOpenEndpointInContext    2 call sites (ostls_async.c:
                                        OSTLS_Start and the TLS 1.3 ->
                                        1.2 fallback reconnect)

    OTOpenEndpointInContext is used only by upstream files we do not
    compile (b1_tcp, b2/b3 handshake, d1 probe, fetch, tls13_otprobe);
    its macro is kept so those still build if ever added. There are no
    TARGET_API_MAC_CARBON conditionals in the glue. The classic
    equivalents take the same arguments minus the trailing context
    pointer, so we define the Carbon names as macros that discard it.

    ------------------------------------------------------------------
    How to use it
    ------------------------------------------------------------------
    Do not set this file itself as the CodeWarrior prefix. Set
    tls18/https_prefix.h as the prefix (C/C++ Language panel -> Prefix
    File). That file includes, in this order:
        <MacTypes.h>            (Point etc., needed by the next file)
        ostls_cw8_prefix.h      (inline -> empty, BR_* configuration)
        ostls_classic_shim.h    (this file)

    ------------------------------------------------------------------
    The OT context global
    ------------------------------------------------------------------
    ostls_async.c declares  extern OTClientContextPtr g_ostls_ot_context;
    and passes it as the (discarded) context argument. The symbol must
    still exist for the link. Define it once in the application (our
    https_client.c does) and initialise Open Transport the classic way:

        OTClientContextPtr g_ostls_ot_context = NULL;   (never read)
        InitOpenTransport();     NOT InitOpenTransportInContext
        ...
        CloseOpenTransport();    NOT CloseOpenTransportInContext

    OTClientContextPtr is still declared by the classic UI 3.4.1
    OpenTransport.h, so the definition compiles. (UNVERIFIED on the
    guest's exact headers: if it is not, add
        typedef struct OpaqueOTClientContextPtr *OTClientContextPtr;
    at the end of https_prefix.h.)

    ------------------------------------------------------------------
    Host builds
    ------------------------------------------------------------------
    The tls18 host proof compiles the glue with -D__MWERKS__ against
    fake Mac headers, so this shim IS in scope there too (deliberately:
    it proves the macros expand correctly). macTLS's own upstream host
    stubs (the !MACSURF_CLASSIC_RUNTIME branches) are then not
    compiled. To build those upstream stubs instead, define
    OSTLS_CLASSIC_SHIM_OFF.
*/

#ifndef OSTLS_CLASSIC_SHIM_H
#define OSTLS_CLASSIC_SHIM_H

#ifndef OSTLS_CLASSIC_SHIM_OFF

/* We are deliberately not Carbon. If this fires, the project settings
   are wrong and the reason for staying classic has been lost. */
#if defined(TARGET_API_MAC_CARBON) && TARGET_API_MAC_CARBON
#error "ostls_classic_shim.h is for CLASSIC PPC builds. TARGET_API_MAC_CARBON is set -- see the header comment."
#endif

#include <Files.h>         /* tls18 GUEST FIX 1: OpenTransportProviders.h needs FSSpec */
#include <OpenTransport.h>
#include <OpenTptInternet.h>
#include <Threads.h>        /* YieldToAnyThread, called by ostls_async.c */

/*
    Carbon:  EndpointRef OTOpenEndpointInContext(cfg, flags, info, err, ctx)
    Classic: EndpointRef OTOpenEndpoint        (cfg, flags, info, err)
*/
#define OTOpenEndpointInContext(cfg, flags, info, errp, ctx) \
    ( (void)(ctx), OTOpenEndpoint((cfg), (flags), (info), (errp)) )

/*
    Carbon:  OSStatus OTAsyncOpenEndpointInContext(cfg, flags, info,
                                                   notifier, contextPtr, ctx)
    Classic: OSStatus OTAsyncOpenEndpoint         (cfg, flags, info,
                                                   notifier, contextPtr)
    macTLS passes the connection object as contextPtr and recovers it in
    the notifier, so that argument matters: it is the SECOND to last.
*/
#define OTAsyncOpenEndpointInContext(cfg, flags, info, notifier, ctxPtr, ctx) \
    ( (void)(ctx), OTAsyncOpenEndpoint((cfg), (flags), (info), (notifier), (ctxPtr)) )

/* For completeness; nothing we compile calls these. */
#ifndef InitOpenTransportInContext
#define InitOpenTransportInContext(mask, ctxp) \
    ( (void)(mask), (void)((ctxp) ? (*(ctxp) = NULL) : NULL), InitOpenTransport() )
#endif

#ifndef CloseOpenTransportInContext
#define CloseOpenTransportInContext(ctx) \
    ( (void)(ctx), CloseOpenTransport() )
#endif

#endif /* OSTLS_CLASSIC_SHIM_OFF */
#endif /* OSTLS_CLASSIC_SHIM_H */
