#include "media-watcher.hpp"
#include "app-volume.hpp"
#include "music-channel.hpp"
#include "text-source-reader.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>

namespace {

QString moduleFile(const char *relative)
{
	// Ruta completa: en Windows, con el plugin en la carpeta de OBS, llega relativa a bin\64bit.
	char *path = obs_module_file(relative);
	QString result = path ? QFileInfo(QString::fromUtf8(path)).absoluteFilePath() : QString();
	bfree(path);
	return result;
}

struct AppInfo {
	QString kind;
	QString label;
};

// De qué reproductor viene una sesión: decide el color y el "Escuchando en …". En un
// navegador, site (del lector, en modo video) dice si la pestaña es YouTube, YouTube Music
// o el reproductor web de Spotify.
AppInfo classifyApp(const QString &app, const QString &name, const QString &site)
{
	const QString id = (app + " " + name).toLower();
	if (id.contains("spotify"))
		return {"spotify", "Spotify"};
	// La PWA de YouTube Music instalada desde Chrome/Edge tiene este id de extensión.
	if (id.contains("cinhimbnkkaeohfgghhklpknlkffjgod") || id.contains("youtube music"))
		return {"ytmusic", "YouTube Music"};
	// Apple Music: la app de Mac, la de Windows (AppleInc.AppleMusicWin…) y, en Windows sin
	// ella, iTunes (cuenta como Apple Music para "Solo Apple Music").
	if (id.contains("applemusic") || id.contains("com.apple.music"))
		return {"applemusic", "Apple Music"};
	if (id.contains("itunes"))
		return {"applemusic", "iTunes"};
	// El Reproductor multimedia de Windows (el nuevo, Microsoft.ZuneMusic, y el clásico).
	if (id.contains("zunemusic") || id.contains("wmplayer"))
		return {"system", "Reproductor multimedia"};
	if (id.contains("tidal"))
		return {"other", "TIDAL"};
	if (id.contains("deezer"))
		return {"other", "Deezer"};
	static const QList<QPair<QString, QString>> browsers = {
		{"chrome", "Chrome"}, {"msedge", "Edge"},     {"firefox", "Firefox"}, {"brave", "Brave"},
		{"opera", "Opera"},   {"vivaldi", "Vivaldi"}, {"safari", "Safari"},   {"thebrowser", "Arc"},
#ifdef __linux__
		// En Linux el reproductor MPRIS de Chrome y Chromium se llama "chromium".
		{"chromium", "Chromium"},
#endif
	};
	for (const auto &browser : browsers) {
		if (!id.contains(browser.first))
			continue;
		if (site == "ytmusic")
			return {"ytmusic", "YouTube Music"};
		if (site == "youtube")
			return {"youtube", "YouTube"};
		if (site == "spotify")
			return {"spotify", "Spotify"};
		return {"browser", browser.second};
	}
	QString fallback = name;
	if (fallback.isEmpty()) {
		fallback = app.section(QRegularExpression("[\\\\/!]"), -1);
		fallback.remove(QRegularExpression("\\.exe$", QRegularExpression::CaseInsensitiveOption));
	}
	return {"other", fallback.isEmpty() ? QString("Otra app") : fallback};
}

constexpr qint64 kStaleMs = 10000;

QString playerFile()
{
	char *path = obs_module_config_path("player.txt");
	QString result = QString::fromUtf8(path);
	bfree(path);
	return result;
}

// La sesión de la radio desde un archivo (no es una app: el lector del sistema no la conoce).
const QString kRadioApp = QStringLiteral("radio-file");
constexpr qint64 kRadioMaxBytes = 64 * 1024;

QString radioFile()
{
	char *path = obs_module_config_path("radio.json");
	QString result = QString::fromUtf8(path);
	bfree(path);
	return result;
}

RadioSettings loadRadio()
{
	RadioSettings settings;
	QFile file(radioFile());
	if (!file.open(QIODevice::ReadOnly))
		return settings;
	const QJsonObject o = QJsonDocument::fromJson(file.readAll()).object();
	settings.enabled = o.value("enabled").toBool();
	settings.path = o.value("path").toString();
	settings.separator = o.value("separator").toString(settings.separator);
	settings.titleFirst = o.value("order").toString() == "title-artist";
	settings.pattern = o.value("pattern").toString();
	settings.idleMinutes = std::max(0, o.value("idleMinutes").toInt());
	settings.label = o.value("label").toString();
	return settings;
}

void saveRadio(const RadioSettings &settings)
{
	const QJsonObject o{
		{"enabled", settings.enabled},     {"path", settings.path},
		{"separator", settings.separator}, {"order", settings.titleFirst ? "title-artist" : "artist-title"},
		{"pattern", settings.pattern},     {"idleMinutes", settings.idleMinutes},
		{"label", settings.label},
	};
	QDir().mkpath(QFileInfo(radioFile()).absolutePath());
	QSaveFile file(radioFile());
	if (file.open(QIODevice::WriteOnly)) {
		file.write(QJsonDocument(o).toJson());
		file.commit();
	}
}

} // namespace

