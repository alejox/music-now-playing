#pragma once

// Registra la fuente "Now Playing": una tarjeta con la canción que suena. Por dentro
// es una fuente de navegador privada con data/overlay/index.html, a la que el plugin
// le manda la canción con el evento JavaScript "obs-now-playing".
void register_now_playing_source();

// Manda la canción actual a todas las tarjetas (lo llama el lector).
void push_now_playing_sources();

// Botón Refrescar del dock: recarga la página de todas las tarjetas (sin caché) y les
// vuelve a mandar la configuración y la canción cuando cargan.
void refresh_now_playing_sources();
