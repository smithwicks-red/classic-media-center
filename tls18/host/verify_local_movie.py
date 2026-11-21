"""HOST TEST ONLY. Check a Media Center 0.2 local movie against its source.

usage: python3 -I verify_local_movie.py ORIGINAL.mp4 LOCAL.mov
Checks: top-level layout (ftyp->free, moov->free, mdat same offset, new
moov appended last); every byte before the appended moov is identical to
the original except the two relabelled box types; the appended moov has
one 'alis' self-reference dref per track and QuickTime-form audio; every
stco/co64 chunk offset equals the original's and lies inside the mdat;
ffmpeg decodes both to identical frame MD5s (video and audio) when
ffmpeg is installed. Prints OK lines, exits 1 on the first failure.
"""
import struct, sys, subprocess, shutil

CONT = {b'moov', b'trak', b'mdia', b'minf', b'stbl', b'dinf', b'edts', b'udta'}


def boxes(d, off, end):
    while off + 8 <= end:
        s, t = struct.unpack('>I4s', d[off:off + 8])
        h = 8
        if s == 1:
            s = struct.unpack('>Q', d[off + 8:off + 16])[0]; h = 16
        if s < h or off + s > end:
            raise ValueError('bad box %r at %d' % (t, off))
        yield t, off, h, s
        off += s


def walk(d, off, end, path, out):
    for t, o, h, s in boxes(d, off, end):
        p = path + [t]
        out.append((p, o, h, s))
        if t in CONT:
            walk(d, o + h, o + s, p, out)


def chunk_offsets(d, moov_off, moov_size):
    lst = []
    walk(d, moov_off + 8, moov_off + moov_size, [b'moov'], lst)
    res, drefs, stsd = [], [], []
    for p, o, h, s in lst:
        t = p[-1]
        if t == b'stco':
            n = struct.unpack('>I', d[o + h + 4:o + h + 8])[0]
            res.append(list(struct.unpack('>%dI' % n, d[o + h + 8:o + h + 8 + 4 * n])))
        elif t == b'co64':
            n = struct.unpack('>I', d[o + h + 4:o + h + 8])[0]
            res.append(list(struct.unpack('>%dQ' % n, d[o + h + 8:o + h + 8 + 8 * n])))
        elif t == b'dref':
            drefs.append(d[o + h:o + s])
        elif t == b'stsd':
            stsd.append(d[o + h:o + s])
    return res, drefs, stsd


def fail(m):
    print('FAIL', m); sys.exit(1)


def main():
    orig = open(sys.argv[1], 'rb').read()
    loc = open(sys.argv[2], 'rb').read()
    ob = list(boxes(orig, 0, len(orig)))
    lb = list(boxes(loc, 0, len(loc)))
    if lb[-1][0] != b'moov': fail('last box is not moov')
    tail_off = lb[-1][1]
    if tail_off != len(orig): fail('appended moov not at original EOF (%d vs %d)' % (tail_off, len(orig)))
    for (t, o, h, s), (t2, o2, h2, s2) in zip(ob, lb):
        if (o, s) != (o2, s2): fail('box moved: %r' % t)
        want = b'free' if t in (b'ftyp', b'moov') else t
        if t2 != want: fail('box %r at %d is %r, want %r' % (t, o, t2, want))
    # identical bytes except the relabelled types
    diff = [i for i in range(0, len(orig), 1 << 16) if orig[i:i + (1 << 16)] != loc[i:i + (1 << 16)]]
    allowed = set(o + 4 for t, o, h, s in ob if t in (b'ftyp', b'moov'))
    for blk in diff:
        for i in range(blk, min(blk + (1 << 16), len(orig))):
            if orig[i] != loc[i] and not any(a <= i < a + 4 for a in allowed):
                fail('byte %d differs' % i)
    print('OK layout: %s' % ' '.join('%s@%d' % (t.decode(), o) for t, o, h, s in lb))
    print('OK %d original bytes identical (except 2 relabelled types)' % len(orig))
    om = [x for x in ob if x[0] == b'moov'][0]
    oco, _, _ = chunk_offsets(orig, om[1], om[3])
    lco, drefs, stsd = chunk_offsets(loc, lb[-1][1], lb[-1][3])
    if oco != lco: fail('chunk offsets differ')
    md = [x for x in lb if x[0] == b'mdat'][0]
    for tr in lco:
        for c in tr:
            if not (md[1] + md[2] <= c < md[1] + md[3]): fail('offset %d outside mdat' % c)
    print('OK %d tracks, %d chunk offsets identical to the original, all inside mdat'
          % (len(lco), sum(len(t) for t in lco)))
    for dr in drefs:
        if dr != struct.pack('>II', 0, 1) + struct.pack('>I4sI', 12, b'alis', 1):
            fail('dref is not one alis self-reference: %r' % dr[:32])
    print('OK %d dref(s) = alis self-reference' % len(drefs))
    for sd in stsd:
        if b'mp4a' in sd[:16 + 8]:
            ver = struct.unpack('>H', sd[8 + 8 + 8:8 + 8 + 10])[0]
            if ver != 1 or b'wave' not in sd: fail('audio entry not QuickTime v1 + wave')
            print('OK audio sample description: QuickTime version 1 with wave')
    if shutil.which('ffmpeg'):
        def md5s(f, sel):
            r = subprocess.run(['ffmpeg', '-v', 'error', '-i', f, '-map', sel, '-f', 'framemd5', '-'],
                               capture_output=True, text=True)
            return [l.split(',')[-1].strip() for l in r.stdout.splitlines() if l and l[0] != '#']
        for sel, nm in (('0:v', 'video'), ('0:a', 'audio')):
            a, b = md5s(sys.argv[1], sel), md5s(sys.argv[2], sel)
            if not a or a != b: fail('%s decode differs (%d vs %d frames)' % (nm, len(a), len(b)))
            print('OK ffmpeg %s: %d frames, identical MD5s' % (nm, len(a)))
    print('ALL OK')


main()
