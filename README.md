# M480-APP

삼성 SCH-M480 (Windows Mobile 6.1)용 앱 모음.

## 휴대폰에서 받기
M480Web 브라우저의 **내 파일함**(홈 화면 또는 메뉴 ≡)에서 이 저장소의 파일을 바로 받고 설치할 수 있습니다.
목록은 `downloads/files.txt` 에 있습니다.

| 폴더 | 내용 |
|---|---|
| `M480Web/` | TLS 1.2 웹 브라우저 소스 (BearSSL 내장) |
| `AbyssLabyrinth/` | 심연의 미궁 (1인칭 던전 RPG) 소스·데이터 |
| `downloads/` | 휴대폰용 완성 파일 + 파일 목록(files.txt) |
| `tools/update_manifest.py` | `downloads/files.in` → `files.txt` (크기 자동 기입) |

> 저장소가 **비공개**이면 휴대폰에서 받을 때 GitHub 토큰이 필요합니다
> (M480Web > 내 파일함 > 파일함 설정, 또는 `github_token.txt`).

## 빌드
- M480Web: `cd M480Web && make ce` (cegcc `/opt/cegcc` 필요), PC 테스트판 `make win`
- AbyssLabyrinth: Actions 탭의 빌드 결과 또는 `make wm`
