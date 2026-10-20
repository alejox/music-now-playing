#include "now-playing-source.hpp"
#include "media-watcher.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <set>

namespace {

// Tamaños sugeridos de cada diseño. Al cambiar de diseño, si la fuente tiene el tamaño
// sugerido del otro, se pasa al de este; si el usuario puso uno a mano, no se toca.
constexpr long long kHorizontalSize[2] = {480, 140};
constexpr long long kVerticalSize[2] = {320, 430};

struct NowPlayingSource {
	obs_source_t *self = nullptr;
	obs_source_t *browser = nullptr;
	QJsonObject config;
	uint32_t width = 480;
	uint32_t height = 140;
	QString sentArtKey; // canción cuya carátula ya le mandamos a la página
	int beats = 0;
};

QJsonObject trackJson(const Track &track, bool withArtwork)
{
	QJsonObject json{
		{"title", track.title},
		{"artist", track.artist},
		{"album", track.album},
		{"kind", track.kind},
		// Para un navegador no sabemos qué página suena: la tarjeta dice "Escuchando ahora".
		{"sourceLabel", track.kind == "browser" ? QString() : track.label},
		{"isPlaying", track.isPlaying},
		{"progressMs", track.progressNow(MediaWatcher::now())},
		{"durationMs", track.durationMs},
		{"hasArtwork", !track.artwork.isEmpty()},
	};
	if (withArtwork)
		json.insert("artwork", track.artwork);
	return json;
}

// OBS puede destruir una fuente desde otro hilo mientras el lector actualiza: todo
// acceso a las fuentes vivas pasa por este candado.
std::mutex sourcesMutex;
std::set<NowPlayingSource *> sources;

// Le manda a la página la configuración y la canción actual. La carátula (pesada) va
// solo cuando cambia y, por si la página se recargó, cada 5 latidos.
void push(NowPlayingSource *s)
{
	if (!s->browser)
		return;
	Track track = MediaWatcher::instance().current();
	QString artKey = track.app + '\n' + track.title + '\n' + track.artist + '\n' + QString::number(track.artwork.size());
	bool withArtwork = artKey != s->sentArtKey || ++s->beats % 5 == 0;
	s->sentArtKey = artKey;

	QJsonObject payload{{"config", s->config}};
	payload.insert("track", track.valid() ? QJsonValue(trackJson(track, withArtwork)) : QJsonValue());
	QByteArray json = QJsonDocument(payload).toJson(QJsonDocument::Compact);

	calldata_t cd;
	calldata_init(&cd);
	calldata_set_string(&cd, "eventName", "obs-now-playing");
	calldata_set_string(&cd, "jsonString", json.constData());
	proc_handler_call(obs_source_get_proc_handler(s->browser), "javascript_event", &cd);
	calldata_free(&cd);
}

void readSettings(NowPlayingSource *s, obs_data_t *settings)
{
	s->width = (uint32_t)std::max<long long>(100, obs_data_get_int(settings, "width"));
	s->height = (uint32_t)std::max<long long>(60, obs_data_get_int(settings, "height"));
	s->config = QJsonObject{
		{"theme", obs_data_get_string(settings, "theme")},
		{"layout", obs_data_get_string(settings, "layout")},
		{"style", obs_data_get_string(settings, "style")},
		{"opacity", (int)obs_data_get_int(settings, "opacity")},
		{"art", obs_data_get_string(settings, "art")},
		{"colors", obs_data_get_bool(settings, "colors")},
		{"blur", obs_data_get_bool(settings, "blur")},
		{"progress", obs_data_get_bool(settings, "progress")},
		{"label", obs_data_get_bool(settings, "label")},
		{"preview", obs_data_get_bool(settings, "preview")},
	};
}

obs_data_t *browserSettings(const NowPlayingSource *s)
{
	obs_data_t *bs = obs_data_create();
	char *page = obs_module_file("overlay/index.html");
	obs_data_set_bool(bs, "is_local_file", true);
	obs_data_set_string(bs, "local_file", page ? page : "");
	obs_data_set_int(bs, "width", s->width);
	obs_data_set_int(bs, "height", s->height);
	obs_data_set_int(bs, "fps", 30);
	obs_data_set_bool(bs, "shutdown", false);
	obs_data_set_bool(bs, "reroute_audio", false);
	obs_data_set_string(bs, "css", "");
	bfree(page);
	return bs;
}

const char *np_get_name(void *)
{
	return obs_module_text("NowPlaying.Source");
}

void *np_create(obs_data_t *settings, obs_source_t *source)
{
	auto *s = new NowPlayingSource();
	s->self = source;
	readSettings(s, settings);

	obs_data_t *bs = browserSettings(s);
	s->browser = obs_source_create_private("browser_source", "now-playing-overlay", bs);
	obs_data_release(bs);
	if (!s->browser)
		obs_log(LOG_WARNING, "no se pudo crear la fuente de navegador (¿falta obs-browser?)");

	std::lock_guard<std::mutex> lock(sourcesMutex);
	sources.insert(s);
	push(s);
	return s;
}

void np_destroy(void *data)
{
	auto *s = static_cast<NowPlayingSource *>(data);
	{
		std::lock_guard<std::mutex> lock(sourcesMutex);
		sources.erase(s);
	}
	obs_source_release(s->browser);
	delete s;
}

void np_update(void *data, obs_data_t *settings)
{
	auto *s = static_cast<NowPlayingSource *>(data);
	std::lock_guard<std::mutex> lock(sourcesMutex);
	readSettings(s, settings);
	if (s->browser) {
		obs_data_t *bs = browserSettings(s);
		obs_source_update(s->browser, bs);
		obs_data_release(bs);
	}
	s->sentArtKey.clear();
	push(s);
}

void np_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "width", kHorizontalSize[0]);
	obs_data_set_default_int(settings, "height", kHorizontalSize[1]);
	obs_data_set_default_string(settings, "theme", "dark");
	obs_data_set_default_string(settings, "layout", "horizontal");
	obs_data_set_default_string(settings, "style", "solid");
	obs_data_set_default_int(settings, "opacity", 85);
	obs_data_set_default_string(settings, "art", "normal");
	obs_data_set_default_bool(settings, "colors", true);
	obs_data_set_default_bool(settings, "blur", false);
	obs_data_set_default_bool(settings, "progress", true);
	obs_data_set_default_bool(settings, "label", true);
	obs_data_set_default_bool(settings, "preview", false);
}

