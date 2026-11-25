#!/usr/bin/env python3
"""Minimal Windows CE (ARM) user-mode test harness built on Unicorn.

Loads the real PS1Emu.exe, emulates the handful of COREDLL/AYGSHELL functions it
imports, feeds scripted key/touch events and saves screenshots.  The CPU model is
an XScale PXA270 (ARMv5TE), so any instruction newer than what the SCH-M480's
PXA312 understands faults here too.
"""
import os, sys, struct, re, math, argparse, wave
import numpy as np
import pefile
from PIL import Image, ImageDraw, ImageFont
from unicorn import *
from unicorn.arm_const import *

ap = argparse.ArgumentParser()
ap.add_argument('exe'); ap.add_argument('--root', default='cefs'); ap.add_argument('--script', default='script.txt')
ap.add_argument('--out', default='out'); ap.add_argument('--count', action='store_true', help='count instructions')
ap.add_argument('--mips', type=float, default=0, help='advance the clock by executed instructions (million insn/sec)')
ap.add_argument('--align', action='store_true', help='report unaligned 16/32-bit memory accesses');
ap.add_argument('--profile', action='store_true', help='per-function instruction profile (needs the unstripped exe via --symexe)');
ap.add_argument('--symexe', default='');
ap.add_argument('--max-insn', type=float, default=0, help='stop with a state dump after this many million instructions');
ap.add_argument('--free-mb', type=int, default=60); ap.add_argument('--cmdline', default=''); ap.add_argument('--clock', type=float, default=1000.0)
ap.add_argument('--appdir', default='\\Program Files\\Emu')
ap.add_argument('--trace-api', action='store_true'); ap.add_argument('--max-ms', type=int, default=600000)
args = ap.parse_args()
os.makedirs(args.out, exist_ok=True)
FREE_MB = args.free_mb

uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
uc.ctl_set_cpu_model(UC_CPU_ARM_PXA270)

def rd(a, n): return bytes(uc.mem_read(a, n))
def wr(a, b): uc.mem_write(a, bytes(b))
def r32(a): return struct.unpack('<I', rd(a, 4))[0]
def w32(a, v): wr(a, struct.pack('<I', v & 0xffffffff))
def reg(i): return uc.reg_read(UC_ARM_REG_R0 + i)
def cstr_b(a, lim=1 << 20):
    out = bytearray()
    while len(out) < lim:
        chunk = rd(a + len(out), 64)
        i = chunk.find(b'\0')
        if i >= 0: out += chunk[:i]; break
        out += chunk
    return bytes(out)
def cstr(a): return cstr_b(a).decode('cp949', 'replace')
def wstr(a, n=-1):
    out = bytearray()
    while True:
        c = rd(a + len(out), 2)
        if c == b'\0\0' or (n >= 0 and len(out) >= 2 * n): break
        out += c
    return out.decode('utf-16le', 'replace')

# ---- load PE ---------------------------------------------------------------
pe = pefile.PE(args.exe)
base = pe.OPTIONAL_HEADER.ImageBase
size = (pe.OPTIONAL_HEADER.SizeOfImage + 0xfff) & ~0xfff
uc.mem_map(base, size)
wr(base, pe.header)
for s in pe.sections:
    data = s.get_data()
    wr(base + s.VirtualAddress, data[:max(s.Misc_VirtualSize, 0) or len(data)])

syms = []
try:
    import subprocess
    for line in subprocess.run(['/opt/cegcc/bin/arm-mingw32ce-nm', '-n', args.symexe or args.exe], capture_output=True, text=True).stdout.splitlines():
        p = line.split()
        if len(p) == 3 and p[1] in 'TtDdBbRr': syms.append((int(p[0], 16), p[2]))
except Exception: pass
def sym(a):
    import bisect
    i = bisect.bisect_right(syms, (a, '~')) - 1
    if i >= 0 and a - syms[i][0] < 0x100000 and base <= a < base + size: return '%s+0x%x' % (syms[i][1], a - syms[i][0])
    return '?'

TRAP = 0xF0000000
uc.mem_map(TRAP, 0x10000)
imports = []
for ent in pe.DIRECTORY_ENTRY_IMPORT:
    for imp in ent.imports:
        name = imp.name.decode() if imp.name else 'ord%d' % imp.ordinal
        addr = TRAP + 16 * len(imports)
        imports.append(name)
        wr(addr, struct.pack('<I', 0xE12FFF1C if name == 'DispatchMessageW' else 0xE12FFF1E))  # bx r12 / bx lr
        w32(imp.address, addr)
EXIT = TRAP + 0xFF00
wr(EXIT, struct.pack('<I', 0xEAFFFFFE))

STACK_TOP = 0x0F100000
uc.mem_map(STACK_TOP - 0x100000, 0x100000)

# ---- memory allocators ---------------------------------------------------------
HEAP = 0x20000000; HEAP_SIZE = 96 << 20
uc.mem_map(HEAP, HEAP_SIZE)
heap_top = HEAP + 16; blocks = {}; freelist = {}
big_log = []
def malloc(n):
    global heap_top
    if n >= 128 * 1024:
        try: big_log.append((n, sym(uc.reg_read(UC_ARM_REG_LR))))
        except Exception: pass
    n = max((n + 15) & ~15, 16)
    fl = freelist.get(n)
    if fl: a = fl.pop()
    else:
        a = heap_top; heap_top += n + 16
        if heap_top > HEAP + HEAP_SIZE: raise MemoryError('harness heap exhausted')
    blocks[a] = n
    return a
def free(a):
    if a == 0: return
    n = blocks.pop(a, None)
    if n is None: raise RuntimeError('free of unknown block %x' % a)
    freelist.setdefault(n, []).append(a)

VA = 0x42000000; va_top = VA; va_blocks = {}; va_peak = 0
def valloc(n):
    global va_top, va_peak
    n = (n + 0xffff) & ~0xffff
    a = va_top; va_top += n
    uc.mem_map(a, n); va_blocks[a] = n
    va_peak = max(va_peak, sum(va_blocks.values()))
    return a
def vfree(a):
    n = va_blocks.pop(a, None)
    if n: uc.mem_unmap(a, n)
    return 1 if n else 0

# ---- virtual clock, events --------------------------------------------------------
clock = args.clock
insn_total = 0; insn_mark = 0
script = []
if os.path.exists(args.script):
    for line in open(args.script):
        p = line.split(None, 2)
        if len(p) >= 2 and p[0].isdigit(): script.append((int(p[0]), p[1], p[2].strip() if len(p) > 2 else ''))
