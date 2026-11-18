#include "support-dialog.hpp"

#include <obs-module.h>

#include <QDesktopServices>
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace {

const char *const kKofiUrl = "https://ko-fi.com/alejoxcode";

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

} // namespace

void open_support_dialog(QWidget *parent)
{
	QDialog dialog(parent);
	dialog.setWindowTitle(text("NowPlaying.Support.Link"));
	dialog.setMinimumWidth(380);
	auto *layout = new QVBoxLayout(&dialog);
	layout->setContentsMargins(22, 20, 22, 18);
	layout->setSpacing(12);

	auto *title = new QLabel(QStringLiteral("☕  ") + text("NowPlaying.Support.Title"), &dialog);
	QFont titleFont = title->font();
	titleFont.setBold(true);
	titleFont.setPointSizeF(titleFont.pointSizeF() * 1.3);
	title->setFont(titleFont);
	title->setWordWrap(true);
	layout->addWidget(title);

	for (const char *key : {"NowPlaying.Support.Free", "NowPlaying.Support.Why", "NowPlaying.Support.NoPressure"}) {
		auto *paragraph = new QLabel(text(key), &dialog);
		paragraph->setWordWrap(true);
		layout->addWidget(paragraph);
	}
	layout->addSpacing(4);

	// El principal, resaltado con el color de selección del tema de OBS; el otro, normal.
	auto *kofi = new QPushButton(text("NowPlaying.Support.Kofi"), &dialog);
	kofi->setDefault(true);
	kofi->setCursor(Qt::PointingHandCursor);
	kofi->setMinimumHeight(34);
	kofi->setStyleSheet("QPushButton { background-color: palette(highlight); color: palette(highlighted-text); "
			    "font-weight: bold; border: none; border-radius: 6px; padding: 6px 12px; }");
	auto *later = new QPushButton(text("NowPlaying.Support.Continue"), &dialog);
	later->setMinimumHeight(34);
	layout->addWidget(kofi);
	layout->addWidget(later);

	QObject::connect(kofi, &QPushButton::clicked, &dialog, [&dialog] {
		QDesktopServices::openUrl(QUrl(QString::fromLatin1(kKofiUrl)));
		dialog.accept();
	});
	QObject::connect(later, &QPushButton::clicked, &dialog, &QDialog::reject);
	dialog.exec();
}
