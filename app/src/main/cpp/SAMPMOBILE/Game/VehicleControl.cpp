#include "VehicleControl.h"
#include "Camera.h"
#include "Common.h"
#include "Game.h"
#include "Pad.h"
#include "PadInternal.h"
#include "PlayerPed.h"
#include "PlayerContextScope.h"
#include "Util.h"
#include "Vehicle.h"
#include "Engine/PatchRegistry.h"
#include "Engine/Symbols.h"
#include "../Addr.h"
#include "../Net/NetGame.h"
#include "../Net/LocalPlayer.h"
#include "../Net/RemotePlayer.h"
#include "../Net/VehiclePool.h"
#include "../Client.h"
#include <Memory.h>
#include <SA.h>
#include <spdlog/spdlog.h>
#include <atomic>

static inline void ApplyVehicleTyreBurstFix(sa::CVehicle* pVehicle)
{
	if (pVehicle->pDriver) {
		if (pVehicle->m_nVehicleFlags.bTyresDontBurst) {
			pVehicle->m_nVehicleFlags.bTyresDontBurst = 0;
		}
		if (!pVehicle->m_nVehicleFlags.bCanBeDamaged) {
			pVehicle->m_nVehicleFlags.bCanBeDamaged = true;
		}
	}
	else {
		if (!pVehicle->m_nVehicleFlags.bTyresDontBurst) {
			pVehicle->m_nVehicleFlags.bTyresDontBurst = 1;
		}
		if (pVehicle->m_nVehicleFlags.bCanBeDamaged) {
			pVehicle->m_nVehicleFlags.bCanBeDamaged = false;
		}
	}
}

