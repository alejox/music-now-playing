#!/usr/bin/env node
// OBS Now Playing — servidor local que lee "lo que está sonando" del sistema
// (Windows: controles multimedia SMTC; macOS: "Ahora suena") y lo sirve como
// overlay para una Fuente de navegador de OBS. Sin dependencias: Node 18+.
'use strict';

const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const os = require('node:os');
const readline = require('node:readline');
const { spawn } = require('node:child_process');

const VERSION = '2.4.17';
const ARGS = new Set(process.argv.slice(2));

// Archivos de la app (overlay, lectores del sistema, userscript): junto a este
// script, que en la app instalada es de solo lectura. Lo que cambia (configuración,
// archivo de texto, log) va a la carpeta de datos del usuario.
const ROOT = __dirname;
const DATA_DIR = process.env.OBS_NOW_PLAYING_DATA || defaultDataDir();
const CONFIG_PATH = path.join(DATA_DIR, 'config.json');
const CONTROL_PATH = path.join(DATA_DIR, '.control');
const COMMAND_PATH = path.join(DATA_DIR, '.command'); // comandos para el lector de Windows
fs.mkdirSync(DATA_DIR, { recursive: true });

function defaultDataDir() {
  const home = os.homedir();
  if (process.platform === 'darwin') return path.join(home, 'Library', 'Application Support', 'OBS Now Playing');
  if (process.platform === 'win32') {
    return path.join(process.env.APPDATA || path.join(home, 'AppData', 'Roaming'), 'OBS Now Playing');
  }
  return path.join(process.env.XDG_CONFIG_HOME || path.join(home, '.config'), 'obs-now-playing');
}

const DEFAULT_CONFIG = {
  port: 8787,
  textFile: 'now-playing.txt',
  textFormat: '{artist} - {title}',
  ignoredApps: [],
};
const config = { ...DEFAULT_CONFIG, ...readJson(CONFIG_PATH, {}) };
if (!Array.isArray(config.ignoredApps)) config.ignoredApps = [];
delete config.spotifyClientId; // v1 usaba la API de Spotify; ya no hace falta
writeJson(CONFIG_PATH, config);

const PORT = Number(config.port) || 8787;
const ORIGIN = `http://127.0.0.1:${PORT}`;

// Las apps instaladas arrancan sin consola: con --log la salida va a un archivo.
if (ARGS.has('--log')) {
  // Se agrega (no se pisa): una segunda copia que solo abre el panel no borra el log de la primera.
  const logPath = path.join(DATA_DIR, 'log.txt');
  let big = false;
  try {
    big = fs.statSync(logPath).size > 512 * 1024;
  } catch {}
  const log = fs.createWriteStream(logPath, { flags: big ? 'w' : 'a' });
  for (const level of ['log', 'warn', 'error']) {
    console[level] = (...parts) => log.write(`${new Date().toISOString()} ${parts.join(' ')}\n`);
  }
}

// Secreto de esta ejecución: protege los botones del panel de formularios
// enviados desde otras páginas.
const CONTROL_TOKEN = crypto.randomBytes(24).toString('base64url');

function openExternal(target) {
  const [cmd, args] =
    process.platform === 'darwin'
      ? ['open', [target]]
      : process.platform === 'win32'
        ? ['rundll32', ['url.dll,FileProtocolHandler', target]]
        : ['xdg-open', [target]];
  try {
    spawn(cmd, args, { detached: true, stdio: 'ignore' }).on('error', () => {}).unref();
  } catch {
    // sin navegador disponible: la URL ya quedó en el log
  }
}

// --stop: cierra la copia que esté corriendo (lo usan el instalador y el desinstalador).
// No usa la red: deja "stop" en el archivo de control, que la app revisa cada segundo
// y borra al cerrarse.
if (ARGS.has('--stop')) {
  if (!fs.existsSync(CONTROL_PATH)) process.exit(0);
  fs.writeFileSync(CONTROL_PATH, 'stop');
  const deadline = Date.now() + 6000;
  setInterval(() => {
    if (!fs.existsSync(CONTROL_PATH)) process.exit(0);
    if (Date.now() > deadline) {
      fs.rmSync(CONTROL_PATH, { force: true }); // quedó de una copia que ya no corre
      process.exit(0);
    }
  }, 200);
  return;
}

