#include "updater.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>

#include <QCoreApplication>
#include <QCryptographicHash>
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
#include <QTimer>
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

// Desde la 1.0.36 los paquetes del plugin se llaman music-now-playing-<versión>-<sistema>.zip y
// traen la carpeta que se copia tal cual en la de plugins de OBS. Los de antes
// (music-now-playing-plugin-…, obs-now-playing-plugin-…) traían otra forma: no se aceptan.
#ifdef _WIN32
const char *kPlatform = "windows-x64";
#elif defined(__APPLE__)
const char *kPlatform = "macos";
#else
// En Linux el plugin no se actualiza solo (el usuario lo copió en su carpeta de plugins):
// solo se avisa y se abre la página de la Release.
const char *kPlatform = "linux-x86_64";
#endif

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

QWidget *mainWindow()
{
	return static_cast<QMainWindow *>(obs_frontend_get_main_window());
}

#ifdef _WIN32
// C:\Windows\System32 según Windows (no según una variable de entorno, que el usuario puede cambiar).
QString systemDirectory()
{
	wchar_t buffer[MAX_PATH];
	UINT size = GetSystemDirectoryW(buffer, MAX_PATH);
	if (size == 0 || size >= MAX_PATH)
		return QString();
	return QString::fromWCharArray(buffer, int(size));
}
#endif

QString systemTool(const char *name)
{
#ifdef _WIN32
	// curl.exe y tar.exe vienen con Windows 10 (1803) y 11.
	return systemDirectory() + "\\" + name + ".exe";
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
// El script no se guarda en disco: va dentro de la línea de comandos (-EncodedCommand), así
// nadie puede cambiarlo mientras Windows pide el permiso. Lo que copia (que está en la carpeta
// temporal del usuario) primero lo pasa a una carpeta nueva dentro de la de destino, que el
// usuario no puede modificar, y ahí compara cada archivo con el SHA-256 que el plugin calculó
// antes de pedir el permiso ($Manifest: "ruta relativa|hash" por línea). Si algo no coincide
// (o sobra un archivo), no instala nada.
const char *kNewFolderScript = R"PS(param([string]$Dll, [string]$Target, [string]$Manifest, [string]$Data = '', [string]$Pdb = '', [string]$OldDll = '', [string]$OldData = '', [string]$OldBin = '')
$ErrorActionPreference = 'Stop'
$env:PSModulePath = "$PSHOME\Modules"
$robocopy = Join-Path ([Environment]::SystemDirectory) 'robocopy.exe'
$newDll = Join-Path $Target 'obs-now-playing.dll'
$stage = Join-Path $Target ".staging-$([guid]::NewGuid().ToString('N'))"
$backup = ''
$copying = $false
try {
  New-Item -ItemType Directory -Force -Path $Target | Out-Null
  New-Item -ItemType Directory -Path $stage | Out-Null
  Copy-Item -LiteralPath $Dll -Destination (Join-Path $stage 'obs-now-playing.dll')
  if ($Pdb) { Copy-Item -LiteralPath $Pdb -Destination (Join-Path $stage 'obs-now-playing.pdb') }
  if ($Data) {
    & $robocopy $Data (Join-Path $stage 'data') /E /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy $LASTEXITCODE" }
  }
  $expected = @($Manifest -split "`n" | Where-Object { $_ })
  $staged = @(Get-ChildItem -LiteralPath $stage -Recurse -File -Force)
  if ($expected.Count -eq 0 -or $staged.Count -ne $expected.Count) { throw 'manifest' }
  foreach ($entry in $expected) {
    $rel, $hash = $entry -split '\|', 2
    $file = Join-Path $stage $rel
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "missing $rel" }
    if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $hash) { throw "hash $rel" }
  }
  Get-ChildItem -LiteralPath $Target -Filter 'obs-now-playing.dll.*.old' -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
  if (Test-Path -LiteralPath $newDll) {
    $name = "obs-now-playing.dll.$([DateTimeOffset]::Now.ToUnixTimeSeconds()).old"
    Rename-Item -LiteralPath $newDll -NewName $name
    $backup = Join-Path $Target $name
  }
  $copying = $true
  Copy-Item -LiteralPath (Join-Path $stage 'obs-now-playing.dll') -Destination $newDll -Force
  if ($Pdb) { Copy-Item -LiteralPath (Join-Path $stage 'obs-now-playing.pdb') -Destination (Join-Path $Target 'obs-now-playing.pdb') -Force }
  if ($Data) {
    & $robocopy (Join-Path $stage 'data') (Join-Path $Target 'data') /E /IS /IT /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy $LASTEXITCODE" }
  }
} catch {
  if ($copying) { Remove-Item -LiteralPath $newDll -Force -ErrorAction SilentlyContinue }
  if ($backup) { Rename-Item -LiteralPath $backup -NewName 'obs-now-playing.dll' -ErrorAction SilentlyContinue }
  Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
  exit 1
}
Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
if ($OldDll -and (Test-Path -LiteralPath $OldDll)) {
  Get-ChildItem -LiteralPath (Split-Path $OldDll) -Filter 'obs-now-playing.dll.*.old' -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
  Rename-Item -LiteralPath $OldDll -NewName "obs-now-playing.dll.$([DateTimeOffset]::Now.ToUnixTimeSeconds()).old" -ErrorAction SilentlyContinue
  Remove-Item -LiteralPath ([IO.Path]::ChangeExtension($OldDll, '.pdb')) -Force -ErrorAction SilentlyContinue
}
if ($OldData -and (Test-Path -LiteralPath $OldData)) { Remove-Item -LiteralPath $OldData -Recurse -Force -ErrorAction SilentlyContinue }
if ($OldBin -and (Test-Path -LiteralPath $OldBin)) { Remove-Item -LiteralPath $OldBin -Recurse -Force -ErrorAction SilentlyContinue }
exit 0
)PS";

