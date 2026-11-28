#include "LocalPlayer.h"
#include "NetGame.h"
#include "RemotePlayer.h"
#include "VehiclePool.h"
#include "ActorPool.h"
#include "Proto/RpcOutbound.h"
#include "Proto/Sync.h"
#include "../Client.h"
#include "../Settings.h"
#include "../Game/Game.h"
#include "../Game/Animation.h"
#include "../Game/Camera.h"
#include "../Game/Common.h"
#include "../Game/Pad.h"
#include "../Game/PlayerPed.h"
#include "../Game/RadarColors.h"
#include "../Game/Vehicle.h"
#include "../Game/Util.h"
#include "Enums/WidgetIDs.h"
#include "../UI/UI.h"
#include "../UI/SAMPWidgets/Spawn.h"
#include "../Java/EditObject.h"
#include <RakNet/RakClientInterface.h>
#include <RakNet/PacketEnumerations.h>
#include <RakNet/SAMP/SAMPRPC.h>
#include <spdlog/spdlog.h>


uint32_t dwEnterVehTimeElasped = -1;
bool bFirstSpawn = true;
// bool g_bLockEnterVehicleWidget = false;

CLocalPlayer::CLocalPlayer()
{
	m_bInRCMode = false;

	m_pPlayerPed = pGame->FindPlayerPed();
	m_bIsActive = false;
	m_bIsWasted = false;

	m_iDisplayZoneTick = 0;
	m_dwLastSendTick = GetTickCount();
	m_dwLastSendSpecTick = GetTickCount();
	m_dwLastInCarSyncTick = GetTickCount();
	m_dwLastPassengerSyncTick = GetTickCount();
	m_dwLastTrailerSyncTick = GetTickCount();
	m_dwLastUnoccupiedSyncTick = GetTickCount();
	m_dwLastOnFootSyncTick = GetTickCount();
	m_dwLastAimDataSyncTick = GetTickCount();
	m_dwLastSendAimSyncTick = GetTickCount();
	m_dwLastStatsUpdateTick = GetTickCount();
	m_bIsSpectating = false;
	m_byteSpectateType = SPECTATE_TYPE_NONE;
	m_SpectateID = 0xFFFFFFFF;
	m_bSpawnDialogShowed = false;
	ResetAllSyncAttributes();

	m_statsSync.dwLastMoney = 0;
	m_statsSync.dwLastDrunkLevel = 0;

	m_surfSync.bIsActive = false;
	m_surfSync.pSurfInst = 0;
	m_surfSync.bIsVehicle = false;
	m_surfSync.dwSurfVehID = INVALID_VEHICLE_ID;
	m_surfSync.vecOffsetPos = sa::CVector{0.0f, 0.0f, 0.0f};

	m_DamageVehicle = INVALID_VEHICLE_ID;
	m_dwDamagePanels = 0;
	m_dwDamageDoors = 0;
	m_byteDamageLights = 0;
	m_byteDamageTires = 0;

	for (int i = 0; i < 13; i++) {
		m_byteLastWeapon[i] = 0;
		m_dwLastAmmo[i] = 0;
	}

	m_byteTeam = NO_TEAM;
}

CLocalPlayer::~CLocalPlayer()
{
	// ~
}

void ApplyEffectiveLocalOnFootKeys(CPlayerPed* pPlayerPed, uint16_t& wKeys, uint16_t& lrAnalog, uint16_t& udAnalog)
{
	if (!pPlayerPed || !pPlayerPed->m_ped || pPlayerPed->IsDead() || pPlayerPed->IsInVehicle()) {
		return;
	}
	ApplyLocalCombatSyncKeys(wKeys, lrAnalog, udAnalog);

	sa::CCamera* pTheCam = &sa::getTheCamera();
	const bool bAimingNow =
		(pPlayerPed->m_ped->m_pPlayerData && pPlayerPed->m_ped->m_pPlayerData->m_bFreeAiming)
		|| eng::fn::CCamera_Using1stPersonWeaponMode(pTheCam)
		|| eng::fn::CCamera_IsTargetingActive(pTheCam);
	if (bAimingNow) {
		wKeys |= 128u; // KEY_HANDBRAKE
	}
	// Match PC playerped.cpp:1577-1582: suppress KEY_FIRE when total ammo == 0 (weapons > 15, except parachute 46)
	if ((wKeys & 4u) != 0) {
		sa::CWeapon* pSlot = pPlayerPed->GetCurrentWeaponSlot();
		if (pSlot && pSlot->dwType > 15 && pSlot->dwType != 46 && pSlot->dwAmmo == 0) {
			wKeys &= static_cast<uint16_t>(~4u);
		}
	}
}

