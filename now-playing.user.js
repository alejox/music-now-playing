// ==UserScript==
// @name         OBS Now Playing (YouTube Music + Spotify Web)
// @namespace    obs-now-playing
// @version      1.0.0
// @description  Envía la canción que suena en YouTube Music o en el reproductor web de Spotify al overlay de OBS.
// @match        https://music.youtube.com/*
// @match        https://open.spotify.com/*
// @grant        GM_xmlhttpRequest
// @connect      127.0.0.1
// @run-at       document-idle
// @noframes
// ==/UserScript==

(function () {
  'use strict';

  // Si cambias "port" en config.json, cámbialo también aquí.
  const ENDPOINT = 'http://127.0.0.1:8787/api/browser';
  const SOURCE = location.hostname === 'music.youtube.com' ? 'ytmusic' : 'spotify';

  const artworkSize = (a) => {
    const m = /(\d+)x(\d+)/.exec(a.sizes || '');
    return m ? Number(m[1]) * Number(m[2]) : 0;
  };

  // "1:02:03" / "3:45" → milisegundos
  const parseClock = (text) => {
    if (!text) return 0;
    const parts = text.trim().split(':').map(Number);
    if (parts.some((n) => !Number.isFinite(n))) return 0;
    return parts.reduce((acc, n) => acc * 60 + n, 0) * 1000;
  };

  function mediaElement() {
    return [...document.querySelectorAll('video, audio')].find((m) => m.currentSrc || m.src) || null;
  }

  function snapshot() {
    const md = navigator.mediaSession && navigator.mediaSession.metadata;
    if (!md || !md.title) return null;

    const art = [...(md.artwork || [])].sort((a, b) => artworkSize(b) - artworkSize(a))[0];
    const media = mediaElement();
    let isPlaying = navigator.mediaSession.playbackState === 'playing';
    let progressMs = 0;
    let durationMs = 0;

    if (media && Number.isFinite(media.duration) && media.duration > 0) {
      isPlaying = !media.paused && !media.ended;
      progressMs = media.currentTime * 1000;
      durationMs = media.duration * 1000;
    } else if (SOURCE === 'spotify') {
      // El reproductor web de Spotify no siempre deja el <audio> en el DOM: leemos la barra.
      progressMs = parseClock(document.querySelector('[data-testid="playback-position"]')?.textContent);
      durationMs = parseClock(document.querySelector('[data-testid="playback-duration"]')?.textContent);
    }

    return {
      source: SOURCE,
      title: md.title,
      artist: md.artist || '',
      album: md.album || '',
      artwork: art ? art.src : '',
      url: location.href,
      isPlaying,
      progressMs,
      durationMs,
    };
  }

  function send() {
    const data = snapshot();
    if (!data) return;
    GM_xmlhttpRequest({
      method: 'POST',
      url: ENDPOINT,
      headers: { 'Content-Type': 'application/json' },
      data: JSON.stringify(data),
      timeout: 2000,
      onerror() {}, // el servidor puede no estar abierto: no pasa nada
    });
  }

  setInterval(send, 1000);
})();
