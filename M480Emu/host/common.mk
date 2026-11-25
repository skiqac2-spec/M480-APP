# Shared build rules: front end + zlib for one system executable (emu_<SYS>.exe)
# A system makefile sets SYS, COREOBJ, SYSCFLAGS, NEEDCXX and includes this file.
CROSS  ?= /opt/cegcc/bin/arm-mingw32ce-
CC     := $(CROSS)gcc
CXX    := $(CROSS)g++
HOST   ?= $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
ZLIB   := $(if $(ZLIB),$(ZLIB),$(abspath $(HOST)/../zlib))
ARCHF  := -march=armv5te -mtune=xscale
CEDEFS := -D_WIN32_WCE=0x0502 -DUNICODE -D_UNICODE -DNDEBUG
OUT    := $(HOST)/out/$(SYS)
EXE    := $(OUT)/$(if $(EXENAME),$(EXENAME),emu_$(SYS)).exe

HOSTSRC := plat_ce ui cfg sysreg ce_compat $(if $(NOCORE),core_none,core_glue)
HOBJ := $(addprefix $(OUT)/,$(addsuffix .o,$(HOSTSRC))) $(OUT)/sys_$(SYS).o
ZOBJ := $(addprefix $(OUT)/z_,adler32.o compress.o crc32.o deflate.o inffast.o inflate.o inftrees.o trees.o uncompr.o zutil.o)
HOSTCFLAGS := $(ARCHF) $(CEDEFS) -O2 -Wall -Wno-unused -I$(HOST) -I$(HOST)/compat -I$(ZLIB) \
              -include $(HOST)/ce_compat.h -finput-charset=UTF-8 -ffunction-sections $(SYSCFLAGS)

all: $(EXE)

$(EXE): $(HOBJ) $(ZOBJ) $(COREOBJ)
	$(if $(NEEDCXX),$(CXX),$(CC)) $(ARCHF) -o $@ $^ -Wl,--gc-sections -Wl,-Map=$@.map -Wl,--stack,0x100000 \
	  -static-libgcc $(if $(NEEDCXX),-static-libstdc++ -static) -laygshell -lm
	$(CROSS)strip -o $(OUT)/stripped.exe $@

$(OUT)/%.o: $(HOST)/%.c $(HOST)/host.h
	@mkdir -p $(OUT)
	$(CC) $(HOSTCFLAGS) -c $< -o $@
$(OUT)/sys_%.o: $(HOST)/sys/sys_%.c $(HOST)/host.h
	@mkdir -p $(OUT)
	$(CC) $(HOSTCFLAGS) -c $< -o $@
$(OUT)/z_%.o: $(ZLIB)/%.c
	@mkdir -p $(OUT)
	$(CC) $(ARCHF) $(CEDEFS) -O2 -I$(HOST)/compat -include $(HOST)/ce_compat.h -c $< -o $@
