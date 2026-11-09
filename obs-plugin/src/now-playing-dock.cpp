#include "now-playing-dock.hpp"

#include <obs-module.h>

#include <QComboBox>
#include <QContextMenuEvent>
#include <QMenu>
#include <QPaintEvent>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QWheelEvent>
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
#include <QMouseEvent>
#include <QScrollArea>
#include <QSlider>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kArtSize = 168;
constexpr int kSideButton = 40;
constexpr int kSkipButton = 32;
constexpr qint64 kSkipMs = 10000;
constexpr int kMainButton = 52;
constexpr int kSmallButton = 28;
// Botones del transporte en compacto o angosto (los de ±10 s se ocultan).
constexpr int kSideButtonSmall = 32;
constexpr int kMainButtonSmall = 40;
// Carátula: escala con el dock (en pasos de 8 px, para no reescalar en cada píxel).
constexpr int kArtLargeMin = 96;
constexpr int kArtLargeMax = 320;
constexpr int kArtMediumMin = 56;
constexpr int kArtMediumMax = 120;
constexpr int kArtThumb = 48;
// Alto del dock para cada diseño, con histéresis (se entra con un valor y se sale con otro
// menor) para que no parpadee justo en el límite.
constexpr int kLargeEnter = 480;
constexpr int kLargeLeave = 460;
constexpr int kMediumEnter = 330;
constexpr int kMediumLeave = 310;
// Ancho por debajo del cual el dock es "angosto".
constexpr int kNarrowEnter = 290;
constexpr int kNarrowLeave = 305;
// Tras mover el volumen, la barra no salta a lo que diga el reproductor durante este tiempo
// (algunos tardan en aplicarlo y en contestar).
constexpr qint64 kVolumeHoldMs = 4000;

// Los reproductores de la lista, con su color (el punto al lado del nombre).
struct PlayerInfo {
	const char *key;  // texto de la lista
	const char *name; // para "Spotify no está sonando" (una clave de texto si empieza con "NowPlaying.")
	const char *color;
};

// El reproductor del sistema: Apple Music en Mac; en Windows, el Reproductor multimedia.
#ifdef __APPLE__
constexpr const char *kSystemName = "Apple Music";
constexpr const char *kSystemColor = "#fa2d48";
#elif defined(_WIN32)
constexpr const char *kSystemName = "NowPlaying.Player.SystemName.Windows";
constexpr const char *kSystemColor = "#0078d4";
#else
constexpr const char *kSystemName = "NowPlaying.Player.SystemName.Other";
constexpr const char *kSystemColor = "#8b8cff";
#endif

const QHash<QString, PlayerInfo> &playerInfo()
{
	static const QHash<QString, PlayerInfo> info = {
		{"spotify", {"NowPlaying.Player.Spotify", "Spotify", "#1db954"}},
		{"ytmusic", {"NowPlaying.Player.YTMusic", "YouTube Music", "#ff4e45"}},
		{"youtube", {"NowPlaying.Player.YouTube", "YouTube", "#ff0033"}},
		{"system", {"NowPlaying.Player.System", kSystemName, kSystemColor}},
	};
	return info;
}

QIcon dotIcon(const QColor &color, qreal dpr)
{
	const int size = 10;
	QPixmap pixmap(QSize(size, size) * dpr);
	pixmap.setDevicePixelRatio(dpr);
	pixmap.fill(Qt::transparent);
	QPainter p(&pixmap);
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	p.drawEllipse(QRectF(1, 1, size - 2, size - 2));
	return QIcon(pixmap);
}

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

