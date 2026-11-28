#include "../Client.h"
#include "../Game/Game.h"
#include "RpcTable.h"
#include "RpcHandlers.h"
#include "NetGame.h"

#include <RakNet/RakClientInterface.h>
#include <RakNet/SAMP/SAMPRPC.h>

#include <array>
#include <atomic>
#include <iterator>


namespace {

struct RpcEntry {
	int* idPtr;
	int id;
	const char* name;
	uint8_t versionMask;
	void (*thunk)(RPCParameters*);
};

struct RpcCounters {
	std::atomic<uint64_t> hits{0};
	std::atomic<uint64_t> readFails{0};
};

std::array<RpcCounters, 256> g_counters{};

template <typename Packet, void (*Handler)(const Packet&)>
void dispatchRpc(RPCParameters* rpcParams)
{
	constexpr int kId = Packet::kRpcId;
	static_assert(kId >= 0 && kId < 256, "RPC ID out of 0..255 range");

	g_counters[kId].hits.fetch_add(1, std::memory_order_relaxed);

	if (!pNetGame) {
		return;
	}
	if (!rpcParams) {
		g_counters[kId].readFails.fetch_add(1, std::memory_order_relaxed);
		spdlog::debug("RPC {} ({}) dropped: null RPCParameters", Packet::kName, kId);
		return;
	}

	const uint32_t bitLength = static_cast<uint32_t>(rpcParams->numberOfBitsOfData);
	if (bitLength > 0 && !rpcParams->input) {
		g_counters[kId].readFails.fetch_add(1, std::memory_order_relaxed);
		spdlog::debug("RPC {} ({}) dropped: null input ({} bits)", Packet::kName, kId, bitLength);
		return;
	}

	proto::Reader reader(reinterpret_cast<const uint8_t*>(rpcParams->input), bitLength);
	const proto::ProtoVersion ver = proto::versionFromNetgame(static_cast<uint32_t>(Client::netgameVersion()));
	Packet pkt{};
	if (!pkt.serialize(reader, ver)) {
		g_counters[kId].readFails.fetch_add(1, std::memory_order_relaxed);
		spdlog::debug("RPC {} ({}) read failed ({} bits, {} unread)", Packet::kName, kId, bitLength, reader.unreadBits());
		return;
	}

	Handler(pkt);
}

#define RPC_ENTRY(idVar, PacketType, HandlerFn) \
	{ &(idVar), PacketType::kRpcId, PacketType::kName, PacketType::kVersionMask, &dispatchRpc<PacketType, HandlerFn> }

const RpcEntry kInbound[] = {
	// Batch 1: Dialog
	RPC_ENTRY(RPC_DialogBox, proto::ShowDialog, RpcHandlers::OnShowDialog),
	// Batch 1: Checkpoint
	RPC_ENTRY(RPC_SetCheckpoint, proto::SetCheckpoint, RpcHandlers::OnSetCheckpoint),
	RPC_ENTRY(RPC_DisableCheckpoint, proto::DisableCheckpoint, RpcHandlers::OnDisableCheckpoint),
	RPC_ENTRY(RPC_SetRaceCheckpoint, proto::SetRaceCheckpoint, RpcHandlers::OnSetRaceCheckpoint),
	RPC_ENTRY(RPC_DisableRaceCheckpoint, proto::DisableRaceCheckpoint, RpcHandlers::OnDisableRaceCheckpoint),
	// Batch 1: GangZone
	RPC_ENTRY(RPC_ScrAddGangZone, proto::ShowGangZone, RpcHandlers::OnShowGangZone),
	RPC_ENTRY(RPC_ScrRemoveGangZone, proto::HideGangZone, RpcHandlers::OnHideGangZone),
	RPC_ENTRY(RPC_ScrFlashGangZone, proto::FlashGangZone, RpcHandlers::OnFlashGangZone),
	RPC_ENTRY(RPC_ScrStopFlashGangZone, proto::StopFlashGangZone, RpcHandlers::OnStopFlashGangZone),
	// Batch 1: Pickup
	RPC_ENTRY(RPC_Pickup, proto::CreatePickup, RpcHandlers::OnCreatePickup),
	RPC_ENTRY(RPC_DestroyPickup, proto::DestroyPickup, RpcHandlers::OnDestroyPickup),
	RPC_ENTRY(RPC_ScrDestroyWeaponPickup, proto::DestroyWeaponPickup, RpcHandlers::OnDestroyWeaponPickup),
	// Batch 1: TextLabel
	RPC_ENTRY(RPC_ScrCreate3DTextLabel, proto::Create3DTextLabel, RpcHandlers::OnCreate3DTextLabel),
	RPC_ENTRY(RPC_ScrUpdate3DTextLabel, proto::Delete3DTextLabel, RpcHandlers::OnDelete3DTextLabel),

	// Batch 2: Core upper half (session, chat, time/weather, player properties)
	RPC_ENTRY(RPC_InitGame, proto::InitGame, RpcHandlers::OnInitGame),
	RPC_ENTRY(RPC_ServerJoin, proto::ServerJoin, RpcHandlers::OnServerJoin),
	RPC_ENTRY(RPC_ServerQuit, proto::ServerQuit, RpcHandlers::OnServerQuit),
	RPC_ENTRY(RPC_GameModeRestart, proto::GameModeRestart, RpcHandlers::OnGameModeRestart),
	RPC_ENTRY(RPC_ConnectionRejected, proto::ConnectionRejected, RpcHandlers::OnConnectionRejected),
	RPC_ENTRY(RPC_Chat, proto::ChatMessage, RpcHandlers::OnChatMessage),
	RPC_ENTRY(RPC_ClientMessage, proto::ClientMessage, RpcHandlers::OnClientMessage),
	RPC_ENTRY(RPC_ChatBubble, proto::ChatBubble, RpcHandlers::OnChatBubble),
	RPC_ENTRY(RPC_WorldTime, proto::WorldTime, RpcHandlers::OnWorldTime),
	RPC_ENTRY(RPC_Weather, proto::Weather, RpcHandlers::OnWeather),
	RPC_ENTRY(RPC_SetTimeEx, proto::SetTimeEx, RpcHandlers::OnSetTimeEx),
	RPC_ENTRY(RPC_ToggleClock, proto::ToggleClock, RpcHandlers::OnToggleClock),
	RPC_ENTRY(RPC_TimerUpdate, proto::TimerUpdate, RpcHandlers::OnTimerUpdate),
	RPC_ENTRY(RPC_UpdateScoresPingsIPs, proto::UpdateScoresPingsIPs, RpcHandlers::OnUpdateScoresPingsIPs),
	RPC_ENTRY(RPC_ClientCheck, proto::ClientCheckRequest, RpcHandlers::OnClientCheck),

	RPC_ENTRY(RPC_ScrSetGravity, proto::SetGravity, RpcHandlers::OnSetGravity),
	RPC_ENTRY(RPC_ScrSetPlayerPos, proto::SetPlayerPos, RpcHandlers::OnSetPlayerPos),
	RPC_ENTRY(RPC_ScrSetPlayerPosFindZ, proto::SetPlayerPosFindZ, RpcHandlers::OnSetPlayerPosFindZ),
	RPC_ENTRY(RPC_ScrSetPlayerFacingAngle, proto::SetPlayerFacingAngle, RpcHandlers::OnSetPlayerFacingAngle),
	RPC_ENTRY(RPC_ScrSetPlayerHealth, proto::SetPlayerHealth, RpcHandlers::OnSetPlayerHealth),
	RPC_ENTRY(RPC_ScrSetPlayerArmour, proto::SetPlayerArmour, RpcHandlers::OnSetPlayerArmour),
	RPC_ENTRY(RPC_ScrSetPlayerColor, proto::SetPlayerColor, RpcHandlers::OnSetPlayerColor),
	RPC_ENTRY(RPC_ScrSetPlayerTeam, proto::SetPlayerTeam, RpcHandlers::OnSetPlayerTeam),
	RPC_ENTRY(RPC_ScrSetPlayerName, proto::SetPlayerName, RpcHandlers::OnSetPlayerName),
	RPC_ENTRY(RPC_SetPlayerSkillLevel, proto::SetPlayerSkillLevel, RpcHandlers::OnSetPlayerSkillLevel),
	RPC_ENTRY(RPC_ScrSetPlayerSkin, proto::SetPlayerSkin, RpcHandlers::OnSetPlayerSkin),
	RPC_ENTRY(RPC_ScrSetInterior, proto::SetPlayerInterior, RpcHandlers::OnSetPlayerInterior),
	RPC_ENTRY(RPC_ScrTogglePlayerControllable, proto::TogglePlayerControllable, RpcHandlers::OnTogglePlayerControllable),
	RPC_ENTRY(RPC_ScrSetWorldBounds, proto::SetWorldBounds, RpcHandlers::OnSetWorldBounds),
	RPC_ENTRY(RPC_ScrHaveSomeMoney, proto::GivePlayerMoney, RpcHandlers::OnGivePlayerMoney),
	RPC_ENTRY(RPC_ScrResetMoney, proto::ResetPlayerMoney, RpcHandlers::OnResetPlayerMoney),
	RPC_ENTRY(RPC_ScrSetPlayerWantedLevel, proto::SetPlayerWantedLevel, RpcHandlers::OnSetPlayerWantedLevel),
	RPC_ENTRY(RPC_ScrSetFightingStyle, proto::SetPlayerFightingStyle, RpcHandlers::OnSetPlayerFightingStyle),
	RPC_ENTRY(RPC_ScrSetPlayerVelocity, proto::SetPlayerVelocity, RpcHandlers::OnSetPlayerVelocity),
	RPC_ENTRY(RPC_ScrSetSpecialAction, proto::SetPlayerSpecialAction, RpcHandlers::OnSetPlayerSpecialAction),
	RPC_ENTRY(RPC_ScrSetPlayerDrunkLevel, proto::SetPlayerDrunkLevel, RpcHandlers::OnSetPlayerDrunkLevel),
	RPC_ENTRY(RPC_ScrSetPlayerDrunkVisuals, proto::SetPlayerDrunkVisuals, RpcHandlers::OnSetPlayerDrunkVisuals),
	RPC_ENTRY(RPC_ScrSetPlayerDrunkHandling, proto::SetPlayerDrunkHandling, RpcHandlers::OnSetPlayerDrunkHandling),
	RPC_ENTRY(RPC_ScrResetPlayerWeapons, proto::ResetPlayerWeapons, RpcHandlers::OnResetPlayerWeapons),
	RPC_ENTRY(RPC_ScrGivePlayerWeapon, proto::GivePlayerWeapon, RpcHandlers::OnGivePlayerWeapon),
	RPC_ENTRY(RPC_ScrSetWeaponAmmo, proto::SetPlayerAmmo, RpcHandlers::OnSetPlayerAmmo),
	RPC_ENTRY(RPC_ScrSetArmedWeapon, proto::SetArmedWeapon, RpcHandlers::OnSetArmedWeapon),
	RPC_ENTRY(RPC_ScrDisplayGameText, proto::ShowGameText, RpcHandlers::OnShowGameText),
	RPC_ENTRY(RPC_ScrDeathMessage, proto::DeathMessage, RpcHandlers::OnDeathMessage),
	RPC_ENTRY(RPC_ScrShowNameTag, proto::ShowNameTag, RpcHandlers::OnShowNameTag),
	RPC_ENTRY(RPC_ScrSetMapIcon, proto::SetPlayerMapIcon, RpcHandlers::OnSetPlayerMapIcon),
	RPC_ENTRY(RPC_ScrDisableMapIcon, proto::RemovePlayerMapIcon, RpcHandlers::OnRemovePlayerMapIcon),
	RPC_ENTRY(RPC_ScrCreateExplosion, proto::CreateExplosion, RpcHandlers::OnCreateExplosion),
	RPC_ENTRY(RPC_ScrPlaySound, proto::PlayerPlaySound, RpcHandlers::OnPlayerPlaySound),
	RPC_ENTRY(RPC_ScrEnableStuntBonus, proto::EnableStuntBonus, RpcHandlers::OnEnableStuntBonus),
	RPC_ENTRY(RPC_ScrSetPlayerShopName, proto::SetPlayerShopName, RpcHandlers::OnSetPlayerShopName),
	RPC_ENTRY(RPC_ScrPlayCrimeReport, proto::PlayCrimeReport, RpcHandlers::OnPlayCrimeReport),
	RPC_ENTRY(RPC_ScrPlayAudioStream, proto::PlayAudioStream, RpcHandlers::OnPlayAudioStream),
	RPC_ENTRY(RPC_ScrStopAudioStream, proto::StopAudioStream, RpcHandlers::OnStopAudioStream),
	RPC_ENTRY(RPC_ScrRemoveBuilding, proto::RemoveBuilding, RpcHandlers::OnRemoveBuilding),
	RPC_ENTRY(RPC_ScrDisableVehicleCollisions, proto::DisableVehicleCollisions, RpcHandlers::OnDisableVehicleCollisions),

	// Batch 3: Core lower half & Class
	RPC_ENTRY(RPC_WorldPlayerAdd, proto::WorldPlayerAdd, RpcHandlers::OnWorldPlayerAdd),
	RPC_ENTRY(RPC_WorldPlayerDeath, proto::WorldPlayerDeath, RpcHandlers::OnWorldPlayerDeath),
	RPC_ENTRY(RPC_WorldPlayerRemove, proto::WorldPlayerRemove, RpcHandlers::OnWorldPlayerRemove),
	RPC_ENTRY(RPC_ScrApplyPlayerAnimation, proto::ApplyPlayerAnimation, RpcHandlers::OnApplyPlayerAnimation),
	RPC_ENTRY(RPC_ScrClearPlayerAnimations, proto::ClearPlayerAnimations, RpcHandlers::OnClearPlayerAnimations),
	RPC_ENTRY(RPC_ScrSetCameraPos, proto::SetCameraPos, RpcHandlers::OnSetCameraPos),
	RPC_ENTRY(RPC_ScrSetCameraLookAt, proto::SetCameraLookAt, RpcHandlers::OnSetCameraLookAt),
	RPC_ENTRY(RPC_ScrSetCameraBehindPlayer, proto::SetCameraBehindPlayer, RpcHandlers::OnSetCameraBehindPlayer),
	RPC_ENTRY(RPC_ScrInterpolateCamera, proto::InterpolateCamera, RpcHandlers::OnInterpolateCamera),
	RPC_ENTRY(RPC_ScrAttachCameraToObject, proto::AttachCameraToObject, RpcHandlers::OnAttachCameraToObject),
	RPC_ENTRY(RPC_ScrTogglePlayerSpectating, proto::TogglePlayerSpectating, RpcHandlers::OnTogglePlayerSpectating),
	RPC_ENTRY(RPC_ScrPlayerSpectatePlayer, proto::SpectatePlayer, RpcHandlers::OnSpectatePlayer),
	RPC_ENTRY(RPC_ScrPlayerSpectateVehicle, proto::SpectateVehicle, RpcHandlers::OnSpectateVehicle),
	RPC_ENTRY(RPC_ScrToggleWidescreen, proto::ToggleWidescreen, RpcHandlers::OnToggleWidescreen),
	RPC_ENTRY(RPC_ScrEnableCameraTarget, proto::EnableCameraTarget, RpcHandlers::OnEnableCameraTarget),
	RPC_ENTRY(RPC_RequestClass, proto::RequestClassReply, RpcHandlers::OnRequestClass),
	RPC_ENTRY(RPC_RequestSpawn, proto::RequestSpawnReply, RpcHandlers::OnRequestSpawn),
	RPC_ENTRY(RPC_ScrSetSpawnInfo, proto::SetSpawnInfo, RpcHandlers::OnSetSpawnInfo),
	RPC_ENTRY(RPC_ScrForceSpawnSelection, proto::ForceClassSelection, RpcHandlers::OnForceClassSelection),

	// Batch 4: Vehicle
	RPC_ENTRY(RPC_WorldVehicleAdd, proto::WorldVehicleAdd, RpcHandlers::OnWorldVehicleAdd),
	RPC_ENTRY(RPC_WorldVehicleRemove, proto::WorldVehicleRemove, RpcHandlers::OnWorldVehicleRemove),
	RPC_ENTRY(RPC_DamageVehicle, proto::DamageVehicle, RpcHandlers::OnDamageVehicle),
	RPC_ENTRY(RPC_VehicleParamsEx, proto::VehicleParamsEx, RpcHandlers::OnVehicleParamsEx),
	RPC_ENTRY(RPC_EnterVehicle, proto::EnterVehicle, RpcHandlers::OnEnterVehicle),
	RPC_ENTRY(RPC_ExitVehicle, proto::ExitVehicle, RpcHandlers::OnExitVehicle),
	RPC_ENTRY(RPC_ScmEvent, proto::ScmEvent, RpcHandlers::OnScmEvent),
	RPC_ENTRY(RPC_ScrSetVehiclePos, proto::SetVehiclePos, RpcHandlers::OnSetVehiclePos),
	RPC_ENTRY(RPC_ScrSetVehicleZAngle, proto::SetVehicleZAngle, RpcHandlers::OnSetVehicleZAngle),
	RPC_ENTRY(RPC_ScrVehicleParams, proto::SetVehicleParamsForPlayer, RpcHandlers::OnSetVehicleParamsForPlayer),
	RPC_ENTRY(RPC_ScrSetVehicleHealth, proto::SetVehicleHealth, RpcHandlers::OnSetVehicleHealth),
	RPC_ENTRY(RPC_ScrSetVehicleVelocity, proto::SetVehicleVelocity, RpcHandlers::OnSetVehicleVelocity),
	RPC_ENTRY(RPC_ScrSetVehicleTireStatus, proto::SetVehicleTireDamageStatus, RpcHandlers::OnSetVehicleTireDamageStatus),
	RPC_ENTRY(RPC_ScrNumberPlate, proto::SetVehicleNumberPlate, RpcHandlers::OnSetVehicleNumberPlate),
	RPC_ENTRY(RPC_ScrLinkVehicle, proto::LinkVehicleToInterior, RpcHandlers::OnLinkVehicleToInterior),
	RPC_ENTRY(RPC_ScrAttachTrailerToVehicle, proto::AttachTrailerToVehicle, RpcHandlers::OnAttachTrailerToVehicle),
	RPC_ENTRY(RPC_ScrDetachTrailerFromVehicle, proto::DetachTrailerFromVehicle, RpcHandlers::OnDetachTrailerFromVehicle),
	RPC_ENTRY(RPC_ScrRemoveComponent, proto::RemoveVehicleComponent, RpcHandlers::OnRemoveVehicleComponent),
	RPC_ENTRY(RPC_ScrPutPlayerInVehicle, proto::PutPlayerInVehicle, RpcHandlers::OnPutPlayerInVehicle),
	RPC_ENTRY(RPC_ScrRemovePlayerFromVehicle, proto::RemovePlayerFromVehicle, RpcHandlers::OnRemovePlayerFromVehicle),

	// Batch 5: Object, Actor, TextDraw
	RPC_ENTRY(RPC_ScrCreateObject, proto::CreateObject, RpcHandlers::OnCreateObject),
	RPC_ENTRY(RPC_ScrSetObjectPos, proto::SetObjectPos, RpcHandlers::OnSetObjectPos),
	RPC_ENTRY(RPC_ScrSetObjectRotation, proto::SetObjectRotation, RpcHandlers::OnSetObjectRotation),
	RPC_ENTRY(RPC_ScrDestroyObject, proto::DestroyObject, RpcHandlers::OnDestroyObject),
	RPC_ENTRY(RPC_ScrMoveObject, proto::MoveObject, RpcHandlers::OnMoveObject),
	RPC_ENTRY(RPC_ScrStopObject, proto::StopObject, RpcHandlers::OnStopObject),
	RPC_ENTRY(RPC_ScrAttachObjectToPlayer, proto::AttachObjectToPlayer, RpcHandlers::OnAttachObjectToPlayer),
	RPC_ENTRY(RPC_ScrSetObjectMaterial, proto::SetObjectMaterial, RpcHandlers::OnSetObjectMaterial),
	RPC_ENTRY(RPC_ScrStopObjectCameraCollision, proto::StopObjectCameraCollision, RpcHandlers::OnStopObjectCameraCollision),
	RPC_ENTRY(RPC_ScrSetPlayerAttachedObject, proto::SetPlayerAttachedObject, RpcHandlers::OnSetPlayerAttachedObject),
	RPC_ENTRY(RPC_EditAttachedObject, proto::EditAttachedObject, RpcHandlers::OnEditAttachedObject),
	RPC_ENTRY(RPC_EditObject, proto::EditObject, RpcHandlers::OnEditObject),
	RPC_ENTRY(RPC_EnterEditObject, proto::EnterEditObject, RpcHandlers::OnEnterEditObject),
	RPC_ENTRY(RPC_CancelEditObject, proto::CancelEditObject, RpcHandlers::OnCancelEditObject),
	RPC_ENTRY(RPC_ShowActor, proto::ShowActor, RpcHandlers::OnShowActor),
	RPC_ENTRY(RPC_HideActor, proto::HideActor, RpcHandlers::OnHideActor),
	RPC_ENTRY(RPC_ScrApplyActorAnimation, proto::ApplyActorAnimation, RpcHandlers::OnApplyActorAnimation),
	RPC_ENTRY(RPC_ScrClearActorAnimations, proto::ClearActorAnimation, RpcHandlers::OnClearActorAnimation),
	RPC_ENTRY(RPC_ScrSetActorFacingAngle, proto::SetActorFacingAngle, RpcHandlers::OnSetActorFacingAngle),
	RPC_ENTRY(RPC_ScrSetActorPos, proto::SetActorPos, RpcHandlers::OnSetActorPos),
	RPC_ENTRY(RPC_ScrSetActorHealth, proto::SetActorHealth, RpcHandlers::OnSetActorHealth),
	RPC_ENTRY(RPC_ScrShowTextDraw, proto::ShowTextDraw, RpcHandlers::OnShowTextDraw),
	RPC_ENTRY(RPC_ScrHideTextDraw, proto::HideTextDraw, RpcHandlers::OnHideTextDraw),
	RPC_ENTRY(RPC_ScrTextDrawSetString, proto::TextDrawSetString, RpcHandlers::OnTextDrawSetString),
	RPC_ENTRY(RPC_ClickTextDraw, proto::SelectTextDraw, RpcHandlers::OnSelectTextDraw),

	// Menu (T11)
	RPC_ENTRY(RPC_ScrInitMenu, proto::InitMenu, RpcHandlers::OnInitMenu),
	RPC_ENTRY(RPC_ScrShowMenu, proto::ShowMenu, RpcHandlers::OnShowMenu),
	RPC_ENTRY(RPC_ScrHideMenu, proto::HideMenu, RpcHandlers::OnHideMenu),

	// Network Stats (T28)
	RPC_ENTRY(RPC_SvrStats, proto::SvrStats, RpcHandlers::OnSvrStats),

	// 0.3.DL Artwork Handshake Placeholder (T30)
	RPC_ENTRY(RPC_ScrSetPlayerVirtualWorld, proto::SetPlayerVirtualWorld, RpcHandlers::OnSetPlayerVirtualWorld),
	RPC_ENTRY(RPC_DlModelRequest, proto::DlModelRequest, RpcHandlers::OnDlModelRequest),
	RPC_ENTRY(RPC_DlModelReset, proto::DlModelReset, RpcHandlers::OnDlModelReset),
	RPC_ENTRY(RPC_DlModelUrl, proto::DlModelUrl, RpcHandlers::OnDlModelUrl),
	RPC_ENTRY(RPC_DlDownloadCompleted, proto::DlDownloadCompleted, RpcHandlers::OnDlDownloadCompleted),
};

#undef RPC_ENTRY

} // namespace

