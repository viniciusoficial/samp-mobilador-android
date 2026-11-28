#include "RpcHandlers.h"
#include "NetGame.h"
#include "LocalPlayer.h"
#include "RemotePlayer.h"
#include "VehiclePool.h"
#include "../Client.h"
#include "../Game/Game.h"
#include "../Game/Aimstuff.h"
#include "../Game/Animation.h"
#include "../Game/AudioStream.h"
#include "../Game/Camera.h"
#include "../Game/Common.h"
#include "../Game/Object.h"
#include "../Game/PlayerPed.h"
#include "../Game/Scripting.h"
#include "../Game/Vehicle.h"
#include "../Game/Util.h"
#include "../Java/EditObject.h"
#include "../UI/UI.h"
#include "../UI/SAMPWidgets/Chat.h"
#include "../UI/SAMPWidgets/DeathWindow.h"
#include "../UI/SAMPWidgets/Spawn.h"
#include <spdlog/spdlog.h>
#include <cmath>

namespace {
inline bool isFinite3(float x, float y, float z) {
	return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
}
} // namespace

void RpcHandlers::OnSetGravity(const proto::SetGravity& p)
{
	spdlog::debug("ScriptRPC: ScrSetGravity");
	if (pGame && std::isfinite(p.gravity)) {
		pGame->SetGravity(p.gravity);
	}
}

void RpcHandlers::OnSetCameraPos(const proto::SetCameraPos& p)
{
	spdlog::debug("ScriptRPC: ScrSetCameraPos");
	if (pGame && pGame->GetCamera() && isFinite3(p.position.x, p.position.y, p.position.z)) {
		pGame->GetCamera()->SetPosition(p.position.x, p.position.y, p.position.z, 0.0f, 0.0f, 0.0f);
	}
}

void RpcHandlers::OnSetCameraLookAt(const proto::SetCameraLookAt& p)
{
	spdlog::debug("ScriptRPC: ScrSetCameraLookAt");
	if (!pGame || !pGame->GetCamera() || !isFinite3(p.position.x, p.position.y, p.position.z)) {
		return;
	}

	uint8_t byteType = p.cutType;
	if (byteType < 1 || byteType > 2) {
		byteType = 2;
	}

	pGame->GetCamera()->LookAtPoint(p.position.x, p.position.y, p.position.z, byteType);
}

void RpcHandlers::OnInterpolateCamera(const proto::InterpolateCamera& p)
{
	spdlog::debug("ScriptRPC: ScrInterpolateCamera");
	if (!pGame || !pGame->GetCamera() || !pNetGame || !pNetGame->GetPlayerPool() || !pNetGame->GetPlayerPool()->GetLocalPlayer()) {
		return;
	}
	if (!isFinite3(p.from.x, p.from.y, p.from.z) || !isFinite3(p.to.x, p.to.y, p.to.z)) {
		return;
	}

	uint8_t byteMode = p.cutType;
	if (byteMode < 1 || byteMode > 2) {
		byteMode = 2;
	}

	if (p.time > 0) {
		sa::CVector vecFrom(p.from.x, p.from.y, p.from.z);
		sa::CVector vecTo(p.to.x, p.to.y, p.to.z);
		pNetGame->GetPlayerPool()->GetLocalPlayer()->m_bSpectateProcessed = true;
		pGame->GetCamera()->m_pAttachedTo = nullptr;
		if (p.setPos) {
			pGame->GetCamera()->InterpolateCameraPos(&vecFrom, &vecTo, p.time, byteMode);
		}
		else {
			pGame->GetCamera()->InterpolateCameraLookAt(&vecFrom, &vecTo, p.time, byteMode);
		}
	}
}

void RpcHandlers::OnTogglePlayerSpectating(const proto::TogglePlayerSpectating& p)
{
	spdlog::debug("ScriptRPC: ScrTogglePlayerSpectating");
	spdlog::debug("dwToggle: {}", p.toggle);

	if (pNetGame && pNetGame->GetPlayerPool() && pNetGame->GetPlayerPool()->GetLocalPlayer()) {
		pNetGame->GetPlayerPool()->GetLocalPlayer()->ToggleSpectating(p.toggle);
	}
}