qint64 Track::progressNow(qint64 now) const
{
	qint64 elapsed = isPlaying ? now - updatedAt : 0;
	qint64 value = progressMs + elapsed;
	return durationMs > 0 ? std::min(durationMs, value) : value;
}

qint64 MediaWatcher::now()
{
	static QElapsedTimer clock = [] {
		QElapsedTimer t;
		t.start();
		return t;
	}();
	return clock.elapsed();
}

MediaWatcher &MediaWatcher::instance()
{
	static MediaWatcher *watcher = new MediaWatcher();
	return *watcher;
}

void MediaWatcher::start()
{
	stopping_ = false;
#if defined(_WIN32) || defined(__APPLE__) || defined(__linux__)
	// Los comandos para el lector de Windows van por esta carpeta (ver media/windows.ps1).
	QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
	commandDir_ = QDir(dir).filePath("obs-now-playing-commands");
	videoFlagFile_ = QDir(dir).filePath("obs-now-playing-video.flag");
	othersFlagFile_ = QDir(dir).filePath("obs-now-playing-others.flag");
	QFile saved(playerFile());
	if (saved.open(QIODevice::ReadOnly)) {
		QString player = QString::fromUtf8(saved.readAll()).trimmed();
		player_ = players().contains(player) ? player : QString();
	}
	writeVideoFlag();
	launch();
#else
	setStatus(Status::Unsupported);
#endif
	radio_ = loadRadio();
	if (!radio_.enabled && player_ == "radio")
		player_.clear();
	startRadio();
}

void MediaWatcher::stop()
{
	stopping_ = true;
	if (radioTimer_)
		radioTimer_->stop();
	if (process_) {
		process_->disconnect(this);
		process_->kill();
		process_->waitForFinished(1000);
		delete process_;
		process_ = nullptr;
	}
}

void MediaWatcher::launch()
{
	// Con un lector andando no se abre otro (un reinicio pendiente tras el botón Refrescar).
	if (stopping_ || process_)
		return;

	QString program;
	QStringList args;
#ifdef _WIN32
	QString systemRoot = qEnvironmentVariable("SystemRoot", "C:\\Windows");
	program = systemRoot + "\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
	args = {"-NoProfile",
		"-NonInteractive",
		"-ExecutionPolicy",
		"Bypass",
		"-File",
		QDir::toNativeSeparators(moduleFile("media/windows.ps1")),
		"-CommandDir",
		QDir::toNativeSeparators(commandDir_),
		"-VideoFlag",
		QDir::toNativeSeparators(videoFlagFile_)};
	// Un comando viejo no se ejecuta al arrancar.
	QDir(commandDir_).removeRecursively();
	QDir().mkpath(commandDir_);
#elif defined(__APPLE__)
	// Desde macOS 15.4 solo programas del sistema pueden leer "Ahora suena": por eso
	// el lector corre en osascript y no dentro de OBS.
	program = "/usr/bin/osascript";
	args = {"-l", "JavaScript", moduleFile("media/macos.js"), "watch", videoFlagFile_, othersFlagFile_};
#else
	// Linux: los reproductores MPRIS por medio de playerctl (ver media/linux.sh). Con bash
	// y no ejecutando el archivo: así no importa si perdió el permiso de ejecución.
	program = "bash";
	args = {moduleFile("media/linux.sh"), "watch", videoFlagFile_};
#endif

	process_ = new QProcess(this);
	process_->setProgram(program);
	process_->setArguments(args);
	process_->setProcessChannelMode(QProcess::SeparateChannels);
	connect(process_, &QProcess::readyReadStandardOutput, this, &MediaWatcher::readLines);
	connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
		if (!process_)
			return;
		setStatus(Status::Error, process_->errorString());
		// Si ni siquiera arrancó, "finished" no llega: reintentamos desde aquí.
		if (error == QProcess::FailedToStart) {
			process_->deleteLater();
			process_ = nullptr;
			failures_++;
			if (!stopping_)
				QTimer::singleShot(std::min(60000, 1000 << std::min(failures_, 6)), this,
						   &MediaWatcher::launch);
		}
	});
	connect(process_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
		QString stderrText = QString::fromUtf8(process_->readAllStandardError()).trimmed();
		process_->deleteLater();
		process_ = nullptr;
		if (stopping_)
			return;
		// Los lectores se reinician solos cada ~30 min (código 0) para no acumular memoria.
		bool quick = now() - startedAt_ < 15000;
		failures_ = quick ? failures_ + 1 : 0;
		if (quick && code != 0 && status_ != Status::Error) {
			QString last = stderrText.section('\n', -1);
			setStatus(Status::Error, last.isEmpty() ? QString("código %1").arg(code) : last);
		}
		int wait = quick ? std::min(60000, 1000 << std::min(failures_, 6)) : 500;
		QTimer::singleShot(wait, this, &MediaWatcher::launch);
	});

	startedAt_ = now();
	process_->start();
	obs_log(LOG_INFO, "lector iniciado: %s", program.toUtf8().constData());
}

