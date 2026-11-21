/*
 * ostls_noverify_x509.c -- tls18 PATCH 1 (new file). See the header.
 * Strict C89.
 */
#include "ostls_noverify_x509.h"

static void
nv_start_chain(const br_x509_class **ctx, const char *server_name)
{
    ostls_noverify_x509 *nv;

    nv = (ostls_noverify_x509 *)(void *)ctx;
    nv->certs_seen = 0;
    nv->inner_err = -1;
    br_x509_decoder_init(&nv->dec, 0, 0);
    (*nv->inner)->start_chain(nv->inner, server_name);
}

static void
nv_start_cert(const br_x509_class **ctx, uint32_t length)
{
    ostls_noverify_x509 *nv;

    nv = (ostls_noverify_x509 *)(void *)ctx;
    nv->certs_seen++;
    (*nv->inner)->start_cert(nv->inner, length);
}

static void
nv_append(const br_x509_class **ctx, const unsigned char *buf, size_t len)
{
    ostls_noverify_x509 *nv;

    nv = (ostls_noverify_x509 *)(void *)ctx;
    if (nv->certs_seen == 1) {
        br_x509_decoder_push(&nv->dec, buf, len);   /* leaf only */
    }
    (*nv->inner)->append(nv->inner, buf, len);
}

static void
nv_end_cert(const br_x509_class **ctx)
{
    ostls_noverify_x509 *nv;

    nv = (ostls_noverify_x509 *)(void *)ctx;
    (*nv->inner)->end_cert(nv->inner);
}

static unsigned
nv_end_chain(const br_x509_class **ctx)
{
    ostls_noverify_x509 *nv;

    nv = (ostls_noverify_x509 *)(void *)ctx;
    nv->inner_err = (int)(*nv->inner)->end_chain(nv->inner);
    /* Fail only if we could not even extract the leaf's key: without
     * it the handshake cannot proceed anyway. */
    if (br_x509_decoder_get_pkey(&nv->dec) == 0) {
        return (unsigned)(nv->inner_err > 0 ? nv->inner_err
                                            : BR_ERR_X509_INVALID_VALUE);
    }
    return 0;
}

static const br_x509_pkey *
nv_get_pkey(const br_x509_class *const *ctx, unsigned *usages)
{
    const ostls_noverify_x509 *nv;

    nv = (const ostls_noverify_x509 *)(const void *)ctx;
    if (usages != 0) {
        *usages = BR_KEYTYPE_KEYX | BR_KEYTYPE_SIGN;
    }
    return br_x509_decoder_get_pkey((br_x509_decoder_context *)&nv->dec);
}

static const br_x509_class ostls_noverify_vtable = {
    sizeof(ostls_noverify_x509),
    nv_start_chain,
    nv_start_cert,
    nv_append,
    nv_end_cert,
    nv_end_chain,
    nv_get_pkey
};

void
ostls_noverify_x509_init(ostls_noverify_x509 *nv, const br_x509_class **inner)
{
    nv->vtable = &ostls_noverify_vtable;
    nv->inner = inner;
    nv->certs_seen = 0;
    nv->inner_err = -1;
    br_x509_decoder_init(&nv->dec, 0, 0);
}

int
ostls_noverify_x509_ignored_error(const ostls_noverify_x509 *nv)
{
    return nv->inner_err;
}
