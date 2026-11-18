"""The wanderer (player character) as separate equipment layers.

Every layer is rendered alone with the same skeleton pose and keeps its depth,
so the game can composite any combination of armour / helm / weapon / shield.
Order of the variant lists = index used by the game (see HERO_* tables in game.c).
"""
import math
import numpy as np
from render import V, lerp, norm, C, rot_x
from bodies import biped_pose, muscled_limb, hand, foot, _perp

H = 100.0
P = dict(H=H, build=1.05, hunch=0.05, weapon='sword', jaw=0.1)
SKIN = {'col': C(184, 146, 120), 'bloody': False, 'cracks': False}

ARMORS = ['rags', 'leather', 'chain', 'plate', 'gothic', 'ancient', 'dark', 'dragon', 'holy', 'abyss']
HELMS = ['hood', 'cap', 'skull', 'great', 'crown', 'bone', 'horned', 'abyss_crown']
WEAPONS = ['dagger', 'club', 'sword', 'axe', 'bastard', 'greatsword', 'runeblade', 'scythe', 'greataxe', 'holysword',
           'doomsword', 'abyssblade']
SHIELDS = ['buckler', 'kite', 'tower']


def pose(f):
    return biped_pose(P, f)


# ------------------------------------------------------------------ helpers
def ring(m, c, rx, rz, y, mat, p, r, n=10, a0=0.0, a1=2 * math.pi, seed=0.0):
    pts = [c + V(rx * math.sin(a0 + (a1 - a0) * i / n), y, rz * math.cos(a0 + (a1 - a0) * i / n)) for i in range(n + 1)]
    for i in range(n):
        m.cap(pts[i], pts[i + 1], r, r, mat, p, seed + i)


def rivets(m, pts, r, mat='metal', p=None):
    for q in pts:
        m.ell(q, r, r, r * 0.7, mat, p or {'col': C(170, 160, 140), 'bloody': False})


def cape(m, J, f, st, p, ln=0.5, tatter=0.0, trim=None, seed=0.0):
    sL = J['shL'] + V(0.035 * H, -0.004 * H, 0.075 * H); sR = J['shR'] + V(-0.035 * H, -0.004 * H, 0.075 * H)
    n = 8
    lift = 0.12 * H if st == 'atk' else 0.04 * H if st == 'cast' else 0.0
    top = [lerp(sL, sR, i / n) for i in range(n + 1)]
    mid = []
    bot = []
    for i in range(n + 1):
        sway = 0.018 * H * math.sin(f * 0.8 + i * 0.7)
        x = top[i][0] * 1.18
        mid.append(V(x * 1.05, lerp(top[i], V(0, 0.9 - ln, 0) * H, 0.5)[1], J['chest'][2] + 0.1 * H + lift * 0.5 + sway * 0.5))
        jag = 0.0
        if tatter:
            jag = tatter * H * (0.06 * ((i * 7919 % 5) / 4.0) - 0.02)
        bot.append(V(x * 1.12, (0.9 - ln) * H + jag + (0.02 * H if i % 2 else 0), J['pelvis'][2] + 0.13 * H + lift + sway))
    cp = dict(p)
    if trim is not None:
        cp['trim'] = trim
    for i in range(n):
        m.quad(top[i], top[i + 1], mid[i + 1], mid[i], 'cloth', cp, seed)
        m.quad(mid[i], mid[i + 1], bot[i + 1], bot[i], 'cloth', cp, seed)
    # clasp
    for q in (sL, sR):
        m.ell(q + V(0, 0, 0.004 * H), 0.012 * H, 0.012 * H, 0.008 * H, 'gold' if trim is not None else 'metal', {'col': C(150, 140, 120), 'bloody': False})


def pauldron(m, J, s, mat, p, plates=3, size=1.0, spike=0, seed=0.0):
    sh = J['sh' + s]; sx = -1 if s == 'L' else 1
    for i in range(plates):
        y = 0.022 * H - i * 0.022 * H
        m.ell(sh + V(sx * (0.006 + i * 0.006) * H, y, 0.002 * H), (0.058 - i * 0.004) * H * size, (0.036 - i * 0.004) * H * size,
              (0.056 - i * 0.003) * H * size, mat, p, seed + i)
    if spike:
        for k in range(spike):
            b = sh + V(sx * 0.02 * H, 0.05 * H * size, (k - (spike - 1) / 2) * 0.03 * H)
            m.cap(b, b + V(sx * 0.03 * H, 0.06 * H * size, 0.01 * H), 0.012 * H, 0.002 * H, 'bone', {'col': C(60, 50, 44), 'bloody': False})


def vambrace(m, J, s, mat, p, r=1.0, seed=0.0):
    el, ha = J['el' + s], J['ha' + s]
    m.cap(lerp(el, ha, 0.12), lerp(el, ha, 0.86), 0.031 * H * r, 0.026 * H * r, mat, p, seed)
    m.ell(lerp(el, ha, 0.86), 0.028 * H * r, 0.012 * H, 0.028 * H * r, mat, p, seed + 1)


