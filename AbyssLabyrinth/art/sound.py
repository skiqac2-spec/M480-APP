"""Original music + sound effects for Abyss Labyrinth, synthesized from scratch.

Output: abyss_snd.dat
  'ASND' u16 version, u16 ntracks, u16 nsfx, u16 rate
  ntracks x (u32 offset, u32 nsamples, u32 loopstart, u32 bytes)   music: IMA ADPCM mono (blocks of 1024 samples)
  nsfx    x (u32 offset, u32 nsamples)                            sfx:   raw s16 mono
"""
import sys, struct, math
import numpy as np
from scipy.signal import butter, lfilter, fftconvolve

SR = 22050
rng = np.random.RandomState(1234)

# ------------------------------------------------------------------ helpers
def t_(sec):
    return np.arange(int(sec * SR)) / SR

def env_adsr(n, a=0.01, d=0.1, s=0.7, r=0.2):
    e = np.ones(n) * s
    na, nd, nr = int(a * SR), int(d * SR), int(r * SR)
    na = min(na, n); e[:na] = np.linspace(0, 1, na, endpoint=False)
    nd = min(nd, max(0, n - na)); e[na:na + nd] = np.linspace(1, s, nd, endpoint=False)
    nr = min(nr, n); e[n - nr:] *= np.linspace(1, 0, nr)
    return e

def lp(x, fc, order=2):
    b, a = butter(order, min(fc / (SR / 2), 0.99), 'low'); return lfilter(b, a, x)

def hp(x, fc, order=2):
    b, a = butter(order, min(fc / (SR / 2), 0.99), 'high'); return lfilter(b, a, x)

def bp(x, lo, hi, order=2):
    b, a = butter(order, [lo / (SR / 2), min(hi / (SR / 2), 0.99)], 'band'); return lfilter(b, a, x)

def saw(f, t, ph=0.0):
    return 2 * ((f * t + ph) % 1.0) - 1

def note(m):
    return 440.0 * 2 ** ((m - 69) / 12.0)

_IR = {}
def reverb(x, sec=2.5, wet=0.35, damp=3000):
    key = (sec, damp)
    if key not in _IR:
        n = int(sec * SR)
        ir = rng.randn(n) * np.exp(-np.arange(n) / (SR * sec / 6.0))
        ir = lp(ir, damp, 1); ir[:int(0.01 * SR)] *= np.linspace(0, 1, int(0.01 * SR))
        _IR[key] = ir / np.sqrt(np.sum(ir ** 2))
    y = fftconvolve(x, _IR[key])[:len(x)]
    return x * (1 - wet) + y * wet

def place(buf, snd, pos, gain=1.0):
    p = int(pos * SR)
    if p >= len(buf): return
    n = min(len(snd), len(buf) - p)
    buf[p:p + n] += snd[:n] * gain

def pad(freqs, dur, bright=1200, det=0.004, vowel=None):
    t = t_(dur); x = np.zeros_like(t)
    for f in freqs:
        for d in (-det, 0, det):
            x += saw(f * (1 + d), t, rng.rand())
    x = lp(x, bright, 2)
    if vowel:  # crude choir formants
        y = np.zeros_like(x)
        for fc, g in vowel:
            y += bp(x, fc * 0.85, fc * 1.15) * g
        x = y * 2.2
    return x * env_adsr(len(t), 1.5, 0.5, 0.9, 1.8) / (len(freqs) * 3)

VOWEL_A = [(700, 1.0), (1150, 0.6), (2600, 0.25)]
VOWEL_O = [(450, 1.0), (800, 0.6), (2830, 0.15)]

def pluck(f, dur, damp=0.996, bright=0.5):
    n = int(dur * SR); N = max(2, int(SR / f))
    exc = np.zeros(n)
    exc[:N] = rng.uniform(-1, 1, N) * bright + rng.uniform(-1, 1, N) * (1 - bright) * 0.3
    a = np.zeros(N + 2); a[0] = 1; a[N] = -damp * 0.5; a[N + 1] = -damp * 0.5
    out = lfilter([1.0], a, exc)
    return out * env_adsr(n, 0.002, 0.05, 1.0, 0.05)

