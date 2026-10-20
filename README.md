# 🎵 OBS Now Playing

**English** · [Español](README.es.md)

Shows the song playing on your computer in OBS: cover art, title, artist and a progress bar, using the **cover art colors**, plus **buttons to skip songs**. Works on **Windows and macOS** with the **Spotify** app, **YouTube Music** or Spotify in the browser, **Apple Music** and any player the system recognizes. No accounts, no API keys and no extensions.

![Overlay](docs/overlay.png)

## Plugin or app?

There are two ways to use it, with the same readers inside:

- **OBS plugin** (recommended): adds the **Now Playing (música)** source and the **Música** dock right inside OBS, configured from the source Properties. See below and **[obs-plugin/](obs-plugin/)**.
- **App**: runs on its own and serves the overlay by URL. Also useful for other streaming programs, or to use a Text source.

## OBS plugin

- **"Now Playing (música)" source**: horizontal or vertical layout (the size adjusts itself), light/dark theme, solid or **glass** style with adjustable opacity, normal/large/hidden cover art, your **logo** when a song has no cover, and the YouTube Music / YouTube **video** (muted and in sync) instead of the cover.
- **"Música" dock**: cover art and **previous / pause / next** buttons, inside OBS and off-stream.
- **Create music channel** (in the source Properties): a channel in the Audio Mixer with only the player's sound, so you can control its volume separately.
- **Updates** offered with a prompt when OBS starts.

The plugin's interface is in Spanish (with English strings included).

### Install the plugin

Download `obs-now-playing-plugin-<version>-macos.zip` or `-windows-x64.zip` from **[Releases](../../releases/latest)** and close OBS first.

- **macOS** (OBS 31+): unzip, copy `obs-now-playing.plugin` to `~/Library/Application Support/obs-studio/plugins/` and run in Terminal:
  `xattr -cr ~/Library/Application\ Support/obs-studio/plugins/obs-now-playing.plugin`
- **Windows** (OBS 31+): unzip into `C:\ProgramData\obs-studio\plugins\`.

Then: **Sources → + → Now Playing (música)** and **Docks → Música**. More details (in Spanish) in **[obs-plugin/README.md](obs-plugin/README.md)**.

## App

### How it works

Windows and macOS already know what's playing: it's what the Windows volume flyout and the Mac's Control Center "Now Playing" show. The app reads that information and serves it as a page that OBS displays with a **Browser source**.

```
Spotify / YouTube Music / Apple Music…
        │  (the player tells the system)
        ▼
Windows: media controls (SMTC) ──── PowerShell ─┐
macOS:   "Now Playing" (MediaRemote) ─ osascript ┼─► local app (127.0.0.1:8787) ─► Browser source in OBS
Plan B:  Tampermonkey script in the browser ─────┘                             └─► now-playing.txt (Text source)
```

The idea of reading the system media controls comes from [SMTC Bridge](https://github.com/nuttylmao/smtc-bridge) (Windows only); this app brings it to macOS too, ships its own overlay and doesn't expose what you listen to to other web pages.

### 1. Install the app

Download the app installer from **[Releases](../../releases/latest)**. You don't need Node.js or the terminal: each installer includes everything.

#### Windows 10/11

1. Download `OBS-Now-Playing-Setup-<version>.exe` and open it.
2. If **"Windows protected your PC"** appears, click **More info → Run anyway** (this happens with any app without a paid code-signing certificate).
3. Follow the wizard. By default the app **starts with Windows** in the background, so the overlay is ready when you open OBS.

It installs for your user, without administrator rights. To remove it: **Settings → Apps → OBS Now Playing → Uninstall**.

#### macOS 11 or later

1. Download the zip for your Mac ( menu → **About This Mac**):
   - **Apple M1/M2/M3/M4… chip** → `OBS-Now-Playing-<version>-mac-apple.zip`
   - **Intel processor** → `OBS-Now-Playing-<version>-mac-intel.zip`
2. Unzip it and **drag `OBS Now Playing` to Applications**.
3. Open it. The first time, macOS blocks it because it isn't signed with an Apple developer account:
   - Click **OK / Done** on the warning.
   - Go to **System Settings → Privacy & Security**, scroll to *"OBS Now Playing was blocked"* and click **Open Anyway**. Confirm with your password.
   - This only happens once.

   If it says **"is damaged and can't be opened"** instead (some macOS versions do this with apps not signed by Apple), it isn't damaged: open Terminal and paste
   `xattr -cr "/Applications/OBS Now Playing.app" && open "/Applications/OBS Now Playing.app"`
4. To start it automatically: **System Settings → General → Login Items → +** and choose the app.

The app has no window or Dock icon: opening it opens its **panel** in the browser. If you close that tab it keeps running; open the app again to get back to the panel.

### 2. The panel

At **http://127.0.0.1:8787/** you see what's playing, whether the system reader works, the **detected apps** (you can hide the ones you don't want, e.g. the browser if you also watch videos), the URL for OBS and the **Close OBS Now Playing** button. The panel is in Spanish.

### 3. Add it to OBS

1. In **Sources**, click **+** and choose **Browser**.
2. URL: `http://127.0.0.1:8787/overlay`
3. Width `640`, height `200` (or your canvas size, if you use `pos=`).

Options you can add to the URL (combine them with `&`):

