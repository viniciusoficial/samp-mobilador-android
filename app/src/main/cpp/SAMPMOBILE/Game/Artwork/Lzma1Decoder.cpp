#include "Lzma1Decoder.h"

#include <algorithm>
#include <array>

namespace artwork {

namespace {

// SA-MP 0.3.DL fixed LZMA1 properties: {0x5D, 0x00, 0x00, 0x01, 0x00}
// 0x5D = 93 -> lc = 3, lp = 0, pb = 2 (pb*45 + lp*9 + lc = 90 + 0 + 3 = 93)
constexpr uint32_t kLc = 3u;
constexpr uint32_t kLp = 0u;
constexpr uint32_t kPb = 2u;
constexpr uint32_t kPosStatesMax = 1u << kPb; // 4
constexpr uint32_t kPosStateMask = kPosStatesMax - 1u;
constexpr uint32_t kDictSize = 65536u;
constexpr size_t kMaxChunkBytes = 4096u;

constexpr uint32_t kNumBitModelTotalBits = 11u;
constexpr uint32_t kBitModelTotal = 1u << kNumBitModelTotalBits; // 2048
constexpr uint16_t kProbInit = kBitModelTotal / 2u;              // 1024
constexpr uint32_t kNumMoveBits = 5u;
constexpr uint32_t kTopValue = 1u << 24u;

constexpr uint32_t kNumStates = 12u;
constexpr uint32_t kNumLenToPosStates = 4u;
constexpr uint32_t kStartPosModelIndex = 4u;
constexpr uint32_t kEndPosModelIndex = 14u;
constexpr uint32_t kNumFullDistances = 1u << (kEndPosModelIndex >> 1u); // 128
constexpr uint32_t kNumAlignBits = 4u;
constexpr uint32_t kMatchMinLen = 2u;

class RangeDecoder {
public:
	RangeDecoder(const uint8_t* data, size_t len) noexcept
		: m_data(data), m_len(len) {}

	bool init() noexcept
	{
		if (!m_data || m_len < 5u || m_data[0] != 0u) {
			return false;
		}
		m_code = (static_cast<uint32_t>(m_data[1]) << 24u)
			| (static_cast<uint32_t>(m_data[2]) << 16u)
			| (static_cast<uint32_t>(m_data[3]) << 8u)
			| static_cast<uint32_t>(m_data[4]);
		m_pos = 5u;
		m_range = 0xFFFFFFFFu;
		return true;
	}

	uint32_t decodeBit(uint16_t& prob) noexcept
	{
		const uint32_t bound = (m_range >> kNumBitModelTotalBits) * prob;
		uint32_t bit = 0u;
		if (m_code < bound) {
			m_range = bound;
			prob = static_cast<uint16_t>(prob + ((kBitModelTotal - prob) >> kNumMoveBits));
			bit = 0u;
		} else {
			m_range -= bound;
			m_code -= bound;
			prob = static_cast<uint16_t>(prob - (prob >> kNumMoveBits));
			bit = 1u;
		}
		normalize();
		return bit;
	}

	uint32_t decodeDirectBits(uint32_t numBits) noexcept
	{
		uint32_t res = 0u;
		for (uint32_t i = 0; i < numBits; ++i) {
			m_range >>= 1u;
			const uint32_t t = (m_code - m_range) >> 31u;
			m_code -= m_range & (t - 1u);
			res = (res << 1u) | (1u - t);
			normalize();
		}
		return res;
	}

	uint32_t decodeBitTree(uint16_t* probs, uint32_t numBits) noexcept
	{
		uint32_t m = 1u;
		for (uint32_t i = 0; i < numBits; ++i) {
			m = (m << 1u) | decodeBit(probs[m]);
		}
		return m - (1u << numBits);
	}

	uint32_t decodeReverseBitTree(uint16_t* probs, uint32_t numBits) noexcept
	{
		uint32_t m = 1u;
		uint32_t symbol = 0u;
		for (uint32_t i = 0; i < numBits; ++i) {
			const uint32_t bit = decodeBit(probs[m]);
			m = (m << 1u) | bit;
			symbol |= (bit << i);
		}
		return symbol;
	}

