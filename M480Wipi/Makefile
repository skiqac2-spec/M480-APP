# M480 WIPI emulator
#   make        : Windows Mobile 6.1 exe (cegcc / arm-mingw32ce)
#   make rt     : rebuild the Java runtime library (needs javac) -> src/rt_jar.c
#   make host   : PC test build (Linux, headless)
CE_CC ?= arm-mingw32ce-gcc
CORE = src/class.c src/heap.c src/interp.c src/native.c src/str.c src/zip.c src/gfx.c src/image.c \
       src/audio.c src/api.c src/pkg.c src/app.c src/ui.c src/rt_jar.c
CE_SRC = $(CORE) src/pal_ce.c src/main_ce.c

.PHONY: all rt host clean
all: out/M480Wipi.exe

out/M480Wipi.exe: $(CE_SRC) src/*.h
	mkdir -p out
	$(CE_CC) -std=gnu99 -O2 -march=armv5te -mtune=arm1136j-s -fno-strict-aliasing -D_WIN32_WCE=0x502 -DUNDER_CE -DUNICODE -D_UNICODE \
	  -Wall -Wno-unused-function -o $@ $(CE_SRC) -laygshell -lmmtimer -static-libgcc -lm
	arm-mingw32ce-strip $@ || true

rt:
	rm -rf build/rt && mkdir -p build/rt
	javac -nowarn -source 8 -target 8 -bootclasspath build/rt -sourcepath rt -d build/rt -encoding UTF-8 $$(find rt -name '*.java')
	python3 mkrt.py build/rt src/rt_jar.c

host:
	mkdir -p build && gcc -O1 -g -Wall -Wno-unused-function -o build/m480host $(filter-out src/ui.c,$(CORE)) src/pal_host.c src/font_host.c test/main_host.c -lm

clean:
	rm -rf out build