void MediaWatcher::readLines()
{
	while (process_ && process_->canReadLine())
		handleLine(process_->readLine().trimmed());
}

void MediaWatcher::setStatus(Status status, const QString &error)
{
	if (status == Status::Error && error != error_)
		obs_log(LOG_WARNING, "lector del sistema: %s", error.toUtf8().constData());
	status_ = status;
	error_ = error;
	emit updated();
}

void MediaWatcher::handleLine(const QByteArray &line)
{
	QJsonParseError parseError;
	QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
	if (parseError.error != QJsonParseError::NoError || !doc.isObject())
		return;
	QJsonObject msg = doc.object();

	// El reproductor no hizo caso a un comando (p. ej. no deja adelantar): no es un error
	// del lector, se avisa en el dock.
	if (msg.contains("commandError")) {
		emit commandFailed(msg.value("commandError").toString().left(300));
		return;
	}
	if (msg.contains("error")) {
		setStatus(Status::Error, msg.value("error").toString().left(500));
		return;
	}

	qint64 t = now();
	QSet<QString> seen;
	const QJsonArray sessions = msg.value("sessions").toArray();
	for (qsizetype i = 0; i < sessions.size(); ++i) {
		QJsonObject s = sessions.at(i).toObject();
		QString app = s.value("app").toString().left(200);
		QString title = s.value("title").toString().trimmed().left(300);
		if (app.isEmpty() || title.isEmpty())
			continue;
#ifdef _WIN32
		// El video de la tarjeta suena dentro de OBS (sin sonido): nunca es la música.
		if (app.compare("obs64.exe", Qt::CaseInsensitive) == 0)
			continue;
#endif

		Track track;
		AppInfo info = classifyApp(app, s.value("name").toString().left(100), s.value("site").toString());
		track.app = app;
		track.kind = info.kind;
		track.label = info.label;
		track.title = title;
		track.artist = s.value("artist").toString().trimmed().left(300);
		track.album = s.value("album").toString().trimmed().left(300);
		track.isPlaying = s.value("isPlaying").toBool();
		track.isCurrent = s.value("current").toBool();
		track.progressMs = std::max<qint64>(0, s.value("progressMs").toInteger());
		track.durationMs = std::max<qint64>(0, s.value("durationMs").toInteger());
		static const QRegularExpression videoIdPattern("^[A-Za-z0-9_-]{11}$");
		QString videoId = s.value("videoId").toString();
		if (videoIdPattern.match(videoId).hasMatch())
			track.videoId = videoId;
		track.updatedAt = t;
		// Windows: el aleatorio llega con cada lectura (si el reproductor lo deja cambiar).
		if (s.contains("shuffle")) {
			QString shuffle = s.value("shuffle").isBool() ? (s.value("shuffle").toBool() ? "on" : "off") : QString();
			if (states_.value(app).shuffle != shuffle) {
				states_[app].shuffle = shuffle;
				emit playerStateChanged();
			}
		}

		// La carátula llega solo cuando cambia la canción: la guardamos con su canción
		// para no mostrar la de la anterior mientras llega la nueva.
		QString artKey = app + '\n' + track.title + '\n' + track.artist;
		if (s.contains("artwork")) {
			QString art = s.value("artwork").toString();
			artworkOrder_.removeAll(artKey);
			if (art.startsWith("data:image/")) {
				artworks_.insert(artKey, art);
				artworkOrder_.append(artKey);
			} else {
				artworks_.remove(artKey);
			}
			evictArtworks(artKey);
		}
		track.artwork = artworks_.value(artKey);

		const Track prev = tracks_.value(app);
		// Una vez por canción: si llegó el video de YouTube (para saber qué pasó si no se ve).
		if (!track.videoId.isEmpty() && track.videoId != prev.videoId)
			obs_log(LOG_INFO, "video de YouTube para %s (%s): %s", app.toUtf8().constData(),
				track.title.left(80).toUtf8().constData(), track.videoId.toUtf8().constData());
		bool changed = !prev.valid() || prev.title != track.title || prev.artist != track.artist ||
			       (!prev.isPlaying && track.isPlaying);
		track.changedAt = changed ? t : prev.changedAt;
		// Una vez por canción: si sigue sin carátula después de un rato sonando (para los
		// avisos de "se quedó sin carátula").
		if (track.artwork.isEmpty() && track.isPlaying && t - track.changedAt >= kArtMissingLogMs &&
		    !artMissingLogged_.contains(artKey)) {
			if (artMissingLogged_.size() >= 200)
				artMissingLogged_.clear();
			artMissingLogged_.insert(artKey);
			obs_log(LOG_INFO, "sin carátula tras %d s: %s (%s), video: %s", int(kArtMissingLogMs / 1000),
				app.toUtf8().constData(), track.title.left(80).toUtf8().constData(),
				track.videoId.isEmpty() ? "ninguno" : track.videoId.toUtf8().constData());
		}
		tracks_.insert(app, track);
		seen.insert(app);
	}
	// Las sesiones que el sistema ya no reporta (app cerrada) desaparecen. La radio no viene
	// del lector: la maneja su propio sondeo.
	for (auto it = tracks_.begin(); it != tracks_.end();) {
		if (seen.contains(it.key()) || it.key() == kRadioApp)
			++it;
		else
			it = tracks_.erase(it);
	}

	status_ = Status::Ok;
	error_.clear();
	emit updated();
}