template <bool CallAudioService, typename OrigFn>
static void ProcessRemoteVehicleControl(OrigFn orig, sa::CVehicle* pVehicle)
{
	const uint8_t prevCurPlayer = byteCurPlayer;
	byteInternalPlayer = CurrentPlayerSlot();

	CVehicle* pSampVeh = nullptr;
	if (pVehicle && pNetGame && pNetGame->GetVehiclePool()) {
		VEHICLEID vid = pNetGame->GetVehiclePool()->FindIDFromGtaPtr(pVehicle);
		if (vid != INVALID_VEHICLE_ID) {
			pSampVeh = pNetGame->GetVehiclePool()->GetAt(vid);
		}
	}

	const bool bManualEngineOff = pSampVeh && (
		pSampVeh->m_iEngineState == 0 ||
		(pNetGame && pNetGame->m_netSet && pNetGame->m_netSet->manualVehicleEngineAndLight && pSampVeh->m_iEngineState < 0));
	const bool bIsHeli = pSampVeh && (pSampVeh->GetVehicleSubtype() == VEHICLE_SUBTYPE_HELI);

	const uint8_t curPlayer = (pVehicle && pVehicle->pDriver && pVehicle->pDriver->m_nPedType == 0
		&& pVehicle->pDriver != GamePool_FindPlayerPed() && byteInternalPlayer == 0)
		? FindPlayerNumFromPedPtr(pVehicle->pDriver)
		: 0;

	if (curPlayer != 0) {
		byteCurPlayer = curPlayer;

		ScopedPlayerContext camAimCtx(curPlayer, ScopedPlayerContext::kVehicleCamAim);

		// Tyre burst fix
		ApplyVehicleTyreBurstFix(pVehicle);

		pVehicle->pDriver->m_nPedType = sa::PEDTYPE_CIVMALE; // So CPed::IsPlayer returns FALSE
		const uint8_t savedControlFlags = pVehicle->nControlFlags;
		pVehicle->nControlFlags = 26;

		if constexpr (CallAudioService) {
			eng::fn::CAEVehicleAudioEntity_Service(&pVehicle->m_VehicleAudioEntity);
		}

		pVehicle->nControlFlags = savedControlFlags;
		pVehicle->pDriver->m_nPedType = sa::PEDTYPE_PLAYER1;

		// Preserve Z position & velocity for boats/aircraft and distant non-train vehicles
		// so unstreamed collision geometry cannot drop remote vehicles through the map.
		const float savedZ = pVehicle->GetMatrix().m_pos.z;
		const float savedVelZ = pVehicle->GetMoveSpeed().z;
		bool bLockZ = false;
		if (pSampVeh) {
			const uint8_t sub = pSampVeh->GetVehicleSubtype();
			bLockZ = (sub == VEHICLE_SUBTYPE_BOAT || sub == VEHICLE_SUBTYPE_PLANE || sub == VEHICLE_SUBTYPE_HELI)
				|| (!pSampVeh->IsATrainPart() && pSampVeh->GetDistanceFromLocalPlayerPed() > 160.0f);
		}

		if (bManualEngineOff) {
			pVehicle->m_nVehicleFlags.bEngineOn = 0;
		}
		if (bIsHeli) {
			if (!pVehicle->m_nVehicleFlags.bEngineOn) pVehicle->nControlFlags |= 0x20;
			else pVehicle->nControlFlags &= ~0x20;
		}
		{
			ScopedPlayerContext focusCtx(curPlayer, ScopedPlayerContext::kFocusOnly);
			orig(pVehicle);
		}
		if (bManualEngineOff) {
			pVehicle->m_nVehicleFlags.bEngineOn = 0;
		}
		if (bIsHeli) {
			if (!pVehicle->m_nVehicleFlags.bEngineOn) pVehicle->nControlFlags |= 0x20;
			else pVehicle->nControlFlags &= ~0x20;
		}
		if (bLockZ) {
			sa::CMatrix mat = pVehicle->GetMatrix();
			mat.m_pos.z = savedZ;
			pVehicle->SetMatrix(mat);
			sa::CVector vel = pVehicle->GetMoveSpeed();
			vel.z = savedVelZ;
			pVehicle->SetVelocity(vel);
		}
	}
	else {
		if (pVehicle && !pVehicle->pDriver && !pVehicle->m_pTowingVehicle
			&& pSampVeh && !pSampVeh->IsATrainPart()
			&& pSampVeh->GetDistanceFromLocalPlayerPed() > 160.0f) {
			// Skip physics for distant unmanned vehicles outside loaded collision sector
			byteCurPlayer = prevCurPlayer;
			return;
		}

		if (pVehicle && pVehicle->pDriver && pVehicle->pDriver->m_nPedType == 0
			&& GamePool_FindPlayerPed() == pVehicle->pDriver) {
			ApplyVehicleTyreBurstFix(pVehicle);
		}

		if (bManualEngineOff && pVehicle) {
			pVehicle->m_nVehicleFlags.bEngineOn = 0;
		}
		if (bIsHeli && pVehicle) {
			if (!pVehicle->m_nVehicleFlags.bEngineOn) pVehicle->nControlFlags |= 0x20;
			else pVehicle->nControlFlags &= ~0x20;
		}

		if constexpr (CallAudioService) {
			eng::fn::CAEVehicleAudioEntity_Service(&pVehicle->m_VehicleAudioEntity);
		}

		orig(pVehicle);

		if (bManualEngineOff && pVehicle) {
			pVehicle->m_nVehicleFlags.bEngineOn = 0;
		}
		if (bIsHeli && pVehicle) {
			if (!pVehicle->m_nVehicleFlags.bEngineOn) pVehicle->nControlFlags |= 0x20;
			else pVehicle->nControlFlags &= ~0x20;
		}
	}

	byteCurPlayer = prevCurPlayer;
}

