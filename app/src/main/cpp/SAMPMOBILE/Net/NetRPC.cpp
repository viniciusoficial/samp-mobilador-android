#include "NetGame.h"
#include "LocalPlayer.h"
#include "RemotePlayer.h"
#include "VehiclePool.h"
#include "TextDrawPool.h"
#include "TextLabelPool.h"
#include "PlayerBubblePool.h"
#include "PickupPool.h"
#include "GangZonePool.h"
#include "ActorPool.h"
#include "MenuPool.h"
#include "RpcHandlers.h"
#include "ScmEvents.h"
#include "SessionHooks.h"
#include "Proto/RpcOutbound.h"
#include "../Client.h"
#include "../Settings.h"
#include "../Game/Game.h"
#include "../Game/Artwork/ArtworkManager.h"
#include "../Game/PlayerPed.h"
#include "../Game/RadarColors.h"
#include "../Game/Vehicle.h"
#include "../Game/Util.h"
#include "../UI/UI.h"
#include "../UI/SAMPWidgets/Chat.h"
#include "../UI/SAMPWidgets/DeathWindow.h"
#include "../UI/SAMPWidgets/NetStats.h"
#include "../UI/SAMPWidgets/Scoreboard.h"
#include "../UI/SAMPWidgets/VoiceButton.h"
#include "../Java/Java.h"
#include "../Java/EditObject.h"
#include <RakNet/RakClientInterface.h>
#include <RakNet/StringCompressor.h>
#include <RakNet/SAMP/SAMPRPC.h>
#include <RakNet/GetTime.h>
#include "../L10n.h"
#include <encoding.h>
#include <spdlog/spdlog.h>
#include <cmath>


#define REJECT_REASON_BAD_VERSION 1
#define REJECT_REASON_BAD_NICKNAME 2
#define REJECT_REASON_BAD_MOD 3
#define REJECT_REASON_BAD_PLAYERID 4

void RpcHandlers::OnInitGame(const proto::InitGame& p)
{
	spdlog::info("RPC: InitGame");

	CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
	if (!pPlayerPool) return;

	SessionState& sess = pNetGame->session();
	sess.settings.zoneNames = p.zoneNames;
	sess.settings.useCJWalk = p.useCJWalk;
	sess.settings.allowWeapons = p.allowWeapons;
	sess.settings.limitGlobalChatRadius = p.limitGlobalChatRadius;
	sess.settings.globalChatRadius = p.globalChatRadius;
	sess.settings.nameTagDrawDistance = p.nameTagDrawDistance;
	sess.settings.disableInteriorEnterExits = p.disableInteriorEnterExits;
	// Protocol bit 9 is DisableNameTagLOS (1 = LOS disabled, 0 = LOS enabled).
	sess.settings.nameTagLOS = !p.nameTagLOS;
	sess.settings.manualVehicleEngineAndLight = p.manualVehicleEngineAndLight;
	sess.settings.spawnsAvailable = p.spawnsAvailable;
	sess.settings.showNameTags = p.showNameTags;
	sess.settings.showPlayerMarkers = p.showPlayerMarkers;
	sess.settings.worldTime_Hour = p.worldTimeHour;
	sess.settings.weather = p.weather;
	sess.settings.gravity = std::isfinite(p.gravity) ? p.gravity : 0.008f;
	sess.settings.deathDropMoney = p.deathDropMoney;
	sess.settings.instagib = p.instagib;

	sess.onFootSendRate = std::clamp(p.onFootSendRate, 10, 5000);
	sess.inCarSendRate = std::clamp(p.inCarSendRate, 10, 5000);
	sess.firingSendRate = std::clamp(p.firingSendRate, 10, 5000);
	sess.sendMultiplier = std::clamp(p.sendMultiplier, 1, 20);

	switch (p.lagCompensation) {
		case 1: sess.lagCompensationMode = 0;
			break;
		case 2: sess.lagCompensationMode = 1;
			break;
		default: sess.lagCompensationMode = 2;
			break;
	}

	if (pGame) {
		pGame->DisableAutoAim();
	}

	memset(sess.hostName, 0, sizeof(sess.hostName));
	strncpy(sess.hostName, p.hostName.c_str(), sizeof(sess.hostName) - 1);
	sess.hostName[sizeof(sess.hostName) - 1] = '\0';

	// pGame->PreloadVehicleModels(p.vehicleModels.data());

	sess.settings.vehicleFriendlyFire = p.vehicleFriendlyFire;
	if (p.myPlayerId < MAX_PLAYERS) {
		pPlayerPool->SetLocalPlayerID(p.myPlayerId);
	}
	if (pGame) {
		pGame->EnableStuntBonus(p.stuntBonus);
	}

	sess.lanMode = p.lanMode;

	pNetGame->InitGameLogic();
	if (pGame) {
		pGame->SetGravity(sess.settings.gravity);
		if (sess.settings.disableInteriorEnterExits) {
			pGame->DisableEnterExits();
		}
	}

	pGame->SetWorldWeather(sess.settings.weather);
	pGame->ToggleCJWalk(sess.settings.useCJWalk);

	spdlog::info("szHostName, {}", sess.hostName);
	spdlog::debug("vehicleFriendlyFire, {}", sess.settings.vehicleFriendlyFire);

	if (pUI) {
		pUI->chat()->addDebugMessage(L10N("Connected to {B9C9BF}%s", "已连接到 {B9C9BF}%s"), Encoding::cp2utf(sess.hostName).c_str());
		pUI->voicebutton()->setVisible(Settings::voice() && Settings::voiceMicEnable());
	}

	pNetGame->SetGameState(GAMESTATE_CONNECTED);

	pPlayerPool->GetLocalPlayer()->HandleClassSelection();
}

