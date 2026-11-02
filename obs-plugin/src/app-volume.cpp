#include "app-volume.hpp"

#ifdef _WIN32

#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <functional>

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

// Llama a visit con cada sesión de audio de esa app (en cada salida activa).
void forEachSession(const QString &exe, const std::function<void(ISimpleAudioVolume *)> &visit)
{
	if (exe.isEmpty())
		return;
	// Qt ya inició COM en el hilo de la interfaz; si no, se inicia aquí.
	HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	{
		ComPtr<IMMDeviceEnumerator> enumerator;
		ComPtr<IMMDeviceCollection> devices;
		UINT deviceCount = 0;
		if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
					       IID_PPV_ARGS(&enumerator))) &&
		    SUCCEEDED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices)))
			devices->GetCount(&deviceCount);
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
				DWORD pid = 0;
				if (FAILED(sessions->GetSession(i, &control)) || FAILED(control.As(&control2)) ||
				    FAILED(control2->GetProcessId(&pid)) || pid == 0)
					continue;
				if (processExe(pid).compare(exe, Qt::CaseInsensitive) != 0)
					continue;
				if (SUCCEEDED(control.As(&volume)))
					visit(volume.Get());
			}
		}
	}
	if (SUCCEEDED(init))
		CoUninitialize();
}

} // namespace

int app_volume(const QString &exe)
{
	float level = -1.0f;
	forEachSession(exe, [&level](ISimpleAudioVolume *volume) {
		float value = 0.0f;
		BOOL muted = FALSE;
		if (SUCCEEDED(volume->GetMasterVolume(&value)) && SUCCEEDED(volume->GetMute(&muted)))
			level = std::max(level, muted ? 0.0f : value);
	});
	return level < 0.0f ? -1 : int(std::lround(level * 100.0f));
}

bool set_app_volume(const QString &exe, int percent)
{
	const float value = std::clamp(percent, 0, 100) / 100.0f;
	bool done = false;
	forEachSession(exe, [&](ISimpleAudioVolume *volume) {
		volume->SetMute(FALSE, nullptr);
		done = SUCCEEDED(volume->SetMasterVolume(value, nullptr)) || done;
	});
	return done;
}

#else

int app_volume(const QString &)
{
	return -1;
}

bool set_app_volume(const QString &, int)
{
	return false;
}

#endif
