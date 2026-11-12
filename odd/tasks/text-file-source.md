# Feature: Text file source (radio automation software)

## Objective
Let the app read "now playing" from a text/XML file written by radio automation software (Dinesat, RadioBoss, ZaraRadio, SAM, mAirList) and show it like any other source.

## Problem
Today the app only writes `now-playing.txt` (output). Radio automation software does not publish to SMTC/MPRIS reliably, but it usually exports the on-air song to a file for RDS/web. There is no way to use that file as input.

## Scope
- New `textFile` source in `server.js`: watches a configurable file path, parses it into artist/title, feeds the same track pipeline (priority: last started wins).
- Config in `config.json`: path, enabled flag, separator, field order, optional regex, idle timeout.
- Robust decoding: UTF-8 (with/without BOM), UTF-16 LE/BE (BOM), Windows-1252/Latin-1 fallback (common in Windows radio software).
- Simple XML support: extract common tags (`Title`, `Artist`, case-insensitive) when the file looks like XML.
- Status page / dock show the new source with its label; no artwork, no progress bar.
- Tests in `test/` for the parser and decoding; docs in READMEs.

## Constraints
- Real export samples from Dinesat/RadioBoss are not available yet: parser stays configurable (separator, order, regex) instead of hardcoding one format.
- No artwork or progress: those files only carry text. "Stopped" = empty file or no change within the idle timeout.
- Code/comments/UI follow existing project conventions.
- RDD: check `gentle-ai review mode status`; ordinary checks otherwise.
- User asked to test locally BEFORE merging to main or releasing. Push, PR, merge and release are the user's decisions.

## Tasks
- [x] T1 Text file source in `server.js` + parser/decoder + tests + config defaults + status label. Route: delegated (writer; reading server.js to prepare the write).
- [x] T2 Dock/plugin awareness of the new source (label/icon in `now-playing-dock.cpp` if sources are enumerated there) + docs (README.md, README.es.md). Route: delegated (same writer when small).
- [x] T3 Local manual test with sample files (user confirmed on macOS 2026-11-12: works).
- [x] T4 (inline, mechanical; `npm test` green after change) Packaging: ship `text-source.js` with the app (`build/build.sh` cp list, `build/package_mac.py` file tuple, `build/windows/installer.nsi` File + Delete lines) and add `node --check text-source.js` to `.github/workflows/ci.yml`. Outside the T1/T2 writer surface; without it the packaged app fails at startup (`require('./text-source')`).
- [x] T5 Plugin support (user decision 2026-11-11: keep server.js part as base, add to the C++ plugin because releases ship plugin only): text file reader in `obs-plugin` MediaWatcher + path/separator config in the dock + local macOS build for manual test. Route: delegated (writer, several non-trivial C++ files).

## Acceptance criteria
- `npm test` passes, including new parser tests (separator, order, regex, XML, UTF-8 BOM, UTF-16, Latin-1, empty file).
- Writing `Artista - Título` to the configured file shows it in the overlay within ~1-2 s; emptying the file or idle timeout hides it.
- Disabled by default; no behavior change when not configured.

## Delivery
Strategy: ask-on-risk. Forecast: ~300-450 authored lines.

## Progress
- Branch `feat/text-file-source` created from `main` (3ac74fe).
- T1 (delegated writer, uncommitted): `text-source.js` (pure decode/parse), wiring in `server.js` (config `textSource`, 1 s stat polling, status-page box, controls refuse radio), tests `test/text-source.js` (15 parser cases) + `test/text-source-server.js` (integration) + `test/silent-helper.js`, wired into `npm test`.
  - RED: `node test/text-source.js` with module missing → exit 1 (MODULE_NOT_FOUND); with a stub module → 14/15 cases failed, exit 1. GREEN: 15/15 after implementation. Integration test written after implementation (no RED).
  - Evidence: `npm test` → smoke OK, Linux reader OK, parser 15 cases OK, radio source OK (exit 0). Manual smoke (port 18790): content → `{"title":"Canción","artist":"Artista","kind":"radio","sourceLabel":"Radio Uno",...}`; emptied → `"track":null`.
- T2: the OBS plugin does not consume `server.js` (its `MediaWatcher` runs `media/*` readers directly), so a dock entry/icon would be unreachable; no C++ change. Plugin support would need a C++ text source in `media-watcher.cpp` (not in scope). READMEs (en/es) document `textSource` and its limitations.
- Next: T4 (packaging, needs authorization for `build/**` and CI), then T3 (user manual test), then work-unit commit.
- T5 (delegated writer, uncommitted): plugin port.
  - `obs-plugin/src/text-source-reader.{hpp,cpp}`: pure std-only decode/parse (same rules as `text-source.js`; regex is a hook the plugin fills with `QRegularExpression`, named groups `artist`/`title`).
  - `MediaWatcher`: `RadioSettings` persisted in `obs_module_config_path("radio.json")` (disabled by default); 1 s `QTimer` poll; stability rule (stat unchanged between polls or two equal reads), missing file = not playing, read error keeps state, idle from last change (file mtime at startup); track `app=radio-file`, `kind=radio`, `isPlaying`, no artwork, `durationMs 0`, `changedAt` = change time (so it competes as "last started"). Not removed by the system reader's session sweep. `players()` gains `radio`; radio never sets the video/others flags; commands/volume/state are no-ops for radio; music channel ignores radio.
  - Dock: radio item in Player list only when enabled (orange radio icon drawn like the platform icons); new small radio button in the options row + right-click "Radio desde un archivo…" opening a dialog (enable, file + Elegir…, separator, order, name, advanced regex with live validation, idle minutes); transport buttons disabled for radio. Source properties list gains "Solo la radio (archivo)". Overlay accent for `radio`. Locale es/en. READMEs updated.
  - Tests: `obs-plugin/tests/run-text-source-test.sh` (28 checks, ported from `test/text-source.js`). Written after the implementation (no RED observed); first run caught a wrong expectation in the test itself (cp1252 0xC0 = À), cross-checked against `text-source.js`.
  - Evidence: `cmake --build build_macos --config RelWithDebInfo` → BUILD SUCCEEDED (universal x86_64+arm64, -Werror, 0 warnings, after `cmake -S . -B build_macos` reconfigure); `run-text-source-test.sh` → 28 OK; `run-signature-test.sh` → OK; `npm test` → exit 0; clang-format 19.1.1 (via uvx): new files clean, no violations on changed lines (the existing files already have unrelated violations).
  - Not done: Windows/Linux builds not run locally; poll/dock behavior not exercised in OBS (manual test = T3); new test not wired into `.github/workflows/ci.yml` (outside writer surface).
- Next: T3 manual test in OBS on macOS with the built plugin, then work-unit commit.
