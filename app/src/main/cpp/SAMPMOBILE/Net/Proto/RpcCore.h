#pragma once

#include "RpcIds.h"
#include "Version.h"
#include "Wire.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace proto {

// RPC 139 (server -> client): InitGame
struct InitGame {
	static constexpr int kRpcId = rpc::kInitGame;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "InitGame";

	bool zoneNames = false;
	bool useCJWalk = false;
	bool allowWeapons = false;
	bool limitGlobalChatRadius = false;
	float globalChatRadius = 0.0f;
	bool stuntBonus = false;
	float nameTagDrawDistance = 0.0f;
	bool disableInteriorEnterExits = false;
	bool nameTagLOS = false; // Raw 9th bit (T35: server sends DisableNameTagLOS)
	bool manualVehicleEngineAndLight = false;
	int32_t spawnsAvailable = 0;
	uint16_t myPlayerId = 0;
	bool showNameTags = false;
	int32_t showPlayerMarkers = 0;
	uint8_t worldTimeHour = 0;
	uint8_t weather = 0;
	float gravity = 0.008f;
	bool lanMode = false;
	int32_t deathDropMoney = 0;
	bool instagib = false;
	int32_t onFootSendRate = 40;
	int32_t inCarSendRate = 40;
	int32_t firingSendRate = 40;
	int32_t sendMultiplier = 10;
	int32_t lagCompensation = 0;
	std::string hostName;
	std::array<uint8_t, 212> vehicleModels{};
	int32_t vehicleFriendlyFire = 0; // Optional trailing field

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.bit(zoneNames) && s.bit(useCJWalk) && s.bit(allowWeapons) && s.bit(limitGlobalChatRadius) &&
			s.f32(globalChatRadius) && s.bit(stuntBonus) && s.f32(nameTagDrawDistance) &&
			s.bit(disableInteriorEnterExits) && s.bit(nameTagLOS) && s.bit(manualVehicleEngineAndLight) &&
			s.i32(spawnsAvailable) && s.u16(myPlayerId) && s.bit(showNameTags) && s.i32(showPlayerMarkers) &&
			s.u8(worldTimeHour) && s.u8(weather) && s.f32(gravity) && s.bit(lanMode) && s.i32(deathDropMoney) &&
			s.bit(instagib) && s.i32(onFootSendRate) && s.i32(inCarSendRate) && s.i32(firingSendRate) &&
			s.i32(sendMultiplier) && s.i32(lagCompensation) && s.dynStr8(hostName) &&
			s.bytes(vehicleModels.data(), vehicleModels.size()) &&
			s.optionalTail([this](S& st) { return st.i32(vehicleFriendlyFire); });
	}
};

// RPC 137 (server -> client): ServerJoin
struct ServerJoin {
	static constexpr int kRpcId = rpc::kServerJoin;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "ServerJoin";

	uint16_t playerId = 0xFFFF;
	uint32_t markerColor = 0;
	uint8_t isNpc = 0;
	std::string name;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(playerId) && s.u32(markerColor) && s.u8(isNpc) && s.dynStr8(name);
	}
};

// RPC 138 (server -> client): ServerQuit
struct ServerQuit {
	static constexpr int kRpcId = rpc::kServerQuit;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "ServerQuit";

	uint16_t playerId = 0xFFFF;
	uint8_t reason = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(playerId) && s.u8(reason);
	}
};

// RPC 40 (server -> client): GameModeRestart
struct GameModeRestart {
	static constexpr int kRpcId = rpc::kGameModeRestart;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "GameModeRestart";

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.ok();
	}
};

// RPC 130 (server -> client): ConnectionRejected
struct ConnectionRejected {
	static constexpr int kRpcId = rpc::kConnectionRejected;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "ConnectionRejected";

	uint8_t reason = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u8(reason);
	}
};

// RPC 101 (server -> client): Chat
struct ChatMessage {
	static constexpr int kRpcId = rpc::kChat;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "ChatMessage";

