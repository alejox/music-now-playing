/*
OBS Now Playing
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
	// Al cerrar OBS, el lector se apaga antes de que Qt desarme la interfaz.
	if (event == OBS_FRONTEND_EVENT_EXIT)
		MediaWatcher::instance().stop();
	// Con OBS ya abierto, revisa si hay una versión nueva (sin apurar el arranque).
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
		QTimer::singleShot(5000, &Updater::instance(), [] { Updater::instance().check(false); });
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
	MediaWatcher::instance().start();

	auto *main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	auto *dock = new NowPlayingDock(main_window);
	obs_frontend_add_dock_by_id("obs_now_playing_dock", obs_module_text("NowPlaying.Dock"), dock);
	obs_frontend_add_event_callback(on_frontend_event, nullptr);
	obs_frontend_add_tools_menu_item(
		obs_module_text("NowPlaying.Update.MenuItem"), [](void *) { Updater::instance().check(true); }, nullptr);
	Updater::cleanupPreviousUpdate();

	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(on_frontend_event, nullptr);
	MediaWatcher::instance().stop();
	obs_log(LOG_INFO, "plugin unloaded");
}
