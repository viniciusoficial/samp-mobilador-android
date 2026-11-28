#pragma once

#include "Version.h"
#include "Wire.h"

#include <RakNet/PacketEnumerations.h>

#include <cstdint>
#include <string>
#include <vector>

namespace proto {

// Shared helper for inbound sync packets that may begin with an optional
// ID_TIMESTAMP (40) + uint32_t timestamp prefix before the sync packet ID.
template <typename S>
inline bool serializeSyncHeader(S& s, uint8_t expectedPacketId, bool& hasTimestamp, uint32_t& timestamp)
{
	if constexpr (S::kReading) {
		uint8_t first = 0;
		if (!s.u8(first)) return false;
		if (first == static_cast<uint8_t>(ID_TIMESTAMP)) {
			hasTimestamp = true;
			if (!s.u32(timestamp)) return false;
			if (!s.u8(first)) return false;
		}
		else {
			hasTimestamp = false;
			timestamp = 0;
		}
		return first == expectedPacketId;
	}
	else {
		if (hasTimestamp) {
			if (!s.u8(static_cast<uint8_t>(ID_TIMESTAMP)) || !s.u32(timestamp)) return false;
		}
		return s.u8(expectedPacketId);
	}
}

// Packet 12 (bidirectional): ID_AUTH_KEY challenge / response
struct AuthKeyPacket {
	static constexpr uint8_t kPacketId = ID_AUTH_KEY;
	static constexpr const char* kName = "AuthKey";

	std::string key;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		if constexpr (S::kReading) {
			uint8_t pid = 0;
			if (!s.u8(pid) || pid != kPacketId) return false;
		}
		else {
			if (!s.u8(kPacketId)) return false;
		}
		return s.dynStr8(key);
	}
};

// Packet 34 (server -> client): ID_CONNECTION_REQUEST_ACCEPTED
struct ConnectionSucceededPacket {
	static constexpr uint8_t kPacketId = ID_CONNECTION_REQUEST_ACCEPTED;
	static constexpr const char* kName = "ConnectionSucceeded";

	uint32_t binaryAddress = 0;
	uint16_t port = 0;
	uint16_t playerId = 0xFFFF;
	uint32_t challenge = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		if constexpr (S::kReading) {
			uint8_t pid = 0;
			if (!s.u8(pid) || pid != kPacketId) return false;
		}
		else {
			if (!s.u8(kPacketId)) return false;
		}
		return s.u32(binaryAddress) && s.u16(port) && s.u16(playerId) && s.u32(challenge);
	}
};

// ============================================================================
// Inbound sync packets (server -> client)
// ============================================================================

// Packet 207 (server -> client): ID_PLAYER_SYNC (PlayerFootSync)
struct InboundPlayerSync {
	static constexpr uint8_t kPacketId = ID_PLAYER_SYNC;
	static constexpr const char* kName = "InPlayerSync";

	bool hasTimestamp = false;
	uint32_t timestamp = 0;
	uint16_t playerId = 0xFFFF;
	bool hasLR = false;
	uint16_t lrAnalog = 0;
	bool hasUD = false;
	uint16_t udAnalog = 0;
	uint16_t keys = 0;
	Vec3 position{};
	Quat quat{};
	uint8_t health = 0;
	uint8_t armour = 0;
	uint8_t currentWeapon = 0;
	uint8_t specialAction = 0;
	Vec3 moveSpeed{};
	bool hasSurfing = false;
	uint16_t surfInfo = 0xFFFF;
	Vec3 surfOffsets{};
	bool hasAnim = false;
	uint32_t animation = 0x80000000u;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		if (!serializeSyncHeader(s, kPacketId, hasTimestamp, timestamp)) return false;
		if (!s.u16(playerId)) return false;

		if (!s.bit(hasLR)) return false;
		if (hasLR && !s.u16(lrAnalog)) return false;

