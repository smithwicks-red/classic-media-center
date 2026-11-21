import sys, struct
m = bytearray(open(sys.argv[1], 'rb').read()); s = open(sys.argv[2], 'rb').read()
path = []
def walk(off, end, chain):
    while off + 8 <= end:
        n, t = struct.unpack('>I4s', m[off:off+8])
        if t == b'avcC':
            return chain + [off], off
        if t in (b'moov', b'trak', b'mdia', b'minf', b'stbl'):
            r = walk(off + 8, off + n, chain + [off])
            if r: return r
        if t == b'stsd':
            r = walk(off + 16, off + n, chain + [off])
            if r: return r
        if t == b'avc1':
            r = walk(off + 86, off + n, chain + [off])
            if r: return r
        off += n
chain, a = walk(0, len(m), [])
p = a + 8
sl = struct.unpack('>H', m[p+6:p+8])[0]
delta = len(s) - sl
new = m[:p+6] + struct.pack('>H', len(s)) + s + m[p+8+sl:]
for off in chain:
    n = struct.unpack('>I', new[off:off+4])[0]
    new[off:off+4] = struct.pack('>I', n + delta)
open(sys.argv[3], 'wb').write(new)
