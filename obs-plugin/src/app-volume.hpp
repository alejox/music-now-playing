#pragma once

#include <QString>

#include <functional>

class QObject;

// Volumen de una app en el Mezclador de volumen de Windows (el mismo de "Mezclador de
// volumen" en la bandeja), por su .exe ("Spotify.exe", "chrome.exe"). Se aplica a todas
// las sesiones de audio de esa app en todos los dispositivos de salida. Solo en Windows: en
// macOS y Linux el volumen se le pide al reproductor (ver MediaWatcher::setVolume).
//
// Recorrer los dispositivos y las sesiones de audio es lento: se hace en un hilo aparte, de a
// un pedido a la vez y en el orden en que llegan. done se llama después en el hilo de context
// (context tiene que vivir más que el plugin, como MediaWatcher). El plugin nunca silencia ni
// quita el silencio de una app: una app silenciada en Windows sigue silenciada.

// done(0–100), o done(-1) si la app no tiene sonido abierto ahora.
void app_volume_read(const QString &exe, QObject *context, std::function<void(int)> done);
// done(false) si la app no tiene sonido abierto ahora.
void app_volume_set(const QString &exe, int percent, QObject *context, std::function<void(bool)> done);
// Al cerrar OBS: devuelve cada sesión que el plugin cambió (y sigue viva) al nivel que tenía
// antes, salvo que alguien la haya movido después, y apaga el hilo. Espera como mucho unos
// 2 s. Se puede llamar más de una vez; después ya no se atienden pedidos.
void app_volume_shutdown();
