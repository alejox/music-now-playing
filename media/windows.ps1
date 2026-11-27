# Lee "lo que está sonando" en Windows: los System Media Transport Controls (SMTC),
# la misma información que muestra el control de volumen. Escribe una línea JSON
# por segundo en la salida estándar; server.js la lee. Los comandos llegan como archivos
# (aquí se borran al leerlos): el plugin de OBS deja uno por comando en la carpeta
# -CommandDir, con un número que crece en el nombre (00000000001234567890.json), y se
# atienden todos en ese orden (si se pulsa "siguiente" varias veces seguidas, no se pierde
# ninguna); server.js escribe uno solo en el archivo -CommandFile. Cada uno es:
#
#   {"cmd":"next","app":"Spotify.exe"}      cmd: previous | playpause | next
#   {"cmd":"seek","app":"chrome.exe","positionMs":61000}   saltar a esa posición
#   {"cmd":"shuffle","app":"Spotify.exe"}   activar / desactivar el aleatorio
#
# Si el reproductor no deja saltar o cambiar el aleatorio, se escribe {"commandError":"…"}
# (no es un error del lector). El volumen no pasa por aquí: lo cambia el plugin en el
# Mezclador de volumen de Windows.
#
# Es un archivo y no la entrada estándar a propósito: en Windows PowerShell 5.1 leer
# la entrada sin bloquear el bucle no es confiable, y un lector bloqueado congela el overlay.
#
# Carátula: la que entrega el reproductor. Si no trae (navegadores, algunas apps), se busca
# por artista y canción en el catálogo público de Apple (iTunes Search, igual que en macOS)
# y, para YouTube en el navegador, se usa la miniatura del video. Esas búsquedas en internet
# no frenan la lectura: corren aparte y la carátula llega en una línea posterior (ver
# Get-Lookup).
#
# Modo video (plugin de OBS): mientras exista el archivo -VideoFlag, para lo que suena en
# un navegador se agrega "videoId", el id del video de YouTube / YouTube Music leído de la
# barra de direcciones (UI Automation) de la ventana cuya pestaña visible es YouTube. Si la
# barra no se puede leer, se busca el video por su título en YouTube. Con él va "site":
# "ytmusic" (music.youtube.com) o "youtube", para decir "Escuchando en …". Para Spotify,
# Apple Music y las demás apps, el video se busca en YouTube por título y artista.
#
#   {"sessions":[{"app":"Spotify.exe","title":"…","artist":"…","album":"…",
#                 "isPlaying":true,"current":true,"progressMs":1234,"durationMs":5678,
#                 "shuffle":false,                        ← solo si el reproductor deja cambiarlo
#                 "artwork":"data:image/png;base64,…"}]}      ← artwork solo cuando cambia
#   {"error":"…"}
#
# iTunes no se anuncia en los SMTC: mientras está abierto, se lee por su interfaz COM
# (iTunes.Application) y sale como una sesión más, "iTunes.exe" (ver $itunesCode). AIMP
# tampoco (sin un complemento): se lee por su interfaz remota y sale como "AIMP.exe" (ver
# Read-Aimp).
#
# Usa solo lo que trae Windows PowerShell 5.1 (Windows 10/11): sin módulos extra.

param([string]$CommandFile = '', [string]$CommandDir = '', [string]$VideoFlag = '', [switch]$SelfTest)

$ErrorActionPreference = 'Stop'
$stdout = New-Object System.IO.StreamWriter ([Console]::OpenStandardOutput()), (New-Object System.Text.UTF8Encoding $false)
$stdout.AutoFlush = $true

function Emit($obj) {
  $stdout.WriteLine(($obj | ConvertTo-Json -Depth 5 -Compress))
}

try {
  Add-Type -AssemblyName System.Runtime.WindowsRuntime
  $null = [Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager,Windows.Media.Control,ContentType=WindowsRuntime]
  $null = [Windows.Storage.Streams.IRandomAccessStreamWithContentType,Windows.Storage.Streams,ContentType=WindowsRuntime]
} catch {
  Emit @{ error = "Este Windows no expone los controles multimedia (se necesita Windows 10 1809 o superior): $($_.Exception.Message)" }
  exit 2
}

# PowerShell no sabe esperar operaciones WinRT: las convertimos en Task de .NET.
$asTaskMethod = [System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
  $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1'
} | Select-Object -First 1

function Await($operation, [Type]$resultType) {
  $task = $asTaskMethod.MakeGenericMethod($resultType).Invoke($null, @($operation))
  if (-not $task.Wait(3000)) { throw 'Windows tardó demasiado en responder' }
  $task.Result
}

$mediaPropsType = [Windows.Media.Control.GlobalSystemMediaTransportControlsSessionMediaProperties]

# Ejecuta un comando sobre la sesión de esa app (o la actual de Windows si ya no está).
function Send-MediaCommand($line) {
  $msg = $line | ConvertFrom-Json
  $target = $null
  foreach ($s in $manager.GetSessions()) {
    if ($s.SourceAppUserModelId -eq $msg.app) { $target = $s }
  }
  # AIMP (por su interfaz remota): se le manda directo.
  if (-not $target -and "$($msg.app)" -eq $aimpApp) {
    Send-AimpCommand $msg
    return
  }
  # iTunes (leído por COM): el comando va en la próxima lectura de su runspace.
  if (-not $target -and "$($msg.app)" -eq $itunesApp) {
    [void]$itunes.commands.Add($msg)
    Update-ITunes
    return
  }
  if (-not $target) { $target = $manager.GetCurrentSession() }
  if (-not $target) { return }
  $operation = switch ($msg.cmd) {
    'previous' { $target.TrySkipPreviousAsync() }
    'playpause' { $target.TryTogglePlayPauseAsync() }
    'next' { $target.TrySkipNextAsync() }
    # La posición va en unidades de 100 ns.
    'seek' { $target.TryChangePlaybackPositionAsync([int64]$msg.positionMs * 10000) }
    'shuffle' { $target.TryChangeShuffleActiveAsync(-not [bool]$target.GetPlaybackInfo().IsShuffleActive) }
  }
  if ($operation) {
    $done = Await $operation ([bool])
    if ($msg.cmd -eq 'seek' -and -not $done) {
      Emit @{ commandError = 'Este reproductor no deja adelantar ni retroceder desde OBS.' }
    }
    if ($msg.cmd -eq 'shuffle' -and -not $done) {
      Emit @{ commandError = 'Este reproductor no deja cambiar el aleatorio desde OBS.' }
    }
  }
}

function Read-Thumbnail($reference) {
  $stream = Await ($reference.OpenReadAsync()) ([Windows.Storage.Streams.IRandomAccessStreamWithContentType])
  try {
    $memory = New-Object System.IO.MemoryStream
    [System.IO.WindowsRuntimeStreamExtensions]::AsStreamForRead($stream).CopyTo($memory)
    $bytes = $memory.ToArray()
    if ($bytes.Length -eq 0) { return $null }
    $mime = if ($stream.ContentType -like 'image/*') { $stream.ContentType } else { 'image/jpeg' }
    @{
      url  = "data:$mime;base64," + [Convert]::ToBase64String($bytes)
      hash = [BitConverter]::ToString($md5.ComputeHash($bytes))
    }
  } finally {
    $stream.Dispose()
  }
}

