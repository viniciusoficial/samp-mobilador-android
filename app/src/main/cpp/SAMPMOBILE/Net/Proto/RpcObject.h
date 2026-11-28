#pragma once

#include "RpcIds.h"
#include "Version.h"
#include "Wire.h"

#include <cstdint>
#include <string>
#include <vector>

namespace proto {

inline constexpr uint8_t kObjectMaterialTexture = 1;
inline constexpr uint8_t kObjectMaterialText = 2;

// Shared material payload used by CreateObject (RPC 44) and SetObjectMaterial (RPC 84).
struct ObjectMaterial {
	uint8_t materialType = 0;
	uint8_t materialIndex = 0;

	// Texture variant (materialType == 1)
	uint16_t modelId = 0;
	std::string txdName;
	std::string textureName;
	uint32_t materialColor = 0;

	// Text variant (materialType == 2)
	uint8_t materialSize = 0;
	std::string fontName;
	uint8_t fontSize = 0;
	uint8_t fontBold = 0;
	uint32_t fontColor = 0;
	uint32_t backgroundColor = 0;
	uint8_t alignment = 0;
	std::string text;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		if (!s.u8(materialType) || !s.u8(materialIndex)) return false;
		if (materialType == kObjectMaterialTexture) {
			return s.u16(modelId) && s.dynStr8(txdName) && s.dynStr8(textureName) &&
			       s.u32(materialColor);
		}
		if (materialType == kObjectMaterialText) {
			return s.u8(materialSize) && s.dynStr8(fontName) && s.u8(fontSize) &&
			       s.u8(fontBold) && s.u32(fontColor) && s.u32(backgroundColor) &&
			       s.u8(alignment) && s.compressedStr(text, 2048);
		}
		return false;
	}
};

// RPC 44 (server -> client): CreateObject
struct CreateObject {
	static constexpr int kRpcId = rpc::kScrCreateObject;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "CreateObject";

	uint16_t objectId = 0xFFFF;
	int32_t modelId = 0;
	Vec3 position{};
	Vec3 rotation{};
	float drawDistance = 0.0f;
	uint8_t noCameraCol = 0;
	uint16_t attachedVehicleId = 0xFFFF;
	uint16_t attachedObjectId = 0xFFFF;
	Vec3 attachOffset{};
	Vec3 attachRotation{};
	uint8_t syncRotation = 0;
	std::vector<ObjectMaterial> materials;

	template <typename S>
	bool serialize(S& s, ProtoVersion v)
	{
		if (!s.u16(objectId) || !s.i32(modelId) || !s.vec3(position) || !s.vec3(rotation) ||
		    !s.f32(drawDistance) || !s.u8(noCameraCol) ||
		    !s.u16(attachedVehicleId) || !s.u16(attachedObjectId)) {
			return false;
		}
		if (attachedObjectId != 0xFFFF || attachedVehicleId != 0xFFFF) {
			if (!s.vec3(attachOffset) || !s.vec3(attachRotation) || !s.u8(syncRotation)) {
				return false;
			}
		}
		if constexpr (S::kReading) {
			uint8_t count = 0;
			if (!s.u8(count)) return false;
			materials.clear();
			materials.reserve(count);
			for (uint8_t i = 0; i < count; ++i) {
				ObjectMaterial m{};
				if (!m.serialize(s, v)) return false;
				materials.push_back(std::move(m));
			}
		}
		else {
			uint8_t count = static_cast<uint8_t>(materials.size());
			if (!s.u8(count)) return false;
			for (uint8_t i = 0; i < count; ++i) {
				if (!materials[i].serialize(s, v)) return false;
			}
		}
		return s.ok();
	}
};

// RPC 45 (server -> client): SetObjectPos (open.mp sends 14 bytes without trailing float,
// legacy SA-MP server sends 18 bytes with an unused trailing float).
struct SetObjectPos {
	static constexpr int kRpcId = rpc::kScrSetObjectPos;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SetObjectPos";

	uint16_t objectId = 0xFFFF;
	Vec3 position{};
	float unused = 0.0f;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		if (!s.u16(objectId) || !s.vec3(position)) return false;
		s.optionalTail([&](auto& tail) { return tail.f32(unused); });
		return s.ok();
	}
};

// RPC 46 (server -> client): SetObjectRotation
struct SetObjectRotation {
	static constexpr int kRpcId = rpc::kScrSetObjectRotation;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SetObjectRotation";

