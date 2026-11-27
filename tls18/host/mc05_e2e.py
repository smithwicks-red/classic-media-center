"""HOST TEST ONLY (J5). Media Center 0.5s https STREAMING end to end, the
whole app flow on Linux: mc05_host (mc_fetch + mc_stream + mc_proxy_ot over
the fake OT listener + mc_proxy + https_client/BearSSL) against
mc02_server.py (plays archive.org: the "httpsonly" item's plain chain 301s
to https like *.ca.archive.org storage nodes). Python plays QuickTime
Player 6: it reads the dref out of the reference movie the app wrote and
fetches it with QuickTime's exact request form while the app's serve loop
polls.
usage: mc05_e2e.py BIN CLIPDIR WORKDIR [BASEPORT] [PROXYPORT]
"""
import hashlib, os, re, socket, subprocess, sys, threading, time

BIN, CLIPS, W = sys.argv[1], sys.argv[2], sys.argv[3]
B = int(sys.argv[4]) if len(sys.argv) > 4 else 19643
PP = int(sys.argv[5]) if len(sys.argv) > 5 else 18080
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from moovpad import pad_moov
os.makedirs(W, exist_ok=True)
SRC = open(os.path.join(CLIPS, 'base.mp4'), 'rb').read()
SHA = hashlib.sha256(SRC).hexdigest()
QT_UA = b'QuickTime (qtver=6.0.2;cpu=PPC;os=Mac 9.2.2)'
PASS = FAIL = 0
SAN = 0


def ok(c, msg):
    global PASS, FAIL
    if c:
        PASS += 1
        print('PASS', msg)
    else:
        FAIL += 1
        print('FAIL', msg)
    sys.stdout.flush()
    return c


def sh(cmd):
    subprocess.run(cmd, shell=True, cwd=W, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)


def certs():
    if os.path.exists(os.path.join(W, 'srv.pem')):
        return
    sh('openssl ecparam -name prime256v1 -genkey -noout -out ca.key')
    sh('openssl req -x509 -new -key ca.key -sha256 -days 30 -subj "/CN=mc05 test CA" '
       '-addext "basicConstraints=critical,CA:TRUE" -addext "keyUsage=critical,keyCertSign,cRLSign" -out ca.pem')
    sh('openssl x509 -in ca.pem -outform DER -out ca.der')
    for n, how in (('srv', 'ca'), ('self', 'self')):
        sh('openssl ecparam -name prime256v1 -genkey -noout -out %s.key' % n)
        sh('openssl req -new -key %s.key -subj "/CN=localhost" -out %s.csr' % (n, n))
        open(os.path.join(W, n + '.ext'), 'w').write(
            'subjectAltName=DNS:localhost,IP:127.0.0.1\nbasicConstraints=CA:FALSE\n'
            'extendedKeyUsage=serverAuth\nkeyUsage=digitalSignature\n')
        if how == 'self':
            sh('openssl x509 -req -in %s.csr -signkey %s.key -sha256 -days 30 -extfile %s.ext -out %s.pem' % (n, n, n, n))
        else:
            sh('openssl x509 -req -in %s.csr -CA ca.pem -CAkey ca.key -CAcreateserial -sha256 -days 30 '
               '-extfile %s.ext -out %s.pem' % (n, n, n))