# Descargas (carátulas y búsquedas): Windows PowerShell 5.1 no siempre trae TLS 1.2 activado.
try { [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12 } catch {}

# Lo que va a internet. Se define aquí (para -SelfTest y los nombres) y también se carga en
# el runspace de las búsquedas (ver Get-Lookup): por eso va en un bloque y no suelto.
$netCode = {
  $md5 = [System.Security.Cryptography.MD5]::Create()

  # Cada descarga espera como mucho 4 s (conectar y cada lectura).
  function Get-WebBytes($url) {
    $request = [System.Net.HttpWebRequest]::Create($url)
    $request.Timeout = 4000
    $request.ReadWriteTimeout = 4000
    $request.UserAgent = 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0 Safari/537.36'
    $request.Headers.Add('Accept-Language', 'en-US,en;q=0.8')
    # Sin esta cookie, en Europa YouTube responde con la página de consentimiento.
    $request.Headers.Add('Cookie', 'SOCS=CAI; CONSENT=YES+')
    $response = $request.GetResponse()
    try {
      $memory = New-Object System.IO.MemoryStream
      $response.GetResponseStream().CopyTo($memory)
      $memory.ToArray()
    } finally {
      $response.Dispose()
    }
  }

  function Get-WebImage($url) {
    $bytes = $null
    try { $bytes = Get-WebBytes $url } catch { return $null }
    if (-not $bytes -or $bytes.Length -lt 1000) { return $null }
    @{
      url  = 'data:image/jpeg;base64,' + [Convert]::ToBase64String($bytes)
      hash = [BitConverter]::ToString($md5.ComputeHash($bytes))
    }
  }

  # "Canción (Official Video)" o "Artista - Topic" son cosas de YouTube, no del disco; un
  # "feat. Otro" sin paréntesis al final del título, tampoco.
  function Clean-Name($s) { ((("$s" -replace '\s*[\(\[][^\)\]]*[\)\]]', '') -replace '(?i)\s+(feat\.?|ft\.?|featuring)\s.*$', '') -replace '\s+-\s+topic$', '').Trim() }
  function Normalize-Name($s) {
    $plain = "$s".ToLowerInvariant().Normalize([Text.NormalizationForm]::FormD) -replace '\p{Mn}', ''
    $plain -replace '[^a-z0-9]', ''
  }

  # YouTube Music une varios artistas con una conjunción en el idioma de la página
  # ("YODAKA y Kainbeats", "A & B", "A, B", "A x B", "A feat. B"): el primero es el principal.
  function Get-PrimaryArtist($artist) {
    ((Clean-Name $artist) -split '(?i)\s*,\s*|\s*&\s*|\s+(?:y|e|and|x|con|with|feat\.?|ft\.?|featuring)\s+')[0].Trim()
  }

  # Carátula del catálogo público de Apple (iTunes Search, sin cuenta ni clave). Solo se
  # acepta si artista y título coinciden, para no mostrar una carátula equivocada. Como
  # mucho 3 intentos: artista completo y canción; artista principal y canción; solo la
  # canción, aceptando el resultado si es del artista principal.
  function Search-CatalogArtwork($title, $artist) {
    $full = Clean-Name $artist
    $song = Clean-Name $title
    $t = Normalize-Name $song
    if (-not (Normalize-Name $full) -or -not $t) { return $null }
    $main = Get-PrimaryArtist $artist
    if (-not $main) { $main = $full }
    $attempts = @(@{ term = "$full $song"; artist = $full })
    if ((Normalize-Name $main) -ne (Normalize-Name $full)) { $attempts += @{ term = "$main $song"; artist = $main } }
    $attempts += @{ term = $song; artist = $main }
    foreach ($attempt in $attempts) {
      $a = Normalize-Name $attempt.artist
      try {
        $term = [Uri]::EscapeDataString($attempt.term)
        $json = [Text.Encoding]::UTF8.GetString((Get-WebBytes "https://itunes.apple.com/search?media=music&entity=song&limit=10&term=$term")) | ConvertFrom-Json
        $hit = $json.results | Where-Object {
          $ra = Normalize-Name $_.artistName
          $rt = Normalize-Name $_.trackName
          $ra -and $rt -and ($ra.Contains($a) -or $a.Contains($ra)) -and ($rt.Contains($t) -or $t.Contains($rt))
        } | Select-Object -First 1
        if ($hit -and $hit.artworkUrl100) {
          return Get-WebImage ($hit.artworkUrl100 -replace '/\d+x\d+bb\.', '/600x600bb.')
        }
      } catch {}
    }
    $null
  }

  # Miniatura de un video de YouTube (la grande si existe; si no, la mediana, sin franjas).
  function Get-VideoThumbnail($id) {
    $art = Get-WebImage "https://i.ytimg.com/vi/$id/maxresdefault.jpg"
    if (-not $art) { $art = Get-WebImage "https://i.ytimg.com/vi/$id/mqdefault.jpg" }
    $art
  }

  # Si la barra de direcciones no se puede leer, se busca el video en YouTube por título y
  # canal: el primer resultado con ese mismo título (o el primero, si ninguno coincide).
  function Search-YouTubeVideoId($title, $artist) {
    $query = [Uri]::EscapeDataString("$title $artist".Trim())
    $html = [Text.Encoding]::UTF8.GetString((Get-WebBytes "https://www.youtube.com/results?search_query=$query"))
    $pattern = '"videoRenderer":\{"videoId":"([A-Za-z0-9_-]{11})".{0,4000}?"title":\{"runs":\[\{"text":"((?:[^"\\]|\\.)*)"'
    $results = [regex]::Matches($html, $pattern)
    if ($results.Count -eq 0) { return '' }
    $wanted = Normalize-Name $title
    foreach ($result in ($results | Select-Object -First 5)) {
      $name = $result.Groups[2].Value
      try { $name = ConvertFrom-Json ('"' + $name + '"') } catch {}
      if ((Normalize-Name $name) -eq $wanted) { return $result.Groups[1].Value }
    }
    $results[0].Groups[1].Value
  }
}
. $netCode

# ---- Búsquedas sin frenar la lectura -------------------------------------------------
# El plugin da por muerto un lector que no escribe en 10 s: con la red lenta al cambiar de
# canción, la tarjeta desaparecía en pleno directo. Por eso las búsquedas en internet corren
# en otro runspace de PowerShell (sin módulos extra: Windows PowerShell 5.1 no trae
# Start-ThreadJob), de a una y en orden, y el bucle solo mira en cada vuelta si ya
# terminaron: la línea sale sin esperar y la carátula o el video llegan en una posterior.
# Cada descarga tiene 4 s como máximo; una búsqueda que igual se cuelga se abandona a los
# 20 s. El resultado (también "no se encontró") se guarda: una búsqueda por canción.
$lookups = @{}   # clave → @{ done; value } (value: texto, '' si no se encontró o falló)
$lookupQueue = New-Object System.Collections.Queue
$lookupJob = $null
$lookupRunspace = $null

# Pide la búsqueda (si no se pidió antes) y devuelve cómo va: @{ done = $false } mientras
# corre, @{ done = $true; value = '…' } al terminar. call es el código que corre en el
# runspace, con los argumentos en $args.
function Get-Lookup($key, [string]$call, [object[]]$arguments) {
  if (-not $lookups.ContainsKey($key)) {
    $lookups[$key] = @{ done = $false; value = '' }
    $lookupQueue.Enqueue(@{ key = $key; call = $call; arguments = $arguments })
    Update-Lookups
  }
  $lookups[$key]
}

# Recoge la búsqueda que terminó y lanza la siguiente. No espera nunca.
function Update-Lookups {
  $job = $script:lookupJob
  if ($job) {
    if ($job.handle.IsCompleted) {
      $value = ''
      try {
        $out = $job.ps.EndInvoke($job.handle)
        if ($out.Count -gt 0 -and $null -ne $out[0]) { $value = "$($out[0])" }
      } catch {}
      $job.ps.Dispose()
      $lookups[$job.key] = @{ done = $true; value = $value }
      $script:lookupJob = $null
    } elseif (([DateTime]::UtcNow - $job.started).TotalSeconds -gt 20) {
      # Colgada: se deja con su runspace (se cierra con el lector) y se sigue con otra.
      try { [void]$job.ps.BeginStop($null, $null) } catch {}
      $lookups[$job.key] = @{ done = $true; value = '' }
      $script:lookupJob = $null
      $script:lookupRunspace = $null
    } else {
      return
    }
  }
  if ($lookupQueue.Count -eq 0) { return }
  if (-not $script:lookupRunspace) {
    $script:lookupRunspace = [runspacefactory]::CreateRunspace()
    $script:lookupRunspace.Open()
  }
  $next = $lookupQueue.Dequeue()
  $ps = [PowerShell]::Create()
  $ps.Runspace = $script:lookupRunspace
  [void]$ps.AddScript($netCode.ToString() + "`n" + $next.call)
  foreach ($argument in $next.arguments) { [void]$ps.AddArgument($argument) }
  $script:lookupJob = @{ key = $next.key; ps = $ps; handle = $ps.BeginInvoke(); started = [DateTime]::UtcNow }
}

# Las búsquedas que corren en el runspace (devuelven texto: la carátula como data: o el id).
$catalogCall = '$art = Search-CatalogArtwork $args[0] $args[1]; if ($art) { $art.url }'
$thumbnailCall = '$art = Get-VideoThumbnail $args[0]; if ($art) { $art.url }'
$videoSearchCall = 'try { Search-YouTubeVideoId $args[0] $args[1] } catch {}'

# Una carátula que llegó como data: (de una búsqueda): su "hash" para compararla.
function New-ArtFromUrl($url) {
  @{ url = $url; hash = "$($url.Length):$($url.Substring([Math]::Max(0, $url.Length - 96)))" }
}

# El id del video de YouTube que suena en un navegador, leído de la barra de direcciones de
# sus ventanas (la de la pestaña visible). Una ventana sirve si su título (el de la pestaña)
# dice YouTube o trae el nombre de la canción.
$browserApps = 'chrome|msedge|brave|vivaldi|opera|firefox'
# Firefox no dice "firefox" en su id de app: es un hash de su carpeta (308046B0AF4A39CB en
# la instalación normal). Firefox anota los suyos en el registro (TaskBarIDs).
$firefoxIds = @{ '308046B0AF4A39CB' = $true }
foreach ($path in 'HKCU:\Software\Mozilla\Firefox\TaskBarIDs', 'HKLM:\Software\Mozilla\Firefox\TaskBarIDs', 'HKLM:\Software\WOW6432Node\Mozilla\Firefox\TaskBarIDs') {
  try {
    foreach ($prop in (Get-ItemProperty -Path $path -ErrorAction Stop).PSObject.Properties) {
      if ($prop.Name -notlike 'PS*' -and "$($prop.Value)" -match '^[0-9A-Fa-f]{16}$') { $firefoxIds["$($prop.Value)"] = $true }
    }
  } catch {}
}
function Test-Firefox($app) { [bool]$firefoxIds["$app"] }
function Test-BrowserApp($app) { ($app -match $browserApps) -or (Test-Firefox $app) }
$youtubeWatch = '^(?:https?://)?(music\.|www\.|m\.)?youtube\.com/watch\?(?:[^#]*&)?v=([A-Za-z0-9_-]{11})'
$videoIds = @{}   # "app`ncanción" → @{ id; site }
$videoTries = @{}
$uiaReady = $false

# La barra de direcciones está en la parte del navegador, no en la página: se recorre el
# árbol por niveles sin entrar a la página (Document), que es enorme y tiene sus propios
# cuadros de texto (el buscador de YouTube).
function Read-AddressBar($window) {
  $walker = [System.Windows.Automation.TreeWalker]::ControlViewWalker
  $editType = [System.Windows.Automation.ControlType]::Edit
  $documentType = [System.Windows.Automation.ControlType]::Document
  $queue = New-Object System.Collections.Queue
  $queue.Enqueue($window)
  $visited = 0
  while ($queue.Count -gt 0 -and $visited -lt 400) {
    $child = $walker.GetFirstChild($queue.Dequeue())
    while ($child -and $visited -lt 400) {
      $visited++
      $type = $child.Current.ControlType
      if ($type -eq $editType) {
        $value = ''
        try { $value = "$($child.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value)" } catch {}
        if ($value -match $youtubeWatch) { return $value }
      } elseif ($type -ne $documentType) {
        $queue.Enqueue($child)
      }
      $child = $walker.GetNextSibling($child)
    }
  }
  ''
}

# Las ventanas cuya pestaña visible es YouTube: { id = video de su barra ('' si no se pudo
# leer); site = ytmusic | youtube (de la barra o, si no, del título); name = título }.
function Read-YouTubeWindows {
  if (-not $script:uiaReady) {
    Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
    $script:uiaReady = $true
  }
  $auto = [System.Windows.Automation.AutomationElement]
  $condition = New-Object System.Windows.Automation.PropertyCondition($auto::ControlTypeProperty, [System.Windows.Automation.ControlType]::Window)
  $windows = New-Object System.Collections.ArrayList
  foreach ($window in $auto::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, $condition)) {
    $name = "$($window.Current.Name)".ToLower()
    if ($name -notmatch 'youtube') { continue }
    $url = ''
    try { $url = Read-AddressBar $window } catch {}
    $id = ''
    $site = if ($name -match 'youtube music') { 'ytmusic' } else { 'youtube' }
    if ($url -match $youtubeWatch) {
      $id = $Matches[2]
      $site = if ($Matches[1] -eq 'music.') { 'ytmusic' } else { 'youtube' }
    }
    [void]$windows.Add(@{ id = $id; site = $site; name = $name })
  }
  , $windows
}