script.sort(key=lambda e: e[0]); script_i = 0
while script_i < len(script) and script[script_i][0] < clock: script_i += 1
spawned = []
msgq = []
WM_KEYDOWN, WM_KEYUP, WM_MOUSEMOVE, WM_LBUTTONDOWN, WM_LBUTTONUP, WM_CLOSE, WM_ACTIVATE = 0x100, 0x101, 0x200, 0x201, 0x202, 0x10, 6
screen = np.zeros((320, 320), np.uint16)
shots = 0
wave_fresh = [True]
wav_data = bytearray(); wave_hdrs = []; wave_done_at = 0.0; wave_rate = 44100
stats = {'blit': 0, 'flush': 0, 'wavewrite': 0, 'api': 0}
log_lines = []

def save_shot(name):
    rgb = np.zeros((320, 320, 3), np.uint8)
    rgb[..., 0] = (screen >> 11) << 3; rgb[..., 1] = ((screen >> 5) & 63) << 2; rgb[..., 2] = (screen & 31) << 3
    Image.fromarray(rgb).save(os.path.join(args.out, name + '.png'))

def advance(ms):
    global clock
    clock += ms
    update_wave()

def update_wave():
    for h in wave_hdrs[:]:
        if clock >= h[1]:
            w32(h[0] + 16, (r32(h[0] + 16) | 1) & ~0x10)       # WHDR_DONE, not INQUEUE
            wave_hdrs.remove(h)

def pump_script():
    global script_i, insn_mark
    while script_i < len(script) and script[script_i][0] <= clock:
        t, cmd, arg = script[script_i]; script_i += 1
        print('[%7d ms] %s %s' % (clock, cmd, arg), flush=True)
        a = arg.split()
        if cmd == 'key': msgq.append((WM_KEYDOWN, int(a[0]), 0)); msgq.append((WM_KEYUP, int(a[0]), 0))
        elif cmd == 'down': msgq.append((WM_KEYDOWN, int(a[0]), 0))
        elif cmd == 'up': msgq.append((WM_KEYUP, int(a[0]), 0))
        elif cmd == 'tap':
            lp = int(a[0]) | (int(a[1]) << 16); msgq.append((WM_LBUTTONDOWN, 1, lp)); msgq.append((WM_LBUTTONUP, 0, lp))
        elif cmd == 'tdown': msgq.append((WM_LBUTTONDOWN, 1, int(a[0]) | (int(a[1]) << 16)))
        elif cmd == 'tmove': msgq.append((WM_MOUSEMOVE, 1, int(a[0]) | (int(a[1]) << 16)))
        elif cmd == 'tup': msgq.append((WM_LBUTTONUP, 0, int(a[0]) | (int(a[1]) << 16)))
        elif cmd == 'deactivate': msgq.append((WM_ACTIVATE, 0, 0))
        elif cmd == 'activate': msgq.append((WM_ACTIVATE, 1, 0))
        elif cmd == 'shot': save_shot(a[0])
        elif cmd == 'mark':
            print('    insns since last mark: %.1f M' % ((insn_total - insn_mark) / 1e6)); insn_mark = insn_total
        elif cmd == 'quit': msgq.append((WM_CLOSE, 0, 0))

# ---- files ---------------------------------------------------------------------------
files = {}; find_handles = {}; next_h = [0x7000]
def host_path(p, create=False):
    p = p.replace('/', '\\')
    cur = os.path.abspath(args.root)
    parts = [x for x in p.split('\\') if x]
    for i, part in enumerate(parts):
        cand = os.path.join(cur, part)
        if not os.path.exists(cand) and os.path.isdir(cur):
            for e in os.listdir(cur):
                if e.lower() == part.lower(): cand = os.path.join(cur, e); break
        cur = cand
    return cur

class CFile:
    def __init__(s, f): s.f = f; s.eof = False

def fake_file(f):
    a = malloc(32); files[a] = CFile(f); return a
STDF = [malloc(32) for _ in range(3)]

# ---- printf / scanf engines ---------------------------------------------------------
class VA:
    """argument reader: registers r(start)..r3, then the stack; or a va_list pointer"""
    def __init__(s, start=None, ptr=None):
        s.i = start; s.ptr = ptr; s.sp = uc.reg_read(UC_ARM_REG_SP)
    def word(s):
        if s.ptr is not None:
            v = r32(s.ptr); s.ptr += 4; return v
        if s.i < 4: v = reg(s.i)
        else: v = r32(s.sp + 4 * (s.i - 4))
        s.i += 1; return v
    def dword(s):
        lo = s.word(); hi = s.word(); return lo | (hi << 32)

FMT = re.compile(r'%([-+ #0]*)(\*|\d+)?(?:\.(\*|\d+))?(hh|h|ll|l|L|z|j|t|I64)?([diouxXcspfFeEgG%n])')
def c_format(fmt, va):
    out = []; pos = 0
    for m in FMT.finditer(fmt):
        out.append(fmt[pos:m.start()]); pos = m.end()
        flags, width, prec, ln, conv = m.groups()
        if conv == '%': out.append('%'); continue
        if width == '*': width = str(struct.unpack('<i', struct.pack('<I', va.word()))[0])
        if prec == '*': prec = str(va.word())
        spec = '%' + (flags or '') + (width or '') + ('.' + prec if prec is not None else '')
        if conv in 'di':
            v = va.dword() if ln in ('ll', 'I64', 'j') else va.word()
            bits = 64 if ln in ('ll', 'I64', 'j') else 32
            if v >= 1 << (bits - 1): v -= 1 << bits
            if ln == 'h': v = struct.unpack('<h', struct.pack('<H', v & 0xffff))[0]
            if ln == 'hh': v = struct.unpack('<b', struct.pack('<B', v & 0xff))[0]
            out.append((spec + 'd') % v)
        elif conv in 'ouxX':
            v = va.dword() if ln in ('ll', 'I64', 'j') else va.word()
            if ln == 'h': v &= 0xffff
            if ln == 'hh': v &= 0xff
            out.append((spec + ('d' if conv == 'u' else conv)) % v)
        elif conv == 'c': out.append((spec + 'c') % chr(va.word() & 0xff))
        elif conv == 's':
            a = va.word(); out.append((spec + 's') % ('(null)' if a == 0 else cstr_b(a).decode('latin-1')))
        elif conv == 'p': out.append('0x%08x' % va.word())
        elif conv in 'fFeEgG':
            out.append((spec + conv) % struct.unpack('<d', struct.pack('<Q', va.dword()))[0])
        elif conv == 'n': w32(va.word(), len(''.join(out)))
    out.append(fmt[pos:])
    return ''.join(out).encode('latin-1', 'replace')

