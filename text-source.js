// Lector de "lo que suena" desde un archivo de texto: los programas de automatización
// de radio (Dinesat, RadioBoss, ZaraRadio, SAM, mAirList…) suelen exportar la canción al
// aire a un archivo para el RDS o la web. Aquí solo se decodifica y se interpreta; el
// sondeo del archivo está en server.js. Sin dependencias, para poder probarlo aparte.
'use strict';

// Windows-1252 en 0x80–0x9F (en Latin-1 son controles). Los huecos quedan como Latin-1.
const CP1252 = {
  0x80: 0x20ac, 0x82: 0x201a, 0x83: 0x0192, 0x84: 0x201e, 0x85: 0x2026, 0x86: 0x2020, 0x87: 0x2021,
  0x88: 0x02c6, 0x89: 0x2030, 0x8a: 0x0160, 0x8b: 0x2039, 0x8c: 0x0152, 0x8e: 0x017d,
  0x91: 0x2018, 0x92: 0x2019, 0x93: 0x201c, 0x94: 0x201d, 0x95: 0x2022, 0x96: 0x2013, 0x97: 0x2014,
  0x98: 0x02dc, 0x99: 0x2122, 0x9a: 0x0161, 0x9b: 0x203a, 0x9c: 0x0153, 0x9e: 0x017e, 0x9f: 0x0178,
};

const evenLength = (buf) => buf.subarray(0, buf.length - (buf.length % 2));
const utf16be = (buf) => Buffer.from(evenLength(buf)).swap16().toString('utf16le');

// UTF-16 sin BOM: en texto latino, uno de cada dos bytes es 0.
function guessUtf16(buf) {
  const n = Math.min(evenLength(buf).length, 64);
  if (n < 4) return '';
  let evenZeros = 0;
  let oddZeros = 0;
  for (let i = 0; i < n; i += 2) {
    if (buf[i] === 0) evenZeros++;
    if (buf[i + 1] === 0) oddZeros++;
  }
  const pairs = n / 2;
  if (oddZeros >= pairs * 0.6 && evenZeros === 0) return 'le';
  if (evenZeros >= pairs * 0.6 && oddZeros === 0) return 'be';
  return '';
}

// BOM UTF-8 / UTF-16 LE / UTF-16 BE, UTF-8 válido y, si no, Windows-1252.
function decodeBuffer(buf) {
  if (!buf || !buf.length) return '';
  if (buf[0] === 0xef && buf[1] === 0xbb && buf[2] === 0xbf) return buf.subarray(3).toString('utf8');
  if (buf[0] === 0xff && buf[1] === 0xfe) return evenLength(buf.subarray(2)).toString('utf16le');
  if (buf[0] === 0xfe && buf[1] === 0xff) return utf16be(buf.subarray(2));
  const utf16 = guessUtf16(buf);
  if (utf16 === 'le') return evenLength(buf).toString('utf16le');
  if (utf16 === 'be') return utf16be(buf);
  try {
    return new TextDecoder('utf-8', { fatal: true }).decode(buf);
  } catch {
    // No es UTF-8 válido: lo más común en programas de Windows es Windows-1252.
    let out = '';
    for (const byte of buf) out += String.fromCharCode(CP1252[byte] || byte);
    return out;
  }
}

const ENTITIES = { amp: '&', lt: '<', gt: '>', quot: '"', apos: "'", nbsp: ' ' };
function decodeEntities(s) {
  return s.replace(/&(#x[0-9a-f]+|#\d+|[a-z]+);/gi, (whole, name) => {
    if (name[0] === '#') {
      const code = /^#x/i.test(name) ? parseInt(name.slice(2), 16) : parseInt(name.slice(1), 10);
      return code > 0 && code <= 0x10ffff ? String.fromCodePoint(code) : whole;
    }
    return ENTITIES[name.toLowerCase()] ?? whole;
  });
}

const cleanLine = (s) => s.replace(/[\u0000-\u0008\u000b-\u001f\u007f]/g, '').replace(/\s+/g, ' ').trim();

// Primer <tag>…</tag> (sin importar mayúsculas ni atributos), con CDATA y entidades.
function xmlField(xml, tag) {
  const m = new RegExp(`<${tag}(?:\\s[^>]*)?>([\\s\\S]*?)</${tag}\\s*>`, 'i').exec(xml);
  if (!m) return '';
  const cdata = /^\s*<!\[CDATA\[([\s\S]*?)\]\]>\s*$/.exec(m[1]);
  return cleanLine(cdata ? cdata[1] : decodeEntities(m[1].replace(/<[^>]*>/g, '')));
}

function parseXml(xml) {
  const title = xmlField(xml, 'title');
  const artist = xmlField(xml, 'artist');
  if (!title && !artist) return null;
  // Con un solo campo, se muestra como título.
  return title ? { artist, title } : { artist: '', title: artist };
}

// options: { separator = ' - ', order = 'artist-title' | 'title-artist', pattern = '' }.
// pattern es una expresión regular con grupos (?<artist>…) y (?<title>…); si está, manda
// sobre el separador. Devuelve { parse(text) → { artist, title } | null, error }.
function createParser(options = {}) {
  const separator = typeof options.separator === 'string' && options.separator ? options.separator : ' - ';
  const titleFirst = options.order === 'title-artist';
  let regex = null;
  let error = '';
  if (typeof options.pattern === 'string' && options.pattern) {
    try {
      regex = new RegExp(options.pattern, 'u');
    } catch (err) {
      error = `Expresión regular inválida en "pattern": ${err.message}`;
    }
  }

  function parseLine(line) {
    if (regex) {
      const groups = regex.exec(line)?.groups || {};
      const title = cleanLine(groups.title || '');
      const artist = cleanLine(groups.artist || '');
      return title ? { artist, title } : { artist: '', title: line };
    }
    const at = line.indexOf(separator);
    if (at < 0) return { artist: '', title: line };
    const first = line.slice(0, at).trim();
    const second = line.slice(at + separator.length).trim();
    if (!first || !second) return { artist: '', title: line };
    return titleFirst ? { artist: second, title: first } : { artist: first, title: second };
  }

  function parse(text) {
    const content = String(text || '').replace(/^﻿/, '').trim();
    if (!content) return null;
    if (content[0] === '<') return parseXml(content);
    const line = content.split(/\r\n|\r|\n/).map(cleanLine).find(Boolean);
    return line ? parseLine(line) : null;
  }

  return { parse, error };
}

const parseBuffer = (buf, options) => createParser(options).parse(decodeBuffer(buf));

module.exports = { decodeBuffer, createParser, parseBuffer };