let helper = null;
function shutdown() {
  fs.rmSync(CONTROL_PATH, { force: true });
  if (helper) helper.kill();
  process.exit(0);
}

function readJson(file, fallback) {
  try {
    return JSON.parse(fs.readFileSync(file, 'utf8'));
  } catch {
    return fallback;
  }
}

function writeJson(file, data) {
  const tmp = `${file}.tmp`;
  fs.writeFileSync(tmp, JSON.stringify(data, null, 2));
  fs.renameSync(tmp, file);
}

const cleanString = (v, max = 300) => (typeof v === 'string' ? v.trim().slice(0, max) : '');
const cleanMs = (v) => (Number.isFinite(v) && v >= 0 ? Math.round(v) : 0);
function cleanUrl(v) {
  try {
    const u = new URL(v);
    return u.protocol === 'https:' ? u.href : '';
  } catch {
    return '';
  }
}

// ---------------------------------------------------------------------------
// Apps: de qué reproductor viene cada sesión del sistema.
// ---------------------------------------------------------------------------

const BROWSERS = [
  ['chrome', 'Chrome'],
  ['msedge', 'Edge'],
  ['firefox', 'Firefox'],
  ['brave', 'Brave'],
  ['opera', 'Opera'],
  ['vivaldi', 'Vivaldi'],
  ['safari', 'Safari'],
  ['thebrowser', 'Arc'],
];

// kind decide el color del overlay; label, el texto "Escuchando en …".
function classifyApp(app, name = '') {
  const id = `${app} ${name}`.toLowerCase();
  if (id.includes('spotify')) return { kind: 'spotify', label: 'Spotify' };
  // La PWA de YouTube Music instalada desde Chrome/Edge tiene este id de extensión.
  if (id.includes('cinhimbnkkaeohfgghhklpknlkffjgod') || id.includes('youtube music')) {
    return { kind: 'ytmusic', label: 'YouTube Music' };
  }
  if (id.includes('applemusic') || id.includes('com.apple.music')) return { kind: 'applemusic', label: 'Apple Music' };
  if (id.includes('tidal')) return { kind: 'other', label: 'TIDAL' };
  if (id.includes('deezer')) return { kind: 'other', label: 'Deezer' };
  for (const [needle, label] of BROWSERS) {
    if (id.includes(needle)) return { kind: 'browser', label };
  }
  const fallback = name || app.split(/[\\/!]/).pop().replace(/\.exe$/i, '') || 'Otra app';
  return { kind: 'other', label: fallback };
}

// ---------------------------------------------------------------------------
// Estado: pistas de todas las fuentes; gana la que suena y empezó último.
// ---------------------------------------------------------------------------

// Clave → pista. Sistema: "system:<app>". Navegador (userscript): "ytmusic", "spotifyWeb".
const tracks = new Map();
const knownApps = new Map(); // app → label, para el panel
const artworks = new Map(); // id → { mime, data }
const STALE_MS = 10000;

function artworkId(app, title, artist) {
  return crypto.createHash('sha1').update(`${app}\n${title}\n${artist}`).digest('hex').slice(0, 16);
}

function storeArtwork(id, dataUrl) {
  const m = /^data:(image\/[\w.+-]+);base64,(.+)$/s.exec(dataUrl || '');
  if (!m) return false;
  artworks.set(id, { mime: m[1], data: Buffer.from(m[2], 'base64') });
  while (artworks.size > 30) artworks.delete(artworks.keys().next().value);
  return true;
}

function upsertTrack(key, track) {
  const prev = tracks.get(key);
  const now = Date.now();
  const changed =
    !prev || prev.title !== track.title || prev.artist !== track.artist || (!prev.isPlaying && track.isPlaying);
  tracks.set(key, { ...track, key, updatedAt: now, changedAt: changed ? now : prev.changedAt });
}

const normalize = (s) => (s || '').toLowerCase().replace(/\s+/g, ' ').trim();

// Con includePaused (panel de controles), si nada suena devuelve la última en pausa,
// para poder reanudarla.
function currentTrack({ includePaused = false } = {}) {
  const now = Date.now();
  const alive = [...tracks.values()].filter(
    (t) => now - t.updatedAt <= STALE_MS && !(t.app && config.ignoredApps.includes(t.app))
  );
  const playing = alive.filter((t) => t.isPlaying);
  if (!playing.length) {
    if (!includePaused || !alive.length) return null;
    return alive.sort((a, b) => b.changedAt - a.changedAt || Number(b.isCurrent) - Number(a.isCurrent))[0];
  }
  // La misma canción puede llegar por el sistema y por el userscript: la del userscript
  // sabe si es YouTube Music o Spotify, la del sistema solo ve "Chrome".
  playing.sort((a, b) => b.changedAt - a.changedAt || Number(b.isCurrent) - Number(a.isCurrent));
  const best = playing[0];
  const twin = playing.find((t) => !t.app && normalize(t.title) === normalize(best.title));
  return twin || best;
}

