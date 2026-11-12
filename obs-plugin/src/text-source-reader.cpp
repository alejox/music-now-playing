#include "text-source-reader.hpp"

#include <algorithm>
#include <cstdint>

namespace text_source {

namespace {

using Text = std::u32string;
constexpr char32_t kReplacement = 0xfffd;

// Windows-1252 en 0x80–0x9F (en Latin-1 son controles). Los huecos quedan como Latin-1.
char32_t cp1252(unsigned char byte)
{
	static const char32_t table[32] = {
		0x20ac, 0x81,   0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021, 0x02c6, 0x2030, 0x0160,
		0x2039, 0x0152, 0x8d,   0x017d, 0x8f,   0x90,   0x2018, 0x2019, 0x201c, 0x201d, 0x2022,
		0x2013, 0x2014, 0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0x9d,   0x017e, 0x0178,
	};
	return byte >= 0x80 && byte <= 0x9f ? table[byte - 0x80] : char32_t(byte);
}

void appendUtf8(std::string &out, char32_t c)
{
	if (c >= 0xd800 && c <= 0xdfff)
		c = kReplacement;
	if (c < 0x80) {
		out += char(c);
	} else if (c < 0x800) {
		out += char(0xc0 | (c >> 6));
		out += char(0x80 | (c & 0x3f));
	} else if (c < 0x10000) {
		out += char(0xe0 | (c >> 12));
		out += char(0x80 | ((c >> 6) & 0x3f));
		out += char(0x80 | (c & 0x3f));
	} else {
		out += char(0xf0 | (c >> 18));
		out += char(0x80 | ((c >> 12) & 0x3f));
		out += char(0x80 | ((c >> 6) & 0x3f));
		out += char(0x80 | (c & 0x3f));
	}
}

std::string toUtf8(const Text &text)
{
	std::string out;
	out.reserve(text.size());
	for (char32_t c : text)
		appendUtf8(out, c);
	return out;
}

// UTF-8 a puntos de código. strict: false si hay una secuencia inválida (larga de más,
// sustitutos, fuera de rango, cortada); si no, la secuencia inválida queda como U+FFFD.
bool fromUtf8(const std::string &bytes, size_t from, Text &out, bool strict)
{
	const size_t n = bytes.size();
	for (size_t i = from; i < n;) {
		const unsigned char b = static_cast<unsigned char>(bytes[i]);
		int extra = 0;
		char32_t c = 0;
		char32_t min = 0;
		if (b < 0x80) {
			out += char32_t(b);
			i++;
			continue;
		} else if (b >= 0xc2 && b <= 0xdf) {
			extra = 1;
			c = b & 0x1f;
			min = 0x80;
		} else if (b >= 0xe0 && b <= 0xef) {
			extra = 2;
			c = b & 0x0f;
			min = 0x800;
		} else if (b >= 0xf0 && b <= 0xf4) {
			extra = 3;
			c = b & 0x07;
			min = 0x10000;
		} else {
			if (strict)
				return false;
			out += kReplacement;
			i++;
			continue;
		}
		bool ok = i + size_t(extra) < n;
		for (int k = 1; ok && k <= extra; k++) {
			const unsigned char next = static_cast<unsigned char>(bytes[i + k]);
			if ((next & 0xc0) != 0x80)
				ok = false;
			else
				c = (c << 6) | (next & 0x3f);
		}
		if (ok && (c < min || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)))
			ok = false;
		if (!ok) {
			if (strict)
				return false;
			out += kReplacement;
			i++;
			continue;
		}
		out += c;
		i += size_t(extra) + 1;
	}
	return true;
}

Text fromUtf16(const std::string &bytes, size_t from, bool bigEndian)
{
	Text out;
	const size_t end = from + ((bytes.size() - from) / 2) * 2;
	for (size_t i = from; i < end; i += 2) {
		const auto lo = static_cast<unsigned char>(bytes[i + (bigEndian ? 1 : 0)]);
		const auto hi = static_cast<unsigned char>(bytes[i + (bigEndian ? 0 : 1)]);
		char32_t unit = char32_t(hi << 8 | lo);
		if (unit >= 0xd800 && unit <= 0xdbff && i + 2 < end) {
			const auto lo2 = static_cast<unsigned char>(bytes[i + 2 + (bigEndian ? 1 : 0)]);
			const auto hi2 = static_cast<unsigned char>(bytes[i + 2 + (bigEndian ? 0 : 1)]);
			const char32_t low = char32_t(hi2 << 8 | lo2);
			if (low >= 0xdc00 && low <= 0xdfff) {
				out += 0x10000 + ((unit - 0xd800) << 10) + (low - 0xdc00);
				i += 2;
				continue;
			}
		}
		out += unit >= 0xd800 && unit <= 0xdfff ? kReplacement : unit;
	}
	return out;
}

// UTF-16 sin BOM: en texto latino, uno de cada dos bytes es 0. 'l', 'b' o 0 (no parece).
char guessUtf16(const std::string &bytes)
{
	const size_t n = std::min<size_t>(bytes.size() - bytes.size() % 2, 64);
	if (n < 4)
		return 0;
	size_t evenZeros = 0;
	size_t oddZeros = 0;
	for (size_t i = 0; i < n; i += 2) {
		if (bytes[i] == 0)
			evenZeros++;
		if (bytes[i + 1] == 0)
			oddZeros++;
	}
	const double pairs = double(n) / 2;
	if (double(oddZeros) >= pairs * 0.6 && evenZeros == 0)
		return 'l';
	if (double(evenZeros) >= pairs * 0.6 && oddZeros == 0)
		return 'b';
	return 0;
}

// Los espacios de \s en JavaScript.
bool isSpace(char32_t c)
{
	return (c >= 0x09 && c <= 0x0d) || c == 0x20 || c == 0xa0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200a) ||
	       c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000 || c == 0xfeff;
}