DECL_HOOK(void, CAutomobile_ProcessControl, sa::CVehicle* _this)   { ProcessRemoteVehicleControl<true>(CAutomobile_ProcessControl, _this); }
DECL_HOOK(void, CBoat_ProcessControl, sa::CVehicle* _this)         { ProcessRemoteVehicleControl<true>(CBoat_ProcessControl, _this); }
DECL_HOOK(void, CBike_ProcessControl, sa::CVehicle* _this)
{
	CVehicle* pSampVeh = nullptr;
	if (pNetGame && pNetGame->GetVehiclePool()) {
		VEHICLEID vid = pNetGame->GetVehiclePool()->FindIDFromGtaPtr(_this);
		if (vid != INVALID_VEHICLE_ID) {
			pSampVeh = pNetGame->GetVehiclePool()->GetAt(vid);
		}
	}

	ProcessRemoteVehicleControl<true>(CBike_ProcessControl, _this);

	// T20: hold remote bike lean angle per-frame so CBike::ProcessControl does not
	// decay RideAnimData.m_fLeanAngle back toward 0 between sync packets.
	if (pSampVeh && pSampVeh->VerifyInstance()) {
		if (pSampVeh->IsDriverLocalPlayer()) {
			pSampVeh->m_bHasRemoteBikeLean = false;
			pSampVeh->m_fRemoteBikeLean = 0.0f;
		}
		else if (pSampVeh->m_bHasRemoteBikeLean) {
			if (!pSampVeh->HasADriver()) {
				pSampVeh->m_bHasRemoteBikeLean = false;
				pSampVeh->m_fRemoteBikeLean = 0.0f;
			}
			else {
				pSampVeh->SetBikeLean(pSampVeh->m_fRemoteBikeLean);
			}
		}
	}
}
DECL_HOOK(void, CHeli_ProcessControl, sa::CVehicle* _this)         { ProcessRemoteVehicleControl<false>(CHeli_ProcessControl, _this); }
static std::atomic<bool> g_bPlaneGearToggleRequested{false};
void RequestLocalPlaneGearToggle()
{
	g_bPlaneGearToggleRequested.store(true, std::memory_order_release);
}

DECL_HOOK(void, CPlane_ProcessControl, sa::CVehicle* _this)
{
	CVehicle* pSampVeh = nullptr;
	if (pNetGame && pNetGame->GetVehiclePool()) {
		VEHICLEID vid = pNetGame->GetVehiclePool()->FindIDFromGtaPtr(_this);
		if (vid != INVALID_VEHICLE_ID) {
			pSampVeh = pNetGame->GetVehiclePool()->GetAt(vid);
		}
	}

	// Sample logical gear state BEFORE CPlane::ProcessControlInputs runs, because
	// GTA Mobile's 30m auto-gear mutates m_fLGearAngle mid-frame (e.g. 0.0 -> 0.02).
	uint8_t prevLocalGear = 0;
	if (pSampVeh && pSampVeh->VerifyInstance() && pSampVeh->IsDriverLocalPlayer()) {
		prevLocalGear = (pSampVeh->m_iLocalLandingGear >= 0)
				? static_cast<uint8_t>(pSampVeh->m_iLocalLandingGear)
				: pSampVeh->GetLandingGearState();
	}

	ProcessRemoteVehicleControl<false>(CPlane_ProcessControl, _this);

	if (pSampVeh && pSampVeh->VerifyInstance()) {
		auto* pPlane = reinterpret_cast<sa::CPlane*>(_this);
		if (pSampVeh->IsDriverLocalPlayer()) {
			pSampVeh->m_iRemoteLandingGear = -1;
			if (g_bPlaneGearToggleRequested.exchange(false, std::memory_order_acq_rel)) {
				const uint8_t next = (prevLocalGear == 0) ? 1 : 0;
				pSampVeh->m_iLocalLandingGear = static_cast<int8_t>(next);
				pSampVeh->SetLandingGearState(next);
			}
			else if (pSampVeh->m_iLocalLandingGear >= 0) {
				// Preserve manual gear choice against GTA Mobile's 30m auto-gear logic in CPlane::ProcessControlInputs.
				const float target = (pSampVeh->m_iLocalLandingGear == 1) ? 1.0f : 0.0f;
				if (pPlane->m_fLGearAngle != target) {
					pSampVeh->SetLandingGearState(static_cast<uint8_t>(pSampVeh->m_iLocalLandingGear));
				}
			}
		}
		else {
			pSampVeh->m_iLocalLandingGear = -1;
			if (pSampVeh->m_iRemoteLandingGear >= 0) {
				if (!pSampVeh->HasADriver()) {
					pSampVeh->m_iRemoteLandingGear = -1;
				}
				else {
					const float target = (pSampVeh->m_iRemoteLandingGear == 1) ? 1.0f : 0.0f;
					if (pPlane->m_fLGearAngle != target) {
						pSampVeh->SetLandingGearState(static_cast<uint8_t>(pSampVeh->m_iRemoteLandingGear));
					}
				}
			}
		}
	}
}
DECL_HOOK(void, CBmx_ProcessControl, sa::CVehicle* _this)          { ProcessRemoteVehicleControl<false>(CBmx_ProcessControl, _this); }
DECL_HOOK(void, CMonsterTruck_ProcessControl, sa::CVehicle* _this) { ProcessRemoteVehicleControl<false>(CMonsterTruck_ProcessControl, _this); }
DECL_HOOK(void, CQuadBike_ProcessControl, sa::CVehicle* _this)     { ProcessRemoteVehicleControl<false>(CQuadBike_ProcessControl, _this); }
DECL_HOOK(void, CTrain_ProcessControl, sa::CVehicle* _this)        { ProcessRemoteVehicleControl<true>(CTrain_ProcessControl, _this); }
DECL_HOOK(void, CTrailer_ProcessControl, sa::CVehicle* _this)      { ProcessRemoteVehicleControl<false>(CTrailer_ProcessControl, _this); }

