#pragma once

#include <QHash>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

// Una sesión multimedia del sistema (una por app que reproduce algo).
struct Track {
	QString app;   // id de la app: "Spotify.exe", "com.spotify.client"…
	QString kind;  // spotify | ytmusic | youtube | applemusic | browser | other (decide el color)
	QString label; // "Spotify", "Chrome"…
	QString title;
	QString artist;
	QString album;
	QString artwork; // data:image/…;base64,… (vacío si no hay)
	QString videoId; // id de YouTube si se pudo leer (modo video, solo macOS)
	bool isPlaying = false;
	bool isCurrent = false;
	qint64 progressMs = 0;
	qint64 durationMs = 0;
	qint64 updatedAt = 0; // ms, reloj monotónico de Qt
	qint64 changedAt = 0;

	bool valid() const { return !title.isEmpty(); }
	// Progreso estimado a este instante.
	qint64 progressNow(qint64 now) const;
};

// Lee "lo que está sonando" con el lector del sistema (media/windows.ps1,
// media/macos.js o media/linux.sh, en la carpeta de datos del plugin) y manda los comandos de los
// botones. Vive en el hilo de la interfaz de OBS.
class MediaWatcher : public QObject {
	Q_OBJECT

public:
	enum class Status { Starting, Ok, Error, Unsupported };

	static MediaWatcher &instance();

	void start();
	void stop();

	// Con includePaused (el dock), si nada suena devuelve la última en pausa. Con player
	// (spotify | ytmusic | youtube | applemusic), solo ese reproductor; vacío: cualquiera.
	Track current(bool includePaused = false, const QString &player = QString()) const;
	// El reproductor elegido en el dock ("" = Automático). Lo usan el dock, sus botones, el
	// canal de música y las fuentes con "Igual que el dock". Se guarda entre sesiones.
	QString player() const { return player_; }
	void setPlayer(const QString &player);
	// Los reproductores que se pueden elegir, en orden.
	static const QStringList &players();
	Status status() const { return status_; }
	QString error() const { return error_; }

	// previous | playpause | next, sobre la app que se está mostrando.
	void sendCommand(const QString &action);
	// Adelantar / retroceder: salta a esa posición (ms) de la canción que se está mostrando.
	void seekTo(qint64 positionMs);

	static qint64 now();

	// Modo video: el lector solo lee las pestañas del navegador si alguna fuente lo pidió
	// (leerlas hace que macOS pida permiso para controlar el navegador). También hacen falta
	// para elegir reproductor: sin ellas, una pestaña de YouTube o YouTube Music es solo
	// "el navegador".
	void setVideoWanted(bool wanted);

signals:
	// Cada vez que el lector escribe (una vez por segundo) o cambia el estado.
	void updated();
	// Un botón no funcionó (p. ej. falta el permiso de Accesibilidad en macOS).
	void commandFailed(const QString &message);

private:
	MediaWatcher() = default;
	// positionMs >= 0: va con el comando (seek).
	void runCommand(const QString &action, qint64 positionMs);
	void launch();
	void readLines();
	void handleLine(const QByteArray &line);
	void setStatus(Status status, const QString &error = QString());
	void writeVideoFlag();

	QProcess *process_ = nullptr;
	QHash<QString, Track> tracks_;
	QHash<QString, QString> artworks_; // app → carátula de su canción actual
	Status status_ = Status::Starting;
	QString error_;
	QString commandFile_;
	QString videoFlagFile_;
	bool videoWanted_ = false;
	QString player_;
	qint64 startedAt_ = 0;
	int failures_ = 0;
	bool stopping_ = false;
};
