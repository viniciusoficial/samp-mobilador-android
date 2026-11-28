#include "BulletSync.h"
#include "Game.h"
#include "Common.h"
#include "PlayerPed.h"
#include "Util.h"
#include "Engine/PatchRegistry.h"
#include "Engine/Symbols.h"
#include "../Addr.h"
#include "../Net/NetGame.h"
#include "../Net/LocalPlayer.h"
#include "../Net/RemotePlayer.h"
#include "../Client.h"
#include <Memory.h>
#include <SA.h>
#include <spdlog/spdlog.h>

static bool g_bEnableLineOfSightHook = false;

RwMatrix* RwMatrixOrthoNormalize(RwMatrix* matrixOut, const RwMatrix* matrixIn)
{
	return eng::fn::RwMatrixOrthoNormalize<RwMatrix*>( matrixOut, matrixIn);
}

void SendBulletSync(sa::CVector* vecOrigin, sa::CVector* vecPos, sa::CEntity** ppEntity)
{
	static BULLET_DATA bulletData;
	memset(&bulletData, 0, sizeof(BULLET_DATA));

	bulletData.vecOrigin.x = vecOrigin->x;
	bulletData.vecOrigin.y = vecOrigin->y;
	bulletData.vecOrigin.z = vecOrigin->z;
	bulletData.vecPos.x = vecPos->x;
	bulletData.vecPos.y = vecPos->y;
	bulletData.vecPos.z = vecPos->z;

	if (ppEntity) {
		static sa::CEntity* pEntity;
		pEntity = *ppEntity;
		if (pEntity) {
			if (pEntity->m_matrix) {
				const int lagComp = pNetGame ? pNetGame->session().lagCompensationMode : 0;
				if (lagComp) {
					bulletData.vecOffset.x = vecPos->x - pEntity->m_matrix->m_pos.x;
					bulletData.vecOffset.y = vecPos->y - pEntity->m_matrix->m_pos.y;
					bulletData.vecOffset.z = vecPos->z - pEntity->m_matrix->m_pos.z;
				}
				else {
					static RwMatrix mat1;
					memset(&mat1, 0, sizeof(mat1));

					static RwMatrix mat2;
					memset(&mat2, 0, sizeof(mat2));

					RwMatrix entityMat = pEntity->m_matrix->ToRwMatrix();
					RwMatrixOrthoNormalize(&mat2, &entityMat);
					RwMatrixInvert(&mat1, &mat2);
					ProjectMatrix(&bulletData.vecOffset, reinterpret_cast<sa::CMatrix*>(&mat1), vecPos);
				}
			}

			bulletData.pEntity = pEntity;
		}
	}

	pGame->FindPlayerPed()->ProcessBulletData(&bulletData);
}

struct ScopedLineOfSightHook {
	bool prev;
	ScopedLineOfSightHook() : prev(g_bEnableLineOfSightHook) { g_bEnableLineOfSightHook = true; }
	~ScopedLineOfSightHook() { g_bEnableLineOfSightHook = prev; }
};

DECL_HOOK(uint32_t, CWeapon_FireInstantHit, sa::CWeapon* _this, sa::CPed* pFiringEntity, sa::CVector* vecOrigin, sa::CVector* muzzlePosn, sa::CEntity* targetEntity, sa::CVector* target, sa::CVector* originForDriveBy, bool arg6, bool muzzle)
{
	ScopedLineOfSightHook losGuard;

	if (pFiringEntity != GamePool_FindPlayerPed()) {
		return muzzle;
	}

	if (pNetGame) {
		CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
		if (pPlayerPool) {
			pPlayerPool->ApplyCollisionChecking();
		}
	}

	if (pGame) {
		CPlayerPed* pPlayerPed = pGame->FindPlayerPed();
		if (pPlayerPed) {
			pPlayerPed->FireInstant();
		}
	}

	if (pNetGame) {
		CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
		if (pPlayerPool) {
			pPlayerPool->ResetCollisionChecking();
		}
	}

	return muzzle;
}

DECL_HOOK(uint32_t, CWeapon_FireInstantHit_full, sa::CWeapon* _this, sa::CPed* pFiringEntity, sa::CVector* vecOrigin, sa::CVector* muzzlePosn, sa::CEntity* targetEntity, sa::CVector* target, sa::CVector* originForDriveBy, bool arg6, bool muzzle)
{
	ScopedLineOfSightHook losGuard;
	return CWeapon_FireInstantHit_full(_this, pFiringEntity, vecOrigin, muzzlePosn, targetEntity, target, originForDriveBy, arg6, muzzle);
}

DECL_HOOK(uint32_t, CWeapon_FireSniper, sa::CWeapon* _this, sa::CPed* pFiringEntity, sa::CEntity* victim, sa::CVector* target)
{
	if (pFiringEntity == GamePool_FindPlayerPed()) {
		CPlayerPool* pPlayerPool = pNetGame ? pNetGame->GetPlayerPool() : nullptr;
		if (pPlayerPool) {
			pPlayerPool->ApplyCollisionChecking();
		}
		if (pGame) {
			CPlayerPed* pPlayerPed = pGame->FindPlayerPed();
			if (pPlayerPed) {
				pPlayerPed->FireInstant();
			}
		}
		if (pPlayerPool) {
			pPlayerPool->ResetCollisionChecking();
		}
	}

	return true;
}