function publicTrack(t) {
  if (!t) return null;
  const elapsed = t.isPlaying ? Date.now() - t.updatedAt : 0;
  return {
    title: t.title,
    artist: t.artist,
    album: t.album,
    artwork: artworks.has(t.artworkId) ? `/artwork/${t.artworkId}` : '',
    isPlaying: t.isPlaying,
    progressMs: t.durationMs ? Math.min(t.durationMs, t.progressMs + elapsed) : t.progressMs + elapsed,
    durationMs: t.durationMs,
    kind: t.kind,
    // Para un navegador no sabemos qué página suena: el overlay dice solo "Escuchando".
    sourceLabel: t.kind === 'browser' ? '' : t.label,
  };
}

// Archivo de texto para quien prefiera una Fuente de texto de OBS ("Leer desde archivo").
let lastText = null;
const textFilePath = () => path.resolve(DATA_DIR, config.textFile);
function refreshTextFile() {
  if (!config.textFile) return;
  const t = currentTrack();
  const line = t
    ? config.textFormat
        .replace(/\{(title|artist|album)\}/g, (_, key) => t[key] || '')
        .replace(/^\s*[-–—·|]\s*|\s*[-–—·|]\s*$/g, '')
        .trim()
    : '';
  if (line === lastText) return;
  lastText = line;
  try {
    fs.writeFileSync(textFilePath(), line);
  } catch (err) {
    console.error('[texto] No se pudo escribir el archivo:', err.message);
  }
}
setInterval(refreshTextFile, 1000);

// ---------------------------------------------------------------------------
// Lector del sistema: un proceso hijo que escribe una línea JSON por segundo.
//   Windows: PowerShell + SMTC (viene con Windows 10/11).
//   macOS:   osascript + MediaRemote (viene con macOS).
// ---------------------------------------------------------------------------

const system = { status: 'starting', error: '', lastLineAt: 0, startedAt: 0 };

function helperCommand() {
  if (process.env.OBS_NOW_PLAYING_HELPER) {
    // Solo para pruebas: un lector falso que escribe el mismo protocolo.
    return [process.execPath, [process.env.OBS_NOW_PLAYING_HELPER]];
  }
  if (process.platform === 'win32') {
    const ps = path.join(process.env.SystemRoot || 'C:\\Windows', 'System32', 'WindowsPowerShell', 'v1.0', 'powershell.exe');
    const script = path.join(ROOT, 'media', 'windows.ps1');
    return [ps, ['-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', script, '-CommandFile', COMMAND_PATH]];
  }
  if (process.platform === 'darwin') {
    return ['/usr/bin/osascript', ['-l', 'JavaScript', path.join(ROOT, 'media', 'macos.js')]];
  }
  return null;
}

function handleHelperLine(line) {
  let msg;
  try {
    msg = JSON.parse(line.replace(/^\uFEFF/, ''));
  } catch {
    return;
  }
  system.lastLineAt = Date.now();
  if (msg.error) {
    system.status = 'error';
    system.error = cleanString(msg.error, 500);
    return;
  }
  system.status = 'ok';
  system.error = '';

  const seen = new Set();
  for (const s of [].concat(msg.sessions || [])) {
    const app = cleanString(s.app, 200);
    const title = cleanString(s.title);
    if (!app || !title) continue;
    const artist = cleanString(s.artist);
    const { kind, label } = classifyApp(app, cleanString(s.name, 100));
    const key = `system:${app}`;
    const id = artworkId(app, title, artist);
    if (typeof s.artwork === 'string' && !storeArtwork(id, s.artwork)) artworks.delete(id);
    knownApps.set(app, label);
    seen.add(key);
    upsertTrack(key, {
      app,
      kind,
      label,
      title,
      artist,
      album: cleanString(s.album),
      artworkId: id,
      isPlaying: s.isPlaying === true,
      isCurrent: s.current === true,
      progressMs: cleanMs(s.progressMs),
      durationMs: cleanMs(s.durationMs),
    });
  }
  // Las sesiones que el sistema ya no reporta (app cerrada) desaparecen.
  for (const key of tracks.keys()) {
    if (key.startsWith('system:') && !seen.has(key)) tracks.delete(key);
  }
}

