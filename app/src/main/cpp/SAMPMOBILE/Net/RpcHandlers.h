#pragma once

#include "Proto/RpcActor.h"
#include "Proto/RpcCheckpoint.h"
#include "Proto/RpcClass.h"
#include "Proto/RpcCore.h"
#include "Proto/RpcDialog.h"
#include "Proto/RpcGangZone.h"
#include "Proto/RpcMenu.h"
#include "Proto/RpcObject.h"
#include "Proto/RpcPickup.h"
#include "Proto/RpcTextDraw.h"
#include "Proto/RpcTextLabel.h"
#include "Proto/RpcVehicle.h"

namespace RpcHandlers {

// Batch 1: Dialog, Checkpoint, GangZone, Pickup, TextLabel
void OnShowDialog(const proto::ShowDialog& p);
void OnSetCheckpoint(const proto::SetCheckpoint& p);
void OnDisableCheckpoint(const proto::DisableCheckpoint& p);
void OnSetRaceCheckpoint(const proto::SetRaceCheckpoint& p);
void OnDisableRaceCheckpoint(const proto::DisableRaceCheckpoint& p);
void OnShowGangZone(const proto::ShowGangZone& p);
void OnHideGangZone(const proto::HideGangZone& p);
void OnFlashGangZone(const proto::FlashGangZone& p);
void OnStopFlashGangZone(const proto::StopFlashGangZone& p);
void OnCreatePickup(const proto::CreatePickup& p);
void OnDestroyPickup(const proto::DestroyPickup& p);
void OnDestroyWeaponPickup(const proto::DestroyWeaponPickup& p);
void OnCreate3DTextLabel(const proto::Create3DTextLabel& p);
void OnDelete3DTextLabel(const proto::Delete3DTextLabel& p);

// Batch 2: Core upper half (NetRPC)
void OnInitGame(const proto::InitGame& p);
void OnServerJoin(const proto::ServerJoin& p);
void OnServerQuit(const proto::ServerQuit& p);
void OnGameModeRestart(const proto::GameModeRestart& p);
void OnConnectionRejected(const proto::ConnectionRejected& p);
void OnChatMessage(const proto::ChatMessage& p);
void OnClientMessage(const proto::ClientMessage& p);
void OnChatBubble(const proto::ChatBubble& p);
void OnWorldTime(const proto::WorldTime& p);
void OnWeather(const proto::Weather& p);
void OnSetTimeEx(const proto::SetTimeEx& p);
void OnToggleClock(const proto::ToggleClock& p);
void OnTimerUpdate(const proto::TimerUpdate& p);
void OnUpdateScoresPingsIPs(const proto::UpdateScoresPingsIPs& p);
void OnClientCheck(const proto::ClientCheckRequest& p);
void OnSetPlayerSkillLevel(const proto::SetPlayerSkillLevel& p);

// Batch 2: Core upper half (ScriptRPC)
void OnSetGravity(const proto::SetGravity& p);
void OnSetPlayerPos(const proto::SetPlayerPos& p);
void OnSetPlayerPosFindZ(const proto::SetPlayerPosFindZ& p);
void OnSetPlayerFacingAngle(const proto::SetPlayerFacingAngle& p);
void OnSetPlayerHealth(const proto::SetPlayerHealth& p);
void OnSetPlayerArmour(const proto::SetPlayerArmour& p);
void OnSetPlayerColor(const proto::SetPlayerColor& p);
void OnSetPlayerTeam(const proto::SetPlayerTeam& p);
void OnSetPlayerName(const proto::SetPlayerName& p);
void OnSetPlayerSkin(const proto::SetPlayerSkin& p);
void OnSetPlayerInterior(const proto::SetPlayerInterior& p);
void OnTogglePlayerControllable(const proto::TogglePlayerControllable& p);
void OnSetWorldBounds(const proto::SetWorldBounds& p);
void OnGivePlayerMoney(const proto::GivePlayerMoney& p);
void OnResetPlayerMoney(const proto::ResetPlayerMoney& p);
void OnSetPlayerWantedLevel(const proto::SetPlayerWantedLevel& p);
void OnSetPlayerFightingStyle(const proto::SetPlayerFightingStyle& p);
void OnSetPlayerVelocity(const proto::SetPlayerVelocity& p);
void OnSetPlayerSpecialAction(const proto::SetPlayerSpecialAction& p);
void OnSetPlayerDrunkLevel(const proto::SetPlayerDrunkLevel& p);
void OnSetPlayerDrunkVisuals(const proto::SetPlayerDrunkVisuals& p);
void OnSetPlayerDrunkHandling(const proto::SetPlayerDrunkHandling& p);
void OnResetPlayerWeapons(const proto::ResetPlayerWeapons& p);
void OnGivePlayerWeapon(const proto::GivePlayerWeapon& p);
void OnSetPlayerAmmo(const proto::SetPlayerAmmo& p);
void OnSetArmedWeapon(const proto::SetArmedWeapon& p);
void OnShowGameText(const proto::ShowGameText& p);
void OnDeathMessage(const proto::DeathMessage& p);
void OnShowNameTag(const proto::ShowNameTag& p);
void OnSetPlayerMapIcon(const proto::SetPlayerMapIcon& p);
void OnRemovePlayerMapIcon(const proto::RemovePlayerMapIcon& p);
void OnCreateExplosion(const proto::CreateExplosion& p);
void OnPlayerPlaySound(const proto::PlayerPlaySound& p);
void OnEnableStuntBonus(const proto::EnableStuntBonus& p);
void OnPlayAudioStream(const proto::PlayAudioStream& p);
void OnStopAudioStream(const proto::StopAudioStream& p);
void OnRemoveBuilding(const proto::RemoveBuilding& p);
void OnDisableVehicleCollisions(const proto::DisableVehicleCollisions& p);

// Batch 3: Core lower half & Class
void OnWorldPlayerAdd(const proto::WorldPlayerAdd& p);
void OnWorldPlayerDeath(const proto::WorldPlayerDeath& p);
void OnWorldPlayerRemove(const proto::WorldPlayerRemove& p);
void OnApplyPlayerAnimation(const proto::ApplyPlayerAnimation& p);
void OnClearPlayerAnimations(const proto::ClearPlayerAnimations& p);
void OnSetCameraPos(const proto::SetCameraPos& p);
void OnSetCameraLookAt(const proto::SetCameraLookAt& p);
void OnSetCameraBehindPlayer(const proto::SetCameraBehindPlayer& p);
void OnInterpolateCamera(const proto::InterpolateCamera& p);
void OnAttachCameraToObject(const proto::AttachCameraToObject& p);
void OnTogglePlayerSpectating(const proto::TogglePlayerSpectating& p);
void OnSpectatePlayer(const proto::SpectatePlayer& p);
void OnSpectateVehicle(const proto::SpectateVehicle& p);
void OnToggleWidescreen(const proto::ToggleWidescreen& p);
void OnEnableCameraTarget(const proto::EnableCameraTarget& p);
void OnRequestClass(const proto::RequestClassReply& p);
void OnRequestSpawn(const proto::RequestSpawnReply& p);
void OnSetSpawnInfo(const proto::SetSpawnInfo& p);
void OnForceClassSelection(const proto::ForceClassSelection& p);

// Batch 4: Vehicle
void OnWorldVehicleAdd(const proto::WorldVehicleAdd& p);
void OnWorldVehicleRemove(const proto::WorldVehicleRemove& p);
void OnDamageVehicle(const proto::DamageVehicle& p);
void OnVehicleParamsEx(const proto::VehicleParamsEx& p);
void OnEnterVehicle(const proto::EnterVehicle& p);
void OnExitVehicle(const proto::ExitVehicle& p);
void OnScmEvent(const proto::ScmEvent& p);
void OnSetVehiclePos(const proto::SetVehiclePos& p);
void OnSetVehicleZAngle(const proto::SetVehicleZAngle& p);
void OnSetVehicleParamsForPlayer(const proto::SetVehicleParamsForPlayer& p);
void OnSetVehicleHealth(const proto::SetVehicleHealth& p);
void OnSetVehicleVelocity(const proto::SetVehicleVelocity& p);
void OnSetVehicleTireDamageStatus(const proto::SetVehicleTireDamageStatus& p);
void OnSetVehicleNumberPlate(const proto::SetVehicleNumberPlate& p);
void OnLinkVehicleToInterior(const proto::LinkVehicleToInterior& p);
void OnAttachTrailerToVehicle(const proto::AttachTrailerToVehicle& p);
void OnDetachTrailerFromVehicle(const proto::DetachTrailerFromVehicle& p);
void OnRemoveVehicleComponent(const proto::RemoveVehicleComponent& p);
void OnPutPlayerInVehicle(const proto::PutPlayerInVehicle& p);
void OnRemovePlayerFromVehicle(const proto::RemovePlayerFromVehicle& p);

// Batch 5: Object, Actor, TextDraw
void OnCreateObject(const proto::CreateObject& p);
void OnSetObjectPos(const proto::SetObjectPos& p);
void OnSetObjectRotation(const proto::SetObjectRotation& p);
void OnDestroyObject(const proto::DestroyObject& p);
void OnMoveObject(const proto::MoveObject& p);
void OnStopObject(const proto::StopObject& p);
void OnAttachObjectToPlayer(const proto::AttachObjectToPlayer& p);
void OnSetObjectMaterial(const proto::SetObjectMaterial& p);
void OnStopObjectCameraCollision(const proto::StopObjectCameraCollision& p);
void OnSetPlayerAttachedObject(const proto::SetPlayerAttachedObject& p);
void OnEditAttachedObject(const proto::EditAttachedObject& p);
void OnEditObject(const proto::EditObject& p);
void OnEnterEditObject(const proto::EnterEditObject& p);
void OnCancelEditObject(const proto::CancelEditObject& p);
void OnShowActor(const proto::ShowActor& p);
void OnHideActor(const proto::HideActor& p);
void OnApplyActorAnimation(const proto::ApplyActorAnimation& p);
void OnClearActorAnimation(const proto::ClearActorAnimation& p);
void OnSetActorFacingAngle(const proto::SetActorFacingAngle& p);
void OnSetActorPos(const proto::SetActorPos& p);
void OnSetActorHealth(const proto::SetActorHealth& p);
void OnShowTextDraw(const proto::ShowTextDraw& p);
void OnHideTextDraw(const proto::HideTextDraw& p);
void OnTextDrawSetString(const proto::TextDrawSetString& p);
void OnSelectTextDraw(const proto::SelectTextDraw& p);

// Menu (T11)
void OnInitMenu(const proto::InitMenu& p);
void OnShowMenu(const proto::ShowMenu& p);
void OnHideMenu(const proto::HideMenu& p);

// Shop & Crime Report (T25)
void OnSetPlayerShopName(const proto::SetPlayerShopName& p);
void OnPlayCrimeReport(const proto::PlayCrimeReport& p);

// Network Stats (T28)
void OnSvrStats(const proto::SvrStats& p);

// 0.3.DL Artwork Handshake Placeholder (T30)
void OnSetPlayerVirtualWorld(const proto::SetPlayerVirtualWorld& p);
void OnDlModelRequest(const proto::DlModelRequest& p);
void OnDlModelReset(const proto::DlModelReset& p);
void OnDlModelUrl(const proto::DlModelUrl& p);
void OnDlDownloadCompleted(const proto::DlDownloadCompleted& p);

} // namespace RpcHandlers
