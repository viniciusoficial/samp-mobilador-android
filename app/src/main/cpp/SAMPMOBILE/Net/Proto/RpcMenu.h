#pragma once

#include "RpcIds.h"
#include "Version.h"
#include "Wire.h"

#include <array>
#include <cstdint>
#include <string>

namespace proto {

inline constexpr size_t kMaxMenuItems = 12;
inline constexpr size_t kMaxMenuTextLength = 32;

namespace detail {

inline void trimFixedMenuStr(std::string& s)
{
	const size_t pos = s.find('\0');
	if (pos != std::string::npos) {
		s.resize(pos);
	}
}

} // namespace detail

// RPC 76 (server -> client): InitMenu
struct InitMenu {
	static constexpr int kRpcId = rpc::kScrInitMenu;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "InitMenu";

	uint8_t menuId = 0;
	bool hasTwoColumns = false;
	std::string title;
	Vec2 position{};
	float col1Width = 0.0f;
	float col2Width = 0.0f;
	bool menuEnabled = false;
	std::array<bool, kMaxMenuItems> rowEnabled{};
	std::array<std::string, 2> columnHeaders{};
	std::array<uint8_t, 2> columnItemCount{};
	std::array<std::array<std::string, kMaxMenuItems>, 2> items{};

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		if (!s.u8(menuId)) return false;

		uint32_t twoCols = hasTwoColumns ? 1u : 0u;
		if (!s.u32(twoCols)) return false;
		if constexpr (S::kReading) hasTwoColumns = (twoCols != 0);

		if (!s.fixedStr(title, kMaxMenuTextLength)) return false;
		if constexpr (S::kReading) detail::trimFixedMenuStr(title);

		if (!s.vec2(position) || !s.f32(col1Width)) return false;
		if (hasTwoColumns) {
			if (!s.f32(col2Width)) return false;
		}
		else if constexpr (S::kReading) {
			col2Width = 0.0f;
		}

		uint32_t menuEn = menuEnabled ? 1u : 0u;
		if (!s.u32(menuEn)) return false;
		if constexpr (S::kReading) menuEnabled = (menuEn != 0);

		for (size_t i = 0; i < kMaxMenuItems; ++i) {
			uint32_t rowEn = rowEnabled[i] ? 1u : 0u;
			if (!s.u32(rowEn)) return false;
			if constexpr (S::kReading) rowEnabled[i] = (rowEn != 0);
		}

		if (!s.fixedStr(columnHeaders[0], kMaxMenuTextLength)) return false;
		if constexpr (S::kReading) detail::trimFixedMenuStr(columnHeaders[0]);

		if (!s.u8(columnItemCount[0])) return false;
		if (columnItemCount[0] > kMaxMenuItems) return false;
		for (uint8_t i = 0; i < columnItemCount[0]; ++i) {
			if (!s.fixedStr(items[0][i], kMaxMenuTextLength)) return false;
			if constexpr (S::kReading) detail::trimFixedMenuStr(items[0][i]);
		}

		if (hasTwoColumns) {
			if (!s.fixedStr(columnHeaders[1], kMaxMenuTextLength)) return false;
			if constexpr (S::kReading) detail::trimFixedMenuStr(columnHeaders[1]);

			if (!s.u8(columnItemCount[1])) return false;
			if (columnItemCount[1] > kMaxMenuItems) return false;
			for (uint8_t i = 0; i < columnItemCount[1]; ++i) {
				if (!s.fixedStr(items[1][i], kMaxMenuTextLength)) return false;
				if constexpr (S::kReading) detail::trimFixedMenuStr(items[1][i]);
			}
		}
		else if constexpr (S::kReading) {
			columnHeaders[1].clear();
			columnItemCount[1] = 0;
		}

		return s.ok();
	}
};

// RPC 77 (server -> client): ShowMenu
struct ShowMenu {
	static constexpr int kRpcId = rpc::kScrShowMenu;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "ShowMenu";

	uint8_t menuId = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u8(menuId);
	}
};

// RPC 78 (server -> client): HideMenu
struct HideMenu {
	static constexpr int kRpcId = rpc::kScrHideMenu;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "HideMenu";

	uint8_t menuId = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u8(menuId);
	}
};

} // namespace proto