class App:
    """one mc05_host run; stdout lines collected with their times"""
    n = 0

    def __init__(self, args, env=None):
        App.n += 1
        self.dir = os.path.join(W, 'run%02d' % App.n)
        os.makedirs(self.dir, exist_ok=True)
        self.ctl = os.path.join(self.dir, 'ctl.txt')
        self.qte = os.path.join(self.dir, 'qt_events.txt')
        open(self.ctl, 'w').close()
        e = dict(os.environ)
        e.update({'MC_API': 'https://localhost:%d' % B, 'MC_DL': 'http://localhost:%d' % (B + 1),
                  'MC_HTTP_PORT': str(B + 1), 'MC_HTTPS_PORT': str(B), 'OUT_DIR': self.dir,
                  'FAKE_MAC_DIR': self.dir, 'HOST_TEST_CA': os.path.join(W, 'ca.der'),
                  'MC_CTL': self.ctl, 'MC_QT_EVENTS': self.qte, 'MC_PROXY_PORT': str(PP),
                  'ASAN_OPTIONS': 'detect_leaks=1', 'UBSAN_OPTIONS': 'print_stacktrace=1:halt_on_error=1'})
        e.update(env or {})
        self.err = open(os.path.join(self.dir, 'stderr.txt'), 'w')
        self.p = subprocess.Popen([BIN] + args, stdout=subprocess.PIPE, stderr=self.err, env=e)
        self.lines = []
        self.t0 = time.time()
        threading.Thread(target=self._rd, daemon=True).start()

    def _rd(self):
        out = open(os.path.join(self.dir, 'stdout.txt'), 'w')   # (J5b) kept for the logs
        for raw in self.p.stdout:
            l = raw.decode('latin-1').rstrip('\n')
            self.lines.append((time.time() - self.t0, l))
            out.write('%8.3f %s\n' % (self.lines[-1][0], l))
            out.flush()

    def wait_for(self, pat, timeout=30):
        r = re.compile(pat)
        end = time.time() + timeout
        while time.time() < end:
            for t, l in list(self.lines):
                m = r.search(l)
                if m:
                    return m, t
            if self.p.poll() is not None and not any(r.search(l) for _, l in list(self.lines)):
                time.sleep(0.2)
                if not any(r.search(l) for _, l in list(self.lines)):
                    return None, None
            time.sleep(0.05)
        return None, None

    def has(self, pat):
        return any(re.search(pat, l) for _, l in self.lines)

    def first(self, pat):
        for i, (t, l) in enumerate(self.lines):
            if re.search(pat, l):
                return i, t
        return None, None

    def count(self, pat):
        return sum(1 for _, l in self.lines if re.search(pat, l))

    def cmd(self, c):
        with open(self.ctl, 'a') as f:
            f.write(c + '\n')

    def finish(self, timeout=20):
        global SAN
        try:
            rc = self.p.wait(timeout)
        except subprocess.TimeoutExpired:
            self.p.kill()
            rc = 'killed'
        time.sleep(0.2)
        self.err.close()
        open(os.path.join(self.dir, 'stdout.txt'), 'w').write('\n'.join('%8.3f %s' % x for x in self.lines) + '\n')
        e = open(os.path.join(self.dir, 'stderr.txt')).read()
        if 'AddressSanitizer' in e or 'runtime error' in e or 'LeakSanitizer' in e:
            SAN += 1
            print('SANITIZER in', self.dir)
            print(e[:3000])
        return rc

    def dref(self):
        m, _ = self.wait_for(r'^OPEN_REF (.*) \d+$', 60)
        if not m:
            return None, None
        data = open(m.group(1), 'rb').read()
        d = re.search(rb'http://127\.0\.0\.1:\d+/[\x21-\x7e]+', data)
        return m.group(1), d.group(0).decode() if d else None


def split(url):
    m = re.match(r'http://127\.0\.0\.1:(\d+)(/.*)$', url)
    return int(m.group(1)), m.group(2)


def qt_get(url, rng=b'bytes=0-', stop_after=None, pause_after=None, pause_evt=None, close_evt=None, rcvbuf=16384):
    """QuickTime 6.0.2's request, measured in the VM. Returns (status, headers, body, how)"""
    port, path = split(url)
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, rcvbuf)
    s.connect(('127.0.0.1', port))
    req = b'GET ' + path.encode() + b' HTTP/1.1\r\nHost: 127.0.0.1\r\n'
    if rng:
        req += b'Range: ' + rng + b'\r\n'
    req += b'Accept: */*\r\nUser-Agent: ' + QT_UA + b'\r\n\r\n'
    s.sendall(req)
    s.settimeout(30)
    buf = b''
    how = 'eof'
    try:
        while b'\r\n\r\n' not in buf:
            d = s.recv(4096)
            if not d:
                break
            buf += d
        head, _, body = buf.partition(b'\r\n\r\n')
        lines = head.decode('latin-1').split('\r\n')
        status = int(lines[0].split()[1]) if lines and len(lines[0].split()) > 1 else 0
        hdr = {}
        for l in lines[1:]:
            k, _, v = l.partition(':')
            hdr[k.strip().lower()] = v.strip()
        while True:
            if stop_after is not None and len(body) >= stop_after:
                how = 'client-closed'
                break
            if pause_after is not None and len(body) >= pause_after and pause_evt is not None:
                pause_evt[0].set()             # tell the test we are paused
                while not pause_evt[1].is_set():
                    if close_evt is not None and close_evt.is_set():
                        how = 'closed-on-event'
                        s.close()
                        return status, hdr, body, how
                    time.sleep(0.02)
                pause_after = None
            if close_evt is not None and close_evt.is_set():
                how = 'closed-on-event'
                break
            d = s.recv(4096)
            if not d:
                break
            body += d
    except (ConnectionResetError, socket.timeout) as e:
        how = 'reset' if isinstance(e, ConnectionResetError) else 'timeout'
    s.close()
    return status, hdr, body, how


