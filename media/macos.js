// Lee "Ahora suena" de macOS (lo mismo que muestra el Centro de control) y escribe
// una línea JSON por segundo en la salida estándar; server.js la lee. Mismo
// protocolo que media/windows.ps1.
//
// Corre con: /usr/bin/osascript -l JavaScript media/macos.js
// Para controlar el reproductor: ... media/macos.js command next   (previous | playpause | next)
// Para buscar una canción y ponerla: ... media/macos.js search "canción o artista"
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

// Botones anterior / pausa / siguiente.
//
// En macOS 27 el comando de MediaRemote (MRMediaRemoteSendCommand) responde "ok" pero los
// reproductores lo ignoran. Por eso se usa, en orden:
//   1. Spotify y Apple Music: se le pide directo a la app (mismo permiso de Automatización
//      que ya usa la carátula de Spotify).
//   2. El resto (YouTube Music, navegadores…): se simulan las teclas de música del
//      teclado (⏮ ⏯ ⏭), que macOS siempre entrega al reproductor que suena. Simular teclas
//      necesita el permiso de Accesibilidad para OBS (o la Terminal, si se prueba desde ahí).
const ACTIONS = ['previous', 'playpause', 'next'];
const APP_COMMANDS = {
  'com.spotify.client': { previous: 'previousTrack', playpause: 'playpause', next: 'nextTrack' },
  'com.apple.Music': { previous: 'previousTrack', playpause: 'playpause', next: 'nextTrack' },
};
// Códigos NX_KEYTYPE_* de las teclas de música (IOKit/hidsystem/ev_keymap.h).
const MEDIA_KEYS = { playpause: 16, next: 17, previous: 18 };

function nowPlayingApp() {
  if (!loadMediaRemote()) return '';
  try {
    const Request = $.NSClassFromString('MRNowPlayingRequest');
    return ObjC.unwrap(Request.localNowPlayingPlayerPath.client.bundleIdentifier) || '';
  } catch (e) {
    return '';
  }
}

function mediaKeyEvent(code, down) {
  ObjC.import('Cocoa');
  const flags = down ? 0xa00 : 0xb00;
  return $.NSEvent.otherEventWithTypeLocationModifierFlagsTimestampWindowNumberContextSubtypeData1Data2(
    14, // NSEventTypeSystemDefined
    $.NSMakePoint(0, 0),
    flags,
    0,
    0,
    null,
    8, // subtipo de las teclas especiales (NX_SUBTYPE_AUX_CONTROL_BUTTONS)
    (code << 16) | flags,
    -1,
  );
}

function accessibilityAllowed(prompt) {
  ObjC.import('ApplicationServices');
  return $.AXIsProcessTrustedWithOptions($({ AXTrustedCheckOptionPrompt: prompt }));
}

function pressMediaKey(code) {
  // Si OBS no tiene el permiso, macOS descarta las teclas en silencio: mejor avisar (y
  // la primera vez, esto abre el aviso de macOS para darlo).
  if (!accessibilityAllowed(true)) return false;
  for (const down of [true, false]) $.CGEventPost(0, mediaKeyEvent(code, down).CGEvent); // kCGHIDEventTap
  return true;
}

// Para CI: arma las teclas de música sin enviarlas y consulta el permiso (sin pedirlo).
function selfTest() {
  const event = mediaKeyEvent(MEDIA_KEYS.next, true);
  const ok = Boolean(event && !event.isNil() && event.CGEvent);
  // El buscador abre spotify:search: con NSWorkspace (necesita AppKit).
  ObjC.import('AppKit');
  const workspace = Boolean($.NSWorkspace && $.NSWorkspace.sharedWorkspace && !$.NSWorkspace.sharedWorkspace.isNil());
  emit({ mediaKeyEvent: ok, accessibilityAllowed: Boolean(accessibilityAllowed(false)), cgEventPost: typeof $.CGEventPost === 'function', workspace });
  return ok && workspace ? 0 : 1;
}

