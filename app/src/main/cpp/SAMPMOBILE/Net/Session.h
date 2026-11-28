#pragma once

#include <cstdint>
#include "Proto/Version.h"

struct ServerSettings {
	bool useCJWalk = false;
	int deathDropMoney = 0;
	bool allowWeapons = false;
	float gravity = 0.008f;
	bool disableInteriorEnterExits = false;
	int vehicleFriendlyFire = 0;
	bool holdTime = true;
	bool instagib = false;
	bool zoneNames = false;
	bool friendlyFire = true;
	int spawnsAvailable = 0;
	float nameTagDrawDistance = 60.0f;
	bool manualVehicleEngineAndLight = false;
	uint8_t worldTime_Hour = 12;
	uint8_t worldTime_Minute = 0;
	uint8_t weather = 1;
	bool nameTagLOS = false;
	int showPlayerMarkers = 0;
	float globalChatRadius = 0.0f;
	bool showNameTags = false;
	bool limitGlobalChatRadius = false;
	float worldBounds[4]{};
	bool disableVehicleCollisions = false;
};

struct SessionState {
	enum class ResetReason {
		Connect,
		Restart,
		Disconnect,
	};

	ServerSettings settings{};

	// Sync send rates & lag compensation (populated by InitGame)
	int onFootSendRate = 30;
	int inCarSendRate = 30;
	int firingSendRate = 30;
	int sendMultiplier = 2;
	int lagCompensationMode = 0;

	proto::ProtoVersion protoVersion = proto::ProtoVersion::V037;
	char hostName[256 + 1]{};
	bool lanMode = false;
	bool cameraTargetEnabled = false;

	void reset(ResetReason reason);
	void dumpToLog() const;
};
