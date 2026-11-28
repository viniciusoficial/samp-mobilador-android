#pragma once

#include "NetTypes.h"

#pragma pack(push, 1)
typedef struct _PICKUP {
	int iModel;
	int iType;
	float fX;
	float fY;
	float fZ;
} PICKUP;

typedef struct _DROPPED_WEAPON {
	bool bDroppedWeapon;
	PLAYERID PlayerID;
} DROPPED_WEAPON;
#pragma pack(pop)

class CPickupPool {
public:
	CPickupPool();
	~CPickupPool();

	void Process();

	void New(PICKUP* pPickup, int iPickup);
	void CreateWeapon(int iModel, float fX, float fY, float fZ, int iAmmo, PLAYERID owner);
	void Destroy(int iPickup);
	void DestroyWeaponByOwner(PLAYERID owner);
	void PickedUp(int dwGTAId);
	void RefreshCustomModels();
	void DestroyCustomModelsBeforeUnload();

private:
	int GetIDFromGTAId(int dwGTAId);

public:
	uintptr_t m_dwHnd[MAX_PICKUPS];
	int m_dwGTAId[MAX_PICKUPS];
	int m_effectiveModel[MAX_PICKUPS];
	uint32_t m_cooldownUntilMs[MAX_PICKUPS];
	DROPPED_WEAPON m_droppedWeapon[MAX_PICKUPS];
	PICKUP m_Pickups[MAX_PICKUPS];
	static constexpr int kMaxGtaPickups = 620;
	int m_gtaIdToSampId[kMaxGtaPickups];
	int m_highestActiveSlot = -1;
};