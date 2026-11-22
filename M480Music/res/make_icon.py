#!/usr/bin/env python3
"""M480Music 아이콘 생성 (16/32/48, 8bit + 32bit BMP 항목 - WM6.1 호환)"""
import struct, sys
from PIL import Image, ImageDraw

def render(size):
    S = size * 8
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    # 남보라→파랑 그라데이션 둥근 사각형
    grad = Image.new("RGBA", (S, S))
    gd = ImageDraw.Draw(grad)
    for y in range(S):
        t = y / (S - 1)
        c = (int(98 + (34 - 98) * t), int(70 + (110 - 70) * t), int(220 + (230 - 220) * t), 255)
        gd.line([(0, y), (S, y)], fill=c)
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, S - 1, S - 1], radius=int(S * 0.23), fill=255)
    im.paste(grad, (0, 0), mask)
    d = ImageDraw.Draw(im)
    w = (255, 255, 255, 255)
    # 8분음표 하나 + 코랄색 음파 두 줄
    hr = S * 0.13
    x1, y1 = S * 0.36, S * 0.70
    sw = S * 0.07
    d.ellipse([x1 - hr * 1.3, y1 - hr, x1 + hr * 1.3, y1 + hr], fill=w)
    sx = x1 + hr * 1.3 - sw
    top = S * 0.20
    d.rectangle([sx, top, sx + sw, y1], fill=w)
    d.polygon([(sx, top), (sx + S * 0.20, top + S * 0.10), (sx + S * 0.20, top + S * 0.20), (sx + sw, top + S * 0.12)], fill=w)
    coral = (255, 120, 90, 255)
    for k, r in enumerate((S * 0.16, S * 0.27)):
        cx, cy = S * 0.62, S * 0.62
        d.arc([cx - r, cy - r, cx + r, cy + r], start=-50, end=50, fill=coral, width=int(S * 0.055))
    return im.resize((size, size), Image.LANCZOS)

def bmp_entry(im, bits):
    n = im.size[0]
    px = im.load()
    androw = ((n + 31) // 32) * 4
    andmask = bytearray()
    for y in range(n - 1, -1, -1):
        row = bytearray(androw)
        for x in range(n):
            if px[x, y][3] < 128:
                row[x // 8] |= 0x80 >> (x % 8)
        andmask += row
    if bits == 32:
        hdr = struct.pack("<IiiHHIIiiII", 40, n, n * 2, 1, 32, 0, 0, 0, 0, 0, 0)
        data = bytearray()
        for y in range(n - 1, -1, -1):
            for x in range(n):
                r, g, b, a = px[x, y]
                data += bytes((b, g, r, a))
        return hdr + bytes(data) + bytes(andmask), 0, 32
    # 8bit: 배경을 검정으로 합성 후 팔레트화
    bg = Image.new("RGB", im.size, (0, 0, 0))
    bg.paste(im, (0, 0), im)
    q = bg.quantize(colors=256, method=Image.MEDIANCUT)
    pal = q.getpalette()[:768]
    pal += [0] * (768 - len(pal))
    hdr = struct.pack("<IiiHHIIiiII", 40, n, n * 2, 1, 8, 0, 0, 0, 0, 256, 0)
    ptab = bytearray()
    for i in range(256):
        ptab += bytes((pal[i * 3 + 2], pal[i * 3 + 1], pal[i * 3], 0))
    qp = q.load()
    rowb = ((n + 3) // 4) * 4
    data = bytearray()
    for y in range(n - 1, -1, -1):
        row = bytearray(rowb)
        for x in range(n):
            row[x] = qp[x, y]
        data += row
    return hdr + bytes(ptab) + bytes(data) + bytes(andmask), 0, 8

entries = []
for bits in (8, 32):
    for sz in (16, 32, 48):
        blob, cc, bc = bmp_entry(render(sz), bits)
        entries.append((sz, bc, blob))
out = bytearray(struct.pack("<HHH", 0, 1, len(entries)))
off = 6 + 16 * len(entries)
for sz, bc, blob in entries:
    out += struct.pack("<BBBBHHII", sz, sz, 0, 0, 1, bc, len(blob), off)
    off += len(blob)
for sz, bc, blob in entries:
    out += blob
open(sys.argv[1] if len(sys.argv) > 1 else "m480music.ico", "wb").write(out)
render(256).save("icon_preview.png") if "--preview" in sys.argv else None