def greave(m, J, s, mat, p, r=1.0, knee=True, sabaton=True, seed=0.0):
    hi, kn, ft = J['hi' + s], J['kn' + s], J['ft' + s]
    m.cap(lerp(kn, ft, 0.1), lerp(kn, ft, 0.85), 0.043 * H * r, 0.03 * H * r, mat, p, seed)
    if knee:
        m.ell(kn + V(0, 0.004 * H, -0.03 * H), 0.032 * H * r, 0.03 * H * r, 0.022 * H, mat, p, seed + 1)
    if sabaton:
        for i in range(3):
            m.ell(ft + V(0, 0.017 * H, (-0.012 - i * 0.022) * H), 0.034 * H * r, 0.019 * H - i * 0.002 * H, 0.022 * H, mat, p, seed + 2 + i)


def cuisse(m, J, s, mat, p, r=1.0, seed=0.0):
    hi, kn = J['hi' + s], J['kn' + s]
    m.cap(lerp(hi, kn, 0.18), lerp(hi, kn, 0.85), 0.064 * H * r, 0.046 * H * r, mat, p, seed)


def cuirass(m, J, mat, p, b=1.05, r=1.0, seed=0.0):
    ch, pv = J['chest'], J['pelvis']
    m.ell(ch + V(0, -0.004 * H, 0.0), 0.138 * H * b * r, 0.112 * H * r, 0.09 * H * r, mat, p, seed)
    m.ell(lerp(pv, ch, 0.45) + V(0, 0, 0.002 * H), 0.105 * H * b * r, 0.095 * H, 0.074 * H * r, mat, p, seed + 1)
    # back ridge + plate edges
    m.cap(ch + V(0, 0.08 * H, 0.085 * H * r), lerp(pv, ch, 0.3) + V(0, 0, 0.072 * H * r), 0.008 * H, 0.008 * H, mat, p, seed + 2)


def faulds(m, J, mat, p, n=3, ln=0.13, flare=1.0, seed=0.0):
    pv = J['pelvis']
    for i in range(n):
        y = -0.01 * H - i * ln * H / n
        m.ell(pv + V(0, y, 0.0), (0.108 + 0.01 * i * flare) * H, ln * H / n * 0.85, (0.08 + 0.008 * i * flare) * H, mat, p, seed + i)


def belt(m, J, p=None, buckle='gold'):
    pv = J['pelvis'] + V(0, 0.035 * H, 0)
    ring(m, pv, 0.1 * H, 0.07 * H, 0, 'leather', p or {'col': C(70, 44, 26)}, 0.01 * H, 14)
    m.ell(pv + V(0, 0, -0.08 * H), 0.016 * H, 0.014 * H, 0.008 * H, buckle, {'col': C(160, 150, 130), 'bloody': False})
    m.ell(pv + V(0.105 * H, -0.03 * H, 0.0), 0.02 * H, 0.03 * H, 0.026 * H, 'leather', p or {'col': C(86, 56, 32)})   # pouch