def bell(f, dur, ratio=3.5, idx=4.0):
    t = t_(dur)
    e = np.exp(-t * 2.2)
    mod = np.sin(2 * np.pi * f * ratio * t) * idx * np.exp(-t * 3)
    return np.sin(2 * np.pi * f * t + mod) * e * 0.5

def flute(f, dur):
    t = t_(dur)
    vib = 1 + 0.006 * np.sin(2 * np.pi * 5.2 * t) * np.clip(t * 2, 0, 1)
    ph = np.cumsum(f * vib) / SR
    x = np.sin(2 * np.pi * ph) + 0.25 * np.sin(4 * np.pi * ph) + lp(rng.randn(len(t)), 3000) * 0.08
    return x * env_adsr(len(t), 0.12, 0.1, 0.8, 0.25) * 0.4

def kick(dur=0.4, f0=110, f1=42):
    t = t_(dur)
    f = f1 + (f0 - f1) * np.exp(-t * 25)
    return np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t * 7)

def taiko(dur=0.7, f=70):
    t = t_(dur)
    body = np.sin(2 * np.pi * np.cumsum(f + 40 * np.exp(-t * 18)) / SR) * np.exp(-t * 5)
    skin = lp(rng.randn(len(t)), 900) * np.exp(-t * 30) * 0.6
    return (body + skin) * 0.9

def snare(dur=0.25):
    t = t_(dur)
    return (hp(rng.randn(len(t)), 1500) * 0.6 + np.sin(2 * np.pi * 190 * t) * 0.4) * np.exp(-t * 18)

def metal_hit(dur=1.6, f=420):
    t = t_(dur)
    x = sum(np.sin(2 * np.pi * f * r * t) * a for r, a in [(1, 1), (2.76, .6), (5.4, .4), (8.93, .25)])
    return x * np.exp(-t * 3) * 0.35

def strings(f, dur, bright=2500):
    t = t_(dur)
    x = saw(f, t) + saw(f * 1.003, t, 0.3) + saw(f * 0.997, t, 0.6)
    return lp(x, bright) * env_adsr(len(t), 0.02, 0.08, 0.6, 0.06) * 0.25

def wind(dur, lo=300, hi=1200):
    t = t_(dur)
    x = rng.randn(len(t))
    mod = 0.5 + 0.5 * np.sin(2 * np.pi * 0.07 * t + rng.rand() * 6) * np.sin(2 * np.pi * 0.023 * t)
    return bp(x, lo, hi) * mod * 0.5

def finish(x, peak=0.7, fade=0.0):
    x = x - np.mean(x)
    m = np.max(np.abs(x)) + 1e-9
    x = x / m * peak
    x = np.tanh(x * 1.2) / np.tanh(1.2) * peak
    if fade:
        nf = int(fade * SR); x[-nf:] *= np.linspace(1, 0, nf)
    return x

def make_loop(x, xfade=1.5):
    """crossfade the tail into the head so the track loops seamlessly"""
    n = int(xfade * SR)
    head = x[:n].copy(); tail = x[-n:]
    w = np.linspace(0, 1, n)
    x = x[:-n].copy()
    x[:n] = head * w + tail * (1 - w)
    return x

# ------------------------------------------------------------------ music
def m_title():
    dur = 64; x = np.zeros(int(dur * SR))
    chords = [[50, 57, 62, 65], [46, 53, 58, 62], [48, 55, 60, 63], [45, 52, 57, 61]]
    for i in range(8):
        c = chords[i % 4]
        place(x, pad([note(m) for m in c], 8.5, 900, vowel=VOWEL_O if i % 2 else VOWEL_A), i * 8, 1.0)
        place(x, pad([note(c[0] - 12)], 8.5, 300), i * 8, 0.8)
    mel = [74, 72, 69, 70, 69, 67, 65, 67, 69, 65, 62, 64, 65, 62, 61, 62]
    for i, m in enumerate(mel):
        place(x, bell(note(m), 3.0), 4 + i * 3.6, 0.5)
    for i in range(16):
        place(x, metal_hit(2.0, 120), i * 4, 0.25)
    return make_loop(finish(reverb(x, 3.5, 0.45)))