//Prompt box rpc processing
void RpcHandlers::OnShowDialog(const proto::ShowDialog& p)
{
	spdlog::debug("RPC: DialogBox");
	spdlog::debug("wDialogID {}", p.id);
	spdlog::debug("byteDialogStyle {}", (int) p.style);
	spdlog::debug("szTitle {}", p.title);
	spdlog::debug("szButton1 {}", p.button1);
	spdlog::debug("szButton2 {}", p.button2);
	spdlog::debug("szMessage {}", p.body);

	if (p.id < 0) {
		if (g_java) {
			g_java->hideDialog();
		}
		return;
	}

	if (g_java) {
		std::string uTitle = Encoding::cp2utf(p.title.c_str());
		std::string uMessage = Encoding::cp2utf(p.body.c_str());
		std::string uBtn1 = Encoding::cp2utf(p.button1.c_str());
		std::string uBtn2 = Encoding::cp2utf(p.button2.c_str());
		g_java->showDialog(p.id, p.style,
			const_cast<char*>(uTitle.c_str()),
			const_cast<char*>(uMessage.c_str()),
			const_cast<char*>(uBtn1.c_str()),
			const_cast<char*>(uBtn2.c_str()));
	}
}

//chat handling
void RpcHandlers::OnChatMessage(const proto::ChatMessage& p)
{
	if (pNetGame->GetGameState() != GAMESTATE_CONNECTED) {
		return;
	}

	CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
	if (!pPlayerPool) return;
	if (p.playerId == pPlayerPool->GetLocalPlayerID()) {
		if (pUI && pUI->chat() && pPlayerPool->GetLocalPlayer()) {
			pUI->chat()->addChatMessage(Encoding::cp2utf(p.text.c_str()), Encoding::cp2utf(pPlayerPool->GetLocalPlayerName()), pPlayerPool->GetLocalPlayer()->GetPlayerColorAsRGBA());
		}
	}
	else {
		CRemotePlayer* pRemotePlayer = pPlayerPool->GetAt(p.playerId);
		if (pRemotePlayer) {
			pRemotePlayer->Say(const_cast<char*>(p.text.c_str()));
		}
	}
}

//client message
void RpcHandlers::OnClientMessage(const proto::ClientMessage& p)
{
	spdlog::debug("RPC: Client message ");
	if (p.message.empty()) {
		return;
	}

	spdlog::debug("dwColor {}", p.color);
	spdlog::debug("dwStrLen {}", p.message.size());
	spdlog::debug("szMsg {}", p.message);

	if (pUI && pUI->chat()) {
		pUI->chat()->addClientMessage(Encoding::cp2utf(p.message.c_str()), p.color);
	}
}

void RpcHandlers::OnRequestClass(const proto::RequestClassReply& p)
{
	spdlog::debug("RPC: Request class");

	CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
	if (!pPlayerPool || !pPlayerPool->GetLocalPlayer()) return;
	CLocalPlayer* pLocalPlayer = pPlayerPool->GetLocalPlayer();

	PLAYER_SPAWN_INFO SpawnInfo{};
	SpawnInfo.byteTeam = p.spawnInfo.team;
	SpawnInfo.iSkin = p.spawnInfo.skin;
	SpawnInfo.unk = p.spawnInfo.unk;
	SpawnInfo.vecPos = sa::CVector(p.spawnInfo.position.x, p.spawnInfo.position.y, p.spawnInfo.position.z);
	SpawnInfo.fRotation = p.spawnInfo.rotation;
	memcpy(SpawnInfo.iSpawnWeapons, p.spawnInfo.spawnWeapons.data(), sizeof(SpawnInfo.iSpawnWeapons));
	memcpy(SpawnInfo.iSpawnWeaponsAmmo, p.spawnInfo.spawnWeaponsAmmo.data(), sizeof(SpawnInfo.iSpawnWeaponsAmmo));

	if (p.selectable) {
		pLocalPlayer->SetSpawnInfo(&SpawnInfo, p.spawnInfo.customSkin);
		pLocalPlayer->HandleClassSelectionOutcome(true);
	}
	else {
		pLocalPlayer->HandleClassSelectionOutcome(false);
	}
}

//Request spawn players
void RpcHandlers::OnRequestSpawn(const proto::RequestSpawnReply& p)
{
	spdlog::debug("RPC: Request spawn");

	CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
	if (!pPlayerPool || !pPlayerPool->GetLocalPlayer()) return;
	CLocalPlayer* pLocalPlayer = pPlayerPool->GetLocalPlayer();

	spdlog::debug("byteRequestOutcome {}", p.outcome);

	if (p.outcome == 2 || (p.outcome && pLocalPlayer->m_bWaitingForSpawnRequestReply)) {
		pLocalPlayer->Spawn();
	}
	else {
		pLocalPlayer->m_bWaitingForSpawnRequestReply = false;
	}
}