bool CLocalPlayer::Process()
{
	uint32_t dwThisTick;

	if (Game::IsGamePaused()) {
		// Suppress outbound player sync while pause menu is open so server/remotes detect AFK (PC parity)
		return true;
	}

	if (m_bIsActive && m_pPlayerPed != nullptr) {
		// local player is dead
		if (!m_bIsWasted && (m_pPlayerPed->GetActionTrigger() == ACTION_DEATH || m_pPlayerPed->IsDead())) {
			ToggleSpectating(false);

			if (m_pPlayerPed->IsDancing()) {
				m_pPlayerPed->StopDancing();
			}

			if (m_pPlayerPed->IsCellphoneEnabled()) {
				m_pPlayerPed->ToggleCellphone(false);
			}

			if (m_pPlayerPed->IsPissing()) {
				m_pPlayerPed->TogglePissing(false);
			}

			if (m_pPlayerPed->IsCuffed()) {
				m_pPlayerPed->ToggleCuffed(false);
			}

			if (m_pPlayerPed->IsCarry()) {
				m_pPlayerPed->ToggleCarry(false);
			}

			if (m_pPlayerPed->IsInJetpack()) {
				m_pPlayerPed->StopJetpack();
			}

			if (m_pPlayerPed->GetStuff() != STUFF_TYPE_NONE) {
				m_pPlayerPed->DropStuff();
			}

			if (m_pPlayerPed->GetDrunkLevel() > 0) {
				m_pPlayerPed->SetDrunkLevel(0);
				m_pPlayerPed->DrunkProcess();
			}

			m_pPlayerPed->ExtinguishFire();

			// reset tasks/anims
			m_pPlayerPed->TogglePlayerControllable(true);

			if (m_bInRCMode) {
				m_bInRCMode = false;
				m_pPlayerPed->Add();
			}

			if (m_pPlayerPed->IsInVehicle() && !m_pPlayerPed->IsAPassenger()) {
				SendInCarFullSyncData(); // for explosion
				CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
				if (pVehiclePool) {
					m_LastVehicle = pVehiclePool->FindIDFromGtaPtr(m_pPlayerPed->GetGtaVehicle());
				}
			}

			m_pPlayerPed->ExtinguishFire();
			SendWastedNotification();
			m_bIsActive = false;
			m_bIsWasted = true;
			if (m_bWantsAnotherClass) {
				m_bHasSpawnInfo = false;
				m_bClearedToSpawn = false;
			}
			pGame->EnableZoneNames(false);
			return true;
		}

		uint16_t wKeys, lrAnalog, udAnalog;
		uint8_t extKeys;
		wKeys = m_pPlayerPed->GetKeys(&lrAnalog, &udAnalog, &extKeys, false);
		ApplyEffectiveLocalOnFootKeys(m_pPlayerPed, wKeys, lrAnalog, udAnalog);

		// HANDLE DANCING LOCAL PED
		if (m_pPlayerPed->IsDancing()) {
			m_pPlayerPed->ProcessDancing();
			if (LocalPlayerKeys.bKeys[ePadKeys::KEY_SECONDARY_ATTACK] || m_pPlayerPed->IsInVehicle()) {
				m_pPlayerPed->StopDancing();
			}
		}

		// HANDLE I GOT MY HANDS UP BUT DON'T WANT TO ANYMORE
		if (m_pPlayerPed->HasHandsUp()) {
			if (LocalPlayerKeys.bKeys[ePadKeys::KEY_SECONDARY_ATTACK] || m_pPlayerPed->IsInVehicle()) {
				m_pPlayerPed->TogglePlayerControllable(true);
			}
		}

		// HANDLE CELLPHONE LOCAL PED: PC keeps cellphone active in vehicles & across Enter

		// HANDLE PISSING LOCAL PED
		if (m_pPlayerPed->IsPissing()) {
			if (LocalPlayerKeys.bKeys[ePadKeys::KEY_SECONDARY_ATTACK] || m_pPlayerPed->IsInVehicle()) {
				m_pPlayerPed->TogglePissing(false);
			}
		}

		// HANDLE CUFF LOCAL PED (server-controlled only; enforce unarmed while cuffed on foot)
		if (m_pPlayerPed->IsCuffed()) {
			if (!m_pPlayerPed->IsInVehicle() && m_pPlayerPed->GetCurrentWeapon() != 0) {
				m_pPlayerPed->SetArmedWeapon(0);
			}
		}

		// HANDLE DRINKING / SMOKING LOCAL PED (T17)
		if (m_pPlayerPed->GetStuff() != STUFF_TYPE_NONE) {
			if (!m_pPlayerPed->IsInVehicle() && m_pPlayerPed->GetCurrentWeapon() != 0) {
				m_pPlayerPed->SetArmedWeapon(0);
			}
			if (LocalPlayerKeys.bKeys[ePadKeys::KEY_SECONDARY_ATTACK] || m_pPlayerPed->IsInVehicle()) {
				ConsumeStuffUseRequest();
				m_pPlayerPed->DropStuff();
			}
			else if (LocalPlayerKeys.bKeys[ePadKeys::KEY_FIRE] || ConsumeStuffUseRequest()) {
				m_pPlayerPed->ApplyStuff();
			}
		}
		else {
			ConsumeStuffUseRequest();
		}

		// HANDLE CARRY LOCAL PED (server-controlled only; enforce unarmed while carrying on foot)
		if (m_pPlayerPed->IsCarry()) {
			if (!m_pPlayerPed->IsInVehicle() && m_pPlayerPed->GetCurrentWeapon() != 0) {
				m_pPlayerPed->SetArmedWeapon(0);
			}
		}

		// HANDLE JETPACK LOCAL PED
		if (m_pPlayerPed->IsInJetpack()) {
			if (LocalPlayerKeys.bKeys[ePadKeys::KEY_SECONDARY_ATTACK] || m_pPlayerPed->IsInVehicle()) {
				m_pPlayerPed->StopJetpack();
			}
		}

		m_pPlayerPed->DrunkProcess();

		m_pPlayerPed->SatisfyHunger();

		dwThisTick = GetTickCount();

		if (dwEnterVehTimeElasped != -1 && (dwThisTick - dwEnterVehTimeElasped) > 5000 && !m_pPlayerPed->IsInVehicle()) {
			pGame->GetCamera()->SetBehindPlayer();
			dwEnterVehTimeElasped = -1;
		}

		if (dwThisTick >= m_iDisplayZoneTick) {
			pGame->EnableZoneNames(pNetGame->m_netSet->zoneNames);
		}

		pGame->UpdateCheckpoints();

		if ((dwThisTick - m_dwLastStatsUpdateTick) > 1000) {
			SendStatsUpdate();
			m_dwLastStatsUpdateTick = dwThisTick;
		}

		CheckWeapons();

		uint8_t byteInterior = pGame->GetActiveInterior();
		if (byteInterior != m_byteCurInterior) {
			UpdateRemoteInterior(byteInterior);
		}

		// T21: AllowInteriorWeapons(false) disarms the local player inside interiors
		if (pNetGame->m_netSet && !pNetGame->m_netSet->allowWeapons && byteInterior != 0) {
			if (m_pPlayerPed->GetCurrentWeapon() != 0) {
				m_pPlayerPed->SetArmedWeapon(0);
			}
		}

		UpdateCameraTarget();

		// Local head-turn look-at sync (runs every 500ms while on foot or in vehicle)
		if (m_pPlayerPed->IsAdded() && Settings::headMove()
				&& !m_bIsSpectating && !EditObject::m_visible
				&& m_pPlayerPed->GetStuff() == STUFF_TYPE_NONE
				&& (wKeys & 0x84) == 0
				&& m_pPlayerPed->IsEnteringVehicle() == 0 && !m_pPlayerPed->IsExitingVehicle()
				&& (dwThisTick - m_dwLastHeadUpdate) > 500) {
			CAMERA_AIM* Aim = GameGetInternalAim();
			if (Aim && m_pPlayerPed->m_ped) {
				RwMatrix matPed = m_pPlayerPed->m_ped->GetMatrix().ToRwMatrix();
				sa::CVector LookAt;
				LookAt.x = matPed.pos.x + (Aim->f1x * 20.0f);
				LookAt.y = matPed.pos.y + (Aim->f1y * 20.0f);
				LookAt.z = matPed.pos.z + (Aim->f1z * 20.0f);
				m_pPlayerPed->ApplyCommandTask("FollowPedSA", nullptr, 2000, -1, &LookAt, 0, 0.1f, 500, 3, 0);
				m_dwLastHeadUpdate = dwThisTick;
			}
		}

		// PLAYER DATA UPDATES
		if (m_bIsSpectating) {
			if (m_surfSync.bIsActive) DisableSurfing();
			ProcessSpectating();
			m_bPassengerDriveByMode = false;
		}
			// DRIVER CONDITIONS
		else if (m_pPlayerPed->GetActionTrigger() == ACTION_INCAR && !m_pPlayerPed->IsAPassenger()) {
			if (m_surfSync.bIsActive) DisableSurfing();
			// g_bLockEnterVehicleWidget = false;

			m_pPlayerPed->RemoveWeaponWhenEnteringVehicle();

			ProcessInCarWorldBounds();

			if ((dwThisTick - m_dwLastSendAimSyncTick) > 1000) {
				m_dwLastSendAimSyncTick = dwThisTick;
				SendAimSyncData();
			}

			CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
			CVehicle* pVehicle = nullptr;

			if (pVehiclePool) {
				m_CurrentVehicle = pVehiclePool->FindIDFromGtaPtr(m_pPlayerPed->GetGtaVehicle());
			}

			UpdateVehicleDamage(m_CurrentVehicle);

			if (pVehiclePool) {
				pVehicle = pVehiclePool->GetAt(m_CurrentVehicle);
			}
			if (pVehicle && !m_bInRCMode && pVehicle->IsRCVehicle()) {
				m_pPlayerPed->Remove();
				m_bInRCMode = true;
			}

			if (m_bInRCMode && !pVehicle) {
				m_pPlayerPed->SetHealth(0.0f);
				m_pPlayerPed->SetDead();
			}

			if (m_bInRCMode && pVehicle && pVehicle->GetHealth() == 0.0f) {
				m_pPlayerPed->SetHealth(0.0f);
				m_pPlayerPed->SetDead();
			}

			if (CompareInCarSyncKeys(wKeys, lrAnalog, udAnalog) || (dwThisTick - m_dwLastSendTick) > GetOptimumInCarSendRate()) {
				m_dwLastSendTick = GetTickCount();
				SendInCarFullSyncData();
			}

			m_bPassengerDriveByMode = false;
		}
			// ONFOOT CONDITIONS
		else if (m_pPlayerPed->GetActionTrigger() == ACTION_NORMAL || m_pPlayerPed->GetActionTrigger() == ACTION_SCOPE) {
			// g_bLockEnterVehicleWidget = true;

			ProcessSurfing();
			UpdateSurfing();

			if (m_bInRCMode) {
				m_bInRCMode = false;
				m_pPlayerPed->Add();
			}

			ProcessOnFootWorldBounds();

			if (m_CurrentVehicle != 0xFFFF) {
				m_LastVehicle = m_CurrentVehicle;
				m_CurrentVehicle = 0xFFFF;
			}

			if (CompareOnFootSyncKeys(wKeys, lrAnalog, udAnalog) || (dwThisTick - m_dwLastSendTick) > GetOptimumOnFootSendRate()) {
				m_dwLastSendTick = GetTickCount();
				SendOnFootFullSyncData();
			}

			// Broadcast camera mode transitions (4 <-> 53/7/8/51/46) immediately in lockstep
			// with OnFootSync KEY_HANDBRAKE so PC & Mobile observers never see un-aimed sliding.
			bool bWannaSync = (m_pPlayerPed->GetCameraMode() != m_aimSync.byteCamMode);
			if (!bWannaSync) {
				if (wKeys & 0x84) {
					if (pNetGame->session().lagCompensationMode == 2) {
						bWannaSync = (dwThisTick - m_dwLastSendAimSyncTick) > pNetGame->session().firingSendRate;
					}
					else {
						bWannaSync = (dwThisTick - m_dwLastSendAimSyncTick) > 100;
					}
				}
				else {
					bWannaSync = (dwThisTick - m_dwLastSendAimSyncTick) > 1000;
				}
			}

			if (bWannaSync) {
				m_dwLastSendAimSyncTick = dwThisTick;
				SendAimSyncData();
			}

			m_bPassengerDriveByMode = false;
		}
			// PASSENGER CONDITIONS
		else if (m_pPlayerPed->GetActionTrigger() == ACTION_INCAR && m_pPlayerPed->IsAPassenger()) {
			// g_bLockEnterVehicleWidget = false;

			if (m_bInRCMode) {
				m_bInRCMode = false;
				m_pPlayerPed->Add();
			}

			// FOR ENTERING PASSENGER DRIVEBY MODE
			const bool bToggleDriveBy = LocalPlayerKeys.bKeys[ePadKeys::KEY_CTRL_BACK] || ConsumePassengerDriveByToggle();
			if (!m_bPassengerDriveByMode && bToggleDriveBy) {
				if (m_pPlayerPed->StartPassengerDriveByMode()) {
					m_bPassengerDriveByMode = true;
				}
			}
			else if (m_bPassengerDriveByMode && bToggleDriveBy) {
				m_pPlayerPed->StopPassengerDriveByMode();
				m_bPassengerDriveByMode = false;
			}

			if (m_bPassengerDriveByMode) {
				sa::CWeapon* pWeapSlot = m_pPlayerPed->GetCurrentWeaponSlot();
				if (m_pPlayerPed->GetCurrentWeapon() == 0 || !pWeapSlot || pWeapSlot->dwAmmo == 0) {
					m_pPlayerPed->StopPassengerDriveByMode();
					m_bPassengerDriveByMode = false;
				}
			}

			if (dwThisTick - m_dwLastSendTick > GetOptimumInCarSendRate()) {
				m_dwLastSendTick = GetTickCount();
				SendPassengerFullSyncData();
			}
		}
	}

	// Matches PC localplayer.cpp: whenever inactive (spectating / wasted / class selection),
	// immediately reset drunk level & clear drunk visuals/HUD override.
	if (!m_bIsActive && m_pPlayerPed && m_pPlayerPed->GetDrunkLevel() > 0) {
		m_pPlayerPed->SetDrunkLevel(0);
		m_pPlayerPed->DrunkProcess();
	}

	// HANDLE !IsActive spectating
	if (m_bIsSpectating && !m_bIsActive) {
		if (m_bSpawnDialogShowed) {
			m_bSpawnDialogShowed = false;
			if (pUI) {
				pUI->spawn()->setVisible(false);
			}
		}

		ProcessSpectating();
		return true;
	}

	// HANDLE NEEDS TO RESPAWN AFTER DEATH
	if (m_bIsWasted && m_pPlayerPed->GetActionTrigger() != ACTION_WASTED && m_pPlayerPed->GetActionTrigger() != ACTION_DEATH) {
		if (m_pPlayerPed->HasAttachedObject()) {
			m_pPlayerPed->RemoveAllAttachedObjects();
		}

		if (IsClearedToSpawn() && !m_bWantsAnotherClass && pNetGame->GetGameState() == GAMESTATE_CONNECTED) {
			if (m_pPlayerPed->GetHealth() > 0.0f) {
				Spawn();
			}
		}
		else {
			m_bIsWasted = false;
			m_bSpawnDialogShowed = false;
			HandleClassSelection();
			m_bWantsAnotherClass = false;
		}

		return true;
	}

	if (m_pPlayerPed->GetActionTrigger() != ACTION_WASTED && m_pPlayerPed->GetActionTrigger() != ACTION_DEATH && pNetGame->GetGameState() == GAMESTATE_CONNECTED && !m_bIsActive && !m_bIsSpectating) {
		ProcessClassSelection();
	}

	return true;
}

