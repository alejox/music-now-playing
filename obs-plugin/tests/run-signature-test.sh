#!/usr/bin/env bash
# Firma un archivo con OpenSSL 3 (clave Ed25519 de usar y tirar) y comprueba que la
# verificación del plugin (src/release-signature.cpp + Monocypher) da lo mismo que OpenSSL.
#
#   obs-plugin/tests/run-signature-test.sh
#
# OPENSSL: el openssl a usar (en macOS el de /usr/bin es LibreSSL y no firma Ed25519; el de
# Homebrew está en /opt/homebrew/opt/openssl@3/bin/openssl). CC y CXX: los compiladores.
set -euo pipefail

openssl="${OPENSSL:-openssl}"
cc="${CC:-cc}"
cxx="${CXX:-c++}"
src="$(cd "$(dirname "$0")/../src" && pwd)"
tests="$(cd "$(dirname "$0")" && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

"$openssl" version
"$cc" -std=c99 -O2 -c "$src/third-party/monocypher/monocypher.c" -o "$work/monocypher.o"
"$cc" -std=c99 -O2 -c "$src/third-party/monocypher/monocypher-ed25519.c" -o "$work/monocypher-ed25519.o"
"$cxx" -std=c++17 -O2 -Wall -Wextra -Werror -I "$src" -c "$src/release-signature.cpp" -o "$work/release-signature.o"
"$cxx" -std=c++17 -O2 -Wall -Wextra -Werror -I "$src" -c "$tests/verify-signature-test.cpp" -o "$work/test.o"
"$cxx" "$work/test.o" "$work/release-signature.o" "$work/monocypher.o" "$work/monocypher-ed25519.o" \
	-o "$work/verify-signature-test"

# Igual que la Release: clave privada en PEM, firma -rawin de 64 bytes.
"$openssl" genpkey -algorithm ed25519 -out "$work/key.pem"
"$openssl" pkey -in "$work/key.pem" -pubout -outform DER -out "$work/public.der"
head -c 300000 /dev/urandom > "$work/sample.zip"
"$openssl" pkeyutl -sign -rawin -inkey "$work/key.pem" -in "$work/sample.zip" -out "$work/sample.zip.sig"
"$openssl" pkeyutl -verify -pubin -inkey "$work/public.der" -keyform DER -rawin \
	-in "$work/sample.zip" -sigfile "$work/sample.zip.sig"

"$work/verify-signature-test" "$work/public.der" "$work/sample.zip" "$work/sample.zip.sig"