	uint16_t playerId = 0xFFFF;
	std::string text;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(playerId) && s.dynStr8(text);
	}
};

// RPC 93 (server -> client): SendClientMessage
struct ClientMessage {
	static constexpr int kRpcId = rpc::kClientMessage;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "ClientMessage";

	uint32_t color = 0;
	std::string message;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u32(color) && s.dynStr32(message, 4096);
	}
};

// RPC 59 (server -> client): SetPlayerChatBubble
struct ChatBubble {
	static constexpr int kRpcId = rpc::kChatBubble;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "ChatBubble";

	uint16_t playerId = 0xFFFF;
	uint32_t color = 0;
	float drawDistance = 0.0f;
	uint32_t duration = 0;
	std::string text;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(playerId) && s.u32(color) && s.f32(drawDistance) && s.u32(duration) && s.dynStr8(text);
	}
};

// RPC 94 (server -> client): SetWorldTime
struct WorldTime {
	static constexpr int kRpcId = rpc::kWorldTime;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "WorldTime";

	uint8_t hour = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u8(hour);
	}
};

// RPC 152 (server -> client): SetWeather / SetPlayerWeather
struct Weather {
	static constexpr int kRpcId = rpc::kWeather;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "Weather";

	uint8_t weatherId = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u8(weatherId);
	}
};

// RPC 29 (server -> client): SetPlayerTime
struct SetTimeEx {
	static constexpr int kRpcId = rpc::kSetTimeEx;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SetTimeEx";

	uint8_t hour = 0;
	uint8_t minute = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u8(hour) && s.u8(minute);
	}
};

// RPC 30 (server -> client): TogglePlayerClock
struct ToggleClock {
	static constexpr int kRpcId = rpc::kToggleClock;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "ToggleClock";

	uint8_t toggle = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u8(toggle);
	}
};

// RPC 60 (server -> client): SendGameTimeUpdate
struct TimerUpdate {
	static constexpr int kRpcId = rpc::kTimerUpdate;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "TimerUpdate";

	uint32_t time = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u32(time);
	}
};

// RPC 155 (server -> client): UpdateScoresPingsIPs
struct UpdateScoresPingsIPs {
	static constexpr int kRpcId = rpc::kUpdateScoresPingsIPs;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "UpdateScoresPingsIPs";

	struct Entry {
		uint16_t playerId = 0xFFFF;
		int32_t score = 0;
		int32_t ping = 0;
	};

	std::vector<Entry> entries;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		if constexpr (S::kReading) {
			entries.clear();
			while (s.unreadBits() >= 80) {
				Entry e{};
				if (!s.u16(e.playerId) || !s.i32(e.score) || !s.i32(e.ping)) {
					return false;
				}
				entries.push_back(e);
			}
			// Reject partial trailing entry (>= 1 byte left over).
			return s.ok() && s.unreadBits() < 8;
		} else {
			for (const auto& e : entries) {
				if (!s.u16(e.playerId) || !s.i32(e.score) || !s.i32(e.ping)) {
					return false;
				}
			}
			return s.ok();
		}
	}
};

// RPC 103 (server -> client, both 0.3.7 and 0.3.DL): ClientCheck
struct ClientCheckRequest {
	static constexpr int kRpcId = rpc::kClientCheck;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "ClientCheck";

	uint8_t type = 0;
	uint32_t argument = 0;
	uint16_t offset = 0;
	uint16_t length = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u8(type) && s.u32(argument) && s.u16(offset) && s.u16(length);
	}
};

// RPC 146 (server -> client): SetGravity / SetPlayerGravity
struct SetGravity {
	static constexpr int kRpcId = rpc::kScrSetGravity;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SetGravity";

	float gravity = 0.008f;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.f32(gravity);
	}
};

// RPC 12 (server -> client): SetPlayerPos
struct SetPlayerPos {
	static constexpr int kRpcId = rpc::kScrSetPlayerPos;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SetPlayerPos";