void CLocalPlayer::ProcessClassSelection()
{
	if (!m_bSpawnDialogShowed) {
		if (pUI) {
			pUI->spawn()->setVisible(true);
		}
		RequestClass(m_iSelectedClass);
		m_bSpawnDialogShowed = true;
	}
}

void CLocalPlayer::ResetAllSyncAttributes()
{
	m_bHasSpawnInfo = false;
	m_iCustomSpawnSkin = 0;
	m_bWantsAnotherClass = false;
	m_bWaitingForSpawnRequestReply = false;
	m_bSpawnDialogShowed = false;
	m_byteTeam = NO_TEAM;
	m_iSelectedClass = 0;
	m_byteCurInterior = 0;
	m_LastVehicle = 0xFFFF;
	m_bInRCMode = false;
	m_lastTargetPlayerId = INVALID_PLAYER_ID;
	m_lastTargetActorId = INVALID_PLAYER_ID;
	DisableSurfing();

	memset(&m_ofSync, 0, sizeof(ONFOOT_SYNC_DATA));
	memset(&m_icSync, 0, sizeof(INCAR_SYNC_DATA));
	memset(&m_psSync, 0, sizeof(PASSENGER_SYNC_DATA));
	memset(&m_trSync, 0, sizeof(TRAILER_SYNC_DATA));
	memset(&m_ucSync, 0, sizeof(UNOCCUPIED_SYNC_DATA));
	memset(&m_aimSync, 0, sizeof(AIM_SYNC_DATA));
	memset(&m_statsSync, 0, sizeof(m_statsSync));
	memset(m_byteLastWeapon, 0, sizeof(m_byteLastWeapon));
	memset(m_dwLastAmmo, 0, sizeof(m_dwLastAmmo));
	for (auto& target : m_wCameraTargets) {
		target = 0xFFFF;
	}

	m_dwLastWeaponsUpdateTick = GetTickCount();
	m_byteCurrentWeapon = 0;

	m_CurrentVehicle = INVALID_VEHICLE_ID;
	m_DamageVehicle = INVALID_VEHICLE_ID;
	m_dwDamagePanels = 0;
	m_dwDamageDoors = 0;
	m_byteDamageLights = 0;
	m_byteDamageTires = 0;
}