namespace RpcTable {

void registerAll(RakClientInterface* rakClient, proto::ProtoVersion version)
{
	if (!rakClient) return;
	resetStats();
	size_t count = 0;
	for (const RpcEntry& e : kInbound) {
		if (!proto::versionMatches(e.versionMask, version)) continue;
		rakClient->RegisterAsRemoteProcedureCall(e.idPtr, e.thunk);
		++count;
	}
	spdlog::info("RpcTable: registered {} declarative RPCs (mode={})",
		count, version == proto::ProtoVersion::V03DL ? "0.3.DL" : "0.3.7");
}

void unregisterAll(RakClientInterface* rakClient, proto::ProtoVersion version)
{
	if (!rakClient) return;
	for (const RpcEntry& e : kInbound) {
		if (!proto::versionMatches(e.versionMask, version)) continue;
		rakClient->UnregisterAsRemoteProcedureCall(e.idPtr);
	}
}

std::vector<StatEntry> snapshotStats()
{
	std::vector<StatEntry> out;
	out.reserve(std::size(kInbound));
	for (const RpcEntry& e : kInbound) {
		StatEntry s{};
		s.id = e.id;
		s.name = e.name;
		s.hits = g_counters[e.id].hits.load(std::memory_order_relaxed);
		s.readFails = g_counters[e.id].readFails.load(std::memory_order_relaxed);
		out.push_back(s);
	}
	return out;
}

void resetStats()
{
	for (auto& c : g_counters) {
		c.hits.store(0, std::memory_order_relaxed);
		c.readFails.store(0, std::memory_order_relaxed);
	}
}

size_t entryCount()
{
	return std::size(kInbound);
}

} // namespace RpcTable
