"""
Pre-rendered monster art for Abyss Labyrinth.
Tiny numpy software renderer: capsules, ellipsoids and triangles with
per-pixel normals, z-buffer, procedural materials and 2x supersampling.
"""
import math
import numpy as np

SS = 2  # supersampling factor
RS = 2  # resolution scale: art is stored at RS x the logical size; the game area-filters it down

# ------------------------------------------------------------------ noise
def _hash(ix, iy, seed):
    v = np.sin(ix * 127.1 + iy * 311.7 + seed * 74.7) * 43758.5453
    return v - np.floor(v)

def vnoise(x, y, seed=0.0):
    ix = np.floor(x); iy = np.floor(y)
    fx = x - ix; fy = y - iy
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy)
    a = _hash(ix, iy, seed); b = _hash(ix + 1, iy, seed)
    c = _hash(ix, iy + 1, seed); d = _hash(ix + 1, iy + 1, seed)
    return (a + (b - a) * fx) * (1 - fy) + (c + (d - c) * fx) * fy

def fbm(x, y, seed=0.0, octaves=4):
    s = 0.0; amp = 0.5; f = 1.0
    for i in range(octaves):
        s = s + amp * vnoise(x * f, y * f, seed + i * 13.1)
        amp *= 0.5; f *= 2.03
    return s / (1 - 0.5 ** octaves)

def C(r, g, b):
    return np.array([r, g, b], np.float32) / 255.0

