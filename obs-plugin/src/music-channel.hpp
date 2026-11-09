#pragma once

#include <QString>

class QWidget;
struct Track;

// El canal "Música (Now Playing)": una captura de audio por aplicación de OBS con solo el
// sonido de la app que está sonando, para manejar su volumen aparte en el Mezclador. No está
// en ninguna escena: vive en un canal de salida libre de OBS (como la voz de Multichat), así
// suena en todas las escenas y aparece siempre en el Mezclador. Se activa a pedido desde
// Herramientas → Canal de música o el menú del dock (apagado por defecto). Todo esto corre en
// el hilo principal de Qt.

// Lee lo guardado y agrega la entrada marcable a Herramientas. Desde obs_module_load.
void music_channel_init();

// La colección de escenas quedó cargada (FINISHED_LOADING, SCENE_COLLECTION_CHANGED): migra
// un canal que estaba en una escena y lo pone en un canal de salida si está activado.
void music_channel_collection_loaded();

// OBS va a vaciar la colección (SCENE_COLLECTION_CHANGING, SCENE_COLLECTION_CLEANUP): guarda
// el estado del canal y suelta el canal de salida.
void music_channel_collection_unloading();

// Al salir de OBS (OBS_FRONTEND_EVENT_EXIT).
void music_channel_shutdown();

bool music_channel_enabled();

// Activa o apaga el canal (y lo recuerda). Al crearlo por primera vez avisa con un mensaje
// sobre parent; si el sistema no tiene captura por aplicación, avisa y no lo activa.
void set_music_channel_enabled(bool enabled, QWidget *parent);

// Si el canal está activado, lo reapunta en silencio a la app que está sonando ahora (al pasar
// de Spotify a YouTube Music, el canal sigue a la música). Se llama con cada lectura.
void follow_music_channel();

// La app a la que apunta la captura de audio por aplicación para esa canción (el .exe en
// Windows, el id del paquete en macOS, el programa en Linux). También la usa el volumen.
QString audio_target(const Track &track);
