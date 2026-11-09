#!/usr/bin/env bash
# Lee "lo que está sonando" en Linux: los reproductores MPRIS (Spotify, navegadores, VLC,
# Rhythmbox…), los mismos que controlan las teclas de música del escritorio, por medio de
# playerctl. Escribe una línea JSON por segundo en la salida estándar; server.js y el
# plugin la leen. Mismo protocolo que media/windows.ps1 y media/macos.js:
#
#   {"sessions":[{"app":"spotify","name":"Spotify","title":"…","artist":"…","album":"…",
#                 "isPlaying":true,"current":true,"progressMs":1234,"durationMs":5678,
#                 "artwork":"data:image/jpeg;base64,…"}]}      ← artwork solo cuando cambia
#   {"error":"…"}
#
# Corre con: bash media/linux.sh                  (watch <archivo>: el plugin pide modo video)
# Para controlar el reproductor: bash media/linux.sh command next
#   (previous | playpause | next | seek <ms> | volume <0–100> | shuffle | state; los tres
#   últimos responden con {"state":{"volume":40,"shuffle":true}}). El reproductor es el de la variable
#   OBS_NOW_PLAYING_PLAYER (el "app" de la sesión); sin ella, el que elija playerctl.
#
# Carátula: la del reproductor (mpris:artUrl, un archivo local o una dirección https).
# Lo que se baja de internet (carátula https, búsqueda en YouTube) no frena la lectura: corre
# en segundo plano y llega en una línea posterior (ver fetch_async).
# Modo video (plugin de OBS): mientras exista el archivo de "watch", se agrega "videoId",
# el id del video de YouTube de la dirección que anuncia el reproductor o, si no la anuncia,
# el primer resultado de buscar título y artista en YouTube.
#
# Necesita playerctl (sudo apt install playerctl). Con OBS en Flatpak se usa el playerctl
# de la máquina por flatpak-spawn --host. Corre en bash 3.2 o superior.

US=$'\x1f' # separa los campos de un reproductor
RS=$'\x1e' # separa un reproductor del siguiente
BS=\\
QUOTE='"'
LOOPS=${OBS_NOW_PLAYING_LOOPS:-1800} # ~30 min y salimos: quien nos abrió nos vuelve a abrir

emit() { printf '%s\n' "$1"; }

# Texto → cadena JSON con comillas, en JSON_OUT (sin abrir otro proceso). Los caracteres no
# ASCII van tal cual: la salida es UTF-8, como la de los otros lectores.
json_str() {
  local s=$1
  s=${s//"$BS"/"$BS$BS"}
  s=${s//"$QUOTE"/"$BS$QUOTE"}
  s=${s//$'\n'/"${BS}n"}
  s=${s//$'\r'/"${BS}r"}
  s=${s//$'\t'/"${BS}t"}
  s=${s//[[:cntrl:]]/}
  JSON_OUT="$QUOTE$s$QUOTE"
}

# ---- playerctl --------------------------------------------------------------------

PCTL=()
if [ -n "${PLAYERCTL:-}" ]; then
  # Para pruebas: otro playerctl (uno falso o uno que no existe).
  if command -v "$PLAYERCTL" >/dev/null 2>&1; then PCTL=("$PLAYERCTL"); fi
elif command -v playerctl >/dev/null 2>&1; then
  PCTL=(playerctl)
elif [ -f /.flatpak-info ] && command -v flatpak-spawn >/dev/null 2>&1 &&
  flatpak-spawn --host sh -c 'command -v playerctl' >/dev/null 2>&1; then
  # OBS en Flatpak: playerctl no viene en la caja de arena, se usa el de la máquina.
  PCTL=(flatpak-spawn --host playerctl)
fi

missing_playerctl() {
  local msg='Falta playerctl, que lee lo que suena en Linux: instálalo (sudo apt install playerctl, sudo dnf install playerctl o sudo pacman -S playerctl).'
  if [ -f /.flatpak-info ]; then
    msg="$msg Con OBS en Flatpak, instálalo en el sistema y da permiso: flatpak override --user --talk-name=org.freedesktop.Flatpak com.obsproject.Studio"
  fi
  json_str "$msg"
  emit "{\"error\":$JSON_OUT}"
}

# ---- Nombres -----------------------------------------------------------------------

# Nombre para mostrar de cada reproductor (el "name" de los otros lectores), en NAME_OUT.
display_name() {
  case $1 in
    spotify) NAME_OUT='Spotify' ;;
    chromium) NAME_OUT='Chromium' ;;
    chrome | google-chrome) NAME_OUT='Google Chrome' ;;
    brave) NAME_OUT='Brave' ;;
    firefox) NAME_OUT='Firefox' ;;
    vivaldi) NAME_OUT='Vivaldi' ;;
    opera) NAME_OUT='Opera' ;;
    edge | msedge | microsoft-edge) NAME_OUT='Microsoft Edge' ;;
    vlc) NAME_OUT='VLC' ;;
    rhythmbox) NAME_OUT='Rhythmbox' ;;
    tidal-hifi) NAME_OUT='TIDAL' ;;
    *) NAME_OUT=$1 ;;
  esac
}

