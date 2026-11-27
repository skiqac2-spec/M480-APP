#!/bin/sh
# Builds a minimal static FFmpeg (mov demuxer, h264 + aac_fixed decoders).
# usage: build_ffmpeg.sh ce|win  -> installs into ./ffmpeg-<target>
set -e
TARGET=${1:-ce}
FFVER=4.4.5
HERE=$(cd "$(dirname "$0")" && pwd)
PREFIX=$HERE/ffmpeg-$TARGET
SRC=$HERE/build/ffmpeg-$FFVER
[ -f "$PREFIX/lib/libavcodec.a" ] && { echo "ffmpeg-$TARGET already built"; exit 0; }
mkdir -p "$HERE/build"
if [ ! -d "$SRC" ]; then
  cd "$HERE/build"
  curl -sSL "https://ffmpeg.org/releases/ffmpeg-$FFVER.tar.xz" -o ff.tar.xz || \
  curl -sSL "https://github.com/FFmpeg/FFmpeg/archive/refs/tags/n$FFVER.tar.gz" -o ff.tar.gz
  if [ -f ff.tar.xz ]; then tar xJf ff.tar.xz; else tar xzf ff.tar.gz && mv FFmpeg-n$FFVER ffmpeg-$FFVER; fi
fi
cd "$SRC"
# Windows CE (PE/COFF) objects cannot align data to more than 8 bytes
for h in libavutil/mem_internal.h libavutil/mem.h; do
if ! grep -q "_WIN32_WCE" $h; then
  sed -i 's/^#elif defined(__DJGPP__)$/#elif defined(_WIN32_WCE)\n    #define DECLARE_ALIGNED(n,t,v)      t __attribute__ ((aligned (FFMIN(n, 8)))) v\n    #define DECLARE_ASM_ALIGNED(n,t,v)  t av_used __attribute__ ((aligned (FFMIN(n, 8)))) v\n    #define DECLARE_ASM_CONST(n,t,v)    static const t av_used __attribute__ ((aligned (FFMIN(n, 8)))) v\n#elif defined(__DJGPP__)/' $h
fi
done
make distclean >/dev/null 2>&1 || true
COMMON="--prefix=$PREFIX --enable-cross-compile --target-os=none
 --disable-everything --disable-programs --disable-doc --disable-avdevice --disable-swresample
 --disable-swscale --disable-postproc --disable-avfilter --disable-network
 --disable-pthreads --disable-w32threads --disable-os2threads --disable-autodetect
 --disable-iconv --disable-zlib --disable-bzlib --disable-lzma --disable-debug
 --enable-decoder=h264 --enable-decoder=aac_fixed
 --enable-demuxer=mov --enable-parser=h264 --enable-parser=aac
 --enable-runtime-cpudetect"
if [ "$TARGET" = ce ]; then
  # SCH-M480 = Marvell PXA (XScale, ARMv5TE, no VFP)
  ./configure $COMMON --cross-prefix=arm-mingw32ce- --arch=arm --cpu=xscale \
    --disable-vfp --disable-neon --disable-vfpv3 --disable-armv6 --disable-armv6t2 --enable-armv5te --disable-thumb \
    --extra-cflags="-D_WIN32_WCE=0x0502 -DWINCE -I$HERE/compat -include $HERE/compat/ce_compat.h" \
    || { tail -60 ffbuild/config.log; exit 1; }
  # math functions that the CE headers declare but configure could not link:
  # mark them present (implemented in compat/ce_math.c) to avoid conflicting static fallbacks
  for f in $(grep -E '^#define HAVE_[A-Z0-9]+ 0' config.h | sed -E 's/#define HAVE_([A-Z0-9]+) 0/\1/'); do
    lf=$(echo "$f" | tr 'A-Z' 'a-z')
    if grep -q "libm.h" /dev/null; then :; fi
    if grep -Eq "^#if !HAVE_$f\b" libavutil/libm.h && grep -Eqw "$lf" /opt/cegcc/arm-mingw32ce/include/math.h; then
      echo "math: $lf declared but missing -> compat"
      sed -i "s/#define HAVE_$f 0/#define HAVE_$f 1/" config.h
    fi
  done
  grep -E "HAVE_(COPYSIGN|CBRT|CBRTF|ERF|EXP2|EXP2F|HYPOT|LLRINT|LLRINTF|LOG2|LOG2F|LRINT|LRINTF|RINT|ROUND|ROUNDF|TRUNC|TRUNCF|ISINF|ISNAN|ISFINITE|ATANF|ATAN2F|POWF|SINF|COSF|EXPF|LDEXPF|LOG10F) " config.h
else
  ./configure $COMMON --cross-prefix=i686-w64-mingw32- --arch=x86 --disable-asm \
    || { tail -60 ffbuild/config.log; exit 1; }
fi
make -j"$(nproc)"
make install
