HOST := $(if $(HOST),$(HOST),$(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..))
# run inside the patched pcsx_rearmed tree: make -f .../mk/ps1.mk
SYS := ps1
SYSCFLAGS := -Ilibpcsxcore -Iinclude
PDEFS  = -DNO_FRONTEND -DHAVE_LIBRETRO -DGPU_UNAI -DUSE_GPULIB=1 -DGPU_UNAI_USE_INT_DIV_MULTINV -DREARMED \
         -DP_HAVE_MMAP=0 -DP_HAVE_POSIX_MEMALIGN=0 -DDISABLE_MEM_LUTS=0 -DGIT_VERSION=\"-m480\" -DNO_DYLIB -DNO_SOCKET
PINCS  = -I$(HOST)/compat -Iinclude -Ideps/libretro-common/include -I$(ZLIB) -Ilibpcsxcore -I$(HOST)
PCOMMON = $(ARCHF) $(CEDEFS) $(PDEFS) $(PINCS) -Wall -Wno-unused -ffast-math -fsingle-precision-constant \
         -ffunction-sections -fno-strict-aliasing
PCFLAGS   = $(PCOMMON) -O2 -include $(HOST)/ce_compat.h
PCXXFLAGS = $(PCOMMON) -O3 -include $(HOST)/ce_compat.h -fno-exceptions -fno-rtti
COREOBJ = libpcsxcore/cdriso.o libpcsxcore/cdrom.o libpcsxcore/cdrom-async.o libpcsxcore/cheat.o \
  libpcsxcore/database.o libpcsxcore/decode_xa.o libpcsxcore/mdec.o libpcsxcore/misc.o \
  libpcsxcore/plugins.o libpcsxcore/ppf.o libpcsxcore/psxbios.o libpcsxcore/psxcommon.o \
  libpcsxcore/psxcounters.o libpcsxcore/psxdma.o libpcsxcore/psxhw.o libpcsxcore/psxinterpreter.o \
  libpcsxcore/psxmem.o libpcsxcore/psxevents.o libpcsxcore/r3000a.o libpcsxcore/sio.o \
  libpcsxcore/spu.o libpcsxcore/gpu.o libpcsxcore/pad.o libpcsxcore/gte.o libpcsxcore/gte_nf.o \
  libpcsxcore/gte_divider.o libpcsxcore/gte_arm.o \
  libpcsxcore/new_dynarec/new_dynarec.o libpcsxcore/new_dynarec/pcsxmem.o \
  libpcsxcore/new_dynarec/linkage_arm.o libpcsxcore/new_dynarec/emu_if.o \
  plugins/dfsound/dma.o plugins/dfsound/freeze.o plugins/dfsound/registers.o plugins/dfsound/spu.o \
  plugins/dfsound/out.o plugins/dfsound/nullsnd.o plugins/dfsound/arm_utils.o \
  plugins/gpulib/gpu.o plugins/gpulib/vout_pl.o plugins/gpulib/prim.o \
  plugins/gpu_unai/gpulib_if.o plugins/gpu_unai/gpu_arm.o plugins/gpu_unai/old/if.o \
  frontend/cspace.o frontend/cspace_arm.o frontend/libretro.o frontend/main.o frontend/plugin.o
include $(HOST)/common.mk

libpcsxcore/new_dynarec/%.o: PCFLAGS += -DBASE_ADDR_DYNAMIC=1
frontend/cspace.o: PCFLAGS += -DHAVE_bgr555_to_rgb565
%.o: %.c
	$(CC) $(PCFLAGS) -c $< -o $@
%.o: %.cpp
	$(CXX) $(PCXXFLAGS) -c $< -o $@
%.o: %.S
	$(CC) $(ARCHF) $(CEDEFS) $(PDEFS) $(PINCS) -E -x assembler-with-cpp $< | sed -e '/^[ \t]*\.size[ \t]/d' -e '/^[ \t]*\.type[ \t].*%/d' -e 's/\.cfi_[a-z_]*[^;]*//g' > $@.s
	$(CC) $(ARCHF) -c -x assembler $@.s -o $@
