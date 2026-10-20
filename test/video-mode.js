// Prueba del modo video de la tarjeta del plugin (obs-plugin/data/overlay) en un
// Chromium real con internet: necesita YouTube, así que corre en GitHub Actions.
//   node test/video-mode.js <url de index.html> <carpeta para capturas>
'use strict';
const { chromium } = require('playwright');
const assert = require('node:assert/strict');

const [url, outDir = '.'] = process.argv.slice(2);
// Big Buck Bunny (Blender Foundation): público y se deja insertar.
const VIDEO_ID = 'aqz-KE-bpKQ';

const config = {
  theme: 'dark', layout: 'horizontal', style: 'solid', opacity: 85, art: 'normal', media: 'video',
  colors: true, blur: false, progress: true, label: true, preview: false,
};

(async () => {
  // El Chromium completo (no el "headless shell"): más parecido al navegador de OBS.
  const browser = await chromium.launch({ channel: 'chromium', args: ['--autoplay-policy=no-user-gesture-required'] });
  try {
    for (const layout of ['horizontal', 'vertical']) {
      const page = await browser.newPage({ viewport: layout === 'vertical' ? { width: 320, height: 420 } : { width: 480, height: 150 } });
      page.on('pageerror', (e) => console.log('error JS:', e.message));
      page.on('console', (m) => ['error', 'warning'].includes(m.type()) && console.log(`consola (${m.type()}):`, m.text().slice(0, 200)));
      page.on('requestfailed', (r) => /youtube|ytimg|googlevideo/.test(r.url()) && console.log('falló:', r.url().slice(0, 120), r.failure()?.errorText));
      const state = () =>
        page.evaluate(() => ({
          api: typeof YT !== 'undefined' && Boolean(YT.Player),
          ready: video.ready,
          id: video.id,
          playerState: video.ready ? video.player.getPlayerState() : null,
          failed: [...video.failed],
          visible: !document.getElementById('videoBox').hidden,
        }));
      await page.goto(url);

      const start = Date.now();
      const track = (videoId) => ({
        title: 'Big Buck Bunny', artist: 'Blender Foundation', kind: 'ytmusic', sourceLabel: 'YouTube Music',
        isPlaying: true, progressMs: 30000 + (Date.now() - start), durationMs: 634000, videoId,
      });
      // Igual que el plugin: un evento por segundo.
      const beat = (videoId) =>
        page.evaluate((d) => window.dispatchEvent(new CustomEvent('obs-now-playing', { detail: d })), {
          config: { ...config, layout },
          track: track(videoId),
        });

      let visible = false;
      for (let i = 0; i < 40 && !visible; i++) {
        await beat(VIDEO_ID);
        await page.waitForTimeout(1000);
        visible = await page.evaluate(() => !document.getElementById('videoBox').hidden);
        if (i % 5 === 4) console.log(`${layout} a los ${i + 1} s:`, await state());
      }
      if (!visible) {
        await page.screenshot({ path: `${outDir}/video-${layout}-fallo.png` });
        // Lo que muestra el reproductor por dentro (p. ej. "confirma que no eres un bot").
        const frame = page.frames().find((f) => /youtube/.test(f.url()));
        const text = frame ? await frame.evaluate(() => document.body.innerText).catch(() => '') : '';
        console.log('texto del reproductor:', text.slice(0, 120));
        // YouTube bloquea las IPs de centros de datos (como las de GitHub) con "confirma que
        // no eres un bot". En una casa no pasa. Aquí solo se puede probar que la tarjeta
        // vuelve a la carátula y no se queda con una caja negra.
        if (/not a bot|no eres un bot/i.test(text)) {
          const s = await state();
          assert.ok(s.api && s.ready, 'el reproductor de YouTube ni siquiera cargó');
          assert.ok(!s.visible && s.failed.includes(VIDEO_ID), 'con el video bloqueado, la tarjeta no volvió a la carátula');
          console.log('⚠ YouTube bloquea este servidor: se comprobó solo la vuelta a la carátula (OK).');
          await page.close();
          console.log('✔ modo video: reproductor carga y cae a la carátula cuando YouTube no deja reproducir');
          return;
        }
      }
      assert.ok(visible, `${layout}: el video no apareció en 40 s`);

      // Dejarlo correr unos segundos con los latidos y medir el desfase.
      for (let i = 0; i < 6; i++) {
        await beat(VIDEO_ID);
        await page.waitForTimeout(1000);
      }
      const drift = await page.evaluate(() => Math.abs(video.player.getCurrentTime() - expectedSeconds()));
      const muted = await page.evaluate(() => video.player.isMuted());
      console.log(`${layout}: video visible, desfase ${drift.toFixed(2)} s, sin sonido: ${muted}`);
      assert.ok(drift < 3, `${layout}: desfase de ${drift} s`);
      assert.ok(muted, `${layout}: el video tiene sonido`);
      await page.screenshot({ path: `${outDir}/video-${layout}.png` });

      // Un video que no existe: vuelve a la carátula y no lo reintenta.
      for (let i = 0; i < 8; i++) {
        await beat('xxxxxxxxxxx');
        await page.waitForTimeout(1000);
      }
      const fallback = await page.evaluate(() => ({ hidden: document.getElementById('videoBox').hidden, failed: video.failed.has('xxxxxxxxxxx') }));
      console.log(`${layout}: video inexistente →`, fallback);
      assert.ok(fallback.hidden, `${layout}: con un video inválido sigue mostrando el reproductor`);
      await page.close();
    }
    console.log('✔ modo video OK');
  } finally {
    await browser.close();
  }
})().catch((err) => {
  console.error(err);
  process.exit(1);
});
