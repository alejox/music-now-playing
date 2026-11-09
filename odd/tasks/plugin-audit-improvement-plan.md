# Plan: Music Now Playing plugin audit and improvements

The plugin works, CI is green and the structure fits its size, so nothing here calls for a rewrite. The real exposure is in three places: the self-updater installs code without verifying it, the on-stream card and error messages are Spanish-only and disappear in common situations, and none of the C++ that auto-updates onto users' machines has a test. This plan lists every problem found, the evidence, and the fix, in the order to tackle them.

Status: **plan only, nothing implemented.** Audit date 2026-11-08.

## Baseline

| Item | Value |
|---|---|
| Audited code | `origin/main` `b8b5f2c`, app 2.4.31 / plugin 1.0.39 (the version installed in OBS on the Mac) |
| Local clone | `main` `91c958d`, plugin 1.0.36, **18 commits behind**. Run `git pull --ff-only` before implementing; line numbers below are from 1.0.39 |
| Tests | `npm test` passes (smoke + Linux reader), run on the local 1.0.36 tree |
| CI | Latest CI, Plugin and Release runs on `main` are green; release v2.4.31 has the 3 zips |
| Reviews run | Technical defects, UX/UI, architecture, OBS best practices (official docs, `obs-plugintemplate`, forum policy) |

How to read the **Status** column:

- **V** — verified by re-reading the cited code after the review.
- **A** — the reviewer cited the lines; not re-checked a second time.
- **R** — the code path exists, but the effect needs a runtime check (named in the row).

Effort: S = hours, M = 1-2 days, L = several days.

## Order of work

1. **Phase 1 — Safety and stability.** Things that can harm a user's machine or crash OBS live.
2. **Phase 2 — What viewers and new users see.** Wrong language on stream, invisible card, misleading states.
3. **Phase 3 — Distribution and OBS alignment.** Signing, minimum versions, deprecated API, release pipeline.
4. **Phase 4 — Structure and tests.** A test seam and a documented reader contract.
5. **Phase 5 — UX polish and performance.** Properties layout, dock ergonomics, theme quality, polling cost.

Item 4.1 (extract pure logic + test target) is a pure move with low risk. Doing it before 1.5, 1.6 and 2.6 lets those fixes ship with tests.

## Decisions needed

| # | Decision | Recommendation |
|---|---|---|
| D1 | Pay for an Apple Developer account to sign and notarize the macOS plugin? | **Decided 2026-11-08: no.** macOS stays ad-hoc signed; 3.1 becomes "document the `xattr -cr` step clearly". |
| D2 | What happens to the legacy root app (`server.js`, `overlay.html`, `controls.html`, `build/`)? | Move to `legacy/` and make `obs-plugin/data/` the canonical copy of the helpers. It is no longer published but still drives the release tag and CI. |
| D3 | Card behaviour when the music is paused | Add a property: hide / dim / hide after N seconds, default "hide after 10 s". |
| D4 | Default player for a new source | "Automatic" instead of "Same as the dock", so a private dock control does not change the stream by surprise. |
| D5 | Linux minimum OBS version | Pin the libobs the build uses to the declared minimum; otherwise state the real minimum in the README. |
| D6 | List the plugin on the OBS forum? | If yes, read the forum policy first: per the research pass it requires an AI-usage disclaimer and restricts mostly-AI-written resources. Not re-fetched for this plan. |

## Phase 1 — Safety and stability