def c_scanf(s, fmt, va):
    """returns the number of assigned items; s and fmt are latin-1 strings"""
    si = 0; fi = 0; n = 0
    def store(v, ln, conv):
        a = va.word()
        if ln == 'hh': wr(a, struct.pack('<B', v & 0xff))
        elif ln == 'h': wr(a, struct.pack('<H', v & 0xffff))
        elif ln in ('ll',): wr(a, struct.pack('<Q', v & (2**64 - 1)))
        else: w32(a, v)
    while fi < len(fmt):
        c = fmt[fi]
        if c.isspace():
            while si < len(s) and s[si].isspace(): si += 1
            fi += 1; continue
        if c != '%':
            if si >= len(s) or s[si] != c: return n if (n or si < len(s)) else -1
            si += 1; fi += 1; continue
        m = re.compile(r'%(\*)?(\d+)?(hh|h|ll|l|L)?(\[\^?\]?[^\]]*\]|[diouxXcsnfeg%])').match(fmt, fi)
        if not m: raise RuntimeError('scanf format ' + fmt)
        fi = m.end(); sup, width, ln, conv = m.groups(); width = int(width) if width else None
        if conv == '%':
            if si < len(s) and s[si] == '%': si += 1; continue
            return n
        if conv == 'n':
            if not sup: w32(va.word(), si)
            continue
        if conv[0] not in 'c[':
            while si < len(s) and s[si].isspace(): si += 1
        if si >= len(s) and conv != 'n': return n if n else -1
        if conv in 'diouxX':
            pat = {'d': r'[-+]?\d+', 'u': r'[-+]?\d+', 'i': r'[-+]?(0[xX][0-9a-fA-F]+|\d+)', 'o': r'[-+]?[0-7]+',
                   'x': r'[-+]?(0[xX])?[0-9a-fA-F]+', 'X': r'[-+]?(0[xX])?[0-9a-fA-F]+'}[conv]
            mm = re.compile(pat).match(s[si:si + width] if width else s, 0 if width else si)
            if not mm: return n
            tok = mm.group(0); si += len(tok)
            v = int(tok, 16 if conv in 'xX' else 8 if conv == 'o' else 0 if conv == 'i' else 10)
            if not sup: store(v, ln, conv); n += 1
        elif conv in 'feg':
            mm = re.compile(r'[-+]?(\d+\.?\d*([eE][-+]?\d+)?|\.\d+)').match(s, si)
            if not mm: return n
            si = mm.end()
            if not sup:
                a = va.word(); v = float(mm.group(0))
                wr(a, struct.pack('<d', v) if ln == 'l' else struct.pack('<f', v)); n += 1
        elif conv == 'c':
            w = width or 1
            if si + w > len(s): return n if n else -1
            if not sup: wr(va.word(), s[si:si + w].encode('latin-1')); n += 1
            si += w
        else:
            if conv == 's': ok = lambda ch: not ch.isspace()
            else:
                body = conv[1:-1]; neg = body.startswith('^'); body = body[1:] if neg else body
                chars = set(); k = 0
                while k < len(body):
                    if k + 2 < len(body) and body[k + 1] == '-':
                        chars.update(chr(x) for x in range(ord(body[k]), ord(body[k + 2]) + 1)); k += 3
                    else: chars.add(body[k]); k += 1
                ok = (lambda ch: ch not in chars) if neg else (lambda ch: ch in chars)
            st = si
            while si < len(s) and ok(s[si]) and (width is None or si - st < width): si += 1
            if si == st: return n
            if not sup: wr(va.word(), s[st:si].encode('latin-1') + b'\0'); n += 1
    return n

# ---- GDI ----------------------------------------------------------------------------
dib = {}          # handle -> (ptr, w, h)
dc_state = {}     # dc -> dict(bitmap=, font=, color=)
font_tab = {}     # handle -> (px, bold)
FONT_R = '/usr/share/fonts/truetype/nanum/NanumGothic.ttf'; FONT_B = '/usr/share/fonts/truetype/nanum/NanumGothicBold.ttf'
font_cache = {}
def get_font(h):
    px, bold = font_tab.get(h, (16, 0))
    k = (px, bold)
    if k not in font_cache: font_cache[k] = ImageFont.truetype(FONT_B if bold else FONT_R, px)
    return font_cache[k]

def wrap_text(draw, text, font, width):
    lines = []
    for para in text.split('\n'):
        cur = ''
        for word in re.findall(r'\S+\s*', para) or ['']:
            if draw.textlength((cur + word).rstrip(), font=font) <= width or not cur: cur += word
            else: lines.append(cur.rstrip()); cur = word
            while draw.textlength(cur.rstrip(), font=font) > width and len(cur) > 1:
                k = len(cur) - 1
                while k > 1 and draw.textlength(cur[:k], font=font) > width: k -= 1
                lines.append(cur[:k]); cur = cur[k:]
        lines.append(cur.rstrip())
    return lines

def draw_text(dc, text, rect, fmt):
    st = dc_state.get(dc)
    if not st or st.get('bitmap') not in dib: return 0
    ptr, w, h = dib[st['bitmap']]
    l, t, r, b = rect
    l = max(l, 0); t0 = max(t, 0); r = min(r, w); b0 = min(b, h)
    if r <= l or b0 <= t0: return 0
    font = get_font(st.get('font')); col = st.get('color', 0xffffff)
    rgbc = (col & 0xff, (col >> 8) & 0xff, (col >> 16) & 0xff)
    raw = np.frombuffer(rd(ptr + t0 * w * 2, (b0 - t0) * w * 2), np.uint16).reshape(b0 - t0, w)
    rgb = np.zeros((b0 - t0, w, 3), np.uint8)
    rgb[..., 0] = (raw >> 11) << 3; rgb[..., 1] = ((raw >> 5) & 63) << 2; rgb[..., 2] = (raw & 31) << 3
    img = Image.fromarray(rgb); d = ImageDraw.Draw(img)
    asc, desc = font.getmetrics(); lh = asc + desc
    if fmt & 0x10: lines = wrap_text(d, text, font, r - l)
    else:
        lines = [text.replace('\n', ' ')]
        if fmt & 0x8000 and d.textlength(lines[0], font=font) > r - l:
            s = lines[0]
            while len(s) > 1 and d.textlength(s + '...', font=font) > r - l: s = s[:-1]
            lines = [s + '...']
    y = t - t0
    if (fmt & 0x20) and (fmt & 4): y += (b - t - lh) // 2
    clip = Image.new('L', img.size, 0); ImageDraw.Draw(clip).rectangle([l, 0, r - 1, b0 - t0 - 1], fill=255)
    layer = img.copy(); dl = ImageDraw.Draw(layer)
    for line in lines:
        tw = d.textlength(line, font=font)
        x = l + (r - l - tw) / 2 if fmt & 1 else (r - tw if fmt & 2 else l)
        dl.text((x, y), line, font=font, fill=rgbc); y += lh
    img.paste(layer, (0, 0), clip)
    a = np.asarray(img).astype(np.uint16)
    out = ((a[..., 0] >> 3) << 11) | ((a[..., 1] >> 2) << 5) | (a[..., 2] >> 3)
    wr(ptr + t0 * w * 2, out.astype('<u2').tobytes())
    return lh * len(lines)