def m_town():
    dur = 72; x = np.zeros(int(dur * SR))
    prog = [(57, [0, 3, 7]), (53, [0, 4, 7]), (55, [0, 4, 7]), (52, [0, 3, 7])]   # Am F G Em
    beat = 0.42
    for bar in range(int(dur / (beat * 8))):
        root, iv = prog[bar % 4]
        arp = [root - 12, root + iv[1], root + iv[2], root + 12, root + iv[2], root + iv[1], root + 12, root + iv[2] + 12]
        for k, m in enumerate(arp):
            place(x, pluck(note(m), 1.6, 0.997), bar * beat * 8 + k * beat, 0.5)
        place(x, pad([note(root - 12), note(root - 5)], beat * 8 + 0.5, 600), bar * beat * 8, 0.35)
    mel = [76, 74, 72, 71, 72, 69, 71, 72, 74, 72, 71, 67, 69]
    for i, m in enumerate(mel):
        place(x, flute(note(m), 2.4), 13.4 + i * 3.36, 0.35)
    return make_loop(finish(reverb(x, 2.2, 0.3)))

def m_dungeon(kind):
    dur = 80; x = np.zeros(int(dur * SR)); t = t_(dur)
    base = {0: 38, 1: 41, 2: 36, 3: 37, 4: 43}[kind]
    drone = (np.sin(2 * np.pi * note(base) * t) + 0.5 * saw(note(base + 7), t)) * 0.3
    drone = lp(drone, 400) * (0.7 + 0.3 * np.sin(2 * np.pi * 0.05 * t))
    x += drone
    x += wind(dur, 200, 900 if kind != 4 else 2400) * (0.6 if kind == 4 else 0.35)
    if kind == 0:   # caves / crypts: distant metal, dripping, whisper choir
        for i in range(14):
            place(x, metal_hit(2.5, rng.uniform(90, 260)), rng.uniform(0, dur - 3), 0.3)
        for i in range(40):
            place(x, bell(rng.uniform(1800, 2600), 0.4, 1.0, 1.0), rng.uniform(0, dur - 1), 0.12)
        for i in range(5):
            place(x, pad([note(base + 12), note(base + 15), note(base + 19)], 10, 800, vowel=VOWEL_O), i * 16, 0.6)
    elif kind == 1:   # desert tombs: phrygian flute + frame drum
        scale = [0, 1, 4, 5, 7, 8, 10, 12]
        for i in range(18):
            place(x, flute(note(base + 24 + scale[rng.randint(0, 8)]), rng.uniform(1.2, 3.0)), 2 + i * 4.3, 0.35)
        for i in range(0, int(dur / 0.9)):
            if i % 4 in (0, 3):
                place(x, taiko(0.6, 95), i * 0.9, 0.25)
    elif kind == 2:   # jungle: wooden plucks ostinato, frogs-like clicks
        pat = [0, 3, 7, 10, 7, 3]
        for i in range(int(dur / 0.35)):
            place(x, pluck(note(base + 24 + pat[i % 6]), 0.5, 0.985, 0.9), i * 0.35, 0.25)
        for i in range(60):
            place(x, bell(rng.uniform(900, 1500), 0.15, 2.0, 3.0), rng.uniform(0, dur - 1), 0.1)
    elif kind == 3:   # hell: industrial hits, choir, distorted drone
        x += np.tanh(lp(saw(note(base - 12), t) * 3, 300)) * 0.25
        for i in range(int(dur / 1.6)):
            place(x, metal_hit(1.2, 70), i * 1.6, 0.35)
            if i % 2: place(x, kick(0.5, 80, 30), i * 1.6 + 0.8, 0.4)
        for i in range(5):
            place(x, pad([note(base + 12), note(base + 13), note(base + 19)], 12, 1000, vowel=VOWEL_A), i * 16, 0.6)
    else:             # snow: cold bells, high pads
        for i in range(5):
            place(x, pad([note(base + 24), note(base + 27), note(base + 31)], 14, 2000), i * 16, 0.5)
        for i in range(30):
            place(x, bell(note(base + 36 + [0, 3, 7, 10, 12][rng.randint(0, 5)]), 3.0, 2.0, 2.5), rng.uniform(0, dur - 3), 0.25)
    return make_loop(finish(reverb(x, 3.0, 0.4)))

