//
// Created by ALYN on 2024/8/15.
//

#include "Vehicle.h"
#include "Common.h"
#include "Game.h"
#include "MaterialTextGenerator.h"
#include "PlayerPed.h"
#include "Scripting.h"
#include "Util.h"
#include "Hooks/HookDomains.h"
#include "VehicleComponentTable.h"
#include "Engine/Symbols.h"
#include "../Client.h"
#include "../UI/UI.h"
#include "../UI/SAMPWidgets/Chat.h"
#include "../Net/NetGame.h"
#include "../Net/VehiclePool.h"
#include <SA.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>

void CVehicle::ApplyEngineStatus(bool state)
{
	if (m_pVehicle && VerifyInstance()) {
		m_pVehicle->SetEngineStatus(state);
	}
}

int CVehicle::GetEngineState()
{
	if (m_pVehicle && VerifyInstance()) {
		return m_pVehicle->GetEngineStatus();
	}
	return 0;
}

void CVehicle::SetSirenOn(uint8_t state)
{
	if (m_pVehicle && VerifyInstance()) {
		m_pVehicle->m_nVehicleFlags.bSirenOrAlarm = state;
	}
}

bool CVehicle::IsSirenOn()
{
	return m_pVehicle && VerifyInstance() && m_pVehicle->m_nVehicleFlags.bSirenOrAlarm;
}

void CVehicle::SetAlarmState(uint16_t wAlarmState)
{
	if (m_pVehicle && VerifyInstance()) {
		m_pVehicle->m_nAlarmState = wAlarmState;
	}
}

uint16_t CVehicle::GetAlarmState()
{
	return (m_pVehicle && VerifyInstance()) ? m_pVehicle->m_nAlarmState : 0;
}


static sa::CVehicle* pCreatedTrain = nullptr;

