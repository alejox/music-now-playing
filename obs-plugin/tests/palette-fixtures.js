// Imágenes de prueba de 48 × 48 para la paleta (las mismas que dibuja tests/palette-test.cpp)
// y cómo se sacan los valores esperados: extractColors() de data/overlay/ui.js en Chromium, con
// el RGB que da el propio navegador para cada hsla().
//
//   node obs-plugin/tests/palette-fixtures.js   (necesita playwright con Chromium)
//
// Imprime las líneas que van en EXPECTED de palette-test.cpp.
'use strict';

const SIZE = 48;

function fill(px, x0, y0, x1, y1, [r, g, b, a = 255]) {
  for (let y = y0; y < y1; y++) {
    for (let x = x0; x < x1; x++) {
      const i = 4 * (y * SIZE + x);
      px[i] = r;
      px[i + 1] = g;
      px[i + 2] = b;
      px[i + 3] = a;
    }
  }
}

const FIXTURES = {
  // Casi negra con un logo rojo: el acento es el rojo, no el negro.
  red_on_black(px) {
    fill(px, 0, 0, SIZE, SIZE, [10, 10, 12]);
    fill(px, 18, 18, 30, 30, [220, 30, 40]);
  },
  // Degradado de atardecer con un sol.
  sunset(px) {
    for (let y = 0; y < SIZE; y++) fill(px, 0, y, SIZE, y + 1, [40 + 4 * y, 20 + 2 * y, Math.max(0, 90 - y)]);
    fill(px, 16, 6, 32, 18, [255, 190, 80]);
  },
  // Gris: no hay color vivo, el acento sale del dominante con poca saturación.
  gray(px) {
    fill(px, 0, 0, SIZE, SIZE, [128, 128, 128]);
  },
  // Mitad verde azulado, mitad naranja: empate en cantidad (gana el primero que aparece).
  teal_orange(px) {
    fill(px, 0, 0, SIZE / 2, SIZE, [20, 150, 140]);
    fill(px, SIZE / 2, 0, SIZE, SIZE, [240, 120, 30]);
  },
};

function pixels(name) {
  const px = new Uint8ClampedArray(SIZE * SIZE * 4);
  FIXTURES[name](px);
  return px;
}

module.exports = { SIZE, FIXTURES, pixels };

if (require.main === module) {
  const path = require('path');
  const fs = require('fs');
  const { chromium } = require('playwright');
  const ui = fs.readFileSync(path.join(__dirname, '..', 'data', 'overlay', 'ui.js'), 'utf8');
  (async () => {
    const browser = await chromium.launch();
    const page = await browser.newPage();
    await page.setContent('<!doctype html><body></body>');
    await page.addScriptTag({ content: ui });
    for (const name of Object.keys(FIXTURES)) {
      const data = Array.from(pixels(name));
      const result = await page.evaluate(({ data, size }) => {
        const canvas = document.createElement('canvas');
        canvas.width = canvas.height = size;
        canvas.getContext('2d').putImageData(new ImageData(new Uint8ClampedArray(data), size, size), 0, 0);
        const c = NowPlayingUI.extractColors(canvas, 'dark');
        const rgb = (css) => {
          const el = document.createElement('i');
          el.style.color = css;
          document.body.appendChild(el);
          const [r, g, b] = getComputedStyle(el).color.match(/\d+/g).map(Number);
          el.remove();
          return ((r << 16) | (g << 8) | b) >>> 0;
        };
        return { css: [c.accent, c.glass(1), c.solid(1)], rgb: [rgb(c.accent), rgb(c.glass(1)), rgb(c.solid(1))] };
      }, { data, size: SIZE });
      const hex = (v) => `0x${v.toString(16).padStart(6, '0').toUpperCase()}`;
      console.log(`\t{"${name}", ${hex(result.rgb[0])}, ${hex(result.rgb[1])}, ${hex(result.rgb[2])}}, // ${result.css.join(' · ')}`);
    }
    await browser.close();
  })();
}