Text trim(const Text &s)
{
	size_t start = 0;
	size_t end = s.size();
	while (start < end && isSpace(s[start]))
		start++;
	while (end > start && isSpace(s[end - 1]))
		end--;
	return s.substr(start, end - start);
}

// Sin caracteres de control, con los espacios juntos en uno y sin espacios en los bordes.
Text cleanLine(const Text &s)
{
	Text out;
	bool space = false;
	for (char32_t c : s) {
		if (c <= 0x08 || (c >= 0x0b && c <= 0x1f) || c == 0x7f)
			continue;
		if (isSpace(c)) {
			space = true;
			continue;
		}
		if (space && !out.empty())
			out += U' ';
		space = false;
		out += c;
	}
	return out;
}

char32_t lower(char32_t c)
{
	return c >= U'A' && c <= U'Z' ? c + 32 : c;
}

bool startsWithAt(const Text &s, size_t at, const Text &prefix)
{
	if (at + prefix.size() > s.size())
		return false;
	for (size_t i = 0; i < prefix.size(); i++)
		if (lower(s[at + i]) != lower(prefix[i]))
			return false;
	return true;
}

Text decodeEntities(const Text &s)
{
	Text out;
	for (size_t i = 0; i < s.size();) {
		if (s[i] != U'&') {
			out += s[i++];
			continue;
		}
		// &#x1F3B5; &#233; &amp;
		size_t j = i + 1;
		enum { Hex, Dec, Name } kind = Name;
		if (j < s.size() && s[j] == U'#') {
			j++;
			kind = Dec;
			if (j < s.size() && lower(s[j]) == U'x') {
				j++;
				kind = Hex;
			}
		}
		const size_t digits = j;
		while (j < s.size()) {
			const char32_t c = lower(s[j]);
			const bool ok = kind == Hex   ? ((c >= U'0' && c <= U'9') || (c >= U'a' && c <= U'f'))
					: kind == Dec ? (c >= U'0' && c <= U'9')
						      : (c >= U'a' && c <= U'z');
			if (!ok)
				break;
			j++;
		}
		if (j == digits || j >= s.size() || s[j] != U';') {
			out += s[i++];
			continue;
		}
		const Text name = s.substr(digits, j - digits);
		const Text whole = s.substr(i, j + 1 - i);
		i = j + 1;
		if (kind != Name) {
			uint64_t code = 0;
			for (char32_t c : name) {
				const char32_t d = lower(c);
				code = code * (kind == Hex ? 16 : 10) + (d <= U'9' ? d - U'0' : d - U'a' + 10);
				if (code > 0x10ffff)
					break;
			}
			if (code > 0 && code <= 0x10ffff)
				out += char32_t(code);
			else
				out += whole;
			continue;
		}
		Text key;
		for (char32_t c : name)
			key += lower(c);
		if (key == U"amp")
			out += U'&';
		else if (key == U"lt")
			out += U'<';
		else if (key == U"gt")
			out += U'>';
		else if (key == U"quot")
			out += U'"';
		else if (key == U"apos")
			out += U'\'';
		else if (key == U"nbsp")
			out += char32_t(0xa0);
		else
			out += whole;
	}
	return out;
}

Text stripTags(const Text &s)
{
	Text out;
	for (size_t i = 0; i < s.size(); i++) {
		if (s[i] == U'<') {
			const size_t close = s.find(U'>', i + 1);
			if (close != Text::npos) {
				i = close;
				continue;
			}
		}
		out += s[i];
	}
	return out;
}

