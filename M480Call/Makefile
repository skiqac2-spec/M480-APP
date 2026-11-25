# M480 통화 - Windows Mobile 6.x (ARM) 빌드
# 필요: CeGCC (arm-mingw32ce) 툴체인 (PATH에 있거나 /opt/cegcc)
CC      := arm-mingw32ce-gcc
WINDRES := arm-mingw32ce-windres
STRIP   := arm-mingw32ce-strip
CFLAGS  := -O2 -march=armv5te -mtune=arm1136j-s -D_WIN32_WCE=0x0502 -DUNICODE -D_UNICODE -Wall -Wno-unused-function
LDFLAGS := -static -Wl,--major-subsystem-version=5,--minor-subsystem-version=2 -lws2

all: M480Call.exe

res.o: res/m480call.rc res/m480call.ico
	$(WINDRES) -I res $< -o $@

M480Call.exe: src/call.c res.o
	$(CC) $(CFLAGS) -o $@ src/call.c res.o $(LDFLAGS)
	$(STRIP) $@

clean:
	rm -f M480Call.exe res.o
