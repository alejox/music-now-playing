#include "music-channel.hpp"
#include "media-watcher.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QMessageBox>
#include <QRegularExpression>

#include <cstring>

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
#elif defined(__APPLE__)
	QString id = track.app;
	id.remove(QRegularExpression("\\.app\\..*$"));
	return id;
#else
	return QString();
#endif
}

#ifdef _WIN32
constexpr const char *kSourceId = "wasapi_process_output_capture";
#elif defined(__APPLE__)
constexpr const char *kSourceId = "sck_audio_capture";
#else
constexpr const char *kSourceId = "";
#endif

obs_data_t *captureSettings(const QString &target)
{
	obs_data_t *settings = obs_data_create();
#ifdef _WIN32
	// "título:clase:exe" con prioridad por ejecutable: sirve con cualquier ventana de la app.
	obs_data_set_string(settings, "window", ("::" + target).toUtf8().constData());
	obs_data_set_int(settings, "priority", 2); // WINDOW_PRIORITY_EXE
#elif defined(__APPLE__)
	obs_data_set_int(settings, "type", 1); // ScreenCaptureAudioApplicationStream
	obs_data_set_string(settings, "application", target.toUtf8().constData());
#else
	(void)target;
#endif
	return settings;
}

// En macOS la captura de OBS (ScreenCaptureKit) busca la app en una lista de apps que
// arma al crearse la fuente o al abrir sus Propiedades, no al cambiarle la app: una app
// abierta después (o sin ventana a la vista en ese momento) no está en esa lista y la
// captura queda en silencio sin avisar. Aplicar los ajustes a sus propiedades corre el
// callback de "Método", que vuelve a pedir la lista y llena la de "Aplicación": así se
// refresca la lista que usa la captura y se sabe si la app está. En Windows no hace falta.
bool refreshAndFindApp(obs_source_t *source, obs_data_t *settings, const QString &target)
{
#ifdef __APPLE__
	obs_properties_t *props = obs_source_properties(source);
	if (!props)
		return true;
	obs_properties_apply_settings(props, settings);
	obs_property_t *apps = obs_properties_get(props, "application");
	bool found = !apps; // si OBS cambió las propiedades, no se bloquea el cambio
	for (size_t i = 0; apps && !found && i < obs_property_list_item_count(apps); i++)
		found = target.compare(QString::fromUtf8(obs_property_list_item_string(apps, i)), Qt::CaseInsensitive) == 0;
	obs_properties_destroy(props);
	return found;
#else
	(void)source;
	(void)settings;
	(void)target;
	return true;
#endif
}

// A qué app apunta hoy la captura.
QString currentTarget(obs_source_t *source)
{
	obs_data_t *settings = obs_source_get_settings(source);
#ifdef _WIN32
	QString target = QString::fromUtf8(obs_data_get_string(settings, "window")).section(':', 2);
#elif defined(__APPLE__)
	QString target = QString::fromUtf8(obs_data_get_string(settings, "application"));
#else
	QString target = QString::fromUtf8(obs_data_get_string(settings, "TargetName"));
#endif
	obs_data_release(settings);
	return target;
}

} // namespace

void follow_music_channel()
{
	// Solo lo que está sonando: una app en pausa no se lleva el canal.
	Track track = MediaWatcher::instance().current(false);
	if (!track.valid())
		return;
	QString target = audioTarget(track);
	// El video de la tarjeta suena dentro de OBS (sin sonido): nunca es la música.
	if (target.isEmpty() || target.contains("obsproject", Qt::CaseInsensitive) ||
	    target.compare("obs64.exe", Qt::CaseInsensitive) == 0 || target.compare("obs", Qt::CaseInsensitive) == 0)
		return;

	QByteArray name = text("NowPlaying.Channel.SourceName").toUtf8();
	obs_source_t *source = obs_get_source_by_name(name.constData());
	if (!source)
		return;
	// Solo el canal que creó el plugin (no otra fuente que se llame igual).
	if (strcmp(obs_source_get_id(source), kSourceId) == 0 &&
	    currentTarget(source).compare(target, Qt::CaseInsensitive) != 0) {
		// Si la app todavía no aparece en la lista de macOS, se reintenta cada 3 segundos
		// (sin tocar el canal, que si no quedaría en silencio).
		static qint64 lastTry = 0;
		qint64 now = MediaWatcher::now();
		if (now - lastTry >= 3000) {
			lastTry = now;
			obs_data_t *settings = captureSettings(target);
			if (refreshAndFindApp(source, settings, target)) {
				obs_source_update(source, settings);
				blog(LOG_INFO, "[obs-now-playing] canal de música → %s", target.toUtf8().constData());
			} else {
				blog(LOG_INFO, "[obs-now-playing] canal de música: %s aún no se puede capturar, se reintenta",
				     target.toUtf8().constData());
			}
			obs_data_release(settings);
		}
	}
	obs_source_release(source);
}

void create_music_channel(QWidget *parent)
{
	Track track = MediaWatcher::instance().current(true);
	if (!track.valid()) {
		QMessageBox::information(parent, text("NowPlaying.Channel.Title"), text("NowPlaying.Channel.NothingPlaying"));
		return;
	}
	QString target = audioTarget(track);
	const char *sourceId = kSourceId;
	// La captura por aplicación existe en macOS 13+ y Windows 10 2004+ (OBS solo la registra
	// ahí).
	const char *unsupported = "NowPlaying.Channel.Unsupported";
	if (target.isEmpty() || !obs_source_get_display_name(sourceId)) {
		QMessageBox::warning(parent, text("NowPlaying.Channel.Title"),
				     target.isEmpty() ? text("NowPlaying.Channel.UnknownApp").arg(track.label)
						      : text(unsupported));
		return;
	}

	obs_data_t *settings = captureSettings(target);

	// Si el canal ya existe se reapunta a la app de ahora (de Spotify a YouTube Music…).
	QByteArray name = text("NowPlaying.Channel.SourceName").toUtf8();
	obs_source_t *source = obs_get_source_by_name(name.constData());
	bool created = !source;
	if (source) {
		refreshAndFindApp(source, settings, target);
		obs_source_update(source, settings);
	} else
		source = obs_source_create(sourceId, name.constData(), settings, nullptr);
	obs_data_release(settings);
	if (!source) {
		QMessageBox::warning(parent, text("NowPlaying.Channel.Title"), text(unsupported));
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
