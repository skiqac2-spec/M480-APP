# M480 배터리 진단 - Windows Mobile 6.x (ARM) 빌드
# 필요: CeGCC (arm-mingw32ce) 툴체인 (PATH에 있거나 /opt/cegcc)
CC      := arm-mingw32ce-gcc
WINDRES := arm-mingw32ce-windres
STRIP   := arm-mingw32ce-strip
CFLAGS  := -O2 -march=armv5te -mtune=arm1136j-s -D_WIN32_WCE=0x0502 -DUNICODE -D_UNICODE -Wall -Wno-unused-function
LDFLAGS := -static -Wl,--major-subsystem-version=5,--minor-subsystem-version=2

all: M480Batt.exe

res.o: res/m480batt.rc res/m480batt.ico
	$(WINDRES) -I res $< -o $@

M480Batt.exe: src/batt.c res.o
	$(CC) $(CFLAGS) -o $@ src/batt.c res.o $(LDFLAGS)
	$(STRIP) $@

# PC(와인)에서 화면 미리보기: make preview SCR=0 CRASH=1  (SCR 0 처음, 1 기록 / CRASH 0 없음, 1~6 판정)
SCR ?= 0
CRASH ?= 0
preview:
	x86_64-w64-mingw32-gcc -O1 -Wall -DUNICODE -D_UNICODE -DPREVIEW_W=320 -DPREVIEW_H=294 -DPREVIEW_SCR=$(SCR) \
	  $(if $(filter-out 0,$(CRASH)),-DPREVIEW_CRASH=$(CRASH)) \
	  -DPREVIEW_FONT=L\"NanumGothic\" -include test/preview/ce_stubs.h \
	  -o M480Batt-preview.exe src/batt.c -mwindows -municode

clean:
	rm -f M480Batt.exe M480Batt-preview.exe res.o
