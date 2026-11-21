"""HOST TEST ONLY. A fake archive.org for mc02 tests.

usage: mc02_server.py CERTDIR CLIPDIR BASEPORT
  BASE+0 TLS (cert for localhost, test CA)   BASE+1 plain http
  BASE+2 TLS with a self-signed cert (untrusted)
Item identifiers choose the behaviour:
  httpok    plain download chain stays http          (base.mp4)
  httpsonly plain chain 301s to https (like *.ca.)   (base.mp4)
  moovend   moov at the end (Range path)            (end.mp4)
  high      High profile                            (high.mp4)
  changed   http serves base.mp4, https other.mp4   (different moov)
  cutdrop   https drops the connection at ~600 KB
  slow      https sends slowly (for cancel)
  big       19.5 MB file                             (big.mp4)
(0.3) search: the q= is title:(...) or (...); the first word picks the
item, except: a query naming "federation" -> JimmyAndTheFederationGang
(synthetic 425-file metadata, every .mp4 serves base.mp4), "jimmyhttps"
-> same files on an https-only chain, "iahigh" -> a .ia.mp4 (h.264 IA)
+ .mp4 (MPEG4) pair, both High; "notitle" -> 0 hits in the title
scope, httpok in the every-field scope. Every q= is logged to
CERTDIR/queries.txt.
"""
import re
import os, sys, ssl, json, threading, time, socket
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs, unquote

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
CERTDIR, CLIPDIR, BASE = sys.argv[1], sys.argv[2], int(sys.argv[3])
CLIPS = {}
for n in ('base', 'end', 'high', 'other', 'big'):
    CLIPS[n] = open(os.path.join(CLIPDIR, n + '.mp4'), 'rb').read()
FILEOF = {'moovend': 'end', 'high': 'high', 'big': 'big', 'iahigh': 'high',
          'bigmoov': 'bigmoov', 'hugemoov': 'hugemoov'}


from moovpad import pad_moov
CLIPS['bigmoov'] = pad_moov(CLIPS['base'], 1750112)     # the Jimmy film's moov
CLIPS['hugemoov'] = pad_moov(CLIPS['base'], 4000000)    # over the 4 MB pre-buffer cap
# (0.4) extra items xb0, xb1, ...: the files listed (comma-separated
# absolute paths) in env MC02_XFILES, read when first asked for; their
# metadata lists one file "<ident>.mp4", format "h.264 IA", no size hint.
XFILES = [x for x in os.environ.get('MC02_XFILES', '').split(',') if x]
for i, x in enumerate(XFILES):
    FILEOF['xb%d' % i] = 'xb%d' % i
HTTPSONLY = ('httpsonly', 'jimmyhttps', 'bigmoov', 'hugemoov')
HERE = os.path.dirname(os.path.abspath(__file__))
FIX = os.path.join(HERE, '..', '..', 'mediacenter', 'fixtures')


def jimmy_meta(ident):
    import subprocess
    out = os.path.join(CERTDIR, 'meta_%s.json' % ident)
    if not os.path.exists(out):
        srv = 'localhost:%d' % (BASE + 1)
        subprocess.check_call([sys.executable, '-I', os.path.join(FIX, 'gen_meta_jimmy.py'),
                               os.path.join(FIX, 'jimmy_files.txt'), out, '700000', srv, '/node/' + ident])
    return open(out, 'rb').read()
COUNT = {}


def clip_for(ident, tls):
    if ident == 'changed':
        return CLIPS['other' if tls else 'base']
    if ident.startswith('xb') and ident in FILEOF and ident not in CLIPS:
        CLIPS[ident] = open(XFILES[int(ident[2:])], 'rb').read()
    return CLIPS[FILEOF.get(ident, 'base')]


