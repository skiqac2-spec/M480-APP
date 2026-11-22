// D2M480 변환기 - 그래픽 포맷 (팔레트, DC6, DCC, DT1) 해독 + 1/2 축소 + SPR1 인코딩
// DCC 해독 순서는 OpenDiablo2(GPL-3.0)의 구현을 참고함
(function (root) {
  'use strict';
  var D2C = root.D2C = root.D2C || {};

  function u16(d, o) { return d[o] | (d[o + 1] << 8); }
  function i16(d, o) { var v = u16(d, o); return v & 0x8000 ? v - 0x10000 : v; }
  function u32(d, o) { return (d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) | (d[o + 3] << 24)) >>> 0; }
  function i32(d, o) { return d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) | (d[o + 3] << 24); }

  // ------------------------------------------------------------ 팔레트 (pal.dat: 256 x BGR)
  function Palette(dat) {
    this.r = new Uint8Array(256); this.g = new Uint8Array(256); this.b = new Uint8Array(256);
    for (var i = 0; i < 256; i++) { this.b[i] = dat[i * 3]; this.g[i] = dat[i * 3 + 1]; this.r[i] = dat[i * 3 + 2]; }
    this.lut = null;
  }
  // RGB(각 5비트) → 가장 가까운 팔레트 번호 (0번=투명은 제외)
  Palette.prototype.nearest = function (r, g, b) {
    if (!this.lut) {
      this.lut = new Uint8Array(32768);
      this.lutDone = new Uint8Array(32768);
    }
    var k = ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3);
    if (this.lutDone[k]) return this.lut[k];
    var rr = (r & 0xF8) | 4, gg = (g & 0xF8) | 4, bb = (b & 0xF8) | 4;
    var best = 1, bd = 1e9;
    for (var i = 1; i < 256; i++) {
      var dr = this.r[i] - rr, dg = this.g[i] - gg, db = this.b[i] - bb;
      var d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
      if (d < bd) { bd = d; best = i; }
    }
    this.lut[k] = best; this.lutDone[k] = 1;
    return best;
  };

  // ------------------------------------------------------------ 프레임 = {left, top, w, h, px(Uint8Array, 0=투명)}
  function emptyFrame() { return { left: 0, top: 0, w: 0, h: 0, px: new Uint8Array(0) }; }

  // ------------------------------------------------------------ DC6
  function decodeDC6(d) {
    var ver = i32(d, 0);
    if (ver !== 6) throw new Error('DC6 버전 오류');
    var dirs = u32(d, 16), fpd = u32(d, 20), n = dirs * fpd;
    var frames = [];
    for (var i = 0; i < n; i++) {
      var p = u32(d, 24 + i * 4);
      var flip = u32(d, p), w = u32(d, p + 4), h = u32(d, p + 8);
      var ox = i32(d, p + 12), oy = i32(d, p + 16), len = u32(d, p + 28);
      var data = p + 32;
      if (w === 0 || h === 0 || w > 4096 || h > 4096) { frames.push(emptyFrame()); continue; }
      var px = new Uint8Array(w * h), x = 0, y = flip ? 0 : h - 1, k = 0;
      while (k < len) {
        var b = d[data + k++];
        if (b === 0x80) { x = 0; y += flip ? 1 : -1; }
        else if (b & 0x80) x += b & 0x7F;
        else {
          for (var j = 0; j < b; j++) {
            var c = d[data + k++];
            if (x < w && y >= 0 && y < h) px[y * w + x] = c;
            x++;
          }
        }
      }
      frames.push({ left: ox, top: oy - h, w: w, h: h, px: px });
    }
    return { dirs: dirs, fpd: fpd, frames: frames };
  }

  // ------------------------------------------------------------ DCC
  function Bits(d, bitOff) { this.d = d; this.o = bitOff; this.start = bitOff; }
  Bits.prototype.bit = function () {
    var v = (this.d[this.o >>> 3] >>> (this.o & 7)) & 1; this.o++; return v;
  };
  Bits.prototype.get = function (n) {
    var r = 0;
    for (var i = 0; i < n; i++) r |= this.bit() << i;
    return r >>> 0;
  };
  Bits.prototype.signed = function (n) {
    if (n === 0) return 0;
    var v = this.get(n);
    if (n === 1) return -v;
    if (v & (1 << (n - 1))) return v - Math.pow(2, n);
    return v;
  };
  Bits.prototype.copy = function () { return new Bits(this.d, this.o); };
  Bits.prototype.read = function () { return this.o - this.start; };

  var CRAZY = [0, 1, 2, 4, 6, 8, 10, 12, 14, 16, 20, 24, 26, 28, 30, 32];
  var PMASK = [0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4];

  function decodeDCC(d) {
    if (d[0] !== 0x74) throw new Error('DCC 서명 오류');
    var ndirs = d[2], fpd = i32(d, 3);
    var dirOff = [];
    for (var i = 0; i < ndirs; i++) dirOff.push(u32(d, 15 + i * 4));
    var frames = [];
    for (i = 0; i < ndirs; i++) {
      var fr = decodeDCCDir(d, dirOff[i] * 8, fpd);
      for (var f = 0; f < fpd; f++) frames.push(fr[f]);
    }
    return { dirs: ndirs, fpd: fpd, frames: frames };
  }

  function decodeDCCDir(d, bitOff, fpd) {
    var bm = new Bits(d, bitOff);
    bm.get(32); // OutSizeCoded
    var cflags = bm.get(2);
    var v0b = CRAZY[bm.get(4)], wb = CRAZY[bm.get(4)], hb = CRAZY[bm.get(4)], xb = CRAZY[bm.get(4)],
        yb = CRAZY[bm.get(4)], optb = CRAZY[bm.get(4)], cbb = CRAZY[bm.get(4)];
    var frs = [], minx = 1e9, miny = 1e9, maxx = -1e9, maxy = -1e9, f;
    for (f = 0; f < fpd; f++) {
      bm.get(v0b);
      var fw = bm.get(wb), fh = bm.get(hb), fx = bm.signed(xb), fy = bm.signed(yb);
      var nopt = bm.get(optb); bm.get(cbb);
      var bottomUp = bm.bit();
      if (bottomUp) throw new Error('DCC: 아래→위 프레임은 지원 안 함');
      var fr = { w: fw, h: fh, left: fx, top: fy - fh + 1, nopt: nopt };
      frs.push(fr);
      minx = Math.min(minx, fr.left); miny = Math.min(miny, fr.top);
      maxx = Math.max(maxx, fr.left + fr.w); maxy = Math.max(maxy, fr.top + fr.h);
    }
    // 선택 데이터 건너뛰기 (원작 파일에는 거의 없음)
    for (f = 0; f < fpd; f++) if (frs[f].nopt) throw new Error('DCC: 선택 데이터 미지원');
    var eqSize = 0, pmSize = 0, etSize = 0, rpSize = 0;
    if (cflags & 2) eqSize = bm.get(20);
    pmSize = bm.get(20);
    if (cflags & 1) { etSize = bm.get(20); rpSize = bm.get(20); }
    var palEnt = new Uint8Array(256), pc = 0;
    for (i = 0; i < 256; i++) if (bm.bit()) palEnt[pc++] = i;
    var ec = bm.copy(); bm.o += eqSize;
    var pm = bm.copy(); bm.o += pmSize;
    var et = bm.copy(); bm.o += etSize;
    var rp = bm.copy(); bm.o += rpSize;
    var pcd = bm.copy();

    var bw = maxx - minx, bh = maxy - miny;
    if (bw <= 0 || bh <= 0) { var e = []; for (f = 0; f < fpd; f++) e.push(emptyFrame()); return e; }
    // 방향 전체 셀
    var hcc = 1 + ((bw - 1) >> 2), vcc = 1 + ((bh - 1) >> 2), i, x, y;
    var cells = [];
    for (y = 0; y < vcc; y++) for (x = 0; x < hcc; x++) {
      cells.push({ w: (x === hcc - 1) ? bw - 4 * (hcc - 1) : 4, h: (y === vcc - 1) ? bh - 4 * (vcc - 1) : 4,
                   x: x * 4, y: y * 4, lw: -1, lh: -1, lx: 0, ly: 0 });
    }
    if (hcc === 1) cells.forEach(function (c) { c.w = bw; });
    if (vcc === 1) cells.forEach(function (c) { c.h = bh; });
    // 프레임별 셀
    for (f = 0; f < fpd; f++) {
      var F = frs[f];
      var w0 = 4 - ((F.left - minx) % 4), hc, vc, t;
      if (F.w - w0 <= 1) hc = 1; else { t = F.w - w0 - 1; hc = 2 + ((t / 4) | 0); if (t % 4 === 0) hc--; }
      var h0 = 4 - ((F.top - miny) % 4);
      if (F.h - h0 <= 1) vc = 1; else { t = F.h - h0 - 1; vc = 2 + ((t / 4) | 0); if (t % 4 === 0) vc--; }
      var cw = [], ch = [];
      if (hc === 1) cw[0] = F.w; else { cw[0] = w0; for (i = 1; i < hc - 1; i++) cw[i] = 4; cw[hc - 1] = F.w - w0 - 4 * (hc - 2); }
      if (vc === 1) ch[0] = F.h; else { ch[0] = h0; for (i = 1; i < vc - 1; i++) ch[i] = 4; ch[vc - 1] = F.h - h0 - 4 * (vc - 2); }
      F.hc = hc; F.vc = vc; F.cells = [];
      var oy = F.top - miny;
      for (y = 0; y < vc; y++) {
        var ox = F.left - minx;
        for (x = 0; x < hc; x++) { F.cells.push({ x: ox, y: oy, w: cw[x], h: ch[y] }); ox += cw[x]; }
        oy += ch[y];
      }
    }
    // 픽셀 버퍼 채우기
    var pbuf = [];   // {v:[4], frame, idx}
    var cellBuf = new Array(hcc * vcc);
    var pixelMask = 0;
    for (f = 0; f < fpd; f++) {
      F = frs[f];
      var ocx = ((F.left - minx) / 4) | 0, ocy = ((F.top - miny) / 4) | 0;
      for (var cy = 0; cy < F.vc; cy++) {
        var ccy = cy + ocy;
        for (var cx = 0; cx < F.hc; cx++) {
          var cur = ocx + cx + ccy * hcc, next = false;
          if (cellBuf[cur]) {
            var tmp = eqSize > 0 ? ec.bit() : 0;
            if (tmp === 0) pixelMask = pm.get(4); else next = true;
          } else pixelMask = 0x0F;
          if (next) continue;
          var stack = [0, 0, 0, 0], last = 0, nbits = PMASK[pixelMask];
          var enc = (nbits !== 0 && etSize > 0) ? et.bit() : 0;
          var decoded = 0;
          for (i = 0; i < nbits; i++) {
            if (enc) stack[i] = rp.get(8);
            else {
              stack[i] = last;
              var disp = pcd.get(4); stack[i] += disp;
              while (disp === 15) { disp = pcd.get(4); stack[i] += disp; }
            }
            if (stack[i] === last) { stack[i] = 0; break; }
            else { last = stack[i]; decoded++; }
          }
          var old = cellBuf[cur], ent = { v: [0, 0, 0, 0], frame: f, idx: cx + cy * F.hc };
          var ci = decoded - 1;
          for (i = 0; i < 4; i++) {
            if (pixelMask & (1 << i)) { if (ci >= 0) { ent.v[i] = stack[ci]; ci--; } else ent.v[i] = 0; }
            else ent.v[i] = old.v[i];
          }
          cellBuf[cur] = ent;
          pbuf.push(ent);
        }
      }
    }
    for (i = 0; i < pbuf.length; i++) for (x = 0; x < 4; x++) pbuf[i].v[x] = palEnt[pbuf[i].v[x] & 0xFF];
    // 프레임 생성
    var work = new Uint8Array(bw * bh), out = [], pbi = 0;
    for (f = 0; f < fpd; f++) {
      F = frs[f];
      var fpx = new Uint8Array(bw * bh);
      for (var c = 0; c < F.cells.length; c++) {
        var cell = F.cells[c];
        var bc = cells[((cell.x >> 2)) + ((cell.y >> 2)) * hcc];
        var pbe = pbuf[pbi];
        var X, Y;
        if (!pbe || pbe.frame !== f || pbe.idx !== c) {
          if (cell.w !== bc.lw || cell.h !== bc.lh) {
            for (Y = 0; Y < cell.h; Y++) for (X = 0; X < cell.w; X++) work[X + cell.x + (Y + cell.y) * bw] = 0;
          } else {
            for (Y = 0; Y < cell.h; Y++) for (X = 0; X < cell.w; X++)
              work[X + cell.x + (Y + cell.y) * bw] = work[X + bc.lx + (Y + bc.ly) * bw];
            for (Y = 0; Y < cell.h; Y++) for (X = 0; X < cell.w; X++)
              fpx[X + cell.x + (Y + cell.y) * bw] = work[X + cell.x + (Y + cell.y) * bw];
          }
        } else {
          if (pbe.v[0] === pbe.v[1]) {
            for (Y = 0; Y < cell.h; Y++) for (X = 0; X < cell.w; X++) work[X + cell.x + (Y + cell.y) * bw] = pbe.v[0];
          } else {
            var nb = (pbe.v[1] !== pbe.v[2]) ? 2 : 1;
            for (Y = 0; Y < cell.h; Y++) for (X = 0; X < cell.w; X++) work[X + cell.x + (Y + cell.y) * bw] = pbe.v[pcd.get(nb)];
          }
          for (Y = 0; Y < cell.h; Y++) for (X = 0; X < cell.w; X++)
            fpx[X + cell.x + (Y + cell.y) * bw] = work[X + cell.x + (Y + cell.y) * bw];
          pbi++;
        }
        bc.lw = cell.w; bc.lh = cell.h; bc.lx = cell.x; bc.ly = cell.y;
      }
      out.push(trim({ left: minx, top: miny, w: bw, h: bh, px: fpx }));
    }
    if (ec.read() !== eqSize || pm.read() !== pmSize || et.read() !== etSize || rp.read() !== rpSize)
      throw new Error('DCC: 비트 수 불일치 (손상된 파일)');
    return out;
  }

  // 투명 테두리 잘라내기
  function trim(fr) {
    var w = fr.w, h = fr.h, px = fr.px, x0 = w, y0 = h, x1 = -1, y1 = -1, x, y;
    for (y = 0; y < h; y++) for (x = 0; x < w; x++) if (px[y * w + x]) {
      if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y;
    }
    if (x1 < 0) return emptyFrame();
    if (x0 === 0 && y0 === 0 && x1 === w - 1 && y1 === h - 1) return fr;
    var nw = x1 - x0 + 1, nh = y1 - y0 + 1, np = new Uint8Array(nw * nh);
    for (y = 0; y < nh; y++) np.set(px.subarray((y + y0) * w + x0, (y + y0) * w + x0 + nw), y * nw);
    return { left: fr.left + x0, top: fr.top + y0, w: nw, h: nh, px: np };
  }

  // ------------------------------------------------------------ 1/2 축소
  // 2x2 묶음에서 불투명 픽셀이 minOpaque개 이상이면 평균색 → 팔레트 근사
  // keepIndex: 평균 대신 가장 많이 나온 색 번호를 그대로 쓸지 (색 효과용 영역 보존)
  function half(fr, pal, minOpaque) {
    if (!fr.w) return fr;
    minOpaque = minOpaque || 2;
    var L = Math.floor(fr.left / 2), T = Math.floor(fr.top / 2);
    var R = Math.floor((fr.left + fr.w - 1) / 2), B = Math.floor((fr.top + fr.h - 1) / 2);
    var nw = R - L + 1, nh = B - T + 1, out = new Uint8Array(nw * nh);
    var w = fr.w, h = fr.h, px = fr.px;
    for (var oy = 0; oy < nh; oy++) {
      for (var ox = 0; ox < nw; ox++) {
        var sx0 = (L + ox) * 2 - fr.left, sy0 = (T + oy) * 2 - fr.top;
        var cnt = 0, r = 0, g = 0, b = 0, c0 = 0, same = true;
        for (var dy = 0; dy < 2; dy++) {
          var sy = sy0 + dy; if (sy < 0 || sy >= h) continue;
          for (var dx = 0; dx < 2; dx++) {
            var sx = sx0 + dx; if (sx < 0 || sx >= w) continue;
            var c = px[sy * w + sx];
            if (!c) continue;
            if (cnt && c !== c0) same = false;
            if (!cnt) c0 = c;
            cnt++; r += pal.r[c]; g += pal.g[c]; b += pal.b[c];
          }
        }
        if (cnt >= minOpaque || (cnt > 0 && minOpaque > cnt && isEdgeSingle(fr, sx0, sy0))) {
          out[oy * nw + ox] = same ? c0 : pal.nearest((r / cnt) | 0, (g / cnt) | 0, (b / cnt) | 0);
        }
      }
    }
    return trim({ left: L, top: T, w: nw, h: nh, px: out });
  }
  // 아주 가는 선(1픽셀)이 사라지지 않도록: 원본 프레임이 2픽셀 이하 폭/높이일 때만 살림
  function isEdgeSingle(fr) { return fr.w <= 2 || fr.h <= 2; }

  // ------------------------------------------------------------ SPR1 인코딩
  // 'SPR1' u16 dirs u16 fpd, [i16 left, i16 top, u16 w, u16 h, u32 off] * n, 데이터
  // 데이터(행 단위, 위→아래): 0x80=행 끝, 0x80|n=투명 n칸, n(1..127)=픽셀 n개
  function encodeRows(fr) {
    var out = [], w = fr.w, px = fr.px;
    for (var y = 0; y < fr.h; y++) {
      var x = 0, row = y * w;
      // 행 끝의 투명은 생략
      var end = w; while (end > 0 && !px[row + end - 1]) end--;
      while (x < end) {
        if (!px[row + x]) {
          var s = x; while (x < end && !px[row + x] && x - s < 127) x++;
          out.push(0x80 | (x - s));
        } else {
          var s2 = x; while (x < end && px[row + x] && x - s2 < 127) x++;
          out.push(x - s2);
          for (var k = s2; k < x; k++) out.push(px[row + k]);
        }
      }
      out.push(0x80);
    }
    return out;
  }
  function encodeSPR(spr) {
    var n = spr.frames.length, head = 8 + n * 12, chunks = [], total = 0, offs = [];
    for (var i = 0; i < n; i++) {
      var rows = spr.frames[i].w ? encodeRows(spr.frames[i]) : [];
      offs.push(total); chunks.push(rows); total += rows.length;
    }
    var out = new Uint8Array(head + total), dv = new DataView(out.buffer);
    out[0] = 0x53; out[1] = 0x50; out[2] = 0x52; out[3] = 0x31;
    dv.setUint16(4, spr.dirs, true); dv.setUint16(6, spr.fpd, true);
    var p = head;
    for (i = 0; i < n; i++) {
      var f = spr.frames[i], e = 8 + i * 12;
      dv.setInt16(e, f.left, true); dv.setInt16(e + 2, f.top, true);
      dv.setUint16(e + 4, f.w, true); dv.setUint16(e + 6, f.h, true); dv.setUint32(e + 8, offs[i], true);
      out.set(chunks[i], p); p += chunks[i].length;
    }
    return out;
  }
  function decodeSPR(d) {   // 검증용
    var dirs = u16(d, 4), fpd = u16(d, 6), n = dirs * fpd, head = 8 + n * 12, frames = [];
    for (var i = 0; i < n; i++) {
      var e = 8 + i * 12, w = u16(d, e + 4), h = u16(d, e + 6), p = head + u32(d, e + 8);
      var px = new Uint8Array(w * h), x = 0, y = 0;
      while (y < h) {
        var b = d[p++];
        if (b === 0x80) { x = 0; y++; }
        else if (b & 0x80) x += b & 0x7F;
        else { for (var k = 0; k < b; k++) px[y * w + x++] = d[p++]; }
      }
      frames.push({ left: i16(d, e), top: i16(d, e + 2), w: w, h: h, px: px });
    }
    return { dirs: dirs, fpd: fpd, frames: frames };
  }

  // 원본(DC6/DCC 바이트) → 축소된 SPR1 바이트
  function convertSprite(bytes, isDCC, pal, opts) {
    var spr = isDCC ? decodeDCC(bytes) : decodeDC6(bytes);
    var scale = !(opts && opts.noScale);
    if (scale) spr.frames = spr.frames.map(function (f) { return half(trim(f), pal, opts && opts.minOpaque); });
    else spr.frames = spr.frames.map(trim);
    return encodeSPR(spr);
  }

  // ------------------------------------------------------------ DT1 (바닥/벽 타일)
  var XJ = [14, 12, 10, 8, 6, 4, 2, 0, 2, 4, 6, 8, 10, 12, 14];
  var NB = [4, 8, 12, 16, 20, 24, 28, 32, 28, 24, 20, 16, 12, 8, 4];
  function decodeDT1(d) {
    if (i32(d, 0) !== 7 || i32(d, 4) !== 6) throw new Error('DT1 버전 오류');
    var nt = i32(d, 268), body = i32(d, 272), tiles = [];
    for (var t = 0; t < nt; t++) {
      var p = body + t * 96;
      var T = {
        dir: i32(d, p), roof: i16(d, p + 4), mat: u16(d, p + 6), h: i32(d, p + 8), w: i32(d, p + 12),
        type: i32(d, p + 20), style: i32(d, p + 24), seq: i32(d, p + 28), rarity: i32(d, p + 32),
        sub: d.slice(p + 40, p + 65), bptr: i32(d, p + 72), nb: i32(d, p + 80)
      };
      tiles.push(T);
    }
    tiles.forEach(function (T) {
      var blocks = [], miny = 0, maxy = 0, i;
      for (i = 0; i < T.nb; i++) {
        var q = T.bptr + i * 20;
        var B = { x: i16(d, q), y: i16(d, q + 2), fmt: i16(d, q + 8), len: i32(d, q + 10), off: i32(d, q + 16) };
        blocks.push(B);
        miny = Math.min(miny, B.y); maxy = Math.max(maxy, B.y + 32);
      }
      var w = Math.max(T.w, 160), h = Math.max(1, maxy - miny);
      var px = new Uint8Array(w * h);
      blocks.forEach(function (B) {
        var data = T.bptr + B.off, k = 0, x, y = 0;
        if (B.fmt === 1) {
          var len = 256;
          while (len > 0 && y < 15) {
            x = XJ[y]; var n = NB[y]; len -= n;
            while (n-- > 0) { put(B.x + x, B.y + y - miny, d[data + k++]); x++; }
            y++;
          }
        } else {
          var L = B.len; x = 0;
          while (L > 0) {
            var b1 = d[data + k], b2 = d[data + k + 1]; k += 2; L -= 2;
            if ((b1 | b2) === 0) { x = 0; y++; continue; }
            x += b1; L -= b2;
            while (b2-- > 0) { put(B.x + x, B.y + y - miny, d[data + k++]); x++; }
          }
        }
      });
      function put(x, y, c) { if (x >= 0 && x < w && y >= 0 && y < h) px[y * w + x] = c; }
      T.img = { left: 0, top: miny, w: w, h: h, px: px };
      delete T.bptr;
    });
    return tiles;
  }
  // TIL1: 'TIL1' u32 n, 타일마다: i32 dir, i16 roof, u16 mat, i32 type, style, seq, rarity, u8 sub[25], u8 pad[3], u32 sprOff
  //        이어서 SPR1(dirs=1, fpd=n) 하나에 모든 타일 이미지
  function convertDT1(bytes, pal, opts) {
    var tiles = decodeDT1(bytes), n = tiles.length;
    var scale = !(opts && opts.noScale);
    var spr = { dirs: 1, fpd: n, frames: tiles.map(function (T) {
      var f = trim(T.img); return scale ? half(f, pal, 1) : f; }) };
    var sb = encodeSPR(spr), head = 8 + n * 48;
    var out = new Uint8Array(head + sb.length), dv = new DataView(out.buffer);
    out[0] = 0x54; out[1] = 0x49; out[2] = 0x4C; out[3] = 0x31; dv.setUint32(4, n, true);
    tiles.forEach(function (T, i) {
      var e = 8 + i * 48;
      dv.setInt32(e, T.dir, true); dv.setInt16(e + 4, scale ? (T.roof >> 1) : T.roof, true); dv.setUint16(e + 6, T.mat, true);
      dv.setInt32(e + 8, T.type, true); dv.setInt32(e + 12, T.style, true); dv.setInt32(e + 16, T.seq, true);
      dv.setInt32(e + 20, T.rarity, true); out.set(T.sub, e + 24);
    });
    out.set(sb, head);
    return out;
  }

  D2C.Palette = Palette;
  D2C.decodeDC6 = decodeDC6;
  D2C.decodeDCC = decodeDCC;
  D2C.decodeDT1 = decodeDT1;
  D2C.half = half;
  D2C.trim = trim;
  D2C.encodeSPR = encodeSPR;
  D2C.decodeSPR = decodeSPR;
  D2C.convertSprite = convertSprite;
  D2C.convertDT1 = convertDT1;
})(typeof window !== 'undefined' ? window : global);