def m_battle(boss=False):
    bpm = 140 if not boss else 120
    beat = 60.0 / bpm
    bars = 28 if not boss else 32
    dur = bars * 4 * beat + 1.6
    x = np.zeros(int(dur * SR))
    prog = [40, 40, 43, 38] if not boss else [38, 38, 34, 36]
    for bar in range(bars):
        r = prog[bar % 4]
        for k in range(8):
            place(x, strings(note(r + 12 + (7 if k in (3, 6) else 0)), beat * 0.45, 2200), bar * 4 * beat + k * beat / 2, 0.6)
        for k in (0, 1.5, 2, 3, 3.5):
            place(x, taiko(0.6, 65 if k in (0, 2) else 90), bar * 4 * beat + k * beat, 0.8 if k in (0, 2) else 0.5)
        place(x, snare(), bar * 4 * beat + 1 * beat, 0.35); place(x, snare(), bar * 4 * beat + 3 * beat, 0.35)
        if bar % 2 == 0:
            place(x, pad([note(r), note(r + 7), note(r + 12)], 8 * beat, 1800, vowel=VOWEL_A if boss else None), bar * 4 * beat, 0.5 if boss else 0.35)
        if bar % 4 == 3:
            for k in range(3):
                place(x, strings(note(r + 24 - k), beat * 0.9, 3500), bar * 4 * beat + 2 * beat + k * beat * 0.6, 0.45)
    if boss:
        for i in range(bars // 2):
            place(x, metal_hit(2.0, 60), i * 8 * beat, 0.5)
    return make_loop(finish(reverb(x, 1.6, 0.22), 0.75), 1.0)

def m_jingle(win=True):
    x = np.zeros(int((5 if win else 6) * SR))
    if win:
        for i, m in enumerate([64, 67, 71, 76]):
            place(x, strings(note(m), 0.35 if i < 3 else 2.4, 3500), i * 0.22, 0.7)
            place(x, bell(note(m + 12), 2.5), i * 0.22, 0.3)
        place(x, taiko(1.0, 60), 0.66, 0.8)
    else:
        for i, m in enumerate([50, 49, 46, 45]):
            place(x, pad([note(m), note(m + 3), note(m - 12)], 2.0, 700, vowel=VOWEL_O), i * 1.2, 0.8)
        place(x, metal_hit(4.0, 55), 0, 0.6)
    return finish(reverb(x, 3.0, 0.4), 0.75, 0.4)

# ------------------------------------------------------------------ sfx
def noise(sec): return rng.randn(int(sec * SR))
def seq(parts, total):
    buf = np.zeros(int(total * SR))
    for pos, snd in parts: place(buf, snd, pos)
    return buf
def sx(x, peak=0.8): return finish(x, peak)

def sfx_list():
    S = []
    t = t_(0.05); S.append(sx(np.sin(2 * np.pi * 1400 * t) * np.exp(-t * 80), 0.4))                       # 0 click
    S.append(sx(lp(noise(0.12), 500) * np.exp(-t_(0.12) * 35), 0.35))                                    # 1 step
    S.append(sx(kick(0.25, 90, 50) + lp(noise(0.25), 800) * np.exp(-t_(0.25) * 20) * 0.5, 0.5))        # 2 bump
    t = t_(0.28); S.append(sx(bp(noise(0.28), 600, 4000) * np.sin(np.pi * t / 0.28) ** 2, 0.6))         # 3 swing
    t = t_(0.3); S.append(sx(lp(noise(0.3), 1200) * np.exp(-t * 14) + kick(0.3, 140, 60) * 0.8, 0.85))   # 4 hit flesh
    S.append(sx(seq([(0, S[4]), (0, metal_hit(0.3, 900) * 0.8)], 0.3), 0.9))  # 5 crit
    t = t_(0.35); S.append(sx(hp(noise(0.35), 1500) * np.sin(np.pi * t / 0.35) ** 3, 0.4))              # 6 miss
    t = t_(0.7)                                                                                           # 7 growl
    g = saw(70 + 15 * np.sin(2 * np.pi * 7 * t), t) * (0.6 + 0.4 * rng.rand(len(t)))
    S.append(sx(bp(g, 120, 900) * env_adsr(len(t), 0.05, 0.2, 0.8, 0.3), 0.75))
    t = t_(0.4); v = saw(140 - 50 * t, t)                                                               # 8 hurt grunt
    S.append(sx(sum(bp(v, f * 0.8, f * 1.2) * a for f, a in VOWEL_A) * env_adsr(len(t), 0.01, 0.1, 0.6, 0.2), 0.7))
    t = t_(0.9); sq = lp(noise(0.9), 700) * (np.sin(2 * np.pi * 9 * t) > 0) * np.exp(-t * 4)              # 9 die squelch
    S.append(sx(sq + bp(saw(55 - 20 * t, t), 80, 600) * np.exp(-t * 3) * 0.8, 0.8))
    t = t_(0.9); S.append(sx(bp(noise(0.9), 200, 2500) * np.sin(np.pi * t / 0.9) * (1 + 0.5 * np.sin(2 * np.pi * 25 * t)), 0.7))   # 10 fire
    t = t_(1.2); S.append(sx(lp(noise(1.2), 900) * np.exp(-t * 4) + kick(1.2, 80, 30), 0.9))            # 11 explosion
    t = t_(1.0); cr = hp(noise(1.0), 2000) * np.exp(-t * 6) * (rng.rand(len(t)) > 0.6)                     # 12 lightning
    S.append(sx(cr + lp(noise(1.0), 300) * np.exp(-t * 2) * 0.8, 0.9))
    t = t_(1.0); S.append(sx(sum(bell(f, 1.0, 2.01, 1.5) for f in (1800, 2400, 3100, 3700)) + hp(noise(1.0), 4000) * np.exp(-t * 5) * 0.3, 0.6))  # 13 frost
    S.append(sx(seq([(i * 0.09, bell(note(m), 1.0)) for i, m in enumerate([72, 76, 79, 84])], 1.4), 0.55))  # 14 heal
    t = t_(0.6); gl = lp(noise(0.6), 600) * (np.sin(2 * np.pi * 6 * t) > 0.3)                             # 15 drink
    S.append(sx(gl * env_adsr(len(t), 0.02, 0.1, 0.8, 0.1), 0.5))
    S.append(sx(seq([(i * 0.06, metal_hit(0.4, rng.uniform(2500, 4000))) for i in range(5)], 0.7), 0.5))  # 16 coins
    t = t_(0.8); cre = bp(saw(90 + 60 * np.sin(2 * np.pi * 3 * t), t), 300, 2000) * np.sin(np.pi * t / 0.8)    # 17 chest creak
    S.append(sx(cre, 0.5))
    t = t_(0.25); S.append(sx(np.sin(2 * np.pi * (600 + 900 * t / 0.25) * t) * np.exp(-t * 8), 0.45))     # 18 pickup
    S.append(sx(seq([(i * 0.14, strings(note(m), 0.6 if i < 3 else 1.4, 4000)) for i, m in enumerate([60, 64, 67, 72])], 2.0), 0.7))  # 19 level up
    t = t_(1.3); S.append(sx(bp(noise(1.3), 300, 3000) * np.sin(np.pi * t / 1.3) + np.sin(2 * np.pi * (200 + 500 * t) * t) * np.sin(np.pi * t / 1.3) * 0.4, 0.6))  # 20 portal
    S.append(sx(reverb(sum(bell(note(m), 2.0, 1.5, 1.0) for m in (79, 83, 86)), 2.0, 0.5)[:int(2.0 * SR)], 0.5))  # 21 shrine
    S.append(sx(S[16][:int(0.35 * SR)], 0.45))                                                          # 22 trade
    st = np.zeros(int(1.4 * SR)); place(st, taiko(0.8, 60), 0, 1.0); place(st, strings(note(52), 1.2, 3000), 0.0, 0.8)
    place(st, strings(note(53), 0.9, 3000), 0.25, 0.8)
    S.append(sx(reverb(st, 1.5, 0.3), 0.85))                                                            # 23 encounter
    t = t_(1.2); v = saw(160 - 90 * t, t)                                                               # 24 death scream
    S.append(sx(sum(bp(v, f * 0.8, f * 1.2) * a for f, a in VOWEL_A) * env_adsr(len(t), 0.02, 0.2, 0.7, 0.6), 0.75))
    return S

# ------------------------------------------------------------------ IMA ADPCM
STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
        130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166,
        1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845,
        8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767]