# ------------------------------------------------------------------ materials
# each returns base(N,3), spec(N), shin(N or scalar), emis(N,3), alpha(N)
def material(name, p, tu, tv, seed):
    n = tu.shape[0]
    base = np.zeros((n, 3), np.float32)
    spec = np.zeros(n, np.float32)
    shin = 12.0
    emis = np.zeros((n, 3), np.float32)
    alpha = np.ones(n, np.float32)
    f1 = fbm(tu * 0.09, tv * 0.09, seed)
    f2 = fbm(tu * 0.25 + 7, tv * 0.25 + 3, seed + 3)

    def blood_streaks(amount=0.55):
        # vertical streaks of dried / fresh blood that follow the surface
        s = vnoise(tv * 0.35 + 11, tu * 0.02, seed + 5)
        drip = (s > (1 - amount * 0.4)) & (vnoise(tu * 0.05, tv * 0.35, seed + 9) > 0.5)
        return drip

    if name == 'rot':          # rotting grey-green skin
        col = p.get('col', C(104, 110, 88))
        base[:] = col * (0.82 + 0.3 * f1[:, None])
        m = f2 > 0.7
        base[m] = base[m] * 0.6 + C(40, 30, 20) * 0.25
        vein = np.abs(fbm(tu * 0.15 + 2, tv * 0.15, seed + 7) - 0.5) < 0.018
        base[vein] = C(60, 26, 50)
        sore = fbm(tu * 0.12 + 5, tv * 0.12 + 1, seed + 11) > 0.74
        base[sore] = C(120, 20, 16) * (0.6 + 0.6 * f1[sore, None])
        spec[sore] = 0.7
        d = blood_streaks(0.35)
        base[d] = C(80, 8, 6) * (0.8 + 0.4 * f2[d, None])
        spec[:] = 0.18; shin = 10
    elif name == 'raw':        # exposed wet muscle
        fib = vnoise(tu * 0.12, tv * 1.1, seed + 2)
        base[:] = C(140, 20, 16) * (0.6 + 0.5 * fib[:, None]) * (0.8 + 0.3 * f1[:, None]) + C(30, 0, 0) * f2[:, None]
        fat = f2 > 0.76
        base[fat] = C(190, 160, 110) * (0.8 + 0.3 * f1[fat, None])
        dk = fbm(tu * 0.1 + 3, tv * 0.1, seed + 6) > 0.68
        base[dk] *= 0.45
        spec[:] = 0.38; shin = 22
    elif name == 'gut':
        base[:] = C(160, 92, 86) * (0.75 + 0.35 * f1[:, None])
        bl = f2 > 0.64
        base[bl] = C(100, 18, 18)
        vein = np.abs(fbm(tu * 0.14, tv * 0.14, seed + 7) - 0.5) < 0.02
        base[vein] = C(70, 20, 50)
        spec[:] = 0.4; shin = 24
    elif name == 'blood':
        base[:] = C(90, 4, 4) * (0.8 + 0.3 * f1[:, None])
        spec[:] = 0.9; shin = 50
    elif name == 'bone':
        col = p.get('col', C(206, 196, 164))
        base[:] = col * (0.72 + 0.35 * f1[:, None])
        crack = np.abs(fbm(tu * 0.2, tv * 0.2, seed + 2) - 0.5) < 0.02
        base[crack] = col * 0.3
        dirt = f2 > 0.66
        base[dirt] = base[dirt] * 0.55 + C(60, 40, 20) * 0.4
        if p.get('bloody', True):
            d = blood_streaks(0.28)
            base[d] = C(96, 10, 8)
        spec[:] = 0.25; shin = 14
    elif name == 'skin':       # leathery / demon / pale human skin
        col = p.get('col', C(150, 110, 90))
        cells = fbm(tu * 0.22, tv * 0.22, seed + 4)
        base[:] = col * (0.84 + 0.26 * f1[:, None])
        if p.get('cracks', True):
            crack = np.abs(cells - 0.5) < 0.016
            base[crack] = col * 0.35
        else:   # living skin: soft subsurface-ish variation, faint veins
            base[:] = col * (0.9 + 0.14 * f1[:, None]) + C(40, 8, 6) * (0.15 * f2[:, None])
            vein = np.abs(fbm(tu * 0.1 + 2, tv * 0.1, seed + 7) - 0.5) < 0.008
            base[vein] = base[vein] * 0.85 + C(60, 50, 90) * 0.15
        if p.get('lava'):
            glow = np.abs(cells - 0.5) < 0.022
            emis[glow] = C(255, 110, 20) * (0.7 + 0.6 * f2[glow, None])
        if p.get('bloody', True):
            d = blood_streaks(0.28)
            base[d] = C(90, 6, 5)
        spec[:] = 0.3; shin = 16
    elif name == 'fur':
        col = p.get('col', C(110, 90, 70))
        strand = vnoise(tu * 0.6, tv * 6.0, seed + 1)
        base[:] = col * (0.5 + 0.8 * strand[:, None]) * (0.8 + 0.3 * f1[:, None])
        if p.get('bloody', True):
            d = blood_streaks(0.25)
            base[d] = C(90, 8, 6)
        spec[:] = 0.05; shin = 6
    elif name == 'chitin':
        col = p.get('col', C(60, 50, 64))
        base[:] = col * (0.7 + 0.4 * f1[:, None])
        band = (np.sin(tu * 0.5) > 0.8)
        base[band] *= 0.45
        if p.get('skull'):
            sk = (np.abs(tv) < 0.45) & (np.abs(tu * 0.04 - 1.0) < 0.6) & (f2 > 0.45)
            base[sk] = C(200, 190, 160)
        spec[:] = 0.9; shin = 44
    elif name == 'rag':
        col = p.get('col', C(150, 134, 100))
        band = 0.5 + 0.5 * np.sin(tu * 0.7 + tv * 2.2 + f1 * 3)
        base[:] = col * (0.55 + 0.45 * band[:, None]) * (0.8 + 0.3 * f2[:, None])
        st = fbm(tu * 0.05, tv * 0.5, seed + 8) > 0.7
        base[st] = C(70, 8, 6)
        spec[:] = 0.04
    elif name == 'robe':
        col = p.get('col', C(40, 30, 34))
        fold = 0.5 + 0.5 * np.sin(tv * 3.0 + f1 * 2.5)
        base[:] = col * (0.55 + 0.6 * fold[:, None])
        if p.get('bloody', True):
            d = blood_streaks(0.28)
            base[d] = C(70, 4, 4)
        spec[:] = 0.04
    elif name == 'metal':
        col = p.get('col', C(110, 110, 116))
        base[:] = col * (0.7 + 0.35 * f1[:, None])
        rust = f2 > p.get('rust', 0.55)
        base[rust] = C(110, 60, 30) * (0.7 + 0.5 * f1[rust, None])
        if p.get('bloody', True):
            d = blood_streaks(0.28)
            base[d] = C(100, 6, 4)
        spec[:] = np.where(rust, 0.15, 0.9); shin = 30
        if p.get('glow') is not None:
            g = np.abs(f2 - 0.5) < 0.02
            emis[g] = p['glow']
    elif name == 'stone':
        col = p.get('col', C(110, 106, 100))
        base[:] = col * (0.65 + 0.45 * f1[:, None])
        crack = np.abs(fbm(tu * 0.12, tv * 0.12, seed + 6) - 0.5) < 0.025
        base[crack] = col * 0.25
        if p.get('glow') is not None:
            emis[crack] = p['glow']
        spec[:] = 0.1
    elif name == 'ice':
        base[:] = C(150, 200, 235) * (0.7 + 0.3 * f1[:, None])
        crack = np.abs(fbm(tu * 0.15, tv * 0.15, seed + 6) - 0.5) < 0.02
        base[crack] = C(240, 250, 255)
        emis[:] = C(20, 40, 60)
        spec[:] = 1.0; shin = 60
    elif name == 'ecto':
        col = p.get('col', C(150, 190, 220))
        base[:] = col * (0.7 + 0.4 * f1[:, None])
        emis[:] = col * 0.35 * (0.6 + 0.6 * f2[:, None])
        alpha[:] = p.get('a', 0.62) * (0.65 + 0.45 * f2)
        spec[:] = 0.2
    elif name == 'eye':
        col = p.get('col', C(255, 60, 20))
        base[:] = col
        emis[:] = col * 0.95
    elif name == 'eyeball':
        r = np.sqrt(tu * tu + tv * tv)
        base[:] = C(220, 210, 190)
        iris = r < 0.45
        base[iris] = p.get('col', C(200, 20, 10))
        pup = r < 0.2
        base[pup] = C(5, 0, 0)
        vein = (np.abs(np.sin(np.arctan2(tv, tu) * 7 + f1 * 5)) < 0.1) & ~iris
        base[vein] = C(170, 30, 30)
        spec[:] = 1.0; shin = 60
    elif name == 'teeth':
        base[:] = C(220, 206, 160) * (0.8 + 0.25 * f1[:, None])
        spec[:] = 0.5; shin = 30
    elif name == 'mouth':
        base[:] = C(40, 4, 4) * (0.6 + 0.6 * f1[:, None])
        spec[:] = 0.6; shin = 30
    elif name == 'dark':
        base[:] = C(6, 4, 4)
    elif name == 'wood':
        g = 0.5 + 0.5 * np.sin(tv * 6 + f1 * 6)
        base[:] = C(92, 64, 38) * (0.6 + 0.5 * g[:, None])
        spec[:] = 0.05
    elif name == 'gold':
        base[:] = C(210, 160, 60) * (0.75 + 0.35 * f1[:, None])
        spec[:] = 1.0; shin = 40
    elif name == 'membrane':
        col = p.get('col', C(110, 70, 60))
        base[:] = col * (0.6 + 0.5 * f1[:, None])
        vein = np.abs(fbm(tu * 0.12, tv * 0.12, seed + 3) - 0.5) < 0.03
        base[vein] = col * 0.35
        tear = f2 > 0.78
        alpha[tear] = 0.0
        alpha[~tear] = 0.95
        spec[:] = 0.3; shin = 20
    elif name == 'leather':
        col = p.get('col', C(96, 62, 36))
        grain = fbm(tu * 0.4, tv * 0.4, seed + 2)
        base[:] = col * (0.7 + 0.45 * f1[:, None]) * (0.85 + 0.25 * grain[:, None])
        stitch = (np.abs(((tv * 0.5) % 6.0) - 3.0) < 0.25) & (np.sin(tu * 2.2) > 0.2)
        base[stitch] = C(170, 150, 110)
        wear = f2 > 0.72
        base[wear] = base[wear] * 1.25
        spec[:] = 0.22; shin = 10
    elif name == 'mail':      # riveted chain mail: rows of rings
        col = p.get('col', C(130, 128, 124))
        u = tu * 0.9; v = tv * 0.9 + 0.5 * np.floor(tu * 0.9)
        ring = np.abs(np.sqrt((u % 1.0 - 0.5) ** 2 + (v % 1.0 - 0.5) ** 2) - 0.32) < 0.13
        base[:] = col * 0.22
        base[ring] = col * (0.75 + 0.4 * f1[ring, None])
        rust = f2 > p.get('rust', 0.8)
        base[rust & ring] = C(110, 64, 34)
        spec[:] = np.where(ring, 0.9, 0.05); shin = 26
    elif name == 'scale':     # overlapping armour scales
        col = p.get('col', C(70, 110, 70))
        u = tu * 0.45; v = tv * 0.45 + 0.5 * np.floor(tu * 0.45)
        fu = u % 1.0; fv = v % 1.0
        edge = np.clip(1 - np.sqrt((fu - 0.5) ** 2 * 2 + fv ** 2) * 1.1, 0, 1)
        base[:] = col * (0.35 + 0.8 * edge[:, None]) * (0.85 + 0.3 * f1[:, None])
        rim = fv > 0.86
        base[rim] = col * 0.2
        if p.get('glow') is not None:
            emis[rim] = p['glow'] * 0.6
        spec[:] = 0.7 * edge; shin = 30
    elif name == 'cloth':
        col = p.get('col', C(120, 110, 96))
        weave = 0.5 + 0.25 * np.sin(tu * 3.0) + 0.25 * np.sin(tv * 3.0)
        fold = 0.5 + 0.5 * np.sin(tv * 1.4 + f1 * 3.0)
        base[:] = col * (0.55 + 0.35 * fold[:, None] + 0.15 * weave[:, None])
        if p.get('trim') is not None:
            tr = (np.abs(tv) % 12.0) < 1.2
            base[tr] = p['trim']
        if p.get('bloody', False):
            d = blood_streaks(0.25)
            base[d] = C(70, 4, 4)
        spec[:] = 0.03
    elif name == 'plate':     # polished plate with bevels, engraving, optional trim / glow seams
        col = p.get('col', C(150, 150, 156))
        base[:] = col * (0.72 + 0.3 * f1[:, None])
        scratch = np.abs(vnoise(tu * 0.08, tv * 1.6, seed + 3) - 0.5) < 0.03
        base[scratch] = base[scratch] * 1.3
        if p.get('etch') is not None:
            et = np.abs(np.sin(tu * 0.9 + np.sin(tv * 0.7) * 2.0)) < 0.12
            base[et] = p['etch']
        if p.get('rust') is not None:
            r = f2 > p['rust']
            base[r] = C(100, 56, 28) * (0.7 + 0.5 * f1[r, None])
        if p.get('glow') is not None:
            g = np.abs(fbm(tu * 0.12, tv * 0.12, seed + 6) - 0.5) < 0.022
            emis[g] = p['glow']; base[g] = p['glow'] * 0.5
        spec[:] = p.get('spec', 1.0); shin = p.get('shin', 40)
    elif name == 'glowmat':
        col = p.get('col', C(160, 80, 255))
        base[:] = col * (0.5 + 0.5 * f1[:, None])
        emis[:] = col * (0.55 + 0.6 * f2[:, None])
        spec[:] = 0.5; shin = 30
    elif name == 'fire':
        h = vnoise(tu * 0.3, tv * 0.3, seed)
        base[:] = C(255, 140, 30)
        emis[:] = np.where((h > 0.5)[:, None], C(255, 210, 90), C(255, 90, 10))
        alpha[:] = 0.85
    else:
        base[:] = C(255, 0, 255)
    return base, spec, shin, emis, alpha

