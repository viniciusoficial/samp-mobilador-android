#pragma once

#include "RpcIds.h"
#include "Version.h"
#include "Wire.h"

#include <array>
#include <cstdint>
#include <string>

namespace proto {

// RPC 164 (server -> client): StreamInVehicle / WorldVehicleAdd
struct WorldVehicleAdd {
	static constexpr int kRpcId = rpc::kWorldVehicleAdd;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "WorldVehicleAdd";

	uint16_t vehicleId = 0xFFFF;
	int32_t modelId = 0;
	Vec3 position{};
	float rotation = 0.0f;
	uint8_t interiorColor1 = 0;
	uint8_t interiorColor2 = 0;
	float health = 0.0f;
	uint8_t interior = 0;
	uint32_t doorDamageStatus = 0;
	uint32_t panelDamageStatus = 0;
	uint8_t lightDamageStatus = 0;
	uint8_t tireDamageStatus = 0;
	uint8_t addSiren = 0;
	std::array<uint8_t, 14> modSlots{};
	uint8_t paintJob = 0;
	int32_t bodyColor1 = -1;
	int32_t bodyColor2 = -1;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		if (!s.u16(vehicleId) || !s.i32(modelId) || !s.vec3(position) || !s.f32(rotation) ||
		    !s.u8(interiorColor1) || !s.u8(interiorColor2) || !s.f32(health) ||
		    !s.u8(interior) || !s.u32(doorDamageStatus) || !s.u32(panelDamageStatus) ||
		    !s.u8(lightDamageStatus) || !s.u8(tireDamageStatus) || !s.u8(addSiren)) {
			return false;
		}
		if (!s.bytes(modSlots.data(), modSlots.size())) return false;
		return s.u8(paintJob) && s.i32(bodyColor1) && s.i32(bodyColor2);
	}
};

// RPC 165 (server -> client): StreamOutVehicle / WorldVehicleRemove
struct WorldVehicleRemove {
	static constexpr int kRpcId = rpc::kWorldVehicleRemove;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "WorldVehicleRemove";

	uint16_t vehicleId = 0xFFFF;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(vehicleId);
	}
};

// RPC 106 (server -> client): SetVehicleDamageStatus / DamageVehicle
struct DamageVehicle {
	static constexpr int kRpcId = rpc::kDamageVehicle;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "DamageVehicle";

	uint16_t vehicleId = 0xFFFF;
	uint32_t panelDamageStatus = 0;
	uint32_t doorDamageStatus = 0;
	uint8_t lightDamageStatus = 0;
	uint8_t tireDamageStatus = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(vehicleId) && s.u32(panelDamageStatus) && s.u32(doorDamageStatus) &&
		       s.u8(lightDamageStatus) && s.u8(tireDamageStatus);
	}
};

// RPC 24 (server -> client): SetVehicleParamsEx / VehicleParamsEx
struct VehicleParamsEx {
	static constexpr int kRpcId = rpc::kVehicleParamsEx;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "VehicleParamsEx";

	uint16_t vehicleId = 0xFFFF;
	uint8_t engine = 0;
	uint8_t lights = 0;
	uint8_t alarm = 0;
	uint8_t doors = 0;
	uint8_t bonnet = 0;
	uint8_t boot = 0;
	uint8_t objective = 0;
	uint8_t siren = 0;
	uint8_t doorDriver = 0;
	uint8_t doorPassenger = 0;
	uint8_t doorBackLeft = 0;
	uint8_t doorBackRight = 0;
	uint8_t windowDriver = 0;
	uint8_t windowPassenger = 0;
	uint8_t windowBackLeft = 0;
	uint8_t windowBackRight = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(vehicleId) &&
		       s.u8(engine) && s.u8(lights) && s.u8(alarm) && s.u8(doors) &&
		       s.u8(bonnet) && s.u8(boot) && s.u8(objective) && s.u8(siren) &&
		       s.u8(doorDriver) && s.u8(doorPassenger) && s.u8(doorBackLeft) && s.u8(doorBackRight) &&
		       s.u8(windowDriver) && s.u8(windowPassenger) && s.u8(windowBackLeft) && s.u8(windowBackRight);
	}
};

// RPC 26 (server -> client): EnterVehicle
struct EnterVehicle {
	static constexpr int kRpcId = rpc::kEnterVehicle;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "EnterVehicle";

	uint16_t playerId = 0xFFFF;
	uint16_t vehicleId = 0xFFFF;
	uint8_t passenger = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(playerId) && s.u16(vehicleId) && s.u8(passenger);
	}
};

// RPC 154 (server -> client): ExitVehicle
struct ExitVehicle {
	static constexpr int kRpcId = rpc::kExitVehicle;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "ExitVehicle";

	uint16_t playerId = 0xFFFF;
	uint16_t vehicleId = 0xFFFF;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(playerId) && s.u16(vehicleId);
	}
};

// RPC 96 (server -> client): SCMEvent
struct ScmEvent {
	static constexpr int kRpcId = rpc::kScmEvent;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "ScmEvent";