# ---- API table ------------------------------------------------------------------------
api_hist = {}
wndproc = [0]; last_error = [0]; strtok_state = [0]; running = [True]
def sarg(i): return r32(uc.reg_read(UC_ARM_REG_SP) + 4 * i)       # i-th stack argument
def s32(v): return v - (1 << 32) if v & 0x80000000 else v
def dbl(lo, hi): return struct.unpack('<d', struct.pack('<II', lo, hi))[0]
def ret_dbl(v):
    lo, hi = struct.unpack('<II', struct.pack('<d', v)); uc.reg_write(UC_ARM_REG_R1, hi); return lo

def find_fill(fd, path, name):
    st = os.stat(os.path.join(path, name)); isdir = os.path.isdir(os.path.join(path, name))
    attr = 0x10 if isdir else 0x20
    if isdir and name.lower() == 'storage card': attr |= 0x100
    wr(fd, struct.pack('<I', attr) + b'\0' * 24 + struct.pack('<III', 0, st.st_size & 0xffffffff, 0))
    wr(fd + 40, (name + '\0').encode('utf-16le'))

def api_FindFirstFileW(a, b, c, d):
    pat = wstr(a); dirp = host_path(pat.rsplit('\\', 1)[0])
    if not os.path.isdir(dirp): last_error[0] = 3; return 0xffffffff
    names = sorted(os.listdir(dirp))
    if not names: last_error[0] = 18; return 0xffffffff
    h = next_h[0]; next_h[0] += 1; find_handles[h] = (dirp, names, 1)
    find_fill(b, dirp, names[0]); return h
def api_FindNextFileW(a, b, c, d):
    dirp, names, i = find_handles[a]
    if i >= len(names): last_error[0] = 18; return 0
    find_handles[a] = (dirp, names, i + 1); find_fill(b, dirp, names[i]); return 1

def api_GetFileAttributesW(a, b, c, d):
    p = host_path(wstr(a))
    if not os.path.exists(p): return 0xffffffff
    return 0x10 if os.path.isdir(p) else 0x20
def api_GetFileAttributesExW(a, b, c, d):
    p = host_path(wstr(a))
    if not os.path.exists(p): return 0
    wr(c, struct.pack('<I', 0x10 if os.path.isdir(p) else 0x20) + b'\0' * 24 + struct.pack('<II', 0, os.path.getsize(p) & 0xffffffff))
    return 1

def api_MultiByteToWideChar(cp, fl, src, n, *_):
    dst, dn = sarg(0), sarg(1)
    s = (cstr_b(src) + b'\0') if s32(n) < 0 else rd(src, n)
    w = s.decode('cp949', 'replace').encode('utf-16le')
    if dn == 0: return len(w) // 2
    if len(w) // 2 > dn: last_error[0] = 122; return 0
    wr(dst, w); return len(w) // 2
def api_WideCharToMultiByte(cp, fl, src, n, *_):
    dst, dn = sarg(0), sarg(1)
    s = (wstr(src) + '\0') if s32(n) < 0 else rd(src, 2 * n).decode('utf-16le')
    b = s.encode('cp949', 'replace')
    if dn == 0: return len(b)
    if len(b) > dn: last_error[0] = 122; return 0
    wr(dst, b); return len(b)

def api_PeekMessageW(msg, hwnd, lo, hi):
    pump_script()
    if not msgq: return 0
    m, wp, lp = msgq.pop(0)
    wr(msg, struct.pack('<IIIIIii', 0x1001, m, wp, lp, int(clock) & 0xffffffff, 0, 0)); return 1
def api_DispatchMessageW(msg, *_):
    hwnd, m, wp, lp = struct.unpack('<IIII', rd(msg, 16))
    uc.reg_write(UC_ARM_REG_R1, m); uc.reg_write(UC_ARM_REG_R2, wp); uc.reg_write(UC_ARM_REG_R3, lp)
    uc.reg_write(UC_ARM_REG_R12, wndproc[0]); return hwnd
def api_MsgWait(n, h, ms, *_):
    pump_script()
    if msgq: return 0
    nxt = script[script_i][0] - clock if script_i < len(script) else ms
    advance(max(1, min(ms, nxt))); pump_script(); return 0

def api_CreateDIBSection(hdc, bmi, usage, pbits):
    w, h = struct.unpack('<ii', rd(bmi + 4, 8)); h = abs(h)
    bpp = struct.unpack('<H', rd(bmi + 14, 2))[0]; assert bpp == 16
    p = malloc(w * h * 2); wr(p, b'\0' * (w * h * 2)); w32(pbits, p)
    hnd = next_h[0]; next_h[0] += 1; dib[hnd] = (p, w, h); return hnd
def api_SelectObject(dc, obj, *_):
    st = dc_state.setdefault(dc, {}); kind = 'bitmap' if obj in dib else 'font'
    prev = st.get(kind, 0x4002); st[kind] = obj; return prev
def api_BitBlt(dst, x, y, w):
    h, src, sx, sy = sarg(0), sarg(1), sarg(2), sarg(3)
    x, y, w, h, sx, sy = map(s32, (x, y, w, h, sx, sy))
    st = dc_state.get(src, {})
    if st.get('bitmap') in dib and w > 0 and h > 0:
        ptr, bw, bh = dib[st['bitmap']]
        assert 0 <= sx and 0 <= sy and sx + w <= bw and sy + h <= bh and x + w <= 320 and y + h <= 320, 'BitBlt out of range %r' % ((x, y, w, h, sx, sy),)
        a = np.frombuffer(rd(ptr + sy * bw * 2, h * bw * 2), np.uint16).reshape(h, bw)
        screen[y:y + h, x:x + w] = a[:, sx:sx + w]
    stats['blit'] += 1; return 1
def api_DrawTextW(dc, s, n, rc):
    fmt = sarg(0); l, t, r, b = struct.unpack('<iiii', rd(rc, 16))
    return draw_text(dc, wstr(s, s32(n)), (l, t, r, b), fmt)
def api_CreateFontIndirectW(lf, *_):
    hgt, = struct.unpack('<i', rd(lf, 4)); wt = r32(lf + 16)
    h = next_h[0]; next_h[0] += 1; font_tab[h] = (abs(hgt), wt >= 600); return h

def api_waveOutOpen(ph, dev, wfx, cb):
    global wave_rate
    wave_rate = r32(wfx + 4); w32(ph, 0x8001); return 0
