<div align="center">

# 🎵 Music Now Playing

**Muestra en tu stream la canción que estás escuchando, con su carátula, sus colores y botones para manejarla.**

[![Última versión](https://img.shields.io/github/v/release/alejox/music-now-playing?label=versi%C3%B3n&color=1db954)](../../releases/latest)
![macOS](https://img.shields.io/badge/macOS-12%2B-000000?logo=apple&logoColor=white)
![Windows](https://img.shields.io/badge/Windows-10%20%7C%2011-0078D4?logo=windows&logoColor=white)
![OBS](https://img.shields.io/badge/OBS-31%2B-302E31?logo=obsstudio&logoColor=white)

[English](README.md) · **Español**

![Tarjeta de Now Playing](docs/overlay.png)

[**⬇️ Descargar**](../../releases/latest) &nbsp;·&nbsp; [Instalar](#-instalar-el-plugin) &nbsp;·&nbsp; [Funciones](#-funciones) &nbsp;·&nbsp; [Preguntas](#-preguntas-frecuentes)

</div>

---

Funciona con **Spotify** (app o web), **YouTube Music**, **Apple Music** y cualquier reproductor que el sistema reconozca. Sin cuentas, sin claves de API y sin extensiones.

## ✨ Funciones

| | |
|---|---|
| 🎨 **Colores de la carátula** | La tarjeta toma los colores de la carátula de cada canción, con una transición suave. |
| 🪟 **Sólido o cristal** | Tarjeta sólida o traslúcida tipo "cristal", con opacidad ajustable. Tema claro u oscuro. |
| 📐 **Horizontal o vertical** | Barra o tarjeta vertical; el tamaño de la fuente se ajusta solo. |
| 🎬 **Modo video** | El video de YouTube en vez de la carátula (sin sonido y sincronizado), en Mac y Windows: el de la pestaña con YouTube Music / YouTube y, con Spotify (app o web) u otras apps, el que encuentra buscando la canción en YouTube. |
| 🖼️ **Tu logo** | Una imagen tuya para las canciones sin carátula. |
| ⏯️ **Dock de control** | Anterior / pausa / siguiente, retroceder y adelantar 10 s y una barra de progreso que se arrastra para saltar, dentro de OBS y fuera del stream. |
| 🔊 **Canal de música** | Con un clic, un canal en el Mezclador de audio solo con el sonido del reproductor; te sigue si cambias de reproductor. |
| 🔄 **Actualizaciones** | OBS te avisa cuando hay una versión nueva y la instala por ti. |

## 📦 Instalar el plugin

Descarga el archivo de tu sistema desde **[Releases](../../releases/latest)** y **cierra OBS antes**.

| | macOS (OBS 31+) | Windows (OBS 33+) |
|---|---|---|
| **Archivo** | `music-now-playing-plugin-<versión>-macos.zip` | `music-now-playing-plugin-<versión>-windows-x64.zip` |
| **1** | Descomprímelo: queda `obs-now-playing.plugin` | Descomprímelo (clic derecho → **Extraer todo**): quedan las carpetas `obs-plugins` y `data` |
| **2** | Cópialo a `~/Library/Application Support/obs-studio/plugins/` | Crea `C:\ProgramData\obs-studio\plugins\obs-now-playing\` y copia ahí `obs-plugins\64bit\obs-now-playing.dll` (acepta el permiso de administrador) |
| **3** | Pega en la Terminal el comando de abajo (una vez) | Crea dentro una carpeta `data` y copia ahí el contenido de `data\obs-plugins\obs-now-playing\` |
| **4** | Abre OBS | Borra las copias de antes, si hay (ver abajo), y abre OBS |

> [!TIP]
> En Windows, lo más fácil es `Music-Now-Playing-Setup-<versión>.exe`: también instala el plugin en la carpeta que corresponde a tu versión de OBS y quita las copias de antes (opción **Plugin para OBS**, marcada por defecto; pide permiso de administrador), sin descomprimir nada.

> [!IMPORTANT]
> **Windows y OBS 33:** OBS 33 [cambió la carpeta de los plugins](https://obsproject.com/kb/legacy-plugin-developer-info) a `C:\ProgramData\obs-studio\plugins\`. Con el plugin en la carpeta nueva, borra las copias de antes: `C:\Program Files\obs-studio\obs-plugins\64bit\obs-now-playing.dll`, `C:\Program Files\obs-studio\data\obs-plugins\obs-now-playing\` y la carpeta `bin` de `C:\ProgramData\obs-studio\plugins\obs-now-playing\`. Si actualizas OBS a la 33 con el plugin en la carpeta de antes, el plugin (1.0.35 o más nuevo) ofrece moverse solo al abrir OBS: acepta el permiso de administrador y reinicia OBS.
>
> **OBS 31 o 32:** copia las carpetas `obs-plugins` y `data` del zip dentro de `C:\Program Files\obs-studio\` (acepta reemplazar y el permiso de administrador) y comprueba que quedó `C:\Program Files\obs-studio\obs-plugins\64bit\obs-now-playing.dll`.

```bash
xattr -cr ~/Library/Application\ Support/obs-studio/plugins/obs-now-playing.plugin
```

> [!TIP]
> En Finder: **Ir → Ir a la carpeta…** y pega la ruta. Si la carpeta `plugins` no existe, créala.

## 🎛️ Usarlo

1. **Fuentes → + → ♫ Now Playing (música)** para agregar la tarjeta. En sus **Propiedades** cambias el estilo.
2. **Docks → Música** para los botones.
3. Opcional: en las Propiedades de la fuente, **Crear canal de música** para manejar el volumen de la música aparte.

> [!NOTE]
> En Mac, para que los botones funcionen con YouTube Music, activa una vez en Chrome: **Ver → Opciones para desarrolladores → Permitir JavaScript desde Apple Events**.

## ❓ Preguntas frecuentes

<details>
<summary><b>No sale la carátula</b></summary>

En los macOS recientes el sistema ya no entrega la imagen. El plugin se la pide a la app de Spotify (macOS pregunta si OBS puede controlar Spotify: responde Permitir) y, para los demás reproductores, la busca por artista y canción en el catálogo público de Apple. En Windows, si el reproductor no manda imagen (los navegadores muchas veces no), hace la misma búsqueda y, para YouTube en el navegador, usa la miniatura del video.
</details>

<details>
<summary><b>Los botones anterior / siguiente no cambian la canción</b></summary>

- **YouTube Music (Mac):** activa *Permitir JavaScript desde Apple Events* (ver arriba).
- **Otros (Mac):** se simulan las teclas de música, y eso necesita **Ajustes del Sistema → Privacidad y seguridad → Accesibilidad → OBS**.
- El dock muestra debajo de los botones qué falta.
</details>

<details>
<summary><b>El dock dice que el lector del sistema no funciona</b></summary>

El mensaje dice por qué. El registro de OBS (**Ayuda → Archivos de registro**) tiene las líneas `[obs-now-playing]`.
</details>

<details>
<summary><b>macOS dice que el plugin está dañado o lo bloquea</b></summary>

Pega el comando `xattr -cr …` de los pasos de instalación. Pasa porque el plugin no está firmado con una cuenta paga de desarrollador de Apple.
</details>

<details>
<summary><b>¿Cómo se actualiza?</b></summary>

Al abrir OBS, si hay una versión nueva sale un aviso con **Actualizar ahora**: la descarga, reemplaza el plugin y pide reiniciar OBS. En Windows, como el plugin está en una carpeta de administrador, Windows pide permiso para copiarlo; con OBS 33+ la actualización lo deja en la carpeta nueva y quita la copia de antes. También en **Herramientas → Now Playing: buscar actualizaciones**.
</details>

## 🖥️ App aparte (opcional)

Además del plugin hay una app que corre por su cuenta y sirve la tarjeta como página web, para otros programas de streaming o para una Fuente de texto. La mayoría solo necesita el plugin.

<details>
<summary><b>Cómo funciona la app y cómo instalarla</b></summary>

### Cómo funciona

Windows y macOS ya saben qué está sonando: es lo que muestran el control de volumen de Windows y el "Ahora suena" del Centro de control del Mac. La app lee esa información y la sirve como una página que OBS muestra con una **Fuente de navegador**.

```
Spotify / YouTube Music / Apple Music…
        │  (el reproductor avisa al sistema)
        ▼
Windows: controles multimedia (SMTC) ── PowerShell ─┐
macOS:   "Ahora suena" (MediaRemote) ── osascript ──┼─► app local (127.0.0.1:8787) ─► Fuente de navegador en OBS
Plan B:  script de Tampermonkey en el navegador ────┘                              └─► now-playing.txt (Fuente de texto)
```

La idea de leer los controles multimedia del sistema viene de [SMTC Bridge](https://github.com/nuttylmao/smtc-bridge) (solo Windows); esta app la lleva también a macOS.

### Instalar

Descarga el instalador desde **[Releases](../../releases/latest)**. No necesitas Node.js ni la terminal.

> Music Now Playing es un plugin y una app para OBS Studio. Antes se llamaba "OBS Now Playing": la app instalada, sus accesos directos y sus carpetas conservan ese nombre para que las instalaciones existentes sigan funcionando y actualizándose.

**Windows 10/11:** abre `Music-Now-Playing-Setup-<versión>.exe`. Si sale **"Windows protegió su PC"**, pulsa **Más información → Ejecutar de todas formas**. Por defecto arranca con Windows. También instala el plugin de OBS (se puede desmarcar).

**macOS 11+:** descarga `…-mac-apple.zip` (chip Apple) o `…-mac-intel.zip` (Intel), descomprímelo, arrastra **OBS Now Playing** (la app conserva su nombre anterior) a la carpeta **Aplicaciones** (si la abres desde Descargas no queda instalada) y ábrela. Al abrirse instala también el **plugin de OBS** en `~/Library/Application Support/obs-studio/plugins/` (o lo actualiza si el que tienes es más viejo): abre o reinicia OBS y aparece **Fuentes → + → ♫ Now Playing (música)**. Si la bloquea: **Ajustes del Sistema → Privacidad y seguridad → Abrir igualmente**. Si dice *"está dañado"*:

```bash
xattr -cr "/Applications/OBS Now Playing.app" && open "/Applications/OBS Now Playing.app"
```

La app no tiene ventana: al abrirla se abre su **panel** en **http://127.0.0.1:8787/** (qué suena, apps detectadas y la URL para OBS).

### Añadirla a OBS

**Fuentes → + → Navegador**, URL `http://127.0.0.1:8787/overlay`, ancho `640`, alto `200`. Botones: **Docks → Docks de navegador personalizados**, URL `http://127.0.0.1:8787/controls`.

Opciones para la URL (se combinan con `&`):

| Parámetro | Efecto |
|---|---|
| `theme=light` | Tema claro |
| `pos=br` | Esquina: `bl` (por defecto), `br`, `tl`, `tr` |
| `art=0` / `art=big` | Sin carátula / carátula grande |
| `colors=0` | Sin los colores de la carátula |
| `blur=1` | Fondo con la carátula difuminada |
| `controls=1` | Botones en la tarjeta (se ven en el stream; se pulsan con **Interactuar**) |
| `progress=0` | Sin barra de progreso |
| `label=0` | Sin la línea "Escuchando en …" |
| `width=480` | Ancho de la tarjeta en px |
| `accent=ff00aa` | Color de acento fijo |
| `preview=1` | Canción de ejemplo cuando no suena nada |

**Solo texto:** una **Fuente de texto** que lea `now-playing.txt` (el panel muestra la ruta; el formato va en `textFormat` de `config.json`).

**Plan B:** si el lector del sistema no funciona, instala [Tampermonkey](https://www.tampermonkey.net/) y pulsa **Instalar script** en el panel.

### Problemas frecuentes

- **No aparece nada en OBS:** abre el panel; si no carga, la app está cerrada. Prueba con `?preview=1`.
- **Sale un video de YouTube en vez de la música:** oculta el navegador en **Apps detectadas**.
- **El puerto 8787 está ocupado:** cambia `port` en `config.json` (y `ENDPOINT` en el script de Tampermonkey).
- **Registro:** `log.txt` en `%APPDATA%\OBS Now Playing` (Windows) o `~/Library/Application Support/OBS Now Playing` (macOS).
</details>

## 🔒 Privacidad

- Todo corre en tu computador; no hay servidores nuestros.
- El plugin solo se conecta a internet para buscar actualizaciones en GitHub, para buscar la carátula que falte en el catálogo público de Apple (nombre de la canción y artista), en el modo video para buscar en YouTube el video de la canción (título y artista; en Windows también cuando no se puede leer la barra de direcciones) y para cargar el reproductor de YouTube.
- La app solo escucha en `127.0.0.1` y no deja que otras páginas web lean lo que escuchas.

## 🛠️ Para desarrolladores

<details>
<summary><b>Compilar y estructura del proyecto</b></summary>

- **Plugin:** CMake desde `obs-plugin/` (`cmake --preset macos` → Xcode o `cmake --preset windows-x64` → Visual Studio 2022). Ver [obs-plugin/README.md](obs-plugin/README.md).
- **App:** `npm start` (Node 18+); `OBS_NOW_PLAYING_DATA=/otra/ruta` cambia la carpeta de datos. Instaladores: `./build/build.sh [all|windows|mac]`.
- **Lectores del sistema:** `media/windows.ps1` (PowerShell 5.1 + WinRT) y `media/macos.js` (JavaScript for Automation). Los dos escriben una línea JSON por segundo. Desde macOS 15.4 solo los programas del sistema pueden leer "Ahora suena", por eso el lector corre en `osascript`.
- **Releases:** el flujo **Release** de GitHub Actions compila todo y lo publica.
</details>

## 📄 Licencia

MIT — ver [LICENSE](LICENSE). El plugin de OBS es GPL-2.0 o posterior (como todo plugin que enlaza con libobs), ver `obs-plugin/LICENSE`.