bool np_layout_modified(obs_properties_t *, obs_property_t *, obs_data_t *settings)
{
	bool vertical = strcmp(obs_data_get_string(settings, "layout"), "vertical") == 0;
	const long long *from = vertical ? kHorizontalSize : kVerticalSize;
	const long long *to = vertical ? kVerticalSize : kHorizontalSize;
	if (obs_data_get_int(settings, "width") == from[0] && obs_data_get_int(settings, "height") == from[1]) {
		obs_data_set_int(settings, "width", to[0]);
		obs_data_set_int(settings, "height", to[1]);
	}
	return true;
}

obs_properties_t *np_properties(void *)
{
	obs_properties_t *props = obs_properties_create();
	obs_properties_add_int(props, "width", obs_module_text("NowPlaying.Width"), 100, 4096, 1);
	obs_properties_add_int(props, "height", obs_module_text("NowPlaying.Height"), 60, 4096, 1);

	obs_property_t *theme = obs_properties_add_list(props, "theme", obs_module_text("NowPlaying.Theme"),
							OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(theme, obs_module_text("NowPlaying.Theme.Dark"), "dark");
	obs_property_list_add_string(theme, obs_module_text("NowPlaying.Theme.Light"), "light");

	obs_property_t *layout = obs_properties_add_list(props, "layout", obs_module_text("NowPlaying.Layout"),
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(layout, obs_module_text("NowPlaying.Layout.Horizontal"), "horizontal");
	obs_property_list_add_string(layout, obs_module_text("NowPlaying.Layout.Vertical"), "vertical");
	obs_property_set_long_description(layout, obs_module_text("NowPlaying.Layout.Help"));
	obs_property_set_modified_callback(layout, np_layout_modified);

	obs_property_t *style = obs_properties_add_list(props, "style", obs_module_text("NowPlaying.Style"),
							OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(style, obs_module_text("NowPlaying.Style.Solid"), "solid");
	obs_property_list_add_string(style, obs_module_text("NowPlaying.Style.Glass"), "glass");
	obs_property_set_long_description(style, obs_module_text("NowPlaying.Style.Help"));

	obs_property_t *opacity =
		obs_properties_add_int_slider(props, "opacity", obs_module_text("NowPlaying.Opacity"), 0, 100, 1);
	obs_property_int_set_suffix(opacity, " %");

	obs_property_t *art = obs_properties_add_list(props, "art", obs_module_text("NowPlaying.Art"),
						      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(art, obs_module_text("NowPlaying.Art.Normal"), "normal");
	obs_property_list_add_string(art, obs_module_text("NowPlaying.Art.Big"), "big");
	obs_property_list_add_string(art, obs_module_text("NowPlaying.Art.Hidden"), "hidden");

	obs_properties_add_bool(props, "colors", obs_module_text("NowPlaying.Colors"));
	obs_properties_add_bool(props, "blur", obs_module_text("NowPlaying.Blur"));
	obs_properties_add_bool(props, "progress", obs_module_text("NowPlaying.Progress"));
	obs_properties_add_bool(props, "label", obs_module_text("NowPlaying.Label"));
	obs_properties_add_bool(props, "preview", obs_module_text("NowPlaying.Preview"));
	return props;
}

uint32_t np_width(void *data)
{
	return static_cast<NowPlayingSource *>(data)->width;
}

uint32_t np_height(void *data)
{
	return static_cast<NowPlayingSource *>(data)->height;
}

void np_render(void *data, gs_effect_t *)
{
	auto *s = static_cast<NowPlayingSource *>(data);
	if (s->browser)
		obs_source_video_render(s->browser);
}

// La fuente de navegador se pausa si nadie la muestra: le pasamos nuestra visibilidad.
void np_show(void *data)
{
	auto *s = static_cast<NowPlayingSource *>(data);
	if (s->browser)
		obs_source_inc_showing(s->browser);
}

void np_hide(void *data)
{
	auto *s = static_cast<NowPlayingSource *>(data);
	if (s->browser)
		obs_source_dec_showing(s->browser);
}

void np_activate(void *data)
{
	auto *s = static_cast<NowPlayingSource *>(data);
	if (s->browser)
		obs_source_inc_active(s->browser);
}

void np_deactivate(void *data)
{
	auto *s = static_cast<NowPlayingSource *>(data);
	if (s->browser)
		obs_source_dec_active(s->browser);
}

void np_enum_sources(void *data, obs_source_enum_proc_t enum_callback, void *param)
{
	auto *s = static_cast<NowPlayingSource *>(data);
	if (s->browser)
		enum_callback(s->self, s->browser, param);
}

} // namespace

void push_now_playing_sources()
{
	std::lock_guard<std::mutex> lock(sourcesMutex);
	for (NowPlayingSource *s : sources)
		push(s);
}

void register_now_playing_source()
{
	// Cada vez que el lector escribe (una vez por segundo), actualizamos las tarjetas.
	QObject::connect(&MediaWatcher::instance(), &MediaWatcher::updated, &MediaWatcher::instance(),
			 push_now_playing_sources);

	obs_source_info info = {};
	info.id = "obs_now_playing_source";
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
	info.icon_type = OBS_ICON_TYPE_MEDIA;
	info.get_name = np_get_name;
	info.create = np_create;
	info.destroy = np_destroy;
	info.update = np_update;
	info.get_defaults = np_defaults;
	info.get_properties = np_properties;
	info.get_width = np_width;
	info.get_height = np_height;
	info.video_render = np_render;
	info.show = np_show;
	info.hide = np_hide;
	info.activate = np_activate;
	info.deactivate = np_deactivate;
	info.enum_active_sources = np_enum_sources;
	info.enum_all_sources = np_enum_sources;
	obs_register_source(&info);
}
