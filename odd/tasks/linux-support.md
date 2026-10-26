# Feature: Linux support

## Objective
Ship OBS Now Playing on Linux with the same experience as macOS and Windows: plugin, standalone app, CI artifacts, release assets and docs.

## Problem
The plugin compiles on Linux but does nothing useful there: `MediaWatcher` reports `Unsupported`, `server.js` has no Linux helper, the music channel targets the macOS-only `sck_audio_capture` source, and the updater falls into the macOS branch (`ditto`, `xattr`, `.plugin` bundle). CI, `build/build.sh` and docs only know macOS and Windows.

## Scope
- Linux media reader over MPRIS using `playerctl`, emitting the same JSON-lines protocol as `media/macos.js` and `media/windows.ps1`, plus transport commands.
- Wire the reader into `server.js` and `obs-plugin/src/media-watcher.cpp`.
- `music-channel.cpp`: Linux branch using the PipeWire per-app audio source when available, otherwise disabled with a clear message.
- `updater.cpp`: Linux only notifies and opens the release page (no in-place install).
- CI: Linux plugin build job (`plugin.yml`), Linux release assets (`release.yml`), reader sync check.
- `build/build.sh linux`: tarball with Node runtime, app files, launcher and `.desktop` entry.
- Docs: README.md, README.es.md, obs-plugin/README.md, package.json description, release notes.

## Constraints
- Code, comments and UI copy follow the existing project language (Spanish comments and UI strings are already used in this repo).
- No Docker locally: Linux-specific runtime and C++ build are verified only by CI.
- RDD review mode is off (global); ordinary checks only.
- Push, PR and merge are the user's decisions.

## Tasks
- [x] T1 Linux MPRIS reader `media/linux.sh` + copy in `obs-plugin/data/media/` + sync check + `server.js` helper/commands. Route: delegated (writer, 3+ files).
- [x] T2 Plugin adaptations: `media-watcher.cpp`, `music-channel.cpp`, `updater.cpp`. Route: delegated (writer, 3 non-trivial C++ files).
- [x] T3 Packaging and CI: `build/build.sh linux`, Linux launcher/.desktop, `plugin.yml` ubuntu job, `release.yml` linux jobs and assets, `ci.yml` reader check. Route: delegated (writer).
- [ ] T4 Docs: READMEs, obs-plugin README, package.json, release notes. Route: delegated (writer).

## Acceptance criteria
- `npm test` passes; `node --check` passes on JS files; `bash -n` and `shellcheck` (if available) pass on new shell scripts.
- The Linux reader emits valid JSON lines in the same shape as the macOS/Windows readers.
- CI defines a Linux plugin build producing a `.deb`/tarball artifact and release publishes Linux assets.
- Docs describe Linux install, `playerctl` dependency and Flatpak notes.

## Delivery
Strategy: ask-on-risk. Forecast: ~600-800 authored lines across 4 tasks (exceeds ~400; chain strategy to be asked before PR creation).

## Progress
- Branch `feat/linux-support` created from `main` (5e0b642).
- T1 done (delegated writer): `media/linux.sh` polls `playerctl --all-players metadata --format` once per second (bash 3.2+), same JSON-lines protocol; commands via `linux.sh command <action> [ms]` with `OBS_NOW_PLAYING_PLAYER`. Test `test/linux-reader.js` (fake playerctl) runs in `npm test`. Checks: `npm test` OK, `node --check server.js` OK, `bash -n` OK, `cmp` OK; shellcheck only in CI. Commit 014dda9.
- T2 done (delegated writer): `media-watcher.cpp` runs `bash <data>/media/linux.sh watch <flag>` and commands with `OBS_NOW_PLAYING_PLAYER`; `music-channel.cpp` uses `pipewire_audio_application_capture` (dimtpap/obs-pipewire-audio-capture, keys `CaptureMode`/`TargetName`, verified against its source) or shows `NowPlaying.Channel.NeedsPipeWire`; `updater.cpp` matches `linux-x86_64.(tar.gz|deb)` and only opens the Release page. No local Linux compile: CI verifies. Commit 1288243.
- T3 done (delegated writer): `plugin.yml` job `linux` (ubuntu-24.04, OBS PPA, `ubuntu-ci-x86_64` preset, cpack DEB + user-plugins tarball, artifact `plugin-linux`); `build/build.sh linux` (Node 22.22.0 linux-x64 with SHA check, launcher, `.desktop`, `install.sh`, `PLUGIN_TAR`); `release.yml` job `linux` + publishes `*.tar.gz`/`*.deb`; ci.yml shellcheck on Linux scripts. Checks: `./build/build.sh linux` built locally on macOS (no plugin), `install.sh` tested with a scratch HOME, YAML parsed with Ruby; plugin build and shellcheck only in CI.

## Next step
T4.
