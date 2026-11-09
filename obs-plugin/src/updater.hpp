#pragma once

#include <QObject>
#include <QString>

class QProcess;
class QProgressDialog;

// Actualizaciones del plugin desde las Releases de GitHub.
//
// Al abrir OBS (y desde Herramientas → "Now Playing: buscar actualizaciones") revisa la
// última Release; si trae un plugin más nuevo para este sistema, pregunta con un modal.
// Si se acepta, lo instala solo: descarga el zip con el curl del sistema, comprueba su firma
// Ed25519 (<zip>.sig, ver release-signature.hpp; sin firma válida no instala nada), lo descomprime
// (ditto en macOS, tar en Windows) y reemplaza el plugin. El cambio se aplica al reiniciar OBS.
// En Windows se instala en C:\ProgramData\obs-studio\plugins\obs-now-playing (la carpeta de
// OBS 33, la forma del zip) con permiso de administrador; si el plugin está en una carpeta de
// antes, ofrece moverlo ahí. Con OBS 31 o 32 no se ofrece: esas versiones no cargan esa forma.
// En Linux no se instala solo: el aviso abre la página de la Release.
class Updater : public QObject {
	Q_OBJECT

public:
	static Updater &instance();

	// manual: también avisa si ya está al día o si falla la revisión. La revisión manual
	// (Herramientas) avisa aunque estés en vivo: la pediste tú.
	void check(bool manual);

	// La revisión automática al abrir OBS, dentro de delayMs. Su aviso nunca aparece mientras
	// transmites, grabas, usas el búfer de repetición o la cámara virtual: espera a que paren.
	void scheduleAutomaticCheck(int delayMs);
	// OBS avisó que se detuvo una salida: si ya no queda ninguna en vivo, sigue la revisión o
	// el aviso que estaba esperando.
	void outputsStopped();
	// OBS se cierra: no se revisa ni se avisa más (ningún temporizador pendiente hace nada).
	void shutdown();

	// Limpia lo que haya quedado de una actualización anterior (la descarga temporal; en
	// Windows los .dll viejos los borra la actualización siguiente, que tiene el permiso).
	static void cleanupPreviousUpdate();

	// "1.0.10" > "1.0.9". Pública para poder probarla.
	static int compareVersions(const QString &a, const QString &b);

private:
	Updater() = default;
	void handleRelease(const QByteArray &json, bool manual);
	void askToUpdate(const QString &version, const QString &url, const QString &sigUrl, const QString &pageUrl);
	void download(const QString &version, const QString &url, const QString &sigUrl, const QString &pageUrl);
	void install(const QString &version, const QString &zipPath);
	// unzip: el código de salida de tar/ditto (-1 si no se pudo ejecutar).
	void installExtracted(const QString &version, const QString &zipPath, const QString &extracted, int unzip);
	void finishInstall(const QString &version, const QString &root, bool ok, bool stillRunning,
			   const QString &problem);
	void closeProgress();
	void fail(const QString &message, const QString &pageUrl, const QString &detail = QString());

	// Transmisión, grabación, búfer de repetición o cámara virtual activos.
	static bool live();

	bool busy_ = false; // revisando, o descargando e instalando
	bool exiting_ = false;
	bool checkPending_ = false; // la revisión automática espera a que no estés en vivo
	// El aviso automático que espera a que no estés en vivo (vacío: ninguno).
	QString pendingVersion_;
	QString pendingUrl_;
	QString pendingSigUrl_;
	QString pendingPage_;
	QProgressDialog *progress_ = nullptr;
};
