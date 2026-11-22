// D2M480 변환기 - 사운드: WAV(PCM) → 모노 저샘플레이트 IMA ADPCM (ADP1 형식)
// ADP1: 'ADP1' u32 rate, u32 samples, u16 blockSamples(=1024), u16 0
//       블록마다: i16 첫 샘플, u8 스텝 번호, u8 0, 이어서 (blockSamples-1)개 샘플의 4비트 코드(낮은 니블 먼저)
(function (root) {
  'use strict';
  var D2C = root.D2C = root.D2C || {};

  function parseWav(d) {
    if (d.length < 12 || d[0] !== 0x52 || d[1] !== 0x49 || d[8] !== 0x57) throw new Error('WAV 형식이 아님');
    var dv = new DataView(d.buffer, d.byteOffset, d.length), p = 12, fmt = null, data = null;
    while (p + 8 <= d.length) {
      var id = String.fromCharCode(d[p], d[p + 1], d[p + 2], d[p + 3]), sz = dv.getUint32(p + 4, true);
      if (id === 'fmt ') fmt = { tag: dv.getUint16(p + 8, true), ch: dv.getUint16(p + 10, true), rate: dv.getUint32(p + 12, true), bits: dv.getUint16(p + 22, true) };
      else if (id === 'data') { data = d.subarray(p + 8, Math.min(d.length, p + 8 + sz)); }
      p += 8 + sz + (sz & 1);
    }
    if (!fmt || !data) throw new Error('WAV 정보 없음');
    if (fmt.tag !== 1) throw new Error('PCM이 아닌 WAV(' + fmt.tag + ')');
    // 모노 float(-1..1)로
    var ch = fmt.ch, n, out, i, c;
    if (fmt.bits === 16) {
      n = Math.floor(data.length / (2 * ch)); out = new Float32Array(n);
      var ddv = new DataView(data.buffer, data.byteOffset, n * 2 * ch);
      for (i = 0; i < n; i++) { var s = 0; for (c = 0; c < ch; c++) s += ddv.getInt16((i * ch + c) * 2, true); out[i] = s / (32768 * ch); }
    } else if (fmt.bits === 8) {
      n = Math.floor(data.length / ch); out = new Float32Array(n);
      for (i = 0; i < n; i++) { var s8 = 0; for (c = 0; c < ch; c++) s8 += data[i * ch + c] - 128; out[i] = s8 / (128 * ch); }
    } else throw new Error(fmt.bits + '비트 WAV 미지원');
    return { rate: fmt.rate, samples: out };
  }

  // 정수배 축소는 구간 평균(저역통과 효과), 그 외는 선형 보간
  function resample(x, from, to) {
    if (from === to) return x;
    var ratio = from / to, n = Math.floor(x.length / ratio), out = new Float32Array(n), i;
    if (ratio > 1) {
      for (i = 0; i < n; i++) {
        var a = i * ratio, b = a + ratio, s = 0, cnt = 0;
        for (var k = Math.floor(a); k < Math.min(x.length, Math.ceil(b)); k++) { s += x[k]; cnt++; }
        out[i] = cnt ? s / cnt : 0;
      }
    } else {
      for (i = 0; i < n; i++) { var t = i * ratio, k0 = Math.floor(t), f = t - k0; out[i] = x[k0] * (1 - f) + (x[Math.min(x.length - 1, k0 + 1)] || 0) * f; }
    }
    return out;
  }

  var STEP = [7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,
    157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,
    1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,
    10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767];
  var IDX = [-1, -1, -1, -1, 2, 4, 6, 8];
  var BS = 1024;

  function encodeADP(samples, rate, gain) {
    gain = gain || 1;
    var n = samples.length, nb = Math.ceil(n / BS) || 1, blockBytes = 4 + (BS >> 1);
    var out = new Uint8Array(16 + nb * blockBytes), dv = new DataView(out.buffer);
    out[0] = 0x41; out[1] = 0x44; out[2] = 0x50; out[3] = 0x31;
    dv.setUint32(4, rate, true); dv.setUint32(8, n, true); dv.setUint16(12, BS, true);
    var idx = 0;
    function s16(i) { var v = i < n ? Math.round(samples[i] * 32767 * gain) : 0; return v > 32767 ? 32767 : v < -32768 ? -32768 : v; }
    for (var b = 0; b < nb; b++) {
      var base = 16 + b * blockBytes, i0 = b * BS, pred = s16(i0);
      dv.setInt16(base, pred, true); out[base + 2] = idx;
      var nib = 0, half = 0, op = base + 4;
      for (var i = 1; i < BS; i++) {
        var smp = s16(i0 + i), step = STEP[idx], diff = smp - pred, code = 0;
        if (diff < 0) { code = 8; diff = -diff; }
        var dq = step >> 3;
        if (diff >= step) { code |= 4; diff -= step; dq += step; }
        step >>= 1; if (diff >= step) { code |= 2; diff -= step; dq += step; }
        step >>= 1; if (diff >= step) { code |= 1; dq += step; }
        pred += (code & 8) ? -dq : dq;
        if (pred > 32767) pred = 32767; else if (pred < -32768) pred = -32768;
        idx += IDX[code & 7]; if (idx < 0) idx = 0; else if (idx > 88) idx = 88;
        if (!half) { nib = code; half = 1; } else { out[op++] = nib | (code << 4); half = 0; }
      }
      if (half) out[op++] = nib;
    }
    return out;
  }
  // 검증용 디코더 (엔진과 같은 방식)
  function decodeADP(d) {
    var dv = new DataView(d.buffer, d.byteOffset, d.length), n = dv.getUint32(8, true), bs = dv.getUint16(12, true);
    var out = new Int16Array(n), blockBytes = 4 + (bs >> 1), nb = Math.ceil(n / bs);
    for (var b = 0; b < nb; b++) {
      var base = 16 + b * blockBytes, pred = dv.getInt16(base, true), idx = d[base + 2], o = b * bs;
      if (o < n) out[o] = pred;
      for (var i = 1; i < bs && o + i < n; i++) {
        var byte = d[base + 4 + ((i - 1) >> 1)], code = ((i - 1) & 1) ? byte >> 4 : byte & 15, step = STEP[idx];
        var dq = step >> 3; if (code & 4) dq += step; if (code & 2) dq += step >> 1; if (code & 1) dq += step >> 2;
        pred += (code & 8) ? -dq : dq; if (pred > 32767) pred = 32767; else if (pred < -32768) pred = -32768;
        idx += IDX[code & 7]; if (idx < 0) idx = 0; else if (idx > 88) idx = 88;
        out[o + i] = pred;
      }
    }
    return out;
  }

  // 원본 WAV 바이트 → ADP1 바이트
  function convertWav(bytes, targetRate) {
    var w = parseWav(bytes);
    var rate = Math.min(targetRate, w.rate);
    var x = resample(w.samples, w.rate, rate);
    // 너무 큰 소리는 이어폰에서 깨지므로 최대치를 0.9로 제한
    var peak = 0; for (var i = 0; i < x.length; i++) { var a = Math.abs(x[i]); if (a > peak) peak = a; }
    var gain = peak > 0.9 ? 0.9 / peak : 1;
    return encodeADP(x, rate, gain);
  }

  D2C.parseWav = parseWav;
  D2C.resample = resample;
  D2C.encodeADP = encodeADP;
  D2C.decodeADP = decodeADP;
  D2C.convertWav = convertWav;
})(typeof window !== 'undefined' ? window : global);
