#pragma once

#include "RpcActor.h"
#include "RpcCheckpoint.h"
#include "RpcClass.h"
#include "RpcCore.h"
#include "RpcDialog.h"
#include "RpcGangZone.h"
#include "RpcMenu.h"
#include "RpcObject.h"
#include "RpcOutbound.h"
#include "RpcPickup.h"
#include "RpcTextDraw.h"
#include "RpcTextLabel.h"
#include "RpcVehicle.h"
#include "Sync.h"
#include "Version.h"
#include "Wire.h"

#include <cstdint>

namespace proto {

enum class DecodeStatus : uint8_t {
	kOk = 0,
	kNotMigrated = 1,
	kVersionMismatch = 2,
	kReadFailed = 3,
};

struct DecodeResult {
	DecodeStatus status = DecodeStatus::kNotMigrated;
	const char* name = nullptr;
	int unreadBits = 0;
};

namespace detail {

template <typename Packet>
DecodeResult decodeOne(const uint8_t* data, uint32_t bitLength, ProtoVersion v)
{
	DecodeResult res{};
	res.name = Packet::kName;
	if (!versionMatches(Packet::kVersionMask, v)) {
		res.status = DecodeStatus::kVersionMismatch;
		return res;
	}
	Reader r(data, bitLength);
	Packet pkt{};
	if (!pkt.serialize(r, v)) {
		res.status = DecodeStatus::kReadFailed;
		res.unreadBits = r.unreadBits();
		return res;
	}
	res.status = DecodeStatus::kOk;
	res.unreadBits = r.unreadBits();
	return res;
}

template <typename Packet>
DecodeResult decodeOnePacket(const uint8_t* data, uint32_t bitLength, ProtoVersion v)
{
	DecodeResult res{};
	res.name = Packet::kName;
	Reader r(data, bitLength);
	Packet pkt{};
	if (!pkt.serialize(r, v)) {
		res.status = DecodeStatus::kReadFailed;
		res.unreadBits = r.unreadBits();
		return res;
	}
	res.status = DecodeStatus::kOk;
	res.unreadBits = r.unreadBits();
	return res;
}

} // namespace detail

// Decodes an inbound RPC payload using the declarative protocol structs.
// Used by host-side ncapreplay and tests; grows as each F02 batch migrates RPCs.
inline DecodeResult decodeInboundRpc(int rpcId, const uint8_t* data, uint32_t bitLength, ProtoVersion v)
{
	switch (rpcId) {
		// Batch 1: Dialog, Checkpoint, GangZone, Pickup, TextLabel
		case ShowDialog::kRpcId: return detail::decodeOne<ShowDialog>(data, bitLength, v);
		case SetCheckpoint::kRpcId: return detail::decodeOne<SetCheckpoint>(data, bitLength, v);
		case DisableCheckpoint::kRpcId: return detail::decodeOne<DisableCheckpoint>(data, bitLength, v);
		case SetRaceCheckpoint::kRpcId: return detail::decodeOne<SetRaceCheckpoint>(data, bitLength, v);
		case DisableRaceCheckpoint::kRpcId: return detail::decodeOne<DisableRaceCheckpoint>(data, bitLength, v);
		case ShowGangZone::kRpcId: return detail::decodeOne<ShowGangZone>(data, bitLength, v);
		case HideGangZone::kRpcId: return detail::decodeOne<HideGangZone>(data, bitLength, v);
		case FlashGangZone::kRpcId: return detail::decodeOne<FlashGangZone>(data, bitLength, v);
		case StopFlashGangZone::kRpcId: return detail::decodeOne<StopFlashGangZone>(data, bitLength, v);
		case CreatePickup::kRpcId: return detail::decodeOne<CreatePickup>(data, bitLength, v);
		case DestroyPickup::kRpcId: return detail::decodeOne<DestroyPickup>(data, bitLength, v);
		case DestroyWeaponPickup::kRpcId: return detail::decodeOne<DestroyWeaponPickup>(data, bitLength, v);
		case Create3DTextLabel::kRpcId: return detail::decodeOne<Create3DTextLabel>(data, bitLength, v);
		case Delete3DTextLabel::kRpcId: return detail::decodeOne<Delete3DTextLabel>(data, bitLength, v);

		// Batch 2: Core upper half (session, chat, time/weather, player properties)
		case InitGame::kRpcId: return detail::decodeOne<InitGame>(data, bitLength, v);
		case ServerJoin::kRpcId: return detail::decodeOne<ServerJoin>(data, bitLength, v);
		case ServerQuit::kRpcId: return detail::decodeOne<ServerQuit>(data, bitLength, v);
		case GameModeRestart::kRpcId: return detail::decodeOne<GameModeRestart>(data, bitLength, v);
		case ConnectionRejected::kRpcId: return detail::decodeOne<ConnectionRejected>(data, bitLength, v);
		case ChatMessage::kRpcId: return detail::decodeOne<ChatMessage>(data, bitLength, v);
		case ClientMessage::kRpcId: return detail::decodeOne<ClientMessage>(data, bitLength, v);
		case ChatBubble::kRpcId: return detail::decodeOne<ChatBubble>(data, bitLength, v);
		case WorldTime::kRpcId: return detail::decodeOne<WorldTime>(data, bitLength, v);
		case Weather::kRpcId: return detail::decodeOne<Weather>(data, bitLength, v);
		case SetTimeEx::kRpcId: return detail::decodeOne<SetTimeEx>(data, bitLength, v);
		case ToggleClock::kRpcId: return detail::decodeOne<ToggleClock>(data, bitLength, v);
		case TimerUpdate::kRpcId: return detail::decodeOne<TimerUpdate>(data, bitLength, v);
		case UpdateScoresPingsIPs::kRpcId: return detail::decodeOne<UpdateScoresPingsIPs>(data, bitLength, v);
		case ClientCheckRequest::kRpcId: return detail::decodeOne<ClientCheckRequest>(data, bitLength, v);
		case SetGravity::kRpcId: return detail::decodeOne<SetGravity>(data, bitLength, v);
		case SetPlayerPos::kRpcId: return detail::decodeOne<SetPlayerPos>(data, bitLength, v);
		case SetPlayerPosFindZ::kRpcId: return detail::decodeOne<SetPlayerPosFindZ>(data, bitLength, v);
		case SetPlayerFacingAngle::kRpcId: return detail::decodeOne<SetPlayerFacingAngle>(data, bitLength, v);
		case SetPlayerHealth::kRpcId: return detail::decodeOne<SetPlayerHealth>(data, bitLength, v);
		case SetPlayerArmour::kRpcId: return detail::decodeOne<SetPlayerArmour>(data, bitLength, v);
		case SetPlayerColor::kRpcId: return detail::decodeOne<SetPlayerColor>(data, bitLength, v);
		case SetPlayerTeam::kRpcId: return detail::decodeOne<SetPlayerTeam>(data, bitLength, v);
		case SetPlayerName::kRpcId: return detail::decodeOne<SetPlayerName>(data, bitLength, v);
		case SetPlayerSkillLevel::kRpcId: return detail::decodeOne<SetPlayerSkillLevel>(data, bitLength, v);
		case SetPlayerSkin::kRpcId: return detail::decodeOne<SetPlayerSkin>(data, bitLength, v);
		case SetPlayerInterior::kRpcId: return detail::decodeOne<SetPlayerInterior>(data, bitLength, v);
		case TogglePlayerControllable::kRpcId: return detail::decodeOne<TogglePlayerControllable>(data, bitLength, v);
		case SetWorldBounds::kRpcId: return detail::decodeOne<SetWorldBounds>(data, bitLength, v);
		case GivePlayerMoney::kRpcId: return detail::decodeOne<GivePlayerMoney>(data, bitLength, v);
		case ResetPlayerMoney::kRpcId: return detail::decodeOne<ResetPlayerMoney>(data, bitLength, v);
		case SetPlayerWantedLevel::kRpcId: return detail::decodeOne<SetPlayerWantedLevel>(data, bitLength, v);
		case SetPlayerFightingStyle::kRpcId: return detail::decodeOne<SetPlayerFightingStyle>(data, bitLength, v);
		case SetPlayerVelocity::kRpcId: return detail::decodeOne<SetPlayerVelocity>(data, bitLength, v);
		case SetPlayerSpecialAction::kRpcId: return detail::decodeOne<SetPlayerSpecialAction>(data, bitLength, v);
		case SetPlayerDrunkLevel::kRpcId: return detail::decodeOne<SetPlayerDrunkLevel>(data, bitLength, v);
		case SetPlayerDrunkVisuals::kRpcId: return detail::decodeOne<SetPlayerDrunkVisuals>(data, bitLength, v);
		case SetPlayerDrunkHandling::kRpcId: return detail::decodeOne<SetPlayerDrunkHandling>(data, bitLength, v);
		case ResetPlayerWeapons::kRpcId: return detail::decodeOne<ResetPlayerWeapons>(data, bitLength, v);
		case GivePlayerWeapon::kRpcId: return detail::decodeOne<GivePlayerWeapon>(data, bitLength, v);
		case SetPlayerAmmo::kRpcId: return detail::decodeOne<SetPlayerAmmo>(data, bitLength, v);
		case SetArmedWeapon::kRpcId: return detail::decodeOne<SetArmedWeapon>(data, bitLength, v);
		case ShowGameText::kRpcId: return detail::decodeOne<ShowGameText>(data, bitLength, v);
		case DeathMessage::kRpcId: return detail::decodeOne<DeathMessage>(data, bitLength, v);
		case ShowNameTag::kRpcId: return detail::decodeOne<ShowNameTag>(data, bitLength, v);
		case SetPlayerMapIcon::kRpcId: return detail::decodeOne<SetPlayerMapIcon>(data, bitLength, v);
		case RemovePlayerMapIcon::kRpcId: return detail::decodeOne<RemovePlayerMapIcon>(data, bitLength, v);
		case CreateExplosion::kRpcId: return detail::decodeOne<CreateExplosion>(data, bitLength, v);
		case PlayerPlaySound::kRpcId: return detail::decodeOne<PlayerPlaySound>(data, bitLength, v);
		case EnableStuntBonus::kRpcId: return detail::decodeOne<EnableStuntBonus>(data, bitLength, v);
		case PlayAudioStream::kRpcId: return detail::decodeOne<PlayAudioStream>(data, bitLength, v);
		case StopAudioStream::kRpcId: return detail::decodeOne<StopAudioStream>(data, bitLength, v);
		case RemoveBuilding::kRpcId: return detail::decodeOne<RemoveBuilding>(data, bitLength, v);
		case DisableVehicleCollisions::kRpcId: return detail::decodeOne<DisableVehicleCollisions>(data, bitLength, v);

		// Batch 3: Core lower half & Class
		case WorldPlayerAdd::kRpcId: return detail::decodeOne<WorldPlayerAdd>(data, bitLength, v);
		case WorldPlayerDeath::kRpcId: return detail::decodeOne<WorldPlayerDeath>(data, bitLength, v);
		case WorldPlayerRemove::kRpcId: return detail::decodeOne<WorldPlayerRemove>(data, bitLength, v);
		case ApplyPlayerAnimation::kRpcId: return detail::decodeOne<ApplyPlayerAnimation>(data, bitLength, v);
		case ClearPlayerAnimations::kRpcId: return detail::decodeOne<ClearPlayerAnimations>(data, bitLength, v);
		case SetCameraPos::kRpcId: return detail::decodeOne<SetCameraPos>(data, bitLength, v);
		case SetCameraLookAt::kRpcId: return detail::decodeOne<SetCameraLookAt>(data, bitLength, v);
		case SetCameraBehindPlayer::kRpcId: return detail::decodeOne<SetCameraBehindPlayer>(data, bitLength, v);
		case InterpolateCamera::kRpcId: return detail::decodeOne<InterpolateCamera>(data, bitLength, v);
		case AttachCameraToObject::kRpcId: return detail::decodeOne<AttachCameraToObject>(data, bitLength, v);
		case TogglePlayerSpectating::kRpcId: return detail::decodeOne<TogglePlayerSpectating>(data, bitLength, v);
		case SpectatePlayer::kRpcId: return detail::decodeOne<SpectatePlayer>(data, bitLength, v);
		case SpectateVehicle::kRpcId: return detail::decodeOne<SpectateVehicle>(data, bitLength, v);
		case ToggleWidescreen::kRpcId: return detail::decodeOne<ToggleWidescreen>(data, bitLength, v);
		case EnableCameraTarget::kRpcId: return detail::decodeOne<EnableCameraTarget>(data, bitLength, v);
		case RequestClassReply::kRpcId: return detail::decodeOne<RequestClassReply>(data, bitLength, v);
		case RequestSpawnReply::kRpcId: return detail::decodeOne<RequestSpawnReply>(data, bitLength, v);
		case SetSpawnInfo::kRpcId: return detail::decodeOne<SetSpawnInfo>(data, bitLength, v);
		case ForceClassSelection::kRpcId: return detail::decodeOne<ForceClassSelection>(data, bitLength, v);

		// Batch 4: Vehicle
		case WorldVehicleAdd::kRpcId: return detail::decodeOne<WorldVehicleAdd>(data, bitLength, v);
		case WorldVehicleRemove::kRpcId: return detail::decodeOne<WorldVehicleRemove>(data, bitLength, v);
		case DamageVehicle::kRpcId: return detail::decodeOne<DamageVehicle>(data, bitLength, v);
		case VehicleParamsEx::kRpcId: return detail::decodeOne<VehicleParamsEx>(data, bitLength, v);
		case EnterVehicle::kRpcId: return detail::decodeOne<EnterVehicle>(data, bitLength, v);
		case ExitVehicle::kRpcId: return detail::decodeOne<ExitVehicle>(data, bitLength, v);
		case ScmEvent::kRpcId: return detail::decodeOne<ScmEvent>(data, bitLength, v);
		case SetVehiclePos::kRpcId: return detail::decodeOne<SetVehiclePos>(data, bitLength, v);
		case SetVehicleZAngle::kRpcId: return detail::decodeOne<SetVehicleZAngle>(data, bitLength, v);
		case SetVehicleParamsForPlayer::kRpcId: return detail::decodeOne<SetVehicleParamsForPlayer>(data, bitLength, v);
		case SetVehicleHealth::kRpcId: return detail::decodeOne<SetVehicleHealth>(data, bitLength, v);
		case SetVehicleVelocity::kRpcId: return detail::decodeOne<SetVehicleVelocity>(data, bitLength, v);
		case SetVehicleTireDamageStatus::kRpcId: return detail::decodeOne<SetVehicleTireDamageStatus>(data, bitLength, v);
		case SetVehicleNumberPlate::kRpcId: return detail::decodeOne<SetVehicleNumberPlate>(data, bitLength, v);
		case LinkVehicleToInterior::kRpcId: return detail::decodeOne<LinkVehicleToInterior>(data, bitLength, v);
		case AttachTrailerToVehicle::kRpcId: return detail::decodeOne<AttachTrailerToVehicle>(data, bitLength, v);
		case DetachTrailerFromVehicle::kRpcId: return detail::decodeOne<DetachTrailerFromVehicle>(data, bitLength, v);
		case RemoveVehicleComponent::kRpcId: return detail::decodeOne<RemoveVehicleComponent>(data, bitLength, v);
		case PutPlayerInVehicle::kRpcId: return detail::decodeOne<PutPlayerInVehicle>(data, bitLength, v);
		case RemovePlayerFromVehicle::kRpcId: return detail::decodeOne<RemovePlayerFromVehicle>(data, bitLength, v);

		// Batch 5: Object, Actor, TextDraw
		case CreateObject::kRpcId: return detail::decodeOne<CreateObject>(data, bitLength, v);
		case SetObjectPos::kRpcId: return detail::decodeOne<SetObjectPos>(data, bitLength, v);
		case SetObjectRotation::kRpcId: return detail::decodeOne<SetObjectRotation>(data, bitLength, v);
		case DestroyObject::kRpcId: return detail::decodeOne<DestroyObject>(data, bitLength, v);
		case MoveObject::kRpcId: return detail::decodeOne<MoveObject>(data, bitLength, v);
		case StopObject::kRpcId: return detail::decodeOne<StopObject>(data, bitLength, v);
		case AttachObjectToPlayer::kRpcId: return detail::decodeOne<AttachObjectToPlayer>(data, bitLength, v);
		case SetObjectMaterial::kRpcId: return detail::decodeOne<SetObjectMaterial>(data, bitLength, v);
		case StopObjectCameraCollision::kRpcId: return detail::decodeOne<StopObjectCameraCollision>(data, bitLength, v);
		case SetPlayerAttachedObject::kRpcId: return detail::decodeOne<SetPlayerAttachedObject>(data, bitLength, v);
		case EditAttachedObject::kRpcId: return detail::decodeOne<EditAttachedObject>(data, bitLength, v);
		case EditObject::kRpcId: return detail::decodeOne<EditObject>(data, bitLength, v);
		case EnterEditObject::kRpcId: return detail::decodeOne<EnterEditObject>(data, bitLength, v);
		case CancelEditObject::kRpcId: return detail::decodeOne<CancelEditObject>(data, bitLength, v);
		case ShowActor::kRpcId: return detail::decodeOne<ShowActor>(data, bitLength, v);
		case HideActor::kRpcId: return detail::decodeOne<HideActor>(data, bitLength, v);
		case ApplyActorAnimation::kRpcId: return detail::decodeOne<ApplyActorAnimation>(data, bitLength, v);
		case ClearActorAnimation::kRpcId: return detail::decodeOne<ClearActorAnimation>(data, bitLength, v);
		case SetActorFacingAngle::kRpcId: return detail::decodeOne<SetActorFacingAngle>(data, bitLength, v);
		case SetActorPos::kRpcId: return detail::decodeOne<SetActorPos>(data, bitLength, v);
		case SetActorHealth::kRpcId: return detail::decodeOne<SetActorHealth>(data, bitLength, v);
		case ShowTextDraw::kRpcId: return detail::decodeOne<ShowTextDraw>(data, bitLength, v);
		case HideTextDraw::kRpcId: return detail::decodeOne<HideTextDraw>(data, bitLength, v);
		case TextDrawSetString::kRpcId: return detail::decodeOne<TextDrawSetString>(data, bitLength, v);
		case SelectTextDraw::kRpcId: return detail::decodeOne<SelectTextDraw>(data, bitLength, v);

		// Menu (T11)
		case InitMenu::kRpcId: return detail::decodeOne<InitMenu>(data, bitLength, v);
		case ShowMenu::kRpcId: return detail::decodeOne<ShowMenu>(data, bitLength, v);
		case HideMenu::kRpcId: return detail::decodeOne<HideMenu>(data, bitLength, v);

		// Shop & Crime Report (T25)
		case SetPlayerShopName::kRpcId: return detail::decodeOne<SetPlayerShopName>(data, bitLength, v);
		case PlayCrimeReport::kRpcId: return detail::decodeOne<PlayCrimeReport>(data, bitLength, v);

		// Network Stats (T28)
		case SvrStats::kRpcId: return detail::decodeOne<SvrStats>(data, bitLength, v);

		// 0.3.DL Artwork Handshake (T30)
		case SetPlayerVirtualWorld::kRpcId: return detail::decodeOne<SetPlayerVirtualWorld>(data, bitLength, v);
		case DlModelRequest::kRpcId: return detail::decodeOne<DlModelRequest>(data, bitLength, v);
		case DlModelReset::kRpcId: return detail::decodeOne<DlModelReset>(data, bitLength, v);
		case DlModelUrl::kRpcId: return detail::decodeOne<DlModelUrl>(data, bitLength, v);
		case DlDownloadCompleted::kRpcId: return detail::decodeOne<DlDownloadCompleted>(data, bitLength, v);

		default: return DecodeResult{};
	}
}

// Decodes an inbound raw RakNet packet (AuthKey, ConnectionSucceeded, and all 8 inbound sync packets).
// Automatically peeks past a leading ID_TIMESTAMP (40) + 4-byte timestamp when selecting the packet type.
inline DecodeResult decodeInboundPacket(const uint8_t* data, uint32_t bitLength, ProtoVersion v)
{
	if (!data || bitLength < 8) return DecodeResult{};
	uint8_t id = data[0];
	if (id == static_cast<uint8_t>(ID_TIMESTAMP)) {
		if (bitLength < 48) return DecodeResult{};
		id = data[5];
	}
	switch (id) {
		case AuthKeyPacket::kPacketId: return detail::decodeOnePacket<AuthKeyPacket>(data, bitLength, v);
		case ConnectionSucceededPacket::kPacketId: return detail::decodeOnePacket<ConnectionSucceededPacket>(data, bitLength, v);
		case InboundPlayerSync::kPacketId: return detail::decodeOnePacket<InboundPlayerSync>(data, bitLength, v);
		case InboundVehicleSync::kPacketId: return detail::decodeOnePacket<InboundVehicleSync>(data, bitLength, v);
		case InboundAimSync::kPacketId: return detail::decodeOnePacket<InboundAimSync>(data, bitLength, v);
		case InboundBulletSync::kPacketId: return detail::decodeOnePacket<InboundBulletSync>(data, bitLength, v);
		case InboundPassengerSync::kPacketId: return detail::decodeOnePacket<InboundPassengerSync>(data, bitLength, v);
		case InboundMarkersSync::kPacketId: return detail::decodeOnePacket<InboundMarkersSync>(data, bitLength, v);
		case InboundTrailerSync::kPacketId: return detail::decodeOnePacket<InboundTrailerSync>(data, bitLength, v);
		case InboundUnoccupiedSync::kPacketId: return detail::decodeOnePacket<InboundUnoccupiedSync>(data, bitLength, v);
		default: return DecodeResult{};
	}
}

// Decodes an outbound RPC payload (client -> server) for capture validation.
inline DecodeResult decodeOutboundRpc(int rpcId, const uint8_t* data, uint32_t bitLength, ProtoVersion v)
{
	switch (rpcId) {
		case ClientJoin::kRpcId: return detail::decodeOne<ClientJoin>(data, bitLength, v);
		case ClientCheckReply::kRpcId: return detail::decodeOne<ClientCheckReply>(data, bitLength, v);
		case DialogResponse::kRpcId: return detail::decodeOne<DialogResponse>(data, bitLength, v);
		case ChatReq::kRpcId: return detail::decodeOne<ChatReq>(data, bitLength, v);
		case ServerCommandReq::kRpcId: return detail::decodeOne<ServerCommandReq>(data, bitLength, v);
		case UpdateScoresPingsReq::kRpcId: return detail::decodeOne<UpdateScoresPingsReq>(data, bitLength, v);
		case RequestClassReq::kRpcId: return detail::decodeOne<RequestClassReq>(data, bitLength, v);
		case RequestSpawnReq::kRpcId: return detail::decodeOne<RequestSpawnReq>(data, bitLength, v);
		case SpawnReq::kRpcId: return detail::decodeOne<SpawnReq>(data, bitLength, v);
		case DeathReq::kRpcId: return detail::decodeOne<DeathReq>(data, bitLength, v);
		case SetInteriorReq::kRpcId: return detail::decodeOne<SetInteriorReq>(data, bitLength, v);
		case EnterVehicleReq::kRpcId: return detail::decodeOne<EnterVehicleReq>(data, bitLength, v);
		case ExitVehicleReq::kRpcId: return detail::decodeOne<ExitVehicleReq>(data, bitLength, v);
		case DamageVehicleReq::kRpcId: return detail::decodeOne<DamageVehicleReq>(data, bitLength, v);
		case VehicleDestroyedReq::kRpcId: return detail::decodeOne<VehicleDestroyedReq>(data, bitLength, v);
		case PickedUpPickupReq::kRpcId: return detail::decodeOne<PickedUpPickupReq>(data, bitLength, v);
		case ClickTextDrawReq::kRpcId: return detail::decodeOne<ClickTextDrawReq>(data, bitLength, v);
		case ClickPlayerReq::kRpcId: return detail::decodeOne<ClickPlayerReq>(data, bitLength, v);
		case MapMarkerReq::kRpcId: return detail::decodeOne<MapMarkerReq>(data, bitLength, v);
		case GiveTakeDamageReq::kRpcId: return detail::decodeOne<GiveTakeDamageReq>(data, bitLength, v);
		case GiveDamageActorReq::kRpcId: return detail::decodeOne<GiveDamageActorReq>(data, bitLength, v);
		case EditObjectReply::kRpcId: return detail::decodeOne<EditObjectReply>(data, bitLength, v);
		case EditAttachedObjectReply::kRpcId: return detail::decodeOne<EditAttachedObjectReply>(data, bitLength, v);
		case SelectObjectReply::kRpcId: return detail::decodeOne<SelectObjectReply>(data, bitLength, v);
		case MenuSelectReq::kRpcId: return detail::decodeOne<MenuSelectReq>(data, bitLength, v);
		case MenuQuitReq::kRpcId: return detail::decodeOne<MenuQuitReq>(data, bitLength, v);
		case CameraTargetReply::kRpcId: return detail::decodeOne<CameraTargetReply>(data, bitLength, v);
		case PickedUpWeaponPickupReq::kRpcId: return detail::decodeOne<PickedUpWeaponPickupReq>(data, bitLength, v);
		case DlRequestDffReq::kRpcId: return detail::decodeOne<DlRequestDffReq>(data, bitLength, v);
		case DlRequestTxdReq::kRpcId: return detail::decodeOne<DlRequestTxdReq>(data, bitLength, v);
		case DlFinishDownloadReq::kRpcId: return detail::decodeOne<DlFinishDownloadReq>(data, bitLength, v);
		default: return DecodeResult{};
	}
}

// Decodes an outbound raw/sync packet (client -> server) for capture validation.
inline DecodeResult decodeOutboundPacket(const uint8_t* data, uint32_t bitLength, ProtoVersion v)
{
	if (!data || bitLength < 8) return DecodeResult{};
	switch (data[0]) {
		case AuthKeyPacket::kPacketId: return detail::decodeOnePacket<AuthKeyPacket>(data, bitLength, v);
		case RconCommandPacket::kPacketId: return detail::decodeOnePacket<RconCommandPacket>(data, bitLength, v);
		case OutboundPlayerSync::kPacketId: return detail::decodeOnePacket<OutboundPlayerSync>(data, bitLength, v);
		case OutboundVehicleSync::kPacketId: return detail::decodeOnePacket<OutboundVehicleSync>(data, bitLength, v);
		case OutboundPassengerSync::kPacketId: return detail::decodeOnePacket<OutboundPassengerSync>(data, bitLength, v);
		case OutboundUnoccupiedSync::kPacketId: return detail::decodeOnePacket<OutboundUnoccupiedSync>(data, bitLength, v);
		case OutboundTrailerSync::kPacketId: return detail::decodeOnePacket<OutboundTrailerSync>(data, bitLength, v);
		case OutboundAimSync::kPacketId: return detail::decodeOnePacket<OutboundAimSync>(data, bitLength, v);
		case OutboundBulletSync::kPacketId: return detail::decodeOnePacket<OutboundBulletSync>(data, bitLength, v);
		case OutboundSpectatorSync::kPacketId: return detail::decodeOnePacket<OutboundSpectatorSync>(data, bitLength, v);
		case OutboundStatsUpdate::kPacketId: return detail::decodeOnePacket<OutboundStatsUpdate>(data, bitLength, v);
		case OutboundWeaponsUpdate::kPacketId: return detail::decodeOnePacket<OutboundWeaponsUpdate>(data, bitLength, v);
		default: return DecodeResult{};
	}
}

} // namespace proto
