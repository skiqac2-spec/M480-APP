# M480 홈 - Windows Mobile 6.x (ARM) 빌드
# 필요: CeGCC (arm-mingw32ce) 툴체인 (PATH에 있거나 /opt/cegcc)
CC      := arm-mingw32ce-gcc
WINDRES := arm-mingw32ce-windres
STRIP   := arm-mingw32ce-strip
CFLAGS  := -O2 -march=armv5te -mtune=arm1136j-s -D_WIN32_WCE=0x0502 -DUNICODE -D_UNICODE -Wall -Wno-unused-function
LDFLAGS := -static -Wl,--major-subsystem-version=5,--minor-subsystem-version=2

all: M480Home.exe

res.o: res/m480home.rc res/m480home.ico
	$(WINDRES) -I res $< -o $@

M480Home.exe: src/home.c res.o
	$(CC) $(CFLAGS) -o $@ src/home.c res.o $(LDFLAGS)
	$(STRIP) $@

# PC(와인)에서 화면 미리보기: make preview SCR=0  (0 홈, 1 전체 앱, 2 실행 중, 3 설정, 9 꾹 누름 메뉴)
SCR ?= 0
preview:
	x86_64-w64-mingw32-gcc -O1 -DUNICODE -D_UNICODE -DPREVIEW_W=320 -DPREVIEW_H=294 -DPREVIEW_SCR=$(SCR) \
	  -DPREVIEW_FONT=L\"NanumGothic\" -include test/preview/ce_stubs.h \
	  -o M480Home-preview.exe src/home.c -mwindows -municode -lwinmm

clean:
	rm -f M480Home.exe M480Home-preview.exe res.o
