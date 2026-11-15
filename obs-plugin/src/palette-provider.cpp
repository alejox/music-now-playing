#include "palette-provider.hpp"
#include "media-watcher.hpp"
#include "palette.hpp"

#include <obs-module.h>
#include <util/platform.h>

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QObject>

#include <cstring>
#include <mutex>
#include <vector>

namespace {

// Lo que responde el procedimiento. Se calcula en el hilo de la interfaz cuando el lector
// escribe (una vez por segundo) y el procedimiento, que OBS llama desde su hilo gráfico unas
// 4 veces por segundo, solo lo copia bajo el candado: no toca Qt ni decodifica imágenes.
struct Cache {
	bool playing = false;
	uint32_t accent = 0, tint = 0, background = 0;
	uint64_t updatedNs = 0; // si el lector deja de escribir, a los 10 s ya no suena nada
	bool stopped = false;
};

std::mutex cacheMutex;
Cache cache;

// Solo en el hilo de la interfaz.
QString lastKey;
palette::Colors lastColors;
QMetaObject::Connection connection;

constexpr uint64_t kStaleNs = 10ull * 1000 * 1000 * 1000;

// Sin carátula, los mismos colores que la tarjeta: el del tipo de app (ACCENTS de
// data/overlay/index.html) y el gris de fondo (sólido) y de cristal.
uint32_t kindAccent(const QString &kind)
{
	static const QHash<QString, uint32_t> accents = {
		{"spotify", 0x1ED760}, {"ytmusic", 0xFF0033}, {"youtube", 0xFF0033}, {"applemusic", 0xFA2D48},
		{"system", 0x0078D4},  {"radio", 0xF97316},   {"browser", 0x8B8CFF}, {"other", 0x8B8CFF},
	};
	return accents.value(kind, 0x8B8CFF);
}
constexpr uint32_t kNoArtTint = 0x2C2E3A;
constexpr uint32_t kNoArtBackground = 0x101116;

// La carátula (data:image/…;base64,…) reducida a 48 × 48 como la dibuja el canvas de la
// tarjeta (palette::shrink).
bool artworkColors(const QString &artwork, palette::Colors &out)
{
	if (!artwork.startsWith("data:image/"))
		return false;
	const qsizetype comma = artwork.indexOf(',');
	if (comma < 0)
		return false;
	QImage image;
	if (!image.loadFromData(QByteArray::fromBase64(artwork.mid(comma + 1).toLatin1())))
		return false;
	image = image.convertToFormat(QImage::Format_RGBA8888);
	const int w = image.width(), h = image.height();
	if (w < 1 || h < 1)
		return false;
	std::vector<uint8_t> full(static_cast<size_t>(w) * h * 4);
	for (int y = 0; y < h; y++)
		std::memcpy(&full[static_cast<size_t>(y) * w * 4], image.constScanLine(y), static_cast<size_t>(w) * 4);
	std::vector<uint8_t> pixels(palette::kSize * palette::kSize * 4);
	palette::shrink(full.data(), w, h, pixels.data());
	return palette::extract(pixels.data(), palette::kSize * palette::kSize, out);
}

void refresh()
{
	MediaWatcher &watcher = MediaWatcher::instance();
	// Lo que suena (no lo que está en pausa) del reproductor elegido en el dock.
	const Track track = watcher.current(false, watcher.player());
	if (!track.valid()) {
		lastKey.clear();
		std::lock_guard<std::mutex> lock(cacheMutex);
		cache.playing = false;
		cache.accent = cache.tint = cache.background = 0;
		cache.updatedNs = os_gettime_ns();
		return;
	}
	// Se recalcula solo si cambia la carátula (o la canción, para el color por tipo de app).
	const QString key = track.app + '\n' + track.kind + '\n' + track.title + '\n' + track.artist + '\n' +
			    QString::number(track.artwork.size());
	if (key != lastKey) {
		lastKey = key;
		if (!artworkColors(track.artwork, lastColors)) {
			lastColors.accent = kindAccent(track.kind);
			lastColors.tint = kNoArtTint;
			lastColors.background = kNoArtBackground;
		}
	}
	std::lock_guard<std::mutex> lock(cacheMutex);
	cache.playing = true;
	cache.accent = lastColors.accent;
	cache.tint = lastColors.tint;
	cache.background = lastColors.background;
	cache.updatedNs = os_gettime_ns();
}

void getPalette(void *, calldata_t *cd)
{
	Cache now;
	{
		std::lock_guard<std::mutex> lock(cacheMutex);
		now = cache;
	}
	const bool playing = !now.stopped && now.playing && os_gettime_ns() - now.updatedNs < kStaleNs;
	calldata_set_bool(cd, "playing", playing);
	calldata_set_int(cd, "accent", playing ? now.accent : 0);
	calldata_set_int(cd, "tint", playing ? now.tint : 0);
	calldata_set_int(cd, "background", playing ? now.background : 0);
}

} // namespace

void palette_provider_start()
{
	{
		std::lock_guard<std::mutex> lock(cacheMutex);
		cache = Cache();
	}
	MediaWatcher &watcher = MediaWatcher::instance();
	connection = QObject::connect(&watcher, &MediaWatcher::updated, &watcher, refresh);
	// OBS no deja quitar un procedimiento global: se registra una sola vez y, tras
	// palette_provider_stop(), responde playing = false.
	static bool registered = false;
	if (!registered) {
		registered = true;
		proc_handler_add(obs_get_proc_handler(),
				 "void music_now_playing_get_palette(out bool playing, out int accent, out int tint, out int background)",
				 getPalette, nullptr);
	}
}

void palette_provider_stop()
{
	QObject::disconnect(connection);
	std::lock_guard<std::mutex> lock(cacheMutex);
	cache.stopped = true;
	cache.playing = false;
}
