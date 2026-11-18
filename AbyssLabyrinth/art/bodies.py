"""Body plans with skeletal animation (24 frames: idle 8, attack 8, hurt 4, death 4)."""
import math
import numpy as np
from render import V, lerp, norm, rot_x, rot_z, rot_y, sn, phase, C

# =================================================================== BIPED

# ------------------------------------------------------------------ anatomy detail helpers
def _perp(d):
    """two unit vectors perpendicular to d: 'fwd' (towards -z as much as possible) and 'side'"""
    fz = V(0, 0, -1)
    side = np.cross(d, fz)
    if np.linalg.norm(side) < 1e-4:
        side = V(1, 0, 0)
    side = norm(side)
    fwd = norm(np.cross(side, d))
    if fwd[2] > 0:
        fwd = -fwd
    return fwd, side


def muscled_limb(m, A, B, r0, r1, mat, p, kind, seed=0.0):
    """limb segment with muscle bellies instead of a plain tube"""
    d = norm(B - A); L = float(np.linalg.norm(B - A))
    fwd, side = _perp(d)
    m.cap(A, B, r0 * 0.9, r1 * 0.88, mat, p, seed)
    if kind == 'thigh':
        m.cap(lerp(A, B, 0.12) + fwd * r0 * 0.22, lerp(A, B, 0.62) + fwd * r0 * 0.18, r0 * 0.86, r0 * 0.7, mat, p, seed + 1)   # quads
        m.cap(lerp(A, B, 0.15) - fwd * r0 * 0.2, lerp(A, B, 0.7) - fwd * r0 * 0.12, r0 * 0.8, r0 * 0.6, mat, p, seed + 2)    # hamstrings
        m.cap(lerp(A, B, 0.55) + side * r0 * 0.3 + fwd * r0 * 0.2, lerp(A, B, 0.85) + side * r0 * 0.15 + fwd * r0 * 0.25, r0 * 0.5, r1 * 0.55, mat, p, seed + 3)
        m.ell(B + fwd * r1 * 0.7 + d * r1 * 0.1, r1 * 0.55, r1 * 0.6, r1 * 0.4, mat, p, seed + 4)                     # kneecap
    elif kind == 'calf':
        m.cap(lerp(A, B, 0.08) - fwd * r0 * 0.3, lerp(A, B, 0.5) - fwd * r0 * 0.25, r0 * 0.95, r0 * 0.7, mat, p, seed + 1)    # gastrocnemius
        m.cap(lerp(A, B, 0.1) + fwd * r0 * 0.35, lerp(A, B, 0.9) + fwd * r1 * 0.3, r0 * 0.45, r1 * 0.4, mat, p, seed + 2)    # shin
        m.ell(B + side * r1 * 0.75, r1 * 0.35, r1 * 0.4, r1 * 0.35, mat, p, seed + 3)                                  # ankle
        m.ell(B - side * r1 * 0.75, r1 * 0.35, r1 * 0.4, r1 * 0.35, mat, p, seed + 3)
    elif kind == 'upper':
        m.cap(lerp(A, B, 0.2) + fwd * r0 * 0.3, lerp(A, B, 0.75) + fwd * r0 * 0.25, r0 * 0.78, r0 * 0.6, mat, p, seed + 1)   # biceps
        m.cap(lerp(A, B, 0.15) - fwd * r0 * 0.3, lerp(A, B, 0.7) - fwd * r0 * 0.2, r0 * 0.8, r0 * 0.58, mat, p, seed + 2)    # triceps
    elif kind == 'fore':
        m.cap(lerp(A, B, 0.05) + side * r0 * 0.25, lerp(A, B, 0.55) + side * r0 * 0.1, r0 * 0.95, r1 * 0.75, mat, p, seed + 1)
        m.ell(A - fwd * r0 * 0.4, r0 * 0.55, r0 * 0.55, r0 * 0.55, mat, p, seed + 2)                                   # elbow


def hand(m, ha, hd, H, mat, p, grip=0.0, w=1.0, nails=None, seed=0.0):
    """palm + 4 two-segment fingers + thumb. grip 0 = relaxed, 1 = closed fist around a handle"""
    fwd, side = _perp(hd)
    pr = 0.022 * H * w
    palm = ha + hd * 0.018 * H
    m.ell(palm, pr * 1.05, pr * 1.15, pr * 0.62, mat, p, seed)
    for i in range(4):
        o = (i - 1.5) * 0.36 * pr
        k0 = palm + hd * pr * 0.95 + side * o
        bend = 0.35 + 0.9 * grip
        d1 = norm(hd * math.cos(bend * 0.6) + fwd * math.sin(bend * 0.6))
        k1 = k0 + d1 * pr * (0.75 - abs(i - 1.5) * 0.08)
        d2 = norm(hd * math.cos(bend * 1.4) + fwd * math.sin(bend * 1.4))
        k2 = k1 + d2 * pr * (0.6 - abs(i - 1.5) * 0.06)
        fr = pr * 0.24
        m.cap(k0, k1, fr, fr * 0.92, mat, p, seed + i)
        m.cap(k1, k2, fr * 0.92, fr * 0.75, mat, p, seed + i)
        if nails is not None:
            m.cap(k2, k2 + d2 * pr * 0.35, fr * 0.6, fr * 0.15, 'bone', {'col': nails})
    tb = palm - side * pr * 0.8 + fwd * pr * 0.2
    td = norm(hd * 0.6 + fwd * (0.7 + grip * 0.3) - side * 0.3)
    m.cap(tb, tb + td * pr * 0.7, pr * 0.3, pr * 0.25, mat, p, seed + 7)
    m.cap(tb + td * pr * 0.7, tb + td * pr * 1.2 + side * pr * 0.25 * grip, pr * 0.25, pr * 0.2, mat, p, seed + 8)


def foot(m, ft, H, th, mat, p, toes=True, seed=0.0):
    fw = 0.03 * H * max(th, 0.5)
    m.ell(ft + V(0, 0.012 * H, -0.025 * H), fw, 0.017 * H, 0.05 * H, mat, p, seed)
    m.ell(ft + V(0, 0.016 * H, 0.012 * H), fw * 0.8, 0.02 * H, 0.025 * H, mat, p, seed + 1)       # heel
    if toes:
        for i in range(5):
            x = (i - 2) * fw * 0.36
            ln = 0.022 * H * (1.25 if i == 1 else 1.0 - abs(i - 1) * 0.08)
            a = ft + V(x, 0.008 * H, -0.06 * H)
            m.cap(a, a + V(x * 0.15, -0.002 * H, -ln), 0.0075 * H * (1.25 if i == 1 else 0.9), 0.006 * H, mat, p, seed + 2 + i)


def torso_detail(m, J, H, b, skin, sp, seed=0.0):
    """collarbones, trapezius, shoulder blades, spine ridge, obliques"""
    nk, ch, pv = J['neck'], J['chest'], J['pelvis']
    up = norm(nk - pv)
    for s in 'LR':
        sh = J['sh' + s]
        m.cap(nk + V(0, -0.005 * H, 0.014 * H), lerp(nk, sh, 0.8) + V(0, -0.012 * H, 0.016 * H), 0.024 * H * b, 0.016 * H * b, skin, sp, seed)    # trapezius
        m.cap(nk + V(0, -0.03 * H, -0.03 * H), sh + V(0, -0.006 * H, -0.03 * H), 0.009 * H, 0.008 * H, skin, sp, seed + 1)       # collarbone
        sx = -1 if s == 'L' else 1
        m.ell(ch + V(sx * 0.052 * H * b, 0.022 * H, 0.046 * H), 0.04 * H * b, 0.05 * H, 0.03 * H, skin, sp, seed + 2)          # shoulder blade
        m.cap(ch + V(sx * 0.1 * H * b, 0.02 * H, 0.0), lerp(pv, ch, 0.35) + V(sx * 0.075 * H * b, 0, 0.0), 0.04 * H * b, 0.035 * H * b, skin, sp, seed + 3)  # lats / obliques
    for i in range(7):                                                                             # spine ridge
        c = lerp(pv, nk, 0.12 + i * 0.13)
        m.ell(c + V(0, 0, 0.068 * H * (1.0 if 2 < i < 6 else 0.9)), 0.008 * H, 0.009 * H, 0.008 * H, skin, sp, seed + 4 + i)