def good(r, start=0, src=None):
    st, h, body, how = r
    SRC_ = SRC if src is None else src
    want = SRC_[start:]
    return st == 206 and body == want and hashlib.sha256(body).hexdigest() == hashlib.sha256(want).hexdigest() \
        and h.get('content-length') == str(len(want)) \
        and h.get('content-range') == 'bytes %d-%d/%d' % (start, len(SRC_) - 1, len(SRC_))


RETRIES = [0]


def start_stream(env=None, extra=()):
    # (0.6.1) host flake, also in the 0.6 base: now and then the proxy's
    # FIRST upstream request to the test server is closed within ~4 ms (502),
    # twice in a row, and Play correctly falls back to a download (no
    # reference movie). Start the app again (max 3 times; counted and
    # printed) ONLY when the app log shows that 502. (F1) Any other missing
    # reference movie (a real fallback, e.g. a -3 misfire) fails at once.
    for attempt in range(3):
        a = App(['httpsonly'] + list(extra), env)
        path, url = a.dref()
        if path is not None and url is not None:
            return a, path, url
        sig = [l for _, l in list(a.lines) if re.search(r'\b502\b', l)]
        if not sig:
            ok(False, 'start_stream: no reference movie and no upstream 502 in the app log (not the flake): %s'
               % ((a.lines[-1][1] if a.lines else 'no output')[:120]))
            a.finish(30)
            print('mc05 e2e: %d passed, %d failed (%d flake restarts) -- stopped: real fallback' % (PASS, FAIL, RETRIES[0]))
            sys.exit(1)
        RETRIES[0] += 1
        print('NOTE start_stream: no reference movie (attempt %d, restart #%d): upstream 502 flake (%s), starting again'
              % (attempt + 1, RETRIES[0], sig[0][:100]))
        a.finish(30)
    return a, path, url


def main():
    certs()
    srv = subprocess.Popen([sys.executable, '-I', os.path.join(HERE, 'mc02_server.py'), W, CLIPS, str(B)],
                           stdout=subprocess.PIPE, stderr=open(os.path.join(W, 'server.log'), 'w'))
    srv.stdout.readline()
    try:
        tests()
    finally:
        srv.kill()
    print('mc05 e2e: %d passed, %d failed, sanitizer reports %d (%d flake restarts)' % (PASS, FAIL, SAN, RETRIES[0]))
    sys.exit(1 if FAIL or SAN else 0)


def paused_fetch(url, close_on_event=None):
    pe = (threading.Event(), threading.Event())
    res = {}

    def run():
        res['r'] = qt_get(url, pause_after=65536, pause_evt=pe, close_evt=close_on_event)
    th = threading.Thread(target=run, daemon=True)
    th.start()
    pe[0].wait(20)
    return th, pe, res


def watch_events(app, evt):
    def run():
        while not evt.is_set():
            if os.path.exists(app.qte) and 'close ' in open(app.qte).read():
                evt.set()
                return
            time.sleep(0.02)
            if app.p.poll() is not None:
                return
    threading.Thread(target=run, daemon=True).start()


