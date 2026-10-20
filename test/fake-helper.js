// Lector falso con el mismo protocolo que media/windows.ps1 y media/macos.js:
// Spotify con dos canciones (con carátula al cambiar) y Chrome en pausa. Atiende los
// comandos del archivo .command igual que el lector de Windows.
'use strict';
const fs = require('node:fs');
const path = require('node:path');

const COMMAND_PATH = path.join(process.env.OBS_NOW_PLAYING_DATA, '.command');
const PNG_RED =
  'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==';
const SONGS = [
  { title: 'Ojitos Lindos', artist: 'Bad Bunny, Bomba Estéreo', album: 'Un Verano Sin Ti', durationMs: 258000 },
  { title: 'La Bachata', artist: 'Manuel Turizo', album: '2000', durationMs: 162000 },
];

let song = 0;
let playing = true;
let progress = 30000;
let sentArt = -1;
const emit = (obj) => process.stdout.write(JSON.stringify(obj) + '\n');

function handleCommand() {
  if (!fs.existsSync(COMMAND_PATH)) return;
  const { cmd } = JSON.parse(fs.readFileSync(COMMAND_PATH, 'utf8'));
  fs.rmSync(COMMAND_PATH);
  if (cmd === 'next' || cmd === 'previous') {
    song = (song + (cmd === 'next' ? 1 : SONGS.length - 1)) % SONGS.length;
    progress = 0;
  }
  if (cmd === 'playpause') playing = !playing;
}

function loop() {
  handleCommand();
  if (playing) progress += 1000;
  const spotify = { app: 'Spotify.exe', ...SONGS[song], isPlaying: playing, current: true, progressMs: progress };
  if (sentArt !== song) {
    spotify.artwork = `data:image/png;base64,${PNG_RED}`;
    sentArt = song;
  }
  emit({
    sessions: [
      spotify,
      { app: 'chrome.exe', title: 'Un video', artist: 'Canal', isPlaying: false, current: false, progressMs: 0, durationMs: 0 },
    ],
  });
}

loop();
setInterval(loop, 200);
