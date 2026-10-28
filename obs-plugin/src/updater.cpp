#include "updater.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>

#include <QCoreApplication>
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

const char *kLatestRelease = "https://api.github.com/repos/alejox/music-now-playing/releases/latest";
const char *kReleasesPage = "https://github.com/alejox/music-now-playing/releases/latest";

// Los paquetes del plugin se llaman music-now-playing-plugin-<versión>-<sistema>.<extensión>.
// Hasta la 1.0.33 se llamaban obs-now-playing-plugin-…: se aceptan los dos nombres y, si la
// Release trae ambos, se prefiere el nuevo.
#ifdef _WIN32
const char *kPlatform = "windows-x64";
const char *kPackage = "zip";
#elif defined(__APPLE__)
const char *kPlatform = "macos";
const char *kPackage = "zip";
#else
// En Linux el plugin no se actualiza solo (lo instaló un .deb o el usuario en su carpeta):
// solo se avisa y se abre la página de la Release.
const char *kPlatform = "linux-x86_64";
const char *kPackage = "(?:tar\\.gz|deb)";
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

#ifdef __APPLE__
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
#endif

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

// OBS 33 carga los plugins de C:\ProgramData\obs-studio\plugins\<nombre>\ (el .dll ahí y los
// datos en data\) y, hasta OBS 34, también los de las carpetas de antes: <OBS>\obs-plugins\64bit
// con <OBS>\data\obs-plugins\<nombre>, o …\plugins\<nombre>\bin\64bit. Si encuentra dos copias,
// usa la de la carpeta nueva. https://obsproject.com/kb/legacy-plugin-developer-info
#ifndef MAKE_SEMANTIC_VERSION
#define MAKE_SEMANTIC_VERSION(major, minor, patch) ((major << 24) | (minor << 16) | patch)
#endif

bool obsUsesNewPluginFolder()
{
	return obs_get_version() >= static_cast<uint32_t>(MAKE_SEMANTIC_VERSION(33, 0, 0));
}

// C:\ProgramData\obs-studio\plugins\obs-now-playing (vacío si no se sabe dónde está ProgramData).
QString newPluginFolder()
{
	QString programData = qEnvironmentVariable("ProgramData");
	if (programData.isEmpty())
		programData = qEnvironmentVariable("ALLUSERSPROFILE");
	if (programData.isEmpty())
		return QString();
	return QDir::cleanPath(programData + "/obs-studio/plugins/obs-now-playing");
}

// El .dll cargado, con ruta completa (en la carpeta de OBS llega relativa a bin\64bit).
QString moduleBinary()
{
	return QDir::cleanPath(
		QFileInfo(QString::fromUtf8(obs_get_module_binary_path(obs_current_module()))).absoluteFilePath());
}

enum class PluginLocation { NewFolder, LegacyObsFolder, LegacyProgramData, Other };

PluginLocation pluginLocation()
{
	QString binDir = QFileInfo(moduleBinary()).absolutePath();
	QString target = newPluginFolder();
	if (!target.isEmpty() && binDir.compare(target, Qt::CaseInsensitive) == 0)
		return PluginLocation::NewFolder;
	if (!target.isEmpty() && binDir.compare(target + "/bin/64bit", Qt::CaseInsensitive) == 0)
		return PluginLocation::LegacyProgramData;
	QDir dir(binDir);
	if (dir.dirName() == "64bit" && QFileInfo(dir.absolutePath()).dir().dirName() == "obs-plugins")
		return PluginLocation::LegacyObsFolder;
	return PluginLocation::Other;
}

