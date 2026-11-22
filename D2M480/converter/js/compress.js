// D2M480 변환기 - MPQ 압축 해제 모듈 (PKWARE explode, 적응형 허프만, IMA ADPCM, zlib inflate)
// StormLib(Ladislav Zezula)의 알고리즘을 JavaScript로 옮김
(function (root) {
  'use strict';
  var D2C = root.D2C = root.D2C || {};

  // ---------------------------------------------------------------- PKWARE DCL explode
  var DistBits = [
    2,4,4,5,5,5,5,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,7,7,7,7,7,7,7,7,7,7,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8];
  var DistCode = [
    0x03,0x0D,0x05,0x19,0x09,0x11,0x01,0x3E,0x1E,0x2E,0x0E,0x36,0x16,0x26,0x06,0x3A,
    0x1A,0x2A,0x0A,0x32,0x12,0x22,0x42,0x02,0x7C,0x3C,0x5C,0x1C,0x6C,0x2C,0x4C,0x0C,
    0x74,0x34,0x54,0x14,0x64,0x24,0x44,0x04,0x78,0x38,0x58,0x18,0x68,0x28,0x48,0x08,
    0xF0,0x70,0xB0,0x30,0xD0,0x50,0x90,0x10,0xE0,0x60,0xA0,0x20,0xC0,0x40,0x80,0x00];
  var ExLenBits = [0,0,0,0,0,0,0,0,1,2,3,4,5,6,7,8];
  var LenBase = [0,1,2,3,4,5,6,7,8,0x0A,0x0E,0x16,0x26,0x46,0x86,0x106];
  var LenBits = [3,2,3,3,4,4,4,5,5,5,5,6,6,6,7,7];
  var LenCode = [5,3,1,6,0x0A,2,0x0C,0x14,4,0x18,8,0x30,0x10,0x20,0x40,0];
  var ChBitsAsc = [
    0x0B,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x08,0x07,0x0C,0x0C,0x07,0x0C,0x0C,
    0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0D,0x0C,0x0C,0x0C,0x0C,0x0C,
    0x04,0x0A,0x08,0x0C,0x0A,0x0C,0x0A,0x08,0x07,0x07,0x08,0x09,0x07,0x06,0x07,0x08,
    0x07,0x06,0x07,0x07,0x07,0x07,0x08,0x07,0x07,0x08,0x08,0x0C,0x0B,0x07,0x09,0x0B,
    0x0C,0x06,0x07,0x06,0x06,0x05,0x07,0x08,0x08,0x06,0x0B,0x09,0x06,0x07,0x06,0x06,
    0x07,0x0B,0x06,0x06,0x06,0x07,0x09,0x08,0x09,0x09,0x0B,0x08,0x0B,0x09,0x0C,0x08,
    0x0C,0x05,0x06,0x06,0x06,0x05,0x06,0x06,0x06,0x05,0x0B,0x07,0x05,0x06,0x05,0x05,
    0x06,0x0A,0x05,0x05,0x05,0x05,0x08,0x07,0x08,0x08,0x0A,0x0B,0x0B,0x0C,0x0C,0x0C,
    0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,
    0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,
    0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,
    0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,
    0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,
    0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,
    0x0D,0x0C,0x0D,0x0D,0x0D,0x0C,0x0D,0x0D,0x0D,0x0C,0x0D,0x0D,0x0D,0x0D,0x0C,0x0D,
    0x0D,0x0D,0x0C,0x0C,0x0C,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D];
  var ChCodeAsc = [
    0x0490,0x0FE0,0x07E0,0x0BE0,0x03E0,0x0DE0,0x05E0,0x09E0,0x01E0,0x00B8,0x0062,0x0EE0,0x06E0,0x0022,0x0AE0,0x02E0,
    0x0CE0,0x04E0,0x08E0,0x00E0,0x0F60,0x0760,0x0B60,0x0360,0x0D60,0x0560,0x1240,0x0960,0x0160,0x0E60,0x0660,0x0A60,
    0x000F,0x0250,0x0038,0x0260,0x0050,0x0C60,0x0390,0x00D8,0x0042,0x0002,0x0058,0x01B0,0x007C,0x0029,0x003C,0x0098,
    0x005C,0x0009,0x001C,0x006C,0x002C,0x004C,0x0018,0x000C,0x0074,0x00E8,0x0068,0x0460,0x0090,0x0034,0x00B0,0x0710,
    0x0860,0x0031,0x0054,0x0011,0x0021,0x0017,0x0014,0x00A8,0x0028,0x0001,0x0310,0x0130,0x003E,0x0064,0x001E,0x002E,
    0x0024,0x0510,0x000E,0x0036,0x0016,0x0044,0x0030,0x00C8,0x01D0,0x00D0,0x0110,0x0048,0x0610,0x0150,0x0060,0x0088,
    0x0FA0,0x0007,0x0026,0x0006,0x003A,0x001B,0x001A,0x002A,0x000A,0x000B,0x0210,0x0004,0x0013,0x0032,0x0003,0x001D,
    0x0012,0x0190,0x000D,0x0015,0x0005,0x0019,0x0008,0x0078,0x00F0,0x0070,0x0290,0x0410,0x0010,0x07A0,0x0BA0,0x03A0,
    0x0240,0x1C40,0x0C40,0x1440,0x0440,0x1840,0x0840,0x1040,0x0040,0x1F80,0x0F80,0x1780,0x0780,0x1B80,0x0B80,0x1380,
    0x0380,0x1D80,0x0D80,0x1580,0x0580,0x1980,0x0980,0x1180,0x0180,0x1E80,0x0E80,0x1680,0x0680,0x1A80,0x0A80,0x1280,
    0x0280,0x1C80,0x0C80,0x1480,0x0480,0x1880,0x0880,0x1080,0x0080,0x1F00,0x0F00,0x1700,0x0700,0x1B00,0x0B00,0x1300,
    0x0DA0,0x05A0,0x09A0,0x01A0,0x0EA0,0x06A0,0x0AA0,0x02A0,0x0CA0,0x04A0,0x08A0,0x00A0,0x0F20,0x0720,0x0B20,0x0320,
    0x0D20,0x0520,0x0920,0x0120,0x0E20,0x0620,0x0A20,0x0220,0x0C20,0x0420,0x0820,0x0020,0x0FC0,0x07C0,0x0BC0,0x03C0,
    0x0DC0,0x05C0,0x09C0,0x01C0,0x0EC0,0x06C0,0x0AC0,0x02C0,0x0CC0,0x04C0,0x08C0,0x00C0,0x0F40,0x0740,0x0B40,0x0340,
    0x0300,0x0D40,0x1D00,0x0D00,0x1500,0x0540,0x0500,0x1900,0x0900,0x0940,0x1100,0x0100,0x1E00,0x0E00,0x0140,0x1600,
    0x0600,0x1A00,0x0E40,0x0640,0x0A40,0x0A00,0x1200,0x0200,0x1C00,0x0C00,0x1400,0x0400,0x1800,0x0800,0x1000,0x0000];

  function genDecodeTabs(startIdx, lenBits) {
    var pos = new Uint8Array(256);
    for (var i = 0; i < startIdx.length; i++) {
      var len = 1 << lenBits[i];
      for (var idx = startIdx[i]; idx < 256; idx += len) pos[idx] = i;
    }
    return pos;
  }
  var LengthCodes = genDecodeTabs(LenCode, LenBits);
  var DistPosCodes = genDecodeTabs(DistCode, DistBits);
  var ascTabs = null;
  function genAscTabs() {
    var t = { o2C34: new Uint8Array(256), o2D34: new Uint8Array(256), o2E34: new Uint8Array(128),
              o2EB4: new Uint8Array(256), bits: ChBitsAsc.slice() };
    for (var count = 255; count >= 0; count--) {
      var bits = t.bits[count], code = ChCodeAsc[count], acc, add;
      if (bits <= 8) {
        add = 1 << bits; acc = code;
        do { t.o2C34[acc] = count; acc += add; } while (acc < 256);
      } else if ((acc = code & 0xFF) !== 0) {
        t.o2C34[acc] = 0xFF;
        if (code & 0x3F) {
          bits -= 4; t.bits[count] = bits; add = 1 << bits; acc = code >> 4;
          do { t.o2D34[acc] = count; acc += add; } while (acc < 256);
        } else {
          bits -= 6; t.bits[count] = bits; add = 1 << bits; acc = code >> 6;
          do { t.o2E34[acc] = count; acc += add; } while (acc < 128);
        }
      } else {
        bits -= 8; t.bits[count] = bits; add = 1 << bits; acc = code >> 8;
        do { t.o2EB4[acc] = count; acc += add; } while (acc < 256);
      }
    }
    return t;
  }

  // 입력: Uint8Array (압축 데이터), outSize: 예상 크기. 반환: Uint8Array
  function explode(inp, outSize) {
    if (inp.length <= 4) throw new Error('explode: 데이터가 너무 짧음');
    var ctype = inp[0], dbits = inp[1];
    if (dbits < 4 || dbits > 6) throw new Error('explode: 사전 크기 오류');
    if (ctype !== 0 && ctype !== 1) throw new Error('explode: 모드 오류');
    var dmask = 0xFFFF >> (16 - dbits);
    var asc = null;
    if (ctype === 1) { if (!ascTabs) ascTabs = genAscTabs(); asc = ascTabs; }
    var bitBuf = inp[2], extra = 0, ip = 3, n = inp.length;
    var out = new Uint8Array(outSize), op = 0;
    var END = false;
    function waste(nb) {   // true = 입력 끝
      if (nb <= extra) { extra -= nb; bitBuf >>>= nb; return false; }
      bitBuf >>>= extra;
      if (ip >= n) return true;
      bitBuf |= inp[ip++] << 8;
      bitBuf >>>= (nb - extra);
      extra = (extra - nb) + 8;
      return false;
    }
    function decodeLit() {
      var v;
      if (bitBuf & 1) {
        if (waste(1)) return 0x306;
        var lc = LengthCodes[bitBuf & 0xFF];
        if (waste(LenBits[lc])) return 0x306;
        var eb = ExLenBits[lc];
        if (eb !== 0) {
          var el = bitBuf & ((1 << eb) - 1);
          if (waste(eb)) { if ((lc + el) !== 0x10E) return 0x306; }
          lc = LenBase[lc] + el;
        }
        return lc + 0x100;
      }
      if (waste(1)) return 0x306;
      if (ctype === 0) {
        v = bitBuf & 0xFF;
        if (waste(8)) return 0x306;
        return v;
      }
      if (bitBuf & 0xFF) {
        v = asc.o2C34[bitBuf & 0xFF];
        if (v === 0xFF) {
          if (bitBuf & 0x3F) { if (waste(4)) return 0x306; v = asc.o2D34[bitBuf & 0xFF]; }
          else { if (waste(6)) return 0x306; v = asc.o2E34[bitBuf & 0x7F]; }
        }
      } else {
        if (waste(8)) return 0x306;
        v = asc.o2EB4[bitBuf & 0xFF];
      }
      return waste(asc.bits[v]) ? 0x306 : v;
    }
    function decodeDist(rep) {
      var pc = DistPosCodes[bitBuf & 0xFF];
      var pb = DistBits[pc], d;
      if (waste(pb)) return 0;
      if (rep === 2) { d = (pc << 2) | (bitBuf & 3); if (waste(2)) return 0; }
      else { d = (pc << dbits) | (bitBuf & dmask); if (waste(dbits)) return 0; }
      return d + 1;
    }
    var lit;
    while ((lit = decodeLit()) < 0x305) {
      if (lit >= 0x100) {
        var rep = lit - 0xFE, dist = decodeDist(rep);
        if (dist === 0) throw new Error('explode: 거리 오류');
        var src = op - dist;
        for (var k = 0; k < rep && op < outSize; k++) { out[op] = src + k >= 0 ? out[src + k] : 0; op++; }
      } else {
        if (op < outSize) out[op++] = lit; else break;
      }
      if (op >= outSize) break;
    }
    return op === outSize ? out : out.subarray(0, op);
  }

  // ---------------------------------------------------------------- 적응형 허프만 (StormLib huff.cpp)
  var HUFF_DIST = [
    hx('0A0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000020000'),
    hx('5416160D0C08060506050603040403050E0B141313090B0605040302030202020D070906060403020403030303030202090604040404030203020202020302040803040709050303030302020203020203020202020202020201010102010202060A0808060704030404020204020303040307070906040303020102020202020A0202030202010102020206030502030201010101010101010101020301010102010101010101020404040709080C02010101010101010101010101020101030401020405010101010101010201010104010101010102010101010101010101020101010101010103010101010101010201010101010102020101020202064B0000'),
    hx('0000000000000000000327000023000000000000000000000000000000000000FF0101010101010102020101060E10040608050404030302020303010102010101040204020202010104010102030302030103060401010101010102010201010129071612400A0A112501031710262A100123232F100607020901010101010000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'),
    hx('FF0B07050B020202060202010402010309010101030401010201010102010101050101010D0101010101010101010101020101030101010101010102010101010A0402010603020101010101030101010502030403030302010101020102030301030101020501010403050103010303020104030A06010101010101010101010202010A0205010102070217010501010E0101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101060201040501010201010101020101010101010101010101010101010101010101010101010101010701010201010101020101010101010102010101010101110000'),
    hx('FFFB989A848563643E3E2222131318170000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'),
    hx('FFF19D9E9A9B9A9793938C8E868880827C7C7273696B5F6055564A4B404137372F2F272721211B1C1717131310100D0D0B0B09090808070706050504040419180000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'),
    hx('C3CBF541FF7BF7210000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000BFCCF240FD7CF72200000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000007A460000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'),
    hx('C3D9EF3DF97CE91EFDABF12CFC5BFE17000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000BDD9EC3DF57DE81DFBAEF02CFB5CFF18000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000706C0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'),
    hx('BAC5DA33E36DD818E594DA23DF4AD110EEAFE42CEA5ADE15F487E921F643FC120000000000000000000000000000000000000000000000000000000000000000B0C7D833E36BD618E795D823DB49D011E9B2E22BE85CDD15F187E720F744FF1300000000000000000000000000000000000000000000000000000000000000005F9E0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000')
  ];
  function hx(s) {
    var a = new Uint8Array(s.length >> 1);
    for (var i = 0; i < a.length; i++) a[i] = parseInt(s.substr(i * 2, 2), 16);
    return a;
  }

  function huffDecompress(inp, outSize) {
    // 비트 입력
    var ip = 0, n = inp.length, bitBuf = 0, bitCnt = 0;
    function get1() { if (bitCnt === 0) { if (ip >= n) return -1; bitBuf = inp[ip++]; bitCnt = 8; }
      var v = bitBuf & 1; bitBuf >>>= 1; bitCnt--; return v; }
    function get8() { if (bitCnt < 8) { if (ip >= n) return -1; bitBuf |= inp[ip++] << bitCnt; bitCnt += 8; }
      var v = bitBuf & 0xFF; bitBuf >>>= 8; bitCnt -= 8; return v; }

    // 트리 항목: {next, prev, parent, childLo, value, weight}
    var head = { next: null, prev: null, isHead: true };
    head.next = head.prev = head;
    var byByte = new Array(0x102);
    function removeItem(it) { if (it.next) { it.prev.next = it.next; it.next.prev = it.prev; it.next = it.prev = null; } }
    function linkTwo(i1, i2) { i2.next = i1.next; i2.prev = i1.next.prev; i1.next.prev = i2; i1.next = i2; }
    function insertItem(it, after) { removeItem(it); if (after) linkTwo(head, it); else linkTwo(head.prev, it); }
    function findHigherOrEqual(it, w) {
      if (it) { while (it !== head) { if (it.weight >= w) return it; it = it.prev; } }
      return head;
    }
    var used = 0;
    function createItem(val, w, after) {
      if (used >= 0x203) return null;
      used++;
      var it = { next: null, prev: null, parent: null, childLo: null, value: val, weight: w };
      insertItem(it, after);
      return it;
    }
    function fixupPos(it, maxW) {
      if (it.weight < maxW) { var h = findHigherOrEqual(head.prev, it.weight); removeItem(it); linkTwo(h, it); }
      else maxW = it.weight;
      return maxW;
    }
    var dataType = get8();
    if (dataType < 0) throw new Error('huffman: 데이터 없음');
    var isSparse = dataType === 0;
    var dt = dataType & 0x0F;
    if (dt >= HUFF_DIST.length) throw new Error('huffman: 형식 오류');
    var dist = HUFF_DIST[dt], maxW = 0, i, it;
    for (i = 0; i < 256; i++) {
      if (dist[i] !== 0) { it = byByte[i] = createItem(i, dist[i], true); maxW = fixupPos(it, maxW); }
    }
    byByte[0x100] = createItem(0x100, 1, false);
    byByte[0x101] = createItem(0x101, 1, false);
    var lo = head.prev, hi;
    while (lo !== head) {
      hi = lo.prev; if (hi === head) break;
      it = createItem(0, hi.weight + lo.weight, true);
      lo.parent = it; hi.parent = it; it.childLo = lo;
      maxW = fixupPos(it, maxW);
      lo = hi.prev;
    }
    function incWeights(item) {
      for (; item; item = item.parent) {
        item.weight++;
        var higher = findHigherOrEqual(item.prev, item.weight);
        var cHi = higher.next;
        if (cHi !== item) {
          removeItem(cHi); linkTwo(item, cHi);
          removeItem(item); linkTwo(higher, item);
          var cLo = cHi.parent.childLo;
          var p = item.parent;
          if (p.childLo === item) p.childLo = cHi;
          if (cLo === cHi) cHi.parent.childLo = item;
          p = item.parent;
          item.parent = cHi.parent;
          cHi.parent = p;
        }
      }
    }
    function insertBranch(v1, v2) {
      var last = head.prev;
      var cHi = createItem(v1, last.weight, false); if (!cHi) return false;
      cHi.parent = last; byByte[v1] = cHi;
      var cLo = createItem(v2, 0, false); if (!cLo) return false;
      cLo.parent = last; last.childLo = cLo; byByte[v2] = cLo;
      incWeights(cLo);
      return true;
    }
    var out = new Uint8Array(outSize), op = 0;
    for (;;) {
      if (head.next === head) throw new Error('huffman: 트리 오류');
      it = head.next;
      while (it.childLo) {
        var b = get1(); if (b < 0) throw new Error('huffman: 입력 끝');
        it = b ? it.childLo.prev : it.childLo;
      }
      var val = it.value;
      if (val === 0x100) break;
      if (val === 0x101) {
        val = get8(); if (val < 0) throw new Error('huffman: 입력 끝');
        if (!insertBranch(head.prev.value, val)) throw new Error('huffman: 트리 넘침');
        if (!isSparse) incWeights(byByte[val]);
      }
      if (op >= outSize) break;
      out[op++] = val;
      if (isSparse) incWeights(byByte[val]);
    }
    return op === outSize ? out : out.subarray(0, op);
  }

  // ---------------------------------------------------------------- IMA ADPCM (Storm 변형)
  var NextStep = [-1,0,-1,4,-1,2,-1,6,-1,1,-1,5,-1,3,-1,7,-1,1,-1,5,-1,3,-1,7,-1,2,-1,4,-1,6,-1,8];
  var StepSize = [7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,
    157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,
    1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,
    10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767];
  function adpcmDecompress(inp, outSize, ch) {
    var out = new Uint8Array(outSize), op = 0, ip = 0, n = inp.length;
    var pred = [0, 0], sidx = [0x2C, 0x2C];
    function w16(s) { if (op + 2 > outSize) return false; out[op++] = s & 0xFF; out[op++] = (s >> 8) & 0xFF; return true; }
    if (n < 2) return out.subarray(0, 0);
    ip = 1; var shift = inp[ip++];
    for (var c = 0; c < ch; c++) {
      if (ip + 2 > n) return out.subarray(0, op);
      var s0 = inp[ip] | (inp[ip + 1] << 8); ip += 2; if (s0 & 0x8000) s0 -= 0x10000;
      pred[c] = s0; if (!w16(s0)) return out.subarray(0, op);
    }
    var ci = ch - 1;
    while (ip < n) {
      var e = inp[ip++];
      ci = (ci + 1) % ch;
      if (e === 0x80) {
        if (sidx[ci] !== 0) sidx[ci]--;
        if (!w16(pred[ci])) break;
      } else if (e === 0x81) {
        sidx[ci] += 8; if (sidx[ci] > 0x58) sidx[ci] = 0x58;
        ci = (ci + 1) % ch;
      } else {
        var si = sidx[ci], st = StepSize[si], diff = st >> shift, p = pred[ci];
        if (e & 1) diff += st; if (e & 2) diff += st >> 1; if (e & 4) diff += st >> 2;
        if (e & 8) diff += st >> 3; if (e & 0x10) diff += st >> 4; if (e & 0x20) diff += st >> 5;
        if (e & 0x40) { p -= diff; if (p <= -32768) p = -32768; }
        else { p += diff; if (p >= 32767) p = 32767; }
        pred[ci] = p;
        if (!w16(p)) break;
        si += NextStep[e & 0x1F]; if (si < 0) si = 0; else if (si > 88) si = 88;
        sidx[ci] = si;
      }
    }
    return op === outSize ? out : out.subarray(0, op);
  }

  // ---------------------------------------------------------------- zlib inflate (RFC1950/1951)
  function inflate(inp, outSize, raw) {
    var ip = raw ? 0 : 2, bb = 0, bc = 0, n = inp.length;
    var out = new Uint8Array(outSize || Math.max(1024, inp.length * 4)), op = 0;
    function need(cnt) { while (bc < cnt) { if (ip >= n) throw new Error('inflate: 입력 끝'); bb |= inp[ip++] << bc; bc += 8; } }
    function bits(cnt) { need(cnt); var v = bb & ((1 << cnt) - 1); bb >>>= cnt; bc -= cnt; return v; }
    function grow(k) { if (op + k <= out.length) return; var no = new Uint8Array(Math.max(out.length * 2, op + k)); no.set(out); out = no; }
    function build(lens) {
      var max = 0, i; for (i = 0; i < lens.length; i++) if (lens[i] > max) max = lens[i];
      var cnt = new Uint16Array(max + 1), offs = new Uint16Array(max + 2), sym = new Uint16Array(lens.length);
      for (i = 0; i < lens.length; i++) cnt[lens[i]]++;
      cnt[0] = 0;
      for (i = 1; i <= max; i++) offs[i + 1] = offs[i] + cnt[i];
      for (i = 0; i < lens.length; i++) if (lens[i]) sym[offs[lens[i]]++] = i;
      return { cnt: cnt, sym: sym, max: max };
    }
    function decode(h) {
      var code = 0, first = 0, index = 0;
      for (var len = 1; len <= h.max; len++) {
        code |= bits(1);
        var c = h.cnt[len];
        if (code - c < first) return h.sym[index + (code - first)];
        index += c; first += c; first <<= 1; code <<= 1;
      }
      throw new Error('inflate: 코드 오류');
    }
    var LB = [3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258];
    var LE = [0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0];
    var DB = [1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577];
    var DE = [0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13];
    var fixedL = null, fixedD = null;
    var last;
    do {
      last = bits(1);
      var type = bits(2), i;
      if (type === 0) {
        bb = 0; bc = 0;
        if (ip + 4 > n) throw new Error('inflate: 입력 끝');
        var len = inp[ip] | (inp[ip + 1] << 8); ip += 4;
        grow(len); out.set(inp.subarray(ip, ip + len), op); ip += len; op += len;
      } else if (type === 1 || type === 2) {
        var lh, dh;
        if (type === 1) {
          if (!fixedL) {
            var l = new Uint8Array(288);
            for (i = 0; i < 144; i++) l[i] = 8; for (; i < 256; i++) l[i] = 9; for (; i < 280; i++) l[i] = 7; for (; i < 288; i++) l[i] = 8;
            fixedL = build(l); var d = new Uint8Array(30); for (i = 0; i < 30; i++) d[i] = 5; fixedD = build(d);
          }
          lh = fixedL; dh = fixedD;
        } else {
          var nlen = bits(5) + 257, ndist = bits(5) + 1, ncode = bits(4) + 4;
          var ord = [16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15];
          var cl = new Uint8Array(19); for (i = 0; i < ncode; i++) cl[ord[i]] = bits(3);
          var ch = build(cl), lens = new Uint8Array(nlen + ndist), idx = 0;
          while (idx < nlen + ndist) {
            var s = decode(ch);
            if (s < 16) lens[idx++] = s;
            else {
              var rep, val = 0;
              if (s === 16) { if (idx === 0) throw new Error('inflate: 반복 오류'); val = lens[idx - 1]; rep = 3 + bits(2); }
              else if (s === 17) rep = 3 + bits(3); else rep = 11 + bits(7);
              while (rep--) lens[idx++] = val;
            }
          }
          lh = build(lens.subarray(0, nlen)); dh = build(lens.subarray(nlen));
        }
        for (;;) {
          var sy = decode(lh);
          if (sy < 256) { grow(1); out[op++] = sy; }
          else if (sy === 256) break;
          else {
            sy -= 257; var ln = LB[sy] + bits(LE[sy]);
            var ds = decode(dh); var dd = DB[ds] + bits(DE[ds]);
            grow(ln);
            for (var k = 0; k < ln; k++) { out[op] = out[op - dd]; op++; }
          }
        }
      } else throw new Error('inflate: 블록 형식 오류');
    } while (!last);
    return out.subarray(0, op);
  }

  // ---------------------------------------------------------------- MPQ v1 다중 압축 해제
  // 순서: bzip2(0x10), pkware(0x08), zlib(0x02), huffman(0x01), adpcm stereo(0x80), adpcm mono(0x40)
  function decompressMulti(inp, outSize) {
    if (inp.length === outSize) return inp;
    var mask = inp[0], data = inp.subarray(1);
    if (mask & 0x10) throw new Error('bzip2 압축은 지원하지 않음');
    if (mask & 0x08) data = explode(data, outSize);
    if (mask & 0x02) data = inflate(data, outSize);
    if (mask & 0x01) data = huffDecompress(data, outSize);
    if (mask & 0x80) data = adpcmDecompress(data, outSize, 2);
    if (mask & 0x40) data = adpcmDecompress(data, outSize, 1);
    if (mask & ~0xDB) throw new Error('알 수 없는 압축 방식 0x' + mask.toString(16));
    return data;
  }

  D2C.explode = explode;
  D2C.huffDecompress = huffDecompress;
  D2C.adpcmDecompress = adpcmDecompress;
  D2C.inflate = inflate;
  D2C.decompressMulti = decompressMulti;
})(typeof window !== 'undefined' ? window : global);