void RpcHandlers::OnSetSpawnInfo(const proto::SetSpawnInfo& p)
{
	spdlog::debug("ScriptRPC: ScrSetSpawnInfo");
	if (!pNetGame || !pNetGame->GetPlayerPool() || !pNetGame->GetPlayerPool()->GetLocalPlayer()) {
		return;
	}
	if (!isFinite3(p.spawnInfo.position.x, p.spawnInfo.position.y, p.spawnInfo.position.z)
			|| !std::isfinite(p.spawnInfo.rotation)) {
		return;
	}

	PLAYER_SPAWN_INFO spawnInfo{};
	spawnInfo.byteTeam = p.spawnInfo.team;
	spawnInfo.iSkin = p.spawnInfo.skin;
	spawnInfo.unk = p.spawnInfo.unk;
	spawnInfo.vecPos = sa::CVector(p.spawnInfo.position.x, p.spawnInfo.position.y, p.spawnInfo.position.z);
	spawnInfo.fRotation = p.spawnInfo.rotation;
	memcpy(spawnInfo.iSpawnWeapons, p.spawnInfo.spawnWeapons.data(), sizeof(spawnInfo.iSpawnWeapons));
	memcpy(spawnInfo.iSpawnWeaponsAmmo, p.spawnInfo.spawnWeaponsAmmo.data(), sizeof(spawnInfo.iSpawnWeaponsAmmo));

	pNetGame->GetPlayerPool()->GetLocalPlayer()->SetSpawnInfo(&spawnInfo, p.spawnInfo.customSkin);
}

void RpcHandlers::OnForceClassSelection(const proto::ForceClassSelection&)
{
	spdlog::debug("ScriptRPC: ScrForceClassSelection");
	CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
	if (pPlayerPool && pPlayerPool->GetLocalPlayer()) {
		pPlayerPool->GetLocalPlayer()->ReturnToClassSelection();
	}
}

void RpcHandlers::OnAttachCameraToObject(const proto::AttachCameraToObject& p)
{
	spdlog::debug("ScriptRPC: ScrAttachCameraToObject");

	CObjectPool* pObjectPool = pNetGame->GetObjectPool();
	if (!pObjectPool) return;

	CObject* pObject = pObjectPool->GetAt(p.objectId);
	if (!pObject) return;

	CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
	if (pPlayerPool && pPlayerPool->GetLocalPlayer()) {
		pPlayerPool->GetLocalPlayer()->m_bSpectateProcessed = true;
	}

	if (pGame && pGame->GetCamera()) {
		pGame->GetCamera()->Attach(pObject);
	}
}

void RpcHandlers::OnSetPlayerSkin(const proto::SetPlayerSkin& p)
{
	spdlog::debug("ScriptRPC: ScrSetPlayerSkin");

	CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
	if (!pPlayerPool) {
		return;
	}

	const int baseSkin = static_cast<int>(p.skinId);
	const int customSkin = static_cast<int>(p.customSkinId);
	const int previewSkin = ResolveEffectivePedSkin(baseSkin, customSkin);
	if (IsValidPedModel(static_cast<uint>(previewSkin))) {
		if (pPlayerPool->GetLocalPlayerID() == p.playerId) {
			if (pPlayerPool->GetLocalPlayer() && pPlayerPool->GetLocalPlayer()->GetPlayerPed()) {
				pPlayerPool->GetLocalPlayer()->GetPlayerPed()->SetSkin(baseSkin, customSkin);
			}
		}
		else if (p.playerId < MAX_PLAYERS) {
			const PLAYERID pid = static_cast<PLAYERID>(p.playerId);
			if (pPlayerPool->GetSlotState(pid)) {
				CRemotePlayer* pPlayer = pPlayerPool->GetAt(pid);
				if (!pPlayer || pPlayer->GetState() == PLAYER_STATE_NONE) {
					return;
				}
				CPlayerPed* pPlayerPed = pPlayer->GetPlayerPed();
				if (!pPlayerPed) {
					return;
				}
				pPlayerPed->SetSkin(baseSkin, customSkin);
			}
		}
	}
	else {
		if (pUI) pUI->chat()->addDebugMessage("Warning: SetPlayerSkin %u isn't a valid ped model.", p.skinId);
	}
}

