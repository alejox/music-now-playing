#!/usr/bin/env bash
# Instala Music Now Playing para este usuario (sin sudo):
#   - la app en ~/.local/share/obs-now-playing y su ícono en el menú de aplicaciones;
#   - el plugin de OBS (si viene en la carpeta plugin/) en ~/.config/obs-studio/plugins y,
#     si OBS está instalado como Flatpak, también en su carpeta de plugins.
# Desinstalar: ./install.sh --uninstall
set -euo pipefail
SRC="$(cd "$(dirname "$0")" && pwd)"
APP_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/obs-now-playing"
DESKTOP="${XDG_DATA_HOME:-$HOME/.local/share}/applications/obs-now-playing.desktop"
PLUGIN_DIRS=("${XDG_CONFIG_HOME:-$HOME/.config}/obs-studio/plugins")
FLATPAK_OBS="$HOME/.var/app/com.obsproject.Studio"
if [ -d "$FLATPAK_OBS" ]; then PLUGIN_DIRS+=("$FLATPAK_OBS/config/obs-studio/plugins"); fi

if [ "${1:-}" = --uninstall ]; then
  if [ -x "$APP_DIR/obs-now-playing" ]; then "$APP_DIR/obs-now-playing" --stop || true; fi
  rm -rf "$APP_DIR" "$DESKTOP"
  for dir in "${PLUGIN_DIRS[@]}"; do rm -rf "$dir/obs-now-playing"; done
  echo "Music Now Playing desinstalado."
  exit 0
fi

# Cierra la copia abierta antes de reemplazarla.
if [ -x "$APP_DIR/obs-now-playing" ]; then "$APP_DIR/obs-now-playing" --stop || true; fi
rm -rf "$APP_DIR"
mkdir -p "$APP_DIR" "$(dirname "$DESKTOP")"
cp -R "$SRC/app" "$SRC/node" "$SRC/obs-now-playing" "$SRC/icon.png" "$APP_DIR/"
chmod +x "$APP_DIR/obs-now-playing" "$APP_DIR/node/node" "$APP_DIR/app/media/linux.sh"
EXEC="\"$APP_DIR/obs-now-playing\""
ICON="$APP_DIR/icon.png"
while IFS= read -r line; do
  line=${line//@EXEC@/"$EXEC"}
  line=${line//@ICON@/"$ICON"}
  printf '%s\n' "$line"
done <"$SRC/obs-now-playing.desktop" >"$DESKTOP"
if command -v update-desktop-database >/dev/null 2>&1; then
  update-desktop-database "$(dirname "$DESKTOP")" >/dev/null 2>&1 || true
fi
echo "App instalada en $APP_DIR (búscala como \"Music Now Playing\" en el menú)."

if [ -d "$SRC/plugin/obs-now-playing" ]; then
  for dir in "${PLUGIN_DIRS[@]}"; do
    mkdir -p "$dir"
    rm -rf "$dir/obs-now-playing"
    cp -R "$SRC/plugin/obs-now-playing" "$dir/"
    echo "Plugin de OBS instalado en $dir/obs-now-playing (reinicia OBS)."
  done
fi

if ! command -v playerctl >/dev/null 2>&1; then
  echo
  echo "Falta playerctl, que lee lo que suena: instálalo con"
  echo "  sudo apt install playerctl   (Debian/Ubuntu)"
  echo "  sudo dnf install playerctl   (Fedora)"
  echo "  sudo pacman -S playerctl     (Arch)"
fi
