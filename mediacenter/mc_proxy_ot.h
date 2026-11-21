/*
    mc_proxy_ot.h -- Classic Media Center 0.5s: the Open Transport side of
    the streaming proxy (mc_proxy.c is the portable core).

    * listener on 127.0.0.1:8080, else 8081..8089, else an OT-assigned
      port (127.0.0.1 first, then any address: peers that are not
      127.x.x.x are refused);
    * MCPO_SLOTS acceptor endpoints opened + bound up front, reused;
    * (0.5t) ASYNCHRONOUS endpoints with one notifier (QTSniff Q2): the
      notifier accepts, reads the request and answers QuickTime's GET of
      the pre-buffered film with the head + MCPO_PREBUF bytes from RAM
      (mc_notify.c); the event loop then adopts the connection into
      mc_proxy; every other request is replayed into mc_proxy;
    * QuickTime's side = McpOps down_read/down_write/down_close over
      OTRcv/OTSnd/OTLook, orderly release both ways;
    * upstream = https_client's HC_Stream (macTLS for https).

    Mac only (Universal Interfaces 3.4.1, CodeWarrior 8.3, C89). It also
    compiles on the host against tls18/host's fake Open Transport (which
    has a POSIX listener since J5), so the host tests run THIS file.
*/
#ifndef MC_PROXY_OT_H
#define MC_PROXY_OT_H

#include "mc_proxy.h"

#define MCPO_BASE_PORT  8080
#define MCPO_NPORTS     10     /* 8080..8089                          */
#define MCPO_MAX_CONNS  4      /* McpConfig.max_conns                 */
#define MCPO_SLOTS      5      /* one more: a 5th call gets the 503   */
#define MCPO_CALLS      4      /* listener qlen / pending calls       */
#define MCPO_LINGER     300    /* ticks an orderly close may take     */
#define MCPO_PREBUF     (512UL * 1024UL)  /* (0.5t) media bytes pre-buffered */
#define MCPO_PREBUF_MAX (4096UL * 1024UL) /* (0.5u) cap of the whole Ptr  */
#define MCPO_PARK_TICKS 1800UL /* parked upstream lifetime (30 s)      */

/* log: one line, no line end (may be NULL). 0 = listening (port in
   *port), -1 = no listener (why logged). Opening twice is harmless. */
int  mcpo_open(void (*log)(void *ref, const char *line), void *ref,
               unsigned short *port);
int  mcpo_is_open(void);
unsigned short mcpo_port(void);
McpServer *mcpo_server(void);
/* "Skip certificate checks" for the upstreams opened from now on */
void mcpo_set_insecure(int on);
/* accept calls, run the core, finish orderly closes. >0 = bytes moved */
int  mcpo_poll(void);
/* open QuickTime connections / parked upstreams / closing endpoints */
int  mcpo_active(void);
int  mcpo_parked(void);
int  mcpo_closing(void);
void mcpo_cancel_all(void);   /* hard-close every connection + upstream */
void mcpo_close(void);        /* everything, listener included        */

/* (0.5t) pre-buffer: target = the dref's path ("/a/host/path"). The
   core fetches [0, want) into a Ptr (0.5u: allocated at the size
   needed; freed and reallocated when a later film needs more), through
   a memory downstream, and parks the upstream at want. want =
   mediaFrom + MCPO_PREBUF (mc_stream.c), at most MCPO_PREBUF_MAX.
   start: 0 started, 1 busy (a slot still sends the previous film's
   pre-buffer: call again), -1 failed. poll: 0 running, 1 done (the
   notifier now answers QuickTime from it), -1 failed. got / want /
   total (0 = not known yet) are for the "Buffering N%" line. */
int  mcpo_prebuf_start(const char *target, unsigned long want);
int  mcpo_prebuf_poll(unsigned long *got, unsigned long *want,
                      unsigned long *total);
void mcpo_prebuf_cancel(void);

/* host tests only (0 = the real values) */
extern unsigned long  mcpo_test_prebuf;      /* instead of MCPO_PREBUF */
extern unsigned long  mcpo_test_prebuf_max;  /* instead of MCPO_PREBUF_MAX */
extern unsigned short mcpo_test_base_port;   /* instead of 8080        */
extern unsigned short mcpo_test_https_port;  /* upstream 443 -> this   */
extern int            mcpo_test_localhost;   /* allow /a/localhost/    */
extern int            mcpo_test_fail;        /* mcpo_open fails        */

#endif