| Parameter | Effect |
|---|---|
| `theme=light` | Light theme |
| `pos=br` | Corner: `bl` (bottom left, default), `br`, `tl`, `tr` |
| `art=0` / `art=big` | No cover / large cover |
| `colors=0` | Don't use the cover colors (uses each app's color) |
| `blur=1` | Card background with the blurred cover |
| `controls=1` | Previous / pause / next buttons **on the overlay** (visible on stream; in OBS click them via right click → **Interact**) |
| `progress=0` | No progress bar |
| `label=0` | No "Escuchando en …" (Listening on …) line |
| `width=480` | Card width in px |
| `accent=ff00aa` | Fixed accent color (default: from the cover) |
| `preview=1` | Sample song when nothing is playing (to position it) |

Example: `http://127.0.0.1:8787/overlay?pos=br&art=big&blur=1`

By default the overlay takes the most vivid color of the cover for the accents (bar, label) and a dark shade of the dominant color for the background, and changes them with a smooth transition on every song.

### Buttons to change songs

The recommended way is an OBS **dock**: a panel with the cover and the previous / pause / next buttons that lives inside OBS, next to your scenes, and **doesn't show on stream**.

1. In OBS: **Docks → Custom Browser Docks**.
2. Name: `Música`; URL: `http://127.0.0.1:8787/controls`. Apply and place the panel wherever you like.

The buttons control the app the overlay shows: on Windows through the system media controls, on macOS with the same commands as the media keys, and with plan B by clicking the buttons on the YouTube Music or Spotify page (reinstall the script from the panel if you had it before).

**Prefer plain text?** Use a **Text source** with "Read from file" pointing to `now-playing.txt` (the panel shows the exact path and has an **Open folder** button). Change the format with `textFormat` in `config.json`, in that same folder.

### Plan B: browser script

If the system reader doesn't work on your computer (the panel says so), install [Tampermonkey](https://www.tampermonkey.net/) and click **Install script** in the panel. It reads YouTube Music and Spotify web directly from the tab. On recent Chrome/Edge you need to turn on **Developer mode** in `chrome://extensions` (or "Allow user scripts" in the extension details).

## Troubleshooting

- **Nothing shows in OBS:** open the panel (http://127.0.0.1:8787/). If it doesn't load, the app is closed: open it. Also try the URL with `?preview=1`.
- **A YouTube video shows instead of the music:** in the panel, hide the browser under **Detected apps**.
- **The browser shows as "Escuchando ahora" instead of "YouTube Music":** the system only knows the sound comes from Chrome/Edge, not which page. If you install YouTube Music as an app (browser menu → *Install YouTube Music*), it shows with its own name and color.
- **Port 8787 is used by another program:** change `port` in `config.json` (data folder) **and** `ENDPOINT` in the Tampermonkey script if you use it.
- **Something fails and I don't know what:** the log is `log.txt`, in the data folder:
  - Windows: `%APPDATA%\OBS Now Playing`
  - macOS: `~/Library/Application Support/OBS Now Playing`
- **Plugin:** if the dock says the system reader doesn't work, the message explains why; the OBS log (**Help → Log Files**) has the `[obs-now-playing]` lines.

## Privacy

The app runs entirely on your computer and nothing goes to the internet. The server only listens on `127.0.0.1`, rejects requests with another `Host` and sends no CORS headers, so the web pages you visit can't read what you listen to.

The plugin doesn't use any servers of its own either. It only goes online to check for updates on GitHub, to look up the cover art in Apple's public catalog when the player doesn't provide it (it sends the song title and artist), and, if you choose video mode, to load the YouTube player.

## For developers

- Run from source: `npm start` (Node 18+). It uses the same data folder as the installed app; `OBS_NOW_PLAYING_DATA=/other/path` changes it.
- System readers: `media/windows.ps1` (Windows PowerShell 5.1 + WinRT) and `media/macos.js` (JavaScript for Automation). Both write one JSON line per second; the protocol is described at the top of `media/windows.ps1`. Buttons reach Windows through the `.command` file in the data folder, macOS through `osascript media/macos.js command next`, and the userscript in the response to its heartbeat. To test without Windows or a Mac, `OBS_NOW_PLAYING_HELPER=fake-reader.js` makes the server use a Node script that writes the same protocol.
- On macOS 15.4+ Apple only lets system programs read "Now Playing"; that's why the reader runs in `osascript` and not in Node.
- Build the installers: `./build/build.sh [all|windows|mac]` (requires `python3` with Pillow; Windows: `makensis`/NSIS; Mac: preferably on a Mac, the only place the whole app gets signed, or on Linux with `clang` and `lld`, unsigned). It downloads the official Node.js, verifies its SHA-256 and puts everything in `dist/`. The **Release** workflow in GitHub Actions does the same and uploads the files to Releases.
- The plugin is built with CMake from `obs-plugin/` (Xcode on macOS, Visual Studio 2022 on Windows); see [obs-plugin/README.md](obs-plugin/README.md).
- The Mac app uses the unmodified official Node binary (signed and notarized by Node.js); what macOS can't verify is the `.app` bundle. Removing the warning entirely requires signing and notarizing with an Apple Developer account (USD 99/year). On Windows, the equivalent is a code-signing certificate.

## License

MIT — see [LICENSE](LICENSE). The OBS plugin is GPL-2.0 or later (like every plugin that links against libobs), see `obs-plugin/LICENSE`.
