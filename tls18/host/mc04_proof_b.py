# mc04_proof_b.py -- HOST ONLY (0.4). For each B-frame file: the streaming
# reference movie and the downloaded local movie that the REAL 0.4 app path
# produced (run_mc04_tests.sh, via mc02_host + mc02_server.py), checked with
# the rewriter's tooling (read-only, /home/claude/s5/rw/sim):
#   * local movie layout: original bytes with ftyp/moov relabelled 'free',
#     then the appended moov ("tail")
#   * stream moov == tail except the 'dref' box (url vs self-reference)
#   * compare.compare vs the Python reference rewrite (pyout/A_<f>): video
#     tables, chunk/sample offsets, sample bytes, other tracks, A/V start
#   * qt6.play design 'A' over the real AVCDecLib: wrong pictures, lost at end
#     The stream result is a reference movie (media at the URL); the
#     equivalent self-contained file = original bytes relabelled + the
#     stream moov appended (offsets are absolute and unchanged, so the
#     samples it points at are the same bytes the URL would serve).
# usage: python3 mc04_proof_b.py ORIGDIR RESDIR  (RESDIR has <f>.stream.mov,
#        <f>.local.mov); prints one JSON line per file and a summary.
import sys, os, json, struct
sys.path.insert(0, '/home/claude/s5/rw/sim')
import compare, qt6

ORIG, RES = sys.argv[1], sys.argv[2]
PY = '/home/claude/s5/rw/pyout'; REF = '/home/claude/s5/sim/ref'
CONT = {b'moov', b'trak', b'mdia', b'minf', b'dinf', b'stbl', b'edts', b'udta'}

def leaves(b, off, end, path, out):
    while off + 8 <= end:
        n, t = struct.unpack('>I4s', b[off:off+8])
        if n < 8: break
        if t in CONT: leaves(b, off + 8, off + n, path + [t], out)
        elif t != b'dref': out.append((tuple(path + [t]), bytes(b[off+8:off+n])))
        off += n
    return out

def relabel(orig):
    b = bytearray(orig); p = 0
    while p + 8 <= len(b):
        n = struct.unpack('>I', b[p:p+4])[0]
        if n == 1: n = struct.unpack('>Q', b[p+8:p+16])[0]
        if n < 8: break
        if b[p+4:p+8] in (b'ftyp', b'moov'): b[p+4:p+8] = b'free'
        p += n
    return bytes(b)

def check(f):
    o = open(f'{ORIG}/{f}', 'rb').read()
    s = open(f'{RES}/{f}.stream.mov', 'rb').read()
    l = open(f'{RES}/{f}.local.mov', 'rb').read()
    rel = relabel(o); tail = l[len(o):]
    r = dict(file=f, layout=(l[:len(o)] == rel and struct.unpack('>I', tail[:4])[0] == len(tail)
                             and tail[4:8] == b'moov'),
             stream_eq_tail_but_dref=(leaves(s, 8, len(s), [], []) == leaves(tail, 8, len(tail), [], [])),
             stream_moov=len(s), tail=len(tail))
    eq = f'{RES}/{f}.streamequiv.mov'
    open(eq, 'wb').write(rel + s)
    times, md, end = qt6.reference(f'{ORIG}/{f}', f'{REF}/{f}.md5')
    for tag, path, ml in (('local', f'{RES}/{f}.local.mov', len(tail)), ('stream', eq, len(s))):
        diffs, st = compare.compare(f'{ORIG}/{f}', f'{PY}/A_{f}', path, len(o), ml, 0)
        N = compare.info(path)[4]['rdly']
        rec = qt6.play(path, 'A', N, times, md, end)[0]
        sc = qt6.score(rec, times, md, end)
        r[tag] = dict(tables_equal=not diffs, diffs=diffs, N=N, av_orig_ms=st['av_orig_ms'],
                      av_ms=st['av_c_ms'], wrong=sc['wrong'], lost_end=sc['lost_end'],
                      off_mean=round(sc['off_mean'], 3), off_max=round(sc['off_max'], 3))
    os.remove(eq)
    return r

files = sorted(x for x in os.listdir(ORIG) if os.path.exists(f'{RES}/{x}.local.mov'))
rows = []
for f in files:
    r = check(f); rows.append(r); print(json.dumps(r), flush=True)
def ok(r, t): return r[t]['tables_equal'] and r[t]['wrong'] == 0 and r[t]['lost_end'] == 0 \
    and (r[t]['av_orig_ms'] is None or abs(r[t]['av_ms'] - r[t]['av_orig_ms']) < 1e-6)
print('SUMMARY files=%d layout=%d stream==tail(but dref)=%d tables_equal local=%d stream=%d '
      'qt6 ok local=%d stream=%d wrong=%d lost_end=%d max|av-av_orig|=%.3f ms max|off|=%.3f ms' % (
      len(rows), sum(r['layout'] for r in rows), sum(r['stream_eq_tail_but_dref'] for r in rows),
      sum(r['local']['tables_equal'] for r in rows), sum(r['stream']['tables_equal'] for r in rows),
      sum(ok(r, 'local') for r in rows), sum(ok(r, 'stream') for r in rows),
      sum(r[t]['wrong'] for r in rows for t in ('local', 'stream')),
      sum(r[t]['lost_end'] for r in rows for t in ('local', 'stream')),
      max(abs((r[t]['av_ms'] or 0) - (r[t]['av_orig_ms'] or 0)) for r in rows for t in ('local', 'stream')),
      max(r[t]['off_max'] for r in rows for t in ('local', 'stream'))))
