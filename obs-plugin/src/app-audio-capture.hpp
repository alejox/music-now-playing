#pragma once

#include <QString>

// Captura propia del audio de una app en macOS (ScreenCaptureKit), para el canal de música.
//
// La captura por aplicación de OBS solo ve las apps con una ventana a la vista: la app
// instalada de YouTube Music muestra su ventana con su propio id, pero el sonido sale de
// Chrome (com.google.Chrome), que no tiene ventana a la vista, y OBS no puede capturarlo.
// Esta fuente pide también las apps sin ventana, así captura al reproductor igual.
//
// La fuente "obs_now_playing_app_audio" solo tiene audio y no aparece en "Agregar fuente":
// la crea el canal de música. Su ajuste "application" es el id del paquete de la app.

#ifdef __APPLE__

constexpr const char *kAppAudioSourceId = "obs_now_playing_app_audio";

// Registra la fuente (solo en macOS 13 o posterior). Desde obs_module_load.
void register_app_audio_capture();

// La app con ese id de paquete está abierta (con o sin ventana), así que se puede capturar.
bool app_audio_is_capturable(const QString &bundleId);

#endif