def biped_pose(p, f):
    H = p['H']; b = p.get('build', 1.0)
    st, t = phase(f)
    J = {}
    J['pelvis'] = V(0, 0.53 * H, 0)
    J['belly'] = V(0, 0.62 * H, -0.01 * H)
    J['chest'] = V(0, 0.73 * H, 0)
    J['neck'] = V(0, 0.845 * H, 0.005 * H)
    J['head'] = V(0, 0.925 * H, -0.005 * H)
    sh = 0.125 * H * b
    J['shL'] = V(-sh, 0.815 * H, 0.01 * H); J['shR'] = V(sh, 0.815 * H, 0.01 * H)
    J['elL'] = V(-sh - 0.045 * H, 0.66 * H, -0.03 * H); J['elR'] = V(sh + 0.045 * H, 0.66 * H, -0.03 * H)
    J['haL'] = V(-sh - 0.01 * H, 0.57 * H, -0.17 * H); J['haR'] = V(sh + 0.01 * H, 0.57 * H, -0.17 * H)
    hp = 0.07 * H * b
    J['hiL'] = V(-hp, 0.515 * H, 0); J['hiR'] = V(hp, 0.515 * H, 0)
    J['knL'] = V(-hp - 0.02 * H, 0.27 * H, -0.06 * H); J['knR'] = V(hp + 0.02 * H, 0.27 * H, -0.06 * H)
    J['ftL'] = V(-hp - 0.04 * H, 0.025 * H, 0.01 * H); J['ftR'] = V(hp + 0.04 * H, 0.025 * H, 0.03 * H)
    if p.get('arms_raised'):   # zombie-like reaching arms
        J['elL'] = V(-sh - 0.02 * H, 0.74 * H, -0.12 * H); J['elR'] = V(sh + 0.02 * H, 0.74 * H, -0.12 * H)
        J['haL'] = V(-sh + 0.01 * H, 0.70 * H, -0.27 * H); J['haR'] = V(sh - 0.01 * H, 0.70 * H, -0.27 * H)
    if p.get('long_arms'):
        J['haL'] = V(-sh - 0.06 * H, 0.36 * H, -0.06 * H); J['haR'] = V(sh + 0.06 * H, 0.36 * H, -0.06 * H)
        J['elL'] = V(-sh - 0.06 * H, 0.60 * H, 0.03 * H); J['elR'] = V(sh + 0.06 * H, 0.60 * H, 0.03 * H)
    upper = ['belly', 'chest', 'neck', 'head', 'shL', 'shR', 'elL', 'elR', 'haL', 'haR']
    arms = ['elL', 'elR', 'haL', 'haR']
    hunch = p.get('hunch', 0.12)
    jaw = p.get('jaw', 0.15)
    roll = 0.0
    if st == 'idle':
        br = sn(t)
        for k in upper:
            J[k] = J[k] + V(0.012 * H * sn(t + 0.25) * (J[k][1] - 0.5 * H) / (0.4 * H), 0.007 * H * br, 0)
        J['haL'] = J['haL'] + V(0, 0.01 * H * sn(t + 0.1), 0.03 * H * sn(t))
        J['haR'] = J['haR'] + V(0, 0.01 * H * sn(t + 0.6), -0.03 * H * sn(t))
        J['head'] = J['head'] + V(0.008 * H * sn(t + 0.4), 0, 0)
        jaw = jaw + 0.05 * (0.5 + 0.5 * sn(t))
    elif st == 'atk':
        q = t
        wh = 'haR' if p.get('weapon') else 'haR'
        we = 'elR'
        if q < 0.45:
            s = q / 0.45
            J[wh] = lerp(J[wh], V(sh + 0.03 * H, 1.0 * H, 0.10 * H), s)
            J[we] = lerp(J[we], V(sh + 0.08 * H, 0.88 * H, 0.06 * H), s)
            hx = -0.10 * s
        elif q < 0.78:
            s = (q - 0.45) / 0.33
            J[wh] = lerp(V(sh + 0.03 * H, 1.0 * H, 0.10 * H), V(0.04 * H, 0.52 * H, -0.42 * H), s)
            J[we] = lerp(V(sh + 0.08 * H, 0.88 * H, 0.06 * H), V(sh * 0.7, 0.70 * H, -0.22 * H), s)
            hx = -0.10 + 0.45 * s
        else:
            s = (q - 0.78) / 0.22
            J[wh] = lerp(V(0.04 * H, 0.52 * H, -0.42 * H), J[wh], s)
            J[we] = lerp(V(sh * 0.7, 0.70 * H, -0.22 * H), J[we], s)
            hx = 0.35 * (1 - s)
        J['haL'] = J['haL'] + V(0.04 * H * math.sin(math.pi * q), 0.10 * H * math.sin(math.pi * q), -0.16 * H * math.sin(math.pi * q))
        hunch += hx
        jaw = 0.15 + 0.75 * math.sin(math.pi * min(1, q * 1.2))
    elif st == 'hurt':
        k = 1 - t
        hunch -= 0.28 * k
        for nm in upper:
            J[nm] = J[nm] + V(0.05 * H * k * (J[nm][1] - 0.5 * H) / (0.4 * H), 0, 0)
        J['haL'] = J['haL'] + V(-0.04 * H * k, 0.12 * H * k, 0)
        J['haR'] = J['haR'] + V(0.04 * H * k, 0.12 * H * k, 0)
        jaw = 0.6 * k + 0.15
    elif st == 'cast':
        k = math.sin(math.pi * min(1.0, t * 1.6)) if t < 0.62 else 1.0 - (t - 0.62) / 0.38 * 0.4
        J['haL'] = lerp(J['haL'], V(-sh * 0.4, 0.98 * H, -0.24 * H), k)
        J['haR'] = lerp(J['haR'], V(sh * 0.4, 0.98 * H, -0.24 * H), k)
        J['elL'] = lerp(J['elL'], V(-sh - 0.02 * H, 0.84 * H, -0.12 * H), k)
        J['elR'] = lerp(J['elR'], V(sh + 0.02 * H, 0.84 * H, -0.12 * H), k)
        hunch -= 0.12 * k
        J['castk'] = k
    else:  # die
        d = t
        drop = 0.36 * H * d
        J['knL'] = lerp(J['knL'], V(J['knL'][0] * 1.4, 0.07 * H, -0.18 * H), d)
        J['knR'] = lerp(J['knR'], V(J['knR'][0] * 1.4, 0.07 * H, -0.16 * H), d)
        for nm in ['pelvis', 'hiL', 'hiR'] + upper:
            J[nm] = J[nm] - V(0, drop, 0)
        hunch += 0.9 * d
        roll = 0.55 * d
        for nm in arms:
            J[nm] = lerp(J[nm], V(J[nm][0] * 1.5, 0.05 * H, J[nm][2] - 0.2 * H), d * 0.7)
        jaw = 0.5
    # apply hunch (lean towards viewer) around pelvis
    pv = J['pelvis']
    for nm in upper:
        J[nm] = rot_x(J[nm], pv, hunch)
    if roll:
        for nm in upper:
            J[nm] = rot_z(J[nm], pv, roll)
    J['jaw'] = jaw
    J['state'] = st; J['t'] = t
    return J


def draw_weapon(m, p, J, H):
    w = p.get('weapon')
    if not w:
        return
    hand = J['haR']; el = J['elR']
    d = norm(hand - el)
    if J['state'] != 'atk' or J['t'] < 0.4 or J['t'] > 0.85:
        d = norm(d + V(0.25, 1.1, -0.2))
    up = norm(np.cross(d, V(0, 0, 1)) + V(0, 0.001, 0))
    bl = p.get('wmat', 'metal'); bp = p.get('wp', {'col': C(120, 116, 112)})
    if w in ('sword', 'greatsword', 'dagger', 'cleaver', 'scythe'):
        ln = {'sword': 0.45, 'greatsword': 0.62, 'dagger': 0.2, 'cleaver': 0.28, 'scythe': 0.5}[w] * H
        wd = {'sword': 0.018, 'greatsword': 0.03, 'dagger': 0.012, 'cleaver': 0.06, 'scythe': 0.016}[w] * H
        tip = hand + d * ln
        m.cap(hand - d * 0.06 * H, hand + d * 0.02 * H, 0.012 * H, 0.012 * H, 'wood')
        m.cap(hand + d * 0.02 * H - up * 0.05 * H, hand + d * 0.02 * H + up * 0.05 * H, 0.01 * H, 0.01 * H, bl, bp)
        if w == 'cleaver':
            a = hand + d * 0.06 * H; b = tip
            m.quad(a, b, b + up * wd, a + up * wd, bl, dict(bp, bloody=True))
        else:
            m.cap(hand + d * 0.03 * H, tip, wd, wd * 0.3, bl, dict(bp, bloody=True))
        if w == 'scythe':
            m.cap(tip, tip + up * 0.25 * H - d * 0.05 * H, 0.02 * H, 0.004 * H, bl, bp)
    elif w in ('axe', 'spear', 'staff', 'crook'):
        ln = {'axe': 0.45, 'spear': 0.75, 'staff': 0.7, 'crook': 0.7}[w] * H
        base = hand - d * 0.15 * H if w != 'axe' else hand - d * 0.05 * H
        tip = hand + d * (ln - 0.15 * H)
        m.cap(base, tip, 0.013 * H, 0.011 * H, 'wood')
        if w == 'axe':
            a = tip - d * 0.1 * H
            m.quad(a, tip, tip + up * 0.14 * H + d * 0.04 * H, a + up * 0.14 * H - d * 0.04 * H, bl, dict(bp, bloody=True))
        elif w == 'spear':
            m.tri(tip, tip + d * 0.14 * H + up * 0.0, tip + up * 0.035 * H, bl, bp)
            m.tri(tip, tip + d * 0.14 * H, tip - up * 0.035 * H, bl, bp)
        elif w == 'staff':
            m.ell(tip + d * 0.03 * H, 0.045 * H, 0.05 * H, 0.045 * H, 'bone', {'col': C(200, 190, 160)})
            m.ell(tip + d * 0.03 * H + V(-0.015 * H, 0.0, -0.04 * H), 0.012 * H, 0.012 * H, 0.01 * H, 'eye', {'col': p.get('eyec', C(255, 80, 20))})
            m.ell(tip + d * 0.03 * H + V(0.015 * H, 0.0, -0.04 * H), 0.012 * H, 0.012 * H, 0.01 * H, 'eye', {'col': p.get('eyec', C(255, 80, 20))})
        else:
            m.cap(tip, tip + up * 0.1 * H, 0.012 * H, 0.012 * H, 'gold')
    elif w == 'claws':
        for i in (-1, 0, 1):
            a = hand + up * 0.012 * H * i
            m.cap(a, a + d * 0.11 * H + up * 0.02 * H * i - V(0, 0, 0.02 * H), 0.006 * H, 0.002 * H, 'bone', {'col': C(60, 50, 40)})
    if p.get('shield'):
        hl = J['haL']
        m.ell(hl + V(-0.01 * H, 0.04 * H, -0.05 * H), 0.075 * H, 0.085 * H, 0.02 * H, p.get('shmat', 'metal'), p.get('shp', {'col': C(100, 90, 80)}))
        m.ell(hl + V(-0.01 * H, 0.04 * H, -0.07 * H), 0.022 * H, 0.022 * H, 0.015 * H, 'metal', {'col': C(150, 140, 120)})


def ribcage(m, J, H, w, mat='bone', p=None, cavity=True):
    c = J['chest']; up = norm(J['neck'] - J['pelvis'])
    if cavity:
        m.ell(c + V(0, -0.02 * H, -0.035 * H), 0.10 * H * w, 0.09 * H, 0.03 * H, 'raw')
    for i in range(6):
        y = c + up * (0.075 * H - i * 0.027 * H)
        rr = (0.105 - abs(i - 2) * 0.006) * H * w
        pts = []
        for a in np.linspace(-1.25, 1.25, 7):
            pts.append(y + V(rr * math.sin(a), -abs(a) * 0.02 * H, -0.065 * H * math.cos(a) + 0.01 * H))
        for k in range(len(pts) - 1):
            m.cap(pts[k], pts[k + 1], 0.009 * H, 0.009 * H, mat, p)
    m.cap(c + up * 0.08 * H + V(0, 0, -0.06 * H), c - up * 0.08 * H + V(0, 0, -0.06 * H), 0.012 * H, 0.01 * H, mat, p)