| ID | Problem | Evidence | Fix | Effort | Status |
|---|---|---|---|---|---|
| 1.1 | The updater installs whatever it downloads. There is no hash or signature check; the only guard is an `https://github.com/` prefix. | `obs-plugin/src/updater.cpp:435-459, 541` | Publish a SHA-256 (or minisign signature) per asset and verify it against a key compiled into the plugin before extracting. Add `--proto =https --proto-redir =https` to curl. | M | V |
| 1.2 | On Windows the install script is written to the user's temp folder and then run elevated with `-ExecutionPolicy Bypass` through an unqualified `powershell.exe`. Any same-user process can swap the script or the DLL before the UAC prompt is accepted. | `updater.cpp:262-291` | Pass the script with `-EncodedCommand`, use the absolute System32 path, and re-verify the DLL hash on the elevated side. | M | V |
| 1.3 | The update prompt is a modal shown 5 s after OBS loads, with "Update now" as the default button, and nothing checks whether the user is streaming or recording. | `updater.cpp:488-505`; `plugin-main.cpp:44`; no `obs_frontend_*_active` call in `src/` | Skip the automatic prompt while any output is active, make "Later" the default, show a non-modal badge in the dock, add a "check on startup" setting. | S-M | V |
| 1.4 | Data race: `np_update` reads `MediaWatcher` state (`tracks_`, `player_`) that the UI thread mutates; `sourcesMutex` does not cover it. libobs defers `update` of video sources to the video thread. | `now-playing-source.cpp:228-241, 104, 66-71`; `media-watcher.cpp:329-338, 345-366` | `np_create`/`np_update` only store settings, then queue the push onto the UI thread (the file already does this for `syncVideoWanted`, `:482-485`). | S | R — log the thread id in `np_update` or run TSan |
| 1.5 | The artwork cache evicts an arbitrary entry once it holds 30 covers (`erase(begin())` on a `QHash`). The evicted cover can belong to the song now playing, and helpers send artwork once per song. Candidate cause for the missing YouTube Music covers on macOS; not proven. | `media-watcher.cpp:316-317` | Evict the oldest entry and never evict keys of live sessions. | S | V |
| 1.6 | Blocking network lookups (artwork, video id) run inside the 1 Hz reader loop with no timeout. A slow network at song change can silence the reader past the 10 s staleness limit, so the card vanishes. On Windows the command file holds one command, so repeated "next" clicks can be lost. | `windows.ps1:118-134, 512-516`; `macos.js:478, 491-493`; `media-watcher.cpp:90, 352` | Emit the session first, resolve artwork and video asynchronously with 3-4 s timeouts, make Windows commands a queue. | M-L | R — throttle the network during a song change |
| 1.7 | The Windows volume slider calls `SetMute(FALSE)` on every change, matches sessions by exe name, re-enumerates all audio endpoints on the UI thread every 2.5 s, and never restores the level. It un-mutes an app the user muted and lowers a Discord or Meet call in the same browser. | `app-volume.cpp:39-105` (`:101` verified); `media-watcher.cpp:462, 483` | Only unmute what the plugin muted, restore the original level on exit, cache session pointers or move enumeration to a worker. | M | V (`SetMute`), A (rest) |
| 1.8 | The music channel is found by its translated name. Changing the OBS language orphans it and creates a duplicate, and a user's own source with that name gets capture settings written onto it. | `music-channel.cpp:143-144, 196-202` | Identify the channel with a private settings marker or stored UUID; check the source id before updating. | S | V |
| 1.9 | The updater blocks the UI thread: `WaitForSingleObject` up to 180 s (a timeout is reported as failure while the script still runs), synchronous `tar`/`ditto`, no download error handler, first matching asset wins instead of the highest version. | `updater.cpp:157-161, 449-457, 531-541, 551, 558` | Run install steps off the UI thread, handle `errorOccurred`, pick the highest version. | S-M | A |

## Phase 2 — What viewers and new users see