void RpcHandlers::OnSetPlayerMapIcon(const proto::SetPlayerMapIcon& p)
{
	spdlog::debug("ScriptRPC: ScrSetPlayerMapIcon");

	// CRadar::RadarBlipSprites holds 64 entries (0..63); guard out-of-range IDs and non-finite coords.
	if (!proto::isFinite(p.position) || p.type >= 64) {
		return;
	}

	pNetGame->SetMapIcon(p.iconId, p.position.x, p.position.y, p.position.z, p.type, p.color, p.style);
}

void RpcHandlers::OnRemovePlayerMapIcon(const proto::RemovePlayerMapIcon& p)
{
	spdlog::debug("ScriptRPC: ScrRemovePlayerMapIcon");
	pNetGame->DisableMapIcon(p.iconId);
}

void RpcHandlers::OnShowNameTag(const proto::ShowNameTag& p)
{
	spdlog::debug("ScriptRPC: ScrShowNameTag");

	CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
	if (pPlayerPool && pPlayerPool->GetSlotState(p.playerId)) {
		CRemotePlayer* pPlayer = pPlayerPool->GetAt(p.playerId);
		if (pPlayer) {
			pPlayer->m_bShowNameTag = p.show;
		}
	}
}

void RpcHandlers::OnApplyPlayerAnimation(const proto::ApplyPlayerAnimation& p)
{
	spdlog::debug("ScriptRPC: ScrApplyPlayerAnimation");

	CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
	CPlayerPed* pPlayerPed = nullptr;

	if (pPlayerPool) {
		if (pPlayerPool->GetLocalPlayerID() == p.playerId) {
			pPlayerPed = pPlayerPool->GetLocalPlayer()->GetPlayerPed();
		}
		else if (pPlayerPool->GetSlotState(p.playerId)) {
			CRemotePlayer* pRemotePlayer = pPlayerPool->GetAt(p.playerId);
			if (pRemotePlayer) {
				pPlayerPed = pRemotePlayer->GetPlayerPed();
			}
		}

		spdlog::debug("ApplyAnimation: {}, {}", p.animLib, p.animName);

		if (pPlayerPed) {
			pPlayerPed->ApplyAnimation(
				const_cast<char*>(p.animName.c_str()),
				const_cast<char*>(p.animLib.c_str()),
				p.delta, p.loop, p.lockX, p.lockY, p.freeze, p.time);
		}
	}
}

void RpcHandlers::OnClearPlayerAnimations(const proto::ClearPlayerAnimations& p)
{
	spdlog::debug("ScriptRPC: ScrClearPlayerAnimations");

	CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
	CPlayerPed* pPlayerPed = nullptr;

	if (pPlayerPool) {
		if (p.playerId == pPlayerPool->GetLocalPlayerID()) {
			pPlayerPed = pPlayerPool->GetLocalPlayer()->GetPlayerPed();
		}
		else {
			if (pPlayerPool->GetSlotState(p.playerId)) {
				CRemotePlayer* pRemotePlayer = pPlayerPool->GetAt(p.playerId);
				if (pRemotePlayer) {
					pPlayerPed = pRemotePlayer->GetPlayerPed();
				}
			}
		}

		if (pPlayerPed && pPlayerPed->m_ped) {
			RwMatrix mat = pPlayerPed->m_ped->GetMatrix().ToRwMatrix();
			pPlayerPed->TeleportTo(mat.pos.x, mat.pos.y, mat.pos.z);
			if (pPlayerPed->m_gtaId) {
				ScriptCommand(&clear_char_tasks, pPlayerPed->m_gtaId);
			}
		}
	}
}

void RpcHandlers::OnSetPlayerHealth(const proto::SetPlayerHealth& p)
{
	spdlog::debug("ScriptRPC: ScrSetPlayerHealth");
	if (!pNetGame || !pNetGame->GetPlayerPool() || !pNetGame->GetPlayerPool()->GetLocalPlayer() || !std::isfinite(p.health)) {
		return;
	}

	CPlayerPed* pPlayerPed = pNetGame->GetPlayerPool()->GetLocalPlayer()->GetPlayerPed();
	if (pPlayerPed) {
		pPlayerPed->SetHealth(p.health);
	}
}

