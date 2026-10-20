#!/usr/bin/env bash
# Genera los instaladores en dist/:
#   OBS-Now-Playing-Setup-<v>.exe        (Windows 10/11, x64)
#   OBS-Now-Playing-<v>-mac-apple.zip    (Mac con chip Apple M1/M2/M3/M4…)
#   OBS-Now-Playing-<v>-mac-intel.zip    (Mac con Intel)
# Requiere: curl, python3 con Pillow, makensis (NSIS), clang y lld. Corre en Linux o WSL.
set -euo pipefail
cd "$(dirname "$0")/.."

VERSION="$(node -p "require('./package.json').version" 2>/dev/null || python3 -c "import json;print(json.load(open('package.json'))['version'])")"
NODE_VERSION="${NODE_VERSION:-22.22.0}"
CACHE=build/.cache
STAGE=build/.stage
DIST=dist
mkdir -p "$CACHE" "$DIST"
rm -rf "$STAGE/win"

# 1. Node.js oficial, verificado contra las sumas SHA-256 publicadas.
BASE="https://nodejs.org/dist/v$NODE_VERSION"
FILES=("node-v$NODE_VERSION-win-x64.zip" "node-v$NODE_VERSION-darwin-arm64.tar.gz" "node-v$NODE_VERSION-darwin-x64.tar.gz")
curl -sSfL -o "$CACHE/SHASUMS256-$NODE_VERSION.txt" "$BASE/SHASUMS256.txt"
for f in "${FILES[@]}"; do
  [ -f "$CACHE/$f" ] || curl -sSfL -o "$CACHE/$f" "$BASE/$f"
  (cd "$CACHE" && grep " $f\$" "SHASUMS256-$NODE_VERSION.txt" | sha256sum -c --quiet -)
done

# 2. Íconos.
python3 build/make-icons.py "$STAGE/icons"

# 3. Windows: lanzador sin consola + instalador.
W="$STAGE/win"
mkdir -p "$W/app/media"
cp server.js overlay.html now-playing.user.js "$W/app/"
cp media/windows.ps1 "$W/app/media/"
cp "$STAGE/icons/icon.ico" "$W/"
python3 -c "import zipfile,sys; z=zipfile.ZipFile(sys.argv[1]); open(sys.argv[2],'wb').write(z.read(next(n for n in z.namelist() if n.endswith('/node.exe'))))" \
  "$CACHE/node-v$NODE_VERSION-win-x64.zip" "$W/node.exe"
STAGE_ABS="$(cd "$W" && pwd)"
makensis -V2 -DSTAGE="$STAGE_ABS" -DVERSION="$VERSION" build/windows/launcher.nsi
makensis -V2 -DSTAGE="$STAGE_ABS" -DVERSION="$VERSION" -DOUTFILE="$(pwd)/$DIST/OBS-Now-Playing-Setup-$VERSION.exe" build/windows/installer.nsi

# 4. macOS: lanzador compilado (sin SDK de Apple: clang + ld64.lld y un .tbd mínimo
#    de libSystem) y una app por arquitectura. ld64.lld firma ad hoc el de arm64,
#    que es lo que exige macOS en los chips Apple.
LD64="$(command -v ld64.lld || command -v ld64.lld-18 || command -v ld64.lld-19 || true)"
[ -n "$LD64" ] || { echo "Falta ld64.lld (paquete lld)"; exit 1; }
M="$STAGE/mac"
mkdir -p "$M"
for arch in arm64 x86_64; do
  clang -target "$arch-apple-macos11" -Os -fno-stack-protector -fno-builtin -ffreestanding -Wall -Werror \
    -c build/macos/launcher.c -o "$M/launcher-$arch.o"
  "$LD64" -arch "$arch" -platform_version macos 11.0 11.0 -L build/macos -lSystem -o "$M/launcher-$arch" "$M/launcher-$arch.o"
done
python3 build/package_mac.py "$VERSION" arm64 "$CACHE/node-v$NODE_VERSION-darwin-arm64.tar.gz" "$STAGE/icons/AppIcon.icns" "$M/launcher-arm64" "$DIST/OBS-Now-Playing-$VERSION-mac-apple.zip"
python3 build/package_mac.py "$VERSION" x64 "$CACHE/node-v$NODE_VERSION-darwin-x64.tar.gz" "$STAGE/icons/AppIcon.icns" "$M/launcher-x86_64" "$DIST/OBS-Now-Playing-$VERSION-mac-intel.zip"

ls -lh "$DIST"
