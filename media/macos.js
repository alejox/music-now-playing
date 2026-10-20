// Lee "Ahora suena" de macOS (lo mismo que muestra el Centro de control) y escribe
// una línea JSON por segundo en la salida estándar; server.js la lee. Mismo
// protocolo que media/windows.ps1.
//
// Corre con: /usr/bin/osascript -l JavaScript media/macos.js
// Para controlar el reproductor: ... media/macos.js command next   (previous | playpause | next)
// Para probar la búsqueda de carátulas: ... media/macos.js artwork "Canción" "Artista"
// Para probar el modo video con lo que suena: ... media/macos.js video
//
// Desde macOS 15.4 Apple solo deja leer MediaRemote a programas del propio sistema;
// osascript lo es, por eso esto va en JavaScript for Automation y no en Node.
ObjC.import('Foundation');
ObjC.import('stdlib');

const stdout = $.NSFileHandle.fileHandleWithStandardOutput;
const emit = (obj) => stdout.writeData($(JSON.stringify(obj) + '\n').dataUsingEncoding($.NSUTF8StringEncoding));

function loadMediaRemote() {
  return $.NSBundle.bundleWithPath('/System/Library/PrivateFrameworks/MediaRemote.framework/').load;
}

// Códigos de MRMediaRemoteSendCommand: el mismo comando que mandan las teclas multimedia.
const COMMANDS = { playpause: 2, next: 4, previous: 5 };

function sendCommand(action) {
  if (!(action in COMMANDS)) {
    emit({ error: `Comando desconocido: ${action}` });
    return 2;
  }
  if (!loadMediaRemote()) {
    emit({ error: 'No se pudo cargar MediaRemote de macOS.' });
    return 3;
  }
  ObjC.bindFunction('MRMediaRemoteSendCommand', ['bool', ['int', 'id']]);
  // El segundo argumento son opciones: un diccionario vacío (con null macOS falla).
  if (!$.MRMediaRemoteSendCommand(COMMANDS[action], $.NSDictionary.dictionary)) {
    emit({ error: 'macOS no aceptó el comando (¿no hay nada sonando?).' });
    return 1;
  }
  emit({ ok: true });
  return 0;
}

// macOS nuevos (visto en macOS 27) ya no mandan la imagen en "Ahora suena": solo un
// identificador y su tamaño. A la app de Spotify se la pedimos directamente: da el
// enlace de la carátula de la canción que suena. La primera vez macOS pregunta si OBS
// (o la app) puede controlar Spotify; si se le dice que no, simplemente no hay carátula.
function spotifyArtwork(title) {
  try {
    const spotify = Application('com.spotify.client');
    if (!spotify.running()) return '';
    const track = spotify.currentTrack;
    if (String(track.name()) !== title) return ''; // Spotify aún no cambió: se reintenta
    const url = String(track.artworkUrl() || '');
    if (!/^https:\/\//.test(url)) return '';
    const data = $.NSData.dataWithContentsOfURL($.NSURL.URLWithString(url));
    if (!data || data.isNil() || !data.length) return '';
    return `data:image/jpeg;base64,${ObjC.unwrap(data.base64EncodedStringWithOptions(0))}`;
  } catch (e) {
    return '';
  }
}

// Último recurso (YouTube Music, navegadores…): buscar la carátula por artista y
// canción en el catálogo público de Apple (iTunes Search, sin cuenta ni clave). Solo
// se acepta si artista y título coinciden, para no mostrar una carátula equivocada.
const lookups = {}; // "artista\ncanción" → carátula ('' = no se encontró), una búsqueda por canción

function fetchData(url) {
  const data = $.NSData.dataWithContentsOfURL($.NSURL.URLWithString(url));
  return data && !data.isNil() && data.length ? data : null;
}

function catalogArtwork(title, artist) {
  const key = `${artist}\n${title}`;
  if (key in lookups) return lookups[key];
  lookups[key] = '';
  // "Canción (Official Video)" o "Artista - Topic" son cosas de YouTube, no del disco.
  const clean = (s) => s.replace(/\s*[([][^)\]]*[)\]]/g, '').replace(/\s+-\s+topic$/i, '').trim();
  const norm = (s) => s.toLowerCase().normalize('NFD').replace(/[\u0300-\u036f]/g, '').replace(/[^a-z0-9]/g, '');
  const a = norm(clean(artist));
  const t = norm(clean(title));
  if (!a || !t) return '';
  try {
    const term = encodeURIComponent(`${clean(artist)} ${clean(title)}`);
    const json = fetchData(`https://itunes.apple.com/search?media=music&entity=song&limit=10&term=${term}`);
    if (!json) return '';
    const text = ObjC.unwrap($.NSString.alloc.initWithDataEncoding(json, $.NSUTF8StringEncoding));
    const hit = (JSON.parse(text).results || []).find((r) => {
      const ra = norm(r.artistName || '');
      const rt = norm(r.trackName || '');
      return ra && rt && (ra.includes(a) || a.includes(ra)) && (rt.includes(t) || t.includes(rt));
    });
    if (!hit || !hit.artworkUrl100) return '';
    const image = fetchData(hit.artworkUrl100.replace(/\/\d+x\d+bb\./, '/600x600bb.'));
    if (image) lookups[key] = `data:image/jpeg;base64,${ObjC.unwrap(image.base64EncodedStringWithOptions(0))}`;
  } catch (e) {
    // sin internet o respuesta rara: sin carátula
  }
  return lookups[key];
}

