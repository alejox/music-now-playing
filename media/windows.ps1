# Lee "lo que está sonando" en Windows: los System Media Transport Controls (SMTC),
# la misma información que muestra el control de volumen. Escribe una línea JSON
# por segundo en la salida estándar; server.js la lee. Los comandos llegan en el
# archivo -CommandFile (server.js lo escribe y aquí se borra al leerlo):
#
#   {"cmd":"next","app":"Spotify.exe"}      cmd: previous | playpause | next
#
# Es un archivo y no la entrada estándar a propósito: en Windows PowerShell 5.1 leer
# la entrada sin bloquear el bucle no es confiable, y un lector bloqueado congela el overlay.
#
# Carátula: la que entrega el reproductor. Si no trae (navegadores, algunas apps), se busca
# por artista y canción en el catálogo público de Apple (iTunes Search, igual que en macOS)
# y, para YouTube en el navegador, se usa la miniatura del video.
#
# Modo video (plugin de OBS): mientras exista el archivo -VideoFlag, para lo que suena en
# un navegador se agrega "videoId", el id del video de YouTube / YouTube Music leído de la
# barra de direcciones (UI Automation) de la ventana cuya pestaña visible es YouTube. Si la
# barra no se puede leer, se busca el video por su título en YouTube. Con él va "site":
# "ytmusic" (music.youtube.com) o "youtube", para decir "Escuchando en …".
#
#   {"sessions":[{"app":"Spotify.exe","title":"…","artist":"…","album":"…",
#                 "isPlaying":true,"current":true,"progressMs":1234,"durationMs":5678,
#                 "artwork":"data:image/png;base64,…"}]}      ← artwork solo cuando cambia
#   {"error":"…"}
#
# Usa solo lo que trae Windows PowerShell 5.1 (Windows 10/11): sin módulos extra.

param([string]$CommandFile = '', [string]$VideoFlag = '', [switch]$SelfTest)

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
  if (-not $target) { $target = $manager.GetCurrentSession() }
  if (-not $target) { return }
  $operation = switch ($msg.cmd) {
    'previous' { $target.TrySkipPreviousAsync() }
    'playpause' { $target.TryTogglePlayPauseAsync() }
    'next' { $target.TrySkipNextAsync() }
  }
  if ($operation) { $null = Await $operation ([bool]) }
}

$md5 = [System.Security.Cryptography.MD5]::Create()

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

