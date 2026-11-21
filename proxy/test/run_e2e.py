#!/usr/bin/env python3
"""HOST ONLY. End-to-end proof of mc_proxy: mcp_host (mc_proxy + REAL
https_client/BearSSL via HC_Stream over fake OT) between a fake QuickTime
client (raw sockets) and local upstream servers (plain http + Python ssl,
self-signed, proxy in insecure mode).  usage: run_e2e.py BIN WORKDIR LOG"""
import hashlib, os, random, signal, socket, ssl, subprocess, sys, threading, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

BIN, W, LOG = sys.argv[1], sys.argv[2], sys.argv[3]
os.makedirs(W, exist_ok=True)
random.seed(7)
DATA = os.urandom(3 * 1024 * 1024 + 12345)
SIZE = len(DATA)
SHA = hashlib.sha256(DATA).hexdigest()
P1, P2, PX = 18080 + random.randint(0, 500), 18600 + random.randint(0, 500), 19200 + random.randint(0, 500)
_sh = int(os.environ.get("R1_PORTSHIFT", "0")); P1, P2, PX = P1 + _sh, P2 + _sh, PX + _sh
results = []
upstream_log = []
stall_once = {"n": 0}

def check(name, ok, info=""):
    results.append((name, bool(ok)))
    print(("PASS " if ok else "FAIL ") + name + ("" if ok else "  " + info), flush=True)

def parse_range(h):
    if not h or not h.startswith("bytes="): return None
    a, b = h[6:].split("-", 1)
    if a == "": n = int(b); return (max(0, SIZE - n), SIZE - 1)
    return (int(a), min(int(b), SIZE - 1) if b else SIZE - 1)

class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def log_message(self, *a): pass
    def send_body(self, s, e, status, chunked=False, drop_after=None, stall_after=None):
        self.send_response(status)
        self.send_header("Content-Type", "video/mp4")
        if status == 206: self.send_header("Content-Range", "bytes %d-%d/%d" % (s, e, SIZE))
        if chunked: self.send_header("Transfer-Encoding", "chunked")
        else: self.send_header("Content-Length", str(e - s + 1))
        self.send_header("Connection", "close")
        self.end_headers()
        pos, sent = s, 0
        try:
            while pos <= e:
                k = min(random.randint(1, 40000), e - pos + 1)
                piece = DATA[pos:pos + k]
                if chunked: self.wfile.write(b"%x;ext=1\r\n" % k + piece + b"\r\n")
                else: self.wfile.write(piece)
                pos += k; sent += k
                if drop_after and sent >= drop_after:
                    self.connection.shutdown(socket.SHUT_RDWR); return
                if stall_after and sent >= stall_after:
                    time.sleep(6); return
            if chunked: self.wfile.write(b"0\r\nX-T: 1\r\n\r\n")
        except (BrokenPipeError, ConnectionResetError, ssl.SSLError, OSError):
            pass
    def do_GET(self):
        rng = self.headers.get("Range")
        upstream_log.append((self.server.server_port, self.path, rng))
        p = self.path
        if p == "/redir302":
            self.send_response(302); self.send_header("Location", "http://127.0.0.1:%d/redir301" % P1)
            self.send_header("Content-Length", "0"); self.end_headers(); return
        if p == "/redir301":
            self.send_response(301); self.send_header("Location", "https://localhost:%d/file" % P2)
            self.send_header("Content-Length", "5"); self.end_headers(); self.wfile.write(b"moved"); return
        r = parse_range(rng)
        if r and r[0] >= SIZE:
            self.send_response(416); self.send_header("Content-Range", "bytes */%d" % SIZE)
            self.send_header("Content-Length", "0"); self.end_headers(); return
        if p == "/file":
            if r: self.send_body(r[0], r[1], 206)
            else: self.send_body(0, SIZE - 1, 200)
        elif p == "/chunked":   # ignores Range, chunked 200, no length
            self.send_body(0, SIZE - 1, 200, chunked=True)
        elif p == "/chunked206":
            s, e = r if r else (0, SIZE - 1)
            self.send_body(s, e, 206 if r else 200, chunked=True)
        elif p == "/norange":
            self.send_body(0, SIZE - 1, 200)
        elif p == "/drop":
            s, e = r if r else (0, SIZE - 1)
            self.send_body(s, e, 206 if r else 200, drop_after=300000)
        elif p == "/stall":
            s, e = r if r else (0, SIZE - 1)
            stall_once["n"] += 1
            self.send_body(s, e, 206 if r else 200, stall_after=100000 if stall_once["n"] == 1 else None)
        elif p == "/gone":
            self.send_response(404); self.send_header("Content-Length", "0"); self.end_headers()
        else:
            self.send_response(500); self.send_header("Content-Length", "0"); self.end_headers()

