#pragma once

#include "RpcIds.h"
#include "Version.h"
#include "Wire.h"

#include <cstdint>

namespace proto {

// RPC 107 (server -> client): SetPlayerCheckpoint
struct SetCheckpoint {
	static constexpr int kRpcId = rpc::kSetCheckpoint;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SetCheckpoint";

	Vec3 position{};
	float size = 0.0f;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.vec3(position) && s.f32(size);
	}
};

// RPC 37 (server -> client): DisablePlayerCheckpoint
struct DisableCheckpoint {
	static constexpr int kRpcId = rpc::kDisableCheckpoint;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "DisableCheckpoint";

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.ok();
	}
};

// RPC 38 (server -> client): SetPlayerRaceCheckpoint
struct SetRaceCheckpoint {
	static constexpr int kRpcId = rpc::kSetRaceCheckpoint;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SetRaceCheckpoint";

	uint8_t type = 0;
	Vec3 position{};
	Vec3 nextPosition{};
	float radius = 0.0f;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u8(type) && s.vec3(position) && s.vec3(nextPosition) && s.f32(radius);
	}
};

// RPC 39 (server -> client): DisablePlayerRaceCheckpoint
struct DisableRaceCheckpoint {
	static constexpr int kRpcId = rpc::kDisableRaceCheckpoint;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "DisableRaceCheckpoint";

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.ok();
	}
};

} // namespace proto