# site (de las pestañas, si ya se sabe): solo cuentan las ventanas de ese sitio. Si no, con la
# canción en YouTube Music y la ventana visible en YouTube, se tomaba el video de esa otra
# pestaña. Devuelve @{ id; site }, @{ id = ''; site } si hay ventanas de YouTube pero
# ninguna barra dio el video (hay que buscarlo en YouTube) o $null. Sin internet.
function Find-YouTubeVideo($title, $site = '') {
  $windows = Read-YouTubeWindows
  $found = @($windows | Where-Object { $_.id -and (-not $site -or $_.site -eq $site) })
  # Con varias ventanas de YouTube, la que tiene el título de la canción.
  $lower = "$title".ToLower()
  $hit = $found | Where-Object { $lower -and $_.name.Contains($lower) } | Select-Object -First 1
  if (-not $hit -and $found.Count -eq 1) { $hit = $found[0] }
  if ($hit) { return @{ id = $hit.id; site = $hit.site } }
  if ($windows.Count -gt 0) {
    if (-not $site) { $site = if (@($windows | Where-Object { $_.site -eq 'ytmusic' }).Count -gt 0) { 'ytmusic' } else { 'youtube' } }
    return @{ id = ''; site = $site }
  }
  $null
}

# ---- YouTube o YouTube Music en el navegador ("site") --------------------------------
# Windows solo dice que suena "Chrome": para saber si es YouTube Music, YouTube u otra cosa
# (el filtro "Solo YouTube Music" del plugin y "Escuchando en …") se leen los nombres de
# TODAS las pestañas de los navegadores (no solo la visible: la música sigue sonando aunque
# se cambie de pestaña). Aparte del video, para que una búsqueda sin sitio no lo tape.

# Los nombres (en minúsculas) de las pestañas de las ventanas de navegador. Como la barra de
# direcciones, se recorre la parte del navegador sin entrar a la página.
function Read-BrowserTabs {
  if (-not $script:uiaReady) {
    Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
    $script:uiaReady = $true
  }
  $auto = [System.Windows.Automation.AutomationElement]
  $walker = [System.Windows.Automation.TreeWalker]::ControlViewWalker
  $tabType = [System.Windows.Automation.ControlType]::TabItem
  $documentType = [System.Windows.Automation.ControlType]::Document
  $condition = New-Object System.Windows.Automation.PropertyCondition($auto::ControlTypeProperty, [System.Windows.Automation.ControlType]::Window)
  $names = New-Object System.Collections.ArrayList
  foreach ($window in $auto::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, $condition)) {
    if ("$($window.Current.ClassName)" -notmatch '^(Chrome_WidgetWin_1|MozillaWindowClass)$') { continue }
    $queue = New-Object System.Collections.Queue
    $queue.Enqueue($window)
    $visited = 0
    while ($queue.Count -gt 0 -and $visited -lt 600) {
      $child = $walker.GetFirstChild($queue.Dequeue())
      while ($child -and $visited -lt 600) {
        $visited++
        $type = $child.Current.ControlType
        if ($type -eq $tabType) {
          [void]$names.Add("$($child.Current.Name)".ToLower())
        } elseif ($type -ne $documentType) {
          $queue.Enqueue($child)
        }
        $child = $walker.GetNextSibling($child)
      }
    }
    # Por si el navegador no expone sus pestañas: al menos la visible (el título de la ventana).
    [void]$names.Add("$($window.Current.Name)".ToLower())
  }
  , $names
}