# --- servers ---
subprocess.run(["openssl", "req", "-x509", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:prime256v1",
                "-nodes", "-keyout", W + "/k.pem", "-out", W + "/c.pem", "-days", "2", "-subj", "/CN=localhost",
                "-addext", "subjectAltName=DNS:localhost"], check=True, capture_output=True)
plain = ThreadingHTTPServer(("127.0.0.1", P1), H)
tls = ThreadingHTTPServer(("127.0.0.1", P2), H)
ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); ctx.load_cert_chain(W + "/c.pem", W + "/k.pem")
tls.socket = ctx.wrap_socket(tls.socket, server_side=True)
for srv in (plain, tls): threading.Thread(target=srv.serve_forever, daemon=True).start()

U = "https://localhost:%d" % P2
toks = [("chain", "http://127.0.0.1:%d/redir302" % P1, "-"),
        ("chunked_known", U + "/chunked", str(SIZE)),
        ("chunked_unknown", U + "/chunked", "-"),
        ("norange", U + "/norange", "-"),
        ("drop", U + "/drop", "-"),
        ("file_known", U + "/file", str(SIZE)),
        ("chunked206", U + "/chunked206", "-"),
        ("plain", "http://127.0.0.1:%d/file" % P1, "-"),
        ("stall", U + "/stall", "-"),
        ("gone", U + "/gone", "-"),
        ("suffix", U + "/file", "-"),
        ("seq", U + "/file", "-"),
        ("headfirst", U + "/file", "-")]   # R1: HEAD on an unknown size, then GET
args = [BIN, str(PX), "--insecure"] + os.environ.get("MCP_EXTRA", "").split()
for _, u, sz in toks: args += ["--token", u, sz]
logf = open(LOG, "w")
env = dict(os.environ, ASAN_OPTIONS="detect_leaks=1:abort_on_error=0", UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1")
px = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=logf, text=True, env=env)
T = {}
while True:
    line = px.stdout.readline()
    if not line: print("proxy died"); sys.exit(1)
    if line.startswith("TOKEN"):
        _, i, t = line.split(); T[toks[int(i)][0]] = t
    if line.startswith("READY"): break

# --- fake QuickTime ---
def request(path, rng=None, method="GET", http="1.0", read_limit=None, raw=None, timeout=60):
    s = socket.create_connection(("127.0.0.1", PX), timeout=timeout)
    if raw is None:
        raw = "%s %s HTTP/%s\r\nHost: 127.0.0.1:%d\r\nUser-Agent: QuickTime/6.0.3\r\n" % (method, path, http, PX)
        if rng: raw += "Range: %s\r\n" % rng
        raw += "\r\n"
        raw = raw.encode()
    # dribble the request in random pieces (robustness)
    i = 0
    while i < len(raw):
        k = random.randint(1, 12); s.sendall(raw[i:i + k]); i += k
    buf = b""
    while b"\r\n\r\n" not in buf:
        d = s.recv(65536)
        if not d: break
        buf += d
    head, _, body = buf.partition(b"\r\n\r\n")
    lines = head.decode("latin1").split("\r\n")
    status = int(lines[0].split()[1]) if lines and len(lines[0].split()) > 1 else 0
    hd = {}
    for l in lines[1:]:
        if ":" in l: k, v = l.split(":", 1); hd[k.strip().lower()] = v.strip()
    want = int(hd.get("content-length", "-1"))
    while (want < 0 or len(body) < want) and (read_limit is None or len(body) < read_limit):
        try: d = s.recv(65536)
        except socket.timeout: break
        if not d: break
        body += d
    s.close()
    return status, hd, body, lines[0]

def tp(name, fn="f.mp4"): return "/t/%s/%s" % (T[name], fn)

def body_ok(name, st, hd, body, s, e, want_status):
    ok = st == want_status and int(hd.get("content-length", -1)) == e - s + 1 and len(body) == e - s + 1 \
         and hashlib.sha256(body).hexdigest() == hashlib.sha256(DATA[s:e + 1]).hexdigest() \
         and "transfer-encoding" not in hd
    if want_status == 206: ok = ok and hd.get("content-range") == "bytes %d-%d/%d" % (s, e, SIZE)
    if want_status == 200: ok = ok and hashlib.sha256(body).hexdigest() == SHA
    check(name, ok, "status %s len %d hd %s" % (st, len(body), hd))

