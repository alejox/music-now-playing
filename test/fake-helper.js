// Lector falso con el mismo protocolo que media/windows.ps1 y media/macos.js:
// Spotify sonando (con carátula la primera vez) y Chrome en pausa.
'use strict';
const PNG_1PX =
  'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==';
let tick = 0;
const emit = (obj) => process.stdout.write(JSON.stringify(obj) + '\n');
function loop() {
  tick++;
  const spotify = {
    app: 'Spotify.exe', title: 'Ojitos Lindos', artist: 'Bad Bunny, Bomba Estéreo', album: 'Un Verano Sin Ti',
    isPlaying: true, current: true, progressMs: 30000 + tick * 1000, durationMs: 258000,
  };
  if (tick === 1) spotify.artwork = `data:image/png;base64,${PNG_1PX}`;
  emit({
    sessions: [
      spotify,
      { app: 'chrome.exe', title: 'Un video', artist: 'Canal', isPlaying: false, current: false, progressMs: 0, durationMs: 0 },
    ],
  });
}
loop();
setInterval(loop, 1000);
