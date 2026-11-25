HOST := $(if $(HOST),$(HOST),$(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..))
# run inside the gambatte-libretro tree
SYS := gb
NEEDCXX := 1
G := libgambatte/src
BDEFS = -D__LIBRETRO__ -DHAVE_STDINT_H -DHAVE_INTTYPES_H -DVIDEO_RGB565 -DINLINE=inline -DNO_LINK_CABLE
BINCS = -I$(HOST)/compat -I$(G) -Ilibgambatte/include -Icommon -Icommon/resample -Ilibgambatte/libretro -Ilibgambatte/libretro-common/include -I$(HOST)
BFLAGS = $(ARCHF) $(CEDEFS) $(BDEFS) $(BINCS) -O2 -Wall -Wno-unused -fomit-frame-pointer -ffunction-sections \
         -fno-strict-aliasing -include $(HOST)/ce_compat.h
BC := libgambatte/libretro/gambatte_log.c libgambatte/libretro/blipper.c libgambatte/libretro/cc_resampler.c \
      libgambatte/libretro-common/compat/compat_strl.c
BCXX := $(addprefix $(G)/,bootloader.cpp cpu.cpp gambatte.cpp initstate.cpp interrupter.cpp interruptrequester.cpp \
  gambatte-memory.cpp sound.cpp statesaver.cpp tima.cpp video.cpp video_libretro.cpp mem/cartridge.cpp \
  mem/cartridge_libretro.cpp mem/huc3.cpp mem/memptrs.cpp mem/rtc.cpp sound/channel1.cpp sound/channel2.cpp \
  sound/channel3.cpp sound/channel4.cpp sound/duty_unit.cpp sound/envelope_unit.cpp sound/length_counter.cpp \
  video/ly_counter.cpp video/lyc_irq.cpp video/next_m0_time.cpp video/ppu.cpp video/sprite_mapper.cpp) \
  libgambatte/libretro/libretro.cpp
COREOBJ = $(BC:.c=.o) $(BCXX:.cpp=.o) ce_filestream.o ce_retrocommon.o
include $(HOST)/common.mk

ce_%.o: $(HOST)/ce_%.c
	$(CC) $(BFLAGS) -c $< -o $@
%.o: %.c
	$(CC) $(BFLAGS) -c $< -o $@
%.o: %.cpp
	$(CXX) $(BFLAGS) -fno-exceptions -fno-rtti -c $< -o $@