		if (!s.bit(hasUD)) return false;
		if (hasUD && !s.u16(udAnalog)) return false;

		if (!s.u16(keys) || !s.vec3(position) || !s.normQuat(quat)) return false;
		if (!s.healthArmour(health, armour)) return false;
		if (!s.u8(currentWeapon) || !s.u8(specialAction) || !s.compressedVec(moveSpeed)) return false;

		if (!s.bit(hasSurfing)) return false;
		if (hasSurfing) {
			if (!s.u16(surfInfo) || !s.vec3(surfOffsets)) return false;
		}
		else if constexpr (S::kReading) {
			surfInfo = 0xFFFF;
			surfOffsets = {};
		}

		if (!s.bit(hasAnim)) return false;
		if (hasAnim) {
			if (!s.u32(animation)) return false;
		}
		else if constexpr (S::kReading) {
			animation = 0x80000000u;
		}

		return s.ok();
	}
};

// Packet 200 (server -> client): ID_VEHICLE_SYNC (PlayerVehicleSync)
struct InboundVehicleSync {
	static constexpr uint8_t kPacketId = ID_VEHICLE_SYNC;
	static constexpr const char* kName = "InVehicleSync";

	bool hasTimestamp = false;
	uint32_t timestamp = 0;
	uint16_t playerId = 0xFFFF;
	uint16_t vehicleId = 0xFFFF;
	uint16_t lrAnalog = 0;
	uint16_t udAnalog = 0;
	uint16_t keys = 0;
	Quat quat{};
	Vec3 position{};
	Vec3 moveSpeed{};
	uint16_t carHealth = 0;
	uint8_t playerHealth = 0;
	uint8_t playerArmour = 0;
	uint8_t currentWeapon = 0;
	bool sirenOn = false;
	bool landingGearState = false;
	bool hasTrainOrHydra = false;
	uint32_t trainOrHydra = 0;
	bool hasTrailer = false;
	uint16_t trailerId = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		if (!serializeSyncHeader(s, kPacketId, hasTimestamp, timestamp)) return false;
		if (!s.u16(playerId) || !s.u16(vehicleId) ||
		    !s.u16(lrAnalog) || !s.u16(udAnalog) || !s.u16(keys)) {
			return false;
		}
		if (!s.normQuat(quat) || !s.vec3(position) || !s.compressedVec(moveSpeed)) return false;
		if (!s.u16(carHealth) || !s.healthArmour(playerHealth, playerArmour) || !s.u8(currentWeapon)) {
			return false;
		}
		if (!s.bit(sirenOn) || !s.bit(landingGearState)) return false;
		if (!s.bit(hasTrainOrHydra)) return false;
		if (hasTrainOrHydra && !s.u32(trainOrHydra)) return false;
		if (!s.bit(hasTrailer)) return false;
		if (hasTrailer && !s.u16(trailerId)) return false;
		return s.ok();
	}
};

// Packet 203 (server -> client): ID_AIM_SYNC (PlayerAimSync)
struct InboundAimSync {
	static constexpr uint8_t kPacketId = ID_AIM_SYNC;
	static constexpr const char* kName = "InAimSync";

	bool hasTimestamp = false;
	uint32_t timestamp = 0;
	uint16_t playerId = 0xFFFF;
	uint8_t camMode = 0;
	Vec3 aimF1{};
	Vec3 aimPos{};
	float aimZ = 0.0f;
	uint8_t zoomWeaponState = 0;
	uint8_t aspectRatio = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		if (!serializeSyncHeader(s, kPacketId, hasTimestamp, timestamp)) return false;
		return s.u16(playerId) && s.u8(camMode) && s.vec3(aimF1) && s.vec3(aimPos) &&
		       s.f32(aimZ) && s.u8(zoomWeaponState) && s.u8(aspectRatio);
	}
};

// Packet 206 (server -> client): ID_BULLET_SYNC (PlayerBulletSync)
struct InboundBulletSync {
	static constexpr uint8_t kPacketId = ID_BULLET_SYNC;
	static constexpr const char* kName = "InBulletSync";