bool CLocalPlayer::Spawn()
{
	if (!m_bHasSpawnInfo) {
		return false;
	}

	if (m_bSpawnDialogShowed) {
		m_bSpawnDialogShowed = false;
		if (pUI) {
			pUI->spawn()->setVisible(false);
		}
	}

	spdlog::info("Spawn localplayer");
	DisableSurfing();

	uintptr_t* widgets = GetTouchWidgets();
	if (!widgets || !widgets[sa::WidgetIDs::WIDGET_PED_MOVE] || !widgets[sa::WidgetIDs::WIDGET_ATTACK]) {
		spdlog::info("Widgets not inited, calling Menu_SwitchOffToGame");
		eng::fn::Menu_SwitchOffToGame();
	}
	EnsureTouchWidgetsCreated();

	pGame->GetCamera()->Restore();
	pGame->GetCamera()->SetBehindPlayer();
	Settings::applyToGtaEngine(true);
	pGame->SetHudHidden(Game::HUD_HIDE_CLASS_SELECT | Game::HUD_HIDE_SPECTATE, false);
	pGame->DisplayHUD(true);
	m_pPlayerPed->TogglePlayerControllable(true);

	if (!bFirstSpawn) {
		if (m_pPlayerPed->IsInVehicle()) {
			m_pPlayerPed->RemoveFromVehicleAndPutAt(
				m_SpawnInfo.vecPos.x, m_SpawnInfo.vecPos.y, m_SpawnInfo.vecPos.z + 0.5f);
		}
		m_pPlayerPed->SetInitialState();
	}
	else {
		bFirstSpawn = false;
	}

	pGame->RefreshStreamingAt(m_SpawnInfo.vecPos.x, m_SpawnInfo.vecPos.y);

	if (m_pPlayerPed->IsCuffed()) {
		m_pPlayerPed->ToggleCuffed(false);
	}

	m_pPlayerPed->RestartIfWastedAt(&m_SpawnInfo.vecPos, m_SpawnInfo.fRotation);
	m_pPlayerPed->SetSkin(m_SpawnInfo.iSkin, m_iCustomSpawnSkin);
	m_pPlayerPed->ClearWeapons();
	m_pPlayerPed->ResetDamageEntity();

	ApplySpecialAction(0);

	if (m_SpawnInfo.iSpawnWeapons[2] != -1) {
		m_pPlayerPed->GiveWeapon(m_SpawnInfo.iSpawnWeapons[2], m_SpawnInfo.iSpawnWeaponsAmmo[2]);
	}

	if (m_SpawnInfo.iSpawnWeapons[1] != -1) {
		m_pPlayerPed->GiveWeapon(m_SpawnInfo.iSpawnWeapons[1], m_SpawnInfo.iSpawnWeaponsAmmo[1]);
	}

	if (m_SpawnInfo.iSpawnWeapons[0] != -1) {
		m_pPlayerPed->GiveWeapon(m_SpawnInfo.iSpawnWeapons[0], m_SpawnInfo.iSpawnWeaponsAmmo[0]);
	}

	pGame->DisableTrainTraffic();

	m_pPlayerPed->TeleportTo(m_SpawnInfo.vecPos.x, m_SpawnInfo.vecPos.y, m_SpawnInfo.vecPos.z + 0.5f);
	m_pPlayerPed->SetTargetRotation(m_SpawnInfo.fRotation);

	m_bIsWasted = false;
	m_bIsActive = true;
	m_bWaitingForSpawnRequestReply = false;

	m_iDisplayZoneTick = GetTickCount() + 1000;

	m_surfSync.bIsActive = false;
	ResetLocalPedQueuedAnim();

	RakNet::BitStream bsSendSpawn;
	proto::SpawnReq req{};
	proto::Writer w(bsSendSpawn);
	req.serialize(w, proto::ProtoVersion::V037);
	pNetGame->GetRakClient()->RPC(&RPC_Spawn, &bsSendSpawn, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false, UNASSIGNED_NETWORK_ID, nullptr);
	return true;
}

