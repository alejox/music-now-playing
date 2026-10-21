#include "updater.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>

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

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

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
//   macOS:   …/obs-now-playing.plugin   (binario en Contents/MacOS/)
//   Windows: la carpeta de OBS          (binario en obs-plugins\64bit\)
QString installedRoot()
{
	QString binary = QString::fromUtf8(obs_get_module_binary_path(obs_current_module()));
	QDir dir = QFileInfo(binary).absoluteDir();
	dir.cdUp();
	dir.cdUp();
	return dir.absolutePath();
}

// Copia un árbol de carpetas encima de otro (crea lo que falte, pisa lo que exista).
bool copyTree(const QString &from, const QString &to, QString *problem)
{
	QDir source(from);
	QDirIterator it(from, QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);
	while (it.hasNext()) {
		QString file = it.next();
		QString target = QDir(to).filePath(source.relativeFilePath(file));
		QDir().mkpath(QFileInfo(target).absolutePath());
		QFile existing(target);
		if (existing.exists() && !existing.remove()) {
			*problem = QString("No se pudo reemplazar %1: %2")
					   .arg(QDir::toNativeSeparators(target), existing.errorString());
			return false;
		}
		QFile copy(file);
		if (!copy.copy(target)) {
			*problem = QString("No se pudo copiar %1: %2").arg(QDir::toNativeSeparators(target), copy.errorString());
			return false;
		}
	}
	return true;
}

#ifdef _WIN32
// La carpeta de OBS (Archivos de programa) pide permiso de administrador: la copia la hace
// una PowerShell con "Ejecutar como administrador" (Windows muestra el aviso de permiso).
// Devuelve el código de salida, o -1 si no se pudo pedir el permiso (o se canceló).
int runElevated(const QString &program, const QString &params, QString *problem)
{
	std::wstring file = program.toStdWString();
	std::wstring args = params.toStdWString();
	SHELLEXECUTEINFOW info = {};
	info.cbSize = sizeof(info);
	info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
	info.lpVerb = L"runas";
	info.lpFile = file.c_str();
	info.lpParameters = args.c_str();
	info.nShow = SW_HIDE;
	if (!ShellExecuteExW(&info) || !info.hProcess) {
		DWORD error = GetLastError();
		*problem = error == ERROR_CANCELLED ? QString("Se canceló el permiso de administrador.")
						    : QString("No se pudo pedir permiso de administrador (error %1).").arg(error);
		return -1;
	}
	WaitForSingleObject(info.hProcess, 180000);
	DWORD code = 1;
	GetExitCodeProcess(info.hProcess, &code);
	CloseHandle(info.hProcess);
	return int(code);
}

// Copia el zip descomprimido (obs-plugins\, data\) encima de la carpeta de OBS. El .dll
// cargado no se puede pisar, pero sí renombrar: queda como .old (se borra en la próxima
// actualización) y el nuevo se usa al reiniciar OBS. Si la copia falla, vuelve el de antes.
const char *kInstallScript = R"PS(param([string]$Source, [string]$Obs)
$ErrorActionPreference = 'Stop'
$dir = Join-Path $Obs 'obs-plugins\64bit'
$dll = Join-Path $dir 'obs-now-playing.dll'
Get-ChildItem -LiteralPath $dir -Filter 'obs-now-playing.dll.*.old' -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
$old = ''
if (Test-Path -LiteralPath $dll) {
  $old = "$dll.$([DateTimeOffset]::Now.ToUnixTimeSeconds()).old"
  Rename-Item -LiteralPath $dll -NewName (Split-Path $old -Leaf)
}
robocopy $Source $Obs /E /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) {
  if ($old -and -not (Test-Path -LiteralPath $dll)) { Rename-Item -LiteralPath $old -NewName 'obs-now-playing.dll' }
  exit 1
}
exit 0
)PS";
#endif

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
				fail(text("NowPlaying.Update.CheckFailed"), kReleasesPage,
				     QString("curl %1: %2").arg(code).arg(QString::fromUtf8(curl->readAllStandardError()).trimmed()));
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
			fail(text("NowPlaying.Update.CheckFailed"), kReleasesPage, curl->errorString());
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
			fail(text("NowPlaying.Update.CheckFailed"), pageUrl,
			     release.contains("message") ? "GitHub: " + release.value("message").toString()
							 : QString("La Release no trae obs-now-playing-plugin-…-%1.zip").arg(kPlatform));
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
			fail(text("NowPlaying.Update.DownloadFailed"), kReleasesPage,
			     QString("curl %1: %2").arg(code).arg(QString::fromUtf8(curl->readAllStandardError()).trimmed()));
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
	// El zip trae la forma de la carpeta de OBS: obs-plugins\64bit\…dll y data\obs-plugins\…
	QString newRoot = extracted;
	bool unzipped = unzip == 0 && QFileInfo::exists(QDir(extracted).filePath("obs-plugins/64bit/obs-now-playing.dll"));
