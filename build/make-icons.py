"""Dibuja el ícono de la app y lo exporta como .png, .ico (Windows) e .icns (macOS).

Uso: python3 build/make-icons.py <carpeta_de_salida>
"""
import sys
from pathlib import Path

from PIL import Image, ImageDraw

SIZE = 1024
out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)

img = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
draw = ImageDraw.Draw(img)

# Fondo: cuadrado redondeado oscuro con el margen que usan los íconos de macOS.
pad = 100
draw.rounded_rectangle((pad, pad, SIZE - pad, SIZE - pad), radius=185, fill=(22, 24, 31, 255))

# Ecualizador: barras verdes (Spotify) y rojas (YouTube Music).
bars = [(0.42, (30, 215, 96)), (0.70, (30, 215, 96)), (0.52, (255, 0, 51)), (0.82, (255, 0, 51)), (0.36, (30, 215, 96))]
inner_left, inner_right = 250, SIZE - 250
base = SIZE - 270
max_h = 470
gap = 28
width = (inner_right - inner_left - gap * (len(bars) - 1)) / len(bars)
for i, (h, color) in enumerate(bars):
    x0 = inner_left + i * (width + gap)
    draw.rounded_rectangle((x0, base - max_h * h, x0 + width, base), radius=width / 2, fill=color + (255,))

img.save(out / "icon.png")
img.save(out / "icon.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
img.save(out / "AppIcon.icns")
print(f"Íconos en {out}")