t0 = time.time()
# 1 whole GET through 302 -> 301 (http->https) -> 200
st, hd, b, sl = request(tp("chain"))
body_ok("whole GET via 302->301(http->https)->200, sha256", st, hd, b, 0, SIZE - 1, 200)
check("status line HTTP/1.1, Accept-Ranges, Connection: close", sl.startswith("HTTP/1.1 200") and hd.get("accept-ranges") == "bytes" and hd.get("connection") == "close")
# 2 range from the middle (cached final URL now)
st, hd, b, _ = request(tp("chain"), "bytes=1000000-1999999")
body_ok("Range middle 1000000-1999999 -> 206", st, hd, b, 1000000, 1999999, 206)
st, hd, b, _ = request(tp("chain"), "bytes=%d-" % (SIZE - 5000))
body_ok("Range a- (tail)", st, hd, b, SIZE - 5000, SIZE - 1, 206)
st, hd, b, _ = request(tp("chain"), "bytes=-777")
body_ok("Range -777 (suffix, size known)", st, hd, b, SIZE - 777, SIZE - 1, 206)
st, hd, b, _ = request(tp("suffix"), "bytes=-4096")
body_ok("Range -4096 (suffix, size unknown -> upstream suffix)", st, hd, b, SIZE - 4096, SIZE - 1, 206)
st, hd, b, _ = request(tp("chain"), "bytes=5-5000000000")
check("Range a-b with huge b ignored -> 200 whole", st == 200 and len(b) == SIZE)
st, hd, b, _ = request(tp("chain"), "bytes=0-0")
body_ok("Range 0-0 (one byte)", st, hd, b, 0, 0, 206)
# 3 several ranges in parallel
res = {}
def worker(i, s, e):
    res[i] = (s, e, request(tp("chain"), "bytes=%d-%d" % (s, e)))
th = []
for i in range(4):
    s = random.randint(0, SIZE - 600000); e = s + random.randint(1, 500000)
    t = threading.Thread(target=worker, args=(i, s, e)); t.start(); th.append(t)
for t in th: t.join()
for i in range(4):
    s, e, (st, hd, b, _) = res[i]
    body_ok("parallel range %d (%d-%d)" % (i, s, e), st, hd, b, s, e, 206)
