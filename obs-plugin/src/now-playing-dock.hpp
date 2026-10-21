#pragma once

#include <QColor>
#include <QWidget>

class QLabel;
class QSlider;
class QToolButton;

// Dock "Música": la canción que suena con anterior / pausa / siguiente, retroceder y
// adelantar 10 segundos y una barra de progreso que se puede arrastrar (o pulsar) para
// saltar. Vive dentro de OBS (Docks → Música) y no sale en el stream.
class NowPlayingDock : public QWidget {
	Q_OBJECT

public:
	explicit NowPlayingDock(QWidget *parent = nullptr);

protected:
	void changeEvent(QEvent *event) override;

private:
	void refresh();
	void tick();
	void applyAccent(const QColor &accent);
	void updateIcons();
	void seekBy(qint64 deltaMs);
	void seekToMs(qint64 positionMs);

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
	QString artKey_;
	QColor accent_;
	bool playing_ = false;
	QString commandError_;
	qint64 commandErrorUntil_ = 0;
	// Tras saltar, se muestra esa posición unos segundos (el lector tarda en enterarse).
	qint64 seekTarget_ = -1;
	qint64 seekAt_ = 0;
};