	Vec3 position{};

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.vec3(position);
	}
};

// RPC 13 (server -> client): SetPlayerPosFindZ
struct SetPlayerPosFindZ {
	static constexpr int kRpcId = rpc::kScrSetPlayerPosFindZ;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SetPlayerPosFindZ";

	Vec3 position{};

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.vec3(position);
	}
};

// RPC 19 (server -> client): SetPlayerFacingAngle
struct SetPlayerFacingAngle {
	static constexpr int kRpcId = rpc::kScrSetPlayerFacingAngle;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SetPlayerFacingAngle";

	float angle = 0.0f;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.f32(angle);
	}
};

// RPC 14 (server -> client): SetPlayerHealth
struct SetPlayerHealth {
	static constexpr int kRpcId = rpc::kScrSetPlayerHealth;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SetPlayerHealth";

	float health = 100.0f;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.f32(health);
	}
};

// RPC 66 (server -> client): SetPlayerArmour
struct SetPlayerArmour {
	static constexpr int kRpcId = rpc::kScrSetPlayerArmour;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SetPlayerArmour";

	float armour = 0.0f;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.f32(armour);
	}
};

// RPC 72 (server -> client): SetPlayerColor
struct SetPlayerColor {
	static constexpr int kRpcId = rpc::kScrSetPlayerColor;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SetPlayerColor";

	uint16_t playerId = 0xFFFF;
	uint32_t color = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(playerId) && s.u32(color);
	}
};

// RPC 69 (server -> client): SetPlayerTeam
struct SetPlayerTeam {
	static constexpr int kRpcId = rpc::kScrSetPlayerTeam;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SetPlayerTeam";

	uint16_t playerId = 0xFFFF;
	uint8_t team = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(playerId) && s.u8(team);
	}
};

// RPC 11 (server -> client): SetPlayerName
struct SetPlayerName {
	static constexpr int kRpcId = rpc::kScrSetPlayerName;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SetPlayerName";

	uint16_t playerId = 0xFFFF;
	std::string name;
	uint8_t success = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(playerId) && s.dynStr8(name) && s.u8(success);
	}
};

// RPC 34 (server -> client): SetPlayerSkillLevel
struct SetPlayerSkillLevel {
	static constexpr int kRpcId = rpc::kSetPlayerSkillLevel;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SetPlayerSkillLevel";

	uint16_t playerId = 0xFFFF;
	uint32_t skillType = 0;
	uint16_t skillLevel = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(playerId) && s.u32(skillType) && s.u16(skillLevel);
	}
};

// RPC 153 (server -> client): SetPlayerSkin — 0.3.7 (u32+u32) / 0.3.DL (u16+u32+u32).
struct SetPlayerSkin {
	static constexpr int kRpcId = rpc::kScrSetPlayerSkin;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SetPlayerSkin";

	uint32_t playerId = 0;
	uint32_t skinId = 0;
	uint32_t customSkinId = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion v)
	{
		if (v == ProtoVersion::V03DL) {
			uint16_t id16 = static_cast<uint16_t>(playerId);
			if (!s.u16(id16)) return false;
			if constexpr (S::kReading) playerId = id16;
			return s.u32(skinId) && s.u32(customSkinId);
		}
		return s.u32(playerId) && s.u32(skinId);
	}
};

// RPC 156 (server -> client): SetPlayerInterior
struct SetPlayerInterior {
	static constexpr int kRpcId = rpc::kScrSetInterior;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SetPlayerInterior";

	uint8_t interiorId = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u8(interiorId);
	}
};

// RPC 15 (server -> client): TogglePlayerControllable
struct TogglePlayerControllable {
	static constexpr int kRpcId = rpc::kScrTogglePlayerControllable;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "TogglePlayerControllable";

	uint8_t controllable = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u8(controllable);
	}
};