void RpcHandlers::OnToggleClock(const proto::ToggleClock& p)
{
	spdlog::debug("RPC: Toggle clock");
	spdlog::debug("byteClock {}", p.toggle);

	if (pGame) {
		pGame->EnableClock(p.toggle != 0);
	}

	if (p.toggle) {
		pNetGame->m_netSet->holdTime = false;
	}
	else {
		pNetGame->m_netSet->holdTime = true;
		int hour = 0, minute = 0;
		if (pGame) {
			pGame->GetWorldTime(&hour, &minute);
		}
		pNetGame->m_netSet->worldTime_Hour = static_cast<uint8_t>(hour);
		pNetGame->m_netSet->worldTime_Minute = static_cast<uint8_t>(minute);
	}
}

void RpcHandlers::OnSetTimeEx(const proto::SetTimeEx& p)
{
	spdlog::debug("RPC: SetTimeEx");
	spdlog::debug("byteHour {}", p.hour);
	spdlog::debug("byteMinute {}", p.minute);

	if (pGame) pGame->SetWorldTime(p.hour, p.minute);
	pNetGame->m_netSet->worldTime_Hour = p.hour;
	pNetGame->m_netSet->worldTime_Minute = p.minute;
}

void RpcHandlers::OnWeather(const proto::Weather& p)
{
	spdlog::debug("RPC: Weather");
	spdlog::debug("weather {}", p.weatherId);

	pNetGame->m_netSet->weather = p.weatherId;
	if (pGame) pGame->SetWorldWeather(p.weatherId);
}

void RpcHandlers::OnWorldTime(const proto::WorldTime& p)
{
	spdlog::debug("RPC: World time");
	spdlog::debug("byteWorldTIme {}", p.hour);

	pNetGame->m_netSet->worldTime_Hour = p.hour;
	if (pGame) {
		pGame->SetWorldTime(p.hour, pNetGame->m_netSet->worldTime_Minute);
	}
}

void RpcHandlers::OnConnectionRejected(const proto::ConnectionRejected& p)
{
	switch (p.reason) {
		case REJECT_REASON_BAD_VERSION:
			if (pUI) {
				pUI->chat()->addInfoMessage(L10N("CONNECTION REJECTED: Incorrect Version.", "连接被拒绝：版本不正确。"));
			}

			break;

		case REJECT_REASON_BAD_NICKNAME:
			if (pUI) {
				pUI->chat()->addInfoMessage(L10N("CONNECTION REJECTED: Unacceptable NickName", "连接被拒绝：昵称不可用"));
				pUI->chat()->addInfoMessage(L10N("Please choose another nick between and 3-20 characters", "请改用 3-20 个字符的昵称"));
				pUI->chat()->addInfoMessage(L10N("Please use only a-z, A-Z, 0-9", "请只使用 a-z、A-Z、0-9"));
				pUI->chat()->addInfoMessage(L10N("Use /quit to exit or press ESC and select Quit Game", "输入 /quit 退出，或按 ESC 选择退出游戏"));
			}
			break;

		case REJECT_REASON_BAD_MOD:
			if (pUI) {
				pUI->chat()->addInfoMessage(L10N("CONNECTION REJECTED: Bad mod version.", "连接被拒绝：模组版本错误。"));
			}
			break;
		case REJECT_REASON_BAD_PLAYERID:
			if (pUI) {
				pUI->chat()->addInfoMessage(L10N("CONNECTION REJECTED: Unable to allocate a player slot.", "连接被拒绝：无法分配玩家位置。"));
			}
			break;
	}

	SessionHooks::NotifyDisconnect();
	pNetGame->GetRakClient()->Disconnect(100);
}

void RpcHandlers::OnGameModeRestart(const proto::GameModeRestart&)
{
	pNetGame->ShutdownForGameModeRestart();
}

void RpcHandlers::OnServerJoin(const proto::ServerJoin& p)
{
	CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
	if (!pPlayerPool) return;

	spdlog::debug("PlayerID {}", p.playerId);
	spdlog::debug("dwMarkerColor {}", p.markerColor);
	spdlog::debug("byteIsNPC {}", p.isNpc);
	spdlog::debug("szPlayerName {}", p.name);

	if (p.name.size() > MAX_PLAYER_NAME) {
		return;
	}

	pPlayerPool->New(p.playerId, p.name.c_str(), p.isNpc);

	CRemotePlayer* pRemotePlayer = pPlayerPool->GetAt(p.playerId);

	if (p.markerColor && pRemotePlayer) {
		pRemotePlayer->SetPlayerColor(p.markerColor);
	}
}

void RpcHandlers::OnServerQuit(const proto::ServerQuit& p)
{
	spdlog::debug("RPC: Server quit");

	if (pNetGame->GetPlayerPool()) {
		pNetGame->GetPlayerPool()->Delete(p.playerId, p.reason);
	}
}