CVehicle::CVehicle(int iType, float fX, float fY, float fZ, float fRotation, bool bPreloaded, bool bSiren)
{
	spdlog::debug("CVehicle::CVehicle({}, {}, {}, {}, {}, {}, {})", iType, fX, fY, fZ, fRotation, bPreloaded, bSiren);
	uint32_t dwRetID = 0;
	RwMatrix mat;

	m_pVehicle = nullptr;
	m_gtaId = 0;
	m_pTrailer = nullptr;
	m_bPreloaded = bPreloaded;
	m_bIsInvulnerable = false;
	m_bIsLocked = false;
	m_iEngineState = -1;
	m_iLightState = -1;
	m_bDoorsLocked = false;
	m_byteObjectiveVehicle = 0;
	m_bSpecialMarkerEnabled = false;
	m_byteColor1 = 0xFF;
	m_byteColor2 = 0xFF;
	m_bHaveColor = false;
	m_bSiren = bSiren;
	m_bLocalUnoccupiedSync = false;
	m_bRemoteUnoccupiedSync = false;
	m_dwMarkerID = 0;
	m_bPlateDirty = false;
	m_bPlateGenAttempts = 0;
	m_iRemoteLandingGear = -1;
	m_iLocalLandingGear = -1;
	m_fRemoteBikeLean = 0.0f;
	m_bHasRemoteBikeLean = false;
	memset(m_szPlateText, 0, sizeof(m_szPlateText));

	if ((iType != TRAIN_PASSENGER_LOCO) &&
			(iType != TRAIN_FREIGHT_LOCO) &&
			(iType != TRAIN_PASSENGER) &&
			(iType != TRAIN_FREIGHT) &&
			(iType != TRAIN_TRAM)) {

		if (!pGame->IsModelLoaded(iType)) {
			pGame->RequestModel(iType);
			pGame->LoadRequestedModels();
			int iTryCount = 0;
			while (!pGame->IsModelLoaded(iType)) {
				usleep(10000);
				if (++iTryCount > 200) {
					spdlog::error("CVehicle::CVehicle: model {} wouldn't load in time", iType);
					return;
				}
			}
		}

		g_bCreatingSirenVehicle = bSiren;
		ScriptCommand(&create_car, iType, fX, fY, fZ, &dwRetID);
		g_bCreatingSirenVehicle = false;
		if (!dwRetID) {
			spdlog::error("CVehicle::CVehicle: create_car failed for model {}", iType);
			return;
		}
		ScriptCommand(&set_car_z_angle, dwRetID, fRotation);
		ScriptCommand(&car_gas_tank_explosion, dwRetID, 0);
		ScriptCommand(&set_car_hydraulics, dwRetID, 0);
		ScriptCommand(&toggle_car_tires_vulnerable, dwRetID, 0);

		spdlog::debug("vehicle ret id: {}", dwRetID);

		m_pVehicle = GamePool_Vehicle_GetAt(dwRetID);
		if (!m_pVehicle) {
			spdlog::error("CVehicle::CVehicle: create_car returned handle {} with no entity", dwRetID);
			return;
		}
		m_entity = m_pVehicle;
		m_gtaId = dwRetID;

		if (m_pVehicle) {
			m_pVehicle->m_nDoorLock = sa::CARLOCK_NOT_USED;
			m_pVehicle->m_fDirtLevel = 0.0f;
			// Note: bSiren (CreateVehicle addsiren=1) enables horn-toggled siren via CVehicle::UsesSiren hook;
			// do NOT auto-activate bSirenOrAlarm on spawn.
			if (pNetGame && pNetGame->m_netSet && pNetGame->m_netSet->manualVehicleEngineAndLight) {
				SetEngineState(false);
				SetLightState(false);
			}

			mat = m_pVehicle->GetMatrix().ToRwMatrix();
			mat.pos.x = fX;
			mat.pos.y = fY;
			mat.pos.z = fZ;

			if (GetVehicleSubtype() != VEHICLE_SUBTYPE_BIKE && GetVehicleSubtype() != VEHICLE_SUBTYPE_PUSHBIKE) {
				mat.pos.z += 0.25f;
			}

			m_pVehicle->SetMatrix(mat);
		}
	}
	else if ((iType == TRAIN_PASSENGER_LOCO) ||
			(iType == TRAIN_FREIGHT_LOCO) ||
			(iType == TRAIN_TRAM)) {
		if (iType == TRAIN_PASSENGER_LOCO) { iType = 5; }
		else if (iType == TRAIN_FREIGHT_LOCO) { iType = 3; }
		else if (iType == TRAIN_TRAM) iType = 9;

		uint32_t dwDirection = 0;
		if (fRotation > 180.0f) {
			dwDirection = 1;
		}

		pGame->RequestModel(TRAIN_PASSENGER_LOCO);
		pGame->RequestModel(TRAIN_PASSENGER);
		pGame->RequestModel(TRAIN_FREIGHT_LOCO);
		pGame->RequestModel(TRAIN_FREIGHT);
		pGame->RequestModel(TRAIN_TRAM);
		pGame->LoadRequestedModels();

		int iTryCount = 0;
		const int iTrainModels[5] = {TRAIN_PASSENGER_LOCO, TRAIN_PASSENGER, TRAIN_FREIGHT_LOCO, TRAIN_FREIGHT, TRAIN_TRAM};
		for (int iTrainModel : iTrainModels) {
			while (!pGame->IsModelLoaded(iTrainModel)) {
				usleep(10000);
				if (++iTryCount > 200) {
					spdlog::error("CVehicle::CVehicle: train model {} wouldn't load in time", iTrainModel);
					return;
				}
			}
		}

		ScriptCommand(&create_train, iType, fX, fY, fZ, dwDirection, &dwRetID);
		m_pVehicle = dwRetID ? GamePool_Vehicle_GetAt(dwRetID) : nullptr;
		if (!m_pVehicle) {
			spdlog::error("CVehicle::CVehicle: create_train failed for type {}", iType);
			pCreatedTrain = nullptr;
			return;
		}
		m_entity = m_pVehicle;
		m_gtaId = dwRetID;

		pCreatedTrain = m_pVehicle;

		GamePrepareTrain(m_pVehicle);
	}
	else if ((iType == TRAIN_PASSENGER) ||
			(iType == TRAIN_FREIGHT)) {
		if (!pCreatedTrain ||
				GamePool_Vehicle_GetAt(GamePool_Vehicle_GetIndex(pCreatedTrain)) != pCreatedTrain) {
			// train entity gone (deleted/streamed between carriage spawns)
			pCreatedTrain = nullptr;
			m_entity = nullptr;
			m_pVehicle = nullptr;
			return;
		}

		m_pVehicle = ((sa::CTrain*) pCreatedTrain)->pLinkedToBackward;
		if (!m_pVehicle) {
			if (pUI) pUI->chat()->addDebugMessage("Warning: bad train carriages");
			m_entity = nullptr;
			m_pVehicle = nullptr;
			pCreatedTrain = nullptr;
			return;
		}

		m_gtaId = GamePool_Vehicle_GetIndex(m_pVehicle);
		m_entity = m_pVehicle;
		pCreatedTrain = m_pVehicle;
	}
}