def head(m, p, J, H):
    hd = J['head']; nk = J['neck']
    hr = p.get('headr', 1.2) * H
    skin = p.get('skin', 'rot'); sp = p.get('sp', {})
    eyes = p.get('eyec', C(255, 60, 20))
    up = norm(hd - nk)
    fwd = V(0, 0, -1)
    jaw = J['jaw']
    if p.get('headless'):
        m.cap(nk - up * 0.02 * H, nk + up * 0.025 * H, 0.036 * H, 0.04 * H, skin, sp)
        m.ell(nk + up * 0.03 * H, 0.034 * H, 0.012 * H, 0.032 * H, 'raw')
        m.cap(nk + up * 0.03 * H, nk + up * 0.045 * H, 0.008 * H, 0.006 * H, 'bone')
        return
    if p.get('skull'):
        m.ell(hd + up * 0.01 * H, 0.055 * hr, 0.062 * hr, 0.06 * hr, 'bone', sp)
        m.ell(hd + V(0, -0.035 * hr, -0.02 * hr), 0.04 * hr, 0.03 * hr, 0.045 * hr, 'bone', sp)
        for sx in (-1, 1):
            m.ell(hd + V(sx * 0.022 * hr, 0.004 * hr, -0.048 * hr), 0.016 * hr, 0.019 * hr, 0.01 * hr, 'dark')
            m.ell(hd + V(sx * 0.022 * hr, 0.004 * hr, -0.052 * hr), 0.006 * hr, 0.006 * hr, 0.004 * hr, 'eye', {'col': eyes})
        m.ell(hd + V(0, -0.022 * hr, -0.056 * hr), 0.007 * hr, 0.01 * hr, 0.005 * hr, 'dark')
        jy = -0.06 * hr - jaw * 0.04 * hr
        m.ell(hd + V(0, jy, -0.03 * hr), 0.035 * hr, 0.016 * hr, 0.035 * hr, 'bone', sp)
        for i in range(-3, 4):
            m.cap(hd + V(i * 0.008 * hr, -0.048 * hr, -0.06 * hr), hd + V(i * 0.008 * hr, -0.056 * hr, -0.06 * hr), 0.004 * hr, 0.003 * hr, 'teeth')
            m.cap(hd + V(i * 0.008 * hr, jy + 0.012 * hr, -0.058 * hr), hd + V(i * 0.008 * hr, jy + 0.004 * hr, -0.058 * hr), 0.004 * hr, 0.003 * hr, 'teeth')
    else:
        m.cap(nk - up * 0.02 * H, hd - up * 0.03 * H, 0.033 * H * p.get('neckw', 1.0), 0.03 * H, skin, sp)
        m.ell(hd + up * 0.005 * H, 0.052 * hr, 0.066 * hr, 0.058 * hr, skin, sp)
        # brow + cheekbones
        m.ell(hd + V(0, 0.016 * hr, -0.045 * hr), 0.048 * hr, 0.014 * hr, 0.018 * hr, skin, sp)
        for sx in (-1, 1):
            m.ell(hd + V(sx * 0.02 * hr, 0.0, -0.05 * hr), 0.014 * hr, 0.011 * hr, 0.008 * hr, 'dark')
            m.ell(hd + V(sx * 0.02 * hr, 0.0, -0.054 * hr), 0.007 * hr, 0.006 * hr, 0.004 * hr, 'eye', {'col': eyes})
        m.ell(hd + V(0, -0.012 * hr, -0.058 * hr), 0.01 * hr, 0.016 * hr, 0.01 * hr, skin, sp)
        m.cap(hd + V(0, 0.008 * hr, -0.056 * hr), hd + V(0, -0.016 * hr, -0.064 * hr), 0.005 * hr, 0.007 * hr, skin, sp)      # nose bridge
        for sx in (-1, 1):
            m.ell(hd + V(sx * 0.03 * hr, -0.016 * hr, -0.046 * hr), 0.014 * hr, 0.01 * hr, 0.01 * hr, skin, sp)           # cheekbones
            if not p.get('ears'):
                m.ell(hd + V(sx * 0.052 * hr, -0.002 * hr, 0.004 * hr), 0.007 * hr, 0.016 * hr, 0.011 * hr, skin, sp)      # ears
                m.ell(hd + V(sx * 0.055 * hr, -0.002 * hr, 0.002 * hr), 0.003 * hr, 0.009 * hr, 0.005 * hr, 'dark')
        # mouth & jaw
        mo = 0.012 + 0.03 * jaw
        m.ell(hd + V(0, -0.04 * hr - mo * hr * 0.5, -0.05 * hr), 0.026 * hr, (mo + 0.006) * hr, 0.012 * hr, 'mouth')
        for i in range(-3, 4):
            m.cap(hd + V(i * 0.0075 * hr, -0.036 * hr, -0.058 * hr), hd + V(i * 0.0075 * hr, -0.044 * hr, -0.06 * hr), 0.0035 * hr, 0.001 * hr, 'teeth')
            m.cap(hd + V(i * 0.0075 * hr, -0.046 * hr - mo * hr, -0.058 * hr), hd + V(i * 0.0075 * hr, -0.038 * hr - mo * hr, -0.06 * hr), 0.0035 * hr, 0.001 * hr, 'teeth')
        if p.get('jawless'):
            m.ell(hd + V(0, -0.058 * hr - mo * hr, -0.035 * hr), 0.03 * hr, 0.012 * hr, 0.03 * hr, 'raw')
        else:
            m.ell(hd + V(0, -0.058 * hr - mo * hr, -0.03 * hr), 0.038 * hr, 0.014 * hr, 0.035 * hr, skin, sp)
        if p.get('blood_mouth'):
            m.cap(hd + V(-0.01 * hr, -0.06 * hr - mo * hr, -0.062 * hr), hd + V(-0.012 * hr, -0.12 * hr, -0.06 * hr), 0.005 * hr, 0.002 * hr, 'blood')
            m.cap(hd + V(0.012 * hr, -0.06 * hr - mo * hr, -0.062 * hr), hd + V(0.014 * hr, -0.1 * hr, -0.06 * hr), 0.004 * hr, 0.002 * hr, 'blood')
        if p.get('ears'):
            for sx in (-1, 1):
                m.tri(hd + V(sx * 0.045 * hr, 0.02 * hr, 0), hd + V(sx * 0.13 * hr, 0.07 * hr, 0.02 * hr), hd + V(sx * 0.045 * hr, -0.02 * hr, 0.01 * hr), skin, sp)
    if p.get('horns'):
        hs = p['horns']
        for sx in (-1, 1):
            pts = [hd + V(sx * 0.035 * hr, 0.05 * hr, -0.01 * hr)]
            for i in range(1, 5):
                a = i / 4.0
                pts.append(hd + V(sx * (0.035 + 0.06 * a * hs) * hr, (0.05 + 0.09 * a * hs - 0.03 * a * a * hs) * hr, (-0.01 + 0.02 * a) * hr))
            m.chain(pts, 0.016 * hr * hs, 0.003 * hr, 'bone', {'col': C(60, 50, 44), 'bloody': False})
    if p.get('hood'):
        m.ell(hd + V(0, 0.01 * hr, 0.012 * hr), 0.07 * hr, 0.085 * hr, 0.07 * hr, 'robe', p.get('robep', {}))
        m.ell(hd + V(0, -0.005 * hr, -0.045 * hr), 0.042 * hr, 0.055 * hr, 0.02 * hr, 'dark')
        for sx in (-1, 1):
            m.ell(hd + V(sx * 0.016 * hr, 0.0, -0.06 * hr), 0.006 * hr, 0.005 * hr, 0.003 * hr, 'eye', {'col': eyes})
    if p.get('crown'):
        for i in range(7):
            a = -1.2 + i * 0.4
            base = hd + V(0.055 * hr * math.sin(a), 0.05 * hr, -0.055 * hr * math.cos(a))
            m.cap(base, base + V(0, 0.035 * hr, 0), 0.008 * hr, 0.002 * hr, 'gold')
        m.cap(hd + V(-0.055 * hr, 0.05 * hr, 0), hd + V(0.055 * hr, 0.05 * hr, 0), 0.008 * hr, 0.008 * hr, 'gold')
    if p.get('mask'):
        m.ell(hd + V(0, -0.005 * hr, -0.05 * hr), 0.045 * hr, 0.06 * hr, 0.015 * hr, 'gold')
        for sx in (-1, 1):
            m.ell(hd + V(sx * 0.018 * hr, 0.0, -0.064 * hr), 0.008 * hr, 0.006 * hr, 0.003 * hr, 'eye', {'col': eyes})
    if p.get('helm'):
        m.ell(hd + V(0, 0.02 * hr, 0.0), 0.062 * hr, 0.06 * hr, 0.064 * hr, 'metal', p.get('helmp', {}))
        m.ell(hd + V(0, -0.01 * hr, -0.06 * hr), 0.04 * hr, 0.006 * hr, 0.006 * hr, 'dark')
    if p.get('feathers'):
        for i in range(5):
            a = -0.8 + i * 0.4
            b = hd + V(0.04 * hr * math.sin(a), 0.05 * hr, 0.01 * hr)
            m.tri(b, b + V(0.06 * hr * math.sin(a) - 0.01 * hr, 0.13 * hr, 0.02 * hr), b + V(0.06 * hr * math.sin(a) + 0.01 * hr, 0.12 * hr, 0.02 * hr), 'skin', {'col': C(150, 30, 20) if i % 2 else C(40, 90, 60), 'bloody': False})


def draw_wings(m, p, J, H, st, t):
    wc = p.get('wings')
    if not wc:
        return
    ws = p.get('wingsize', 1.0)
    flap = 0.0
    if st == 'idle':
        flap = 0.15 * sn(t)
    elif st == 'atk':
        flap = 0.4 * math.sin(math.pi * t)
    elif st == 'die':
        flap = -0.5 * t
    for sx in (-1, 1):
        root = J['chest'] + V(sx * 0.06 * H, 0.05 * H, 0.06 * H)
        elbow = root + V(sx * 0.22 * H * ws, (0.18 + flap) * H * ws, 0.06 * H)
        tips = []
        for i, (ax, ay) in enumerate([(0.42, 0.28), (0.44, 0.08), (0.36, -0.12), (0.22, -0.26)]):
            tips.append(root + V(sx * ax * H * ws, (ay + flap * (1 - i * 0.2)) * H * ws, 0.1 * H))
        m.cap(root, elbow, 0.018 * H, 0.012 * H, 'skin', wc)
        prev = elbow
        lowest = root + V(sx * 0.05 * H, -0.2 * H * ws, 0.06 * H)
        pts = tips + [lowest]
        for i, tp in enumerate(tips):
            m.cap(elbow, tp, 0.007 * H, 0.002 * H, 'bone', {'col': C(70, 50, 44), 'bloody': False})
        for i in range(len(pts) - 1):
            m.tri(elbow, pts[i], pts[i + 1], 'membrane', wc)
        m.tri(root, elbow, lowest, 'membrane', wc)