void MediaWatcher::restartReader()
{
	obs_log(LOG_INFO, "refresco manual");
	// Lo que suena vuelve a buscar su carátula y su video: el lector nuevo los manda de nuevo.
	for (Track &track : tracks_) {
		const QString artKey = track.app + '\n' + track.title + '\n' + track.artist;
		artworks_.remove(artKey);
		artworkOrder_.removeAll(artKey);
		artMissingLogged_.remove(artKey);
		track.artwork.clear();
		track.videoId.clear();
	}
	failures_ = 0;
	if (process_) {
		process_->disconnect(this);
		process_->kill();
		process_->waitForFinished(1000);
		delete process_;
		process_ = nullptr;
	}
	if (status_ != Status::Unsupported)
		launch();
	emit updated();
}

void MediaWatcher::evictArtworks(const QString &keep)
{
	if (artworks_.size() <= kMaxArtworks)
		return;
	// Las carátulas de las canciones que están en una sesión viva no se borran: el lector la
	// manda una sola vez por canción y no volvería a llegar.
	QSet<QString> live{keep};
	for (const Track &track : tracks_)
		live.insert(track.app + '\n' + track.title + '\n' + track.artist);
	// artworkOrder_ va de la más vieja a la más nueva: se borran primero las viejas.
	for (qsizetype i = 0; i < artworkOrder_.size() && artworks_.size() > kMaxArtworks;) {
		const QString key = artworkOrder_.at(i);
		if (live.contains(key)) {
			++i;
			continue;
		}
		artworks_.remove(key);
		artworkOrder_.removeAt(i);
	}
}

