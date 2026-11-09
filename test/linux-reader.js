// Prueba del lector de Linux del plugin (obs-plugin/data/media/linux.sh) con un playerctl falso (test/fake-playerctl.sh):
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
const READER = path.join(ROOT, 'obs-plugin', 'data', 'media', 'linux.sh');
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'obs-np-linux-'));
const bin = path.join(tmp, 'bin');
fs.mkdirSync(bin);
fs.copyFileSync(path.join(__dirname, 'fake-playerctl.sh'), path.join(bin, 'playerctl'));
fs.chmodSync(path.join(bin, 'playerctl'), 0o755);
// pactl y busctl falsos van aparte: solo las pruebas del mezclador los ponen (PACTL / BUSCTL).
const fakePactl = path.join(tmp, 'pactl');
fs.copyFileSync(path.join(__dirname, 'fake-pactl.sh'), fakePactl);
fs.chmodSync(fakePactl, 0o755);
const fakeBusctl = path.join(tmp, 'busctl');
fs.copyFileSync(path.join(__dirname, 'fake-busctl.sh'), fakeBusctl);
fs.chmodSync(fakeBusctl, 0o755);
const missing = path.join(tmp, 'no-existe');
const pactlDir = path.join(tmp, 'pactl-state');
const runtimeDir = path.join(tmp, 'runtime');
const volumeFile = path.join(tmp, 'mpris-volume');
// Procesos de prueba: 5001 (el audio de Firefox) es hijo de 5000 (Firefox).
const procDir = path.join(tmp, 'proc');
for (const [pid, comm, ppid] of [
  [5000, 'firefox', 1],
  [5001, 'Socket Process', 5000],
]) {
  fs.mkdirSync(path.join(procDir, String(pid)), { recursive: true });
  fs.writeFileSync(path.join(procDir, String(pid), 'stat'), `${pid} (${comm}) S ${ppid} ${pid} ${pid} 0 -1\n`);
}

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
  fs.rmSync(volumeFile, { force: true });
  fs.mkdirSync(runtimeDir, { recursive: true });
  fs.mkdirSync(pactlDir, { recursive: true });
  const r = spawnSync('bash', [READER, ...args], {
    encoding: 'utf8',
    timeout: 20000,
    env: {
      ...process.env,
      PATH: `${bin}${path.delimiter}${process.env.PATH}`,
      OBS_NOW_PLAYING_LOOPS: '2',
      FAKE_ART: art,
      FAKE_PLAYERCTL_LOG: log,
      FAKE_PLAYERCTL_VOLUME_FILE: volumeFile,
      FAKE_PACTL_DIR: pactlDir,
      XDG_RUNTIME_DIR: runtimeDir,
      OBS_NOW_PLAYING_PROC: procDir,
      // Sin mezclador salvo que la prueba lo pida (el de la máquina no entra).
      PACTL: missing,
      BUSCTL: missing,
      DBUS_SEND: missing,
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

  // ---- Una carátula de internet lenta no frena la lectura ----
  // curl falso (primero en el PATH) que tarda 2 s y "baja" la carátula de prueba. La línea
  // sale cada segundo sin esperarlo y la carátula llega en una posterior.
  const slowBin = path.join(tmp, 'slow-bin');
  fs.mkdirSync(slowBin);
  fs.writeFileSync(
    path.join(slowBin, 'curl'),
    '#!/usr/bin/env bash\nout=\'\'\nwhile [ $# -gt 0 ]; do\n  if [ "$1" = -o ]; then out=$2; shift; fi\n  shift\ndone\nsleep 2\ncp "$FAKE_ART" "$out"\n',
  );
  fs.chmodSync(path.join(slowBin, 'curl'), 0o755);
  const startedAt = Date.now();
  const slow = run([], {
    OBS_NOW_PLAYING_LOOPS: '5',
    FAKE_ART_URL: 'https://covers.invalid/cover.png',
    PATH: `${slowBin}${path.delimiter}${bin}${path.delimiter}${process.env.PATH}`,
  });
  const slowMs = Date.now() - startedAt;
  assert.equal(slow.code, 0, slow.stderr);
  assert.equal(slow.lines.length, 5, 'una línea por vuelta');
  const slowSpotify = slow.lines.map((l) => l.sessions.find((s) => s.app === 'spotify'));
  assert.ok(slowSpotify.every(Boolean), 'Spotify está en todas las líneas');
  assert.ok(!('artwork' in slowSpotify[0]), 'la primera línea no espera a la carátula');
  assert.equal(slowSpotify.filter((s) => 'artwork' in s).length, 1, 'la carátula va una sola vez');
  assert.equal(slowSpotify.find((s) => 'artwork' in s).artwork, `data:image/png;base64,${PNG}`);
  // 5 vueltas de 1 s: esperar a curl (2 s) en la primera las llevaría a más de 7 s.
  assert.ok(slowMs < 6500, `la lectura esperó a la descarga (${slowMs} ms)`);

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

  // Volumen y aleatorio (dock): 0–100 y On/Off de playerctl → {"state":…}.
  r = run(['command', 'state'], { OBS_NOW_PLAYING_PLAYER: 'spotify' });
  assert.equal(r.code, 0);
  assert.deepEqual(r.lines.at(-1), {
    ok: true,
    via: 'spotify',
    volumeVia: 'mpris',
    state: { volume: 40, shuffle: true },
  });

  r = run(['command', 'volume', '7'], { OBS_NOW_PLAYING_PLAYER: 'spotify' });
  assert.equal(r.code, 0);
  assert.match(r.calls, /^-p spotify volume 0\.07$/m);

  r = run(['command', 'volume', '250'], { OBS_NOW_PLAYING_PLAYER: 'spotify' });
  assert.match(r.calls, /^-p spotify volume 1\.00$/m);

  r = run(['command', 'shuffle'], { OBS_NOW_PLAYING_PLAYER: 'spotify' });
  assert.equal(r.code, 0);
  assert.match(r.calls, /^-p spotify shuffle Toggle$/m);

  // Un reproductor que no deja: state no sabe nada; volume avisa.
  r = run(['command', 'state'], { OBS_NOW_PLAYING_PLAYER: 'firefox' });
  assert.deepEqual(r.lines.at(-1).state, { volume: null, shuffle: null });
  r = run(['command', 'volume', '50'], { OBS_NOW_PLAYING_PLAYER: 'firefox' });
  assert.equal(r.code, 1);
  assert.match(r.lines.at(-1).error, /volumen/);

  // ---- Volumen por el mezclador de PulseAudio/PipeWire ----
  const pulse = { PACTL: fakePactl, BUSCTL: fakeBusctl };
  const resetPulse = () => {
    fs.rmSync(pactlDir, { recursive: true, force: true });
    fs.rmSync(runtimeDir, { recursive: true, force: true });
  };

  // (b) El reproductor aplica el volumen de MPRIS: no se toca el mezclador.
  resetPulse();
  r = run(['command', 'volume', '7'], { OBS_NOW_PLAYING_PLAYER: 'spotify', ...pulse });
  assert.equal(r.code, 0, r.stderr);
  assert.equal(r.lines.at(-1).volumeVia, 'mpris');
  assert.equal(r.lines.at(-1).state.volume, 7);
  assert.doesNotMatch(r.calls, /^pactl /m);

  // (a) Spotify acepta el volumen de MPRIS pero no lo aplica: va al flujo de audio de su pid.
  resetPulse();
  r = run(['command', 'volume', '55'], { OBS_NOW_PLAYING_PLAYER: 'spotify', FAKE_PLAYERCTL_VOLUME: 'ignore', ...pulse });
  assert.equal(r.code, 0, r.stderr);
  assert.equal(r.lines.at(-1).ok, true);
  assert.equal(r.lines.at(-1).volumeVia, 'pulse');
  assert.equal(r.lines.at(-1).state.volume, 55);
  assert.match(r.calls, /^-p spotify volume 0\.55$/m, 'primero prueba MPRIS');
  assert.match(r.calls, /^busctl .*GetConnectionUnixProcessID s org\.mpris\.MediaPlayer2\.spotify$/m);
  assert.match(r.calls, /^pactl set-sink-input-volume 7 55%$/m);
  assert.doesNotMatch(r.calls, /^pactl set-sink-input-volume (8|9) /m, 'solo el flujo de Spotify');
  // La barra lee lo mismo: el nivel del mezclador, no el 40 que dice MPRIS.
  r = run(['command', 'state'], { OBS_NOW_PLAYING_PLAYER: 'spotify', FAKE_PLAYERCTL_VOLUME: 'ignore', ...pulse });
  assert.deepEqual(r.lines.at(-1), {
    ok: true,
    via: 'spotify',
    volumeVia: 'pulse',
    state: { volume: 55, shuffle: true },
  });

  // Sin el pid (sin busctl ni dbus-send): por el nombre del programa.
  resetPulse();
  r = run(['command', 'volume', '20'], {
    OBS_NOW_PLAYING_PLAYER: 'spotify',
    FAKE_PLAYERCTL_VOLUME: 'ignore',
    PACTL: fakePactl,
  });
  assert.equal(r.code, 0, r.stderr);
  assert.equal(r.lines.at(-1).volumeVia, 'pulse');
  assert.match(r.calls, /^pactl set-sink-input-volume 7 20%$/m);
  assert.doesNotMatch(r.calls, /^pactl set-sink-input-volume (8|9) /m);

  // Firefox (sin volumen MPRIS): el audio sale de un proceso hijo.
  resetPulse();
  r = run(['command', 'volume', '50'], { OBS_NOW_PLAYING_PLAYER: 'firefox', ...pulse });
  assert.equal(r.code, 0, r.stderr);
  assert.equal(r.lines.at(-1).volumeVia, 'pulse');
  assert.equal(r.lines.at(-1).state.volume, 50);
  assert.match(r.calls, /^pactl set-sink-input-volume 8 50%$/m);
  assert.doesNotMatch(r.calls, /^pactl set-sink-input-volume (7|9) /m);
  r = run(['command', 'state'], { OBS_NOW_PLAYING_PLAYER: 'firefox', ...pulse });
  assert.equal(r.lines.at(-1).state.volume, 50);
  assert.equal(r.lines.at(-1).volumeVia, 'pulse');

  // (c) Sin pactl, como antes: Firefox no deja y un volumen que no se aplica no es "ok".
  resetPulse();
  r = run(['command', 'volume', '50'], { OBS_NOW_PLAYING_PLAYER: 'firefox' });
  assert.equal(r.code, 1);
  assert.match(r.lines.at(-1).error, /volumen/);
  r = run(['command', 'state'], { OBS_NOW_PLAYING_PLAYER: 'firefox' });
  assert.deepEqual(r.lines.at(-1).state, { volume: null, shuffle: null });
  r = run(['command', 'volume', '55'], { OBS_NOW_PLAYING_PLAYER: 'spotify', FAKE_PLAYERCTL_VOLUME: 'ignore' });
  assert.equal(r.code, 1);
  assert.match(r.lines.at(-1).error, /volumen/);

  r = run(['command', 'bailar']);
  assert.equal(r.code, 2);
  assert.match(r.lines.at(-1).error, /desconocido/);

  // ---- Sin playerctl: un error del protocolo que dice qué instalar ----
  r = run([], { PLAYERCTL: missing });
  assert.equal(r.code, 2);
  assert.match(r.lines[0].error, /playerctl/);

  console.log('Lector de Linux: OK');
} finally {
  fs.rmSync(tmp, { recursive: true, force: true });
}