CVehicle::~CVehicle()
{
	if (pCreatedTrain == m_pVehicle) {
		pCreatedTrain = nullptr;
	}
	if (m_dwMarkerID) {
		spdlog::debug("remove vehicle radar marker: {}", m_dwMarkerID);
		pGame->DisableMarker(m_dwMarkerID);
		m_dwMarkerID = 0;
	}

	if (m_pVehicle && GamePool_Vehicle_GetAt(m_gtaId) == m_pVehicle) {
		RemoveEveryoneFromVehicle();

		if (m_pTrailer) {
			DetachTrailer();
			m_pTrailer = nullptr;
		}

		int iModel = m_pVehicle->m_nModelIndex;
		if (iModel == 538 || iModel == 537 || iModel == 449) {
			ScriptCommand(&destroy_train, m_gtaId);
		}
		else if (iModel != 569 && iModel != 570) {
			ScriptCommand(&destroy_car, m_gtaId);
			int iRefs = GetModelRefCounts(iModel);
			spdlog::debug("Destroy car({}): Refs: {}", iModel, iRefs);
			if (!iRefs && !m_bPreloaded) {
				pGame->RemoveModel(iModel, true);
			}
		}
	}
}

void CVehicle::RemoveEveryoneFromVehicle()
{
	if (!VerifyInstance()) return;

	const sa::CVector pos = m_pVehicle->GetPosition();
	float fPosX = pos.x;
	float fPosY = pos.y;
	float fPosZ = pos.z;

	int iPlayerID = 0;
	if (m_pVehicle->pDriver) {
		iPlayerID = GamePool_Ped_GetIndex(m_pVehicle->pDriver);
		ScriptCommand(&remove_actor_from_car_and_put_at, iPlayerID, fPosX, fPosY, fPosZ + 2);
	}

	for (int i = 0; i < 8; i++) {
		if (m_pVehicle->m_apPassengers[i] != NULL) {
			iPlayerID = GamePool_Ped_GetIndex(m_pVehicle->m_apPassengers[i]);
			ScriptCommand(&remove_actor_from_car_and_put_at, iPlayerID, fPosX, fPosY, fPosZ + 2);
		}
	}
}

float CVehicle::GetHealth()
{
	if (m_pVehicle && VerifyInstance()) {
		return m_pVehicle->fHealth;
	}

	return 0.0f;
}

/**
 * @param byteColor1
 * @param byteColor2
 */
void CVehicle::SetColor(uint8_t byteColor1, uint8_t byteColor2)
{
	if (byteColor1 != 0xFF) {
		m_byteColor1 = byteColor1;
	}
	if (byteColor2 != 0xFF) {
		m_byteColor2 = byteColor2;
	}

	if (m_pVehicle && VerifyInstance()) {
		if (m_byteColor1 != 0xFF) {
			m_pVehicle->m_nPrimaryColor = m_byteColor1;
		}
		if (m_byteColor2 != 0xFF) {
			m_pVehicle->m_nSecondaryColor = m_byteColor2;
		}
	}

	m_bHaveColor = (m_byteColor1 != 0xFF || m_byteColor2 != 0xFF);
}

void CVehicle::UpdateColor()
{
	if (m_pVehicle && VerifyInstance() && m_bHaveColor) {
		if (m_byteColor1 != 0xFF && m_pVehicle->m_nPrimaryColor != m_byteColor1) {
			m_pVehicle->m_nPrimaryColor = m_byteColor1;
		}
		if (m_byteColor2 != 0xFF && m_pVehicle->m_nSecondaryColor != m_byteColor2) {
			m_pVehicle->m_nSecondaryColor = m_byteColor2;
		}
	}
}

/**
 * @param iInterior
 */
void CVehicle::LinkToInterior(int iInterior)
{
	if (VerifyInstance()) {
		ScriptCommand(&link_vehicle_to_interior, m_gtaId, iInterior);
	}
}

/**
 * @param byteTireDamageStatus
 */