// RPC 17 (server -> client): SetPlayerWorldBounds
struct SetWorldBounds {
	static constexpr int kRpcId = rpc::kScrSetWorldBounds;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SetWorldBounds";

	std::array<float, 4> bounds{}; // maxX, minX, maxY, minY

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.f32(bounds[0]) && s.f32(bounds[1]) && s.f32(bounds[2]) && s.f32(bounds[3]);
	}
};

// RPC 18 (server -> client): GivePlayerMoney
struct GivePlayerMoney {
	static constexpr int kRpcId = rpc::kScrHaveSomeMoney;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "GivePlayerMoney";

	int32_t money = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.i32(money);
	}
};

// RPC 20 (server -> client): ResetPlayerMoney
struct ResetPlayerMoney {
	static constexpr int kRpcId = rpc::kScrResetMoney;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "ResetPlayerMoney";

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.ok();
	}
};

// RPC 133 (server -> client): SetPlayerWantedLevel
struct SetPlayerWantedLevel {
	static constexpr int kRpcId = rpc::kScrSetPlayerWantedLevel;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SetPlayerWantedLevel";

	uint8_t level = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u8(level);
	}
};

// RPC 89 (server -> client): SetPlayerFightingStyle
struct SetPlayerFightingStyle {
	static constexpr int kRpcId = rpc::kScrSetFightingStyle;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SetPlayerFightingStyle";

	uint16_t playerId = 0xFFFF;
	uint8_t style = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(playerId) && s.u8(style);
	}
};

// RPC 90 (server -> client): SetPlayerVelocity
struct SetPlayerVelocity {
	static constexpr int kRpcId = rpc::kScrSetPlayerVelocity;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SetPlayerVelocity";

	Vec3 velocity{};

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.vec3(velocity);
	}
};

// RPC 88 (server -> client): SetPlayerSpecialAction
struct SetPlayerSpecialAction {
	static constexpr int kRpcId = rpc::kScrSetSpecialAction;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SetPlayerSpecialAction";

	uint8_t action = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u8(action);
	}
};

// RPC 35 (server -> client): SetPlayerDrunkLevel
struct SetPlayerDrunkLevel {
	static constexpr int kRpcId = rpc::kScrSetPlayerDrunkLevel;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SetPlayerDrunkLevel";

	int32_t level = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.i32(level);
	}
};

// RPC 92 (server -> client): SetPlayerDrunkVisuals
struct SetPlayerDrunkVisuals {
	static constexpr int kRpcId = rpc::kScrSetPlayerDrunkVisuals;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SetPlayerDrunkVisuals";

	uint32_t visuals = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u32(visuals);
	}
};

// RPC 150 (server -> client): SetPlayerDrunkHandling
struct SetPlayerDrunkHandling {
	static constexpr int kRpcId = rpc::kScrSetPlayerDrunkHandling;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SetPlayerDrunkHandling";

	uint32_t handling = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u32(handling);
	}
};

// RPC 21 (server -> client): ResetPlayerWeapons
struct ResetPlayerWeapons {
	static constexpr int kRpcId = rpc::kScrResetPlayerWeapons;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "ResetPlayerWeapons";

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.ok();
	}
};

// RPC 22 (server -> client): GivePlayerWeapon
struct GivePlayerWeapon {
	static constexpr int kRpcId = rpc::kScrGivePlayerWeapon;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "GivePlayerWeapon";

	int32_t weapon = 0;
	int32_t ammo = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.i32(weapon) && s.i32(ammo);
	}
};

// RPC 145 (server -> client): SetPlayerAmmo
struct SetPlayerAmmo {
	static constexpr int kRpcId = rpc::kScrSetWeaponAmmo;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SetPlayerAmmo";

	uint8_t weapon = 0;
	uint16_t ammo = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u8(weapon) && s.u16(ammo);
	}
};

// RPC 67 (server -> client): SetPlayerArmedWeapon
struct SetArmedWeapon {
	static constexpr int kRpcId = rpc::kScrSetArmedWeapon;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SetArmedWeapon";