DECL_HOOK(uint32_t, CWorld_ProcessLineOfSight, sa::CVector* vecOrigin, sa::CVector* vecEnd, sa::CColPoint* colPoint, sa::CEntity** ppEntity, bool b1, bool b2, bool b3, bool b4, bool b5, bool b6, bool b7, bool b8)
{
	if (!g_bEnableLineOfSightHook) {
		return CWorld_ProcessLineOfSight(vecOrigin, vecEnd, colPoint, ppEntity, b1, b2, b3, b4, b5, b6, b7, b8);
	}

	static sa::CVector vecPosPlusOffset;
	CPlayerPed* pLocalPlayerPed = pGame ? pGame->FindPlayerPed() : nullptr;

	const int lagComp = pNetGame ? pNetGame->session().lagCompensationMode : 0;

	if (lagComp != 2 && vecOrigin && vecEnd) {
		if (g_pCurrentFiredPed && g_pCurrentFiredPed != pLocalPlayerPed) {
			if (g_pCurrentBulletData && g_pCurrentBulletData->pEntity) {
				if (!IsEntityPlaceable(g_pCurrentBulletData->pEntity) && g_pCurrentBulletData->pEntity->m_matrix) {
					if (lagComp) {
						vecPosPlusOffset.x = g_pCurrentBulletData->pEntity->m_matrix->m_pos.x + g_pCurrentBulletData->vecOffset.x;
						vecPosPlusOffset.y = g_pCurrentBulletData->pEntity->m_matrix->m_pos.y + g_pCurrentBulletData->vecOffset.y;
						vecPosPlusOffset.z = g_pCurrentBulletData->pEntity->m_matrix->m_pos.z + g_pCurrentBulletData->vecOffset.z;
					}
					else {
						ProjectMatrix(&vecPosPlusOffset, g_pCurrentBulletData->pEntity->m_matrix, &g_pCurrentBulletData->vecOffset);
					}

					vecEnd->x = vecPosPlusOffset.x - vecOrigin->x + vecPosPlusOffset.x;
					vecEnd->y = vecPosPlusOffset.y - vecOrigin->y + vecPosPlusOffset.y;
					vecEnd->z = vecPosPlusOffset.z - vecOrigin->z + vecPosPlusOffset.z;
				}
			}
		}
	}

	uint32_t result = CWorld_ProcessLineOfSight(vecOrigin, vecEnd, colPoint, ppEntity, b1, b2, b3, b4, b5, b6, b7, b8);

	if (lagComp == 2) {
		if (g_pCurrentFiredPed && g_pCurrentFiredPed == pLocalPlayerPed && vecOrigin && colPoint) {
			SendBulletSync(vecOrigin, &colPoint->m_vecPoint, ppEntity);
		}
		return result;
	}

	if (g_pCurrentFiredPed) {
		if (g_pCurrentFiredPed != pLocalPlayerPed) {
			if (g_pCurrentBulletData && g_pCurrentBulletData->pEntity == nullptr && ppEntity) {
				sa::CPed* pLocalPed = GamePool_FindPlayerPed();
				if (pLocalPed && (*ppEntity == pLocalPed || (pLocalPed->IsInVehicle() && *ppEntity == pLocalPed->pVehicle))) {
					result = 0;
					*ppEntity = nullptr;
					if (colPoint) {
						colPoint->m_vecPoint.x = 0.0f;
						colPoint->m_vecPoint.y = 0.0f;
						colPoint->m_vecPoint.z = 0.0f;
					}
					return result;
				}
			}
		}
		else if (vecOrigin && colPoint) {
			SendBulletSync(vecOrigin, &colPoint->m_vecPoint, ppEntity);
		}
	}

	return result;
}

DECL_HOOK(sa::CEntity *, CBulletInfo_Update, uintptr_t _this)
{
	ScopedLineOfSightHook losGuard;
	return CBulletInfo_Update(_this);
}

DECL_HOOK(uint32_t, CBulletInfo_AddBullet, sa::CEntity* creator, int weaponType, sa::CVector pos, sa::CVector velocity)
{
	velocity.x *= 50.0f;
	velocity.y *= 50.0f;
	velocity.z *= 50.0f;

	CBulletInfo_AddBullet(creator, weaponType, pos, velocity);

	// CBulletInfo::Update
	eng::fn::CBulletInfo_Update();
	return true;
}

void InstallBulletSyncHooks()
{
	REG_HOOK_BL(Boot, addr::CWeapon_FireInstantHit_branch1, "CWeapon_FireInstantHit.Branch1",
		&CWeapon_FireInstantHit_hook, &CWeapon_FireInstantHit, "Bullet sync fire hook (branch 1)");
	REG_HOOK_BL(Boot, addr::CWeapon_FireInstantHit_branch2, "CWeapon_FireInstantHit.Branch2",
		&CWeapon_FireInstantHit_hook, &CWeapon_FireInstantHit, "Bullet sync fire hook (branch 2)");

	REG_HOOK(Boot, CWeapon_FireInstantHit, CWeapon_FireInstantHit_full, "Lag-comp instant hit full hook");
	REG_HOOK(Boot, CWeapon_FireSniper, CWeapon_FireSniper, "Sniper fire sync");

	REG_HOOK(Boot, CWorld_ProcessLineOfSight, CWorld_ProcessLineOfSight,
		"Bullet raycast hit capture (gated in hook)");

	REG_HOOK(Boot, CBulletInfo_Update, CBulletInfo_Update, "Projectile bullet update hook");
	REG_HOOK(Boot, CBulletInfo_AddBullet, CBulletInfo_AddBullet, "Bullet tracer sync");
}
