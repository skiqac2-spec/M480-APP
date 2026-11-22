# M480 뮤직 - Windows Mobile 6.x (ARM) 빌드
# 필요: CeGCC (arm-mingw32ce) 툴체인 (PATH에 있거나 /opt/cegcc)
CC      := arm-mingw32ce-gcc
WINDRES := arm-mingw32ce-windres
STRIP   := arm-mingw32ce-strip
CFLAGS  := -O2 -march=armv5te -mtune=arm1136j-s -D_WIN32_WCE=0x0502 -DUNICODE -D_UNICODE -Wall -Wno-unused-function -Isrc/helix
LDFLAGS := -static -Wl,--major-subsystem-version=5,--minor-subsystem-version=2
HELIX   := $(wildcard src/helix/*.c)
SRCS    := src/player.c src/art.c src/source.c src/dsp.c src/fileio.c $(HELIX)

all: M480Music.exe

res.o: res/m480music.rc res/m480music.ico
	$(WINDRES) -I res $< -o $@

M480Music.exe: $(SRCS) src/*.h res.o
	$(CC) $(CFLAGS) -o $@ $(SRCS) res.o $(LDFLAGS)
	$(STRIP) $@

# PC(와인)에서 화면 미리보기: make preview  → M480Music-preview.exe
preview:
	x86_64-w64-mingw32-gcc -O1 -DUNICODE -D_UNICODE -D_WIN32_WCE=0x0502 -DPREVIEW_W=320 -DPREVIEW_H=294 \
	  -DPREVIEW_FONT=L\"NanumGothic\" -Isrc/helix -Itest/preview -include test/preview/ce_stubs.h \
	  -o M480Music-preview.exe $(SRCS) -mwindows -municode -lwinmm

# 리눅스에서 디코더/DSP 검증용
hosttest:
	gcc -O2 -Wall -o hosttest test/host_test.c src/source.c src/dsp.c src/fileio.c $(HELIX) -Isrc/helix

clean:
	rm -f M480Music.exe M480Music-preview.exe hosttest res.o
