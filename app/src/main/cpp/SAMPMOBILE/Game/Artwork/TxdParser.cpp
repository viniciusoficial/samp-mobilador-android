#include "TxdParser.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace artwork {

namespace {

constexpr uint32_t kRwStruct = 0x0001u;
constexpr uint32_t kRwTextureNative = 0x0015u;
constexpr uint32_t kRwTexDictionary = 0x0016u;

constexpr uint32_t kFourCcDxt1 = 0x31545844u; // "DXT1"
constexpr uint32_t kFourCcDxt3 = 0x33545844u; // "DXT3"
constexpr uint32_t kFourCcDxt5 = 0x35545844u; // "DXT5"

constexpr uint32_t kMaxTexturesPerTxd = 1024u;
constexpr uint32_t kMaxInputDim = 4096u;

inline uint16_t readLE16(const uint8_t* p) noexcept
{
	return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

inline uint32_t readLE32(const uint8_t* p) noexcept
{
	return static_cast<uint32_t>(p[0]) |
	       (static_cast<uint32_t>(p[1]) << 8) |
	       (static_cast<uint32_t>(p[2]) << 16) |
	       (static_cast<uint32_t>(p[3]) << 24);
}

inline uint64_t readLE64(const uint8_t* p) noexcept
{
	return static_cast<uint64_t>(readLE32(p)) | (static_cast<uint64_t>(readLE32(p + 4)) << 32);
}

std::string readFixedAscii(const uint8_t* p, size_t maxLen)
{
	size_t n = 0;
	while (n < maxLen && p[n] != 0) {
		++n;
	}
	// Trim trailing ASCII whitespace
	while (n > 0 && (p[n - 1] == ' ' || p[n - 1] == '\t' || p[n - 1] == '\r' || p[n - 1] == '\n')) {
		--n;
	}
	return std::string(reinterpret_cast<const char*>(p), n);
}

inline void rgb565ToRgba(uint16_t c, uint8_t out[4]) noexcept
{
	const uint32_t r = (c >> 11) & 0x1Fu;
	const uint32_t g = (c >> 5) & 0x3Fu;
	const uint32_t b = c & 0x1Fu;
	out[0] = static_cast<uint8_t>((r << 3) | (r >> 2));
	out[1] = static_cast<uint8_t>((g << 2) | (g >> 4));
	out[2] = static_cast<uint8_t>((b << 3) | (b >> 2));
	out[3] = 255;
}

inline void lerpColor(const uint8_t a[4], const uint8_t b[4], uint32_t wa, uint32_t wb, uint32_t div, uint8_t out[4]) noexcept
{
	out[0] = static_cast<uint8_t>((a[0] * wa + b[0] * wb) / div);
	out[1] = static_cast<uint8_t>((a[1] * wa + b[1] * wb) / div);
	out[2] = static_cast<uint8_t>((a[2] * wa + b[2] * wb) / div);
	out[3] = 255;
}

void decodeDxt1(const uint8_t* src, size_t len, uint32_t w, uint32_t h, bool allowPunchthrough, std::vector<uint8_t>& out)
{
	out.assign(static_cast<size_t>(w) * h * 4u, 0);
	const uint32_t bw = std::max(1u, (w + 3u) >> 2);
	const uint32_t bh = std::max(1u, (h + 3u) >> 2);
	size_t p = 0;
	uint8_t c[4][4];
	for (uint32_t by = 0; by < bh; ++by) {
		for (uint32_t bx = 0; bx < bw; ++bx) {
			if (p + 8u > len) return;
			const uint16_t c0 = readLE16(src + p);
			const uint16_t c1 = readLE16(src + p + 2);
			const uint32_t idx = readLE32(src + p + 4);
			p += 8u;

			rgb565ToRgba(c0, c[0]);
			rgb565ToRgba(c1, c[1]);
			if (c0 > c1) {
				lerpColor(c[0], c[1], 2, 1, 3, c[2]);
				lerpColor(c[0], c[1], 1, 2, 3, c[3]);
			} else {
				lerpColor(c[0], c[1], 1, 1, 2, c[2]);
				c[3][0] = 0;
				c[3][1] = 0;
				c[3][2] = 0;
				c[3][3] = allowPunchthrough ? 0 : 255;
			}

			for (uint32_t py = 0; py < 4; ++py) {
				const uint32_t y = (by << 2) + py;
				if (y >= h) continue;
				for (uint32_t px = 0; px < 4; ++px) {
					const uint32_t x = (bx << 2) + px;
					if (x >= w) continue;
					const uint32_t ci = (idx >> (((py << 2) + px) << 1)) & 3u;
					uint8_t* dst = out.data() + (static_cast<size_t>(y) * w + x) * 4u;
					dst[0] = c[ci][0];
					dst[1] = c[ci][1];
					dst[2] = c[ci][2];
					dst[3] = c[ci][3];
				}
			}
		}
	}
}

void decodeDxt3(const uint8_t* src, size_t len, uint32_t w, uint32_t h, std::vector<uint8_t>& out)
{
	out.assign(static_cast<size_t>(w) * h * 4u, 0);
	const uint32_t bw = std::max(1u, (w + 3u) >> 2);
	const uint32_t bh = std::max(1u, (h + 3u) >> 2);
	size_t p = 0;
	uint8_t c[4][4];
	for (uint32_t by = 0; by < bh; ++by) {
		for (uint32_t bx = 0; bx < bw; ++bx) {
			if (p + 16u > len) return;
			const uint64_t alphaBits = readLE64(src + p);
			const uint16_t c0 = readLE16(src + p + 8);
			const uint16_t c1 = readLE16(src + p + 10);
			const uint32_t idx = readLE32(src + p + 12);
			p += 16u;

			rgb565ToRgba(c0, c[0]);
			rgb565ToRgba(c1, c[1]);
			lerpColor(c[0], c[1], 2, 1, 3, c[2]);
			lerpColor(c[0], c[1], 1, 2, 3, c[3]);

			for (uint32_t py = 0; py < 4; ++py) {
				const uint32_t y = (by << 2) + py;
				if (y >= h) continue;
				for (uint32_t px = 0; px < 4; ++px) {
					const uint32_t x = (bx << 2) + px;
					if (x >= w) continue;
					const uint32_t bitIdx = (py << 2) + px;
					const uint32_t ci = (idx >> (bitIdx << 1)) & 3u;
					const uint32_t a4 = static_cast<uint32_t>((alphaBits >> (bitIdx << 2)) & 0xFu);
					uint8_t* dst = out.data() + (static_cast<size_t>(y) * w + x) * 4u;
					dst[0] = c[ci][0];
					dst[1] = c[ci][1];
					dst[2] = c[ci][2];
					dst[3] = static_cast<uint8_t>((a4 << 4) | a4);
				}
			}
		}
	}
}

void decodeDxt5(const uint8_t* src, size_t len, uint32_t w, uint32_t h, std::vector<uint8_t>& out)
{
	out.assign(static_cast<size_t>(w) * h * 4u, 0);
	const uint32_t bw = std::max(1u, (w + 3u) >> 2);
	const uint32_t bh = std::max(1u, (h + 3u) >> 2);
	size_t p = 0;
	uint8_t c[4][4];
	uint8_t a[8];
	for (uint32_t by = 0; by < bh; ++by) {
		for (uint32_t bx = 0; bx < bw; ++bx) {
			if (p + 16u > len) return;
			a[0] = src[p];
			a[1] = src[p + 1];
			if (a[0] > a[1]) {
				for (int i = 1; i <= 6; ++i) {
					a[1 + i] = static_cast<uint8_t>(((7 - i) * a[0] + i * a[1]) / 7);
				}
			} else {
				for (int i = 1; i <= 4; ++i) {
					a[1 + i] = static_cast<uint8_t>(((5 - i) * a[0] + i * a[1]) / 5);
				}
				a[6] = 0;
				a[7] = 255;
			}

			uint64_t aIdx = 0;
			for (int i = 0; i < 6; ++i) {
				aIdx |= static_cast<uint64_t>(src[p + 2 + i]) << (i * 8);
			}

			const uint16_t c0 = readLE16(src + p + 8);
			const uint16_t c1 = readLE16(src + p + 10);
			const uint32_t idx = readLE32(src + p + 12);
			p += 16u;

			rgb565ToRgba(c0, c[0]);
			rgb565ToRgba(c1, c[1]);
			lerpColor(c[0], c[1], 2, 1, 3, c[2]);
			lerpColor(c[0], c[1], 1, 2, 3, c[3]);

			for (uint32_t py = 0; py < 4; ++py) {
				const uint32_t y = (by << 2) + py;
				if (y >= h) continue;
				for (uint32_t px = 0; px < 4; ++px) {
					const uint32_t x = (bx << 2) + px;
					if (x >= w) continue;
					const uint32_t bitIdx = (py << 2) + px;
					const uint32_t ci = (idx >> (bitIdx << 1)) & 3u;
					const uint32_t ai = static_cast<uint32_t>((aIdx >> (bitIdx * 3u)) & 7u);
					uint8_t* dst = out.data() + (static_cast<size_t>(y) * w + x) * 4u;
					dst[0] = c[ci][0];
					dst[1] = c[ci][1];
					dst[2] = c[ci][2];
					dst[3] = a[ai];
				}
			}
		}
	}
}

void decodePal8(const uint8_t* src, size_t len, const uint8_t* palette, uint32_t w, uint32_t h, std::vector<uint8_t>& out)
{
	const size_t pixels = static_cast<size_t>(w) * h;
	out.assign(pixels * 4u, 0);
	const size_t count = std::min(pixels, len);
	for (size_t i = 0; i < count; ++i) {
		const size_t pi = static_cast<size_t>(src[i]) * 4u;
		uint8_t* dst = out.data() + i * 4u;
		dst[0] = palette[pi + 0];
		dst[1] = palette[pi + 1];
		dst[2] = palette[pi + 2];
		dst[3] = palette[pi + 3];
	}
}

void decodePal4(const uint8_t* src, size_t len, const uint8_t* palette, uint32_t w, uint32_t h, std::vector<uint8_t>& out)
{
	const size_t pixels = static_cast<size_t>(w) * h;
	out.assign(pixels * 4u, 0);
	for (size_t i = 0; i < pixels; ++i) {
		const size_t byteIdx = i >> 1;
		if (byteIdx >= len) break;
		const uint8_t b = src[byteIdx];
		const uint8_t idx = (i & 1u) ? ((b >> 4) & 0x0Fu) : (b & 0x0Fu);
		const size_t pi = static_cast<size_t>(idx) * 4u;
		uint8_t* dst = out.data() + i * 4u;
		dst[0] = palette[pi + 0];
		dst[1] = palette[pi + 1];
		dst[2] = palette[pi + 2];
		dst[3] = palette[pi + 3];
	}
}

void decodeBgra32(const uint8_t* src, size_t len, uint32_t w, uint32_t h, bool hasAlphaChan, std::vector<uint8_t>& out)
{
	const size_t pixels = static_cast<size_t>(w) * h;
	out.assign(pixels * 4u, 0);
	const size_t count = std::min(pixels, len / 4u);
	for (size_t i = 0; i < count; ++i) {
		const uint8_t* s = src + i * 4u;
		uint8_t* dst = out.data() + i * 4u;
		dst[0] = s[2]; // R
		dst[1] = s[1]; // G
		dst[2] = s[0]; // B
		dst[3] = hasAlphaChan ? s[3] : 255;
	}
}

void decode16Bit(const uint8_t* src, size_t len, uint32_t w, uint32_t h, uint32_t formatMask, std::vector<uint8_t>& out)
{
	const size_t pixels = static_cast<size_t>(w) * h;
	out.assign(pixels * 4u, 0);
	const size_t count = std::min(pixels, len / 2u);
	for (size_t i = 0; i < count; ++i) {
		const uint16_t v = readLE16(src + i * 2u);
		uint8_t* dst = out.data() + i * 4u;
		if (formatMask == 0x0300u) {
			// ARGB4444
			const uint32_t a = (v >> 12) & 0xFu;
			const uint32_t r = (v >> 8) & 0xFu;
			const uint32_t g = (v >> 4) & 0xFu;
			const uint32_t b = v & 0xFu;
			dst[0] = static_cast<uint8_t>((r << 4) | r);
			dst[1] = static_cast<uint8_t>((g << 4) | g);
			dst[2] = static_cast<uint8_t>((b << 4) | b);
			dst[3] = static_cast<uint8_t>((a << 4) | a);
		} else if (formatMask == 0x0100u) {
			// ARGB1555
			const uint32_t a = (v & 0x8000u) ? 255u : 0u;
			const uint32_t r = (v >> 10) & 0x1Fu;
			const uint32_t g = (v >> 5) & 0x1Fu;
			const uint32_t b = v & 0x1Fu;
			dst[0] = static_cast<uint8_t>((r << 3) | (r >> 2));
			dst[1] = static_cast<uint8_t>((g << 3) | (g >> 2));
			dst[2] = static_cast<uint8_t>((b << 3) | (b >> 2));
			dst[3] = static_cast<uint8_t>(a);
		} else {
			// RGB565 (0x0200 or default)
			rgb565ToRgba(v, dst);
		}
	}
}

uint32_t nearestPow2Clamped(uint32_t v, uint32_t maxDim) noexcept
{
	if (v <= 1u) return 1u;
	uint32_t p = 1u;
	while (p < v && p < maxDim) {
		p <<= 1u;
	}
	if (p > maxDim) {
		p = maxDim;
	}
	// If v is closer to p/2 than p (e.g. 300 -> 256 rather than 512), pick p/2 to save memory.
	if (p > 1u && (p - v) > (v - (p >> 1u))) {
		p >>= 1u;
	}
	return std::max(1u, p);
}

void resampleBilinear(const std::vector<uint8_t>& src, uint32_t sw, uint32_t sh,
	uint32_t dw, uint32_t dh, std::vector<uint8_t>& dst)
{
	dst.resize(static_cast<size_t>(dw) * dh * 4u);
	const float scaleX = static_cast<float>(sw) / static_cast<float>(dw);
	const float scaleY = static_cast<float>(sh) / static_cast<float>(dh);

	for (uint32_t y = 0; y < dh; ++y) {
		const float fy = (static_cast<float>(y) + 0.5f) * scaleY - 0.5f;
		const int y0 = std::clamp(static_cast<int>(std::floor(fy)), 0, static_cast<int>(sh) - 1);
		const int y1 = std::min(y0 + 1, static_cast<int>(sh) - 1);
		const float wy = std::clamp(fy - static_cast<float>(y0), 0.0f, 1.0f);
		for (uint32_t x = 0; x < dw; ++x) {
			const float fx = (static_cast<float>(x) + 0.5f) * scaleX - 0.5f;
			const int x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, static_cast<int>(sw) - 1);
			const int x1 = std::min(x0 + 1, static_cast<int>(sw) - 1);
			const float wx = std::clamp(fx - static_cast<float>(x0), 0.0f, 1.0f);

			const uint8_t* p00 = src.data() + (static_cast<size_t>(y0) * sw + x0) * 4u;
			const uint8_t* p10 = src.data() + (static_cast<size_t>(y0) * sw + x1) * 4u;
			const uint8_t* p01 = src.data() + (static_cast<size_t>(y1) * sw + x0) * 4u;
			const uint8_t* p11 = src.data() + (static_cast<size_t>(y1) * sw + x1) * 4u;

			uint8_t* out = dst.data() + (static_cast<size_t>(y) * dw + x) * 4u;
			for (int c = 0; c < 4; ++c) {
				const float top = p00[c] * (1.0f - wx) + p10[c] * wx;
				const float bot = p01[c] * (1.0f - wx) + p11[c] * wx;
				const float val = top * (1.0f - wy) + bot * wy;
				out[c] = static_cast<uint8_t>(std::clamp(static_cast<int>(val + 0.5f), 0, 255));
			}
		}
	}
}

bool parseRasterStruct(const uint8_t* data, size_t start, size_t end,
	bool decodePixels, uint32_t maxDim, bool normalizePow2,
	size_t maxTotalBytes, size_t& accumDecodedBytes,
	DecodedTexture& outTex, TxdParseFailure& outFailure, std::string& outErr)
{
	if (end < start || (end - start) < 92u) {
		outErr = "TextureNative struct truncated (<92B)";
		return false;
	}
	const uint32_t platformId = readLE32(data + start);
	if (platformId != 8u && platformId != 9u) {
		outErr = "Unsupported TXD platform ID (expected D3D8=8 or D3D9=9)";
		return false;
	}

	const uint32_t filterFlags = readLE32(data + start + 4);
	std::string name = readFixedAscii(data + start + 8, 32);
	std::string maskName = readFixedAscii(data + start + 40, 32);
	if (name.empty()) {
		outErr = "TextureNative has empty name";
		return false;
	}

	const uint32_t rasterFormat = readLE32(data + start + 72);
	const uint32_t d3dFmtOrAlpha = readLE32(data + start + 76);
	const uint16_t width = readLE16(data + start + 80);
	const uint16_t height = readLE16(data + start + 82);
	const uint8_t depth = data[start + 84];
	const uint8_t mipCount = data[start + 85];
	const uint8_t flagsOrComp = data[start + 87];

	if (width == 0 || height == 0 || width > kMaxInputDim || height > kMaxInputDim) {
		outErr = "Texture dimensions out of range";
		return false;
	}
	if (mipCount > 16u) {
		outErr = "Texture mipmap count out of range";
		return false;
	}

	size_t cur = start + 88u;
	const uint32_t formatMask = rasterFormat & 0x0F00u;
	const bool isPal8 = (rasterFormat & 0x2000u) != 0;
	const bool isPal4 = (rasterFormat & 0x4000u) != 0;

	const uint8_t* palette = nullptr;
	if (isPal8) {
		if (cur + 1024u > end) {
			outErr = "PAL8 palette truncated";
			return false;
		}
		palette = data + cur;
		cur += 1024u;
	} else if (isPal4) {
		if (cur + 64u > end) {
			outErr = "PAL4 palette truncated";
			return false;
		}
		palette = data + cur;
		cur += 64u;
	}

	if (cur + 4u > end) {
		outErr = "Missing mip0 size field";
		return false;
	}
	const uint32_t rawMip0Size = readLE32(data + cur);
	cur += 4u;
	if (rawMip0Size == 0 || rawMip0Size > (end - cur)) {
		outErr = "Invalid or truncated mip0 payload";
		return false;
	}

	int dxtMode = 0;
	if (platformId == 9u) {
		if (d3dFmtOrAlpha == kFourCcDxt1) dxtMode = 1;
		else if (d3dFmtOrAlpha == kFourCcDxt3) dxtMode = 3;
		else if (d3dFmtOrAlpha == kFourCcDxt5) dxtMode = 5;
	} else {
		if (flagsOrComp == 1u) dxtMode = 1;
		else if (flagsOrComp == 3u) dxtMode = 3;
		else if (flagsOrComp == 5u) dxtMode = 5;
	}

	const bool supported = (dxtMode == 1 || dxtMode == 3 || dxtMode == 5) ||
	                       (palette != nullptr && (isPal8 || isPal4)) ||
	                       (depth == 32u || formatMask == 0x0500u || formatMask == 0x0600u) ||
	                       (depth == 16u);
	if (!supported) {
		outErr = "Unsupported PC TXD pixel format";
		return false;
	}

	// Enforce exact minimum mip0 payload length for the declared pixel format & dimensions
	// so truncated mip0 buffers are rejected at cache commit time instead of silently decoding black.
	size_t requiredMip0Bytes = 0;
	if (dxtMode == 1) {
		const size_t bw = std::max<size_t>(1u, (static_cast<size_t>(width) + 3u) >> 2);
		const size_t bh = std::max<size_t>(1u, (static_cast<size_t>(height) + 3u) >> 2);
		requiredMip0Bytes = bw * bh * 8u;
	} else if (dxtMode == 3 || dxtMode == 5) {
		const size_t bw = std::max<size_t>(1u, (static_cast<size_t>(width) + 3u) >> 2);
		const size_t bh = std::max<size_t>(1u, (static_cast<size_t>(height) + 3u) >> 2);
		requiredMip0Bytes = bw * bh * 16u;
	} else if (palette != nullptr && isPal8) {
		requiredMip0Bytes = static_cast<size_t>(width) * static_cast<size_t>(height);
	} else if (palette != nullptr && isPal4) {
		requiredMip0Bytes = (static_cast<size_t>(width) * static_cast<size_t>(height) + 1u) >> 1;
	} else if (depth == 32u || formatMask == 0x0500u || formatMask == 0x0600u) {
		requiredMip0Bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
	} else {
		requiredMip0Bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 2u;
	}
	if (rawMip0Size < requiredMip0Bytes) {
		outErr = "TXD mip0 payload shorter than required by pixel format and dimensions";
		return false;
	}

	const uint32_t clampedMax = std::clamp(maxDim, 16u, 2048u);
	const bool isPow2 = ((width & (width - 1u)) == 0) && ((height & (height - 1u)) == 0);
	uint32_t targetW = width;
	uint32_t targetH = height;
	if (width > clampedMax || height > clampedMax || (normalizePow2 && !isPow2)) {
		targetW = normalizePow2 ? nearestPow2Clamped(width, clampedMax) : std::min<uint32_t>(width, clampedMax);
		targetH = normalizePow2 ? nearestPow2Clamped(height, clampedMax) : std::min<uint32_t>(height, clampedMax);
	}
	const size_t projectedBytes = static_cast<size_t>(targetW) * static_cast<size_t>(targetH) * 4u;
	if (projectedBytes > maxTotalBytes || accumDecodedBytes > maxTotalBytes - projectedBytes) {
		outFailure = TxdParseFailure::BudgetExceeded;
		outErr = "TXD cumulative decoded pixel memory exceeds safety budget";
		return false;
	}
	accumDecodedBytes += projectedBytes;

	if (!decodePixels) {
		outTex.name = std::move(name);
		outTex.maskName = std::move(maskName);
		outTex.width = targetW;
		outTex.height = targetH;
		outTex.filterFlags = filterFlags;
		return true;
	}

	bool headerHasAlpha = (platformId == 9u) ? ((flagsOrComp & 0x01u) != 0) : (d3dFmtOrAlpha != 0);
	std::vector<uint8_t> rgba;

	if (dxtMode == 1) {
		decodeDxt1(data + cur, rawMip0Size, width, height, headerHasAlpha, rgba);
	} else if (dxtMode == 3) {
		decodeDxt3(data + cur, rawMip0Size, width, height, rgba);
		headerHasAlpha = true;
	} else if (dxtMode == 5) {
		decodeDxt5(data + cur, rawMip0Size, width, height, rgba);
		headerHasAlpha = true;
	} else if (palette != nullptr && isPal8) {
		decodePal8(data + cur, rawMip0Size, palette, width, height, rgba);
	} else if (palette != nullptr && isPal4) {
		decodePal4(data + cur, rawMip0Size, palette, width, height, rgba);
	} else if (depth == 32u || formatMask == 0x0500u || formatMask == 0x0600u) {
		const bool hasAlphaChan = (formatMask == 0x0500u) || (platformId == 9u && d3dFmtOrAlpha == 21u) || headerHasAlpha;
		decodeBgra32(data + cur, rawMip0Size, width, height, hasAlphaChan, rgba);
	} else {
		decode16Bit(data + cur, rawMip0Size, width, height, formatMask, rgba);
	}

	uint32_t outW = width;
	uint32_t outH = height;
	if (targetW != outW || targetH != outH) {
		std::vector<uint8_t> resampled;
		resampleBilinear(rgba, outW, outH, targetW, targetH, resampled);
		rgba.swap(resampled);
		outW = targetW;
		outH = targetH;
	}

	bool pixelAlpha = false;
	for (size_t i = 3; i < rgba.size(); i += 4u) {
		if (rgba[i] < 250u) {
			pixelAlpha = true;
			break;
		}
	}

	outTex.name = std::move(name);
	outTex.maskName = std::move(maskName);
	outTex.width = outW;
	outTex.height = outH;
	outTex.filterFlags = filterFlags;
	outTex.hasAlpha = pixelAlpha || (headerHasAlpha && dxtMode != 1);
	outTex.rgba = std::move(rgba);
	return true;
}

TxdParseResult parseTxdInternal(const uint8_t* data, size_t size, bool decodePixels, uint32_t maxDim, bool normalizePow2, size_t maxTotalBytes,
	const std::function<bool(DecodedTexture&&)>* streamCallback = nullptr)
{
	TxdParseResult res{};
	res.failure = TxdParseFailure::Malformed;
	if (!data || size < 24u) {
		res.error = "TXD buffer too small";
		return res;
	}

	const uint32_t rootType = readLE32(data);
	const uint32_t rootSize = readLE32(data + 4);
	if (rootType != kRwTexDictionary) {
		res.error = "Invalid TXD root chunk (expected 0x16)";
		return res;
	}
	if (rootSize > size - 12u) {
		res.error = "TXD root chunk length exceeds file size";
		return res;
	}

	const size_t rootEnd = 12u + rootSize;
	size_t off = 12u;
	bool sawHeaderStruct = false;
	size_t accumDecodedBytes = 0;
	size_t parsedTextureCount = 0;

	while (off + 12u <= rootEnd) {
		const uint32_t type = readLE32(data + off);
		const uint32_t len = readLE32(data + off + 4);
		const size_t childStart = off + 12u;
		if (len > rootEnd - childStart) {
			res.error = "TXD child chunk length overflows root";
			return res;
		}
		const size_t childEnd = childStart + len;

		if (type == kRwStruct && !sawHeaderStruct) {
			if (len < 4u) {
				res.error = "TXD header struct truncated";
				return res;
			}
			res.headerTextureCount = readLE16(data + childStart);
			if (res.headerTextureCount > kMaxTexturesPerTxd) {
				res.error = "TXD texture count exceeds limit (1024)";
				return res;
			}
			sawHeaderStruct = true;
		} else if (type == kRwTextureNative) {
			if (parsedTextureCount >= kMaxTexturesPerTxd) {
				res.error = "TXD contains more than 1024 textures";
				return res;
			}
			// Locate inner RW_STRUCT inside TextureNative
			size_t sub = childStart;
			bool foundStruct = false;
			while (sub + 12u <= childEnd) {
				const uint32_t subType = readLE32(data + sub);
				const uint32_t subLen = readLE32(data + sub + 4);
				const size_t payStart = sub + 12u;
				if (subLen > childEnd - payStart) {
					res.error = "TextureNative sub-chunk overflows parent";
					return res;
				}
				const size_t payEnd = payStart + subLen;
				if (subType == kRwStruct) {
					DecodedTexture tex{};
					TxdParseFailure texFail = TxdParseFailure::Malformed;
					std::string texErr;
					if (!parseRasterStruct(data, payStart, payEnd, decodePixels, maxDim, normalizePow2,
							maxTotalBytes, accumDecodedBytes, tex, texFail, texErr)) {
						res.failure = texFail;
						res.error = std::move(texErr);
						return res;
					}
					++parsedTextureCount;
					if (streamCallback && *streamCallback) {
						if (!(*streamCallback)(std::move(tex))) {
							res.failure = TxdParseFailure::Malformed;
							res.error = "Texture consumer aborted";
							return res;
						}
					} else {
						res.textures.push_back(std::move(tex));
					}
					foundStruct = true;
					break;
				}
				sub = payEnd;
			}
			if (!foundStruct) {
				res.error = "TextureNative missing inner struct";
				return res;
			}
		}

		off = childEnd;
	}

	if (!sawHeaderStruct) {
		res.failure = TxdParseFailure::Malformed;
		res.error = "TXD missing header struct";
		return res;
	}
	res.totalDecodedBytes = accumDecodedBytes;
	res.failure = TxdParseFailure::None;
	res.ok = true;
	return res;
}

} // namespace

