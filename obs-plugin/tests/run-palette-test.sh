#!/usr/bin/env bash
# Compila y corre las pruebas de la paleta de colores (src/palette.cpp), sin OBS ni Qt.
#
#   obs-plugin/tests/run-palette-test.sh
#
# CXX: el compilador.
set -euo pipefail

cxx="${CXX:-c++}"
src="$(cd "$(dirname "$0")/../src" && pwd)"
tests="$(cd "$(dirname "$0")" && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

"$cxx" -std=c++17 -O2 -Wall -Wextra -Werror -I "$src" "$src/palette.cpp" "$tests/palette-test.cpp" -o "$work/palette-test"
"$work/palette-test"
