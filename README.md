<div align="center">

# 🎵 Music Now Playing

**Show the song you're listening to on your stream — with its cover art, its colors and buttons to control it.**

[![Latest release](https://img.shields.io/github/v/release/alejox/music-now-playing?label=release&color=1db954)](../../releases/latest)
![macOS](https://img.shields.io/badge/macOS-12%2B-000000?logo=apple&logoColor=white)
![Windows](https://img.shields.io/badge/Windows-10%20%7C%2011-0078D4?logo=windows&logoColor=white)
![Linux](https://img.shields.io/badge/Linux-x86__64-FCC624?logo=linux&logoColor=black)
![OBS](https://img.shields.io/badge/OBS-31%2B-302E31?logo=obsstudio&logoColor=white)

**English** · [Español](README.es.md)

![Now Playing overlay](docs/overlay.png)

[**⬇️ Download**](../../releases/latest) &nbsp;·&nbsp; [Install](#-install-the-plugin) &nbsp;·&nbsp; [Features](#-features) &nbsp;·&nbsp; [FAQ](#-faq)

</div>

---

Works with **Spotify** (app or web), **YouTube Music**, **Apple Music** and any player your system recognizes. No accounts, no API keys, no browser extensions.

## ✨ Features

| | |
|---|---|
| 🎨 **Cover art colors** | The card takes the colors of each song's cover, with a smooth transition. |
| 🪟 **Solid or glass** | A solid card or a translucent "glass" one, with adjustable opacity. Light or dark theme. |
| 📐 **Horizontal or vertical** | A bar or a vertical card; the source size adjusts itself. |
| 🎬 **Video mode** | The YouTube video instead of the cover (muted and in sync), on Mac, Windows and Linux: the one in the YouTube Music / YouTube tab and, with Spotify (app or web) or other apps, the one found by searching the song on YouTube. On Linux, browsers usually do not share the tab address, so it is almost always the search result. |
| 🖼️ **Your logo** | An image of your own for songs without a cover. |
| ⏯️ **Control dock** | Previous / pause / next, back and forward 10 s and a draggable progress bar to jump around, inside OBS and off-stream. |
| 🔊 **Music channel** | One click adds an Audio Mixer channel with only the player's sound; it follows you when you switch players. |
| 🔄 **Updates** | OBS tells you when there's a new version and installs it for you (on Linux it opens the download page instead). |

## 📦 Install the plugin

Download the zip for your system from **[Releases](../../releases/latest)**, **close OBS first**, unzip it and copy the folder it contains:

| | Windows 10/11 (OBS 33+) | macOS (OBS 31+) | Linux x86_64 (OBS 31+) |
|---|---|---|---|
| **File** | `music-now-playing-<version>-windows-x64.zip` | `music-now-playing-<version>-macos.zip` | `music-now-playing-<version>-linux-x86_64.zip` |
| **You get** | the `obs-now-playing` folder | `obs-now-playing.plugin` | the `obs-now-playing` folder |
| **Copy it to** | `C:\ProgramData\obs-studio\plugins\` (accept the administrator prompt) | `~/Library/Application Support/obs-studio/plugins/` | `~/.config/obs-studio/plugins/` |
| **Then** | Delete old copies, if any (see below) | Run the Terminal command below (once) | Install `playerctl` (`sudo apt install playerctl`, `sudo dnf install playerctl` or `sudo pacman -S playerctl`) |

Open OBS. From then on the plugin updates itself (plugins 1.0.35 and older don't see this version: download it by hand once).

> [!IMPORTANT]
> **Windows:** OBS 33 [moved the plugins folder](https://obsproject.com/kb/legacy-plugin-developer-info) to `C:\ProgramData\obs-studio\plugins\`. Delete the old copies, if any: `C:\Program Files\obs-studio\obs-plugins\64bit\obs-now-playing.dll` and `C:\Program Files\obs-studio\data\obs-plugins\obs-now-playing`. OBS 31 and 32 don't load this layout: update OBS to 33 or newer.

> [!NOTE]
> **Linux:** the plugin reads what's playing from the MPRIS players (Spotify, Firefox, Chromium/Chrome/Brave, VLC…) through `playerctl`. **OBS as a Flatpak:** copy the folder into `~/.var/app/com.obsproject.Studio/config/obs-studio/plugins/` and, since `playerctl` isn't inside the sandbox, install it on the system and let OBS run it there: `flatpak override --user --talk-name=org.freedesktop.Flatpak com.obsproject.Studio`.

```bash
xattr -cr ~/Library/Application\ Support/obs-studio/plugins/obs-now-playing.plugin
```

> [!TIP]
> In Finder, use **Go → Go to Folder…** and paste the path. Create the `plugins` folder if it doesn't exist.

## 🎛️ Use it

1. **Sources → + → ♫ Now Playing (music)** to add the card. Open its **Properties** to change the style.
2. **Docks → Música** for the buttons.
3. Optional: in the source Properties, **Crear canal de música** (Create music channel) to control the music volume separately.

> [!NOTE]
> On Linux, the music channel needs the [obs-pipewire-audio-capture](https://github.com/dimtpap/obs-pipewire-audio-capture) plugin (OBS has no per-application audio capture of its own there).

> [!NOTE]
> On Mac, for the YouTube Music buttons to work, turn on once in Chrome: **View → Developer → Allow JavaScript from Apple Events**.

The plugin's interface is in Spanish, with English strings included.

## ❓ FAQ

<details>
<summary><b>The cover art doesn't show</b></summary>

On recent macOS versions the system no longer passes the image. The plugin asks the Spotify app for it (macOS asks if OBS can control Spotify: allow it) and, for other players, looks it up in Apple's public catalog by artist and title. On Windows, when the player sends no image (browsers often don't), it does the same lookup and, for YouTube in the browser, uses the video thumbnail.
</details>

<details>
<summary><b>The previous / next buttons don't work</b></summary>

- **YouTube Music (Mac):** enable *Allow JavaScript from Apple Events* (see above).
- **Others (Mac):** media keys are simulated, which needs **System Settings → Privacy & Security → Accessibility → OBS**.
- **Linux:** the buttons go to the player through `playerctl`; if a player doesn't support jumping, the dock says so.
- The dock shows a message under the buttons explaining what's missing.
</details>

<details>
<summary><b>The dock says the system reader doesn't work</b></summary>

The message says why (on Linux it's usually that `playerctl` is missing). The OBS log (**Help → Log Files**) has the `[obs-now-playing]` lines.
</details>

<details>
<summary><b>macOS says the plugin is damaged or blocks it</b></summary>

Run the `xattr -cr …` command from the install steps. The plugin isn't signed with a paid Apple developer account.
</details>

## 🖥️ Standalone app (optional)

Besides the plugin, this repository has an app that runs on its own and serves the overlay as a web page, for other streaming programs or for a Text source. It's no longer published as an installer: run it from the source code (`npm start`, Node 18+). Most people only need the plugin.

<details>
<summary><b>How the app works and how to use it</b></summary>

### How it works

Windows and macOS already know what's playing: it's what the Windows volume flyout and the Mac's Control Center "Now Playing" show. The app reads that and serves it as a page that OBS displays with a **Browser source**.

```
Spotify / YouTube Music / Apple Music…
        │  (the player tells the system)
        ▼
Windows: media controls (SMTC) ──── PowerShell ─┐
macOS:   "Now Playing" (MediaRemote) ─ osascript ┼─► local app (127.0.0.1:8787) ─► Browser source in OBS
Plan B:  Tampermonkey script in the browser ─────┘                             └─► now-playing.txt (Text source)
```

The idea of reading the system media controls comes from [SMTC Bridge](https://github.com/nuttylmao/smtc-bridge) (Windows only); this app brings it to macOS too.

### Run it

`npm start` in the repository folder (Node 18+). Its **panel** is at **http://127.0.0.1:8787/** (what's playing, detected apps, the URL for OBS).

### Add it to OBS

**Sources → + → Browser**, URL `http://127.0.0.1:8787/overlay`, width `640`, height `200`. Buttons: **Docks → Custom Browser Docks**, URL `http://127.0.0.1:8787/controls`.

URL options (combine with `&`):

| Parameter | Effect |
|---|---|
| `theme=light` | Light theme |
| `pos=br` | Corner: `bl` (default), `br`, `tl`, `tr` |
| `art=0` / `art=big` | No cover / large cover |
| `colors=0` | Don't use the cover colors |
| `blur=1` | Blurred cover as the background |
| `controls=1` | Buttons on the overlay (visible on stream; click via **Interact**) |
| `progress=0` | No progress bar |
| `label=0` | No "Listening on …" line |
| `width=480` | Card width in px |
| `accent=ff00aa` | Fixed accent color |
| `preview=1` | Sample song when nothing is playing |

**Plain text:** a **Text source** reading `now-playing.txt` (the panel shows the path; format in `textFormat` of `config.json`).

**Plan B:** if the system reader doesn't work, install [Tampermonkey](https://www.tampermonkey.net/) and click **Install script** in the panel.

### Troubleshooting

- **Nothing in OBS:** open the panel; if it doesn't load, the app is closed. Try `?preview=1`.
- **A YouTube video shows instead of the music:** hide the browser under **Detected apps**.
- **Port 8787 is taken:** change `port` in `config.json` (and `ENDPOINT` in the Tampermonkey script).
- **Log:** `log.txt` in `%APPDATA%\OBS Now Playing` (Windows) or `~/Library/Application Support/OBS Now Playing` (macOS).
</details>

## 🔒 Privacy

- Everything runs on your computer; there's no server of ours.
- The plugin only goes online to check for updates on GitHub, to look up missing cover art in Apple's public catalog (song title and artist), in video mode to search YouTube for the song's video (title and artist; on Windows also when the address bar can't be read) and to load the YouTube player.
- The app only listens on `127.0.0.1` and doesn't let other web pages read what you listen to.

## 🛠️ For developers

<details>
<summary><b>Build and project layout</b></summary>

- **Plugin:** CMake from `obs-plugin/` (`cmake --preset macos` → Xcode, `cmake --preset windows-x64` → Visual Studio 2022, `cmake --preset ubuntu-x86_64` → Ninja with the OBS PPA headers). See [obs-plugin/README.md](obs-plugin/README.md).
- **App:** `npm start` (Node 18+); `OBS_NOW_PLAYING_DATA=/other/path` changes the data folder. Installers: `./build/build.sh [all|windows|mac]`.
- **System readers:** `media/windows.ps1` (PowerShell 5.1 + WinRT) and `media/macos.js` (JavaScript for Automation). Both write one JSON line per second. On macOS 15.4+ only system programs can read "Now Playing", so the reader runs in `osascript`.
- **Plugin system readers:** the plugin carries copies of the app readers in `obs-plugin/data/media/`, plus its own Linux reader `obs-plugin/data/media/linux.sh` (bash + `playerctl`, tested with a fake `playerctl` in `npm test`).
- **Releases:** the **Release** workflow in GitHub Actions builds the plugin for the three systems and publishes only its three zips.
</details>

## 📄 License

MIT — see [LICENSE](LICENSE). The OBS plugin is GPL-2.0 or later (like every plugin that links against libobs), see `obs-plugin/LICENSE`.