// YouTube Music / YouTube en un navegador: se pulsa el botón de la propia página, en la
// pestaña que suena (Chrome y compañía: "Permitir JavaScript desde Apple Events" en
// Ver → Opciones para desarrolladores; Safari: lo mismo en el menú Desarrollo). Las teclas
// de música las recibe "la app que suena" según macOS, y esa puede ser otra (p. ej. el
// video de la tarjeta dentro de OBS): por eso no alcanzan para el navegador.
function pageScript(action) {
  return `(() => {
    const music = location.hostname === 'music.youtube.com';
    const selectors = {
      next: music ? 'ytmusic-player-bar .next-button' : '.ytp-next-button',
      previous: music ? 'ytmusic-player-bar .previous-button' : '.ytp-prev-button',
      playpause: music ? 'ytmusic-player-bar #play-pause-button' : '.ytp-play-button',
    };
    const button = document.querySelector(selectors['${action}']);
    if (button && button.offsetParent !== null) { button.click(); return 'ok'; }
    const video = document.querySelector('video');
    if (!video) return 'sin-boton';
    if ('${action}' === 'playpause') { video.paused ? video.play() : video.pause(); return 'ok'; }
    if ('${action}' === 'previous') { video.currentTime = 0; return 'ok'; }
    return 'sin-boton';
  })()`;
}

function browserCommand(app, action) {
  const browser = BROWSERS.find(([id]) => app === id || app.startsWith(`${id}.`));
  // Si macOS cree que "suena" OBS (el video de la tarjeta), la música real está en algún
  // navegador: se prueba en todos los abiertos.
  if (!browser) {
    if (!/obsproject/i.test(app)) return { tried: false };
    let blocked = false;
    for (const [id] of BROWSERS) {
      const r = browserCommandIn(BROWSERS.find(([b]) => b === id), action);
      if (r.ok) return r;
      blocked = blocked || r.blocked;
    }
    return { tried: true, ok: false, blocked };
  }
  return browserCommandIn(browser, action);
}

function browserCommandIn(browser, action) {
  try {
    const b = Application(browser[0]);
    if (!b.running()) return { tried: false };
    const title = String(ObjC.unwrap($.NSClassFromString('MRNowPlayingRequest').localNowPlayingItem.nowPlayingInfo
      .objectForKey('kMRMediaRemoteNowPlayingInfoTitle')) || '').toLowerCase();
    const tabs = [];
    for (const w of b.windows()) {
      for (const t of w.tabs()) {
        if (YOUTUBE_WATCH.test(String(t.url() || ''))) tabs.push({ tab: t, name: String(t[browser[1]]() || '').toLowerCase() });
      }
    }
    const hit = tabs.find((t) => title && t.name.includes(title)) || (tabs.length === 1 ? tabs[0] : null);
    if (!hit) return { tried: false };
    const code = pageScript(action);
    const result = browser[0] === 'com.apple.Safari' ? b.doJavaScript(code, { in: hit.tab }) : hit.tab.execute({ javascript: code });
    return { tried: true, ok: result === 'ok' };
  } catch (e) {
    // El navegador no deja ejecutar JavaScript desde Apple Events (viene apagado).
    return { tried: true, ok: false, blocked: /javascript/i.test(String(e)) };
  }
}

function sendCommand(action) {
  if (!ACTIONS.includes(action)) {
    emit({ error: `Comando desconocido: ${action}` });
    return 2;
  }
  const app = nowPlayingApp();
  const page = browserCommand(app, action);
  if (page.ok) {
    emit({ ok: true, via: 'página de YouTube' });
    return 0;
  }
  const hint = page.blocked
    ? 'Para que los botones vayan siempre a YouTube Music, activa en el navegador: Ver → Opciones para desarrolladores → Permitir JavaScript desde Apple Events.'
    : '';
  const direct = APP_COMMANDS[app];
  if (direct) {
    try {
      const player = Application(app);
      if (player.running()) {
        player[direct[action]]();
        emit({ ok: true, via: app });
        return 0;
      }
    } catch (e) {
      // sin permiso de Automatización: se prueba con las teclas de música
    }
  }
  if (!pressMediaKey(MEDIA_KEYS[action])) {
    emit({
      error: 'Falta el permiso de Accesibilidad: en Ajustes del Sistema → Privacidad y seguridad → Accesibilidad, activa OBS.',
      needsAccessibility: true,
    });
    return 1;
  }
  emit({ ok: true, via: 'teclas de música', ...(hint ? { hint } : {}) });
  return 0;
}

