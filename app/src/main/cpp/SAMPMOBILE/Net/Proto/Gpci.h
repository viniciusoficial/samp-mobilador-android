#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace proto {
namespace detail {

inline uint32_t rotl32(uint32_t v, unsigned shift)
{
	return (v << shift) | (v >> (32 - shift));
}

inline std::array<uint8_t, 20> sha1(const uint8_t* data, size_t len)
{
	uint32_t h0 = 0x67452301u;
	uint32_t h1 = 0xEFCDAB89u;
	uint32_t h2 = 0x98BADCFEu;
	uint32_t h3 = 0x10325476u;
	uint32_t h4 = 0xC3D2E1F0u;

	const uint64_t bitLen = static_cast<uint64_t>(len) * 8u;
	std::vector<uint8_t> msg(data, data + len);
	msg.push_back(0x80u);
	while ((msg.size() % 64u) != 56u) {
		msg.push_back(0u);
	}
	for (int i = 7; i >= 0; --i) {
		msg.push_back(static_cast<uint8_t>((bitLen >> (i * 8)) & 0xFFu));
	}

	uint32_t w[80];
	for (size_t chunk = 0; chunk < msg.size(); chunk += 64u) {
		for (int i = 0; i < 16; ++i) {
			const size_t off = chunk + static_cast<size_t>(i) * 4u;
			w[i] = (static_cast<uint32_t>(msg[off]) << 24) |
				(static_cast<uint32_t>(msg[off + 1]) << 16) |
				(static_cast<uint32_t>(msg[off + 2]) << 8) |
				static_cast<uint32_t>(msg[off + 3]);
		}
		for (int i = 16; i < 80; ++i) {
			w[i] = rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
		}

		uint32_t a = h0;
		uint32_t b = h1;
		uint32_t c = h2;
		uint32_t d = h3;
		uint32_t e = h4;

		for (int i = 0; i < 80; ++i) {
			uint32_t f = 0;
			uint32_t k = 0;
			if (i < 20) {
				f = (b & c) | ((~b) & d);
				k = 0x5A827999u;
			}
			else if (i < 40) {
				f = b ^ c ^ d;
				k = 0x6ED9EBA1u;
			}
			else if (i < 60) {
				f = (b & c) | (b & d) | (c & d);
				k = 0x8F1BBCDCu;
			}
			else {
				f = b ^ c ^ d;
				k = 0xCA62C1D6u;
			}
			const uint32_t temp = rotl32(a, 5) + f + e + k + w[i];
			e = d;
			d = c;
			c = rotl32(b, 30);
			b = a;
			a = temp;
		}

		h0 += a;
		h1 += b;
		h2 += c;
		h3 += d;
		h4 += e;
	}

	std::array<uint8_t, 20> out{};
	const uint32_t h[5] = {h0, h1, h2, h3, h4};
	for (int i = 0; i < 5; ++i) {
		out[i * 4 + 0] = static_cast<uint8_t>((h[i] >> 24) & 0xFFu);
		out[i * 4 + 1] = static_cast<uint8_t>((h[i] >> 16) & 0xFFu);
		out[i * 4 + 2] = static_cast<uint8_t>((h[i] >> 8) & 0xFFu);
		out[i * 4 + 3] = static_cast<uint8_t>(h[i] & 0xFFu);
	}
	return out;
}

inline char hexUpper(uint8_t nibble)
{
	return nibble < 10 ? static_cast<char>('0' + nibble) : static_cast<char>('A' + (nibble - 10));
}

inline int hexVal(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
	if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
	return -1;
}

} // namespace detail

// Generate a SA-MP / open.mp compatible GPCI string from an opaque per-device seed.
// Mirrors PC NetGenerateGpci: SHA-1 -> 2-bit pair sorting per nibble -> multiply by 1001 -> uppercase hex.
inline std::string generateGpci(const std::string& seed, uint32_t factor = 1001u)
{
	std::array<uint8_t, 20> transformed = detail::sha1(
		reinterpret_cast<const uint8_t*>(seed.data()), seed.size());

	bool allZero = true;
	for (uint8_t& byte : transformed) {
		const uint8_t a = byte & 3u;
		const uint8_t b = (byte >> 2) & 3u;
		const uint8_t c = (byte >> 4) & 3u;
		const uint8_t d = (byte >> 6) & 3u;
		const uint8_t low = (a > b) ? static_cast<uint8_t>((a << 2) | b) : static_cast<uint8_t>((b << 2) | a);
		const uint8_t high = (c > d) ? static_cast<uint8_t>((c << 2) | d) : static_cast<uint8_t>((d << 2) | c);
		byte = static_cast<uint8_t>((high << 4) | low);
		if (byte != 0) allZero = false;
	}
	if (allZero) {
		transformed[19] = 1u;
	}

	// Multiply 20-byte big-endian unsigned integer by factor (1001 -> <= 22 bytes).
	std::array<uint8_t, 24> prod{};
	uint32_t carry = 0;
	for (int i = 19; i >= 0; --i) {
		const uint32_t v = static_cast<uint32_t>(transformed[i]) * factor + carry;
		prod[static_cast<size_t>(i + 4)] = static_cast<uint8_t>(v & 0xFFu);
		carry = v >> 8;
	}
	for (int i = 3; i >= 0 && carry > 0; --i) {
		prod[static_cast<size_t>(i)] = static_cast<uint8_t>(carry & 0xFFu);
		carry >>= 8;
	}

	std::string hex;
	hex.reserve(44);
	bool started = false;
	for (uint8_t byte : prod) {
		const uint8_t hi = static_cast<uint8_t>(byte >> 4);
		const uint8_t lo = static_cast<uint8_t>(byte & 0x0Fu);
		if (started || hi != 0) {
			hex.push_back(detail::hexUpper(hi));
			started = true;
		}
		if (started || lo != 0) {
			hex.push_back(detail::hexUpper(lo));
			started = true;
		}
	}
	return hex.empty() ? std::string("0") : hex;
}

// Validate a GPCI string using the exact open.mp check:
// hex integer divisible by 1001, non-empty quotient with hex length < 50.
inline bool validateGpci(const std::string& hex, std::string* outSerial = nullptr)
{
	if (hex.empty()) return false;
	uint32_t rem = 0;
	std::string quotient;
	quotient.reserve(hex.size());
	for (char ch : hex) {
		const int d = detail::hexVal(ch);
		if (d < 0) return false;
		const uint32_t cur = (rem << 4) | static_cast<uint32_t>(d);
		const uint32_t q = cur / 1001u;
		rem = cur % 1001u;
		if (!quotient.empty() || q != 0) {
			quotient.push_back(detail::hexUpper(static_cast<uint8_t>(q)));
		}
	}
	if (rem != 0 || quotient.empty() || quotient.size() >= 50) {
		return false;
	}
	if (outSerial) {
		*outSerial = std::move(quotient);
	}
	return true;
}

} // namespace proto
