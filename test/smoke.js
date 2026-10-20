// Prueba de humo: levanta el servidor con el lector falso y revisa lo esencial.
//   npm test
'use strict';
const { spawn } = require('node:child_process');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const ROOT = path.join(__dirname, '..');
const PORT = 18787;
const BASE = `http://127.0.0.1:${PORT}`;
const data = fs.mkdtempSync(path.join(os.tmpdir(), 'obs-np-test-'));
fs.writeFileSync(path.join(data, 'config.json'), JSON.stringify({ port: PORT, spotifyClientId: 'viejo' }));
const env = { ...process.env, OBS_NOW_PLAYING_DATA: data, OBS_NOW_PLAYING_HELPER: path.join(__dirname, 'fake-helper.js') };

// fetch no deja cambiar Host: para simular DNS rebinding usamos http directamente.
const statusWithHost = (host) =>
  new Promise((resolve, reject) => {
    require('node:http')
      .get({ host: '127.0.0.1', port: PORT, path: '/', headers: { Host: host } }, (res) => {
        res.resume();
        resolve(res.statusCode);
      })
      .on('error', reject);
  });
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const get = (p, init) => fetch(BASE + p, init);
const nowPlaying = async () => (await (await get('/api/now-playing')).json()).track;

async function waitFor(check, what) {
  for (let i = 0; i < 50; i++) {
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
    await waitFor(async () => (await nowPlaying())?.title, 'una canción del lector');

    const track = await nowPlaying();
    assert.equal(track.title, 'Ojitos Lindos');
    assert.equal(track.kind, 'spotify');
    assert.equal(track.sourceLabel, 'Spotify');
    assert.ok(track.progressMs >= 30000);

    const art = await get(track.artwork);
    assert.equal(art.status, 200);
    assert.equal(art.headers.get('content-type'), 'image/png');

    const config = JSON.parse(fs.readFileSync(path.join(data, 'config.json'), 'utf8'));
    assert.equal(config.spotifyClientId, undefined, 'la config de v1 se migra');
    await waitFor(() => fs.readFileSync(path.join(data, 'now-playing.txt'), 'utf8') === 'Bad Bunny, Bomba Estéreo - Ojitos Lindos', 'el archivo de texto');

    // Seguridad: sin CORS, Host ajeno rechazado, formularios sin token rechazados.
    assert.equal((await get('/api/now-playing', { headers: { Origin: 'https://evil.example' } })).headers.get('access-control-allow-origin'), null);
    assert.equal(await statusWithHost('evil.example'), 403);
    assert.equal((await get('/apps', { method: 'POST', body: 'app=Spotify.exe' })).status, 403);
    assert.equal((await get('/api/browser', { method: 'POST', body: '{}', headers: { 'Content-Type': 'text/plain' } })).status, 415);

    // Controles: siguiente, pausa y reanudar llegan al lector.
    const control = (action, type = 'application/json') =>
      get('/api/control', { method: 'POST', headers: { 'Content-Type': type }, body: JSON.stringify({ action }) });
    assert.equal((await control('next', 'text/plain')).status, 415, 'otras páginas no pueden mandar comandos');
    assert.equal((await control('borrar')).status, 400);
    assert.equal((await control('next')).status, 200);
    await waitFor(async () => (await nowPlaying())?.title === 'La Bachata', 'que "siguiente" cambie la canción');
    await control('playpause');
    await waitFor(async () => (await nowPlaying()) === null, 'que la pausa oculte el overlay');
    const paused = (await (await get('/api/now-playing?paused=1')).json()).track;
    assert.equal(paused.title, 'La Bachata', 'el panel de controles sigue viendo la canción en pausa');
    assert.equal(paused.isPlaying, false);
    await control('playpause');
    await waitFor(async () => (await nowPlaying())?.isPlaying === true, 'que "reproducir" la reanude');
    for (const page of ['/controls', '/ui.js', '/overlay']) assert.equal((await get(page)).status, 200, page);

    // Ocultar una app desde el panel la saca del overlay.
    const token = fs.readFileSync(path.join(data, '.control'), 'utf8');
    const form = { method: 'POST', redirect: 'manual', headers: { 'Content-Type': 'application/x-www-form-urlencoded' } };
    assert.equal((await get('/apps', { ...form, body: `token=${token}&app=Spotify.exe` })).status, 303);
    assert.equal(await nowPlaying(), null);
    await get('/apps', { ...form, body: `token=${token}&app=Spotify.exe` });

    // Plan B: si la misma canción llega por el userscript, gana su etiqueta.
    await get('/api/browser', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ source: 'ytmusic', title: 'Ojitos Lindos', artist: 'Bad Bunny', isPlaying: true }),
    });
    assert.equal((await nowPlaying()).sourceLabel, 'YouTube Music');

    // ...y los botones le llegan al userscript en la respuesta de su siguiente latido.
    assert.equal((await control('next')).status, 200);
    const beat = await get('/api/browser', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ source: 'ytmusic', title: 'Ojitos Lindos', artist: 'Bad Bunny', isPlaying: true }),
    });
    assert.deepEqual(await beat.json(), { cmd: 'next' });

    // --stop cierra la app (lo usan instalador y desinstalador).
    const stop = spawn(process.execPath, [path.join(ROOT, 'server.js'), '--stop'], { env, stdio: 'inherit' });
    await new Promise((r) => stop.on('exit', r));
    await waitFor(async () => server.exitCode !== null, 'que la app se cierre con --stop');
    console.log('✔ prueba de humo OK');
  } finally {
    if (server.exitCode === null) server.kill();
    fs.rmSync(data, { recursive: true, force: true });
  }
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
