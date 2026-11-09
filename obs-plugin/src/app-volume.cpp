#include "app-volume.hpp"

#include <QMetaObject>
#include <QObject>

#ifdef _WIN32

#include <obs-module.h>
#include <plugin-support.h>

#include <QFileInfo>
#include <QHash>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

// Sin las macros min / max de windows.h (chocan con std::max).
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace {

QString processExe(DWORD pid)
{
	HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (!process)
		return QString();
	wchar_t path[MAX_PATH];
	DWORD size = MAX_PATH;
	QString exe;
	if (QueryFullProcessImageNameW(process, 0, path, &size))
		exe = QFileInfo(QString::fromWCharArray(path, int(size))).fileName();
	CloseHandle(process);
	return exe;
}

// Una sesión de audio viva: su identificador de instancia (único por proceso y sesión, no
// se repite si la app se cierra y se vuelve a abrir), su proceso y su volumen.
struct Session {
	QString id;
	DWORD pid = 0;
	ISimpleAudioVolume *volume = nullptr;
};

// Llama a visit con cada sesión de audio viva (en cada salida activa). Solo en el hilo del
// volumen, que ya inició COM.
template<typename Visit> void forEachSession(Visit visit)
{
	ComPtr<IMMDeviceEnumerator> enumerator;
	ComPtr<IMMDeviceCollection> devices;
	UINT deviceCount = 0;
	if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))) ||
	    FAILED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices)) ||
	    FAILED(devices->GetCount(&deviceCount)))
		return;
	for (UINT d = 0; d < deviceCount; d++) {
		ComPtr<IMMDevice> device;
		ComPtr<IAudioSessionManager2> manager;
		ComPtr<IAudioSessionEnumerator> sessions;
		int count = 0;
		if (FAILED(devices->Item(d, &device)) ||
		    FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
					    reinterpret_cast<void **>(manager.GetAddressOf()))) ||
		    FAILED(manager->GetSessionEnumerator(&sessions)) || FAILED(sessions->GetCount(&count)))
			continue;
		for (int i = 0; i < count; i++) {
			ComPtr<IAudioSessionControl> control;
			ComPtr<IAudioSessionControl2> control2;
			ComPtr<ISimpleAudioVolume> volume;
			AudioSessionState state = AudioSessionStateInactive;
			Session session;
			LPWSTR id = nullptr;
			if (FAILED(sessions->GetSession(i, &control)) || FAILED(control.As(&control2)) ||
			    FAILED(control2->GetProcessId(&session.pid)) || session.pid == 0 ||
			    FAILED(control->GetState(&state)) || state == AudioSessionStateExpired ||
			    FAILED(control.As(&volume)))
				continue;
			if (SUCCEEDED(control2->GetSessionInstanceIdentifier(&id)) && id) {
				session.id = QString::fromWCharArray(id);
				CoTaskMemFree(id);
			}
			session.volume = volume.Get();
			visit(session);
		}
	}
}

// Las sesiones de esa app. El volumen es por proceso: si la música suena en una pestaña del
// navegador, cambiarlo cambia todo el navegador (también una llamada en otra pestaña). Windows
// no da un volumen por pestaña, así que no hay forma de evitarlo.
template<typename Visit> void forEachAppSession(const QString &exe, Visit visit)
{
	if (exe.isEmpty())
		return;
	QHash<DWORD, bool> matches; // una app suele tener varias sesiones: cada proceso se mira una vez
	forEachSession([&](const Session &session) {
		auto known = matches.find(session.pid);
		if (known == matches.end())
			known = matches.insert(session.pid,
					       processExe(session.pid).compare(exe, Qt::CaseInsensitive) == 0);
		if (known.value())
			visit(session);
	});
}

// El nivel de cada sesión antes de que el plugin la tocara por primera vez, y el último que le
// puso el plugin (si ahora tiene otro, alguien la movió a mano y no se le devuelve el viejo).
struct Original {
	float level = 1.0f;
	float lastSet = 1.0f;
};

class VolumeThread {
public:
	static VolumeThread &instance()
	{
		// Nunca se destruye: el hilo se cierra en shutdown(), no al descargar el plugin.
		static VolumeThread *thread = new VolumeThread();
		return *thread;
	}

