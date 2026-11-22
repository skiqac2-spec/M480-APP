#!/usr/bin/env python3
"""converter/converter.html + js/*.js -> converter/D2M480_Converter.html (파일 하나로 합침)"""
import os
here = os.path.dirname(os.path.abspath(__file__))
conv = os.path.join(here, '..', 'converter')
import json
objtxt = open(os.path.join(conv, 'data', 'obj.txt'), encoding='utf-8').read()
objjs = ('// 맵 배치 번호 → 유닛 토큰 표 (출처: OpenDiablo2 프로젝트의 object lookup 표 / 커뮤니티 정리 자료)\n'
         '(function (root) { var D2C = root.D2C = root.D2C || {}; D2C.OBJ_TXT = ' + json.dumps(objtxt) + '; })'
         "(typeof window !== 'undefined' ? window : global);\n")
open(os.path.join(conv, 'js', 'objdata.js'), 'w', encoding='utf-8').write(objjs)
order = ['compress', 'mpq', 'gfx', 'sound', 'pack', 'objdata', 'builder']
js = '\n'.join(open(os.path.join(conv, 'js', m + '.js'), encoding='utf-8').read() for m in order)
html = open(os.path.join(conv, 'converter.html'), encoding='utf-8').read()
assert '<!--SCRIPTS-->' in html
out = html.replace('<!--SCRIPTS-->', '<script>\n' + js.replace('</script', '<\\/script') + '\n</script>')
dst = os.path.join(conv, 'D2M480_Converter.html')
open(dst, 'w', encoding='utf-8').write(out)
print(dst, len(out), 'bytes')
