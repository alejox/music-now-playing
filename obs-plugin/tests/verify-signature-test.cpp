// Prueba que la verificación de firmas del plugin (src/release-signature.cpp, Monocypher)
// acepta lo que firma OpenSSL y rechaza un archivo o una firma alterados.
//
//   verify-signature-test <clave-publica.der> <archivo> <archivo.sig>
//
// La clave pública es la de `openssl pkey -pubout -outform DER` (44 bytes; los últimos 32 son
// la clave Ed25519). Lo usa run-signature-test.sh.
#include "release-signature.hpp"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

namespace {

std::vector<std::uint8_t> readFile(const char *path)
{
	std::ifstream file(path, std::ios::binary);
	return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

int failures = 0;

void expect(bool condition, const char *what)
{
	std::printf("%s %s\n", condition ? "ok  " : "FALLA", what);
	if (!condition)
		++failures;
}

} // namespace

int main(int argc, char **argv)
{
	using namespace release_signature;
	if (argc != 4) {
		std::fprintf(stderr, "uso: %s <clave-publica.der> <archivo> <archivo.sig>\n", argv[0]);
		return 2;
	}
	std::vector<std::uint8_t> der = readFile(argv[1]);
	std::vector<std::uint8_t> data = readFile(argv[2]);
	std::vector<std::uint8_t> signature = readFile(argv[3]);
	if (der.size() != 44 || data.empty()) {
		std::fprintf(stderr, "clave pública (%zu bytes) o archivo (%zu bytes) inválidos\n", der.size(),
			     data.size());
		return 2;
	}
	const std::uint8_t *key = der.data() + der.size() - kPublicKeySize;

	expect(signature.size() == kSignatureSize, "la firma de OpenSSL tiene 64 bytes");
	expect(verify(data.data(), data.size(), signature.data(), signature.size(), key),
	       "la firma de OpenSSL es válida");

	std::vector<std::uint8_t> changed = data;
	changed[changed.size() / 2] ^= 0x01;
	expect(!verify(changed.data(), changed.size(), signature.data(), signature.size(), key),
	       "un byte cambiado en el archivo invalida la firma");

	std::vector<std::uint8_t> badSignature = signature;
	badSignature[badSignature.size() / 2] ^= 0x01;
	expect(!verify(data.data(), data.size(), badSignature.data(), badSignature.size(), key),
	       "un byte cambiado en la firma la invalida");

	expect(!verify(data.data(), data.size(), signature.data(), signature.size() - 1, key),
	       "una firma de 63 bytes no vale");
	expect(!verify(data.data(), data.size(), nullptr, 0, key), "sin firma no vale");

	// La firma de esta prueba es de una clave de usar y tirar: con la de las Releases no vale.
	expect(!verifyRelease(data.data(), data.size(), signature.data(), signature.size()),
	       "otra clave no pasa por la de las Releases");

	return failures == 0 ? 0 : 1;
}
