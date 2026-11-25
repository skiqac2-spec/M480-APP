HOST := $(if $(HOST),$(HOST),$(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..))
# run inside the patched gpsp tree
SYS := gba
NEEDCXX := 1
GDEFS = -DHAVE_DYNAREC -DARM_ARCH -DSMALL_TRANSLATION_CACHE -DROM_BUFFER_SIZE=16 -D__LIBRETRO__ -DFRONTEND_SUPPORTS_RGB565 -DINLINE=inline
GINCS = -I$(HOST)/compat -I. -Ilibretro -Ilibretro/libretro-common/include -I$(HOST)
GFLAGS = $(ARCHF) $(CEDEFS) $(GDEFS) $(GINCS) -O2 -Wall -Wno-unused -fomit-frame-pointer -ffast-math -ffunction-sections \
         -fno-strict-aliasing -include $(HOST)/ce_compat.h
COREOBJ = main.o gba_memory.o savestate.o input.o sound.o cheats.o memmap.o serial.o gbp.o rfu.o serial_proto.o \
  libretro/libretro.o gba_cc_lut.o cpu_threaded.o video.o cpu.o bios_data.o arm/arm_stub.o \
  libretro/libretro-common/compat/compat_strl.o ce_filestream.o
include $(HOST)/common.mk

ce_%.o: $(HOST)/ce_%.c
	$(CC) $(GFLAGS) -c $< -o $@
%.o: %.c
	$(CC) $(GFLAGS) -c $< -o $@
%.o: %.cc
	$(CXX) $(GFLAGS) -fno-exceptions -fno-rtti -std=gnu++11 -c $< -o $@
%.o: %.S
	$(CC) $(ARCHF) $(CEDEFS) $(GDEFS) $(GINCS) -E -x assembler-with-cpp $< | sed -e 's/\.size[ \t][^;]*//g' -e '/^[ \t]*\.type[ \t].*%/d' -e 's/\.cfi_[a-z_]*[^;]*//g' > $@.s
	$(CC) $(ARCHF) -c -x assembler $@.s -o $@
