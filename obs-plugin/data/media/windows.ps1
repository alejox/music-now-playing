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
# Modo video (plugin de OBS): mientras exista el archivo -VideoFlag, para lo que suena en
# un navegador se agrega "videoId", el id del video de YouTube / YouTube Music leído de la
# barra de direcciones (UI Automation). Solo sirve si esa pestaña es la visible de su ventana.
#
#   {"sessions":[{"app":"Spotify.exe","title":"…","artist":"…","album":"…",
#                 "isPlaying":true,"current":true,"progressMs":1234,"durationMs":5678,
#                 "artwork":"data:image/png;base64,…"}]}      ← artwork solo cuando cambia
#   {"error":"…"}
#
# Usa solo lo que trae Windows PowerShell 5.1 (Windows 10/11): sin módulos extra.

param([string]$CommandFile = '', [string]$VideoFlag = '')

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

# Modo video: el id del video de YouTube que suena, leído de la barra de direcciones de
# las ventanas del navegador (la de la pestaña visible). Una ventana sirve si su título
# (el de la pestaña) dice YouTube o trae el nombre de la canción.
$browserApps = 'chrome|msedge|brave|vivaldi|opera|firefox'
$youtubeWatch = '^(?:https?://)?(?:music\.|www\.|m\.)?youtube\.com/watch\?(?:[^#]*&)?v=([A-Za-z0-9_-]{11})'
$videoIds = @{}   # "app`ncanción" → id
$videoTries = @{}
$uiaReady = $false

function Find-YouTubeVideoId($title) {
  if (-not $script:uiaReady) {
    Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
    $script:uiaReady = $true
  }
  $auto = [System.Windows.Automation.AutomationElement]
  $scope = [System.Windows.Automation.TreeScope]
  $byType = { param($type) New-Object System.Windows.Automation.PropertyCondition($auto::ControlTypeProperty, $type) }
  $found = New-Object System.Collections.ArrayList
  $windows = $auto::RootElement.FindAll($scope::Children, (& $byType ([System.Windows.Automation.ControlType]::Window)))
  foreach ($window in $windows) {
    $name = "$($window.Current.Name)"
    if ($name -notmatch 'YouTube' -and -not ($title -and $name.ToLower().Contains($title.ToLower()))) { continue }
    # La barra de direcciones es el primer cuadro de texto (la página viene después):
    # FindFirst no recorre la página entera.
    $edit = $window.FindFirst($scope::Descendants, (& $byType ([System.Windows.Automation.ControlType]::Edit)))
    if (-not $edit) { continue }
    $value = ''
    try { $value = "$($edit.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value)" } catch { continue }
    if ($value -match $youtubeWatch) { [void]$found.Add(@{ id = $Matches[1]; name = $name.ToLower() }) }
  }
  # Con varias ventanas de YouTube, la que tiene el título de la canción.
  $hit = $found | Where-Object { $title -and $_.name.Contains($title.ToLower()) } | Select-Object -First 1
  if (-not $hit -and $found.Count -eq 1) { $hit = $found[0] }
  if ($hit) { $hit.id } else { '' }
}

function Get-VideoId($app, $title) {
  if (-not $VideoFlag -or $app -notmatch $browserApps -or -not [System.IO.File]::Exists($VideoFlag)) { return '' }
  $key = "$app`n$title"
  if ($videoIds[$key]) { return $videoIds[$key] }
  if ([int]$videoTries[$key] -ge 3) { return '' } # la dirección cambia un instante después del título
  $videoTries[$key] = 1 + [int]$videoTries[$key]
  $id = ''
  try { $id = Find-YouTubeVideoId $title } catch { $id = '' }
  if ($id) { $videoIds[$key] = $id }
  $id
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
        $fresh = $art -and $art.hash -ne $sentHash[$app]
        if ($fresh -or $tries[$app] -ge 5) {
          $entry.artwork = if ($art) { $art.url } else { '' }
          $sentKey[$app] = $key
          $sentHash[$app] = if ($art) { $art.hash } else { '' }
          $tries[$app] = 0
        }
      }
      $videoId = Get-VideoId $app $props.Title
      if ($videoId) { $entry.videoId = $videoId }
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
