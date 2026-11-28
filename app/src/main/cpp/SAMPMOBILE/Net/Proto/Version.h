#pragma once

#include <cstdint>

namespace proto {

enum class ProtoVersion : uint8_t {
	V037 = 0,
	V03DL = 1,
};

constexpr uint32_t kNetgame037 = 0xFD9;
constexpr uint32_t kNetgame03DL = 0xFDE;

constexpr ProtoVersion versionFromNetgame(uint32_t netgameVersion)
{
	return netgameVersion == kNetgame03DL ? ProtoVersion::V03DL : ProtoVersion::V037;
}

enum VersionMask : uint8_t {
	kVer037 = 1u << 0,
	kVer03DL = 1u << 1,
	kVerAll = kVer037 | kVer03DL,
};

constexpr bool versionMatches(uint8_t mask, ProtoVersion v)
{
	return (mask & (v == ProtoVersion::V03DL ? kVer03DL : kVer037)) != 0;
}

} // namespace proto
