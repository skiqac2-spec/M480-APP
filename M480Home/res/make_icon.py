#!/usr/bin/env python3
"""M480Home 아이콘 생성 (16/32/48, 8bit + 32bit BMP 항목 - WM6.1 호환)"""
import struct, sys
from PIL import Image, ImageDraw

def render(size):
    S = size * 8
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    grad = Image.new("RGBA", (S, S))
    gd = ImageDraw.Draw(grad)
    for y in range(S):
        t = y / (S - 1)
        c = (int(255 + (220 - 255) * t), int(120 + (70 - 120) * t), int(84 + (70 - 84) * t), 255)
        gd.line([(0, y), (S, y)], fill=c)
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, S - 1, S - 1], radius=int(S * 0.23), fill=255)
    im.paste(grad, (0, 0), mask)
    d = ImageDraw.Draw(im)
    w = (255, 255, 255, 255)
    # 2x2 칸 (홈 화면)
    g, m = S * 0.07, S * 0.2
    c = (S - 2 * m - g) / 2
    for i in range(2):
        for j in range(2):
            x, y = m + i * (c + g), m + j * (c + g)
            d.rounded_rectangle([x, y, x + c, y + c], radius=int(c * 0.28), fill=w)
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
open(sys.argv[1] if len(sys.argv) > 1 else "m480home.ico", "wb").write(out)
render(256).save("icon_preview.png") if "--preview" in sys.argv else None
