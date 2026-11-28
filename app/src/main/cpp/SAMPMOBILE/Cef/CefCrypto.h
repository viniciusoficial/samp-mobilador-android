#pragma once

// Crypto primitives required by the omp-cef wire protocol, reimplemented for
// Android on top of vendored public-domain code (TweetNaCl, BLAKE2 ref,
// tiny-AES-c). Behaviour must match libsodium exactly where the PC client and
// the server plugin use it:
//
//   * session key exchange == libsodium crypto_kx_keypair +
//     crypto_kx_client_session_keys (X25519 + BLAKE2b-512, no salt/person)
//   * packet encryption     == crypto_secretbox_easy / _open_easy
//     (XSalsa20-Poly1305, 24-byte nonce prepended)
//   * resource entries      == OCEF2 header + AES-128-CBC (tiny-AES-c),
//     PKCS#7-style padding, plus the legacy (no-magic) format
//
// Verified against libsodium-generated vectors in tools/cef_kat (see
// AUDIT/protocol notes).

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cefcrypto
{

constexpr size_t KX_PUBLIC_KEY_BYTES  = 32;
constexpr size_t KX_SECRET_KEY_BYTES  = 32;
constexpr size_t SESSION_KEY_BYTES    = 32;
constexpr size_t PACKET_NONCE_BYTES   = 24; // crypto_secretbox_NONCEBYTES
constexpr size_t PACKET_MAC_BYTES     = 16; // crypto_secretbox_MACBYTES
constexpr size_t AES_IV_BYTES         = 16;

struct KeyPair
{
    uint8_t publicKey[KX_PUBLIC_KEY_BYTES]{};
    uint8_t secretKey[KX_SECRET_KEY_BYTES]{};
};

struct SessionKeys
{
    uint8_t rx[SESSION_KEY_BYTES]{}; // decrypt server -> client
    uint8_t tx[SESSION_KEY_BYTES]{}; // encrypt client -> server
};

// Fills `out` with OS randomness (getrandom(2), /dev/urandom fallback).
void RandomBytes(uint8_t* out, size_t n);

// crypto_kx_keypair equivalent: sk = random(32), pk = X25519_base(sk).
void GenerateKeyPair(KeyPair& out);

// X25519_base(sk): derives the public key for an existing secret key
// (used by known-answer tests and for restoring a persisted keypair).
void DerivePublicKey(const uint8_t secretKey[KX_SECRET_KEY_BYTES],
                     uint8_t publicKey[KX_PUBLIC_KEY_BYTES]);

// crypto_kx_client_session_keys equivalent. Returns false if the X25519
// shared secret is invalid (low-order server public key), matching
// libsodium's -1 return path.
bool GenerateClientSessionKeys(const KeyPair& client,
                               const uint8_t serverPublicKey[KX_PUBLIC_KEY_BYTES],
                               SessionKeys& out);

// crypto_secretbox_easy: returns nonce(24) || mac(16) || ciphertext,
// empty vector on bad input.
std::vector<uint8_t> EncryptPacket(const uint8_t* plaintext, size_t size,
                                   const uint8_t key[SESSION_KEY_BYTES]);

// crypto_secretbox_open_easy for nonce-prefixed input.
std::vector<uint8_t> DecryptPacket(const uint8_t* data, size_t size,
                                   const uint8_t key[SESSION_KEY_BYTES]);

// Lowercase hex SHA-256 (resource manifest/file hashes).
std::string Sha256Hex(const uint8_t* data, size_t size);

/**
 * 分块计算文件 SHA-256（小写 hex）。
 *
 * 资源包可能有几十到上百 MB，ReadFile + Sha256Hex 需要把整个文件放进内存，
 * 而这个调用点在游戏主线程。流式读取只用 1MB 缓冲，内存峰值与文件大小无关。
 *
 * @param maxBytes 最多读取的字节数；超过则视为失败（防止被超大文件拖死）
 * @return 失败（打不开 / 超限）返回空串
 */
std::string Sha256FileHex(const char* path, size_t maxBytes);

// Decodes one stored resource file (OCEF2 format, or legacy raw IV||ct).
// Mirrors DecodeResourceEntry() in src/shared/crypto.hpp.
bool DecodeResourceEntry(const std::vector<uint8_t>& input,
                         const std::vector<uint8_t>& masterKey,
                         std::vector<uint8_t>& output);

} // namespace cefcrypto
