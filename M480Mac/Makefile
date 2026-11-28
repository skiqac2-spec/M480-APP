# M480Mac - Mini vMac (Mac II, 640x480, 256 colors) for SCH-M480 / Windows Mobile 6.1
# make        -> build/M480Mac.exe (needs arm-mingw32ce toolchain in /opt/cegcc)
CROSS   ?= /opt/cegcc/bin/arm-mingw32ce-
CC      := $(CROSS)gcc
WINDRES := $(CROSS)windres
STRIP   := $(CROSS)strip

ARCHF   := -march=armv5te -mtune=xscale
CEDEFS  := -D_WIN32_WCE=0x0502 -DUNICODE -D_UNICODE
CFLAGS  := $(ARCHF) $(CEDEFS) -O2 -fomit-frame-pointer -Wall -Wno-unused -Wno-pointer-sign \
           -finput-charset=UTF-8 -Icfg -Isrc

SRCS := MINEM68K OSGLUWIN GLOBGLUE M68KITAB VIAEMDEV VIA2EMDV IWMEMDEV SCCEMDEV RTCEMDEV \
        ROMEMDEV SCSIEMDV SONYEMDV SCRNEMDV VIDEMDEV MOUSEMDV ADBEMDEV ASCEMDEV PROGMAIN
OBJS := $(addprefix build/,$(addsuffix .o,$(SRCS))) build/res.o

all: build/M480Mac.exe

build/%.o: src/%.c src/*.h cfg/*.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/res.o: cfg/main.rc
	@mkdir -p build
	$(WINDRES) --include-dir src -i cfg/main.rc -o $@

build/M480Mac.exe: $(OBJS)
	$(CC) $(ARCHF) -o $@ $(OBJS) -Wl,--stack,0x40000 -static-libgcc \
	  -lcommctrl -laygshell -lm
	cp $@ build/M480Mac-sym.exe
	$(STRIP) $@

clean:
	rm -rf build
