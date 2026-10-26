# Feature: Rename to "Music Now Playing"

## Objective
Meet the OBS Forum resource policy: remove "OBS" from the resource's main name, in the forum post and in the repository. New name: **Music Now Playing**. Repo: `alejox/obs-music-player` -> `alejox/music-now-playing`.

## Constraint (hard)
Existing macOS and Windows installs must keep working and keep receiving updates exactly as today. Only visible names change; identifiers and paths stay.

## Keep unchanged (identity/paths)
- macOS: `OBS Now Playing.app` folder, executable name, bundle id `io.github.obs-now-playing`, codesign identifier.
- Windows: install dir, exe name, `.lnk` names, registry keys `Software\OBSNowPlaying` / `Uninstall\OBSNowPlaying`.
- Data dirs in server.js (`OBS Now Playing`, `obs-now-playing`).
- Plugin `name: obs-now-playing` (binary, data folder, .deb name), bundleId, source id `obs_now_playing_source`, dock id, JS event names, `OBS_NOW_PLAYING_*` env vars, temp file names.
- Plugin release asset prefix `obs-now-playing-plugin-` (updater regex).
- Userscript `@name` / `@namespace` (changing them duplicates the script in Tampermonkey).
- Linux `.desktop` file name and install dir.

## Tasks
- [x] T1 Visible rename + repo URLs. Route: delegated (writer, many files).
  - README.md / README.es.md titles and prose (keep literal paths/file names), obs-plugin/README.md.
  - server.js panel title/h1/close button/closed page/console banner; controls.html offline message; plugin-main.cpp comment.
  - release.yml release title `Music Now Playing ${tag}`; app installer asset names `Music-Now-Playing-*` (not matched by any updater) in build.sh, release.yml, READMEs.
  - NSIS: split `APP` into `APP_NAME` (display) and `APP_ID` (paths); launcher.nsi ProductName/FileDescription.
  - package_mac.py CFBundleName/CFBundleDisplayName; Linux `.desktop` Name=, install.sh messages.
  - buildspec.json displayName and website; updater.cpp kLatestRelease/kReleasesPage; README badges -> `alejox/music-now-playing`.
  - package.json description.
- [ ] T2 Verify: npm test, syntax, YAML; CI plugin + ci on branch (Mac/Win/Linux green, windows-reader pre-existing failure only); diff audit that no "keep unchanged" item moved.
- [ ] T3 Rename the GitHub repo and update the local remote. Never recreate `alejox/obs-music-player` (it would kill the redirect).

## Checks
- `npm test`, `node --check server.js`, `bash -n build/build.sh build/linux/install.sh`, YAML parse.
- `rg` for remaining visible "OBS Now Playing" only in allowed identity/path places.

## Progress
- Branch `chore/rename-music-now-playing` from main 356cbca.
- Evidence: updater.cpp uses `curl -fsSL` (lines 240, 338), so installed 1.0.32 follows GitHub's 301 after the repo rename.

- T1 done (commit: see `git log` for "chore: renombra el recurso a Music Now Playing"). NSIS `APP` split: APP_NAME (Name, BrandingText, ProductName, FileDescription, finish-page run text, main section name, uninstall DisplayName) / APP_ID (InstallDir, exe File/Delete, finish-page run path, every .lnk create/delete). Registry keys untouched.
- T1 checks: npm test exit 0; node --check ok; bash -n ok; py_compile ok; YAML parse of ci/plugin/release ok; JSON parse ok; makensis unavailable locally (CI builds the installer); remaining "OBS Now Playing"/"obs-now-playing" hits are identity/path items or the compatibility notes in READMEs.

## Next step
T2 (CI on branch, diff audit).
