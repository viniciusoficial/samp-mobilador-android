#pragma once

#include <cstdint>
#include <cstring>
#include "Entity.h"

namespace sa {
class CVehicle;
}

class CVehicle : public CEntity {
public:
	CVehicle(int iType, float fX, float fY, float fZ, float fRotation, bool bPreloaded, bool bSiren);
	virtual ~CVehicle();

	int GetVehicleSubtype();
	void AddComponent(int iComponentID);
	void RemoveComponent(int iComponentID);
	void SetPaintJob(uint8_t bytePaintJobID);
	void SetColor(uint8_t iColor1, uint8_t iColor2);
	void UpdateColor();
	void LinkToInterior(int iInterior);
	void SetDamageStatus(uint32_t dwPanelDamageStatus, uint32_t dwDoorDamageStatus, uint8_t byteLightDamageStatus);
	void SetTireDamageStatus(uint8_t byteTireDamageStatus);
	void GetDamageStatus(uint32_t* pdwPanels, uint32_t* pdwDoors, uint8_t* pbyteLights, uint8_t* pbyteTires);
	void SetPlateText(const char* szPlateText)
	{
		if (szPlateText) {
			strncpy(m_szPlateText, szPlateText, 31);
			m_szPlateText[31] = '\0';
		}
		else {
			m_szPlateText[0] = '\0';
		}
		m_bPlateDirty = true;
		m_bPlateGenAttempts = 0;
	}
	void ProcessLicensePlate();
	void SetZAngle(float fAngle);
	void ProcessMarkers();
	bool IsOccupied();

	void AttachTrailer();
	void DetachTrailer();
	void SetTrailer(CVehicle* pTrailer);

	bool IsRCVehicle();
	float GetHealth();
	void SetHealth(float fHealth);
	CVehicle* GetTrailer();
	CVehicle* GetTractor();
	float GetTrainSpeed();
	uint32_t GetHydraThrusters();
	float GetBikeLean();
	void SetBikeLean(float fLean);
	void SetRemoteBikeLean(float fLean);
	uint8_t GetLandingGearState();
	void SetLandingGearState(uint8_t state);
	void SetRemoteLandingGearState(uint8_t state);
	bool IsATrainPart();
	bool VerifyInstance();
	bool IsDriverLocalPlayer();
	void SetInvulnerable(bool bInv);
	bool HasSunk();
	bool HasADriver();

	void ApplyEngineStatus(bool state);
	void SetEngineState(bool state)
	{
		m_iEngineState = state ? 1 : 0;
		ApplyEngineStatus(state);
	}
	int GetEngineState();

	void ApplyLightStatus(bool state);
	void SetLightState(bool state);

	int GetLightState()
	{
		return m_iLightState > 0 ? 1 : 0;
	}

	void SetDoorState(int state);
	bool GetDoorState();

	void SetSirenOn(uint8_t state);
	bool IsSirenOn();

	void SetHydraThrusters(uint32_t state);

	void SetAlarmState(uint16_t wAlarmState);
	uint16_t GetAlarmState();

	void SetComponentOpenState(int iDoor, int iComponent, float fDoorOpenRatio);

	void OpenWindow(uint8_t component);
	void CloseWindow(uint8_t component);

	void RemoveEveryoneFromVehicle();

	bool HasTurret()
	{
		if (!m_pVehicle) return false;

		int nModel = GetModelIndex();
		return (nModel == 432 ||    // Tank
				nModel == 564 ||    // RC Tank
				nModel == 407 ||    // Firetruck
				nModel == 601       // Swatvan
		);
	}

	bool IsATowTruck()
	{
		if (!m_pVehicle) return false;
		return GetModelIndex() == 525;
	}

	bool IsATrailer()
	{
		if (!m_pVehicle) return false;

		int nModel = GetModelIndex();
		return (nModel == 435 ||
				nModel == 450 ||
				nModel == 584 ||
				nModel == 591 ||
				nModel == 606 ||
				nModel == 607 ||
				nModel == 608 ||
				nModel == 610 ||
				nModel == 611
		);
	}

public:
	sa::CVehicle* m_pVehicle;
	CVehicle* m_pTrailer;
	bool m_bPreloaded;
	bool m_bIsInvulnerable;
	bool m_bIsLocked;
	int8_t m_iEngineState; // -1 = unset (auto), 0 = off, 1 = on
	int8_t m_iLightState;  // -1 = unset (auto day/night), 0 = off, 1 = on
	bool m_bDoorsLocked;
	uint8_t m_byteObjectiveVehicle;
	bool m_bSpecialMarkerEnabled;
	uint8_t m_byteColor1;
	uint8_t m_byteColor2;
	bool m_bHaveColor;
	bool m_bSiren;
	bool m_bLocalUnoccupiedSync;
	bool m_bRemoteUnoccupiedSync;
	char m_szPlateText[32];
	bool m_bPlateDirty;
	uint8_t m_bPlateGenAttempts;
	int8_t m_iRemoteLandingGear;
	int8_t m_iLocalLandingGear;
	float m_fRemoteBikeLean;
	bool m_bHasRemoteBikeLean;
	uint32_t m_dwMarkerID;
};