void CLocalPlayer::HandleClassSelection()
{
	m_bClearedToSpawn = false;
	if (pGame) {
		pGame->SetHudHidden(Game::HUD_HIDE_CLASS_SELECT, true);
	}
	if (m_pPlayerPed) {
		m_pPlayerPed->SetInitialState();
		m_pPlayerPed->SetHealth(100.0f);
		m_pPlayerPed->TogglePlayerControllable(false);
	}
}

void CLocalPlayer::UpdateRemoteInterior(uint8_t byteInterior)
{
	m_byteCurInterior = byteInterior;
	RakNet::BitStream bsUpdateInterior;
	proto::SetInteriorReq req{};
	req.interiorId = byteInterior;
	proto::Writer w(bsUpdateInterior);
	req.serialize(w, proto::ProtoVersion::V037);
	pNetGame->GetRakClient()->RPC(&RPC_SetInteriorId, &bsUpdateInterior, HIGH_PRIORITY, RELIABLE, 0, false, UNASSIGNED_NETWORK_ID, nullptr);
}

void CLocalPlayer::ProcessOnFootWorldBounds()
{
	if (pGame->GetActiveInterior() != 0) {
		return;
	} // can't enforce inside interior

	if (m_pPlayerPed->EnforceWorldBoundries(pNetGame->m_netSet->worldBounds[0], pNetGame->m_netSet->worldBounds[1], pNetGame->m_netSet->worldBounds[2], pNetGame->m_netSet->worldBounds[3])) {
		m_pPlayerPed->SetArmedWeapon(0);
		pGame->DisplayGameText("Stay within the ~r~world boundries", 1000, 5);
	}
}

