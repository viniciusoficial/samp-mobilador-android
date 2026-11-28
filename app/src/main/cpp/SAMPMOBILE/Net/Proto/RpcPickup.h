#pragma once

#include "RpcIds.h"
#include "Version.h"
#include "Wire.h"

#include <cstdint>

namespace proto {

// RPC 95 (server -> client): CreatePickup
struct CreatePickup {
	static constexpr int kRpcId = rpc::kPickup;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "CreatePickup";

	int32_t pickupId = -1;
	int32_t model = 0;
	int32_t type = 0;
	Vec3 position{};

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.i32(pickupId) && s.i32(model) && s.i32(type) && s.vec3(position);
	}
};

// RPC 63 (server -> client): DestroyPickup
struct DestroyPickup {
	static constexpr int kRpcId = rpc::kDestroyPickup;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "DestroyPickup";

	int32_t pickupId = -1;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.i32(pickupId);
	}
};

// RPC 151 (server -> client, 0.3.DL only): DestroyWeaponPickup
struct DestroyWeaponPickup {
	static constexpr int kRpcId = rpc::kScrDestroyWeaponPickup;
	static constexpr uint8_t kVersionMask = kVer03DL;
	static constexpr const char* kName = "DestroyWeaponPickup";

	uint8_t owner = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u8(owner);
	}
};

// RPC 131 (client -> server): OnPlayerPickUpPickup
struct PickedUpPickup {
	static constexpr int kRpcId = rpc::kPickedUpPickup;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "PickedUpPickup";

	int32_t pickupId = -1;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.i32(pickupId);
	}
};

} // namespace proto