def api_waveOutWrite(h, hdr, sz, *_):
    global wave_done_at
    data, ln = r32(hdr), r32(hdr + 4)
    if stats['wavewrite'] and not wave_fresh[0] and clock > wave_done_at + 1.0:
        stats['underrun'] = stats.get('underrun', 0) + 1; stats['gap_ms'] = stats.get('gap_ms', 0) + (clock - wave_done_at)
    wave_fresh[0] = False
    wav_data.extend(rd(data, ln))
    wave_done_at = max(wave_done_at, clock) + ln / 4 * 1000.0 / wave_rate
    w32(hdr + 16, (r32(hdr + 16) & ~1) | 0x10); wave_hdrs.append((hdr, wave_done_at)); stats['wavewrite'] += 1; return 0
def api_waveOutReset(*_):
    global wave_done_at
    wave_fresh[0] = True
    for h in wave_hdrs: w32(h[0] + 16, (r32(h[0] + 16) | 1) & ~0x10)
    wave_hdrs.clear(); wave_done_at = clock; return 0

def api_wfopen(p, m, *_):
    path = host_path(wstr(p)); mode = wstr(m)
    pm = ('r+b' if '+' in mode else 'rb') if 'r' in mode else ('w+b' if '+' in mode else 'wb') if 'w' in mode else 'ab'
    try: return fake_file(open(path, pm))
    except OSError: return 0
def api_fread(buf, sz, n, f):
    cf = files[f]; b = cf.f.read(sz * n)
    if len(b) < sz * n: cf.eof = True
    if b: wr(buf, b)
    return len(b) // sz if sz else 0
def api_fwrite(buf, sz, n, f):
    b = rd(buf, sz * n)
    if f in files: files[f].f.write(b)
    else: log_lines.append(b.decode('latin-1'))
    return n
def api_fgets(buf, n, f, *_):
    cf = files[f]; b = cf.f.readline(n - 1)
    if not b: cf.eof = True; return 0
    wr(buf, b + b'\0'); return buf
def api_fgetc(f, *_):
    cf = files[f]; b = cf.f.read(1)
    if not b: cf.eof = True; return 0xffffffff
    return b[0]
def out_text(f, b):
    if f in files: files[f].f.write(b)
    else: log_lines.append(b.decode('latin-1')); sys.stdout.write('    | ' + b.decode('latin-1'))
def api_fseek(f, off, wh, *_):
    try: files[f].f.seek(s32(off), wh)
    except OSError: return 0xffffffff
    files[f].eof = False; return 0
def api_fclose(f, *_):
    cf = files.pop(f, None)
    if cf: cf.f.close(); free(f)
    return 0
def api_strtol(s, endp, basev, *_, signed=True):
    t = cstr_b(s).decode('latin-1'); m = re.match(r'\s*[-+]?(0[xX][0-9a-fA-F]+|[0-9a-zA-Z]+)?', t)
    b = basev; body = t.strip()
    mm = re.match(r'\s*([-+]?)(0[xX])?', t); neg = mm.group(1) == '-'; i = mm.end() if (b in (0, 16) and mm.group(2)) else len(mm.group(0)) - len(mm.group(2) or '')
    if b == 0: b = 16 if mm.group(2) else (8 if t[i:i + 1] == '0' else 10)
    j = i; v = 0
    while j < len(t) and t[j].isalnum() and int(t[j], 36) < b: v = v * b + int(t[j], 36); j += 1
    if j == i: j = 0 if not mm.group(2) else i - 1
    if endp: w32(endp, s + j)
    v = -v if neg else v
    return v & 0xffffffff
def api_strtok(s, delim, *_):
    d = cstr_b(delim); p = s or strtok_state[0]
    if not p: return 0
    data = cstr_b(p); i = 0
    while i < len(data) and data[i] in d: i += 1
    if i == len(data): strtok_state[0] = 0; return 0
    j = i
    while j < len(data) and data[j] not in d: j += 1
    if j < len(data): wr(p + j, b'\0'); strtok_state[0] = p + j + 1
    else: strtok_state[0] = 0
    return p + i
def api_ecvt(lo, hi, nd, decpt, fcvt=False):
    v = dbl(lo, hi); psign = sarg(0); nd = s32(nd)
    w32(psign, 1 if v < 0 else 0); v = abs(v)
    if fcvt:
        s = '%.*f' % (max(nd, 0), v); ip, _, fp = s.partition('.'); digits = (ip.lstrip('0') + fp); dp = len(ip.lstrip('0'))
        if not ip.lstrip('0'):
            k = len(fp) - len(fp.lstrip('0')); dp = -k if fp.strip('0') else 0; digits = fp.lstrip('0') if fp.strip('0') else fp
    else:
        if v == 0: digits = '0' * max(nd, 1); dp = 0
        else:
            s = '%.*e' % (max(nd, 1) - 1, v); mant, _, ex = s.partition('e'); digits = mant.replace('.', ''); dp = int(ex) + 1
    w32(decpt, dp); buf = malloc(len(digits) + 1); wr(buf, digits.encode() + b'\0'); return buf

def mk_strdup(a):
    b = cstr_b(a) + b'\0'; p = malloc(len(b)); wr(p, b); return p
def api_realloc(p, n, *_):
    if p == 0: return malloc(n)
    old = blocks[p]; q = malloc(n); wr(q, rd(p, min(old, n))); free(p); return q
def api_calloc(n, sz, *_):
    p = malloc(n * sz); wr(p, b'\0' * (n * sz)); return p
def cmp(a, b): return (a > b) - (a < b)