# ------------------------------------------------------------------ layers
def layer_body(m, f):
    J = pose(f); st = J['state']
    shirt = {'col': C(118, 104, 84), 'bloody': False}
    pants = {'col': C(70, 58, 46), 'bloody': False}
    boots = {'col': C(66, 42, 26)}
    if st == 'die':
        m.ell(V(0, 0.002 * H, -0.05 * H), 0.25 * H * J['t'], 0.02 * H, 0.12 * H * J['t'], 'blood')
    for s in 'LR':
        hi, kn, ft = J['hi' + s], J['kn' + s], J['ft' + s]
        muscled_limb(m, hi, kn, 0.058 * H * 1.05, 0.042 * H, 'cloth', pants, 'thigh', 3.1 if s == 'L' else 5.7)
        muscled_limb(m, kn, ft, 0.041 * H, 0.028 * H, 'cloth', pants, 'calf', 1.3)
        m.cap(lerp(kn, ft, 0.45), ft + V(0, 0.01 * H, 0), 0.036 * H, 0.03 * H, 'leather', boots)       # boot shaft
        m.ell(lerp(kn, ft, 0.45), 0.039 * H, 0.012 * H, 0.039 * H, 'leather', boots)                 # boot cuff
        foot(m, ft, H, 1.0, 'leather', boots, toes=False)
    pv, ch, nk, hd = J['pelvis'], J['chest'], J['neck'], J['head']
    m.cap(pv, J['belly'], 0.088 * H, 0.083 * H, 'cloth', shirt)
    m.ell(ch + V(0, -0.005 * H, 0), 0.128 * H, 0.106 * H, 0.078 * H, 'cloth', shirt)
    m.ell(lerp(pv, ch, 0.45), 0.098 * H, 0.09 * H, 0.068 * H, 'cloth', shirt)
    m.ell(pv, 0.098 * H, 0.062 * H, 0.062 * H, 'cloth', pants)
    belt(m, J)
    for s in 'LR':
        sh_, el, ha = J['sh' + s], J['el' + s], J['ha' + s]
        hdv = norm(ha - el)
        m.ell(sh_, 0.052 * H, 0.046 * H, 0.05 * H, 'cloth', shirt)
        muscled_limb(m, sh_, el, 0.041 * H, 0.033 * H, 'cloth', shirt, 'upper', 4.4)
        muscled_limb(m, el, ha, 0.031 * H, 0.023 * H, 'skin', SKIN, 'fore', 7.7)
        m.cap(lerp(el, ha, 0.55), lerp(el, ha, 0.95), 0.028 * H, 0.025 * H, 'leather', {'col': C(80, 52, 30)})    # wrist wrap
        hand(m, ha, hdv, H, 'skin', SKIN, grip=0.9 if s == 'R' else 0.3, seed=2.2)
    # neck + head
    up = norm(hd - nk)
    m.cap(nk - up * 0.02 * H, hd - up * 0.03 * H, 0.034 * H, 0.03 * H, 'skin', SKIN)
    for sx in (-1, 1):
        m.cap(nk + V(sx * 0.02 * H, -0.01 * H, 0.0), hd + V(sx * 0.012 * H, -0.04 * H, -0.02 * H), 0.01 * H, 0.008 * H, 'skin', SKIN)
    hr = 1.2 * H
    m.ell(hd + up * 0.005 * H, 0.052 * hr, 0.066 * hr, 0.058 * hr, 'skin', SKIN)
    m.ell(hd + V(0, -0.058 * hr, -0.03 * hr), 0.04 * hr, 0.016 * hr, 0.036 * hr, 'skin', SKIN)     # jaw
    for sx in (-1, 1):
        m.ell(hd + V(sx * 0.053 * hr, -0.004 * hr, 0.004 * hr), 0.008 * hr, 0.017 * hr, 0.012 * hr, 'skin', SKIN)
        m.ell(hd + V(sx * 0.056 * hr, -0.004 * hr, 0.002 * hr), 0.003 * hr, 0.01 * hr, 0.006 * hr, 'dark')
        m.ell(hd + V(sx * 0.02 * hr, 0.0, -0.052 * hr), 0.012 * hr, 0.009 * hr, 0.007 * hr, 'dark')
    m.cap(hd + V(0, 0.008 * hr, -0.056 * hr), hd + V(0, -0.018 * hr, -0.066 * hr), 0.006 * hr, 0.008 * hr, 'skin', SKIN)
    # stubble / beard
    m.ell(hd + V(0, -0.052 * hr, -0.036 * hr), 0.042 * hr, 0.022 * hr, 0.034 * hr, 'fur', {'col': C(60, 44, 30), 'bloody': False})
    if st == 'cast':
        k = J.get('castk', 0)
        c = lerp(J['haL'], J['haR'], 0.5) + V(0, 0.05 * H, -0.04 * H)
        m.ell(c, 0.05 * H * k + 0.005 * H, 0.05 * H * k + 0.005 * H, 0.05 * H * k + 0.005 * H, 'eye', {'col': C(140, 200, 255)})


def layer_hair(m, f):
    J = pose(f); hd = J['head']
    hp = {'col': C(74, 52, 34), 'bloody': False}
    m.ell(hd + V(0, 0.032 * H, 0.012 * H), 0.066 * H, 0.06 * H, 0.068 * H, 'fur', hp)
    for i in range(9):
        a = -1.2 + i * 0.3
        m.cap(hd + V(0.045 * H * math.sin(a), 0.07 * H, 0.03 * H * math.cos(a)),
              hd + V(0.062 * H * math.sin(a), -0.01 * H - 0.012 * H * (i % 3), 0.06 * H + 0.012 * H * math.cos(a)), 0.016 * H, 0.004 * H, 'fur', hp, i)
    # tied tail
    t0 = hd + V(0, 0.0, 0.075 * H)
    m.ell(t0, 0.012 * H, 0.012 * H, 0.01 * H, 'leather', {'col': C(60, 40, 24)})
    m.chain([t0, t0 + V(0.004 * H, -0.04 * H, 0.012 * H), t0 + V(0.01 * H, -0.08 * H, 0.008 * H)], 0.014 * H, 0.004 * H, 'fur', hp)


