#!/usr/bin/env bash
# Genera los instaladores en dist/:
#   OBS-Now-Playing-Setup-<v>.exe        (Windows 10/11, x64)
#   OBS-Now-Playing-<v>-mac-apple.zip    (Mac con chip Apple M1/M2/M3/M4…)
#   OBS-Now-Playing-<v>-mac-intel.zip    (Mac con Intel)
#
# Uso: ./build/build.sh [all|windows|mac]
#
# Windows se arma en Linux/WSL (python3 con Pillow y makensis/NSIS). Con
# PLUGIN_ZIP=<obs-now-playing-plugin-…-windows-x64.zip> el instalador también instala el
# plugin de OBS (la Release siempre lo hace).
# Mac se arma mejor EN UN MAC (python3 con Pillow y las herramientas de Xcode): ahí la
# app queda firmada ad hoc entera. En Linux también se arma (clang + lld), pero sin
# firmar el paquete, y macOS la da por "dañada" al descargarla. La Release la arma en
# un Mac de GitHub Actions.
set -euo pipefail
cd "$(dirname "$0")/.."

TARGET="${1:-all}"
case "$TARGET" in all | windows | mac) ;; *) echo "Uso: $0 [all|windows|mac]"; exit 1 ;; esac
want() { [ "$TARGET" = all ] || [ "$TARGET" = "$1" ]; }

VERSION="$(node -p "require('./package.json').version" 2>/dev/null || python3 -c "import json;print(json.load(open('package.json'))['version'])")"
NODE_VERSION="${NODE_VERSION:-22.22.0}"
CACHE=build/.cache
STAGE=build/.stage
DIST=dist
mkdir -p "$CACHE" "$DIST"
sha256_check() { if command -v sha256sum >/dev/null; then sha256sum -c --quiet -; else shasum -a 256 -c --quiet -; fi; }

# 1. Node.js oficial, verificado contra las sumas SHA-256 publicadas.
BASE="https://nodejs.org/dist/v$NODE_VERSION"
FILES=()
want windows && FILES+=("node-v$NODE_VERSION-win-x64.zip")
want mac && FILES+=("node-v$NODE_VERSION-darwin-arm64.tar.gz" "node-v$NODE_VERSION-darwin-x64.tar.gz")
curl -sSfL -o "$CACHE/SHASUMS256-$NODE_VERSION.txt" "$BASE/SHASUMS256.txt"
for f in "${FILES[@]}"; do
  [ -f "$CACHE/$f" ] || curl -sSfL -o "$CACHE/$f" "$BASE/$f"
  (cd "$CACHE" && grep " $f\$" "SHASUMS256-$NODE_VERSION.txt" | sha256_check)
done

# 2. Íconos.
python3 build/make-icons.py "$STAGE/icons"

# 3. Windows: lanzador sin consola + instalador.
if want windows; then
  W="$STAGE/win"
  rm -rf "$W"
  mkdir -p "$W/app/media"
  cp server.js overlay.html controls.html ui.js now-playing.user.js "$W/app/"
  cp media/windows.ps1 "$W/app/media/"
  cp "$STAGE/icons/icon.ico" "$W/"
  python3 -c "import zipfile,sys; z=zipfile.ZipFile(sys.argv[1]); open(sys.argv[2],'wb').write(z.read(next(n for n in z.namelist() if n.endswith('/node.exe'))))" \
    "$CACHE/node-v$NODE_VERSION-win-x64.zip" "$W/node.exe"
  STAGE_ABS="$(cd "$W" && pwd)"
  PLUGIN_ARGS=()
  if [ -n "${PLUGIN_ZIP:-}" ]; then
    # El zip trae la forma de la carpeta de OBS: obs-plugins/64bit/obs-now-playing.dll y
    # data/obs-plugins/obs-now-playing/… (el instalador lo copia encima de la de OBS).
    python3 -c "import zipfile,sys; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])" "$PLUGIN_ZIP" "$W/plugin"
    find "$W/plugin" -name '*.pdb' -delete
    [ -f "$W/plugin/obs-plugins/64bit/obs-now-playing.dll" ] || { echo "$PLUGIN_ZIP no trae el plugin"; exit 1; }
    PLUGIN_ARGS=(-DPLUGIN="$STAGE_ABS/plugin")
  else
    echo "Aviso: sin PLUGIN_ZIP el instalador no incluye el plugin de OBS."
  fi
  makensis -V2 -DSTAGE="$STAGE_ABS" -DVERSION="$VERSION" build/windows/launcher.nsi
  makensis -V2 -DSTAGE="$STAGE_ABS" -DVERSION="$VERSION" ${PLUGIN_ARGS[@]+"${PLUGIN_ARGS[@]}"} -DOUTFILE="$(pwd)/$DIST/OBS-Now-Playing-Setup-$VERSION.exe" build/windows/installer.nsi
fi

# 4. macOS: lanzador compilado (build/macos/launcher.c) y una app por arquitectura.
if want mac; then
  M="$STAGE/mac"
  rm -rf "$M"
  mkdir -p "$M"
  for arch in arm64 x86_64; do
    if [ "$(uname)" = Darwin ]; then
      clang -arch "$arch" -mmacosx-version-min=11.0 -Os -fno-builtin -ffreestanding -Wall -Werror \
        -o "$M/launcher-$arch" build/macos/launcher.c
    else
      # Sin SDK de Apple: ld64.lld con un .tbd mínimo de libSystem (firma ad hoc el de arm64).
      LD64="$(command -v ld64.lld || command -v ld64.lld-18 || command -v ld64.lld-19 || true)"
      [ -n "$LD64" ] || { echo "Falta ld64.lld (paquete lld)"; exit 1; }
      clang -target "$arch-apple-macos11" -Os -fno-stack-protector -fno-builtin -ffreestanding -Wall -Werror \
        -c build/macos/launcher.c -o "$M/launcher-$arch.o"
      "$LD64" -arch "$arch" -platform_version macos 11.0 11.0 -L build/macos -lSystem -o "$M/launcher-$arch" "$M/launcher-$arch.o"
    fi
  done

  for pair in "arm64:arm64:apple" "x64:x86_64:intel"; do
    IFS=: read -r node_arch launcher_arch label <<<"$pair"
    zip="$DIST/OBS-Now-Playing-$VERSION-mac-$label.zip"
    python3 build/package_mac.py "$VERSION" "$node_arch" "$CACHE/node-v$NODE_VERSION-darwin-$node_arch.tar.gz" \
      "$STAGE/icons/AppIcon.icns" "$M/launcher-$launcher_arch" "$zip"

    if [ "$(uname)" = Darwin ]; then
      # Firma ad hoc de la app ENTERA (sella Info.plist y recursos). Sin --deep: el
      # node de adentro conserva la firma de Developer ID de Node.js.
      app_dir="$M/$label"
      rm -rf "$app_dir" && mkdir -p "$app_dir"
      ditto -x -k "$zip" "$app_dir"
      codesign --force --sign - --identifier io.github.obs-now-playing "$app_dir/OBS Now Playing.app"
      codesign --verify --deep --strict --verbose=2 "$app_dir/OBS Now Playing.app"
      rm "$zip"
      ditto -c -k --keepParent "$app_dir/OBS Now Playing.app" "$zip"
    else
      echo "Aviso: $zip queda sin firmar como paquete (solo se puede en un Mac)."
    fi
  done
fi

ls -lh "$DIST"
