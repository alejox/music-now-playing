// Prueba del lector de Linux (media/linux.sh) con un playerctl falso (test/fake-playerctl.sh):
// protocolo JSON, textos con comillas / barras / tabs / acentos, carátula, modo video y
// comandos. Corre con el bash del sistema (también el 3.2 de macOS).
//   node test/linux-reader.js
'use strict';
const { spawnSync } = require('node:child_process');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

if (process.platform === 'win32') {
  console.log('Lector de Linux: se omite en Windows (necesita bash).');
  process.exit(0);
}

const ROOT = path.join(__dirname, '..');
const READER = path.join(ROOT, 'media', 'linux.sh');
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'obs-np-linux-'));
const bin = path.join(tmp, 'bin');
fs.mkdirSync(bin);
fs.copyFileSync(path.join(__dirname, 'fake-playerctl.sh'), path.join(bin, 'playerctl'));
fs.chmodSync(path.join(bin, 'playerctl'), 0o755);

// Una carátula PNG de 1×1 en una carpeta con espacio (llega como file://…%20…).
const PNG =
  'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==';
const artDir = path.join(tmp, 'mis carátulas');
fs.mkdirSync(artDir);
const art = path.join(artDir, 'cover.png');
fs.writeFileSync(art, Buffer.from(PNG, 'base64'));
const log = path.join(tmp, 'calls.log');

function run(args, extraEnv = {}) {
  fs.writeFileSync(log, '');
  const r = spawnSync('bash', [READER, ...args], {
    encoding: 'utf8',
    timeout: 20000,
    env: {
      ...process.env,
      PATH: `${bin}${path.delimiter}${process.env.PATH}`,
      OBS_NOW_PLAYING_LOOPS: '2',
      FAKE_ART: art,
      FAKE_PLAYERCTL_LOG: log,
      ...extraEnv,
    },
  });
  const lines = r.stdout.split('\n').filter(Boolean).map((l) => JSON.parse(l));
  return { code: r.status, lines, calls: fs.readFileSync(log, 'utf8'), stderr: r.stderr };
}

try {
  // ---- Lectura ----
  const watch = run([]);
  assert.equal(watch.code, 0, watch.stderr);
  assert.equal(watch.lines.length, 2, 'una línea por vuelta');
  const [first, second] = watch.lines;
  assert.equal(first.sessions.length, 2);
  const spotify = first.sessions.find((s) => s.app === 'spotify');
  const firefox = first.sessions.find((s) => s.app === 'firefox');
  assert.ok(spotify && firefox, JSON.stringify(first));
  assert.equal(spotify.name, 'Spotify');
  assert.equal(spotify.title, 'Say "Hi" \\ Bye\tnow');
  assert.equal(spotify.artist, 'Bomba Estéreo, Bad Bunny');
  assert.equal(spotify.album, 'Canción ñ 🎵');
  assert.equal(spotify.isPlaying, true);
  assert.equal(spotify.current, true);
  assert.equal(spotify.progressMs, 61500, 'microsegundos → ms');
  assert.equal(spotify.durationMs, 258000);
  assert.equal(spotify.artwork, `data:image/png;base64,${PNG}`);
  assert.equal(firefox.name, 'Firefox');
  assert.equal(firefox.isPlaying, false);
  assert.equal(firefox.current, false);
  assert.equal(firefox.durationMs, 0);
  assert.equal(firefox.artist, '');
  assert.ok(!('videoId' in spotify), 'sin modo video no hay videoId');
  // La carátula solo va al cambiar de canción (Firefox, sin carátula, la manda vacía al 5.º intento).
  assert.ok(!('artwork' in second.sessions.find((s) => s.app === 'spotify')));
  assert.match(watch.calls, /--all-players metadata --format/);

  // ---- Nada suena ----
  const empty = run([], { FAKE_PLAYERCTL_MODE: 'empty' });
  assert.equal(empty.code, 0);
  assert.deepEqual(empty.lines[0], { sessions: [] });

  // ---- Modo video: el id de la dirección de YouTube Music que anuncia el navegador ----
  const flag = path.join(tmp, 'video.flag');
  fs.writeFileSync(flag, '');
  const video = run(['watch', flag], { FAKE_PLAYERCTL_MODE: 'youtube' });
  const yt = video.lines[0].sessions[0];
  assert.equal(yt.app, 'chromium');
  assert.equal(yt.name, 'Chromium');
  assert.equal(yt.videoId, 'dQw4w9WgXcQ');
  assert.equal(yt.site, 'ytmusic');

  // ---- Comandos ----
  let r = run(['command', 'next'], { OBS_NOW_PLAYING_PLAYER: 'spotify' });
  assert.equal(r.code, 0);
  assert.deepEqual(r.lines.at(-1), { ok: true, via: 'spotify' });
  assert.match(r.calls, /^-p spotify next$/m);

  // Por nombre ("firefox") se usa su instancia.
  r = run(['command', 'playpause'], { OBS_NOW_PLAYING_PLAYER: 'firefox' });
  assert.equal(r.code, 0);
  assert.match(r.calls, /^-p firefox\.instance_1_23 play-pause$/m);

  r = run(['command', 'seek', '61000'], { OBS_NOW_PLAYING_PLAYER: 'spotify' });
  assert.equal(r.code, 0);
  assert.match(r.calls, /^-p spotify position 61\.000$/m);

  r = run(['command', 'seek', '5000'], { OBS_NOW_PLAYING_PLAYER: 'firefox' });
  assert.equal(r.code, 1);
  assert.match(r.lines.at(-1).error, /adelantar/);

  r = run(['command', 'bailar']);
  assert.equal(r.code, 2);
  assert.match(r.lines.at(-1).error, /desconocido/);

  // ---- Sin playerctl: un error del protocolo que dice qué instalar ----
  r = run([], { PLAYERCTL: path.join(tmp, 'no-existe') });
  assert.equal(r.code, 2);
  assert.match(r.lines[0].error, /playerctl/);

  console.log('Lector de Linux: OK');
} finally {
  fs.rmSync(tmp, { recursive: true, force: true });
}
