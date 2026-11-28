#pragma once

#include <cstdint>

namespace sa {
class CWeapon;
class CPed;
class CVector;
class CEntity;
}
class CPlayerPed;
struct _BULLET_DATA;
using BULLET_DATA = _BULLET_DATA;

extern CPlayerPed* g_pCurrentFiredPed;
extern BULLET_DATA* g_pCurrentBulletData;

extern uint32_t (*CWeapon_FireInstantHit)(sa::CWeapon* _this, sa::CPed* pFiringEntity, sa::CVector* vecOrigin, sa::CVector* muzzlePosn, sa::CEntity* targetEntity, sa::CVector* target, sa::CVector* originForDriveBy, bool arg6, bool muzzle);
extern uint32_t (*CWeapon_FireSniper)(sa::CWeapon* _this, sa::CPed* pFiringEntity, sa::CEntity* victim, sa::CVector* target);

void InstallBulletSyncHooks();