function Get-WebBytes($url) {
  $request = [System.Net.HttpWebRequest]::Create($url)
  $request.Timeout = 5000
  $request.ReadWriteTimeout = 5000
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

# "Canción (Official Video)" o "Artista - Topic" son cosas de YouTube, no del disco.
function Clean-Name($s) { (("$s" -replace '\s*[\(\[][^\)\]]*[\)\]]', '') -replace '\s+-\s+topic$', '').Trim() }
function Normalize-Name($s) {
  $plain = "$s".ToLowerInvariant().Normalize([Text.NormalizationForm]::FormD) -replace '\p{Mn}', ''
  $plain -replace '[^a-z0-9]', ''
}

# Carátula del catálogo público de Apple (iTunes Search, sin cuenta ni clave). Solo se
# acepta si artista y título coinciden, para no mostrar una carátula equivocada.
$lookups = @{} # "artista`ncanción" → carátula ($null = no se encontró), una búsqueda por canción
function Find-CatalogArtwork($title, $artist) {
  $key = "$artist`n$title"
  if ($lookups.ContainsKey($key)) { return $lookups[$key] }
  $lookups[$key] = $null
  $a = Normalize-Name (Clean-Name $artist)
  $t = Normalize-Name (Clean-Name $title)
  if (-not $a -or -not $t) { return $null }
  try {
    $term = [Uri]::EscapeDataString("$(Clean-Name $artist) $(Clean-Name $title)")
    $json = [Text.Encoding]::UTF8.GetString((Get-WebBytes "https://itunes.apple.com/search?media=music&entity=song&limit=10&term=$term")) | ConvertFrom-Json
    $hit = $json.results | Where-Object {
      $ra = Normalize-Name $_.artistName
      $rt = Normalize-Name $_.trackName
      $ra -and $rt -and ($ra.Contains($a) -or $a.Contains($ra)) -and ($rt.Contains($t) -or $t.Contains($rt))
    } | Select-Object -First 1
    if ($hit -and $hit.artworkUrl100) {
      $lookups[$key] = Get-WebImage ($hit.artworkUrl100 -replace '/\d+x\d+bb\.', '/600x600bb.')
    }
  } catch {}
  $lookups[$key]
}

# Miniatura de un video de YouTube (la grande si existe; si no, la mediana, sin franjas).
$thumbnails = @{}
function Find-VideoThumbnail($id) {
  if (-not $thumbnails.ContainsKey($id)) {
    $art = Get-WebImage "https://i.ytimg.com/vi/$id/maxresdefault.jpg"
    if (-not $art) { $art = Get-WebImage "https://i.ytimg.com/vi/$id/mqdefault.jpg" }
    $thumbnails[$id] = $art
  }
  $thumbnails[$id]
}

# El id del video de YouTube que suena en un navegador, leído de la barra de direcciones de
# sus ventanas (la de la pestaña visible). Una ventana sirve si su título (el de la pestaña)
# dice YouTube o trae el nombre de la canción.
$browserApps = 'chrome|msedge|brave|vivaldi|opera|firefox'
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

# Si la barra no se puede leer, se busca el video en YouTube por título y canal: el primer
# resultado con ese mismo título (o el primero, si ninguno coincide).
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

function Find-YouTubeVideo($title, $artist) {
  $windows = Read-YouTubeWindows
  $found = @($windows | Where-Object { $_.id })
  # Con varias ventanas de YouTube, la que tiene el título de la canción.
  $lower = "$title".ToLower()
  $hit = $found | Where-Object { $lower -and $_.name.Contains($lower) } | Select-Object -First 1
  if (-not $hit -and $found.Count -eq 1) { $hit = $found[0] }
  if ($hit) { return @{ id = $hit.id; site = $hit.site } }
  if ($windows.Count -gt 0) {
    $id = Search-YouTubeVideoId $title $artist
    $site = if (@($windows | Where-Object { $_.site -eq 'ytmusic' }).Count -gt 0) { 'ytmusic' } else { 'youtube' }
    if ($id) { return @{ id = $id; site = $site } }
  }
  $null
}

# @{ id; site } del video de YouTube que suena en el navegador, o $null.
function Get-Video($app, $title, $artist) {
  if ($app -notmatch $browserApps) { return $null }
  $key = "$app`n$title"
  if ($videoIds[$key]) { return $videoIds[$key] }
  if ([int]$videoTries[$key] -ge 3) { return $null } # la dirección cambia un instante después del título
  $videoTries[$key] = 1 + [int]$videoTries[$key]
  $video = $null
  try { $video = Find-YouTubeVideo $title $artist } catch { $video = $null }
  if ($video) { $videoIds[$key] = $video }
  $video
}

function Test-VideoWanted {
  $VideoFlag -and [System.IO.File]::Exists($VideoFlag)
}

# Prueba (CI): las búsquedas de carátula y de video y la lectura de la barra de direcciones
# funcionan en Windows PowerShell 5.1 de verdad.
if ($SelfTest) {
  $art = Find-CatalogArtwork 'Blinding Lights' 'The Weeknd'
  $id = ''
  try { $id = Search-YouTubeVideoId 'Rick Astley - Never Gonna Give You Up (Official Video) (4K Remaster)' 'Rick Astley' } catch { $id = "error: $($_.Exception.Message)" }
  $thumb = if ($id -match '^[A-Za-z0-9_-]{11}$') { Find-VideoThumbnail $id } else { $null }
  $windows = Read-YouTubeWindows
  Emit ([ordered]@{
    catalogArtwork = [bool]$art
    searchVideoId  = $id
    thumbnail      = [bool]$thumb
    youtubeWindows = @($windows | ForEach-Object { [ordered]@{ name = $_.name; id = $_.id; site = $_.site } })
  })
  exit 0
}

try {
  $manager = Await ([Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager]::RequestAsync()) ([Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager])
} catch {
  Emit @{ error = "No se pudo abrir los controles multimedia de Windows: $($_.Exception.Message)" }
  exit 3
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

      $key = "$($props.Title)`n$($props.Artist)"
      if ($sentKey[$app] -ne $key) {
        $tries[$app] = 1 + [int]$tries[$app]
        $art = $null
        if ($props.Thumbnail) {
          try { $art = Read-Thumbnail $props.Thumbnail } catch { $art = $null }
        }
        # Sin carátula del reproductor (tras unos segundos, por si llega tarde): la del
        # catálogo de Apple o, en el navegador, la miniatura del video de YouTube.
        if (-not $art -and $tries[$app] -ge 3) {
          $art = Find-CatalogArtwork $props.Title $props.Artist
          if (-not $art) {
            $video = Get-Video $app $props.Title $props.Artist
            if ($video) { $art = Find-VideoThumbnail $video.id }
          }
        }
        $fresh = $art -and $art.hash -ne $sentHash[$app]
        if ($fresh -or $tries[$app] -ge 5) {
          $entry.artwork = if ($art) { $art.url } else { '' }
          $sentKey[$app] = $key
          $sentHash[$app] = if ($art) { $art.hash } else { '' }
          $tries[$app] = 0
        }
      }
      if (Test-VideoWanted) {
        $video = Get-Video $app $props.Title $props.Artist
        if ($video) {
          $entry.videoId = $video.id
          $entry.site = $video.site
        }
      }
      [void]$sessions.Add($entry)
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
    if ($CommandFile -and [System.IO.File]::Exists($CommandFile)) {
      $line = ''
      try {
        $line = [System.IO.File]::ReadAllText($CommandFile)
        [System.IO.File]::Delete($CommandFile)
      } catch {}
      if ($line) {
        try { Send-MediaCommand $line } catch { Emit @{ error = "No se pudo controlar el reproductor: $($_.Exception.Message)" } }
        Start-Sleep -Milliseconds 300 # que la app alcance a cambiar de canción antes de leerla
        break
      }
    }
    Start-Sleep -Milliseconds 100
  }
}
exit 0
