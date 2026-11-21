/*
    mc_notify.c -- Classic Media Center 0.5t: notifier-time state machine
    (see mc_notify.h). Strict C89, no newline escapes (CodeWarrior "Map
    newlines to CR"): wire bytes are written as '\015' / '\012'.
*/
#include <string.h>

#include "mc_notify.h"

static unsigned long qnow(McnState *st) { return st->io.ticks(st->io.ctx); }

/* ------------------------------------------------------------------ */
/* [N] log ring: single producer per state (notifiers are serialized;  */
/* the event loop writes only inside OTEnterNotifier)                  */
/* ------------------------------------------------------------------ */

void mcn_log(McnState *st, int slot, int code, unsigned long a,
             unsigned long b, const char *txt)
{
    McnRec *r;
    unsigned long h = st->rhead;
    if (h - st->rtail >= MCN_RING) { st->dropped++; return; }
    r = &st->ring[h % MCN_RING];
    r->tick = qnow(st);
    r->slot = slot;
    r->id = (slot >= 0 && slot < MCN_SLOTS) ? st->s[slot].id : 0;
    r->code = code;
    r->a = a;
    r->b = b;
    r->txt = txt;
    st->rhead = h + 1;
}

int mcn_next_rec(McnState *st, McnRec *out)
{
    unsigned long t = st->rtail;
    if (t == st->rhead) return 0;
    *out = st->ring[t % MCN_RING];
    st->rtail = t + 1;
    return 1;
}

/* ------------------------------------------------------------------ */
/* event loop                                                          */
/* ------------------------------------------------------------------ */

void mcn_init(McnState *st, const McnIo *io)
{
    int k;
    memset(st, 0, sizeof *st);
    st->io = *io;
    for (k = 0; k < MCN_SLOTS; k++) st->s[k].st = MCN_OFF;
}

void mcn_prebuf_off(McnState *st) { st->pre.on = 0; }

int mcn_prebuf_busy(McnState *st)
{
    int k;
    for (k = 0; k < MCN_SLOTS; k++)
        if (st->s[k].st == MCN_SEND
            || (st->s[k].st == MCN_READY && st->s[k].why == MCN_WHY_SERVED))
            return 1;
    return 0;
}

void mcn_set_prebuf(McnState *st, const char *target, const char *buf,
                    unsigned long len, unsigned long total,
                    const char *h206, unsigned long l206,
                    const char *h200, unsigned long l200)
{
    McnPre *p = &st->pre;
    unsigned long k = 0;
    p->on = 0;
    while (target[k] && target[k] != '?' && target[k] != '#'
           && k + 1 < sizeof p->target)
    { p->target[k] = target[k]; k++; }
    p->target[k] = 0;
    p->tlen = k;
    p->buf = buf;
    p->len = len < total ? len : total;
    p->total = total;
    if (l206 > MCN_HEAD) l206 = 0;
    if (l200 > MCN_HEAD) l200 = 0;
    memcpy(p->h206, h206, (size_t)l206);
    memcpy(p->h200, h200, (size_t)l200);
    p->l206 = l206;
    p->l200 = l200;
    if (k > 0 && len > 0 && l206 > 0 && l200 > 0) p->on = 1;
}

int mcn_busy(McnState *st, int slot)
{
    int x = st->s[slot].st;
    return x >= MCN_ACCEPTING && x <= MCN_READY;
}

void mcn_set_state(McnState *st, int slot, int state)
{
    st->s[slot].st = state;
    if (state == MCN_FREE || state == MCN_OFF)
    {
        st->s[slot].fin = st->s[slot].disc = 0;
        st->s[slot].need_kick = 0;
    }
}