	uint16_t playerId = 0xFFFF;
	int32_t eventType = 0;
	uint32_t param1 = 0;
	uint32_t param2 = 0;
	uint32_t param3 = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(playerId) && s.i32(eventType) && s.u32(param1) && s.u32(param2) && s.u32(param3);
	}
};

// RPC 159 (server -> client): SetVehiclePos
struct SetVehiclePos {
	static constexpr int kRpcId = rpc::kScrSetVehiclePos;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SetVehiclePos";

	uint16_t vehicleId = 0xFFFF;
	Vec3 position{};

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(vehicleId) && s.vec3(position);
	}
};

// RPC 160 (server -> client): SetVehicleZAngle
struct SetVehicleZAngle {
	static constexpr int kRpcId = rpc::kScrSetVehicleZAngle;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SetVehicleZAngle";

	uint16_t vehicleId = 0xFFFF;
	float angle = 0.0f;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(vehicleId) && s.f32(angle);
	}
};

// RPC 161 (server -> client): SetVehicleParamsForPlayer (legacy 0.3.7)
struct SetVehicleParamsForPlayer {
	static constexpr int kRpcId = rpc::kScrVehicleParams;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SetVehicleParams";

	uint16_t vehicleId = 0xFFFF;
	uint8_t objective = 0;
	uint8_t doorsLocked = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(vehicleId) && s.u8(objective) && s.u8(doorsLocked);
	}
};

// RPC 147 (server -> client): SetVehicleHealth
struct SetVehicleHealth {
	static constexpr int kRpcId = rpc::kScrSetVehicleHealth;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SetVehicleHealth";

	uint16_t vehicleId = 0xFFFF;
	float health = 0.0f;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(vehicleId) && s.f32(health);
	}
};

// RPC 91 (server -> client): SetVehicleVelocity / SetVehicleAngularVelocity
struct SetVehicleVelocity {
	static constexpr int kRpcId = rpc::kScrSetVehicleVelocity;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SetVehicleVelocity";

	uint8_t type = 0;
	Vec3 velocity{};

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u8(type) && s.vec3(velocity);
	}
};

// RPC 98 (server -> client): SetVehicleTireDamageStatus
struct SetVehicleTireDamageStatus {
	static constexpr int kRpcId = rpc::kScrSetVehicleTireStatus;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SetVehicleTireDamageStatus";

	uint16_t vehicleId = 0xFFFF;
	uint8_t tireDamageStatus = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(vehicleId) && s.u8(tireDamageStatus);
	}
};

// RPC 123 (server -> client): SetVehicleNumberPlate
struct SetVehicleNumberPlate {
	static constexpr int kRpcId = rpc::kScrNumberPlate;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SetVehicleNumberPlate";

	uint16_t vehicleId = 0xFFFF;
	std::string plate;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(vehicleId) && s.dynStr8(plate);
	}
};

// RPC 65 (server -> client): LinkVehicleToInterior
struct LinkVehicleToInterior {
	static constexpr int kRpcId = rpc::kScrLinkVehicle;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "LinkVehicleToInterior";

	uint16_t vehicleId = 0xFFFF;
	uint8_t interiorId = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(vehicleId) && s.u8(interiorId);
	}
};

// RPC 148 (server -> client): AttachTrailerToVehicle
struct AttachTrailerToVehicle {
	static constexpr int kRpcId = rpc::kScrAttachTrailerToVehicle;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "AttachTrailerToVehicle";

	uint16_t trailerId = 0xFFFF;
	uint16_t vehicleId = 0xFFFF;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(trailerId) && s.u16(vehicleId);
	}
};

// RPC 149 (server -> client): DetachTrailerFromVehicle
struct DetachTrailerFromVehicle {
	static constexpr int kRpcId = rpc::kScrDetachTrailerFromVehicle;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "DetachTrailerFromVehicle";

	uint16_t vehicleId = 0xFFFF;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(vehicleId);
	}
};

// RPC 57 (server -> client): RemoveVehicleComponent
struct RemoveVehicleComponent {
	static constexpr int kRpcId = rpc::kScrRemoveComponent;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "RemoveVehicleComponent";

	uint16_t vehicleId = 0xFFFF;
	uint16_t componentId = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(vehicleId) && s.u16(componentId);
	}
};

// RPC 70 (server -> client): PutPlayerInVehicle
struct PutPlayerInVehicle {
	static constexpr int kRpcId = rpc::kScrPutPlayerInVehicle;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "PutPlayerInVehicle";

	uint16_t vehicleId = 0xFFFF;
	uint8_t seatId = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(vehicleId) && s.u8(seatId);
	}
};

// RPC 71 (server -> client): RemovePlayerFromVehicle
struct RemovePlayerFromVehicle {
	static constexpr int kRpcId = rpc::kScrRemovePlayerFromVehicle;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "RemovePlayerFromVehicle";

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.ok();
	}
};

} // namespace proto