# ytmusic | youtube | '' para una canción, según los nombres de las pestañas. Con YouTube y
# YouTube Music abiertos a la vez, de la señal más segura a la menos:
#   1. la pestaña que suena: Chrome y Edge agregan " - Reproduciendo audio" / " - Audio
#      playing" a su nombre (no la palabra "audio" sola: muchos videos se llaman "Official
#      Audio"; y una pestaña silenciada dice otra cosa);
#   2. YouTube Music si la canción trae álbum (YouTube no lo manda) y hay una pestaña suya;
#   3. las pestañas cuyo nombre trae la canción, si son todas del mismo sitio (con un
#      título corto como "Stay", otra pestaña de YouTube puede traerlo sin que suene);
#   4. si todas las pestañas de YouTube son del mismo sitio, ese.
function Select-BrowserSite($names, $title, $album) {
  $youtube = @($names | Where-Object { $_ -match 'youtube' })
  if ($youtube.Count -eq 0) { return '' }
  $siteOf = { param($name) if ($name -match 'youtube music') { 'ytmusic' } else { 'youtube' } }
  $sitesOf = { param($list) @($list | ForEach-Object { & $siteOf $_ } | Sort-Object -Unique) }
  $audible = @(& $sitesOf @($youtube | Where-Object { $_ -match '\s-\s(audio playing|playing audio|reproduciendo audio|reproduciendo sonido)' }))
  if ($audible.Count -eq 1) { return $audible[0] }
  $all = @(& $sitesOf $youtube)
  if ("$album".Trim() -and $all -contains 'ytmusic') { return 'ytmusic' }
  $wanted = Normalize-Name $title
  if ($wanted.Length -ge 3) {
    $hits = @(& $sitesOf @($youtube | Where-Object { (Normalize-Name $_).Contains($wanted) }))
    if ($hits.Count -eq 1) { return $hits[0] }
  }
  if ($all.Count -eq 1) { return $all[0] }
  ''
}

$browserSites = @{} # "app`ncanción" → ytmusic | youtube | 'spotify'
$siteTries = @{}

function Get-BrowserSite($app, $title, $album) {
  $key = "$app`n$title"
  if ($browserSites[$key]) { return $browserSites[$key] }
  if ([int]$siteTries[$key] -ge 3) { return '' }
  $siteTries[$key] = 1 + [int]$siteTries[$key]
  $site = ''
  try { $site = Select-BrowserSite (Read-BrowserTabs) $title $album } catch { $site = '' }
  if ($site) { $browserSites[$key] = $site }
  $site
}

# @{ id; site } del video de YouTube de lo que suena, @{ waiting = $true } mientras se busca
# en YouTube, o $null. En un navegador, el de su pestaña de YouTube; si no hay (el
# reproductor web de Spotify u otra página de música, que traen álbum; un stream o una serie
# no) y en las demás apps (Spotify…), el que da la búsqueda en YouTube (sin site).
$videoSearchSite = @{} # "app`ncanción" → site del video que se está buscando en YouTube
function Get-Video($app, $title, $artist, $album, $site = '') {
  if ($app -match 'obs64') { return $null } # el video de la propia tarjeta
  $key = "$app`n$title"
  if ($videoIds[$key]) { return $videoIds[$key] }
  if ($videoSearchSite.ContainsKey($key)) {
    $search = Get-Lookup "yt`n$title`n$artist" $videoSearchCall @($title, $artist)
    if (-not $search.done) { return @{ waiting = $true } }
    if ($search.value -match '^[A-Za-z0-9_-]{11}$') {
      $videoIds[$key] = @{ id = $search.value; site = $videoSearchSite[$key] }
      return $videoIds[$key]
    }
  }
  if ([int]$videoTries[$key] -ge 3) { return $null } # la dirección cambia un instante después del título
  $videoTries[$key] = 1 + [int]$videoTries[$key]
  $searchSite = $null
  try {
    if (Test-BrowserApp $app) {
      $found = Find-YouTubeVideo $title $site
      if ($found -and $found.id) {
        $videoIds[$key] = $found
        return $found
      }
      if ($found) { $searchSite = $found.site } elseif ($album) { $searchSite = '' }
    } else {
      $searchSite = ''
    }
  } catch { $searchSite = $null }
  if ($null -ne $searchSite -and -not $videoSearchSite.ContainsKey($key)) {
    $videoSearchSite[$key] = $searchSite
    [void](Get-Lookup "yt`n$title`n$artist" $videoSearchCall @($title, $artist))
    return @{ waiting = $true }
  }
  $null
}

# Sin carátula del reproductor: la del catálogo de Apple o, en el navegador, la miniatura
# del video de YouTube. @{ art; waiting } (waiting: todavía se está buscando).
function Get-NetworkArtwork($app, $title, $artist, $album) {
  $catalog = Get-Lookup "art`n$artist`n$title" $catalogCall @($title, $artist)
  if (-not $catalog.done) { return @{ waiting = $true } }
  if ($catalog.value) { return @{ art = (New-ArtFromUrl $catalog.value) } }
  if (-not (Test-BrowserApp $app)) { return @{} }
  $site = Get-BrowserSite $app $title $album
  $video = Get-Video $app $title $artist $album $site
  if (-not $video) { return @{} }
  if ($video.waiting) { return @{ waiting = $true } }
  $thumbnail = Get-Lookup "thumb`n$($video.id)" $thumbnailCall @($video.id)
  if (-not $thumbnail.done) { return @{ waiting = $true } }
  if ($thumbnail.value) { return @{ art = (New-ArtFromUrl $thumbnail.value) } }
  @{}
}

# Solo para -SelfTest: espera una búsqueda (como mucho 30 s).
function Wait-Lookup($key, [string]$call, [object[]]$arguments) {
  $deadline = [DateTime]::UtcNow.AddSeconds(30)
  do {
    Update-Lookups
    $lookup = Get-Lookup $key $call $arguments
    if (-not $lookup.done) { Start-Sleep -Milliseconds 100 }
  } while (-not $lookup.done -and [DateTime]::UtcNow -lt $deadline)
  $lookup.value
}