void CLocalPlayer::ProcessInCarWorldBounds()
{
	CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
	if (pVehiclePool) {
		if (pGame->GetActiveInterior() != 0) {
			return;
		} // can't enforce inside interior

		VEHICLEID vehicleId = pVehiclePool->FindIDFromGtaPtr(m_pPlayerPed->GetGtaVehicle());
		if (vehicleId != INVALID_VEHICLE_ID) {
			CVehicle* pVehicle = pVehiclePool->GetAt(vehicleId);
			if (pVehicle) {
				if (pVehicle->EnforceWorldBoundries(
						pNetGame->m_netSet->worldBounds[0], pNetGame->m_netSet->worldBounds[1],
						pNetGame->m_netSet->worldBounds[2],
						pNetGame->m_netSet->worldBounds[3])) {
					pGame->DisplayGameText("Stay within the ~r~world boundries", 1000, 5);
				}
			}
		}
	}
}

void CLocalPlayer::SendNextClass()
{
	RwMatrix matPlayer;

	if (!m_bSpawnDialogShowed) {
		return;
	}

	m_bClearedToSpawn = false;
	matPlayer = m_pPlayerPed->m_ped->GetMatrix().ToRwMatrix();

	if (m_iSelectedClass == (pNetGame->m_netSet->spawnsAvailable - 1)) {
		m_iSelectedClass = 0;
	}
	else {
		m_iSelectedClass++;
	}

	pGame->PlaySound(1052, matPlayer.pos.x, matPlayer.pos.y, matPlayer.pos.z);
	RequestClass(m_iSelectedClass);
}

