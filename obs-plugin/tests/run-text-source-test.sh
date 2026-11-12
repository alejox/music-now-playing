#!/usr/bin/env bash
# Compila y corre las pruebas del lector de archivos de texto (src/text-source-reader.cpp),
# sin OBS ni Qt.
#
#   obs-plugin/tests/run-text-source-test.sh
#
# CXX: el compilador.
set -euo pipefail

cxx="${CXX:-c++}"
src="$(cd "$(dirname "$0")/../src" && pwd)"
tests="$(cd "$(dirname "$0")" && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

"$cxx" -std=c++17 -O2 -Wall -Wextra -Werror -I "$src" "$src/text-source-reader.cpp" "$tests/text-source-test.cpp" \
	-o "$work/text-source-test"
"$work/text-source-test"