	bool overflowed() const noexcept { return m_overflowed; }

private:
	void normalize() noexcept
	{
		if (m_range < kTopValue) {
			m_range <<= 8u;
			if (m_pos < m_len) {
				m_code = (m_code << 8u) | m_data[m_pos++];
			} else {
				// Past end of compressed input while decoder still demands bits.
				m_code <<= 8u;
				m_overflowed = true;
			}
		}
	}

	const uint8_t* m_data = nullptr;
	size_t m_len = 0;
	size_t m_pos = 0;
	uint32_t m_range = 0xFFFFFFFFu;
	uint32_t m_code = 0u;
	bool m_overflowed = false;
};

struct LenDecoder {
	uint16_t choice = kProbInit;
	uint16_t choice2 = kProbInit;
	std::array<std::array<uint16_t, 1u << 3u>, kPosStatesMax> low{};
	std::array<std::array<uint16_t, 1u << 3u>, kPosStatesMax> mid{};
	std::array<uint16_t, 1u << 8u> high{};

	void init() noexcept
	{
		choice = kProbInit;
		choice2 = kProbInit;
		for (auto& arr : low) arr.fill(kProbInit);
		for (auto& arr : mid) arr.fill(kProbInit);
		high.fill(kProbInit);
	}

	uint32_t decode(RangeDecoder& rc, uint32_t posState) noexcept
	{
		if (rc.decodeBit(choice) == 0u) {
			return rc.decodeBitTree(low[posState].data(), 3u);
		}
		if (rc.decodeBit(choice2) == 0u) {
			return 8u + rc.decodeBitTree(mid[posState].data(), 3u);
		}
		return 16u + rc.decodeBitTree(high.data(), 8u);
	}
};

} // namespace

bool decodeLzma1Chunk03dl(
	const uint8_t* src, size_t srcLen,
	uint8_t* dst, size_t expectedUnpackedLen,
	size_t& outDecodedLen) noexcept
{
	outDecodedLen = 0u;
	if (!src || !dst || expectedUnpackedLen == 0u || expectedUnpackedLen > kMaxChunkBytes || srcLen < 5u || srcLen > kMaxChunkBytes) {
		return false;
	}

	RangeDecoder rc(src, srcLen);
	if (!rc.init()) {
		return false;
	}

	std::array<uint16_t, kNumStates << kPb> isMatch{};
	std::array<uint16_t, kNumStates> isRep{};
	std::array<uint16_t, kNumStates> isRepG0{};
	std::array<uint16_t, kNumStates> isRepG1{};
	std::array<uint16_t, kNumStates> isRepG2{};
	std::array<uint16_t, kNumStates << kPb> isRep0Long{};
	std::array<std::array<uint16_t, 1u << 6u>, kNumLenToPosStates> posSlotDecoder{};
	// Extra slot at index 0 so reverse bit-tree indexing (m >= 1) never forms an out-of-bounds pointer.
	std::array<uint16_t, 1u + kNumFullDistances - kEndPosModelIndex> posDecoders{};
	std::array<uint16_t, 1u << kNumAlignBits> alignDecoder{};
	std::array<uint16_t, 0x300u << (kLc + kLp)> litProbs{};

	isMatch.fill(kProbInit);
	isRep.fill(kProbInit);
	isRepG0.fill(kProbInit);
	isRepG1.fill(kProbInit);
	isRepG2.fill(kProbInit);
	isRep0Long.fill(kProbInit);
	for (auto& arr : posSlotDecoder) arr.fill(kProbInit);
	posDecoders.fill(kProbInit);
	alignDecoder.fill(kProbInit);
	litProbs.fill(kProbInit);

	LenDecoder lenDecoder;
	LenDecoder repLenDecoder;
	lenDecoder.init();
	repLenDecoder.init();

	uint32_t state = 0u;
	uint32_t rep0 = 0u;
	uint32_t rep1 = 0u;
	uint32_t rep2 = 0u;
	uint32_t rep3 = 0u;
	size_t outPos = 0u;

	while (outPos < expectedUnpackedLen) {
		const uint32_t posState = static_cast<uint32_t>(outPos) & kPosStateMask;
		if (rc.decodeBit(isMatch[(state << kPb) + posState]) == 0u) {
			// Literal byte
			const uint8_t prevByte = (outPos > 0u) ? dst[outPos - 1u] : 0u;
			const uint32_t litState = prevByte >> (8u - kLc); // lp == 0
			uint16_t* probs = litProbs.data() + (static_cast<size_t>(litState) * 0x300u);

			uint32_t symbol = 1u;
			if (state >= 7u) {
				if (rep0 >= outPos) {
					return false;
				}
				uint32_t matchByte = dst[outPos - 1u - rep0];
				do {
					const uint32_t matchBit = (matchByte >> 7u) & 1u;
					matchByte <<= 1u;
					const uint32_t bit = rc.decodeBit(probs[((1u + matchBit) << 8u) + symbol]);
					symbol = (symbol << 1u) | bit;
					if (matchBit != bit) {
						break;
					}
				} while (symbol < 0x100u);
			}
			while (symbol < 0x100u) {
				symbol = (symbol << 1u) | rc.decodeBit(probs[symbol]);
			}
			dst[outPos++] = static_cast<uint8_t>(symbol - 0x100u);
			state = (state < 4u) ? 0u : ((state < 10u) ? (state - 3u) : (state - 6u));
		} else {
			uint32_t len = 0u;
			if (rc.decodeBit(isRep[state]) == 1u) {
				if (rc.decodeBit(isRepG0[state]) == 0u) {
					if (rc.decodeBit(isRep0Long[(state << kPb) + posState]) == 0u) {
						state = (state < 7u) ? 9u : 11u;
						len = 1u;
					}
				} else {
					uint32_t dist = 0u;
					if (rc.decodeBit(isRepG1[state]) == 0u) {
						dist = rep1;
					} else {
						if (rc.decodeBit(isRepG2[state]) == 0u) {
							dist = rep2;
						} else {
							dist = rep3;
							rep3 = rep2;
						}
						rep2 = rep1;
					}
					rep1 = rep0;
					rep0 = dist;
				}
				if (len == 0u) {
					len = repLenDecoder.decode(rc, posState) + kMatchMinLen;
					state = (state < 7u) ? 8u : 11u;
				}
			} else {
				rep3 = rep2;
				rep2 = rep1;
				rep1 = rep0;
				len = kMatchMinLen + lenDecoder.decode(rc, posState);
				state = (state < 7u) ? 7u : 10u;

				const uint32_t lenToPosState = (len - kMatchMinLen < kNumLenToPosStates)
					? (len - kMatchMinLen)
					: (kNumLenToPosStates - 1u);
				const uint32_t posSlot = rc.decodeBitTree(posSlotDecoder[lenToPosState].data(), 6u);
				if (posSlot >= kStartPosModelIndex) {
					const uint32_t numDirectBits = (posSlot >> 1u) - 1u;
					rep0 = (2u | (posSlot & 1u)) << numDirectBits;
					if (posSlot < kEndPosModelIndex) {
						const uint32_t baseIdx = rep0 - posSlot;
						const uint32_t treeSpan = (1u << numDirectBits);
						if (baseIdx + treeSpan > posDecoders.size()) {
							return false;
						}
						rep0 += rc.decodeReverseBitTree(posDecoders.data() + baseIdx, numDirectBits);
					} else {
						rep0 += rc.decodeDirectBits(numDirectBits - kNumAlignBits) << kNumAlignBits;
						rep0 += rc.decodeReverseBitTree(alignDecoder.data(), kNumAlignBits);
					}
				} else {
					rep0 = posSlot;
				}
				// End-of-stream marker (0xFFFFFFFF)
				if (rep0 == 0xFFFFFFFFu) {
					break;
				}
			}

			if (rep0 >= outPos || rep0 >= kDictSize) {
				return false;
			}
			if (outPos + len > expectedUnpackedLen) {
				return false;
			}
			for (uint32_t i = 0; i < len; ++i) {
				dst[outPos] = dst[outPos - 1u - rep0];
				++outPos;
			}
		}

		if (rc.overflowed()) {
			return false;
		}
	}

	outDecodedLen = outPos;
	return outPos == expectedUnpackedLen;
}

} // namespace artwork