IDX = [-1, -1, -1, -1, 2, 4, 6, 8]
BLOCK = 1024

def adpcm_encode(s16):
    out = bytearray()
    for b in range(0, len(s16), BLOCK):
        blk = s16[b:b + BLOCK]
        if len(blk) < BLOCK: blk = np.concatenate([blk, np.zeros(BLOCK - len(blk), np.int16)])
        pred = int(blk[0]); idx = 0
        out += struct.pack('<hBB', pred, idx, 0)
        nib = []
        for v in blk[1:]:
            step = STEP[idx]; diff = int(v) - pred; code = 0
            if diff < 0: code = 8; diff = -diff
            dq = step >> 3
            if diff >= step: code |= 4; diff -= step; dq += step
            step >>= 1
            if diff >= step: code |= 2; diff -= step; dq += step
            step >>= 1
            if diff >= step: code |= 1; dq += step
            pred = pred - dq if code & 8 else pred + dq
            pred = max(-32768, min(32767, pred))
            idx = max(0, min(88, idx + IDX[code & 7]))
            nib.append(code)
        nib.append(0)
        for i in range(0, len(nib), 2):
            out.append(nib[i] | (nib[i + 1] << 4))
    return bytes(out)

def to16(x):
    return np.clip(np.round(x * 32767), -32768, 32767).astype(np.int16)