void CVehicle::SetTireDamageStatus(uint8_t byteTireDamageStatus)
{
	if (m_pVehicle && VerifyInstance()) {
		const int iSubType = GetVehicleSubtype();
		if (iSubType == VEHICLE_SUBTYPE_CAR || iSubType == VEHICLE_SUBTYPE_PLANE) {
			// SA-MP tire damage byte is packed MSB-first: bit 3 -> wheel 0
			((sa::CAutomobile*) m_pVehicle)->Damage.m_Wheel[0] = (byteTireDamageStatus >> 3) & 1;
			((sa::CAutomobile*) m_pVehicle)->Damage.m_Wheel[1] = (byteTireDamageStatus >> 2) & 1;
			((sa::CAutomobile*) m_pVehicle)->Damage.m_Wheel[2] = (byteTireDamageStatus >> 1) & 1;
			((sa::CAutomobile*) m_pVehicle)->Damage.m_Wheel[3] = byteTireDamageStatus & 1;
		}
		else if (iSubType == VEHICLE_SUBTYPE_BIKE) {
			((sa::CBike*) m_pVehicle)->m_nWheelStatus[0] = (byteTireDamageStatus >> 1) & 1;
			((sa::CBike*) m_pVehicle)->m_nWheelStatus[1] = byteTireDamageStatus & 1;
		}
	}
}

void CVehicle::GetDamageStatus(uint32_t* pdwPanels, uint32_t* pdwDoors, uint8_t* pbyteLights, uint8_t* pbyteTires)
{
	*pdwPanels = 0;
	*pdwDoors = 0;
	*pbyteLights = 0;
	*pbyteTires = 0;

	if (!m_pVehicle || !VerifyInstance()) return;

	int iSubType = GetVehicleSubtype();
	if (iSubType == VEHICLE_SUBTYPE_CAR || iSubType == VEHICLE_SUBTYPE_PLANE) {
		sa::CAutomobile* pAuto = (sa::CAutomobile*) m_pVehicle;
		*pdwPanels = pAuto->Damage.m_Panels;
		*pdwDoors = (uint32_t) (pAuto->Damage.m_Door[0] | (pAuto->Damage.m_Door[1] << 8) |
				(pAuto->Damage.m_Door[2] << 16) | (pAuto->Damage.m_Door[3] << 24));
		*pbyteLights = (uint8_t) (pAuto->Damage.m_Lights & 0xFF);
		// packed MSB-first to mirror SetTireDamageStatus
		*pbyteTires = (uint8_t) ((pAuto->Damage.m_Wheel[0] != 0) << 3 |
				(pAuto->Damage.m_Wheel[1] != 0) << 2 |
				(pAuto->Damage.m_Wheel[2] != 0) << 1 |
				(pAuto->Damage.m_Wheel[3] != 0));
	}
	else if (iSubType == VEHICLE_SUBTYPE_BIKE) {
		sa::CBike* pBike = (sa::CBike*) m_pVehicle;
		*pbyteTires = (uint8_t) ((pBike->m_nWheelStatus[0] != 0) << 1 | (pBike->m_nWheelStatus[1] != 0));
	}
}

/**
 * @param dwPanelDamageStatus
 * @param dwDoorDamageStatus
 * @param byteLightDamageStatus
 */
