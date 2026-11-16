#include "palette.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace palette {

namespace {

struct Hsl {
	double h, s, l; // 0–1, como rgbToHsl de ui.js
};

// rgbToHsl de ui.js (los canales llegan como promedios, con decimales).
Hsl rgbToHsl(double r, double g, double b)
{
	r /= 255;
	g /= 255;
	b /= 255;
	const double max = std::max({r, g, b});
	const double min = std::min({r, g, b});
	const double l = (max + min) / 2;
	if (max == min)
		return {0, 0, l};
	const double d = max - min;
	const double s = l > 0.5 ? d / (2 - max - min) : d / (max + min);
	const double h = max == r ? (g - b) / d + (g < b ? 6 : 0) : max == g ? (b - r) / d + 2 : (r - g) / d + 4;
	return {h / 6, s, l};
}

// Math.round de JavaScript (las mitades redondean hacia arriba, también las negativas).
int jsRound(double v)
{
	return static_cast<int>(std::floor(v + 0.5));
}

// Como hsl() de ui.js: la página escribe "hsla(H, S%, L%)" con enteros y el navegador lo pasa
// a RGB; aquí, lo mismo.
uint32_t toRgb(const Hsl &c)
{
	return hslToRgb(jsRound(c.h * 360), jsRound(c.s * 100), jsRound(c.l * 100));
}

struct Bucket {
	double r = 0, g = 0, b = 0;
	int n = 0;
};

// Una imagen RGBA de 8 bits.
struct Image {
	int w = 0, h = 0;
	std::vector<uint8_t> px;
	uint8_t at(int x, int y, int c) const
	{
		x = std::clamp(x, 0, w - 1);
		y = std::clamp(y, 0, h - 1);
		return px[4 * (y * w + x) + c];
	}
};

// Un nivel de mipmap como los de Skia (el motor gráfico de Chromium): cada píxel junta 2 × 2
// del nivel anterior; si un lado es impar, 3 en ese sentido con pesos 1-2-1. La suma se
// divide hacia abajo.
Image half(const Image &src)
{
	Image out;
	out.w = std::max(1, src.w / 2);
	out.h = std::max(1, src.h / 2);
	out.px.resize(static_cast<size_t>(out.w) * out.h * 4);
	const bool oddX = src.w > 1 && src.w % 2 == 1;
	const bool oddY = src.h > 1 && src.h % 2 == 1;
	const int wx[3] = {1, oddX ? 2 : 1, oddX ? 1 : 0};
	const int wy[3] = {1, oddY ? 2 : 1, oddY ? 1 : 0};
	const int shift = (oddX ? 2 : 1) + (oddY ? 2 : 1); // pesos: 2 o 4 por lado
	for (int y = 0; y < out.h; y++)
		for (int x = 0; x < out.w; x++)
			for (int c = 0; c < 4; c++) {
				int sum = 0;
				for (int j = 0; j < 3; j++)
					for (int i = 0; i < 3; i++)
						if (wx[i] && wy[j])
							sum += wx[i] * wy[j] * src.at(2 * x + i, 2 * y + j, c);
				out.px[4 * (y * out.w + x) + c] = static_cast<uint8_t>(sum >> shift);
			}
	return out;
}

} // namespace

void shrink(const uint8_t *rgba, int width, int height, uint8_t *out)
{
	Image level;
	level.w = width;
	level.h = height;
	level.px.assign(rgba, rgba + static_cast<size_t>(width) * height * 4);
	// Nivel de mipmap: log2 de la reducción hacia abajo, por el lado que menos se reduce (así
	// ningún lado queda más chico que el pedido).
	const double scale = std::min(static_cast<double>(width) / kSize, static_cast<double>(height) / kSize);
	const int levels = scale > 1 ? static_cast<int>(std::floor(std::log2(scale))) : 0;
	for (int i = 0; i < levels && level.w > 1 && level.h > 1; i++)
		level = half(level);
	// Bilineal con la posición dentro del píxel en 4 bits (dieciseisavos), como Skia al dibujar
	// por software; la suma se divide hacia abajo.
	for (int y = 0; y < kSize; y++) {
		const double sy = (y + 0.5) * level.h / kSize - 0.5;
		const int y0 = static_cast<int>(std::floor(sy));
		const int fy = static_cast<int>(std::floor((sy - y0) * 16));
		for (int x = 0; x < kSize; x++) {
			const double sx = (x + 0.5) * level.w / kSize - 0.5;
			const int x0 = static_cast<int>(std::floor(sx));
			const int fx = static_cast<int>(std::floor((sx - x0) * 16));
			for (int c = 0; c < 4; c++) {
				const int top = level.at(x0, y0, c) * (16 - fx) + level.at(x0 + 1, y0, c) * fx;
				const int bottom = level.at(x0, y0 + 1, c) * (16 - fx) + level.at(x0 + 1, y0 + 1, c) * fx;
				out[4 * (y * kSize + x) + c] = static_cast<uint8_t>((top * (16 - fy) + bottom * fy) >> 8);
			}
		}
	}
}

