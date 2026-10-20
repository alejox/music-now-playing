#include "music-channel.hpp"
#include "media-watcher.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QMessageBox>
#include <QRegularExpression>

namespace {

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

// La app a la que apunta la captura de audio por aplicación de OBS.
//   macOS:   id del paquete ("com.spotify.client"). Las apps instaladas desde el
//            navegador (YouTube Music) suenan por el navegador: com.google.Chrome.app.<id>
//            → com.google.Chrome.
//   Windows: el .exe ("Spotify.exe"), que es como identifica la ventana la captura.
QString audioTarget(const Track &track)
{
#ifdef _WIN32
	QString id = track.app.toLower();
	static const QList<QPair<QString, QString>> exes = {
		{"spotify", "Spotify.exe"}, {"msedge", "msedge.exe"}, {"chrome", "chrome.exe"},   {"firefox", "firefox.exe"},
		{"brave", "brave.exe"},     {"opera", "opera.exe"},   {"vivaldi", "vivaldi.exe"}, {"applemusic", "AppleMusic.exe"},
		{"tidal", "TIDAL.exe"},     {"deezer", "Deezer.exe"},
	};
	for (const auto &exe : exes) {
		if (id.contains(exe.first))
			return exe.second;
	}
	QString last = track.app.section(QRegularExpression("[\\\\/!]"), -1);
	return last.endsWith(".exe", Qt::CaseInsensitive) ? last : QString();
#else
	QString id = track.app;
	id.remove(QRegularExpression("\\.app\\..*$"));
	return id;
#endif
}

} // namespace

void create_music_channel(QWidget *parent)
{
	Track track = MediaWatcher::instance().current(true);
	if (!track.valid()) {
		QMessageBox::information(parent, text("NowPlaying.Channel.Title"), text("NowPlaying.Channel.NothingPlaying"));
		return;
	}
	QString target = audioTarget(track);
#ifdef _WIN32
	const char *sourceId = "wasapi_process_output_capture";
#else
	const char *sourceId = "sck_audio_capture";
#endif
	// La captura por aplicación existe en macOS 13+ y Windows 10 2004+ (OBS solo la registra ahí).
	if (target.isEmpty() || !obs_source_get_display_name(sourceId)) {
		QMessageBox::warning(parent, text("NowPlaying.Channel.Title"),
				     target.isEmpty() ? text("NowPlaying.Channel.UnknownApp").arg(track.label)
						      : text("NowPlaying.Channel.Unsupported"));
		return;
	}

	obs_data_t *settings = obs_data_create();
#ifdef _WIN32
	// "título:clase:exe" con prioridad por ejecutable: sirve con cualquier ventana de la app.
	obs_data_set_string(settings, "window", ("::" + target).toUtf8().constData());
	obs_data_set_int(settings, "priority", 2); // WINDOW_PRIORITY_EXE
#else
	obs_data_set_int(settings, "type", 1); // ScreenCaptureAudioApplicationStream
	obs_data_set_string(settings, "application", target.toUtf8().constData());
#endif

	// Si el canal ya existe se reapunta a la app de ahora (de Spotify a YouTube Music…).
	QByteArray name = text("NowPlaying.Channel.SourceName").toUtf8();
	obs_source_t *source = obs_get_source_by_name(name.constData());
	bool created = !source;
	if (source)
		obs_source_update(source, settings);
	else
		source = obs_source_create(sourceId, name.constData(), settings, nullptr);
	obs_data_release(settings);
	if (!source) {
		QMessageBox::warning(parent, text("NowPlaying.Channel.Title"), text("NowPlaying.Channel.Unsupported"));
		return;
	}

	// A la escena abierta, si aún no está ahí: así aparece en el Mezclador.
	obs_source_t *sceneSource = obs_frontend_get_current_scene();
	obs_scene_t *scene = obs_scene_from_source(sceneSource);
	if (scene && !obs_scene_find_source(scene, name.constData()))
		obs_scene_add(scene, source);
	obs_source_release(sceneSource);
	obs_source_release(source);

	QMessageBox::information(parent, text("NowPlaying.Channel.Title"),
				 text(created ? "NowPlaying.Channel.Created" : "NowPlaying.Channel.Updated")
					 .arg(QString::fromUtf8(name), track.label));
}