def tests():
    # ---- A: the main flow ----
    a, path, url = start_stream({'MC_CPU_G3': '1', 'MC_G3_KBS': '1'})
    ok(a.has(r'RESULT rc=0 mode=2 '), 'A1 https-only item -> mode PROXY (2)')
    ok(a.has(r'insists on https: Media Center streams it') and a.has('proxy: LISTENING 127.0.0.1:%d' % PP),
       'A2 log: proxy streaming, listener on 127.0.0.1:%d' % PP)
    ok(path is not None and os.path.basename(path) == 'httpsonly_512kb (MC).mov' and len(os.path.basename(path)) <= 31,
       'A3 reference movie name "%s" (<= 31)' % (path and os.path.basename(path)))
    ok(url == 'http://127.0.0.1:%d/a/localhost/download/httpsonly/httpsonly_512kb.mp4' % PP,
       'A4 dref = %s' % url)
    ok(a.wait_for('SERVE_LOOP start', 10)[0] is not None, 'A5 app is in its serve loop')
    ok(a.has('G3 HINT: this film averages') and a.has(r'\[status\] G3 HINT'), 'A6 G3 hint logged + status (MC_CPU_G3=1, 1 KB/s threshold)')
    if url:
        r = qt_get(url)
        ok(good(r), 'A7 QuickTime request -> 206, %d bytes, sha256 == source (%s)' % (len(r[2]), r[3]))
        ok(r[1].get('content-type', '').startswith('video/') and r[1].get('accept-ranges') == 'bytes'
           and r[1].get('connection') == 'close', 'A8 head: Content-Type %s, Accept-Ranges, Connection: close'
           % r[1].get('content-type'))
        r = qt_get(url + '?t=2')
        ok(good(r), 'A9 "?t=2" -> same film, sha256 ok')
        res = [None, None]
        ths = [threading.Thread(target=lambda i=i: res.__setitem__(i, qt_get(url))) for i in range(2)]
        [t.start() for t in ths]
        [t.join(60) for t in ths]
        ok(all(x and good(x) for x in res), 'A10 two connections at once: both whole and right')
        r = qt_get(url, stop_after=200000)
        ok(r[0] == 206 and r[2] == SRC[:len(r[2])], 'A11 client closes mid-body after %d bytes' % len(r[2]))
        time.sleep(0.3)
        r = qt_get(url, rng=b'bytes=1000000-')
        ok(good(r, 1000000), 'A12 then a seek-style Range 1000000- -> 206, right bytes')
        r = qt_get(url)
        ok(good(r), 'A13 and the whole film again')
        r = qt_get('http://127.0.0.1:%d/a/evil.example/x.mp4' % PP)
        ok(r[0] == 404, 'A14 /a/ for a host not allowed -> %d' % r[0])
    a.cmd('esc')
    time.sleep(1)
    ok(a.has('ESC: cancel/back only') and a.p.poll() is None, 'A15 Esc does not quit')
    if url:
        ok(good(qt_get(url)), 'A16 still serving after Esc')
    a.cmd('quit')
    rc = a.finish()
    i1, _ = a.first('QT_CLOSE_EVENT httpsonly_512kb \\(MC\\).mov')
    ok(rc == 0 and i1 is not None and a.has('^EXIT') and not a.has('QUIT: QuickTime Player is playing'),
       'A17 Quit while NOT serving: no warning, movie closed in QuickTime first, clean exit (rc %s)' % rc)

    # ---- B: Stop, QuickTime does NOT close the movie: cut after ~2 s ----
    a, path, url = start_stream()
    th, pe, res = paused_fetch(url)
    time.sleep(0.5)
    a.cmd('stop')
    th_ok = a.wait_for('STOP: cancelling', 10)[0] is not None
    pe[1].set()
    th.join(30)
    rc = a.finish()
    ie, te = a.first('QT_CLOSE_EVENT')
    ic, tc = a.first('STOP: cancelling')
    r = res.get('r')
    ok(th_ok and ie is not None and ic is not None and ie < ic, 'B1 Stop: QuickTime close event logged BEFORE the cancel')
    ok(te is not None and tc is not None and 1.7 <= tc - te <= 4.0, 'B2 waited %.2f s for QuickTime, then cut' % ((tc or 0) - (te or 0)))
    ok(a.has('did not close the film within 2 s'), 'B3 log says QuickTime did not close it')
    ok(r is not None and len(r[2]) < len(SRC) and r[2] == SRC[:len(r[2])], 'B4 QuickTime side cut short (%s, %d bytes)' % (r and r[3], r and len(r[2])))
    ok(rc == 0, 'B5 hook run quits after Stop, rc %s' % rc)

    # ---- C: Stop, QuickTime obeys (closes on the event) ----
    a, path, url = start_stream()
    ce = threading.Event()
    watch_events(a, ce)
    th, pe, res = paused_fetch(url, close_on_event=ce)
    time.sleep(0.5)
    a.cmd('stop')
    a.wait_for('STOP: cancelling', 10)
    pe[1].set()
    th.join(30)
    rc = a.finish()
    ie, te = a.first('QT_CLOSE_EVENT')
    ic, tc = a.first('STOP: cancelling')
    ok(ie is not None and ic is not None and ie < ic and a.has('let go of the film') and tc - te < 1.5,
       'C1 Stop: QuickTime closed on the event, no cut needed (%.2f s)' % ((tc or 0) - (te or 0)))
    ok(res.get('r') and res['r'][3] == 'closed-on-event' and rc == 0, 'C2 clean exit rc %s' % rc)

    # ---- D: Quit while serving: needs a second Quit ----
    a, path, url = start_stream()
    th, pe, res = paused_fetch(url)
    time.sleep(0.5)
    a.cmd('quit')
    w = a.wait_for('QUIT: QuickTime Player is playing', 10)[0] is not None
    time.sleep(1.0)
    ok(w and a.p.poll() is None and not a.has('QT_CLOSE_EVENT') and a.has(r'\[status\] QuickTime Player is playing'),
       'D1 first Quit while serving: status-line warning only, still running')
    pe[1].set()                       # QuickTime reads on: still served
    time.sleep(0.3)
    a.cmd('quit')
    rc = a.finish(20)
    th.join(30)
    ie, te = a.first('QT_CLOSE_EVENT')
    ic, tc = a.first('STOP: cancelling')
    ok(ie is not None and ic is not None and ie < ic and rc == 0 and a.has('^EXIT'),
       'D2 second Quit within 10 s: close event first, then cancel, exit rc %s' % rc)
    r = res.get('r')
    ok(r is not None and r[2] == SRC[:len(r[2])] and len(r[2]) > 65536,
       'D3 QuickTime kept getting right bytes between the Quits (%d bytes, %s)' % (r and len(r[2]), r and r[3]))

    # ---- E: the second Quit must come within 10 s ----
    a, path, url = start_stream()
    th, pe, res = paused_fetch(url)
    time.sleep(0.5)
    a.cmd('quit')
    a.wait_for('QUIT: QuickTime Player is playing', 10)
    time.sleep(10.8)
    a.cmd('quit')
    time.sleep(1.0)
    n = a.count('QUIT: QuickTime Player is playing')
    ok(n == 2 and a.p.poll() is None and not a.has('QT_CLOSE_EVENT'), 'E1 a Quit 10.8 s later warns again (%d warnings)' % n)
    a.cmd('quit')
    rc = a.finish(20)
    pe[1].set()
    th.join(30)
    ok(rc == 0 and a.has('QT_CLOSE_EVENT'), 'E2 then the next Quit closes and quits')

    # ---- F/G: Download first, listener failure ----
    a = App(['httpsonly', 'download'])
    rc = a.finish(120)
    ok(rc == 0 and a.has('RESULT rc=0 mode=1') and a.has('OPEN_LOCAL') and not a.has('LISTENING'),
       'F1 "Download first" -> mode DOWNLOAD (1), no listener')
    a = App(['httpsonly'], {'MC_PROXY_FAIL': '1'})
    rc = a.finish(120)
    ok(rc == 0 and a.has('RESULT rc=0 mode=1') and a.has('proxy is not available') and a.has('OPEN_LOCAL'),
       'G1 listener cannot open -> falls back to download')

    # ---- H/I: port fallback ----
    hold = socket.socket()
    hold.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    hold.bind(('127.0.0.1', PP))
    hold.listen(1)
    a, path, url = start_stream()
    ok(url is not None and url.startswith('http://127.0.0.1:%d/a/' % (PP + 1)) and a.has('127.0.0.1:%d is taken' % PP),
       'H1 %d taken -> listener on %d, dref %s' % (PP, PP + 1, url))
    ok(url is not None and good(qt_get(url)), 'H2 fetch through the fallback port')
    a.cmd('quit')
    a.finish()
    hold.close()
    holds = []
    for p in range(PP, PP + 10):
        h = socket.socket()
        h.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        h.bind(('127.0.0.1', p))
        h.listen(1)
        holds.append(h)
    a, path, url = start_stream()
    port = split(url)[0] if url else 0
    ok(url is not None and not (PP <= port < PP + 10) and a.has('OT-assigned -> 0'),
       'I1 all 10 ports taken -> OT-assigned port %d' % port)
    ok(url is not None and good(qt_get(url)), 'I2 fetch through the OT-assigned port')
    a.cmd('quit')
    a.finish()
    for h in holds:
        h.close()

    # ---- J: plain http stays plain streaming; no listener ----
    a = App(['httpok'], {'MC_CPU_G3': '1', 'MC_G3_KBS': '1'})
    rc = a.finish(60)
    ok(rc == 0 and a.has('RESULT rc=0 mode=0') and not a.has('LISTENING') and not a.has('SERVE_LOOP')
       and not a.has('G3 HINT'), 'J1 plain-http item -> mode STREAM (0), no proxy, no serve loop')

    # ---- K: hook timers ----
    a, path, url = start_stream(None, ['stop 2'])
    rc = a.finish(20)
    ie, te = a.first('QT_CLOSE_EVENT')
    ok(rc == 0 and a.has('HOOK: stop time') and ie is not None and a.has('STOP: cancelling'), 'K1 hook "stop 2": Stop pressed, quit')
    a, path, url = start_stream(None, ['serve 3'])
    rc = a.finish(20)
    ok(rc == 0 and a.has('HOOK: serve time is up') and a.has('QT_CLOSE_EVENT') and a.has('^EXIT'), 'K2 hook "serve 3": closes the movie, quits')
    # an anchor that does NOT sign the server: only "insecure" gets through
    sh('openssl x509 -in self.pem -outform DER -out other.der')
    a = App(['httpsonly'], {'HOST_TEST_CA': os.path.join(W, 'other.der')})
    rc = a.finish(60)
    ok(not a.has('RESULT rc=0') and not a.has('SERVE_LOOP'), 'K3 untrusted certificate without "insecure": refused, no streaming')
    a = App(['httpsonly', 'insecure'], {'HOST_TEST_CA': os.path.join(W, 'other.der')})
    path, url = a.dref()
    ok(url is not None and good(qt_get(url)), 'K4 "insecure": moov AND the proxy upstream skip the certificate check')
    a.cmd('quit')
    a.finish()