| ID | Problem | Evidence | Fix | Effort | Status |
|---|---|---|---|---|---|
| 2.1 | On-stream text is hardcoded Spanish. An English user broadcasts "Escuchando en Spotify" although the locale key exists. | `obs-plugin/data/overlay/index.html:2, 599, 641-642, 902` | Send localized strings in the `config` payload and add an editable "label text" property (stream language can differ from OBS language). | S | V |
| 2.2 | Reader and command errors are Spanish literals shown in the dock, so English users see mixed-language errors. | `linux.sh:62`; `macos.js:232, 336`; `windows.ps1:88-91`; `media-watcher.cpp:58, 87, 221`; about 12 strings in `updater.cpp:122-609` | Helpers emit stable error codes; C++ maps them to locale keys. Move the C++ literals to the `.ini` files. | M | A |
| 2.3 | A new source is fully transparent when nothing plays and gives no status. Reader errors (missing `playerctl`, permission denied) appear only in the dock; a missing obs-browser only logs. It looks broken. | `index.html:890-893`; `now-playing-source.cpp:104, 204-207, 263` | Read-only status line at the top of Properties ("Reader OK · Spotify — Song" or the error), and show the sample card until the first real track. | M | A |
| 2.4 | The card disappears on pause because the source requests playing tracks only. The paused designs in the CSS are unreachable. | `now-playing-source.cpp:104`; `index.html:67, 490, 919` | Implement D3 with a short grace period to avoid flicker between tracks. | M | V |
| 2.5 | The dock's player picker silently changes what is on stream, because sources default to "Same as the dock". When a source picks a player explicitly, the dock buttons can target a different player with no indication. | `now-playing-source.cpp:66-71, 249`; `now-playing-dock.cpp:400-434` | Implement D4, and show "Card follows this" / "Card: Spotify only" next to the picker. | S-M | V |
| 2.6 | Player selection misses valid cases. macOS: `site` only comes from `/watch` URLs, so "Only YouTube Music" on the library page reports "not playing". Linux: kind `system` is never produced but all four players are offered. | `macos.js:541-566, 651, 726`; `linux.sh:204-206`; `media-watcher.cpp:42-88`; `now-playing-dock.cpp:420-423` | Merge the matching tab's `site`/`videoId` into the announced entry; hide players that cannot match on the current OS. | S-M | A |
| 2.7 | Text is fixed in px and does not scale with the source: an 18 px title and a 9.5-11 px label on a 480x150 card. Enlarging means an OBS transform. | `index.html:127-130, 552, 570-573` | A "Size" slider driving a CSS scale together with the browser source resolution. | M | A; blur on transform is R |
| 2.8 | Horizontal Synthwave title is illegible: pink script title and cyan artist sit on the pink/orange sun. | `shots/horizontal-synthwave-dark.png`; `index.html:246-260` | Move the sun to the right as Neon City does, or add a dark scrim behind `.meta`. | S | A |
| 2.9 | Low-contrast secondary text in the Cyber, Neon City, Glass and macOS themes. | `shots/*-cyber-dark.png`, `vertical-neoncity-dark.png`, `horizontal-glass-dark.png`, `horizontal-macos-dark.png` | Per-theme scrim behind `.meta`, white duration text, higher accent lightness floor on glass. | S | A |
| 2.10 | Docs drift: the English README calls the dock "Música" and says the UI is Spanish; the hero image shows the legacy overlay; `shots/` is not referenced anywhere. | `README.md:67-68, 76`; `docs/overlay.png` | Fix the README, add a style gallery from `shots/`, new hero image. | S | A |

## Phase 3 — Distribution and OBS alignment

Sources read by the research pass (primary, not re-fetched for this plan): OBS docs `reference-sources`, `reference-properties`, `reference-frontend-api`; OBS release notes 32.0.0 and 33.0.0; `obsproject/obs-plugintemplate` and its macOS codesigning wiki; the OBS forum resource policy thread.