# 4 seek: start at 0, read 200 KB, close; then new range far ahead
st, hd, b, _ = request(tp("file_known"), "bytes=0-", read_limit=200000)
check("seek: early close after %d bytes" % len(b), st == 206 and b == DATA[:len(b)] and len(b) < SIZE)
st, hd, b, _ = request(tp("file_known"), "bytes=2500000-")
body_ok("seek: new range 2500000- after the close", st, hd, b, 2500000, SIZE - 1, 206)
# sequential continuation -> parked upstream adopted
st, hd, b, _ = request(tp("seq"), "bytes=0-499999")
body_ok("sequential 1: 0-499999", st, hd, b, 0, 499999, 206)
opens_before = sum(1 for x in upstream_log if x[1] == "/file")
st, hd, b, _ = request(tp("seq"), "bytes=500000-999999")
body_ok("sequential 2: 500000-999999", st, hd, b, 500000, 999999, 206)
st, hd, b, _ = request(tp("seq"), "bytes=1100000-1199999")   # 100 KB gap: adopt + skip
body_ok("sequential 3 after a 100 KB gap", st, hd, b, 1100000, 1199999, 206)
check("parked upstream reused (no new upstream GET for 2 and 3)", sum(1 for x in upstream_log if x[1] == "/file") == opens_before)
# 5 early close at once (headers only)
st, hd, b, _ = request(tp("chain"), None, read_limit=1)
check("early close right after the head", st == 200)
# 6 chunked upstream
st, hd, b, _ = request(tp("chunked_known"))
body_ok("chunked upstream (size registered) -> 200 with Content-Length", st, hd, b, 0, SIZE - 1, 200)
st, hd, b, _ = request(tp("chunked_known"), "bytes=123456-654321")
body_ok("chunked upstream ignoring Range -> skip -> 206", st, hd, b, 123456, 654321, 206)
st, hd, b, _ = request(tp("chunked_unknown"))
check("chunked upstream, size unknown -> 502 (never chunk to QuickTime)", st == 502)
st, hd, b, _ = request(tp("chunked206"), "bytes=1000-2000000")
body_ok("chunked 206 upstream -> de-chunked 206", st, hd, b, 1000, 2000000, 206)
# 7 upstream without Range support
st, hd, b, _ = request(tp("norange"), "bytes=2000000-2100000")
body_ok("upstream ignores Range (200) -> skip -> 206", st, hd, b, 2000000, 2100000, 206)
# 8 upstream drops mid-body -> resume
st, hd, b, _ = request(tp("drop"))
body_ok("upstream drops every 300 KB -> resumed, whole file", st, hd, b, 0, SIZE - 1, 200)
st, hd, b, _ = request(tp("stall"), "bytes=0-999999")
body_ok("upstream stalls 6 s -> resumed after idle timeout", st, hd, b, 0, 999999, 206)
# 9 plain http upstream
st, hd, b, _ = request(tp("plain"), "bytes=10-20")
body_ok("plain http upstream", st, hd, b, 10, 20, 206)
# 10 errors
st, *_ = request(tp("chain"), "bytes=%d-" % SIZE)
check("416 beyond the end", st == 416)
st, hd, *_ = request(tp("file_known"), "bytes=%d-%d" % (SIZE + 5, SIZE + 9))
check("416 with Content-Range */T", st == 416 and hd.get("content-range") == "bytes */%d" % SIZE)
st, *_ = request("/t/deadbeef/x.mp4")
check("404 unknown token", st == 404)
st, *_ = request("/a/evil.example.com/x.mp4")
check("404 /a/ host not allowed", st == 404)
st, *_ = request(tp("gone"))
check("upstream 404 -> 404", st == 404)
st, *_ = request("/x", raw=b"\x16\x03\x01junk junk\r\n\r\n")
check("400 junk request", st == 400)
st, *_ = request(tp("chain"), method="POST")
check("405 POST", st == 405)
st, hd, b, _ = request(tp("chain"), method="HEAD")
check("HEAD: head only, Content-Length = size", st == 200 and hd.get("content-length") == str(SIZE) and b == b"")
st, hd, b, _ = request("http://127.0.0.1:%d%s" % (PX, tp("chain")), "bytes=7-9")
body_ok("absolute-form target", st, hd, b, 7, 9, 206)
# 11 busy: 4 idle connections hold every slot, the 5th gets 503
idle = [socket.create_connection(("127.0.0.1", PX)) for _ in range(4)]
time.sleep(0.3)
st, *_ = request(tp("chain"), "bytes=0-9")
check("503 when all 4 connections are busy", st == 503)
for s in idle: s.close()
time.sleep(0.3)
st, hd, b, _ = request(tp("chain"), "bytes=0-9")
body_ok("served again after the idle connections left", st, hd, b, 0, 9, 206)
# 12 random fuzz of ranges against the known-size token, plus random early closes
bad = 0
for i in range(40):
    s = random.randint(0, SIZE - 1); e = random.randint(s, min(SIZE - 1, s + 300000))
    lim = random.choice([None, None, random.randint(1, 50000)])
    st, hd, b, _ = request(tp("file_known"), "bytes=%d-%d" % (s, e), read_limit=lim)
    if st != 206 or b != DATA[s:s + len(b)] or (lim is None and len(b) != e - s + 1): bad += 1
check("40 random ranges with random early closes (bytes compared)", bad == 0, "bad %d" % bad)
# R1: HEAD while the size is unknown opens an upstream; it is parked; a GET adopts it
st, hd, b, _ = request(tp("headfirst"), method="HEAD")
check("R1 HEAD on unknown size", st == 200 and hd.get("content-length") == str(SIZE))
st, hd, b, _ = request(tp("headfirst"))
check("R1 GET after HEAD: whole file, bytes compared", st == 200 and b == DATA,
      "len %d, first diff at %s" % (len(b), next((i for i in range(min(len(b), SIZE)) if b[i] != DATA[i]), None)))
elapsed = time.time() - t0
time.sleep(1.0)
px.send_signal(signal.SIGTERM)
px.wait(timeout=20)
logf.close()
txt = open(LOG).read()
check("proxy exited cleanly (exit 0)", px.returncode == 0, "rc %s" % px.returncode)
check("no ASan/UBSan report", "ERROR: AddressSanitizer" not in txt and "runtime error" not in txt and "LeakSanitizer" not in txt)
stats = [l for l in txt.splitlines() if l.startswith("STATS")]
print(stats[0] if stats else "no stats")
check("no connection left open at exit", stats and stats[0].endswith("active 0"))
https_gets = sum(1 for x in upstream_log if x[0] == P2)
print("upstream requests: %d (https %d); elapsed %.1f s" % (len(upstream_log), https_gets, elapsed))
n_ok = sum(1 for _, ok in results if ok)
print("E2E: %d passed, %d failed" % (n_ok, len(results) - n_ok))
plain.shutdown(); tls.shutdown()
sys.exit(0 if n_ok == len(results) else 1)