def biped(m, p, f):
    H = p['H']; b = p.get('build', 1.0)
    J = biped_pose(p, f)
    st, t = J['state'], J['t']
    skin = p.get('skin', 'rot'); sp = p.get('sp', {})
    bonesonly = p.get('skeleton', False)
    limb = 'bone' if bonesonly else skin
    lp = p.get('bp', {}) if bonesonly else sp
    th = 0.35 if bonesonly else 1.0
    if p.get('fat'):
        th = 1.25
    draw_wings(m, p, J, H, st, t)
    if st == 'die':
        m.ell(V(0, 0.002 * H, -0.05 * H), 0.25 * H * t, 0.02 * H, 0.12 * H * t, 'blood')
    # legs
    for s in 'LR':
        hi, kn, ft = J['hi' + s], J['kn' + s], J['ft' + s]
        lgm = p.get('legmat', limb); lgp = p.get('legp', lp)
        if bonesonly or p.get('fat'):
            m.cap(hi, kn, 0.058 * H * th * b, 0.04 * H * th * b, lgm, lgp)
            m.cap(kn, ft, 0.04 * H * th * b, 0.026 * H * th, p.get('bootmat', lgm), p.get('bootp', lgp))
            if bonesonly:
                m.ell(kn + V(0, 0, -0.012 * H), 0.016 * H, 0.018 * H, 0.012 * H, 'bone', lp)
        else:
            muscled_limb(m, hi, kn, 0.058 * H * th * b, 0.04 * H * th * b, lgm, lgp, 'thigh', 3.1 if s == 'L' else 5.7)
            muscled_limb(m, kn, ft, 0.04 * H * th * b, 0.026 * H * th, p.get('bootmat', lgm), p.get('bootp', lgp), 'calf', 1.3 if s == 'L' else 2.9)
        if bonesonly:
            for i in range(5):
                x = (i - 2) * 0.009 * H
                m.cap(ft + V(x, 0.012 * H, -0.01 * H), ft + V(x * 1.3, 0.004 * H, -0.075 * H), 0.004 * H, 0.0035 * H, 'bone', lp)
            m.ell(ft + V(0, 0.014 * H, 0.012 * H), 0.012 * H, 0.012 * H, 0.016 * H, 'bone', lp)
        else:
            barefoot = 'bootmat' not in p and p.get('legmat', limb) == limb
            foot(m, ft, H, th, p.get('bootmat', lgm), p.get('bootp', lgp), toes=barefoot and not p.get('claws_feet'))
        if p.get('claws_feet'):
            for i in (-1, 0, 1):
                m.cap(ft + V(i * 0.012 * H, 0.005 * H, -0.07 * H), ft + V(i * 0.014 * H, 0.0, -0.1 * H), 0.004 * H, 0.001 * H, 'bone', {'col': C(50, 40, 30)})
    # torso
    if bonesonly:
        spine = [J['pelvis'], lerp(J['pelvis'], J['chest'], 0.5), J['chest'], J['neck']]
        for i in range(len(spine) - 1):
            for k in range(4):
                c = lerp(spine[i], spine[i + 1], k / 4.0)
                m.ell(c + V(0, 0, 0.01 * H), 0.016 * H, 0.011 * H, 0.016 * H, 'bone', lp)
        m.ell(J['pelvis'] + V(0, 0.0, 0.0), 0.085 * H * b, 0.045 * H, 0.04 * H, 'bone', lp)
        m.ell(J['pelvis'] + V(0, 0.0, -0.035 * H), 0.03 * H, 0.02 * H, 0.01 * H, 'dark')
        ribcage(m, J, H, b, 'bone', lp, cavity=False)
        m.cap(J['shL'], J['shR'], 0.012 * H, 0.012 * H, 'bone', lp)
    else:
        m.cap(J['pelvis'], J['belly'], 0.085 * H * b, 0.08 * H * b, skin, sp)
        m.ell(J['chest'] + V(0, -0.005 * H, 0), 0.125 * H * b, 0.105 * H, 0.075 * H, skin, sp)
        m.ell(lerp(J['pelvis'], J['chest'], 0.45), 0.095 * H * b, 0.09 * H, 0.065 * H, skin, sp)
        m.ell(J['pelvis'], 0.095 * H * b, 0.06 * H, 0.06 * H, p.get('loin', skin), p.get('loinp', sp))
        if not p.get('fat') and skin not in ('rag',):
            torso_detail(m, J, H, b, skin, sp, 9.0)
        if p.get('fat'):
            m.ell(J['belly'] + V(0, -0.02 * H, -0.04 * H), 0.15 * H * b, 0.14 * H, 0.12 * H, skin, sp)
            m.ell(J['belly'] + V(-0.04 * H, -0.04 * H, -0.15 * H), 0.03 * H, 0.025 * H, 0.012 * H, 'raw')
        if p.get('ribs'):
            ribcage(m, J, H, b, 'bone', {'col': C(200, 180, 150)}, cavity=True)
        if p.get('pecs', False):
            for sx in (-1, 1):
                m.ell(J['chest'] + V(sx * 0.05 * H * b, 0.01 * H, -0.06 * H), 0.055 * H * b, 0.045 * H, 0.03 * H, skin, sp)
            for i in range(3):
                for sx in (-1, 1):
                    m.ell(lerp(J['pelvis'], J['chest'], 0.25 + i * 0.18) + V(sx * 0.03 * H, 0, -0.06 * H), 0.026 * H, 0.022 * H, 0.015 * H, skin, sp)
        if p.get('gut'):
            g0 = J['belly'] + V(0.02 * H, -0.03 * H, -0.06 * H)
            pts = [g0]
            for i in range(1, 6):
                pts.append(g0 + V(0.015 * H * math.sin(i * 1.7 + f * 0.4), -0.05 * H * i, -0.02 * H * math.sin(i)))
            m.ell(g0, 0.045 * H, 0.035 * H, 0.02 * H, 'raw')
            m.chain(pts, 0.018 * H, 0.012 * H, 'gut')
        for w in p.get('wounds', []):
            c = J[w[0]] + V(w[1] * H, w[2] * H, -0.066 * H * w[4] if len(w) > 4 else -0.056 * H)
            m.ell(c, w[3] * H * 1.1, w[3] * H * 0.85, w[3] * H * 0.28, 'raw')
            m.ell(c + V(0, -w[3] * H * 0.2, -w[3] * H * 0.12), w[3] * H * 0.5, w[3] * H * 0.3, w[3] * H * 0.15, 'blood')
            m.cap(c + V(w[3] * H * 0.2, -w[3] * H * 0.6, -w[3] * H * 0.12), c + V(w[3] * H * 0.3, -w[3] * H * 2.6, -w[3] * H * 0.05), w[3] * H * 0.18, w[3] * H * 0.08, 'blood')
        if p.get('apron'):
            a0 = J['chest'] + V(-0.1 * H, 0.0, -0.09 * H); a1 = J['chest'] + V(0.1 * H, 0.0, -0.09 * H)
            a2 = J['pelvis'] + V(0.11 * H, -0.18 * H, -0.12 * H); a3 = J['pelvis'] + V(-0.11 * H, -0.18 * H, -0.12 * H)
            m.quad(a0, a1, a2, a3, 'rag', {'col': C(150, 130, 110)})
        if p.get('armor'):
            ap = p.get('armorp', {})
            m.ell(J['chest'] + V(0, 0.0, -0.01 * H), 0.135 * H * b, 0.11 * H, 0.085 * H, 'metal', ap)
            for s in 'LR':
                m.ell(J['sh' + s] + V(0, 0.015 * H, 0), 0.055 * H, 0.045 * H, 0.05 * H, 'metal', ap)
            m.ell(J['pelvis'] + V(0, -0.02 * H, -0.01 * H), 0.1 * H * b, 0.05 * H, 0.07 * H, 'metal', ap)
    if p.get('robe'):
        rp = p.get('robep', {})
        top = J['chest'] + V(0, 0.06 * H, 0)
        n = 7
        hem = []
        for i in range(n + 1):
            a = -1.4 + 2.8 * i / n
            jag = (0.03 if i % 2 else -0.02) * H + 0.02 * H * math.sin(f * 0.8 + i)
            hem.append(J['pelvis'] + V(0.17 * H * math.sin(a), -0.47 * H + jag + (J['pelvis'][1] - 0.53 * H), -0.12 * H * math.cos(a)))
        for i in range(n):
            a0 = -1.1 + 2.2 * i / n; a1 = -1.1 + 2.2 * (i + 1) / n
            t0 = top + V(0.13 * H * math.sin(a0), 0, -0.08 * H * math.cos(a0))
            t1 = top + V(0.13 * H * math.sin(a1), 0, -0.08 * H * math.cos(a1))
            m.quad(t0, t1, hem[i + 1], hem[i], 'robe', rp)
    # arms
    for s in 'LR':
        if p.get('armless') == s:
            m.ell(J['sh' + s] + V(0, -0.02 * H, -0.01 * H), 0.035 * H, 0.03 * H, 0.03 * H, 'raw')
            m.cap(J['sh' + s], J['sh' + s] + V(0, -0.05 * H, -0.01 * H), 0.008 * H, 0.004 * H, 'bone')
            continue
        sh_, el, ha = J['sh' + s], J['el' + s], J['ha' + s]
        aw = p.get('armw', 1.0) * th
        if not bonesonly:
            m.ell(sh_, 0.05 * H * b * max(aw, 0.6), 0.045 * H, 0.05 * H, skin, sp)
        hd = norm(ha - el)
        if bonesonly:
            m.cap(sh_, el, 0.04 * H * aw, 0.032 * H * aw, p.get('armmat', limb), p.get('armp', lp))
            m.cap(el, ha, 0.032 * H * aw, 0.024 * H * aw, p.get('bracer', p.get('armmat', limb)), p.get('bracerp', p.get('armp', lp)))
            m.cap(el + V(0.006 * H, 0, 0), ha + V(0.006 * H, 0, 0), 0.008 * H, 0.007 * H, 'bone', lp)
            hand(m, ha, hd, H, 'bone', lp, grip=0.8 if (s == 'R' and p.get('weapon')) else 0.2, w=0.75)
        else:
            muscled_limb(m, sh_, el, 0.04 * H * aw, 0.032 * H * aw, p.get('armmat', limb), p.get('armp', lp), 'upper', 4.4 if s == 'L' else 6.6)
            muscled_limb(m, el, ha, 0.032 * H * aw, 0.024 * H * aw, p.get('bracer', p.get('armmat', limb)), p.get('bracerp', p.get('armp', lp)), 'fore', 7.7 if s == 'L' else 8.8)
            if not p.get('claws'):
                hand(m, ha, hd, H, limb, lp, grip=0.85 if (s == 'R' and p.get('weapon')) else 0.25, w=max(aw, 0.6),
                     nails=C(70, 56, 40) if skin in ('rot', 'rag') else None, seed=2.2 if s == 'L' else 3.3)
            else:
                m.ell(ha + hd * 0.02 * H, 0.025 * H * max(aw, 0.6), 0.03 * H, 0.02 * H, limb, lp)
        if p.get('claws'):
            for i in (-1, 0, 1):
                a = ha + hd * 0.035 * H + V(i * 0.01 * H, 0, 0)
                m.cap(a, a + hd * 0.06 * H * (1.6 if p.get('claws') else 0.8) + V(i * 0.01 * H, 0, -0.01 * H), 0.005 * H, 0.0015 * H,
                      'bone', {'col': C(60, 48, 40) if p.get('claws') else C(200, 190, 160)})
        if p.get('bloody_hands'):
            m.ell(ha + hd * 0.02 * H + V(0, 0, -0.02 * H), 0.022 * H, 0.025 * H, 0.008 * H, 'blood')
    head(m, p, J, H)
    if st == 'cast':
        k = J.get('castk', 0)
        c = lerp(J['haL'], J['haR'], 0.5) + V(0, 0.05 * H, -0.04 * H)
        m.ell(c, 0.05 * H * k + 0.005 * H, 0.05 * H * k + 0.005 * H, 0.05 * H * k + 0.005 * H, 'eye', {'col': C(140, 200, 255)})
    if p.get('cape'):
        cp = p['cape']
        sL = J['shL'] + V(0.03 * H, 0.0, 0.07 * H); sR = J['shR'] + V(-0.03 * H, 0.0, 0.07 * H)
        n = 6
        for i in range(n):
            a0 = i / n; a1 = (i + 1) / n
            t0 = lerp(sL, sR, a0); t1 = lerp(sL, sR, a1)
            sway = 0.02 * H * math.sin(f * 0.8 + i)
            lift = 0.12 * H if st == 'atk' else 0.0
            b0 = V(t0[0] * 1.15, 0.36 * H + (0.025 * H if i % 2 else 0), J['pelvis'][2] + 0.11 * H + lift + sway)
            b1 = V(t1[0] * 1.15, 0.36 * H + (0.025 * H if (i + 1) % 2 else 0), J['pelvis'][2] + 0.11 * H + lift + sway)
            m.quad(t0, t1, b1, b0, 'robe', cp)
    if p.get('hair'):
        hd = J['head']
        m.ell(hd + V(0, 0.03 * H, 0.012 * H), 0.064 * H, 0.06 * H, 0.066 * H, 'fur', p['hair'])
        for i in range(5):
            a = -0.9 + i * 0.45
            m.cap(hd + V(0.04 * H * math.sin(a), 0.06 * H, 0.02 * H), hd + V(0.06 * H * math.sin(a), 0.0, 0.07 * H), 0.018 * H, 0.004 * H, 'fur', p['hair'])
    if s and p.get('tail'):
        pv = J['pelvis']
        pts = [pv + V(0, -0.02 * H, 0.05 * H)]
        for i in range(1, 6):
            pts.append(pv + V(0.05 * H * math.sin(i * 0.8 + f * 0.6), -0.05 * H * i + 0.0, 0.05 * H + 0.03 * H * i))
        m.chain(pts, 0.025 * H, 0.004 * H, skin, sp)
    if p.get('fire'):
        draw_fire(m, J['chest'], H, f, p.get('fire'))
    if p.get('weapon') or p.get('shield'):
        draw_weapon(m, p, J, H)
    if p.get('rags'):
        for k, nm in enumerate(['haL', 'haR', 'elL', 'elR', 'pelvis']):
            b0 = J[nm]
            m.tri(b0 + V(-0.012 * H, 0, -0.03 * H), b0 + V(0.012 * H, 0, -0.03 * H), b0 + V(0.004 * H * math.sin(f + k), -0.09 * H, -0.03 * H), 'rag', p.get('sp', {}))


