#pragma once

#include "RpcIds.h"
#include "Version.h"
#include "Wire.h"

#include <cstdint>
#include <string>

namespace proto {

// RPC 36 (server -> client): Create3DTextLabel / CreatePlayer3DTextLabel
struct Create3DTextLabel {
	static constexpr int kRpcId = rpc::kScrCreate3DTextLabel;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "Create3DTextLabel";

	uint16_t labelId = 0xFFFF;
	uint32_t color = 0;
	Vec3 position{};
	float drawDistance = 0.0f;
	uint8_t testLOS = 0;
	uint16_t attachedPlayerId = 0xFFFF;
	uint16_t attachedVehicleId = 0xFFFF;
	std::string text;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(labelId) && s.u32(color) && s.vec3(position) && s.f32(drawDistance) && s.u8(testLOS) &&
			s.u16(attachedPlayerId) && s.u16(attachedVehicleId) && s.compressedStr(text, 2048);
	}
};

// RPC 58 (server -> client): Delete3DTextLabel / DeletePlayer3DTextLabel
struct Delete3DTextLabel {
	static constexpr int kRpcId = rpc::kScrUpdate3DTextLabel;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "Delete3DTextLabel";

	uint16_t labelId = 0xFFFF;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(labelId);
	}
};

} // namespace proto
