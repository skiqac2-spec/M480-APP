#!/usr/bin/env python3
"""M480Batt 아이콘 생성 (16/32/48, 8bit + 32bit BMP 항목 - WM6.1 호환)"""
import struct, sys
from PIL import Image, ImageDraw

def render(size):
    S = size * 8
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    grad = Image.new("RGBA", (S, S))
    gd = ImageDraw.Draw(grad)
    for y in range(S):
        t = y / (S - 1)
        c = (int(70 + (30 - 70) * t), int(200 + (150 - 200) * t), int(120 + (90 - 120) * t), 255)
        gd.line([(0, y), (S, y)], fill=c)
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, S - 1, S - 1], radius=int(S * 0.23), fill=255)
    im.paste(grad, (0, 0), mask)
    d = ImageDraw.Draw(im)
    w = (255, 255, 255, 255)
    # 배터리 모양 + 번개
    bx0, by0, bx1, by1 = S * 0.22, S * 0.30, S * 0.72, S * 0.70
    t = S * 0.06
    d.rounded_rectangle([bx0, by0, bx1, by1], radius=int(S * 0.06), outline=w, width=int(t))
    d.rectangle([bx1, S * 0.42, bx1 + S * 0.07, S * 0.58], fill=w)
    d.polygon([(S * 0.50, S * 0.35), (S * 0.36, S * 0.52), (S * 0.46, S * 0.52),
               (S * 0.42, S * 0.66), (S * 0.58, S * 0.47), (S * 0.48, S * 0.47)], fill=w)
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
open(sys.argv[1] if len(sys.argv) > 1 else "m480batt.ico", "wb").write(out)
render(256).save("icon_preview.png") if "--preview" in sys.argv else None
