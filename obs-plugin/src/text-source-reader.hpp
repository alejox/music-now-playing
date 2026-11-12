#pragma once

// "Lo que suena" desde un archivo de texto: los programas de automatización de radio
// (Dinesat, RadioBoss, ZaraRadio, SAM, mAirList…) suelen exportar la canción al aire a un
// archivo para el RDS o la web. Aquí solo se decodifica y se interpreta (las mismas reglas que
// text-source.js de la app); el sondeo del archivo está en media-watcher.cpp. Solo la
// biblioteca estándar, para poder probarlo aparte (tests/run-text-source-test.sh).

#include <functional>
#include <string>

namespace text_source {

struct Song {
	std::string artist; // UTF-8
	std::string title;  // UTF-8
};

struct Options {
	std::string separator = " - "; // vacío: " - "
	bool titleFirst = false;       // "Título - Artista"
	// Expresión regular del usuario (opcional): recibe la línea y devuelve los grupos artist y
	// title (vacíos si no hay). Si no coincide, devuelve false. Si está, manda sobre el separador.
	std::function<bool(const std::string &line, std::string &artist, std::string &title)> match;
};

// Los bytes del archivo a UTF-8: BOM UTF-8 / UTF-16 LE / UTF-16 BE, UTF-16 sin BOM (texto
// latino), UTF-8 válido y, si no, Windows-1252 (lo más común en programas de Windows).
std::string decode(const std::string &bytes);

// El texto (UTF-8) a artista y título: XML con <Title> y <Artist> o la primera línea con
// texto. Una línea sin separador es solo el título. false: no hay canción (texto vacío).
bool parse(const std::string &text, const Options &options, Song &song);

} // namespace text_source