let helperFailures = 0;
function startHelper() {
  const cmd = helperCommand();
  if (!cmd) {
    system.status = 'unsupported';
    return;
  }
  const startedAt = Date.now();
  system.startedAt = startedAt;
  fs.rmSync(COMMAND_PATH, { force: true }); // un comando viejo no se ejecuta al arrancar
  const child = spawn(cmd[0], cmd[1], { stdio: ['ignore', 'pipe', 'pipe'], windowsHide: true });
  helper = child;
  readline.createInterface({ input: child.stdout, crlfDelay: Infinity }).on('line', handleHelperLine);
  let stderr = '';
  child.stderr.on('data', (d) => {
    stderr = (stderr + d).slice(-2000);
  });
  child.on('error', (err) => {
    system.status = 'error';
    system.error = `No se pudo iniciar el lector del sistema: ${err.message}`;
  });
  child.on('exit', (code) => {
    helper = null;
    // Los lectores se reinician solos cada ~30 min (código 0) para no acumular memoria.
    const quick = Date.now() - startedAt < 15000;
    helperFailures = quick ? helperFailures + 1 : 0;
    if (quick && code !== 0) {
      if (system.status !== 'error') {
        system.status = 'error';
        system.error = stderr.trim().split('\n').pop() || `El lector del sistema terminó (código ${code}).`;
      }
      console.error('[sistema]', system.error);
    }
    const wait = Math.min(60000, 1000 * 2 ** Math.min(helperFailures, 6));
    setTimeout(startHelper, quick ? wait : 500);
  });
}

// ---------------------------------------------------------------------------
// Controles: anterior / pausa / siguiente sobre lo que muestra el overlay.
// ---------------------------------------------------------------------------

const ACTIONS = new Set(['previous', 'playpause', 'next']);
// Comandos para el userscript (plan B): se los entregamos en la respuesta de su próximo latido.
const browserCommands = new Map();

function runMacCommand(action) {
  return new Promise((resolve) => {
    const child = spawn('/usr/bin/osascript', ['-l', 'JavaScript', path.join(ROOT, 'media', 'macos.js'), 'command', action], {
      stdio: ['ignore', 'pipe', 'pipe'],
    });
    let out = '';
    child.stdout.on('data', (d) => (out += d));
    child.stderr.on('data', (d) => (out += d));
    const timer = setTimeout(() => child.kill(), 5000);
    child.on('error', (err) => resolve({ ok: false, error: err.message }));
    child.on('exit', (code) => {
      clearTimeout(timer);
      resolve(code === 0 ? { ok: true } : { ok: false, error: out.trim().split('\n').pop() || `código ${code}` });
    });
  });
}

async function sendControl(action) {
  const t = currentTrack({ includePaused: true });
  if (!t) return { ok: false, error: 'No hay nada sonando.' };
  if (!t.app) {
    browserCommands.set(t.key, action);
    return { ok: true };
  }
  if (process.platform === 'darwin' && !process.env.OBS_NOW_PLAYING_HELPER) return runMacCommand(action);
  if (!helper) return { ok: false, error: 'El lector del sistema no está corriendo.' };
  // El lector revisa este archivo cada 100 ms (ver media/windows.ps1).
  writeJson(COMMAND_PATH, { cmd: action, app: t.app });
  return { ok: true };
}

// Las carátulas del userscript son URLs de Google/Spotify: las bajamos y servimos desde
// /artwork/ para que el overlay pueda leer sus colores (un canvas no deja leer
// imágenes de otro origen). Solo de estos dominios, para no convertirnos en un proxy.
const ARTWORK_HOSTS = /(^|\.)(googleusercontent\.com|ggpht\.com|ytimg\.com|scdn\.co|spotifycdn\.com)$/i;
const artworkFetches = new Set();