def draw_fire(m, c, H, f, amount=1.0):
    for i in range(7):
        a = i * 0.9 + f * 0.7
        b = c + V(0.09 * H * math.sin(a * 1.3), -0.12 * H + 0.06 * H * math.cos(a), -0.08 * H)
        h = (0.12 + 0.06 * math.sin(a * 2.1)) * H * amount
        m.tri(b + V(-0.025 * H, 0, 0), b + V(0.025 * H, 0, 0), b + V(0.01 * H * math.sin(a * 3), h, -0.01 * H), 'fire', seed=i)


# =================================================================== QUADRUPED
def quadruped(m, p, f):
    L = p['L']; h = p['h']
    st, t = phase(f)
    fur = p.get('skin', 'fur'); sp = p.get('sp', {})
    lunge = 0.0; head_dn = 0.0; jaw = 0.2; roll = 0.0; drop = 0.0; frise = 0.0; recoil = 0.0
    if st == 'idle':
        breath = sn(t)
        head_dn = 0.02 * h * sn(t + 0.3)
    elif st == 'atk':
        lunge = 0.3 * L * math.sin(math.pi * t)
        frise = 0.25 * h * math.sin(math.pi * min(1, t * 1.3))
        head_dn = -0.05 * h * math.sin(math.pi * t)
        jaw = 0.2 + 0.8 * math.sin(math.pi * t)
    elif st == 'hurt':
        recoil = 0.12 * L * (1 - t); head_dn = -0.06 * h * (1 - t); jaw = 0.6 * (1 - t)
    else:
        drop = 0.42 * h * t; roll = 1.1 * t; jaw = 0.5
        m.ell(V(0, 0.002 * h, 0.0), 0.5 * L * t, 0.02 * h, 0.3 * L * t, 'blood')
    dz = -lunge + recoil
    sh_ = V(0, 0.64 * h - drop, -0.28 * L + dz)
    hp_ = V(0, 0.60 * h - drop, 0.30 * L + dz)
    def R(P):
        return rot_z(P, V(0, 0.0, 0), roll * 0.7) if roll else P
    # legs
    for sx in (-1, 1):
        x = sx * 0.11 * h * p.get('build', 1.0)
        ph = sn(t + (0 if sx < 0 else 0.5)) if st == 'idle' else 0
        fa = V(x, 0.58 * h - drop, -0.28 * L + dz)
        fe = V(x * 1.05, 0.3 * h - drop * 0.6 + frise * 0.6, -0.30 * L + dz - frise * 0.6)
        fp = V(x * 1.1, 0.02 * h + frise + 0.01 * h * ph, -0.33 * L + dz - frise * 0.9)
        if st == 'die':
            fe = fe + V(sx * 0.1 * h * t, 0, 0); fp = fp + V(sx * 0.2 * h * t, 0.1 * h * t, 0)
        m.cap(R(fa), R(fe), 0.06 * h, 0.04 * h, fur, sp)
        m.cap(R(fe), R(fp), 0.04 * h, 0.03 * h, fur, sp)
        m.ell(R(fp) + V(0, 0, -0.03 * h), 0.035 * h, 0.02 * h, 0.05 * h, fur, sp)
        for i in (-1, 0, 1):
            m.cap(R(fp) + V(i * 0.015 * h, 0, -0.07 * h), R(fp) + V(i * 0.017 * h, -0.01 * h, -0.1 * h), 0.006 * h, 0.001 * h, 'bone', {'col': C(40, 34, 28)})
        ba = V(x, 0.56 * h - drop, 0.30 * L + dz)
        bk = V(x * 1.05, 0.36 * h - drop * 0.6, 0.40 * L + dz)
        bh = V(x * 1.05, 0.14 * h - drop * 0.3, 0.30 * L + dz)
        bp = V(x * 1.1, 0.02 * h, 0.33 * L + dz)
        if st == 'die':
            bp = bp + V(sx * 0.2 * h * t, 0.1 * h * t, 0)
        m.cap(R(ba), R(bk), 0.075 * h, 0.05 * h, fur, sp)
        m.cap(R(bk), R(bh), 0.04 * h, 0.03 * h, fur, sp)
        m.cap(R(bh), R(bp), 0.03 * h, 0.028 * h, fur, sp)
    # body
    m.cap(R(sh_), R(hp_), 0.2 * h * p.get('build', 1.0), 0.17 * h * p.get('build', 1.0), fur, sp)
    m.ell(R(sh_ + V(0, 0.0, -0.02 * L)), 0.2 * h * p.get('build', 1.0), 0.22 * h, 0.2 * h, fur, sp)
    if p.get('ribs'):
        for i in range(5):
            z = sh_[2] + 0.08 * L + i * 0.06 * L
            for sx in (-1, 1):
                m.cap(R(V(sx * 0.17 * h, sh_[1] + 0.08 * h, z)), R(V(sx * 0.2 * h, sh_[1] - 0.1 * h, z + 0.02 * L)), 0.012 * h, 0.01 * h, 'bone', {'col': C(170, 150, 120)})
    if p.get('wounds'):
        for (a, b2, r) in p['wounds']:
            m.ell(R(V(a * h, sh_[1] + b2 * h, sh_[2] + 0.2 * L)), r * h, r * h * 0.8, r * h * 0.4, 'raw')
    if p.get('spikes'):
        for i in range(7):
            base = lerp(sh_, hp_, i / 6.0) + V(0, 0.17 * h, 0)
            m.cap(R(base), R(base + V(0.02 * h * math.sin(i), 0.18 * h, 0.05 * L)), 0.025 * h, 0.002 * h, 'bone', {'col': C(70, 60, 50), 'bloody': False})
    if p.get('fire'):
        draw_fire(m, R(sh_ + V(0, 0.15 * h, 0.1 * L)), h * 1.4, f, 1.0)
    # tail
    tp = [hp_ + V(0, 0.05 * h, 0.12 * L)]
    for i in range(1, 6):
        tp.append(hp_ + V(0.07 * h * math.sin(i * 0.7 + f * 0.8) * (1 if st != 'die' else 0.2), 0.05 * h + (0.04 * i if p.get('tailup') else -0.03 * i) * h, 0.12 * L + 0.07 * L * i))
    m.chain([R(q) for q in tp], 0.05 * h, 0.012 * h, fur, sp)
    # neck + head
    nb = sh_ + V(0, 0.12 * h, -0.08 * L)
    hb = V(0, 0.78 * h - drop + head_dn, -0.55 * L + dz)
    hl = p.get('headl', 1.0)
    sn_ = hb + V(0, -0.06 * h, -0.2 * L * hl)
    m.cap(R(nb), R(hb), 0.15 * h, 0.1 * h, fur, sp)
    m.ell(R(hb), 0.12 * h, 0.11 * h, 0.12 * h, fur, sp)
    m.cap(R(hb + V(0, 0.01 * h, -0.04 * L)), R(sn_), 0.085 * h, 0.05 * h, fur, sp)
    m.ell(R(sn_ + V(0, 0.01 * h, -0.02 * L)), 0.03 * h, 0.022 * h, 0.02 * h, 'dark')
    jawo = jaw * 0.09 * h
    m.cap(R(hb + V(0, -0.07 * h, -0.02 * L)), R(sn_ + V(0, -0.05 * h - jawo, 0.02 * L)), 0.06 * h, 0.035 * h, fur, sp)
    m.ell(R(lerp(hb, sn_, 0.6) + V(0, -0.05 * h - jawo * 0.5, -0.02 * L)), 0.05 * h, 0.012 * h + jawo * 0.5, 0.06 * h, 'mouth')
    for i in range(-3, 4):
        z = sn_[2] + 0.02 * L + abs(i) * 0.02 * L
        x = i * 0.013 * h
        m.cap(R(V(x, sn_[1] - 0.03 * h, z)), R(V(x, sn_[1] - 0.06 * h, z)), 0.007 * h, 0.001 * h, 'teeth')
        m.cap(R(V(x, sn_[1] - 0.075 * h - jawo, z)), R(V(x, sn_[1] - 0.048 * h - jawo, z)), 0.006 * h, 0.001 * h, 'teeth')
    m.cap(R(sn_ + V(0, -0.06 * h - jawo, -0.01 * L)), R(sn_ + V(0, -0.14 * h - jawo, -0.01 * L)), 0.008 * h, 0.002 * h, 'blood')
    for sx in (-1, 1):
        m.ell(R(hb + V(sx * 0.06 * h, 0.03 * h, -0.07 * L)), 0.018 * h, 0.012 * h, 0.01 * h, 'eye', {'col': p.get('eyec', C(255, 200, 40))})
        m.tri(R(hb + V(sx * 0.05 * h, 0.08 * h, 0.0)), R(hb + V(sx * 0.11 * h, 0.2 * h * p.get('earl', 1.0), 0.03 * L)), R(hb + V(sx * 0.1 * h, 0.06 * h, 0.04 * L)), fur, sp)
    if p.get('horns'):
        for sx in (-1, 1):
            m.chain([R(hb + V(sx * 0.07 * h, 0.06 * h, 0)), R(hb + V(sx * 0.18 * h, 0.12 * h, 0.02 * L)), R(hb + V(sx * 0.2 * h, 0.24 * h, -0.02 * L))], 0.035 * h, 0.004 * h, 'bone', {'col': C(70, 60, 50)})