is_browser() {
  case $1 in
    chromium | chrome | google-chrome | brave | firefox | vivaldi | opera | edge | msedge | microsoft-edge | epiphany) return 0 ;;
  esac
  return 1
}

# ---- Descargas sin frenar la lectura -----------------------------------------------
# El plugin da por muerto un lector que no escribe en 10 s: con la red lenta al cambiar de
# canción, la tarjeta desaparecía en pleno directo. Por eso cada descarga corre con curl en
# segundo plano (4 s como máximo) y el bucle solo mira en cada vuelta si ya terminó. Una
# descarga que falló no se repite hasta pasados 30 s; una que salió bien no se repite.
FETCH_DIR=''
F_URL=()
F_PID=()
F_STATE=() # pending | done | failed
F_AT=()    # $SECONDS cuando falló
ERR_FILE=''

cleanup() {
  local pids
  pids=$(jobs -p 2>/dev/null)
  if [ -n "$pids" ]; then kill $pids 2>/dev/null; fi
  if [ -n "$FETCH_DIR" ]; then rm -rf "$FETCH_DIR"; fi
  if [ -n "$ERR_FILE" ]; then rm -f "$ERR_FILE"; fi
}
trap cleanup EXIT

# fetch_async <url> [opciones de curl…]: deja FETCH_STATE en pending | done | failed y, con
# done, FETCH_FILE con lo bajado. No espera nunca.
fetch_async() {
  local url=$1 i n=${#F_URL[@]} found=-1
  shift
  FETCH_FILE=''
  for ((i = 0; i < n; i++)); do
    if [ "${F_URL[i]}" = "$url" ]; then found=$i; fi
  done
  if [ "$found" -lt 0 ] || { [ "${F_STATE[found]}" = 'failed' ] && [ $((SECONDS - F_AT[found])) -ge 30 ]; }; then
    if [ "$found" -lt 0 ]; then
      found=$n
      F_URL[found]=$url
    fi
    if [ -z "$FETCH_DIR" ]; then
      FETCH_DIR=$(mktemp -d 2>/dev/null) || FETCH_DIR="/tmp/obs-now-playing-fetch.$$"
      mkdir -p "$FETCH_DIR"
    fi
    rm -f "$FETCH_DIR/$found" "$FETCH_DIR/$found.ok"
    # El .ok dice que curl terminó bien (sin esperar a curl no se sabe cómo salió).
    (curl -fsSL --max-time 4 "$@" -o "$FETCH_DIR/$found" "$url" && : >"$FETCH_DIR/$found.ok") </dev/null >/dev/null 2>&1 &
    F_PID[found]=$!
    F_STATE[found]='pending'
  fi
  if [ "${F_STATE[found]}" = 'pending' ]; then
    if [ -e "$FETCH_DIR/$found.ok" ]; then
      F_STATE[found]='done'
    elif ! kill -0 "${F_PID[found]}" 2>/dev/null; then
      if [ -e "$FETCH_DIR/$found.ok" ]; then F_STATE[found]='done'; else F_STATE[found]='failed'; fi
      F_AT[found]=$SECONDS
    fi
  fi
  FETCH_STATE=${F_STATE[found]}
  if [ "$FETCH_STATE" = 'done' ]; then FETCH_FILE="$FETCH_DIR/$found"; fi
}

# ---- Carátula ----------------------------------------------------------------------


# La última carátula leída (se reintenta varias veces por canción: no se baja otra vez).
ART_CACHE_URL=''
ART_CACHE_DATA=''

# mpris:artUrl → data:image/…;base64,… en ART_OUT ('' si no hay o no se pudo leer).
# ART_PENDING=1: todavía se está bajando (la carátula llega en otra vuelta).
artwork_data() {
  local url=$1 file magic mime
  ART_OUT=''
  ART_PENDING=0
  if [ -z "$url" ]; then return; fi
  if [ "$url" = "$ART_CACHE_URL" ]; then
    ART_OUT=$ART_CACHE_DATA
    return
  fi
  case $url in
    file://*)
      file=${url#file://}
      file=${file//%/"${BS}x"} # %20 → espacio (fuera de comillas: bash 3.2 dejaría las de adentro)
      file=$(printf '%b' "$file")
      ;;
    https://* | http://*)
      command -v curl >/dev/null 2>&1 || return
      fetch_async "$url" --max-filesize 3145728
      if [ "$FETCH_STATE" = 'pending' ]; then ART_PENDING=1; fi
      if [ "$FETCH_STATE" != 'done' ]; then return; fi
      file=$FETCH_FILE
      ;;
    *) return ;;
  esac
  if [ ! -s "$file" ] || [ ! -r "$file" ]; then return; fi
  magic=$(od -An -tx1 -N4 "$file" 2>/dev/null | tr -d ' \n')
  case $magic in
    89504e47) mime='image/png' ;;
    ffd8ff*) mime='image/jpeg' ;;
    47494638) mime='image/gif' ;;
    52494646) mime='image/webp' ;;
    *) return ;;
  esac
  ART_OUT="data:$mime;base64,$(base64 <"$file" | tr -d '\n\r')"
  ART_CACHE_URL=$url
  ART_CACHE_DATA=$ART_OUT
}