API = {
 # --- C runtime ---
 'malloc': lambda n, *_: malloc(n), 'free': lambda p, *_: free(p) or 0, 'realloc': api_realloc, 'calloc': api_calloc,
 '_strdup': lambda a, *_: mk_strdup(a),
 'memcpy': lambda d, s, n, *_: (wr(d, rd(s, n)) if n else None, d)[1],
 'memmove': lambda d, s, n, *_: (wr(d, rd(s, n)) if n else None, d)[1],
 'memset': lambda d, c, n, *_: (wr(d, bytes([c & 0xff]) * n) if n else None, d)[1],
 'memcmp': lambda a, b, n, *_: cmp(rd(a, n), rd(b, n)) & 0xffffffff,
 'strlen': lambda a, *_: len(cstr_b(a)), 'wcslen': lambda a, *_: len(wstr(a)),
 'strcpy': lambda d, s, *_: (wr(d, cstr_b(s) + b'\0'), d)[1],
 'strncpy': lambda d, s, n, *_: (wr(d, (cstr_b(s, n)[:n]).ljust(n, b'\0')) if n else None, d)[1],
 'strcat': lambda d, s_, *_: (wr(d + len(cstr_b(d)), cstr_b(s_) + b'\0'), d)[1],
 'strncat': lambda d, s_, n, *_: (wr(d + len(cstr_b(d)), cstr_b(s_)[:n] + b'\0'), d)[1],
 'memchr': lambda a, c, n, *_: (lambda i: a + i if i >= 0 else 0)(rd(a, n).find(bytes([c & 0xff]))),
 'strcspn': lambda a, b, *_: next((i for i, ch in enumerate(cstr_b(a)) if ch in cstr_b(b)), len(cstr_b(a))),
 'strspn': lambda a, b, *_: next((i for i, ch in enumerate(cstr_b(a)) if ch not in cstr_b(b)), len(cstr_b(a))),
 'strpbrk': lambda a, b, *_: next((a + i for i, ch in enumerate(cstr_b(a)) if ch in cstr_b(b)), 0),
 'abs': lambda v, *_: abs(s32(v)), 'labs': lambda v, *_: abs(s32(v)),
 'strcmp': lambda a, b, *_: cmp(cstr_b(a), cstr_b(b)) & 0xffffffff,
 'strncmp': lambda a, b, n, *_: cmp(cstr_b(a)[:n], cstr_b(b)[:n]) & 0xffffffff,
 '_stricmp': lambda a, b, *_: cmp(cstr_b(a).lower(), cstr_b(b).lower()) & 0xffffffff,
 '_strnicmp': lambda a, b, n, *_: cmp(cstr_b(a)[:n].lower(), cstr_b(b)[:n].lower()) & 0xffffffff,
 'strchr': lambda a, c, *_: (lambda s, i: a + i if i >= 0 else 0)(cstr_b(a) + b'\0', (cstr_b(a) + b'\0').find(bytes([c & 0xff]))),
 'strrchr': lambda a, c, *_: (lambda i: a + i if i >= 0 else 0)((cstr_b(a) + b'\0').rfind(bytes([c & 0xff]))),
 'wcsrchr': lambda a, c, *_: (lambda i: a + 2 * i if i >= 0 else 0)(wstr(a).rfind(chr(c & 0xffff))),
 'strstr': lambda a, b, *_: (lambda i: a + i if i >= 0 else 0)(cstr_b(a).find(cstr_b(b))),
 'strtok': api_strtok,
 'tolower': lambda c, *_: ord(chr(c).lower()) if c < 128 else c, 'toupper': lambda c, *_: ord(chr(c).upper()) if c < 128 else c,
 '_isctype': lambda c, m, *_: int(c < 128 and bool((m & 1 and chr(c).isupper()) or (m & 2 and chr(c).islower()) or (m & 4 and chr(c).isdigit()) or (m & 8 and chr(c).isspace()) or (m & 0x10 and chr(c) in '!"#$%&\'()*+,-./:;<=>?@[\\]^_`{|}~') or (m & 0x80 and chr(c) in '0123456789abcdefABCDEF') or (m & 0x40 and c == 32))),
 'atoi': lambda a, *_: (lambda m: int(m.group(0)) if m and m.group(0).strip('+- \t') else 0)(re.match(r'\s*[-+]?\d*', cstr(a))) & 0xffffffff,
 'atof': lambda a, *_: ret_dbl((lambda m: float(m.group(0)) if m else 0.0)(re.match(r'\s*[-+]?(\d+\.?\d*([eE][-+]?\d+)?|\.\d+)', cstr(a)))),
 'strtol': api_strtol, 'strtoul': api_strtol,
 'sprintf': lambda buf, fmt, *_: (lambda b: (wr(buf, b + b'\0'), len(b))[1])(c_format(cstr_b(fmt).decode('latin-1'), VA(start=2))),
 'vsprintf': lambda buf, fmt, va, *_: (lambda b: (wr(buf, b + b'\0'), len(b))[1])(c_format(cstr_b(fmt).decode('latin-1'), VA(ptr=va))),
 'fprintf': lambda f, fmt, *_: (lambda b: (out_text(f, b), len(b))[1])(c_format(cstr_b(fmt).decode('latin-1'), VA(start=2))),
 'vfprintf': lambda f, fmt, va, *_: (lambda b: (out_text(f, b), len(b))[1])(c_format(cstr_b(fmt).decode('latin-1'), VA(ptr=va))),
 'sscanf': lambda s, fmt, *_: c_scanf(cstr_b(s).decode('latin-1'), cstr_b(fmt).decode('latin-1'), VA(start=2)) & 0xffffffff,
 'puts': lambda a, *_: out_text(0, cstr_b(a) + b'\n') or 0, 'putchar': lambda c, *_: out_text(0, bytes([c & 0xff])) or c,
 'fputc': lambda c, f, *_: out_text(f, bytes([c & 0xff])) or c, 'getchar': lambda *_: 0xffffffff,
 '_wfopen': api_wfopen, 'fclose': api_fclose, 'fread': api_fread, 'fwrite': api_fwrite, 'fgets': api_fgets, 'fgetc': api_fgetc,
 'fflush': lambda f, *_: (files[f].f.flush() if f in files else None, 0)[1],
 'feof': lambda f, *_: int(files[f].eof) if f in files else 0,
 'fseek': api_fseek,
 'ftell': lambda f, *_: files[f].f.tell(),
 '_fcloseall': lambda *_: 0, '_getstdfilex': lambda n, *_: STDF[n % 3], '_fpreset': lambda *_: 0,
 'floorf': lambda a, *_: struct.unpack('<I', struct.pack('<f', math.floor(struct.unpack('<f', struct.pack('<I', a))[0])))[0],
 '_isnan': lambda lo, hi, *_: int(math.isnan(dbl(lo, hi))), '_isnanf': lambda a, *_: int(math.isnan(struct.unpack('<f', struct.pack('<I', a))[0])),
 'rand': lambda *_: __import__('random').randint(0, 0x7fff), 'srand': lambda *_: 0,
 'sqrtf': lambda a, *_: struct.unpack('<I', struct.pack('<f', math.sqrt(max(struct.unpack('<f', struct.pack('<I', a))[0], 0.0))))[0],
 'sin': lambda lo, hi, *_: ret_dbl(math.sin(dbl(lo, hi))), 'cos': lambda lo, hi, *_: ret_dbl(math.cos(dbl(lo, hi))),
 'atan': lambda lo, hi, *_: ret_dbl(math.atan(dbl(lo, hi))), 'sqrt': lambda lo, hi, *_: ret_dbl(math.sqrt(max(dbl(lo, hi), 0.0))),
 'tan': lambda lo, hi, *_: ret_dbl(math.tan(dbl(lo, hi))), 'exp': lambda lo, hi, *_: ret_dbl(math.exp(min(dbl(lo, hi), 700.0))), 'log': lambda lo, hi, *_: ret_dbl(math.log(dbl(lo, hi)) if dbl(lo, hi) > 0 else -1e308),
 'floor': lambda lo, hi, *_: ret_dbl(math.floor(dbl(lo, hi))), 'ceil': lambda lo, hi, *_: ret_dbl(math.ceil(dbl(lo, hi))), 'fabs': lambda lo, hi, *_: ret_dbl(abs(dbl(lo, hi))),
 'pow': lambda lo, hi, lo2, hi2: ret_dbl(dbl(lo, hi) ** dbl(lo2, hi2) if (dbl(lo, hi) > 0 or float(dbl(lo2, hi2)).is_integer()) and not (dbl(lo, hi) == 0 and dbl(lo2, hi2) < 0) else 0.0),
 'atan2': lambda lo, hi, lo2, hi2: ret_dbl(math.atan2(dbl(lo, hi), dbl(lo2, hi2))), 'fmod': lambda lo, hi, lo2, hi2: ret_dbl(math.fmod(dbl(lo, hi), dbl(lo2, hi2)) if dbl(lo2, hi2) else 0.0),
 'log10': lambda lo, hi, *_: ret_dbl(math.log10(dbl(lo, hi)) if dbl(lo, hi) > 0 else -308.0),
 '_ecvt': api_ecvt, '_fcvt': lambda a, b, c, d: api_ecvt(a, b, c, d, True),
 # --- kernel ---
 'GetTickCount': lambda *_: (pump_script(), update_wave(), int(clock) & 0xffffffff)[2],
 'Sleep': lambda ms, *_: advance(ms) or 0,
 'VirtualAlloc': lambda a, n, t, p: a if a else valloc(n), 'VirtualFree': lambda a, n, t, *_: 1 if (t & 0x4000) else vfree(a),
 'VirtualProtect': lambda a, n, p, old: (w32(old, 4) if old else None, 1)[1], 'VirtualQuery': lambda *_: 0,
 'FlushInstructionCache': lambda *_: stats.__setitem__('flush', stats['flush'] + 1) or 1,
 'GlobalMemoryStatus': lambda p, *_: wr(p, struct.pack('<8I', 32, 50, 128 << 20, FREE_MB << 20, 0, 0, 32 << 20, 20 << 20)) or 0,
 'GetLastError': lambda *_: last_error[0], 'TerminateProcess': lambda *_: (dump_state('TerminateProcess called'), running.__setitem__(0, False), 1)[2],
 'GetLocalTime': lambda p, *_: wr(p, struct.pack('<8H', 2026, 11, 1, 16, 12, 0, 0, 0)) or 0,
 'FileTimeToSystemTime': lambda a, p, *_: wr(p, struct.pack('<8H', 2026, 11, 1, 16, 12, 0, 0, 0)) or 1,
 'printf': lambda fmt, *_: (lambda b: (out_text(0, b), len(b))[1])(c_format(cstr_b(fmt).decode('latin-1'), VA(start=1))),
 'GetSystemTime': lambda p, *_: wr(p, b'\0' * 16) or 0, 'SystemTimeToFileTime': lambda a, p, *_: wr(p, b'\0' * 8) or 1,
 'GetCPInfo': lambda cp, p, *_: wr(p, struct.pack('<I', 2) + b'?\0' + b'\0' * 12) or 1,
 'MultiByteToWideChar': api_MultiByteToWideChar, 'WideCharToMultiByte': api_WideCharToMultiByte,
 'GetModuleFileNameW': lambda h, buf, n, *_: (lambda s: (wr(buf, (s + '\0').encode('utf-16le')), len(s))[1])(args.appdir + '\\' + os.path.basename(args.exe)),
 'CreateDirectoryW': lambda p, *_: (os.makedirs(host_path(wstr(p)), exist_ok=True), 1)[1],
 'CreateProcessW': lambda exe, cmd, *_: (spawned.append((wstr(exe), wstr(cmd) if cmd else '')), print('    CreateProcess', spawned[-1]), 1)[2],
 'CloseHandle': lambda *_: 1, 'GetCurrentThread': lambda *_: 0x41, 'SetThreadPriority': lambda *_: 1,
 'DeleteFileW': lambda p, *_: (os.path.isfile(host_path(wstr(p))) and (os.remove(host_path(wstr(p))) or True)) and 1 or 0,
 'GetFileAttributesW': api_GetFileAttributesW, 'GetFileAttributesExW': api_GetFileAttributesExW,
 'FindFirstFileW': api_FindFirstFileW, 'FindNextFileW': api_FindNextFileW, 'FindClose': lambda h, *_: find_handles.pop(h, None) and 1 or 1,
 # --- user / gdi ---
 'FindWindowW': lambda *_: 0, 'RegisterClassW': lambda wc, *_: wndproc.__setitem__(0, r32(wc + 4)) or 0xC001,
 'CreateWindowExW': lambda *_: 0x1001, 'DefWindowProcW': lambda *_: 0, 'DestroyWindow': lambda *_: 1,
 'ShowWindow': lambda *_: 1, 'UpdateWindow': lambda *_: 1, 'MoveWindow': lambda *_: 1, 'SetForegroundWindow': lambda *_: 1,
 'InvalidateRect': lambda *_: 1, 'SetCapture': lambda *_: 0, 'ReleaseCapture': lambda *_: 1,
 'GetSystemMetrics': lambda i, *_: 320 if i in (0, 1) else 0,
 'PeekMessageW': api_PeekMessageW, 'DispatchMessageW': api_DispatchMessageW, 'MsgWaitForMultipleObjectsEx': api_MsgWait,
 'GetDC': lambda *_: 0x2001, 'ReleaseDC': lambda *_: 1, 'CreateCompatibleDC': lambda *_: 0x2002, 'DeleteDC': lambda *_: 1,
 'BeginPaint': lambda h, ps, *_: 0x2001, 'EndPaint': lambda *_: 1, 'GetStockObject': lambda *_: 0x4001, 'DeleteObject': lambda *_: 1,
 'CreateDIBSection': api_CreateDIBSection, 'SelectObject': api_SelectObject, 'BitBlt': api_BitBlt, 'StretchBlt': lambda *_: 1,
 'SetBkMode': lambda *_: 1, 'SetTextColor': lambda dc, c, *_: dc_state.setdefault(dc, {}).__setitem__('color', c) or 0,
 'DrawTextW': api_DrawTextW, 'CreateFontIndirectW': api_CreateFontIndirectW,
 'SHFullScreen': lambda *_: 1, 'ImmAssociateContext': lambda *_: 0, 'ImmGetVirtualKey': lambda *_: 0, 'SystemIdleTimerReset': lambda *_: 0,
 # --- audio ---
 'waveOutOpen': api_waveOutOpen, 'waveOutClose': lambda *_: 0, 'waveOutPrepareHeader': lambda *_: 0,
 'waveOutUnprepareHeader': lambda *_: 0, 'waveOutWrite': api_waveOutWrite, 'waveOutReset': api_waveOutReset,
}
missing = [n for n in imports if n not in API]
if missing: print('WARNING: unimplemented imports:', missing)

