# Feature: Plugin asset rename bridge

## Objective
Rename the plugin release assets from `obs-now-playing-plugin-*` to `music-now-playing-plugin-*` without stranding existing installs, so a later release can drop every "obs" file name from the Release page.

## Why
Installed plugins (<= 1.0.33) only read `releases/latest` and match `^obs-now-playing-plugin-<ver>-<platform>.<ext>$` (obs-plugin/src/updater.cpp). GitHub shows every asset, so the old-name files must stay visible during a bridge period.

## Plan
- Bridge release v2.4.26 / plugin 1.0.34:
  - Updater accepts both prefixes and prefers `music-now-playing-plugin-`.
  - CI/release produce `music-now-playing-plugin-<v>-<platform>.<ext>` as the primary assets.
  - Publish also uploads byte-identical copies named `obs-now-playing-plugin-*`, controlled by one clearly marked switch in release.yml (to turn off after the bridge period).
  - Release notes explain the legacy copies are only for automatic updates from older versions.
- After the bridge (a few weeks / releases): flip the switch off; only `music-now-playing-plugin-*` remain.

## Keep unchanged
Binary/bundle names inside archives (`obs-now-playing.plugin`, `.dll`, `.so`, data folder), plugin `name`, source/dock ids, macOS app internal path `Contents/Resources/plugin/obs-now-playing-plugin-macos.zip` (unless every reader is updated together), Windows installer behavior, app installer names. macOS and Windows behavior must stay identical apart from accepting the new asset name.

## Tasks
- [x] T1 Implement bridge (updater, plugin.yml, release.yml, build.sh/installer/package_mac inputs, READMEs, version 2.4.26 / 1.0.34). Route: delegated (writer, several non-trivial files).
- [ ] T2 Verify: npm test, syntax, YAML; CI + plugin + dry-run release on branch; check artifact names and that the Windows/Mac/Linux app packages still embed the plugin.
- [ ] T3 Merge to main, publish v2.4.26, verify 12 plugin+app assets and notes.
- [ ] T4 (later, separate) Turn off legacy copies.

## Decisions
- Updater regex `^(music|obs)-now-playing-plugin-(<ver>)-<platform>.<ext>$`: the first `music-` match wins over any `obs-` match regardless of asset order; with a single prefix the last match wins, as before. macOS/Windows install and Linux open-page paths unchanged; the "not found" message names the new prefix.
- Switch `env.LEGACY_PLUGIN_ASSET_NAMES: 'true'` at the top of release.yml. Step "Copias del plugin con el nombre viejo" in publish requires exactly 4 `dist/music-now-playing-plugin-*` files, `cp` + `cmp` each to `obs-now-playing-plugin-*`; existing `files:` globs (`*.zip`, `*.tar.gz`, `*.deb`) pick them up. With the switch on, the notes add one line saying the old-name files only exist for automatic updates.
- macOS app keeps the internal path `Contents/Resources/plugin/obs-now-playing-plugin-macos.zip` (read by server.js); only the input globs in release.yml changed.
- CI artifact names (`plugin-macos`, `plugin-windows`, `plugin-linux`) unchanged. installer.nsi, install.sh, ci.yml and test/ had no asset-name references.

## Progress
- Branch `feat/plugin-asset-rename-bridge` from main 5ca3f26.
- T1 done (route: delegated writer). Local checks: npm test OK, node --check OK, bash -n OK, py_compile OK, 3 workflows parse as YAML, legacy-copy step simulated (on: 4 copies + note; off: no copies, empty note), updater regex simulated (new only / old only / both in either order -> new / wrong platform -> none / app zip -> none).

## Next step
T2: run CI, plugin and a dry-run release on the branch; check artifact names and that the app packages still embed the plugin.
