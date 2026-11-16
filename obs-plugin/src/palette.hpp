#pragma once

#include <cstdint>

// Paleta de colores de una carátula: el mismo algoritmo que extractColors() de
// data/overlay/ui.js en tema oscuro (agrupa los píxeles por color, el acento es el grupo más
// vivo, el fondo y el tinte salen del dominante), con el mismo redondeo a HSL entero que la
// página antes de pasar a RGB. Sin Qt ni OBS: recibe los píxeles ya reducidos (48 × 48).
namespace palette {

struct Colors {
	uint32_t accent = 0;     // 0xRRGGBB: el color más vivo, para leerse sobre el fondo
	uint32_t tint = 0;       // 0xRRGGBB: el dominante con más color y luz (estilo cristal)
	uint32_t background = 0; // 0xRRGGBB: el dominante, muy oscuro
};

constexpr int kSize = 48; // como el canvas de extractColors()

// Reduce una imagen RGBA (width × height) a kSize × kSize sin mantener la proporción, como
// drawImage(img, 0, 0, 48, 48) en el canvas de Chromium: baja por mitades (mipmaps) hasta el
// nivel más cercano por arriba y de ahí interpola (bilineal). Así los colores que salen son
// los de la tarjeta; un promedio de todos los píxeles (QImage::SmoothTransformation) cambia
// el tono en carátulas con degradados.
void shrink(const uint8_t *rgba, int width, int height, uint8_t *out);

// rgba: pixelCount píxeles RGBA de 8 bits (los de alfa < 128 no cuentan). Devuelve false si
// no hay ningún píxel visible.
bool extract(const uint8_t *rgba, int pixelCount, Colors &out);

// HSL (h en grados, s y l en por ciento, ya enteros como los escribe la página) → 0xRRGGBB.
uint32_t hslToRgb(int hueDegrees, int saturationPercent, int lightnessPercent);

} // namespace palette
