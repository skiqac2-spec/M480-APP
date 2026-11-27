# M480Nico build
# make ce   -> Windows Mobile ARM build (needs /opt/cegcc and ./build_ffmpeg.sh ce)
# make win  -> desktop Win32 test build (needs i686-w64-mingw32 and ./build_ffmpeg.sh win)

WEB := ../M480Web
BEARSSL_SRC := $(wildcard $(WEB)/third_party/bearssl/src/*/*.c)
APP_SRC := src/main.c src/net.c src/url.c src/util.c src/json.c src/yt.c src/player.c src/img.c src/tubelog.c src/ime.c
CE_SRC := src/ce_compat.c
INC := -Isrc -I$(WEB)/third_party -I$(WEB)/third_party/bearssl/inc -I$(WEB)/third_party/bearssl/src
BRDEFS := -DBR_USE_UNIX_TIME=0 -DBR_USE_WIN32_TIME=0 -DBR_USE_URANDOM=0 -DBR_USE_WIN32_RAND=0

CE_CC := /opt/cegcc/bin/arm-mingw32ce-gcc
CE_WINDRES := /opt/cegcc/bin/arm-mingw32ce-windres
CE_FF := ffmpeg-ce
CE_CFLAGS := -O2 -DBUILD_UNIX=$(shell date +%s)UL -march=armv5te -mtune=xscale -D_WIN32_WCE=0x0502 -Icompat -DUNICODE -D_UNICODE $(INC) -I$(CE_FF)/include $(BRDEFS) -Wall -Wno-unused-function -Wno-pointer-sign -Wno-deprecated-declarations
CE_LDFLAGS := -static -Wl,--stack=0x40000 -Wl,--major-subsystem-version=5,--minor-subsystem-version=2 \
	-L$(CE_FF)/lib -lavformat -lavcodec -lavutil -lws2 -laygshell

W_CC := i686-w64-mingw32-gcc
W_FF := ffmpeg-win
W_CFLAGS := -O2 -DBR_AES_X86NI=0 -DBR_SSE2=0 -DBR_PCLMUL=0 -DBR_RDRAND=0 -DUNICODE -D_UNICODE $(INC) -I$(W_FF)/include $(BRDEFS) -Wall -Wno-unused-function -Wno-pointer-sign -Wno-deprecated-declarations
W_LDFLAGS := -static -mwindows -municode -L$(W_FF)/lib -lavformat -lavcodec -lavutil -lws2_32 -lgdi32 -luser32 -lwinmm -lbcrypt

BR_CE_OBJ := $(patsubst $(WEB)/third_party/bearssl/src/%.c,build/ce/br/%.o,$(BEARSSL_SRC))
BR_W_OBJ := $(patsubst $(WEB)/third_party/bearssl/src/%.c,build/win/br/%.o,$(BEARSSL_SRC))
APP_CE_OBJ := $(patsubst src/%.c,build/ce/%.o,$(APP_SRC) $(CE_SRC))
APP_W_OBJ := $(patsubst src/%.c,build/win/%.o,$(APP_SRC))

all: ce

ce: build/M480Nico.exe
win: build/M480Nico-win32.exe

build/ce/br/%.o: $(WEB)/third_party/bearssl/src/%.c
	@mkdir -p $(dir $@)
	$(CE_CC) $(CE_CFLAGS) -c $< -o $@
build/win/br/%.o: $(WEB)/third_party/bearssl/src/%.c
	@mkdir -p $(dir $@)
	$(W_CC) $(W_CFLAGS) -c $< -o $@
build/ce/%.o: src/%.c src/*.h
	@mkdir -p $(dir $@)
	$(CE_CC) $(CE_CFLAGS) -c $< -o $@
build/win/%.o: src/%.c src/*.h
	@mkdir -p $(dir $@)
	$(W_CC) $(W_CFLAGS) -c $< -o $@

build/ce/res.o: res/m480nico.rc res/m480nico.ico
	@mkdir -p build/ce
	$(CE_WINDRES) -I res $< -o $@

build/M480Nico.exe: $(APP_CE_OBJ) $(BR_CE_OBJ) build/ce/res.o
	$(CE_CC) -o $@ $(APP_CE_OBJ) $(BR_CE_OBJ) build/ce/res.o $(CE_LDFLAGS)
	/opt/cegcc/bin/arm-mingw32ce-strip $@

build/M480Nico-win32.exe: $(APP_W_OBJ) $(BR_W_OBJ)
	$(W_CC) -o $@ $^ $(W_LDFLAGS)

clean:
	rm -rf build/ce build/win build/*.exe