Track MediaWatcher::current(bool includePaused, const QString &player) const
{
	qint64 t = now();
	QList<Track> alive;
	for (const Track &track : tracks_) {
		const bool wanted = player.isEmpty() || track.kind == player ||
				    (player == "system" && track.kind == "applemusic");
		if (t - track.updatedAt <= kStaleMs && wanted)
			alive.append(track);
	}
	auto newestFirst = [](const Track &a, const Track &b) {
		if (a.changedAt != b.changedAt)
			return a.changedAt > b.changedAt;
		return a.isCurrent && !b.isCurrent;
	};
	std::sort(alive.begin(), alive.end(), newestFirst);
	for (const Track &track : alive) {
		if (track.isPlaying)
			return track;
	}
	return includePaused && !alive.isEmpty() ? alive.first() : Track();
}

const QStringList &MediaWatcher::players()
{
	// system: el reproductor del sistema (Apple Music en Mac; en Windows, el Reproductor
	// multimedia y también Apple Music o iTunes). radio: la radio desde un archivo (el dock la
	// muestra solo si está activada).
	static const QStringList list = {"spotify", "ytmusic", "youtube", "system", "radio"};
	return list;
}

void MediaWatcher::setPlayer(const QString &player)
{
	QString value = players().contains(player) ? player : QString();
	if (value == player_)
		return;
	player_ = value;
	QDir().mkpath(QFileInfo(playerFile()).absolutePath());
	QSaveFile file(playerFile());
	if (file.open(QIODevice::WriteOnly)) {
		file.write(player_.toUtf8());
		file.commit();
	}
	obs_log(LOG_INFO, "reproductor del dock: %s", player_.isEmpty() ? "automático" : player_.toUtf8().constData());
	writeVideoFlag();
	emit updated();
}

void MediaWatcher::setVideoWanted(bool wanted)
{
	videoWanted_ = wanted;
	writeVideoFlag();
}

void MediaWatcher::setOthersWanted(bool wanted)
{
	othersWanted_ = wanted;
	writeVideoFlag();
}

void MediaWatcher::writeVideoFlag()
{
	if (videoFlagFile_.isEmpty())
		return;
	// Con un reproductor elegido (dock o fuente), en macOS hay que buscar lo que suena por
	// detrás: si suenan dos a la vez, macOS anuncia solo uno. La radio no viene del lector.
	const bool playerChosen = !player_.isEmpty() && player_ != "radio";
	if (othersWanted_ || playerChosen) {
		QFile others(othersFlagFile_);
		if (!others.exists() && others.open(QIODevice::WriteOnly))
			others.close();
	} else {
		QFile::remove(othersFlagFile_);
	}
	// Con un reproductor elegido en el dock, el lector tiene que distinguir las pestañas.
	bool wanted = videoWanted_ || playerChosen;
	if (!wanted) {
		QFile::remove(videoFlagFile_);
		return;
	}
	QFile flag(videoFlagFile_);
	if (!flag.exists() && flag.open(QIODevice::WriteOnly))
		flag.close();
}

void MediaWatcher::sendCommand(const QString &action)
{
	runCommand(action, -1);
}

void MediaWatcher::seekTo(qint64 positionMs)
{
	runCommand("seek", std::max<qint64>(0, positionMs));
}

PlayerState MediaWatcher::playerState() const
{
	return states_.value(current(true, player_).app);
}

void MediaWatcher::storeState(const QString &app, const QJsonObject &state, bool takeVolume)
{
	PlayerState value = states_.value(app);
	if (takeVolume)
		value.volume = state.value("volume").isDouble() ? std::clamp(state.value("volume").toInt(), 0, 100) : -1;
	QJsonValue shuffle = state.value("shuffle");
	value.shuffle = shuffle.isBool() ? (shuffle.toBool() ? "on" : "off")
					 : (shuffle.toString() == "action" ? QString("action") : QString());
	states_.insert(app, value);
	emit playerStateChanged();
}

void MediaWatcher::refreshPlayerState()
{
	Track track = current(true, player_);
	// La radio no tiene volumen ni aleatorio: se controla desde su propio programa.
	if (!track.valid() || track.kind == "radio")
		return;
	if (stateAsked_)
		return;
	stateAsked_ = true;
#ifdef _WIN32
	// El volumen del Mezclador se lee en otro hilo (ver app-volume.hpp); la interfaz no espera.
	const QString app = track.app;
	app_volume_read(audio_target(track), this, [this, app](int volume) {
		stateAsked_ = false;
		// Los pedidos van en orden: una lectura que vuelve mientras cambia el volumen se pidió
		// antes del cambio y trae el nivel viejo (la barra volvería atrás).
		if (volumeBusy_ || pendingVolume_ >= 0)
			return;
		if (states_.value(app).volume != volume) {
			states_[app].volume = volume;
			emit playerStateChanged();
		}
	});
#else
	runCommand("state", -1);
#endif
}