	bool hasTimestamp = false;
	uint32_t timestamp = 0;
	uint16_t playerId = 0xFFFF;
	uint8_t targetType = 0;
	uint16_t targetId = 0xFFFF;
	Vec3 origin{};
	Vec3 pos{};
	Vec3 offset{};
	uint8_t weaponId = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		if (!serializeSyncHeader(s, kPacketId, hasTimestamp, timestamp)) return false;
		return s.u16(playerId) && s.u8(targetType) && s.u16(targetId) &&
		       s.vec3(origin) && s.vec3(pos) && s.vec3(offset) && s.u8(weaponId);
	}
};

// Packet 211 (server -> client): ID_PASSENGER_SYNC (PlayerPassengerSync)
struct InboundPassengerSync {
	static constexpr uint8_t kPacketId = ID_PASSENGER_SYNC;
	static constexpr const char* kName = "InPassengerSync";

	bool hasTimestamp = false;
	uint32_t timestamp = 0;
	uint16_t playerId = 0xFFFF;
	uint16_t vehicleId = 0xFFFF;
	uint8_t seatFlags = 0;
	uint8_t currentWeapon = 0;
	uint8_t playerHealth = 0;
	uint8_t playerArmour = 0;
	uint16_t lrAnalog = 0;
	uint16_t udAnalog = 0;
	uint16_t keys = 0;
	Vec3 position{};

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		if (!serializeSyncHeader(s, kPacketId, hasTimestamp, timestamp)) return false;
		return s.u16(playerId) && s.u16(vehicleId) && s.u8(seatFlags) && s.u8(currentWeapon) &&
		       s.u8(playerHealth) && s.u8(playerArmour) && s.u16(lrAnalog) && s.u16(udAnalog) &&
		       s.u16(keys) && s.vec3(position);
	}
};

// Packet 208 (server -> client): ID_MARKERS_SYNC (PlayerMarkersSync)
struct InboundMarkersSync {
	static constexpr uint8_t kPacketId = ID_MARKERS_SYNC;
	static constexpr const char* kName = "InMarkersSync";

	struct Entry {
		uint16_t playerId = 0xFFFF;
		bool active = false;
		int16_t x = 0;
		int16_t y = 0;
		int16_t z = 0;
	};

	bool hasTimestamp = false;
	uint32_t timestamp = 0;
	std::vector<Entry> entries;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		if (!serializeSyncHeader(s, kPacketId, hasTimestamp, timestamp)) return false;
		if constexpr (S::kReading) {
			int32_t count = 0;
			if (!s.i32(count) || count < 0 || count > 1004) return false;
			entries.clear();
			entries.reserve(static_cast<size_t>(count));
			for (int32_t i = 0; i < count; ++i) {
				Entry e{};
				if (!s.u16(e.playerId) || !s.bit(e.active)) return false;
				if (e.active) {
					if (!s.i16(e.x) || !s.i16(e.y) || !s.i16(e.z)) return false;
				}
				entries.push_back(e);
			}
		}
		else {
			int32_t count = static_cast<int32_t>(entries.size());
			if (!s.i32(count)) return false;
			for (const auto& e : entries) {
				if (!s.u16(e.playerId) || !s.bit(e.active)) return false;
				if (e.active) {
					if (!s.i16(e.x) || !s.i16(e.y) || !s.i16(e.z)) return false;
				}
			}
		}
		return s.ok();
	}
};

// Packet 210 (server -> client): ID_TRAILER_SYNC (PlayerTrailerSync)
struct InboundTrailerSync {
	static constexpr uint8_t kPacketId = ID_TRAILER_SYNC;
	static constexpr const char* kName = "InTrailerSync";