	uint32_t weapon = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u32(weapon);
	}
};

// RPC 73 (server -> client): GameTextForPlayer
struct ShowGameText {
	static constexpr int kRpcId = rpc::kScrDisplayGameText;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "ShowGameText";

	int32_t style = 0;
	int32_t time = 0;
	std::string text;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.i32(style) && s.i32(time) && s.dynStr32(text, 4096);
	}
};

// RPC 55 (server -> client): SendDeathMessage
struct DeathMessage {
	static constexpr int kRpcId = rpc::kScrDeathMessage;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "DeathMessage";

	uint16_t killerId = 0xFFFF;
	uint16_t playerId = 0xFFFF;
	uint8_t reason = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(killerId) && s.u16(playerId) && s.u8(reason);
	}
};

// RPC 80 (server -> client): ShowPlayerNameTagForPlayer
struct ShowNameTag {
	static constexpr int kRpcId = rpc::kScrShowNameTag;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "ShowNameTag";

	uint16_t playerId = 0xFFFF;
	uint8_t show = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(playerId) && s.u8(show);
	}
};

// RPC 56 (server -> client): SetPlayerMapIcon
struct SetPlayerMapIcon {
	static constexpr int kRpcId = rpc::kScrSetMapIcon;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SetPlayerMapIcon";

	uint8_t iconId = 0;
	Vec3 position{};
	uint8_t type = 0;
	uint32_t color = 0;
	uint8_t style = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u8(iconId) && s.vec3(position) && s.u8(type) && s.u32(color) && s.u8(style);
	}
};

// RPC 144 (server -> client): RemovePlayerMapIcon
struct RemovePlayerMapIcon {
	static constexpr int kRpcId = rpc::kScrDisableMapIcon;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "RemovePlayerMapIcon";

	uint8_t iconId = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u8(iconId);
	}
};

// RPC 79 (server -> client): CreateExplosion
struct CreateExplosion {
	static constexpr int kRpcId = rpc::kScrCreateExplosion;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "CreateExplosion";

	Vec3 position{};
	uint32_t type = 0;
	float radius = 0.0f;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.vec3(position) && s.u32(type) && s.f32(radius);
	}
};

// RPC 16 (server -> client): PlayerPlaySound
struct PlayerPlaySound {
	static constexpr int kRpcId = rpc::kScrPlaySound;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "PlayerPlaySound";

	int32_t soundId = 0;
	Vec3 position{};

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.i32(soundId) && s.vec3(position);
	}
};

// RPC 104 (server -> client): EnableStuntBonusForPlayer
struct EnableStuntBonus {
	static constexpr int kRpcId = rpc::kScrEnableStuntBonus;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "EnableStuntBonus";

	bool enable = false;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.bit(enable);
	}
};

// RPC 41 (server -> client): PlayAudioStreamForPlayer
struct PlayAudioStream {
	static constexpr int kRpcId = rpc::kScrPlayAudioStream;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "PlayAudioStream";

	std::string url;
	Vec3 position{};
	float radius = 0.0f;
	uint8_t usePos = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.dynStr8(url) && s.vec3(position) && s.f32(radius) && s.u8(usePos);
	}
};

// RPC 42 (server -> client): StopAudioStreamForPlayer
struct StopAudioStream {
	static constexpr int kRpcId = rpc::kScrStopAudioStream;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "StopAudioStream";

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.ok();
	}
};

// RPC 43 (server -> client): RemoveBuildingForPlayer
struct RemoveBuilding {
	static constexpr int kRpcId = rpc::kScrRemoveBuilding;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "RemoveBuilding";

	int32_t modelId = 0;
	Vec3 position{};
	float radius = 0.0f;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.i32(modelId) && s.vec3(position) && s.f32(radius);
	}
};

