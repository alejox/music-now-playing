# Feature: OBS 33 plugin location, drop Linux, clean Release

## Objective
Ship app 2.4.27 / plugin 1.0.35 for OBS 33: Windows plugin in the new OBS 33 location with auto-update, macOS and Windows only, and a Release with only the current files. No functional change otherwise (volume control was dropped by the user).

## Facts (sources)
- OBS 33.0.0 release notes: third-party plugins have a new location/structure; legacy locations load until OBS 34.0 (https://github.com/obsproject/obs-studio/releases/tag/33.0.0).
- KB "Legacy Plugin Developer Info": Windows new format `C:\ProgramData\obs-studio\plugins\<name>\<name>.dll` + `...\<name>\data\*`; legacy: `Program Files\obs-studio\obs-plugins\64bit\<name>.dll` + `data\obs-plugins\<name>\`, and `ProgramData\...\<name>\bin\64bit\<name>.dll`. OBS tries the new location first and skips duplicates. Installers should remove the Program Files copy. macOS unchanged.
- Updaters 1.0.27-1.0.34 require a zip with `obs-plugins/64bit/obs-now-playing.dll` at its root, robocopy the whole zip root into the OBS folder, and only update a DLL inside `obs-plugins\64bit` (updater.cpp installedRoot/ownFolder/zip check).

## Decisions
- Windows plugin zip keeps the legacy layout so 1.0.34 can auto-update to 1.0.35.
- 1.0.35 migrates itself on OBS >= 33 (obs_get_version() >= MAKE_SEMANTIC_VERSION(33,0,0)) when loaded from a legacy location: one elevated step copies DLL + data to the new location, removes legacy copies, asks to restart OBS.
- 1.0.35+ updater installs to the new location (remapping the legacy-shaped zip) when running from it.
- NSIS installer installs to the new location on OBS >= 33 detection? -> installs to `%ProgramData%\obs-studio\plugins\obs-now-playing\` and removes legacy copies (OBS 33 required for that layout; on older OBS keep legacy install). See T2.
- Build stays against OBS 31.1.1 headers (buildspec unchanged) so 1.0.35 still loads on OBS 31/32; migration only on OBS 33+.
- Linux support removed (no builds, assets, docs, code).
- Release: `LEGACY_PLUGIN_ASSET_NAMES` off. Assets: Setup .exe, 2 mac app zips, plugin macos + windows-x64 (5 files). Users on <= 1.0.33 update manually (user decision 2026-10-28).
- Volume control: not doing (user decision).

## Tasks
- [x] T1 Remove Linux (workflows, build.sh, build/linux, media/linux.sh + copy, tests, package.json, server.js, C++ __linux__ branches, locale key, presets, READMEs, release notes). Route: delegated writer.
- [x] T2 Windows OBS 33 location: self-migration, updater install target, NSIS installer + uninstall, README/obs-plugin README manual steps, release notes. Route: delegated writer.
- [ ] T3 Release cleanup + versions: legacy switch off, legacy step adjusted, version 2.4.27 / 1.0.35. Route: delegated writer.
- [ ] T4 Verify: local checks, CI + plugin + dry-run release on branch, asset list = 5, diff audit that macOS behavior is unchanged.
- [ ] T5 Merge + publish after user OK.

## Progress
- Branch `feat/obs33-windows-location` from main 79ca849.
- T1 done (delegated writer): Linux jobs/assets/scripts/tests/C++ branches/locale key/presets/docs removed. Kept generic non-Win/non-Mac fallbacks in server.js (data dir, xdg-open) because CI runs `npm test` on Ubuntu; build.sh still mentions Linux/WSL only as a build host for the Windows installer. `cmake/linux/*` kept (template). Checks: `npm test` OK, `node --check server.js` OK, `bash -n build/build.sh` OK, CMakePresets JSON parses. Commit 12dc16e.
- T2 done (delegated writer): updater.cpp (Windows only) detects OBS >= 33 and the module location (new / legacy Program Files / legacy ProgramData bin\64bit); offers a one-step elevated move at startup (once per session; flag `moved-to-obs33-folder.txt` = plugin version after success); updates from the new location, or from a legacy one on OBS 33+, install into the new folder from the legacy-shaped zip and clean legacy copies; OBS < 33 keeps the old path unchanged. NSIS reads obs64.exe file version: >= 33 installs to `%ProgramData%\obs-studio\plugins\obs-now-playing` (one elevated cmd: robocopy dll + data, then delete legacy copies only if both copies succeeded), else legacy; uninstall also removes the new folder. Docs + release body updated. Not compiled locally (no MSVC/makensis): CI must confirm.

## Next step
T1-T3 (one writer, one commit per task).