// Un texto como literal de PowerShell entre comillas simples. PowerShell también toma como
// comilla simple las tipográficas (‘ ’ ‚ ‛): se duplican todas.
QString psLiteral(const QString &value)
{
	QString quoted("'");
	for (QChar c : value) {
		quoted += c;
		if (c == u'\'' || c == QChar(0x2018) || c == QChar(0x2019) || c == QChar(0x201A) || c == QChar(0x201B))
			quoted += c;
	}
	return quoted + "'";
}

// SHA-256 de un archivo en hexadecimal (vacío si no se pudo leer).
QString sha256Of(const QString &path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return QString();
	QCryptographicHash hash(QCryptographicHash::Sha256);
	if (!hash.addData(&file))
		return QString();
	return QString::fromLatin1(hash.result().toHex().toUpper());
}

bool addToManifest(QStringList *manifest, const QString &relative, const QString &file, QString *problem)
{
	QString hash = sha256Of(file);
	if (hash.isEmpty()) {
		*problem = text("NowPlaying.Update.Error.ReadFailed").arg(QDir::toNativeSeparators(file));
		return false;
	}
	*manifest << relative + "|" + hash;
	return true;
}

// Instala dll y data (data vacío si ya está en su lugar) en la carpeta de OBS 33 con permiso de
// administrador y quita las copias de la carpeta de donde se cargó el plugin (from).
bool installToNewFolder(const QString &dll, const QString &data, PluginLocation from, QString *problem)
{
	QString target = newPluginFolder();
	QString system = systemDirectory();
	if (target.isEmpty()) {
		*problem = text("NowPlaying.Update.Error.NoProgramData");
		return false;
	}
	if (system.isEmpty()) {
		*problem = text("NowPlaying.Update.Error.NoSystemFolder");
		return false;
	}

	// Lo que se va a copiar y su SHA-256, antes de pedir el permiso.
	QStringList manifest;
	QString pdb = QDir::cleanPath(QFileInfo(dll).absolutePath() + "/" + QFileInfo(dll).completeBaseName() + ".pdb");
	if (!QFileInfo(pdb).isFile())
		pdb.clear();
	if (!addToManifest(&manifest, "obs-now-playing.dll", dll, problem) ||
	    (!pdb.isEmpty() && !addToManifest(&manifest, "obs-now-playing.pdb", pdb, problem)))
		return false;
	if (!data.isEmpty()) {
		QDir dataDir(data);
		QDirIterator it(data, QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
				QDirIterator::Subdirectories);
		while (it.hasNext()) {
			QString file = it.next();
			if (!addToManifest(&manifest, "data\\" + QDir::toNativeSeparators(dataDir.relativeFilePath(file)),
					   file, problem))
				return false;
		}
	}

	auto arg = [](const char *name, const QString &value) {
		return QString(" -%1 %2").arg(QString::fromUtf8(name), psLiteral(QDir::toNativeSeparators(value)));
	};
	QString command = QString("& {\n%1\n}").arg(QString::fromUtf8(kNewFolderScript));
	command += arg("Dll", dll) + arg("Target", target) + " -Manifest " + psLiteral(manifest.join('\n'));
	if (!data.isEmpty())
		command += arg("Data", data);
	if (!pdb.isEmpty())
		command += arg("Pdb", pdb);
	// Los datos de la carpeta de OBS no se borran mientras el plugin los usa (overlay y lector):
	// quedan hasta la próxima actualización, cuando el plugin ya corre desde la carpeta nueva.
	if (from == PluginLocation::LegacyObsFolder || from == PluginLocation::LegacyProgramData)
		command += arg("OldDll", moduleBinary());
	if (from == PluginLocation::NewFolder) {
		QString obsRoot = QDir::cleanPath(QCoreApplication::applicationDirPath() + "/../..");
		QString oldData = obsRoot + "/data/obs-plugins/obs-now-playing";
		if (QFileInfo(oldData).isDir())
			command += arg("OldData", oldData);
	}
	// La carpeta bin\ de antes (lo que quede de ella) está dentro de la carpeta nueva.
	if (QFileInfo(target + "/bin").isDir())
		command += arg("OldBin", target + "/bin");

	// -EncodedCommand: el comando en UTF-16LE y base64.
	QByteArray utf16;
	utf16.reserve(command.size() * 2);
	for (QChar c : command) {
		utf16.append(char(c.unicode() & 0xff));
		utf16.append(char(c.unicode() >> 8));
	}
	QString params = "-NoLogo -NoProfile -NonInteractive -EncodedCommand " + QString::fromLatin1(utf16.toBase64());
	// ShellExecuteEx no acepta una línea de comandos de más de 32767 caracteres.
	if (params.size() > 32000) {
		*problem = text("NowPlaying.Update.Error.CommandTooLong").arg(params.size());
		return false;
	}

	int code = runElevated(system + "\\WindowsPowerShell\\v1.0\\powershell.exe", params, problem);
	if (code > 0)
		*problem = text("NowPlaying.Update.Error.CopyFailed").arg(QDir::toNativeSeparators(target)).arg(code);
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
	QPushButton *later = box.addButton(text("NowPlaying.Update.Later"), QMessageBox::RejectRole);
	box.setDefaultButton(later);
	box.setEscapeButton(later);
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
		ok = installToNewFolder(moduleBinary(), data, where, &problem);
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

bool Updater::live()
{
	return obs_frontend_streaming_active() || obs_frontend_recording_active() ||
	       obs_frontend_replay_buffer_active() || obs_frontend_virtualcam_active();
}

void Updater::scheduleAutomaticCheck(int delayMs)
{
	QTimer::singleShot(delayMs, this, [this] { check(false); });
}

// Se espera a los eventos de OBS (las salidas al detenerse) en vez de revisar cada tanto:
// no hay sondeo y, como al dispararse se vuelve a preguntar si queda algo en vivo, una salida
// que sigue (o que arrancó en el medio) solo pospone el aviso hasta que también pare.
void Updater::outputsStopped()
{
	if (exiting_ || (!checkPending_ && pendingVersion_.isEmpty()))
		return;
	// Unos segundos de margen: al cerrar una transmisión suele parar también la grabación.
	QTimer::singleShot(3000, this, [this] {
		if (exiting_ || busy_ || live())
			return;
		if (!pendingVersion_.isEmpty()) {
			if (pendingVersion_ != skippedVersion())
				askToUpdate(pendingVersion_, pendingUrl_, pendingPage_);
			pendingVersion_.clear();
		} else if (checkPending_) {
			checkPending_ = false;
			check(false);
		}
	});
}

void Updater::shutdown()
{
	exiting_ = true;
	checkPending_ = false;
	pendingVersion_.clear();
}

void Updater::check(bool manual)
{
	if (busy_ || exiting_)
		return;
	// Nada automático en vivo (ni siquiera ofrecer mover la carpeta en Windows).
	if (!manual && live()) {
		checkPending_ = true;
		obs_log(LOG_INFO, "en vivo: la revisión de actualizaciones espera a que termine");
		return;
	}
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
		if (exiting_)
			return;
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

	QRegularExpression pattern(
		QString("^music-now-playing-(\\d+(?:\\.\\d+)*)-%1\\.zip$").arg(QString::fromUtf8(kPlatform)));
	QString version;
	QString url;
	const QJsonArray assets = release.value("assets").toArray();
	for (qsizetype i = 0; i < assets.size(); ++i) {
		QJsonObject asset = assets.at(i).toObject();
		QRegularExpressionMatch match = pattern.match(asset.value("name").toString());
		if (!match.hasMatch())
			continue;
		version = match.captured(1);
		url = asset.value("browser_download_url").toString();
		break;
	}

	if (version.isEmpty() || !url.startsWith("https://github.com/")) {
		if (manual)
			fail(text("NowPlaying.Update.CheckFailed"), pageUrl,
			     release.contains("message") ? "GitHub: " + release.value("message").toString()
							 : QString("La Release no trae music-now-playing-…-%1.zip").arg(kPlatform));
		return;
	}
	if (compareVersions(version, QString::fromUtf8(PLUGIN_VERSION)) <= 0) {
		if (manual)
			QMessageBox::information(mainWindow(), text("NowPlaying.Update.Title"),
						 text("NowPlaying.Update.UpToDate").arg(QString::fromUtf8(PLUGIN_VERSION)));
		return;
	}
#ifdef _WIN32
	// El zip trae la carpeta de OBS 33 (obs-now-playing\obs-now-playing.dll y data\), que OBS 31
	// y 32 no cargan: con esas versiones no se ofrece; en la revisión manual se explica por qué.
	if (!obsUsesNewPluginFolder()) {
		obs_log(LOG_INFO, "hay una versión nueva (%s), pero necesita OBS 33 o superior", version.toUtf8().constData());
		if (manual)
			QMessageBox::information(mainWindow(), text("NowPlaying.Update.Title"),
						 text("NowPlaying.Update.NeedsObs33").arg(version, QString::fromUtf8(PLUGIN_VERSION)));
		return;
	}
#endif
	if (!manual && version == skippedVersion())
		return;
	// Si empezaste a transmitir o grabar mientras se revisaba, el aviso espera.
	if (!manual && live()) {
		pendingVersion_ = version;
		pendingUrl_ = url;
		pendingPage_ = pageUrl;
		obs_log(LOG_INFO, "hay una versión nueva (%s): se avisa cuando no estés en vivo", version.toUtf8().constData());
		return;
	}
	askToUpdate(version, url, pageUrl);
}

void Updater::askToUpdate(const QString &version, const QString &url, const QString &pageUrl)
{
	pendingVersion_.clear();
	QMessageBox box(mainWindow());
	box.setWindowTitle(text("NowPlaying.Update.Title"));
	box.setIcon(QMessageBox::Information);
	box.setTextFormat(Qt::RichText);
	box.setTextInteractionFlags(Qt::TextBrowserInteraction);
	box.setText(text("NowPlaying.Update.Available")
			    .arg(version, QString::fromUtf8(PLUGIN_VERSION), pageUrl.toHtmlEscaped()));
	QPushButton *update = box.addButton(text("NowPlaying.Update.Now"), QMessageBox::AcceptRole);
	QPushButton *later = box.addButton(text("NowPlaying.Update.Later"), QMessageBox::RejectRole);
	QPushButton *skip = box.addButton(text("NowPlaying.Update.Skip"), QMessageBox::DestructiveRole);
	// Un Enter o un Esc sin querer no instala nada.
	box.setDefaultButton(later);
	box.setEscapeButton(later);
#ifdef __linux__
	// En Linux no se instala solo: el botón abre la página para bajar el zip.
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
	// El zip trae la carpeta de OBS 33: obs-now-playing\obs-now-playing.dll y sus datos en
	// obs-now-playing\data (se copia tal cual en C:\ProgramData\obs-studio\plugins).
	QString newRoot = QDir(extracted).filePath("obs-now-playing");
	bool unzipped = unzip == 0 && QFileInfo::exists(QDir(newRoot).filePath("obs-now-playing.dll")) &&
			QFileInfo::exists(QDir(newRoot).filePath("data/overlay/index.html"));
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
	// propia carpeta; en Windows (OBS 33+) en la carpeta nueva o en una de las de antes, y
	// siempre se instala en la nueva (se quitan las copias de antes).
#ifdef _WIN32
	PluginLocation where = pluginLocation();
	bool ownFolder = obsUsesNewPluginFolder() && where != PluginLocation::Other;
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
	ok = installToNewFolder(QDir(newRoot).filePath("obs-now-playing.dll"), QDir(newRoot).filePath("data"), where,
				&problem);
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
