/* mcp_host.c -- HOST ONLY. Runs mc_proxy over POSIX sockets (QuickTime's
   side) and the REAL https_client transports via HC_Stream (upstream; the
   fake OT layer maps them onto POSIX sockets, BearSSL does real TLS).
   usage: mcp_host PORT [--insecure] [--maxconns N] [--park N]
                        [--token URL SIZE|-] ...
   Prints "TOKEN i xxxxxxxx" per token, then "READY". SIGTERM -> clean exit
   (so ASan's leak check runs). Stats on stderr at exit. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include "mc_proxy.h"
#include "hc_stream.h"   /* (J5) same declarations as the merged https_client.h (J2) */

extern void FakeOT_Poll(void);
extern long InitOpenTransport(void);

static volatile sig_atomic_t g_stop = 0;
static int g_insecure = 0;
static void on_term(int s) { (void)s; g_stop = 1; }

static double mono(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}
static double g_t0;

/* --- upstream: HC_Stream --- */
static void *h_up_open(void *ctx, const char *host, unsigned short port, int tls)
{
    (void)ctx;
    return HC_StreamOpen(host, port, tls, g_insecure, 600);
}
static int h_up_step(void *h)
{
    int s = HC_StreamStep((HC_Stream *)h);
    return s == HC_S_OPEN ? MCP_T_OPEN : s == HC_S_FAILED ? MCP_T_FAILED : MCP_T_CONNECTING;
}
static long h_up_write(void *h, const char *p, unsigned long n) { return HC_StreamWrite((HC_Stream *)h, p, n); }
static long h_up_read(void *h, char *p, unsigned long cap)
{
    long r = HC_StreamRead((HC_Stream *)h, p, cap);
    return r == -1 ? MCP_IO_EOF : r == -2 ? MCP_IO_ERR : r;
}
static void h_up_close(void *h) { HC_StreamClose((HC_Stream *)h); }

/* --- downstream: POSIX socket fd stored in the handle --- */
typedef struct { int fd; } Down;
static long h_down_read(void *h, char *p, unsigned long cap)
{
    ssize_t r = recv(((Down *)h)->fd, p, cap, MSG_DONTWAIT);
    if (r > 0) return (long)r;
    if (r == 0) return MCP_IO_EOF;
    if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
    return MCP_IO_ERR;
}
static long h_down_write(void *h, const char *p, unsigned long n)
{
    ssize_t r = send(((Down *)h)->fd, p, n, MSG_DONTWAIT | MSG_NOSIGNAL);
    if (r >= 0) return (long)r;
    if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
    return MCP_IO_ERR;
}
static void h_down_close(void *h, int abort)
{
    Down *d = (Down *)h;
    if (abort)
    {
        struct linger l;
        l.l_onoff = 1;
        l.l_linger = 0;
        setsockopt(d->fd, SOL_SOCKET, SO_LINGER, &l, sizeof l);
    }
    close(d->fd);
    free(d);
}
static unsigned long h_now(void *ctx) { (void)ctx; return (unsigned long)((mono() - g_t0) * 60.0) + 1000UL; }
static void h_log(void *ctx, const char *line) { (void)ctx; fprintf(stderr, "[%8.3f] %s\n", mono() - g_t0, line); }
static void *h_alloc(void *ctx, unsigned long n) { (void)ctx; return malloc(n ? n : 1); }
static void h_release(void *ctx, void *p) { (void)ctx; free(p); }
static int h_allow(void *ctx, const char *host)
{
    size_t n = strlen(host);
    (void)ctx;
    return strcmp(host, "archive.org") == 0 || (n > 12 && strcmp(host + n - 12, ".archive.org") == 0);
}

int main(int argc, char **argv)
{
    McpOps ops;
    McpConfig cfg;
    McpServer *s;
    int lfd, one = 1, i;
    struct sockaddr_in a;
    unsigned short port;
    const McpStats *st;

    if (argc < 2) { fprintf(stderr, "usage\n"); return 2; }
    g_t0 = mono();
    signal(SIGTERM, on_term);
    signal(SIGINT, on_term);
    signal(SIGPIPE, SIG_IGN);
    InitOpenTransport();
    port = (unsigned short)atoi(argv[1]);
    memset(&ops, 0, sizeof ops);
    ops.up_open = h_up_open; ops.up_step = h_up_step; ops.up_write = h_up_write;
    ops.up_read = h_up_read; ops.up_close = h_up_close;
    ops.down_read = h_down_read; ops.down_write = h_down_write; ops.down_close = h_down_close;
    ops.now = h_now; ops.log = h_log; ops.alloc = h_alloc; ops.release = h_release;
    memset(&cfg, 0, sizeof cfg);
    cfg.allow_host = h_allow;
    cfg.idle_ticks = 180;      /* 3 s: the stall test should not take 20 s */
    for (i = 2; i < argc; i++)
    {
        if (strcmp(argv[i], "--insecure") == 0) g_insecure = 1;
        else if (strcmp(argv[i], "--maxconns") == 0 && i + 1 < argc) cfg.max_conns = atoi(argv[++i]);
        else if (strcmp(argv[i], "--park") == 0 && i + 1 < argc) cfg.max_parked = atoi(argv[++i]);
        else if (strcmp(argv[i], "--buf") == 0 && i + 1 < argc) cfg.buf_size = (unsigned long)atol(argv[++i]);
    }
    s = mcp_new(&ops, &cfg);
    if (s == NULL) { fprintf(stderr, "mcp_new failed\n"); return 1; }
    for (i = 2; i < argc; i++)
        if (strcmp(argv[i], "--token") == 0 && i + 2 < argc)
        {
            char tok[9];
            unsigned long size = strcmp(argv[i + 2], "-") == 0 ? MCP_UNKNOWN : (unsigned long)atol(argv[i + 2]);
            int slot = mcp_token_add(s, argv[i + 1], size, NULL, tok);
            printf("TOKEN %d %s\n", slot, tok);
            i += 2;
        }
    lfd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(0x7F000001UL);
    if (bind(lfd, (struct sockaddr *)&a, sizeof a) != 0 || listen(lfd, 16) != 0) { perror("bind"); return 1; }
    fcntl(lfd, F_SETFL, O_NONBLOCK);
    printf("READY\n");
    fflush(stdout);
    while (!g_stop)
    {
        int busy, fd;
        while ((fd = accept(lfd, NULL, NULL)) >= 0)
        {
            Down *d = (Down *)malloc(sizeof *d);
            fcntl(fd, F_SETFL, O_NONBLOCK);
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
            d->fd = fd;
            mcp_accept(s, d);
        }
        FakeOT_Poll();
        busy = mcp_poll(s);
        if (!busy) usleep(mcp_active(s) ? 200 : 2000);
    }
    st = mcp_stats(s);
    fprintf(stderr, "STATS accepted %lu requests %lu busy %lu bytes_out %lu upstream_opens %lu redirects %lu "
            "adopted %lu parked %lu resumes %lu errors %lu client_aborts %lu active %d\n",
            st->accepted, st->requests, st->refused_busy, st->bytes_out, st->upstream_opens, st->redirects,
            st->adopted, st->parked, st->resumes, st->errors, st->client_aborts, mcp_active(s));
    mcp_free(s);
    close(lfd);
    return 0;
}
