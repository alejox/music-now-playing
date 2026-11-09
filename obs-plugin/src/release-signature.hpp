#pragma once

#include <cstddef>
#include <cstdint>

// Firma de los zips de la Release: Ed25519 estándar (con SHA-512, como la de OpenSSL).
// La Release publica, junto a cada music-now-playing-<versión>-<sistema>.zip, el archivo
// <zip>.sig con los 64 bytes de la firma. El actualizador no instala un zip cuya firma no
// coincida con la clave pública que va dentro del plugin.
//
// Sin Qt ni OBS: se compila también en la prueba de obs-plugin/tests.
namespace release_signature {

constexpr std::size_t kSignatureSize = 64;
constexpr std::size_t kPublicKeySize = 32;

// La clave pública con la que se firman las Releases.
extern const std::uint8_t kReleasePublicKey[kPublicKeySize];

// true solo si signature (de signatureSize bytes) es la firma Ed25519 de data con publicKey.
bool verify(const std::uint8_t *data, std::size_t size, const std::uint8_t *signature, std::size_t signatureSize,
	    const std::uint8_t publicKey[kPublicKeySize]);

// verify() con la clave de las Releases.
bool verifyRelease(const std::uint8_t *data, std::size_t size, const std::uint8_t *signature,
		   std::size_t signatureSize);

} // namespace release_signature