def starve_fetch(app, url, pre=524288):
    """QuickTime connects the moment the movie is opened; record when the
    head + pre-buffer arrived (seconds on the app's clock)"""
    port, path = split(url)
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 65536)
    s.connect(('127.0.0.1', port))
    t_conn = time.time() - app.t0
    s.sendall(b'GET ' + path.encode() + b' HTTP/1.1\r\nHost: 127.0.0.1\r\nRange: bytes=0-\r\n'
              b'Accept: */*\r\nUser-Agent: ' + QT_UA + b'\r\n\r\n')
    s.settimeout(60)
    buf, marks = b'', {}
    while True:
        try:
            d = s.recv(65536)
        except (socket.timeout, ConnectionResetError):
            break
        if not d:
            break
        buf += d
        if b'\r\n\r\n' in buf:
            body = len(buf) - buf.index(b'\r\n\r\n') - 4
            for k in (1, 100000, 524288, pre):
                if body >= k and k not in marks:
                    marks[k] = time.time() - app.t0
    s.close()
    head, _, body = buf.partition(b'\r\n\r\n')
    lines = head.decode('latin-1').split('\r\n')
    hdr = dict((l.partition(':')[0].strip().lower(), l.partition(':')[2].strip()) for l in lines[1:])
    st = int(lines[0].split()[1]) if lines and len(lines[0].split()) > 1 else 0
    return (st, hdr, body, 'eof'), t_conn, marks