def layer_armor(m, f, k):
    J = pose(f); st = J['state']
    a = ARMORS[k]
    if a == 'rags':
        rp = {'col': C(120, 104, 80)}
        for i in range(5):
            y = J['chest'][1] + 0.06 * H - i * 0.045 * H
            c = V(0, y, J['chest'][2] * (1 - i / 5) + J['pelvis'][2] * i / 5)
            ring(m, c, 0.135 * H - abs(i - 1.5) * 0.008 * H, 0.088 * H, 0, 'rag', rp, 0.014 * H, 12, seed=i)
        for i in range(6):
            x = (i - 2.5) * 0.035 * H
            b0 = J['pelvis'] + V(x, -0.02 * H, 0.07 * H)
            m.quad(b0, b0 + V(0.02 * H, 0, 0), b0 + V(0.024 * H, -0.13 * H - 0.03 * H * (i % 2), 0.012 * H), b0 + V(0.004 * H, -0.12 * H, 0.01 * H), 'rag', rp, i)
        for s in 'LR':
            vambrace(m, J, s, 'rag', rp, 1.0, 3)
        return
    if a == 'leather':
        lp = {'col': C(104, 66, 38)}
        cuirass(m, J, 'leather', lp, r=0.98)
        for s in 'LR':
            pauldron(m, J, s, 'leather', {'col': C(90, 56, 32)}, plates=2, size=0.85)
            vambrace(m, J, s, 'leather', lp)
            sh = J['sh' + s]
            m.cap(sh + V(0, 0.02 * H, 0.03 * H), J['pelvis'] + V(0.06 * H * (1 if s == 'L' else -1), 0.04 * H, 0.08 * H), 0.008 * H, 0.008 * H, 'leather', {'col': C(60, 38, 22)})
        faulds(m, J, 'leather', lp, n=2, ln=0.1)
        rivets(m, [J['chest'] + V(x * 0.04 * H, y * 0.04 * H, 0.09 * H) for x in (-2, -1, 1, 2) for y in (1, -1)], 0.006 * H)
        return
    if a == 'chain':
        mp = {'col': C(140, 138, 132), 'rust': 0.86}
        cuirass(m, J, 'mail', mp, r=0.98)
        faulds(m, J, 'mail', mp, n=3, ln=0.2, flare=1.6)
        for s in 'LR':
            sh_, el = J['sh' + s], J['el' + s]
            m.ell(sh_, 0.055 * H, 0.05 * H, 0.054 * H, 'mail', mp)
            m.cap(sh_, lerp(sh_, el, 0.85), 0.046 * H, 0.038 * H, 'mail', mp)
            vambrace(m, J, s, 'leather', {'col': C(90, 58, 34)})
        m.cap(J['neck'] + V(0, -0.02 * H, 0), J['neck'] + V(0, 0.01 * H, 0.01 * H), 0.05 * H, 0.04 * H, 'mail', mp)   # coif collar
        belt(m, J, {'col': C(64, 40, 24)})
        return
    spec = {
        'plate':   dict(mat='plate', p={'col': C(140, 140, 146), 'shin': 50}, cape=None, spike=0, glow=None),
        'gothic':  dict(mat='plate', p={'col': C(120, 122, 130), 'etch': C(200, 160, 70), 'shin': 50}, cape=({'col': C(110, 18, 16)}, C(200, 160, 70), 0.0), spike=0, fluted=True),
        'ancient': dict(mat='plate', p={'col': C(176, 140, 76), 'etch': C(90, 120, 90), 'rust': 0.78, 'shin': 30}, cape=({'col': C(40, 70, 64)}, C(200, 170, 90), 0.3), spike=0, scaleskirt=True),
        'dark':    dict(mat='plate', p={'col': C(56, 52, 58), 'glow': C(255, 40, 16), 'shin': 30}, cape=({'col': C(26, 20, 24)}, None, 1.0), spike=2),
        'dragon':  dict(mat='scale', p={'col': C(70, 116, 64), 'glow': C(255, 140, 30)}, cape=None, spike=1, horn=True),
        'holy':    dict(mat='plate', p={'col': C(214, 214, 222), 'etch': C(230, 190, 80), 'shin': 60}, cape=({'col': C(200, 196, 186)}, C(220, 180, 60), 0.0), spike=0, halo=True),
        'abyss':   dict(mat='plate', p={'col': C(48, 30, 66), 'glow': C(200, 90, 255), 'shin': 36}, cape=({'col': C(34, 16, 46)}, None, 1.4), spike=3, bonespikes=True),
    }[a]
    mat, ap = spec['mat'], spec['p']
    if spec['cape']:
        cp, trim, tat = spec['cape']
        cape(m, J, f, st, cp, ln=0.54, tatter=tat, trim=trim)
    if a == 'dragon':
        cuirass(m, J, 'scale', ap)
        faulds(m, J, 'scale', ap, n=3, ln=0.2, flare=1.4)
        for s in 'LR':
            pauldron(m, J, s, 'scale', ap, plates=3, size=1.05, spike=1)
            m.cap(J['sh' + s], lerp(J['sh' + s], J['el' + s], 0.85), 0.046 * H, 0.038 * H, 'scale', ap)
            vambrace(m, J, s, 'plate', {'col': C(60, 80, 56), 'shin': 30})
            cuisse(m, J, s, 'scale', ap)
            greave(m, J, s, 'plate', {'col': C(60, 80, 56), 'shin': 30})
        # dorsal spines
        for i in range(5):
            c = lerp(J['pelvis'], J['neck'], 0.25 + i * 0.15) + V(0, 0, 0.09 * H)
            m.tri(c + V(-0.008 * H, -0.02 * H, 0), c + V(0.008 * H, -0.02 * H, 0), c + V(0, 0.01 * H, 0.04 * H), 'bone', {'col': C(70, 60, 40), 'bloody': False})
        return
    cuirass(m, J, mat, ap)
    if spec.get('fluted'):
        for i in range(5):
            x = (i - 2) * 0.035 * H
            m.cap(J['chest'] + V(x, 0.07 * H, 0.088 * H), lerp(J['pelvis'], J['chest'], 0.35) + V(x * 0.8, 0, 0.076 * H), 0.007 * H, 0.005 * H, mat, ap, i)
    if spec.get('scaleskirt'):
        faulds(m, J, 'scale', {'col': C(150, 120, 60)}, n=3, ln=0.22, flare=1.5)
    else:
        faulds(m, J, mat, ap, n=3, ln=0.14)
    for s in 'LR':
        pauldron(m, J, s, mat, ap, plates=3, size=1.12 if a in ('gothic', 'dark', 'abyss') else 1.0, spike=spec['spike'])
        sh_, el = J['sh' + s], J['el' + s]
        m.cap(lerp(sh_, el, 0.3), lerp(sh_, el, 0.9), 0.042 * H, 0.036 * H, mat, ap)          # rerebrace
        m.ell(el, 0.038 * H, 0.034 * H, 0.038 * H, mat, ap)                                     # couter
        if a in ('gothic', 'dark', 'abyss'):
            m.cap(el, el + norm(el - J['ha' + s]) * 0.04 * H + V(0, 0, 0.03 * H), 0.016 * H, 0.002 * H, mat, ap)
        vambrace(m, J, s, mat, ap, 1.08)
        cuisse(m, J, s, mat, ap)
        greave(m, J, s, mat, ap)
    rivets(m, [J['chest'] + V(x * 0.05 * H, 0.085 * H, 0.06 * H) for x in (-2, -1, 1, 2)], 0.005 * H)
    m.cap(J['neck'] + V(-0.05 * H, -0.03 * H, 0.03 * H), J['neck'] + V(0.05 * H, -0.03 * H, 0.03 * H), 0.024 * H, 0.024 * H, mat, ap)   # gorget
    if spec.get('bonespikes'):
        for i in range(4):
            c = lerp(J['pelvis'], J['neck'], 0.3 + i * 0.16) + V(0, 0, 0.09 * H)
            m.cap(c, c + V(0, 0.03 * H, 0.06 * H), 0.012 * H, 0.002 * H, 'bone', {'col': C(200, 190, 170), 'bloody': False})
    if spec.get('halo'):
        ring(m, J['head'] + V(0, 0.1 * H, 0.03 * H), 0.05 * H, 0.05 * H, 0, 'glowmat', {'col': C(255, 230, 150)}, 0.004 * H, 16)