class H(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, *a):
        sys.stderr.write('[%d] %s\n' % (self.server.server_address[1], self.requestline))

    def send(self, code, body=b'', ctype='application/json', extra=()):
        self.send_response(code)
        self.send_header('Content-Type', ctype)
        self.send_header('Content-Length', str(len(body)))
        for k, v in extra:
            self.send_header(k, v)
        self.send_header('Connection', 'close')
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        tls = self.server.server_address[1] != BASE + 1
        u = urlparse(self.path)
        parts = [unquote(p) for p in u.path.split('/') if p]
        key = (tls, parts[0] if parts else '')
        COUNT[key] = COUNT.get(key, 0) + 1
        if u.path == '/advancedsearch.php':
            q = parse_qs(u.query)['q'][0]
            open(os.path.join(CERTDIR, 'queries.txt'), 'a').write(q + '\n')
            words = re.findall(r'[A-Za-z0-9]+', q[q.index('(') + 1:q.index(') AND mediatype')])
            term = words[0] if words else ''
            if 'federation' in words:
                term = 'JimmyAndTheFederationGang'
            if term == 'notitle':
                if q.startswith('title:'):
                    return self.send(200, json.dumps({'responseHeader': {'status': 0}, 'response':
                                     {'numFound': 0, 'start': 0, 'docs': []}}).encode())
                term = 'httpok'
            body = {'responseHeader': {'status': 0}, 'response': {'numFound': 1, 'start': 0,
                    'docs': [{'identifier': term, 'title': 'Test film ' + term}]}}
            return self.send(200, json.dumps(body).encode())
        if parts[:1] == ['metadata'] and len(parts) == 2:
            ident = parts[1]
            if ident in ('JimmyAndTheFederationGang', 'jimmyhttps'):
                return self.send(200, jimmy_meta(ident))
            data = clip_for(ident, False)
            srv = 'localhost:%d' % (BASE + 1)
            files = [{'name': ident + '_512kb.mp4', 'source': 'derivative',
                      'format': '512Kb MPEG4', 'size': str(len(data)),
                      'height': '240', 'width': '320'}]
            if ident.startswith('xb') and ident in FILEOF:
                files = [{'name': ident + '.mp4', 'format': 'h.264 IA', 'size': str(len(data))}]
            if ident == 'iahigh':
                files = [{'name': 'clip.mp4', 'format': 'MPEG4', 'size': str(len(data))},
                         {'name': 'clip.ia.mp4', 'format': 'h.264 IA', 'size': str(len(data)), 'title': 'Clip (IA)'}]
            body = {'server': srv, 'd1': srv, 'dir': '/node/' + ident, 'files': files}
            return self.send(200, json.dumps(body).encode())
        if parts[:1] == ['download'] and len(parts) == 3:
            ident, name = parts[1], parts[2]
            if tls:
                loc = 'https://localhost:%d/node/%s/%s' % (BASE, ident, name)
            elif ident in HTTPSONLY:
                loc = 'http://localhost:%d/canode/%s/%s' % (BASE + 1, ident, name)
            else:
                loc = 'http://localhost:%d/node/%s/%s' % (BASE + 1, ident, name)
            return self.send(302, b'', 'text/html', [('Location', loc)])
        if parts[:1] == ['canode']:
            loc = 'https://localhost:%d/node/%s' % (BASE, '/'.join(parts[1:]))
            return self.send(301, b'', 'text/html', [('Location', loc)])
        if parts[:1] == ['node'] and len(parts) == 3:
            ident = parts[1]
            if ident in HTTPSONLY and not tls:
                return self.send(301, b'', 'text/html',
                                 [('Location', 'https://localhost:%d%s' % (BASE, u.path))])
            data = clip_for(ident, tls)
            a, b, code = 0, len(data) - 1, 200
            rng = self.headers.get('Range')
            if rng and rng.startswith('bytes='):
                x, _, y = rng[6:].partition('-')
                a = int(x); b = int(y) if y else len(data) - 1; code = 206
            body = data[a:b + 1]
            self.send_response(code)
            self.send_header('Content-Type', 'video/mp4')
            self.send_header('Content-Length', str(len(body)))
            if code == 206:
                self.send_header('Content-Range', 'bytes %d-%d/%d' % (a, b, len(data)))
            self.send_header('Connection', 'close')
            self.end_headers()
            blk = 16384
            try:
                for off in range(0, len(body), blk):
                    if tls and ident == 'cutdrop' and a == 0 and off >= 600000:
                        self.connection.shutdown(socket.SHUT_RDWR)
                        self.close_connection = True
                        return
                    self.wfile.write(body[off:off + blk])
                    if tls and ident == 'slow' and a == 0:
                        time.sleep(0.01)
            except (BrokenPipeError, ConnectionResetError, ssl.SSLError):
                pass
            return
        if u.path == '/count':
            return self.send(200, json.dumps({'%s:%s' % k: v for k, v in COUNT.items()}).encode())
        self.send(404, b'not found\n', 'text/plain')


def serve(port, cert):
    srv = ThreadingHTTPServer(('127.0.0.1', port), H)
    srv.daemon_threads = True
    if cert:
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.load_cert_chain(os.path.join(CERTDIR, cert + '.pem'), os.path.join(CERTDIR, cert + '.key'))
        srv.socket = ctx.wrap_socket(srv.socket, server_side=True)
    threading.Thread(target=srv.serve_forever, daemon=True).start()


serve(BASE, 'srv')
serve(BASE + 1, None)
serve(BASE + 2, 'self')
print('ready', flush=True)
while True:
    time.sleep(3600)
