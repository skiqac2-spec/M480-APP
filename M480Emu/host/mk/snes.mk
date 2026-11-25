HOST := $(if $(HOST),$(HOST),$(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..))
# run inside the snes9x2002 tree
SYS := snes
C := src
SDEFS = -DARM_ASM -DRIGHTSHIFT_IS_SAR -DHAVE_STRINGS_H -DHAVE_STDINT_H -DHAVE_INTTYPES_H -D__LIBRETRO__ -DUSE_SA1 -DINLINE=inline -DLSB_FIRST -DFAST_ALIGNED_LSB_WORD_ACCESS
SINCS = -I$(HOST)/compat -I. -Ilibretro -Ilibretro/libretro-common/include -I$(C) -I$(HOST)
SFLAGS = $(ARCHF) $(CEDEFS) $(SDEFS) $(SINCS) -O2 -Wall -Wno-unused -fomit-frame-pointer -ffunction-sections \
         -finline -fsigned-char -ffast-math -fno-strict-aliasing -include $(HOST)/ce_compat.h
SSRC := $(addprefix $(C)/,apu.c apuaux.c c4.c c4emu.c cheats.c cheats2.c clip.c data.c dsp1.c fxemu.c fxinst.c globals.c dma.c \
  memmap.c cpu.c cpuexec.c cpuops.c sa1.c sa1cpu.c sdd1.c sdd1emu.c snapshot.c soundux.c spc700.c srtc.c \
  ppu.c rops.c mode7new.c mode7.c mode7add.c mode7add1_2.c mode7sub.c mode7sub1_2.c mode7prio.c mode7addprio.c \
  mode7add1_2prio.c mode7subprio.c mode7sub1_2prio.c gfx16.c tile16.c tile16add.c tile16add1_2.c tile16fadd1_2.c \
  tile16sub.c tile16sub1_2.c tile16fsub1_2.c) libretro/libretro.c libretro/libretro-common/streams/memory_stream.c
COREOBJ = $(SSRC:.c=.o) ce_retrocommon.o
include $(HOST)/common.mk

ce_%.o: $(HOST)/ce_%.c
	$(CC) $(SFLAGS) -c $< -o $@
%.o: %.c
	$(CC) $(SFLAGS) -c $< -o $@
