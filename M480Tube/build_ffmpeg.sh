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
make distclean >/dev/null 2>&1 || true
COMMON="--prefix=$PREFIX --enable-cross-compile --target-os=none
 --disable-everything --disable-programs --disable-doc --disable-avdevice --disable-swresample
 --disable-swscale --disable-postproc --disable-avfilter --disable-network
 --disable-pthreads --disable-w32threads --disable-os2threads --disable-autodetect
 --disable-iconv --disable-zlib --disable-bzlib --disable-lzma --disable-debug
 --enable-decoder=h264 --enable-decoder=aac_fixed --enable-decoder=aac
 --enable-demuxer=mov --enable-parser=h264 --enable-parser=aac
 --enable-runtime-cpudetect"
if [ "$TARGET" = ce ]; then
  ./configure $COMMON --cross-prefix=arm-mingw32ce- --arch=arm --cpu=arm1136j-s \
    --disable-vfp --disable-neon --disable-vfpv3 --disable-armv6t2 --enable-armv5te --enable-armv6 --disable-thumb \
    --extra-cflags="-D_WIN32_WCE=0x0502 -DWINCE -mcpu=arm1136j-s" || { tail -60 ffbuild/config.log; exit 1; }
else
  ./configure $COMMON --cross-prefix=i686-w64-mingw32- --arch=x86 --disable-asm \
    || { tail -60 ffbuild/config.log; exit 1; }
fi
make -j"$(nproc)"
make install