void RpcHandlers::OnGivePlayerWeapon(const proto::GivePlayerWeapon& p)
{
	spdlog::debug("ScriptRPC: ScrGivePlayerWeapon");
	if (!pNetGame || !pNetGame->GetPlayerPool() || !pNetGame->GetPlayerPool()->GetLocalPlayer()) {
		return;
	}

	CPlayerPed* pPlayerPed = pNetGame->GetPlayerPool()->GetLocalPlayer()->GetPlayerPed();
	if (pPlayerPed) {
		pPlayerPed->GiveWeapon(p.weapon, p.ammo);
	}
}

void RpcHandlers::OnSetPlayerInterior(const proto::SetPlayerInterior& p)
{
	spdlog::debug("ScriptRPC: ScrSetPlayerInterior");
	if (CPlayerPed* pPed = pGame ? pGame->FindPlayerPed() : nullptr) {
		pPed->SetInterior(p.interiorId, true);
	}
}

void RpcHandlers::OnSetPlayerAmmo(const proto::SetPlayerAmmo& p)
{
	spdlog::debug("ScriptRPC: ScrSetPlayerAmmo");

	CLocalPlayer* pLocalPlayer = pNetGame->GetPlayerPool() ? pNetGame->GetPlayerPool()->GetLocalPlayer() : nullptr;
	if (pLocalPlayer && pLocalPlayer->GetPlayerPed()) {
		pLocalPlayer->GetPlayerPed()->SetAmmo(p.weapon, p.ammo);
	}
}

void RpcHandlers::OnCreateExplosion(const proto::CreateExplosion& p)
{
	spdlog::debug("ScriptRPC: ScrCreateExplosion");
	ScriptCommand(&create_explosion_with_radius, p.position.x, p.position.y, p.position.z, p.type, p.radius);
}

#define SPECTATE_TYPE_NORMAL 1
#define SPECTATE_TYPE_FIXED 2
#define SPECTATE_TYPE_SIDE 3

void RpcHandlers::OnSpectatePlayer(const proto::SpectatePlayer& p)
{
	spdlog::debug("ScriptRPC: ScrSpectatePlayer");

	uint8_t byteMode = p.mode;
	switch (byteMode) {
		case SPECTATE_TYPE_FIXED: byteMode = 15;
			break;
		case SPECTATE_TYPE_SIDE: byteMode = 14;
			break;
		default: byteMode = 4;
	}

	CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
	CLocalPlayer* pLocalPlayer = pPlayerPool ? pPlayerPool->GetLocalPlayer() : nullptr;
	if (!pLocalPlayer) {
		return;
	}
	pLocalPlayer->m_byteSpectateMode = byteMode;
	pLocalPlayer->SpectatePlayer(p.playerId);
}

void RpcHandlers::OnSpectateVehicle(const proto::SpectateVehicle& p)
{
	spdlog::debug("ScriptRPC: ScrSpectateVehicle");

	uint8_t byteMode = p.mode;
	switch (byteMode) {
		case SPECTATE_TYPE_FIXED: byteMode = 15;
			break;
		case SPECTATE_TYPE_SIDE: byteMode = 14;
			break;
		default: byteMode = 3;
	}

	CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
	CLocalPlayer* pLocalPlayer = pPlayerPool ? pPlayerPool->GetLocalPlayer() : nullptr;
	if (!pLocalPlayer) {
		return;
	}
	pLocalPlayer->m_byteSpectateMode = byteMode;
	pLocalPlayer->SpectateVehicle(p.vehicleId);
}

void RpcHandlers::OnSetPlayerWantedLevel(const proto::SetPlayerWantedLevel& p)
{
	spdlog::debug("ScriptRPC: ScrSetPlayerWantedLevel");

	if (pGame) {
		pGame->SetWantedLevel(p.level);
	}
}

void RpcHandlers::OnSetPlayerSpecialAction(const proto::SetPlayerSpecialAction& p)
{
	spdlog::debug("ScriptRPC: ScrSetPlayerSpecialAction");

	CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
	if (!pPlayerPool) {
		return;
	}

	pPlayerPool->GetLocalPlayer()->ApplySpecialAction(p.action);
}

void RpcHandlers::OnEnableStuntBonus(const proto::EnableStuntBonus& p)
{
	spdlog::debug("ScriptRPC: ScrEnableStuntBonus");
	pGame->EnableStuntBonus(p.enable);
}

