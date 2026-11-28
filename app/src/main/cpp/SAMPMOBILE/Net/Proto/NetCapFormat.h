#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

// On-disk layout of a packet capture (.ncap), little-endian:
//   header  : magic[8] "SAMPNCAP", u16 formatVersion, u16 reserved, u32 netgameVersion, i64 unixTime
//   records : u32 timeMs, u8 dir, u8 kind, u16 id, u32 bitLength, then (bitLength + 7) / 8 payload bytes
namespace netcap {

constexpr char kMagic[8] = {'S', 'A', 'M', 'P', 'N', 'C', 'A', 'P'};
constexpr uint16_t kFormatVersion = 1;
constexpr size_t kHeaderSize = 24;
constexpr size_t kRecordHeaderSize = 12;
constexpr uint32_t kMaxRecordBits = 8u * 1024u * 1024u;

enum Dir : uint8_t { kIn = 0, kOut = 1 };
enum Kind : uint8_t { kRpc = 0, kPacket = 1 };

struct Header {
	uint16_t formatVersion = 0;
	uint32_t netgameVersion = 0;
	int64_t unixTime = 0;
};

struct Record {
	uint32_t timeMs = 0;
	uint8_t dir = 0;
	uint8_t kind = 0;
	uint16_t id = 0;
	uint32_t bitLength = 0;
	std::vector<uint8_t> data;
};

inline void putLE(uint8_t* p, uint64_t v, int bytes)
{
	for (int i = 0; i < bytes; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}

inline uint64_t getLE(const uint8_t* p, int bytes)
{
	uint64_t v = 0;
	for (int i = 0; i < bytes; ++i) v |= static_cast<uint64_t>(p[i]) << (8 * i);
	return v;
}

inline bool writeHeader(FILE* f, uint32_t netgameVersion, int64_t unixTime)
{
	uint8_t h[kHeaderSize] = {};
	memcpy(h, kMagic, sizeof(kMagic));
	putLE(h + 8, kFormatVersion, 2);
	putLE(h + 12, netgameVersion, 4);
	putLE(h + 16, static_cast<uint64_t>(unixTime), 8);
	return fwrite(h, 1, sizeof(h), f) == sizeof(h);
}

inline bool writeRecord(FILE* f, uint32_t timeMs, uint8_t dir, uint8_t kind, uint16_t id, const uint8_t* data, uint32_t bitLength)
{
	if (bitLength > kMaxRecordBits || (bitLength && !data)) return false;
	uint8_t h[kRecordHeaderSize];
	putLE(h, timeMs, 4);
	h[4] = dir;
	h[5] = kind;
	putLE(h + 6, id, 2);
	putLE(h + 8, bitLength, 4);
	if (fwrite(h, 1, sizeof(h), f) != sizeof(h)) return false;
	const size_t bytes = (bitLength + 7) / 8;
	return bytes == 0 || fwrite(data, 1, bytes, f) == bytes;
}

inline bool readHeader(FILE* f, Header& out)
{
	uint8_t h[kHeaderSize];
	if (fread(h, 1, sizeof(h), f) != sizeof(h) || memcmp(h, kMagic, sizeof(kMagic)) != 0) return false;
	out.formatVersion = static_cast<uint16_t>(getLE(h + 8, 2));
	out.netgameVersion = static_cast<uint32_t>(getLE(h + 12, 4));
	out.unixTime = static_cast<int64_t>(getLE(h + 16, 8));
	return out.formatVersion == kFormatVersion;
}

// Returns false at end of file or on a truncated trailing record.
inline bool readRecord(FILE* f, Record& out)
{
	uint8_t h[kRecordHeaderSize];
	if (fread(h, 1, sizeof(h), f) != sizeof(h)) return false;
	out.timeMs = static_cast<uint32_t>(getLE(h, 4));
	out.dir = h[4];
	out.kind = h[5];
	out.id = static_cast<uint16_t>(getLE(h + 6, 2));
	out.bitLength = static_cast<uint32_t>(getLE(h + 8, 4));
	if (out.bitLength > kMaxRecordBits) return false;
	out.data.resize((out.bitLength + 7) / 8);
	return out.data.empty() || fread(out.data.data(), 1, out.data.size(), f) == out.data.size();
}

}