// Copia el plugin a la carpeta de OBS 33 y después quita las copias de antes. El .dll cargado
// no se puede borrar, pero sí renombrar: queda como .old (se borra en la próxima
// actualización). Si la copia falla, no queda un .dll a medias en la carpeta nueva y la copia
// de antes sigue como estaba.
const char *kNewFolderScript = R"PS(param([string]$Dll, [string]$Target, [string]$Data = '', [string]$OldDll = '', [string]$OldData = '', [string]$OldBin = '')
$ErrorActionPreference = 'Stop'
$newDll = Join-Path $Target 'obs-now-playing.dll'
$backup = ''
$copying = $false
try {
  New-Item -ItemType Directory -Force -Path $Target | Out-Null
  Get-ChildItem -LiteralPath $Target -Filter 'obs-now-playing.dll.*.old' -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
  if (Test-Path -LiteralPath $newDll) {
    $name = "obs-now-playing.dll.$([DateTimeOffset]::Now.ToUnixTimeSeconds()).old"
    Rename-Item -LiteralPath $newDll -NewName $name
    $backup = Join-Path $Target $name
  }
  $copying = $true
  Copy-Item -LiteralPath $Dll -Destination $newDll -Force
  $pdb = [IO.Path]::ChangeExtension($Dll, '.pdb')
  if (Test-Path -LiteralPath $pdb) { Copy-Item -LiteralPath $pdb -Destination (Join-Path $Target 'obs-now-playing.pdb') -Force }
  if ($Data) {
    robocopy $Data (Join-Path $Target 'data') /E /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy $LASTEXITCODE" }
  }
} catch {
  if ($copying) { Remove-Item -LiteralPath $newDll -Force -ErrorAction SilentlyContinue }
  if ($backup) { Rename-Item -LiteralPath $backup -NewName 'obs-now-playing.dll' -ErrorAction SilentlyContinue }
  exit 1
}
if ($OldDll -and (Test-Path -LiteralPath $OldDll)) {
  Get-ChildItem -LiteralPath (Split-Path $OldDll) -Filter 'obs-now-playing.dll.*.old' -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
  Rename-Item -LiteralPath $OldDll -NewName "obs-now-playing.dll.$([DateTimeOffset]::Now.ToUnixTimeSeconds()).old" -ErrorAction SilentlyContinue
  Remove-Item -LiteralPath ([IO.Path]::ChangeExtension($OldDll, '.pdb')) -Force -ErrorAction SilentlyContinue
}
if ($OldData -and (Test-Path -LiteralPath $OldData)) { Remove-Item -LiteralPath $OldData -Recurse -Force -ErrorAction SilentlyContinue }
if ($OldBin -and (Test-Path -LiteralPath $OldBin)) { Remove-Item -LiteralPath $OldBin -Recurse -Force -ErrorAction SilentlyContinue }
exit 0
)PS";

// Instala dll y data (data vacío si ya está en su lugar) en la carpeta de OBS 33 con permiso de
// administrador y quita las copias de la carpeta de donde se cargó el plugin (from).
bool installToNewFolder(const QString &workDir, const QString &dll, const QString &data, PluginLocation from,
			QString *problem)
{
	QString target = newPluginFolder();
	if (target.isEmpty()) {
		*problem = QString("No se encontró la carpeta ProgramData.");
		return false;
	}
	QDir().mkpath(workDir);
	QString script = QDir(workDir).filePath("instalar-obs33.ps1");
	QFile scriptFile(script);
	if (!scriptFile.open(QIODevice::WriteOnly) || scriptFile.write(kNewFolderScript) < 0) {
		*problem = QString("No se pudo escribir %1").arg(QDir::toNativeSeparators(script));
		return false;
	}
	scriptFile.close();

	auto arg = [](const char *name, const QString &value) {
		return QString(" -%1 \"%2\"").arg(QString::fromUtf8(name), QDir::toNativeSeparators(value));
	};
	QString params = QString("-NoProfile -ExecutionPolicy Bypass -File \"%1\"").arg(QDir::toNativeSeparators(script));
	params += arg("Dll", dll) + arg("Target", target);
	if (!data.isEmpty())
		params += arg("Data", data);
	// Los datos de la carpeta de OBS no se borran mientras el plugin los usa (overlay y lector):
	// quedan hasta la próxima actualización, cuando el plugin ya corre desde la carpeta nueva.
	if (from == PluginLocation::LegacyObsFolder || from == PluginLocation::LegacyProgramData)
		params += arg("OldDll", moduleBinary());
	if (from == PluginLocation::NewFolder) {
		QString obsRoot = QDir::cleanPath(QCoreApplication::applicationDirPath() + "/../..");
		QString oldData = obsRoot + "/data/obs-plugins/obs-now-playing";
		if (QFileInfo(oldData).isDir())
			params += arg("OldData", oldData);
	}
	// La carpeta bin\ de antes (lo que quede de ella) está dentro de la carpeta nueva.
	if (QFileInfo(target + "/bin").isDir())
		params += arg("OldBin", target + "/bin");

	int code = runElevated("powershell.exe", params, problem);
	if (code > 0)
		*problem = QString("No se pudo copiar el plugin a %1 (código %2).").arg(QDir::toNativeSeparators(target)).arg(code);
	return code == 0;
}