# ------------------------------------------------------------------ lighting
L = np.array([-0.45, 0.55, -0.70], np.float32); L /= np.linalg.norm(L)
Hh = L + np.array([0, 0, -1], np.float32); Hh /= np.linalg.norm(Hh)
KEY = C(255, 228, 200) * 1.3
AMB = C(30, 28, 40)
RIM = C(200, 60, 40)

BUMP = {'rot': 0.55, 'raw': 0.6, 'gut': 0.45, 'stone': 0.8, 'skin': 0.4, 'bone': 0.35, 'fur': 0.6, 'chitin': 0.15,
        'metal': 0.25, 'ice': 0.25, 'rag': 0.6, 'robe': 0.3, 'membrane': 0.3, 'wood': 0.3, 'leather': 0.45,
        'cloth': 0.25, 'plate': 0.08, 'scale': 0.2}

FINE = {'rot': 0.22, 'raw': 0.25, 'gut': 0.2, 'stone': 0.3, 'skin': 0.07, 'bone': 0.18, 'fur': 0.3, 'chitin': 0.1,
        'metal': 0.12, 'ice': 0.1, 'rag': 0.28, 'robe': 0.18, 'membrane': 0.2, 'wood': 0.25, 'leather': 0.2, 'mail': 0.1}

def shade(mat, p, nrm, tu, tv, seed):
    tu = tu / RS; tv = tv / RS          # texture space stays body-relative at any resolution
    base, spec, shin, emis, alpha = material(mat, p, tu, tv, seed)
    bmp = BUMP.get(mat, 0.0)
    if bmp:
        sc = 0.35 if mat != 'stone' else 0.2
        gx = vnoise(tu * sc + 0.5, tv * sc, seed + 21) - vnoise(tu * sc - 0.5, tv * sc, seed + 21)
        gy = vnoise(tu * sc, tv * sc + 0.5, seed + 21) - vnoise(tu * sc, tv * sc - 0.5, seed + 21)
        # second, finer octave: pores, scratches, weave
        sc2 = sc * 3.1
        gx = gx + 0.32 * (vnoise(tu * sc2 + 0.5, tv * sc2, seed + 33) - vnoise(tu * sc2 - 0.5, tv * sc2, seed + 33))
        gy = gy + 0.32 * (vnoise(tu * sc2, tv * sc2 + 0.5, seed + 33) - vnoise(tu * sc2, tv * sc2 - 0.5, seed + 33))
        nrm = nrm + bmp * np.stack([gx, gy, np.zeros_like(gx)], 1)
        nrm = nrm / (np.linalg.norm(nrm, axis=1, keepdims=True) + 1e-9)
    fn = FINE.get(mat, 0.0)
    if fn:
        g = vnoise(tu * 1.3 + 3.3, tv * 1.3, seed + 41) * 0.6 + vnoise(tu * 2.9, tv * 2.9 + 1.7, seed + 43) * 0.4
        base = base * (1 - fn + 2 * fn * g)[:, None]
    d = np.clip(nrm @ L, 0, 1)
    sp = np.clip(nrm @ Hh, 0, 1) ** shin * spec
    rim = (1 - np.abs(nrm[:, 2])) ** 3 * 0.55
    col = base * (AMB + d[:, None] * KEY) + rim[:, None] * RIM * base * 1.5 + sp[:, None] * 0.9 + emis
    return col, alpha