void CLocalPlayer::SendPrevClass()
{
	RwMatrix matPlayer;

	if (!m_bSpawnDialogShowed) {
		return;
	}

	m_bClearedToSpawn = false;
	matPlayer = m_pPlayerPed->m_ped->GetMatrix().ToRwMatrix();

	if (m_iSelectedClass == 0) {
		m_iSelectedClass = (pNetGame->m_netSet->spawnsAvailable - 1);
	}
	else {
		m_iSelectedClass--;
	}

	pGame->PlaySound(1053, matPlayer.pos.x, matPlayer.pos.y, matPlayer.pos.z);
	RequestClass(m_iSelectedClass);
}

void CLocalPlayer::SendSpawn()
{
	if (!m_bSpawnDialogShowed) {
		return;
	}

	RequestSpawn();
	m_bWaitingForSpawnRequestReply = true;
}

void CLocalPlayer::RequestClass(int iClass)
{
	RakNet::BitStream bsClassRequest;
	proto::RequestClassReq req{};
	req.classId = iClass;
	proto::Writer w(bsClassRequest);
	req.serialize(w, proto::ProtoVersion::V037);
	pNetGame->GetRakClient()->RPC(&RPC_RequestClass, &bsClassRequest, HIGH_PRIORITY, RELIABLE, 0, false, UNASSIGNED_NETWORK_ID, nullptr);
}

void CLocalPlayer::RequestSpawn()
{
	RakNet::BitStream bsSpawnRequest;
	proto::RequestSpawnReq req{};
	proto::Writer w(bsSpawnRequest);
	req.serialize(w, proto::ProtoVersion::V037);
	pNetGame->GetRakClient()->RPC(&RPC_RequestSpawn, &bsSpawnRequest, HIGH_PRIORITY, RELIABLE, 0, false, UNASSIGNED_NETWORK_ID, nullptr);
}

