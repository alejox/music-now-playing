# Feature: Phase 1 — Safety and stability

## Objective
Implement Phase 1 of `odd/tasks/plugin-audit-improvement-plan.md` (items 1.1-1.9): stop the updater from installing unverified or tamperable code, keep it out of the way while live, and fix the stability defects found in the audit.

## Problem / Why
The audit of plugin 1.0.39 found the self-updater installs downloads without integrity checks (and elevates a script staged in `%TEMP%` on Windows), a probable cross-thread read in `np_update`, an artwork cache that can drop the playing song's cover, a music channel located by its translated name, Windows volume side effects, and blocking work on the UI thread and in the reader loops.

## Scope
- In: plan items 1.1-1.9, their tests where a runnable test exists, docs touched by behaviour changes.
- Out: phases 2-5; macOS notarization (D1 = no, user decision 2026-11-08); version bump and release (user decides).

## Constraints
- Branch `fix/phase1-safety-stability` from `main` b8b5f2c (plugin 1.0.39).
- Repo convention: Spanish code comments and Conventional Commit subjects in Spanish; user-facing strings via `data/locale/en-US.ini` and `es-ES.ini` (both, same keys).
- Frozen identifiers (`obs-now-playing`) stay.
- No C++ test target exists (that is Phase 4, item 4.1): C++ changes are checked by compiling; helper changes by `npm test`, `bash -n`, `node --check`.
- Receipt-driven review: off (global). Delivery (push, PR, release) is the user's decision.

## Tasks
- [x] T1 (1.5) Artwork cache: evict the oldest entry, never a key of a live session. Route: delegated writer (batch A). Commit 7d2ccee.
- [x] T2 (1.8) Music channel: identify by a private settings marker, check the source id before updating; migrate an existing channel found by name. Route: delegated writer (batch A). Commit ead9721. Known limit: a channel created under another OBS language (old name, no marker) is not adopted; a second one is created.
- [x] T3 (1.4) `np_create`/`np_update` store settings only and queue the push onto the UI thread. Route: delegated writer (batch A). Commit 462e5a0. The fallback image is now loaded on the UI thread too.
- [x] T4 (1.3) Update prompt: no automatic prompt while streaming/recording/replay buffer/virtual camera (retried on the outputs' STOPPED events), "Later" as default button, frontend callback removed inside EXIT. Route: delegated writer (batch A). Commit 5bbf141.
- [x] T5 (1.2) Windows elevated install: script passed inline (`-EncodedCommand`), absolute System32 `powershell.exe`. Route: delegated writer (batch B). Commit 51e5c28. The script stages the files into a fresh folder inside the admin-only target and checks each one (DLL, PDB, data) against a SHA-256 manifest computed before elevation; `PSModulePath` pinned to `$PSHOME`, robocopy by absolute path, curl/tar via `GetSystemDirectoryW`. Encoded command ~13k chars (limit 32767). CI-verified only (+ local syntax check with stubbed Win32 headers); CI now parses the embedded script with the PowerShell parser.
- [x] T6 (1.1) Update integrity verification with Ed25519 signatures (user decision 2026-11-08, chosen over SHA-256 only). CI signs each release zip with the private key (GitHub secret `RELEASE_SIGNING_KEY`, OpenSSL 3 `pkeyutl -sign -rawin`) and publishes `<zip>.sig` (64 raw bytes); the updater downloads the `.sig`, verifies it against the public key compiled into the plugin (`e614461f088694160336f4b0825e7d0917de81c9742994ce7b221e8cd606bd94`) with vendored Monocypher (`crypto_ed25519_check`, standard SHA-512 Ed25519), and refuses to install when the signature is missing or invalid. Key pair generated locally at `~/.config/obs-now-playing-signing/` (outside the repo, mode 600); the user sets the secret and keeps a backup. Route: delegated writer (batch B). Commits 87833f8 (plugin, Monocypher 4.0.2 vendored, tarball SHA-512 matched monocypher.org and the GitHub release asset; no detached signature published) and b2616b9 (release signs and verifies each zip, checks the secret matches the compiled key, 6 assets; CI runs `obs-plugin/tests/run-signature-test.sh`). RED (unverifying stub) 3 failures, GREEN 7/7 with Homebrew OpenSSL 3.6.5. Pending: user sets the `RELEASE_SIGNING_KEY` secret before the next release (the release job fails without it).
- [ ] T7 (1.7) Windows volume: unmute only what the plugin muted, restore levels on exit, no full re-enumeration on the UI thread every tick. Route: delegated writer (batch B).
- [x] T8 (1.9) Updater off the UI thread: no 180 s wait, async extract, download error handling, highest matching version wins. Route: delegated writer (batch B). Commit 57eb21d. Elevated wait polled by a 250 ms QTimer, timeout reported as "still running"; `ShellExecuteEx` itself still blocks while the UAC prompt is open.
- [ ] T9 (1.6) Reader helpers: emit the session before network lookups, lookups with timeouts, Windows command queue. Route: delegated writer (batch C).

Route evidence: every task touches non-trivial C++ or helper code whose reading prepares the write (preparation trigger), and batches A/B span 2+ non-trivial files (writer trigger).

## Acceptance criteria
- Each task: behaviour in the plan's fix column; compiles on macOS locally (or CI if local build is not feasible); `npm test` green.
- No new hardcoded user-facing strings; new strings exist in both locale files.

## Checks
- `npm test`
- `node --check` / `bash -n` on changed helpers
- macOS plugin build (`cmake --preset macos` + build), Windows/Linux via CI

## Progress
- 2026-11-08: branch created from b8b5f2c; RDD off (global). Local `.deps` absent; Xcode 27.2 and cmake available.
- Engram mirror `odd/phase1-safety-stability/tasks`: PENDING (auto mode cannot review Engram writes in this session because a hook rewrites them).

- 2026-11-08: batch A done (T1-T4). Evidence: each commit built alone on macOS (`cmake --build --preset macos`: BUILD SUCCEEDED, no warnings in touched files); `npm test` OK; parent re-ran the build at 5bbf141: BUILD SUCCEEDED. Windows/Linux-only branches not compiled locally (CI). `obs-plugin/.deps/` added to `obs-plugin/.gitignore`.

- 2026-11-09: batch B done (T5, T6, T8). Evidence: macOS build after each task BUILD SUCCEEDED with 0 warnings; Windows and Linux branches of `updater.cpp` syntax-checked locally with `-Wall -Wextra` (Win32 API stubbed); signature test RED then GREEN; `npm test` OK; changed workflows parse as YAML. Residual: on Windows a same-user process could still swap the extracted files between the signature check and the manifest hash (small window, before elevation).

## Next step
T7, then T9. Before the next release: set the `RELEASE_SIGNING_KEY` secret.