TxdParseResult decodeTxdBytes(const uint8_t* data, size_t size, uint32_t maxDim, bool normalizePow2, size_t maxTotalBytes)
{
	return parseTxdInternal(data, size, true, maxDim, normalizePow2, maxTotalBytes, nullptr);
}

bool decodeTxdStreaming(const uint8_t* data, size_t size, uint32_t maxDim, bool normalizePow2,
	size_t maxTotalBytes, const std::function<bool(DecodedTexture&&)>& onTexture,
	size_t* outTotalDecodedBytes, TxdParseFailure* outFailure, std::string* outError)
{
	TxdParseResult res = parseTxdInternal(data, size, true, maxDim, normalizePow2, maxTotalBytes, &onTexture);
	if (outTotalDecodedBytes) *outTotalDecodedBytes = res.totalDecodedBytes;
	if (!res.ok) {
		if (outError) *outError = res.error;
		if (outFailure) *outFailure = res.failure;
	} else if (outFailure) {
		*outFailure = TxdParseFailure::None;
	}
	return res.ok;
}

bool validateTxdBytes(const uint8_t* data, size_t size, std::string* outError, size_t maxTotalBytes,
	TxdParseFailure* outFailure, size_t* outProjectedBytes)
{
	TxdParseResult res = parseTxdInternal(data, size, false, 2048u, true, maxTotalBytes);
	if (outProjectedBytes) *outProjectedBytes = res.totalDecodedBytes;
	if (!res.ok) {
		if (outError) *outError = res.error;
		if (outFailure) *outFailure = res.failure;
	} else if (outFailure) {
		*outFailure = TxdParseFailure::None;
	}
	return res.ok;
}

} // namespace artwork
