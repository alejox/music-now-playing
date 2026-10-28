# Feature: Plugin-only Release (copy-and-paste), Multichat style

## Objective
Release v2.4.28 / plugin 1.0.36 containing ONLY plugin zips for manual copy-and-paste installation, like `obs-multichat`:
- `music-now-playing-<pv>-macos.zip` -> `obs-now-playing.plugin`
- `music-now-playing-<pv>-windows-x64.zip` -> folder `obs-now-playing\` with `obs-now-playing.dll` + `data\` (OBS 33 layout, copied into `C:\ProgramData\obs-studio\plugins\`)
- `music-now-playing-<pv>-linux-x86_64.zip` -> folder `obs-now-playing/` with `bin/64bit/obs-now-playing.so` + `data/` (copied into `~/.config/obs-studio/plugins/`)
No app installers (.exe, Mac app zips, Linux app tarball, .deb).

## Decisions (user, 2026-10-28)
- Only copy-and-paste plugin distribution; drop app installers from the Release.
- Windows zip uses the OBS 33 folder layout (like obs-multichat).
- Linux plugin comes back (plugin only, not the standalone app).
- New asset name `music-now-playing-<pv>-<platform>.zip` (no `-plugin-`): plugins <= 1.0.35 do not match it, so they see no update instead of failing on the new layout; those users download manually once. From 1.0.36 auto-update works with the new names/layouts.

## Tasks
- [x] T1 Restore Linux plugin support (reader `obs-plugin/data/media/linux.sh`, C++ __linux__ branches, locale key, ubuntu presets, plugin.yml linux job, linux reader test) from commit 12dc16e's parent, plugin side only. Route: delegated writer.
- [ ] T2 New packaging + updater: plugin.yml produces the three zips with the new layouts/names; updater 1.0.36 matches `music-now-playing-<v>-<platform>.zip` and installs the new Windows layout directly (macOS/Linux behavior per platform); release.yml publishes only those 3 zips (no app jobs), exact-asset check; READMEs/obs-plugin README/release notes describe copy-and-paste install per OS; version 2.4.28 / 1.0.36. Route: delegated writer.
- [ ] T3 Verify CI + dry run, merge, publish, verify 3 assets.

## Progress
- Branch `feat/plugin-only-release` from main dd5525d.
- T1 done (delegated writer): reverse-applied 12dc16e for media-watcher, music-channel, locale NeedsPipeWire, ubuntu presets, plugin.yml linux job (old .deb/.tar.gz packaging, adapted in T2); updater Linux branches merged by hand onto the OBS 33 Windows code; reader test points at `obs-plugin/data/media/linux.sh` and runs in `npm test`; ci.yml bash -n + shellcheck of the plugin reader and the fake playerctl. No root `media/linux.sh`; plugin.yml sync no longer compares it. Checks: `npm test` OK (smoke + Linux reader on macOS bash 3.2), `bash -n` OK, workflows parse with Ruby YAML, CMakePresets JSON parses; shellcheck not installed locally (runs in CI).
