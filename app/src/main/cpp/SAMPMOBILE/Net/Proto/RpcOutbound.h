#pragma once

#include "RpcDialog.h"
#include "RpcIds.h"
#include "RpcVehicle.h"
#include "Version.h"
#include "Wire.h"

#include <cstdint>
#include <string>

namespace proto {

// RPC 25 (client -> server): ClientJoin
struct ClientJoin {
	static constexpr int kRpcId = rpc::kClientJoin;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "ClientJoin";

	int32_t version = 0;
	uint8_t mod = 1;
	std::string nickname;
	uint32_t challengeResponse = 0;
	std::string authKey;
	std::string clientVersion;

	template <typename S>
	bool serialize(S& s, ProtoVersion v)
	{
		if (!s.i32(version) || !s.u8(mod) || !s.dynStr8(nickname) ||
		    !s.u32(challengeResponse) || !s.dynStr8(authKey) || !s.dynStr8(clientVersion)) {
			return false;
		}
		if (v == ProtoVersion::V03DL) {
			uint32_t challengeCopy = challengeResponse;
			if (!s.u32(challengeCopy)) return false;
		}
		return s.ok();
	}
};

// RPC 103 (client -> server): ClientCheck response
struct ClientCheckReply {
	static constexpr int kRpcId = rpc::kClientCheck;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "ClientCheckReply";

	uint8_t type = 0;
	uint32_t argument = 0;
	uint8_t result = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u8(type) && s.u32(argument) && s.u8(result);
	}
};

// RPC 62 (client -> server): DialogResponse is defined in RpcDialog.h

// RPC 101 (client -> server): PlayerRequestChatMessage
struct ChatReq {
	static constexpr int kRpcId = rpc::kChat;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "ChatReq";

	std::string message;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.dynStr8(message);
	}
};

// RPC 50 (client -> server): PlayerRequestCommandMessage
struct ServerCommandReq {
	static constexpr int kRpcId = rpc::kServerCommand;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "ServerCommandReq";

	std::string command;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.dynStr32(command, 256);
	}
};

// RPC 155 (client -> server): OnPlayerRequestScoresAndPings
struct UpdateScoresPingsReq {
	static constexpr int kRpcId = rpc::kUpdateScoresPingsIPs;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "UpdateScoresPingsReq";

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.ok();
	}
};

// RPC 128 (client -> server): PlayerRequestClass
// Note: client writes 32-bit int classId (open.mp reads the low 16 bits).
struct RequestClassReq {
	static constexpr int kRpcId = rpc::kRequestClass;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "RequestClassReq";

	int32_t classId = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.i32(classId);
	}
};

// RPC 129 (client -> server): PlayerRequestSpawn
struct RequestSpawnReq {
	static constexpr int kRpcId = rpc::kRequestSpawn;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "RequestSpawnReq";

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.ok();
	}
};

// RPC 52 (client -> server): PlayerSpawn
struct SpawnReq {
	static constexpr int kRpcId = rpc::kSpawn;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SpawnReq";

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.ok();
	}
};

// RPC 53 (client -> server): OnPlayerDeath
struct DeathReq {
	static constexpr int kRpcId = rpc::kDeath;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "DeathReq";

	uint8_t reason = 0;
	uint16_t killerId = 0xFFFF;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u8(reason) && s.u16(killerId);
	}
};

// RPC 118 (client -> server): OnPlayerInteriorChange
struct SetInteriorReq {
	static constexpr int kRpcId = rpc::kSetInteriorId;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SetInteriorReq";

	uint8_t interiorId = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u8(interiorId);
	}
};

// RPC 26 (client -> server): OnPlayerEnterVehicle
struct EnterVehicleReq {
	static constexpr int kRpcId = rpc::kEnterVehicle;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "EnterVehicleReq";

	uint16_t vehicleId = 0xFFFF;
	uint8_t passenger = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(vehicleId) && s.u8(passenger);
	}
};

// RPC 154 (client -> server): OnPlayerExitVehicle
struct ExitVehicleReq {
	static constexpr int kRpcId = rpc::kExitVehicle;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "ExitVehicleReq";

	uint16_t vehicleId = 0xFFFF;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(vehicleId);
	}
};

// RPC 106 (client -> server): SetVehicleDamageStatus (same wire layout as inbound DamageVehicle)
using DamageVehicleReq = DamageVehicle;

// RPC 136 (client -> server): VehicleDeath
struct VehicleDestroyedReq {
	static constexpr int kRpcId = rpc::kVehicleDestroyed;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "VehicleDestroyedReq";

	uint16_t vehicleId = 0xFFFF;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(vehicleId);
	}
};

// RPC 131 (client -> server): OnPlayerPickUpPickup
struct PickedUpPickupReq {
	static constexpr int kRpcId = rpc::kPickedUpPickup;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "PickedUpPickupReq";

	int32_t pickupId = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.i32(pickupId);
	}
};

// RPC 83 (client -> server): OnPlayerSelectTextDraw
struct ClickTextDrawReq {
	static constexpr int kRpcId = rpc::kClickTextDraw;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "ClickTextDrawReq";

	uint16_t textDrawId = 0xFFFF;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(textDrawId);
	}
};

// RPC 23 (client -> server): OnPlayerClickPlayer
struct ClickPlayerReq {
	static constexpr int kRpcId = rpc::kClickPlayer;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "ClickPlayerReq";

	uint16_t playerId = 0xFFFF;
	uint8_t source = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(playerId) && s.u8(source);
	}
};

