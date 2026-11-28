#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace settings_contract {

struct SampVersionSpec {
	const char* id;
	int netgameVersion;
};

inline constexpr SampVersionSpec kSampVersions[] = {
	{"0.3.7",     0xFD9},
	{"0.3.7-R4",  0xFD9},
	{"0.3.7-R5",  0xFD9},
	{"0.3.DL-R1", 0xFDE},
};

inline bool iequalsAscii(const char* a, const char* b)
{
	if (!a || !b) return false;
	while (*a && *b) {
		if (std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b))) {
			return false;
		}
		++a;
		++b;
	}
	return *a == '\0' && *b == '\0';
}

inline SampVersionSpec parseSampVersion(const char* id)
{
	if (id) {
		for (const auto& spec : kSampVersions) {
			if (iequalsAscii(id, spec.id)) {
				return spec;
			}
		}
	}
	return kSampVersions[0];
}

inline SampVersionSpec parseSampVersionLegacyInt(int idx)
{
	constexpr int count = static_cast<int>(sizeof(kSampVersions) / sizeof(kSampVersions[0]));
	if (idx < 0 || idx >= count) {
		return kSampVersions[0];
	}
	return kSampVersions[idx];
}

struct EnumEntry {
	const char* id;
	int value;
};

inline constexpr EnumEntry kAspectRatios[] = {
	{"auto",  0},
	{"16:9",  1},
	{"20:9",  2},
	{"21:9",  3},
	{"16:10", 4},
	{"4:3",   5},
};

inline constexpr EnumEntry kCrosshairStyles[] = {
	{"default",   0},
	{"cross",     1},
	{"dot",       2},
	{"cross_dot", 3},
	{"hidden",    4},
};

inline constexpr EnumEntry kFontFamilies[] = {
	{"chinese",    0},
	{"arial_bold", 1},
	{"custom",     2},
};

inline constexpr EnumEntry kScreenshotModes[] = {
	{"hud",  0},
	{"game", 1},
};

inline constexpr EnumEntry kVoiceMicModes[] = {
	{"ptt",    0},
	{"toggle", 1},
};

template <size_t N>
inline int parseEnumString(const EnumEntry (&table)[N], const char* id, int defaultValue)
{
	if (id) {
		for (const auto& e : table) {
			if (iequalsAscii(id, e.id)) {
				return e.value;
			}
		}
	}
	return defaultValue;
}

template <size_t N>
inline int parseEnumLegacyInt(const EnumEntry (&table)[N], int value, int defaultValue)
{
	for (const auto& e : table) {
		if (e.value == value) {
			return e.value;
		}
	}
	return defaultValue;
}

template <size_t N>
inline const char* enumIdForValue(const EnumEntry (&table)[N], int value, const char* defaultId)
{
	for (const auto& e : table) {
		if (e.value == value) {
			return e.id;
		}
	}
	return defaultId;
}

inline int clampInt(int value, int minValue, int maxValue)
{
	return std::max(minValue, std::min(maxValue, value));
}

inline float clampFloat(float value, float minValue, float maxValue)
{
	return std::max(minValue, std::min(maxValue, value));
}

inline int parseAspectRatio(const char* id) { return parseEnumString(kAspectRatios, id, 0); }
inline int parseAspectRatioLegacyInt(int v) { return parseEnumLegacyInt(kAspectRatios, v, 0); }

inline int parseCrosshairStyle(const char* id) { return parseEnumString(kCrosshairStyles, id, 0); }
inline int parseCrosshairStyleLegacyInt(int v) { return parseEnumLegacyInt(kCrosshairStyles, v, 0); }

inline int parseFontFamily(const char* id) { return parseEnumString(kFontFamilies, id, 0); }
inline int parseFontFamilyLegacyInt(int v) { return parseEnumLegacyInt(kFontFamilies, v, 0); }

inline int parseScreenshotMode(const char* id) { return parseEnumString(kScreenshotModes, id, 0); }
inline int parseScreenshotModeLegacyInt(int v) { return parseEnumLegacyInt(kScreenshotModes, v, 0); }

inline int parseVoiceMicMode(const char* id) { return parseEnumString(kVoiceMicModes, id, 0); }
inline int parseVoiceMicModeLegacyInt(int v) { return parseEnumLegacyInt(kVoiceMicModes, v, 0); }

} // namespace settings_contract