void RpcHandlers::OnWorldPlayerAdd(const proto::WorldPlayerAdd& p)
{
	spdlog::debug("RPC: World player add");

	if (!proto::isFinite(p.position) || !std::isfinite(p.rotation)) return;

	CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
	if (pPlayerPool) {
		CRemotePlayer* pRemotePlayer = pPlayerPool->GetAt(p.playerId);
		if (pRemotePlayer) {
			sa::CVector vecPos(p.position.x, p.position.y, p.position.z);
			if (pRemotePlayer->Spawn(p.team, p.skin, &vecPos, p.rotation, p.color, p.fightingStyle, p.customSkin)) {
				CPlayerPed* pPlayerPed = pRemotePlayer->GetPlayerPed();
				if (pPlayerPed) {
					for (int i = 0; i < 11; i++) {
						pPlayerPed->SetSkillLevel(i, p.skillLevels[i]);
					}
				}

				if (p.team != 0xFF) {
					pRemotePlayer->SetTeam(p.team);
				}

				if (pPlayerPool->IsPlayerNPC(p.playerId)) {
					if (pPlayerPed) {
						pPlayerPed->SetImmunities(0, 1, 1, 0, 0);
					}
				}
			}
			else {
				if (pUI && pUI->chat()) {
					pUI->chat()->addDebugMessage("Warning: Couldn't spawn player(%u)", p.playerId);
				}
			}
		}
	}
}

void RpcHandlers::OnWorldPlayerRemove(const proto::WorldPlayerRemove& p)
{
	spdlog::debug("RPC: World player remove");
	spdlog::debug("playerId {}", p.playerId);

	CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
	if (pPlayerPool) {
		CRemotePlayer* pRemotePlayer = pPlayerPool->GetAt(p.playerId);
		if (pRemotePlayer) {
			pRemotePlayer->Remove();
		}
	}
}

void RpcHandlers::OnWorldPlayerDeath(const proto::WorldPlayerDeath& p)
{
	spdlog::debug("RPC: Player Death");

	CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
	if (pPlayerPool) {
		CRemotePlayer* pRemotePlayer = pPlayerPool->GetAt(p.playerId);
		if (pRemotePlayer) {
			pRemotePlayer->HandleDeath();
		}
	}
}

void RpcHandlers::OnWorldVehicleAdd(const proto::WorldVehicleAdd& p)
{
	spdlog::debug("RPC: World vehicle add");

	CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
	if (!pVehiclePool) {
		return;
	}

	if (p.modelId < 400 || p.modelId > 611 ||
			p.vehicleId >= MAX_VEHICLES ||
			!proto::isFinite(p.position) || !std::isfinite(p.rotation)) {
		return;
	}

	NEW_VEHICLE VehicleInfo{};
	VehicleInfo.VehicleID = p.vehicleId;
	VehicleInfo.iVehicleType = p.modelId;
	VehicleInfo.vecPos = sa::CVector(p.position.x, p.position.y, p.position.z);
	VehicleInfo.fRotation = p.rotation;
	VehicleInfo.byteInteriorColor1 = p.interiorColor1;
	VehicleInfo.byteInteriorColor2 = p.interiorColor2;
	VehicleInfo.fHealth = p.health;
	VehicleInfo.byteInterior = p.interior;
	VehicleInfo.dwDoorDamageStatus = p.doorDamageStatus;
	VehicleInfo.dwPanelDamageStatus = p.panelDamageStatus;
	VehicleInfo.byteLightDamageStatus = p.lightDamageStatus;
	VehicleInfo.byteTireDamageStatus = p.tireDamageStatus;
	VehicleInfo.byteAddSiren = p.addSiren;

	if (!pVehiclePool->New(&VehicleInfo)) {
		return;
	}
	CVehicle* pVehicle = pVehiclePool->GetAt(VehicleInfo.VehicleID);
	if (!pVehicle) {
		return;
	}

	bool bIsTrain = false;

	if (VehicleInfo.iVehicleType == 537) {
		VehicleInfo.iVehicleType = 569;
		bIsTrain = true;
	}

	if (VehicleInfo.iVehicleType == 538) {
		VehicleInfo.iVehicleType = 570;
		bIsTrain = true;
	}

	if (bIsTrain) {
		for (int c = 0; c < 3; ++c) {
			VehicleInfo.VehicleID++;
			if (pVehiclePool->New(&VehicleInfo)) {
				if (CVehicle* pCarriage = pVehiclePool->GetAt(VehicleInfo.VehicleID)) {
					if (p.bodyColor1 != -1 || p.bodyColor2 != -1) {
						pCarriage->SetColor(p.bodyColor1, p.bodyColor2);
					}
				}
			}
		}
	}

	if (pVehicle->GetVehicleSubtype() == VEHICLE_SUBTYPE_CAR) {
		for (int i = 0; i < 14; i++) {
			uint8_t byteMod = p.modSlots[i];
			if (byteMod) {
				pVehicle->AddComponent(byteMod + 999);
			}
		}

		if (p.paintJob) {
			pVehicle->SetPaintJob(p.paintJob - 1);
		}
	}

	if (p.bodyColor1 != -1 || p.bodyColor2 != -1) {
		pVehicle->SetColor(p.bodyColor1, p.bodyColor2);
	}
}

void RpcHandlers::OnWorldVehicleRemove(const proto::WorldVehicleRemove& p)
{
	spdlog::debug("RPC: World vehicle remove");

	CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
	if (!pVehiclePool) {
		return;
	}

	spdlog::debug("VehicleID {}", p.vehicleId);

	CVehicle* pVehicle = pVehiclePool->GetAt(p.vehicleId);
	if (!pVehicle) {
		return;
	}

	int iModel = pVehicle->GetModelIndex();
	// Evict and delete train carriages BEFORE destroying the locomotive (which frees all GTA carriages).
	if (iModel == 537 || iModel == 538) {
		pVehiclePool->Delete(p.vehicleId + 1);
		pVehiclePool->Delete(p.vehicleId + 2);
		pVehiclePool->Delete(p.vehicleId + 3);
	}
	pVehiclePool->Delete(p.vehicleId);
}

