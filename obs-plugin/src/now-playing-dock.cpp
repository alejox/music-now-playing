#include "now-playing-dock.hpp"
#include "media-watcher.hpp"

#include <obs-module.h>

#include <QEvent>
#include <QHBoxLayout>
#include <QHash>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QProgressBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kArtSize = 168;
constexpr int kSideButton = 40;
constexpr int kMainButton = 52;

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

enum class Glyph { Previous, Play, Pause, Next };

// Iconos dibujados aquí (y no los del sistema, que en el tema de OBS se ven grises y
// borrosos, como deshabilitados): triángulos y barras redondeados del color pedido.
QPixmap glyphPixmap(Glyph glyph, const QColor &color, int size, qreal dpr)
{
	QPixmap pixmap(QSize(size, size) * dpr);
	pixmap.setDevicePixelRatio(dpr);
	pixmap.fill(Qt::transparent);
	QPainter p(&pixmap);
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(QPen(color, size * 0.08, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	p.setBrush(color);
	const qreal s = size;
	auto triangle = [&](qreal left, qreal right, bool pointsRight) {
		QPainterPath path;
		qreal tip = pointsRight ? right : left;
		qreal base = pointsRight ? left : right;
		path.moveTo(base, s * 0.24);
		path.lineTo(tip, s * 0.5);
		path.lineTo(base, s * 0.76);
		path.closeSubpath();
		p.drawPath(path);
	};
	auto bar = [&](qreal x, qreal width) { p.drawRoundedRect(QRectF(x, s * 0.24, width, s * 0.52), s * 0.03, s * 0.03); };
	switch (glyph) {
	case Glyph::Play:
		triangle(s * 0.34, s * 0.76, true);
		break;
	case Glyph::Pause:
		bar(s * 0.3, s * 0.13);
		bar(s * 0.57, s * 0.13);
		break;
	case Glyph::Next:
		triangle(s * 0.24, s * 0.62, true);
		bar(s * 0.66, s * 0.1);
		break;
	case Glyph::Previous:
		bar(s * 0.24, s * 0.1);
		triangle(s * 0.38, s * 0.76, false);
		break;
	}
	return pixmap;
}

QIcon glyphIcon(Glyph glyph, const QColor &color, int size, qreal dpr)
{
	QIcon icon;
	icon.addPixmap(glyphPixmap(glyph, color, size, dpr), QIcon::Normal);
	QColor faded = color;
	faded.setAlphaF(0.35f);
	icon.addPixmap(glyphPixmap(glyph, faded, size, dpr), QIcon::Disabled);
	return icon;
}

// Texto negro o blanco, el que se lea mejor sobre el color.
QColor contrastOn(const QColor &background)
{
	double luminance = 0.2126 * background.redF() + 0.7152 * background.greenF() + 0.0722 * background.blueF();
	return luminance > 0.6 ? QColor(20, 20, 20) : QColor(Qt::white);
}

QString rgba(const QColor &color, double alpha)
{
	return QString("rgba(%1,%2,%3,%4)").arg(color.red()).arg(color.green()).arg(color.blue()).arg(alpha);
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
	layout->setContentsMargins(16, 18, 16, 18);
	layout->setSpacing(4);

	art_ = new QLabel(this);
	art_->setFixedSize(kArtSize, kArtSize);
	art_->setAlignment(Qt::AlignCenter);
	art_->setStyleSheet("background: rgba(127,127,127,0.18); border-radius: 10px;");
	layout->addWidget(art_, 0, Qt::AlignHCenter);
	layout->addSpacing(12);

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
	layout->addSpacing(4);
	layout->addWidget(source_);
	layout->addSpacing(14);

	auto *progressRow = new QHBoxLayout();
	progressRow->setSpacing(10);
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
	layout->addSpacing(16);

	auto *buttons = new QHBoxLayout();
	buttons->setSpacing(22);
	buttons->addStretch(1);
	auto makeButton = [this, buttons](const char *tip, const QString &action, int size) {
		auto *button = new QToolButton(this);
		button->setFixedSize(size, size);
		button->setToolTip(text(tip));
		button->setCursor(Qt::PointingHandCursor);
		button->setFocusPolicy(Qt::NoFocus);
		connect(button, &QToolButton::clicked, this, [this, action] {
			MediaWatcher::instance().sendCommand(action);
			// Que se vea el cambio enseguida, sin esperar al siguiente segundo.
			QTimer::singleShot(500, this, &NowPlayingDock::refresh);
		});
		buttons->addWidget(button, 0, Qt::AlignVCenter);
		return button;
	};
	previous_ = makeButton("NowPlaying.Previous", "previous", kSideButton);
	playPause_ = makeButton("NowPlaying.PlayPause", "playpause", kMainButton);
	next_ = makeButton("NowPlaying.Next", "next", kSideButton);
	buttons->addStretch(1);
	layout->addLayout(buttons);
	layout->addSpacing(10);

	status_ = new QLabel(this);
	status_->setWordWrap(true);
	status_->setAlignment(Qt::AlignHCenter);
	layout->addWidget(status_);
	layout->addStretch(1);

	applyAccent(QColor());
	connect(&MediaWatcher::instance(), &MediaWatcher::updated, this, &NowPlayingDock::refresh);
	// Si un botón no funcionó, el motivo se muestra unos segundos debajo de los botones.
	connect(&MediaWatcher::instance(), &MediaWatcher::commandFailed, this, [this](const QString &message) {
		commandError_ = message;
		commandErrorUntil_ = MediaWatcher::now() + 12000;
		refresh();
	});
	auto *timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, &NowPlayingDock::tick);
	timer->start(250);
	refresh();
}

void NowPlayingDock::applyAccent(const QColor &accent)
{
	accent_ = accent.isValid() ? accent : palette().color(QPalette::Highlight);
	const QString color = accent_.name();
	progress_->setStyleSheet(QString("QProgressBar { border: 0; border-radius: 2px; background: rgba(127,127,127,0.25); }"
					 "QProgressBar::chunk { border-radius: 2px; background: %1; }")
					 .arg(color));
	source_->setStyleSheet(QString("color: %1;").arg(color));

	// Anterior / siguiente: redondos, con borde y fondo suave del color de la carátula,
	// que se aclaran al pasar el mouse y se marcan al presionar.
	const QString side = QString("QToolButton { border-radius: %1px; border: 1px solid %2; background: %3; }"
				     "QToolButton:hover { border-color: %4; background: %5; }"
				     "QToolButton:pressed { background: %6; }"
				     "QToolButton:disabled { border-color: rgba(127,127,127,0.2); background: transparent; }")
				     .arg(kSideButton / 2)
				     .arg(rgba(accent_, 0.45), rgba(accent_, 0.14), color, rgba(accent_, 0.28),
					  rgba(accent_, 0.45));
	previous_->setStyleSheet(side);
	next_->setStyleSheet(side);
	playPause_->setStyleSheet(QString("QToolButton { border-radius: %1px; border: 0; background: %2; }"
					  "QToolButton:hover { background: %3; }"
					  "QToolButton:pressed { background: %4; }"
					  "QToolButton:disabled { background: rgba(127,127,127,0.25); }")
					  .arg(kMainButton / 2)
					  .arg(color, accent_.lighter(115).name(), accent_.darker(120).name()));
	updateIcons();
}

void NowPlayingDock::changeEvent(QEvent *event)
{
	QWidget::changeEvent(event);
	// Al cambiar el tema de OBS, los iconos toman el nuevo color del texto.
	if (event->type() == QEvent::PaletteChange && previous_)
		updateIcons();
}

void NowPlayingDock::updateIcons()
{
	const qreal dpr = devicePixelRatioF();
	const QColor textColor = palette().color(QPalette::WindowText);
	const int side = kSideButton / 2;
	previous_->setIcon(glyphIcon(Glyph::Previous, textColor, side, dpr));
	previous_->setIconSize(QSize(side, side));
	next_->setIcon(glyphIcon(Glyph::Next, textColor, side, dpr));
	next_->setIconSize(QSize(side, side));
	const int main = kMainButton / 2;
	playPause_->setIcon(glyphIcon(playing_ ? Glyph::Pause : Glyph::Play, contrastOn(accent_), main, dpr));
	playPause_->setIconSize(QSize(main, main));
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
	if (!commandError_.isEmpty() && MediaWatcher::now() < commandErrorUntil_)
		status_->setText(commandError_);
	status_->setStyleSheet("color: #f87171;");

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

	int width = std::max(120, this->width() - 32);
	title_->setText(title_->fontMetrics().elidedText(track.title, Qt::ElideRight, width));
	artist_->setText(artist_->fontMetrics().elidedText(track.artist, Qt::ElideRight, width));
	source_->setText(track.kind == "browser" ? text("NowPlaying.ListeningNow")
						 : text("NowPlaying.ListeningOn").arg(track.label));
	if (track.isPlaying != playing_) {
		playing_ = track.isPlaying;
		updateIcons();
	}

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