	bool hasTimestamp = false;
	uint32_t timestamp = 0;
	uint16_t playerId = 0xFFFF;
	uint16_t trailerId = 0xFFFF;
	Vec3 position{};
	Quat quat{}; // raw 4 floats in wire order (w, x, y, z matching sa::CQuaternion)
	Vec3 moveSpeed{};
	Vec3 turnSpeed{};

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		if (!serializeSyncHeader(s, kPacketId, hasTimestamp, timestamp)) return false;
		return s.u16(playerId) && s.u16(trailerId) && s.vec3(position) &&
		       s.f32(quat.w) && s.f32(quat.x) && s.f32(quat.y) && s.f32(quat.z) &&
		       s.vec3(moveSpeed) && s.vec3(turnSpeed);
	}
};

// Packet 209 (server -> client): ID_UNOCCUPIED_SYNC (PlayerUnoccupiedSync)
struct InboundUnoccupiedSync {
	static constexpr uint8_t kPacketId = ID_UNOCCUPIED_SYNC;
	static constexpr const char* kName = "InUnoccupiedSync";

	bool hasTimestamp = false;
	uint32_t timestamp = 0;
	uint16_t playerId = 0xFFFF;
	uint16_t vehicleId = 0xFFFF;
	uint8_t seatId = 0;
	Vec3 roll{};
	Vec3 direction{};
	Vec3 position{};
	Vec3 moveSpeed{};
	Vec3 turnSpeed{};
	float carHealth = 0.0f;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		if (!serializeSyncHeader(s, kPacketId, hasTimestamp, timestamp)) return false;
		return s.u16(playerId) && s.u16(vehicleId) && s.u8(seatId) &&
		       s.vec3(roll) && s.vec3(direction) && s.vec3(position) &&
		       s.vec3(moveSpeed) && s.vec3(turnSpeed) && s.f32(carHealth);
	}
};

// ============================================================================
// Outbound sync & raw packets (client -> server)
// ============================================================================

template <typename S>
inline bool serializeFixedPacketId(S& s, uint8_t expectedPacketId)
{
	if constexpr (S::kReading) {
		uint8_t pid = 0;
		return s.u8(pid) && pid == expectedPacketId;
	}
	else {
		return s.u8(expectedPacketId);
	}
}

// Packet 201 (client -> server): ID_RCON_COMMAND
struct RconCommandPacket {
	static constexpr uint8_t kPacketId = ID_RCON_COMMAND;
	static constexpr const char* kName = "OutRconCommand";

	std::string command;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return serializeFixedPacketId(s, kPacketId) && s.dynStr32(command, 4096);
	}
};

// Packet 207 (client -> server): ID_PLAYER_SYNC (69 bytes total)
struct OutboundPlayerSync {
	static constexpr uint8_t kPacketId = ID_PLAYER_SYNC;
	static constexpr const char* kName = "OutPlayerSync";

	uint16_t lrAnalog = 0;
	uint16_t udAnalog = 0;
	uint16_t keys = 0;
	Vec3 position{};
	Quat quat{}; // raw 4 floats (w, x, y, z)
	uint8_t health = 0;
	uint8_t armour = 0;
	uint8_t currentWeapon = 0;
	uint8_t specialAction = 0;
	Vec3 moveSpeed{};
	Vec3 surfOffsets{};
	uint16_t surfInfo = 0;
	uint32_t animation = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return serializeFixedPacketId(s, kPacketId) &&
		       s.u16(lrAnalog) && s.u16(udAnalog) && s.u16(keys) &&
		       s.vec3(position) &&
		       s.f32(quat.w) && s.f32(quat.x) && s.f32(quat.y) && s.f32(quat.z) &&
		       s.u8(health) && s.u8(armour) && s.u8(currentWeapon) && s.u8(specialAction) &&
		       s.vec3(moveSpeed) && s.vec3(surfOffsets) && s.u16(surfInfo) && s.u32(animation);
	}
};

