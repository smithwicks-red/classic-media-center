"""(0.5u) HOST TEST ONLY: grow a faststart mp4's moov"""
import struct


def pad_moov(data, moov_total):
    """(0.5u) base.mp4 with its moov grown to moov_total bytes by a 'free'
    child box at the end of the moov; every stco/co64 entry shifted so the
    file stays valid (moov stays at offset 32, in front of the mdat)"""
    o = 0
    while data[o + 4:o + 8] != b'moov':
        o += struct.unpack('>I', data[o:o + 4])[0]
    msz = struct.unpack('>I', data[o:o + 4])[0]
    pad = moov_total - msz
    moov = bytearray(data[o:o + msz])

    def walk(b, lo, hi):
        while lo + 8 <= hi:
            sz, t = struct.unpack('>I4s', bytes(b[lo:lo + 8]))
            if t in (b'trak', b'mdia', b'minf', b'stbl'):
                walk(b, lo + 8, lo + sz)
            elif t == b'stco':
                n = struct.unpack('>I', bytes(b[lo + 12:lo + 16]))[0]
                for i in range(n):
                    p = lo + 16 + 4 * i
                    b[p:p + 4] = struct.pack('>I', struct.unpack('>I', bytes(b[p:p + 4]))[0] + pad)
            elif t == b'co64':
                n = struct.unpack('>I', bytes(b[lo + 12:lo + 16]))[0]
                for i in range(n):
                    p = lo + 16 + 8 * i
                    b[p:p + 8] = struct.pack('>Q', struct.unpack('>Q', bytes(b[p:p + 8]))[0] + pad)
            lo += sz
    walk(moov, 8, msz)
    moov += struct.pack('>I4s', pad, b'free') + bytes(pad - 8)
    moov[0:4] = struct.pack('>I', moov_total)
    return data[:o] + bytes(moov) + data[o + msz:]
