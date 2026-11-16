// Pruebas de la paleta (src/palette.cpp): las mismas imágenes de 48 × 48 que
// tests/palette-fixtures.js y los colores que da extractColors() de data/overlay/ui.js en
// Chromium para ellas (los saca ese archivo; si cambia el algoritmo, se regeneran ahí).
//   obs-plugin/tests/run-palette-test.sh
#include "palette.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

constexpr int kSize = 48;
using Pixels = std::vector<uint8_t>;

int failed = 0;
int checks = 0;

void fill(Pixels &px, int x0, int y0, int x1, int y1, std::array<int, 4> c)
{
	for (int y = y0; y < y1; y++) {
		for (int x = x0; x < x1; x++) {
			uint8_t *p = &px[4 * (y * kSize + x)];
			for (int k = 0; k < 4; k++)
				p[k] = static_cast<uint8_t>(c[k]);
		}
	}
}

Pixels fixture(const char *name)
{
	Pixels px(kSize * kSize * 4, 0);
	if (!std::strcmp(name, "red_on_black")) {
		fill(px, 0, 0, kSize, kSize, {10, 10, 12, 255});
		fill(px, 18, 18, 30, 30, {220, 30, 40, 255});
	} else if (!std::strcmp(name, "sunset")) {
		for (int y = 0; y < kSize; y++)
			fill(px, 0, y, kSize, y + 1, {40 + 4 * y, 20 + 2 * y, std::max(0, 90 - y), 255});
		fill(px, 16, 6, 32, 18, {255, 190, 80, 255});
	} else if (!std::strcmp(name, "gray")) {
		fill(px, 0, 0, kSize, kSize, {128, 128, 128, 255});
	} else if (!std::strcmp(name, "teal_orange")) {
		fill(px, 0, 0, kSize / 2, kSize, {20, 150, 140, 255});
		fill(px, kSize / 2, 0, kSize, kSize, {240, 120, 30, 255});
	}
	return px;
}

struct Expected {
	const char *name;
	uint32_t accent, tint, background;
};

// De tests/palette-fixtures.js (ui.js en Chromium).
const Expected EXPECTED[] = {
	{"red_on_black", 0xE74B53, 0x2A2A46, 0x1A1A1F}, // hsla(357, 76%, 60%) · hsla(240, 25%, 22%) · hsla(240, 9%, 11%)
	{"sunset", 0xFFBF52, 0x351B55, 0x1B0F29},       // hsla(38, 100%, 66%) · hsla(267, 52%, 22%) · hsla(267, 45%, 11%)
	{"gray", 0x999999, 0x462A2A, 0x1C1C1C},         // hsla(0, 0%, 60%) · hsla(0, 25%, 22%) · hsla(0, 0%, 11%)
	{"teal_orange", 0xF38D3F, 0x145D56, 0x0F2927},  // hsla(26, 88%, 60%) · hsla(175, 65%, 22%) · hsla(175, 45%, 11%)
};

void expectColor(const char *what, const char *name, uint32_t got, uint32_t want)
{
	checks++;
	if (got != want) {
		failed++;
		std::fprintf(stderr, "✘ %s de %s: esperado 0x%06X, obtenido 0x%06X\n", what, name, want, got);
	}
}

} // namespace

int main()
{
	for (const Expected &e : EXPECTED) {
		const Pixels px = fixture(e.name);
		palette::Colors c;
		checks++;
		if (!palette::extract(px.data(), kSize * kSize, c)) {
			failed++;
			std::fprintf(stderr, "✘ %s: no dio paleta\n", e.name);
			continue;
		}
		expectColor("acento", e.name, c.accent, e.accent);
		expectColor("tinte", e.name, c.tint, e.tint);
		expectColor("fondo", e.name, c.background, e.background);
	}

	// Toda transparente: sin paleta.
	{
		Pixels px(kSize * kSize * 4, 0);
		palette::Colors c;
		checks++;
		if (palette::extract(px.data(), kSize * kSize, c)) {
			failed++;
			std::fprintf(stderr, "✘ transparente: dio paleta\n");
		}
	}

	// Reducción a 48 × 48 (palette::shrink, como drawImage en el canvas de Chromium):
	// una de 48 × 48 queda igual, y una de 96 × 96 hecha de bloques de 2 × 2 da el promedio de
	// cada bloque (un nivel de mipmap, redondeado hacia abajo).
	{
		const Pixels same = fixture("sunset");
		Pixels out(kSize * kSize * 4);
		palette::shrink(same.data(), kSize, kSize, out.data());
		checks++;
		if (out != same) {
			failed++;
			std::fprintf(stderr, "✘ reducir 48 × 48: cambió la imagen\n");
		}
		Pixels big(96 * 96 * 4);
		for (int y = 0; y < 96; y++)
			for (int x = 0; x < 96; x++) {
				uint8_t *p = &big[4 * (y * 96 + x)];
				const int k = (x % 2) + 2 * (y % 2); // cuatro tonos por bloque
				p[0] = static_cast<uint8_t>(10 + 40 * k);
				p[1] = static_cast<uint8_t>(200 - 30 * k + (x / 2) % 7);
				p[2] = static_cast<uint8_t>(77 + k);
				p[3] = 255;
			}
		palette::shrink(big.data(), 96, 96, out.data());
		int wrong = 0;
		for (int y = 0; y < kSize; y++)
			for (int x = 0; x < kSize; x++)
				for (int c = 0; c < 4; c++) {
					int sum = 0;
					for (int j = 0; j < 2; j++)
						for (int i = 0; i < 2; i++)
							sum += big[4 * ((2 * y + j) * 96 + 2 * x + i) + c];
					wrong += out[4 * (y * kSize + x) + c] != (sum >> 2);
				}
		checks++;
		if (wrong) {
			failed++;
			std::fprintf(stderr, "✘ reducir 96 × 96: %d valores distintos del promedio\n", wrong);
		}
	}

	// hsl() → RGB como el navegador (casos conocidos de CSS).
	expectColor("hsl", "rojo", palette::hslToRgb(0, 100, 50), 0xFF0000);
	expectColor("hsl", "verde", palette::hslToRgb(120, 100, 50), 0x00FF00);
	expectColor("hsl", "azul 360+240", palette::hslToRgb(600, 100, 50), 0x0000FF);
	expectColor("hsl", "gris", palette::hslToRgb(200, 0, 50), 0x808080);

	if (failed) {
		std::fprintf(stderr, "Paleta: %d de %d comprobaciones fallaron\n", failed, checks);
		return 1;
	}
	std::printf("Paleta: OK (%d comprobaciones)\n", checks);
	return 0;
}
