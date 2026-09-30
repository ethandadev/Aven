#pragma once

// Ed25519 signature checking (RFC 8032), for Rynax's signed updates: only verifying, never signing,
// and on public data, so it's plain code rather than constant-time. The signing side is
// tools/release/update_key.py. Also SHA-512, which Ed25519 is built on.

#include <cstddef>
#include <cstdint>

namespace rynax::ed25519 {

// True when `signature` is `publicKey`'s signature of the message. Rejects malformed keys and
// signatures (points not on the curve, an S part that isn't reduced).
bool verify(const uint8_t publicKey[32], const uint8_t* message, size_t size, const uint8_t signature[64]);

void sha512(const uint8_t* data, size_t size, uint8_t out[64]);

} // namespace rynax::ed25519