void RpcHandlers::OnSetPlayerFightingStyle(const proto::SetPlayerFightingStyle& p)
{
	spdlog::debug("ScriptRPC: ScrSetPlayerFightingStyle");

	CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
	if (!pPlayerPool) {
		return;
	}

	CPlayerPed* pPlayerPed = nullptr;

	if (p.playerId == pPlayerPool->GetLocalPlayerID()) {
		pPlayerPed = pPlayerPool->GetLocalPlayer()->GetPlayerPed();
	}
	else {
		CRemotePlayer* pRemotePlayer = pPlayerPool->GetAt(p.playerId);
		if (pRemotePlayer) {
			pPlayerPed = pRemotePlayer->GetPlayerPed();
		}
	}

	if (pPlayerPed) {
		pPlayerPed->SetFightingStyle(p.style);
	}
}

void RpcHandlers::OnSetPlayerVelocity(const proto::SetPlayerVelocity& p)
{
	spdlog::debug("ScriptRPC: ScrSetPlayerVelocity");

	CPlayerPed* pPlayerPed = pGame->FindPlayerPed();
	if (!pPlayerPed) {
		return;
	}

	if (pPlayerPed->IsOnGround()) {
		auto flags = pPlayerPed->GetStateFlags();
		flags.bIsStanding = !flags.bIsStanding;
		flags.bWasStanding = !flags.bWasStanding;
		pPlayerPed->SetStateFlags(flags);
	}

	sa::CVector vecVelocity(p.velocity.x, p.velocity.y, p.velocity.z);
	pPlayerPed->m_ped->SetVelocity(vecVelocity);
}

void RpcHandlers::OnToggleWidescreen(const proto::ToggleWidescreen& p)
{
	spdlog::debug("ScriptRPC: ScrToggleWidescreen");
	ScriptCommand(&toggle_widescreen, p.toggle);
}

void RpcHandlers::OnSetPlayerTeam(const proto::SetPlayerTeam& p)
{
	spdlog::debug("ScriptRPC: ScrSetPlayerTeam");

	CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
	if (!pPlayerPool) {
		return;
	}

	CLocalPlayer* pLocalPlayer = pPlayerPool->GetLocalPlayer();
	if (!pLocalPlayer) {
		return;
	}

	if (p.playerId == pPlayerPool->GetLocalPlayerID()) {
		pLocalPlayer->m_byteTeam = p.team;
	}
	else {
		CRemotePlayer* pRemotePlayer = pPlayerPool->GetAt(p.playerId);
		if (pRemotePlayer) {
			pRemotePlayer->SetTeam(p.team);
		}
	}
}

void RpcHandlers::OnSetPlayerName(const proto::SetPlayerName& p)
{
	spdlog::debug("ScriptRPC: ScrSetPlayerName");

	if (p.name.size() > MAX_PLAYER_NAME) {
		return;
	}

	CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
	if (!pPlayerPool) {
		return;
	}

	if (p.success == 1) {
		pPlayerPool->SetPlayerName(p.playerId, p.name.c_str());
	}

	if (pPlayerPool->GetLocalPlayerID() == p.playerId) {
		pPlayerPool->SetLocalPlayerName(p.name.c_str());
	}
}

void RpcHandlers::OnSetPlayerPos(const proto::SetPlayerPos& p)
{
	spdlog::debug("ScriptRPC: ScrSetPlayerPos");

	CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
	if (!pPlayerPool) {
		return;
	}
	CLocalPlayer* pLocalPlayer = pPlayerPool->GetLocalPlayer();
	if (!pLocalPlayer) {
		return;
	}

	if (!isFinite3(p.position.x, p.position.y, p.position.z)) {
		return;
	}
	pLocalPlayer->DisableSurfing();
	if (CPlayerPed* pPed = pLocalPlayer->GetPlayerPed()) {
		if (pPed->IsInVehicle()) {
			pPed->RemoveFromVehicleAndPutAt(p.position.x, p.position.y, p.position.z);
		}
		else {
			pPed->TeleportTo(p.position.x, p.position.y, p.position.z);
		}
	}
}

