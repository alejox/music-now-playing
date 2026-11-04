#pragma once

#include <QString>

class QWidget;
struct Track;

// Crea (o reapunta, si ya existe) el canal "Música (Now Playing)": una captura de audio por
// aplicación de OBS con solo el sonido de la app que está sonando, en la escena abierta,
// para manejar su volumen aparte en el Mezclador. Avisa con un mensaje sobre parent.
// Se llama desde el dock, las Propiedades de la fuente y el menú Herramientas.
void create_music_channel(QWidget *parent);

// Si el canal ya existe, lo reapunta en silencio a la app que está sonando ahora (al pasar
// de Spotify a YouTube Music, el canal sigue a la música). Se llama con cada lectura.
void follow_music_channel();

// La app a la que apunta la captura de audio por aplicación para esa canción (el .exe en
// Windows, el id del paquete en macOS, el programa en Linux). También la usa el volumen.
QString audio_target(const Track &track);

// Volumen del canal "Música (Now Playing)" (modo "Stream" del dock): 0–100 como la barra
// del Mezclador de OBS. Devuelven false si el canal no existe.
bool music_channel_volume(int *percent, bool *muted);
bool set_music_channel_volume(int percent);
bool set_music_channel_muted(bool muted);