QString movedFile()
{
	char *path = obs_module_config_path("moved-to-obs33-folder.txt");
	QString result = QString::fromUtf8(path);
	bfree(path);
	return result;
}

// Con OBS 33+ y el plugin en una carpeta de antes, ofrece moverlo a la nueva: una vez por
// sesión de OBS y, si ya se movió en esta versión, no otra vez. Devuelve true si se movió.
bool offerMoveToNewFolder()
{
	static bool offered = false;
	if (offered || !obsUsesNewPluginFolder())
		return false;
	PluginLocation where = pluginLocation();
	if (where != PluginLocation::LegacyObsFolder && where != PluginLocation::LegacyProgramData)
		return false;
	QString target = newPluginFolder();
	QFile moved(movedFile());
	if (target.isEmpty() ||
	    (moved.open(QIODevice::ReadOnly) && QString::fromUtf8(moved.readAll()).trimmed() == QString::fromUtf8(PLUGIN_VERSION)))
		return false;
	moved.close();
	offered = true;

	QMessageBox box(mainWindow());
	box.setWindowTitle(text("NowPlaying.Update.Title"));
	box.setIcon(QMessageBox::Information);
	box.setTextFormat(Qt::RichText);
	box.setText(text("NowPlaying.Update.Move").arg(QDir::toNativeSeparators(target).toHtmlEscaped()));
	QPushButton *move = box.addButton(text("NowPlaying.Update.MoveNow"), QMessageBox::AcceptRole);
	box.addButton(text("NowPlaying.Update.Later"), QMessageBox::RejectRole);
	box.setDefaultButton(move);
	box.exec();
	if (box.clickedButton() != move) {
		obs_log(LOG_INFO, "no se movió el plugin a la carpeta de OBS 33 (se vuelve a ofrecer al abrir OBS)");
		return false;
	}

	// Lo que se copia: el .dll cargado y su carpeta de datos (la que tiene overlay\index.html).
	char *index = obs_module_file("overlay/index.html");
	QString data = index ? QDir::cleanPath(QFileInfo(QString::fromUtf8(index)).absolutePath() + "/..") : QString();
	bfree(index);
	if (data.compare(target + "/data", Qt::CaseInsensitive) == 0)
		data.clear(); // ya está en su lugar (…\obs-now-playing\data)

	QString problem;
	bool ok = false;
	if (data.isEmpty() && where == PluginLocation::LegacyObsFolder)
		problem = QString("No se encontró la carpeta de datos del plugin.");
	else
		ok = installToNewFolder(QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
						.filePath("obs-now-playing-update"),
					moduleBinary(), data, where, &problem);
	if (!ok) {
		obs_log(LOG_WARNING, "no se pudo mover el plugin a %s: %s", target.toUtf8().constData(),
			problem.toUtf8().constData());
		QMessageBox warning(mainWindow());
		warning.setWindowTitle(text("NowPlaying.Update.Title"));
		warning.setIcon(QMessageBox::Warning);
		warning.setText(text("NowPlaying.Update.MoveFailed"));
		warning.setInformativeText(problem);
		warning.exec();
		return false;
	}

	QDir().mkpath(QFileInfo(movedFile()).absolutePath());
	QSaveFile flag(movedFile());
	if (flag.open(QIODevice::WriteOnly)) {
		flag.write(PLUGIN_VERSION);
		flag.commit();
	}
	obs_log(LOG_INFO, "plugin movido a %s; se usa al reiniciar OBS", target.toUtf8().constData());
	QMessageBox::information(mainWindow(), text("NowPlaying.Update.Title"), text("NowPlaying.Update.Moved"));
	return true;
}
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
#ifdef _WIN32
	// OBS 33+: primero el plugin a la carpeta nueva (si se movió, lo demás espera al reinicio).
	if (!manual && offerMoveToNewFolder())
		return;