# =================================================================== ARACHNID / INSECT
def arachnid(m, p, f):
    L = p['L']; h = p['h']
    st, t = phase(f)
    ch = p.get('skin', 'chitin'); sp = p.get('sp', {})
    rear = 0.0; fang = 0.2; curl = 0.0; drop = 0.0; lunge = 0.0
    if st == 'atk':
        rear = math.sin(math.pi * t); fang = 0.2 + 0.8 * math.sin(math.pi * t); lunge = 0.15 * L * math.sin(math.pi * t)
    elif st == 'hurt':
        rear = -0.3 * (1 - t); fang = 0.7 * (1 - t)
    elif st == 'die':
        curl = t; drop = 0.25 * h * t
        m.ell(V(0, 0.002 * h, 0.0), 0.6 * L * t, 0.02 * h, 0.35 * L * t, 'blood' if not p.get('ichor') else 'gut')
    tc = V(0, 0.38 * h - drop + rear * 0.15 * h, -0.18 * L - lunge)
    ab = V(0, 0.48 * h - drop, 0.32 * L)
    nlegs = p.get('legs', 8)
    pairs = nlegs // 2
    for sx in (-1, 1):
        for i in range(pairs):
            ph = i / max(1, pairs - 1)
            att = tc + V(sx * 0.12 * h, -0.02 * h, (ph - 0.5) * 0.18 * L)
            ang = (-0.9 + 1.6 * ph)
            out = 0.5 * L * p.get('legspan', 1.0)
            kn = att + V(sx * out * 0.45 * math.cos(ang * 0.3), 0.22 * h, (ang * 0.3) * L)
            ft = att + V(sx * out * 0.8 * math.cos(ang * 0.25), -att[1] + 0.01 * h, ang * 0.45 * L)
            if st == 'idle':
                lift = 0.04 * h * max(0, sn(t + i * 0.25 + (0.5 if sx > 0 else 0)))
                ft = ft + V(0, lift, 0); kn = kn + V(0, lift * 1.5, 0)
            if i == 0 and rear > 0:
                kn = kn + V(0, 0.35 * h * rear, -0.1 * L * rear)
                ft = ft + V(-sx * 0.15 * L * rear, 0.55 * h * rear, -0.25 * L * rear)
            if curl:
                kn = lerp(kn, att + V(sx * 0.12 * L, 0.2 * h, 0), curl)
                ft = lerp(ft, att + V(sx * 0.05 * L, 0.3 * h, -0.05 * L), curl)
            m.cap(att, kn, 0.035 * h, 0.025 * h, ch, sp)
            m.cap(kn, ft, 0.025 * h, 0.006 * h, ch, sp)
            m.ell(kn, 0.03 * h, 0.03 * h, 0.03 * h, ch, sp)
    # abdomen
    if p.get('scorpion'):
        segs = [ab + V(0, 0.02 * h, -0.05 * L)]
        for i in range(1, 7):
            a = i / 6.0
            strike = (rear if st == 'atk' else 0)
            segs.append(ab + V(0, (0.15 + 0.85 * math.sin(a * 2.4)) * h * 0.9 + strike * 0.2 * h * a, (0.15 * a - 0.75 * a * a - strike * 0.6 * a) * L))
        m.ell(ab, 0.22 * h, 0.16 * h, 0.3 * L, ch, sp)
        for i in range(len(segs) - 1):
            m.cap(segs[i], segs[i + 1], 0.09 * h * (1 - i * 0.1), 0.08 * h * (1 - i * 0.1), ch, sp)
        tip = segs[-1]
        m.ell(tip, 0.06 * h, 0.06 * h, 0.06 * h, ch, sp)
        m.cap(tip, tip + V(0, -0.08 * h, -0.12 * L), 0.03 * h, 0.002 * h, 'bone', {'col': C(40, 30, 20)})
    else:
        m.ell(ab, p.get('abw', 1.0) * 0.34 * h, 0.3 * h * p.get('abh', 1.0), 0.32 * L, ch, dict(sp, skull=p.get('skull', False)))
        if p.get('shell'):
            m.ell(ab + V(0, 0.02 * h, -0.02 * L), 0.38 * h, 0.3 * h, 0.4 * L, ch, sp)
            m.cap(ab + V(0, 0.3 * h, -0.2 * L), ab + V(0, 0.0, 0.2 * L), 0.004 * h, 0.004 * h, 'dark')
    m.ell(tc, 0.16 * h, 0.13 * h, 0.17 * L, ch, sp)
    # head, eyes, fangs
    hd = tc + V(0, 0.02 * h, -0.14 * L)
    m.ell(hd, 0.1 * h, 0.08 * h, 0.08 * L, ch, sp)
    for i in range(p.get('eyes', 8)):
        a = (i % 4 - 1.5) * 0.35; r = 0 if i < 4 else 1
        m.ell(hd + V(0.06 * h * math.sin(a) * (1 + 0.3 * r), 0.04 * h + 0.025 * h * r, -0.07 * L), 0.014 * h * (1.5 - r * 0.5), 0.014 * h * (1.5 - r * 0.5), 0.01 * h, 'eye', {'col': p.get('eyec', C(255, 40, 30))})
    if p.get('pincers'):
        for sx in (-1, 1):
            a = tc + V(sx * 0.12 * h, 0.0, -0.08 * L)
            b = a + V(sx * 0.18 * h, 0.12 * h + rear * 0.2 * h, -0.25 * L - rear * 0.15 * L)
            c = b + V(-sx * 0.06 * h, 0.04 * h, -0.2 * L)
            m.cap(a, b, 0.045 * h, 0.04 * h, ch, sp)
            m.cap(b, c, 0.04 * h, 0.05 * h, ch, sp)
            m.ell(c, 0.07 * h, 0.05 * h, 0.08 * L, ch, sp)
            m.cap(c, c + V(-sx * 0.02 * h, 0.05 * h + fang * 0.04 * h, -0.12 * L), 0.035 * h, 0.006 * h, ch, sp)
            m.cap(c, c + V(-sx * 0.02 * h, -0.03 * h - fang * 0.04 * h, -0.12 * L), 0.03 * h, 0.005 * h, ch, sp)
    for sx in (-1, 1):
        b = hd + V(sx * 0.035 * h, -0.05 * h, -0.06 * L)
        tip = b + V(sx * 0.02 * h * fang, -0.1 * h, -0.03 * L * (1 + fang))
        m.cap(b, tip, 0.022 * h, 0.004 * h, 'bone', {'col': C(50, 30, 30)})
        m.cap(tip, tip + V(0, -0.05 * h, 0), 0.006 * h, 0.002 * h, 'blood')
    if p.get('horn'):
        m.chain([hd + V(0, 0.06 * h, -0.04 * L), hd + V(0, 0.2 * h, -0.08 * L), hd + V(0, 0.28 * h, 0.0)], 0.04 * h, 0.006 * h, ch, sp)


# =================================================================== FLYER (bat)
def flyer(m, p, f):
    h = p['h']
    st, t = phase(f)
    fur = p.get('skin', 'fur'); sp = p.get('sp', {}); mem = p.get('mem', {'col': C(90, 60, 50)})
    alt = 0.55 * h; flap = 0.0; dz = 0.0; jaw = 0.3
    if st == 'idle':
        flap = 0.45 * sn(t); alt += 0.05 * h * sn(t + 0.25)
    elif st == 'atk':
        flap = 0.7 - 1.4 * t; dz = -0.35 * h * math.sin(math.pi * t); alt -= 0.15 * h * math.sin(math.pi * t); jaw = 0.3 + 0.7 * math.sin(math.pi * t)
    elif st == 'hurt':
        flap = 0.6 * (1 - t); alt += 0.08 * h * (1 - t)
    else:
        flap = -0.6 * t; alt = lerp(alt, 0.08 * h, t)
        m.ell(V(0, 0.002 * h, 0), 0.4 * h * t, 0.02 * h, 0.2 * h * t, 'blood')
    c = V(0, alt, dz)
    for sx in (-1, 1):
        sh_ = c + V(sx * 0.08 * h, 0.06 * h, 0.02 * h)
        wr = sh_ + V(sx * 0.32 * h, (0.25 * flap + 0.12) * h, 0.04 * h)
        tips = [wr + V(sx * 0.42 * h, (0.3 * flap + 0.1) * h, 0.05 * h), wr + V(sx * 0.48 * h, (0.1 * flap - 0.12) * h, 0.05 * h),
                wr + V(sx * 0.3 * h, (-0.1 * flap - 0.32) * h, 0.04 * h), wr + V(sx * 0.1 * h, -0.36 * h, 0.03 * h)]
        m.cap(sh_, wr, 0.03 * h, 0.02 * h, fur, sp)
        m.cap(wr, wr + V(sx * 0.0, 0.06 * h, 0), 0.012 * h, 0.004 * h, 'bone', {'col': C(60, 50, 40)})
        pts = [sh_] + tips + [c + V(sx * 0.06 * h, -0.18 * h, 0.03 * h)]
        for tp in tips:
            m.cap(wr, tp, 0.01 * h, 0.003 * h, 'bone', {'col': C(60, 46, 40), 'bloody': False})
        for i in range(1, len(pts) - 1):
            m.tri(wr, pts[i], pts[i + 1], 'membrane', mem)
        m.tri(sh_, wr, pts[-1], 'membrane', mem)
        m.tri(sh_, pts[-1], c + V(0, -0.08 * h, 0.03 * h), 'membrane', mem)
    m.ell(c, 0.11 * h, 0.15 * h, 0.1 * h, fur, sp)
    if p.get('ribs'):
        for i in range(4):
            m.cap(c + V(-0.08 * h, 0.06 * h - i * 0.035 * h, -0.08 * h), c + V(0.08 * h, 0.06 * h - i * 0.035 * h, -0.08 * h), 0.008 * h, 0.008 * h, 'bone', {'col': C(170, 150, 120)})
    hd = c + V(0, 0.18 * h, -0.04 * h)
    m.ell(hd, 0.085 * h, 0.08 * h, 0.08 * h, fur, sp)
    for sx in (-1, 1):
        m.tri(hd + V(sx * 0.04 * h, 0.05 * h, 0), hd + V(sx * 0.1 * h, 0.2 * h, 0.02 * h), hd + V(sx * 0.09 * h, 0.03 * h, 0.03 * h), 'membrane', mem)
        m.ell(hd + V(sx * 0.035 * h, 0.015 * h, -0.07 * h), 0.016 * h, 0.012 * h, 0.01 * h, 'eye', {'col': p.get('eyec', C(255, 40, 20))})
    m.ell(hd + V(0, -0.01 * h, -0.08 * h), 0.022 * h, 0.018 * h, 0.012 * h, fur, sp)
    mo = 0.02 * h + jaw * 0.03 * h
    m.ell(hd + V(0, -0.045 * h, -0.07 * h), 0.03 * h, mo, 0.015 * h, 'mouth')
    for sx in (-1, 1):
        m.cap(hd + V(sx * 0.015 * h, -0.035 * h, -0.08 * h), hd + V(sx * 0.016 * h, -0.075 * h, -0.08 * h), 0.006 * h, 0.001 * h, 'teeth')
        m.cap(hd + V(sx * 0.016 * h, -0.075 * h, -0.08 * h), hd + V(sx * 0.018 * h, -0.13 * h, -0.08 * h), 0.004 * h, 0.001 * h, 'blood')
    for sx in (-1, 1):
        lg = c + V(sx * 0.05 * h, -0.13 * h, 0)
        m.cap(lg, lg + V(sx * 0.02 * h, -0.12 * h, -0.05 * h - (0.1 * h if st == 'atk' else 0)), 0.018 * h, 0.01 * h, fur, sp)
    if p.get('fire'):
        draw_fire(m, c, h, f, 0.8)


