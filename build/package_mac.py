"""Arma "OBS Now Playing.app" y lo comprime en un .zip listo para descargar.

Uso: python3 build/package_mac.py <version> <arch> <node_tar_gz> <icns> <lanzador> <salida.zip> [plugin.zip]

Con plugin.zip (obs-now-playing-plugin-<versión>-macos.zip), la app lo lleva en
Contents/Resources/plugin/ y server.js lo instala en la carpeta de plugins de OBS.

El ejecutable de la app es un lanzador compilado (build/macos/launcher.c) que
arranca el `node` oficial (firmado y notarizado por Node.js) en segundo plano y
termina: así, abrir la app otra vez lo vuelve a correr y server.js muestra el
panel de la copia abierta.
"""
import re
import sys
import tarfile
import zipfile
from pathlib import Path

version, arch, node_tgz, icns, launcher, out_zip = sys.argv[1:7]
plugin_zip = sys.argv[7] if len(sys.argv) > 7 and sys.argv[7] else None
ROOT = Path(__file__).resolve().parent.parent
APP = "OBS Now Playing.app"

INFO_PLIST = f"""<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>OBS Now Playing</string>
  <key>CFBundleDisplayName</key><string>OBS Now Playing</string>
  <key>CFBundleIdentifier</key><string>io.github.obs-now-playing</string>
  <key>CFBundleVersion</key><string>{version}</string>
  <key>CFBundleShortVersionString</key><string>{version}</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
  <key>CFBundleExecutable</key><string>OBS Now Playing</string>
  <key>CFBundleIconFile</key><string>AppIcon</string>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
  <key>LSUIElement</key><true/>
  <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
"""


def add(zf, name, data, mode=0o644):
    info = zipfile.ZipInfo(f"{APP}/{name}", date_time=(2026, 1, 1, 0, 0, 0))
    info.create_system = 3  # Unix: conserva los permisos (el bit de ejecución)
    info.external_attr = (0o100000 | mode) << 16
    info.compress_type = zipfile.ZIP_DEFLATED
    zf.writestr(info, data)


with tarfile.open(node_tgz) as tar:
    member = next(m for m in tar.getmembers() if m.name.endswith("/bin/node"))
    node_binary = tar.extractfile(member).read()

with zipfile.ZipFile(out_zip, "w", compresslevel=9) as zf:
    add(zf, "Contents/Info.plist", INFO_PLIST)
    add(zf, "Contents/MacOS/OBS Now Playing", Path(launcher).read_bytes(), 0o755)
    add(zf, "Contents/Resources/AppIcon.icns", Path(icns).read_bytes())
    add(zf, "Contents/Resources/node", node_binary, 0o755)
    for f in ("server.js", "overlay.html", "controls.html", "ui.js", "now-playing.user.js"):
        add(zf, f"Contents/Resources/app/{f}", (ROOT / f).read_bytes())
    add(zf, "Contents/Resources/app/media/macos.js", (ROOT / "media" / "macos.js").read_bytes())
    if plugin_zip:
        plugin_version = re.search(r"plugin-(\d+(?:\.\d+)*)-macos\.zip$", plugin_zip).group(1)
        add(zf, "Contents/Resources/plugin/obs-now-playing-plugin-macos.zip", Path(plugin_zip).read_bytes())
        add(zf, "Contents/Resources/plugin/version.txt", plugin_version + "\n")

print(f"{out_zip} ({arch})")
