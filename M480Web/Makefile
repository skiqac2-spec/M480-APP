# M480Web build
# make ce   -> ARM Windows Mobile build (needs arm-mingw32ce toolchain in /opt/cegcc)
# make win  -> desktop Win32 test build (i686-w64-mingw32), runnable under wine

BEARSSL_SRC := $(wildcard third_party/bearssl/src/*/*.c)
APP_SRC := src/main.c src/net.c src/html.c src/layout.c src/img.c src/url.c src/util.c
INC := -Isrc -Ithird_party -Ithird_party/bearssl/inc -Ithird_party/bearssl/src
BRDEFS := -DBR_USE_UNIX_TIME=0 -DBR_USE_WIN32_TIME=0 -DBR_USE_URANDOM=0 -DBR_USE_WIN32_RAND=0

CE_CC := /opt/cegcc/bin/arm-mingw32ce-gcc
CE_WINDRES := /opt/cegcc/bin/arm-mingw32ce-windres
CE_CFLAGS := -O2 -march=armv5te -mtune=arm1136j-s -D_WIN32_WCE=0x0502 -DUNICODE -D_UNICODE $(INC) $(BRDEFS) -Wall -Wno-unused-function -Wno-pointer-sign
CE_LDFLAGS := -static -Wl,--stack=0x40000 -Wl,--major-subsystem-version=5,--minor-subsystem-version=2 -lws2

W_CC := i686-w64-mingw32-gcc
W_CFLAGS := -O2 $(WEXTRA) -DBR_AES_X86NI=0 -DBR_SSE2=0 -DBR_PCLMUL=0 -DBR_RDRAND=0 -DUNICODE -D_UNICODE $(INC) $(BRDEFS) -Wall -Wno-unused-function -Wno-pointer-sign
W_LDFLAGS := -static -mwindows -municode -lws2_32 -lgdi32 -luser32

BR_CE_OBJ := $(patsubst third_party/bearssl/src/%.c,build/ce/br/%.o,$(BEARSSL_SRC))
BR_W_OBJ := $(patsubst third_party/bearssl/src/%.c,build/win/br/%.o,$(BEARSSL_SRC))
APP_CE_OBJ := $(patsubst src/%.c,build/ce/%.o,$(APP_SRC))
APP_W_OBJ := $(patsubst src/%.c,build/win/%.o,$(APP_SRC))

all: ce

ce: build/M480Web.exe
win: build/M480Web-win32.exe

build/ce/br/%.o: third_party/bearssl/src/%.c
	@mkdir -p $(dir $@)
	$(CE_CC) $(CE_CFLAGS) -c $< -o $@
build/win/br/%.o: third_party/bearssl/src/%.c
	@mkdir -p $(dir $@)
	$(W_CC) $(W_CFLAGS) -c $< -o $@
build/ce/%.o: src/%.c src/*.h
	@mkdir -p $(dir $@)
	$(CE_CC) $(CE_CFLAGS) -c $< -o $@
build/win/%.o: src/%.c src/*.h
	@mkdir -p $(dir $@)
	$(W_CC) $(W_CFLAGS) -c $< -o $@

build/ce/res.o: res/m480web.rc res/m480web.ico
	@mkdir -p build/ce
	$(CE_WINDRES) -I res $< -o $@

build/M480Web.exe: $(APP_CE_OBJ) $(BR_CE_OBJ) build/ce/res.o
	$(CE_CC) -o $@ $^ $(CE_LDFLAGS)
	/opt/cegcc/bin/arm-mingw32ce-strip $@

build/M480Web-win32.exe: $(APP_W_OBJ) $(BR_W_OBJ)
	$(W_CC) -o $@ $^ $(W_LDFLAGS)

clean:
	rm -rf build