void CLocalPlayer::ApplySpecialAction(uint8_t byteSpecialAction)
{
	if (!m_pPlayerPed) {
		return;
	}

	switch (byteSpecialAction) {
		case SPECIAL_ACTION_NONE:
			if (m_pPlayerPed->IsDancing()) m_pPlayerPed->StopDancing();
			if (m_pPlayerPed->HasHandsUp()) m_pPlayerPed->TogglePlayerControllable(true);
			if (m_pPlayerPed->IsInJetpack()) m_pPlayerPed->StopJetpack();
			if (m_pPlayerPed->IsPissing()) m_pPlayerPed->TogglePissing(false);
			if (m_pPlayerPed->IsCuffed()) m_pPlayerPed->ToggleCuffed(false);
			if (m_pPlayerPed->IsCarry()) m_pPlayerPed->ToggleCarry(false);
			if (m_pPlayerPed->IsCellphoneEnabled()) m_pPlayerPed->ToggleCellphone(false);
			if (m_pPlayerPed->GetStuff() != STUFF_TYPE_NONE) m_pPlayerPed->DropStuff();
			break;
		case SPECIAL_ACTION_USEJETPACK:
			if (!m_pPlayerPed->IsInJetpack()) m_pPlayerPed->StartJetpack();
			break;
		case SPECIAL_ACTION_DANCE1:
			m_pPlayerPed->StartDancing(0);
			break;
		case SPECIAL_ACTION_DANCE2:
			m_pPlayerPed->StartDancing(1);
			break;
		case SPECIAL_ACTION_DANCE3:
			m_pPlayerPed->StartDancing(2);
			break;
		case SPECIAL_ACTION_DANCE4:
			m_pPlayerPed->StartDancing(3);
			break;
		case SPECIAL_ACTION_HANDSUP:
			if (!m_pPlayerPed->HasHandsUp()) m_pPlayerPed->HandsUp();
			break;
		case SPECIAL_ACTION_USECELLPHONE:
			if (!m_pPlayerPed->IsCellphoneEnabled()) m_pPlayerPed->ToggleCellphone(true);
			break;
		case SPECIAL_ACTION_STOPUSECELLPHONE:
			if (m_pPlayerPed->IsCellphoneEnabled()) m_pPlayerPed->ToggleCellphone(false);
			break;
		case SPECIAL_ACTION_DRINK_BEER:
			m_pPlayerPed->GiveStuff(STUFF_TYPE_BEER);
			break;
		case SPECIAL_ACTION_SMOKE_CIGGY:
			m_pPlayerPed->GiveStuff(STUFF_TYPE_CIGGI);
			break;
		case SPECIAL_ACTION_DRINK_WINE:
			m_pPlayerPed->GiveStuff(STUFF_TYPE_DYN_BEER);
			break;
		case SPECIAL_ACTION_DRINK_SPRUNK:
			m_pPlayerPed->GiveStuff(STUFF_TYPE_PINT_GLASS);
			break;
		case SPECIAL_ACTION_CUFFED:
			m_pPlayerPed->ToggleCuffed(true);
			break;
		case SPECIAL_ACTION_CARRY:
			m_pPlayerPed->ToggleCarry(true);
			break;
		case SPECIAL_ACTION_PISSING:
			m_pPlayerPed->TogglePissing(true);
			break;
		default:
			break;
	}
}

void CLocalPlayer::SetSpawnInfo(PLAYER_SPAWN_INFO* pSpawnInfo, int iCustomSkin)
{
	memcpy(&m_SpawnInfo, pSpawnInfo, sizeof(PLAYER_SPAWN_INFO));
	m_iCustomSpawnSkin = iCustomSkin;
	m_bHasSpawnInfo = true;
}

void CLocalPlayer::HandleClassSelectionOutcome(bool bOutcome)
{
	if (bOutcome) {
		if (m_pPlayerPed) {
			m_pPlayerPed->ClearWeapons();
			m_pPlayerPed->SetSkin(m_SpawnInfo.iSkin, m_iCustomSpawnSkin);
		}

		m_bClearedToSpawn = true;
	}
}

uint8_t CLocalPlayer::GetSpecialAction()
{
	if (m_pPlayerPed) {
		return m_pPlayerPed->GetSpecialAction();
	}

	return SPECIAL_ACTION_NONE;
}

uint32_t CLocalPlayer::GetPlayerColorAsARGB()
{
	return (TranslateColorCodeToRGBA(pNetGame->GetPlayerPool()->GetLocalPlayerID()) >> 8) | 0xFF000000;
}

uint32_t CLocalPlayer::GetPlayerColorAsRGBA()
{
	return TranslateColorCodeToRGBA(pNetGame->GetPlayerPool()->GetLocalPlayerID());
}

void CLocalPlayer::SetPlayerColor(uint32_t dwColor)
{
	if (pNetGame && pNetGame->GetPlayerPool()) {
		SetRadarColor(pNetGame->GetPlayerPool()->GetLocalPlayerID(), dwColor);
	}
}