// Packet 200 (client -> server): ID_VEHICLE_SYNC (64 bytes total)
struct OutboundVehicleSync {
	static constexpr uint8_t kPacketId = ID_VEHICLE_SYNC;
	static constexpr const char* kName = "OutVehicleSync";

	uint16_t vehicleId = 0xFFFF;
	uint16_t lrAnalog = 0;
	uint16_t udAnalog = 0;
	uint16_t keys = 0;
	Quat quat{};
	Vec3 position{};
	Vec3 moveSpeed{};
	float carHealth = 0.0f;
	uint8_t playerHealth = 0;
	uint8_t playerArmour = 0;
	uint8_t currentWeapon = 0;
	uint8_t sirenOn = 0;
	uint8_t landingGearState = 0;
	uint16_t trailerId = 0;
	uint32_t trainOrHydra = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return serializeFixedPacketId(s, kPacketId) &&
		       s.u16(vehicleId) && s.u16(lrAnalog) && s.u16(udAnalog) && s.u16(keys) &&
		       s.f32(quat.w) && s.f32(quat.x) && s.f32(quat.y) && s.f32(quat.z) &&
		       s.vec3(position) && s.vec3(moveSpeed) && s.f32(carHealth) &&
		       s.u8(playerHealth) && s.u8(playerArmour) && s.u8(currentWeapon) &&
		       s.u8(sirenOn) && s.u8(landingGearState) && s.u16(trailerId) && s.u32(trainOrHydra);
	}
};

// Packet 211 (client -> server): ID_PASSENGER_SYNC (25 bytes total)
struct OutboundPassengerSync {
	static constexpr uint8_t kPacketId = ID_PASSENGER_SYNC;
	static constexpr const char* kName = "OutPassengerSync";

	uint16_t vehicleId = 0xFFFF;
	uint8_t seatFlags = 0;
	uint8_t currentWeapon = 0;
	uint8_t playerHealth = 0;
	uint8_t playerArmour = 0;
	uint16_t lrAnalog = 0;
	uint16_t udAnalog = 0;
	uint16_t keys = 0;
	Vec3 position{};

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return serializeFixedPacketId(s, kPacketId) &&
		       s.u16(vehicleId) && s.u8(seatFlags) && s.u8(currentWeapon) &&
		       s.u8(playerHealth) && s.u8(playerArmour) &&
		       s.u16(lrAnalog) && s.u16(udAnalog) && s.u16(keys) && s.vec3(position);
	}
};

// Packet 209 (client -> server): ID_UNOCCUPIED_SYNC (68 bytes total)
struct OutboundUnoccupiedSync {
	static constexpr uint8_t kPacketId = ID_UNOCCUPIED_SYNC;
	static constexpr const char* kName = "OutUnoccupiedSync";

	uint16_t vehicleId = 0xFFFF;
	uint8_t seatId = 0;
	Vec3 roll{};
	Vec3 direction{};
	Vec3 position{};
	Vec3 moveSpeed{};
	Vec3 turnSpeed{};
	float carHealth = 0.0f;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return serializeFixedPacketId(s, kPacketId) &&
		       s.u16(vehicleId) && s.u8(seatId) &&
		       s.vec3(roll) && s.vec3(direction) && s.vec3(position) &&
		       s.vec3(moveSpeed) && s.vec3(turnSpeed) && s.f32(carHealth);
	}
};

// Packet 210 (client -> server): ID_TRAILER_SYNC (55 bytes total)
struct OutboundTrailerSync {
	static constexpr uint8_t kPacketId = ID_TRAILER_SYNC;
	static constexpr const char* kName = "OutTrailerSync";

	uint16_t trailerId = 0xFFFF;
	Vec3 position{};
	Quat quat{};
	Vec3 moveSpeed{};
	Vec3 turnSpeed{};

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return serializeFixedPacketId(s, kPacketId) &&
		       s.u16(trailerId) && s.vec3(position) &&
		       s.f32(quat.w) && s.f32(quat.x) && s.f32(quat.y) && s.f32(quat.z) &&
		       s.vec3(moveSpeed) && s.vec3(turnSpeed);
	}
};