def layer_helm(m, f, k):
    J = pose(f); hd = J['head']; hr = 1.2 * H
    h = HELMS[k]
    if h == 'hood':
        hp = {'col': C(62, 54, 44), 'bloody': False}
        m.ell(hd + V(0, 0.012 * hr, 0.014 * hr), 0.072 * hr, 0.084 * hr, 0.074 * hr, 'cloth', hp)
        m.cap(hd + V(0, -0.02 * hr, 0.07 * hr), J['chest'] + V(0, 0.06 * H, 0.08 * H), 0.04 * H, 0.06 * H, 'cloth', hp)   # cowl drape
        m.ell(hd + V(0, -0.005 * hr, -0.05 * hr), 0.044 * hr, 0.056 * hr, 0.02 * hr, 'dark')
        return
    if h in ('cap', 'great', 'skull', 'bone', 'horned'):
        mp = {'cap': {'col': C(150, 150, 154)}, 'great': {'col': C(160, 160, 168), 'shin': 50}, 'skull': {'col': C(120, 116, 110), 'rust': 0.8},
              'bone': {'col': C(200, 190, 160)}, 'horned': {'col': C(70, 46, 40), 'glow': C(255, 60, 20)}}[h]
        mat = 'bone' if h == 'bone' else 'plate'
        m.ell(hd + V(0, 0.022 * hr, 0.002 * hr), 0.064 * hr, 0.064 * hr, 0.066 * hr, mat, mp)
        ring(m, hd + V(0, 0.0, 0.0), 0.065 * hr, 0.067 * hr, 0.004 * hr, mat, mp, 0.006 * hr, 14)       # brim band
        m.cap(hd + V(0, 0.08 * hr, -0.04 * hr), hd + V(0, 0.085 * hr, 0.06 * hr), 0.008 * hr, 0.008 * hr, mat, mp)   # crest ridge
        if h in ('great', 'skull', 'bone', 'horned'):
            m.ell(hd + V(0, -0.025 * hr, 0.0), 0.062 * hr, 0.05 * hr, 0.064 * hr, mat, mp)               # face/neck guard
            m.ell(hd + V(0, 0.0, -0.064 * hr), 0.042 * hr, 0.006 * hr, 0.006 * hr, 'dark')                # eye slit
        if h == 'great':
            for i in range(5):
                m.ell(hd + V((i - 2) * 0.012 * hr, -0.03 * hr, -0.062 * hr), 0.003 * hr, 0.012 * hr, 0.003 * hr, 'dark')
            rivets(m, [hd + V(0.066 * hr * math.sin(a), 0.004 * hr, 0.066 * hr * math.cos(a)) for a in np.linspace(-2.6, 2.6, 9)], 0.004 * hr)
        if h == 'skull':
            m.ell(hd + V(0, 0.01 * hr, -0.05 * hr), 0.05 * hr, 0.058 * hr, 0.03 * hr, 'bone', {'col': C(200, 190, 160)})
            for sx in (-1, 1):
                m.ell(hd + V(sx * 0.02 * hr, 0.008 * hr, -0.074 * hr), 0.012 * hr, 0.014 * hr, 0.006 * hr, 'dark')
        if h in ('bone', 'horned', 'skull'):
            hs = 1.5 if h == 'horned' else 0.9
            for sx in (-1, 1):
                pts = [hd + V(sx * 0.05 * hr, 0.04 * hr, 0.0)]
                for i in range(1, 6):
                    a = i / 5.0
                    pts.append(hd + V(sx * (0.05 + 0.07 * a * hs) * hr, (0.04 + 0.08 * a * hs - 0.05 * a * a * hs) * hr, (0.02 * a) * hr))
                m.chain(pts, 0.018 * hr * hs, 0.003 * hr, 'bone', {'col': C(60, 50, 44) if h == 'horned' else C(210, 200, 170), 'bloody': False})
        return
    # crowns (hair stays visible)
    gp = {'col': C(210, 170, 70)}
    nspk = 7 if h == 'crown' else 9
    for i in range(nspk):
        a = -math.pi + 2 * math.pi * i / nspk
        base = hd + V(0.06 * hr * math.sin(a), 0.055 * hr, 0.062 * hr * math.cos(a))
        hgt = 0.035 if h == 'crown' else 0.05 + 0.02 * (i % 2)
        m.cap(base, base + V(0.004 * hr * math.sin(a), hgt * hr, 0.004 * hr * math.cos(a)), 0.008 * hr, 0.002 * hr,
              'gold' if h == 'crown' else 'plate', gp if h == 'crown' else {'col': C(60, 40, 80), 'glow': C(200, 90, 255)})
    ring(m, hd + V(0, 0.055 * hr, 0), 0.064 * hr, 0.066 * hr, 0, 'gold' if h == 'crown' else 'plate',
         gp if h == 'crown' else {'col': C(60, 40, 80)}, 0.011 * hr, 16)
    gem = C(220, 30, 30) if h == 'crown' else C(190, 80, 255)
    for a in (0.0, math.pi, math.pi / 2, -math.pi / 2):
        m.ell(hd + V(0.066 * hr * math.sin(a), 0.058 * hr, 0.068 * hr * math.cos(a)), 0.008 * hr, 0.008 * hr, 0.005 * hr, 'eye', {'col': gem})