void RpcHandlers::OnSetPlayerPosFindZ(const proto::SetPlayerPosFindZ& p)
{
	spdlog::debug("ScriptRPC: ScrSetPlayerPosFindZ");
	if (!pGame || !isFinite3(p.position.x, p.position.y, p.position.z)) {
		return;
	}

	CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
	if (!pPlayerPool) {
		return;
	}

	CLocalPlayer* pLocalPlayer = pPlayerPool->GetLocalPlayer();
	if (!pLocalPlayer) {
		return;
	}

	float fZ = pGame->FindGroundZForCoord(p.position.x, p.position.y, p.position.z) + 1.5f;
	pLocalPlayer->DisableSurfing();
	if (CPlayerPed* pPed = pLocalPlayer->GetPlayerPed()) {
		if (pPed->IsInVehicle()) {
			pPed->RemoveFromVehicleAndPutAt(p.position.x, p.position.y, fZ);
		}
		else {
			pPed->TeleportTo(p.position.x, p.position.y, fZ);
		}
	}
}

void RpcHandlers::OnPutPlayerInVehicle(const proto::PutPlayerInVehicle& p)
{
	spdlog::debug("ScriptRPC: ScrPutPlayerInVehicle");

	CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
	if (!pVehiclePool) {
		return;
	}

	if (CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool()) {
		if (CLocalPlayer* pLocalPlayer = pPlayerPool->GetLocalPlayer()) {
			pLocalPlayer->DisableSurfing();
		}
	}

	uint32_t dwGtaVehicleID = pVehiclePool->FindGtaIDFromID(p.vehicleId);
	CVehicle* pVehicle = pVehiclePool->GetAt(p.vehicleId);
	CPlayerPed* pPlayerPed = pGame->FindPlayerPed();

	if (dwGtaVehicleID != INVALID_VEHICLE_ID && pVehicle && pPlayerPed) {
		pPlayerPed->PutDirectlyInVehicle(dwGtaVehicleID, p.seatId);
	}
}

void RpcHandlers::OnRemovePlayerFromVehicle(const proto::RemovePlayerFromVehicle&)
{
	spdlog::debug("ScriptRPC: ScrRemovePlayerFromVehicle");
	CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
	if (!pPlayerPool) {
		return;
	}

	CLocalPlayer* pLocalPlayer = pPlayerPool->GetLocalPlayer();
	if (!pLocalPlayer || !pLocalPlayer->GetPlayerPed()) {
		return;
	}

	pLocalPlayer->GetPlayerPed()->ExitCurrentVehicle();
}

void RpcHandlers::OnSetPlayerColor(const proto::SetPlayerColor& p)
{
	spdlog::debug("ScriptRPC: ScrSetPlayerColor");

	CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
	if (!pPlayerPool) {
		return;
	}

	if (pPlayerPool->GetLocalPlayerID() == p.playerId) {
		CLocalPlayer* pLocalPlayer = pPlayerPool->GetLocalPlayer();
		if (pLocalPlayer) {
			pLocalPlayer->SetPlayerColor(p.color);
		}
	}
	else {
		CRemotePlayer* pRemotePlayer = pPlayerPool->GetAt(p.playerId);
		if (pRemotePlayer) {
			pRemotePlayer->SetPlayerColor(p.color);
		}
	}
}

void RpcHandlers::OnShowGameText(const proto::ShowGameText& p)
{
	spdlog::debug("ScriptRPC: ScrShowGameText");

	if (p.style < 0 || p.style > 6) {
		return;
	}

	std::string text = (p.text.size() > 512) ? p.text.substr(0, 512) : p.text;
	if (pGame) pGame->DisplayGameText(text.c_str(), p.time, p.style);
}

void RpcHandlers::OnSetCameraBehindPlayer(const proto::SetCameraBehindPlayer&)
{
	spdlog::debug("ScriptRPC: ScrSetPlayerCameraBehindPlayer");
	pGame->GetCamera()->SetBehindPlayer();
}

void RpcHandlers::OnTogglePlayerControllable(const proto::TogglePlayerControllable& p)
{
	spdlog::debug("ScriptRPC: ScrTogglePlayerControllable");
	if (CPlayerPed* pPed = pGame ? pGame->FindPlayerPed() : nullptr) {
		pPed->TogglePlayerControllable(p.controllable);
	}
}

