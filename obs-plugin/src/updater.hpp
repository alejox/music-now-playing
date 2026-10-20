#pragma once

#include <QObject>
#include <QString>

class QProcess;
class QProgressDialog;

// Actualizaciones del plugin desde las Releases de GitHub.
//
// Al abrir OBS (y desde Herramientas → "Now Playing: buscar actualizaciones") revisa la
// última Release; si trae un plugin más nuevo para este sistema, pregunta con un modal y
// lo instala solo: descarga el zip con el curl del sistema, lo descomprime (ditto en
// macOS, tar en Windows) y reemplaza el plugin. El cambio se aplica al reiniciar OBS.
class Updater : public QObject {
	Q_OBJECT

public:
	static Updater &instance();

	// manual: también avisa si ya está al día o si falla la revisión.
	void check(bool manual);

	// Limpia lo que haya quedado de una actualización anterior (archivos .old en Windows).
	static void cleanupPreviousUpdate();

	// "1.0.10" > "1.0.9". Pública para poder probarla.
	static int compareVersions(const QString &a, const QString &b);

private:
	Updater() = default;
	void handleRelease(const QByteArray &json, bool manual);
	void askToUpdate(const QString &version, const QString &url, const QString &pageUrl);
	void download(const QString &version, const QString &url);
	void install(const QString &version, const QString &zipPath);
	void fail(const QString &message, const QString &pageUrl);

	bool busy_ = false;
	QProgressDialog *progress_ = nullptr;
};
