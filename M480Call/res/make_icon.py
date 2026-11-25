#!/usr/bin/env python3
"""M480Call 아이콘 생성 (16/32/48, 8bit + 32bit BMP 항목 - WM6.1 호환)"""
import struct, sys
from PIL import Image, ImageDraw

def render(size):
    S = size * 8
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    grad = Image.new("RGBA", (S, S))
    gd = ImageDraw.Draw(grad)
    for y in range(S):
        t = y / (S - 1)
        c = (int(40 + (20 - 40) * t), int(180 + (130 - 180) * t), int(110 + (90 - 110) * t), 255)
        gd.line([(0, y), (S, y)], fill=c)
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, S - 1, S - 1], radius=int(S * 0.23), fill=255)
    im.paste(grad, (0, 0), mask)
    d = ImageDraw.Draw(im)
    w = (255, 255, 255, 255)
    # 수화기: 굵은 호 + 양 끝 둥근 덩어리
    cx, cy, r = S * 0.50, S * 0.50, S * 0.30
    d.arc([cx - r, cy - r, cx + r, cy + r], start=110, end=250, fill=w, width=int(S * 0.13))
    for ang in (110, 250):
        import math
        a = math.radians(ang)
        px, py = cx + r * math.cos(a), cy + r * math.sin(a)
        e = S * 0.11
        d.ellipse([px - e, py - e, px + e, py + e], fill=w)
    # 블루투스 음파
    blue = (120, 200, 255, 255)
    for rr in (S * 0.16, S * 0.27):
        d.arc([S * 0.48 - rr, S * 0.50 - rr, S * 0.48 + rr, S * 0.50 + rr], start=-45, end=45, fill=blue, width=int(S * 0.06))
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
open(sys.argv[1] if len(sys.argv) > 1 else "m480call.ico", "wb").write(out)
render(256).save("icon_preview.png") if "--preview" in sys.argv else None