def dump_state(msg):
    pc = uc.reg_read(UC_ARM_REG_PC); lr = uc.reg_read(UC_ARM_REG_LR); sp = uc.reg_read(UC_ARM_REG_SP)
    print('\n*** %s' % msg)
    print('pc=%08x (%s) lr=%08x (%s) sp=%08x' % (pc, sym(pc), lr, sym(lr), sp))
    print(' '.join('r%d=%08x' % (i, reg(i)) for i in range(13)))
    try:
        for i in range(0, 96, 4):
            v = r32(sp + i)
            if base <= v < base + size: print('  [sp+%02x] %08x %s' % (i, v, sym(v)))
    except Exception: pass

def hook_trap(u, addr, sz, _):
    idx = (addr - TRAP) // 16
    if addr == EXIT: running[0] = False; u.emu_stop(); return
    name = imports[idx]; fn = API.get(name)
    stats['api'] += 1
    api_hist[name] = api_hist.get(name, 0) + 1
    if fn is None: dump_state('unimplemented API ' + name); running[0] = False; u.emu_stop(); return
    try:
        r = fn(reg(0), reg(1), reg(2), reg(3))
    except Exception as e:
        import traceback; traceback.print_exc(); dump_state('harness error in %s' % name); running[0] = False; u.emu_stop(); return
    if args.trace_api: print('  %s(%x,%x,%x,%x) -> %r   lr=%s' % (name, reg(0), reg(1), reg(2), reg(3), r, sym(u.reg_read(UC_ARM_REG_LR))))
    u.reg_write(UC_ARM_REG_R0, (r or 0) & 0xffffffff)
    if not running[0] or clock > args.max_ms: running[0] = False; u.emu_stop()
