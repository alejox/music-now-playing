#!/usr/bin/env bash
# pactl falso para test/linux-reader.js: tres flujos de audio (sink inputs) con el texto de
# "pactl list sink-inputs" en inglés (LC_ALL=C) y el volumen de cada uno guardado en
# FAKE_PACTL_DIR. Anota cada llamada en FAKE_PLAYERCTL_LOG con "pactl " delante.
#   #7 Spotify (pid 4242) · #8 un proceso hijo de Firefox (pid 5001, padre 5000) · #9 paplay
printf 'pactl %s\n' "$*" >>"${FAKE_PLAYERCTL_LOG:-/dev/null}"
dir=${FAKE_PACTL_DIR:-/tmp}

volume_of() {
  local v=30
  if [ -f "$dir/vol.$1" ]; then v=$(<"$dir/vol.$1"); fi
  printf '%s' "$v"
}

sink_input() {
  local index=$1 pid=$2 binary=$3 name=$4 v
  v=$(volume_of "$index")
  printf 'Sink Input #%s\n' "$index"
  printf '\tDriver: PipeWire\n\tOwner Module: n/a\n\tClient: 55\n\tSink: 49\n'
  printf '\tSample Specification: float32le 2ch 48000Hz\n\tChannel Map: front-left,front-right\n'
  printf '\tCorked: no\n\tMute: no\n'
  printf '\tVolume: front-left: 26214 / %3s%% / -23.88 dB,   front-right: 26214 / %3s%% / -23.88 dB\n' "$v" "$v"
  printf '\t        balance 0.00\n\tBuffer Latency: 0 usec\n\tSink Latency: 0 usec\n'
  printf '\tResample method: PipeWire\n\tProperties:\n'
  printf '\t\tapplication.name = "%s"\n' "$name"
  printf '\t\tapplication.process.id = "%s"\n' "$pid"
  printf '\t\tapplication.process.binary = "%s"\n' "$binary"
  printf '\t\tmedia.name = "Playback"\n\n'
}

case ${1:-} in
  list)
    if [ "${2:-}" != sink-inputs ]; then exit 1; fi
    sink_input 7 4242 spotify Spotify
    sink_input 8 5001 firefox Firefox
    sink_input 9 999 paplay paplay
    ;;
  set-sink-input-volume)
    v=${3%\%}
    printf '%s' "$v" >"$dir/vol.$2"
    ;;
  *) exit 1 ;;
esac
exit 0