| ID | Problem | Evidence | Fix | Effort | Status |
|---|---|---|---|---|---|
| 3.1 | macOS build is ad-hoc signed and not notarized. Gatekeeper rejects the installed bundle (`spctl: rejected`), every install needs `xattr -cr`, and the updater strips quarantine itself. | `.github/workflows/plugin.yml:49-76`; `updater.cpp:603`; `codesign -dv` on the installed plugin | Depends on D1: adopt the template's Developer ID signing and notarization, ideally with a `.pkg`. | M | V |
| 3.2 | The Linux zip is built against whatever OBS the PPA ships. Since OBS 32.0, OBS refuses plugins built for a newer OBS, so the real minimum can be higher than the "OBS 31+" the docs state. | `plugin.yml:156-162`; `release.yml:75` | Depends on D5. | M | V (unpinned PPA), A (32.0 rule) |
| 3.3 | `obs_properties_add_button` is deprecated as of OBS 32.1; the Linux build suppresses the warning instead of fixing it. | `now-playing-source.cpp:400, 410`; `cmake/linux/compilerconfig.cmake:17-18` | Switch to `obs_properties_add_button2` and drop the suppression. | S | V (usage), A (deprecation) |
| 3.4 | The frontend event callback is removed in `obs_module_unload`, after `OBS_FRONTEND_EVENT_EXIT`. The frontend API should not be called after EXIT. | `plugin-main.cpp:40, 65, 76` | Remove the callback inside the EXIT handler. | S | V |
| 3.5 | Two version sources bumped by hand: the release tag comes from the legacy `package.json` (2.4.x) and assets from `buildspec.json` (1.0.x); `server.js:15` is a third. | `release.yml:34-39` | Derive the tag from `buildspec.json`. The updater matches asset names, not tags, so it is unaffected. | S | V |
| 3.6 | Release hygiene: no checksums published, Windows DLL unsigned, actions pinned by tag with `contents: write`, release gated on live YouTube/iTunes lookups. | `release.yml:17-18, 58`; `plugin.yml:29-42`; `ci.yml:96-97, 124, 128` | Publish hashes (needed by 1.1), pin actions by SHA, make network tests retried or non-blocking. | M | A |
| 3.7 | Undisclosed network use: the overlay loads Google Fonts at runtime, plus a YouTube iframe and YouTube search. The Privacy section lists only the iTunes lookup and the update check. The fonts are Latin-only. | `index.html:7, 779`; `macos.js:400, 592`; `README.md:165-169` | Bundle the woff2 files with CJK/Cyrillic fallbacks and list the remaining requests under Privacy. | S | V (fonts), A (rest) |
| 3.8 | Licence: the zips ship no licence text, most `.cpp` files have no header, and a template placeholder header and email remain. | `plugin.yml` (no LICENSE copy step); `src/plugin-support.c.in:2-3`; `buildspec.json:44` | Ship `LICENSE` in `data/`, add headers, replace placeholders. | S | V (zip), A (rest) |
| 3.9 | The README tells Flatpak users to run `flatpak override --talk-name=org.freedesktop.Flatpak`, which opens the sandbox. | `obs-plugin/README.md:40` | Document Flatpak as unsupported, or build a Flatpak extension (L). | S | A |
| 3.10 | CI has no format check and macOS/Windows use non-CI presets, so warnings are not errors. | `plugin.yml:62, 102` | Add the template's `check-format` job and CI presets. | S | A |
| 3.11 | Declared support is inconsistent: the root badge says "OBS 31+" but Windows needs OBS 33+; Windows is x64 only while OBS 33 ships arm64. | `README.md:11`; `CMakePresets.json:59-60` | State the minimum per OS; decide on Windows arm64 later. | S | A |

## Phase 4 — Structure and tests

| ID | Problem | Evidence | Fix | Effort | Status |
|---|---|---|---|---|---|
| 4.1 | No C++ test target exists. `MediaWatcher` fuses parsing, app classification, track selection, process lifecycle, commands and persistence in a singleton that needs libobs and `QProcess`. `compareVersions` is public "to be testable" and has no test. | `obs-plugin/CMakeLists.txt` (no test target); `media-watcher.cpp:42-88, 247-366`; `updater.hpp:33` | Extract `src/track-model.{hpp,cpp}` (QtCore only): `classifyApp`, `parseReaderLine`, `selectCurrent`, `Track::progressNow`, `compareVersions`, `suggestedSize`. Add a QtTest/ctest target in CI. | M | V (no target), A (rest) |
| 4.2 | The reader contract is implicit, unversioned and differs per OS: `name` is missing on Windows, `shuffle` is in the session on Windows only, errors travel on different channels, capabilities are inferred from absent fields. | `windows.ps1:28-32, 88-91, 498-500`; `linux.sh:7-10, 340, 472`; `macos.js:2, 232, 344, 813`; `media-watcher.cpp:299-305, 444-454` | Add `PROTOCOL.md` next to the helpers, a `"v":1` field on the first line, one fixture-based contract test per helper, and a normalised `commandError`. Keep the Windows file transport. | M | A |
| 4.3 | The legacy root app is a hidden dependency: the canonical helpers are the root copies kept identical by `cmp` in CI, CI runs the root copies, and the plugin ships dead `fetch('/api/control')` code. `server.js` holds a second, drifted `classifyApp`. | `plugin.yml:24-26`; `ci.yml:93, 114-133`; `obs-plugin/data/overlay/ui.js:95-102`; `server.js:148-166` | Depends on D2. | S-M | A |
| 4.4 | Player and style vocabulary is loose strings repeated across layers. Adding a player touches about 8 places; drift already exists (Spotify is `#1db954` in the dock and `#1ed760` in the overlay; the accent clamp differs too). | `now-playing-dock.cpp:67-76, 150`; `index.html:613`; `ui.js:70`; `media-watcher.cpp:350-351, 372`; `music-channel.cpp:31-36` | One `players` table (key, locale key, colour, match rules) in `track-model.hpp`, consumed by watcher, dock and source, with colours sent to the overlay in `config`. | S | V (colour drift), A (rest) |
| 4.5 | Ad hoc persistence and logging: four one-value text files each with its own read/write copy, fixed temp names shared between OBS instances, `obs_log` mixed with raw `blog`, the dock logs nothing, invalid JSON lines dropped silently, an unbounded retry log line every 3 s. | `media-watcher.cpp:92-98, 131-133, 229, 251-252`; `now-playing-dock.cpp:377-383`; `updater.cpp:74-96, 297-303`; `music-channel.cpp:159-161` | One `configValue`/`setConfigValue` pair, per-instance temp names, `obs_log` everywhere, rate-limited retry log. | S | A |
| 4.6 | The dock file mixes layout, glyph painting, colour extraction and persistence; `media-watcher` and `music-channel` include each other. | `now-playing-dock.cpp:111-303, 377-383`; `media-watcher.cpp:3`; `music-channel.cpp:2` | Move glyphs and `accentFrom` to `src/dock-icons.{hpp,cpp}`. Do not split the widget further. | S | A |