def weapon_frame(J):
    hand_, el = J['haR'], J['elR']
    d = norm(hand_ - el)
    if J['state'] != 'atk' or J['t'] < 0.4 or J['t'] > 0.85:
        d = norm(d + V(0.25, 1.1, -0.2))
    up = norm(np.cross(d, V(0, 0, 1)) + V(0, 0.001, 0))
    flat = norm(np.cross(d, up))
    return hand_, d, up, flat


def blade(m, base, d, up, ln, w0, w1, p, mat='plate', fuller=True, seed=0.0, glow=None):
    """flat double-edged blade made of triangles (edges) and a raised spine"""
    flat = norm(np.cross(d, up))
    tip = base + d * ln
    n = 4
    for i in range(n):
        a = i / n; b = (i + 1) / n
        wa = w0 + (w1 - w0) * a; wb = w0 + (w1 - w0) * b if i < n - 1 else 0.0
        ca = base + d * ln * a; cb = base + d * ln * b
        for sgn in (1, -1):
            m.quad(ca, cb, cb + up * wb * sgn, ca + up * wa * sgn, mat, p, seed)
    m.cap(base, base + d * ln * 0.86, w0 * 0.22, w1 * 0.15, mat, p, seed + 1)
    if fuller:
        m.cap(base + d * ln * 0.05 + flat * w0 * 0.05, base + d * ln * 0.6, w0 * 0.12, w0 * 0.06, 'dark' if glow is None else 'glowmat', {'col': glow} if glow is not None else None)
    return tip


def hilt(m, hand_, d, up, guard=0.05, grip=0.07, pom='metal', gp=None, wrap=(70, 40, 24)):
    gp = gp or {'col': C(150, 140, 120), 'bloody': False}
    m.cap(hand_ - d * grip * H, hand_ + d * 0.015 * H, 0.011 * H, 0.011 * H, 'leather', {'col': C(*wrap)})
    m.ell(hand_ - d * (grip + 0.012) * H, 0.016 * H, 0.016 * H, 0.016 * H, pom, gp)
    m.cap(hand_ + d * 0.02 * H - up * guard * H, hand_ + d * 0.02 * H + up * guard * H, 0.01 * H, 0.01 * H, pom, gp)
    for sg in (-1, 1):
        m.ell(hand_ + d * 0.02 * H + up * guard * H * sg, 0.013 * H, 0.013 * H, 0.013 * H, pom, gp)