async function fetchArtwork(id, url) {
  if (artworks.has(id) || artworkFetches.has(id)) return;
  try {
    if (!ARTWORK_HOSTS.test(new URL(url).hostname)) return;
  } catch {
    return;
  }
  artworkFetches.add(id);
  try {
    const res = await fetch(url, { signal: AbortSignal.timeout(8000), redirect: 'error' });
    const mime = (res.headers.get('content-type') || '').split(';')[0].trim();
    if (!res.ok || !/^image\/[\w.+-]+$/.test(mime)) return;
    const data = Buffer.from(await res.arrayBuffer());
    if (data.length > 3 * 1024 * 1024) return;
    artworks.set(id, { mime, data });
    while (artworks.size > 30) artworks.delete(artworks.keys().next().value);
  } catch (err) {
    console.warn('[carátula]', err.message);
  } finally {
    artworkFetches.delete(id);
  }
}

// ---------------------------------------------------------------------------
// HTTP
// ---------------------------------------------------------------------------

function send(res, status, type, body, extraHeaders = {}) {
  res.writeHead(status, { 'Content-Type': type, 'Cache-Control': 'no-store', ...extraHeaders });
  res.end(body);
}

const sendJson = (res, status, data) => send(res, status, 'application/json; charset=utf-8', JSON.stringify(data));
const sendHtml = (res, status, html) => send(res, status, 'text/html; charset=utf-8', html);
const forbidden = (res) => send(res, 403, 'text/plain; charset=utf-8', 'Prohibido');

function sendFile(res, file, type) {
  fs.readFile(path.join(ROOT, file), (err, data) => {
    if (err) return send(res, 404, 'text/plain; charset=utf-8', 'No encontrado');
    send(res, 200, type, data);
  });
}

function readBody(req, limit = 16 * 1024) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    let size = 0;
    req.on('data', (chunk) => {
      size += chunk.length;
      if (size > limit) {
        reject(new Error('Cuerpo demasiado grande'));
        req.destroy();
      } else {
        chunks.push(chunk);
      }
    });
    req.on('end', () => resolve(Buffer.concat(chunks).toString('utf8')));
    req.on('error', reject);
  });
}

const escapeHtml = (s) =>
  String(s).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[c]);

function page(title, body) {
  return `<!doctype html><html lang="es"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>${escapeHtml(title)}</title>
<style>
  body{font-family:system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;max-width:720px;margin:40px auto;padding:0 16px;line-height:1.5;background:#111318;color:#e8e8ee}
  a{color:#7cc4ff} code{background:#1d2028;padding:2px 6px;border-radius:6px;word-break:break-all}
  .box{background:#181b22;border:1px solid #2a2e38;border-radius:12px;padding:16px 20px;margin:16px 0}
  .ok{color:#4ade80}.warn{color:#fbbf24}.err{color:#f87171}.muted{color:#9aa0ac}
  h2{font-size:17px;margin:4px 0 8px}
  .btn{display:inline-block;background:#1ed760;color:#000;font-weight:700;padding:6px 14px;border-radius:999px;text-decoration:none}
  button{font:inherit;padding:5px 12px;border-radius:8px;border:1px solid #3a3f4b;background:#232733;color:inherit;cursor:pointer}
  .danger{border-color:#7f1d1d;color:#fca5a5}
  .apps{list-style:none;padding:0;margin:8px 0 0}
  .apps li{display:flex;justify-content:space-between;align-items:center;gap:12px;padding:6px 0;border-top:1px solid #2a2e38}
  .apps form{margin:0}
  .foot{display:flex;justify-content:space-between;align-items:center;gap:12px;flex-wrap:wrap;margin:24px 0}
</style></head><body>${body}</body></html>`;
}

// El lector arrancó pero lleva más de 20 s sin escribir nada.
const systemSilent = () =>
  system.status !== 'unsupported' && system.startedAt > 0 && Date.now() - Math.max(system.lastLineAt, system.startedAt) > 20000;

function systemStatusHtml() {
  const label = process.platform === 'darwin' ? '"Ahora suena" de macOS' : 'controles multimedia de Windows';
  if (system.status === 'unsupported') return `<span class="warn">no disponible en este sistema</span> — usa el plan B de abajo.`;
  if (system.status === 'error') {
    return `<span class="err">no funciona: ${escapeHtml(system.error)}</span><br><span class="muted">Usa el plan B de abajo mientras tanto.</span>`;
  }
  if (system.status === 'starting' || Date.now() - system.lastLineAt > STALE_MS) {
    if (systemSilent()) {
      const hint = process.platform === 'win32' ? 'un antivirus o una política de la empresa puede estar bloqueando PowerShell' : 'revisa log.txt en la carpeta de datos';
      return `<span class="err">no responde</span> (${hint}).<br><span class="muted">Usa el plan B de abajo mientras tanto.</span>`;
    }
    return `<span class="warn">iniciando…</span>`;
  }
  return `<span class="ok">funcionando</span> — leyendo los ${label}.`;
}