void RpcHandlers::OnTimerUpdate(const proto::TimerUpdate& p)
{
	spdlog::debug("dwTime {}", p.time);

	if (pGame) pGame->UpdateGlobalTimer(p.time);
}

void RpcHandlers::OnScmEvent(const proto::ScmEvent& p)
{
	spdlog::debug("RPC: SCM event");
	spdlog::debug("playerId {}", p.playerId);
	spdlog::debug("iEvent {}", p.eventType);
	spdlog::debug("dwParam1 {}", p.param1);
	spdlog::debug("dwParam2 {}", p.param2);
	spdlog::debug("dwParam3 {}", p.param3);

	ProcessIncomingScmEvent(p.playerId, p.eventType, p.param1, p.param2, p.param3);
}

void RpcHandlers::OnUpdateScoresPingsIPs(const proto::UpdateScoresPingsIPs& p)
{
	spdlog::debug("RPC: Update scores and pings");

	CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
	if (!pPlayerPool) return;

	for (const auto& entry : p.entries) {
		spdlog::debug("RPC_PINGIP: ({}): score: {}, ping: {}", entry.playerId, entry.score, entry.ping);

		pPlayerPool->UpdatePlayerScore(entry.playerId, entry.score);
		pPlayerPool->UpdatePlayerPing(entry.playerId, entry.ping);
	}
}

void RpcHandlers::OnCreatePickup(const proto::CreatePickup& p)
{
	spdlog::debug("RPC: Pickup");

	PICKUP pickup{};
	pickup.iModel = p.model;
	pickup.iType = p.type;
	pickup.fX = p.position.x;
	pickup.fY = p.position.y;
	pickup.fZ = p.position.z;

	CPickupPool* pPickupPool = pNetGame->GetPickupPool();
	if (pPickupPool) {
		pPickupPool->New(&pickup, p.pickupId);
	}
}

//destroy pickup
void RpcHandlers::OnDestroyPickup(const proto::DestroyPickup& p)
{
	spdlog::debug("RPC: Destroy pickup");
	CPickupPool* pPickupPool = pNetGame->GetPickupPool();
	if (pPickupPool) {
		pPickupPool->Destroy(p.pickupId);
	}
}

//destroy dropped weapon pickup by owner (0.3.DL, RPC 151)
void RpcHandlers::OnDestroyWeaponPickup(const proto::DestroyWeaponPickup& p)
{
	spdlog::debug("RPC: Destroy weapon pickup");
	CPickupPool* pPickupPool = pNetGame->GetPickupPool();
	if (pPickupPool) {
		pPickupPool->DestroyWeaponByOwner(p.owner);
	}
}

// Anti-cheat probe (RPC 103, enabled on both 0.3.7 and 0.3.DL):
// - 0x02: returns local player/vehicle CPhysical::m_nPhysicalFlags (bit layout
//   matches PC entity +0x40, though individual runtime flags may differ on
//   Android) with result 1 (on foot / passenger) or 2 (driving).
// - 0x48: returns RakNet::GetTime() with result 0.
// - 0x05 / 0x45 / 0x46 / 0x47: PC memory/model/collision checksums cannot be
//   computed against Android binaries; answer well-formed replies with result 0.
void RpcHandlers::OnClientCheck(const proto::ClientCheckRequest& p)
{
	// Types 0x48 (uptime) and 0x02 (physical flags) do not use offset/length;
	// anti-cheat scripts commonly pass offset=0, length=0.
	if (p.type != 0x48 && p.type != 0x02) {
		if (p.offset > 256 || p.length > 256 || p.length < 2) return;
	}

	uint32_t dwArgument = p.argument;
	uint8_t byteResult = 0;
	if (p.type == 0x02) {
		CPlayerPed* pPlayerPed = pGame ? pGame->FindPlayerPed() : nullptr;
		if (pPlayerPed) {
			if (pPlayerPed->IsInVehicle() && !pPlayerPed->IsAPassenger()) {
				sa::CVehicle* pVeh = pPlayerPed->GetGtaVehicle();
				if (pVeh) {
					dwArgument = pVeh->m_nPhysicalFlags;
					byteResult = 2;
				}
			}
			else if (pPlayerPed->m_ped) {
				dwArgument = pPlayerPed->m_ped->m_nPhysicalFlags;
				byteResult = 1;
			}
		}
	}
	else if (p.type == 0x48) {
		dwArgument = (uint32_t) RakNet::GetTime();
	}

	RakNet::BitStream bsReply;
	proto::Writer writer(bsReply);
	proto::ClientCheckReply reply{};
	reply.type = p.type;
	reply.argument = dwArgument;
	reply.result = byteResult;
	reply.serialize(writer, pNetGame->session().protoVersion);
	if (pNetGame->GetRakClient()) {
		pNetGame->GetRakClient()->RPC(&RPC_ClientCheck, &bsReply, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false, UNASSIGNED_NETWORK_ID, nullptr);
	}
}

