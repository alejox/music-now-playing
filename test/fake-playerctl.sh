#!/usr/bin/env bash
# playerctl falso para test/linux-reader.js: llena el --format de verdad (las {{claves}})
# con reproductores de prueba y anota en FAKE_PLAYERCTL_LOG cada llamada.
#   FAKE_PLAYERCTL_MODE: normal (Spotify sonando + Firefox en pausa) | youtube | empty
printf '%s\n' "$*" >>"${FAKE_PLAYERCTL_LOG:-/dev/null}"
mode=${FAKE_PLAYERCTL_MODE:-normal}

instances() {
  case $mode in
    normal) printf 'spotify\nfirefox.instance_1_23\n' ;;
    youtube) printf 'chromium.instance99\n' ;;
  esac
}

# fill <formato> <clave> <valor>…: reemplaza cada {{clave}}; las que no vienen quedan vacías.
fill() {
  local fmt=$1
  shift
  while [ $# -gt 0 ]; do
    fmt=${fmt//"{{$1}}"/"$2"}
    shift 2
  done
  while [[ $fmt == *'{{'*'}}'* ]]; do
    local before=${fmt%%'{{'*}
    local after=${fmt#*'}}'}
    fmt="$before$after"
  done
  printf '%s\n' "$fmt"
}

player() {
  local fmt=$1
  case $2 in
    spotify)
      fill "$fmt" playerInstance spotify playerName spotify status Playing position 61500000 \
        mpris:length 258000000 title "Say \"Hi\" \\ Bye$(printf '\t')now" artist 'Bomba Estéreo, Bad Bunny' \
        album 'Canción ñ 🎵' mpris:artUrl "file://${FAKE_ART// /%20}"
      ;;
    firefox.instance_1_23)
      fill "$fmt" playerInstance firefox.instance_1_23 playerName firefox status Paused position 0 title 'Un video'
      ;;
    chromium.instance99)
      fill "$fmt" playerInstance chromium.instance99 playerName chromium status Playing position 1000000 \
        mpris:length 213000000 title 'Never Gonna Give You Up' artist 'Rick Astley' \
        xesam:url 'https://music.youtube.com/watch?v=dQw4w9WgXcQ&list=RD'
      ;;
  esac
}

if [ "$mode" = empty ] && [ "$1" != -l ]; then
  echo 'No players found' >&2
  exit 1
fi

case $1 in
  -l) instances ;;
  --all-players)
    # --all-players metadata --format <formato>
    for p in $(instances); do player "$4" "$p"; done
    ;;
  -p)
    target=$2
    case $3 in
      status) if [ "$target" = spotify ]; then echo Playing; else echo Paused; fi ;;
      play-pause | next | previous) ;;
      position)
        if [ "$target" != spotify ]; then
          echo "Player $target cannot seek" >&2
          exit 1
        fi
        ;;
      *) exit 1 ;;
    esac
    ;;
  *) exit 1 ;;
esac
exit 0