void MediaWatcher::setVolume(int percent)
{
	Track track = current(true, player_);
	if (!track.valid() || track.kind == "radio")
		return;
	percent = std::clamp(percent, 0, 100);
	states_[track.app].volume = percent; // la barra no salta atrás mientras llega la respuesta
	// Al arrastrar la barra llegan varios cambios seguidos. Cada uno abre el lector aparte (en
	// Windows, un pedido al hilo del volumen) y, si van a la vez, pueden terminar en otro orden
	// y quedar aplicado uno viejo (el volumen "vuelve"). Por eso va uno a la vez y, al
	// terminar, se manda el último pedido.
	if (volumeBusy_) {
		pendingVolume_ = percent;
		return;
	}
	volumeBusy_ = true;
#ifdef _WIN32
	app_volume_set(audio_target(track), percent, this, [this](bool ok) {
		volumeBusy_ = false;
		volumeDoneAt_ = now();
		if (!ok)
			emit commandFailed(QString::fromUtf8(obs_module_text("NowPlaying.Volume.NoSound")));
		if (pendingVolume_ >= 0) {
			const int next = pendingVolume_;
			pendingVolume_ = -1;
			setVolume(next);
		}
	});
#else
	runCommand("volume", percent);
#endif
}

void MediaWatcher::toggleShuffle()
{
	runCommand("shuffle", -1);
}

void MediaWatcher::runCommand(const QString &action, qint64 arg)
{
	Track track = current(true, player_);
	// La radio se controla desde su propio programa: sus botones no hacen nada.
	if (!track.valid() || track.kind == "radio") {
		if (action == "state")
			stateAsked_ = false;
		if (action == "volume")
			volumeBusy_ = false;
		return;
	}
	if (action != "state" && action != "volume")
		obs_log(LOG_INFO, "comando %s para %s", action.toUtf8().constData(), track.app.toUtf8().constData());
#ifdef _WIN32
	// El lector revisa la carpeta cada 100 ms y atiende todos los comandos que encuentre, en el
	// orden de su número: un archivo por comando para que dos "siguiente" seguidos no se pisen.
	// QSaveFile lo escribe aparte y lo renombra al final: el lector nunca lee uno a medias.
	if (commandSeq_ == 0)
		commandSeq_ = QDateTime::currentMSecsSinceEpoch() * 1000;
	const QString name = QString("%1.json").arg(++commandSeq_, 20, 10, QChar('0'));
	QDir().mkpath(commandDir_);
	QSaveFile file(QDir(commandDir_).filePath(name));
	if (file.open(QIODevice::WriteOnly)) {
		QJsonObject cmd{{"cmd", action}, {"app", track.app}};
		if (arg >= 0)
			cmd.insert("positionMs", arg);
		file.write(QJsonDocument(cmd).toJson(QJsonDocument::Compact));
		file.commit();
	}
#else
	// macOS y Linux: el lector se abre aparte con "command <acción>" y responde una línea JSON.
	auto *command = new QProcess(this);
	const QString app = track.app;
	const qint64 startedAt = now();
	connect(command, &QProcess::finished, this, [this, command, action, app, startedAt](int, QProcess::ExitStatus) {
		QByteArray out = command->readAllStandardOutput().trimmed();
		command->deleteLater();
		QJsonObject result = QJsonDocument::fromJson(out.split('\n').last()).object();
		// Volumen y aleatorio: los devuelven "state" y también "volume" y "shuffle". El volumen
		// que trae la respuesta a "volume" se ignora (el reproductor puede no haberlo aplicado
		// todavía y contestar el de antes: la barra volvía atrás), y el de "state" también si se
		// preguntó mientras cambiaba o justo después.
		const bool takeVolume = action != "volume" &&
					!(action == "state" && (volumeBusy_ || startedAt < volumeDoneAt_ + 1500));
		if (result.value("state").isObject())
			storeState(app, result.value("state").toObject(), takeVolume);
		if (action == "volume") {
			volumeBusy_ = false;
			volumeDoneAt_ = now();
			if (pendingVolume_ >= 0) {
				const int next = pendingVolume_;
				pendingVolume_ = -1;
				setVolume(next);
			}
		}
		if (action == "state") {
			stateAsked_ = false;
			return; // una pregunta de cada pocos segundos: sin avisos ni registro
		}
		QString problem = result.value("error").toString();
		if (problem.isEmpty())
			problem = result.value("hint").toString();
		if (!problem.isEmpty()) {
			obs_log(LOG_WARNING, "botón: %s", problem.toUtf8().constData());
			emit commandFailed(problem);
		} else if (result.contains("via") && action != "volume") {
			// Por dónde llegó el botón (página de YouTube, la app, teclas de música): para
			// saber qué pasó si un reproductor no responde.
			obs_log(LOG_INFO, "botón por: %s", result.value("via").toString().toUtf8().constData());
		}
	});
#ifdef __APPLE__
	// Lo que muestra el dock (puede no ser lo que anuncia macOS: el lector también encuentra
	// lo que suena "por detrás"): el botón va a esa app y a la pestaña con esa canción.
	QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
	env.insert("OBS_NOW_PLAYING_APP", track.app);
	env.insert("OBS_NOW_PLAYING_TITLE", track.title);
	command->setProcessEnvironment(env);
	QString program = "/usr/bin/osascript";
	QStringList args{"-l", "JavaScript", moduleFile("media/macos.js"), "command", action};
#else
	// El reproductor de la sesión (spotify, firefox…): playerctl le manda el botón a ese.
	QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
	env.insert("OBS_NOW_PLAYING_PLAYER", track.app);
	command->setProcessEnvironment(env);
	QString program = "bash";
	QStringList args{moduleFile("media/linux.sh"), "command", action};
#endif
	if (arg >= 0)
		args << QString::number(arg);
	// Si ni siquiera arranca, "finished" no llega.
	connect(command, &QProcess::errorOccurred, this, [this, command, action](QProcess::ProcessError error) {
		if (error != QProcess::FailedToStart)
			return;
		if (action == "state")
			stateAsked_ = false;
		if (action == "volume")
			volumeBusy_ = false;
		command->deleteLater();
	});
	command->start(program, args);
#endif
}

