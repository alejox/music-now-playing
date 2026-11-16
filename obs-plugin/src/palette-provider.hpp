#pragma once

// Publica la paleta de colores de la canción que suena para que otros plugins la lean: el
// procedimiento global de OBS
//
//   void music_now_playing_get_palette(out bool playing, out int accent, out int tint, out int background)
//
// (ver "Integraciones" en el README). Solo lee lo que ya tiene MediaWatcher: no cambia el
// lector, la fuente, el dock ni la tarjeta.

// En obs_module_load, después de MediaWatcher::start() (hilo de la interfaz).
void palette_provider_start();
// En obs_module_unload: desde ahí el procedimiento responde playing = false.
void palette_provider_stop();
