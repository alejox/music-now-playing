#include "now-playing-dock.hpp"
#include "media-watcher.hpp"

#include <obs-module.h>

#include <QHBoxLayout>
#include <QHash>
#include <QImage>
#include <QLabel>
#include <QPalette>
#include <QPixmap>
#include <QProgressBar>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kArtSize = 160;

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

QString formatTime(qint64 ms)
{
	qint64 s = std::max<qint64>(0, ms / 1000);
	qint64 h = s / 3600;
	qint64 m = (s % 3600) / 60;
	QString sec = QString::number(s % 60).rightJustified(2, '0');
	if (h > 0)
		return QString("%1:%2:%3").arg(h).arg(QString::number(m).rightJustified(2, '0'), sec);
	return QString("%1:%2").arg(m).arg(sec);
}

// El color más vivo de la carátula (no el más abundante: una carátula casi negra con
// un logo rojo debe dar rojo), con luminosidad suficiente para leerse. Mismo criterio
// que ui.js del overlay.
QColor accentFrom(const QImage &source)
{
	QImage img = source.scaled(48, 48, Qt::IgnoreAspectRatio, Qt::FastTransformation)
			     .convertToFormat(QImage::Format_RGB32);
	struct Bucket {
		double r = 0, g = 0, b = 0;
		int n = 0;
	};
	QHash<int, Bucket> buckets;
	for (int y = 0; y < img.height(); ++y) {
		const QRgb *row = reinterpret_cast<const QRgb *>(img.constScanLine(y));
		for (int x = 0; x < img.width(); ++x) {
			int r = qRed(row[x]), g = qGreen(row[x]), b = qBlue(row[x]);
			Bucket &bucket = buckets[((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4)];
			bucket.r += r;
			bucket.g += g;
			bucket.b += b;
			bucket.n++;
		}
	}
	QColor best;
	double bestScore = 0;
	for (const Bucket &bucket : buckets) {
		QColor c = QColor::fromRgbF(float(bucket.r / bucket.n / 255), float(bucket.g / bucket.n / 255),
					    float(bucket.b / bucket.n / 255));
		float h, s, l;
		c.getHslF(&h, &s, &l);
		if (s < 0.3f || l < 0.15f || l > 0.9f)
			continue;
		double score = std::sqrt(double(bucket.n)) * s * s * (1 - std::abs(l - 0.55));
		if (score > bestScore) {
			bestScore = score;
			best = c;
		}
	}
	if (!best.isValid())
		return QColor();
	float h, s, l;
	best.getHslF(&h, &s, &l);
	return QColor::fromHslF(h, std::clamp(s, 0.55f, 1.0f), std::clamp(l, 0.55f, 0.7f));
}

QPixmap pixmapFromDataUrl(const QString &dataUrl)
{
	qsizetype comma = dataUrl.indexOf(',');
	if (!dataUrl.startsWith("data:image/") || comma < 0)
		return QPixmap();
	QPixmap pixmap;
	pixmap.loadFromData(QByteArray::fromBase64(dataUrl.mid(comma + 1).toLatin1()));
	return pixmap;
}

} // namespace

NowPlayingDock::NowPlayingDock(QWidget *parent) : QWidget(parent)
{
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(12, 12, 12, 12);
	layout->setSpacing(6);

	art_ = new QLabel(this);
	art_->setFixedSize(kArtSize, kArtSize);
	art_->setAlignment(Qt::AlignCenter);
	art_->setStyleSheet("background: rgba(127,127,127,0.18); border-radius: 10px;");
	layout->addWidget(art_, 0, Qt::AlignHCenter);

	title_ = new QLabel(this);
	title_->setAlignment(Qt::AlignHCenter);
	QFont titleFont = title_->font();
	titleFont.setBold(true);
	titleFont.setPointSizeF(titleFont.pointSizeF() * 1.15);
	title_->setFont(titleFont);
	layout->addWidget(title_);

	artist_ = new QLabel(this);
	artist_->setAlignment(Qt::AlignHCenter);
	QPalette muted = artist_->palette();
	QColor mutedColor = muted.color(QPalette::WindowText);
	mutedColor.setAlphaF(0.65f);
	muted.setColor(QPalette::WindowText, mutedColor);
	artist_->setPalette(muted);
	layout->addWidget(artist_);

	source_ = new QLabel(this);
	source_->setAlignment(Qt::AlignHCenter);
	QFont sourceFont = source_->font();
	sourceFont.setBold(true);
	sourceFont.setPointSizeF(sourceFont.pointSizeF() * 0.8);
	sourceFont.setCapitalization(QFont::AllUppercase);
	source_->setFont(sourceFont);
	layout->addWidget(source_);

	auto *progressRow = new QHBoxLayout();
	elapsed_ = new QLabel("0:00", this);
	duration_ = new QLabel("0:00", this);
	progress_ = new QProgressBar(this);
	progress_->setTextVisible(false);
	progress_->setFixedHeight(4);
	progress_->setRange(0, 1000);
	progressRow->addWidget(elapsed_);
	progressRow->addWidget(progress_, 1);
	progressRow->addWidget(duration_);
	layout->addLayout(progressRow);

	auto *buttons = new QHBoxLayout();
	buttons->addStretch(1);
	auto makeButton = [this, buttons](QStyle::StandardPixmap icon, const char *tip, const QString &action,
					  int size) {
		auto *button = new QToolButton(this);
		button->setIcon(style()->standardIcon(icon));
		button->setIconSize(QSize(size / 2, size / 2));
		button->setFixedSize(size, size);
		button->setToolTip(text(tip));
		button->setAutoRaise(true);
		connect(button, &QToolButton::clicked, this, [this, action] {
			MediaWatcher::instance().sendCommand(action);
			// Que se vea el cambio enseguida, sin esperar al siguiente segundo.
			QTimer::singleShot(500, this, &NowPlayingDock::refresh);
		});
		buttons->addWidget(button);
		return button;
	};
	previous_ = makeButton(QStyle::SP_MediaSkipBackward, "NowPlaying.Previous", "previous", 40);
	playPause_ = makeButton(QStyle::SP_MediaPlay, "NowPlaying.PlayPause", "playpause", 52);
	next_ = makeButton(QStyle::SP_MediaSkipForward, "NowPlaying.Next", "next", 40);
	buttons->addStretch(1);
	layout->addLayout(buttons);

	status_ = new QLabel(this);
	status_->setWordWrap(true);
	status_->setAlignment(Qt::AlignHCenter);
	status_->setStyleSheet("color: #f87171;");
	layout->addWidget(status_);
	layout->addStretch(1);

	applyAccent(QColor());
	connect(&MediaWatcher::instance(), &MediaWatcher::updated, this, &NowPlayingDock::refresh);
	auto *timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, &NowPlayingDock::tick);
	timer->start(250);
	refresh();
}