# ------------------------------------------------------------------ canvas
class Canvas:
    def __init__(self, W, H, ox, oy):
        self.W, self.H = W * SS, H * SS
        self.ox, self.oy = ox, oy
        self.col = np.zeros((self.H, self.W, 3), np.float32)
        self.a = np.zeros((self.H, self.W), np.float32)
        self.z = np.full((self.H, self.W), 1e9, np.float32)
        xs = (np.arange(self.W) + 0.5) / SS - ox
        ys = oy - (np.arange(self.H) + 0.5) / SS
        self.X, self.Y = np.meshgrid(xs, ys)

    def bbox(self, x0, x1, y0, y1):
        px0 = int(max(0, math.floor((x0 + self.ox) * SS)))
        px1 = int(min(self.W, math.ceil((x1 + self.ox) * SS) + 1))
        py0 = int(max(0, math.floor((self.oy - y1) * SS)))
        py1 = int(min(self.H, math.ceil((self.oy - y0) * SS) + 1))
        return px0, px1, py0, py1

    def write(self, sl, inside, z, col, alpha):
        zz = self.z[sl]
        upd = inside & (z < zz)
        if not upd.any():
            return
        cz = self.col[sl]; ca = self.a[sl]
        a = alpha
        # translucent pixels blend over what's there (no z write)
        opaque = a > 0.97
        u1 = upd & opaque
        cz[u1] = col[u1]; ca[u1] = 1.0; zz[u1] = z[u1]
        u2 = upd & ~opaque & (a > 0.01)
        if u2.any():
            aa = a[u2][:, None]
            cz[u2] = cz[u2] * (1 - aa) + col[u2] * aa
            ca[u2] = ca[u2] + (1 - ca[u2]) * a[u2]

    def capsule(self, A, B, ra, rb, mat, p=None, seed=0.0):
        p = p or {}
        x0 = min(A[0] - ra, B[0] - rb); x1 = max(A[0] + ra, B[0] + rb)
        y0 = min(A[1] - ra, B[1] - rb); y1 = max(A[1] + ra, B[1] + rb)
        px0, px1, py0, py1 = self.bbox(x0, x1, y0, y1)
        if px1 <= px0 or py1 <= py0:
            return
        sl = (slice(py0, py1), slice(px0, px1))
        X = self.X[sl]; Y = self.Y[sl]
        abx, aby, abz = B[0] - A[0], B[1] - A[1], B[2] - A[2]
        l2 = abx * abx + aby * aby
        if l2 < 1e-6:
            t = np.zeros_like(X)
        else:
            t = np.clip(((X - A[0]) * abx + (Y - A[1]) * aby) / l2, 0, 1)
        r = ra + (rb - ra) * t
        cx = A[0] + abx * t; cy = A[1] + aby * t
        dx = X - cx; dy = Y - cy
        d2 = dx * dx + dy * dy
        inside = d2 < r * r
        if not inside.any():
            return
        nz = np.sqrt(np.clip(1 - d2 / (r * r), 0, 1))
        z = A[2] + abz * t - nz * r
        idx = inside
        nrm = np.stack([dx[idx] / r[idx], dy[idx] / r[idx], -nz[idx]], 1)
        L3 = math.sqrt(abx * abx + aby * aby + abz * abz)
        tu = t[idx] * L3
        if l2 < 1e-6:
            tv = dx[idx] / r[idx]
        else:
            ln = math.sqrt(l2)
            tv = (dx[idx] * (-aby) + dy[idx] * abx) / ln / r[idx] * (r[idx] * 2)
        col, alpha = shade(mat, p, nrm, tu, tv, seed)
        full = np.zeros(X.shape + (3,), np.float32); full[idx] = col
        fa = np.zeros(X.shape, np.float32); fa[idx] = alpha
        self.write(sl, inside, z, full, fa)

    def ellipsoid(self, Cc, rx, ry, rz, mat, p=None, seed=0.0):
        p = p or {}
        px0, px1, py0, py1 = self.bbox(Cc[0] - rx, Cc[0] + rx, Cc[1] - ry, Cc[1] + ry)
        if px1 <= px0 or py1 <= py0:
            return
        sl = (slice(py0, py1), slice(px0, px1))
        X = self.X[sl]; Y = self.Y[sl]
        dx = (X - Cc[0]) / rx; dy = (Y - Cc[1]) / ry
        q = dx * dx + dy * dy
        inside = q < 1
        if not inside.any():
            return
        nz = np.sqrt(np.clip(1 - q, 0, 1))
        z = Cc[2] - nz * rz
        idx = inside
        n = np.stack([dx[idx] / rx, dy[idx] / ry, -nz[idx] / rz], 1)
        n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-9
        tu = dx[idx] * rx; tv = dy[idx] * ry
        if mat == 'eyeball':
            tu = dx[idx]; tv = dy[idx]
        col, alpha = shade(mat, p, n, tu, tv, seed)
        full = np.zeros(X.shape + (3,), np.float32); full[idx] = col
        fa = np.zeros(X.shape, np.float32); fa[idx] = alpha
        self.write(sl, inside, z, full, fa)

    def tri(self, P0, P1, P2, mat, p=None, seed=0.0, alpha_fade=None):
        p = p or {}
        xs = [P0[0], P1[0], P2[0]]; ys = [P0[1], P1[1], P2[1]]
        px0, px1, py0, py1 = self.bbox(min(xs), max(xs), min(ys), max(ys))
        if px1 <= px0 or py1 <= py0:
            return
        sl = (slice(py0, py1), slice(px0, px1))
        X = self.X[sl]; Y = self.Y[sl]
        x0, y0 = P0[0], P0[1]; x1, y1 = P1[0], P1[1]; x2, y2 = P2[0], P2[1]
        den = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2)
        if abs(den) < 1e-9:
            return
        w0 = ((y1 - y2) * (X - x2) + (x2 - x1) * (Y - y2)) / den
        w1 = ((y2 - y0) * (X - x2) + (x0 - x2) * (Y - y2)) / den
        w2 = 1 - w0 - w1
        inside = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
        if not inside.any():
            return
        z = w0 * P0[2] + w1 * P1[2] + w2 * P2[2]
        e1 = np.array(P1) - np.array(P0); e2 = np.array(P2) - np.array(P0)
        nn = np.cross(e1, e2); nn = nn / (np.linalg.norm(nn) + 1e-9)
        if nn[2] > 0:
            nn = -nn
        idx = inside
        n = np.tile(nn.astype(np.float32), (int(idx.sum()), 1))
        # slight bumpy normal for organic surfaces
        tu = (w1[idx] * 40 + X[idx] * 1.0)
        tv = (w2[idx] * 40 + Y[idx] * 1.0)
        col, alpha = shade(mat, p, n, tu, tv, seed)
        if alpha_fade is not None:
            alpha = alpha * np.clip((w0[idx] * alpha_fade[0] + w1[idx] * alpha_fade[1] + w2[idx] * alpha_fade[2]), 0, 1)
        full = np.zeros(X.shape + (3,), np.float32); full[idx] = col
        fa = np.zeros(X.shape, np.float32); fa[idx] = alpha
        self.write(sl, inside, z, full, fa)

    def quad(self, P0, P1, P2, P3, mat, p=None, seed=0.0, fade=None):
        f = fade
        self.tri(P0, P1, P2, mat, p, seed, None if f is None else (f[0], f[1], f[2]))
        self.tri(P0, P2, P3, mat, p, seed, None if f is None else (f[0], f[2], f[3]))

    def ambient_occlusion(self, strength=0.55):
        """screen-space AO from the z-buffer: pixels sitting deeper than their surroundings get darker"""
        from scipy.ndimage import gaussian_filter
        cov = (self.a > 0.5).astype(np.float32)
        if cov.sum() < 4:
            return
        z = np.where(cov > 0, self.z, 0).astype(np.float32)
        sig = 2.2 * SS * RS
        zb = gaussian_filter(z * cov, sig) / (gaussian_filter(cov, sig) + 1e-6)
        zb2 = gaussian_filter(z * cov, sig * 0.4) / (gaussian_filter(cov, sig * 0.4) + 1e-6)
        rng_ = 3.0 * RS
        occ = np.clip((z - zb) / rng_, 0, 1) * 0.65 + np.clip((z - zb2) / (rng_ * 0.5), 0, 1) * 0.35
        k = 1 - strength * occ * cov
        self.col *= k[..., None]

    def zdown(self):
        """per output pixel: nearest covered depth (for layered compositing)"""
        H, W = self.H // SS, self.W // SS
        z = np.where(self.a > 0.02, self.z, 1e9)
        return z.reshape(H, SS, W, SS).min(axis=(1, 3))

    def result(self, ao=True):
        """downsample SS x -> (H, W, 4) float, colour graded"""
        if ao:
            self.ambient_occlusion()
        H, W = self.H // SS, self.W // SS
        a = self.a.reshape(H, SS, W, SS).mean(axis=(1, 3))
        col = (self.col * self.a[..., None]).reshape(H, SS, W, SS, 3).mean(axis=(1, 3))
        col = col / np.maximum(a[..., None], 1e-6)
        # contact shading toward the ground
        ys = self.oy - (np.arange(H) + 0.5)
        gfac = 0.55 + 0.45 * np.clip(ys / (22.0 * RS), 0, 1)
        col = col * gfac[:, None, None]
        # grim colour grade: slight desaturation + contrast + darkening
        lum = (col * np.array([0.3, 0.55, 0.15])).sum(-1, keepdims=True)
        col = col * 0.85 + lum * 0.15
        col = np.clip((col - 0.03) * 1.08, 0, 1) ** 1.08
        return np.concatenate([col, a[..., None]], -1)