// El primer <tag>…</tag> (sin importar mayúsculas ni atributos), con CDATA y entidades.
Text xmlField(const Text &xml, const Text &tag)
{
	const Text open = U"<" + tag;
	const Text close = U"</" + tag;
	for (size_t at = 0; at < xml.size(); at++) {
		if (!startsWithAt(xml, at, open))
			continue;
		size_t after = at + open.size();
		if (after >= xml.size())
			return Text();
		if (xml[after] != U'>') {
			if (!isSpace(xml[after]))
				continue;
			after = xml.find(U'>', after);
			if (after == Text::npos)
				return Text();
		}
		const size_t contentStart = after + 1;
		for (size_t end = contentStart; end < xml.size(); end++) {
			if (!startsWithAt(xml, end, close))
				continue;
			size_t k = end + close.size();
			while (k < xml.size() && isSpace(xml[k]))
				k++;
			if (k >= xml.size() || xml[k] != U'>')
				continue;
			const Text content = xml.substr(contentStart, end - contentStart);
			const Text trimmed = trim(content);
			const Text cdataOpen = U"<![CDATA[";
			const Text cdataClose = U"]]>";
			if (trimmed.size() >= cdataOpen.size() + cdataClose.size() &&
			    trimmed.compare(0, cdataOpen.size(), cdataOpen) == 0 &&
			    trimmed.compare(trimmed.size() - cdataClose.size(), cdataClose.size(), cdataClose) == 0)
				return cleanLine(trimmed.substr(cdataOpen.size(),
								trimmed.size() - cdataOpen.size() - cdataClose.size()));
			return cleanLine(decodeEntities(stripTags(content)));
		}
		// Sin cierre después de la primera apertura no hay cierre para ninguna.
		return Text();
	}
	return Text();
}

bool parseXml(const Text &xml, Song &song)
{
	const Text title = xmlField(xml, U"title");
	const Text artist = xmlField(xml, U"artist");
	if (title.empty() && artist.empty())
		return false;
	// Con un solo campo, se muestra como título.
	song.title = toUtf8(title.empty() ? artist : title);
	song.artist = title.empty() ? std::string() : toUtf8(artist);
	return true;
}

Text utf32(const std::string &utf8)
{
	Text out;
	fromUtf8(utf8, 0, out, false);
	return out;
}

void parseLine(const Text &line, const Options &options, Song &song)
{
	if (options.match) {
		std::string artist;
		std::string title;
		const std::string raw = toUtf8(line);
		if (options.match(raw, artist, title)) {
			const Text cleanTitle = cleanLine(utf32(title));
			if (!cleanTitle.empty()) {
				song.title = toUtf8(cleanTitle);
				song.artist = toUtf8(cleanLine(utf32(artist)));
				return;
			}
		}
		song.artist.clear();
		song.title = raw;
		return;
	}
	const Text separator = utf32(options.separator.empty() ? std::string(" - ") : options.separator);
	const size_t at = line.find(separator);
	song.artist.clear();
	song.title = toUtf8(line);
	if (at == Text::npos)
		return;
	const Text first = trim(line.substr(0, at));
	const Text second = trim(line.substr(at + separator.size()));
	if (first.empty() || second.empty())
		return;
	song.artist = toUtf8(options.titleFirst ? second : first);
	song.title = toUtf8(options.titleFirst ? first : second);
}

} // namespace

std::string decode(const std::string &bytes)
{
	const auto at = [&](size_t i) {
		return i < bytes.size() ? static_cast<unsigned char>(bytes[i]) : 0;
	};
	if (bytes.empty())
		return std::string();
	Text text;
	if (at(0) == 0xef && at(1) == 0xbb && at(2) == 0xbf) {
		fromUtf8(bytes, 3, text, false);
		return toUtf8(text);
	}
	if (at(0) == 0xff && at(1) == 0xfe)
		return toUtf8(fromUtf16(bytes, 2, false));
	if (at(0) == 0xfe && at(1) == 0xff)
		return toUtf8(fromUtf16(bytes, 2, true));
	const char utf16 = guessUtf16(bytes);
	if (utf16)
		return toUtf8(fromUtf16(bytes, 0, utf16 == 'b'));
	if (fromUtf8(bytes, 0, text, true))
		return bytes;
	// No es UTF-8 válido: lo más común en programas de Windows es Windows-1252.
	text.clear();
	for (char byte : bytes)
		text += cp1252(static_cast<unsigned char>(byte));
	return toUtf8(text);
}

bool parse(const std::string &utf8, const Options &options, Song &song)
{
	song = Song();
	Text content = utf32(utf8);
	if (!content.empty() && content[0] == 0xfeff)
		content.erase(0, 1);
	content = trim(content);
	if (content.empty())
		return false;
	if (content[0] == U'<')
		return parseXml(content, song);
	// La primera línea con texto (\r\n, \r o \n).
	size_t start = 0;
	while (start <= content.size()) {
		size_t end = content.find_first_of(U"\r\n", start);
		if (end == Text::npos)
			end = content.size();
		const Text line = cleanLine(content.substr(start, end - start));
		if (!line.empty()) {
			parseLine(line, options, song);
			return true;
		}
		start = end + (end + 1 < content.size() && content[end] == U'\r' && content[end + 1] == U'\n' ? 2 : 1);
	}
	return false;
}

} // namespace text_source
