#!/usr/bin/env python3
"""Draw mc_skin_render's text runs onto its xRGB buffers with TTF stand-ins
for the Mac fonts; write 1x and 2x PNGs and contact sheets.

usage: mc_mock.py RAWDIR OUTDIR   (headings: ../../font/MCGaramond-Regular.ttf
       or $MC_FONT_TTF)
"""
import os, sys
from PIL import Image, ImageDraw, ImageFont

W, H = 480, 360
HERE = os.path.dirname(os.path.abspath(__file__))
DJ = "/usr/share/fonts/truetype/dejavu"
FONTS = {
    # heading serif: the bundled "MC Garamond" (already condensed to 86%, so xs = 1)
    "G": (os.environ.get("MC_FONT_TTF", os.path.join(HERE, "..", "..", "font", "MCGaramond-Regular.ttf")), 1.00, 1.14),
    # UI: Charcoal / Geneva stand-ins
    "UI": (f"{DJ}/DejaVuSansCondensed.ttf", 1.0, 0.92),
    "UIB": (f"{DJ}/DejaVuSansCondensed-Bold.ttf", 1.0, 0.92),
    # (0.6) font test stand-ins for Hoefler Text / Palatino
    "SERIF": (f"{DJ}/DejaVuSerifCondensed.ttf", 1.0, 0.92),
}
_cache = {}

def font(key, px):
    path, xs, ss = FONTS[key]
    k = (key, px)
    if k not in _cache:
        _cache[k] = ImageFont.truetype(path, max(6, round(px * ss)))
    return _cache[k], xs

def role_font(role, serif):
    if role == 10:
        return "UI"   # MC_F_BTN: Charcoal stand-in
    if role in (0, 1, 7):
        return serif
    if role in (8, 9):
        return "SERIF"
    if role in (3, 6):
        return "UIB"
    return "UI"

def fit(text, f, xs, width, mode):
    def L(s): return f.getlength(s) * xs
    if L(text) <= width:
        return text
    ell = "…"
    if mode == 1:  # middle
        a, b = text[: len(text) // 2], text[len(text) // 2 :]
        while (a or b) and L(a + ell + b) > width:
            if len(a) >= len(b): a = a[:-1]
            else: b = b[1:]
        return a.rstrip() + ell + b.lstrip()
    while text and L(text + ell) > width:
        text = text[:-1]
    return text.rstrip() + ell

def draw_run(img, r, serif):
    role, size, align, trunc, cr, cg, cb, emb, l, t, rr, b, base, hx = r
    text = bytes.fromhex(hx).decode("mac_roman")
    key = role_font(role, serif)
    f, xs = font(key, size)
    width = rr - l
    text = fit(text, f, xs, width, trunc)
    tw = f.getlength(text) * xs
    x = l if align == 0 else (l + (width - tw) / 2 if align == 1 else rr - tw)
    # render the glyph mask unscaled, then condense horizontally
    pad = 4
    mw = int(f.getlength(text)) + 2 * pad
    mask = Image.new("L", (mw, H), 0)
    d = ImageDraw.Draw(mask)
    d.fontmode = "L"
    d.text((pad, base), text, font=f, fill=255, anchor="ls")
    if xs != 1.0:
        mask = mask.resize((max(1, round(mw * xs)), H), Image.LANCZOS)
        pad = pad * xs
    ox = round(x - pad)
    def paste(dy, col, alpha):
        layer = Image.new("RGB", mask.size, col)
        m = mask if alpha == 255 else mask.point(lambda v: v * alpha // 255)
        clip = Image.new("L", (W, H), 0)
        clip.paste(m, (ox, dy))
        box = Image.new("L", (W, H), 0)
        box.paste(255, (l, t, rr, b + (1 if dy else 0)))
        from PIL import ImageChops
        clip = ImageChops.multiply(clip, box)
        full = Image.new("RGB", (W, H), col)
        img.paste(full, (0, 0), clip)
    if emb:
        paste(1, (246, 246, 248), 150)
    paste(0, (cr, cg, cb), 255)

def load(rawdir, name):
    raw = open(os.path.join(rawdir, name + ".xrgb"), "rb").read()
    assert len(raw) == W * H * 4
    # big-endian xRGB -> RGB: drop byte 0 of every pixel
    rgb = bytes(b for i, b in enumerate(raw) if i % 4)
    return Image.frombytes("RGB", (W, H), rgb)

def runs(rawdir, name):
    out = []
    for line in open(os.path.join(rawdir, name + ".runs")):
        p = line.rstrip("\n").split("\t")
        out.append([int(v) for v in p[:13]] + [p[13]])
    return out

def main():
    rawdir, outdir = sys.argv[1], sys.argv[2]
    os.makedirs(outdir, exist_ok=True)
    scenes = sorted(n[:-5] for n in os.listdir(rawdir) if n.endswith(".xrgb"))
    made = {}
    for s in scenes:
        base = load(rawdir, s)
        rl = runs(rawdir, s)
        for serif in ("G",):
            img = base.copy()
            for r in rl:
                draw_run(img, r, serif)
            img.save(os.path.join(outdir, f"{s}.png"))
            img.resize((W * 2, H * 2), Image.NEAREST).save(os.path.join(outdir, f"{s}@2x.png"))
            made[(s, serif)] = img
    # contact sheet: every screen (MC Garamond headings), 3 across, captions
    cap = ImageFont.truetype(f"{DJ}/DejaVuSans.ttf", 13)
    cols, gap, ch = 3, 24, 22
    rows = (len(scenes) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * W + (cols + 1) * gap, rows * (H + ch) + (rows + 1) * gap), (92, 96, 112))
    d = ImageDraw.Draw(sheet)
    for i, s in enumerate(scenes):
        x = gap + (i % cols) * (W + gap)
        y = gap + (i // cols) * (H + ch + gap)
        d.text((x, y), s.replace("_", " "), font=cap, fill=(240, 240, 240))
        sheet.paste(made[(s, "G")], (x, y + ch))
    sheet.save(os.path.join(outdir, "contact_sheet.png"))
    print(f"{len(scenes)} scenes -> {outdir}")

if __name__ == "__main__":
    main()
