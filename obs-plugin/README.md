# Now Playing — plugin para OBS Studio

Plugin nativo para OBS Studio (macOS y Windows) que agrega:

- Una fuente **Now Playing (música)**: la tarjeta con la canción que suena (carátula, título, artista, barra de progreso) con los colores de la carátula. Se agrega a la escena como cualquier fuente y se configura en sus **Propiedades**: diseño **Horizontal** (barra) o **Vertical** (tarjeta con la carátula grande arriba); al cambiar de diseño o de tamaño de carátula, el ancho y el alto de la fuente se ajustan solos; si la tarjeta queda más chica que la fuente, se elige qué borde se mantiene fijo (arriba o abajo, para tarjetas en la parte de abajo de la pantalla), tema claro/oscuro, estilo **Sólido** o **Cristal** (traslúcido, deja ver la escena con el color de la carátula) con su **opacidad**, carátula normal/grande/oculta, **imagen para cuando no hay carátula** (tu logo; la tarjeta toma sus colores), **Carátula o video** (con YouTube Music / YouTube en macOS, el video sin sonido y sincronizado) y su **formato** (mismo tamaño que la carátula con franjas difuminadas, o ajustado al video en 16:9), colores de la carátula, fondo difuminado, barra de progreso y la línea "Escuchando en …".
- Un dock **Música** (menú **Docks**): la canción con los botones **anterior / pausa / siguiente** y un **buscador**: escribe una canción y pulsa Enter. Con YouTube Music en el navegador (Mac) pone el primer resultado; con Spotify abre la búsqueda en la app; con Apple Music pone la primera de tu biblioteca; en Windows abre la búsqueda en Spotify o YouTube Music. Vive dentro de OBS y no sale en el stream. Con YouTube Music en el navegador, los botones pulsan los de la propia página (en Chrome: **Ver → Opciones para desarrolladores → Permitir JavaScript desde Apple Events**, una vez); con Spotify y Apple Music se le pide a la app; si no, se simulan las teclas de música (permiso de Accesibilidad para OBS).
- En las Propiedades de la fuente, el botón **Crear canal de música** agrega al Mezclador de audio un canal "Música (Now Playing)" con solo el sonido de la app que suena (captura de audio por aplicación de OBS; macOS 13+ / Windows 10 2004+), para manejar su volumen aparte. Si el canal ya existe, lo reapunta a la app de ahora; y después el canal sigue solo a la música: al pasar de Spotify a YouTube Music (o al revés), apunta a la app que suena.

**Carátula:** si el reproductor no la entrega (en macOS 27 el sistema solo manda su identificador), la pide a la app de Spotify (la primera vez macOS pregunta si OBS puede controlar Spotify: responde Permitir) y, para YouTube Music y los demás, la busca por artista y canción en el catálogo público de Apple (iTunes Search); solo la usa si artista y título coinciden. Esa búsqueda manda a Apple el nombre de la canción y el artista, nada más.

Lee la música del sistema, como la app: en Windows los controles multimedia (SMTC) y en macOS el "Ahora suena". Funciona con la app de Spotify, YouTube Music o Spotify en el navegador, Apple Music y cualquier reproductor que el sistema reconozca. No necesita la app OBS Now Playing ni ninguna cuenta.

Está basado en la plantilla oficial `obs-plugintemplate` de OBS Project, igual que *Alejox Theme Panel*.

## Instalar

Descarga el zip de tu sistema desde **[Releases](../../../releases/latest)** (`obs-now-playing-plugin-…`). Cierra OBS antes.

### macOS (OBS 31+, macOS 12+)

1. Descomprime el zip: queda `obs-now-playing.plugin`.
2. Cópialo a `~/Library/Application Support/obs-studio/plugins/` (crea `plugins` si no existe; en Finder: **Ir → Ir a la carpeta…** y pega la ruta).
3. Quita la marca de "descargado de internet" (si no, macOS lo bloquea por no estar firmado por Apple):
   ```bash
   xattr -cr ~/Library/Application\ Support/obs-studio/plugins/obs-now-playing.plugin
   ```
4. Abre OBS.

### Windows (OBS 31+, Windows 10/11)

1. Descomprime el zip en `C:\ProgramData\obs-studio\plugins\` (queda `C:\ProgramData\obs-studio\plugins\obs-now-playing\bin\64bit\obs-now-playing.dll`).
2. Abre OBS.

## Actualizaciones

Desde la versión 1.0.3, al abrir OBS el plugin revisa si hay una versión nueva en las Releases de GitHub. Si la hay, muestra un aviso con **Actualizar ahora**, **Más tarde** u **Omitir esta versión**: "Actualizar ahora" la descarga, reemplaza el plugin y pide reiniciar OBS. También se puede revisar a mano en **Herramientas → Now Playing: buscar actualizaciones**.

Solo se actualiza sola si está instalada en su propia carpeta, como dicen las instrucciones de arriba. Si no, el aviso ofrece abrir la página de descargas.

## Usar

1. **Fuentes → + → Now Playing (música)**. Ubícala y escálala como cualquier fuente; el tamaño de la tarjeta se cambia en sus Propiedades (ancho y alto).
2. **Docks → Música** para los botones.

Si en el dock aparece "El lector del sistema no funciona", el mensaje dice por qué. El registro de OBS (**Ayuda → Archivos de registro**) tiene las líneas `[obs-now-playing]`.

## Compilar en Xcode (macOS)

Requisitos: Xcode 16+, CMake 3.28+.

```bash
cd obs-plugin
cmake --preset macos
```

Descarga las fuentes de OBS Studio y las dependencias (obs-deps, Qt6) la primera vez. Después abre `build_macos/obs-now-playing.xcodeproj` en Xcode y compila (Cmd+B). El plugin queda en `build_macos/RelWithDebInfo/obs-now-playing.plugin`.

En Windows: `cmake --preset windows-x64` y abre `build_x64/obs-now-playing.sln` en Visual Studio 2022. GitHub Actions compila los dos en cada cambio (`.github/workflows/plugin.yml`).

## Cómo funciona

- `src/media-watcher.cpp` arranca el lector del sistema con `QProcess`: `data/media/windows.ps1` (PowerShell) o `data/media/macos.js` (`osascript`; desde macOS 15.4 solo programas del sistema pueden leer "Ahora suena", así que no se puede hacer desde dentro de OBS). Son los mismos lectores de la app, probados en Windows y macOS reales (`.github/workflows/ci.yml`); el flujo de CI verifica que las copias sigan iguales.
- `src/now-playing-source.cpp` es la fuente: por dentro, una fuente de navegador privada con `data/overlay/index.html`, a la que el plugin le manda la canción una vez por segundo con el evento JavaScript `obs-now-playing` (`javascript_event` de obs-browser). Sin servidor local.
- `src/now-playing-dock.cpp` es el dock en Qt.

Licencia: GPL-2.0 o posterior (como todo plugin que enlaza con libobs), ver `LICENSE`. El resto del repositorio es MIT.
