# M480Emu — 게임 에뮬레이터 모음 (SCH-M480 / Windows Mobile 6.1)

하나의 메뉴(`Emulator.exe`)에서 게임을 고르면 기종을 알아서 판별해 해당 에뮬레이터(`emu_<기종>.exe`)로 실행합니다.

| 기종 | 엔진 (libretro 코어) | 파일 |
|---|---|---|
| PS1 | PCSX-ReARMed r26 (ARM 동적 재컴파일) | cue/bin, img, iso, pbp |
| GBA | gpSP (ARM 동적 재컴파일) | gba |
| 패미컴 | FCEUmm | nes |
| 슈퍼패미컴 | Snes9x 2002 | smc, sfc |
| 게임보이/컬러 | Gambatte | gb, gbc |
| 메가드라이브·SMS·GG | PicoDrive | md, gen, bin, sms, gg |

- 휴대폰용 완성 파일: `downloads/M480Emu/` (`M480Emu.cab` 설치 파일, `M480Emu.zip` 설치 없이 쓰는 폴더)
- 사용 설명서: `README.txt`
- 화면 터치 버튼: 반투명 십자키·버튼을 게임 화면 위에 겹쳐 표시, 투명도 0~100%·크기 조절 (설정 > 화면 터치 버튼)

## 구조
- `host/` 공용 프론트엔드 (메뉴, 터치 버튼, 오디오 속도 자동 보정, zip, 저장) + 기종별 정의 `host/sys/` + 빌드 규칙 `host/mk/`
- `patches/` 각 코어의 고정 커밋(`*.commit`)과 Windows CE용 수정(`*.patch`)
- `harness/ce_harness.py` 실제 exe를 PC에서 XScale(PXA270) CPU로 돌리는 시험 환경 (스크린샷, 소리, 프로파일, 정렬 오류 검사)

## 빌드
```
./build.sh      # cegcc (/opt/cegcc) 필요. 코어 소스를 받아 패치 후 전부 빌드
./package.sh    # out/M480Emu.cab, out/M480Emu.zip (gcab, zip 필요)
```
Actions 탭에서도 자동 빌드됩니다 (`.github/workflows/m480emu.yml`).

## 라이선스
PCSX-ReARMed·gpSP·FCEUmm·Gambatte: GPL v2. Snes9x·PicoDrive: 비상업적 사용 라이선스 (`licenses/`).
