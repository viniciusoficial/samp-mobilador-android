#include "Session.h"

#include <cstring>
#include <spdlog/spdlog.h>

void SessionState::reset(ResetReason reason)
{
	if (reason == ResetReason::Connect) {
		settings = ServerSettings{};
		settings.deathDropMoney = 0;
		settings.spawnsAvailable = 0;
		settings.nameTagLOS = true;
		settings.holdTime = true;
		settings.useCJWalk = false;
		settings.disableInteriorEnterExits = false;
		settings.zoneNames = false;
		settings.instagib = false;
		settings.nameTagDrawDistance = 60.0f;
		settings.friendlyFire = true;
		settings.worldTime_Hour = 12;
		settings.worldTime_Minute = 0;
		settings.weather = 1;
		settings.gravity = 0.008f;
		settings.disableVehicleCollisions = false;

		onFootSendRate = 30;
		inCarSendRate = 30;
		firingSendRate = 30;
		sendMultiplier = 2;
		lagCompensationMode = 0;

		std::memset(hostName, 0, sizeof(hostName));
		std::strcpy(hostName, "SA-MP");
		lanMode = false;
		cameraTargetEnabled = false;
		return;
	}

	// Restart / Disconnect (Packet_ConnectionLost -> ShutdownForGameModeRestart):
	// Preserve exact existing field-reset set; fields omitted below (spawnsAvailable,
	// instagib, allowWeapons, showNameTags, showPlayerMarkers, manualVehicleEngineAndLight,
	// vehicleFriendlyFire, limitGlobalChatRadius, globalChatRadius, worldBounds,
	// send rates, lagComp, hostName, lanMode) are overwritten by the next InitGame.
	// friendlyFire stays true (no InitGame field); Disconnection/Ban paths untouched (T02).
	settings.disableInteriorEnterExits = false;
	settings.nameTagDrawDistance = 60.0f;
	settings.worldTime_Hour = 12;
	settings.worldTime_Minute = 0;
	settings.weather = 1;
	settings.holdTime = true;
	settings.nameTagLOS = true;
	settings.useCJWalk = false;
	settings.gravity = 0.008f;
	settings.deathDropMoney = 0;
	settings.disableVehicleCollisions = false;
	settings.zoneNames = false;
	cameraTargetEnabled = false;
}

void SessionState::dumpToLog() const
{
	const char* verStr = (protoVersion == proto::ProtoVersion::V03DL) ? "0.3.DL" : "0.3.7";
	spdlog::info("[SESSION] hostName='{}' proto={} lanMode={} lagComp={}",
		hostName, verStr, lanMode ? 1 : 0, lagCompensationMode);
	spdlog::info("[SESSION] rates: onFoot={} inCar={} firing={} mult={}",
		onFootSendRate, inCarSendRate, firingSendRate, sendMultiplier);
	spdlog::info("[SESSION] world: time={:02d}:{:02d} holdTime={} weather={} gravity={:.4f} bounds=[{:.1f},{:.1f},{:.1f},{:.1f}]",
		(int) settings.worldTime_Hour, (int) settings.worldTime_Minute,
		settings.holdTime ? 1 : 0, (int) settings.weather, settings.gravity,
		settings.worldBounds[0], settings.worldBounds[1], settings.worldBounds[2], settings.worldBounds[3]);
	spdlog::info("[SESSION] rules: cjWalk={} allowWep={} enexDis={} vehColDis={} manualVeh={} vehFF={} ff={} instagib={} zoneNames={} dropMoney={} spawns={}",
		settings.useCJWalk ? 1 : 0, settings.allowWeapons ? 1 : 0,
		settings.disableInteriorEnterExits ? 1 : 0, settings.disableVehicleCollisions ? 1 : 0,
		settings.manualVehicleEngineAndLight ? 1 : 0, settings.vehicleFriendlyFire,
		settings.friendlyFire ? 1 : 0, settings.instagib ? 1 : 0,
		settings.zoneNames ? 1 : 0, settings.deathDropMoney, settings.spawnsAvailable);
	spdlog::info("[SESSION] tags/chat: showTags={} tagLOS={} tagDist={:.1f} markers={} limitChat={} chatRadius={:.1f}",
		settings.showNameTags ? 1 : 0, settings.nameTagLOS ? 1 : 0, settings.nameTagDrawDistance,
		settings.showPlayerMarkers, settings.limitGlobalChatRadius ? 1 : 0, settings.globalChatRadius);

	if (auto logger = spdlog::default_logger()) {
		logger->flush();
	}
}