function statusPage(notice = '') {
  const t = currentTrack();
  const now = Date.now();
  const token = `<input type="hidden" name="token" value="${CONTROL_TOKEN}">`;

  const apps = new Map(knownApps);
  for (const app of config.ignoredApps) if (!apps.has(app)) apps.set(app, classifyApp(app).label);
  const appRows = [...apps]
    .map(([app, label]) => {
      const ignored = config.ignoredApps.includes(app);
      const live = tracks.get(`system:${app}`);
      const state = ignored
        ? '<span class="muted">oculta en el overlay</span>'
        : live
          ? `${live.isPlaying ? '<span class="ok">sonando</span>' : '<span class="muted">en pausa</span>'}: ${escapeHtml(live.title)}`
          : '<span class="muted">sin reproducción</span>';
      return `<li><span><b>${escapeHtml(label)}</b> <small class="muted">${escapeHtml(app)}</small><br>${state}</span>
        <form method="post" action="/apps">${token}<input type="hidden" name="app" value="${escapeHtml(app)}">
        <button type="submit">${ignored ? 'Mostrar' : 'Ocultar'}</button></form></li>`;
    })
    .join('');

  const browserSeen = ['ytmusic', 'spotifyWeb'].some((k) => tracks.has(k) && now - tracks.get(k).updatedAt <= STALE_MS);
  const nowLabel = t ? (t.kind === 'browser' ? t.label : t.label || '') : '';

  return page(
    'OBS Now Playing',
    `<h1>🎵 OBS Now Playing</h1>
${notice ? `<div class="box ok">${escapeHtml(notice)}</div>` : ''}
<div class="box"><b>Sonando ahora:</b> ${t ? `${escapeHtml(t.artist)}${t.artist ? ' — ' : ''}${escapeHtml(t.title)} <small class="muted">(${escapeHtml(nowLabel)})</small>` : '<span class="warn">nada</span>'}</div>

<div class="box">
  <h2>1. En OBS</h2>
  <p>Agrega una <b>Fuente de navegador</b> con esta URL (ancho 640, alto 200):</p>
  <p><code id="overlayUrl">${ORIGIN}/overlay</code> <button type="button" onclick="copy('overlayUrl', this)">Copiar</button></p>
  <p><a href="/overlay?preview=1" target="_blank">Ver vista previa</a> · toma los colores de la carátula · más estilos en el README (<code>?art=big</code>, <code>?blur=1</code>, <code>?theme=light</code>, <code>?pos=br</code>…)</p>
</div>

<div class="box">
  <h2>Botones para cambiar de canción (opcional)</h2>
  <p>En OBS: <b>Docks → Docks de navegador personalizados</b>, nombre <i>Música</i> y esta URL:</p>
  <p><code id="controlsUrl">${ORIGIN}/controls</code> <button type="button" onclick="copy('controlsUrl', this)">Copiar</button></p>
  <p class="muted">Queda un panel dentro de OBS con anterior / pausa / siguiente que no sale en el stream. <a href="/controls" target="_blank">Probarlo aquí</a>.</p>
</div>

<div class="box">
  <h2>2. Pon música</h2>
  <p>Funciona solo con lo que suene en tu computador: la app de Spotify, YouTube Music o Spotify en el navegador, Apple Music… Sin cuentas, claves ni extensiones.</p>
  <p><b>Lector del sistema:</b> ${systemStatusHtml()}</p>
  ${appRows ? `<p>Apps detectadas (oculta las que no quieras en el overlay, por ejemplo el navegador si ves videos):</p><ul class="apps">${appRows}</ul>` : '<p class="muted">Aún no se detecta ninguna app: dale play a algo.</p>'}
</div>

<details class="box"${system.status === 'error' || system.status === 'unsupported' || systemSilent() || browserSeen ? ' open' : ''}>
  <summary><b>Plan B: script para el navegador</b> <span class="muted">(solo si lo de arriba no funciona)</span></summary>
  <p>Con <a href="https://www.tampermonkey.net/" target="_blank">Tampermonkey</a> instalado, pulsa
     <a class="btn" href="/now-playing.user.js">Instalar script</a>. Lee YouTube Music y Spotify web directamente desde la pestaña.</p>
  <p><b>Estado:</b> ${browserSeen ? '<span class="ok">recibiendo</span>' : '<span class="muted">sin señal</span>'}</p>
</details>

<div class="box">
  <h2>Texto para OBS (opcional)</h2>
  <p>Si prefieres una <b>Fuente de texto</b> con "Leer desde archivo", usa:<br><code>${escapeHtml(textFilePath())}</code></p>
  <form method="post" action="/open-folder">${token}<button type="submit">Abrir carpeta</button></form>
</div>

<form method="post" action="/quit" class="foot">${token}
  <small class="muted">Versión ${VERSION}. La app sigue funcionando aunque cierres esta pestaña.</small>
  <button type="submit" class="danger">Cerrar OBS Now Playing</button>
</form>
<script>
  function copy(id, btn) {
    navigator.clipboard.writeText(document.getElementById(id).textContent).then(() => { btn.textContent = '¡Copiado!'; });
  }
  // Refresca el estado (sin reenviar formularios ni cerrar lo que abriste).
  setInterval(() => { if (!document.querySelector('details[data-touched]')) location.replace('/'); }, 5000);
  document.querySelectorAll('details').forEach((d) => d.addEventListener('toggle', () => d.setAttribute('data-touched', '')));
</script>`
  );
}