QString MediaWatcher::radioPatternError(const QString &pattern)
{
	if (pattern.isEmpty())
		return QString();
	QRegularExpression regex(pattern);
	return regex.isValid() ? QString() : regex.errorString();
}

QString MediaWatcher::radioPath() const
{
	QString path = radio_.path.trimmed();
	if (path.isEmpty())
		return QString();
	if (path == "~" || path.startsWith("~/"))
		path = QDir::homePath() + path.mid(1);
	// Relativa: desde la carpeta del usuario (no desde la carpeta de OBS, que cambia).
	return QFileInfo(QDir::home(), path).absoluteFilePath();
}

void MediaWatcher::setRadio(const RadioSettings &settings)
{
	radio_ = settings;
	radio_.idleMinutes = std::max(0, radio_.idleMinutes);
	saveRadio(radio_);
	// Si estaba elegida en el dock y se apaga, vuelve a Automático.
	if (!radio_.enabled && player_ == "radio")
		setPlayer(QString());
	startRadio();
}

void MediaWatcher::startRadio()
{
	// Desde cero: otro archivo u otras reglas.
	radioStat_.clear();
	radioReadStat_.clear();
	radioPending_.clear();
	radioHasPending_ = false;
	radioSeen_ = false;
	radioTitle_.clear();
	radioArtist_.clear();
	const QString file = radioPath();
	if (!radio_.enabled || file.isEmpty()) {
		if (radioTimer_)
			radioTimer_->stop();
		if (tracks_.remove(kRadioApp))
			emit updated();
		return;
	}
	radioPattern_ = QRegularExpression();
	if (!radio_.pattern.isEmpty()) {
		radioPattern_ = QRegularExpression(radio_.pattern);
		if (!radioPattern_.isValid())
			obs_log(LOG_WARNING, "radio: expresión regular inválida (%s); se usa el separador",
				radioPattern_.errorString().toUtf8().constData());
	}
	obs_log(LOG_INFO, "radio desde el archivo: %s", file.toUtf8().constData());
	if (!radioTimer_) {
		radioTimer_ = new QTimer(this);
		radioTimer_->setInterval(1000);
		connect(radioTimer_, &QTimer::timeout, this, &MediaWatcher::pollRadio);
	}
	radioTimer_->start();
	pollRadio();
}