# ---- Modo video --------------------------------------------------------------------

YOUTUBE_WATCH='^https?://(music\.|www\.|m\.)?youtube\.com/watch\?(.*&)?v=([A-Za-z0-9_-]{11})'
YOUTUBE_RESULT='"videoRenderer":\{"videoId":"([A-Za-z0-9_-]{11})"'

url_encode() {
  local s=$1 out='' c i
  local LC_ALL=C
  for ((i = 0; i < ${#s}; i++)); do
    c=${s:i:1}
    case $c in
      [a-zA-Z0-9.~_-]) out="$out$c" ;;
      *) out="$out$(printf '%%%02X' "'$c")" ;;
    esac
  done
  ENCODED_OUT=$out
}

# El video de una canción que no suena en YouTube: el primer resultado de la búsqueda.
# VIDEO_PENDING=1: la búsqueda sigue en curso.
search_youtube_video() {
  local html
  VIDEO_OUT=''
  VIDEO_PENDING=0
  command -v curl >/dev/null 2>&1 || return
  url_encode "$1 $2"
  # Sin esta cookie, en Europa YouTube responde con la página de consentimiento.
  fetch_async "https://www.youtube.com/results?search_query=$ENCODED_OUT" \
    -H 'User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0 Safari/537.36' \
    -H 'Accept-Language: en-US,en;q=0.8' -H 'Cookie: SOCS=CAI; CONSENT=YES+'
  if [ "$FETCH_STATE" = 'pending' ]; then VIDEO_PENDING=1; fi
  if [ "$FETCH_STATE" != 'done' ]; then return; fi
  html=$(<"$FETCH_FILE")
  if [[ $html =~ $YOUTUBE_RESULT ]]; then VIDEO_OUT=${BASH_REMATCH[1]}; fi
}

# "app\ncanción" → "id site" ('' mientras no se encuentre); hasta 3 intentos por canción,
# porque la dirección cambia un instante después del título.
VIDEO_KEYS=()
VIDEO_VALUES=()
VIDEO_TRIES=()

# Deja en VIDEO_ID / VIDEO_SITE el video de lo que suena (o vacíos).
video_for() {
  local app=$1 player=$2 title=$3 artist=$4 album=$5 url=$6 key i n found=-1
  VIDEO_ID=''
  VIDEO_SITE=''
  key="$app"$'\n'"$title"
  n=${#VIDEO_KEYS[@]}
  for ((i = 0; i < n; i++)); do
    if [ "${VIDEO_KEYS[i]}" = "$key" ]; then found=$i; fi
  done
  if [ "$found" -ge 0 ]; then
    if [ -n "${VIDEO_VALUES[found]}" ]; then
      VIDEO_ID=${VIDEO_VALUES[found]%% *}
      VIDEO_SITE=${VIDEO_VALUES[found]#* }
      return
    fi
    if [ "${VIDEO_TRIES[found]}" -ge 3 ]; then return; fi
  else
    found=$n
    VIDEO_KEYS[found]=$key
    VIDEO_VALUES[found]=''
    VIDEO_TRIES[found]=0
  fi
  VIDEO_TRIES[found]=$((VIDEO_TRIES[found] + 1))
  if [[ $url =~ $YOUTUBE_WATCH ]]; then
    VIDEO_ID=${BASH_REMATCH[3]}
    if [ "${BASH_REMATCH[1]}" = 'music.' ]; then VIDEO_SITE='ytmusic'; else VIDEO_SITE='youtube'; fi
  elif ! is_browser "$player" || [ -n "$album" ]; then
    # En el navegador, solo si trae álbum (una canción): un stream o una serie no lo traen.
    search_youtube_video "$title" "$artist"
    VIDEO_ID=$VIDEO_OUT
    # Mientras se busca no cuenta como intento: el resultado llega en otra vuelta.
    if [ "$VIDEO_PENDING" = 1 ]; then VIDEO_TRIES[found]=$((VIDEO_TRIES[found] - 1)); fi
  fi
  if [ -n "$VIDEO_ID" ]; then VIDEO_VALUES[found]="$VIDEO_ID $VIDEO_SITE"; fi
}

# ---- Lectura -----------------------------------------------------------------------

FORMAT="{{playerInstance}}$US{{playerName}}$US{{status}}$US{{position}}$US{{mpris:length}}$US{{title}}$US{{artist}}$US{{album}}$US{{mpris:artUrl}}$US{{xesam:url}}${US}fin$RS"

# Por app: qué canción ya tiene carátula enviada, el hash de esa carátula, cuántos intentos
# van, si sonaba en la vuelta anterior y en qué vuelta empezó a sonar (para "current").
ST_APP=()
ST_KEY=()
ST_HASH=()
ST_TRIES=()
ST_PLAYING=()
ST_START=()

state_index() {
  local i n=${#ST_APP[@]}
  for ((i = 0; i < n; i++)); do
    if [ "${ST_APP[i]}" = "$1" ]; then
      IDX=$i
      return
    fi
  done
  IDX=$n
  ST_APP[n]=$1
  ST_KEY[n]=''
  ST_HASH[n]=''
  ST_TRIES[n]=0
  ST_PLAYING[n]=0
  ST_START[n]=-1
}

non_negative_int() {
  local v=${1%%.*}
  case $v in
    '' | *[!0-9]*) INT_OUT=0 ;;
    *) INT_OUT=$v ;;
  esac
}

watch() {
  local video_flag=$1 loop failures=0 out code err rest rec
  local inst player status pos len title artist album art_url url tail
  local apps entries i n best best_start app key json hash playing
  ERR_FILE=$(mktemp 2>/dev/null || echo "/tmp/obs-now-playing-err.$$")

  for ((loop = 0; loop < LOOPS; loop++)); do
    out=$("${PCTL[@]}" --all-players metadata --format "$FORMAT" 2>"$ERR_FILE")
    code=$?
    err=$(head -c 300 "$ERR_FILE" 2>/dev/null)
    # Sin reproductores abiertos playerctl termina con error: eso es "nada suena".
    if [ "$code" -ne 0 ] && [ -z "$out" ] && [ -n "$err" ] && [[ $err != *"No player"* ]]; then
      json_str "playerctl: $err"
      emit "{\"error\":$JSON_OUT}"
      failures=$((failures + 1))
      if [ "$failures" -ge 5 ]; then exit 4; fi
      sleep 1
      continue
    fi
    failures=0

    # Primera pasada: un registro por reproductor con título.
    apps=()
    entries=()
    n=0
    rest=$out
    while [[ $rest == *"$RS"* ]]; do
      rec=${rest%%"$RS"*}
      rest=${rest#*"$RS"}
      rec=${rec#$'\n'}
      IFS=$US read -r -d '' inst player status pos len title artist album art_url url tail <<<"$rec"
      if [[ $tail != fin* ]] || [ -z "$title" ]; then continue; fi
      case $player in obs | obs-studio) continue ;; esac # la tarjeta de OBS, si la anunciara
      # Una app por reproductor ("spotify", "firefox"); dos ventanas del mismo navegador
      # se distinguen por su instancia.
      app=$player
      if [ -z "$app" ]; then app=$inst; fi
      for ((i = 0; i < n; i++)); do
        if [ "${apps[i]}" = "$app" ]; then app=$inst; fi
      done
      apps[n]=$app
      playing=0
      if [ "$status" = 'Playing' ]; then playing=1; fi
      key="$title"$'\n'"$artist"
      state_index "$app"
      # Empezó a sonar ahora (o cambió de canción sonando): es la más reciente.
      if [ "$playing" = 1 ] && { [ "${ST_PLAYING[IDX]}" = 0 ] || [ "${ST_KEY[IDX]}" != "$key" ]; }; then
        ST_START[IDX]=$loop
      fi
      if [ "${ST_START[IDX]}" -lt 0 ]; then ST_START[IDX]=$loop; fi
      ST_PLAYING[IDX]=$playing
      entries[n]="$IDX"
      n=$((n + 1))
      eval "E_PLAYER_$n=\$player E_TITLE_$n=\$title E_ARTIST_$n=\$artist E_ALBUM_$n=\$album E_POS_$n=\$pos E_LEN_$n=\$len E_ART_$n=\$art_url E_URL_$n=\$url"
    done

    # La actual: la que empezó a sonar último; si nada suena, la última que sonó.
    best=-1
    best_start=-2
    for ((i = 0; i < n; i++)); do
      IDX=${entries[i]}
      if [ "${ST_PLAYING[IDX]}" = 1 ] && [ "${ST_START[IDX]}" -gt "$best_start" ]; then
        best=$i
        best_start=${ST_START[IDX]}
      fi
    done
    if [ "$best" -lt 0 ]; then
      for ((i = 0; i < n; i++)); do
        IDX=${entries[i]}
        if [ "${ST_START[IDX]}" -gt "$best_start" ]; then
          best=$i
          best_start=${ST_START[IDX]}
        fi
      done
    fi

    # Segunda pasada: el JSON.
    json=''
    for ((i = 0; i < n; i++)); do
      IDX=${entries[i]}
      app=${apps[i]}
      eval "player=\$E_PLAYER_$((i + 1)) title=\$E_TITLE_$((i + 1)) artist=\$E_ARTIST_$((i + 1)) album=\$E_ALBUM_$((i + 1)) pos=\$E_POS_$((i + 1)) len=\$E_LEN_$((i + 1)) art_url=\$E_ART_$((i + 1)) url=\$E_URL_$((i + 1))"
      display_name "$player"
      json_str "$app"
      rec="{\"app\":$JSON_OUT"
      json_str "$NAME_OUT"
      rec="$rec,\"name\":$JSON_OUT"
      json_str "$title"
      rec="$rec,\"title\":$JSON_OUT"
      json_str "$artist"
      rec="$rec,\"artist\":$JSON_OUT"
      json_str "$album"
      rec="$rec,\"album\":$JSON_OUT"
      if [ "${ST_PLAYING[IDX]}" = 1 ]; then rec="$rec,\"isPlaying\":true"; else rec="$rec,\"isPlaying\":false"; fi
      if [ "$i" = "$best" ]; then rec="$rec,\"current\":true"; else rec="$rec,\"current\":false"; fi
      # playerctl da la posición y la duración en microsegundos.
      non_negative_int "$pos"
      rec="$rec,\"progressMs\":$((INT_OUT / 1000))"
      non_negative_int "$len"
      rec="$rec,\"durationMs\":$((INT_OUT / 1000))"

      # Igual que en Windows y macOS: la carátula solo se manda al cambiar de canción, y si
      # llega la misma imagen de la canción anterior se reintenta (la app aún no la cambió).
      key="$title"$'\n'"$artist"
      if [ "${ST_KEY[IDX]}" != "$key" ]; then
        ST_TRIES[IDX]=$((ST_TRIES[IDX] + 1))
        artwork_data "$art_url"
        hash=''
        if [ -n "$ART_OUT" ]; then hash="${#ART_OUT}:${ART_OUT: -96}"; fi
        # Mientras la carátula se baja, la línea sale sin ella y no se da por perdida.
        if { [ -n "$ART_OUT" ] && [ "$hash" != "${ST_HASH[IDX]}" ]; } || { [ "${ST_TRIES[IDX]}" -ge 5 ] && [ "$ART_PENDING" = 0 ]; }; then
          rec="$rec,\"artwork\":\"$ART_OUT\""
          ST_KEY[IDX]=$key
          ST_HASH[IDX]=$hash
          ST_TRIES[IDX]=0
        fi
      fi

      if [ -n "$video_flag" ] && [ -e "$video_flag" ]; then
        video_for "$app" "$player" "$title" "$artist" "$album" "$url"
        if [ -n "$VIDEO_ID" ]; then
          rec="$rec,\"videoId\":\"$VIDEO_ID\""
          if [ -n "$VIDEO_SITE" ]; then rec="$rec,\"site\":\"$VIDEO_SITE\""; fi
        fi
      fi

      if [ -n "$json" ]; then json="$json,"; fi
      json="$json$rec}"
    done
    emit "{\"sessions\":[$json]}"
    sleep 1
  done
  exit 0
}

# ---- Comandos ----------------------------------------------------------------------

# Volumen y aleatorio del reproductor (dock): state los lee; volume y shuffle los cambian y
# también responden con lo que quedó. playerctl da el volumen de 0 a 1 ("0.400000").
player_state() {
  local action=$1 arg=$2 via=$3 v vol shuf whole frac pct=null shuffle=null
  shift 3
  if [ "$action" = volume ]; then
    non_negative_int "$arg"
    v=$INT_OUT
    if [ "$v" -gt 100 ]; then v=100; fi
    if ! "${PCTL[@]}" "$@" volume "$((v / 100)).$(printf '%02d' $((v % 100)))" >/dev/null 2>&1; then
      json_str 'Este reproductor no deja cambiar el volumen desde OBS.'
      emit "{\"error\":$JSON_OUT}"
      return 1
    fi
  elif [ "$action" = shuffle ]; then
    if ! "${PCTL[@]}" "$@" shuffle Toggle >/dev/null 2>&1; then
      json_str 'Este reproductor no deja cambiar el aleatorio desde OBS.'
      emit "{\"error\":$JSON_OUT}"
      return 1
    fi
  fi
  vol=$("${PCTL[@]}" "$@" volume 2>/dev/null) || vol=''
  if [[ $vol =~ ^[0-9]+(\.[0-9]*)?$ ]]; then
    whole=${vol%%.*}
    frac=0
    if [[ $vol == *.* ]]; then frac=${vol#*.}; fi
    frac="${frac}00"
    pct=$((10#$whole * 100 + 10#${frac:0:2}))
    if [ "$pct" -gt 100 ]; then pct=100; fi
  fi
  shuf=$("${PCTL[@]}" "$@" shuffle 2>/dev/null) || shuf=''
  case $shuf in
    On) shuffle=true ;;
    Off) shuffle=false ;;
  esac
  json_str "$via"
  emit "{\"ok\":true,\"via\":$JSON_OUT,\"state\":{\"volume\":$pct,\"shuffle\":$shuffle}}"
  return 0
}

# Botones y adelantar: sobre el reproductor de OBS_NOW_PLAYING_PLAYER (la instancia exacta o,
# si es un nombre como "firefox", la instancia de ese reproductor que esté sonando).
send_command() {
  local action=$1 arg=$2 target=${OBS_NOW_PLAYING_PLAYER:-} chosen='' fallback='' inst ms out
  local args=()
  if [ -n "$target" ]; then
    while IFS= read -r inst; do
      [ -n "$inst" ] || continue
      if [ "$inst" = "$target" ]; then
        chosen=$inst
        break
      fi
      if [ "${inst%%.*}" = "$target" ]; then
        if [ -z "$fallback" ]; then fallback=$inst; fi
        if [ -z "$chosen" ] && [ "$("${PCTL[@]}" -p "$inst" status 2>/dev/null)" = 'Playing' ]; then chosen=$inst; fi
      fi
    done <<<"$("${PCTL[@]}" -l 2>/dev/null)"
    if [ -z "$chosen" ]; then chosen=$fallback; fi
    if [ -n "$chosen" ]; then args=(-p "$chosen"); fi
  fi
  case $action in
    state | volume | shuffle)
      player_state "$action" "$arg" "${chosen:-playerctl}" "${args[@]}"
      return $?
      ;;
    playpause) args+=(play-pause) ;;
    next) args+=(next) ;;
    previous) args+=(previous) ;;
    seek)
      non_negative_int "$arg"
      ms=$INT_OUT
      args+=(position "$((ms / 1000)).$(printf '%03d' $((ms % 1000)))")
      ;;
    *)
      json_str "Comando desconocido: $action"
      emit "{\"error\":$JSON_OUT}"
      return 2
      ;;
  esac
  if out=$("${PCTL[@]}" "${args[@]}" 2>&1); then
    json_str "${chosen:-playerctl}"
    emit "{\"ok\":true,\"via\":$JSON_OUT}"
    return 0
  fi
  if [ "$action" = 'seek' ]; then
    json_str 'Este reproductor no deja adelantar ni retroceder desde OBS.'
  else
    json_str "No se pudo controlar el reproductor: ${out:0:200}"
  fi
  emit "{\"error\":$JSON_OUT}"
  return 1
}

if [ "${#PCTL[@]}" -eq 0 ]; then
  missing_playerctl
  exit 2
fi

case ${1:-} in
  command)
    send_command "${2:-}" "${3:-}"
    exit $?
    ;;
  watch) watch "${2:-}" ;;
  *) watch '' ;;
esac
