// Lee "Ahora suena" de macOS (lo mismo que muestra el Centro de control) y escribe
// una línea JSON por segundo en la salida estándar; server.js la lee. Mismo
// protocolo que media/windows.ps1.
//
// Corre con: /usr/bin/osascript -l JavaScript media/macos.js
//
// Desde macOS 15.4 Apple solo deja leer MediaRemote a programas del propio sistema;
// osascript lo es, por eso esto va en JavaScript for Automation y no en Node.
ObjC.import('Foundation');
ObjC.import('stdlib');

function main() {
  const stdout = $.NSFileHandle.fileHandleWithStandardOutput;
  const emit = (obj) => stdout.writeData($(JSON.stringify(obj) + '\n').dataUsingEncoding($.NSUTF8StringEncoding));

  const bundle = $.NSBundle.bundleWithPath('/System/Library/PrivateFrameworks/MediaRemote.framework/');
  if (!bundle.load) {
    emit({ error: 'No se pudo cargar MediaRemote de macOS.' });
    return 2;
  }
  const Request = $.NSClassFromString('MRNowPlayingRequest');
  if (!Request || (Request.isNil && Request.isNil())) {
    emit({ error: 'Esta versión de macOS no permite leer "Ahora suena".' });
    return 3;
  }

  const str = (v) => (v && !v.isNil() ? ObjC.unwrap(v) : '');

  // Igual que en Windows: la carátula solo se manda al cambiar de canción, y si llega
  // la misma imagen de la canción anterior se reintenta (la app aún no la actualizó).
  let sentKey = '';
  let sentHash = '';
  let tries = 0;
  let failures = 0;

  // ~30 min y salimos: server.js nos vuelve a abrir (evita acumular memoria).
  for (let i = 0; i < 1800; i++) {
    try {
      const item = Request.localNowPlayingItem;
      const info = item && !item.isNil() ? item.nowPlayingInfo : null;
      const value = (k) => {
        const v = info.objectForKey(k);
        return v.isNil() ? null : ObjC.unwrap(v);
      };
      const title = info && !info.isNil() ? value('kMRMediaRemoteNowPlayingInfoTitle') : null;

      if (!title) {
        emit({ sessions: [] });
      } else {
        const client = Request.localNowPlayingPlayerPath.client;
        const app = str(client.bundleIdentifier) || 'desconocida';
        const artist = value('kMRMediaRemoteNowPlayingInfoArtist') || '';
        const rate = Number(value('kMRMediaRemoteNowPlayingInfoPlaybackRate')) || 0;
        let playing = rate > 0;
        try {
          if (typeof Request.localIsPlaying === 'boolean') playing = Request.localIsPlaying;
        } catch (e) {
          // versiones sin esa propiedad: queda la velocidad de reproducción
        }

        // El tiempo transcurrido vale para Timestamp: lo llevamos a "ahora".
        let elapsed = (Number(value('kMRMediaRemoteNowPlayingInfoElapsedTime')) || 0) * 1000;
        const stamp = value('kMRMediaRemoteNowPlayingInfoTimestamp');
        if (playing && stamp instanceof Date) elapsed += (Date.now() - stamp.getTime()) * (rate || 1);

        const entry = {
          app,
          name: str(client.displayName),
          title: String(title),
          artist: String(artist),
          album: String(value('kMRMediaRemoteNowPlayingInfoAlbum') || ''),
          isPlaying: playing,
          current: true,
          progressMs: Math.max(0, Math.round(elapsed)),
          durationMs: Math.max(0, Math.round((Number(value('kMRMediaRemoteNowPlayingInfoDuration')) || 0) * 1000)),
        };

        const key = `${app}\n${entry.title}\n${entry.artist}`;
        if (key !== sentKey) {
          tries++;
          let art = '';
          let hash = '';
          const data = info.objectForKey('kMRMediaRemoteNowPlayingInfoArtworkData');
          if (!data.isNil() && data.length > 0) {
            const b64 = ObjC.unwrap(data.base64EncodedStringWithOptions(0));
            const mime = value('kMRMediaRemoteNowPlayingInfoArtworkMIMEType') || 'image/jpeg';
            art = `data:${mime};base64,${b64}`;
            hash = `${b64.length}:${b64.slice(-96)}`;
          }
          if ((art && hash !== sentHash) || tries >= 5) {
            entry.artwork = art;
            sentKey = key;
            sentHash = hash;
            tries = 0;
          }
        }
        emit({ sessions: [entry] });
      }
      failures = 0;
    } catch (e) {
      emit({ error: String(e) });
      if (++failures >= 5) return 4;
    }
    delay(1);
  }
  return 0;
}

const code = main();
if (code) $.exit(code);
