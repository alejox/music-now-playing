// Pruebas del lector de archivos de texto de programas de radio (text-source.js).
//   npm test
'use strict';
const assert = require('node:assert/strict');
const { decodeBuffer, createParser, parseBuffer } = require('../text-source');

const parse = (text, options) => createParser(options).parse(text);
const utf16le = (s) => Buffer.concat([Buffer.from([0xff, 0xfe]), Buffer.from(s, 'utf16le')]);
const utf16be = (s) => {
  const le = Buffer.from(s, 'utf16le');
  for (let i = 0; i < le.length; i += 2) [le[i], le[i + 1]] = [le[i + 1], le[i]];
  return Buffer.concat([Buffer.from([0xfe, 0xff]), le]);
};

const cases = {
  'separator, artist-title (default)': () =>
    assert.deepEqual(parse('Bad Bunny - Ojitos Lindos'), { artist: 'Bad Bunny', title: 'Ojitos Lindos' }),

  'custom separator and title-artist order': () =>
    assert.deepEqual(parse('Ojitos Lindos | Bad Bunny', { separator: ' | ', order: 'title-artist' }), {
      artist: 'Bad Bunny',
      title: 'Ojitos Lindos',
    }),

  'splits only at the first separator': () =>
    assert.deepEqual(parse('Artista - Canción - En vivo'), { artist: 'Artista', title: 'Canción - En vivo' }),

  'first non-empty line, trimmed': () =>
    assert.deepEqual(parse('\r\n   \r\n  Artista - Canción  \r\nOtra - Línea\r\n'), { artist: 'Artista', title: 'Canción' }),

  'missing artist: whole line is the title': () => {
    assert.deepEqual(parse('Solo el título'), { artist: '', title: 'Solo el título' });
    assert.deepEqual(parse(' - Canción'), { artist: '', title: '- Canción' });
    assert.deepEqual(parse('Artista - '), { artist: '', title: 'Artista -' });
  },

  'empty content is not playing': () => {
    assert.equal(parse(''), null);
    assert.equal(parse('  \r\n\t\n'), null);
    assert.equal(parseBuffer(Buffer.alloc(0)), null);
  },

  'regex pattern with named groups overrides the separator': () => {
    const options = { separator: ' - ', pattern: '^\\[(?<artist>[^\\]]+)\\]\\s*(?<title>.+)$' };
    assert.deepEqual(parse('[Shakira] Ojos Así - Remix', options), { artist: 'Shakira', title: 'Ojos Así - Remix' });
    // Sin coincidencia: la línea entera es el título.
    assert.deepEqual(parse('Cuña comercial', options), { artist: '', title: 'Cuña comercial' });
  },

  'invalid regex pattern falls back to the separator and reports it': () => {
    const parser = createParser({ pattern: '(?<artist>' });
    assert.ok(parser.error);
    assert.deepEqual(parser.parse('A - B'), { artist: 'A', title: 'B' });
  },

  'XML: first Title/Artist, any case, entities decoded': () => {
    const xml = `<?xml version="1.0" encoding="utf-8"?>
<Playlist><Song id="1"><Artist>Simon &amp; Garfunkel</Artist><Title>The Boxer &#8211; Live</Title></Song>
<Song><Artist>Otro</Artist><Title>Siguiente</Title></Song></Playlist>`;
    assert.deepEqual(parse(xml), { artist: 'Simon & Garfunkel', title: 'The Boxer – Live' });
    assert.deepEqual(parse('<NOWPLAYING><TITLE>Canción</TITLE><ARTIST>Artista</ARTIST></NOWPLAYING>'), {
      artist: 'Artista',
      title: 'Canción',
    });
    assert.deepEqual(parse('<track><title><![CDATA[Rock & Roll]]></title></track>'), { artist: '', title: 'Rock & Roll' });
    assert.equal(parse('<?xml version="1.0"?><Empty></Empty>'), null);
  },

  'UTF-8 with BOM': () =>
    assert.deepEqual(parseBuffer(Buffer.concat([Buffer.from([0xef, 0xbb, 0xbf]), Buffer.from('Artista - Canción')])), {
      artist: 'Artista',
      title: 'Canción',
    }),

  'UTF-8 without BOM': () => assert.equal(decodeBuffer(Buffer.from('Niño ñandú — 🎵')), 'Niño ñandú — 🎵'),

  'UTF-16 LE with BOM': () =>
    assert.deepEqual(parseBuffer(utf16le('Artista - Canción\r\n')), { artist: 'Artista', title: 'Canción' }),

  'UTF-16 BE with BOM': () => assert.equal(decodeBuffer(utf16be('Corazón')), 'Corazón'),

  'Windows-1252 / Latin-1 fallback': () => {
    // "Canción" en cp1252: ó = 0xF3 (no es UTF-8 válido).
    assert.equal(decodeBuffer(Buffer.from('Canción', 'latin1')), 'Canción');
    // 0x80–0x9F son propios de cp1252: € – “ ” ’.
    assert.equal(decodeBuffer(Buffer.from([0x80, 0x20, 0x96, 0x20, 0x93, 0x78, 0x94, 0x92])), '€ – “x”’');
    assert.deepEqual(parseBuffer(Buffer.from('José Feliciano - Canción', 'latin1')), {
      artist: 'José Feliciano',
      title: 'Canción',
    });
  },

  'options with bad types use the defaults': () =>
    assert.deepEqual(parse('A - B', { separator: 5, order: 'nope', pattern: null }), { artist: 'A', title: 'B' }),
};

let failed = 0;
for (const [name, run] of Object.entries(cases)) {
  try {
    run();
  } catch (err) {
    failed++;
    console.error(`✘ ${name}\n  ${err.message.split('\n').join('\n  ')}`);
  }
}
if (failed) {
  console.error(`${failed} prueba(s) del lector de texto fallaron`);
  process.exit(1);
}
console.log(`✔ lector de texto OK (${Object.keys(cases).length} casos)`);