// RPC 167 (server -> client): DisableRemoteVehicleCollisions
struct DisableVehicleCollisions {
	static constexpr int kRpcId = rpc::kScrDisableVehicleCollisions;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "DisableVehicleCollisions";

	bool disable = false;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.bit(disable);
	}
};

// Batch 3: Core lower half (streaming, animations, camera, spectate, widescreen)

// RPC 32 (server -> client): PlayerStreamIn / WorldPlayerAdd
struct WorldPlayerAdd {
	static constexpr int kRpcId = rpc::kWorldPlayerAdd;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "WorldPlayerAdd";

	uint16_t playerId = 0xFFFF;
	uint8_t team = 0;
	int32_t skin = 0;
	int32_t customSkin = 0; // 0.3.DL only
	Vec3 position{};
	float rotation = 0.0f;
	uint32_t color = 0;
	uint8_t fightingStyle = 0;
	std::array<uint16_t, 11> skillLevels{};

	template <typename S>
	bool serialize(S& s, ProtoVersion v)
	{
		if (!s.u16(playerId) || !s.u8(team) || !s.i32(skin)) return false;
		if (v == ProtoVersion::V03DL) {
			if (!s.i32(customSkin)) return false;
		}
		if (!s.vec3(position) || !s.f32(rotation) || !s.u32(color) || !s.u8(fightingStyle)) {
			return false;
		}
		s.optionalTail([&](auto& tail) {
			if constexpr (S::kReading) {
				std::array<uint16_t, 11> tmp{};
				for (auto& lvl : tmp) {
					if (!tail.u16(lvl)) return false;
				}
				skillLevels = tmp;
				return true;
			}
			else {
				for (auto lvl : skillLevels) {
					if (!tail.u16(lvl)) return false;
				}
				return true;
			}
		});
		return s.ok();
	}
};

// RPC 166 (server -> client): PlayerDeath / WorldPlayerDeath
struct WorldPlayerDeath {
	static constexpr int kRpcId = rpc::kWorldPlayerDeath;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "WorldPlayerDeath";

	uint16_t playerId = 0xFFFF;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(playerId);
	}
};

// RPC 163 (server -> client): PlayerStreamOut / WorldPlayerRemove
struct WorldPlayerRemove {
	static constexpr int kRpcId = rpc::kWorldPlayerRemove;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "WorldPlayerRemove";

	uint16_t playerId = 0xFFFF;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(playerId);
	}
};

// RPC 86 (server -> client): ApplyPlayerAnimation
struct ApplyPlayerAnimation {
	static constexpr int kRpcId = rpc::kScrApplyPlayerAnimation;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "ApplyPlayerAnimation";

	uint16_t playerId = 0xFFFF;
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
		return s.u16(playerId) && s.dynStr8(animLib) && s.dynStr8(animName) &&
		       s.f32(delta) && s.bit(loop) && s.bit(lockX) && s.bit(lockY) &&
		       s.bit(freeze) && s.i32(time);
	}
};

// RPC 87 (server -> client): ClearPlayerAnimations / ClearPlayerTasks
struct ClearPlayerAnimations {
	static constexpr int kRpcId = rpc::kScrClearPlayerAnimations;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "ClearPlayerAnimations";

	uint16_t playerId = 0xFFFF;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(playerId);
	}
};

// RPC 157 (server -> client): SetPlayerCameraPos
struct SetCameraPos {
	static constexpr int kRpcId = rpc::kScrSetCameraPos;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SetCameraPos";

	Vec3 position{};

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.vec3(position);
	}
};

// RPC 158 (server -> client): SetPlayerCameraLookAt
struct SetCameraLookAt {
	static constexpr int kRpcId = rpc::kScrSetCameraLookAt;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SetCameraLookAt";

	Vec3 position{};
	uint8_t cutType = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.vec3(position) && s.u8(cutType);
	}
};

// RPC 162 (server -> client): SetCameraBehindPlayer
struct SetCameraBehindPlayer {
	static constexpr int kRpcId = rpc::kScrSetCameraBehindPlayer;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SetCameraBehindPlayer";

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.ok();
	}
};