void RpcHandlers::OnSetPlayerSkillLevel(const proto::SetPlayerSkillLevel& p)
{
	spdlog::debug("RPC: SetPlayerSkillLevel player={} skill={} level={}", p.playerId, p.skillType, p.skillLevel);
	if (p.skillType >= static_cast<uint32_t>(kWeaponSkillCount)) {
		return;
	}

	CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
	if (!pPlayerPool) {
		return;
	}

	CPlayerPed* pPed = nullptr;
	if (p.playerId == pPlayerPool->GetLocalPlayerID()) {
		if (CLocalPlayer* pLocal = pPlayerPool->GetLocalPlayer()) {
			pPed = pLocal->GetPlayerPed();
		}
	}
	else if (pPlayerPool->GetSlotState(p.playerId)) {
		if (CRemotePlayer* pRemote = pPlayerPool->GetAt(p.playerId)) {
			pPed = pRemote->GetPlayerPed();
		}
	}

	if (pPed) {
		pPed->SetSkillLevel(static_cast<int>(p.skillType), static_cast<int>(p.skillLevel));
		// Refresh currently armed weapon so dual-wield model/anim updates immediately
		const uint8_t curWeap = pPed->GetCurrentWeapon();
		if (curWeap != 0) {
			pPed->SetArmedWeapon(curWeap);
		}
	}
}

//Create 3D text
void RpcHandlers::OnCreate3DTextLabel(const proto::Create3DTextLabel& p)
{
	spdlog::debug("RPC: Create 3D text label");

	CTextLabelPool* pTextLabelPool = pNetGame->GetTextLabelPool();
	if (pTextLabelPool == nullptr) {
		return;
	}

	if (!proto::isFinite(p.position) || !std::isfinite(p.drawDistance)) return;

	TEXT_LABEL label{};
	label.dwColor = p.color;
	label.vecPos = sa::CVector(p.position.x, p.position.y, p.position.z);
	label.fDistance = p.drawDistance;
	label.bTestLOS = p.testLOS;
	label.playerId = p.attachedPlayerId;
	label.vehicleId = p.attachedVehicleId;
	strncpy(label.text, p.text.c_str(), sizeof(label.text) - 1);
	label.text[sizeof(label.text) - 1] = '\0';

	if (p.labelId < MAX_TEXT_LABELS) {
		pTextLabelPool->NewLabel(p.labelId, &label);
	}
}

//update 3d text
void RpcHandlers::OnDelete3DTextLabel(const proto::Delete3DTextLabel& p)
{
	spdlog::debug("RPC: Update 3D text label");

	CTextLabelPool* pTextLabelPool = pNetGame->GetTextLabelPool();
	if (pTextLabelPool == nullptr) {
		return;
	}

	if (pTextLabelPool->GetSlotState(p.labelId)) {
		pTextLabelPool->ClearLabel(p.labelId);
	}
}

void RpcHandlers::OnSetCheckpoint(const proto::SetCheckpoint& p)
{
	spdlog::debug("RPC: Set checkpoint");

	if (!proto::isFinite(p.position) || !std::isfinite(p.size)) return;

	sa::CVector vecPos(p.position.x, p.position.y, p.position.z);
	sa::CVector vecSize(p.size, p.size, p.size);

	if (pGame) {
		pGame->SetCheckpointInformation(&vecPos, &vecSize);
		pGame->EnableCheckpoints(true);
	}
}

void RpcHandlers::OnDisableCheckpoint(const proto::DisableCheckpoint&)
{
	spdlog::debug("RPC: Disable checkpoint");
	if (pGame) {
		pGame->EnableCheckpoints(false);
		pGame->UpdateCheckpoints();
	}
}

void RpcHandlers::OnSetRaceCheckpoint(const proto::SetRaceCheckpoint& p)
{
	spdlog::debug("RPC: Set race checkpoint");

	if (!proto::isFinite(p.position) || !proto::isFinite(p.nextPosition) || !std::isfinite(p.radius)) return;

	sa::CVector vecPos(p.position.x, p.position.y, p.position.z);
	sa::CVector vecNextPos(p.nextPosition.x, p.nextPosition.y, p.nextPosition.z);

	if (pGame) {
		pGame->SetRaceCheckpointInformation(p.type, &vecPos, &vecNextPos, p.radius);
		pGame->EnableRaceCheckpoints(true);
	}
}

void RpcHandlers::OnDisableRaceCheckpoint(const proto::DisableRaceCheckpoint&)
{
	spdlog::debug("RPC: Disable race checkpoint");
	if (pGame) {
		pGame->EnableRaceCheckpoints(false);
		pGame->UpdateCheckpoints();
	}
}

void RpcHandlers::OnDamageVehicle(const proto::DamageVehicle& p)
{
	spdlog::debug("RPC: Damage vehicle");

	CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
	if (!pVehiclePool) {
		return;
	}
	CVehicle* pVehicle = pVehiclePool->GetAt(p.vehicleId);
	if (pVehicle) {
		pVehicle->SetDamageStatus(p.panelDamageStatus, p.doorDamageStatus, p.lightDamageStatus);
		pVehicle->SetTireDamageStatus(p.tireDamageStatus);
		if (pNetGame && pNetGame->GetPlayerPool()) {
			if (CLocalPlayer* pLocalPlayer = pNetGame->GetPlayerPool()->GetLocalPlayer()) {
				uint32_t panels = 0, doors = 0;
				uint8_t lights = 0, tires = 0;
				pVehicle->GetDamageStatus(&panels, &doors, &lights, &tires);
				pLocalPlayer->SyncVehicleDamageCache(p.vehicleId, panels, doors, lights, tires);
			}
		}
	}
}