bool NotifyEnterVehicle(sa::CVehicle* _pVehicle)
{
	spdlog::info("NotifyEnterVehicle: 0x{:X}", reinterpret_cast<uintptr_t>(_pVehicle));

	if (!pNetGame || !_pVehicle) {
		return false;
	}

	VEHICLEID vehicleId = INVALID_VEHICLE_ID;

	CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
	if (!pVehiclePool) {
		return false;
	}

	vehicleId = pVehiclePool->FindIDFromGtaPtr(_pVehicle);
	if (vehicleId == INVALID_VEHICLE_ID || !pVehiclePool->GetSlotState(vehicleId)) {
		return false;
	}

	CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
	CLocalPlayer* pLocalPlayer = pPlayerPool ? pPlayerPool->GetLocalPlayer() : nullptr;
	if (!pLocalPlayer || !pLocalPlayer->GetPlayerPed()) {
		return false;
	}

	if (pLocalPlayer->GetPlayerPed()->IsCuffed() || pLocalPlayer->GetPlayerPed()->IsCarry()) {
		return false;
	}

	CVehicle* pVehicle = pVehiclePool->GetAt(vehicleId);
	if (pVehicle && pVehicle->m_pVehicle) {
		if (pVehicle->GetDoorState()) {
			return false;
		}

		if (pVehicle->m_pVehicle->m_nModelIndex == TRAIN_PASSENGER) {
			return false;
		}

		if (pVehicle->m_pVehicle->pDriver) {
			if (pVehicle->m_pVehicle->pDriver->m_nPedType != 0) {
				return false;
			}
			PLAYERID driverId = pPlayerPool->FindRemotePlayerIDFromGtaPtr(pVehicle->m_pVehicle->pDriver);
			if (driverId != INVALID_PLAYER_ID) {
				CRemotePlayer* pDriver = pPlayerPool->GetAt(driverId);
				if (pDriver && pDriver->IsNPC()) {
					return false;
				}
			}
		}
	}

	if (pLocalPlayer->GetPlayerPed()->GetCurrentWeapon() == WEAPON_PARACHUTE) {
		pLocalPlayer->GetPlayerPed()->SetArmedWeapon(0);
	}

	pLocalPlayer->SendEnterVehicleNotification(vehicleId, false);
	return true;
}

DECL_HOOK(void, CTaskComplexEnterCarAsDriver, uintptr_t** _this, sa::CVehicle* pVehicle)
{
	if (!NotifyEnterVehicle(pVehicle)) {
		g_bCancelCarTask = true;
	}
	// Must always invoke constructor because CPlayerInfo::Process immediately passes _this to SetTask
	CTaskComplexEnterCarAsDriver(_this, pVehicle);
}