enum class Glyph { Previous, Play, Pause, Next, Back10, Forward10, Shuffle, Speaker, SpeakerMuted, Backdrop };

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
	// Flecha en círculo con un "10" adentro (retroceder / adelantar 10 segundos).
	auto skip = [&](bool forward) {
		p.setBrush(Qt::NoBrush);
		QPen pen(color, s * 0.08, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
		p.setPen(pen);
		QRectF circle(s * 0.16, s * 0.16, s * 0.68, s * 0.68);
		// El arco deja abierto arriba, donde va la punta de la flecha.
		p.drawArc(circle, forward ? 60 * 16 : 120 * 16, forward ? 300 * 16 : -300 * 16);
		const qreal tipX = forward ? s * 0.66 : s * 0.34;
		const qreal dir = forward ? 1 : -1;
		QPainterPath arrow;
		arrow.moveTo(tipX - dir * s * 0.14, s * 0.06);
		arrow.lineTo(tipX + dir * s * 0.02, s * 0.17);
		arrow.lineTo(tipX - dir * s * 0.12, s * 0.29);
		p.drawPath(arrow);
		QFont font;
		font.setPixelSize(int(s * 0.3));
		font.setBold(true);
		p.setFont(font);
		p.drawText(circle, Qt::AlignCenter, "10");
	};
	// Dos flechas que se cruzan.
	auto shuffle = [&]() {
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(color, s * 0.08, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		for (bool down : {true, false}) {
			const qreal from = down ? 0.3 : 0.7, to = down ? 0.7 : 0.3;
			QPainterPath path;
			path.moveTo(s * 0.1, s * from);
			path.lineTo(s * 0.26, s * from);
			path.cubicTo(s * 0.5, s * from, s * 0.5, s * to, s * 0.72, s * to);
			path.lineTo(s * 0.88, s * to);
			p.drawPath(path);
			QPainterPath head;
			head.moveTo(s * 0.77, s * (to - 0.11));
			head.lineTo(s * 0.88, s * to);
			head.lineTo(s * 0.77, s * (to + 0.11));
			p.drawPath(head);
		}
	};
	// Altavoz con ondas, o con una cruz roja si está silenciado.
	auto speaker = [&](bool muted) {
		QPainterPath body;
		body.moveTo(s * 0.12, s * 0.38);
		body.lineTo(s * 0.28, s * 0.38);
		body.lineTo(s * 0.48, s * 0.2);
		body.lineTo(s * 0.48, s * 0.8);
		body.lineTo(s * 0.28, s * 0.62);
		body.lineTo(s * 0.12, s * 0.62);
		body.closeSubpath();
		p.setPen(Qt::NoPen);
		p.drawPath(body);
		p.setBrush(Qt::NoBrush);
		if (muted) {
			p.setPen(QPen(QColor("#f87171"), s * 0.08, Qt::SolidLine, Qt::RoundCap));
			p.drawLine(QPointF(s * 0.62, s * 0.38), QPointF(s * 0.86, s * 0.62));
			p.drawLine(QPointF(s * 0.86, s * 0.38), QPointF(s * 0.62, s * 0.62));
			return;
		}
		p.setPen(QPen(color, s * 0.07, Qt::SolidLine, Qt::RoundCap));
		p.drawArc(QRectF(s * 0.4, s * 0.33, s * 0.28, s * 0.34), -50 * 16, 100 * 16);
		p.drawArc(QRectF(s * 0.36, s * 0.18, s * 0.5, s * 0.64), -50 * 16, 100 * 16);
	};
	// Un cuadro de imagen (montaña y sol): el fondo con la carátula.
	auto picture = [&]() {
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(color, s * 0.08, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		p.drawRoundedRect(QRectF(s * 0.12, s * 0.18, s * 0.76, s * 0.64), s * 0.1, s * 0.1);
		QPainterPath mountain;
		mountain.moveTo(s * 0.2, s * 0.74);
		mountain.lineTo(s * 0.42, s * 0.48);
		mountain.lineTo(s * 0.58, s * 0.64);
		mountain.lineTo(s * 0.66, s * 0.56);
		mountain.lineTo(s * 0.8, s * 0.74);
		p.drawPath(mountain);
		p.setPen(Qt::NoPen);
		p.setBrush(color);
		p.drawEllipse(QPointF(s * 0.66, s * 0.36), s * 0.06, s * 0.06);
	};
	switch (glyph) {
	case Glyph::Backdrop:
		picture();
		break;
	case Glyph::Shuffle:
		shuffle();
		break;
	case Glyph::Speaker:
		speaker(false);
		break;
	case Glyph::SpeakerMuted:
		speaker(true);
		break;
	case Glyph::Back10:
		skip(false);
		break;
	case Glyph::Forward10:
		skip(true);
		break;
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

// Barra de progreso que se arrastra para saltar en la canción. Un clic fuera de la perilla
// la lleva ahí y empieza a arrastrarla (QSlider normal solo avanza un "paso").
class SeekSlider : public QSlider {
public:
	explicit SeekSlider(QWidget *parent) : QSlider(Qt::Horizontal, parent) {}

protected:
	void mousePressEvent(QMouseEvent *event) override
	{
		if (event->button() == Qt::LeftButton) {
			QStyleOptionSlider option;
			initStyleOption(&option);
			QRect handle = style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, this);
			if (!handle.contains(event->position().toPoint())) {
				int x = int(event->position().x()) - handle.width() / 2;
				setValue(QStyle::sliderValueFromPosition(minimum(), maximum(), x, width() - handle.width()));
			}
		}
		QSlider::mousePressEvent(event);
	}
};

// Barra del volumen: la rueda del ratón sube o baja de 5 en 5.
class VolumeSlider : public QSlider {
public:
	explicit VolumeSlider(QWidget *parent) : QSlider(Qt::Horizontal, parent) {}

protected:
	void wheelEvent(QWheelEvent *event) override
	{
		const int delta = event->angleDelta().y() != 0 ? event->angleDelta().y() : event->angleDelta().x();
		if (delta != 0)
			setValue(value() + (delta > 0 ? 5 : -5));
		event->accept();
	}
};

// La carátula muy difuminada, para el fondo del dock: se reduce en pasos (cada uno suaviza)
// y al pintarla se estira con suavizado, que termina de difuminarla.
QImage blurredBackdrop(const QImage &source)
{
	if (source.isNull())
		return QImage();
	QImage small = source.convertToFormat(QImage::Format_RGB32)
			       .scaled(48, 48, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation)
			       .scaled(12, 12, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
	return small.scaled(96, 96, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

// El fondo difuminado se puede quitar (clic derecho en el dock); se recuerda entre sesiones.
QString backdropFile()
{
	char *path = obs_module_config_path("dock-backdrop.txt");
	QString result = QString::fromUtf8(path);
	bfree(path);
	return result;
}

// Marca como sucio un layout y todos los que tiene dentro, para que su sizeHint() sea el
// de ahora (después de ocultar, mover o cambiar el tamaño de algo).
void invalidateTree(QLayout *layout)
{
	layout->invalidate();
	for (int i = 0; i < layout->count(); ++i)
		if (QLayout *child = layout->itemAt(i)->layout())
			invalidateTree(child);
}

void setGap(QSpacerItem *gap, int height)
{
	gap->changeSize(0, height, QSizePolicy::Minimum, QSizePolicy::Fixed);
}

} // namespace

NowPlayingDock::NowPlayingDock(QWidget *parent) : QWidget(parent)
{
	{
		QFile saved(backdropFile());
		// Por defecto, fondo plano; el difuminado se activa con su botón (o clic derecho).
		blurBackground_ = saved.open(QIODevice::ReadOnly) && saved.readAll().trimmed() == "on";
	}
	// Todo va dentro de un área desplazable sin marco ni fondo: así el dock se puede achicar
	// cuanto se quiera (lo que no quepa se desplaza) y el fondo difuminado, pintado en el
	// propio dock, se sigue viendo detrás.
	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	outer->setSpacing(0);
	outer->setSizeConstraint(QLayout::SetNoConstraint);
	scroll_ = new QScrollArea(this);
	scroll_->setObjectName("nowPlayingScroll");
	scroll_->setFrameShape(QFrame::NoFrame);
	scroll_->setWidgetResizable(true);
	scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	scroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	scroll_->setFocusPolicy(Qt::NoFocus);
	scroll_->setAutoFillBackground(false);
	scroll_->viewport()->setObjectName("nowPlayingViewport");
	scroll_->viewport()->setAutoFillBackground(false);
	auto *content = new QWidget(scroll_);
	content->setObjectName("nowPlayingContent");
	content->setAutoFillBackground(false);
	scroll_->setStyleSheet("QScrollArea#nowPlayingScroll, QWidget#nowPlayingViewport, QWidget#nowPlayingContent"
			       " { background: transparent; }");
	outer->addWidget(scroll_);

	auto *layout = new QVBoxLayout(content);
	layout_ = layout;
	layout->setContentsMargins(16, 18, 16, 18);
	layout->setSpacing(4);
	auto addGap = [](QBoxLayout *box, int height) {
		auto *gap = new QSpacerItem(0, height, QSizePolicy::Minimum, QSizePolicy::Fixed);
		box->addSpacerItem(gap);
		return gap;
	};

	// "Reproductor [Automático ▾]", pequeño y arriba a la derecha.
	auto *playerRow = new QHBoxLayout();
	playerRow->setSpacing(8);
	playerRow->addStretch(1);
	playerLabel_ = new QLabel(text("NowPlaying.Player"), this);
	QPalette labelPalette = playerLabel_->palette();
	QColor labelColor = labelPalette.color(QPalette::WindowText);
	labelColor.setAlphaF(0.65f);
	labelPalette.setColor(QPalette::WindowText, labelColor);
	playerLabel_->setPalette(labelPalette);
	player_ = new QComboBox(this);
	player_->setFocusPolicy(Qt::NoFocus);
	player_->setCursor(Qt::PointingHandCursor);
	// Que la opción más larga ("Solo el reproductor del sistema") no fije el ancho mínimo del
	// dock: la lista se acorta y el texto se recorta.
	player_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	player_->setMinimumContentsLength(10);
	player_->setToolTip(text("NowPlaying.Player.DockHelp"));
	player_->addItem(text("NowPlaying.Player.Auto"), QString());
	player_->insertSeparator(1);
	const qreal dpr = devicePixelRatioF();
	for (const QString &key : MediaWatcher::players()) {
		const PlayerInfo &info = playerInfo()[key];
		player_->addItem(dotIcon(QColor(info.color), dpr), text(info.key), key);
	}
	player_->setCurrentIndex(std::max(0, player_->findData(MediaWatcher::instance().player())));
	connect(player_, &QComboBox::currentIndexChanged, this, [this] {
		MediaWatcher::instance().setPlayer(player_->currentData().toString());
		artKey_.clear();
		seekTarget_ = -1;
		refresh();
	});
	playerLabel_->setBuddy(player_);
	playerRow->addWidget(playerLabel_);
	playerRow->addWidget(player_);
	layout->addLayout(playerRow);
	gapAfterPlayer_ = addGap(layout, 8);

	// Carátula y textos: en columna (carátula arriba, centrada) o, en compacto, en fila
	// (miniatura a la izquierda).
	header_ = new QBoxLayout(QBoxLayout::TopToBottom);
	header_->setSpacing(16);
	art_ = new QLabel(this);
	art_->setAlignment(Qt::AlignCenter);
	art_->setStyleSheet("background: rgba(127,127,127,0.18); border-radius: 10px;");
	header_->addWidget(art_, 0, Qt::AlignHCenter);
	textColumn_ = new QVBoxLayout();
	textColumn_->setSpacing(4);
	header_->addLayout(textColumn_);
	layout->addLayout(header_);
	setArtSize(kArtSize);

	title_ = new QLabel(this);
	title_->setAlignment(Qt::AlignHCenter);
	QFont titleFont = title_->font();
	titleFont.setBold(true);
	titleFont.setPointSizeF(titleFont.pointSizeF() * 1.15);
	title_->setFont(titleFont);
	// El título, el artista y "Escuchando en …" se recortan al ancho del dock: su texto no
	// debe fijar un ancho mínimo (si no, el dock no se puede angostar).
	title_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	textColumn_->addWidget(title_);

	artist_ = new QLabel(this);
	artist_->setAlignment(Qt::AlignHCenter);
	QPalette muted = artist_->palette();
	QColor mutedColor = muted.color(QPalette::WindowText);
	mutedColor.setAlphaF(0.65f);
	muted.setColor(QPalette::WindowText, mutedColor);
	artist_->setPalette(muted);
	artist_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	textColumn_->addWidget(artist_);

	source_ = new QLabel(this);
	source_->setAlignment(Qt::AlignHCenter);
	QFont sourceFont = source_->font();
	sourceFont.setBold(true);
	sourceFont.setPointSizeF(sourceFont.pointSizeF() * 0.8);
	sourceFont.setCapitalization(QFont::AllUppercase);
	source_->setFont(sourceFont);
	source_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	gapBeforeSource_ = addGap(textColumn_, 4);
	textColumn_->addWidget(source_);
	gapAfterHeader_ = addGap(layout, 14);

	auto *progressRow = new QHBoxLayout();
	progressRow->setSpacing(10);
	elapsed_ = new QLabel("0:00", this);
	duration_ = new QLabel("0:00", this);
	progress_ = new SeekSlider(this);
	progress_->setFixedHeight(16);
	progress_->setRange(0, 1000);
	progress_->setCursor(Qt::PointingHandCursor);
	progress_->setFocusPolicy(Qt::NoFocus);
	progress_->setToolTip(text("NowPlaying.Seek"));
	// Mientras se arrastra, el tiempo muestra adónde se va a saltar; al soltar, se salta.
	connect(progress_, &QSlider::sliderMoved, this, [this](int value) {
		Track track = shown();
		if (track.durationMs > 0)
			elapsed_->setText(formatTime(value * track.durationMs / 1000));
	});
	connect(progress_, &QSlider::sliderReleased, this, [this] {
		Track track = shown();
		if (track.valid() && track.durationMs > 0)
			seekToMs(qint64(progress_->value()) * track.durationMs / 1000);
	});
	progressRow->addWidget(elapsed_);
	progressRow->addWidget(progress_, 1);
	progressRow->addWidget(duration_);
	layout->addLayout(progressRow);
	gapAfterProgress_ = addGap(layout, 16);

	buttons_ = new QHBoxLayout();
	auto *buttons = buttons_;
	buttons->setSpacing(10);
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
	auto makeSkip = [this, buttons](const char *tip, qint64 deltaMs) {
		auto *button = new QToolButton(this);
		button->setFixedSize(kSkipButton, kSkipButton);
		button->setToolTip(text(tip));
		button->setCursor(Qt::PointingHandCursor);
		button->setFocusPolicy(Qt::NoFocus);
		connect(button, &QToolButton::clicked, this, [this, deltaMs] { seekBy(deltaMs); });
		buttons->addWidget(button, 0, Qt::AlignVCenter);
		return button;
	};
	back_ = makeSkip("NowPlaying.Back10", -kSkipMs);
	previous_ = makeButton("NowPlaying.Previous", "previous", kSideButton);
	playPause_ = makeButton("NowPlaying.PlayPause", "playpause", kMainButton);
	next_ = makeButton("NowPlaying.Next", "next", kSideButton);
	forward_ = makeSkip("NowPlaying.Forward10", kSkipMs);
	buttons->addStretch(1);
	layout->addLayout(buttons);

	// El volumen del reproductor en su propia fila, a todo el ancho (para moverlo con
	// precisión); debajo, el aleatorio y el fondo (en mediano, al final de la misma fila).
	volumeRow_ = new QHBoxLayout();
	auto *volumeRow = volumeRow_;
	volumeRow->setSpacing(10);
	optionsRow_ = new QHBoxLayout();
	auto *optionsRow = optionsRow_;
	optionsRow->setSpacing(8);
	auto makeSmall = [this](const char *tip) {
		auto *button = new QToolButton(this);
		button->setFixedSize(kSmallButton, kSmallButton);
		button->setToolTip(text(tip));
		button->setCursor(Qt::PointingHandCursor);
		button->setFocusPolicy(Qt::NoFocus);
		return button;
	};
	shuffle_ = makeSmall("NowPlaying.Shuffle");
	optionsRow->addWidget(shuffle_, 0, Qt::AlignVCenter);
	backdropButton_ = makeSmall("NowPlaying.Dock.Backdrop");
	optionsRow->addWidget(backdropButton_, 0, Qt::AlignVCenter);
	connect(backdropButton_, &QToolButton::clicked, this, [this] { setBlurBackground(!blurBackground_); });
	optionsRow->addStretch(1);
	connect(shuffle_, &QToolButton::clicked, this, [this] {
		MediaWatcher::instance().toggleShuffle();
		QTimer::singleShot(600, this, [] { MediaWatcher::instance().refreshPlayerState(); });
	});
	mute_ = makeSmall("NowPlaying.Mute");
	volumeRow->addWidget(mute_, 0, Qt::AlignVCenter);
	connect(mute_, &QToolButton::clicked, this, &NowPlayingDock::toggleMute);
	volume_ = new VolumeSlider(this);
	volume_->setRange(0, 100);
	volume_->setFixedHeight(20);
	volume_->setMinimumWidth(60);
	volume_->setCursor(Qt::PointingHandCursor);
	volume_->setFocusPolicy(Qt::NoFocus);
	volumeRow->addWidget(volume_, 1);
	volumeLabel_ = new QLabel("—", this);
	volumeLabel_->setMinimumWidth(volumeLabel_->fontMetrics().horizontalAdvance("100%") + 2);
	volumeLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	volumeRow->addWidget(volumeLabel_);
	// Mientras se mueve, el número sigue a la barra; se aplica un instante después (al
	// arrastrar no se manda un cambio por cada píxel).
	volumeTimer_ = new QTimer(this);
	volumeTimer_->setSingleShot(true);
	volumeTimer_->setInterval(150);
	connect(volumeTimer_, &QTimer::timeout, this, &NowPlayingDock::applyVolume);
	connect(volume_, &QSlider::valueChanged, this, [this](int value) {
		volumeLabel_->setText(QString("%1%").arg(value));
		volumeHoldUntil_ = MediaWatcher::now() + kVolumeHoldMs;
		volumeTimer_->start();
	});
	connect(volume_, &QSlider::sliderReleased, this, &NowPlayingDock::applyVolume);

	gapBeforeVolume_ = addGap(layout, 12);
	layout->addLayout(volumeRow);
	gapBeforeOptions_ = addGap(layout, 6);
	layout->addLayout(optionsRow);
	gapBeforeStatus_ = addGap(layout, 10);

	status_ = new QLabel(this);
	status_->setWordWrap(true);
	status_->setAlignment(Qt::AlignHCenter);
	status_->hide(); // solo ocupa lugar cuando hay algo que decir
	layout->addWidget(status_);
	layout->addStretch(1);
	scroll_->setWidget(content);

	applyTier();
	applyAccent(QColor());
	connect(&MediaWatcher::instance(), &MediaWatcher::updated, this, &NowPlayingDock::refresh);
	connect(&MediaWatcher::instance(), &MediaWatcher::playerStateChanged, this, &NowPlayingDock::updateVolumeRow);
	// El volumen y el aleatorio se preguntan cada pocos segundos, solo con el dock a la vista.
	auto *statePoll = new QTimer(this);
	connect(statePoll, &QTimer::timeout, this, [this] {
		if (!isVisible())
			return;
		MediaWatcher::instance().refreshPlayerState();
		updateVolumeRow();
	});
	statePoll->start(2500);
	MediaWatcher::instance().refreshPlayerState();
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
	// Barra fina del color de la carátula con una perilla redonda para arrastrar.
	progress_->setStyleSheet(
		QString("QSlider::groove:horizontal { height: 4px; border-radius: 2px; background: rgba(127,127,127,0.25); }"
			"QSlider::sub-page:horizontal { border-radius: 2px; background: %1; }"
			"QSlider::handle:horizontal { width: 12px; height: 12px; margin: -4px 0; border-radius: 6px;"
			" background: %1; }"
			"QSlider::handle:horizontal:hover { background: %2; }")
			.arg(color, accent_.lighter(120).name()));
	source_->setStyleSheet(QString("color: %1;").arg(color));

	// Anterior / siguiente: redondos, con borde y fondo suave del color de la carátula,
	// que se aclaran al pasar el mouse y se marcan al presionar.
	auto round = [&](int size) {
		return QString("QToolButton { border-radius: %1px; border: 1px solid %2; background: %3; }"
			       "QToolButton:hover { border-color: %4; background: %5; }"
			       "QToolButton:pressed { background: %6; }"
			       "QToolButton:disabled { border-color: rgba(127,127,127,0.2); background: transparent; }")
			.arg(size / 2)
			.arg(rgba(accent_, 0.45), rgba(accent_, 0.14), color, rgba(accent_, 0.28), rgba(accent_, 0.45));
	};
	const QString side = round(sideSize_);
	previous_->setStyleSheet(side);
	next_->setStyleSheet(side);
	const QString skip = round(kSkipButton);
	back_->setStyleSheet(skip);
	forward_->setStyleSheet(skip);
	playPause_->setStyleSheet(QString("QToolButton { border-radius: %1px; border: 0; background: %2; }"
					  "QToolButton:hover { background: %3; }"
					  "QToolButton:pressed { background: %4; }"
					  "QToolButton:disabled { background: rgba(127,127,127,0.25); }")
					  .arg(mainSize_ / 2)
					  .arg(color, accent_.lighter(115).name(), accent_.darker(120).name()));
	styleVolumeRow();
	updateIcons();
}

// Aleatorio y fondo difuminado encendidos: del color de la carátula.
void NowPlayingDock::styleVolumeRow()
{
	// Encendido: con borde y fondo suave del color de la carátula; apagado, transparente.
	auto toggle = [&](bool on) {
		return on ? QString("QToolButton { border-radius: %1px; border: 1px solid %2; background: %3; }")
				    .arg(kSmallButton / 2)
				    .arg(rgba(accent_, 0.45), rgba(accent_, 0.14))
			  : QString("QToolButton { border-radius: %1px; border: 0; background: transparent; }"
				    "QToolButton:hover { background: rgba(127,127,127,0.18); }")
				    .arg(kSmallButton / 2);
	};
	shuffle_->setStyleSheet(toggle(shuffleOn_));
	backdropButton_->setStyleSheet(toggle(blurBackground_));
	mute_->setStyleSheet(QString("QToolButton { border-radius: %1px; border: 0; background: transparent; }"
				     "QToolButton:hover { background: rgba(127,127,127,0.18); }")
				     .arg(kSmallButton / 2));
	QColor textColor = palette().color(QPalette::WindowText);
	volume_->setStyleSheet(
		QString("QSlider::groove:horizontal { height: 4px; border-radius: 2px; background: rgba(127,127,127,0.25); }"
			"QSlider::sub-page:horizontal { border-radius: 2px; background: %1; }"
			"QSlider::handle:horizontal { width: 14px; height: 14px; margin: -5px 0; border-radius: 7px;"
			" background: %2; }"
			"QSlider::sub-page:horizontal:disabled, QSlider::handle:horizontal:disabled { background: rgba(127,127,127,0.35); }")
			.arg(rgba(textColor, 0.7), textColor.name()));
}

void NowPlayingDock::applyVolume()
{
	volumeTimer_->stop();
	const int value = volume_->value();
	if (value > 0)
		lastVolume_ = value;
	volumeHoldUntil_ = MediaWatcher::now() + kVolumeHoldMs;
	MediaWatcher::instance().setVolume(value);
	updateVolumeRow();
}

void NowPlayingDock::toggleMute()
{
	// Silenciar es bajar a 0 y recordar dónde estaba.
	const int value = volume_->value();
	if (value > 0)
		lastVolume_ = value;
	QSignalBlocker blocker(volume_);
	volume_->setValue(value > 0 ? 0 : std::max(5, lastVolume_));
	applyVolume();
}

void NowPlayingDock::updateVolumeRow()
{
	MediaWatcher &watcher = MediaWatcher::instance();
	const bool hasTrack = shown().valid();
	const PlayerState state = watcher.playerState();

	const int volume = hasTrack ? state.volume : -1;
	const bool usable = volume >= 0;
	volume_->setEnabled(usable);
	mute_->setEnabled(usable);
	if (usable && !volume_->isSliderDown() && MediaWatcher::now() >= volumeHoldUntil_) {
		QSignalBlocker blocker(volume_);
		volume_->setValue(volume);
	}
	const bool showMuted = usable && volume_->value() == 0;
	volumeLabel_->setText(usable && !showMuted ? QString("%1%").arg(volume_->value()) : QString("—"));
	mute_->setToolTip(text(showMuted ? "NowPlaying.Unmute" : "NowPlaying.Mute"));
	volume_->setToolTip(text(usable ? "NowPlaying.Volume.PlayerHelp" : "NowPlaying.Volume.Unsupported"));

	// Aleatorio: el del reproductor. "action": un botón sin estado (YouTube Music).
	shuffle_->setEnabled(hasTrack && !state.shuffle.isEmpty());
	shuffle_->setToolTip(text(state.shuffle.isEmpty() ? "NowPlaying.Shuffle.Unsupported"
				  : state.shuffle == "action" ? "NowPlaying.Shuffle.Action"
							      : "NowPlaying.Shuffle"));
	const bool shuffleOn = hasTrack && state.shuffle == "on";
	if (showMuted != muted_ || shuffleOn != shuffleOn_) {
		muted_ = showMuted;
		shuffleOn_ = shuffleOn;
		styleVolumeRow();
		updateIcons();
	}
}

QSize NowPlayingDock::minimumSizeHint() const
{
	return QSize(140, 100);
}

void NowPlayingDock::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	updateLayoutForSize();
	refresh(); // vuelve a recortar el título y el artista al ancho nuevo
}

// Elige el diseño por el tamaño del dock (con histéresis) y ajusta la carátula.
void NowPlayingDock::updateLayoutForSize()
{
	const int h = height();
	Tier next;
	if (tier_ == Tier::Large && h >= kLargeLeave)
		next = Tier::Large;
	else if (h >= kLargeEnter)
		next = Tier::Large;
	else if (tier_ != Tier::Compact && h >= kMediumLeave)
		next = Tier::Medium;
	else if (h >= kMediumEnter)
		next = Tier::Medium;
	else
		next = Tier::Compact;
	const bool narrow = narrow_ ? width() < kNarrowLeave : width() < kNarrowEnter;
	if (!tierApplied_ || next != tier_ || narrow != narrow_) {
		tier_ = next;
		narrow_ = narrow;
		applyTier();
	}
	updateArtSize();
}

// Grande: como siempre, con la carátula a la medida. Mediano: todo más junto y el aleatorio
// y el fondo al final de la fila del volumen. Compacto: miniatura al lado del título, sin
// "Escuchando en", sin la lista de reproductor ni la fila del volumen, y solo
// anterior / pausa / siguiente. Angosto: botones más chicos, sin ±10 s ni el porcentaje.
void NowPlayingDock::applyTier()
{
	tierApplied_ = true;
	const bool large = tier_ == Tier::Large;
	const bool compact = tier_ == Tier::Compact;

	const int sideMargin = (narrow_ || compact) ? 10 : large ? 16 : 12;
	const int topMargin = large ? 18 : compact ? 6 : 10;
	layout_->setContentsMargins(sideMargin, topMargin, sideMargin, topMargin);
	layout_->setSpacing(large ? 4 : compact ? 2 : 3);
	setGap(gapAfterPlayer_, large ? 8 : compact ? 0 : 4);
	setGap(gapBeforeSource_, large ? 4 : 2);
	setGap(gapAfterHeader_, large ? 14 : compact ? 4 : 8);
	setGap(gapAfterProgress_, large ? 16 : compact ? 2 : 8);
	setGap(gapBeforeVolume_, large ? 12 : compact ? 0 : 8);
	setGap(gapBeforeOptions_, large ? 6 : 0);
	setGap(gapBeforeStatus_, large ? 10 : compact ? 0 : 6);

	player_->setVisible(!compact);
	playerLabel_->setVisible(!compact && !narrow_);
	player_->setMinimumContentsLength(narrow_ ? 6 : 10);

	header_->setDirection(compact ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom);
	header_->setSpacing(large ? 16 : compact ? 10 : 8);
	header_->setAlignment(art_, compact ? Qt::AlignVCenter : Qt::AlignHCenter);
	header_->setAlignment(textColumn_, compact ? Qt::AlignVCenter : Qt::Alignment());
	header_->setStretchFactor(textColumn_, compact ? 1 : 0);
	const Qt::Alignment textAlign = compact ? (Qt::AlignLeft | Qt::AlignVCenter) : Qt::AlignHCenter;
	title_->setAlignment(textAlign);
	artist_->setAlignment(textAlign);
	source_->setVisible(!compact);

	const bool smallTransport = compact || narrow_;
	sideSize_ = smallTransport ? kSideButtonSmall : kSideButton;
	mainSize_ = smallTransport ? kMainButtonSmall : kMainButton;
	previous_->setFixedSize(sideSize_, sideSize_);
	next_->setFixedSize(sideSize_, sideSize_);
	playPause_->setFixedSize(mainSize_, mainSize_);
	back_->setVisible(!smallTransport);
	forward_->setVisible(!smallTransport);
	buttons_->setSpacing(smallTransport ? 6 : 10);

	// Mediano: el aleatorio y el fondo pasan al final de la fila del volumen.
	const bool merged = tier_ == Tier::Medium;
	if (merged && volumeRow_->indexOf(shuffle_) < 0) {
		optionsRow_->removeWidget(shuffle_);
		optionsRow_->removeWidget(backdropButton_);
		volumeRow_->addWidget(shuffle_, 0, Qt::AlignVCenter);
		volumeRow_->addWidget(backdropButton_, 0, Qt::AlignVCenter);
	} else if (!merged && optionsRow_->indexOf(shuffle_) < 0) {
		volumeRow_->removeWidget(shuffle_);
		volumeRow_->removeWidget(backdropButton_);
		optionsRow_->insertWidget(0, shuffle_, 0, Qt::AlignVCenter);
		optionsRow_->insertWidget(1, backdropButton_, 0, Qt::AlignVCenter);
	}
	volumeRow_->setSpacing(narrow_ ? 6 : 10);
	for (QWidget *widget : {static_cast<QWidget *>(mute_), static_cast<QWidget *>(volume_),
				static_cast<QWidget *>(shuffle_), static_cast<QWidget *>(backdropButton_)})
		widget->setVisible(!compact);
	volumeLabel_->setVisible(!compact && !narrow_);
	volume_->setMinimumWidth(narrow_ ? 40 : 60);

	invalidateTree(layout_);
	applyAccent(accent_); // los botones redondos cambian de radio con su tamaño
}

// Carátula cuadrada que llena el lugar que sobra (grande: entre 96 y 320 px; mediano: entre
// 56 y 120, o nada si no cabe); en compacto, una miniatura.
void NowPlayingDock::updateArtSize()
{
	if (tier_ == Tier::Compact) {
		setArtSize(kArtThumb);
		return;
	}
	const bool large = tier_ == Tier::Large;
	art_->setVisible(true);
	invalidateTree(layout_);
	const QMargins margins = layout_->contentsMargins();
	const int rest = layout_->sizeHint().height() - art_->minimumHeight();
	const int fit = std::min(width() - margins.left() - margins.right(), height() - rest);
	const int low = large ? kArtLargeMin : kArtMediumMin;
	const int high = large ? kArtLargeMax : kArtMediumMax;
	if (fit < low) {
		setArtSize(large ? low : 0);
		return;
	}
	setArtSize(std::max(low, std::min(fit, high) / 8 * 8));
}

void NowPlayingDock::setArtSize(int size)
{
	if (size <= 0) {
		art_->hide();
		return;
	}
	art_->show();
	if (size == artSize_ && devicePixelRatioF() == artDpr_)
		return;
	if (size != artSize_) {
		artSize_ = size;
		art_->setFixedSize(size, size);
		art_->setStyleSheet(QString("background: rgba(127,127,127,0.18); border-radius: %1px;")
					    .arg(size <= kArtThumb ? 6 : 10));
	}
	updateArtPixmap();
}

// Escala la carátula ya decodificada al tamaño actual (y a la densidad de la pantalla); solo
// cuando cambia la canción o el tamaño.
void NowPlayingDock::updateArtPixmap()
{
	artDpr_ = devicePixelRatioF();
	if (artSource_.isNull() || artSize_ <= 0) {
		art_->clear();
		return;
	}
	const int side = qRound(artSize_ * artDpr_);
	QPixmap scaled =
		artSource_.scaled(QSize(side, side), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
	scaled = scaled.copy((scaled.width() - side) / 2, (scaled.height() - side) / 2, side, side);
	scaled.setDevicePixelRatio(artDpr_);
	art_->setPixmap(scaled);
}

void NowPlayingDock::paintEvent(QPaintEvent *event)
{
	QWidget::paintEvent(event);
	if (!blurBackground_ || backdrop_.isNull())
		return;
	QPainter p(this);
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	// Cubre todo el dock (recortando lo que sobre) y encima un velo del color de fondo del
	// tema, para que el texto y los botones se lean igual que sin fondo.
	const QSize area = size();
	QSize cover = backdrop_.size().scaled(area, Qt::KeepAspectRatioByExpanding);
	QRect target(QPoint((area.width() - cover.width()) / 2, (area.height() - cover.height()) / 2), cover);
	p.drawImage(target, backdrop_);
	QColor veil = palette().color(QPalette::Window);
	veil.setAlphaF(0.6f);
	p.fillRect(rect(), veil);
}

void NowPlayingDock::contextMenuEvent(QContextMenuEvent *event)
{
	QMenu menu(this);
	QAction *blur = menu.addAction(text("NowPlaying.Dock.Backdrop"));
	blur->setCheckable(true);
	blur->setChecked(blurBackground_);
	if (menu.exec(event->globalPos()) != blur)
		return;
	setBlurBackground(blur->isChecked());
}

void NowPlayingDock::setBlurBackground(bool on)
{
	blurBackground_ = on;
	QDir().mkpath(QFileInfo(backdropFile()).absolutePath());
	QSaveFile file(backdropFile());
	if (file.open(QIODevice::WriteOnly)) {
		file.write(on ? "on" : "off");
		file.commit();
	}
	styleVolumeRow();
	updateIcons();
	update();
}

void NowPlayingDock::changeEvent(QEvent *event)
{
	QWidget::changeEvent(event);
	// Al cambiar el tema de OBS, los iconos toman el nuevo color del texto.
	if (event->type() == QEvent::PaletteChange && mute_) {
		styleVolumeRow();
		updateIcons();
	}
}

void NowPlayingDock::updateIcons()
{
	const qreal dpr = devicePixelRatioF();
	const QColor textColor = palette().color(QPalette::WindowText);
	const int side = sideSize_ / 2;
	previous_->setIcon(glyphIcon(Glyph::Previous, textColor, side, dpr));
	previous_->setIconSize(QSize(side, side));
	next_->setIcon(glyphIcon(Glyph::Next, textColor, side, dpr));
	next_->setIconSize(QSize(side, side));
	const int skip = kSkipButton - 6;
	back_->setIcon(glyphIcon(Glyph::Back10, textColor, skip, dpr));
	back_->setIconSize(QSize(skip, skip));
	forward_->setIcon(glyphIcon(Glyph::Forward10, textColor, skip, dpr));
	forward_->setIconSize(QSize(skip, skip));
	const int main = mainSize_ / 2;
	playPause_->setIcon(glyphIcon(playing_ ? Glyph::Pause : Glyph::Play, contrastOn(accent_), main, dpr));
	playPause_->setIconSize(QSize(main, main));
	const int small = kSmallButton - 10;
	shuffle_->setIcon(glyphIcon(Glyph::Shuffle, shuffleOn_ ? accent_ : textColor, small, dpr));
	shuffle_->setIconSize(QSize(small, small));
	backdropButton_->setIcon(glyphIcon(Glyph::Backdrop, blurBackground_ ? accent_ : textColor, small, dpr));
	backdropButton_->setIconSize(QSize(small, small));
	mute_->setIcon(glyphIcon(muted_ ? Glyph::SpeakerMuted : Glyph::Speaker, textColor, small, dpr));
	mute_->setIconSize(QSize(small, small));
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
	status_->setVisible(!status_->text().isEmpty());

	Track track = shown();
	for (QToolButton *button : {previous_, playPause_, next_})
		button->setEnabled(track.valid());

	if (!track.valid()) {
		const QString player = watcher.player();
		if (watcher.status() == MediaWatcher::Status::Starting) {
			title_->setText(text("NowPlaying.NothingPlaying"));
			artist_->setText(text("NowPlaying.Reader.Starting"));
		} else if (playerInfo().contains(player)) {
			// Con un reproductor elegido, lo que suene en otro no cuenta.
			const char *raw = playerInfo()[player].name;
			const QString name = QString::fromUtf8(raw).startsWith("NowPlaying.") ? text(raw) : QString::fromUtf8(raw);
			title_->setText(text("NowPlaying.Player.NotPlaying").arg(name));
			artist_->setText(text("NowPlaying.Player.Waiting").arg(name));
		} else {
			title_->setText(text("NowPlaying.NothingPlaying"));
			artist_->setText(text("NowPlaying.PlaySomething"));
		}
		source_->clear();
		artSource_ = QPixmap();
		updateArtPixmap();
		artKey_.clear();
		backdrop_ = QImage();
		update();
		playing_ = false; // el botón apagado muestra "play", no "pausa"
		applyAccent(QColor());
		updateVolumeRow();
		tick();
		return;
	}

	// Ancho para el título y el artista: el del dock sin márgenes (y sin la miniatura en compacto).
	const QMargins margins = layout_->contentsMargins();
	int width = scroll_->viewport()->width() - margins.left() - margins.right();
	if (tier_ == Tier::Compact && art_->isVisible())
		width -= artSize_ + header_->spacing();
	width = std::max(40, width);
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
		// Otra canción (u otro reproductor): su volumen y su aleatorio.
		watcher.refreshPlayerState();
		QPixmap pixmap = pixmapFromDataUrl(track.artwork);
		artSource_ = pixmap;
		updateArtPixmap();
		if (pixmap.isNull()) {
			backdrop_ = QImage();
			applyAccent(QColor());
		} else {
			backdrop_ = blurredBackdrop(pixmap.toImage());
			applyAccent(accentFrom(pixmap.toImage()));
		}
		update();
	}
	updateVolumeRow();
	tick();
}

Track NowPlayingDock::shown() const
{
	MediaWatcher &watcher = MediaWatcher::instance();
	return watcher.current(true, watcher.player());
}

void NowPlayingDock::tick()
{
	Track track = shown();
	bool show = track.valid() && track.durationMs > 0;
	progress_->setVisible(show);
	elapsed_->setVisible(show);
	duration_->setVisible(show);
	back_->setEnabled(show);
	forward_->setEnabled(show);
	if (!show || progress_->isSliderDown())
		return; // mientras se arrastra manda la barra
	const qint64 now = MediaWatcher::now();
	qint64 position = track.progressNow(now);
	// Recién saltado: el lector todavía trae la posición de antes durante un momento.
	if (seekTarget_ >= 0 && now - seekAt_ < 3000)
		position = std::min(track.durationMs, seekTarget_ + (track.isPlaying ? now - seekAt_ : 0));
	progress_->setValue(int(position * 1000 / track.durationMs));
	elapsed_->setText(formatTime(position));
	duration_->setText(formatTime(track.durationMs));
}

void NowPlayingDock::seekBy(qint64 deltaMs)
{
	Track track = shown();
	if (!track.valid() || track.durationMs <= 0)
		return;
	const qint64 now = MediaWatcher::now();
	qint64 position = seekTarget_ >= 0 && now - seekAt_ < 3000 ? seekTarget_ + (track.isPlaying ? now - seekAt_ : 0)
								   : track.progressNow(now);
	seekToMs(std::clamp<qint64>(position + deltaMs, 0, std::max<qint64>(0, track.durationMs - 1000)));
}

void NowPlayingDock::seekToMs(qint64 positionMs)
{
	MediaWatcher::instance().seekTo(positionMs);
	seekTarget_ = positionMs;
	seekAt_ = MediaWatcher::now();
	tick();
}
