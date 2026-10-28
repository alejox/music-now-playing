# Music Now Playing — plugin para OBS Studio

Plugin nativo para OBS Studio (macOS, Windows y Linux) que agrega:

- Una fuente **♫ Now Playing (música)**: la tarjeta con la canción que suena (carátula, título, artista, barra de progreso) con los colores de la carátula. Se agrega a la escena como cualquier fuente y se configura en sus **Propiedades**: diseño **Horizontal** (barra) o **Vertical** (tarjeta con la carátula grande arriba); al cambiar de diseño o de tamaño de carátula, el ancho y el alto de la fuente se ajustan solos; si la tarjeta queda más chica que la fuente, se elige qué borde se mantiene fijo (arriba o abajo, para tarjetas en la parte de abajo de la pantalla), tema claro/oscuro, estilo **Sólido**, **Cristal** (traslúcido, deja ver la escena con el color de la carátula), **Ventana macOS** o **Píldora** (cristal), o uno con diseño propio —**Minimal**, **Línea**, **Synthwave**, **Ciudad neón**, **Cyberpunk**, **Retro** o **Glitch**—, o los skins **Winamp clásico** y **Winamp 5** (en horizontal y en vertical; en los estilos con disco la carátula gira mientras suena y, con video, gira su borde) con su **opacidad**, carátula normal/grande/oculta, **imagen para cuando no hay carátula** (tu logo; la tarjeta toma sus colores), **Carátula o video** (por defecto, video si es posible: con YouTube Music / YouTube en el navegador, el video sin sonido y sincronizado; en Windows, de la barra de direcciones de la ventana cuya pestaña visible es YouTube o, si no se puede leer, buscando el video por su título) y su **formato** (mismo tamaño que la carátula con franjas difuminadas, o ajustado al video en 16:9), colores de la carátula, fondo difuminado, barra de progreso y la línea "Escuchando en …".
- Un dock **Música** (menú **Docks**): la canción con los botones **anterior / pausa / siguiente**, **−10 s / +10 s** y una barra de progreso que se **arrastra** (o se pulsa) para saltar a otra parte de la canción. Vive dentro de OBS y no sale en el stream. Con YouTube Music en el navegador, los botones pulsan los de la propia página (en Chrome: **Ver → Opciones para desarrolladores → Permitir JavaScript desde Apple Events**, una vez); con Spotify y Apple Music se le pide a la app; si no, se simulan las teclas de música (permiso de Accesibilidad para OBS).
- En las Propiedades de la fuente, el botón **Crear canal de música** agrega al Mezclador de audio un canal "Música (Now Playing)" con solo el sonido de la app que suena (captura de audio por aplicación de OBS; macOS 13+ / Windows 10 2004+; en Linux, con el plugin [obs-pipewire-audio-capture](https://github.com/dimtpap/obs-pipewire-audio-capture), su fuente `pipewire_audio_application_capture`), para manejar su volumen aparte. Si el canal ya existe, lo reapunta a la app de ahora; y después el canal sigue solo a la música: al pasar de Spotify a YouTube Music (o al revés), apunta a la app que suena.

**Carátula:** si el reproductor no la entrega (en macOS 27 el sistema solo manda su identificador), la pide a la app de Spotify (la primera vez macOS pregunta si OBS puede controlar Spotify: responde Permitir) y, para YouTube Music y los demás, la busca por artista y canción en el catálogo público de Apple (iTunes Search); solo la usa si artista y título coinciden. Esa búsqueda manda a Apple el nombre de la canción y el artista, nada más. En Windows, si el reproductor no manda imagen, hace la misma búsqueda y, para YouTube en el navegador, usa la miniatura del video.

Lee la música del sistema, como la app: en Windows los controles multimedia (SMTC), en macOS el "Ahora suena" y en Linux los reproductores MPRIS con `playerctl` (hay que instalarlo: `sudo apt install playerctl`). En Linux la carátula es la que entrega el reproductor (sin la búsqueda en el catálogo de Apple). Funciona con la app de Spotify, YouTube Music o Spotify en el navegador, Apple Music y cualquier reproductor que el sistema reconozca. No necesita la app Music Now Playing ni ninguna cuenta.

Está basado en la plantilla oficial `obs-plugintemplate` de OBS Project, igual que *Alejox Theme Panel*.

## Instalar

Descarga el zip de tu sistema desde **[Releases](../../../releases/latest)** (`music-now-playing-<versión>-<sistema>.zip`), cierra OBS, descomprímelo y copia lo que trae en la carpeta de plugins de OBS.

### Windows (OBS 33+, Windows 10/11)

1. Copia la carpeta `obs-now-playing` del zip (trae `obs-now-playing.dll` y `data\`) en `C:\ProgramData\obs-studio\plugins\` ([la carpeta de plugins de OBS 33](https://obsproject.com/kb/legacy-plugin-developer-info); `C:\ProgramData` está oculta: pega la ruta en la barra del Explorador). Windows pide permiso de administrador: acéptalo.
2. Si tenías una copia de antes, bórrala: `C:\Program Files\obs-studio\obs-plugins\64bit\obs-now-playing.dll` y `C:\Program Files\obs-studio\data\obs-plugins\obs-now-playing`.
3. Abre OBS. En el registro (**Ayuda → Archivos de registro**) debe aparecer `[obs-now-playing] plugin loaded successfully (version …)` con la versión nueva.

OBS 31 y 32 no cargan esta forma de carpeta: actualiza OBS a la 33 o superior. Si actualizas OBS a la 33 con el plugin en la carpeta de antes, el plugin ofrece moverse solo a la carpeta nueva al abrir OBS: acepta el permiso de administrador y reinicia OBS.

### macOS (OBS 31+, macOS 12+)

1. Copia `obs-now-playing.plugin` del zip en `~/Library/Application Support/obs-studio/plugins/` (crea `plugins` si no existe; en Finder: **Ir → Ir a la carpeta…** y pega la ruta).
2. Quita la marca de "descargado de internet" (si no, macOS lo bloquea por no estar firmado por Apple):
   ```bash
   xattr -cr ~/Library/Application\ Support/obs-studio/plugins/obs-now-playing.plugin
   ```
3. Abre OBS.

### Linux (OBS 31+, x86_64)

1. Instala `playerctl` (`sudo apt install playerctl`, `sudo dnf install playerctl` o `sudo pacman -S playerctl`).
2. Copia la carpeta `obs-now-playing` del zip en `~/.config/obs-studio/plugins/`: queda `obs-now-playing/bin/64bit/obs-now-playing.so` y `obs-now-playing/data/…`.
   - **OBS en Flatpak:** cópiala en `~/.var/app/com.obsproject.Studio/config/obs-studio/plugins/`. `playerctl` no viene dentro de la caja de arena: el lector usa el del sistema con `flatpak-spawn --host`, que necesita `flatpak override --user --talk-name=org.freedesktop.Flatpak com.obsproject.Studio`.
3. Abre OBS.

## Actualizaciones

Desde la versión 1.0.3, al abrir OBS el plugin revisa si hay una versión nueva en las Releases de GitHub. Si la hay, muestra un aviso con **Actualizar ahora**, **Más tarde** u **Omitir esta versión**: "Actualizar ahora" la descarga, reemplaza el plugin y pide reiniciar OBS. También se puede revisar a mano en **Herramientas → Now Playing: buscar actualizaciones**. Desde la 1.0.36 los zips se llaman `music-now-playing-<versión>-<sistema>.zip` y traen otra forma: los plugins 1.0.35 o anteriores no los ven, hay que descargar la 1.0.36 a mano una vez.

- **macOS:** solo si está en su propia carpeta (`…/plugins/obs-now-playing.plugin`), como dicen las instrucciones de arriba.
- **Windows (OBS 33+):** si está en `C:\ProgramData\obs-studio\plugins\obs-now-playing\` o en una carpeta de antes (`…\obs-studio\obs-plugins\64bit\obs-now-playing.dll`). Al actualizar, Windows pide permiso de administrador una vez; la actualización siempre queda en la carpeta nueva y quita las copias de antes. Con OBS 31 o 32 no se ofrece: la revisión manual avisa que la versión nueva necesita OBS 33 o superior.
- **Linux:** no se instala solo: el aviso abre la página de la Release para bajar el zip.

Si no está donde dicen las instrucciones, el aviso ofrece abrir la página de descargas.

## Usar

1. **Fuentes → + → ♫ Now Playing (música)**. Ubícala y escálala como cualquier fuente; el tamaño de la tarjeta se cambia en sus Propiedades (ancho y alto).
2. **Docks → Música** para los botones.

Si en el dock aparece "El lector del sistema no funciona", el mensaje dice por qué. El registro de OBS (**Ayuda → Archivos de registro**) tiene las líneas `[obs-now-playing]`.

## Compilar en Xcode (macOS)

Requisitos: Xcode 16+, CMake 3.28+.

```bash
cd obs-plugin
cmake --preset macos
```

Descarga las fuentes de OBS Studio y las dependencias (obs-deps, Qt6) la primera vez. Después abre `build_macos/obs-now-playing.xcodeproj` en Xcode y compila (Cmd+B). El plugin queda en `build_macos/RelWithDebInfo/obs-now-playing.plugin`.

En Windows: `cmake --preset windows-x64` y abre `build_x64/obs-now-playing.sln` en Visual Studio 2022.

En Linux (Ubuntu 24.04): con el OBS del PPA y Qt 6 (`sudo add-apt-repository ppa:obsproject/obs-studio && sudo apt install obs-studio qt6-base-dev qt6-base-private-dev libsimde-dev cmake ninja-build`), `cmake --preset ubuntu-x86_64 && cmake --build --preset ubuntu-x86_64`.

GitHub Actions compila los tres en cada cambio y arma sus zips (`.github/workflows/plugin.yml`).

## Cómo funciona

- `src/media-watcher.cpp` arranca el lector del sistema con `QProcess`: `data/media/windows.ps1` (PowerShell), `data/media/macos.js` (`osascript`; desde macOS 15.4 solo programas del sistema pueden leer "Ahora suena", así que no se puede hacer desde dentro de OBS) o `data/media/linux.sh` (`bash` + `playerctl`). Los de Windows y macOS son copias de los de la app, probados en Windows y macOS reales (`.github/workflows/ci.yml`), y el flujo del plugin verifica que sigan iguales; el de Linux solo existe en el plugin y se prueba con un `playerctl` falso (`test/linux-reader.js`, en `npm test`).
- `src/now-playing-source.cpp` es la fuente: por dentro, una fuente de navegador privada con `data/overlay/index.html`, a la que el plugin le manda la canción una vez por segundo con el evento JavaScript `obs-now-playing` (`javascript_event` de obs-browser). Sin servidor local.
- `src/now-playing-dock.cpp` es el dock en Qt.

Licencia: GPL-2.0 o posterior (como todo plugin que enlaza con libobs), ver `LICENSE`. El resto del repositorio es MIT.
