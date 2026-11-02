#pragma once

#include "media-watcher.hpp"

#include <QColor>
#include <QWidget>

class QComboBox;
class QLabel;
class QSlider;
class QTimer;
class QToolButton;

// Dock "Música": la canción que suena con anterior / pausa / siguiente, retroceder y
// adelantar 10 segundos y una barra de progreso que se puede arrastrar (o pulsar) para
// saltar. Arriba, la lista "Reproductor" elige de cuál se muestra la canción (Automático:
// del que suene). Vive dentro de OBS (Docks → Música) y no sale en el stream.
class NowPlayingDock : public QWidget {
	Q_OBJECT

public:
	explicit NowPlayingDock(QWidget *parent = nullptr);

protected:
	void changeEvent(QEvent *event) override;

private:
	void refresh();
	// La canción del reproductor elegido (o la última en pausa).
	Track shown() const;
	void tick();
	void applyAccent(const QColor &accent);
	void updateIcons();
	void seekBy(qint64 deltaMs);
	void seekToMs(qint64 positionMs);
	// Fila de aleatorio y volumen.
	void updateVolumeRow();
	void styleVolumeRow();
	void applyVolume();
	void toggleMute();
	void setStreamMode(bool stream);

	QComboBox *player_ = nullptr;
	QLabel *art_ = nullptr;
	QLabel *title_ = nullptr;
	QLabel *artist_ = nullptr;
	QLabel *source_ = nullptr;
	QLabel *status_ = nullptr;
	QSlider *progress_ = nullptr;
	QLabel *elapsed_ = nullptr;
	QLabel *duration_ = nullptr;
	QToolButton *previous_ = nullptr;
	QToolButton *playPause_ = nullptr;
	QToolButton *next_ = nullptr;
	QToolButton *back_ = nullptr;
	QToolButton *forward_ = nullptr;
	QToolButton *shuffle_ = nullptr;
	QToolButton *mute_ = nullptr;
	QSlider *volume_ = nullptr;
	QLabel *volumeLabel_ = nullptr;
	QToolButton *modePlayer_ = nullptr;
	QToolButton *modeStream_ = nullptr;
	QTimer *volumeTimer_ = nullptr;
	// "Stream": la barra mueve el canal de música de OBS; si no, el volumen del reproductor.
	bool streamMode_ = false;
	bool muted_ = false;
	bool shuffleOn_ = false;
	int lastVolume_ = 50;         // para volver después de silenciar
	qint64 volumeHoldUntil_ = 0;  // recién cambiado: la barra no salta a lo que diga el lector
	QString artKey_;
	QColor accent_;
	bool playing_ = false;
	QString commandError_;
	qint64 commandErrorUntil_ = 0;
	// Tras saltar, se muestra esa posición unos segundos (el lector tarda en enterarse).
	qint64 seekTarget_ = -1;
	qint64 seekAt_ = 0;
};