void RpcHandlers::OnEnterVehicle(const proto::EnterVehicle& p)
{
	spdlog::debug("RPC: Enter vehicle");

	uint8_t bytePassenger = p.passenger ? 1 : 0;

	CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
	if (!pPlayerPool || p.vehicleId >= MAX_VEHICLES) {
		return;
	}
	CRemotePlayer* pRemotePlayer = pPlayerPool->GetAt(p.playerId);
	if (pRemotePlayer) {
		if (pRemotePlayer->GetDistanceFromLocalPlayer() < 200.0f) {
			pRemotePlayer->EnterVehicle(p.vehicleId, bytePassenger);
		}
	}
}

void RpcHandlers::OnExitVehicle(const proto::ExitVehicle& p)
{
	spdlog::debug("RPC: Exit vehicle");

	CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
	if (!pPlayerPool) {
		return;
	}
	CRemotePlayer* pRemotePlayer = pPlayerPool->GetAt(p.playerId);
	if (pRemotePlayer) {
		if (pRemotePlayer->GetDistanceFromLocalPlayer() < 200.0f) {
			pRemotePlayer->ExitVehicle();
		}
	}
}

//Vehicle parameters
void RpcHandlers::OnVehicleParamsEx(const proto::VehicleParamsEx& p)
{
	spdlog::debug("RPC: VehicleParamsEx");

	CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
	if (!pVehiclePool) {
		return;
	}

	CVehicle* pVehicle = pVehiclePool->GetAt(p.vehicleId);
	if (!pVehicle) {
		return;
	}
	//engine status
	if (p.engine <= 1) {
		pVehicle->SetEngineState(p.engine == 1);
	}

	if (p.lights <= 1) {
		pVehicle->SetLightState(p.lights == 1);
	}
	//door state
	if (p.doors <= 1) {
		pVehicle->SetDoorState(p.doors == 1);
	}
	//vehicle object
	if (p.objective <= 1) {
		uint8_t newObjective = (p.objective == 1) ? 1 : 0;
		if (pVehicle->m_byteObjectiveVehicle != newObjective) {
			pVehicle->m_byteObjectiveVehicle = newObjective;
			pVehicle->m_bSpecialMarkerEnabled = false;
		}
	}

	if (p.boot <= 1) {
		pVehicle->SetComponentOpenState(1, 17, p.boot == 1 ? 1.0f : 0.0f);
	}

	if (p.bonnet <= 1) {
		pVehicle->SetComponentOpenState(0, 16, p.bonnet == 1 ? 1.0f : 0.0f);
	}

	if (p.alarm <= 1) {
		pVehicle->SetSirenOn(p.alarm == 1);
		pVehicle->SetAlarmState(p.alarm == 1 ? 20000 : 0);
	}

	if (p.siren <= 1) {
		pVehicle->SetSirenOn(p.siren == 1);
	}

	if (p.doorDriver <= 1) {
		pVehicle->SetComponentOpenState(2, 10, p.doorDriver == 1 ? 1.0f : 0.0f);
	}

	if (p.doorPassenger <= 1) {
		pVehicle->SetComponentOpenState(3, 8, p.doorPassenger == 1 ? 1.0f : 0.0f);
	}

	if (p.doorBackLeft <= 1) {
		pVehicle->SetComponentOpenState(4, 11, p.doorBackLeft == 1 ? 1.0f : 0.0f);
	}

	if (p.doorBackRight <= 1) {
		pVehicle->SetComponentOpenState(5, 9, p.doorBackRight == 1 ? 1.0f : 0.0f);
	}

	if (p.windowDriver) {
		if (p.windowDriver == 1) {
			pVehicle->CloseWindow(10);
		}
	}
	else {
		pVehicle->OpenWindow(10);
	}

	if (p.windowPassenger) {
		if (p.windowPassenger == 1) {
			pVehicle->CloseWindow(8);
		}
	}
	else {
		pVehicle->OpenWindow(8);
	}

	if (p.windowBackLeft) {
		if (p.windowBackLeft == 1) {
			pVehicle->CloseWindow(11);
		}
	}
	else {
		pVehicle->OpenWindow(11);
	}

	if (p.windowBackRight) {
		if (p.windowBackRight == 1) {
			pVehicle->CloseWindow(9);
		}
	}
	else {
		pVehicle->OpenWindow(9);
	}
}

//show npc
void RpcHandlers::OnShowActor(const proto::ShowActor& p)
{
	spdlog::debug("RPC: Show actor");

	CActorPool* pActorPool = pNetGame->GetActorPool();
	if (!pActorPool) {
		return;
	}

	if (!proto::isFinite(p.position) || !std::isfinite(p.angle)) return;

	const int effectiveSkin = ResolveEffectivePedSkin(p.skin, p.customSkin);
	if (!IsValidPedModel(static_cast<uint>(effectiveSkin))) {
		return;
	}

	NEW_ACTOR newActor{};
	newActor.ActorID = p.actorId;
	newActor.iSkin = p.skin;
	newActor.iCustomSkin = p.customSkin;
	newActor.vecPos = sa::CVector(p.position.x, p.position.y, p.position.z);
	newActor.fAngle = p.angle;
	newActor.fHealth = p.health;
	newActor.bInvulnerable = p.invulnerable != 0;

	pActorPool->New(&newActor);
}

