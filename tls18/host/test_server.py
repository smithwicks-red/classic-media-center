#!/usr/bin/env python3
"""HOST TEST ONLY. Local HTTPS/HTTP servers for run_host_tests.sh.

usage: test_server.py CERTDIR BASEPORT
  BASEPORT+0  TLS 1.2/1.3, cert for localhost (trusted by test CA)
  BASEPORT+1  TLS 1.2 only, same cert
  BASEPORT+2  TLS 1.2/1.3, cert for wrong.example (trusted CA, wrong host)
  BASEPORT+3  TLS 1.2/1.3, self-signed localhost cert (untrusted)
  BASEPORT+4  plain HTTP
"""
import os, sys, ssl, socket, threading, time, hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

CERTDIR, BASE = sys.argv[1], int(sys.argv[2])
BIG = 20 * 1024 * 1024


def pattern(n, off=0):
    # deterministic bytes: byte i = (i*7 + i//251) & 0xff
    return bytes(((i * 7 + i // 251) & 0xFF) for i in range(off, off + n))


RANGE_RES = pattern(1000)


class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *a):
        pass

    def send_body(self, code, body, ctype="text/plain", extra=()):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        for k, v in extra:
            self.send_header(k, v)
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        p = self.path
        port = self.server.server_address[1]
        if p == "/small":
            self.send_body(200, b"hello from the test server\n" * 3)
        elif p == "/chunked":
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Transfer-Encoding", "chunked")
            self.send_header("Connection", "close")
            self.end_headers()
            parts = [b"chunk-one;", b"x" * 5000, b"", b"tail"]
            for i, c in enumerate(parts):
                if not c:
                    continue
                ext = ";name=val" if i == 1 else ""
                self.wfile.write(b"%x%s\r\n" % (len(c), ext.encode()) + c + b"\r\n")
                self.wfile.flush()
                time.sleep(0.02)
            self.wfile.write(b"0\r\nX-Trailer: yes\r\n\r\n")
        elif p == "/big":
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(BIG))
            self.send_header("Connection", "close")
            self.end_headers()
            blk = 1 << 16
            for off in range(0, BIG, blk):
                self.wfile.write(BIGDATA[off:off + blk])
        elif p == "/range":
            rng = self.headers.get("Range")
            if rng and rng.startswith("bytes="):
                a, _, b = rng[6:].partition("-")
                a = int(a)
                b = int(b) if b else len(RANGE_RES) - 1
                body = RANGE_RES[a:b + 1]
                self.send_body(206, body, "application/octet-stream",
                               [("Content-Range", "bytes %d-%d/%d" % (a, b, len(RANGE_RES)))])
            else:
                self.send_body(200, RANGE_RES, "application/octet-stream")
        elif p == "/redir-rel":
            self.send_body(302, b"", extra=[("Location", "sub/../small")])
        elif p == "/sub/../small" or p == "/sub/small":
            self.send_body(200, b"relative ok\n")
        elif p == "/go":   # served on the plain port: -> https -> relative
            self.send_body(301, b"moved", extra=[("Location", "https://localhost:%d/redir-rel" % BASE)])
        elif p == "/loop":
            self.send_body(302, b"", extra=[("Location", "/loop")])
        elif p == "/cut":
            self.send_response(200)
            self.send_header("Content-Length", "100000")
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(b"z" * 5000)
            self.wfile.flush()
            time.sleep(0.1)
            # abrupt close (no TLS close_notify)
            try:
                self.connection.shutdown(socket.SHUT_RDWR)
            except Exception:
                pass
            self.close_connection = True
        elif p == "/hang":
            self.send_response(200)
            self.send_header("Content-Length", "1000")
            self.end_headers()
            self.wfile.write(b"h" * 10)
            self.wfile.flush()
            time.sleep(20)
        else:
            self.send_body(404, b"not found\n")


BIGDATA = None


def serve(port, ctx):
    srv = ThreadingHTTPServer(("127.0.0.1", port), H)
    srv.daemon_threads = True
    if ctx is not None:
        srv.socket = ctx.wrap_socket(srv.socket, server_side=True)
    threading.Thread(target=srv.serve_forever, daemon=True).start()


def mkctx(cert, key, maxv=None):
    c = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    c.load_cert_chain(os.path.join(CERTDIR, cert), os.path.join(CERTDIR, key))
    c.minimum_version = ssl.TLSVersion.TLSv1_2
    if maxv:
        c.maximum_version = maxv
    return c


if __name__ == "__main__":
    BIGDATA = (pattern(65536) * (BIG // 65536 + 1))[:BIG]
    with open(os.path.join(CERTDIR, "big.sha256"), "w") as f:
        f.write(hashlib.sha256(BIGDATA).hexdigest() + "\n")
    with open(os.path.join(CERTDIR, "range.bin"), "wb") as f:
        f.write(RANGE_RES)
    serve(BASE + 0, mkctx("srv.pem", "srv.key"))
    serve(BASE + 1, mkctx("srv.pem", "srv.key", ssl.TLSVersion.TLSv1_2))
    serve(BASE + 2, mkctx("wrong.pem", "wrong.key"))
    serve(BASE + 3, mkctx("self.pem", "self.key"))
    serve(BASE + 4, None)
    print("ready", flush=True)
    while True:
        time.sleep(3600)