# =================================================================== SERPENT / WORM
def serpent(m, p, f):
    h = p['h']
    st, t = phase(f)
    sk = p.get('skin', 'skin'); sp = p.get('sp', {})
    sway = 0.0; strike = 0.0; mouth = 0.3; fall = 0.0
    if st == 'idle':
        sway = sn(t)
    elif st == 'atk':
        strike = math.sin(math.pi * t); mouth = 0.3 + 0.7 * strike
    elif st == 'hurt':
        sway = -1.5 * (1 - t); mouth = 0.6
    else:
        fall = t
        m.ell(V(0, 0.002 * h, 0), 0.4 * h * t, 0.02 * h, 0.25 * h * t, 'blood' if not p.get('ichor') else 'gut')
    n = 12
    pts = []
    for i in range(n + 1):
        a = i / n
        if a < 0.35:   # coils on ground
            ang = a / 0.35 * 5.5
            P = V(0.22 * h * math.cos(ang) * (1 - a), 0.05 * h, 0.15 * h + 0.18 * h * math.sin(ang) * (1 - a))
        else:
            b = (a - 0.35) / 0.65
            P = V(0.08 * h * math.sin(b * 3 + sway * 0.6) * (1 - fall), (0.05 + 0.85 * b) * h * (1 - fall * 0.85), 0.15 * h - 0.25 * h * b - strike * 0.45 * h * b * b)
        pts.append(P)
    rmax = p.get('thick', 0.09) * h
    for i in range(n):
        r0 = rmax * (0.7 + 0.3 * math.sin(i / n * math.pi)); r1 = rmax * (0.7 + 0.3 * math.sin((i + 1) / n * math.pi))
        m.cap(pts[i], pts[i + 1], r0, r1, sk, dict(sp), seed=i)
        if p.get('worm'):
            m.ell(pts[i], r0 * 1.05, r0 * 0.35, r0 * 1.05, sk, sp, seed=i + 50)
    hd = pts[-1]
    if p.get('worm'):
        R = rmax * 1.35
        m.ell(hd + V(0, 0.02 * h, 0), R, R, R * 0.6, sk, sp)
        mo = 0.55 + 0.35 * mouth
        m.ell(hd + V(0, 0.02 * h, -R * 0.5), R * mo, R * mo, R * 0.2, 'mouth')
        for ring in range(2):
            rr = R * mo * (0.95 - ring * 0.35)
            for k in range(12):
                a = k / 12 * 2 * math.pi + ring * 0.3
                b = hd + V(rr * math.cos(a), 0.02 * h + rr * math.sin(a), -R * 0.55 - ring * 0.02 * h)
                m.cap(b, b + V(-math.cos(a) * rr * 0.35, -math.sin(a) * rr * 0.35, 0.01 * h), 0.012 * h, 0.002 * h, 'teeth')
        m.ell(hd + V(0, 0.02 * h, -R * 0.4), R * 0.25, R * 0.25, R * 0.1, 'gut')
    else:
        d = norm(pts[-1] - pts[-2])
        hb = hd + d * 0.02 * h
        sn_ = hb + V(0, -0.02 * h, -0.16 * h)
        m.ell(hb, 0.09 * h, 0.07 * h, 0.1 * h, sk, sp)
        m.cap(hb, sn_, 0.07 * h, 0.04 * h, sk, sp)
        jo = mouth * 0.08 * h
        m.cap(hb + V(0, -0.04 * h, 0), sn_ + V(0, -0.04 * h - jo, 0.01 * h), 0.05 * h, 0.03 * h, sk, sp)
        m.ell(lerp(hb, sn_, 0.6) + V(0, -0.035 * h - jo * 0.5, -0.01 * h), 0.045 * h, 0.01 * h + jo * 0.5, 0.06 * h, 'mouth')
        for sx in (-1, 1):
            m.cap(sn_ + V(sx * 0.02 * h, -0.03 * h, 0.02 * h), sn_ + V(sx * 0.02 * h, -0.03 * h - 0.06 * h * mouth, 0.02 * h), 0.008 * h, 0.001 * h, 'teeth')
            m.ell(hb + V(sx * 0.05 * h, 0.03 * h, -0.05 * h), 0.016 * h, 0.012 * h, 0.01 * h, 'eye', {'col': p.get('eyec', C(255, 220, 40))})
        if p.get('hood'):
            for sx in (-1, 1):
                m.tri(hb + V(0, 0.0, 0.08 * h), hb + V(sx * 0.2 * h, -0.05 * h, 0.12 * h), hb + V(sx * 0.12 * h, -0.25 * h, 0.12 * h), sk, sp)


# =================================================================== WRAITH
def wraith(m, p, f):
    h = p['h']
    st, t = phase(f)
    ec = dict(p.get('ep', {'col': C(150, 190, 220)})); ec.setdefault('a', 0.5)
    alt = 0.18 * h; lean = 0.0; arms = 0.0; mouth = 0.6; fade = 1.0; wave = 0.0
    if st == 'idle':
        alt += 0.04 * h * sn(t); wave = t
    elif st == 'atk':
        lean = 0.5 * math.sin(math.pi * t); arms = math.sin(math.pi * t); mouth = 0.6 + 0.6 * arms; wave = t * 2
    elif st == 'hurt':
        lean = -0.3 * (1 - t); wave = t
    else:
        alt -= 0.12 * h * t; lean = 0.6 * t; wave = t
    pv = V(0, alt + 0.42 * h, 0)
    def Lx(P):
        return rot_x(P, pv, lean)
    ch = Lx(pv + V(0, 0.22 * h, 0))
    hd = Lx(pv + V(0, 0.4 * h, -0.02 * h))
    # tattered trail
    n = 9
    for i in range(n):
        a0 = -1.3 + 2.6 * i / n; a1 = -1.3 + 2.6 * (i + 1) / n
        top0 = ch + V(0.13 * h * math.sin(a0), -0.05 * h, -0.07 * h * math.cos(a0))
        top1 = ch + V(0.13 * h * math.sin(a1), -0.05 * h, -0.07 * h * math.cos(a1))
        jag0 = 0.1 * h * (1 if i % 2 else 0.4)
        b0 = V(0.2 * h * math.sin(a0) + 0.06 * h * math.sin(wave * 6.28 + i), alt - 0.18 * h + jag0, -0.1 * h * math.cos(a0))
        b1 = V(0.2 * h * math.sin(a1) + 0.06 * h * math.sin(wave * 6.28 + i + 1), alt - 0.18 * h + (0.1 * h if (i + 1) % 2 else 0.04 * h), -0.1 * h * math.cos(a1))
        m.quad(top0, top1, b1, b0, 'ecto', ec, seed=i, fade=(1, 1, 0.05, 0.05))
    m.cap(pv, ch, 0.07 * h, 0.1 * h, 'ecto', ec)
    m.ell(ch, 0.12 * h, 0.1 * h, 0.07 * h, 'ecto', ec)
    for i in range(4):
        m.cap(ch + V(-0.07 * h, 0.04 * h - i * 0.03 * h, -0.07 * h), ch + V(0.07 * h, 0.04 * h - i * 0.03 * h, -0.07 * h), 0.006 * h, 0.006 * h, 'bone', {'col': C(170, 170, 170), 'bloody': False})
    if p.get('ribs'):
        for i in range(4):
            m.cap(ch + V(-0.08 * h, 0.05 * h - i * 0.03 * h, -0.08 * h), ch + V(0.08 * h, 0.05 * h - i * 0.03 * h, -0.08 * h), 0.007 * h, 0.007 * h, 'bone', {'col': C(200, 200, 190), 'bloody': False})
    # arms with long claws
    for sx in (-1, 1):
        s0 = ch + V(sx * 0.13 * h, 0.05 * h, 0)
        e0 = s0 + V(sx * 0.08 * h, -0.1 * h + arms * 0.15 * h, -0.05 * h - arms * 0.15 * h)
        h0 = e0 + V(sx * 0.02 * h - sx * arms * 0.06 * h, -0.12 * h + arms * 0.12 * h, -0.05 * h - arms * 0.2 * h)
        m.cap(s0, e0, 0.035 * h, 0.028 * h, 'ecto', ec)
        m.cap(e0, h0, 0.028 * h, 0.02 * h, 'ecto', ec)
        for k in (-1, 0, 1):
            m.cap(h0, h0 + V(k * 0.018 * h, -0.14 * h + arms * 0.06 * h, -0.03 * h - arms * 0.08 * h), 0.006 * h, 0.001 * h, 'bone', {'col': C(40, 40, 50), 'bloody': False})
        if p.get('chains'):
            m.chain([e0, e0 + V(0, -0.08 * h, 0), e0 + V(0.02 * h, -0.16 * h, 0), e0 + V(0, -0.24 * h, 0)], 0.01 * h, 0.01 * h, 'metal', {'col': C(90, 86, 80)})
    # skull-like head with stretched screaming mouth
    m.ell(hd, 0.085 * h, 0.1 * h, 0.08 * h, 'ecto', ec)
    for sx in (-1, 1):
        m.ell(hd + V(sx * 0.03 * h, 0.02 * h, -0.07 * h), 0.022 * h, 0.03 * h, 0.01 * h, 'dark')
        m.ell(hd + V(sx * 0.03 * h, 0.02 * h, -0.075 * h), 0.007 * h, 0.007 * h, 0.004 * h, 'eye', {'col': p.get('eyec', C(255, 255, 255))})
    m.ell(hd + V(0, -0.05 * h - 0.02 * h * mouth, -0.07 * h), 0.03 * h, (0.025 + 0.035 * mouth) * h, 0.01 * h, 'dark')
    if p.get('crown'):
        for i in range(7):
            a = -1.2 + i * 0.4
            b = hd + V(0.08 * h * math.sin(a), 0.07 * h, -0.07 * h * math.cos(a))
            m.cap(b, b + V(0, 0.05 * h, 0), 0.01 * h, 0.002 * h, 'gold')
    if p.get('hood'):
        m.ell(hd + V(0, 0.02 * h, 0.03 * h), 0.12 * h, 0.13 * h, 0.1 * h, 'robe', p.get('robep', {'col': C(30, 20, 40)}))


