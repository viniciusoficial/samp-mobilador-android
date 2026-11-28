#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace artwork {

struct DecodedTexture {
	std::string name;
	std::string maskName;
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t filterFlags = 0; // Raw RW filter + U/V address flags
	bool hasAlpha = false;
	std::vector<uint8_t> rgba; // Tightly packed RGBA8888 (width * height * 4)
};

// Hard cumulative RGBA memory cap per TXD to block DXT1 memory-amplification OOM attacks.
inline constexpr size_t kMaxDecodedTxdBytes = 96u * 1024u * 1024u; // 96 MiB

enum class TxdParseFailure : uint8_t {
	None = 0,
	Malformed = 1,
	BudgetExceeded = 2,
};

struct TxdParseResult {
	bool ok = false;
	TxdParseFailure failure = TxdParseFailure::None;
	std::string error;
	uint16_t headerTextureCount = 0;
	size_t totalDecodedBytes = 0;
	std::vector<DecodedTexture> textures;
};

// Decodes a PC GTA:SA RenderWare .txd (D3D8 / D3D9) into tightly packed RGBA8888 textures.
// - Supports DXT1, DXT3, DXT5, PAL8, PAL4, BGRA32/BGRX32, and 16-bit 565/1555/4444.
// - Enforces strict chunk bounds, texture count <= 1024, dimensions <= 4096 (downsampled
//   and normalized to power-of-two <= maxDim for OpenGL ES 2.0 GL_REPEAT compatibility),
//   and cumulative decoded RGBA budget <= maxTotalBytes.
// - Never throws or overflows on truncated/malformed input.
TxdParseResult decodeTxdBytes(const uint8_t* data, size_t size, uint32_t maxDim = 2048,
	bool normalizePow2 = true, size_t maxTotalBytes = kMaxDecodedTxdBytes);

// Streaming variant: decodes one texture at a time and passes it to onTexture so callers
// (e.g. ArtworkStore) can upload and release each RGBA buffer immediately instead of holding
// all decoded textures in CPU RAM simultaneously.
bool decodeTxdStreaming(const uint8_t* data, size_t size, uint32_t maxDim, bool normalizePow2,
	size_t maxTotalBytes, const std::function<bool(DecodedTexture&&)>& onTexture,
	size_t* outTotalDecodedBytes = nullptr, TxdParseFailure* outFailure = nullptr,
	std::string* outError = nullptr);

// Fast structural validation of a downloaded .txd before committing to cache.
// Verifies header, chunk bounds, texture count, platform, dimensions, exact pixel-format
// mip0 payload length, and projected cumulative decoded RGBA memory budget.
bool validateTxdBytes(const uint8_t* data, size_t size, std::string* outError = nullptr,
	size_t maxTotalBytes = kMaxDecodedTxdBytes, TxdParseFailure* outFailure = nullptr,
	size_t* outProjectedBytes = nullptr);

} // namespace artwork
