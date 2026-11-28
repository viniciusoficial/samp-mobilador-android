#pragma once

#include "RpcIds.h"
#include "Version.h"
#include "Wire.h"

#include <cstdint>

namespace proto {

// RPC 108 (server -> client): GangZoneShowForPlayer
struct ShowGangZone {
	static constexpr int kRpcId = rpc::kScrAddGangZone;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "ShowGangZone";

	uint16_t zoneId = 0;
	Vec2 min{};
	Vec2 max{};
	uint32_t color = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(zoneId) && s.vec2(min) && s.vec2(max) && s.u32(color);
	}
};

// RPC 120 (server -> client): GangZoneHideForPlayer
struct HideGangZone {
	static constexpr int kRpcId = rpc::kScrRemoveGangZone;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "HideGangZone";

	uint16_t zoneId = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(zoneId);
	}
};

// RPC 121 (server -> client): GangZoneFlashForPlayer
struct FlashGangZone {
	static constexpr int kRpcId = rpc::kScrFlashGangZone;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "FlashGangZone";

	uint16_t zoneId = 0;
	uint32_t color = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(zoneId) && s.u32(color);
	}
};

// RPC 85 (server -> client): GangZoneStopFlashForPlayer
struct StopFlashGangZone {
	static constexpr int kRpcId = rpc::kScrStopFlashGangZone;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "StopFlashGangZone";

	uint16_t zoneId = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(zoneId);
	}
};

} // namespace proto
