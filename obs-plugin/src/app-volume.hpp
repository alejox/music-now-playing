#pragma once

#include <QString>

// Volumen de una app en el Mezclador de volumen de Windows (el mismo de "Mezclador de
// volumen" en la bandeja), por su .exe ("Spotify.exe", "chrome.exe"). Se aplica a todas
// las sesiones de audio de esa app en todos los dispositivos de salida. Solo en Windows: en
// macOS y Linux el volumen se le pide al reproductor (ver MediaWatcher::setVolume).

// 0–100, o -1 si la app no tiene sonido abierto ahora.
int app_volume(const QString &exe);
bool set_app_volume(const QString &exe, int percent);