void CVehicle::SetDamageStatus(uint32_t dwPanelDamageStatus, uint32_t dwDoorDamageStatus, uint8_t byteLightDamageStatus)
{
	if (m_pVehicle && VerifyInstance()) {
		const int iSubType = GetVehicleSubtype();
		if (iSubType == VEHICLE_SUBTYPE_CAR || iSubType == VEHICLE_SUBTYPE_PLANE) {
			if (!dwPanelDamageStatus && !dwDoorDamageStatus && !byteLightDamageStatus) {
				uint32_t dwDoorStatus = (((sa::CAutomobile*) m_pVehicle)->Damage.m_Door[0] | (((sa::CAutomobile*) m_pVehicle)->Damage.m_Door[1] << 8) | (((sa::CAutomobile*) m_pVehicle)->Damage.m_Door[2] << 16) | (((sa::CAutomobile*) m_pVehicle)->Damage.m_Door[3] << 24));
				if (((sa::CAutomobile*) m_pVehicle)->Damage.m_Panels || dwDoorStatus || ((sa::CAutomobile*) m_pVehicle)->Damage.m_Lights) {
					// The ingame car is damaged in some way although the update
					// says the car should be repaired. So repair it and exit.
					// CAutoMobile::Fix
					eng::fn::CAutomobile_Fix( m_pVehicle);
					return;
				}
			}

			// panel
			((sa::CAutomobile*) m_pVehicle)->Damage.m_Panels = dwPanelDamageStatus;

			// door (preserve full byte per door to match PC & GetDamageStatus)
			((sa::CAutomobile*) m_pVehicle)->Damage.m_Door[0] = static_cast<uint8_t>(dwDoorDamageStatus & 0xFF);
			((sa::CAutomobile*) m_pVehicle)->Damage.m_Door[1] = static_cast<uint8_t>((dwDoorDamageStatus >> 8) & 0xFF);
			((sa::CAutomobile*) m_pVehicle)->Damage.m_Door[2] = static_cast<uint8_t>((dwDoorDamageStatus >> 16) & 0xFF);
			((sa::CAutomobile*) m_pVehicle)->Damage.m_Door[3] = static_cast<uint8_t>((dwDoorDamageStatus >> 24) & 0xFF);

			// light
			((sa::CAutomobile*) m_pVehicle)->Damage.m_Lights = (uint32_t) byteLightDamageStatus;

			// CAutomobile::SetupDamageAfterLoad
			eng::fn::CAutomobile_SetupDamageAfterLoad( m_pVehicle);

		}
	}
}

void CVehicle::SetHealth(float fHealth)
{
	if (m_pVehicle && VerifyInstance()) {
		m_pVehicle->fHealth = fHealth;
	}
}

/**
 * @return
 */
CVehicle* CVehicle::GetTrailer()
{
	if (m_pVehicle && VerifyInstance()) {
		sa::CVehicle* pTrailer = m_pVehicle->m_pTrailer;
		if (pTrailer) {
			if (pNetGame) {
				CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
				if (pVehiclePool) {
					VEHICLEID TrailerID = pVehiclePool->FindIDFromGtaPtr(pTrailer);
					if (TrailerID != INVALID_VEHICLE_ID) {
						return pVehiclePool->GetAt(TrailerID);
					}
				}
			}
		}
	}

	return nullptr;
}

/**
 * @param bytePaintJobID
 */
void CVehicle::SetPaintJob(uint8_t bytePaintJobID)
{
	if (m_pVehicle && VerifyInstance()) {
		if (GetVehicleSubtype() == VEHICLE_SUBTYPE_CAR) {
			if (bytePaintJobID <= 3) {
				ScriptCommand(&change_car_skin, m_gtaId, bytePaintJobID);
			}
		}
	}
}

/**
 * @param iComponentID
 */
void CVehicle::AddComponent(int iComponentID)
{
	if (!m_pVehicle || !VerifyInstance()) return;
	if (GetVehicleSubtype() != VEHICLE_SUBTYPE_CAR) return;

	if (iComponentID < 1000 || iComponentID > 1193
			|| !VehicleComponents::isValidForModel(m_pVehicle->m_nModelIndex, iComponentID)) {
		spdlog::debug("CVehicle::AddComponent: Incompatible component {} for model {}", iComponentID, m_pVehicle->m_nModelIndex);
		return;
	}

	// CStreaming::HasVehicleUpgradeLoaded
	bool HasVehicleUpgradeLoaded = eng::fn::CStreaming_HasVehicleUpgradeLoaded( iComponentID);

	if (!HasVehicleUpgradeLoaded) {
		// CStreaming::RequestVehicleUpgrade
		eng::fn::CStreaming_RequestVehicleUpgrade( iComponentID, 10);
		pGame->LoadRequestedModels();

		int waitPreloaded = 0;
		while (!HasVehicleUpgradeLoaded) {
			usleep(1000);
			if (++waitPreloaded > 30) {
				spdlog::debug("CVehicle::AddComponent: component {} wouldn't load in time", iComponentID);
				return;
			}
			HasVehicleUpgradeLoaded = eng::fn::CStreaming_HasVehicleUpgradeLoaded( iComponentID);
		}
	}

	uint32_t dwRetID;
	ScriptCommand(&add_car_component, m_gtaId, iComponentID, &dwRetID);
}

void CVehicle::RemoveComponent(int iComponentID)
{
	if (!m_pVehicle || !VerifyInstance()) {
		return;
	}

	ScriptCommand(&remove_component, m_gtaId, iComponentID);
}

/**
 * @return
 */
