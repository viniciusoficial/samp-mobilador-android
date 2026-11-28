#pragma once

#include "RpcIds.h"
#include "Version.h"
#include "Wire.h"

#include <cstdint>
#include <string>

namespace proto {

// RPC 134 (server -> client): PlayerShowTextDraw
struct ShowTextDraw {
	static constexpr int kRpcId = rpc::kScrShowTextDraw;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "ShowTextDraw";

	uint16_t textDrawId = 0xFFFF;
	uint8_t flags = 0;
	Vec2 letterSize{};
	uint32_t letterColor = 0;
	Vec2 lineSize{};
	uint32_t boxColor = 0;
	uint8_t shadow = 0;
	uint8_t outline = 0;
	uint32_t backgroundColor = 0;
	uint8_t style = 0;
	uint8_t selectable = 0;
	Vec2 position{};
	uint16_t modelId = 0;
	Vec3 rotation{};
	float zoom = 0.0f;
	uint16_t color1 = 0;
	uint16_t color2 = 0;
	std::string text;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(textDrawId) && s.u8(flags) &&
		       s.vec2(letterSize) && s.u32(letterColor) &&
		       s.vec2(lineSize) && s.u32(boxColor) &&
		       s.u8(shadow) && s.u8(outline) && s.u32(backgroundColor) &&
		       s.u8(style) && s.u8(selectable) && s.vec2(position) &&
		       s.u16(modelId) && s.vec3(rotation) && s.f32(zoom) &&
		       s.u16(color1) && s.u16(color2) && s.dynStr16(text, 4096);
	}
};

// RPC 135 (server -> client): PlayerHideTextDraw
struct HideTextDraw {
	static constexpr int kRpcId = rpc::kScrHideTextDraw;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "HideTextDraw";

	uint16_t textDrawId = 0xFFFF;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(textDrawId);
	}
};

// RPC 105 (server -> client): PlayerTextDrawSetString
struct TextDrawSetString {
	static constexpr int kRpcId = rpc::kScrTextDrawSetString;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "TextDrawSetString";

	uint16_t textDrawId = 0xFFFF;
	std::string text;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(textDrawId) && s.dynStr16(text, 4096);
	}
};

// RPC 83 (server -> client): SelectTextDraw / PlayerBeginTextDrawSelect
struct SelectTextDraw {
	static constexpr int kRpcId = rpc::kScrSelectTextDraw;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SelectTextDraw";

	bool enable = false;
	uint32_t hoverColor = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.bit(enable) && s.u32(hoverColor);
	}
};

} // namespace proto