int mcn_take(McnState *st, int slot, McnTake *t)
{
    McnSlot *s = &st->s[slot];
    if (s->st != MCN_READY) return 0;
    memset(t, 0, sizeof *t);
    t->why = s->why;
    t->id = s->id;
    t->status = s->status;
    t->fin = s->fin;
    t->disc = s->disc;
    t->body_sent = s->body_sent;
    t->n = s->n;
    t->total = s->total;
    t->req = &s->req;
    t->replay = s->raw;
    t->nreplay = s->rawlen;
    s->st = MCN_LOOP;
    return 1;
}

/* ------------------------------------------------------------------ */
/* [N] notifier time                                                   */
/* ------------------------------------------------------------------ */

int mcn_owns_events(McnState *st, int slot)
{
    int x = st->s[slot].st;
    return x == MCN_REQ || x == MCN_SEND;
}

void mcn_accepting(McnState *st, int slot, unsigned long ip,
                   unsigned long port)
{
    McnSlot *s = &st->s[slot];
    s->st = MCN_ACCEPTING;
    s->why = 0;
    s->id = ++st->next_id;
    s->status = 0;
    s->ip = ip;
    s->port = port;
    s->t_accept = qnow(st);
    mcp_req_init(&s->req);
    s->rawlen = 0;
    s->head = NULL;
    s->hlen = s->hsent = 0;
    s->n = s->total = s->pre_len = s->body_sent = s->sends = 0;
    s->fin = s->disc = s->need_kick = s->in_flow = 0;
    mcn_log(st, slot, MCN_L_ACCEPT, ip, port, NULL);
}

static void ready(McnState *st, int slot, int why)
{
    McnSlot *s = &st->s[slot];
    s->why = why;
    s->st = MCN_READY;
}

static void pass(McnState *st, int slot, const char *why)
{
    mcn_log(st, slot, MCN_L_PASS, 0, 0, why);
    ready(st, slot, MCN_WHY_PASS);
}

/* the target names the pre-buffered film (query / fragment ignored) */
static int is_ours(McnPre *p, const char *t)
{
    unsigned long n = p->tlen;
    if (!p->on || n == 0) return 0;
    if (strncmp(t, p->target, (size_t)n) != 0) return 0;
    return t[n] == 0 || t[n] == '?' || t[n] == '#';
}

static void pump(McnState *st, int slot);

/* a whole request head is in s->req */
static void decide(McnState *st, int slot)
{
    McnSlot *s = &st->s[slot];
    McpReq *r = &s->req;
    McnPre *p = &st->pre;
    mcn_log(st, slot, MCN_L_REQ, r->len, qnow(st) - s->t_accept, NULL);
    if (strcmp(r->method, "GET") != 0) { pass(st, slot, "not a GET"); return; }
    if (!is_ours(p, r->target)) { pass(st, slot, "not the pre-buffered film"); return; }
    if (r->range_kind == MCP_R_NONE)
    { s->status = 200; s->head = p->h200; s->hlen = p->l200; }
    else if (r->range_kind == MCP_R_FROM && r->ra == 0)
    { s->status = 206; s->head = p->h206; s->hlen = p->l206; }
    else { pass(st, slot, "a Range other than bytes=0-"); return; }
    s->hsent = 0;
    s->total = p->total;
    s->n = p->total;
    s->pre_len = p->len;
    s->body_sent = 0;
    mcn_log(st, slot, MCN_L_SERVE, (unsigned long)s->status, s->pre_len, NULL);
    s->st = MCN_SEND;
    pump(st, slot);
}