void NowPlayingDock::applyAccent(const QColor &accent)
{
	QString color = accent.isValid() ? accent.name() : QString("palette(highlight)");
	progress_->setStyleSheet(QString("QProgressBar { border: 0; border-radius: 2px; background: rgba(127,127,127,0.25); }"
					 "QProgressBar::chunk { border-radius: 2px; background: %1; }")
					 .arg(color));
	playPause_->setStyleSheet(QString("QToolButton { border-radius: 26px; background: %1; }").arg(color));
	source_->setStyleSheet(QString("color: %1;").arg(color));
}

void NowPlayingDock::refresh()
{
	MediaWatcher &watcher = MediaWatcher::instance();
	switch (watcher.status()) {
	case MediaWatcher::Status::Error:
		status_->setText(text("NowPlaying.Reader.Error").arg(watcher.error()));
		break;
	case MediaWatcher::Status::Unsupported:
		status_->setText(text("NowPlaying.Reader.Unsupported"));
		break;
	default:
		status_->clear();
	}

	Track track = watcher.current(true);
	for (QToolButton *button : {previous_, playPause_, next_})
		button->setEnabled(track.valid());

	if (!track.valid()) {
		title_->setText(text("NowPlaying.NothingPlaying"));
		artist_->setText(watcher.status() == MediaWatcher::Status::Starting ? text("NowPlaying.Reader.Starting")
										     : text("NowPlaying.PlaySomething"));
		source_->clear();
		art_->clear();
		artKey_.clear();
		applyAccent(QColor());
		tick();
		return;
	}

	int width = std::max(120, this->width() - 24);
	title_->setText(title_->fontMetrics().elidedText(track.title, Qt::ElideRight, width));
	artist_->setText(artist_->fontMetrics().elidedText(track.artist, Qt::ElideRight, width));
	source_->setText(track.kind == "browser" ? text("NowPlaying.ListeningNow")
						 : text("NowPlaying.ListeningOn").arg(track.label));
	playPause_->setIcon(style()->standardIcon(track.isPlaying ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));

	QString artKey = track.app + '\n' + track.title + '\n' + QString::number(track.artwork.size());
	if (artKey != artKey_) {
		artKey_ = artKey;
		QPixmap pixmap = pixmapFromDataUrl(track.artwork);
		if (pixmap.isNull()) {
			art_->clear();
			applyAccent(QColor());
		} else {
			art_->setPixmap(pixmap.scaled(kArtSize, kArtSize, Qt::KeepAspectRatioByExpanding,
						      Qt::SmoothTransformation));
			applyAccent(accentFrom(pixmap.toImage()));
		}
	}
	tick();
}

void NowPlayingDock::tick()
{
	Track track = MediaWatcher::instance().current(true);
	bool show = track.valid() && track.durationMs > 0;
	progress_->setVisible(show);
	elapsed_->setVisible(show);
	duration_->setVisible(show);
	if (!show)
		return;
	qint64 position = track.progressNow(MediaWatcher::now());
	progress_->setValue(int(position * 1000 / track.durationMs));
	elapsed_->setText(formatTime(position));
	duration_->setText(formatTime(track.durationMs));
}