// Packet 203 (client -> server): ID_AIM_SYNC (32 bytes total)
struct OutboundAimSync {
	static constexpr uint8_t kPacketId = ID_AIM_SYNC;
	static constexpr const char* kName = "OutAimSync";

	uint8_t camMode = 0;
	Vec3 aimF1{};
	Vec3 aimPos{};
	float aimZ = 0.0f;
	uint8_t zoomWeaponState = 0;
	uint8_t aspectRatio = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return serializeFixedPacketId(s, kPacketId) &&
		       s.u8(camMode) && s.vec3(aimF1) && s.vec3(aimPos) &&
		       s.f32(aimZ) && s.u8(zoomWeaponState) && s.u8(aspectRatio);
	}
};

// Packet 206 (client -> server): ID_BULLET_SYNC (41 bytes total)
struct OutboundBulletSync {
	static constexpr uint8_t kPacketId = ID_BULLET_SYNC;
	static constexpr const char* kName = "OutBulletSync";

	uint8_t targetType = 0;
	uint16_t targetId = 0xFFFF;
	Vec3 origin{};
	Vec3 pos{};
	Vec3 offset{};
	uint8_t weaponId = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return serializeFixedPacketId(s, kPacketId) &&
		       s.u8(targetType) && s.u16(targetId) &&
		       s.vec3(origin) && s.vec3(pos) && s.vec3(offset) && s.u8(weaponId);
	}
};

// Packet 212 (client -> server): ID_SPECTATOR_SYNC (19 bytes total)
struct OutboundSpectatorSync {
	static constexpr uint8_t kPacketId = ID_SPECTATOR_SYNC;
	static constexpr const char* kName = "OutSpectatorSync";

	uint16_t lrAnalog = 0;
	uint16_t udAnalog = 0;
	uint16_t keys = 0;
	Vec3 position{};

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return serializeFixedPacketId(s, kPacketId) &&
		       s.u16(lrAnalog) && s.u16(udAnalog) && s.u16(keys) && s.vec3(position);
	}
};

// Packet 205 (client -> server): ID_STATS_UPDATE (9 bytes total)
struct OutboundStatsUpdate {
	static constexpr uint8_t kPacketId = ID_STATS_UPDATE;
	static constexpr const char* kName = "OutStatsUpdate";

	int32_t money = 0;
	int32_t drunkLevel = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return serializeFixedPacketId(s, kPacketId) && s.i32(money) && s.i32(drunkLevel);
	}
};

// Packet 204 (client -> server): ID_WEAPONS_UPDATE (5 + 4*N bytes)
struct OutboundWeaponsUpdate {
	static constexpr uint8_t kPacketId = ID_WEAPONS_UPDATE;
	static constexpr const char* kName = "OutWeaponsUpdate";

	struct Slot {
		uint8_t slot = 0;
		uint8_t weapon = 0;
		uint16_t ammo = 0;
	};

	uint16_t targetPlayerId = 0xFFFF;
	uint16_t targetActorId = 0xFFFF;
	std::vector<Slot> slots;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		if (!serializeFixedPacketId(s, kPacketId) || !s.u16(targetPlayerId) || !s.u16(targetActorId)) {
			return false;
		}
		if constexpr (S::kReading) {
			slots.clear();
			while (s.unreadBits() > 0 && slots.size() < 13) {
				Slot entry{};
				if (!s.u8(entry.slot) || !s.u8(entry.weapon) || !s.u16(entry.ammo)) return false;
				slots.push_back(entry);
			}
		}
		else {
			for (const auto& entry : slots) {
				if (!s.u8(entry.slot) || !s.u8(entry.weapon) || !s.u16(entry.ammo)) return false;
			}
		}
		return s.ok();
	}
};

} // namespace proto