// RPC 82 (server -> client): InterpolateCameraPos / InterpolateCameraLookAt
struct InterpolateCamera {
	static constexpr int kRpcId = rpc::kScrInterpolateCamera;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "InterpolateCamera";

	bool setPos = false;
	Vec3 from{};
	Vec3 to{};
	int32_t time = 0;
	uint8_t cutType = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.bit(setPos) && s.vec3(from) && s.vec3(to) && s.i32(time) && s.u8(cutType);
	}
};

// RPC 81 (server -> client): AttachCameraToObject
struct AttachCameraToObject {
	static constexpr int kRpcId = rpc::kScrAttachCameraToObject;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "AttachCameraToObject";

	uint16_t objectId = 0xFFFF;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(objectId);
	}
};

// RPC 124 (server -> client): TogglePlayerSpectating
struct TogglePlayerSpectating {
	static constexpr int kRpcId = rpc::kScrTogglePlayerSpectating;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "TogglePlayerSpectating";

	uint32_t toggle = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u32(toggle);
	}
};

// RPC 126 (server -> client): PlayerSpectatePlayer
struct SpectatePlayer {
	static constexpr int kRpcId = rpc::kScrPlayerSpectatePlayer;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SpectatePlayer";

	uint16_t playerId = 0xFFFF;
	uint8_t mode = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(playerId) && s.u8(mode);
	}
};

// RPC 127 (server -> client): PlayerSpectateVehicle
struct SpectateVehicle {
	static constexpr int kRpcId = rpc::kScrPlayerSpectateVehicle;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SpectateVehicle";

	uint16_t vehicleId = 0xFFFF;
	uint8_t mode = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(vehicleId) && s.u8(mode);
	}
};

// RPC 111 (server -> client): ToggleWidescreen
struct ToggleWidescreen {
	static constexpr int kRpcId = rpc::kScrToggleWidescreen;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "ToggleWidescreen";

	uint8_t toggle = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u8(toggle);
	}
};

// RPC 170 (server -> client): EnablePlayerCameraTarget
struct EnableCameraTarget {
	static constexpr int kRpcId = rpc::kScrEnableCameraTarget;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "EnableCameraTarget";

	bool enable = false;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.bit(enable);
	}
};

// RPC 33 (server -> client): SetPlayerShopName
struct SetPlayerShopName {
	static constexpr int kRpcId = rpc::kScrSetPlayerShopName;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SetPlayerShopName";

	std::string shopName;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.fixedStr(shopName, 32);
	}
};

// RPC 112 (server -> client): PlayCrimeReportForPlayer
struct PlayCrimeReport {
	static constexpr int kRpcId = rpc::kScrPlayCrimeReport;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "PlayCrimeReport";

	uint16_t suspectId = 0xFFFF;
	uint32_t inVehicle = 0;
	uint32_t vehicleModel = 0;
	uint32_t vehicleColor = 0;
	uint32_t crimeId = 0;
	Vec3 position{};

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u16(suspectId) && s.u32(inVehicle) && s.u32(vehicleModel) &&
		       s.u32(vehicleColor) && s.u32(crimeId) && s.vec3(position);
	}
};

// RPC 102 (server -> client): Server network statistics snapshot (0x128 bytes)
struct SvrStats {
	static constexpr int kRpcId = rpc::kSvrStats;
	static constexpr uint8_t kVersionMask = kVerAll;
	static constexpr const char* kName = "SvrStats";

	std::array<uint8_t, 296> raw{};

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.bytes(raw.data(), raw.size());
	}
};

// RPC 48 (server -> client, 0.3.DL): SetPlayerVirtualWorld
struct SetPlayerVirtualWorld {
	static constexpr int kRpcId = rpc::kScrSetPlayerVirtualWorld;
	static constexpr uint8_t kVersionMask = kVer03DL;
	static constexpr const char* kName = "SetPlayerVirtualWorld";

