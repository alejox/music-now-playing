#include "updater.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMainWindow>
#include <QMessageBox>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>

namespace {

const char *kLatestRelease = "https://api.github.com/repos/alejox/obs-music-player/releases/latest";
const char *kReleasesPage = "https://github.com/alejox/obs-music-player/releases/latest";

#ifdef _WIN32
const char *kPlatform = "windows-x64";
#else
const char *kPlatform = "macos";
#endif

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

QWidget *mainWindow()
{
	return static_cast<QMainWindow *>(obs_frontend_get_main_window());
}

QString systemTool(const char *name)
{
#ifdef _WIN32
	// curl.exe y tar.exe vienen con Windows 10 (1803) y 11.
	return qEnvironmentVariable("SystemRoot", "C:\\Windows") + "\\System32\\" + name + ".exe";
#else
	return QString("/usr/bin/") + name;
#endif
}

// La versión que el usuario pidió no volver a ofrecer.
QString skipFile()
{
	char *path = obs_module_config_path("skip-version.txt");
	QString result = QString::fromUtf8(path);
	bfree(path);
	return result;
}

QString skippedVersion()
{
	QFile file(skipFile());
	return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).trimmed() : QString();
}

void skipVersion(const QString &version)
{
	QDir().mkpath(QFileInfo(skipFile()).absolutePath());
	QSaveFile file(skipFile());
	if (file.open(QIODevice::WriteOnly)) {
		file.write(version.toUtf8());
		file.commit();
	}
}

// Carpeta raíz del plugin instalado:
//   macOS:   …/obs-now-playing.plugin            (binario en Contents/MacOS/)
//   Windows: …\obs-now-playing                    (binario en bin\64bit\)
QString installedRoot()
{
	QString binary = QString::fromUtf8(obs_get_module_binary_path(obs_current_module()));
	QDir dir = QFileInfo(binary).absoluteDir();
	dir.cdUp();
	dir.cdUp();
	return dir.absolutePath();
}

// Copia un árbol de carpetas encima de otro (crea lo que falte, pisa lo que exista).
bool copyTree(const QString &from, const QString &to)
{
	QDir source(from);
	QDirIterator it(from, QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);
	while (it.hasNext()) {
		QString file = it.next();
		QString target = QDir(to).filePath(source.relativeFilePath(file));
		QDir().mkpath(QFileInfo(target).absolutePath());
		if (QFile::exists(target) && !QFile::remove(target))
			return false;
		if (!QFile::copy(file, target))
			return false;
	}
	return true;
}

} // namespace

Updater &Updater::instance()
{
	static Updater *updater = new Updater();
	return *updater;
}

int Updater::compareVersions(const QString &a, const QString &b)
{
	QStringList pa = a.split('.');
	QStringList pb = b.split('.');
	for (qsizetype i = 0; i < std::max(pa.size(), pb.size()); ++i) {
		int x = i < pa.size() ? pa[i].toInt() : 0;
		int y = i < pb.size() ? pb[i].toInt() : 0;
		if (x != y)
			return x < y ? -1 : 1;
	}
	return 0;
}

void Updater::cleanupPreviousUpdate()
{
#ifdef _WIN32
	// En Windows el .dll en uso no se puede borrar, pero sí renombrar: la actualización
	// lo deja como .old y aquí, ya con el nuevo cargado, se borra.
	QDirIterator it(installedRoot(), {"*.old"}, QDir::Files, QDirIterator::Subdirectories);
	while (it.hasNext())
		QFile::remove(it.next());
#endif
	QDir(QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath("obs-now-playing-update"))
		.removeRecursively();
}

void Updater::check(bool manual)
{
	if (busy_)
		return;
	busy_ = true;
	auto *curl = new QProcess(this);
	connect(curl, &QProcess::finished, this, [this, curl, manual](int code, QProcess::ExitStatus) {
		QByteArray json = curl->readAllStandardOutput();
		curl->deleteLater();
		busy_ = false;
		if (code != 0) {
			obs_log(LOG_INFO, "no se pudo revisar si hay actualizaciones (curl %d)", code);
			if (manual)
				fail(text("NowPlaying.Update.CheckFailed"), kReleasesPage);
			return;
		}
		handleRelease(json, manual);
	});
	connect(curl, &QProcess::errorOccurred, this, [this, curl, manual](QProcess::ProcessError error) {
		if (error != QProcess::FailedToStart)
			return;
		curl->deleteLater();
		busy_ = false;
		if (manual)
			fail(text("NowPlaying.Update.CheckFailed"), kReleasesPage);
	});
	curl->start(systemTool("curl"), {"-fsSL", "--max-time", "20", "-H", "Accept: application/vnd.github+json",
					 QString::fromUtf8(kLatestRelease)});
}