	void post(std::function<void()> job)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (stopping_)
			return;
		if (!thread_.joinable())
			thread_ = std::thread([this] { run(); });
		jobs_.push_back(std::move(job));
		wake_.notify_one();
	}

	void shutdown()
	{
		std::unique_lock<std::mutex> lock(mutex_);
		if (stopping_)
			return;
		stopping_ = true;
		if (!thread_.joinable())
			return;
		jobs_.push_back([this] { restore(); });
		wake_.notify_one();
		const bool done = finishedCv_.wait_for(lock, std::chrono::seconds(2), [this] { return finished_; });
		lock.unlock();
		if (done) {
			thread_.join();
		} else {
			// Windows no contestó a tiempo: OBS no espera más por el volumen.
			obs_log(LOG_WARNING, "el volumen de las apps no se terminó de restaurar al cerrar");
			thread_.detach();
		}
	}

	int read(const QString &exe)
	{
		float level = -1.0f;
		forEachAppSession(exe, [&level](const Session &session) {
			float value = 0.0f;
			BOOL muted = FALSE;
			if (SUCCEEDED(session.volume->GetMasterVolume(&value)) &&
			    SUCCEEDED(session.volume->GetMute(&muted)))
				level = std::max(level, muted ? 0.0f : value);
		});
		return level < 0.0f ? -1 : int(std::lround(level * 100.0f));
	}

	bool set(const QString &exe, int percent)
	{
		const float value = std::clamp(percent, 0, 100) / 100.0f;
		bool done = false;
		forEachAppSession(exe, [&](const Session &session) {
			// El silencio de Windows no se toca: si la app está silenciada, lo sigue estando.
			if (!session.id.isEmpty()) {
				auto it = originals_.find(session.id);
				if (it == originals_.end()) {
					Original original;
					if (FAILED(session.volume->GetMasterVolume(&original.level)))
						return;
					it = originals_.insert(session.id, original);
				}
				it->lastSet = value;
			}
			done = SUCCEEDED(session.volume->SetMasterVolume(value, nullptr)) || done;
		});
		return done;
	}

private:
	VolumeThread() = default;

	void run()
	{
		// Un hilo propio con COM propio (multihilo): no depende de cómo lo inició Qt.
		const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		for (;;) {
			std::function<void()> job;
			{
				std::unique_lock<std::mutex> lock(mutex_);
				wake_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
				if (jobs_.empty())
					break;
				job = std::move(jobs_.front());
				jobs_.pop_front();
			}
			job();
		}
		if (SUCCEEDED(init))
			CoUninitialize();
		std::lock_guard<std::mutex> lock(mutex_);
		finished_ = true;
		finishedCv_.notify_all();
	}

	// Solo las sesiones que siguen vivas (las de una app cerrada ya no aparecen).
	void restore()
	{
		int restored = 0;
		forEachSession([&](const Session &session) {
			auto it = originals_.constFind(session.id);
			if (session.id.isEmpty() || it == originals_.constEnd())
				return;
			float now = 0.0f;
			if (SUCCEEDED(session.volume->GetMasterVolume(&now)) && std::fabs(now - it->lastSet) < 0.01f &&
			    SUCCEEDED(session.volume->SetMasterVolume(it->level, nullptr)))
				restored++;
		});
		originals_.clear();
		if (restored > 0)
			obs_log(LOG_INFO, "volumen de %d sesiones de audio devuelto a como estaba", restored);
	}

	std::mutex mutex_;
	std::condition_variable wake_;
	std::condition_variable finishedCv_;
	std::deque<std::function<void()>> jobs_;
	std::thread thread_;
	bool stopping_ = false;
	bool finished_ = false;
	QHash<QString, Original> originals_; // solo en el hilo del volumen
};

} // namespace

void app_volume_read(const QString &exe, QObject *context, std::function<void(int)> done)
{
	VolumeThread::instance().post([exe, context, done = std::move(done)] {
		const int level = VolumeThread::instance().read(exe);
		QMetaObject::invokeMethod(context, [done, level] { done(level); }, Qt::QueuedConnection);
	});
}

void app_volume_set(const QString &exe, int percent, QObject *context, std::function<void(bool)> done)
{
	VolumeThread::instance().post([exe, percent, context, done = std::move(done)] {
		const bool ok = VolumeThread::instance().set(exe, percent);
		QMetaObject::invokeMethod(context, [done, ok] { done(ok); }, Qt::QueuedConnection);
	});
}

void app_volume_shutdown()
{
	VolumeThread::instance().shutdown();
}

#else

// macOS y Linux no usan el Mezclador de Windows.
void app_volume_read(const QString &, QObject *context, std::function<void(int)> done)
{
	QMetaObject::invokeMethod(context, [done = std::move(done)] { done(-1); }, Qt::QueuedConnection);
}

void app_volume_set(const QString &, int, QObject *context, std::function<void(bool)> done)
{
	QMetaObject::invokeMethod(context, [done = std::move(done)] { done(false); }, Qt::QueuedConnection);
}

void app_volume_shutdown() {}

#endif
