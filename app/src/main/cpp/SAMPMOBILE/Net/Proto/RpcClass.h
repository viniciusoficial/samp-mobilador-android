#pragma once

#include "RpcIds.h"
#include "Version.h"
#include "Wire.h"

#include <array>
#include <cstdint>

namespace proto {

// Shared spawn-info wire layout used by RequestClassReply (RPC 128) and SetSpawnInfo (RPC 68).
// 0.3.7: 46 bytes; 0.3.DL: 50 bytes (inserts int32 customSkin immediately after skin).
struct SpawnInfo {
	uint8_t team = 0;
	int32_t skin = 0;
	int32_t customSkin = 0; // 0.3.DL only
	uint8_t unk = 0;
	Vec3 position{};
	float rotation = 0.0f;
	std::array<int32_t, 3> spawnWeapons{};
	std::array<int32_t, 3> spawnWeaponsAmmo{};

	template <typename S>
	bool serialize(S& s, ProtoVersion v)
	{
		if (!s.u8(team) || !s.i32(skin)) return false;
		if (v == ProtoVersion::V03DL) {
			if (!s.i32(customSkin)) return false;
		}
		if (!s.u8(unk) || !s.vec3(position) || !s.f32(rotation)) return false;
		for (auto& w : spawnWeapons) {
			if (!s.i32(w)) return false;
		}
		for (auto& a : spawnWeaponsAmmo) {
			if (!s.i32(a)) return false;
		}
		return s.ok();
	}
};

// RPC 128 (server -> client): PlayerRequestClassResponse
struct RequestClassReply {
	static constexpr int kRpcId = rpc::kRequestClass;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "RequestClass";

	uint8_t selectable = 0;
	SpawnInfo spawnInfo{};

	template <typename S>
	bool serialize(S& s, ProtoVersion v)
	{
		return s.u8(selectable) && spawnInfo.serialize(s, v);
	}
};

// RPC 129 (server -> client): PlayerRequestSpawnResponse / ImmediatelySpawnPlayer
struct RequestSpawnReply {
	static constexpr int kRpcId = rpc::kRequestSpawn;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "RequestSpawn";

	uint8_t outcome = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		if (!s.u8(outcome)) return false;
		// open.mp writes uint32_t (4 bytes) while legacy SA-MP servers write uint8_t (1 byte).
		// Consume optional 3 trailing zero bytes so both layouts decode cleanly with 0 unread bits.
		uint8_t pad[3] = {0, 0, 0};
		s.optionalTail([&](auto& tail) { return tail.bytes(pad, sizeof(pad)); });
		return s.ok();
	}
};

// RPC 68 (server -> client): SetSpawnInfo
struct SetSpawnInfo {
	static constexpr int kRpcId = rpc::kScrSetSpawnInfo;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SetSpawnInfo";

	SpawnInfo spawnInfo{};

	template <typename S>
	bool serialize(S& s, ProtoVersion v)
	{
		return spawnInfo.serialize(s, v);
	}
};

// RPC 74 (server -> client): ForceClassSelection
struct ForceClassSelection {
	static constexpr int kRpcId = rpc::kScrForceSpawnSelection;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "ForceClassSelection";

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.ok();
	}
};

} // namespace proto