uc.hook_add(UC_HOOK_CODE, hook_trap, begin=TRAP, end=TRAP + 0xffff)

def hook_bad(u, access, addr, sz, val, _):
    dump_state('invalid memory access type %d at %08x size %d' % (access, addr, sz)); return False
uc.hook_add(UC_HOOK_MEM_UNMAPPED, hook_bad)
def hook_intr(u, no, _):
    dump_state('CPU exception %d (undefined instruction / abort)' % no); running[0] = False; u.emu_stop()
uc.hook_add(UC_HOOK_INTR, hook_intr)

prof = {}
if args.count or args.mips or args.max_insn or args.profile:
    per_ms = args.mips * 1000.0
    def hook_block(u, addr, sz, _):
        global insn_total, clock
        n = sz >> 2; insn_total += n
        if args.profile: prof[addr] = prof.get(addr, 0) + n
        if args.max_insn and insn_total > args.max_insn * 1e6: dump_state('instruction limit'); running[0] = False; u.emu_stop()
        if per_ms: clock += n / per_ms
    uc.hook_add(UC_HOOK_BLOCK, hook_block)

unaligned = {}
if args.align:
    def hook_mem(u, access, addr, sz, val, _):
        if sz > 1 and (addr & (sz - 1)):
            pc = u.reg_read(UC_ARM_REG_PC)
            k = (sym(pc).split('+')[0] if base <= pc < base + size else 'JIT/other', sz, 'W' if access == UC_MEM_WRITE else 'R')
            unaligned[k] = unaligned.get(k, 0) + 1
    uc.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, hook_mem)

cmdline = malloc(1024); wr(cmdline, (args.cmdline + '\0').encode('utf-16le'))
uc.reg_write(UC_ARM_REG_SP, STACK_TOP - 64)
uc.reg_write(UC_ARM_REG_LR, EXIT)
uc.reg_write(UC_ARM_REG_R0, base); uc.reg_write(UC_ARM_REG_R1, 0); uc.reg_write(UC_ARM_REG_R2, cmdline); uc.reg_write(UC_ARM_REG_R3, 5)
entry = base + pe.OPTIONAL_HEADER.AddressOfEntryPoint
try:
    uc.emu_start(entry, 0)
except UcError as e:
    dump_state('emulation stopped: %s' % e)
save_shot('final')
for cf in list(files.values()): cf.f.close()
if wav_data:
    w = wave.open(os.path.join(args.out, 'audio.wav'), 'wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(wave_rate); w.writeframes(bytes(wav_data)); w.close()
if args.align: print('UNALIGNED ACCESSES:', sorted(unaligned.items(), key=lambda kv: -kv[1]) or 'none')
if args.profile:
    agg = {}
    for a, n in prof.items():
        k = sym(a).split('+')[0] if base <= a < base + size else ('JIT' if a >= 0x42000000 or a >= base + size else 'other')
        agg[k] = agg.get(k, 0) + n
    tot = sum(agg.values()) or 1
    print('PROFILE:', ', '.join('%s %.1f%%' % (k, v * 100.0 / tot) for k, v in sorted(agg.items(), key=lambda kv: -kv[1])[:22]))
print('top api:', sorted(api_hist.items(), key=lambda kv: -kv[1])[:8])
print('big mallocs:', ', '.join('%dK@%s' % (n // 1024, w) for n, w in big_log))
print('audio: %d underruns, %.0f ms of silence gaps, %.1f s written' % (stats.get('underrun', 0), stats.get('gap_ms', 0), len(wav_data) / 4 / max(wave_rate, 1)))
print('done: clock=%d ms, api calls=%d, blits=%d, icache flushes=%d, wave buffers=%d, big-alloc peak=%.1f MB, heap=%.1f MB, insns=%.0f M'
      % (clock, stats['api'], stats['blit'], stats['flush'], stats['wavewrite'], va_peak / 1048576, (heap_top - HEAP) / 1048576, insn_total / 1e6))
if spawned:
    exe, cmd = spawned[-1]
    nxt = host_path(exe)
    print('>>> following into', exe, cmd, flush=True)
    av = [sys.executable, sys.argv[0], nxt, '--root', args.root, '--script', args.script, '--out', args.out, '--cmdline', cmd,
          '--clock', str(clock + 300), '--appdir', args.appdir, '--free-mb', str(args.free_mb), '--max-ms', str(args.max_ms)]
    if args.count: av.append('--count')
    if args.align: av.append('--align')
    if args.mips: av += ['--mips', str(args.mips)]
    os.execv(sys.executable, av)
