#include "media-watcher.hpp"

#include <obs-module.h>
#include <plugin-support.h>

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
// navegador, site (del lector, en modo video) dice si la pestaña es YouTube o YouTube Music.
AppInfo classifyApp(const QString &app, const QString &name, const QString &site)
{
	const QString id = (app + " " + name).toLower();
	if (id.contains("spotify"))
		return {"spotify", "Spotify"};
	// La PWA de YouTube Music instalada desde Chrome/Edge tiene este id de extensión.
	if (id.contains("cinhimbnkkaeohfgghhklpknlkffjgod") || id.contains("youtube music"))
		return {"ytmusic", "YouTube Music"};
	if (id.contains("applemusic") || id.contains("com.apple.music"))
		return {"applemusic", "Apple Music"};
	if (id.contains("tidal"))
		return {"other", "TIDAL"};
	if (id.contains("deezer"))
		return {"other", "Deezer"};
	static const QList<QPair<QString, QString>> browsers = {
		{"chrome", "Chrome"}, {"msedge", "Edge"},     {"firefox", "Firefox"}, {"brave", "Brave"},
		{"opera", "Opera"},   {"vivaldi", "Vivaldi"}, {"safari", "Safari"},   {"thebrowser", "Arc"},
	};
	for (const auto &browser : browsers) {
		if (!id.contains(browser.first))
			continue;
		if (site == "ytmusic")
			return {"ytmusic", "YouTube Music"};
		if (site == "youtube")
			return {"youtube", "YouTube"};
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
#if defined(_WIN32) || defined(__APPLE__)
	// Los comandos para el lector de Windows van por este archivo (ver media/windows.ps1).
	QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
	commandFile_ = QDir(dir).filePath("obs-now-playing-command.json");
	videoFlagFile_ = QDir(dir).filePath("obs-now-playing-video.flag");
	launch();
#else
	setStatus(Status::Unsupported);
#endif
}

void MediaWatcher::stop()
{
	stopping_ = true;
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
	if (stopping_)
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
		"-CommandFile",
		QDir::toNativeSeparators(commandFile_),
		"-VideoFlag",
		QDir::toNativeSeparators(videoFlagFile_)};
	QFile::remove(commandFile_); // un comando viejo no se ejecuta al arrancar
#else
	// Desde macOS 15.4 solo programas del sistema pueden leer "Ahora suena": por eso
	// el lector corre en osascript y no dentro de OBS.
	program = "/usr/bin/osascript";
	args = {"-l", "JavaScript", moduleFile("media/macos.js"), "watch", videoFlagFile_};
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

		// La carátula llega solo cuando cambia la canción: la guardamos con su canción
		// para no mostrar la de la anterior mientras llega la nueva.
		QString artKey = app + '\n' + track.title + '\n' + track.artist;
		if (s.contains("artwork")) {
			QString art = s.value("artwork").toString();
			if (art.startsWith("data:image/"))
				artworks_.insert(artKey, art);
			else
				artworks_.remove(artKey);
			if (artworks_.size() > 30)
				artworks_.erase(artworks_.begin());
		}
		track.artwork = artworks_.value(artKey);

		const Track prev = tracks_.value(app);
		bool changed = !prev.valid() || prev.title != track.title || prev.artist != track.artist ||
			       (!prev.isPlaying && track.isPlaying);
		track.changedAt = changed ? t : prev.changedAt;
		tracks_.insert(app, track);
		seen.insert(app);
	}
	// Las sesiones que el sistema ya no reporta (app cerrada) desaparecen.
	for (auto it = tracks_.begin(); it != tracks_.end();) {
		if (seen.contains(it.key()))
			++it;
		else
			it = tracks_.erase(it);
	}

	status_ = Status::Ok;
	error_.clear();
	emit updated();
}

Track MediaWatcher::current(bool includePaused) const
{
	qint64 t = now();
	QList<Track> alive;
	for (const Track &track : tracks_) {
		if (t - track.updatedAt <= kStaleMs)
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

void MediaWatcher::setVideoWanted(bool wanted)
{
	if (videoFlagFile_.isEmpty())
		return;
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
	Track track = current(true);
	if (!track.valid())
		return;
	obs_log(LOG_INFO, "comando %s para %s", action.toUtf8().constData(), track.app.toUtf8().constData());
#ifdef _WIN32
	// El lector revisa este archivo cada 100 ms.
	QSaveFile file(commandFile_);
	if (file.open(QIODevice::WriteOnly)) {
		QJsonObject cmd{{"cmd", action}, {"app", track.app}};
		file.write(QJsonDocument(cmd).toJson(QJsonDocument::Compact));
		file.commit();
	}
#elif defined(__APPLE__)
	auto *command = new QProcess(this);
	connect(command, &QProcess::finished, this, [this, command](int, QProcess::ExitStatus) {
		QByteArray out = command->readAllStandardOutput().trimmed();
		command->deleteLater();
		QJsonObject result = QJsonDocument::fromJson(out.split('\n').last()).object();
		QString problem = result.value("error").toString();
		if (problem.isEmpty())
			problem = result.value("hint").toString();
		if (!problem.isEmpty()) {
			obs_log(LOG_WARNING, "botón: %s", problem.toUtf8().constData());
			emit commandFailed(problem);
		} else if (result.contains("via")) {
			// Por dónde llegó el botón (página de YouTube, la app, teclas de música): para
			// saber qué pasó si un reproductor no responde.
			obs_log(LOG_INFO, "botón por: %s", result.value("via").toString().toUtf8().constData());
		}
	});
	command->start("/usr/bin/osascript", {"-l", "JavaScript", moduleFile("media/macos.js"), "command", action});
#endif
}
