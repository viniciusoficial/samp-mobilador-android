#pragma once

#include <unordered_map>
#include <Core/Vector.h>
#include "NetTypes.h"

namespace sa {
class CVehicle;
}
class CVehicle;

#pragma pack(push, 1)
typedef struct _NEW_VEHICLE {
	VEHICLEID VehicleID;
	int iVehicleType;
	sa::CVector vecPos;
	float fRotation;
	uint8_t byteInteriorColor1;
	uint8_t byteInteriorColor2;
	float fHealth;
	uint8_t byteInterior;
	uint32_t dwDoorDamageStatus;
	uint32_t dwPanelDamageStatus;
	uint8_t byteLightDamageStatus;
	uint8_t byteTireDamageStatus;
	uint8_t byteAddSiren;

} NEW_VEHICLE;

typedef struct _VEHICLE_MOD {
	uint8_t byteModSlots[14];
	uint8_t bytePaintJob;
	int iBodyColor1;
	int iBodyColor2;
} VEHICLE_MOD;

typedef struct _VEHICLE_PARAMS_EX {
	uint8_t byteEngine;        // 0
	uint8_t byteLight;        // 1
	uint8_t byteAlarm;        // 2
	uint8_t byteDoors;        // 3
	uint8_t byteBonnet;        // 4
	uint8_t byteBoot;        // 5
	uint8_t byteObjective;    // 6
	uint8_t byteSiren;        // 7
	uint8_t byteDoor1;        // 8
	uint8_t byteDoor2;        // 9
	uint8_t byteDoor3;        // 10
	uint8_t byteDoor4;        // 11
	uint8_t byteWindow1;    // 12
	uint8_t byteWindow2;    // 13
	uint8_t byteWindow3;    // 14
	uint8_t byteWindow4;    // 15
} VEHICLE_PARAMS_EX;

#pragma pack(pop)

class CVehiclePool {
public:
	CVehiclePool();
	~CVehiclePool();

	bool New(NEW_VEHICLE* new_veh);
	bool Delete(VEHICLEID VehicleID);

	void Process();
	void ProcessLicensePlates();
	void NotifyVehicleDeath(VEHICLEID VehicleID);

	CVehicle* GetAt(VEHICLEID vehicleID)
	{
		if (vehicleID < MAX_VEHICLES && m_bVehicleSlotState[vehicleID]) {
			return m_pVehicles[vehicleID];
		}

		return nullptr;
	}

	bool GetSlotState(VEHICLEID VehicleID)
	{
		if (VehicleID < MAX_VEHICLES) {
			return m_bVehicleSlotState[VehicleID];
		}

		return false;
	}

	VEHICLEID FindIDFromGtaPtr(sa::CVehicle* pGtaVehicle)
	{
		if (!pGtaVehicle) return INVALID_VEHICLE_ID;

		auto it = m_mapGtaToId.find(pGtaVehicle);
		if (it != m_mapGtaToId.end()
				&& it->second < MAX_VEHICLES
				&& m_bVehicleSlotState[it->second]
				&& m_pGTAVehicles[it->second] == pGtaVehicle) {
			return it->second;
		}

		for (int i = 0; i < MAX_VEHICLES; i++) {
			if (m_bVehicleSlotState[i] && m_pGTAVehicles[i] == pGtaVehicle) {
				m_mapGtaToId[pGtaVehicle] = (VEHICLEID) i;
				return i;
			}
		}

		m_mapGtaToId.erase(pGtaVehicle);
		return INVALID_VEHICLE_ID;
	}

	uint32_t FindGtaIDFromID(VEHICLEID VehicleID);
	void FindLastVehicleID();

	void AssignSpecialParamsToVehicle(VEHICLEID VehicleID, uint8_t byteObjective, uint8_t byteDoorsLocked);
	void LinkToInterior(VEHICLEID VehicleID, uint8_t byteInterior);
	VEHICLEID FindNearestToLocalPlayerPed();

	sa::CVector GetVehiclePos(VEHICLEID VehicleID)
	{
		if (VehicleID >= MAX_VEHICLES) {
			return sa::CVector{0.0f, 0.0f, 0.0f};
		}
		return m_vecPos[VehicleID];
	}

public:
	CVehicle* m_pVehicles[MAX_VEHICLES];
	bool m_bVehicleSlotState[MAX_VEHICLES];
	sa::CVehicle* m_pGTAVehicles[MAX_VEHICLES];
	sa::CVector m_vecPos[MAX_VEHICLES];
	bool m_bIsActive[MAX_VEHICLES];
	bool m_bIsWasted[MAX_VEHICLES];
	bool m_bDeathNotified[MAX_VEHICLES];
	uint32_t m_dwWastedTime[MAX_VEHICLES];
	uint32_t m_dwLastUnoccupiedSyncTick[MAX_VEHICLES];
	int m_iLastVehicleID;
	std::unordered_map<sa::CVehicle*, VEHICLEID> m_mapGtaToId;
};