# iTunes: no se anuncia en los SMTC, así que se lee por su interfaz COM (iTunes.Application).
# Solo si ya está abierto: crear el objeto COM abriría iTunes. Las llamadas COM se quedan
# esperando mientras iTunes muestra un diálogo; por eso corren en un runspace aparte, con
# tiempo límite, y la lectura principal nunca las espera (Update-ITunes). El objeto se suelta
# tras cada lectura: si quedara tomado, al cerrar iTunes avisaría de que otro programa usa su
# interfaz de scripts.
$itunesApp = 'iTunes.exe'
$itunesCode = {
  # Lee iTunes (o un objeto con la misma forma, en la prueba) tras atender los comandos.
  # $lastKey: la canción cuya carátula ya se devolvió; si cambió, se devuelve la nueva
  # (artKey y art: data: o '' si no tiene).
  function Read-ITunesState($it, $commands, $lastKey, $artDir) {
    foreach ($cmd in @($commands)) {
      try {
        switch ("$($cmd.cmd)") {
          'previous' { $it.PreviousTrack() }
          'playpause' { $it.PlayPause() }
          'next' { $it.NextTrack() }
          # PlayerPosition va en segundos.
          'seek' { $it.PlayerPosition = [int][Math]::Floor([int64]$cmd.positionMs / 1000) }
          'shuffle' {
            $playlist = $it.CurrentPlaylist
            if ($playlist) { $playlist.Shuffle = -not [bool]$playlist.Shuffle }
          }
        }
      } catch {}
    }
    $state = [ordered]@{ ok = $true; title = ''; artist = ''; album = ''; isPlaying = $false; positionMs = 0; durationMs = 0; key = '' }
    $track = $it.CurrentTrack
    if ($track) {
      $state.title = "$($track.Name)"
      $state.artist = "$($track.Artist)"
      $state.album = "$($track.Album)"
      $state.durationMs = [int64]$track.Duration * 1000
    } elseif ($it.CurrentStreamTitle) {
      # Una radio por internet: solo trae el título de lo que suena.
      $state.title = "$($it.CurrentStreamTitle)"
    }
    # PlayerState: 0 detenido, 1 sonando, 2 y 3 adelantando o retrocediendo.
    $state.isPlaying = [int]$it.PlayerState -eq 1
    $ms = $null
    try { $ms = $it.PlayerPositionMS } catch {}
    if ($null -eq $ms) { $ms = [int64]$it.PlayerPosition * 1000 }
    $state.positionMs = [int64]$ms
    try {
      $playlist = $it.CurrentPlaylist
      if ($playlist) { $state.shuffle = [bool]$playlist.Shuffle }
    } catch {}
    $state.key = "$($state.title)`n$($state.artist)"
    if ($state.title -and $state.key -ne $lastKey) {
      $state.artKey = $state.key
      $state.art = ''
      try {
        $artwork = if ($track) { $track.Artwork } else { $null }
        if ($artwork -and $artwork.Count -gt 0) {
          $item = $artwork.Item(1)
          # Format: 1 JPEG, 2 PNG, 3 BMP.
          $type = switch ([int]$item.Format) { 2 { 'png' } 3 { 'bmp' } default { 'jpeg' } }
          $file = Join-Path $artDir "obs-now-playing-itunes.$type"
          $item.SaveToFile($file)
          $bytes = [System.IO.File]::ReadAllBytes($file)
          [System.IO.File]::Delete($file)
          if ($bytes.Length -gt 0) { $state.art = "data:image/$type;base64," + [Convert]::ToBase64String($bytes) }
        }
      } catch {}
    }
    $state
  }
}
. $itunesCode
$itunesCall = @'
$it = $null
try {
  if ([System.Diagnostics.Process]::GetProcessesByName('iTunes').Count -eq 0) { return @{ ok = $false } }
  $it = New-Object -ComObject iTunes.Application
  Read-ITunesState $it $args[0] $args[1] $args[2]
} catch {
  @{ ok = $false }
} finally {
  if ($it) { try { [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($it) } catch {} }
  [GC]::Collect()
  [GC]::WaitForPendingFinalizers()
}
'@
$itunes = @{
  job = $null; runspace = $null; state = $null; readAt = [DateTime]::UtcNow
  lastStart = [DateTime]::MinValue; retryAt = [DateTime]::MinValue
  commands = (New-Object System.Collections.ArrayList); artKey = ''; art = $null
}

# Recoge la lectura de iTunes que terminó y lanza la siguiente (una por segundo, o enseguida
# si hay comandos). No espera nunca. Si iTunes no responde en 5 s (un diálogo abierto), se
# deja esa lectura y se vuelve a probar a los 5 s con otro runspace.
function Update-ITunes {
  $now = [DateTime]::UtcNow
  $job = $itunes.job
  if ($job) {
    if ($job.handle.IsCompleted) {
      $result = $null
      try {
        $out = $job.ps.EndInvoke($job.handle)
        if ($out.Count -gt 0) { $result = $out[0] }
      } catch {}
      $job.ps.Dispose()
      $itunes.job = $null
      if ($result -and $result['ok']) {
        $itunes.state = $result
        $itunes.readAt = $job.started
        if ($null -ne $result['artKey']) {
          $itunes.artKey = $result['artKey']
          $itunes.art = if ($result['art']) { New-ArtFromUrl $result['art'] } else { $null }
        }
      } else {
        $itunes.state = $null
      }
    } elseif (($now - $job.started).TotalSeconds -gt 5) {
      try { [void]$job.ps.BeginStop($null, $null) } catch {}
      $itunes.job = $null
      $itunes.runspace = $null
      $itunes.state = $null
      $itunes.retryAt = $now.AddSeconds(5)
      return
    } else {
      return
    }
  }
  if ($now -lt $itunes.retryAt) { return }
  if ($itunes.commands.Count -eq 0 -and ($now - $itunes.lastStart).TotalMilliseconds -lt 900) { return }
  # Se mira si iTunes está abierto una vez por segundo como mucho (recorre los procesos).
  $itunes.lastStart = $now
  if ([System.Diagnostics.Process]::GetProcessesByName('iTunes').Count -eq 0) {
    $itunes.state = $null
    $itunes.commands.Clear()
    return
  }
  if (-not $itunes.runspace) {
    $runspace = [runspacefactory]::CreateRunspace()
    $runspace.ApartmentState = 'STA'
    $runspace.Open()
    $itunes.runspace = $runspace
  }
  $commands = @($itunes.commands.ToArray())
  $itunes.commands.Clear()
  $ps = [PowerShell]::Create()
  $ps.Runspace = $itunes.runspace
  [void]$ps.AddScript($itunesCode.ToString() + "`n" + $itunesCall)
  [void]$ps.AddArgument($commands)
  [void]$ps.AddArgument($itunes.artKey)
  [void]$ps.AddArgument([System.IO.Path]::GetTempPath())
  $itunes.job = @{ ps = $ps; handle = $ps.BeginInvoke(); started = $now }
}

# AIMP: no se anuncia en los SMTC (solo con un complemento). Se lee por su interfaz remota
# (apiRemote.h del SDK de AIMP): la canción, en el archivo compartido "AIMP2_RemoteInfo"; el
# estado, la posición y el aleatorio, y los comandos, con mensajes a su ventana
# "AIMP2_RemoteInfo" (SendMessageTimeout: si AIMP no responde, no se espera más de 1 s); la
# carátula, que AIMP manda con WM_COPYDATA a una ventana nuestra. Solo si AIMP está abierto;
# la parte en C# se compila la primera vez que se lo ve.
$aimpApp = 'AIMP.exe'
$aimpSource = @'
using System;
using System.Runtime.InteropServices;
using System.Windows.Forms;

public class ObsNowPlayingAimp : NativeWindow {
  [DllImport("user32.dll", CharSet = CharSet.Unicode)]
  static extern IntPtr FindWindow(string className, string windowName);
  [DllImport("user32.dll")]
  static extern IntPtr SendMessageTimeout(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam, uint flags, uint timeout, out IntPtr result);

  [StructLayout(LayoutKind.Sequential)]
  struct CopyData { public IntPtr dwData; public int cbData; public IntPtr lpData; }

  const uint WM_AIMP_COMMAND = 0x400 + 0x75;
  const uint WM_AIMP_PROPERTY = 0x400 + 0x77;
  const int WM_COPYDATA = 0x4A;
  const long ALBUMART_ID = 0x41495043;
  byte[] art;

  // Una ventana solo para mensajes (HWND_MESSAGE): ahí llega la carátula.
  public ObsNowPlayingAimp() {
    CreateParams cp = new CreateParams();
    cp.Parent = new IntPtr(-3);
    CreateHandle(cp);
  }

  public static IntPtr Window() { return FindWindow("AIMP2_RemoteInfo", null); }

  // -1 si AIMP no respondió (SMTO_ABORTIFHUNG, como mucho 1 s).
  static long Send(IntPtr hWnd, uint msg, long wParam, long lParam) {
    IntPtr result;
    if (SendMessageTimeout(hWnd, msg, new IntPtr(wParam), new IntPtr(lParam), 2, 1000, out result) == IntPtr.Zero) return -1;
    return result.ToInt64();
  }
  public static long Get(IntPtr hWnd, int property) { return Send(hWnd, WM_AIMP_PROPERTY, property, 0); }
  public static long Set(IntPtr hWnd, int property, long value) { return Send(hWnd, WM_AIMP_PROPERTY, property | 1, value); }
  public static long Command(IntPtr hWnd, int command) { return Send(hWnd, WM_AIMP_COMMAND, command, 0); }

  // AIMP_RA_CMD_GET_ALBUMART: AIMP manda la imagen a nuestra ventana mientras esperamos.
  public byte[] AlbumArt(IntPtr hWnd) {
    art = null;
    Send(hWnd, WM_AIMP_COMMAND, 29, Handle.ToInt64());
    return art;
  }

  protected override void WndProc(ref Message m) {
    if (m.Msg == WM_COPYDATA) {
      CopyData data = (CopyData)Marshal.PtrToStructure(m.LParam, typeof(CopyData));
      if (data.dwData.ToInt64() == ALBUMART_ID && data.cbData > 0 && data.lpData != IntPtr.Zero) {
        art = new byte[data.cbData];
        Marshal.Copy(data.lpData, art, 0, data.cbData);
      }
      m.Result = new IntPtr(1);
      return;
    }
    base.WndProc(ref m);
  }
}
'@
$aimp = @{ ready = $false; failed = $false; remote = $null; checkedAt = [DateTime]::MinValue; running = $false; artKey = ''; art = $null }

# La canción del archivo compartido de AIMP (TAIMPRemoteFileInfo, empaquetado): DWORD
# Deprecated1, BOOL Active, DWORD BitRate, Channels, Duration, INT64 FileSize, DWORD FileMark,
# SampleRate, TrackNumber, AlbumLength, ArtistLength, DateLength, FileNameLength, GenreLength,
# TitleLength, DWORD Deprecated2[6] (88 bytes); detrás, los textos en UTF-16 en ese orden.
function Read-AimpFileInfo([byte[]]$bytes) {
  if (-not $bytes -or $bytes.Length -lt 88) { return $null }
  $lengths = @(40, 44, 48, 52, 56, 60 | ForEach-Object { [int64][BitConverter]::ToUInt32($bytes, $_) })
  # Deprecated1 es, en la práctica, dónde empiezan los textos (el tamaño de la cabecera).
  $start = [int64][BitConverter]::ToUInt32($bytes, 0)
  if ($start -lt 88 -or $start -gt 256) { $start = 88 }
  $total = 0
  foreach ($length in $lengths) {
    if ($length -lt 0 -or $length -gt 1024) { return $null }
    $total += $length
  }
  if ($start + 2 * $total -gt $bytes.Length) { return $null }
  $texts = @()
  $offset = $start
  foreach ($length in $lengths) {
    $texts += [Text.Encoding]::Unicode.GetString($bytes, [int]$offset, [int](2 * $length)).TrimEnd([char]0)
    $offset += 2 * $length
  }
  $title = $texts[5]
  # Sin etiquetas, el nombre del archivo (o de la radio) hace de título.
  if (-not $title -and $texts[3]) { $title = [IO.Path]::GetFileNameWithoutExtension(($texts[3] -split '[?#]')[0]) }
  [ordered]@{
    active     = [BitConverter]::ToInt32($bytes, 4) -ne 0
    durationMs = [int64][BitConverter]::ToUInt32($bytes, 16)
    album      = $texts[0]
    artist     = $texts[1]
    fileName   = $texts[3]
    title      = $title
  }
}

# Un archivo de imagen (bytes) como data: (su tipo, por la firma).
function New-ArtFromBytes([byte[]]$bytes) {
  if (-not $bytes -or $bytes.Length -lt 8) { return $null }
  $mime = if ($bytes[0] -eq 0x89 -and $bytes[1] -eq 0x50) { 'image/png' }
    elseif ($bytes[0] -eq 0x47 -and $bytes[1] -eq 0x49) { 'image/gif' }
    elseif ($bytes[0] -eq 0x42 -and $bytes[1] -eq 0x4D) { 'image/bmp' }
    elseif ($bytes[0] -eq 0x52 -and $bytes[1] -eq 0x49) { 'image/webp' }
    else { 'image/jpeg' }
  @{ url = "data:$mime;base64," + [Convert]::ToBase64String($bytes); hash = [BitConverter]::ToString($md5.ComputeHash($bytes)) }
}

# ¿Está abierto AIMP? (se mira una vez por segundo como mucho: recorre los procesos). La
# primera vez, compila la parte en C#; si no se puede, AIMP se deja de lado.
function Test-Aimp {
  $now = [DateTime]::UtcNow
  if (($now - $aimp.checkedAt).TotalMilliseconds -ge 900) {
    $aimp.checkedAt = $now
    $aimp.running = [System.Diagnostics.Process]::GetProcessesByName('AIMP').Count -gt 0
  }
  if (-not $aimp.running -or $aimp.failed) { return $false }
  if (-not $aimp.ready) {
    try {
      Add-Type -TypeDefinition $aimpSource -ReferencedAssemblies System.Windows.Forms
      $aimp.remote = New-Object ObsNowPlayingAimp
      $aimp.ready = $true
    } catch {
      $aimp.failed = $true
      Emit @{ error = "No se pudo preparar la lectura de AIMP: $($_.Exception.Message)" }
      return $false
    }
  }
  $true
}

# La sesión de AIMP (o nada si no está abierto o no tiene canción).
function Read-Aimp {
  if (-not (Test-Aimp)) { return $null }
  $window = [ObsNowPlayingAimp]::Window()
  if ($window -eq [IntPtr]::Zero) { return $null }
  $bytes = $null
  try {
    $map = [System.IO.MemoryMappedFiles.MemoryMappedFile]::OpenExisting('AIMP2_RemoteInfo', [System.IO.MemoryMappedFiles.MemoryMappedFileRights]::Read)
    try {
      $view = $map.CreateViewStream(0, 0, [System.IO.MemoryMappedFiles.MemoryMappedFileAccess]::Read)
      try {
        $bytes = New-Object byte[] ([Math]::Min(2048, [int]$view.Length))
        [void]$view.Read($bytes, 0, $bytes.Length)
      } finally { $view.Dispose() }
    } finally { $map.Dispose() }
  } catch { return $null }
  $info = Read-AimpFileInfo $bytes
  if (-not $info -or -not $info.title) { return $null }
  # PLAYER_STATE (0x40): 0 detenido, 1 en pausa, 2 sonando.
  $state = [ObsNowPlayingAimp]::Get($window, 0x40)
  if ($state -lt 0) { return $null }
  $position = [ObsNowPlayingAimp]::Get($window, 0x20)
  $duration = [ObsNowPlayingAimp]::Get($window, 0x30)
  if ($duration -le 0) { $duration = $info.durationMs }
  $shuffle = [ObsNowPlayingAimp]::Get($window, 0x80)
  $entry = [ordered]@{
    app        = $aimpApp
    title      = $info.title
    artist     = $info.artist
    album      = $info.album
    isPlaying  = ($state -eq 2)
    current    = $false
    progressMs = [int64][Math]::Max(0, $position)
    durationMs = [int64][Math]::Max(0, $duration)
  }
  if ($shuffle -ge 0) { $entry.shuffle = $shuffle -ne 0 }
  # La carátula, una vez por canción (la pide Complete-Entry por $getArt).
  $key = "$($info.title)`n$($info.artist)`n$($info.fileName)"
  if ($aimp.artKey -ne $key) {
    $aimp.artKey = $key
    $aimp.art = $null
    try { $aimp.art = New-ArtFromBytes ($aimp.remote.AlbumArt($window)) } catch {}
  }
  $entry
}

# Los comandos del dock para AIMP (AIMP_RA_CMD_* y las propiedades que se pueden cambiar).
function Send-AimpCommand($msg) {
  if (-not (Test-Aimp)) { return }
  $window = [ObsNowPlayingAimp]::Window()
  if ($window -eq [IntPtr]::Zero) { return }
  switch ("$($msg.cmd)") {
    'previous' { [void][ObsNowPlayingAimp]::Command($window, 18) }
    'playpause' { [void][ObsNowPlayingAimp]::Command($window, 14) }
    'next' { [void][ObsNowPlayingAimp]::Command($window, 17) }
    'seek' { [void][ObsNowPlayingAimp]::Set($window, 0x20, [int64]$msg.positionMs) }
    'shuffle' {
      $now = [ObsNowPlayingAimp]::Get($window, 0x80)
      if ($now -ge 0) { [void][ObsNowPlayingAimp]::Set($window, 0x80, [int64]($now -eq 0)) }
    }
  }
}

function Test-VideoWanted {
  $VideoFlag -and [System.IO.File]::Exists($VideoFlag)
}

# Prueba (CI): las búsquedas de carátula y de video (la carátula y la miniatura, por el
# runspace de las búsquedas, como en la lectura) y la lectura de la barra de direcciones
# funcionan en Windows PowerShell 5.1 de verdad.
if ($SelfTest) {
  $art = Wait-Lookup "art`nThe Weeknd`nBlinding Lights" $catalogCall @('Blinding Lights', 'The Weeknd')
  $id = ''
  try { $id = Search-YouTubeVideoId 'Rick Astley - Never Gonna Give You Up (Official Video) (4K Remaster)' 'Rick Astley' } catch { $id = "error: $($_.Exception.Message)" }
  $thumb = if ($id -match '^[A-Za-z0-9_-]{11}$') { Wait-Lookup "thumb`n$id" $thumbnailCall @($id) } else { '' }
  $windows = Read-YouTubeWindows
  # YouTube Music o YouTube según los nombres de las pestañas (casos fijos) y las pestañas de
  # verdad que ve el lector (de un Chrome abierto para la prueba).
  $cases = @(
    @{ names = @('youtube music', 'gmail'); title = 'After Dark'; album = ''; want = 'ytmusic' },
    @{ names = @('after dark - youtube music', 'lofi mix - youtube'); title = 'After Dark'; album = ''; want = 'ytmusic' },
    @{ names = @('youtube music', '(3) lofi mix - youtube - reproduciendo audio'); title = 'Otra'; album = ''; want = 'youtube' },
    @{ names = @('youtube music - audio playing', 'lofi mix - youtube'); title = 'Otra'; album = ''; want = 'ytmusic' },
    @{ names = @('youtube music', 'lofi mix - youtube'); title = 'Otra'; album = 'Un disco'; want = 'ytmusic' },
    @{ names = @('rick astley - never gonna give you up - youtube'); title = 'Never Gonna Give You Up'; album = ''; want = 'youtube' },
    @{ names = @('spotify - web player', 'gmail'); title = 'After Dark'; album = 'X'; want = '' },
    # YouTube y YouTube Music a la vez: un título corto que otra pestaña de YouTube también trae.
    @{ names = @('youtube music - reproduciendo audio', 'stay with me (official video) - youtube'); title = 'Stay'; album = ''; want = 'ytmusic' },
    @{ names = @('youtube music', 'stay with me (official video) - youtube - audio playing'); title = 'Stay'; album = ''; want = 'youtube' },
    @{ names = @('youtube music', 'stay with me (official video) - youtube'); title = 'Stay'; album = 'Stay (Single)'; want = 'ytmusic' },
    @{ names = @('stay - youtube music', 'stay with me (official video) - youtube'); title = 'Stay'; album = ''; want = '' },
    # "Official Audio" en el título no es la que suena.
    @{ names = @('youtube music - reproduciendo audio', 'after dark (official audio) - youtube'); title = 'Otra'; album = ''; want = 'ytmusic' },
    # Silenciada no cuenta como la que suena.
    @{ names = @('youtube music', 'lofi mix - youtube - audio muted'); title = 'Otra'; album = 'Un disco'; want = 'ytmusic' }
  )
  $siteChecks = @($cases | ForEach-Object {
    $got = Select-BrowserSite $_.names $_.title $_.album
    [ordered]@{ want = $_.want; got = $got; ok = ($got -eq $_.want) }
  })
  $tabs = @()
  try { $tabs = @(Read-BrowserTabs) } catch { $tabs = @("error: $($_.Exception.Message)") }
  # iTunes: la lectura y los comandos con un objeto de la misma forma que iTunes.Application
  # (en el runner no hay iTunes).
  $itunesLog = New-Object System.Collections.ArrayList
  $artItem = [pscustomobject]@{ Format = 2 }
  $artItem | Add-Member ScriptMethod SaveToFile { param($path) [System.IO.File]::WriteAllBytes($path, [byte[]](137, 80, 78, 71, 13, 10, 26, 10)) }
  $artwork = [pscustomobject]@{ Count = 1; First = $artItem }
  $artwork | Add-Member ScriptMethod Item { param($index) $this.First }
  $playlist = [pscustomobject]@{ Shuffle = $false }
  $fake = [pscustomobject]@{
    CurrentTrack = [pscustomobject]@{ Name = 'Vice Lights'; Artist = 'The Leonida Keys'; Album = 'Leonida'; Duration = 200; Artwork = $artwork }
    CurrentStreamTitle = ''; PlayerState = 1; PlayerPositionMS = 61500; PlayerPosition = 61; CurrentPlaylist = $playlist; Log = $itunesLog
  }
  foreach ($method in 'PlayPause', 'NextTrack', 'PreviousTrack') {
    $fake | Add-Member ScriptMethod $method ([scriptblock]::Create("[void]`$this.Log.Add('$method')"))
  }
  $commands = @(('{"cmd":"next","app":"iTunes.exe"}' | ConvertFrom-Json), ('{"cmd":"shuffle","app":"iTunes.exe"}' | ConvertFrom-Json),
    ('{"cmd":"seek","app":"iTunes.exe","positionMs":30500}' | ConvertFrom-Json), ('{"cmd":"playpause","app":"iTunes.exe"}' | ConvertFrom-Json))
  $first = Read-ITunesState $fake $commands '' ([System.IO.Path]::GetTempPath())
  $again = Read-ITunesState $fake @() $first.key ([System.IO.Path]::GetTempPath())
  $fake.CurrentTrack = $null
  $fake.CurrentStreamTitle = 'Radio X - Live'
  $fake.PlayerState = 0
  $stream = Read-ITunesState $fake @() $first.key ([System.IO.Path]::GetTempPath())
  # AIMP: el archivo compartido (armado como lo escribe AIMP), la carátula y la parte en C#
  # (compila y crea su ventana; sin AIMP abierto no encuentra la suya).
  $aimpBytes = New-Object byte[] 2048
  $put = { param($offset, $value) [Array]::Copy([BitConverter]::GetBytes([uint32]$value), 0, $aimpBytes, $offset, 4) }
  & $put 0 88; & $put 4 1; & $put 16 215000
  $aimpTexts = @('Leonida', 'The Leonida Keys', '2026', 'C:\Music\vice.mp3', 'Synthwave', 'Vice Lights')
  $offset = 88
  for ($k = 0; $k -lt 6; $k++) {
    & $put (40 + 4 * $k) $aimpTexts[$k].Length
    $raw = [Text.Encoding]::Unicode.GetBytes($aimpTexts[$k])
    [Array]::Copy($raw, 0, $aimpBytes, $offset, $raw.Length)
    $offset += $raw.Length
  }
  $aimpInfo = Read-AimpFileInfo $aimpBytes
  & $put 60 0
  $aimpNoTags = Read-AimpFileInfo $aimpBytes
  & $put 40 5000
  $aimpBroken = Read-AimpFileInfo $aimpBytes
  $aimpCompiled = $false
  try {
    Add-Type -TypeDefinition $aimpSource -ReferencedAssemblies System.Windows.Forms
    $remote = New-Object ObsNowPlayingAimp
    $aimpCompiled = ($remote.Handle -ne [IntPtr]::Zero) -and ([ObsNowPlayingAimp]::Window() -eq [IntPtr]::Zero)
  } catch { $aimpCompiled = "error: $($_.Exception.Message)" }
  $aimpChecks = @(
    @{ what = 'textos'; ok = ($aimpInfo.title -eq 'Vice Lights' -and $aimpInfo.artist -eq 'The Leonida Keys' -and $aimpInfo.album -eq 'Leonida' -and $aimpInfo.fileName -eq 'C:\Music\vice.mp3') },
    @{ what = 'activo y duración'; ok = ($aimpInfo.active -and $aimpInfo.durationMs -eq 215000) },
    @{ what = 'sin etiquetas, el nombre del archivo'; ok = ($aimpNoTags.title -eq 'vice') },
    @{ what = 'datos rotos'; ok = ($null -eq $aimpBroken) },
    @{ what = 'carátula'; ok = ((New-ArtFromBytes ([byte[]](0x89, 0x50, 0x4E, 0x47, 13, 10, 26, 10, 0))).url -like 'data:image/png;base64,*') },
    @{ what = 'parte en C#'; ok = ($aimpCompiled -eq $true) }
  ) | ForEach-Object { [ordered]@{ what = $_.what; ok = [bool]$_.ok } }
  $itunesChecks = @(
    @{ what = 'título'; ok = ($first.title -eq 'Vice Lights' -and $first.artist -eq 'The Leonida Keys' -and $first.album -eq 'Leonida') },
    @{ what = 'sonando y tiempos'; ok = ($first.isPlaying -and $first.positionMs -eq 61500 -and $first.durationMs -eq 200000) },
    @{ what = 'comandos'; ok = (($itunesLog -join ',') -eq 'NextTrack,PlayPause' -and $fake.PlayerPosition -eq 30 -and $playlist.Shuffle -eq $true -and $first.shuffle -eq $true) },
    @{ what = 'carátula'; ok = ("$($first.art)" -like 'data:image/png;base64,*' -and $first.artKey -eq $first.key) },
    @{ what = 'carátula solo al cambiar'; ok = ($null -eq $again.artKey) },
    @{ what = 'radio por internet'; ok = ($stream.title -eq 'Radio X - Live' -and -not $stream.isPlaying -and $stream.art -eq '') }
  ) | ForEach-Object { [ordered]@{ what = $_.what; ok = [bool]$_.ok } }
  Emit ([ordered]@{
    catalogArtwork = [bool]$art
    searchVideoId  = $id
    thumbnail      = [bool]$thumb
    youtubeWindows = @($windows | ForEach-Object { [ordered]@{ name = $_.name; id = $_.id; site = $_.site } })
    siteChecks     = $siteChecks
    itunesChecks   = @($itunesChecks)
    aimpChecks     = @($aimpChecks)
    aimpCompiled   = "$aimpCompiled"
    browserTabs    = $tabs
  })
  exit 0
}

try {
  $manager = Await ([Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager]::RequestAsync()) ([Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager])
} catch {
  Emit @{ error = "No se pudo abrir los controles multimedia de Windows: $($_.Exception.Message)" }
  exit 3
}

# Los comandos que llegaron, en orden: los de -CommandDir (por nombre) y el de -CommandFile.
# Cada archivo se borra antes de atenderlo; si no se puede borrar (lo están escribiendo), se
# deja para la próxima vuelta, así no se repite.
function Read-Commands {
  $lines = New-Object System.Collections.ArrayList
  if ($CommandDir -and [System.IO.Directory]::Exists($CommandDir)) {
    $files = @([System.IO.Directory]::GetFiles($CommandDir, '*.json') |
      Where-Object { [System.IO.Path]::GetFileName($_) -match '^\d+\.json$' } |
      Sort-Object { [System.IO.Path]::GetFileName($_) })
    foreach ($file in $files) {
      try {
        $text = [System.IO.File]::ReadAllText($file)
        [System.IO.File]::Delete($file)
        if ($text) { [void]$lines.Add($text) }
      } catch {}
    }
  }
  if ($CommandFile -and [System.IO.File]::Exists($CommandFile)) {
    try {
      $text = [System.IO.File]::ReadAllText($CommandFile)
      [System.IO.File]::Delete($CommandFile)
      if ($text) { [void]$lines.Add($text) }
    } catch {}
  }
  , $lines
}

# Carátula (solo cuando cambia la canción) y, en modo video, el video de YouTube de una
# sesión. $getArt da la carátula del propio reproductor (o nada).
function Complete-Entry($entry, [scriptblock]$getArt) {
  $app = $entry.app
  $key = "$($entry.title)`n$($entry.artist)"
  if ($sentKey[$app] -ne $key) {
    $tries[$app] = 1 + [int]$tries[$app]
    $art = $null
    $waiting = $false
    try { $art = & $getArt } catch { $art = $null }
    # Sin carátula del reproductor (tras unos segundos, por si llega tarde): la del
    # catálogo de Apple o, en el navegador, la miniatura del video de YouTube. Mientras
    # se busca, la línea sale sin carátula y no se da por perdida.
    if (-not $art -and $tries[$app] -ge 3) {
      $found = Get-NetworkArtwork $app $entry.title $entry.artist $entry.album
      $art = $found.art
      $waiting = [bool]$found.waiting
    }
    $fresh = $art -and $art.hash -ne $sentHash[$app]
    if ($fresh -or ($tries[$app] -ge 5 -and -not $waiting)) {
      $entry.artwork = if ($art) { $art.url } else { '' }
      $sentKey[$app] = $key
      $sentHash[$app] = if ($art) { $art.hash } else { '' }
      $tries[$app] = 0
    }
  }
  if (Test-VideoWanted) {
    # En un navegador: ¿YouTube Music o YouTube? (de los nombres de sus pestañas)
    $site = ''
    if (Test-BrowserApp $app) {
      $site = Get-BrowserSite $app $entry.title $entry.album
      if ($site) { $entry.site = $site }
    }
    $video = Get-Video $app $entry.title $entry.artist $entry.album $site
    if ($video -and $video.id) {
      $entry.videoId = $video.id
      # Si las pestañas no lo dijeron, el sitio que vio la búsqueda del video (la ventana
      # visible puede ser otra pestaña de YouTube, por eso no manda sobre las pestañas).
      if (-not $entry.site -and $video.site) { $entry.site = $video.site }
    }
  }
}

# Por app: qué canción ya tiene carátula enviada, el hash de esa carátula y cuántos
# intentos van. Al cambiar de canción, algunas apps (Spotify) actualizan el título un
# instante antes que la carátula: si llega la misma imagen de antes, reintentamos.
$sentKey = @{}
$sentHash = @{}
$tries = @{}
$failures = 0

# ~30 min y salimos: server.js nos vuelve a abrir (evita acumular memoria).
for ($i = 0; $i -lt 1800; $i++) {
  try {
    Update-Lookups
    $current = $manager.GetCurrentSession()
    $currentApp = if ($current) { $current.SourceAppUserModelId } else { '' }
    $sessions = New-Object System.Collections.ArrayList

    foreach ($session in $manager.GetSessions()) {
      $app = $session.SourceAppUserModelId
      $props = Await ($session.TryGetMediaPropertiesAsync()) $mediaPropsType
      if (-not $props -or [string]::IsNullOrWhiteSpace($props.Title)) { continue }

      $playback = $session.GetPlaybackInfo()
      $timeline = $session.GetTimelineProperties()
      $playing = "$($playback.PlaybackStatus)" -eq 'Playing'

      # La posición vale para LastUpdatedTime: la llevamos a "ahora" si está sonando.
      $position = $timeline.Position.TotalMilliseconds
      if ($playing -and $timeline.LastUpdatedTime.Year -gt 2000) {
        $rate = if ($playback.PlaybackRate) { [double]$playback.PlaybackRate } else { 1.0 }
        $position += ([DateTimeOffset]::Now - $timeline.LastUpdatedTime).TotalMilliseconds * $rate
      }
      $duration = ($timeline.EndTime - $timeline.StartTime).TotalMilliseconds

      $entry = [ordered]@{
        app        = $app
        title      = $props.Title
        artist     = $props.Artist
        album      = $props.AlbumTitle
        isPlaying  = $playing
        current    = ($app -eq $currentApp)
        progressMs = [int64][Math]::Max(0, $position)
        durationMs = [int64][Math]::Max(0, $duration)
      }
      # Así el plugin lo reconoce como navegador (y no como "308046B0AF4A39CB").
      if (Test-Firefox $app) { $entry.name = 'Firefox' }
      # Aleatorio: solo si el reproductor deja cambiarlo (si no, el dock apaga el botón).
      if ($playback.Controls -and $playback.Controls.IsShuffleEnabled -and $null -ne $playback.IsShuffleActive) {
        $entry.shuffle = [bool]$playback.IsShuffleActive
      }

      $thumbnail = $props.Thumbnail
      Complete-Entry $entry { if ($thumbnail) { Read-Thumbnail $thumbnail } }
      [void]$sessions.Add($entry)
    }

    # iTunes (por COM), si no vino ya en los SMTC (con cualquier id de app). La posición es la de la última lectura,
    # llevada a "ahora" si está sonando.
    Update-ITunes
    $st = $itunes.state
    if ($st -and $st['title'] -and -not @($sessions | Where-Object { "$($_.app)" -match '(?i)itunes' })) {
      $position = [double]$st['positionMs']
      if ($st['isPlaying']) { $position += ([DateTime]::UtcNow - $itunes.readAt).TotalMilliseconds }
      $duration = [double]$st['durationMs']
      if ($duration -gt 0) { $position = [Math]::Min($position, $duration) }
      $entry = [ordered]@{
        app        = $itunesApp
        title      = $st['title']
        artist     = $st['artist']
        album      = $st['album']
        isPlaying  = [bool]$st['isPlaying']
        current    = $false
        progressMs = [int64][Math]::Max(0, $position)
        durationMs = [int64][Math]::Max(0, $duration)
      }
      if ($null -ne $st['shuffle']) { $entry.shuffle = [bool]$st['shuffle'] }
      $stKey = $st['key']
      Complete-Entry $entry { if ($itunes.artKey -eq $stKey) { $itunes.art } }
      [void]$sessions.Add($entry)
    }

    # AIMP (por su interfaz remota), si no vino ya en los SMTC (con un complemento).
    if (-not @($sessions | Where-Object { "$($_.app)" -match '(?i)aimp' })) {
      $aimpEntry = $null
      try { $aimpEntry = Read-Aimp } catch { $aimpEntry = $null }
      if ($aimpEntry -and $aimpEntry.title) {
        Complete-Entry $aimpEntry { $aimp.art }
        [void]$sessions.Add($aimpEntry)
      }
    }

    Emit ([ordered]@{ sessions = $sessions })
    $failures = 0
  } catch {
    Emit @{ error = $_.Exception.Message }
    $failures++
    if ($failures -ge 5) { exit 4 }
  }
  # Espera ~1 s, pero atiende al instante los comandos que lleguen.
  for ($wait = 0; $wait -lt 10; $wait++) {
    Update-Lookups
    Update-ITunes
    $commands = Read-Commands
    if ($commands.Count -gt 0) {
      foreach ($line in $commands) {
        try { Send-MediaCommand $line } catch { Emit @{ error = "No se pudo controlar el reproductor: $($_.Exception.Message)" } }
      }
      Start-Sleep -Milliseconds 300 # que la app alcance a cambiar de canción antes de leerla
      break
    }
    Start-Sleep -Milliseconds 100
  }
}
exit 0
