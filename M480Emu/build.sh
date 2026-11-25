#!/bin/bash
# Builds every M480Emu executable from the pinned upstream emulator sources plus our patches.
#   ./build.sh            (needs the cegcc arm-mingw32ce toolchain in /opt/cegcc)
#   WORK=/some/dir ./build.sh   to keep the downloaded sources elsewhere (default ./work)
set -e
ROOT=$(cd "$(dirname "$0")" && pwd)
W=${WORK:-$ROOT/work}
JOBS=${JOBS:-$(nproc)}
mkdir -p "$W"

# name  url  submodules-to-fetch
fetch() {
    local name=$1 url=$2; shift 2
    local d=$W/$name commit
    commit=$(cat "$ROOT/patches/$name.commit")
    if [ ! -f "$d/.m480_patched" ]; then
        rm -rf "$d"
        git init -q "$d"
        git -C "$d" remote add origin "$url"
        git -C "$d" fetch -q --depth 1 origin "$commit"
        git -C "$d" checkout -q FETCH_HEAD
        [ $# -gt 0 ] && git -C "$d" submodule update -q --init --depth 1 "$@"
        if [ -s "$ROOT/patches/$name.patch" ]; then
            (cd "$d" && patch -p1 -s < "$ROOT/patches/$name.patch")
        fi
        touch "$d/.m480_patched"
    fi
}

fetch pcsx_rearmed      https://github.com/notaz/pcsx_rearmed            deps/libretro-common deps/libchdr
fetch gpsp              https://github.com/libretro/gpsp
fetch libretro-fceumm   https://github.com/libretro/libretro-fceumm
fetch snes9x2002        https://github.com/libretro/snes9x2002
fetch gambatte-libretro https://github.com/libretro/gambatte-libretro
fetch picodrive         https://github.com/libretro/picodrive            platform/common/dr_libs platform/libpicofe pico/cd/libchdr pico/sound/emu2413

ZLIB=$W/pcsx_rearmed/deps/libchdr/deps/zlib-1.3.1
export PATH=/opt/cegcc/bin:$PATH
MK="HOST=$ROOT/host ZLIB=$ZLIB -j$JOBS"

make -f "$ROOT/host/mk/menu.mk" $MK
make -C "$W/pcsx_rearmed"      -f "$ROOT/host/mk/ps1.mk"  $MK
make -C "$W/gpsp"              -f "$ROOT/host/mk/gba.mk"  $MK
make -C "$W/libretro-fceumm"   -f "$ROOT/host/mk/nes.mk"  $MK
make -C "$W/snes9x2002"        -f "$ROOT/host/mk/snes.mk" $MK
make -C "$W/gambatte-libretro" -f "$ROOT/host/mk/gb.mk"   $MK
make -C "$W/picodrive"         -f "$ROOT/host/mk/md.mk"   $MK
ls -la "$ROOT"/host/out/*/stripped.exe
echo "build ok"