void MediaWatcher::pollRadio()
{
	const QString file = radioPath();
	QFileInfo info(file);
	if (file.isEmpty() || !info.isFile()) {
		// Todavía no existe (o se está reemplazando): se sigue intentando.
		radioStat_.clear();
		radioReadStat_.clear();
		radioHasPending_ = false;
		radioTitle_.clear();
		radioArtist_.clear();
		refreshRadioTrack();
		return;
	}
	const qint64 modified = info.lastModified().toMSecsSinceEpoch();
	const QString stat =
		QString("%1:%2:%3").arg(modified).arg(info.size()).arg(info.birthTime().toMSecsSinceEpoch());
	const bool settled = stat == radioStat_;
	radioStat_ = stat;
	if (stat != radioReadStat_) {
		QFile f(file);
		if (!f.open(QIODevice::ReadOnly)) {
			// Bloqueado por el programa que lo escribe: se reintenta en el próximo sondeo.
			refreshRadioTrack();
			return;
		}
		const std::string text = text_source::decode(f.read(kRadioMaxBytes).toStdString());
		if (!settled && (!radioHasPending_ || text != radioPending_)) {
			// Puede estar a medio escribir: se confirma en el próximo sondeo.
			radioPending_ = text;
			radioHasPending_ = true;
			refreshRadioTrack();
			return;
		}
		radioHasPending_ = false;
		radioPending_.clear();

		text_source::Options options;
		options.separator = radio_.separator.toStdString();
		options.titleFirst = radio_.titleFirst;
		if (radioPattern_.isValid() && !radio_.pattern.isEmpty()) {
			const QRegularExpression regex = radioPattern_;
			options.match = [regex](const std::string &line, std::string &artist, std::string &title) {
				const QRegularExpressionMatch m = regex.match(QString::fromStdString(line));
				if (!m.hasMatch())
					return false;
				artist = m.captured("artist").toStdString();
				title = m.captured("title").toStdString();
				return true;
			};
		}
		text_source::Song song;
		const bool found = text_source::parse(text, options, song);
		const QString title = found ? QString::fromStdString(song.title).left(300) : QString();
		const QString artist = found && !title.isEmpty() ? QString::fromStdString(song.artist).left(300)
								 : QString();
		if (title.isEmpty() || radioTitle_.isEmpty() || title != radioTitle_ || artist != radioArtist_) {
			// Al arrancar cuenta la fecha del archivo (una canción de hace horas ya está
			// inactiva y no le gana a lo que suena); después, el momento del cambio (copiar un
			// archivo encima puede conservar su fecha).
			const qint64 wall = QDateTime::currentMSecsSinceEpoch();
			radioChangedWall_ = radioSeen_ ? wall : std::min(modified, wall);
			radioChangedAt_ = now() - (wall - radioChangedWall_);
		}
		radioSeen_ = true;
		radioTitle_ = title;
		radioArtist_ = artist;
		radioReadStat_ = stat;
	}
	refreshRadioTrack();
}

void MediaWatcher::refreshRadioTrack()
{
	const qint64 idleMs = qint64(std::max(0, radio_.idleMinutes)) * 60000;
	const bool idle = idleMs > 0 && QDateTime::currentMSecsSinceEpoch() - radioChangedWall_ > idleMs;
	if (radioTitle_.isEmpty() || idle) {
		if (tracks_.remove(kRadioApp))
			emit updated();
		return;
	}
	// Sin latido propio: se refresca en cada sondeo para que kStaleMs no la esconda.
	Track track;
	track.app = kRadioApp;
	track.kind = "radio";
	const QString label = radio_.label.simplified().left(100);
	track.label = label.isEmpty() ? QString("Radio") : label;
	track.title = radioTitle_;
	track.artist = radioArtist_;
	track.isPlaying = true;
	track.updatedAt = now();
	track.changedAt = radioChangedAt_;
	tracks_.insert(kRadioApp, track);
	emit updated();
}
