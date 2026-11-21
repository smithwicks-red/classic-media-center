# hand-made High SPS: scaling matrices, poc type 1, MBAFF, and an
# emulation-prevention byte. Writes sps.bin (NAL incl. header, escaped)
import sys
bits = []
def u(n, v):
    for i in range(n - 1, -1, -1): bits.append((v >> i) & 1)
def ue(v):
    v += 1; n = v.bit_length(); u(n - 1, 0); u(n, v)
def se(v): ue(2 * v - 1 if v > 0 else -2 * v)
big = int(sys.argv[1]) if len(sys.argv) > 1 else 1 << 20
u(8, 100); u(8, 0); u(8, 13); ue(0)
ue(1); ue(0); ue(0); u(1, 0)
u(1, 1)                                    # seq_scaling_matrix_present
for i in range(8):
    if i in (0, 6):
        u(1, 1)
        size = 16 if i < 6 else 64
        last = 8
        for j in range(size):
            val = 8 + (j % 7)
            d = val - last
            if d > 127: d -= 256
            if d < -128: d += 256
            se(d); last = val
    else:
        u(1, 0)
ue(0)                                      # log2_max_frame_num - 4
ue(1)                                      # poc type 1
u(1, 0); se(big); se(-3); ue(2); se(5); se(-big)
ue(2); u(1, 0); ue(19); ue(7); u(1, 0); u(1, 1)   # frame_mbs_only 0, mbaff 1
u(1, 1); u(1, 0); u(1, 0)                  # direct_8x8, crop, vui
u(1, 1)
while len(bits) % 8: bits.append(0)
raw = bytes(int(''.join(map(str, bits[i:i+8])), 2) for i in range(0, len(bits), 8))
out = bytearray(); z = 0
for b in raw:
    if z >= 2 and b <= 3: out.append(3); z = 0
    out.append(b); z = z + 1 if b == 0 else 0
print("epb", len(out) - len(raw))
open('sps.bin', 'wb').write(b'\x67' + bytes(out))