uint32_t hslToRgb(int hueDegrees, int saturationPercent, int lightnessPercent)
{
	// CSS Color 4 (lo que hace el navegador con hsl()), y cada canal redondeado a 0–255.
	const double hue = std::fmod(std::fmod(static_cast<double>(hueDegrees), 360.0) + 360.0, 360.0);
	const double s = std::clamp(saturationPercent, 0, 100) / 100.0;
	const double l = std::clamp(lightnessPercent, 0, 100) / 100.0;
	auto channel = [&](double n) {
		const double k = std::fmod(n + hue / 30, 12);
		const double a = s * std::min(l, 1 - l);
		const double v = l - a * std::max(-1.0, std::min({k - 3, 9 - k, 1.0}));
		return static_cast<uint32_t>(std::clamp(jsRound(v * 255), 0, 255));
	};
	return (channel(0) << 16) | (channel(8) << 8) | channel(4);
}

bool extract(const uint8_t *rgba, int pixelCount, Colors &out)
{
	// Agrupa píxeles parecidos (4 bits por canal) y promedia cada grupo. Los grupos se
	// recorren en el orden en que aparecen, como el Map de ui.js: con un empate, gana el
	// primero.
	std::vector<int> slot(4096, -1);
	std::vector<Bucket> buckets;
	for (int i = 0; i < pixelCount; i++) {
		const uint8_t *p = rgba + 4 * i;
		if (p[3] < 128)
			continue;
		const int key = ((p[0] >> 4) << 8) | ((p[1] >> 4) << 4) | (p[2] >> 4);
		if (slot[key] < 0) {
			slot[key] = static_cast<int>(buckets.size());
			buckets.emplace_back();
		}
		Bucket &b = buckets[slot[key]];
		b.r += p[0];
		b.g += p[1];
		b.b += p[2];
		b.n++;
	}
	if (buckets.empty())
		return false;

	Hsl dominant{};
	int dominantN = 0;
	Hsl vibrant{};
	bool hasVibrant = false;
	double bestScore = 0;
	for (const Bucket &b : buckets) {
		const Hsl color = rgbToHsl(b.r / b.n, b.g / b.n, b.b / b.n);
		if (b.n > dominantN) {
			dominant = color;
			dominantN = b.n;
		}
		if (color.s < 0.3 || color.l < 0.15 || color.l > 0.9)
			continue;
		const double score = std::sqrt(static_cast<double>(b.n)) * color.s * color.s * (1 - std::fabs(color.l - 0.55));
		if (score > bestScore) {
			bestScore = score;
			vibrant = color;
			hasVibrant = true;
		}
	}

	// Variante oscura de ui.js.
	const Hsl base = hasVibrant ? vibrant : dominant;
	const Hsl accent{base.h, hasVibrant ? std::clamp(base.s, 0.55, 1.0) : std::clamp(base.s, 0.0, 0.35),
			 std::clamp(base.l, 0.6, 0.75)};
	const Hsl background{dominant.h, std::clamp(dominant.s, 0.0, 0.45), 0.11};
	const Hsl tint{dominant.h, std::clamp(dominant.s, 0.25, 0.65), 0.22};
	out.accent = toRgb(accent);
	out.tint = toRgb(tint);
	out.background = toRgb(background);
	return true;
}

} // namespace palette