#else
	int unzip = QProcess::execute(systemTool("ditto"), {"-x", "-k", zipPath, extracted});
	QString newRoot = QDir(extracted).filePath("obs-now-playing.plugin");
	bool unzipped = unzip == 0 && QFileInfo(newRoot).isDir();
#endif
	if (!unzipped) {
		fail(text("NowPlaying.Update.InstallFailed"), kReleasesPage,
		     QString("No se pudo descomprimir %1 (código %2)").arg(QDir::toNativeSeparators(zipPath)).arg(unzip));
		return;
	}

	QString root = installedRoot();
	// Solo se actualiza un plugin instalado como dicen las instrucciones: en macOS en su
	// propia carpeta; en Windows en la de OBS (el .dll en obs-plugins\64bit\).
#ifdef _WIN32
	QDir binDir = QFileInfo(QString::fromUtf8(obs_get_module_binary_path(obs_current_module()))).absoluteDir();
	bool ownFolder = binDir.dirName() == "64bit" && QFileInfo(binDir.absolutePath()).dir().dirName() == "obs-plugins";
#else
	bool ownFolder = QFileInfo(root).fileName() == "obs-now-playing.plugin";
#endif
	if (!ownFolder) {
		obs_log(LOG_WARNING, "plugin fuera de su carpeta (%s): no se actualiza solo", root.toUtf8().constData());
		fail(text("NowPlaying.Update.InstallFailed"), kReleasesPage,
		     QString("El plugin no está en su propia carpeta: %1").arg(QDir::toNativeSeparators(root)));
		return;
	}
	bool ok = false;
	QString problem;
#ifdef _WIN32
	QString script = QDir(workDir).filePath("instalar.ps1");
	QFile scriptFile(script);
	if (!scriptFile.open(QIODevice::WriteOnly) || scriptFile.write(kInstallScript) < 0) {
		problem = QString("No se pudo escribir %1").arg(QDir::toNativeSeparators(script));
	} else {
		scriptFile.close();
		QString params = QString("-NoProfile -ExecutionPolicy Bypass -File \"%1\" -Source \"%2\" -Obs \"%3\"")
					 .arg(QDir::toNativeSeparators(script), QDir::toNativeSeparators(newRoot),
					      QDir::toNativeSeparators(root));
		int code = runElevated("powershell.exe", params, &problem);
		ok = code == 0;
		if (code > 0)
			problem = QString("No se pudo copiar el plugin a %1 (código %2).").arg(QDir::toNativeSeparators(root)).arg(code);
	}
#else
	// En macOS el binario cargado sigue funcionando aunque se reemplace el paquete.
	QString backup = root + ".old";
	QDir(backup).removeRecursively();
	if (QDir().rename(root, backup)) {
		ok = QDir().rename(newRoot, root) || copyTree(newRoot, root, &problem);
		if (ok) {
			QDir(backup).removeRecursively();
			// Por si acaso: sin la marca de "descargado de internet" macOS no lo bloquea.
			QProcess::execute("/usr/bin/xattr", {"-cr", root});
		} else {
			QDir(root).removeRecursively();
			QDir().rename(backup, root); // deja el plugin como estaba
		}
	} else {
		problem = QString("No se pudo mover %1").arg(root);
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
		fail(text("NowPlaying.Update.InstallFailed"), kReleasesPage, problem);
		return;
	}
	obs_log(LOG_INFO, "versión %s instalada; se usa al reiniciar OBS", version.toUtf8().constData());
	QMessageBox::information(mainWindow(), text("NowPlaying.Update.Title"), text("NowPlaying.Update.Done").arg(version));
}

void Updater::fail(const QString &message, const QString &pageUrl, const QString &detail)
{
	// El motivo va en el aviso y en el registro de OBS: sin él no se sabe qué falló.
	if (!detail.isEmpty())
		obs_log(LOG_WARNING, "actualización: %s", detail.toUtf8().constData());
	if (progress_) {
		progress_->close();
		progress_->deleteLater();
		progress_ = nullptr;
	}
	QMessageBox box(mainWindow());
	box.setWindowTitle(text("NowPlaying.Update.Title"));
	box.setIcon(QMessageBox::Warning);
	box.setText(message);
	if (!detail.isEmpty())
		box.setInformativeText(detail);
	QPushButton *open = box.addButton(text("NowPlaying.Update.OpenPage"), QMessageBox::AcceptRole);
	box.addButton(QMessageBox::Close);
	box.exec();
	if (box.clickedButton() == open)
		QDesktopServices::openUrl(QUrl(pageUrl));
}