def main(path):
    tracks = [m_title(), m_town()] + [m_dungeon(k) for k in range(5)] + [m_battle(False), m_battle(True), m_jingle(True), m_jingle(False)]
    loops = [True] * 9 + [False, False]
    sfx = sfx_list()
    blobs = []; hdr_t = []; hdr_s = []
    off = 12 + len(tracks) * 16 + len(sfx) * 8
    for x, lp_ in zip(tracks, loops):
        s = to16(x * 0.85); data = adpcm_encode(s)
        hdr_t.append(struct.pack('<IIII', off, len(s), 0 if lp_ else 0xFFFFFFFF, len(data)))
        blobs.append(data); off += len(data)
    for x in sfx:
        s = to16(x); data = s.astype('<i2').tobytes()
        hdr_s.append(struct.pack('<II', off, len(s)))
        blobs.append(data); off += len(data)
    with open(path, 'wb') as f:
        f.write(b'ASND' + struct.pack('<HHHH', 1, len(tracks), len(sfx), SR) + b''.join(hdr_t) + b''.join(hdr_s) + b''.join(blobs))
    print('wrote', path, off // 1024, 'KB,', len(tracks), 'tracks', len(sfx), 'sfx')
    if len(sys.argv) > 2:   # preview wavs
        import wave, os
        os.makedirs(sys.argv[2], exist_ok=True)
        for i, x in enumerate(tracks + sfx):
            w = wave.open(os.path.join(sys.argv[2], '%s%02d.wav' % ('m' if i < len(tracks) else 's', i if i < len(tracks) else i - len(tracks))), 'wb')
            w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR); w.writeframes(to16(x).tobytes()); w.close()

if __name__ == '__main__':
    main(sys.argv[1])