// Los formularios del panel deben venir del propio panel: mismo origen y el secreto de esta ejecución.
async function readForm(req) {
  const origin = req.headers.origin;
  if (origin && origin !== ORIGIN && origin !== `http://localhost:${PORT}`) return null;
  const form = new URLSearchParams(await readBody(req));
  const a = Buffer.from(form.get('token') || '');
  const b = Buffer.from(CONTROL_TOKEN);
  return a.length === b.length && crypto.timingSafeEqual(a, b) ? form : null;
}

async function route(req, res) {
  // Solo aceptamos peticiones dirigidas a esta máquina (evita DNS rebinding).
  // No mandamos cabeceras CORS: otras páginas web no pueden leer lo que escuchas.
  if (!/^(127\.0\.0\.1|localhost)(:\d+)?$/i.test(req.headers.host || '')) return forbidden(res);
  const url = new URL(req.url, ORIGIN);

  if (req.method === 'GET' && url.pathname.startsWith('/artwork/')) {
    const art = artworks.get(url.pathname.slice('/artwork/'.length));
    if (!art) return send(res, 404, 'text/plain; charset=utf-8', 'No encontrado');
    return send(res, 200, art.mime, art.data, { 'Cache-Control': 'max-age=3600' });
  }

  switch (`${req.method} ${url.pathname}`) {
    case 'GET /':
      return sendHtml(res, 200, statusPage());

    case 'POST /apps': {
      const form = await readForm(req);
      if (!form) return forbidden(res);
      const app = cleanString(form.get('app'), 200);
      if (app) {
        const ignored = config.ignoredApps.includes(app);
        config.ignoredApps = ignored ? config.ignoredApps.filter((a) => a !== app) : [...config.ignoredApps, app];
        writeJson(CONFIG_PATH, config);
      }
      res.writeHead(303, { Location: '/' });
      return res.end();
    }

    case 'POST /open-folder': {
      if (!(await readForm(req))) return forbidden(res);
      openExternal(DATA_DIR);
      res.writeHead(303, { Location: '/' });
      return res.end();
    }

    case 'POST /quit': {
      if (!(await readForm(req))) return forbidden(res);
      sendHtml(res, 200, page('OBS Now Playing', '<h1>OBS Now Playing se cerró</h1><p>Para volver a usarlo, abre la app otra vez.</p>'));
      return setTimeout(shutdown, 300);
    }

    case 'GET /overlay':
      return sendFile(res, 'overlay.html', 'text/html; charset=utf-8');

    case 'GET /controls':
      return sendFile(res, 'controls.html', 'text/html; charset=utf-8');

    case 'GET /ui.js':
      return sendFile(res, 'ui.js', 'text/javascript; charset=utf-8');

    case 'POST /api/control': {
      // Igual que /api/browser: exigir JSON hace que otras páginas web no puedan mandarlo.
      if (!String(req.headers['content-type'] || '').startsWith('application/json')) {
        return send(res, 415, 'text/plain; charset=utf-8', 'Se espera JSON');
      }
      let body;
      try {
        body = JSON.parse(await readBody(req));
      } catch {
        return sendJson(res, 400, { error: 'JSON inválido' });
      }
      if (!ACTIONS.has(body.action)) return sendJson(res, 400, { error: 'Acción desconocida' });
      const result = await sendControl(body.action);
      return sendJson(res, result.ok ? 200 : 409, result);
    }

    case 'GET /now-playing.user.js':
      return sendFile(res, 'now-playing.user.js', 'text/javascript; charset=utf-8');

    case 'GET /api/now-playing': {
      const includePaused = url.searchParams.get('paused') === '1';
      return sendJson(res, 200, { now: Date.now(), track: publicTrack(currentTrack({ includePaused })) });
    }

    case 'POST /api/browser': {
      // Plan B (userscript). Exigir JSON obliga a cualquier página web a pasar por un
      // preflight CORS que nunca aprobamos; GM_xmlhttpRequest no tiene esa limitación.
      if (!String(req.headers['content-type'] || '').startsWith('application/json')) {
        return send(res, 415, 'text/plain; charset=utf-8', 'Se espera JSON');
      }
      let body;
      try {
        body = JSON.parse(await readBody(req));
      } catch {
        return send(res, 400, 'text/plain; charset=utf-8', 'JSON inválido');
      }
      const source = { ytmusic: ['ytmusic', 'YouTube Music'], spotify: ['spotify', 'Spotify'] }[body.source];
      if (!source) return send(res, 400, 'text/plain; charset=utf-8', 'Fuente desconocida');
      const key = source[0] === 'spotify' ? 'spotifyWeb' : 'ytmusic';
      const title = cleanString(body.title);
      if (!title) tracks.delete(key);
      else {
        const artworkUrl = cleanUrl(body.artwork);
        const artworkId = artworkUrl ? crypto.createHash('sha1').update(artworkUrl).digest('hex').slice(0, 16) : '';
        if (artworkUrl) fetchArtwork(artworkId, artworkUrl);
        upsertTrack(key, {
          kind: source[0],
          label: source[1],
          title,
          artist: cleanString(body.artist),
          album: cleanString(body.album),
          artworkId,
          isPlaying: body.isPlaying === true,
          isCurrent: false,
          progressMs: cleanMs(body.progressMs),
          durationMs: cleanMs(body.durationMs),
        });
      }
      // La respuesta lleva el comando pendiente (si el usuario pulsó un botón).
      const cmd = browserCommands.get(key);
      browserCommands.delete(key);
      return sendJson(res, 200, cmd ? { cmd } : {});
    }

    default:
      return send(res, 404, 'text/plain; charset=utf-8', 'No encontrado');
  }
}