# ------------------------------------------------------------------ transforms
class View:
    def __init__(self, yaw=0.0, pitch=0.12, dx=0.0, dy=0.0, dz=0.0, scale=1.0):
        self.cy, self.sy = math.cos(yaw), math.sin(yaw)
        self.cp, self.sp = math.cos(pitch), math.sin(pitch)
        self.d = (dx, dy, dz)
        self.s = scale

    def __call__(self, P):
        x, y, z = P[0] * self.s + self.d[0], P[1] * self.s + self.d[1], P[2] * self.s + self.d[2]
        x2 = x * self.cy + z * self.sy
        z2 = -x * self.sy + z * self.cy
        y3 = y * self.cp - z2 * self.sp
        z3 = y * self.sp + z2 * self.cp
        return (x2, y3, z3)


class Model:
    """Collects primitives in model space; drawn through a View."""
    def __init__(self, view):
        self.v = view
        self.prims = []

    def cap(self, A, B, ra, rb, mat, p=None, seed=0.0):
        self.prims.append(('c', self.v(A), self.v(B), ra * self.v.s, rb * self.v.s, mat, p, seed))

    def ell(self, Cc, rx, ry, rz, mat, p=None, seed=0.0):
        s = self.v.s
        self.prims.append(('e', self.v(Cc), rx * s, ry * s, rz * s, mat, p, seed))

    def tri(self, A, B, Cc, mat, p=None, seed=0.0, fade=None):
        self.prims.append(('t', self.v(A), self.v(B), self.v(Cc), mat, p, seed, fade))

    def quad(self, A, B, Cc, D, mat, p=None, seed=0.0, fade=None):
        if fade is None:
            self.tri(A, B, Cc, mat, p, seed); self.tri(A, Cc, D, mat, p, seed)
        else:
            self.tri(A, B, Cc, mat, p, seed, (fade[0], fade[1], fade[2]))
            self.tri(A, Cc, D, mat, p, seed, (fade[0], fade[2], fade[3]))

    def chain(self, pts, r0, r1, mat, p=None, seed=0.0):
        n = len(pts) - 1
        for i in range(n):
            ra = r0 + (r1 - r0) * i / n; rb = r0 + (r1 - r0) * (i + 1) / n
            self.cap(pts[i], pts[i + 1], ra, rb, mat, p, seed + i * 0.37)

    def draw(self, cv):
        for pr in self.prims:
            if pr[0] == 'c':
                cv.capsule(pr[1], pr[2], pr[3], pr[4], pr[5], pr[6], pr[7])
            elif pr[0] == 'e':
                cv.ellipsoid(pr[1], pr[2], pr[3], pr[4], pr[5], pr[6], pr[7])
            else:
                cv.tri(pr[1], pr[2], pr[3], pr[4], pr[5], pr[6], pr[7])


