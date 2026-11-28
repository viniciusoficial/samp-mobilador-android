#pragma once

#include <cstddef>
#include <cstdint>

namespace artwork {

// Self-contained, zero-allocation LZMA1 raw stream decoder for SA-MP 0.3.DL legacy
// direct-chunk transfers (RPC 183 subOpcode 1: fixed props {0x5D, 0, 0, 1, 0},
// i.e. lc=3, lp=0, pb=2, dictSize=65536, max 4096-byte unpacked chunks).
//
// Returns true on success and sets outDecodedLen == expectedUnpackedLen.
// Rejects malformed headers, invalid back-references, or truncated/corrupt streams cleanly.
bool decodeLzma1Chunk03dl(
	const uint8_t* src, size_t srcLen,
	uint8_t* dst, size_t expectedUnpackedLen,
	size_t& outDecodedLen) noexcept;

} // namespace artwork
