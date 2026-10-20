#pragma once

// Registra la fuente "Now Playing": una tarjeta con la canción que suena. Por dentro
// es una fuente de navegador privada con data/overlay/index.html, a la que el plugin
// le manda la canción con el evento JavaScript "obs-now-playing".
void register_now_playing_source();

// Manda la canción actual a todas las tarjetas (lo llama el lector).
void push_now_playing_sources();
