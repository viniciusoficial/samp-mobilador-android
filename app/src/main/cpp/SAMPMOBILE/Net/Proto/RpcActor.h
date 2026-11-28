#pragma once

#include "RpcIds.h"
#include "Version.h"
#include "Wire.h"

#include <cstdint>
#include <string>

namespace proto {

// RPC 171 (server -> client): ShowActorForPlayer
struct ShowActor {
	static constexpr int kRpcId = rpc::kShowActor;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "ShowActor";

	uint16_t actorId = 0xFFFF;
	int32_t skin = 0;
	int32_t customSkin = 0; // 0.3.DL only
	Vec3 position{};
	float angle = 0.0f;
	float health = 0.0f;
	uint8_t invulnerable = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion v)
	{
		if (!s.u16(actorId) || !s.i32(skin)) return false;
		if (v == ProtoVersion::V03DL) {
			if (!s.i32(customSkin)) return false;
		}
		return s.vec3(position) && s.f32(angle) && s.f32(health) && s.u8(invulnerable);
	}
};

// RPC 172 (server -> client): HideActorForPlayer
struct HideActor {
	static constexpr int kRpcId = rpc::kHideActor;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "HideActor";

	uint16_t actorId = 0xFFFF;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(actorId);
	}
};

// RPC 173 (server -> client): ApplyActorAnimationForPlayer
struct ApplyActorAnimation {
	static constexpr int kRpcId = rpc::kScrApplyActorAnimation;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "ApplyActorAnimation";

	uint16_t actorId = 0xFFFF;
	std::string animLib;
	std::string animName;
	float delta = 0.0f;
	bool loop = false;
	bool lockX = false;
	bool lockY = false;
	bool freeze = false;
	int32_t time = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(actorId) && s.dynStr8(animLib) && s.dynStr8(animName) &&
		       s.f32(delta) && s.bit(loop) && s.bit(lockX) && s.bit(lockY) &&
		       s.bit(freeze) && s.i32(time);
	}
};

// RPC 174 (server -> client): ClearActorAnimationsForPlayer
struct ClearActorAnimation {
	static constexpr int kRpcId = rpc::kScrClearActorAnimations;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "ClearActorAnimation";

	uint16_t actorId = 0xFFFF;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(actorId);
	}
};

// RPC 175 (server -> client): SetActorFacingAngleForPlayer
struct SetActorFacingAngle {
	static constexpr int kRpcId = rpc::kScrSetActorFacingAngle;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SetActorFacingAngle";

	uint16_t actorId = 0xFFFF;
	float angle = 0.0f;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(actorId) && s.f32(angle);
	}
};

// RPC 176 (server -> client): SetActorPosForPlayer
struct SetActorPos {
	static constexpr int kRpcId = rpc::kScrSetActorPos;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SetActorPos";

	uint16_t actorId = 0xFFFF;
	Vec3 position{};

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(actorId) && s.vec3(position);
	}
};

// RPC 178 (server -> client): SetActorHealthForPlayer
struct SetActorHealth {
	static constexpr int kRpcId = rpc::kScrSetActorHealth;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SetActorHealth";

	uint16_t actorId = 0xFFFF;
	float health = 0.0f;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(actorId) && s.f32(health);
	}
};

} // namespace proto
