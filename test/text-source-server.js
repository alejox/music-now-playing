// Prueba de la fuente "Radio (archivo)": el servidor lee el archivo que exporta un
// programa de radio y lo muestra como la canción actual.
//   npm test
'use strict';
const { spawn } = require('node:child_process');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const ROOT = path.join(__dirname, '..');
const PORT = 18789;
const BASE = `http://127.0.0.1:${PORT}`;
const data = fs.mkdtempSync(path.join(os.tmpdir(), 'obs-np-radio-'));
const radioFile = path.join(data, 'al-aire.txt');
fs.writeFileSync(path.join(data, 'config.json'), JSON.stringify({ port: PORT, textSource: { path: 'al-aire.txt' } }));
const env = { ...process.env, OBS_NOW_PLAYING_DATA: data, OBS_NOW_PLAYING_HELPER: path.join(__dirname, 'silent-helper.js') };

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const get = (p, init) => fetch(BASE + p, init);
const nowPlaying = async () => (await (await get('/api/now-playing')).json()).track;

async function waitFor(check, what) {
  for (let i = 0; i < 60; i++) {
    try {
      if (await check()) return;
    } catch {}
    await sleep(100);
  }
  throw new Error(`Tiempo agotado esperando: ${what}`);
}

async function main() {
  const server = spawn(process.execPath, [path.join(ROOT, 'server.js')], { env, stdio: 'inherit' });
  try {
    await waitFor(async () => (await get('/')).ok, 'que arranque el servidor');
    assert.equal(await nowPlaying(), null, 'sin archivo no suena nada');
    assert.match(await (await get('/')).text(), /Radio \(archivo de texto\)[\s\S]*no existe todavía/);
    const config = JSON.parse(fs.readFileSync(path.join(data, 'config.json'), 'utf8'));
    assert.equal(config.textSource.separator, ' - ', 'los valores por defecto se completan');

    // Windows-1252, como lo escriben muchos programas de radio.
    fs.writeFileSync(radioFile, Buffer.from('Artista - Canción\r\n', 'latin1'));
    await waitFor(async () => (await nowPlaying())?.title === 'Canción', 'la canción del archivo');
    const track = await nowPlaying();
    assert.equal(track.artist, 'Artista');
    assert.equal(track.kind, 'radio');
    assert.equal(track.sourceLabel, 'Radio');
    assert.equal(track.artwork, '');
    assert.equal(track.durationMs, 0);
    assert.equal((await get('/api/control', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: '{"action":"next"}' })).status, 409);

    // Sin latido propio: se sigue refrescando para no caducar.
    await sleep(2500);
    assert.ok((await nowPlaying()).progressMs < 1500, 'la pista se refresca en cada sondeo');

    // Otra canción (reemplazando el archivo) y luego archivo vacío.
    fs.writeFileSync(`${radioFile}.tmp`, 'Otro Artista - Otra Canción');
    fs.renameSync(`${radioFile}.tmp`, radioFile);
    await waitFor(async () => (await nowPlaying())?.title === 'Otra Canción', 'el cambio de canción');
    fs.writeFileSync(radioFile, '');
    await waitFor(async () => (await nowPlaying()) === null, 'que el archivo vacío oculte la canción');
    console.log('✔ fuente de radio (archivo) OK');
  } finally {
    if (server.exitCode === null) server.kill();
    fs.rmSync(data, { recursive: true, force: true });
  }
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