// RPC 119 (client -> server): OnPlayerClickMap
struct MapMarkerReq {
	static constexpr int kRpcId = rpc::kMapMarker;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "MapMarkerReq";

	Vec3 position{};

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.vec3(position);
	}
};

// RPC 115 (client -> server): OnPlayerGiveTakeDamage
struct GiveTakeDamageReq {
	static constexpr int kRpcId = rpc::kPlayerGiveTakeDamage;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "GiveTakeDamageReq";

	bool taking = false;
	uint16_t playerId = 0xFFFF;
	float damage = 0.0f;
	int32_t weaponId = 0;
	int32_t bodyPart = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.bit(taking) && s.u16(playerId) && s.f32(damage) &&
		       s.i32(weaponId) && s.i32(bodyPart);
	}
};

// RPC 177 (client -> server): OnPlayerDamageActor
struct GiveDamageActorReq {
	static constexpr int kRpcId = rpc::kGiveDamageActor;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "GiveDamageActorReq";

	bool unknown = false;
	uint16_t actorId = 0xFFFF;
	float damage = 0.0f;
	int32_t weaponId = 0;
	int32_t bodyPart = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.bit(unknown) && s.u16(actorId) && s.f32(damage) &&
		       s.i32(weaponId) && s.i32(bodyPart);
	}
};

// RPC 117 (client -> server): OnPlayerEditObject
struct EditObjectReply {
	static constexpr int kRpcId = rpc::kEditObject;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "EditObjectReply";

	bool playerObject = false;
	uint16_t objectId = 0xFFFF;
	uint32_t response = 0;
	Vec3 position{};
	Vec3 rotation{};

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.bit(playerObject) && s.u16(objectId) && s.u32(response) &&
		       s.vec3(position) && s.vec3(rotation);
	}
};

// RPC 116 (client -> server): OnPlayerEditAttachedObject
struct EditAttachedObjectReply {
	static constexpr int kRpcId = rpc::kEditAttachedObject;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "EditAttachedObjectReply";

	uint32_t response = 0;
	int32_t index = 0;
	int32_t modelId = 0;
	int32_t boneId = 0;
	Vec3 offset{};
	Vec3 rotation{};
	Vec3 scale{};
	uint32_t color1 = 0;
	uint32_t color2 = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u32(response) && s.i32(index) && s.i32(modelId) && s.i32(boneId) &&
		       s.vec3(offset) && s.vec3(rotation) && s.vec3(scale) &&
		       s.u32(color1) && s.u32(color2);
	}
};

// RPC 27 (client -> server): OnPlayerSelectObject
struct SelectObjectReply {
	static constexpr int kRpcId = rpc::kEnterEditObject;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SelectObjectReply";

	uint32_t selectType = 0;
	uint16_t objectId = 0xFFFF;
	uint32_t modelId = 0;
	Vec3 position{};

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u32(selectType) && s.u16(objectId) && s.u32(modelId) && s.vec3(position);
	}
};

// RPC 132 (client -> server): OnPlayerSelectedMenuRow
struct MenuSelectReq {
	static constexpr int kRpcId = rpc::kMenuSelect;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "MenuSelectReq";

	uint8_t row = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u8(row);
	}
};

// RPC 140 (client -> server): OnPlayerExitedMenu
struct MenuQuitReq {
	static constexpr int kRpcId = rpc::kMenuQuit;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "MenuQuitReq";

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.ok();
	}
};

// RPC 168 (client -> server): OnPlayerCameraTarget
struct CameraTargetReply {
	static constexpr int kRpcId = rpc::kUpdateCameraTarget;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "CameraTargetReply";

	uint16_t objectId = 0xFFFF;
	uint16_t vehicleId = 0xFFFF;
	uint16_t playerId = 0xFFFF;
	uint16_t actorId = 0xFFFF;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(objectId) && s.u16(vehicleId) && s.u16(playerId) && s.u16(actorId);
	}
};

// RPC 97 (client -> server): Picked up a dropped weapon pickup
struct PickedUpWeaponPickupReq {
	static constexpr int kRpcId = rpc::kDestroyWeaponPickup;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "PickedUpWeaponPickupReq";

	uint16_t owner = 0xFFFF;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(owner);
	}
};

// RPC 181 (client -> server, 0.3.DL): Request DFF download URL/data by CRC32
struct DlRequestDffReq {
	static constexpr int kRpcId = rpc::kDlRequestDff;
	static constexpr uint8_t kVersionMask = kVer03DL;
	static constexpr const char* kName = "DlRequestDffReq";

	uint32_t checksum = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u32(checksum);
	}
};

// RPC 182 (client -> server, 0.3.DL): Request TXD download URL/data by CRC32
struct DlRequestTxdReq {
	static constexpr int kRpcId = rpc::kDlRequestTxd;
	static constexpr uint8_t kVersionMask = kVer03DL;
	static constexpr const char* kName = "DlRequestTxdReq";

	uint32_t checksum = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u32(checksum);
	}
};

// RPC 184 (client -> server, 0.3.DL): Artwork download finished notification
// PC 0.3.DL writes a single `true` bit; open.mp ignores payload and returns true.
struct DlFinishDownloadReq {
	static constexpr int kRpcId = rpc::kDlFinishDownload;
	static constexpr uint8_t kVersionMask = kVer03DL;
	static constexpr const char* kName = "DlFinishDownloadReq";

	bool finished = true;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		if constexpr (S::kReading) {
			finished = true;
			s.optionalTail([&](auto& tail) { return tail.bit(finished); });
			return s.ok();
		} else {
			return s.bit(finished);
		}
	}
};

} // namespace proto
