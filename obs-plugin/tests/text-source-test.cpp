// Pruebas del lector de archivos de texto de programas de radio (src/text-source-reader.cpp):
// los mismos casos que test/text-source.js de la app.
//   obs-plugin/tests/run-text-source-test.sh
#include "text-source-reader.hpp"

#include <cstdio>
#include <regex>
#include <string>

namespace {

int failed = 0;
int checks = 0;

void expectSong(const char *name, const std::string &text, const text_source::Options &options, bool found,
		const std::string &artist = std::string(), const std::string &title = std::string())
{
	checks++;
	text_source::Song song;
	const bool ok = text_source::parse(text, options, song);
	if (ok != found || (found && (song.artist != artist || song.title != title))) {
		failed++;
		std::fprintf(stderr, "✘ %s\n  esperado: %d [%s] [%s]\n  obtenido: %d [%s] [%s]\n", name, found,
			     artist.c_str(), title.c_str(), ok, song.artist.c_str(), song.title.c_str());
	}
}

void expectSong(const char *name, const std::string &text, bool found, const std::string &artist = std::string(),
		const std::string &title = std::string())
{
	expectSong(name, text, text_source::Options(), found, artist, title);
}

void expectText(const char *name, const std::string &bytes, const std::string &text)
{
	checks++;
	const std::string decoded = text_source::decode(bytes);
	if (decoded != text) {
		failed++;
		std::fprintf(stderr, "✘ %s\n  esperado: [%s]\n  obtenido: [%s]\n", name, text.c_str(), decoded.c_str());
	}
}

// Texto ASCII o Latin-1 (un byte por carácter) a UTF-16 con BOM.
std::string utf16(const std::string &latin1, bool bigEndian)
{
	std::string out = bigEndian ? std::string("\xfe\xff") : std::string("\xff\xfe");
	for (unsigned char c : latin1) {
		out += bigEndian ? '\0' : char(c);
		out += bigEndian ? char(c) : '\0';
	}
	return out;
}

} // namespace

int main()
{
	expectSong("separador, artista-título", "Bad Bunny - Ojitos Lindos", true, "Bad Bunny", "Ojitos Lindos");

	text_source::Options titleFirst;
	titleFirst.separator = " | ";
	titleFirst.titleFirst = true;
	expectSong("separador propio, título-artista", "Ojitos Lindos | Bad Bunny", titleFirst, true, "Bad Bunny",
		   "Ojitos Lindos");

	expectSong("solo el primer separador", "Artista - Canción - En vivo", true, "Artista", "Canción - En vivo");
	expectSong("primera línea con texto", "\r\n   \r\n  Artista - Canción  \r\nOtra - Línea\r\n", true, "Artista",
		   "Canción");
	expectSong("sin artista", "Solo el título", true, "", "Solo el título");
	expectSong("sin artista (separador al inicio)", " - Canción", true, "", "- Canción");
	expectSong("sin artista (separador al final)", "Artista - ", true, "", "Artista -");
	expectSong("vacío", "", false);
	expectSong("solo espacios", "  \r\n\t\n", false);
	expectSong("bytes vacíos", text_source::decode(std::string()), false);

	// La expresión regular la pone el plugin (QRegularExpression con grupos con nombre); aquí,
	// una equivalente con grupos numerados.
	text_source::Options pattern;
	pattern.match = [](const std::string &line, std::string &artist, std::string &title) {
		static const std::regex re("^\\[([^\\]]+)\\]\\s*(.+)$");
		std::smatch m;
		if (!std::regex_match(line, m, re))
			return false;
		artist = m[1];
		title = m[2];
		return true;
	};
	expectSong("expresión regular", "[Shakira] Ojos Así - Remix", pattern, true, "Shakira", "Ojos Así - Remix");
	expectSong("expresión regular sin coincidencia", "Cuña comercial", pattern, true, "", "Cuña comercial");

	expectSong("XML: el primer Title/Artist, con entidades",
		   "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<Playlist><Song id=\"1\"><Artist>Simon &amp; "
		   "Garfunkel</Artist><Title>The Boxer &#8211; Live</Title></Song>\n<Song><Artist>Otro</Artist><Title>"
		   "Siguiente</Title></Song></Playlist>",
		   true, "Simon & Garfunkel", "The Boxer – Live");
	expectSong("XML en mayúsculas", "<NOWPLAYING><TITLE>Canción</TITLE><ARTIST>Artista</ARTIST></NOWPLAYING>", true,
		   "Artista", "Canción");
	expectSong("XML con CDATA", "<track><title><![CDATA[Rock & Roll]]></title></track>", true, "", "Rock & Roll");
	expectSong("XML solo con artista", "<track><artist>Solo artista</artist></track>", true, "", "Solo artista");
	expectSong("XML sin campos", "<?xml version=\"1.0\"?><Empty></Empty>", false);
	expectSong("XML: <titles> no es <title>", "<titles>No</titles><title a=\"1\">Sí</title >", true, "", "Sí");

	expectText("UTF-8 con BOM",
		   "\xef\xbb\xbf"
		   "Artista - Canción",
		   "Artista - Canción");
	expectText("UTF-8 sin BOM", "Niño ñandú — 🎵", "Niño ñandú — 🎵");
	expectText("UTF-16 LE con BOM", utf16("Artista - Canci\xf3n\r\n", false), "Artista - Canción\r\n");
	expectText("UTF-16 BE con BOM", utf16("Coraz\xf3n", true), "Corazón");
	expectText("UTF-16 LE sin BOM", utf16("Artista - Canci\xf3n", false).substr(2), "Artista - Canción");
	expectText("UTF-16 LE con emoji", std::string("\xff\xfe\x3c\xd8\xb5\xdf", 6), "🎵");
	expectText("Windows-1252: ó", "Canci\xf3n", "Canción");
	expectText("Windows-1252: 0x80–0x9F", "\x80 \x96 \x93x\x94\x92", "€ – “x”’");
	expectSong("Windows-1252 de punta a punta", text_source::decode("Jos\xe9 Feliciano - Canci\xf3n"), true,
		   "José Feliciano", "Canción");
	expectText("UTF-8 inválido (larga de más) es Windows-1252", "\xc0\xaf", "À¯");

	if (failed) {
		std::fprintf(stderr, "%d de %d pruebas del lector de texto fallaron\n", failed, checks);
		return 1;
	}
	std::printf("✔ lector de texto OK (%d casos)\n", checks);
	return 0;
}
