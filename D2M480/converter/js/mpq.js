// D2M480 변환기 - MPQ(v1) 아카이브 읽기
// 원본 MPQ 파일은 사용자 PC에서만 읽으며, 어디에도 전송하지 않는다.
(function (root) {
  'use strict';
  var D2C = root.D2C = root.D2C || {};

  // 암호 테이블
  var CT = new Uint32Array(0x500);
  (function () {
    var seed = 0x00100001;
    for (var i = 0; i < 0x100; i++) {
      for (var j = 0, idx = i; j < 5; j++, idx += 0x100) {
        seed = (seed * 125 + 3) % 0x2AAAAB; var t1 = (seed & 0xFFFF) << 16;
        seed = (seed * 125 + 3) % 0x2AAAAB; var t2 = seed & 0xFFFF;
        CT[idx] = (t1 | t2) >>> 0;
      }
    }
  })();

  function upper(c) { return (c >= 0x61 && c <= 0x7A) ? c - 0x20 : (c === 0x2F ? 0x5C : c); }
  function hashString(str, type) {
    var s1 = 0x7FED7FED, s2 = 0xEEEEEEEE;
    for (var i = 0; i < str.length; i++) {
      var ch = upper(str.charCodeAt(i) & 0xFF);
      s1 = (CT[type * 0x100 + ch] ^ ((s1 + s2) >>> 0)) >>> 0;
      s2 = (ch + s1 + s2 + ((s2 << 5) >>> 0) + 3) >>> 0;
    }
    return s1 >>> 0;
  }
  // 32비트 곱셈 없이 덧셈만 쓰므로 정확도 문제 없음
  function decryptBlock(u32, key) {
    var s2 = 0xEEEEEEEE;
    for (var i = 0; i < u32.length; i++) {
      s2 = (s2 + CT[0x400 + (key & 0xFF)]) >>> 0;
      var v = (u32[i] ^ ((key + s2) >>> 0)) >>> 0;
      key = ((((~key << 0x15) >>> 0) + 0x11111111) >>> 0 | (key >>> 0x0B)) >>> 0;
      s2 = (v + s2 + ((s2 << 5) >>> 0) + 3) >>> 0;
      u32[i] = v;
    }
  }
  function decryptBytes(bytes, key) {
    // 4바이트 단위만 복호화 (나머지는 그대로)
    var n = bytes.length >> 2;
    if (!n) return;
    var dv = new DataView(bytes.buffer, bytes.byteOffset, n * 4);
    var u = new Uint32Array(n), i;
    for (i = 0; i < n; i++) u[i] = dv.getUint32(i * 4, true);
    decryptBlock(u, key);
    for (i = 0; i < n; i++) dv.setUint32(i * 4, u[i], true);
  }

  var F_IMPLODE = 0x100, F_COMPRESS = 0x200, F_ENCRYPTED = 0x10000, F_FIXKEY = 0x20000,
      F_SINGLE = 0x1000000, F_CRC = 0x4000000, F_EXISTS = 0x80000000;

  // source: { size, read(offset, length) -> Promise<Uint8Array> }
  function MPQ(source, name) {
    this.src = source; this.name = name || 'mpq';
  }
  MPQ.prototype.open = async function () {
    var base = -1, off, hdr;
    for (off = 0; off < Math.min(this.src.size, 0x100000 * 64); off += 0x200) {
      hdr = await this.src.read(off, 32);
      if (hdr[0] === 0x4D && hdr[1] === 0x50 && hdr[2] === 0x51 && hdr[3] === 0x1A) { base = off; break; }
    }
    if (base < 0) throw new Error(this.name + ': MPQ 파일이 아닙니다');
    var dv = new DataView(hdr.buffer, hdr.byteOffset, 32);
    this.base = base;
    this.sectorSize = 512 << dv.getUint16(14, true);
    var hashPos = dv.getUint32(16, true), blockPos = dv.getUint32(20, true);
    var hashCount = dv.getUint32(24, true), blockCount = dv.getUint32(28, true);
    // 해시 테이블
    var hb = await this.src.read(base + hashPos, hashCount * 16);
    var hu = toU32(hb); decryptBlock(hu, hashString('(hash table)', 3));
    this.hash = hu; this.hashCount = hashCount;
    // 블록 테이블 (파일 끝을 넘으면 남는 부분만)
    var bbytes = blockCount * 16;
    var avail = this.src.size - (base + blockPos);
    if (bbytes > avail) { blockCount = Math.floor(avail / 16); bbytes = blockCount * 16; }
    var bb = await this.src.read(base + blockPos, bbytes);
    var bu = toU32(bb); decryptBlock(bu, hashString('(block table)', 3));
    this.block = bu; this.blockCount = blockCount;
    return this;
  };
  function toU32(bytes) {
    var n = bytes.length >> 2, u = new Uint32Array(n), dv = new DataView(bytes.buffer, bytes.byteOffset, n * 4);
    for (var i = 0; i < n; i++) u[i] = dv.getUint32(i * 4, true);
    return u;
  }
  MPQ.prototype.find = function (path) {
    var i0 = hashString(path, 0) % this.hashCount, a = hashString(path, 1), b = hashString(path, 2);
    for (var k = 0; k < this.hashCount; k++) {
      var i = (i0 + k) % this.hashCount, e = i * 4;
      var bi = this.hash[e + 3];
      if (bi === 0xFFFFFFFF) return -1;            // 빈 칸: 없음
      if (this.hash[e] === a && this.hash[e + 1] === b && bi !== 0xFFFFFFFE) {
        if (bi < this.blockCount && (this.block[bi * 4 + 3] & F_EXISTS)) return bi;
      }
    }
    return -1;
  };
  MPQ.prototype.has = function (path) { return this.find(path) >= 0; };
  MPQ.prototype.read = async function (path) {
    var bi = this.find(path);
    if (bi < 0) return null;
    var pos = this.block[bi * 4], csize = this.block[bi * 4 + 1], fsize = this.block[bi * 4 + 2], flags = this.block[bi * 4 + 3];
    if (fsize === 0) return new Uint8Array(0);
    var raw = await this.src.read(this.base + pos, csize);
    raw = new Uint8Array(raw);   // 복사본 (복호화로 원본 손상 방지)
    var key = 0;
    if (flags & F_ENCRYPTED) {
      var fname = path.replace(/\//g, '\\'); fname = fname.substr(fname.lastIndexOf('\\') + 1);
      key = hashString(fname, 3);
      if (flags & F_FIXKEY) key = ((key + pos) ^ fsize) >>> 0;
    }
    var comp = flags & (F_IMPLODE | F_COMPRESS);
    if (flags & F_SINGLE) {
      if (flags & F_ENCRYPTED) decryptBytes(raw, key);
      if (!comp || csize === fsize) return raw.subarray(0, fsize);
      return (flags & F_IMPLODE) ? D2C.explode(raw, fsize) : D2C.decompressMulti(raw, fsize);
    }
    if (!comp) {
      if (flags & F_ENCRYPTED) {
        var nsec = Math.ceil(fsize / this.sectorSize);
        for (var s = 0; s < nsec; s++) {
          var a = s * this.sectorSize, e2 = Math.min(fsize, a + this.sectorSize);
          decryptBytes(raw.subarray(a, e2), (key + s) >>> 0);
        }
      }
      return raw.subarray(0, fsize);
    }
    var ns = Math.ceil(fsize / this.sectorSize);
    var tabN = ns + 1 + ((flags & F_CRC) ? 1 : 0);
    var tab = new Uint32Array(tabN), dv = new DataView(raw.buffer, raw.byteOffset, raw.length), i;
    for (i = 0; i < tabN; i++) tab[i] = dv.getUint32(i * 4, true);
    if (flags & F_ENCRYPTED) decryptBlock(tab, (key - 1) >>> 0);
    var out = new Uint8Array(fsize), op = 0;
    for (i = 0; i < ns; i++) {
      var so = tab[i], se = tab[i + 1];
      if (se < so || se > raw.length) throw new Error(path + ': 섹터 표 오류 (암호화 키 문제일 수 있음)');
      var sec = raw.subarray(so, se);
      if (flags & F_ENCRYPTED) decryptBytes(sec, (key + i) >>> 0);
      var want = Math.min(this.sectorSize, fsize - op);
      var dec;
      if (sec.length === want) dec = sec;
      else dec = (flags & F_IMPLODE) ? D2C.explode(sec, want) : D2C.decompressMulti(sec, want);
      out.set(dec.subarray(0, Math.min(dec.length, want)), op);
      op += want;
    }
    return out;
  };

  // 여러 MPQ를 우선순위대로 묶음 (patch_d2 > d2exp > d2data ...)
  function MPQSet() { this.list = []; }
  MPQSet.prototype.add = function (mpq) { this.list.push(mpq); };
  MPQSet.prototype.which = function (path) {
    for (var i = 0; i < this.list.length; i++) if (this.list[i].has(path)) return this.list[i];
    return null;
  };
  MPQSet.prototype.has = function (path) { return !!this.which(path); };
  MPQSet.prototype.read = async function (path) {
    var m = this.which(path);
    return m ? m.read(path) : null;
  };

  // 브라우저 File → source
  D2C.fileSource = function (file) {
    return {
      size: file.size,
      read: function (off, len) {
        var blob = file.slice(off, off + len);
        if (blob.arrayBuffer) return blob.arrayBuffer().then(function (b) { return new Uint8Array(b); });
        return new Promise(function (res, rej) {
          var fr = new FileReader();
          fr.onload = function () { res(new Uint8Array(fr.result)); };
          fr.onerror = function () { rej(fr.error); };
          fr.readAsArrayBuffer(blob);
        });
      }
    };
  };
  // Node 테스트용
  D2C.nodeSource = function (fd, size) {
    var fs = require('fs');
    return { size: size, read: function (off, len) {
      var b = Buffer.alloc(len); var n = fs.readSync(fd, b, 0, len, off);
      return Promise.resolve(new Uint8Array(b.buffer, b.byteOffset, n));
    } };
  };

  D2C.MPQ = MPQ;
  D2C.MPQSet = MPQSet;
  D2C.hashString = hashString;
})(typeof window !== 'undefined' ? window : global);