def layer_weapon(m, f, k):
    J = pose(f)
    w = WEAPONS[k]
    hand_, d, up, flat = weapon_frame(J)
    steel = {'col': C(176, 178, 186), 'shin': 60}
    if w == 'dagger':
        hilt(m, hand_, d, up, guard=0.03, grip=0.05)
        blade(m, hand_ + d * 0.03 * H, d, up, 0.2 * H, 0.016 * H, 0.008 * H, steel)
    elif w == 'club':
        m.cap(hand_ - d * 0.06 * H, hand_ + d * 0.3 * H, 0.012 * H, 0.03 * H, 'wood', {})
        top = hand_ + d * 0.3 * H
        for i in range(8):
            a = i * 0.8
            q = top - d * (0.03 + 0.03 * (i % 3)) * H + (up * math.cos(a) + flat * math.sin(a)) * 0.03 * H
            m.cap(q, q + (up * math.cos(a) + flat * math.sin(a)) * 0.016 * H, 0.006 * H, 0.002 * H, 'metal', {'col': C(120, 116, 110), 'bloody': False})
        for i in range(2):
            m.cap(hand_ + d * (0.12 + i * 0.1) * H, hand_ + d * (0.125 + i * 0.1) * H, 0.022 * H + i * 0.006 * H, 0.022 * H + i * 0.006 * H, 'metal', {'col': C(100, 96, 90)})
    elif w in ('sword', 'bastard', 'greatsword', 'runeblade', 'holysword', 'doomsword', 'abyssblade'):
        ln = {'sword': 0.46, 'bastard': 0.54, 'greatsword': 0.66, 'runeblade': 0.52, 'holysword': 0.56, 'doomsword': 0.7, 'abyssblade': 0.6}[w] * H
        wd = {'sword': 0.022, 'bastard': 0.024, 'greatsword': 0.032, 'runeblade': 0.024, 'holysword': 0.026, 'doomsword': 0.04, 'abyssblade': 0.028}[w] * H
        bp = dict(steel); glow = None; pom = 'metal'; gp = None
        if w == 'runeblade':
            bp = {'col': C(150, 170, 200), 'shin': 60}; glow = C(90, 200, 255)
        elif w == 'holysword':
            bp = {'col': C(230, 226, 210), 'etch': C(230, 190, 80), 'shin': 70}; glow = C(255, 240, 170); pom = 'gold'
        elif w == 'doomsword':
            bp = {'col': C(60, 50, 54), 'glow': C(255, 40, 10), 'shin': 30}; gp = {'col': C(90, 20, 18), 'bloody': False}
        elif w == 'abyssblade':
            bp = {'col': C(70, 40, 96), 'glow': C(210, 110, 255), 'shin': 40}; glow = C(200, 100, 255)
        grip = 0.1 if w in ('greatsword', 'doomsword', 'bastard') else 0.065
        hilt(m, hand_, d, up, guard=0.07 if ln > 0.5 * H else 0.05, grip=grip, pom=pom, gp=gp)
        tip = blade(m, hand_ + d * 0.03 * H, d, up, ln, wd, wd * (0.85 if w != 'doomsword' else 1.1), bp, glow=glow)
        if w == 'doomsword':
            for i in range(4):
                q = hand_ + d * (0.15 + i * 0.12) * H + up * wd
                m.tri(q, q + d * 0.04 * H, q + up * 0.03 * H - d * 0.01 * H, 'plate', bp)
        if w == 'abyssblade':
            m.ell(hand_ + d * 0.02 * H, 0.018 * H, 0.018 * H, 0.014 * H, 'eye', {'col': C(220, 120, 255)})
        if w == 'holysword':
            m.ell(hand_ + d * 0.02 * H, 0.012 * H, 0.012 * H, 0.01 * H, 'eye', {'col': C(120, 200, 255)})
    elif w in ('axe', 'greataxe'):
        big = w == 'greataxe'
        ln = (0.62 if big else 0.46) * H
        m.cap(hand_ - d * 0.08 * H, hand_ + d * (ln - 0.08 * H), 0.013 * H, 0.012 * H, 'wood', {})
        for i in range(3):
            m.cap(hand_ + d * (0.0 + i * 0.03) * H, hand_ + d * (0.012 + i * 0.03) * H, 0.015 * H, 0.015 * H, 'leather', {'col': C(70, 40, 24)})
        tip = hand_ + d * (ln - 0.08 * H)
        hp = {'col': C(150, 146, 140), 'shin': 40} if not big else {'col': C(176, 140, 76), 'etch': C(80, 110, 90), 'shin': 40}
        a = tip - d * (0.14 if big else 0.1) * H
        reach = (0.2 if big else 0.14) * H
        m.quad(a, tip, tip + up * reach + d * 0.05 * H, a + up * reach - d * 0.06 * H, 'plate', hp)
        m.quad(a + up * reach * 0.7 - d * 0.04 * H, tip + up * reach * 0.7 + d * 0.035 * H, tip + up * reach + d * 0.05 * H, a + up * reach - d * 0.06 * H, 'plate', dict(hp, col=hp['col'] * 1.15))
        if big:
            m.quad(a, tip, tip - up * reach * 0.8 + d * 0.04 * H, a - up * reach * 0.8 - d * 0.05 * H, 'plate', hp)
        m.cap(tip, tip + d * 0.04 * H, 0.01 * H, 0.002 * H, 'plate', hp)
    elif w == 'scythe':
        ln = 0.75 * H
        m.cap(hand_ - d * 0.15 * H, hand_ + d * (ln - 0.15 * H), 0.012 * H, 0.011 * H, 'wood', {'col': C(50, 34, 24)})
        top = hand_ + d * (ln - 0.15 * H)
        bp = {'col': C(80, 74, 80), 'glow': C(255, 50, 20), 'shin': 30}
        pts = [top + up * 0.02 * H]
        for i in range(1, 6):
            a = i / 5.0
            pts.append(top + up * (0.02 + 0.3 * a) * H - d * (0.1 * a * a) * H)
        for i in range(5):
            wa = 0.035 * H * (1 - i / 5.0); wb = 0.035 * H * (1 - (i + 1) / 5.0)
            m.quad(pts[i], pts[i + 1], pts[i + 1] - d * wb, pts[i] - d * wa, 'plate', bp)
        m.ell(top, 0.02 * H, 0.02 * H, 0.016 * H, 'bone', {'col': C(200, 190, 160)})


