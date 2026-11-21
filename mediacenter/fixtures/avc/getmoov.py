import sys, struct
d = open(sys.argv[1], 'rb').read(); p = 0
while p + 8 <= len(d):
    n, t = struct.unpack('>I4s', d[p:p+8])
    if n == 1: n = struct.unpack('>Q', d[p+8:p+16])[0]
    if t == b'moov': open(sys.argv[2], 'wb').write(d[p:p+n]); break
    p += n