#endif
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

	QRegularExpression pattern(QString("^(music|obs)-now-playing-plugin-(\\d+(?:\\.\\d+)*)-%1\\.%2$")
					   .arg(QString::fromUtf8(kPlatform), QString::fromUtf8(kPackage)));
	QString version;
	QString url;
	bool newName = false;
	const QJsonArray assets = release.value("assets").toArray();
	for (qsizetype i = 0; i < assets.size(); ++i) {
		QJsonObject asset = assets.at(i).toObject();
		QRegularExpressionMatch match = pattern.match(asset.value("name").toString());
		if (!match.hasMatch())
			continue;
		// Un paquete con el nombre viejo solo se usa si la Release no trae el nuevo.
		bool isNew = match.captured(1) == "music";
		if (newName && !isNew)
			continue;
		newName = newName || isNew;
		version = match.captured(2);
		url = asset.value("browser_download_url").toString();
	}

	if (version.isEmpty() || !url.startsWith("https://github.com/")) {
		if (manual)
			fail(text("NowPlaying.Update.CheckFailed"), pageUrl,
			     release.contains("message") ? "GitHub: " + release.value("message").toString()
#ifdef __linux__
							 : QString("La Release no trae music-now-playing-plugin-…-%1").arg(kPlatform));
#else
							 : QString("La Release no trae music-now-playing-plugin-…-%1.zip").arg(kPlatform));
#endif
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
#ifdef __linux__
	// En Linux no se instala solo: el botón abre la página para bajar el .deb o el .tar.gz.
	update->setText(text("NowPlaying.Update.OpenPage"));
#endif
	box.exec();

	if (box.clickedButton() == update) {
#ifdef __linux__
		Q_UNUSED(url)
		QDesktopServices::openUrl(QUrl(pageUrl));
#else
		download(version, url);
#endif
	} else if (box.clickedButton() == skip)
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
#elif defined(__APPLE__)
	int unzip = QProcess::execute(systemTool("ditto"), {"-x", "-k", zipPath, extracted});
	QString newRoot = QDir(extracted).filePath("obs-now-playing.plugin");
	bool unzipped = unzip == 0 && QFileInfo(newRoot).isDir();
#else
	// Linux: no se llega aquí (askToUpdate abre la página); por si acaso, no se toca nada.
	int unzip = -1;
	QString newRoot;
	bool unzipped = false;
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
	// En la carpeta de OBS 33, o en una de antes con OBS 33+: se instala en la carpeta nueva
	// (el zip trae la forma de la de antes) y se quitan las copias de antes.
	PluginLocation where = pluginLocation();
	bool toNewFolder = where == PluginLocation::NewFolder || (obsUsesNewPluginFolder() && where != PluginLocation::Other);
	ownFolder = ownFolder || toNewFolder;
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
	if (toNewFolder) {
		ok = installToNewFolder(workDir, QDir(newRoot).filePath("obs-plugins/64bit/obs-now-playing.dll"),
					QDir(newRoot).filePath("data/obs-plugins/obs-now-playing"), where, &problem);
	} else {
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
	}
#elif defined(__APPLE__)
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
#else
	(void)newRoot;
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
