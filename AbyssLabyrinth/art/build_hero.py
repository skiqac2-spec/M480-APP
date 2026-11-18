"""Render the wanderer's equipment layers and pack them into abyss_hero.dat.

  'AHRO' u16 ver, u16 nlayers, u16 W, H, ax, ay, nframes, scale(256 = 1x)
  u8 ngroups, ngroups x u8 count   (groups: body, hair, armor, helm, weapon, shield)
  nlayers x u32 offset
  layer: per frame u32 size + rows: u8 nseg; seg: u16 x, u8 len, len x u16 rgb565, len x u8 alpha, len x u16 depth
  depth: 32768 + z * 64 (smaller = closer to the camera)
"""
import sys, os, struct, time, math
import numpy as np
from multiprocessing import Pool
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from render import Canvas, Model, View, RS
import hero

CV = (132, 140, 66, 132)          # logical canvas W, H, anchor x, ground y
YAW = math.pi - 0.28
NF = 32
LAYERS = hero.all_layers()
GROUPS = ['body', 'hair', 'armor', 'helm', 'weapon', 'shield']


def render_layer(li):
    g, v, fn = LAYERS[li]
    W, H, ox, oy = CV
    out = []
    for f in range(NF):
        c = Canvas(W * RS, H * RS, ox * RS, oy * RS)
        m = Model(View(YAW, 0.12, scale=RS))
        fn(m, f)
        m.draw(c)
        z = c.zdown()
        img = c.result()
        out.append((img, z))
    return li, out


def pack(img, z):
    Hh, W, _ = img.shape
    rgb = (np.clip(img[..., :3], 0, 1) * 255).astype(np.int32)
    a = (np.clip(img[..., 3], 0, 1) * 255).astype(np.int32)
    c565 = ((rgb[..., 0] >> 3) << 11) | ((rgb[..., 1] >> 2) << 5) | (rgb[..., 2] >> 3)
    dq = np.clip(32768 + np.where(z < 1e8, z, 500) * 64, 0, 65535).astype(np.int32)
    out = bytearray()
    for y in range(Hh):
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
            out += struct.pack('<HB', s, n)
            out += c565[y, s:s + n].astype('<u2').tobytes()
            out += a[y, s:s + n].astype(np.uint8).tobytes()
            out += dq[y, s:s + n].astype('<u2').tobytes()
    return bytes(out)


def composite(res, sel):
    """same algorithm as the game: per pixel, sort fragments by depth and blend front to back"""
    frames = []
    for f in range(NF):
        frags = []
        for li in sel:
            img, z = res[li][f]
            frags.append((img, np.where(img[..., 3] > 6 / 255, z, 1e9), li))
        Hh, W = frags[0][0].shape[:2]
        zs = np.stack([fz for _, fz, _ in frags])                     # (n, H, W)
        pri = np.array([li for _, _, li in frags])[:, None, None] * 1e-4
        order = np.argsort(zs - pri, axis=0)
        col = np.zeros((Hh, W, 3)); acc = np.zeros((Hh, W))
        for k in range(len(frags)):
            idx = order[k]
            img = np.stack([frags[i][0] for i in range(len(frags))])
            sel_img = np.take_along_axis(img, idx[None, :, :, None].repeat(4, -1), 0)[0]
            a = sel_img[..., 3] * (1 - acc)
            col += sel_img[..., :3] * a[..., None]
            acc += a
        out = np.concatenate([col / np.maximum(acc, 1e-6)[..., None], acc[..., None]], -1)
        frames.append(out)
    return frames


def main(outpath, prev=None):
    t0 = time.time()
    with Pool(2) as pool:
        res = dict(pool.map(render_layer, range(len(LAYERS))))
    W, H, ox, oy = CV
    counts = [sum(1 for g, _, _ in LAYERS if g == G) for G in GROUPS]
    blocks = []
    for li in range(len(LAYERS)):
        b = bytearray()
        for img, z in res[li]:
            d = pack(img, z)
            b += struct.pack('<I', len(d)) + d
        blocks.append(bytes(b))
    hdr = b'AHRO' + struct.pack('<HHHHHHHH', 1, len(LAYERS), W * RS, H * RS, ox * RS, oy * RS, NF, RS * 256)
    hdr += struct.pack('<B', len(GROUPS)) + bytes(counts)
    off = len(hdr) + 4 * len(LAYERS)
    tab = b''
    for b in blocks:
        tab += struct.pack('<I', off); off += len(b)
    with open(outpath, 'wb') as fo:
        fo.write(hdr + tab + b''.join(blocks))
    print('hero layers', len(LAYERS), os.path.getsize(outpath) // 1024, 'KB', round(time.time() - t0), 's')
    if prev:
        preview(res, prev)


def lid(g, v):
    for i, (G, V_, _) in enumerate(LAYERS):
        if G == g and V_ == v:
            return i


def preview(res, path):
    from PIL import Image
    combos = [
        [('body', 0), ('hair', 0)],
        [('body', 0), ('hair', 0), ('armor', 1), ('weapon', 0), ('shield', 0)],
        [('body', 0), ('armor', 2), ('helm', 1), ('weapon', 2), ('shield', 0)],
        [('body', 0), ('armor', 3), ('helm', 3), ('weapon', 4), ('shield', 1)],
        [('body', 0), ('armor', 4), ('helm', 2), ('weapon', 5)],
        [('body', 0), ('hair', 0), ('armor', 5), ('helm', 4), ('weapon', 8)],
        [('body', 0), ('armor', 6), ('helm', 6), ('weapon', 10)],
        [('body', 0), ('armor', 7), ('helm', 5), ('weapon', 3), ('shield', 2)],
        [('body', 0), ('armor', 8), ('helm', 3), ('weapon', 9), ('shield', 2)],
        [('body', 0), ('hair', 0), ('armor', 9), ('helm', 7), ('weapon', 11)],
        [('body', 0), ('hair', 0), ('armor', 0), ('weapon', 1)],
        [('body', 0), ('armor', 3), ('helm', 0), ('weapon', 7)],
    ]
    cells = []
    for cb in combos:
        fr = composite(res, [lid(g, v) for g, v in cb])
        row = []
        for f in (2, 11, 26):
            im = fr[f]
            c = im[..., :3] * im[..., 3:4] + np.array([26, 22, 20]) / 255 * (1 - im[..., 3:4])
            row.append(Image.fromarray((np.clip(c, 0, 1) * 255).astype(np.uint8)))
        cells.append(row)
    w, h = cells[0][0].size
    sheet = Image.new('RGB', (w * 6, h * 6))
    for i, row in enumerate(cells):
        for j, im in enumerate(row):
            sheet.paste(im, (((i % 2) * 3 + j) * w, (i // 2) * h))
    sheet.save(path)


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)
