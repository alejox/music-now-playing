#pragma once

#include <QColor>
#include <QWidget>

class QLabel;
class QProgressBar;
class QToolButton;

// Dock "Música": la canción que suena con anterior / pausa / siguiente. Vive dentro de OBS
// (Docks → Música) y no sale en el stream.
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

	QLabel *art_ = nullptr;
	QLabel *title_ = nullptr;
	QLabel *artist_ = nullptr;
	QLabel *source_ = nullptr;
	QLabel *status_ = nullptr;
	QProgressBar *progress_ = nullptr;
	QLabel *elapsed_ = nullptr;
	QLabel *duration_ = nullptr;
	QToolButton *previous_ = nullptr;
	QToolButton *playPause_ = nullptr;
	QToolButton *next_ = nullptr;
	QString artKey_;
	QColor accent_;
	bool playing_ = false;
	QString commandError_;
	qint64 commandErrorUntil_ = 0;
};