//hide npc
void RpcHandlers::OnHideActor(const proto::HideActor& p)
{
	spdlog::debug("RPC: Hide actor");

	CActorPool* pActorPool = pNetGame->GetActorPool();
	if (!pActorPool) {
		return;
	}

	pActorPool->Delete(p.actorId);
}

//chat bubble
void RpcHandlers::OnChatBubble(const proto::ChatBubble& p)
{
	spdlog::debug("RPC: ChatBubble");

	CPlayerBubblePool* pPlayerBubblePool = pNetGame->GetPlayerBubblePool();
	CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
	if (pPlayerBubblePool == nullptr || pPlayerPool == nullptr) {
		return;
	}

	if (p.playerId == pPlayerPool->GetLocalPlayerID() || p.playerId >= MAX_PLAYERS) {
		return;
	}

	pPlayerBubblePool->New(p.playerId, p.text.c_str(), p.color, p.drawDistance, p.duration);
}

//Edit attached object
void RpcHandlers::OnEditAttachedObject(const proto::EditAttachedObject& p)
{
	spdlog::debug("RPC: EditAttachedObject");
	if (CObjectPool* pObjPool = pNetGame->GetObjectPool(); pObjPool && pObjPool->IsSelectState()) {
		pObjPool->SetSelectState(false);
	}
	if (CTextDrawPool* pTdPool = pNetGame->GetTextDrawPool(); pTdPool && pTdPool->GetState()) {
		pTdPool->SetSelectState(false, 0, true);
	}
	EditObject::startEditAttachedObject(p.index);
}

//edit object
void RpcHandlers::OnEditObject(const proto::EditObject& p)
{
	spdlog::debug("RPC: EditObject (id={}, playerObject={})", p.objectId, p.playerObject);

	if (CObjectPool* pObjPool = pNetGame->GetObjectPool(); pObjPool && pObjPool->IsSelectState()) {
		pObjPool->SetSelectState(false);
	}
	if (CTextDrawPool* pTdPool = pNetGame->GetTextDrawPool(); pTdPool && pTdPool->GetState()) {
		pTdPool->SetSelectState(false, 0, true);
	}

	// SA-MP shares a single client CObjectPool for both global and player objects;
	// playerObject is simply echoed back in RPC_EditObject responses.
	EditObject::m_playerObj = p.playerObject;
	EditObject::startEditObject(p.objectId);
}

//Go to edit object (SelectObject, RPC 27)
void RpcHandlers::OnEnterEditObject(const proto::EnterEditObject&)
{
	spdlog::debug("RPC: EnterEditObject");
	if (EditObject::m_visible) {
		EditObject::cancelEdit();
	}
	if (CTextDrawPool* pTdPool = pNetGame->GetTextDrawPool(); pTdPool && pTdPool->GetState()) {
		pTdPool->SetSelectState(false, 0, true);
	}
	if (CObjectPool* pObjPool = pNetGame->GetObjectPool()) {
		pObjPool->SetSelectState(true);
	}
}

//cancel editor object (CancelEdit, RPC 28)
void RpcHandlers::OnCancelEditObject(const proto::CancelEditObject&)
{
	spdlog::debug("RPC: CancelEditObject");
	EditObject::cancelEdit();
	if (CObjectPool* pObjPool = pNetGame->GetObjectPool()) {
		pObjPool->SetSelectState(false);
	}
}

void RpcHandlers::OnEnableCameraTarget(const proto::EnableCameraTarget& p)
{
	spdlog::debug("RPC: EnableCameraTarget ({})", p.enable ? 1 : 0);
	pNetGame->session().cameraTargetEnabled = p.enable;
}

void RpcHandlers::OnSvrStats(const proto::SvrStats& p)
{
	spdlog::debug("RPC: SvrStats (296 bytes)");
	if (pUI && pUI->netstats()) {
		pUI->netstats()->updateServerStats(p.raw);
	}
}

void RpcHandlers::OnSetPlayerVirtualWorld(const proto::SetPlayerVirtualWorld& p)
{
	spdlog::debug("RPC: SetPlayerVirtualWorld ({})", p.virtualWorld);
	pNetGame->NotifyDlVirtualWorld(p.virtualWorld);
}

void RpcHandlers::OnDlModelRequest(const proto::DlModelRequest& p)
{
	spdlog::debug("RPC: DlModelRequest (pool={} count={} type={} vw={} base={} new={})",
			p.poolId, p.count, p.type, p.virtualWorld, p.baseId, p.newId);
	artwork::ArtworkManager::instance().onModelRecord(p);
}

void RpcHandlers::OnDlModelReset(const proto::DlModelReset&)
{
	spdlog::debug("RPC: DlModelReset");
	artwork::ArtworkManager::instance().onModelReset();
}

void RpcHandlers::OnDlModelUrl(const proto::DlModelUrl& p)
{
	spdlog::debug("RPC: DlModelUrl (kind={} type={} crc={:#x} url={})",
			p.kind, p.fileType, p.fileChecksum, p.downloadLink);
	artwork::ArtworkManager::instance().onDownloadCommand(p);
}

void RpcHandlers::OnDlDownloadCompleted(const proto::DlDownloadCompleted&)
{
	spdlog::debug("RPC: DlDownloadCompleted");
	artwork::ArtworkManager::instance().onDownloadCompletedRpc();
}
