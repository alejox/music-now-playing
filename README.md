<div align="center">

# 🎵 OBS Now Playing

**Show the song you're listening to on your stream — with its cover art, its colors and buttons to control it.**

[![Latest release](https://img.shields.io/github/v/release/alejox/obs-music-player?label=release&color=1db954)](../../releases/latest)
![macOS](https://img.shields.io/badge/macOS-12%2B-000000?logo=apple&logoColor=white)
![Windows](https://img.shields.io/badge/Windows-10%20%7C%2011-0078D4?logo=windows&logoColor=white)
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
| 🎬 **Video mode** | With YouTube Music / YouTube, the music video instead of the cover (muted and in sync), on Mac and Windows. |
| 🖼️ **Your logo** | An image of your own for songs without a cover. |
| ⏯️ **Control dock** | Previous / pause / next, inside OBS and off-stream. |
| 🔊 **Music channel** | One click adds an Audio Mixer channel with only the player's sound; it follows you when you switch players. |
| 🔄 **Updates** | OBS tells you when there's a new version; on Mac it installs it for you. |

## 📦 Install the plugin

Download the zip for your system from **[Releases](../../releases/latest)** and **close OBS first**.

| | macOS (OBS 31+) | Windows (OBS 31+) |
|---|---|---|
| **File** | `obs-now-playing-plugin-<version>-macos.zip` | `obs-now-playing-plugin-<version>-windows-x64.zip` |
| **1** | Unzip it: you get `obs-now-playing.plugin` | Unzip it (right click → **Extract All**): you get the `obs-plugins` and `data` folders |
| **2** | Copy it to `~/Library/Application Support/obs-studio/plugins/` | Copy those two folders into `C:\Program Files\obs-studio\`, accepting to replace and the administrator prompt |
| **3** | Run the Terminal command below (once) | Check that `C:\Program Files\obs-studio\obs-plugins\64bit\obs-now-playing.dll` is there |
| **4** | Open OBS | Open OBS |

> [!TIP]
> On Windows, `OBS-Now-Playing-Setup-<version>.exe` also installs the plugin into the OBS folder (option **Plugin para OBS**, checked by default; asks for administrator permission), no zip needed.

> [!IMPORTANT]
> On Windows, if you installed it before in `C:\ProgramData\obs-studio\plugins\obs-now-playing`, delete that folder: otherwise OBS loads both copies.

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
- The dock shows a message under the buttons explaining what's missing.
</details>

<details>
<summary><b>The dock says the system reader doesn't work</b></summary>

The message says why. The OBS log (**Help → Log Files**) has the `[obs-now-playing]` lines.
</details>

<details>
<summary><b>macOS says the plugin is damaged or blocks it</b></summary>

Run the `xattr -cr …` command from the install steps. The plugin isn't signed with a paid Apple developer account.
</details>

## 🖥️ Standalone app (optional)

Besides the plugin there's an app that runs on its own and serves the overlay as a web page, for other streaming programs or for a Text source. Most people only need the plugin.

<details>
<summary><b>How the app works and how to install it</b></summary>

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

### Install

Download the installer from **[Releases](../../releases/latest)**. No Node.js or terminal needed.

**Windows 10/11:** open `OBS-Now-Playing-Setup-<version>.exe`. If **"Windows protected your PC"** appears, click **More info → Run anyway**. By default it starts with Windows. It also installs the OBS plugin (you can uncheck it).

**macOS 11+:** download `…-mac-apple.zip` (Apple chip) or `…-mac-intel.zip` (Intel), unzip, drag **OBS Now Playing** to Applications and open it. If it's blocked: **System Settings → Privacy & Security → Open Anyway**. If it says *"is damaged"*:

```bash
xattr -cr "/Applications/OBS Now Playing.app" && open "/Applications/OBS Now Playing.app"
```

The app has no window: opening it opens its **panel** at **http://127.0.0.1:8787/** (what's playing, detected apps, the URL for OBS).

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
- The plugin only goes online to check for updates on GitHub, to look up missing cover art in Apple's public catalog (song title and artist), on Windows to find the YouTube video that is playing when the address bar can't be read (video title and channel) and, in video mode, to load the YouTube player.
- The app only listens on `127.0.0.1` and doesn't let other web pages read what you listen to.

## 🛠️ For developers

<details>
<summary><b>Build and project layout</b></summary>

- **Plugin:** CMake from `obs-plugin/` (`cmake --preset macos` → Xcode, `cmake --preset windows-x64` → Visual Studio 2022). See [obs-plugin/README.md](obs-plugin/README.md).
- **App:** `npm start` (Node 18+); `OBS_NOW_PLAYING_DATA=/other/path` changes the data folder. Installers: `./build/build.sh [all|windows|mac]`.
- **System readers:** `media/windows.ps1` (PowerShell 5.1 + WinRT) and `media/macos.js` (JavaScript for Automation). Both write one JSON line per second. On macOS 15.4+ only system programs can read "Now Playing", so the reader runs in `osascript`.
- **Releases:** the **Release** workflow in GitHub Actions builds everything and publishes it.
</details>

## 📄 License

MIT — see [LICENSE](LICENSE). The OBS plugin is GPL-2.0 or later (like every plugin that links against libobs), see `obs-plugin/LICENSE`.
