// Código compartido por overlay.html y controls.html: colores de la carátula,
// formato de tiempo y botones de control.
'use strict';

const NowPlayingUI = (() => {
  function rgbToHsl(r, g, b) {
    r /= 255;
    g /= 255;
    b /= 255;
    const max = Math.max(r, g, b);
    const min = Math.min(r, g, b);
    const l = (max + min) / 2;
    if (max === min) return [0, 0, l];
    const d = max - min;
    const s = l > 0.5 ? d / (2 - max - min) : d / (max + min);
    const h = max === r ? (g - b) / d + (g < b ? 6 : 0) : max === g ? (b - r) / d + 2 : (r - g) / d + 4;
    return [h / 6, s, l];
  }

  const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
  const hsl = ([h, s, l], a = 1) => `hsla(${Math.round(h * 360)}, ${Math.round(s * 100)}%, ${Math.round(l * 100)}%, ${a})`;

  // Saca dos colores de la carátula:
  //   accent:     el color más vivo (no el más abundante: una carátula casi negra con
  //               un logo rojo debe dar rojo), ajustado para leerse sobre el fondo.
  //   background: el color dominante, muy oscuro (o muy claro en tema claro).
  // La imagen tiene que ser del mismo origen; por eso todas las carátulas pasan por /artwork/.
  function extractColors(img, theme = 'dark') {
    const size = 48;
    const canvas = document.createElement('canvas');
    canvas.width = canvas.height = size;
    const ctx = canvas.getContext('2d', { willReadFrequently: true });
    ctx.drawImage(img, 0, 0, size, size);
    const { data } = ctx.getImageData(0, 0, size, size);

    // Agrupa píxeles parecidos (4 bits por canal) y promedia cada grupo.
    const buckets = new Map();
    for (let i = 0; i < data.length; i += 4) {
      if (data[i + 3] < 128) continue;
      const key = ((data[i] >> 4) << 8) | ((data[i + 1] >> 4) << 4) | (data[i + 2] >> 4);
      const b = buckets.get(key) || { r: 0, g: 0, b: 0, n: 0 };
      b.r += data[i];
      b.g += data[i + 1];
      b.b += data[i + 2];
      b.n++;
      buckets.set(key, b);
    }
    if (!buckets.size) return null;

    let dominant = null;
    let vibrant = null;
    let bestScore = 0;
    for (const b of buckets.values()) {
      const color = rgbToHsl(b.r / b.n, b.g / b.n, b.b / b.n);
      if (!dominant || b.n > dominant.n) dominant = { color, n: b.n };
      const [, s, l] = color;
      if (s < 0.3 || l < 0.15 || l > 0.9) continue;
      const score = Math.sqrt(b.n) * s * s * (1 - Math.abs(l - 0.55));
      if (score > bestScore) {
        bestScore = score;
        vibrant = color;
      }
    }

    const dark = theme !== 'light';
    const [dh, ds] = dominant.color;
    const base = vibrant || dominant.color;
    const accent = [
      base[0],
      vibrant ? clamp(base[1], 0.55, 1) : clamp(base[1], 0, 0.35),
      dark ? clamp(base[2], 0.6, 0.75) : clamp(base[2], 0.3, 0.42),
    ];
    const background = [dh, clamp(ds, 0, 0.45), dark ? 0.11 : 0.93];
    return { accent: hsl(accent), background: hsl(background, dark ? 0.86 : 0.92), track: hsl(accent, 0.25) };
  }

  const fmt = (ms) => {
    const s = Math.max(0, Math.floor(ms / 1000));
    const h = Math.floor(s / 3600);
    const m = Math.floor((s % 3600) / 60);
    const sec = String(s % 60).padStart(2, '0');
    return h ? `${h}:${String(m).padStart(2, '0')}:${sec}` : `${m}:${sec}`;
  };

  // Manda un comando al reproductor (previous | playpause | next).
  async function control(action) {
    const res = await fetch('/api/control', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ action }),
    });
    if (!res.ok) throw new Error((await res.json().catch(() => ({}))).error || `HTTP ${res.status}`);
  }

  const ICONS = {
    previous: '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M6 5h2v14H6zM20 5v14L9.5 12z"/></svg>',
    play: '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M7 4.5v15L19.5 12z"/></svg>',
    pause: '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M6 4.5h4.2v15H6zM13.8 4.5H18v15h-4.2z"/></svg>',
    next: '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M16 5h2v14h-2zM4 5v14l10.5-7z"/></svg>',
  };

  return { extractColors, fmt, control, ICONS };
})();
