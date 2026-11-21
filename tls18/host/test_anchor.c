/* test_anchor.c -- HOST TEST ONLY. Overrides OSTLS_B3_GetAnchors so the
   local tests can trust a throwaway CA. The real 121-anchor table is
   still compiled and linked: its symbol is renamed with objcopy to
   OSTLS_B3_GetAnchors_real, which is used when HOST_TEST_CA is unset. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bearssl.h"

void OSTLS_B3_GetAnchors_real(const br_x509_trust_anchor **a, size_t *n);

#define MAXTA 4
static br_x509_trust_anchor g_tas[MAXTA];
static int g_nta = 0;
static int g_loaded = 0;
static unsigned char g_dns[MAXTA][1024];
static size_t g_dn_len = 0;
static unsigned char *g_dn;
static unsigned char g_keys[MAXTA][1024];

static void dn_cb(void *ctx, const void *buf, size_t len)
{
    (void)ctx;
    if (g_dn_len + len <= 1024) { memcpy(g_dn + g_dn_len, buf, len); g_dn_len += len; }
}

static int load_ca(const char *path)
{
    static unsigned char der[8192];
    size_t n;
    FILE *f;
    br_x509_decoder_context dc;
    const br_x509_pkey *pk;
    f = fopen(path, "rb");
    if (!f) return -1;
    n = fread(der, 1, sizeof der, f);
    fclose(f);
    br_x509_trust_anchor *ta = &g_tas[g_nta];
    unsigned char *g_key = g_keys[g_nta];
    br_x509_trust_anchor g_ta;
    g_dn = g_dns[g_nta];
    g_dn_len = 0;
    br_x509_decoder_init(&dc, dn_cb, NULL);
    br_x509_decoder_push(&dc, der, n);
    pk = br_x509_decoder_get_pkey(&dc);
    if (!pk) return -2;
    g_ta.dn.data = g_dn;
    g_ta.dn.len = g_dn_len;
    g_ta.flags = BR_X509_TA_CA;
    g_ta.pkey.key_type = pk->key_type;
    if (pk->key_type == BR_KEYTYPE_EC) {
        memcpy(g_key, pk->key.ec.q, pk->key.ec.qlen);
        g_ta.pkey.key.ec.curve = pk->key.ec.curve;
        g_ta.pkey.key.ec.q = g_key;
        g_ta.pkey.key.ec.qlen = pk->key.ec.qlen;
    } else {
        memcpy(g_key, pk->key.rsa.n, pk->key.rsa.nlen);
        memcpy(g_key + pk->key.rsa.nlen, pk->key.rsa.e, pk->key.rsa.elen);
        g_ta.pkey.key.rsa.n = g_key;
        g_ta.pkey.key.rsa.nlen = pk->key.rsa.nlen;
        g_ta.pkey.key.rsa.e = g_key + pk->key.rsa.nlen;
        g_ta.pkey.key.rsa.elen = pk->key.rsa.elen;
    }
    *ta = g_ta;
    g_nta++;
    return 0;
}

void OSTLS_B3_GetAnchors(const br_x509_trust_anchor **a, size_t *n)
{
    const char *ca = getenv("HOST_TEST_CA");
    if (!ca || !*ca) { OSTLS_B3_GetAnchors_real(a, n); return; }
    if (!g_loaded) {
        char buf[2048], *p, *c;
        snprintf(buf, sizeof buf, "%s", ca);
        for (p = buf; p && *p && g_nta < MAXTA; p = c) {
            int r;
            c = strchr(p, ':');
            if (c) *c++ = 0;
            r = load_ca(p);
            if (r) { fprintf(stderr, "test CA load failed %d\n", r); exit(3); }
        }
        g_loaded = 1;
    }
    *a = g_tas;
    *n = (size_t)g_nta;
}
