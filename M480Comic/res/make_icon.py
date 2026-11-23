#!/usr/bin/env python3
"""M480Comic 아이콘 생성 (16/32/48, 8bit + 32bit BMP 항목 - WM6.1 호환)"""
import struct, sys
from PIL import Image, ImageDraw

def render(size):
    S = size * 8
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    # 주황→빨강 그라데이션 둥근 사각형
    grad = Image.new("RGBA", (S, S))
    gd = ImageDraw.Draw(grad)
    for y in range(S):
        t = y / (S - 1)
        c = (int(255 + (220 - 255) * t), int(150 + (60 - 150) * t), int(40 + (50 - 40) * t), 255)
        gd.line([(0, y), (S, y)], fill=c)
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, S - 1, S - 1], radius=int(S * 0.23), fill=255)
    im.paste(grad, (0, 0), mask)
    d = ImageDraw.Draw(im)
    w = (255, 255, 255, 255)
    k = (40, 30, 30, 255)
    # 만화 칸 3개 (흰 칸 + 검은 테두리)
    lw = max(2, int(S * 0.035))
    d.rectangle([S * 0.16, S * 0.16, S * 0.84, S * 0.84], fill=w, outline=k, width=lw)
    d.line([(S * 0.16, S * 0.48), (S * 0.84, S * 0.48)], fill=k, width=lw)
    d.line([(S * 0.52, S * 0.16), (S * 0.46, S * 0.48)], fill=k, width=lw)
    # 말풍선
    d.ellipse([S * 0.24, S * 0.56, S * 0.66, S * 0.76], fill=(255, 220, 70, 255), outline=k, width=lw)
    d.polygon([(S * 0.36, S * 0.74), (S * 0.30, S * 0.82), (S * 0.44, S * 0.75)], fill=(255, 220, 70, 255))
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
open(sys.argv[1] if len(sys.argv) > 1 else "m480comic.ico", "wb").write(out)
render(256).save("icon_preview.png") if "--preview" in sys.argv else None
