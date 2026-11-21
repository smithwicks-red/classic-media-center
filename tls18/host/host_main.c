/* host_main.c -- HOST ONLY test driver for https_client.
   usage: hc_test URL [--insecure] [--range A-B|A-] [--out FILE]
                      [--tls12] [--timeout TICKS] [--kat]
   Prints one key=value result line on stdout. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "machdr/ot_fake.h"
#include "ostls_async.h"
#include "ostls_tls13_selftest.h"
#include "ostls_entropy.h"
#include "https_client.h"

static FILE *g_out;
static unsigned long g_calls;

static int body_cb(void *ref, const unsigned char *d, unsigned long n)
{
    (void)ref;
    g_calls++;
    if (g_out && fwrite(d, 1, n, g_out) != n) return 1;
    return 0;
}

static int idle_cb(void *ref, int busy)
{
    (void)ref;
    FakeOT_Poll();
    OSTLS_CollectEntropy();
    if (!busy) usleep(300);
    return 0;
}

int main(int argc, char **argv)
{
    HC_Request rq;
    HC_Result rs;
    int i, e;
    memset(&rq, 0, sizeof rq);
    if (argc < 2) { fprintf(stderr, "usage\n"); return 2; }
    rq.url = argv[1];
    rq.body = body_cb;
    rq.idle = idle_cb;
    for (i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--insecure")) rq.insecure = 1;
        else if (!strcmp(argv[i], "--tls12")) OSTLS_SetTryTLS13(0);
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) g_out = fopen(argv[++i], "wb");
        else if (!strcmp(argv[i], "--timeout") && i + 1 < argc) rq.timeout_ticks = strtoul(argv[++i], 0, 10);
        else if (!strcmp(argv[i], "--range") && i + 1 < argc) {
            char *dash;
            rq.use_range = 1;
            rq.range_start = strtoul(argv[++i], &dash, 10);
            rq.range_end = (dash[0] == '-' && dash[1]) ? strtoul(dash + 1, 0, 10) : 0xFFFFFFFFUL;
        } else if (!strcmp(argv[i], "--kat")) {
            unsigned long fp = 0;
            printf("kat sha384=%d tls13=%d entropy=%d\n", OSTLS_SHA384_KAT(),
                   OSTLS_TLS13_SelfTest(), OSTLS_EntropySelfTest(&fp));
            return 0;
        }
    }
    InitOpenTransport();
    e = HC_Get(&rq, &rs);
    if (g_out) fclose(g_out);
    printf("err=%d(%s) status=%d br=%d os=%ld ot=%ld ign=%d tls=%04x suite=%04x(%s) "
           "redirects=%d chunked=%d len=%lu bytes=%lu calls=%lu t_conn=%lu t_hs=%lu t_fb=%lu t_tot=%lu final=%s crange=%s\n",
           e, HC_ErrName(e), rs.http_status, rs.br_err, rs.os_err, rs.ot_err, rs.cert_err_ignored,
           rs.tls_version, rs.cipher_suite, HC_CipherName(rs.cipher_suite), rs.redirects, rs.chunked,
           rs.content_length, rs.body_bytes, g_calls, rs.t_connect, rs.t_handshake, rs.t_first_byte,
           rs.t_total, rs.final_url, rs.content_range);
    CloseOpenTransport();
    return e;
}