CVehicle* CVehicle::GetTractor()
{
	if (!m_pVehicle || !VerifyInstance()) return nullptr;

	int iSubType = ::GetVehicleSubtype(m_pVehicle);
	if (iSubType == VEHICLE_SUBTYPE_BIKE
			|| iSubType == VEHICLE_SUBTYPE_BOAT
			|| iSubType == VEHICLE_SUBTYPE_TRAIN
			|| iSubType == VEHICLE_SUBTYPE_PUSHBIKE) {
		return nullptr;
	}

	sa::CVehicle* pTractor = m_pVehicle->m_pTowingVehicle;
	if (pTractor) {
		if (pNetGame) {
			CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
			if (pVehiclePool) {
				VEHICLEID TrailerID = pVehiclePool->FindIDFromGtaPtr(pTractor);
				if (TrailerID != INVALID_VEHICLE_ID) {
					return pVehiclePool->GetAt(TrailerID);
				}
			}
		}
	}

	return nullptr;
}

/**
 * @param pTrailer
 */
void CVehicle::SetTrailer(CVehicle* pTrailer)
{
	m_pTrailer = pTrailer;
}

/**
 * @return
 */
float CVehicle::GetTrainSpeed()
{
	if (m_pVehicle && VerifyInstance()) {
		return ((sa::CTrain*) m_pVehicle)->LinearSpeed;
	}
	return 0.0f;
}

/**
 * @return
 */
uint32_t CVehicle::GetHydraThrusters()
{
	if (m_pVehicle && VerifyInstance()) {
		return *(uint32_t*) &((sa::CAutomobile*) m_pVehicle)->m_nSuspensionHydraulics;
	}

	return 0;
}

/**
 * @return
 */
float CVehicle::GetBikeLean()
{
	if (m_pVehicle && VerifyInstance()) {
		const int sub = GetVehicleSubtype();
		if (sub == VEHICLE_SUBTYPE_BIKE || sub == VEHICLE_SUBTYPE_PUSHBIKE) {
			return reinterpret_cast<sa::CBike*>(m_pVehicle)->RideAnimData.m_fLeanAngle;
		}
	}

	return 0.0f;
}

void CVehicle::SetBikeLean(float fLean)
{
	if (!m_pVehicle || !VerifyInstance() || !std::isfinite(fLean)) {
		return;
	}
	const int sub = GetVehicleSubtype();
	if (sub == VEHICLE_SUBTYPE_BIKE || sub == VEHICLE_SUBTYPE_PUSHBIKE) {
		auto* bike = reinterpret_cast<sa::CBike*>(m_pVehicle);
		bike->RideAnimData.m_fLeanAngle = std::clamp(fLean, -1.0f, 1.0f);
		bike->m_bLeanMatrix = false;
		eng::fn::CBike_CalculateLeanMatrix(bike);
	}
}

void CVehicle::SetRemoteBikeLean(float fLean)
{
	if (!std::isfinite(fLean)) {
		return;
	}
	m_fRemoteBikeLean = std::clamp(fLean, -1.0f, 1.0f);
	m_bHasRemoteBikeLean = true;
	SetBikeLean(m_fRemoteBikeLean);
}

uint8_t CVehicle::GetLandingGearState()
{
	if (m_pVehicle && VerifyInstance() && GetVehicleSubtype() == VEHICLE_SUBTYPE_PLANE) {
		auto* plane = reinterpret_cast<sa::CPlane*>(m_pVehicle);
		return (plane->m_fLGearAngle != 0.0f) ? 1 : 0;
	}
	return 0;
}

void CVehicle::SetLandingGearState(uint8_t state)
{
	if (!m_pVehicle || !VerifyInstance() || GetVehicleSubtype() != VEHICLE_SUBTYPE_PLANE) {
		return;
	}
	auto* plane = reinterpret_cast<sa::CPlane*>(m_pVehicle);
	if (state == 1 && plane->m_fLGearAngle != 1.0f) {
		eng::fn::CPlane_SetGearUp(plane);
	}
	else if (state == 0 && plane->m_fLGearAngle != 0.0f) {
		eng::fn::CPlane_SetGearDown(plane);
	}
}

void CVehicle::SetRemoteLandingGearState(uint8_t state)
{
	m_iRemoteLandingGear = (state != 0) ? 1 : 0;
	SetLandingGearState(static_cast<uint8_t>(m_iRemoteLandingGear));
}

