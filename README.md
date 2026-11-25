# M480-APP

삼성 SCH-M480 (Windows Mobile 6.1)용 앱 모음.

## 휴대폰에서 받기
M480Web 브라우저의 **내 파일함**(홈 화면 또는 메뉴 ≡)에서 이 저장소의 파일을 바로 받고 설치할 수 있습니다.
목록은 `downloads/files.txt` 에 있습니다.

| 폴더 | 내용 |
|---|---|
| `M480Web/` | TLS 1.2 웹 브라우저 소스 (BearSSL 내장) |
| `AbyssLabyrinth/` | 심연의 미궁 (1인칭 던전 RPG) 소스·데이터 |
| `M480Tube/` | 유튜브 뷰어 (240p, FFmpeg h264/aac 디코딩) 소스 + 원격 연결 설정 `m480tube_config.txt` |
| `M480Music/` | 안 깨지는 뮤직 플레이어 소스 (MP3/WAV, 22kHz 출력·리미터·자동 음질 하향) |
| `D2M480/` | 디아블로2 이식판 (정품 데이터 사용) — PC용 데이터 변환기 `D2M480/converter/D2M480_Converter.html`, 엔진은 다음 단계 |
| `M480Comic/` | 만화/이미지 뷰어 소스 (주소 입력 → 이미지만 화면 맞춤, 인증서 오류 무시, WebP 지원, 네트워크는 M480Web 코드 공유) |
| `M480Emu/` | 게임 에뮬레이터 모음 (PS1·GBA·패미컴·슈퍼패미컴·게임보이·메가드라이브), 반투명 화면 터치 버튼 |
| `M480Call/` | 블루투스 핸즈프리 통화 앱 (폰에 연결해 M480으로 걸기·받기·끊기, 0.1 테스트판) |
| `M480Home/` | 홈 화면 런처 (큰 아이콘·시계·배터리, 전체 앱, 실행 중 앱 닫기, 오늘 화면 대신) |
| `downloads/` | 휴대폰용 완성 파일 + 파일 목록(files.txt) |
| `tools/update_manifest.py` | `downloads/files.in` → `files.txt` (크기 자동 기입) |

> 저장소가 **비공개**이면 휴대폰에서 받을 때 GitHub 토큰이 필요합니다
> (M480Web > 내 파일함 > 파일함 설정, 또는 `github_token.txt`).

## 빌드
- M480Web: `cd M480Web && make ce` (cegcc `/opt/cegcc` 필요), PC 테스트판 `make win`
- AbyssLabyrinth: Actions 탭의 빌드 결과 또는 `make wm`
- M480Tube: Actions 탭의 빌드 결과, 또는 `./build_ffmpeg.sh ce && make ce` (PC 테스트판 `./build_ffmpeg.sh win && make win`)
- M480Music: `cd M480Music && make` (cegcc 필요) — Actions 탭에서 exe·cab 자동 빌드
- M480Emu: `cd M480Emu && ./build.sh && ./package.sh` (cegcc 필요, 코어 소스 자동 다운로드) — Actions 탭에서 cab·zip 자동 빌드
- M480Home: `cd M480Home && make` (cegcc 필요) — Actions 탭에서 exe·cab 자동 빌드
- M480Call: `cd M480Call && make` (cegcc 필요) — Actions 탭에서 exe·cab 자동 빌드
- M480Comic: `cd M480Comic && make ce` (cegcc 필요, `../M480Web` 소스 사용), PC 테스트판 `make win`
