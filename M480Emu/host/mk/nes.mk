HOST := $(if $(HOST),$(HOST),$(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..))
# run inside the fceumm tree
SYS := nes
S := src
NDEFS = -D__LIBRETRO__ -DPATH_MAX=1024 -DFCEU_VERSION_NUMERIC=9900 -DFRONTEND_SUPPORTS_RGB565 -DINLINE=inline -DHAVE_ASPRINTF=0 -DLSB_FIRST
NINCS = -I$(HOST)/compat -I$(S)/drivers/libretro -I$(S)/drivers/libretro/libretro-common/include -I$(S) -I$(S)/input -I$(S)/boards -I$(HOST)
NFLAGS = $(ARCHF) $(CEDEFS) $(NDEFS) $(NINCS) -O3 -Wall -Wno-unused -fomit-frame-pointer -ffunction-sections \
         -fno-strict-aliasing -fsigned-char -include $(HOST)/ce_compat.h
NSRC := $(wildcard $(S)/boards/*.c) $(wildcard $(S)/input/*.c) \
  $(addprefix $(S)/,drivers/libretro/libretro.c drivers/libretro/libretro_dipswitch.c cart.c cheat.c crc32.c fceu-endian.c \
  fceu-memory.c fceu.c fds.c fds_apu.c file.c filter.c general.c input.c md5.c nsf.c palette.c ppu.c sound.c state.c \
  video.c vsuni.c ines.c unif.c x6502.c) $(EXTRA_SRC)
COREOBJ = $(NSRC:.c=.o) $(S)/drivers/libretro/libretro-common/compat/compat_strl.o ce_filestream.o ce_retrocommon.o
include $(HOST)/common.mk

ce_%.o: $(HOST)/ce_%.c
	$(CC) $(NFLAGS) -c $< -o $@
%.o: %.c
	$(CC) $(NFLAGS) -c $< -o $@
