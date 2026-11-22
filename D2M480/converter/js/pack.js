// D2M480 변환기 - 엑셀 표 읽기, COF 해석, PAK 파일 쓰기
(function (root) {
  'use strict';
  var D2C = root.D2C = root.D2C || {};

  // ------------------------------------------------------------ 탭 구분 표 (.txt)
  function parseTable(bytes) {
    var s = '';
    for (var i = 0; i < bytes.length; i += 8192) s += String.fromCharCode.apply(null, bytes.subarray(i, Math.min(bytes.length, i + 8192)));
    var lines = s.split(/\r?\n/), head = lines[0].split('\t'), rows = [];
    for (i = 1; i < lines.length; i++) {
      if (!lines[i]) continue;
      var c = lines[i].split('\t'), r = {};
      for (var k = 0; k < head.length; k++) r[head[k]] = c[k] === undefined ? '' : c[k];
      r._cells = c;
      rows.push(r);
    }
    return { head: head, rows: rows };
  }
  // 대소문자 구분 없이 열 값 읽기
  function col(row, name) {
    if (row[name] !== undefined) return row[name];
    var ln = name.toLowerCase();
    for (var k in row) if (k.toLowerCase() === ln) return row[k];
    return '';
  }

  // ------------------------------------------------------------ COF
  function parseCOF(d) {
    if (!d || d.length < 28) return null;
    var nl = d[0], fpd = d[1], nd = d[2], layers = [];
    for (var i = 0; i < nl; i++) {
      var p = 28 + i * 9, wc = '';
      for (var k = 5; k < 9; k++) if (d[p + k] && d[p + k] !== 0x20) wc += String.fromCharCode(d[p + k]);
      layers.push({ comp: d[p], shadow: d[p + 1], sel: d[p + 2], trans: d[p + 3], effect: d[p + 4], wclass: wc });
    }
    return { layers: layers, fpd: fpd, dirs: nd, speed: d[24] };
  }
  var COMPS = ['HD', 'TR', 'LG', 'RA', 'LA', 'RH', 'LH', 'SH', 'S1', 'S2', 'S3', 'S4', 'S5', 'S6', 'S7', 'S8'];

  // ------------------------------------------------------------ PAK 쓰기
  // 'D2PK' u32 버전(1), u32 항목 수, u32 목록 위치 / 데이터 / 목록: [u32 위치, u32 크기, u8 이름길이, 이름] (이름순 정렬)
  function PakWriter(name) {
    this.name = name; this.parts = [new Uint8Array(16)]; this.pos = 16; this.entries = []; this.names = {};
    this.bytes = 0;
  }
  PakWriter.prototype.add = function (path, data) {
    var key = path.toLowerCase().replace(/\\/g, '/');
    if (this.names[key]) return false;
    this.names[key] = 1;
    this.entries.push({ name: key, off: this.pos, size: data.length });
    this.parts.push(data); this.pos += data.length; this.bytes += data.length;
    return true;
  };
  PakWriter.prototype.has = function (path) { return !!this.names[path.toLowerCase().replace(/\\/g, '/')]; };
  PakWriter.prototype.finish = function () {
    this.entries.sort(function (a, b) { return a.name < b.name ? -1 : a.name > b.name ? 1 : 0; });
    var size = 0, i;
    for (i = 0; i < this.entries.length; i++) size += 9 + this.entries[i].name.length;
    var dir = new Uint8Array(size), dv = new DataView(dir.buffer), p = 0;
    for (i = 0; i < this.entries.length; i++) {
      var e = this.entries[i];
      dv.setUint32(p, e.off, true); dv.setUint32(p + 4, e.size, true); dir[p + 8] = e.name.length;
      for (var k = 0; k < e.name.length; k++) dir[p + 9 + k] = e.name.charCodeAt(k) & 0x7F;
      p += 9 + e.name.length;
    }
    var hdr = this.parts[0], hv = new DataView(hdr.buffer);
    hdr[0] = 0x44; hdr[1] = 0x32; hdr[2] = 0x50; hdr[3] = 0x4B;
    hv.setUint32(4, 1, true); hv.setUint32(8, this.entries.length, true); hv.setUint32(12, this.pos, true);
    this.parts.push(dir);
    return this.parts;
  };
  // 검증용 PAK 읽기
  function readPak(d) {
    var dv = new DataView(d.buffer, d.byteOffset, d.length), n = dv.getUint32(8, true), p = dv.getUint32(12, true), out = {};
    for (var i = 0; i < n; i++) {
      var off = dv.getUint32(p, true), sz = dv.getUint32(p + 4, true), ln = d[p + 8], nm = '';
      for (var k = 0; k < ln; k++) nm += String.fromCharCode(d[p + 9 + k]);
      out[nm] = d.subarray(off, off + sz); p += 9 + ln;
    }
    return out;
  }

  D2C.parseTable = parseTable;
  D2C.col = col;
  D2C.parseCOF = parseCOF;
  D2C.COMPS = COMPS;
  D2C.PakWriter = PakWriter;
  D2C.readPak = readPak;
})(typeof window !== 'undefined' ? window : global);
