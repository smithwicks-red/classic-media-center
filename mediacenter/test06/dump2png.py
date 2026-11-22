#!/usr/bin/env python3
"""dump2png.py -- Classic Media Center 0.6: convert the app's "MC dump N"
files (written by the 'MCdb'/'dump' Apple event) to PNG.

Format: 16-byte header b"MCD1", width, height, rowBytes (big-endian
uint32 each), then `height` rows of `rowBytes` bytes; each pixel is
4 bytes x,R,G,B (a 32-bit GWorld as is); bytes past width*4 are padding.

usage: dump2png.py FILE... [-o OUTDIR]     -> OUTDIR/<file name>.png (and @2x)
       dump2png.py --check RAWDIR FILE...  (host proof: pixels of "MC dump N"
                                            == RAWDIR/<scene>.xrgb)
"""
import os, struct, sys
from PIL import Image

NAMES = {0: "1_search", 1: "2_results", 2: "2b_results_play_pressed",
         3: "3_files", 4: "4_download", 5: "5_confirm", 6: "5b_error",
         7: "6_streaming", 8: "6b_quit_warning", 9: "7_inactive",
         50: "50_font_test", 99: "99_live"}

def read_dump(path):
    data = open(path, "rb").read()
    if len(data) < 16 or data[:4] != b"MCD1":
        raise ValueError("%s: not an MC dump (magic %r)" % (path, data[:4]))
    w, h, rb = struct.unpack(">III", data[4:16])
    if w == 0 or h == 0 or w > 4096 or h > 4096 or rb < 4 * w:
        raise ValueError("%s: bad header w=%d h=%d rowBytes=%d" % (path, w, h, rb))
    need = 16 + h * rb
    if len(data) < need:
        raise ValueError("%s: truncated: %d of %d bytes" % (path, len(data), need))
    xrgb = b"".join(data[16 + y * rb: 16 + y * rb + 4 * w] for y in range(h))
    return w, h, rb, xrgb

def to_image(w, h, xrgb):
    rgb = bytearray(w * h * 3)
    rgb[0::3] = xrgb[1::4]
    rgb[1::3] = xrgb[2::4]
    rgb[2::3] = xrgb[3::4]
    return Image.frombytes("RGB", (w, h), bytes(rgb))

def number(path):
    tail = os.path.basename(path).rsplit(" ", 1)[-1]
    return int(tail) if tail.isdigit() else None

def main(argv):
    if len(argv) > 1 and argv[1] == "--check":
        rawdir, files, bad = argv[2], argv[3:], 0
        for f in files:
            w, h, rb, px = read_dump(f)
            ref = open(os.path.join(rawdir, NAMES[number(f)] + ".xrgb"), "rb").read()
            ok = px == ref and (w, h) == (480, 360)
            print("  %-12s %dx%d rowBytes %d -> %s" % (os.path.basename(f), w, h, rb,
                  "== " + NAMES[number(f)] + ".xrgb" if ok else "DIFFERS"))
            bad += not ok
        if bad or not files:
            sys.exit(1)
        return
    out, files = ".", []
    i = 1
    while i < len(argv):
        if argv[i] == "-o":
            out = argv[i + 1]; i += 2
        else:
            files.append(argv[i]); i += 1
    os.makedirs(out, exist_ok=True)
    for f in files:
        w, h, rb, px = read_dump(f)
        img = to_image(w, h, px)
        n = number(f)
        stem = os.path.basename(f).replace(" ", "_")
        if n in NAMES:
            stem += "_" + NAMES[n]
        img.save(os.path.join(out, stem + ".png"))
        img.resize((w * 2, h * 2), Image.NEAREST).save(os.path.join(out, stem + "@2x.png"))
        print("%s: %dx%d rowBytes %d -> %s.png" % (f, w, h, rb, os.path.join(out, stem)))

if __name__ == "__main__":
    main(sys.argv)
