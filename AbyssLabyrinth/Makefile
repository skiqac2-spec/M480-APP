# 심연의 미궁 - 빌드
#   make wm   : Windows Mobile 6.1 (ARM, cegcc/arm-mingw32ce 필요)
#   make pc   : 윈도우 PC 테스트판 (mingw-w64 필요)
CE_CC  ?= arm-mingw32ce-gcc
PC_CC  ?= i686-w64-mingw32-gcc
SRC    = src/game.c src/plat_win.c

all: wm pc

wm: out/Abyss_WM61.exe
pc: out/Abyss_PC.exe

out/Abyss_WM61.exe: $(SRC) src/game.h
	mkdir -p out
	$(CE_CC) -std=gnu99 -O2 -mcpu=arm1136j-s -D_WIN32_WCE=0x502 -DUNDER_CE -DUNICODE -D_UNICODE \
	  -o $@ $(SRC) -laygshell -static-libgcc
	arm-mingw32ce-strip $@ || true

out/Abyss_PC.exe: $(SRC) src/game.h
	mkdir -p out
	$(PC_CC) -std=gnu99 -O2 -mwindows -static -o $@ $(SRC) -lgdi32 -luser32

clean:
	rm -rf out