# =================================================================== BLOB / ABERRATION
def blob(m, p, f):
    h = p['h']; w = p.get('w', h)
    st, t = phase(f)
    sk = p.get('skin', 'gut'); sp = p.get('sp', {})
    rng = np.random.RandomState(p.get('seed', 3))
    pulse = 0.0; lash = 0.0; mouth = 0.3; flat = 0.0
    if st == 'idle':
        pulse = sn(t)
    elif st == 'atk':
        lash = math.sin(math.pi * t); mouth = 0.3 + 0.7 * lash; pulse = lash
    elif st == 'hurt':
        pulse = -1.5 * (1 - t); mouth = 0.6
    else:
        flat = t
        m.ell(V(0, 0.002 * h, 0), 0.6 * w * t, 0.02 * h, 0.35 * w * t, 'blood')
    k = 1 + 0.035 * pulse
    hy = (1 - 0.55 * flat)
    body = V(0, 0.34 * h * hy, 0.05 * h)
    nt = p.get('tent', 6)
    for i in range(nt):
        a = -1.5 + 3.0 * i / max(1, nt - 1)
        base = body + V(0.33 * w * math.sin(a), -0.18 * h * hy, -0.12 * h * math.cos(a))
        pts = [base]
        for j in range(1, 7):
            bq = j / 6.0
            pts.append(base + V(0.22 * w * math.sin(a) * bq + 0.06 * h * math.sin(f * 0.8 + i + j * 0.9),
                                (0.3 * bq - 0.34 * bq * bq + 0.55 * lash * bq * (i % 2)) * h * hy,
                                -0.2 * h * bq - lash * 0.4 * h * bq * bq))
        m.chain(pts, 0.045 * h, 0.006 * h, 'raw' if i % 2 else sk, sp, seed=i)
    m.ell(body, 0.4 * w * k, 0.3 * h * k * hy, 0.28 * w * k, sk, sp)
    mats = [sk, 'raw', 'gut', sk, 'rot']
    for i in range(p.get('lumps', 11)):
        ang = rng.uniform(-1.6, 1.6); el = rng.uniform(-0.6, 1.0)
        r = rng.uniform(0.12, 0.22)
        c = body + V(0.36 * w * math.sin(ang) * math.cos(el * 0.6), 0.26 * h * el * hy, -0.22 * w * math.cos(ang))
        m.ell(c, r * w * k, r * h * k * hy * rng.uniform(0.8, 1.2), r * w * 0.8, mats[i % len(mats)] if p.get('mixed', True) else sk, sp, seed=float(i))
    for i in range(p.get('bones', 5)):
        ang = rng.uniform(-1.3, 1.3); el = rng.uniform(-0.2, 0.9)
        c = body + V(0.34 * w * math.sin(ang), 0.24 * h * el * hy, -0.26 * w * math.cos(ang))
        d = V(math.sin(ang) * 0.6, rng.uniform(0.3, 1.0), -0.6)
        d = d / np.linalg.norm(d)
        m.cap(c, c + d * h * rng.uniform(0.12, 0.2), 0.016 * h, 0.004 * h, 'bone', {'col': C(206, 190, 156)})
    for i in range(p.get('neyes', 9)):
        ang = rng.uniform(-1.2, 1.2); el = rng.uniform(-0.1, 0.95)
        c = body + V(0.33 * w * math.sin(ang), 0.24 * h * el * hy, -0.3 * w * math.cos(ang) - 0.04 * w)
        r = rng.uniform(0.018, 0.035) * h
        m.ell(c, r, r, r * 0.6, 'eyeball', {'col': p.get('iris', C(200, 30, 10))}, seed=i)
    mc = body + V(0, -0.06 * h * hy, -0.34 * w)
    mo = 0.05 + 0.09 * mouth
    m.ell(mc, 0.15 * w, mo * h, 0.05 * w, 'mouth')
    for i in range(-5, 6):
        x = i * 0.025 * w
        m.cap(mc + V(x, mo * h * 0.95, -0.02 * w), mc + V(x * 1.05, mo * h * 0.2, -0.035 * w), 0.008 * h, 0.001 * h, 'teeth')
        m.cap(mc + V(x, -mo * h * 0.95, -0.02 * w), mc + V(x * 1.05, -mo * h * 0.2, -0.035 * w), 0.008 * h, 0.001 * h, 'teeth')
    for i in range(3):
        x = (-0.06 + i * 0.05) * w
        m.cap(mc + V(x, -mo * h, -0.04 * w), mc + V(x, -mo * h - (0.08 + 0.05 * i) * h, -0.04 * w), 0.007 * h, 0.002 * h, 'blood')
    for hh in p.get('heads', []):
        dx, dy = hh
        nb = body + V(dx * w, 0.15 * h * hy, -0.1 * w)
        hd = nb + V(dx * 0.4 * w + 0.02 * h * math.sin(f * 0.7 + dx * 5), (0.3 + 0.03 * pulse) * h * hy, -0.12 * w - lash * 0.15 * h)
        m.cap(nb, hd, 0.06 * h, 0.045 * h, sk, sp)
        m.ell(hd, 0.06 * h, 0.075 * h, 0.065 * h, 'skin', {'col': C(150, 40, 36)})
        for sx in (-1, 1):
            m.ell(hd + V(sx * 0.024 * h, 0.012 * h, -0.06 * h), 0.01 * h, 0.008 * h, 0.005 * h, 'eye', {'col': C(255, 200, 40)})
            m.chain([hd + V(sx * 0.035 * h, 0.05 * h, 0), hd + V(sx * 0.09 * h, 0.11 * h, 0.02 * h), hd + V(sx * 0.09 * h, 0.19 * h, -0.02 * h)], 0.016 * h, 0.003 * h, 'bone', {'col': C(60, 50, 40)})
        m.ell(hd + V(0, -0.035 * h, -0.058 * h), 0.03 * h, 0.01 * h + 0.022 * h * mouth, 0.01 * h, 'mouth')
    if p.get('ribs'):
        for i in range(4):
            y = body[1] + (0.14 - i * 0.065) * h * hy
            pts = [V(0.27 * w * math.sin(a), y - abs(a) * 0.03 * h, body[2] - 0.3 * w * math.cos(a)) for a in np.linspace(-1.1, 1.1, 6)]
            m.chain(pts, 0.014 * h, 0.012 * h, 'bone', {'col': C(196, 176, 146)})


# =================================================================== GOLEM
def _chunks(m, A, B, r, n, mat, sp, rng, alt=None, spikes=0):
    """irregular lumps along a limb (rock pieces / flesh masses)"""
    for k in range(n):
        t = (k + 0.5) / n
        c = A + (B - A) * t + V(rng.uniform(-0.3, 0.3) * r, rng.uniform(-0.3, 0.3) * r, rng.uniform(-0.4, 0.1) * r)
        rx = r * rng.uniform(0.75, 1.15); ry = r * rng.uniform(0.7, 1.1); rz = r * rng.uniform(0.75, 1.1)
        mt = mat if (alt is None or rng.uniform() > 0.4) else alt
        m.ell(c, rx, ry, rz, mt, sp, seed=float(rng.uniform(0, 50)))
    for k in range(spikes):
        t = rng.uniform(0.1, 0.9)
        c = A + (B - A) * t
        d = V(rng.uniform(-1, 1), rng.uniform(0.2, 1), rng.uniform(-1, 0.3))
        d = d / (np.linalg.norm(d) + 1e-9)
        m.cap(c, c + d * r * 1.6, r * 0.18, r * 0.02, 'bone', {'col': C(200, 186, 150)})


def golem(m, p, f):
    H = p['H']
    J = biped_pose(dict(p, build=1.3, hunch=0.18), f)
    st, t = J['state'], J['t']
    sk = p.get('skin', 'stone'); sp = p.get('sp', {})
    alt = p.get('alt')
    sp_n = p.get('bonespikes', 0)
    rng = np.random.RandomState(p.get('seed', 7))
    if st == 'die':
        m.ell(V(0, 0.002 * H, -0.05 * H), 0.3 * H * t, 0.02 * H, 0.15 * H * t, 'blood' if sk in ('raw', 'gut') else 'dark')
    for s in 'LR':
        _chunks(m, J['hi' + s], J['kn' + s], 0.075 * H, 3, sk, sp, rng, alt)
        _chunks(m, J['kn' + s], J['ft' + s], 0.065 * H, 3, sk, sp, rng, alt)
        m.ell(J['ft' + s] + V(0, 0.02 * H, -0.03 * H), 0.07 * H, 0.03 * H, 0.08 * H, sk, sp)
    _chunks(m, J['pelvis'], J['chest'], 0.12 * H, 4, sk, sp, rng, alt, sp_n)
    _chunks(m, J['shL'] + V(-0.04 * H, 0, 0), J['shR'] + V(0.04 * H, 0, 0), 0.11 * H, 4, sk, sp, rng, alt, sp_n)
    if p.get('stitches'):
        ribcage(m, J, H, 1.25, 'bone', {'col': C(200, 180, 150)}, cavity=True)
        for i in range(6):
            y = J['chest'][1] - 0.2 * H + i * 0.05 * H
            m.cap(V(-0.05 * H, y, J['chest'][2] - 0.16 * H), V(0.05 * H, y + 0.012 * H, J['chest'][2] - 0.16 * H), 0.005 * H, 0.005 * H, 'dark')
    if p.get('rune') is not None:
        m.ell(J['chest'] + V(0, 0.0, -0.15 * H), 0.035 * H, 0.04 * H, 0.01 * H, 'eye', {'col': p['rune']})
    for s in 'LR':
        sx = -1 if s == 'L' else 1
        _chunks(m, J['sh' + s] + V(sx * 0.06 * H, 0.02 * H, 0), J['el' + s] + V(sx * 0.05 * H, 0, 0), 0.075 * H, 3, sk, sp, rng, alt, sp_n // 2)
        _chunks(m, J['el' + s] + V(sx * 0.05 * H, 0, 0), J['ha' + s] + V(sx * 0.05 * H, 0, 0), 0.075 * H, 3, sk, sp, rng, alt)
        m.ell(J['ha' + s] + V(sx * 0.05 * H, -0.03 * H, 0), 0.085 * H, 0.08 * H, 0.08 * H, sk, sp, seed=7)
    hd = J['head'] - V(0, 0.04 * H, -0.05 * H)
    m.ell(hd, 0.062 * H, 0.06 * H, 0.065 * H, sk, sp)
    m.ell(hd + V(0, 0.025 * H, -0.03 * H), 0.06 * H, 0.02 * H, 0.04 * H, sk, sp)
    for sx in (-1, 1):
        m.ell(hd + V(sx * 0.024 * H, 0.005 * H, -0.062 * H), 0.013 * H, 0.009 * H, 0.006 * H, 'eye', {'col': p.get('eyec', C(255, 80, 20))})
    m.ell(hd + V(0, -0.033 * H, -0.058 * H), 0.03 * H, 0.008 * H + 0.02 * H * J['jaw'], 0.008 * H, 'mouth')
    if p.get('spikes'):
        for i in range(7):
            a = -1.1 + i * 0.37
            bb = J['chest'] + V(0.2 * H * math.sin(a), 0.1 * H, 0.05 * H)
            m.cap(bb, bb + V(0.05 * H * math.sin(a), 0.14 * H, 0.05 * H), 0.03 * H, 0.003 * H, sk, sp)