Suggested order, each step shippable: 4.1 (pure move + ctest) → 4.2 → 4.4 → 4.3 → 4.6 → 4.5.

## Phase 5 — UX polish and performance

| ID | Problem | Evidence | Fix | Effort | Status |
|---|---|---|---|---|---|
| 5.1 | Properties are 19 flat controls. Theme and "Use album art colors" do nothing for the 9 themed styles; "Video shape" shows when media is artwork; Width/Height sit on top yet are overwritten on layout change. | `now-playing-source.cpp:282-308, 323-411`; `index.html:690-694` | `obs_properties_add_group` (Song / Look / Artwork and video / Details) and modified callbacks that hide irrelevant options. | M | A |
| 5.2 | Styles are picked blind: 13 names in a combo with no preview, and with nothing playing the switch shows nothing. | `now-playing-source.cpp:354-368` | Force the sample card while no track plays (shares work with 2.3). | S | A |
| 5.3 | The dock cannot get narrow or short: fixed ~268 px transport row, fixed 168 px artwork, no compact mode. | `now-playing-dock.cpp:38-43, 504-537` | Below a size threshold, shrink or hide the artwork and drop the ±10 s buttons. | M | A; minimum height is R |
| 5.4 | The dock is mouse-only: every control is `Qt::NoFocus`, there are no accessible names and no OBS hotkeys. | `now-playing-dock.cpp:410, 485, 512, 526, 550, 571` | `TabFocus`, accessible names, and `obs_hotkey_register_frontend` for play/pause, next, previous. | S-M | A |
| 5.5 | What the volume slider does is explained only in a tooltip: it changes what the streamer hears too. The stream-only route ("Create music channel") is buried at the bottom of source Properties and its tooltip key is unused. | `en-US.ini:80, 111`; `now-playing-source.cpp:410` | Caption the row "Player volume", add a "Stream-only volume…" entry to the dock, wire the tooltip. | S | A |
| 5.6 | No motion controls: disc spin, equalizers and glitch run forever; track changes swap text instantly. | `index.html:204, 360-372, 560, 903-904` | "Animations: full / subtle / off" property and a 200 ms crossfade. | S-M | A |
| 5.7 | No artwork placeholder: the vertical card collapses and the dock shows an empty grey square. | `index.html:912-915`; `now-playing-dock.cpp:440, 898` | Accent-tinted note glyph. | S | A |
| 5.8 | Near-duplicate styles: Solid/Glass/macOS at default opacity, Minimal/Line, and Synthwave/Neon City/Cyber share a palette. | `shots/horizontal-*-dark.png` | Default Glass to ~55 % opacity, merge Line into Minimal as a variant. | S | A |
| 5.9 | Dock details: hardcoded error red, no tooltip on elided titles, the picker truncates "System player only". | `now-playing-dock.cpp:232, 414-415, 848, 882-883` | Palette-derived error colour, tooltips, wider picker. | S | A |
| 5.10 | Naming drift: "Music Now Playing", "♫ Now Playing (music)", dock "Music", `obs-now-playing` folder, "OBS Now Playing" data directory. | `README.md:3, 45, 162`; `en-US.ini:1-2, 30-31` | One display name in every user-facing string. Keep the frozen `obs-now-playing` identifiers. | S | A |
| 5.11 | Polling cost on macOS/Linux: the dock spawns a helper every 2.5 s for volume/shuffle; with nothing playing the watcher scans all tabs of all browsers every 3 s. Command processes have no timeout, so a hung one leaves the volume state stuck. | `now-playing-dock.cpp:606-614`; `media-watcher.cpp:456-472, 489-493`; `macos.js:270-305, 710-748, 863-869` | Report volume/shuffle from the long-running watcher (as Windows does), scan only the playing browser, kill command processes after ~5 s. | M | A |
| 5.12 | The idle dock repolishes about nine stylesheets and rebuilds icons every second. | `now-playing-dock.cpp:627-664, 848-878` | Skip when the accent is unchanged. | S | A |
| 5.13 | Small leaks: `kill()` skips the `linux.sh` EXIT trap and leaves two temp files per OBS exit; a fallback image up to 8 MB is re-sent as base64 every 5 s. | `media-watcher.cpp:151`; `linux.sh:99-100`; `now-playing-source.cpp:110, 139` | Terminate before kill; downscale and cache the fallback image. | S | A |

