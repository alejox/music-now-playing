#include "music-channel.hpp"
#include "media-watcher.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QAction>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QPointer>
#include <QRegularExpression>
#include <QStringList>

#include <cstring>
#include <vector>

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
//   Linux:   el programa ("spotify", "firefox"), que es el nombre del reproductor MPRIS sin
//            su instancia ("firefox.instance_1_23" → "firefox"). La captura de PipeWire lo
//            compara con el binario y con el nombre de la app, sin mayúsculas.
QString audioTarget(const Track &track)
{
#ifdef _WIN32
	QString id = track.app.toLower();
	static const QList<QPair<QString, QString>> exes = {
		{"spotify", "Spotify.exe"}, {"msedge", "msedge.exe"}, {"chrome", "chrome.exe"},   {"firefox", "firefox.exe"},
		{"brave", "brave.exe"},     {"opera", "opera.exe"},   {"vivaldi", "vivaldi.exe"}, {"applemusic", "AppleMusic.exe"}, {"itunes", "iTunes.exe"},
		{"zunemusic", "Microsoft.Media.Player.exe"}, {"wmplayer", "wmplayer.exe"},
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
	id.remove(QRegularExpression("#.*$")); // "com.google.Chrome#ytmusic": una pestaña que suena por detrás
	return id;
#else
	return track.app.section('.', 0, 0).toLower();
#endif
}

#ifdef _WIN32
constexpr const char *kSourceId = "wasapi_process_output_capture";
#elif defined(__APPLE__)
constexpr const char *kSourceId = "sck_audio_capture";
#else
// OBS no trae captura por aplicación en Linux: la da el plugin obs-pipewire-audio-capture
// (github.com/dimtpap/obs-pipewire-audio-capture), si está instalado.
constexpr const char *kSourceId = "pipewire_audio_application_capture";
#endif

// Ajuste privado que marca la captura como el canal del plugin. Así se reconoce aunque
// cambie el idioma de OBS (el nombre está traducido) o el usuario la renombre, y nunca se
// confunde con una fuente propia del usuario que se llame igual.
constexpr const char *kChannelMarker = "obs_now_playing_channel";

bool hasMarker(obs_source_t *source)
{
	if (strcmp(obs_source_get_id(source), kSourceId) != 0)
		return false;
	obs_data_t *settings = obs_source_get_settings(source);
	bool marked = obs_data_get_bool(settings, kChannelMarker);
	obs_data_release(settings);
	return marked;
}

// El canal del plugin, con una referencia que hay que soltar (obs_source_release), o nullptr.
obs_source_t *findChannel()
{
	obs_source_t *found = nullptr;
	obs_enum_sources(
		[](void *param, obs_source_t *source) {
			if (!hasMarker(source))
				return true;
			*static_cast<obs_source_t **>(param) = obs_source_get_ref(source);
			return false;
		},
		&found);
	if (found)
		return found;

	// Canal creado por una versión anterior (sin la marca): una captura del mismo tipo con el
	// nombre traducido. Se marca para reconocerla de ahora en adelante. Una fuente que se
	// llame igual pero sea de otro tipo es del usuario y no se toca.
	QByteArray name = text("NowPlaying.Channel.SourceName").toUtf8();
	obs_source_t *source = obs_get_source_by_name(name.constData());
	if (!source)
		return nullptr;
	if (strcmp(obs_source_get_id(source), kSourceId) != 0) {
		obs_source_release(source);
		return nullptr;
	}
	obs_data_t *settings = obs_source_get_settings(source);
	obs_data_set_bool(settings, kChannelMarker, true);
	obs_data_release(settings);
	blog(LOG_INFO, "[obs-now-playing] canal de música de una versión anterior: se marca");
	return source;
}

// Un nombre libre para el canal nuevo: el traducido o, si otra fuente ya lo usa, con un número.
QByteArray freeChannelName()
{
	const QString base = text("NowPlaying.Channel.SourceName");
	for (int i = 1;; i++) {
		QByteArray name = (i == 1 ? base : QString("%1 %2").arg(base).arg(i)).toUtf8();
		obs_source_t *other = obs_get_source_by_name(name.constData());
		if (!other)
			return name;
		obs_source_release(other);
	}
}

obs_data_t *captureSettings(const QString &target)
{
	obs_data_t *settings = obs_data_create();
	obs_data_set_bool(settings, kChannelMarker, true);
#ifdef _WIN32
	// "título:clase:exe" con prioridad por ejecutable: sirve con cualquier ventana de la app.
	obs_data_set_string(settings, "window", ("::" + target).toUtf8().constData());
	obs_data_set_int(settings, "priority", 2); // WINDOW_PRIORITY_EXE
#elif defined(__APPLE__)
	obs_data_set_int(settings, "type", 1); // ScreenCaptureAudioApplicationStream
	obs_data_set_string(settings, "application", target.toUtf8().constData());
#else
	obs_data_set_int(settings, "CaptureMode", 0); // CAPTURE_MODE_SINGLE: una sola app
	obs_data_set_string(settings, "TargetName", target.toUtf8().constData());
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

// ---- El canal en un canal de salida de OBS (hilo principal de Qt) ----
//
// Igual que la voz de Multichat: la captura no está en ninguna escena sino en un canal de
// salida libre, así suena en todas las escenas y aparece en el Mezclador. Si está activado y
// su estado (volumen, silencio, monitoreo, balance, pistas, filtros) se guardan en la
// configuración del plugin (music-channel.json), no en la colección de escenas: OBS no
// guarda los canales de salida por encima del 6 y descarta al cargar una fuente que no está
// en ninguna escena, así que el canal se recrea con ese estado al abrir OBS o cambiar de
// colección.

// Canales de salida: el 0 es la transición y del 1 al 6 el audio de escritorio y los
// micrófonos de los Ajustes de OBS. El canal toma el más alto libre (como Multichat); un canal
// que usa otra cosa nunca se toca, así conviven los dos plugins.
constexpr uint32_t kFirstFreeChannel = 7;

obs_source_t *channelSource = nullptr; // referencia propia al canal (nullptr = sin canal)
uint32_t outputChannel = 0;            // canal de salida donde está (0 = ninguno)
bool enabled = false;                  // lo que eligió el usuario (apagado por defecto)
obs_data_t *savedState = nullptr;      // el último obs_save_source del canal
bool collectionReady = false;          // hay una colección de escenas cargada
bool shuttingDown = false;
bool createdFresh = false;             // la última sincronización creó un canal nuevo
QPointer<QAction> toolsAction;

QString configPath()
{
	char *path = obs_module_config_path("music-channel.json");
	QString result = QString::fromUtf8(path);
	bfree(path);
	return result;
}

void loadConfig()
{
	const QByteArray path = configPath().toUtf8();
	obs_data_t *data = obs_data_create_from_json_file_safe(path.constData(), "bak");
	if (!data)
		return;
	enabled = obs_data_get_bool(data, "enabled");
	obs_data_release(savedState);
	savedState = obs_data_has_user_value(data, "state") ? obs_data_get_obj(data, "state") : nullptr;
	obs_data_release(data);
}

void saveConfig()
{
	obs_data_t *data = obs_data_create();
	obs_data_set_bool(data, "enabled", enabled);
	if (savedState)
		obs_data_set_obj(data, "state", savedState);
	const QString path = configPath();
	QDir().mkpath(QFileInfo(path).absolutePath());
	if (!obs_data_save_json_safe(data, path.toUtf8().constData(), "tmp", "bak"))
		obs_log(LOG_WARNING, "no se pudo guardar %s", path.toUtf8().constData());
	obs_data_release(data);
}

void saveChannelState()
{
	if (!channelSource)
		return;
	obs_data_t *state = obs_save_source(channelSource);
	if (!state)
		return;
	obs_data_release(savedState);
	savedState = state;
	saveConfig();
}

void updateAction()
{
	if (toolsAction)
		toolsAction->setChecked(enabled);
}

// Un canal nuevo con el volumen, silencio, monitoreo, balance, pistas y filtros de la última
// vez; la primera vez, sin monitoreo (solo a la emisión) y apuntando a la app que suena.
obs_source_t *createChannel()
{
	const char *typeId = obs_get_latest_input_type_id(kSourceId);
	if (!typeId)
		return nullptr;
	const QByteArray name = freeChannelName();
	if (savedState) {
		// Copia profunda: lo guardado no se toca.
		obs_data_t *state = obs_data_create_from_json(obs_data_get_json(savedState));
		obs_data_erase(state, "uuid"); // uno nuevo: la fuente vieja podría seguir en otra parte
		obs_data_set_string(state, "name", name.constData());
		obs_data_set_string(state, "id", kSourceId);
		obs_data_set_string(state, "versioned_id", typeId);
		obs_data_t *settings = obs_data_get_obj(state, "settings");
		if (!settings) {
			settings = obs_data_create();
			obs_data_set_obj(state, "settings", settings);
		}
		obs_data_set_bool(settings, kChannelMarker, true);
		obs_data_release(settings);
		obs_source_t *source = obs_load_source(state);
		obs_data_release(state);
		if (source) {
			obs_source_load2(source);
			return source;
		}
		obs_log(LOG_WARNING, "no se pudo restaurar el canal de música guardado; se crea uno nuevo");
	}

	MediaWatcher &watcher = MediaWatcher::instance();
	const Track track = watcher.current(true, watcher.player());
	obs_data_t *settings = captureSettings(track.valid() ? audioTarget(track) : QString());
	obs_source_t *source = obs_source_create(typeId, name.constData(), settings, nullptr);
	obs_data_release(settings);
	if (source) {
		// OBS 33 cambió los tipos de monitoreo por un interruptor (como hace Multichat).
#if LIBOBS_API_MAJOR_VER >= 33
		obs_source_set_monitoring_enabled(source, false);
#else
		obs_source_set_monitoring_type(source, OBS_MONITORING_TYPE_NONE);
#endif
		createdFresh = true;
	}
	return source;
}

// El canal en un canal de salida libre, para que suene en todas las escenas y salga en el
// Mezclador. Un canal que usa otra cosa nunca se toca.
void bindChannel()
{
	if (outputChannel) {
		obs_source_t *current = obs_get_output_source(outputChannel);
		const bool ours = current == channelSource;
		obs_source_release(current);
		if (ours)
			return;
		outputChannel = 0;
	}
	for (uint32_t channel = MAX_CHANNELS - 1; channel >= kFirstFreeChannel; --channel) {
		obs_source_t *current = obs_get_output_source(channel);
		if (current) {
			obs_log(LOG_INFO, "el canal de salida %u lo usa \"%s\"; se prueba el siguiente", channel,
				obs_source_get_name(current));
			obs_source_release(current);
			continue;
		}
		obs_set_output_source(channel, channelSource);
		outputChannel = channel;
		obs_log(LOG_INFO, "canal de música \"%s\" en el canal de salida %u", obs_source_get_name(channelSource),
			channel);
		return;
	}
	obs_log(LOG_WARNING, "no hay un canal de salida libre para el canal de música: no se oirá");
}

// Suelta el canal de salida y la referencia, guardando antes su estado. remove = además lo
// quita de la colección (se apagó); si no, OBS está por vaciar la colección él mismo.
void releaseChannel(bool remove)
{
	if (!channelSource)
		return;
	saveChannelState();
	if (outputChannel) {
		obs_source_t *current = obs_get_output_source(outputChannel);
		if (current == channelSource)
			obs_set_output_source(outputChannel, nullptr);
		obs_source_release(current);
		outputChannel = 0;
	}
	if (remove)
		obs_source_remove(channelSource);
	obs_source_release(channelSource);
	channelSource = nullptr;
}

// Pone el canal de acuerdo con lo elegido: lo adopta o lo crea y lo pone en un canal de
// salida si está activado; lo quita si no.
void syncChannel()
{
	if (!collectionReady || shuttingDown)
		return;
	if (!enabled) {
		releaseChannel(true);
		return;
	}
	if (!channelSource) {
		channelSource = findChannel();
		if (!channelSource)
			channelSource = createChannel();
		if (!channelSource) {
			obs_log(LOG_WARNING, "no se pudo crear el canal de música");
			return;
		}
	}
	bindChannel();
}

// Versiones anteriores ponían el canal en una escena. Si la colección lo tiene en alguna, se
// saca de las escenas (la fuente queda, con sus ajustes y filtros) y pasa a un canal de
// salida. Solo el canal del plugin: una fuente sin la marca no se toca.
void migrateSceneChannel()
{
	obs_source_t *found = findChannel();
	if (!found)
		return;
	if (channelSource && channelSource != found) {
		obs_source_release(found);
		return;
	}
	struct Search {
		obs_source_t *source;
		std::vector<obs_sceneitem_t *> items;
	} search{found, {}};
	obs_enum_scenes(
		[](void *param, obs_source_t *sceneSource) {
			obs_scene_t *scene = obs_group_or_scene_from_source(sceneSource);
			if (!scene)
				return true;
			obs_scene_enum_items(
				scene,
				[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
					auto *s = static_cast<Search *>(param);
					if (obs_sceneitem_get_source(item) == s->source) {
						obs_sceneitem_addref(item);
						s->items.push_back(item);
					}
					return true;
				},
				param);
			return true;
		},
		&search);
	if (search.items.empty()) {
		obs_source_release(found);
		return;
	}
	QStringList scenes;
	for (obs_sceneitem_t *item : search.items) {
		scenes << QString::fromUtf8(obs_source_get_name(obs_scene_get_source(obs_sceneitem_get_scene(item))));
		obs_sceneitem_remove(item);
		obs_sceneitem_release(item);
	}
	obs_log(LOG_INFO, "canal de música \"%s\" migrado: se quitó de la(s) escena(s) %s y pasa a un canal de salida",
		obs_source_get_name(found), scenes.join(", ").toUtf8().constData());
	if (channelSource)
		obs_source_release(found);
	else
		channelSource = found;
	enabled = true;
	saveChannelState();
	updateAction();
}

void onSave(obs_data_t *, bool saving, void *)
{
	if (saving)
		saveChannelState();
}

} // namespace

QString audio_target(const Track &track)
{
	return audioTarget(track);
}

void follow_music_channel()
{
	// Solo el canal del plugin, si está activado.
	if (!channelSource)
		return;
	// Solo lo que está sonando (del reproductor elegido en el dock): una app en pausa no se
	// lleva el canal.
	MediaWatcher &watcher = MediaWatcher::instance();
	Track track = watcher.current(false, watcher.player());
	if (!track.valid())
		return;
	QString target = audioTarget(track);
	// El video de la tarjeta suena dentro de OBS (sin sonido): nunca es la música.
	if (target.isEmpty() || target.contains("obsproject", Qt::CaseInsensitive) ||
	    target.compare("obs64.exe", Qt::CaseInsensitive) == 0 || target.compare("obs", Qt::CaseInsensitive) == 0)
		return;

	obs_source_t *source = channelSource;
	if (currentTarget(source).compare(target, Qt::CaseInsensitive) != 0) {
		// Si la app todavía no aparece en la lista de macOS, se reintenta cada 3 segundos
		// (sin tocar el canal, que si no quedaría en silencio).
		static qint64 lastTry = 0;
		qint64 now = MediaWatcher::now();
		if (now - lastTry >= 3000) {
			lastTry = now;
			obs_data_t *settings = captureSettings(target);
			if (refreshAndFindApp(source, settings, target)) {
				obs_source_update(source, settings);
				obs_log(LOG_INFO, "canal de música → %s", target.toUtf8().constData());
			} else {
				obs_log(LOG_INFO, "canal de música: %s aún no se puede capturar, se reintenta",
					target.toUtf8().constData());
			}
			obs_data_release(settings);
		}
	}
}

void music_channel_init()
{
	loadConfig();
	auto *action = static_cast<QAction *>(obs_frontend_add_tools_menu_qaction(obs_module_text("NowPlaying.Channel.Title")));
	if (action) {
		action->setCheckable(true);
		action->setChecked(enabled);
		QObject::connect(action, &QAction::triggered, action, [](bool checked) {
			set_music_channel_enabled(checked, static_cast<QWidget *>(obs_frontend_get_main_window()));
		});
		toolsAction = action;
	}
	obs_frontend_add_save_callback(onSave, nullptr);
}

void music_channel_collection_loaded()
{
	collectionReady = true;
	migrateSceneChannel();
	syncChannel();
}

void music_channel_collection_unloading()
{
	collectionReady = false;
	releaseChannel(false);
}

void music_channel_shutdown()
{
	releaseChannel(false); // normalmente ya se hizo cuando OBS vació la colección
	shuttingDown = true;
	obs_frontend_remove_save_callback(onSave, nullptr);
	obs_data_release(savedState);
	savedState = nullptr;
}

bool music_channel_enabled()
{
	return enabled;
}

void set_music_channel_enabled(bool on, QWidget *parent)
{
	// La captura por aplicación existe en macOS 13+ y Windows 10 2004+ (OBS solo la registra
	// ahí); en Linux, solo con el plugin de PipeWire instalado.
	if (on && !obs_get_latest_input_type_id(kSourceId)) {
#ifdef __linux__
		const char *unsupported = "NowPlaying.Channel.NeedsPipeWire";
#else
		const char *unsupported = "NowPlaying.Channel.Unsupported";
#endif
		updateAction();
		QMessageBox::warning(parent, text("NowPlaying.Channel.Title"), text(unsupported));
		return;
	}
	if (on != enabled) {
		enabled = on;
		saveConfig();
		createdFresh = false;
		syncChannel();
		obs_log(LOG_INFO, "canal de música %s", on ? "activado" : "desactivado");
		if (on && createdFresh && channelSource)
			QMessageBox::information(parent, text("NowPlaying.Channel.Title"),
						 text("NowPlaying.Channel.Enabled")
							 .arg(QString::fromUtf8(obs_source_get_name(channelSource))));
		if (on)
			follow_music_channel();
	}
	updateAction();
}