// ---- Buscar una canción (el buscador del dock) -----------------------------------
// Ningún reproductor deja buscar y reproducir sin una cuenta de desarrollador, así que
// depende de cuál suena:
//   - YouTube Music en el navegador: se abre la búsqueda en su pestaña y se pone el
//     primer resultado (mismo permiso que los botones: JavaScript desde Apple Events).
//   - Spotify: se abre la búsqueda en la app y ahí se elige la canción.
//   - Apple Music: se busca en la biblioteca y se pone la primera que coincida.
//   - Nada de eso: se abre la búsqueda de YouTube Music en el navegador.
const YTM_PAGE = /^https:\/\/music\.youtube\.com\//;

function openUrl(url) {
  ObjC.import('AppKit'); // NSWorkspace (Foundation solo no la trae)
  if (!$.NSWorkspace.sharedWorkspace.openURL($.NSURL.URLWithString(url))) throw new Error(`no se pudo abrir ${url}`);
}

// Pone el primer resultado de la búsqueda (la canción del "Mejor resultado" o la
// primera de la lista). 'esperando' mientras la página aún no cargó los resultados.
function firstResultScript(query) {
  return `(() => {
    if (location.pathname !== '/search' || new URLSearchParams(location.search).get('q') !== ${JSON.stringify(query)}) return 'esperando';
    const link = document.querySelector('ytmusic-section-list-renderer a[href*="watch?v="]');
    if (!link) return document.querySelector('ytmusic-message-renderer') ? 'sin-resultados' : 'esperando';
    link.click();
    return 'ok';
  })()`;
}

function runIn(browser, b, tab, code) {
  return String(browser[0] === 'com.apple.Safari' ? b.doJavaScript(code, { in: tab }) : tab.execute({ javascript: code }));
}

function youtubeMusicTab(app) {
  // Primero el navegador que suena; si no, cualquiera con YouTube Music abierto.
  const first = BROWSERS.find(([id]) => app === id || app.startsWith(`${id}.`));
  const order = first ? [first, ...BROWSERS.filter((b) => b !== first)] : BROWSERS;
  for (const browser of order) {
    try {
      const b = Application(browser[0]);
      if (!b.running()) continue;
      for (const w of b.windows()) {
        for (const t of w.tabs()) {
          if (YTM_PAGE.test(String(t.url() || ''))) return { browser, b, tab: t };
        }
      }
    } catch (e) {
      // sin permiso de Automatización para ese navegador: se sigue con el siguiente
    }
  }
  return null;
}

function searchYouTubeMusic(query, app) {
  const url = `https://music.youtube.com/search?q=${encodeURIComponent(query)}`;
  const found = youtubeMusicTab(app);
  if (!found) {
    openUrl(url);
    return { ok: true, via: 'navegador', info: 'Abrí la búsqueda en YouTube Music: elige la canción ahí.' };
  }
  const { browser, b, tab } = found;
  try {
    runIn(browser, b, tab, `location.href = ${JSON.stringify(url)}; 'ok'`);
  } catch (e) {
    if (/javascript/i.test(String(e))) {
      tab.url = url; // sin JavaScript se puede abrir la búsqueda, pero no elegir la canción
      return {
        ok: true,
        via: 'navegador',
        info: 'Abrí la búsqueda en YouTube Music. Para que ponga la canción sola, activa en el navegador: Ver → Opciones para desarrolladores → Permitir JavaScript desde Apple Events.',
      };
    }
    // La página empezó a navegar antes de responder: se sigue esperando los resultados.
  }
  for (let i = 0; i < 30; i++) {
    delay(0.5);
    let result = 'esperando';
    try {
      result = runIn(browser, b, tab, firstResultScript(query));
    } catch (e) {
      // la página se está recargando
    }
    if (result === 'ok') return { ok: true, via: 'YouTube Music' };
    if (result === 'sin-resultados') return { error: `YouTube Music no encontró "${query}".` };
  }
  return { ok: true, via: 'navegador', info: 'Abrí la búsqueda en YouTube Music: elige la canción ahí.' };
}

