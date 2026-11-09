#include "release-signature.hpp"

#include "third-party/monocypher/monocypher-ed25519.h"

namespace release_signature {

// e614461f088694160336f4b0825e7d0917de81c9742994ce7b221e8cd606bd94
const std::uint8_t kReleasePublicKey[kPublicKeySize] = {
	0xe6, 0x14, 0x46, 0x1f, 0x08, 0x86, 0x94, 0x16, 0x03, 0x36, 0xf4, 0xb0, 0x82, 0x5e, 0x7d, 0x09,
	0x17, 0xde, 0x81, 0xc9, 0x74, 0x29, 0x94, 0xce, 0x7b, 0x22, 0x1e, 0x8c, 0xd6, 0x06, 0xbd, 0x94,
};

bool verify(const std::uint8_t *data, std::size_t size, const std::uint8_t *signature, std::size_t signatureSize,
	    const std::uint8_t publicKey[kPublicKeySize])
{
	if (!signature || signatureSize != kSignatureSize || !publicKey || (!data && size > 0))
		return false;
	// crypto_ed25519_check: 0 si la firma es válida, -1 si no.
	return crypto_ed25519_check(signature, publicKey, data, size) == 0;
}

bool verifyRelease(const std::uint8_t *data, std::size_t size, const std::uint8_t *signature,
		   std::size_t signatureSize)
{
	return verify(data, size, signature, signatureSize, kReleasePublicKey);
}

} // namespace release_signature
