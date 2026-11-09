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

  # "Canción (Official Video)" o "Artista - Topic" son cosas de YouTube, no del disco.
  function Clean-Name($s) { (("$s" -replace '\s*[\(\[][^\)\]]*[\)\]]', '') -replace '\s+-\s+topic$', '').Trim() }
  function Normalize-Name($s) {
    $plain = "$s".ToLowerInvariant().Normalize([Text.NormalizationForm]::FormD) -replace '\p{Mn}', ''
    $plain -replace '[^a-z0-9]', ''
  }

  # Carátula del catálogo público de Apple (iTunes Search, sin cuenta ni clave). Solo se
  # acepta si artista y título coinciden, para no mostrar una carátula equivocada.
  function Search-CatalogArtwork($title, $artist) {
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
        return Get-WebImage ($hit.artworkUrl100 -replace '/\d+x\d+bb\.', '/600x600bb.')
      }
    } catch {}
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
    if ($app -match $browserApps) {
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
  if ($app -notmatch $browserApps) { return @{} }
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
  Emit ([ordered]@{
    catalogArtwork = [bool]$art
    searchVideoId  = $id
    thumbnail      = [bool]$thumb
    youtubeWindows = @($windows | ForEach-Object { [ordered]@{ name = $_.name; id = $_.id; site = $_.site } })
    siteChecks     = $siteChecks
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
      # Aleatorio: solo si el reproductor deja cambiarlo (si no, el dock apaga el botón).
      if ($playback.Controls -and $playback.Controls.IsShuffleEnabled -and $null -ne $playback.IsShuffleActive) {
        $entry.shuffle = [bool]$playback.IsShuffleActive
      }

      $key = "$($props.Title)`n$($props.Artist)"
      if ($sentKey[$app] -ne $key) {
        $tries[$app] = 1 + [int]$tries[$app]
        $art = $null
        $waiting = $false
        if ($props.Thumbnail) {
          try { $art = Read-Thumbnail $props.Thumbnail } catch { $art = $null }
        }
        # Sin carátula del reproductor (tras unos segundos, por si llega tarde): la del
        # catálogo de Apple o, en el navegador, la miniatura del video de YouTube. Mientras
        # se busca, la línea sale sin carátula y no se da por perdida.
        if (-not $art -and $tries[$app] -ge 3) {
          $found = Get-NetworkArtwork $app $props.Title $props.Artist $props.AlbumTitle
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
        if ($app -match $browserApps) {
          $site = Get-BrowserSite $app $props.Title $props.AlbumTitle
          if ($site) { $entry.site = $site }
        }
        $video = Get-Video $app $props.Title $props.Artist $props.AlbumTitle $site
        if ($video -and $video.id) {
          $entry.videoId = $video.id
          # Si las pestañas no lo dijeron, el sitio que vio la búsqueda del video (la ventana
          # visible puede ser otra pestaña de YouTube, por eso no manda sobre las pestañas).
          if (-not $entry.site -and $video.site) { $entry.site = $video.site }
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
    Update-Lookups
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
