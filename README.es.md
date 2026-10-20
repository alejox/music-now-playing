<div align="center">

# 🎵 OBS Now Playing

**Muestra en tu stream la canción que estás escuchando, con su carátula, sus colores y botones para manejarla.**

[![Última versión](https://img.shields.io/github/v/release/alejox/obs-music-player?label=versi%C3%B3n&color=1db954)](../../releases/latest)
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
| 🎬 **Modo video** | Con YouTube Music / YouTube, el video en vez de la carátula (sin sonido y sincronizado). |
| 🖼️ **Tu logo** | Una imagen tuya para las canciones sin carátula. |
| ⏯️ **Dock de control** | Anterior / pausa / siguiente y un **buscador de canciones**, dentro de OBS y fuera del stream. |
| 🔊 **Canal de música** | Con un clic, un canal en el Mezclador de audio solo con el sonido del reproductor. |
| 🔄 **Actualizaciones** | OBS te avisa cuando hay una versión nueva y la instala por ti. |

## 📦 Instalar el plugin

Descarga el zip de tu sistema desde **[Releases](../../releases/latest)** y **cierra OBS antes**.

| | macOS (OBS 31+) | Windows (OBS 31+) |
|---|---|---|
| **Archivo** | `obs-now-playing-plugin-<versión>-macos.zip` | `obs-now-playing-plugin-<versión>-windows-x64.zip` |
| **1** | Descomprímelo: queda `obs-now-playing.plugin` | Descomprímelo en `C:\ProgramData\obs-studio\plugins\` |
| **2** | Cópialo a `~/Library/Application Support/obs-studio/plugins/` | — |
| **3** | Pega en la Terminal el comando de abajo (una vez) | — |
| **4** | Abre OBS | Abre OBS |

```bash
xattr -cr ~/Library/Application\ Support/obs-studio/plugins/obs-now-playing.plugin
```

> [!TIP]
> En Finder: **Ir → Ir a la carpeta…** y pega la ruta. Si la carpeta `plugins` no existe, créala.

## 🎛️ Usarlo

1. **Fuentes → + → Now Playing (música)** para agregar la tarjeta. En sus **Propiedades** cambias el estilo.
2. **Docks → Música** para los botones y el buscador.
3. Opcional: en las Propiedades de la fuente, **Crear canal de música** para manejar el volumen de la música aparte.

### 🔍 Buscador de canciones (dock)

Escribe una canción o un artista y pulsa **Enter**. Lo que pasa depende del reproductor:

| Reproductor | Qué hace el buscador |
|---|---|
| YouTube Music en el navegador (Mac) | Busca en su pestaña y **pone el primer resultado** |
| Spotify | Abre la búsqueda en la app de Spotify y ahí eliges la canción |
| Apple Music | Pone la primera que coincida en tu biblioteca |
| Windows / otros | Abre la búsqueda en Spotify o en YouTube Music |

> [!NOTE]
> En Mac, para que los botones y el buscador funcionen con YouTube Music, activa una vez en Chrome: **Ver → Opciones para desarrolladores → Permitir JavaScript desde Apple Events**.

## ❓ Preguntas frecuentes

<details>
<summary><b>No sale la carátula</b></summary>

En los macOS recientes el sistema ya no entrega la imagen. El plugin se la pide a la app de Spotify (macOS pregunta si OBS puede controlar Spotify: responde Permitir) y, para los demás reproductores, la busca por artista y canción en el catálogo público de Apple.
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

Al abrir OBS, si hay una versión nueva sale un aviso con **Actualizar ahora**: la descarga, reemplaza el plugin y pide reiniciar OBS. También en **Herramientas → Now Playing: buscar actualizaciones**.
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

**Windows 10/11:** abre `OBS-Now-Playing-Setup-<versión>.exe`. Si sale **"Windows protegió su PC"**, pulsa **Más información → Ejecutar de todas formas**. Por defecto arranca con Windows.

**macOS 11+:** descarga `…-mac-apple.zip` (chip Apple) o `…-mac-intel.zip` (Intel), descomprímelo, arrastra **OBS Now Playing** a Aplicaciones y ábrela. Si la bloquea: **Ajustes del Sistema → Privacidad y seguridad → Abrir igualmente**. Si dice *"está dañado"*:

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
- El plugin solo se conecta a internet para buscar actualizaciones en GitHub, para buscar la carátula que falte en el catálogo público de Apple (nombre de la canción y artista) y, en el modo video, para cargar el reproductor de YouTube.
- La app solo escucha en `127.0.0.1` y no deja que otras páginas web lean lo que escuchas.

## 🛠️ Para desarrolladores

<details>
<summary><b>Compilar y estructura del proyecto</b></summary>

- **Plugin:** CMake desde `obs-plugin/` (`cmake --preset macos` → Xcode, `cmake --preset windows-x64` → Visual Studio 2022). Ver [obs-plugin/README.md](obs-plugin/README.md).
- **App:** `npm start` (Node 18+); `OBS_NOW_PLAYING_DATA=/otra/ruta` cambia la carpeta de datos. Instaladores: `./build/build.sh [all|windows|mac]`.
- **Lectores del sistema:** `media/windows.ps1` (PowerShell 5.1 + WinRT) y `media/macos.js` (JavaScript for Automation). Los dos escriben una línea JSON por segundo. Desde macOS 15.4 solo los programas del sistema pueden leer "Ahora suena", por eso el lector corre en `osascript`.
- **Releases:** el flujo **Release** de GitHub Actions compila todo y lo publica.
</details>

## 📄 Licencia

MIT — ver [LICENSE](LICENSE). El plugin de OBS es GPL-2.0 o posterior (como todo plugin que enlaza con libobs), ver `obs-plugin/LICENSE`.
