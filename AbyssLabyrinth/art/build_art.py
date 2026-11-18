"""Render all monsters (24 frames each) and pack them into abyss_mon.dat.

File format (little endian):
  'AMON' u16 version u16 count, then count x u32 block offsets
  block: u16 W, H, anchor_x, ground_y, nframes ; then per frame: u32 size + data
  frame data, per row: u8 nseg, per segment: u8 x, u8 len, len x u16 rgb565, len x u8 alpha
"""
import sys, os, struct, time
import numpy as np
from multiprocessing import Pool
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from render import Canvas, Model, View
from species import S, NFRAMES

NF = 24

def render_species(i):
    key, fn, p, cv, yaw = S[i]
    W, H, ox, oy = cv
    frames = []
    for f in range(NFRAMES.get(key, NF)):
        c = Canvas(W, H, ox, oy)
        m = Model(View(yaw, 0.12))
        fn(m, p, f)
        m.draw(c)
        frames.append(c.result())
    return i, frames

def pack_frame(img):
    H, W, _ = img.shape
    rgb = (np.clip(img[..., :3], 0, 1) * 255).astype(np.int32)
    a = (np.clip(img[..., 3], 0, 1) * 255).astype(np.int32)
    c565 = ((rgb[..., 0] >> 3) << 11) | ((rgb[..., 1] >> 2) << 5) | (rgb[..., 2] >> 3)
    out = bytearray()
    for y in range(H):
        row = a[y] > 6
        segs = []
        x = 0
        while x < W:
            if row[x]:
                s = x
                while x < W and row[x] and x - s < 255:
                    x += 1
                segs.append((s, x - s))
            else:
                x += 1
        out += struct.pack('<B', len(segs))
        for s, n in segs:
            out += struct.pack('<BB', s, n)
            out += c565[y, s:s + n].astype('<u2').tobytes()
            out += a[y, s:s + n].astype(np.uint8).tobytes()
    return bytes(out)

def main(outpath, preview_dir=None):
    t0 = time.time()
    with Pool(2) as pool:
        res = dict(pool.map(render_species, range(len(S))))
    blocks = []
    for i in range(len(S)):
        key, fn, p, cv, yaw = S[i]
        W, H, ox, oy = cv
        b = bytearray(struct.pack('<HHHHH', W, H, ox, oy, len(res[i])))
        for fr in res[i]:
            d = pack_frame(fr)
            b += struct.pack('<I', len(d)) + d
        blocks.append(bytes(b))
    hdr = b'AMON' + struct.pack('<HH', 1, len(S))
    off = len(hdr) + 4 * len(S)
    table = b''
    for b in blocks:
        table += struct.pack('<I', off)
        off += len(b)
    with open(outpath, 'wb') as fo:
        fo.write(hdr + table + b''.join(blocks))
    print('packed', len(S), 'monsters,', os.path.getsize(outpath) // 1024, 'KB in', round(time.time() - t0, 1), 's')
    if preview_dir:
        make_previews(res, preview_dir)

def make_previews(res, d):
    from PIL import Image, ImageDraw, ImageFont
    os.makedirs(d, exist_ok=True)
    try:
        font = ImageFont.truetype('/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc', 11, index=1)
    except Exception:
        font = None
    def comp(img, bg=(26, 22, 20)):
        al = img[..., 3:4]
        c = img[..., :3] * al + np.array(bg) / 255.0 * (1 - al)
        return Image.fromarray((np.clip(c, 0, 1) * 255).astype(np.uint8))
    # bestiary: frame 4 of each species
    cell = 200
    cols = 8
    rows = (len(S) + cols - 1) // cols
    sheet = Image.new('RGB', (cols * cell, rows * cell), (26, 22, 20))
    dr = ImageDraw.Draw(sheet)
    for i in range(len(S)):
        im = comp(res[i][4])
        sc = min(1.8, (cell - 10) / im.width, (cell - 24) / im.height)
        im = im.resize((int(im.width * sc), int(im.height * sc)), Image.LANCZOS)
        x = (i % cols) * cell + (cell - im.width) // 2
        y = (i // cols) * cell + cell - 18 - im.height
        sheet.paste(im, (x, y))
        dr.text(((i % cols) * cell + 4, (i // cols) * cell + cell - 16), S[i][0], fill=(200, 180, 120), font=font)
    sheet.save(os.path.join(d, 'bestiary.png'))
    # animation strips
    for i in range(len(S)):
        fr = [comp(f) for f in res[i]]
        W, H = fr[0].size
        rows = (len(fr) + 11) // 12
        st = Image.new('RGB', (W * 12, H * rows))
        for k, f in enumerate(fr):
            st.paste(f, ((k % 12) * W, (k // 12) * H))
        st.save(os.path.join(d, 'strip_%02d_%s.png' % (i, S[i][0])))

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)