void Updater::handleRelease(const QByteArray &json, bool manual)
{
	QJsonObject release = QJsonDocument::fromJson(json).object();
	QString pageUrl = release.value("html_url").toString(kReleasesPage);

	// Los zips del plugin se llaman obs-now-playing-plugin-<versión>-<sistema>.zip.
	QRegularExpression pattern(QString("^obs-now-playing-plugin-(\\d+(?:\\.\\d+)*)-%1\\.zip$").arg(kPlatform));
	QString version;
	QString url;
	const QJsonArray assets = release.value("assets").toArray();
	for (qsizetype i = 0; i < assets.size(); ++i) {
		QJsonObject asset = assets.at(i).toObject();
		QRegularExpressionMatch match = pattern.match(asset.value("name").toString());
		if (match.hasMatch()) {
			version = match.captured(1);
			url = asset.value("browser_download_url").toString();
		}
	}

	if (version.isEmpty() || !url.startsWith("https://github.com/")) {
		if (manual)
			fail(text("NowPlaying.Update.CheckFailed"), pageUrl);
		return;
	}
	if (compareVersions(version, QString::fromUtf8(PLUGIN_VERSION)) <= 0) {
		if (manual)
			QMessageBox::information(mainWindow(), text("NowPlaying.Update.Title"),
						 text("NowPlaying.Update.UpToDate").arg(QString::fromUtf8(PLUGIN_VERSION)));
		return;
	}
	if (!manual && version == skippedVersion())
		return;
	askToUpdate(version, url, pageUrl);
}

void Updater::askToUpdate(const QString &version, const QString &url, const QString &pageUrl)
{
	QMessageBox box(mainWindow());
	box.setWindowTitle(text("NowPlaying.Update.Title"));
	box.setIcon(QMessageBox::Information);
	box.setTextFormat(Qt::RichText);
	box.setTextInteractionFlags(Qt::TextBrowserInteraction);
	box.setText(text("NowPlaying.Update.Available")
			    .arg(version, QString::fromUtf8(PLUGIN_VERSION), pageUrl.toHtmlEscaped()));
	QPushButton *update = box.addButton(text("NowPlaying.Update.Now"), QMessageBox::AcceptRole);
	box.addButton(text("NowPlaying.Update.Later"), QMessageBox::RejectRole);
	QPushButton *skip = box.addButton(text("NowPlaying.Update.Skip"), QMessageBox::DestructiveRole);
	box.setDefaultButton(update);
	box.exec();

	if (box.clickedButton() == update)
		download(version, url);
	else if (box.clickedButton() == skip)
		skipVersion(version);
}

void Updater::download(const QString &version, const QString &url)
{
	QString dir = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath("obs-now-playing-update");
	QDir(dir).removeRecursively();
	QDir().mkpath(dir);
	QString zip = QDir(dir).filePath("plugin.zip");

	progress_ = new QProgressDialog(text("NowPlaying.Update.Downloading").arg(version), QString(), 0, 0, mainWindow());
	progress_->setWindowTitle(text("NowPlaying.Update.Title"));
	progress_->setWindowModality(Qt::WindowModal);
	progress_->setMinimumDuration(0);
	progress_->show();

	auto *curl = new QProcess(this);
	connect(curl, &QProcess::finished, this, [this, curl, version, zip](int code, QProcess::ExitStatus) {
		curl->deleteLater();
		if (code != 0 || QFileInfo(zip).size() == 0) {
			fail(text("NowPlaying.Update.DownloadFailed"), kReleasesPage);
			return;
		}
		install(version, zip);
	});
	curl->start(systemTool("curl"), {"-fsSL", "--max-time", "300", "-o", zip, url});
}

