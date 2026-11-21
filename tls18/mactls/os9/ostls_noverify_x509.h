/*
 * ostls_noverify_x509.h -- tls18 PATCH 1 (new file, MIT, same terms as
 * macTLS). An X.509 "validator" that NEVER fails: it forwards every
 * call to the real br_x509_minimal context (so the error full
 * validation would have produced is recorded), decodes the leaf
 * certificate's public key itself, and returns that key with
 * end_chain() == 0. Used only for connections opened with
 * OSTLSConfig.insecure_skip_verify != 0.
 */
#ifndef OSTLS_NOVERIFY_X509_H
#define OSTLS_NOVERIFY_X509_H

#include "bearssl_x509.h"

typedef struct ostls_noverify_x509 {
    const br_x509_class *vtable;          /* MUST be first */
    const br_x509_class **inner;          /* the real minimal validator */
    br_x509_decoder_context dec;          /* leaf decoder */
    int certs_seen;                       /* start_cert calls this chain */
    int inner_err;                        /* -1 until end_chain */
} ostls_noverify_x509;

void ostls_noverify_x509_init(ostls_noverify_x509 *nv,
                              const br_x509_class **inner);
int  ostls_noverify_x509_ignored_error(const ostls_noverify_x509 *nv);

#endif
