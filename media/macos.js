// Lee "Ahora suena" de macOS (lo mismo que muestra el Centro de control) y escribe
// una línea JSON por segundo en la salida estándar; server.js la lee. Mismo
// protocolo que media/windows.ps1.
//
// Corre con: /usr/bin/osascript -l JavaScript media/macos.js
// Para controlar el reproductor: ... media/macos.js command next   (previous | playpause | next)
//   ... command seek 61000 (ms) · command volume 40 (0–100) · command shuffle · command state
//   volume, shuffle y state responden con {"state":{"volume":40,"shuffle":true}} (lo que se sepa)
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

// Los botones van a lo que muestra el dock: el plugin lo pasa en OBS_NOW_PLAYING_APP y
// OBS_NOW_PLAYING_TITLE (puede no ser lo que anuncia macOS, ver "por detrás" más abajo).
function envValue(name) {
  const v = $.NSProcessInfo.processInfo.environment.objectForKey(name);
  return v && !v.isNil() ? String(ObjC.unwrap(v)) : '';
}

function commandApp() {
  // "com.google.Chrome#ytmusic" (una pestaña que suena por detrás): el navegador.
  return (envValue('OBS_NOW_PLAYING_APP') || nowPlayingApp()).replace(/#.*$/, '');
}

function commandTitle() {
  const shown = envValue('OBS_NOW_PLAYING_TITLE');
  if (shown) return shown;
  try {
    return ObjC.unwrap($.NSClassFromString('MRNowPlayingRequest').localNowPlayingItem.nowPlayingInfo
      .objectForKey('kMRMediaRemoteNowPlayingInfoTitle')) || '';
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

// El título que la página anuncia a macOS (el mismo que lee el lector).
const MEDIA_SESSION_TITLE = "(navigator.mediaSession && navigator.mediaSession.metadata && navigator.mediaSession.metadata.title) || ''";

// Adelantar / retroceder en la página: se mueve el video (o audio) que está sonando.
function seekScript(seconds) {
  return `(() => {
    const media = [...document.querySelectorAll('video, audio')].find((m) => m.duration > 0);
    if (!media) return 'sin-video';
    media.currentTime = ${Number(seconds) || 0};
    return 'ok';
  })()`;
}

const SPOTIFY_WEB = /^https:\/\/open\.spotify\.com\//;
// Cualquier página de YouTube / YouTube Music, no solo /watch: en YouTube Music la música
// sigue sonando en la barra de abajo mientras se navega (inicio, biblioteca, búsqueda), y
// la app instalada (PWA) suele estar en una de esas páginas.
const YOUTUBE_PAGE = /^https:\/\/(music\.|www\.|m\.)?youtube\.com\//;
// Páginas que son un reproductor por sí mismas: si hay una sola pestaña, es esa.
const PLAYER_PAGE = /^https:\/\/(music\.youtube\.com\/|open\.spotify\.com\/)/;

// code: el JavaScript que se corre en la pestaña que suena. withSpotify: también cuenta la
// pestaña del reproductor web de Spotify (para adelantar; los botones van por las teclas).
function browserCommand(app, code, withSpotify) {
  const browser = BROWSERS.find(([id]) => app === id || app.startsWith(`${id}.`));
  // Si macOS cree que "suena" OBS (el video de la tarjeta), la música real está en algún
  // navegador: se prueba en todos los abiertos.
  if (!browser) {
    if (!/obsproject/i.test(app)) return { tried: false };
    let blocked = false;
    for (const [id] of BROWSERS) {
      const r = browserCommandIn(BROWSERS.find(([b]) => b === id), code, withSpotify);
      if (r.ok) return r;
      blocked = blocked || r.blocked;
    }
    return { tried: true, ok: false, blocked };
  }
  return browserCommandIn(browser, code, withSpotify);
}

function browserCommandIn(browser, code, withSpotify) {
  try {
    const b = Application(browser[0]);
    if (!b.running()) return { tried: false };
    const title = normName(commandTitle());
    const tabs = [];
    for (const w of b.windows()) {
      for (const t of w.tabs()) {
        const url = String(t.url() || '');
        if (YOUTUBE_PAGE.test(url) || (withSpotify && SPOTIFY_WEB.test(url))) tabs.push({ tab: t, url, name: normName(t[browser[1]]() || '') });
      }
    }
    const run = (tab, code) => (browser[0] === 'com.apple.Safari' ? b.doJavaScript(code, { in: tab }) : tab.execute({ javascript: code }));
    // La pestaña que suena: la que tiene la canción en el título o, si hay varias, la que la
    // anuncia en su sesión de medios (la app instalada de YouTube Music titula su ventana
    // "YouTube Music", sin la canción, y puede haber otras pestañas de YouTube abiertas).
    // Por el título o por ser la única solo cuentan un video o un reproductor; otra página de
    // youtube.com (el inicio, una búsqueda con la canción en el título) solo si la anuncia.
    const players = tabs.filter((t) => YOUTUBE_WATCH.test(t.url) || PLAYER_PAGE.test(t.url));
    let hit = players.find((t) => title && t.name.includes(title)) || (tabs.length === 1 && players.length === 1 ? players[0] : null);
    if (!hit && title) hit = tabs.find((t) => normName(run(t.tab, MEDIA_SESSION_TITLE) || '') === title);
    if (!hit) return { tried: false, seen: tabs.length };
    const result = run(hit.tab, code);
    // 'ok' o, para el volumen y el aleatorio, un JSON con lo que hay en la página.
    return { tried: true, ok: result === 'ok' || /^\{/.test(String(result || '')), result };
  } catch (e) {
    // El navegador no deja ejecutar JavaScript desde Apple Events (viene apagado).
    return { tried: true, ok: false, blocked: /javascript/i.test(String(e)), error: String(e).slice(0, 200) };
  }
}

const BLOCKED_HINT = 'Para que los botones vayan siempre a YouTube Music, activa en el navegador: Ver → Opciones para desarrolladores → Permitir JavaScript desde Apple Events.';

// Saltar a una posición (en ms): en la página, o en la app (Spotify, Apple Music).
function seekCommand(positionMs) {
  const seconds = Math.max(0, Number(positionMs) || 0) / 1000;
  const app = commandApp();
  const page = browserCommand(app, seekScript(seconds), true);
  if (page.ok) {
    emit({ ok: true, via: 'página' });
    return 0;
  }
  if (APP_COMMANDS[app]) {
    try {
      const player = Application(app);
      if (player.running()) {
        player.playerPosition = seconds;
        emit({ ok: true, via: app });
        return 0;
      }
    } catch (e) {
      // sin permiso de Automatización
    }
  }
  emit({ error: page.blocked ? BLOCKED_HINT : 'Este reproductor no deja adelantar ni retroceder desde OBS.' });
  return 1;
}

// ---- Volumen y aleatorio (dock) ---------------------------------------------------
// Spotify y Apple Music: se le pide a la app (sound volume, shuffling / shuffle enabled).
// En la página: el reproductor de YouTube (#movie_player, también dentro de YouTube Music)
// tiene getVolume / setVolume; el aleatorio de YouTube Music es un botón que mezcla la cola,
// sin estado ("action"); el del reproductor web de Spotify sí lo tiene (aria-checked).
const APP_SHUFFLE = { 'com.spotify.client': 'shuffling', 'com.apple.Music': 'shuffleEnabled' };

function pageStateScript(volume, toggleShuffle) {
  return `(() => {
    const player = document.getElementById('movie_player');
    const media = [...document.querySelectorAll('video, audio')].find((m) => m.duration > 0);
    const vol = ${volume === null ? 'null' : Number(volume)};
    if (vol !== null) {
      if (player && player.setVolume) {
        player.setVolume(vol);
        if (vol > 0 && player.unMute) player.unMute();
      } else if (media) {
        media.volume = vol / 100;
        media.muted = vol === 0;
      }
    }
    const spotify = location.hostname === 'open.spotify.com';
    const button = document.querySelector(spotify ? '[data-testid="control-button-shuffle"]' : 'ytmusic-player-bar .shuffle');
    if (${toggleShuffle ? 'true' : 'false'} && button) button.click();
    let volume = null;
    if (player && player.getVolume) volume = player.isMuted && player.isMuted() ? 0 : Math.round(player.getVolume());
    else if (media) volume = media.muted ? 0 : Math.round(media.volume * 100);
    let shuffle = null;
    if (button) shuffle = spotify ? button.getAttribute('aria-checked') === 'true' : 'action';
    return JSON.stringify({ volume, shuffle });
  })()`;
}

// action: state | volume (arg 0–100) | shuffle.
function stateCommand(action, arg) {
  const volume = action === 'volume' ? Math.max(0, Math.min(100, Math.round(Number(arg) || 0))) : null;
  const toggle = action === 'shuffle';
  const app = commandApp();
  if (APP_COMMANDS[app]) {
    try {
      const player = Application(app);
      if (player.running()) {
        if (volume !== null) player.soundVolume = volume;
        const shuffle = APP_SHUFFLE[app];
        if (toggle) player[shuffle] = !player[shuffle]();
        emit({ ok: true, via: app, state: { volume: Math.round(player.soundVolume()), shuffle: Boolean(player[shuffle]()) } });
        return 0;
      }
    } catch (e) {
      // sin permiso de Automatización: se prueba en la página (por si es otra cosa)
    }
  }
  const page = browserCommand(app, pageStateScript(volume, toggle), true);
  if (page.ok && page.result) {
    try {
      const r = JSON.parse(page.result);
      emit({ ok: true, via: 'página', state: { volume: r.volume, shuffle: r.shuffle } });
      return 0;
    } catch (e) {
      // respuesta rara de la página
    }
  }
  if (action === 'state') {
    emit({ state: {} }); // el dock apaga el volumen y el aleatorio
    return 0;
  }
  const what = toggle ? 'cambiar el aleatorio' : 'cambiar el volumen';
  emit({ error: page.blocked ? BLOCKED_HINT : `Este reproductor no deja ${what} desde OBS.` });
  return 1;
}

function sendCommand(action, arg) {
  if (action === 'seek') return seekCommand(arg);
  if (action === 'state' || action === 'volume' || action === 'shuffle') return stateCommand(action, arg);
  if (!ACTIONS.includes(action)) {
    emit({ error: `Comando desconocido: ${action}` });
    return 2;
  }
  const app = commandApp();
  const page = browserCommand(app, pageScript(action), false);
  if (page.ok) {
    emit({ ok: true, via: 'página de YouTube' });
    return 0;
  }
  const hint = page.blocked ? BLOCKED_HINT : '';
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
  // Para el registro: por qué no se usó la página.
  const why = page.error ? ` (la página falló: ${page.error})`
    : page.seen !== undefined ? ` (pestañas de YouTube vistas: ${page.seen})` : '';
  emit({ ok: true, via: `teclas de música${why}`, ...(hint ? { hint } : {}) });
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
  const app = commandApp();
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

// ---- Descargas sin frenar la lectura ----------------------------------------------
// El plugin da por muerto un lector que no escribe en 10 s: con la red lenta al cambiar de
// canción, la tarjeta desaparecía en pleno directo. Por eso cada descarga (carátulas,
// búsqueda en YouTube) corre aparte en /usr/bin/curl, con 4 s como máximo, y el bucle solo
// mira en cada vuelta si ya terminó. Mientras tanto las funciones de abajo devuelven
// PENDING y la carátula o el video llegan en una línea posterior. (Desde JXA no hay forma
// confiable de recibir la respuesta de NSURLSession en otro hilo; un proceso aparte sí se
// puede mirar sin esperarlo.) Una descarga que falló no se repite hasta pasados 30 s.
const PENDING = { pending: true };
const FETCH_DIR = `${ObjC.unwrap($.NSTemporaryDirectory())}obs-now-playing-fetch`;
const FETCH_TIMEOUT_S = 4;
const FETCH_RETRY_MS = 30000;
const FETCH_KEEP_MS = 60000;
const MAX_FETCHES = 3; // descargas a la vez
const BROWSER_UA = 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0 Safari/537.36';
const fetches = {}; // url → { task, path, data (NSData; null si falló), at, used }
let fetchCount = 0;

function removeFile(path) {
  $.NSFileManager.defaultManager.removeItemAtPathError(path, $());
}

function startFetch(url, headers) {
  const fm = $.NSFileManager.defaultManager;
  fm.createDirectoryAtPathWithIntermediateDirectoriesAttributesError(FETCH_DIR, true, $(), $());
  const path = `${FETCH_DIR}/${$.NSProcessInfo.processInfo.processIdentifier}-${++fetchCount}`;
  const args = ['-sSfL', '--compressed', '--max-time', String(FETCH_TIMEOUT_S), '--max-filesize', '8388608', '-o', path];
  for (const h of headers) args.push('-H', h);
  args.push(url);
  const task = $.NSTask.alloc.init;
  task.launchPath = '/usr/bin/curl';
  task.arguments = $(args);
  task.standardInput = $.NSFileHandle.fileHandleWithNullDevice;
  task.standardOutput = $.NSFileHandle.fileHandleWithNullDevice;
  task.standardError = $.NSFileHandle.fileHandleWithNullDevice;
  task.launch;
  return { task, path, data: null, at: Date.now(), used: Date.now() };
}

// Los datos de url (NSData), null si no se pudo bajar o PENDING mientras baja. No espera.
function fetchAsync(url, headers = []) {
  let f = fetches[url];
  if (f && !f.task && !f.data && Date.now() - f.at >= FETCH_RETRY_MS) f = null;
  if (!f) {
    if (Object.values(fetches).filter((x) => x.task).length >= MAX_FETCHES) return PENDING;
    try {
      f = startFetch(url, headers);
    } catch (e) {
      f = { task: null, path: '', data: null, at: Date.now(), used: Date.now() };
    }
    fetches[url] = f;
  }
  f.used = Date.now();
  settleFetch(f);
  return f.task ? PENDING : f.data;
}

// Si la descarga terminó: guarda lo bajado (si salió bien) y borra su archivo.
function settleFetch(f) {
  if (!f.task || f.task.isRunning) return;
  if (f.task.terminationStatus === 0) {
    const data = $.NSData.dataWithContentsOfFile(f.path);
    f.data = data && !data.isNil() && data.length ? data : null;
  }
  removeFile(f.path);
  f.task = null;
  f.at = Date.now();
}

// Una vez por vuelta: se recogen las descargas que terminaron (también las que ya nadie
// pide, para que no ocupen lugar) y se olvidan las que nadie pidió en el último minuto (lo
// que se necesitaba de ellas ya quedó guardado más arriba).
function pruneFetches() {
  const now = Date.now();
  for (const url of Object.keys(fetches)) {
    settleFetch(fetches[url]);
    if (!fetches[url].task && now - fetches[url].used > FETCH_KEEP_MS) delete fetches[url];
  }
}

// Al salir: corta las descargas que sigan y borra sus archivos. Al empezar: borra los que
// dejó un lector anterior que se cerró de golpe (de hace más de 10 min).
function stopFetches() {
  for (const f of Object.values(fetches)) {
    if (!f.task) continue;
    if (f.task.isRunning) f.task.terminate;
    removeFile(f.path);
  }
}

function cleanOldFetches() {
  const fm = $.NSFileManager.defaultManager;
  const names = fm.contentsOfDirectoryAtPathError(FETCH_DIR, $());
  if (!names || names.isNil()) return;
  for (const name of ObjC.deepUnwrap(names) || []) {
    const path = `${FETCH_DIR}/${name}`;
    const attrs = fm.attributesOfItemAtPathError(path, $());
    const date = attrs && !attrs.isNil() ? ObjC.unwrap(attrs.objectForKey('NSFileModificationDate')) : null;
    if (date instanceof Date && Date.now() - date.getTime() > 600000) removeFile(path);
  }
}

// Modos de prueba (artwork, ytsearch): esperan lo que en la lectura llega después.
function waitFor(step) {
  for (let i = 0; i < 100; i++) {
    const result = step();
    if (result !== PENDING) return result;
    delay(0.1);
  }
  return '';
}

const dataUrl = (data) => `data:image/jpeg;base64,${ObjC.unwrap(data.base64EncodedStringWithOptions(0))}`;

// macOS nuevos (visto en macOS 27) ya no mandan la imagen en "Ahora suena": solo un
// identificador y su tamaño. A la app de Spotify se la pedimos directamente: da el
// enlace de la carátula de la canción que suena. La primera vez macOS pregunta si OBS
// (o la app) puede controlar Spotify; si se le dice que no, simplemente no hay carátula.
// PENDING mientras se baja.
function spotifyArtwork(title) {
  try {
    const spotify = Application('com.spotify.client');
    if (!spotify.running()) return '';
    const track = spotify.currentTrack;
    if (String(track.name()) !== title) return ''; // Spotify aún no cambió: se reintenta
    const url = String(track.artworkUrl() || '');
    if (!/^https:\/\//.test(url)) return '';
    const data = fetchAsync(url);
    if (data === PENDING) return PENDING;
    return data ? dataUrl(data) : '';
  } catch (e) {
    return '';
  }
}

// Último recurso (YouTube Music, navegadores…): buscar la carátula por artista y
// canción en el catálogo público de Apple (iTunes Search, sin cuenta ni clave). Solo
// se acepta si artista y título coinciden, para no mostrar una carátula equivocada.
// PENDING mientras se busca.
const lookups = {}; // "artista\ncanción" → carátula ('' = no se encontró), una búsqueda por canción

function catalogArtwork(title, artist) {
  const key = `${artist}\n${title}`;
  if (key in lookups) return lookups[key];
  // "Canción (Official Video)" o "Artista - Topic" son cosas de YouTube, no del disco.
  const clean = (s) => s.replace(/\s*[([][^)\]]*[)\]]/g, '').replace(/\s+-\s+topic$/i, '').trim();
  const norm = (s) => s.toLowerCase().normalize('NFD').replace(/[\u0300-\u036f]/g, '').replace(/[^a-z0-9]/g, '');
  const a = norm(clean(artist));
  const t = norm(clean(title));
  if (!a || !t) return (lookups[key] = '');
  lookups[key] = '';
  try {
    const term = encodeURIComponent(`${clean(artist)} ${clean(title)}`);
    const json = fetchAsync(`https://itunes.apple.com/search?media=music&entity=song&limit=10&term=${term}`);
    if (json === PENDING) {
      delete lookups[key];
      return PENDING;
    }
    if (!json) return '';
    const text = ObjC.unwrap($.NSString.alloc.initWithDataEncoding(json, $.NSUTF8StringEncoding));
    const hit = (JSON.parse(text).results || []).find((r) => {
      const ra = norm(r.artistName || '');
      const rt = norm(r.trackName || '');
      return ra && rt && (ra.includes(a) || a.includes(ra)) && (rt.includes(t) || t.includes(rt));
    });
    if (!hit || !hit.artworkUrl100) return '';
    const image = fetchAsync(hit.artworkUrl100.replace(/\/\d+x\d+bb\./, '/600x600bb.'));
    if (image === PENDING) {
      delete lookups[key];
      return PENDING;
    }
    if (image) lookups[key] = dataUrl(image);
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
// PENDING mientras baja.
function fetchText(url) {
  const data = fetchAsync(url, [`User-Agent: ${BROWSER_UA}`, 'Accept-Language: en-US,en;q=0.8', 'Cookie: SOCS=CAI; CONSENT=YES+']);
  if (data === PENDING) return PENDING;
  if (!data) return '';
  return ObjC.unwrap($.NSString.alloc.initWithDataEncoding(data, $.NSUTF8StringEncoding)) || '';
}

const normName = (s) => String(s).toLowerCase().normalize('NFD').replace(/[\u0300-\u036f]/g, '').replace(/[^a-z0-9]/g, '');

// El video de una canción que no suena en YouTube (Spotify, Apple Music…): se busca por
// título y artista y se usa el primer resultado con ese mismo título (o el primero).
// PENDING mientras se busca.
function searchYouTubeVideo(title, artist) {
  const query = encodeURIComponent(`${title} ${artist}`.trim());
  const html = fetchText(`https://www.youtube.com/results?search_query=${query}`);
  if (html === PENDING) return PENDING;
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

// ¿Hay una pestaña del reproductor web de Spotify? (para decir "Escuchando en Spotify").
function hasSpotifyTab(app) {
  const browser = BROWSERS.find(([id]) => app === id || app.startsWith(`${id}.`));
  try {
    const b = Application(browser[0]);
    for (const w of b.windows()) {
      for (const t of w.tabs()) {
        if (/^https:\/\/open\.spotify\.com\//.test(String(t.url() || ''))) return true;
      }
    }
  } catch (e) {
    // sin permiso o navegador sin AppleScript
  }
  return false;
}

// { id, site }, null si no se encontró o PENDING mientras se busca.
function searchedVideo(title, artist, site) {
  try {
    const id = searchYouTubeVideo(title, artist);
    if (id === PENDING) return PENDING;
    return id ? { id, site } : null;
  } catch (e) {
    return null; // respuesta rara: sin video
  }
}

const videoSearches = {}; // "app\ncanción" → { site } del video que se está buscando en YouTube

// { id, site } o null (también mientras se busca: llega en una vuelta posterior).
function videoFor(app, title, artist, album, videoFlag) {
  if (!videoFlag || !$.NSFileManager.defaultManager.fileExistsAtPath(videoFlag)) return null;
  if (/obsproject/i.test(app)) return null; // el video de la propia tarjeta
  const key = `${app}\n${title}`;
  if (videos[key]) return videos[key];
  if (videoSearches[key]) {
    const searched = searchedVideo(title, artist, videoSearches[key].site);
    if (searched === PENDING) return null;
    delete videoSearches[key];
    if (searched) return (videos[key] = searched);
  }
  if ((videoTries[key] || 0) >= 3) return null; // la dirección cambia un instante después del título
  videoTries[key] = (videoTries[key] || 0) + 1;
  let video = null;
  if (BROWSERS.some(([id]) => app === id || app.startsWith(`${id}.`))) {
    video = youtubeVideo(app, title);
    // Sin pestaña de YouTube: el reproductor web de Spotify u otra página de música. Solo
    // si trae álbum (una canción): un stream o una serie no lo traen y no se les busca video.
    if (!video && album) videoSearches[key] = { site: hasSpotifyTab(app) ? 'spotify' : '' };
  } else {
    videoSearches[key] = { site: '' };
  }
  if (video) videos[key] = video;
  return video;
}

// ---- Lo que suena "por detrás" -----------------------------------------------------
// macOS anuncia un solo reproductor a la vez ("Ahora suena") y no siempre suelta al de
// antes: al pasar de YouTube Music a Spotify y volver, puede seguir anunciando Spotify en
// pausa aunque YouTube Music ya suene; y con el modo video, puede anunciar a OBS (el video
// sin sonido de la tarjeta). En esos casos se busca qué suena de verdad: Spotify y Apple
// Music se le preguntan a la app (si está abierta) y las pestañas de YouTube / YouTube
// Music / Spotify web, a la página (solo con el modo video, que ya lee las pestañas).
const PLAYING_PAGE = `(() => {
  const m = navigator.mediaSession && navigator.mediaSession.metadata;
  const media = [...document.querySelectorAll('video, audio')].find((x) => x.duration > 0);
  if (!m || !m.title || !media || media.paused) return '';
  const art = (m.artwork || []).slice(-1)[0];
  return JSON.stringify({
    title: m.title, artist: m.artist || '', album: m.album || '',
    positionMs: Math.round(media.currentTime * 1000),
    durationMs: isFinite(media.duration) ? Math.round(media.duration * 1000) : 0,
    art: art ? art.src : '',
  });
})()`;

// Todo lo que suena fuera de lo anunciado (skipApp, skipTitle: lo anunciado, para no
// repetirlo): las apps (Spotify, Apple Music) y una pestaña por navegador y sitio (YouTube
// Music, YouTube, Spotify web). Las pestañas van como "<navegador>#<sitio>" para no pisarse
// con la app del navegador (el plugin las guarda por app).
function playingInApps(skipApp) {
  const found = [];
  const apps = [
    ['com.spotify.client', 1], // duración en ms
    ['com.apple.Music', 1000], // duración en segundos
  ];
  for (const [id, toMs] of apps) {
    if (id === skipApp) continue;
    try {
      const player = Application(id);
      if (!player.running() || String(player.playerState()) !== 'playing') continue;
      const track = player.currentTrack;
      found.push({
        app: id,
        title: String(track.name() || ''),
        artist: String(track.artist() || ''),
        album: String(track.album() || ''),
        progressMs: Math.round((Number(player.playerPosition()) || 0) * 1000),
        durationMs: Math.round((Number(track.duration()) || 0) * toMs),
      });
    } catch (e) {
      // sin permiso de Automatización para esa app
    }
  }
  return found;
}

function playingInBrowsers(skipTitle) {
  const found = [];
  for (const [id, nameKey] of BROWSERS) {
    try {
      const b = Application(id);
      if (!b.running()) continue;
      const sites = {};
      for (const w of b.windows()) {
        for (const t of w.tabs()) {
          const url = String(t.url() || '');
          if (!YOUTUBE_PAGE.test(url) && !SPOTIFY_WEB.test(url)) continue;
          const site = SPOTIFY_WEB.test(url) ? 'spotify' : /^https:\/\/music\./.test(url) ? 'ytmusic' : 'youtube';
          if (sites[site]) continue;
          const out = id === 'com.apple.Safari' ? b.doJavaScript(PLAYING_PAGE, { in: t }) : t.execute({ javascript: PLAYING_PAGE });
          if (!out) continue;
          const r = JSON.parse(out);
          if (skipTitle && normName(r.title) === skipTitle) continue;
          sites[site] = true;
          const watchId = YOUTUBE_WATCH.exec(url);
          found.push({
            app: `${id}#${site}`,
            name: String(t[nameKey]() || ''),
            title: String(r.title),
            artist: String(r.artist),
            album: String(r.album),
            progressMs: r.positionMs || 0,
            durationMs: r.durationMs || 0,
            site,
            videoId: watchId ? watchId[2] : '',
            art: /^https:\/\//.test(String(r.art || '')) ? r.art : '',
          });
        }
      }
    } catch (e) {
      // el navegador no deja correr JavaScript desde Apple Events, o no hay permiso
    }
  }
  return found;
}

function watch(videoFlag, othersFlag) {
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
  // Lo que suena por detrás: se busca cada 3 s (entre medio se reusa y se adelanta).
  let others = [];
  let othersAt = 0;
  const otherArt = {}; // "app\ncanción" → ya se mandó su carátula
  try {
    cleanOldFetches();
  } catch (e) {
    // sin carpeta todavía
  }

  // ~30 min y salimos: server.js nos vuelve a abrir (evita acumular memoria).
  for (let i = 0; i < 1800; i++) {
    try {
      pruneFetches();
      const item = Request.localNowPlayingItem;
      const info = item && !item.isNil() ? item.nowPlayingInfo : null;
      const value = (k) => {
        const v = info.objectForKey(k);
        return v.isNil() ? null : ObjC.unwrap(v);
      };
      const title = info && !info.isNil() ? value('kMRMediaRemoteNowPlayingInfoTitle') : null;
      const sessions = [];
      let reportedApp = '';
      let reportedTitle = '';
      let reportedPlaying = false;

      if (title) {
        const client = Request.localNowPlayingPlayerPath.client;
        const app = str(client.bundleIdentifier) || 'desconocida';
        // OBS (el video de la tarjeta) no cuenta: su título puede ser el de la canción real.
        if (!/obsproject/i.test(app)) {
          reportedApp = app;
          reportedTitle = normName(String(title));
        }
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
          let waiting = false; // una descarga sigue en curso: la carátula llega después
          const data = info.objectForKey('kMRMediaRemoteNowPlayingInfoArtworkData');
          if (!data.isNil() && data.length > 0) {
            const b64 = ObjC.unwrap(data.base64EncodedStringWithOptions(0));
            const mime = value('kMRMediaRemoteNowPlayingInfoArtworkMIMEType') || 'image/jpeg';
            art = `data:${mime};base64,${b64}`;
            hash = `${b64.length}:${b64.slice(-96)}`;
          } else {
            if (app === 'com.spotify.client') art = spotifyArtwork(entry.title);
            if (!art) art = catalogArtwork(entry.title, entry.artist);
            if (art === PENDING) {
              waiting = true;
              art = '';
            }
            hash = art ? `${art.length}:${art.slice(-96)}` : '';
          }
          if ((art && hash !== sentHash) || (tries >= 5 && !waiting)) {
            entry.artwork = art;
            sentKey = key;
            sentHash = hash;
            tries = 0;
          }
        }
        // El sitio dice si es YouTube o YouTube Music ("Escuchando en …").
        const video = videoFor(app, entry.title, entry.artist, entry.album, videoFlag);
        if (video) {
          entry.videoId = video.id;
          if (video.site) entry.site = video.site;
        }
        // OBS (el video sin sonido de la tarjeta) nunca es la música.
        if (!/obsproject/i.test(app)) {
          sessions.push(entry);
          reportedPlaying = playing;
        }
      }

      // macOS no anuncia nada que suene (o anuncia algo en pausa, u OBS), o hay un
      // reproductor elegido en el plugin (si suenan dos, macOS anuncia solo uno): ¿qué más
      // suena?
      const flagged = (f) => f && $.NSFileManager.defaultManager.fileExistsAtPath(f);
      if (!reportedPlaying || flagged(othersFlag)) {
        const now = Date.now();
        if (now - othersAt >= 3000) {
          othersAt = now;
          others = playingInApps(reportedApp);
          if (flagged(videoFlag)) others = others.concat(playingInBrowsers(reportedTitle));
        }
        others.forEach((other, n) => {
          const extra = {
            app: other.app,
            name: other.name || '',
            title: other.title,
            artist: other.artist,
            album: other.album,
            isPlaying: true,
            current: !reportedPlaying && n === 0,
            progressMs: other.progressMs + (now - othersAt),
            durationMs: other.durationMs,
          };
          if (other.site) extra.site = other.site;
          if (other.videoId) extra.videoId = other.videoId;
          const artKey = `${other.app}\n${other.title}`;
          if (!otherArt[artKey]) {
            let art = '';
            if (/^data:image\//.test(other.art || '')) art = other.art;
            else if (/^https?:\/\//.test(other.art || '')) {
              const data = fetchAsync(other.art);
              art = data === PENDING ? PENDING : data ? dataUrl(data) : '';
            }
            if (!art && other.app === 'com.spotify.client') art = spotifyArtwork(other.title);
            if (!art) art = catalogArtwork(other.title, other.artist);
            // Mientras se baja, la línea sale sin carátula y se vuelve a mirar en la próxima.
            if (art !== PENDING) {
              otherArt[artKey] = true;
              extra.artwork = art;
            }
          }
          if (extra.current) for (const s2 of sessions) s2.current = false;
          sessions.push(extra);
        });
      } else {
        others = [];
      }
      emit({ sessions });
      failures = 0;
    } catch (e) {
      emit({ error: String(e) });
      if (++failures >= 5) {
        stopFetches();
        return 4;
      }
    }
    delay(1);
  }
  stopFetches();
  return 0;
}

// Para probar la búsqueda del video de una canción: ... media/macos.js ytsearch "Canción" "Artista"
function testYouTubeSearch(title, artist) {
  const id = waitFor(() => searchYouTubeVideo(title || '', artist || ''));
  emit({ title, artist, videoId: id });
  return 0;
}

function testArtwork(title, artist) {
  const art = waitFor(() => catalogArtwork(title || '', artist || ''));
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
    mode === 'command' ? sendCommand(argv[1], argv[2])
    : mode === 'search' ? searchSong(argv[1])
    : mode === 'artwork' ? testArtwork(argv[1], argv[2])
    : mode === 'ytsearch' ? testYouTubeSearch(argv[1], argv[2])
    : mode === 'video' ? testVideo()
    : mode === 'selftest' ? selfTest()
    : watch(mode === 'watch' ? argv[1] : '', mode === 'watch' ? argv[2] : ''); // watch <video> <otros>: banderas del plugin
  if (code) $.exit(code);
}