void Updater::install(const QString &version, const QString &zipPath)
{
	QString workDir = QFileInfo(zipPath).absolutePath();
	QString extracted = QDir(workDir).filePath("unzipped");
	QDir().mkpath(extracted);

#ifdef _WIN32
	int unzip = QProcess::execute(systemTool("tar"), {"-xf", zipPath, "-C", extracted});
	// El zip trae obs-now-playing\bin\64bit\…dll y obs-now-playing\data\…
	QString newRoot = QDir(extracted).filePath("obs-now-playing");
#else
	int unzip = QProcess::execute(systemTool("ditto"), {"-x", "-k", zipPath, extracted});
	QString newRoot = QDir(extracted).filePath("obs-now-playing.plugin");
#endif
	if (unzip != 0 || !QFileInfo(newRoot).isDir()) {
		fail(text("NowPlaying.Update.InstallFailed"), kReleasesPage);
		return;
	}

	QString root = installedRoot();
	// Solo se actualiza un plugin en su propia carpeta (la que crean las instrucciones).
	// Si está metido en la carpeta de OBS, reemplazar archivos ahí sería peligroso.
#ifdef _WIN32
	bool ownFolder = QFileInfo(root).fileName() == "obs-now-playing";
#else
	bool ownFolder = QFileInfo(root).fileName() == "obs-now-playing.plugin";
#endif
	if (!ownFolder) {
		obs_log(LOG_WARNING, "plugin fuera de su carpeta (%s): no se actualiza solo", root.toUtf8().constData());
		fail(text("NowPlaying.Update.InstallFailed"), kReleasesPage);
		return;
	}
	bool ok = false;
#ifdef _WIN32
	// El .dll cargado no se puede pisar, pero sí renombrar: queda como .old y se borra en el
	// próximo arranque. Si el plugin está en Archivos de programa sin permisos, falla aquí.
	QString dll = QDir(root).filePath("bin/64bit/obs-now-playing.dll");
	QString old = dll + "." + QString::number(QDateTime::currentSecsSinceEpoch()) + ".old";
	ok = QFile::rename(dll, old) && copyTree(newRoot, root);
	if (!ok && QFile::exists(old) && !QFile::exists(dll))
		QFile::rename(old, dll); // deja el plugin como estaba
#else
	// En macOS el binario cargado sigue funcionando aunque se reemplace el paquete.
	QString backup = root + ".old";
	QDir(backup).removeRecursively();
	if (QDir().rename(root, backup)) {
		ok = QDir().rename(newRoot, root) || copyTree(newRoot, root);
		if (ok) {
			QDir(backup).removeRecursively();
			// Por si acaso: sin la marca de "descargado de internet" macOS no lo bloquea.
			QProcess::execute("/usr/bin/xattr", {"-cr", root});
		} else {
			QDir(root).removeRecursively();
			QDir().rename(backup, root); // deja el plugin como estaba
		}
	}
#endif

	if (progress_) {
		progress_->close();
		progress_->deleteLater();
		progress_ = nullptr;
	}
	if (!ok) {
		obs_log(LOG_WARNING, "no se pudo instalar la versión %s en %s", version.toUtf8().constData(),
			root.toUtf8().constData());
		fail(text("NowPlaying.Update.InstallFailed"), kReleasesPage);
		return;
	}
	obs_log(LOG_INFO, "versión %s instalada; se usa al reiniciar OBS", version.toUtf8().constData());
	QMessageBox::information(mainWindow(), text("NowPlaying.Update.Title"), text("NowPlaying.Update.Done").arg(version));
}

void Updater::fail(const QString &message, const QString &pageUrl)
{
	if (progress_) {
		progress_->close();
		progress_->deleteLater();
		progress_ = nullptr;
	}
	QMessageBox box(mainWindow());
	box.setWindowTitle(text("NowPlaying.Update.Title"));
	box.setIcon(QMessageBox::Warning);
	box.setText(message);
	QPushButton *open = box.addButton(text("NowPlaying.Update.OpenPage"), QMessageBox::AcceptRole);
	box.addButton(QMessageBox::Close);
	box.exec();
	if (box.clickedButton() == open)
		QDesktopServices::openUrl(QUrl(pageUrl));
}