BIGMOOV = pad_moov(SRC, 1750112)      # (0.5u) the Jimmy film's moov size (mc02_server item "bigmoov")


def starve_tests():
    # ---- S (J5b): QuickTime's SYNCHRONOUS open starves the event loop ----
    # (0.5u) the pre-buffer is [0, moov end + 512 KB): S/T moov ends at 20619,
    # M (bigmoov) at 1750144 = the measured film (media starts behind 1.75 MB)
    for tag, item, data, env, pre, mend in (
            ('S', 'httpsonly', SRC, {'MC_STARVE_SECS': '6'}, 20619 + 524288, 20619),
            ('T', 'httpsonly', SRC, {'MC_STARVE_SECS': '4', 'MC_PREBUF_TEST': '100000'}, 20619 + 100000, 20619),
            ('M', 'bigmoov', BIGMOOV, {'MC_STARVE_SECS': '10'}, 1750144 + 524288, 1750144)):
        a = App([item], env)
        m, t_ref = a.wait_for(r'^OPEN_REF (.*) \d+$', 60)
        url = None
        if m:
            d = re.search(rb'http://127\.0\.0\.1:\d+/[\x21-\x7e]+', open(m.group(1), 'rb').read())
            url = d.group(0).decode() if d else None
        ib, _ = a.first(r'\[status\] Buffering 0%')
        io_, _ = a.first('^OPEN_REF')
        ok(ib is not None and io_ is not None and ib < io_ and a.has('PREBUF %d of %d bytes in RAM' % (pre, len(data)))
           and a.has('PREBUF %d bytes \\(moov ends at %d\\)' % (pre, mend)),
           '%s1 "Buffering" + PREBUF %d bytes (moov ends at %d) BEFORE the reference movie was written' % (tag, pre, mend))
        if not url:
            ok(False, '%s2 no dref' % tag)
            a.cmd('quit'); a.finish(); continue
        r, t_conn, marks = starve_fetch(a, url, pre)
        _, t_end = a.wait_for('^STARVE end', 20)
        _, t_beg = a.first('^STARVE begin')
        tp = marks.get(pre)
        ok(t_beg is not None and t_end is not None and tp is not None and t_beg <= t_conn and tp < t_end - 1.0,
           '%s2 head + %d pre-buffer bytes arrived %.2f s after connecting, WHILE the event loop was starved '
           '(starve %.2f..%.2f s, got them at %s)' % (tag, pre, (tp or 0) - t_conn, t_beg or -1, t_end or -1, tp))
        ok(good(r, 0, data), '%s3 whole body after the hand-off: 206, %d bytes, sha256 == source' % (tag, len(r[2])))
        ok(a.has(r'n1 slot\d ACCEPT') and a.has(r'n1 slot\d SERVE a=206 b=%d' % pre)
           and a.has('answered at notifier time \\(206, %d of %d body bytes sent\\); continuing at %d' % (pre, len(data), pre)),
           '%s4 log: notifier ACCEPT/SERVE, then mc_proxy adopts the connection at %d' % (tag, pre))
        ok(a.has('adopted the parked upstream at %d \\(skip 0\\)' % pre),
           '%s5 the pre-buffer\'s upstream (parked at %d) continues the body: no new TLS handshake' % (tag, pre))
        a.cmd('quit')
        rc = a.finish()
        ok(rc == 0 and a.has('^EXIT'), '%s6 Quit -> clean exit (rc %s)' % (tag, rc))
    # ---- C (0.5u): the index is bigger than the 4 MB cap -> DOWNLOAD ----
    a = App(['hugemoov'])
    rc = a.finish(120)
    ok(rc == 0 and a.has('index too large to stream') and a.has('RESULT rc=0 mode=1') and a.has('OPEN_LOCAL')
       and not a.has('PREBUF .* of'),
       'C1 moov ends at 4000032 (+512 KB > 4 MB cap): "index too large to stream" -> DOWNLOAD (1), nothing pre-buffered')


_tests0 = tests


def tests():
    _tests0()
    starve_tests()


main()
