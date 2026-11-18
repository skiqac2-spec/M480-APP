# M480 뮤직 - Windows Mobile 6.x (ARM) 빌드
# 필요: CeGCC (arm-mingw32ce) 툴체인
CC      := arm-mingw32ce-gcc
CFLAGS  := -O2 -march=armv5te -mtune=arm1136j-s -D_WIN32_WCE=0x0502 -Wall -Wno-unused-function -Isrc/helix
LDFLAGS := -static-libgcc
HELIX   := $(wildcard src/helix/*.c)
SRCS    := src/player.c src/source.c src/dsp.c src/fileio.c $(HELIX)

all: M480Music.exe

M480Music.exe: $(SRCS) src/*.h
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDFLAGS)
	arm-mingw32ce-strip $@

# 리눅스에서 디코더/DSP 검증용
hosttest:
	gcc -O2 -Wall -o hosttest test/host_test.c src/source.c src/dsp.c src/fileio.c $(HELIX) -Isrc/helix

clean:
	rm -f M480Music.exe hosttest
