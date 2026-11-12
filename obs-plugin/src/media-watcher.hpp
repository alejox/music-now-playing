#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>

#include <string>

class QTimer;

// Una sesión multimedia del sistema (una por app que reproduce algo).
struct Track {
	QString app;   // id de la app: "Spotify.exe", "com.spotify.client"…
	QString kind;  // spotify | ytmusic | youtube | applemusic | system | radio | browser | other (decide el color)
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

// Lo que se puede saber y cambiar del reproductor además de la canción (dock: volumen y
// aleatorio). Windows: el aleatorio lo da el lector con cada lectura y el volumen es el de
// la app en el Mezclador de Windows. macOS y Linux: se le pregunta al reproductor.
struct PlayerState {
	int volume = -1; // 0–100; -1: no se puede leer ni cambiar
	QString shuffle; // on | off | action (un botón sin estado, como en YouTube Music) | "" (no se puede)
};

// Radio desde un archivo: lo que escribe un programa de automatización (Dinesat, RadioBoss,
// ZaraRadio…). Se guarda en radio.json, en la configuración del plugin. Apagado por defecto.
struct RadioSettings {
	bool enabled = false;
	QString path;
	QString separator = " - ";
	bool titleFirst = false; // "Título - Artista"
	QString pattern;         // expresión regular con (?<artist>…) y (?<title>…); manda sobre el separador
	int idleMinutes = 0;     // sin cambios en tanto tiempo, se deja de mostrar (0: nunca)
	QString label;           // "Escuchando en …" (vacío: Radio)
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
	// (spotify | ytmusic | youtube | system), solo ese reproductor; vacío: cualquiera.
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
	// Volumen y aleatorio del reproductor que se muestra en el dock.
	PlayerState playerState() const;
	void setVolume(int percent);
	void toggleShuffle();
	// Vuelve a preguntar el volumen y el aleatorio (el dock lo hace cada pocos segundos).
	void refreshPlayerState();
	// Botón Refrescar del dock: olvida la carátula y el video de lo que suena (se buscan de
	// nuevo) y reinicia el lector, sin esperas de reintento.
	void restartReader();

	static qint64 now();

	// Radio desde un archivo (ver RadioSettings). La canción llega como una sesión más, de
	// kind "radio", sin carátula, sin duración y sin botones.
	RadioSettings radio() const { return radio_; }
	void setRadio(const RadioSettings &settings);
	// Si la expresión regular de la radio no es válida, por qué (se usa el separador).
	static QString radioPatternError(const QString &pattern);

	// Modo video: el lector solo lee las pestañas del navegador si alguna fuente lo pidió
	// (leerlas hace que macOS pida permiso para controlar el navegador). También hacen falta
	// para elegir reproductor: sin ellas, una pestaña de YouTube o YouTube Music es solo
	// "el navegador".
	void setVideoWanted(bool wanted);
	// Alguna fuente tiene un reproductor fijo: en macOS (que anuncia un solo reproductor) el
	// lector busca también lo que suena por detrás, aunque lo anunciado esté sonando.
	void setOthersWanted(bool wanted);

signals:
	// Cada vez que el lector escribe (una vez por segundo) o cambia el estado.
	void updated();
	// Un botón no funcionó (p. ej. falta el permiso de Accesibilidad en macOS).
	void commandFailed(const QString &message);
	// Llegó el volumen o el aleatorio del reproductor.
	void playerStateChanged();

private:
	MediaWatcher() = default;
	// arg >= 0: va con el comando (la posición en ms de seek, el volumen de volume).
	void runCommand(const QString &action, qint64 arg);
	// takeVolume false: la respuesta trae un volumen que puede ser viejo (ver setVolume).
	void storeState(const QString &app, const QJsonObject &state, bool takeVolume = true);
	void launch();
	void readLines();
	void handleLine(const QByteArray &line);
	void setStatus(Status status, const QString &error = QString());
	void writeVideoFlag();
	void startRadio();
	void pollRadio();
	void refreshRadioTrack();
	QString radioPath() const;

	QProcess *process_ = nullptr;
	QHash<QString, Track> tracks_;
	// Borra las carátulas más viejas si hay más de kMaxArtworks, nunca la de keep ni la de una
	// sesión viva.
	void evictArtworks(const QString &keep);

	static constexpr qsizetype kMaxArtworks = 30;
	static constexpr qint64 kArtMissingLogMs = 10000; // sin carátula tras esto: se anota en el log
	QHash<QString, QString> artworks_; // app + canción → carátula de esa canción
	QStringList artworkOrder_;         // claves de artworks_, de la más vieja a la más nueva
	QSet<QString> artMissingLogged_;   // canciones (app + canción) ya anotadas en el log sin carátula
	QHash<QString, PlayerState> states_; // app → volumen y aleatorio
	bool stateAsked_ = false;            // hay una pregunta de estado en curso
	// Un cambio de volumen a la vez; mientras va uno, el último pedido espera.
	bool volumeBusy_ = false;
	int pendingVolume_ = -1;
	qint64 volumeDoneAt_ = 0;
	Status status_ = Status::Starting;
	QString error_;
	QString commandDir_;     // Windows: un archivo por comando para el lector
	qint64 commandSeq_ = 0;  // número del último comando (crece siempre, también entre reinicios)
	QString videoFlagFile_;
	QString othersFlagFile_;
	bool videoWanted_ = false;
	bool othersWanted_ = false;
	QString player_;
	qint64 startedAt_ = 0;
	int failures_ = 0;
	bool stopping_ = false;

	// Radio desde un archivo: un sondeo por segundo (también en carpetas de red). Un cambio se
	// acepta cuando el archivo dejó de cambiar entre dos sondeos o cuando dos lecturas seguidas
	// dan lo mismo, para no tomar un archivo a medio escribir.
	RadioSettings radio_;
	QRegularExpression radioPattern_;
	QTimer *radioTimer_ = nullptr;
	QString radioStat_;     // fecha, tamaño y creación del archivo en el último sondeo
	QString radioReadStat_; // lo mismo, de la última lectura aceptada
	std::string radioPending_;
	bool radioHasPending_ = false;
	bool radioSeen_ = false;
	QString radioTitle_;
	QString radioArtist_;
	qint64 radioChangedWall_ = 0; // ms desde 1970: para el tiempo sin cambios
	qint64 radioChangedAt_ = 0;   // lo mismo en el reloj de now(): "la última que empezó"
};
