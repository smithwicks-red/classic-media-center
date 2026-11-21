/*
    mc04_build_hosttest.c -- HOST ONLY. (0.4) Runs the REAL
    BuildReferenceMovie() of H264Stream.c (H264STREAM_HOSTTEST, the way
    tls18/host/mc02_host.c includes it) on a local file's moov, so the
    0.3 and 0.4 sources can be compared byte for byte and B-frame files
    reach the moov rewrite without a server. Compiled twice:
      0.4: gcc -std=c89 -DMC04 -I. mc04_build_hosttest.c mc_core.c AVCReorder.c
      0.3: gcc -std=c89 -I<0.3 dir> mc04_build_hosttest.c <0.3 dir>/mc_core.c
    usage: build FILE URL|- OUT [embed]
      URL = the streaming reference movie; "-" = the moov a download
      appends (self-contained). embed: OUT = FILE with its ftyp/moov
      relabelled 'free' + the new moov appended (the download layout of
      mc_fetch.c; same bytes as its walker writes).
      env MC_DECODER_LEVEL (0.4 only): 0 / 1, default 0.
    prints: VERDICT level=L v=V <sentence> / BUILD ok moov_off=.. new_moov=..
    (the gate is printed, not applied: the moov is built regardless)
*/
#define H264STREAM_HOSTTEST 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *MemAlloc(long n) { return malloc((size_t)(n ? n : 1)); }
static void  MemFree(void *p) { free(p); }
static int   NetOpen(const char *h, unsigned short p) { (void)h; (void)p; return -1; }
static long  NetSend(const char *b, long n) { (void)b; (void)n; return -1; }
static long  NetRecv(char *b, long m) { (void)b; (void)m; return -1; }
static void  NetClose(void) {}
static int   SaveAndOpenMovie(const char *name, const unsigned char *d, long n)
{ (void)name; (void)d; (void)n; return -1; }

#include "H264Stream.c"

static long BoxSize(const unsigned char *b, long pos, long len)
{
    long sz = (long)RD32(b + pos);
    if (sz == 1 && pos + 16 <= len && RD32(b + pos + 8) == 0)
        sz = (long)RD32(b + pos + 12);
    return (sz < 8 || sz > len - pos) ? -1 : sz;
}

int main(int argc, char **argv)
{
    FILE *f, *o;
    unsigned char *buf, *ref;
    long len, pos, sz, mo = -1, ms = 0, refLen = 0;
    McAvcInfo info;
    char why[200];
    int v, lvl = 0;

    if (argc < 4) { fprintf(stderr, "usage: build FILE URL|- OUT [embed]\n"); return 2; }
#ifdef MC04
    if (getenv("MC_DECODER_LEVEL") != NULL) lvl = atoi(getenv("MC_DECODER_LEVEL"));
    mc_set_decoder_level(lvl);
#endif
    f = fopen(argv[1], "rb");
    if (f == NULL) return 2;
    fseek(f, 0, SEEK_END); len = ftell(f); fseek(f, 0, SEEK_SET);
    buf = (unsigned char *)malloc((size_t)len);
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) return 2;
    fclose(f);
    for (pos = 0; pos + 8 <= len && (sz = BoxSize(buf, pos, len)) > 0; pos += sz)
        if (ISTYPE(buf + pos + 4, "moov")) { mo = pos; ms = sz; }
    if (mo < 0) { printf("BUILD no moov\n"); return 2; }
    v = mc_avc_verdict(&info, mc_avc_profile(buf + mo, ms, &info), why, sizeof why);
    printf("VERDICT level=%d v=%d %s\n", lvl, v, why);
    ref = BuildReferenceMovie(buf + mo, ms, strcmp(argv[2], "-") ? argv[2] : NULL,
                              &refLen);
    if (ref == NULL) { printf("BUILD failed\n"); return 1; }
    o = fopen(argv[3], "wb");
    if (o == NULL) return 2;
    if (argc > 4)
    {
        for (pos = 0; pos + 8 <= len && (sz = BoxSize(buf, pos, len)) > 0; pos += sz)
            if (ISTYPE(buf + pos + 4, "moov") || ISTYPE(buf + pos + 4, "ftyp"))
                memcpy(buf + pos + 4, "free", 4);
        fwrite(buf, 1, (size_t)len, o);
    }
    fwrite(ref, 1, (size_t)refLen, o);
    fclose(o);
    printf("BUILD ok moov_off=%ld new_moov=%ld\n", argc > 4 ? len : 0L, refLen);
    MemFree(ref);
    free(buf);
    return 0;
}