void mcn_on_data(McnState *st, int slot)
{
    McnSlot *s = &st->s[slot];
    long r;
    unsigned long used, k;
    int rc, guard;
    if (s->st != MCN_REQ) return;
    for (guard = 0; guard < 16 && s->st == MCN_REQ; guard++)
    {
        r = st->io.recv(st->io.ctx, slot, s->rx, (unsigned long)MCN_RX);
        if (r == 0) return;
        if (r < 0)
        {   /* treat as a disconnect: the event loop closes it */
            mcn_log(st, slot, MCN_L_DISC, 0, (unsigned long)s->st, "recv error");
            s->disc = 1;
            ready(st, slot, MCN_WHY_DISC);
            return;
        }
        k = (unsigned long)r;               /* raw copy for a replay */
        if (k > MCP_REQ_CAP - s->rawlen) k = MCP_REQ_CAP - s->rawlen;
        memcpy(s->raw + s->rawlen, s->rx, (size_t)k);
        s->rawlen += k;
        used = 0;
        rc = mcp_req_feed(&s->req, s->rx, (unsigned long)r, &used);
        if (rc == 0) continue;
        if (rc < 0) { pass(st, slot, "bad request (mc_proxy answers it)"); return; }
        decide(st, slot);
        return;
    }
}

void mcn_passcon(McnState *st, int slot)
{
    McnSlot *s = &st->s[slot];
    if (s->st != MCN_ACCEPTING && s->st != MCN_REQ) return;
    s->st = MCN_REQ;
    mcn_on_data(st, slot);              /* data may be queued already */
}

static void pump(McnState *st, int slot)
{
    McnSlot *s = &st->s[slot];
    const char *p;
    unsigned long want;
    long r;
    int g;
    if (s->st != MCN_SEND) return;
    s->need_kick = 0;
    for (g = 0; g < MCN_GUARD; g++)
    {
        if (s->disc) return;
        if (s->hsent < s->hlen)
        { p = s->head + s->hsent; want = s->hlen - s->hsent; }
        else if (s->body_sent < s->pre_len)
        {
            p = st->pre.buf + s->body_sent;
            want = s->pre_len - s->body_sent;
            if (want > MCN_SENDMAX) want = MCN_SENDMAX;
        }
        else
        {
            mcn_log(st, slot, MCN_L_SENT, s->body_sent, s->sends, NULL);
            ready(st, slot, MCN_WHY_SERVED);
            return;
        }
        r = st->io.send(st->io.ctx, slot, p, want);
        if (r < 0)
        {
            mcn_log(st, slot, MCN_L_ERR, s->body_sent, 0, "send failed");
            ready(st, slot, MCN_WHY_ERR);
            return;
        }
        if (r == 0)
        {   /* flow controlled: T_GODATA continues */
            if (!s->in_flow) { s->in_flow = 1; s->t_flow = qnow(st); }
            return;
        }
        if (s->in_flow)
        {
            unsigned long d = qnow(st) - s->t_flow;
            s->in_flow = 0;
            if (d > MCN_FLOW_LOG) mcn_log(st, slot, MCN_L_FLOW, d, s->body_sent, NULL);
        }
        s->sends++;
        if (s->hsent < s->hlen) s->hsent += (unsigned long)r;
        else s->body_sent += (unsigned long)r;
    }
    s->need_kick = 1;                   /* the event loop calls mcn_kick */
}

void mcn_on_godata(McnState *st, int slot) { pump(st, slot); }

void mcn_kick(McnState *st, int slot)
{
    if (st->s[slot].st == MCN_SEND && st->s[slot].need_kick) pump(st, slot);
}

void mcn_on_fin(McnState *st, int slot)
{
    McnSlot *s = &st->s[slot];
    s->fin = 1;
    mcn_log(st, slot, MCN_L_FIN, s->body_sent, (unsigned long)s->st, NULL);
    /* before a whole request: mc_proxy gets the bytes, then the EOF.
       While sending: keep sending (a half-close, as mc_proxy R1 #8). */
    if (s->st == MCN_REQ || s->st == MCN_ACCEPTING) ready(st, slot, MCN_WHY_FIN);
}

void mcn_on_disc(McnState *st, int slot)
{
    McnSlot *s = &st->s[slot];
    s->disc = 1;
    mcn_log(st, slot, MCN_L_DISC, s->body_sent, (unsigned long)s->st, NULL);
    if (s->st >= MCN_ACCEPTING && s->st <= MCN_SEND) ready(st, slot, MCN_WHY_DISC);
}