	int32_t virtualWorld = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.i32(virtualWorld);
	}
};

// RPC 179 (server -> client, 0.3.DL): Custom model manifest entry
struct DlModelRequest {
	static constexpr int kRpcId = rpc::kDlModelRequest;
	static constexpr uint8_t kVersionMask = kVer03DL;
	static constexpr const char* kName = "DlModelRequest";

	uint32_t poolId = 0;
	int32_t count = 0;
	uint8_t type = 0;
	int32_t virtualWorld = 0;
	int32_t baseId = 0;
	int32_t newId = 0;
	uint32_t dffChecksum = 0;
	uint32_t txdChecksum = 0;
	uint32_t dffSize = 0;
	uint32_t txdSize = 0;
	uint8_t timeOn = 0;
	uint8_t timeOff = 0;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.u32(poolId) && s.i32(count) && s.u8(type) &&
		       s.i32(virtualWorld) && s.i32(baseId) && s.i32(newId) &&
		       s.u32(dffChecksum) && s.u32(txdChecksum) &&
		       s.u32(dffSize) && s.u32(txdSize) &&
		       s.u8(timeOn) && s.u8(timeOff);
	}
};

// RPC 180 (server -> client, 0.3.DL): Reset custom models
struct DlModelReset {
	static constexpr int kRpcId = rpc::kDlModelReset;
	static constexpr uint8_t kVersionMask = kVer03DL;
	static constexpr const char* kName = "DlModelReset";

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.ok();
	}
};

// RPC 183 (server -> client, 0.3.DL): Custom model download command (HTTP URL / Abort / Direct)
struct DlModelUrl {
	static constexpr int kRpcId = rpc::kDlModelUrl;
	static constexpr uint8_t kVersionMask = kVer03DL;
	static constexpr const char* kName = "DlModelUrl";

	static constexpr uint8_t kKindAbort = 0;
	static constexpr uint8_t kKindDirectChunk = 1;
	static constexpr uint8_t kKindDirectFinish = 2;
	static constexpr uint8_t kKindDirectBegin = 5;
	static constexpr uint8_t kKindUrlRedirect = 6;

	uint8_t kind = 0;
	uint8_t fileType = 0;
	uint32_t fileChecksum = 0;
	std::string downloadLink;

	// Direct-chunk fields (kind == 1, legacy 0.3.DL server fallback)
	uint8_t chunkStage = 0;
	uint32_t chunkUnpackedLen = 0;
	std::vector<uint8_t> chunkData;

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		if (!s.u8(kind)) return false;
		if (kind == kKindUrlRedirect) {
			return s.u8(fileType) && s.u32(fileChecksum) && s.dynStr8(downloadLink);
		}
		if (kind == kKindDirectBegin) {
			return s.u8(fileType) && s.u32(fileChecksum);
		}
		if (kind == kKindAbort || kind == kKindDirectFinish) {
			return s.ok();
		}
		if (kind == kKindDirectChunk) {
			if (!s.u8(chunkStage) || !s.u32(chunkUnpackedLen)) return false;
			uint32_t len = static_cast<uint32_t>(chunkData.size());
			if (!s.u32(len)) return false;
			if (len > 4096u) return false;
			if constexpr (S::kReading) {
				chunkData.resize(len);
			}
			if (len > 0 && !s.bytes(chunkData.data(), len)) return false;
			return s.ok();
		}
		return false;
	}
};

// RPC 185 (server -> client, 0.3.DL): Server acknowledges FinishDownload
struct DlDownloadCompleted {
	static constexpr int kRpcId = rpc::kDlDownloadCompleted;
	static constexpr uint8_t kVersionMask = kVer03DL;
	static constexpr const char* kName = "DlDownloadCompleted";

	template <typename S>
	bool serialize(S& s, ProtoVersion)
	{
		return s.ok();
	}
};

} // namespace proto
