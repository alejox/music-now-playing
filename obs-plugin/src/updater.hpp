#pragma once

#include <QObject>
#include <QString>

class QProcess;
class QProgressDialog;

// Actualizaciones del plugin desde las Releases de GitHub.
//
// Al abrir OBS (y desde Herramientas → "Now Playing: buscar actualizaciones") revisa la
// última Release; si trae un plugin más nuevo para este sistema, pregunta con un modal.
// Si se acepta, lo instala solo: descarga el zip con el curl del sistema, lo descomprime
// (ditto en macOS, tar en Windows) y reemplaza el plugin. En Windows el plugin está en la
// carpeta de OBS: la copia pide permiso de administrador. El cambio se aplica al reiniciar OBS.
// Con OBS 33+ en Windows va en C:\ProgramData\obs-studio\plugins\obs-now-playing: si el plugin
// está en una carpeta de antes, ofrece moverlo ahí, y las actualizaciones se instalan ahí.
class Updater : public QObject {
	Q_OBJECT

public:
	static Updater &instance();

	// manual: también avisa si ya está al día o si falla la revisión.
	void check(bool manual);

	// Limpia lo que haya quedado de una actualización anterior (la descarga temporal; en
	// Windows los .dll viejos los borra la actualización siguiente, que tiene el permiso).
	static void cleanupPreviousUpdate();

	// "1.0.10" > "1.0.9". Pública para poder probarla.
	static int compareVersions(const QString &a, const QString &b);

private:
	Updater() = default;
	void handleRelease(const QByteArray &json, bool manual);
	void askToUpdate(const QString &version, const QString &url, const QString &pageUrl);
	void download(const QString &version, const QString &url);
	void install(const QString &version, const QString &zipPath);
	void fail(const QString &message, const QString &pageUrl, const QString &detail = QString());

	bool busy_ = false;
	QProgressDialog *progress_ = nullptr;
};