DECL_HOOK(void, CTaskComplexLeaveCar, uintptr_t** _this, sa::CVehicle* pVehicle, int iTargetDoor, int iDelayTime, bool bSensibleLeaveCar, bool bForceGetOut)
{
	if (pNetGame && pVehicle) {
		sa::CPed* pLocalPed = GamePool_FindPlayerPed();
		if (pLocalPed && pLocalPed->pVehicle == pVehicle) {
			CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
			CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
			if (pVehiclePool && pPlayerPool) {
				VEHICLEID VehicleID = pVehiclePool->FindIDFromGtaPtr(pLocalPed->pVehicle);
				if (VehicleID != INVALID_VEHICLE_ID) {
					CVehicle* pVeh = pVehiclePool->GetAt(VehicleID);
					CLocalPlayer* pLocalPlayer = pPlayerPool->GetLocalPlayer();
					if (pVeh && pVeh->m_pVehicle && pLocalPlayer) {
						pLocalPlayer->SendExitVehicleNotification(VehicleID);
						if (pVeh->IsATrainPart()) {
							RwMatrix mat = pVeh->m_pVehicle->GetMatrix().ToRwMatrix();
							if (pLocalPlayer->GetPlayerPed()) {
								pLocalPlayer->GetPlayerPed()->RemoveFromVehicleAndPutAt(mat.pos.x + 2.5f, mat.pos.y + 2.5f, mat.pos.z);
							}
							if (pGame && pGame->GetCamera()) {
								pGame->GetCamera()->SetBehindPlayer();
							}
							pLocalPlayer->m_LastVehicle = VehicleID;
							g_bCancelCarTask = true;
						}
					}
				}
			}
		}
	}

	CTaskComplexLeaveCar(_this, pVehicle, iTargetDoor, iDelayTime, bSensibleLeaveCar, bForceGetOut);
}

void InstallVehicleControlHooks()
{
	REG_HOOK(Boot, CAutomobile_ProcessControl, CAutomobile_ProcessControl, "Remote automobile control context switch");
	REG_HOOK(Boot, CBoat_ProcessControl, CBoat_ProcessControl, "Remote boat control context switch");
	REG_HOOK(Boot, CBike_ProcessControl, CBike_ProcessControl, "Remote bike control context switch");
	REG_HOOK(Boot, CPlane_ProcessControl, CPlane_ProcessControl, "Remote plane control context switch");
	REG_HOOK(Boot, CHeli_ProcessControl, CHeli_ProcessControl, "Remote heli control context switch");
	REG_HOOK(Boot, CBmx_ProcessControl, CBmx_ProcessControl, "Remote bmx control context switch");
	REG_HOOK(Boot, CMonsterTruck_ProcessControl, CMonsterTruck_ProcessControl, "Remote monster truck control context switch");
	REG_HOOK(Boot, CQuadBike_ProcessControl, CQuadBike_ProcessControl, "Remote quad bike control context switch");
	REG_HOOK(Boot, CTrain_ProcessControl, CTrain_ProcessControl, "Remote train control context switch");
	REG_HOOK(Boot, CTrailer_ProcessControl, CTrailer_ProcessControl, "Remote trailer control context switch");

	REG_HOOK_BL(Boot, addr::CTaskComplexEnterCarAsDriver_Branch1, "CTaskComplexEnterCarAsDriver.Branch1",
		&CTaskComplexEnterCarAsDriver_hook, &CTaskComplexEnterCarAsDriver, "Notify server on vehicle enter");
	REG_HOOK_BL(Boot, addr::CTaskComplexLeaveCar_Branch1, "CTaskComplexLeaveCar.Branch1",
		&CTaskComplexLeaveCar_hook, &CTaskComplexLeaveCar, "Notify server on vehicle exit (branch 1)");
	REG_HOOK_BL(Boot, addr::CTaskComplexLeaveCar_Branch2, "CTaskComplexLeaveCar.Branch2",
		&CTaskComplexLeaveCar_hook, &CTaskComplexLeaveCar, "Notify server on vehicle exit (branch 2)");
}