const server = http.createServer((req, res) => {
  route(req, res).catch((err) => {
    console.error(err);
    if (!res.headersSent) send(res, 500, 'text/plain; charset=utf-8', 'Error interno');
  });
});

server.on('error', (err) => {
  if (err.code === 'EADDRINUSE') {
    // Casi siempre es la app ya abierta: mostramos su panel en vez de fallar.
    console.error(`El puerto ${PORT} ya está en uso (¿otra copia abierta?). Se puede cambiar "port" en ${CONFIG_PATH}.`);
    if (ARGS.has('--open')) {
      openExternal(`${ORIGIN}/`);
      return setTimeout(() => process.exit(0), 500);
    }
  } else {
    console.error(err);
  }
  process.exit(1);
});

server.listen(PORT, '127.0.0.1', () => {
  console.log(`🎵 OBS Now Playing ${VERSION} listo`);
  console.log(`   Panel:                   ${ORIGIN}/`);
  console.log(`   Fuente de navegador OBS: ${ORIGIN}/overlay`);
  console.log(`   Carpeta de datos:        ${DATA_DIR}`);
  fs.writeFileSync(CONTROL_PATH, CONTROL_TOKEN);
  setInterval(() => {
    try {
      if (fs.readFileSync(CONTROL_PATH, 'utf8') === 'stop') shutdown();
    } catch {
      // si alguien borró el archivo, lo ignoramos
    }
  }, 1000);
  if (ARGS.has('--open')) openExternal(`${ORIGIN}/`);
  startHelper();
});
