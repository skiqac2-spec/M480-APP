HOST := $(if $(HOST),$(HOST),$(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..))
# run inside the picodrive tree
SYS := md
MDEFS = -DEMU_F68K -DHAVE_ZLIB -DREVISION=\"m480\" -D_USE_CZ80 -D__LIBRETRO__ -DINLINE=inline -DNO_MMAP
MINCS = -I$(HOST)/compat -I. -I$(ZLIB) -Iplatform/libretro/libretro-common/include -I$(HOST)
MFLAGS = $(ARCHF) $(CEDEFS) $(MDEFS) $(MINCS) -O2 -Wall -Wno-unused -fomit-frame-pointer -ffunction-sections \
         -fno-strict-aliasing -ffast-math -include $(HOST)/ce_compat.h
MSRC := platform/libretro/libretro.c platform/libretro/libretro-common/compat/compat_strcasestr.c \
  platform/libretro/libretro-common/compat/compat_strl.c platform/common/mp3.c platform/common/mp3_sync.c platform/common/mp3_drmp3.c \
  unzip/unzip.c pico/pico.c pico/cart.c pico/memory.c pico/state.c pico/sek.c pico/z80if.c pico/videoport.c \
  pico/draw2.c pico/draw.c pico/mode4.c pico/misc.c pico/eeprom.c pico/patch.c pico/debug.c pico/media.c pico/sms.c pico/cd/mcd.c \
  pico/cd/memory.c pico/cd/sek.c pico/cd/cdc.c pico/cd/cdd.c pico/cd/cd_image.c pico/cd/cd_parse.c pico/cd/gfx.c pico/cd/gfx_dma.c \
  pico/cd/misc.c pico/cd/pcm.c pico/cd/megasd.c pico/pico/pico.c pico/pico/memory.c pico/pico/xpcm.c pico/carthw/carthw.c \
  pico/carthw/eeprom_spi.c pico/carthw/svp/svp.c pico/carthw/svp/memory.c pico/carthw/svp/ssp16.c pico/sound/sound.c \
  pico/sound/resampler.c pico/sound/sn76496.c pico/sound/ym2612.c pico/sound/ym2413.c pico/sound/vgm.c pico/sound/mix.c \
  cpu/fame/famec.c cpu/cz80/cz80.c cpu/drc/cmn.c \
  pico/32x/32x.c pico/32x/memory.c pico/32x/draw.c pico/32x/sh2soc.c pico/32x/pwm.c cpu/sh2/sh2.c cpu/sh2/mame/sh2pico.c
COREOBJ = $(MSRC:.c=.o) ce_filestream.o ce_retrocommon.o ce_md_stubs.o
include $(HOST)/common.mk

ce_%.o: $(HOST)/ce_%.c
	$(CC) $(MFLAGS) -c $< -o $@
%.o: %.c
	$(CC) $(MFLAGS) -c $< -o $@
