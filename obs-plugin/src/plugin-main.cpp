/*
Music Now Playing
Copyright (C) 2026 Alejandro (Alejoxgaming)

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>

#include <QMainWindow>
#include <QTimer>

#include "app-audio-capture.hpp"
#include "app-volume.hpp"
#include "media-watcher.hpp"
#include "music-channel.hpp"
#include "now-playing-dock.hpp"
#include "now-playing-source.hpp"
#include "updater.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

namespace {

void on_frontend_event(enum obs_frontend_event event, void *)
{
	// Al cerrar OBS, el lector se apaga antes de que Qt desarme la interfaz, el actualizador
	// deja de avisar y este callback se quita aquí (después de EXIT la interfaz ya no existe).
	// Quitarlo dentro del callback es seguro: OBS los recorre de atrás hacia adelante.
	if (event == OBS_FRONTEND_EVENT_EXIT) {
		// Primero el canal de música: guarda su estado y suelta el canal de salida.
		music_channel_shutdown();
		MediaWatcher::instance().stop();
		// Windows: el volumen que el plugin cambió en el Mezclador vuelve a como estaba.
		app_volume_shutdown();
		Updater::instance().shutdown();
		obs_frontend_remove_event_callback(on_frontend_event, nullptr);
		return;
	}
	// El aviso automático de actualización espera a que no estés en vivo.
	if (event == OBS_FRONTEND_EVENT_STREAMING_STOPPED || event == OBS_FRONTEND_EVENT_RECORDING_STOPPED ||
	    event == OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPED || event == OBS_FRONTEND_EVENT_VIRTUALCAM_STOPPED)
		Updater::instance().outputsStopped();
	// El canal de música vive en un canal de salida (como la voz de Multichat): se suelta antes
	// de que OBS vacíe la colección (al cambiarla o al salir) y se vuelve a poner con la nueva.
	if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING || event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP)
		music_channel_collection_unloading();
	if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED)
		music_channel_collection_loaded();
	// Con OBS ya abierto, revisa si hay una versión nueva (sin apurar el arranque).
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
		music_channel_collection_loaded();
		Updater::instance().scheduleAutomaticCheck(5000);
		// Con las escenas ya cargadas, el canal de música sigue a la app que suena. Una
		// sola vez: Qt::UniqueConnection no sirve con una función suelta (con el Qt de
		// OBS 32 en Mac la conexión falla y el canal nunca seguía a la música).
		static bool following = false;
		if (!following)
			following = static_cast<bool>(QObject::connect(&MediaWatcher::instance(), &MediaWatcher::updated,
								       &MediaWatcher::instance(), &follow_music_channel));
	}
}

} // namespace

bool obs_module_load(void)
{
	register_now_playing_source();
#ifdef __APPLE__
	// La captura de audio propia del canal de música (macOS 13+; no aparece en "Agregar fuente").
	register_app_audio_capture();
#endif
	MediaWatcher::instance().start();

	auto *main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	auto *dock = new NowPlayingDock(main_window);
	obs_frontend_add_dock_by_id("obs_now_playing_dock", obs_module_text("NowPlaying.Dock"), dock);
	obs_frontend_add_event_callback(on_frontend_event, nullptr);
	obs_frontend_add_tools_menu_item(
		obs_module_text("NowPlaying.Update.MenuItem"), [](void *) { Updater::instance().check(true); }, nullptr);
	// "Canal de música" en Herramientas, junto a la entrada de arriba.
	music_channel_init();
	Updater::cleanupPreviousUpdate();

	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	// El callback de eventos ya se quitó al recibir OBS_FRONTEND_EVENT_EXIT.
	MediaWatcher::instance().stop();
	app_volume_shutdown();
	obs_log(LOG_INFO, "plugin unloaded");
}