void CVehicle::ProcessLicensePlate()
{
	if (!m_bPlateDirty || !m_pVehicle || !VerifyInstance() || !pMaterialTextGenerator) {
		return;
	}
	if (m_bPlateGenAttempts >= 3) {
		m_bPlateDirty = false;
		return;
	}
	++m_bPlateGenAttempts;

	RwTexture* tex = pMaterialTextGenerator->CreateCustomCarPlate(m_szPlateText);
	if (tex) {
		if (m_pVehicle->m_pCustomCarPlate) {
			RwTextureDestroy(m_pVehicle->m_pCustomCarPlate);
		}
		m_pVehicle->m_pCustomCarPlate = tex;
		m_bPlateDirty = false;
	}
}

int CVehicle::GetVehicleSubtype()
{
	if (m_pVehicle) {
		return ::GetVehicleSubtype(m_pVehicle);
	}
	return 0;
}

/**
 * @return
 */
bool CVehicle::IsRCVehicle()
{
	if (m_pVehicle && VerifyInstance()) {
		int iModel = m_pVehicle->m_nModelIndex;
		if (iModel == 441
				|| iModel == 464
				|| iModel == 465
				|| iModel == 594
				|| iModel == 501
				|| iModel == 564) {
			return true;
		}
	}

	return false;
}

/**
 * @return
 */
bool CVehicle::VerifyInstance()
{
	return m_pVehicle && GamePool_Vehicle_GetAt(m_gtaId) == m_pVehicle;
}

void CVehicle::DetachTrailer()
{
	if (m_pTrailer) {
		if (m_gtaId && VerifyInstance()) {
			if (m_pTrailer->m_pVehicle && GamePool_Vehicle_GetAt(m_pTrailer->m_gtaId) == m_pTrailer->m_pVehicle) {
				g_bIntentionalTrailerDetach = true;
				ScriptCommand(&detach_trailer_from_cab, m_pTrailer->m_gtaId, m_gtaId);
				g_bIntentionalTrailerDetach = false;
			}
		}
	}
}

void CVehicle::SetHydraThrusters(uint32_t state)
{
	if (m_pVehicle && VerifyInstance()) {
		*(uint32_t*) &((sa::CAutomobile*) m_pVehicle)->m_nSuspensionHydraulics = state;
	}
}

/**
 * @param iDoor
 * @param iComponent
 * @param fDoorOpenRatio
 */
void CVehicle::SetComponentOpenState(int iDoor, int iComponent, float fDoorOpenRatio)
{
	if (!m_pVehicle || !VerifyInstance() || GetVehicleSubtype() != VEHICLE_SUBTYPE_CAR) return;

	ScriptCommand(&open_car_door_a_bit, m_gtaId, iDoor, fDoorOpenRatio);
}

void CVehicle::OpenWindow(uint8_t component)
{
	if (!m_pVehicle || !VerifyInstance()) return;
	eng::fn::CVehicle_SetWindowOpenFlag(m_pVehicle, component);
}

void CVehicle::CloseWindow(uint8_t component)
{
	if (!m_pVehicle || !VerifyInstance()) return;
	eng::fn::CVehicle_ClearWindowOpenFlag(m_pVehicle, component);
}

/**
 * @return
 */
bool CVehicle::IsDriverLocalPlayer()
{
	if (m_pVehicle && VerifyInstance() && m_pVehicle->pDriver == GamePool_FindPlayerPed()) {
		return true;
	}

	return false;
}

/**
 * @param bInv
 */
void CVehicle::SetInvulnerable(bool bInv)
{
	if (m_pVehicle && VerifyInstance() && !IsEntityPlaceable(m_pVehicle)) {
		if (bInv) {
			ScriptCommand(&set_car_immunities, m_gtaId, 1, 1, 1, 1, 1);
			ScriptCommand(&toggle_car_tires_vulnerable, m_gtaId, 0);
			m_bIsInvulnerable = true;
		}
		else {
			ScriptCommand(&set_car_immunities, m_gtaId, 0, 0, 0, 0, 0);
			ScriptCommand(&toggle_car_tires_vulnerable, m_gtaId, 1);
			m_bIsInvulnerable = false;
		}
	}
}

/**
 * @return
 */
bool CVehicle::HasSunk()
{
	if (m_pVehicle && VerifyInstance()) {
		return ScriptCommand(&has_car_sunk, m_gtaId);
	}

	return false;
}

