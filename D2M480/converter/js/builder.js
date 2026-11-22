// D2M480 변환기 - 어떤 원본 파일을 어느 팩에 넣을지 정하고 변환한다
//   d2_common.pak : 수치표·문자열·팔레트·UI·아이템 그림·공용 효과음
//   d2_act1.pak   : 액트1 맵 타일·맵 조각·몬스터·NPC·오브젝트·음악
//   d2_ba.pak     : 바바리안 캐릭터 그림·스킬 이펙트·목소리·스킬 아이콘
(function (root) {
  'use strict';
  var D2C = root.D2C = root.D2C || {};
  var col = function (r, n) { return D2C.col(r, n); };

  var EXCEL = ['levels', 'lvlprest', 'lvltypes', 'lvlwarp', 'lvlmaze', 'lvlsub', 'objects', 'objmode', 'objtype', 'objgroup', 'obj',
    'sounds', 'itemstatcost', 'itemratio', 'itemtypes', 'qualityitems', 'lowqualityitems', 'overlay', 'runes', 'sets', 'setitems',
    'automagic', 'bodylocs', 'events', 'properties', 'hireling', 'hiredesc', 'difficultylevels', 'automap', 'cubemain', 'cubemod',
    'cubetype', 'skills', 'skilldesc', 'skillcalc', 'misscalc', 'treasureclass', 'treasureclassex', 'states', 'soundenviron',
    'shrines', 'monprop', 'elemtypes', 'plrmode', 'pettype', 'npc', 'monumod', 'monequip', 'uniqueappellation', 'monlvl',
    'monsounds', 'monseq', 'playerclass', 'plrtype', 'composit', 'hitclass', 'compcode', 'belts', 'gamble', 'colors',
    'storepage', 'inventory', 'weapons', 'armor', 'armtype', 'weaponclass', 'books', 'misc', 'uniqueitems', 'gems',
    'magicprefix', 'magicsuffix', 'rareprefix', 'raresuffix', 'uniqueprefix', 'uniquesuffix', 'experience', 'charstats',
    'monstats', 'monstats2', 'monpreset', 'montype', 'superuniques', 'monmode', 'monplace', 'monai', 'missiles', 'lowqualityitems',
    'arena', 'charstemplate', 'cubemod', 'misscalc', 'monname', 'qualityitems', 'skillcalc', 'uniquetitle', 'worldevent'];
  var PAL_NAMES = ['act1', 'act2', 'act3', 'act4', 'act5', 'units', 'menu0', 'menu1', 'menu2', 'menu3', 'menu4', 'loading',
    'endgame', 'endgame2', 'fechar', 'sky', 'static', 'trademark'];
  var LANG = { 0: 'eng', 1: 'esp', 2: 'deu', 3: 'fra', 4: 'por', 5: 'ita', 6: 'jpn', 7: 'kor', 8: 'sin', 9: 'chi', 10: 'pol', 11: 'rus', 12: 'eng' };

  var PLR_MODES = ['DT', 'NU', 'WL', 'RN', 'GH', 'TN', 'TW', 'A1', 'A2', 'BL', 'SC', 'TH', 'KK', 'S1', 'S2', 'S3', 'S4', 'DD', 'SQ', 'KB'];
  var MON_MODES = ['DT', 'NU', 'WL', 'GH', 'A1', 'A2', 'BL', 'SC', 'S1', 'S2', 'S3', 'S4', 'DD', 'KB', 'SQ', 'RN'];
  var OBJ_MODES = ['NU', 'OP', 'ON', 'S1', 'S2', 'S3', 'S4', 'S5'];
  var WCLASS = ['HTH', 'BOW', '1HS', '1HT', 'STF', '2HS', '2HT', 'XBW', '1JS', '1JT', '1SS', '1ST', 'HT1', 'HT2'];

  var UI_FILES = {
    'PANEL': ['ctrlpnl7', '800ctrlpnl7', 'overlap', 'hlthmana', 'level', 'menubutton', 'runbutton', 'minipanel', 'minipanel_s',
      'minipanelbtn', 'invchar6', 'invchar6tab', 'invchar', 'skillpoints', 'levelsocket', 'inv_armor', 'inv_belt', 'inv_boots',
      'inv_helm_glove', 'inv_ring_amulet', 'inv_weapons', 'goldcoinbtn', 'buysellbtn', 'buysell', 'buysell2', 'buysellbtn2',
      'tradestash', 'stash', 'trade', '800borderframe', 'skillicon', 'scrollbar', 'npcmenu', 'invbelt', 'popbelt', 'beltpanel',
      'bigbutton', 'gemsocket', 'supertransmogrifier', 'statsbtn', 'skillbtn', 'invbtn', 'questbtn', 'mapbtn', 'chatbtn',
      'weaponswitch', 'inv_hand', 'ctrlpnl7a', 'portrait', 'textbox', 'chardbtn', 'chardgem', 'waypoint', 'hireables'],
    'CURSOR': ['ohand', 'pentspin', 'buy', 'sell', 'repair', 'identify', 'protect', 'cursor', 'hand'],
    'MENU': ['questbackground', 'questdone', 'expquesttabs', 'questlast', 'questsockets', 'boxpieces', 'textslid',
      'helpyellowbullet', 'helpwhitebullet', '800helpborder', 'helpborder', 'party', 'partybuttons', 'partyboxes', 'partybar',
      'dialogbackground', 'expwaygatetabs', 'waygatebackground', 'waygateicons', 'waygatetabs', 'npcbuttons', 'npctalk',
      'creditsbckgexpand', 'options', 'trademark'],
    'BIGMENU': ['numberarrows'],
    'Loading': ['loadingscreen'],
    'FrontEnd': ['trademarkscreenEXP', 'gameselectscreenEXP', 'charactercreationscreenEXP', 'fire', 'PopUpOKCancel', 'PopUpOk',
      'PopUpLarge', 'PopUpWide', 'PopUp_340x224', 'WideButtonBlank', 'MediumButtonBlank', 'CancelButtonBlank', 'NarrowButtonBlank',
      'textbox2', 'clickbox', 'D2logoFireLeft', 'D2logoFireRight', 'D2logoBlackLeft', 'D2logoBlackRight'],
    'CharSelect': ['characterselectscreenEXP', 'charselectbox', 'ShortButtonBlank', 'TallButtonBlank'],
    'widgets': ['optbarc', 'optskull'],
    'SPELLS': ['Skillicon']
  };
  var CLASS = {
    ba: { icon: 'Ba', code: 'BA', cls: 'bar', name: 'Barbarian', sfx: 'barbarian', tree: 'b', fe: 'barbarian',
          fe_files: ['banu1', 'banu2', 'banu3', 'bafw', 'BAFWs', 'babw'] },
    am: { icon: 'Am', code: 'AM', cls: 'ama', name: 'Amazon', sfx: 'amazon', tree: 'a', fe: 'amazon', fe_files: ['AMNU1', 'AMNU2', 'AMNU3', 'AMFW', 'AMFWs', 'AMBW'] },
    so: { icon: 'So', code: 'SO', cls: 'sor', name: 'Sorceress', sfx: 'sorceress', tree: 's', fe: 'sorceress', fe_files: ['SONU1', 'SONU2', 'SONU3', 'SONU3s', 'SOFW', 'SOFWs', 'SOBW', 'SOBWs'] },
    ne: { icon: 'Ne', code: 'NE', cls: 'nec', name: 'Necromancer', sfx: 'necromancer', tree: 'n', fe: 'necromancer', fe_files: ['NENU1', 'NENU2', 'NENU3', 'NENU3s', 'NEFW', 'NEFWs', 'NEBW', 'NEBWs'] },
    pa: { icon: 'Pa', code: 'PA', cls: 'pal', name: 'Paladin', sfx: 'paladin', tree: 'p', fe: 'paladin', fe_files: ['PANU1', 'PANU2', 'PANU3', 'PAFW', 'PAFWs', 'PABW'] },
    as: { icon: 'As', code: 'AI', cls: 'ass', name: 'Assassin', sfx: 'assassin', tree: 'i', fe: 'assassin', fe_files: ['ASNU1', 'ASNU2', 'ASNU3', 'ASFW', 'ASBW'] },
    dz: { icon: 'Dr', code: 'DZ', cls: 'dru', name: 'Druid', sfx: 'druid', tree: 'd', fe: 'druid', fe_files: ['DZNU1', 'DZNU2', 'DZNU3', 'DZFW', 'DZBW'] }
  };
  var ACT_MUSIC = {
    1: ['andarielaction', 'bloodravenresolution', 'caves', 'crypt', 'denofevilaction', 'monastery', 'town1', 'tristram', 'wild'],
    2: ['desert', 'harem', 'horadricaction', 'lair', 'radamentresolution', 'sanctuary', 'sewer', 'taintedsunaction', 'tombs', 'town2', 'valley'],
    3: ['jungle', 'kurast', 'kurastsewer', 'mefdeathaction', 'orbaction', 'spider', 'town3'],
    4: ['diablo', 'diabloaction', 'forgeaction', 'izualaction', 'mesa', 'town4'],
    5: ['baal', 'siege', 'shenkmusic', 'xtown', 'xtemple', 'icecaves', 'nihlathakmusic']
  };

  // ------------------------------------------------------------ 준비: 표·팔레트·언어
  function Ctx(set, log, progress) {
    this.set = set; this.log = log || function () {}; this.progress = progress || function () {};
    this.T = {}; this.pal = {}; this.raw = {};
    this.errors = 0; this.converted = 0;
  }
  Ctx.prototype.init = async function () {
    for (var i = 0; i < EXCEL.length; i++) {
      var nm = EXCEL[i];
      if (this.T[nm]) continue;
      var p = 'data\\global\\excel\\' + nm + '.txt';
      var b = await this.set.read(p);
      if (b) { this.raw[nm] = b; this.T[nm] = D2C.parseTable(b); }
    }
    // obj.txt(맵 배치 번호 → 그림 토큰)는 게임 MPQ에 없으므로 변환기에 내장된 표를 쓴다
    if (!this.T.obj && D2C.OBJ_TXT) {
      this.raw.obj = new TextEncoder().encode(D2C.OBJ_TXT);
      this.T.obj = D2C.parseTable(this.raw.obj);
    }
    if (!this.T.levels || !this.T.monstats) throw new Error('수치표(levels.txt, monstats.txt)를 찾지 못했습니다. d2data.mpq 를 넣었는지 확인하세요. (확장팩판은 d2exp.mpq, patch_d2.mpq 도 필요)');
    for (i = 0; i < PAL_NAMES.length; i++) {
      var pd = await this.set.read('data\\global\\palette\\' + PAL_NAMES[i] + '\\pal.dat');
      if (pd && pd.length >= 768) this.pal[PAL_NAMES[i]] = new D2C.Palette(pd);
    }
    if (!this.pal.units) this.pal.units = this.pal.act1;
    if (!this.pal.act1) throw new Error('팔레트(pal.dat)를 찾지 못했습니다.');
    var use = await this.set.read('data\\local\\use');
    this.lang = (use && LANG[use[0]]) || 'eng';
    this.classic = !this.set.list.some(function (m) { return /d2exp/i.test(m.name); });
    this.log('언어: ' + this.lang + ', 수치표 ' + Object.keys(this.T).length + '개, ' +
      (this.classic ? '오리지널(확장팩 없음)' : '파괴의 군주 포함') + (this.T.monstats2 ? '' : ', 옛 수치표 형식(1.10 이전)'));
    var mh = this.T.monstats.head.join(',');
    if (!/(^|,)Code(,|$)/i.test(mh)) this.log('  ! monstats.txt 에 Code 열이 없습니다: ' + mh.substr(0, 200));
    // 이름 → 행 색인
    this.idx = {
      skills: index(this.T.skills, 'skill'), missiles: index(this.T.missiles, 'Missile'), states: index(this.T.states, 'state'),
      overlay: index(this.T.overlay, 'overlay'), sounds: index(this.T.sounds, 'Sound'), monstats: index(this.T.monstats, 'Id'),
      monstats2: index(this.T.monstats2, 'Id'), monsounds: index(this.T.monsounds, 'Id')
    };
    // 캐릭터 장비 그림 코드 후보
    var v = { lit: 1, med: 1, hvy: 1 };
    ['armor', 'weapons', 'misc'].forEach(function (t) {
      if (!this.T[t]) return;
      this.T[t].rows.forEach(function (r) {
        ['alternategfx', 'code', 'normcode'].forEach(function (c) { var x = col(r, c).toLowerCase(); if (/^[a-z0-9]{3}$/.test(x)) v[x] = 1; });
      });
    }, this);
    this.gfxCodes = Object.keys(v);
  };
  function index(t, key) {
    var m = {}; if (!t) return m;
    t.rows.forEach(function (r) { var k = col(r, key); if (k && !m[k.toLowerCase()]) m[k.toLowerCase()] = r; });
    return m;
  }

  // ------------------------------------------------------------ 변환 도우미
  Ctx.prototype.addRaw = async function (pk, path) {
    if (pk.has(path)) return true;
    var b = await this.set.read(path);
    if (!b) return false;
    pk.add(path, b); return true;
  };
  Ctx.prototype.addGfx = async function (pk, path, pal, opts) {
    if (pk.has(path)) return true;
    if (!this.set.has(path)) return false;
    try {
      var b = await this.set.read(path);
      var isDCC = /\.dcc$/i.test(path);
      pk.add(path, D2C.convertSprite(b, isDCC, pal || this.pal.units, opts));
      this.converted++;
      return true;
    } catch (e) { this.errors++; this.log('  ! ' + path + ' : ' + e.message); return false; }
  };
  Ctx.prototype.addSound = async function (pk, path, rate) {
    if (pk.has(path)) return true;
    if (!this.set.has(path)) return false;
    try {
      var b = await this.set.read(path);
      pk.add(path, D2C.convertWav(b, rate));
      this.converted++;
      return true;
    } catch (e) { this.errors++; this.log('  ! ' + path + ' : ' + e.message); return false; }
  };
  Ctx.prototype.soundPath = function (fn) {
    fn = fn.replace(/\//g, '\\');
    var c = ['data\\global\\sfx\\' + fn, 'data\\local\\sfx\\' + fn, 'data\\local\\sfx\\' + this.lang + '\\' + fn];
    for (var i = 0; i < c.length; i++) if (this.set.has(c[i])) return c[i];
    return null;
  };
  // 유닛(캐릭터/몬스터/오브젝트): COF를 찾고, 각 층의 그림 파일을 변환
  Ctx.prototype.addUnit = async function (pk, base, tok, modes, variants, pal) {
    var found = 0;
    for (var m = 0; m < modes.length; m++) {
      for (var w = 0; w < WCLASS.length; w++) {
        var cofp = base + '\\' + tok + '\\cof\\' + tok + modes[m] + WCLASS[w] + '.cof';
        if (!this.set.has(cofp)) continue;
        var cb = await this.set.read(cofp);
        if (!pk.has(cofp)) pk.add(cofp, cb);
        var cof = D2C.parseCOF(cb); if (!cof) continue;
        for (var l = 0; l < cof.layers.length; l++) {
          var comp = D2C.COMPS[cof.layers[l].comp]; if (!comp) continue;
          var vs = variants[comp] || variants['*'];
          for (var v = 0; v < vs.length; v++) {
            var name = base + '\\' + tok + '\\' + comp + '\\' + tok + comp + vs[v] + modes[m] + cof.layers[l].wclass;
            if (await this.addGfx(pk, name + '.dcc', pal)) found++;
            else if (await this.addGfx(pk, name + '.dc6', pal)) found++;
          }
        }
        this.progress();
      }
    }
    var ps = base + '\\' + tok + '\\cof\\palshift.dat';
    await this.addRaw(pk, ps);
    return found;
  };

  // 참조 따라가기: 스킬/미사일/상태/오버레이/효과음/몬스터
  Ctx.prototype.closure = function (seed) {
    var I = this.idx, seen = {}, q = [], out = { skills: {}, missiles: {}, states: {}, overlay: {}, sounds: {}, monstats: {} };
    function push(type, key) {
      if (!key) return; var k = key.toLowerCase();
      if (!I[type] || !I[type][k] || seen[type + ':' + k]) return;
      seen[type + ':' + k] = 1; out[type][k] = I[type][k]; q.push([type, I[type][k]]);
    }
    Object.keys(seed).forEach(function (t) { seed[t].forEach(function (k) { push(t, k); }); });
    var MONLINK = { minion1: 1, minion2: 1, summon: 1 };
    while (q.length) {
      var it = q.shift(), type = it[0], row = it[1];
      for (var c in row) {
        if (c === '_cells') continue;
        var v = row[c]; if (!v || v.length < 3 || /^-?\d+$/.test(v)) continue;
        var lv = v.toLowerCase();
        if (I.missiles[lv]) push('missiles', v);
        if (I.states[lv]) push('states', v);
        if (I.overlay[lv]) push('overlay', v);
        if (I.sounds[lv]) push('sounds', v);
        if (type === 'monstats' && /^skill\d$/i.test(c)) push('skills', v);
        if (type !== 'skills' || !/^(skill|reqskill\d|passivestate)$/i.test(c)) { if (I.skills[lv] && type !== 'missiles') push('skills', v); }
        if (MONLINK[c.toLowerCase()]) push('monstats', v);
        if (type === 'monstats' && /^(monsound|umonsound)$/i.test(c) && I.monsounds[lv]) {
          var ms = I.monsounds[lv];
          for (var c2 in ms) { if (c2 !== '_cells' && ms[c2] && I.sounds[ms[c2].toLowerCase()]) push('sounds', ms[c2]); }
        }
      }
    }
    return out;
  };
  // 참조 결과로 미사일·오버레이·효과음 그림/소리 넣기
  Ctx.prototype.addRefs = async function (pk, ref, pal, skipSoundPrefix) {
    var k, n = 0;
    for (k in ref.missiles) {
      var cel = col(ref.missiles[k], 'CelFile'); if (!cel) continue;
      if (await this.addGfx(pk, 'data\\global\\missiles\\' + cel + '.dcc', pal) ||
          await this.addGfx(pk, 'data\\global\\missiles\\' + cel + '.dc6', pal)) n++;
    }
    for (k in ref.overlay) {
      var f = col(ref.overlay[k], 'Filename'); if (!f) continue;
      if (await this.addGfx(pk, 'data\\global\\overlays\\' + f + '.dcc', pal) ||
          await this.addGfx(pk, 'data\\global\\overlays\\' + f + '.dc6', pal)) n++;
    }
    for (k in ref.sounds) {
      var fn = col(ref.sounds[k], 'FileName'); if (!fn) continue;
      if (skipSoundPrefix && skipSoundPrefix.test(fn)) continue;
      var sp = this.soundPath(fn);
      if (sp && await this.addSound(pk, sp, 11025)) n++;
    }
    this.progress();
    return n;
  };
  function monVariants(m2, all) {
    var v = { '*': ['lit', 'med', 'hvy', 'nil'] };
    if (!m2 || !D2C.COMPS.some(function (c) { return col(m2, c + 'v') !== ''; })) {
      // 옛 버전(1.10 이전): 변형 정보가 없으므로 무기·방패 칸은 장비 그림 코드 전체로 찾는다
      if (all) { v.RH = all; v.LH = all; v.SH = all; v.HD = all; v['*'] = ['lit', 'med', 'hvy', 'nil', 'hvy']; }
      return v;
    }
    D2C.COMPS.forEach(function (c) {
      var s = col(m2, c + 'v');
      if (s) { var a = s.replace(/"/g, '').split(',').map(function (x) { return x.trim().toLowerCase(); }).filter(Boolean); if (a.length) v[c] = a.concat(['lit']); }
    });
    return v;
  }

  // ------------------------------------------------------------ 공통 팩
  Ctx.prototype.buildCommon = async function () {
    var pk = new D2C.PakWriter('d2_common.pak'), self = this, i, k;
    this.log('[공통 팩] 수치표');
    for (k in this.raw) pk.add('data\\global\\excel\\' + k + '.txt', this.raw[k]);
    this.log('[공통 팩] 팔레트·애니메이션 정보·문자열');
    for (i = 0; i < PAL_NAMES.length; i++) {
      await this.addRaw(pk, 'data\\global\\palette\\' + PAL_NAMES[i] + '\\pal.dat');
      await this.addRaw(pk, 'data\\global\\palette\\' + PAL_NAMES[i] + '\\pal.pl2');
    }
    await this.addRaw(pk, 'data\\global\\animdata.d2');
    await this.addRaw(pk, 'data\\local\\use');
    var langs = [this.lang]; if (this.lang !== 'eng') langs.push('eng');
    for (i = 0; i < langs.length; i++) {
      for (var t of ['string', 'expansionstring', 'patchstring']) await this.addRaw(pk, 'data\\local\\lng\\' + langs[i] + '\\' + t + '.tbl');
    }
    this.log('[공통 팩] 화면 UI 그림');
    for (var dir in UI_FILES) {
      for (i = 0; i < UI_FILES[dir].length; i++) {
        var p = 'data\\global\\ui\\' + dir + '\\' + UI_FILES[dir][i] + '.dc6';
        await this.addGfx(pk, p, this.pal.act1);
      }
    }
    // (listfile)이 있으면 UI 폴더 전체
    var lf = await this.set.read('(listfile)');
    if (lf) {
      var names = String.fromCharCode.apply(null, lf.subarray(0, Math.min(lf.length, 60000))).split(/[\r\n;]+/);
      for (i = 0; i < names.length; i++) if (/^data\\global\\ui\\.*\.dc6$/i.test(names[i])) await this.addGfx(pk, names[i], this.pal.act1);
    }
    this.log('[공통 팩] 아이템 그림');
    var itemFiles = {};
    ['armor', 'weapons', 'misc'].forEach(function (t) {
      if (!self.T[t]) return;
      self.T[t].rows.forEach(function (r) { ['invfile', 'uniqueinvfile', 'setinvfile', 'flippyfile'].forEach(function (c) { var f = col(r, c); if (f) itemFiles[f.toLowerCase()] = 1; }); });
    });
    ['uniqueitems', 'setitems'].forEach(function (t) {
      if (!self.T[t]) return;
      self.T[t].rows.forEach(function (r) { ['invfile', 'flippyfile'].forEach(function (c) { var f = col(r, c); if (f) itemFiles[f.toLowerCase()] = 1; }); });
    });
    var flist = Object.keys(itemFiles);
    for (i = 0; i < flist.length; i++) {
      await this.addGfx(pk, 'data\\global\\items\\' + flist[i] + '.dc6', this.pal.units);
      if (i % 50 === 0) this.progress();
    }
    this.log('[공통 팩] 공용 효과음');
    var skip = /^(monster|npc|amazon|barbarian|necromancer|paladin|sorceress|assassin|druid|skill|act\d|music)\\/i;
    var srows = this.T.sounds ? this.T.sounds.rows : [];
    for (i = 0; i < srows.length; i++) {
      var fn = col(srows[i], 'FileName'); if (!fn || skip.test(fn)) continue;
      var sp = this.soundPath(fn); if (sp) await this.addSound(pk, sp, 11025);
      if (i % 50 === 0) this.progress();
    }
    await this.addSound(pk, 'data\\global\\music\\common\\options.wav', 22050);
    await this.addSound(pk, 'data\\global\\music\\introedit.wav', 22050);
    // 아이템 사용 시 효과 (미사일)
    return pk;
  };

  // ------------------------------------------------------------ 액트 팩
  Ctx.prototype.buildAct = async function (act) {
    var pk = new D2C.PakWriter('d2_act' + act + '.pak'), self = this, i, k;
    var pal = this.pal['act' + act] || this.pal.act1;
    var aPrefix = new RegExp('^act' + act + '\\\\', 'i');
    // 1) 맵 타일 (lvltypes) + 맵 조각 (lvlprest)
    this.log('[액트' + act + ' 팩] 맵 타일');
    var dt1 = {}, ds1 = {};
    (this.T.lvltypes ? this.T.lvltypes.rows : []).forEach(function (r) {
      var a = parseInt(col(r, 'Act'), 10);
      for (var c in r) { if (/^file\s*\d+$/i.test(c) && r[c] && r[c] !== '0') { if (a === act || aPrefix.test(r[c])) dt1[r[c].toLowerCase()] = 1; } }
    });
    (this.T.lvlprest ? this.T.lvlprest.rows : []).forEach(function (r) {
      for (var c in r) { if (/^file\d+$/i.test(c) && r[c] && r[c] !== '0' && aPrefix.test(r[c])) ds1[r[c].toLowerCase()] = 1; }
    });
    var dl = Object.keys(dt1);
    for (i = 0; i < dl.length; i++) {
      var tp = 'data\\global\\tiles\\' + dl[i];
      if (pk.has(tp) || !this.set.has(tp)) continue;
      try { pk.add(tp, D2C.convertDT1(await this.set.read(tp), pal)); this.converted++; }
      catch (e) { this.errors++; this.log('  ! ' + tp + ' : ' + e.message); }
      this.progress();
    }
    var sl = Object.keys(ds1);
    for (i = 0; i < sl.length; i++) await this.addRaw(pk, 'data\\global\\tiles\\' + sl[i]);
    this.log('  타일 ' + dl.length + '종, 맵 조각 ' + sl.length + '개');

    // 2) 몬스터 목록 (levels.txt) + 배치 오브젝트/NPC (obj.txt)
    var mons = {};
    (this.T.levels.rows).forEach(function (r) {
      if (parseInt(col(r, 'Act'), 10) !== act - 1) return;
      for (var c in r) if (/^[nu]?mon\d+$/i.test(c) && r[c]) mons[r[c].toLowerCase()] = 1;
    });
    var objUnits = [];   // {base, tok, variants}
    if (this.T.obj) {
      this.T.obj.rows.forEach(function (r) {
        if (parseInt(col(r, 'Act'), 10) !== act) return;
        var tok = col(r, 'Token'), base = col(r, 'Base').replace(/\//g, '\\').replace(/^\\/, '').toLowerCase();
        if (!tok || !base) return;
        var v = { '*': ['lit', 'med', 'hvy', 'nil'] };
        D2C.COMPS.forEach(function (c) { var x = col(r, c).toLowerCase(); if (x && x !== 'nil' && x !== '0') v[c] = [x, 'lit', 'med', 'hvy']; });
        objUnits.push({ base: base, tok: tok, v: v, monId: col(r, 'monstatsTxtId') });
      });
    }
    // obj.txt 의 몬스터 번호도 몬스터 목록에 추가 (NPC, 보스)
    var monByIdx = this.T.monstats.rows;
    objUnits.forEach(function (u) {
      var n = parseInt(u.monId, 10);
      if (n >= 0 && monByIdx[n]) mons[col(monByIdx[n], 'Id').toLowerCase()] = 1;
    });
    var ref = this.closure({ monstats: Object.keys(mons) });
    this.log('[액트' + act + ' 팩] 몬스터·NPC ' + Object.keys(ref.monstats).length + '종');
    var doneTok = {};
    for (k in ref.monstats) {
      var mr = ref.monstats[k], tok = col(mr, 'Code');
      if (!tok || doneTok['m' + tok.toLowerCase()]) continue;
      doneTok['m' + tok.toLowerCase()] = 1;
      var m2 = this.idx.monstats2[(col(mr, 'MonStatsEx') || col(mr, 'Id')).toLowerCase()] || mr;
      await this.addUnit(pk, 'data\\global\\monsters', tok, MON_MODES, monVariants(m2, this.gfxCodes), this.pal.units);
    }
    this.log('[액트' + act + ' 팩] 배치 오브젝트');
    for (i = 0; i < objUnits.length; i++) {
      var u = objUnits[i], key = u.base + ':' + u.tok.toLowerCase();
      if (doneTok[key]) continue; doneTok[key] = 1;
      var isMon = /monsters$/.test(u.base);
      if (isMon && doneTok['m' + u.tok.toLowerCase()]) continue;
      if (isMon) doneTok['m' + u.tok.toLowerCase()] = 1;
      await this.addUnit(pk, u.base, u.tok, isMon ? MON_MODES : OBJ_MODES, u.v, isMon ? this.pal.units : pal);
    }
    this.log('[액트' + act + ' 팩] 몬스터 스킬 이펙트·효과음');
    await this.addRefs(pk, ref, this.pal.units);
    this.log('[액트' + act + ' 팩] 음악');
    var ml = ACT_MUSIC[act] || [];
    for (i = 0; i < ml.length; i++) { await this.addSound(pk, 'data\\global\\music\\act' + act + '\\' + ml[i] + '.wav', 22050); this.progress(); }
    return pk;
  };

  // ------------------------------------------------------------ 직업 팩
  Ctx.prototype.buildClass = async function (key) {
    var C = CLASS[key], pk = new D2C.PakWriter('d2_' + key + '.pak'), i;
    this.log('[' + C.name + ' 팩] 캐릭터 그림 (장비별)');
    var vars = { '*': this.gfxCodes };
    var n = await this.addUnit(pk, 'data\\global\\chars', C.code, PLR_MODES, vars, this.pal.units);
    this.log('  그림 ' + n + '개');
    if (!n) throw new Error(C.name + ' 그림을 찾지 못했습니다. d2char.mpq 를 넣었는지 확인하세요.');
    this.log('[' + C.name + ' 팩] 스킬');
    var sk = [];
    (this.T.skills ? this.T.skills.rows : []).forEach(function (r) { if (col(r, 'charclass').toLowerCase() === C.cls) sk.push(col(r, 'skill')); });
    var ref = this.closure({ skills: sk });
    this.log('  스킬 ' + sk.length + '개, 미사일 ' + Object.keys(ref.missiles).length + ', 오버레이 ' + Object.keys(ref.overlay).length);
    await this.addRefs(pk, ref, this.pal.units);
    // 소환수
    for (var k in ref.monstats) {
      var tok = col(ref.monstats[k], 'Code'); if (!tok) continue;
      var m2 = this.idx.monstats2[(col(ref.monstats[k], 'MonStatsEx') || k).toLowerCase()] || ref.monstats[k];
      await this.addUnit(pk, 'data\\global\\monsters', tok, MON_MODES, monVariants(m2, this.gfxCodes), this.pal.units);
    }
    this.log('[' + C.name + ' 팩] 목소리·효과음');
    var re = new RegExp('^' + C.sfx + '\\\\', 'i'), srows = this.T.sounds ? this.T.sounds.rows : [];
    for (i = 0; i < srows.length; i++) {
      var fn = col(srows[i], 'FileName'); if (!fn || !re.test(fn)) continue;
      var sp = this.soundPath(fn); if (sp) await this.addSound(pk, sp, 11025);
    }
    this.log('[' + C.name + ' 팩] 스킬 아이콘·선택 화면');
    await this.addGfx(pk, 'data\\global\\ui\\SPELLS\\' + C.icon + 'Skillicon.dc6', this.pal.act1);
    await this.addGfx(pk, 'data\\global\\ui\\SPELLS\\skltree_' + C.tree + '_back.dc6', this.pal.act1);
    for (i = 0; i < C.fe_files.length; i++) await this.addGfx(pk, 'data\\global\\ui\\FrontEnd\\' + C.fe + '\\' + C.fe_files[i] + '.dc6', this.pal.units);
    // 직업 정보 메모
    pk.add('d2m480/class.txt', new TextEncoder().encode('code=' + C.code + '\ncls=' + C.cls + '\nname=' + C.name + '\n'));
    return pk;
  };

  D2C.Ctx = Ctx;
  D2C.CLASS = CLASS;
  D2C.MPQ_ORDER = ['patch_d2', 'd2exp', 'd2xmusic', 'd2xtalk', 'd2xvideo', 'd2data', 'd2char', 'd2sfx', 'd2music', 'd2speech', 'd2video'];
})(typeof window !== 'undefined' ? window : global);