# ------------------------------------------------------------------ vector helpers
def V(x, y, z):
    return np.array([x, y, z], np.float32)

def lerp(a, b, t):
    return a + (b - a) * t

def norm(v):
    return v / (np.linalg.norm(v) + 1e-9)

def rot_x(P, O, a):
    """rotate P around pivot O in the y-z plane (positive = lean towards viewer)"""
    d = P - O
    c, s = math.cos(a), math.sin(a)
    return O + V(d[0], d[1] * c + d[2] * s, -d[1] * s + d[2] * c)

def rot_z(P, O, a):
    d = P - O
    c, s = math.cos(a), math.sin(a)
    return O + V(d[0] * c - d[1] * s, d[0] * s + d[1] * c, d[2])

def rot_y(P, O, a):
    d = P - O
    c, s = math.cos(a), math.sin(a)
    return O + V(d[0] * c + d[2] * s, d[1], -d[0] * s + d[2] * c)

def sn(t):
    return math.sin(2 * math.pi * t)

def phase(f):
    if f < 8:
        return 'idle', f / 8.0
    if f < 16:
        return 'atk', (f - 8) / 7.0
    if f < 20:
        return 'hurt', (f - 16) / 3.0
    if f < 24:
        return 'die', (f - 19) / 4.0
    return 'cast', (f - 24) / 7.0