function searchAppleMusic(query) {
  const music = Application('com.apple.Music');
  const tracks = music.search(music.libraryPlaylists[0], { for: query });
  if (!tracks || !tracks.length) return null;
  tracks[0].play();
  return { ok: true, via: 'Apple Music' };
}

function searchSong(query) {
  query = String(query || '').trim().slice(0, 200);
  if (!query) {
    emit({ error: 'Escribe el nombre de una canción o un artista.' });
    return 2;
  }
  const app = nowPlayingApp();
  let result;
  try {
    if (app === 'com.spotify.client') {
      openUrl(`spotify:search:${encodeURIComponent(query)}`);
      result = { ok: true, via: 'Spotify', info: 'Abrí la búsqueda en Spotify: elige la canción ahí.' };
    } else if (app === 'com.apple.Music') {
      result = searchAppleMusic(query) || searchYouTubeMusic(query, app);
    } else {
      result = searchYouTubeMusic(query, app);
    }
  } catch (e) {
    result = { error: `No se pudo buscar: ${String(e).slice(0, 200)}` };
  }
  emit(result);
  return result.error ? 1 : 0;
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
// navegador). Para Spotify, Apple Music y las demás apps, el video se busca en YouTube
// por título y artista. Solo si el plugin lo pidió (existe el archivo videoFlag): leer
// pestañas hace que macOS pregunte si OBS puede controlar el navegador, y eso no debe
// pasarle a quien no eligió "Video si es posible".
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
const YOUTUBE_WATCH = /^https:\/\/(music\.|www\.|m\.)?youtube\.com\/watch\?(?:[^#]*&)?v=([A-Za-z0-9_-]{11})/;

// { id, site } del video que suena (site: "ytmusic" en music.youtube.com, si no "youtube"),
// o null.
function youtubeVideo(app, title) {
  // Las apps instaladas (PWA) llevan el id del navegador como prefijo:
  // com.google.Chrome.app.cinhimbnkkaeohfgghhklpknlkffjgod es YouTube Music en Chrome.
  const browser = BROWSERS.find(([id]) => app === id || app.startsWith(`${id}.`));
  if (!browser) return null;
  try {
    const b = Application(browser[0]);
    if (!b.running()) return null;
    const found = [];
    for (const w of b.windows()) {
      for (const t of w.tabs()) {
        const m = YOUTUBE_WATCH.exec(String(t.url() || ''));
        if (m) found.push({ id: m[2], site: m[1] === 'music.' ? 'ytmusic' : 'youtube', name: String(t[browser[1]]() || '').toLowerCase() });
      }
    }
    // Con varias pestañas de YouTube, la que tiene el título de la canción.
    const hit = found.find((f) => title && f.name.includes(title.toLowerCase())) || (found.length === 1 ? found[0] : null);
    return hit ? { id: hit.id, site: hit.site } : null;
  } catch (e) {
    return null; // sin permiso o navegador sin AppleScript: sin video
  }
}

// Una página web como texto, con las cabeceras de un navegador (sin la cookie de
// consentimiento, en Europa YouTube responde con su página de "antes de continuar").
function fetchText(url) {
  const request = $.NSMutableURLRequest.requestWithURL($.NSURL.URLWithString(url));
  request.timeoutInterval = 6;
  request.setValueForHTTPHeaderField('Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0 Safari/537.36', 'User-Agent');
  request.setValueForHTTPHeaderField('en-US,en;q=0.8', 'Accept-Language');
  request.setValueForHTTPHeaderField('SOCS=CAI; CONSENT=YES+', 'Cookie');
  const response = Ref();
  const error = Ref();
  const data = $.NSURLConnection.sendSynchronousRequestReturningResponseError(request, response, error);
  if (!data || data.isNil() || !data.length) return '';
  return ObjC.unwrap($.NSString.alloc.initWithDataEncoding(data, $.NSUTF8StringEncoding)) || '';
}

const normName = (s) => String(s).toLowerCase().normalize('NFD').replace(/[\u0300-\u036f]/g, '').replace(/[^a-z0-9]/g, '');

// El video de una canción que no suena en YouTube (Spotify, Apple Music…): se busca por
// título y artista y se usa el primer resultado con ese mismo título (o el primero).
function searchYouTubeVideo(title, artist) {
  const query = encodeURIComponent(`${title} ${artist}`.trim());
  const html = fetchText(`https://www.youtube.com/results?search_query=${query}`);
  const pattern = /"videoRenderer":\{"videoId":"([A-Za-z0-9_-]{11})"[\s\S]{0,4000}?"title":\{"runs":\[\{"text":"((?:[^"\\]|\\.)*)"/g;
  const results = [];
  let m;
  while ((m = pattern.exec(html)) && results.length < 5) {
    let name = m[2];
    try {
      name = JSON.parse(`"${name}"`);
    } catch (e) {
      // queda el texto tal cual
    }
    results.push({ id: m[1], name });
  }
  if (!results.length) return '';
  const wanted = normName(title);
  const hit = results.find((r) => normName(r.name) === wanted) || results[0];
  return hit.id;
}

const videos = {}; // "app\ncanción" → { id, site }
const videoTries = {};

function videoFor(app, title, artist, videoFlag) {
  if (!videoFlag || !$.NSFileManager.defaultManager.fileExistsAtPath(videoFlag)) return null;
  if (/obsproject/i.test(app)) return null; // el video de la propia tarjeta
  const key = `${app}\n${title}`;
  if (videos[key]) return videos[key];
  if ((videoTries[key] || 0) >= 3) return null; // la dirección cambia un instante después del título
  videoTries[key] = (videoTries[key] || 0) + 1;
  let video = null;
  if (BROWSERS.some(([id]) => app === id || app.startsWith(`${id}.`))) {
    video = youtubeVideo(app, title);
  } else {
    try {
      const id = searchYouTubeVideo(title, artist);
      if (id) video = { id, site: '' };
    } catch (e) {
      // sin internet o respuesta rara: sin video
    }
  }
  if (video) videos[key] = video;
  return video;
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
        // El sitio dice si es YouTube o YouTube Music ("Escuchando en …").
        const video = videoFor(app, entry.title, entry.artist, videoFlag);
        if (video) {
          entry.videoId = video.id;
          if (video.site) entry.site = video.site;
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

// Para probar la búsqueda del video de una canción: ... media/macos.js ytsearch "Canción" "Artista"
function testYouTubeSearch(title, artist) {
  const id = searchYouTubeVideo(title || '', artist || '');
  emit({ title, artist, videoId: id });
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
  const video = youtubeVideo(app, title) || {};
  emit({ app, title, videoId: video.id || '', site: video.site || '' });
  return 0;
}

function run(argv) {
  const mode = argv[0];
  const code =
    mode === 'command' ? sendCommand(argv[1])
    : mode === 'search' ? searchSong(argv[1])
    : mode === 'artwork' ? testArtwork(argv[1], argv[2])
    : mode === 'ytsearch' ? testYouTubeSearch(argv[1], argv[2])
    : mode === 'video' ? testVideo()
    : mode === 'selftest' ? selfTest()
    : watch(mode === 'watch' ? argv[1] : ''); // watch <archivo>: el plugin pide modo video
  if (code) $.exit(code);
}
