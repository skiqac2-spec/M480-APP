#!/bin/bash
# Makes out/M480Emu.cab (installer), out/M480Emu.zip (copy-and-run folder) and out/README.txt
# from the executables built by build.sh.   Needs gcab and zip.
set -e
ROOT=$(cd "$(dirname "$0")" && pwd)
O=$ROOT/out
S=$O/stage
rm -rf "$O" && mkdir -p "$S"
cp "$ROOT/host/out/menu/stripped.exe" "$S/Emulator.exe"
for s in ps1 gba nes snes gb md; do cp "$ROOT/host/out/$s/stripped.exe" "$S/emu_$s.exe"; done
cp "$ROOT/README.txt" "$S/README.txt"
cp "$ROOT/licenses/GPLv2.txt" "$S/GPLv2.txt"
cp "$ROOT/licenses/snes9x_license.txt" "$S/snes9x_license.txt"
cp "$ROOT/licenses/picodrive_license.txt" "$S/picodrive_license.txt"
printf '\xef\xbb\xbf본인 소유 기기에서 추출한 BIOS 파일을 이 폴더에 넣으면 자동으로 사용됩니다. 없어도 실행됩니다.\r\nPS1: scph1001.bin 등 / GBA: gba_bios.bin\r\n' > "$S/README_BIOS.txt"

MAIN="Emulator.exe emu_ps1.exe emu_gba.exe emu_nes.exe emu_snes.exe emu_gb.exe emu_md.exe README.txt"
LIC="GPLv2.txt snes9x_license.txt picodrive_license.txt"
ext() { for f in "$@"; do printf '      <characteristic type="%s" translation="install">\n        <characteristic type="Extract">\n          <parm name="Source" value="%s" />\n        </characteristic>\n      </characteristic>\n' "$f" "$f"; done; }
{
cat <<X
<wap-provisioningdoc>
  <characteristic type="Install">
    <parm name="InstallPhase" value="install" />
    <parm name="AppName" value="M480 Emulator" />
    <parm name="InstallDir" value="%CE1%\\M480Emu" translation="install" />
    <parm name="NumDirs" value="3" />
    <parm name="NumFiles" value="12" />
    <parm name="NumRegKeys" value="0" />
    <parm name="NumRegVals" value="0" />
    <parm name="NumShortcuts" value="1" />
  </characteristic>
  <characteristic type="FileOperation">
    <characteristic type="%InstallDir%" translation="install">
      <characteristic type="MakeDir" />
X
ext $MAIN
cat <<X
    </characteristic>
    <characteristic type="%InstallDir%\\licenses" translation="install">
      <characteristic type="MakeDir" />
X
ext $LIC
cat <<X
    </characteristic>
    <characteristic type="%InstallDir%\\bios" translation="install">
      <characteristic type="MakeDir" />
X
ext README_BIOS.txt
cat <<X
    </characteristic>
    <characteristic type="%CE11%" translation="install">
      <characteristic type="MakeDir" />
      <characteristic type="M480 Emulator.lnk" translation="install">
        <characteristic type="Shortcut">
          <parm name="Source" value="%InstallDir%\\Emulator.exe" translation="install" />
        </characteristic>
      </characteristic>
    </characteristic>
  </characteristic>
</wap-provisioningdoc>
X
} > "$S/_setup.xml"

(cd "$S" && gcab -c -z "$O/M480Emu.cab" _setup.xml $MAIN $LIC README_BIOS.txt)

# zip: the same files as an M480Emu folder
Z=$O/zip/M480Emu
mkdir -p "$Z/licenses" "$Z/bios"
(cd "$S" && cp $MAIN "$Z/" && cp $LIC "$Z/licenses/" && cp README_BIOS.txt "$Z/bios/")
(cd "$O/zip" && zip -qr "$O/M480Emu.zip" M480Emu)
cp "$ROOT/README.txt" "$O/README.txt"
rm -rf "$S" "$O/zip"
ls -la "$O"