def layer_shield(m, f, k):
    """shield strapped to the outside of the left forearm (faces sideways, readable from behind)"""
    J = pose(f)
    el, hl = J['elL'], J['haL']
    n = norm(V(-0.4, 0, 0.92))
    u = V(0, 1, 0); w = norm(np.cross(n, u))
    c = lerp(el, hl, 0.5) + V(-0.03 * H, 0, 0.02 * H) + n * 0.03 * H
    P = lambda a, b, o=0.0: c + u * a * H + w * b * H + n * o * H
    sh = SHIELDS[k]
    if sh == 'buckler':
        pts = [P(0.075 * math.sin(a), 0.075 * math.cos(a)) for a in np.linspace(0, 2 * math.pi, 17)]
        for i in range(16):
            m.tri(P(0, 0, 0.012), pts[i], pts[i + 1], 'wood', {})
        for i in range(16):
            m.cap(pts[i], pts[i + 1], 0.008 * H, 0.008 * H, 'metal', {'col': C(110, 106, 100), 'bloody': False})
        m.ell(P(0, 0, 0.018), 0.022 * H, 0.022 * H, 0.022 * H, 'metal', {'col': C(150, 140, 120), 'bloody': False})
        rivets(m, [P(0.05 * math.sin(a), 0.05 * math.cos(a), 0.01) for a in np.linspace(0, 6.28, 8, endpoint=False)], 0.005 * H)
    elif sh == 'kite':
        kp = {'col': C(140, 140, 148), 'shin': 50}
        top, bot, lft, rgt = P(0.08, 0), P(-0.14, 0.0, -0.005), P(0.03, -0.068), P(0.03, 0.068)
        mid = P(0.0, 0.0, 0.016)
        for a_, b_ in ((top, lft), (lft, bot), (bot, rgt), (rgt, top)):
            m.tri(mid, a_, b_, 'plate', kp)
        for a_, b_ in ((top, lft), (lft, bot), (bot, rgt), (rgt, top)):
            m.cap(a_, b_, 0.007 * H, 0.007 * H, 'gold', {})
        m.cap(P(0.06, 0, 0.012), P(-0.11, 0, 0.012), 0.006 * H, 0.006 * H, 'plate', {'col': C(150, 30, 26)})
        m.cap(P(0.03, -0.05, 0.012), P(0.03, 0.05, 0.012), 0.006 * H, 0.006 * H, 'plate', {'col': C(150, 30, 26)})
    else:
        tp = {'col': C(110, 26, 22), 'bloody': False}
        q = [P(0.1, -0.075), P(0.1, 0.075), P(-0.14, 0.07), P(-0.14, -0.07)]
        mid = P(-0.02, 0, 0.012)
        for i in range(4):
            m.tri(mid, q[i], q[(i + 1) % 4], 'cloth', tp)
        for i in range(4):
            m.cap(q[i], q[(i + 1) % 4], 0.009 * H, 0.009 * H, 'plate', {'col': C(200, 160, 70), 'shin': 50})
        m.ell(P(0.0, 0.0, 0.022), 0.032 * H, 0.032 * H, 0.032 * H, 'gold', {})
        for a_, b_ in ((P(0.08, 0, 0.014), P(-0.12, 0, 0.014)), (P(0.0, -0.06, 0.014), P(0.0, 0.06, 0.014))):
            m.cap(a_, b_, 0.006 * H, 0.006 * H, 'plate', {'col': C(200, 160, 70), 'shin': 50})


def all_layers():
    """(group, variant, draw_fn) in file order"""
    L = [('body', 0, layer_body), ('hair', 0, layer_hair)]
    L += [('armor', i, (lambda m, f, i=i: layer_armor(m, f, i))) for i in range(len(ARMORS))]
    L += [('helm', i, (lambda m, f, i=i: layer_helm(m, f, i))) for i in range(len(HELMS))]
    L += [('weapon', i, (lambda m, f, i=i: layer_weapon(m, f, i))) for i in range(len(WEAPONS))]
    L += [('shield', i, (lambda m, f, i=i: layer_shield(m, f, i))) for i in range(len(SHIELDS))]
    return L
