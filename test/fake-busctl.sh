#!/usr/bin/env bash
# busctl falso para test/linux-reader.js: responde GetConnectionUnixProcessID (el pid del
# reproductor MPRIS) como "busctl --user call …". Anota cada llamada en FAKE_PLAYERCTL_LOG.
printf 'busctl %s\n' "$*" >>"${FAKE_PLAYERCTL_LOG:-/dev/null}"
name=${!#}
case $name in
  org.mpris.MediaPlayer2.spotify) echo 'u 4242' ;;
  org.mpris.MediaPlayer2.firefox.instance_1_23) echo 'u 5000' ;;
  *)
    echo "Call failed: The connection does not exist" >&2
    exit 1
    ;;
esac
exit 0