/**
 * @return
 */
bool CVehicle::HasADriver()
{
	if (m_pVehicle && VerifyInstance()) {
		if (m_pVehicle->pDriver) {
			if (m_pVehicle->pDriver->IsInVehicle() && m_pVehicle->pDriver->m_nPedType == 0) {
				return true;
			}
		}
	}

	return false;
}

/**
 * @param fAngle
 */
void CVehicle::SetZAngle(float fAngle)
{
	if (VerifyInstance()) {
		ScriptCommand(&set_car_z_angle, m_gtaId, fAngle);
	}
}

/**
 */
void CVehicle::ProcessMarkers()
{
	if (!VerifyInstance()) return;

	if (m_byteObjectiveVehicle) {
		if (!m_bSpecialMarkerEnabled) {
			if (m_dwMarkerID) {
				pGame->DisableMarker(m_dwMarkerID);
				m_dwMarkerID = 0;
			}

			ScriptCommand(&tie_marker_to_car, m_gtaId, 1, 3, &m_dwMarkerID);
			ScriptCommand(&set_marker_color, m_dwMarkerID, 1006);
			ScriptCommand(&show_on_radar, m_dwMarkerID, 3);
			m_bSpecialMarkerEnabled = true;
		}
	}
	else {
		if (m_bSpecialMarkerEnabled && m_dwMarkerID) {
			pGame->DisableMarker(m_dwMarkerID);
			m_bSpecialMarkerEnabled = false;
			m_dwMarkerID = 0;
		}

		if (GetDistanceFromLocalPlayerPed() < 200.0f && !IsOccupied()) {
			// show
			if (!m_dwMarkerID) {
				ScriptCommand(&tie_marker_to_car, m_gtaId, 1, 2, &m_dwMarkerID);
				ScriptCommand(&set_marker_color, m_dwMarkerID, 1004);
			}
		}
		else {
			// remove
			if (m_dwMarkerID) {
				pGame->DisableMarker(m_dwMarkerID);
				m_dwMarkerID = 0;
			}
		}
	}
}

/**
 * @return
 */
bool CVehicle::IsOccupied()
{
	if (m_pVehicle && VerifyInstance()) {
		if (m_pVehicle->pDriver) return true;
		for (int i = 0; i < 8; i++) {
			if (m_pVehicle->m_apPassengers[i]) return true;
		}
	}

	return false;
}

/**
 * @return
 */
bool CVehicle::IsATrainPart()
{
	if (m_pVehicle && VerifyInstance()) {
		int iModelIndex = m_pVehicle->m_nModelIndex;
		if (iModelIndex == 538 || iModelIndex == 570 || iModelIndex == 537 ||
				iModelIndex == 569 || iModelIndex == 449) {
			return true;
		}
	}

	return false;
}

void CVehicle::AttachTrailer()
{
	if (m_pTrailer && VerifyInstance() &&
			m_pTrailer->m_pVehicle && GamePool_Vehicle_GetAt(m_pTrailer->m_gtaId) == m_pTrailer->m_pVehicle) {
		g_bIntentionalTrailerDetach = true;
		ScriptCommand(&put_trailer_on_cab, m_pTrailer->m_gtaId, m_gtaId);
		g_bIntentionalTrailerDetach = false;
	}
}

void CVehicle::ApplyLightStatus(bool state)
{
	if (m_pVehicle && VerifyInstance()) {
		const uint8_t desired = state ? 2 : 1;
		if (m_pVehicle->m_nOverrideLights != desired) {
			ScriptCommand(&force_car_lights, m_gtaId, desired);
		}
	}
}

void CVehicle::SetLightState(bool state)
{
	m_iLightState = state ? 1 : 0;
	ApplyLightStatus(state);
}

/**
 * @return
 */
bool CVehicle::GetDoorState()
{
	if (m_pVehicle && VerifyInstance()) {
		return m_pVehicle->m_nDoorLock == sa::CARLOCK_LOCKED;
	}

	return false;
}

/**
 * @param state
 */
void CVehicle::SetDoorState(int state)
{
	if (!VerifyInstance()) return;
	if (state) {
		m_pVehicle->m_nDoorLock = sa::CARLOCK_LOCKED;
		m_bDoorsLocked = true;
	}
	else {
		m_pVehicle->m_nDoorLock = sa::CARLOCK_NOT_USED;
		m_bDoorsLocked = false;
	}
}