void RpcHandlers::OnPlayerPlaySound(const proto::PlayerPlaySound& p)
{
	spdlog::debug("ScriptRPC: ScrPlayerPlaySound id={}", p.soundId);
	if (!proto::isFinite(p.position)) {
		return;
	}
	if (pGame) {
		pGame->PlaySoundEx(p.soundId, p.position.x, p.position.y, p.position.z);
	}
}

void RpcHandlers::OnSetWorldBounds(const proto::SetWorldBounds& p)
{
	spdlog::debug("ScriptRPC: ScrSetWorldBounds");

	pNetGame->m_netSet->worldBounds[0] = p.bounds[0];
	pNetGame->m_netSet->worldBounds[1] = p.bounds[1];
	pNetGame->m_netSet->worldBounds[2] = p.bounds[2];
	pNetGame->m_netSet->worldBounds[3] = p.bounds[3];
}

void RpcHandlers::OnGivePlayerMoney(const proto::GivePlayerMoney& p)
{
	spdlog::debug("ScriptRPC: ScrGivePlayerMoney");
	pGame->AddToLocalMoney(p.money);
}

void RpcHandlers::OnSetPlayerFacingAngle(const proto::SetPlayerFacingAngle& p)
{
	spdlog::debug("ScriptRPC: ScrSetPlayerFacingAngle");
	if (CPlayerPed* pPed = pGame ? pGame->FindPlayerPed() : nullptr) {
		pPed->SetTargetRotation(p.angle);
	}
}

void RpcHandlers::OnResetPlayerMoney(const proto::ResetPlayerMoney&)
{
	spdlog::debug("ScriptRPC: ScrResetPlayerMoney");
	pGame->ResetLocalMoney();
}

void RpcHandlers::OnResetPlayerWeapons(const proto::ResetPlayerWeapons&)
{
	spdlog::debug("ScriptRPC: ScrResetPlayerWeapons");
	if (pNetGame->GetPlayerPool() && pNetGame->GetPlayerPool()->GetLocalPlayer()
			&& pNetGame->GetPlayerPool()->GetLocalPlayer()->GetPlayerPed()) {
		pNetGame->GetPlayerPool()->GetLocalPlayer()->GetPlayerPed()->ClearWeapons();
	}
}

void RpcHandlers::OnSetPlayerArmour(const proto::SetPlayerArmour& p)
{
	spdlog::debug("ScriptRPC: ScrSetPlayerArmour");

	CLocalPlayer* pLocalPlayer = pNetGame->GetPlayerPool() ? pNetGame->GetPlayerPool()->GetLocalPlayer() : nullptr;
	if (pLocalPlayer && pLocalPlayer->GetPlayerPed()) {
		pLocalPlayer->GetPlayerPed()->SetArmour(p.armour);
	}
}

void RpcHandlers::OnSetArmedWeapon(const proto::SetArmedWeapon& p)
{
	spdlog::debug("ScriptRPC: ScrSetArmedWeapon");
	if (p.weapon <= 46) {
		CPlayerPed* pPlayerPed = pGame->FindPlayerPed();
		if (pPlayerPed) {
			pPlayerPed->SetArmedWeapon(p.weapon);
		}
	}
}

void RpcHandlers::OnSetPlayerAttachedObject(const proto::SetPlayerAttachedObject& p)
{
	spdlog::debug("ScriptRPC: ScrSetPlayerAttachedObject");

	CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
	if (pPlayerPool) {
		CPlayerPed* pPlayerPed = nullptr;

		if (pPlayerPool->GetLocalPlayerID() == p.playerId) {
			if (pPlayerPool->GetLocalPlayer()) {
				pPlayerPed = pPlayerPool->GetLocalPlayer()->GetPlayerPed();
			}
		}
		else {
			CRemotePlayer* pRemotePlayer = pPlayerPool->GetAt(p.playerId);
			if (pRemotePlayer) {
				pPlayerPed = pRemotePlayer->GetPlayerPed();
			}
		}

		if (pPlayerPed) {
			if (p.create) {
				NEW_ATTACHED_OBJECT NewAttachedObject{};
				NewAttachedObject.iModel = p.modelId;
				NewAttachedObject.iBoneID = p.boneId;
				NewAttachedObject.vecOffset = sa::CVector(p.offset.x, p.offset.y, p.offset.z);
				NewAttachedObject.vecRot = sa::CVector(p.rotation.x, p.rotation.y, p.rotation.z);
				NewAttachedObject.vecScale = sa::CVector(p.scale.x, p.scale.y, p.scale.z);
				NewAttachedObject.dwMaterialColor1 = p.materialColor1;
				NewAttachedObject.dwMaterialColor2 = p.materialColor2;
				pPlayerPed->SetAttachedObject(p.index, &NewAttachedObject);
			}
			else {
				pPlayerPed->RemoveAttachedObject(p.index);
			}
		}
	}
}

