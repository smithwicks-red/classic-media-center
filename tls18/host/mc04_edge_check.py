# mc04_edge_check.py ORIG OUT_URL OUT_DL PYREF  (0.4, HOST ONLY)
# OUT_URL = the reference moov (url mode), OUT_DL = FILE relabelled + the
# appended download moov. Checks: url moov == download moov except 'dref';
# download file vs the Python reference rewrite (compare.py). Prints JSON.
import sys, json, struct, os
sys.path.insert(0, '/home/claude/s5/rw/sim')
import compare
CONT = {b'moov', b'trak', b'mdia', b'minf', b'dinf', b'stbl', b'edts', b'udta'}
def leaves(b, off, end, out):
    while off + 8 <= end:
        n, t = struct.unpack('>I4s', b[off:off+8])
        if n < 8: break
        if t in CONT: leaves(b, off + 8, off + n, out)
        elif t != b'dref': out.append((t, bytes(b[off+8:off+n])))
        off += n
    return out
o, u, d, py = sys.argv[1:5]
ol = os.path.getsize(o); s = open(u, 'rb').read(); t = open(d, 'rb').read()[ol:]
r = dict(file=os.path.basename(o), url_eq_dl_but_dref=leaves(s, 8, len(s), []) == leaves(t, 8, len(t), []))
if os.path.exists(py):
    diffs, st = compare.compare(o, py, d, ol, len(t), 0)
    r.update(tables_equal=not diffs, diffs=diffs, N=st['N'], av_orig_ms=st['av_orig_ms'], av_ms=st['av_c_ms'])
else:
    r.update(tables_equal=None, note='no Python reference')
print(json.dumps(r))