	uint16_t objectId = 0xFFFF;
	Vec3 rotation{};

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(objectId) && s.vec3(rotation);
	}
};

// RPC 47 (server -> client): DestroyObject
struct DestroyObject {
	static constexpr int kRpcId = rpc::kScrDestroyObject;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "DestroyObject";

	uint16_t objectId = 0xFFFF;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(objectId);
	}
};

// RPC 99 (server -> client): MoveObject
struct MoveObject {
	static constexpr int kRpcId = rpc::kScrMoveObject;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "MoveObject";

	uint16_t objectId = 0xFFFF;
	Vec3 fromPos{};
	Vec3 targetPos{};
	float speed = 0.0f;
	Vec3 targetRot{};

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(objectId) && s.vec3(fromPos) && s.vec3(targetPos) &&
		       s.f32(speed) && s.vec3(targetRot);
	}
};

// RPC 122 (server -> client): StopObject
struct StopObject {
	static constexpr int kRpcId = rpc::kScrStopObject;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "StopObject";

	uint16_t objectId = 0xFFFF;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(objectId);
	}
};

// RPC 75 (server -> client): AttachObjectToPlayer
struct AttachObjectToPlayer {
	static constexpr int kRpcId = rpc::kScrAttachObjectToPlayer;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "AttachObjectToPlayer";

	uint16_t objectId = 0xFFFF;
	uint16_t playerId = 0xFFFF;
	Vec3 offset{};
	Vec3 rotation{};

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(objectId) && s.u16(playerId) && s.vec3(offset) && s.vec3(rotation);
	}
};

// RPC 84 (server -> client): SetObjectMaterial / SetObjectMaterialText
struct SetObjectMaterial {
	static constexpr int kRpcId = rpc::kScrSetObjectMaterial;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SetObjectMaterial";

	uint16_t objectId = 0xFFFF;
	ObjectMaterial material{};

	template <typename S>
	bool serialize(S& s, ProtoVersion v)
	{
		return s.u16(objectId) && material.serialize(s, v);
	}
};

// RPC 169 (server -> client): SetObjectNoCameraCol / StopObjectCameraCollision
struct StopObjectCameraCollision {
	static constexpr int kRpcId = rpc::kScrStopObjectCameraCollision;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "StopObjectCameraCollision";

	uint16_t objectId = 0xFFFF;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(objectId);
	}
};

// RPC 113 (server -> client): SetPlayerAttachedObject / RemovePlayerAttachedObject
struct SetPlayerAttachedObject {
	static constexpr int kRpcId = rpc::kScrSetPlayerAttachedObject;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SetPlayerAttachedObject";

	uint16_t playerId = 0xFFFF;
	int32_t index = 0;
	bool create = false;
	int32_t modelId = 0;
	int32_t boneId = 0;
	Vec3 offset{};
	Vec3 rotation{};
	Vec3 scale{};
	uint32_t materialColor1 = 0;
	uint32_t materialColor2 = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		if (!s.u16(playerId) || !s.i32(index) || !s.bit(create)) return false;
		if (create) {
			return s.i32(modelId) && s.i32(boneId) && s.vec3(offset) &&
			       s.vec3(rotation) && s.vec3(scale) &&
			       s.u32(materialColor1) && s.u32(materialColor2);
		}
		return s.ok();
	}
};

// RPC 116 (server -> client): EditAttachedObject
struct EditAttachedObject {
	static constexpr int kRpcId = rpc::kEditAttachedObject;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "EditAttachedObject";

	int32_t index = -1;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.i32(index);
	}
};

// RPC 117 (server -> client): EditObject / EditPlayerObject
struct EditObject {
	static constexpr int kRpcId = rpc::kEditObject;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "EditObject";

	bool playerObject = false;
	uint16_t objectId = 0xFFFF;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.bit(playerObject) && s.u16(objectId);
	}
};

// RPC 27 (server -> client): SelectObject / EnterEditObject
struct EnterEditObject {
	static constexpr int kRpcId = rpc::kEnterEditObject;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "EnterEditObject";

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.ok();
	}
};

// RPC 28 (server -> client): CancelEdit / CancelEditObject
struct CancelEditObject {
	static constexpr int kRpcId = rpc::kCancelEditObject;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "CancelEditObject";

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.ok();
	}
};

} // namespace proto