## Leave as is

- **Helper restart and backoff**, including the 30-minute self-recycle (`media-watcher.cpp:196-225`).
- **Input hardening**: field caps, videoId regex, `data:image/` prefix, `textContent` in the overlay. No XSS path was found.
- **The browser-source wrapper and `javascript_event` push**: no local server, no polling.
- **`MediaWatcher` and `Updater` as singletons**, and the small local `#ifdef` blocks in `music-channel.cpp` and `updater.cpp`.
- **The single-file overlay**: themes already share `:root` tokens with `[data-style]` overrides.
- **The frozen `obs-now-playing` identifiers** and the Windows file-polled command transport: both are deliberate.
- **Locale parity**: `en-US.ini` and `es-ES.ini` have identical keys.
- **`cmake/` and `buildspec.json`**: zero drift from the current `obs-plugintemplate`.
- **Updater consent flow**: Now / Later / Skip, changelog link, rollback on macOS and Windows.

## Not verified

- Nothing was built or run in OBS. Timing, CPU and crash claims come from reading the code.
- Whether `ditto -x -k` and Windows `tar` reject `../` entries in an update zip.
- Rendering of CJK, Cyrillic and emoji per OS; legibility over the blurred dock backdrop in light and custom OBS themes.
- Which OBS version the PPA currently installs on the CI runner.
- The OBS practice claims rest on the research pass's reading of the primary sources listed in Phase 3. The self-update, privacy-disclosure and UI-thread-blocking rows have no official OBS rule behind them; they are judged against general practice.

## Checks per phase

- [ ] Phase 1: an update with a wrong hash is refused on all three OSes; no prompt appears while streaming; TSan or thread-id logging shows no cross-thread read in `np_update`; 31st cached cover does not drop the playing one.
- [ ] Phase 2: with OBS in English, no Spanish string appears on stream, in the dock or in dialogs; a new source shows the sample card; pausing follows D3.
- [ ] Phase 3: `spctl -a -t install` accepts the macOS bundle (if D1 is yes); the Linux zip loads on the declared minimum OBS; the build has no deprecation warning.
- [ ] Phase 4: `ctest` runs in CI and covers `classifyApp`, `parseReaderLine`, `selectCurrent`, `compareVersions`; one contract fixture per helper.
- [ ] Phase 5: Properties are grouped; the dock is usable by keyboard and at a narrow width.

## Next step

Answer D1-D5, fast-forward the local clone, then start Phase 1 on a feature branch with items 1.1-1.4 as the first work unit.