// Modo video: el id del video de YouTube / YouTube Music que suena, leído de la dirección
// de la pestaña (o de la app instalada de YouTube Music, que es una ventana del
// navegador). Solo si el plugin lo pidió (existe el archivo videoFlag): leer pestañas
// hace que macOS pregunte si OBS puede controlar el navegador, y eso no debe pasarle a
// quien no eligió "Video si es posible".
const BROWSERS = [
  // [id de la app, cómo se llama el título de la pestaña en su AppleScript]
  ['com.google.Chrome', 'title'],
  ['com.microsoft.edgemac', 'title'],
  ['com.brave.Browser', 'title'],
  ['com.vivaldi.Vivaldi', 'title'],
  ['com.operasoftware.Opera', 'title'],
  ['company.thebrowser.Browser', 'title'],
  ['com.apple.Safari', 'name'],
];
const YOUTUBE_WATCH = /^https:\/\/(?:music\.|www\.|m\.)?youtube\.com\/watch\?(?:[^#]*&)?v=([A-Za-z0-9_-]{11})/;

function youtubeVideoId(app, title) {
  // Las apps instaladas (PWA) llevan el id del navegador como prefijo:
  // com.google.Chrome.app.cinhimbnkkaeohfgghhklpknlkffjgod es YouTube Music en Chrome.
  const browser = BROWSERS.find(([id]) => app === id || app.startsWith(`${id}.`));
  if (!browser) return '';
  try {
    const b = Application(browser[0]);
    if (!b.running()) return '';
    const found = [];
    for (const w of b.windows()) {
      for (const t of w.tabs()) {
        const m = YOUTUBE_WATCH.exec(String(t.url() || ''));
        if (m) found.push({ id: m[1], name: String(t[browser[1]]() || '').toLowerCase() });
      }
    }
    // Con varias pestañas de YouTube, la que tiene el título de la canción.
    const hit = found.find((f) => title && f.name.includes(title.toLowerCase())) || (found.length === 1 ? found[0] : null);
    return hit ? hit.id : '';
  } catch (e) {
    return ''; // sin permiso o navegador sin AppleScript: sin video
  }
}

const videoIds = {}; // "app\ncanción" → id ('' = no se encontró, tras unos intentos)
const videoTries = {};

function videoIdFor(app, title, videoFlag) {
  if (!videoFlag || !$.NSFileManager.defaultManager.fileExistsAtPath(videoFlag)) return '';
  const key = `${app}\n${title}`;
  if (videoIds[key]) return videoIds[key];
  if ((videoTries[key] || 0) >= 3) return ''; // la dirección cambia un instante después del título
  videoTries[key] = (videoTries[key] || 0) + 1;
  const id = youtubeVideoId(app, title);
  if (id) videoIds[key] = id;
  return id;
}

function watch(videoFlag) {
  if (!loadMediaRemote()) {
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
          } else {
            if (app === 'com.spotify.client') art = spotifyArtwork(entry.title);
            if (!art) art = catalogArtwork(entry.title, entry.artist);
            hash = art ? `${art.length}:${art.slice(-96)}` : '';
          }
          if ((art && hash !== sentHash) || tries >= 5) {
            entry.artwork = art;
            sentKey = key;
            sentHash = hash;
            tries = 0;
          }
        }
        const videoId = videoIdFor(app, entry.title, videoFlag);
        if (videoId) entry.videoId = videoId;
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

function testArtwork(title, artist) {
  const art = catalogArtwork(title || '', artist || '');
  emit({ title, artist, found: Boolean(art), bytes: art.length });
  return 0;
}

// Para probar el modo video con lo que suena ahora: ... media/macos.js video
function testVideo() {
  if (!loadMediaRemote()) return 2;
  const Request = $.NSClassFromString('MRNowPlayingRequest');
  const info = Request.localNowPlayingItem.nowPlayingInfo;
  const title = ObjC.unwrap(info.objectForKey('kMRMediaRemoteNowPlayingInfoTitle')) || '';
  const app = ObjC.unwrap(Request.localNowPlayingPlayerPath.client.bundleIdentifier) || '';
  emit({ app, title, videoId: youtubeVideoId(app, title) });
  return 0;
}

function run(argv) {
  const mode = argv[0];
  const code =
    mode === 'command' ? sendCommand(argv[1])
    : mode === 'artwork' ? testArtwork(argv[1], argv[2])
    : mode === 'video' ? testVideo()
    : watch(mode === 'watch' ? argv[1] : ''); // watch <archivo>: el plugin pide modo video
  if (code) $.exit(code);
}