void RpcHandlers::OnSetPlayerDrunkLevel(const proto::SetPlayerDrunkLevel& p)
{
	spdlog::debug("ScriptRPC: ScrSetPlayerDrunkLevel");

	CPlayerPed* pPlayerPed = pGame->FindPlayerPed();
	if (pPlayerPed) {
		pPlayerPed->SetDrunkLevel(p.level);
	}
}

void RpcHandlers::OnSetPlayerDrunkVisuals(const proto::SetPlayerDrunkVisuals& p)
{
	spdlog::debug("ScriptRPC: ScrSetPlayerDrunkVisuals");
	ScriptCommand(&set_player_drunk_visuals, 0, p.visuals);
}

void RpcHandlers::OnSetPlayerDrunkHandling(const proto::SetPlayerDrunkHandling& p)
{
	spdlog::debug("ScriptRPC: ScrSetPlayerDrunkHandling");
	ScriptCommand(&set_player_drunk_handling, 0, p.handling);
}

void RpcHandlers::OnDeathMessage(const proto::DeathMessage& p)
{
	spdlog::debug("ScriptRPC: ScrDeathMessage");

	std::string killername, playername;
	unsigned int killercolor = 0, playercolor = 0;

	CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
	if (pPlayerPool) {
		if (pPlayerPool->GetLocalPlayerID() == p.playerId) {
			playername = pPlayerPool->GetLocalPlayerName();
			playercolor = pPlayerPool->GetLocalPlayer()->GetPlayerColorAsRGBA();
		}
		else {
			if (pPlayerPool->GetSlotState(p.playerId)) {
				char* szName = pPlayerPool->GetPlayerName(p.playerId);
				if (szName) playername = szName;
				CRemotePlayer* pRemotePlayer = pPlayerPool->GetAt(p.playerId);
				if (pRemotePlayer) playercolor = pRemotePlayer->GetPlayerColor();
			}
		}

		if (pPlayerPool->GetLocalPlayerID() == p.killerId) {
			killername = pPlayerPool->GetLocalPlayerName();
			killercolor = pPlayerPool->GetLocalPlayer()->GetPlayerColorAsRGBA();
		}
		else {
			if (pPlayerPool->GetSlotState(p.killerId)) {
				char* szName = pPlayerPool->GetPlayerName(p.killerId);
				if (szName) killername = szName;
				CRemotePlayer* pRemotePlayer = pPlayerPool->GetAt(p.killerId);
				if (pRemotePlayer) killercolor = pRemotePlayer->GetPlayerColor();
			}
		}
	}

	if (pUI) {
		pUI->deathwindow()->record(playername.c_str(), playercolor, killername.c_str(), killercolor, p.reason);
	}
}

void RpcHandlers::OnSetPlayerShopName(const proto::SetPlayerShopName& p)
{
	spdlog::debug("ScriptRPC: ScrSetPlayerShopName '{}'", p.shopName);
	if (pGame) {
		pGame->LoadShoppingDataSubsection(p.shopName.c_str());
	}
}

void RpcHandlers::OnPlayCrimeReport(const proto::PlayCrimeReport& p)
{
	spdlog::debug("ScriptRPC: ScrPlayCrimeReport suspect={} crime={} inVeh={} model={}",
			p.suspectId, p.crimeId, p.inVehicle, p.vehicleModel);
	if (pGame) {
		sa::CVector pos{p.position.x, p.position.y, p.position.z};
		pGame->PlayCrimeReport(p.crimeId, pos);
	}
}
