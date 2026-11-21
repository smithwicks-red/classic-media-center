/*
    hc_stream.h -- (proposed addition to https_client.h, job J2)
    A non-blocking, step-driven byte stream over the SAME transports
    HC_Get uses (macTLS for https, plain OT for http): HC_Get's static
    HC_Ops table made public. No HTTP in here: mc_proxy speaks HTTP.
    Nothing blocks; call HC_StreamStep before reading/writing.
*/
#ifndef HC_STREAM_H
#define HC_STREAM_H

typedef struct HC_Stream HC_Stream;

#define HC_S_CONNECTING 0
#define HC_S_OPEN       1
#define HC_S_FAILED     2

/* NULL = failed at once (bad host string, no memory, clock < 2000). */
HC_Stream *HC_StreamOpen(const char *host, unsigned short port, int https,
                         int insecure, unsigned long timeout_ticks);
int   HC_StreamStep(HC_Stream *s);                       /* HC_S_*   */
/* >= 0 bytes accepted, -2 error */
long  HC_StreamWrite(HC_Stream *s, const char *p, unsigned long n);
/* > 0 bytes, 0 none now, -1 orderly end, -2 error */
long  HC_StreamRead(HC_Stream *s, char *p, unsigned long cap);
void  HC_StreamClose(HC_Stream *s);
int   HC_StreamError(HC_Stream *s);  /* HC_ERR_* detail after FAILED  */

#endif
