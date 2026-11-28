#include "Damage.h"
#include "Game.h"
#include "Common.h"
#include "Object.h"
#include "PlayerPed.h"
#include "Vehicle.h"
#include "Util.h"
#include "Engine/PatchRegistry.h"
#include "Engine/Symbols.h"
#include "../Addr.h"
#include "../Net/NetGame.h"
#include "../Net/LocalPlayer.h"
#include "../Net/RemotePlayer.h"
#include "../Net/VehiclePool.h"
#include "../Net/ActorPool.h"
#include "../Client.h"
#include <Memory.h>
#include <SA.h>
#include <spdlog/spdlog.h>

#pragma pack(push, 1)
struct CPedDamageResponseCalculator {
	sa::CPed* m_pInflictor;
	float m_fRawDamage;
	int32_t m_eHitZone;
	int32_t m_eWeaponUsed;
	bool m_bJumpedOutOfMovingCar;
	uint8_t pad[3];
};
#pragma pack(pop)
VALIDATE_SIZE(CPedDamageResponseCalculator, (VER_x32 ? 0x14 : 0x18));

PLAYERID FindPlayerIDFromGtaPtr(sa::CEntity* pEntity)
{
	if (pEntity == nullptr || !pNetGame) return INVALID_PLAYER_ID;

	CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
	CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
	if (!pPlayerPool) return INVALID_PLAYER_ID;

	PLAYERID PlayerID = pPlayerPool->FindRemotePlayerIDFromGtaPtr((sa::CPed*) pEntity);
	if (PlayerID != INVALID_PLAYER_ID) return PlayerID;

	if (pVehiclePool) {
		auto* pGtaVehicle = (sa::CVehicle*) pEntity;
		VEHICLEID VehicleID = pVehiclePool->FindIDFromGtaPtr(pGtaVehicle);
		if (VehicleID != INVALID_VEHICLE_ID) {
			if (pGtaVehicle->pDriver) {
				PLAYERID driverId = pPlayerPool->FindRemotePlayerIDFromGtaPtr(pGtaVehicle->pDriver);
				if (driverId != INVALID_PLAYER_ID) {
					return driverId;
				}
			}
			for (PLAYERID i = 0; i < MAX_PLAYERS; i++) {
				CRemotePlayer* pRemotePlayer = pPlayerPool->GetAt(i);
				if (pRemotePlayer && pRemotePlayer->IsActive()
						&& pRemotePlayer->GetState() == PLAYER_STATE_DRIVER
						&& pRemotePlayer->CurrentVehicleID() == VehicleID) {
					return i;
				}
			}
		}
	}

	return INVALID_PLAYER_ID;
}

PLAYERID FindActorIDFromGtaPtr(sa::CPed* pPed)
{
	if (pPed && pNetGame && pNetGame->GetActorPool()) {
		return pNetGame->GetActorPool()->FindIDFromGtaPtr(pPed);
	}

	return INVALID_PLAYER_ID;
}

bool ComputeDamageResponse(CPedDamageResponseCalculator* calculator, sa::CPed* pPed)
{
	sa::CPed* pGamePed = GamePool_FindPlayerPed();
	bool isLocalPed = false;

	if (!pNetGame || !pGamePed) {
		return false;
	}

	sa::CPed* pDamager = calculator->m_pInflictor;
	if (pDamager != pGamePed && (pPed && *(uintptr_t*) pPed == g_saSym->Abs<uintptr_t>(addr::vptr$CCivilianPed))) { /* CCivilianPed */
		return true;
	}

	if (pPed == pGamePed) {
		isLocalPed = true;
	}
	else if (pDamager != pGamePed) {
		return false;
	}

	CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
	CLocalPlayer* pLocalPlayer = pPlayerPool ? pPlayerPool->GetLocalPlayer() : nullptr;
	if (!pLocalPlayer) {
		return false;
	}
	PLAYERID PlayerID = INVALID_PLAYER_ID;

	// open.mp rejects Give/TakeDamage & GiveDamageActor if bodypart is outside [3..9] (Torso..Head).
	const int bodyPart = (calculator->m_eHitZone >= 3 && calculator->m_eHitZone <= 9)
		? calculator->m_eHitZone : 3;

	if (isLocalPed) {
		if (pLocalPlayer->m_surfSync.bIsActive && pLocalPlayer->m_surfSync.dwSurfVehID >= 0) {
			// Matches PC hooks.cpp: while surfing, block both collision damage (54) and
			// any impact coming from the surfed vehicle/object itself.
			if (calculator->m_eWeaponUsed == WEAPON_COLLISION) {
				return true;
			}
			if (pLocalPlayer->m_surfSync.bIsVehicle) {
				CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
				CVehicle* pSurfingVehicle = (pVehiclePool && pLocalPlayer->m_surfSync.dwSurfVehID < MAX_VEHICLES)
						? pVehiclePool->GetAt((VEHICLEID) pLocalPlayer->m_surfSync.dwSurfVehID) : nullptr;
				if (pSurfingVehicle && pSurfingVehicle->m_entity == (sa::CEntity*) pDamager) {
					return true;
				}
			}
			else {
				CObjectPool* pObjectPool = pNetGame->GetObjectPool();
				CObject* pSurfingObject = (pObjectPool && pLocalPlayer->m_surfSync.dwSurfVehID < MAX_OBJECTS)
						? pObjectPool->GetAt((OBJECTID) pLocalPlayer->m_surfSync.dwSurfVehID) : nullptr;
				if (pSurfingObject && pSurfingObject->m_entity == (sa::CEntity*) pDamager) {
					return true;
				}
			}
		}

		PlayerID = FindPlayerIDFromGtaPtr(pDamager);
	}
	else {
		PlayerID = FindPlayerIDFromGtaPtr(pPed);
	}

	if (isLocalPed) {
		pLocalPlayer->SendTakeDamageEvent(PlayerID, calculator->m_fRawDamage, calculator->m_eWeaponUsed, bodyPart);
	}
	else {
		if (PlayerID != INVALID_PLAYER_ID) {
			pLocalPlayer->SendGiveDamageEvent(PlayerID, calculator->m_fRawDamage, calculator->m_eWeaponUsed, bodyPart);
			CRemotePlayer* pRemote = pPlayerPool->GetAt(PlayerID);
			if (pRemote && pRemote->IsNPC()) {
				return true;
			}
		}
		else {
			PLAYERID ActorID = FindActorIDFromGtaPtr(pPed);
			if (ActorID != INVALID_PLAYER_ID) {
				CActorPool* pActorPool = pNetGame->GetActorPool();
				CActor* pActor = pActorPool ? pActorPool->GetAt(ActorID) : nullptr;
				if (pActor && !pActor->m_invulnerable) {
					pLocalPlayer->SendGiveDamageActorEvent(ActorID, calculator->m_fRawDamage, calculator->m_eWeaponUsed, bodyPart);
				}
				return true;
			}
		}
	}

	// Matches PC hooks.cpp:2839-2849 — ALWAYS emit Give/TakeDamage RPCs first
	// (required by weapon-config & server-side damage gamemodes that place all
	// players on the same team), then suppress native GTA HP loss on same-team.
	if (pNetGame->m_netSet && pNetGame->m_netSet->friendlyFire && PlayerID != INVALID_PLAYER_ID) {
		uint8_t byteTeam = pLocalPlayer->m_byteTeam;
		CRemotePlayer* pTargetRemote = pPlayerPool->GetAt(PlayerID);
		if (byteTeam != NO_TEAM && pTargetRemote && pTargetRemote->m_byteTeam == byteTeam) {
			return true;
		}
	}

	return false;
}

DECL_HOOK(void, CPedDamageResponseCalculator_ComputeDamageResponse, CPedDamageResponseCalculator* _this, sa::CPed* pVictim, uintptr_t damageResponse, const bool bTriggerPainAudio)
{
	if (_this != nullptr && pVictim != nullptr) {
		if (ComputeDamageResponse(_this, pVictim)) {
			return;
		}
	}

	return CPedDamageResponseCalculator_ComputeDamageResponse(_this, pVictim, damageResponse, bTriggerPainAudio);
}

// Intercept CWeapon::GenerateDamageEvent BEFORE it blends unsynced local melee hit/knockdown
// animations onto managed CActors (0x706a58) or pushes CEventDamage into CPedIntelligence (0x706bd8).
DECL_HOOK(void, CWeapon_GenerateDamageEvent, sa::CPed* pVictim, sa::CEntity* pCreator, int weaponType, int nDamageFactor, int pedPiece, int direction)
{
	if (pNetGame && pVictim) {
		const PLAYERID actorId = FindActorIDFromGtaPtr(pVictim);
		if (actorId != INVALID_PLAYER_ID) {
			sa::CPed* pLocalPed = GamePool_FindPlayerPed();
			if (pLocalPed && pCreator == reinterpret_cast<sa::CEntity*>(pLocalPed)) {
				CActorPool* pActorPool = pNetGame->GetActorPool();
				CActor* pActor = pActorPool ? pActorPool->GetAt(actorId) : nullptr;
				CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
				CLocalPlayer* pLocalPlayer = pPlayerPool ? pPlayerPool->GetLocalPlayer() : nullptr;
				if (pActor && !pActor->m_invulnerable && pLocalPlayer) {
					const int bodyPart = (pedPiece >= 3 && pedPiece <= 9) ? pedPiece : 3;
					pLocalPlayer->SendGiveDamageActorEvent(actorId, static_cast<float>(nDamageFactor), weaponType, bodyPart);
				}
			}
			return;
		}
	}

	CWeapon_GenerateDamageEvent(pVictim, pCreator, weaponType, nDamageFactor, pedPiece, direction);
}

// T21: EnableVehicleFriendlyFire — block damage to vehicles driven by teammates
DECL_HOOK(void, CVehicle_InflictDamage, sa::CVehicle* _this, sa::CEntity* pDamager, int eWeaponType, float fIntensity, sa::CVector vecCoords)
{
	if (_this && pDamager && pNetGame && pNetGame->m_netSet && pNetGame->m_netSet->vehicleFriendlyFire) {
		CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
		CLocalPlayer* pLocalPlayer = pPlayerPool ? pPlayerPool->GetLocalPlayer() : nullptr;
		if (pLocalPlayer && pLocalPlayer->m_byteTeam != NO_TEAM && pLocalPlayer->GetPlayerPed()) {
			const uint8_t localTeam = pLocalPlayer->m_byteTeam;
			sa::CPed* pLocalGtaPed = pLocalPlayer->GetPlayerPed()->m_ped;

			// Case 1: local player is driving _this, attacker is a teammate (ped or vehicle driven by teammate)
			if (_this->pDriver == pLocalGtaPed) {
				PLAYERID attackerId = FindPlayerIDFromGtaPtr(reinterpret_cast<sa::CPed*>(pDamager));
				if (attackerId == INVALID_PLAYER_ID && pNetGame->GetVehiclePool()) {
					VEHICLEID attVehId = pNetGame->GetVehiclePool()->FindIDFromGtaPtr(reinterpret_cast<sa::CVehicle*>(pDamager));
					if (attVehId != INVALID_VEHICLE_ID) {
						auto* attVeh = reinterpret_cast<sa::CVehicle*>(pDamager);
						if (attVeh->pDriver) {
							attackerId = FindPlayerIDFromGtaPtr(attVeh->pDriver);
						}
					}
				}
				if (attackerId != INVALID_PLAYER_ID) {
					CRemotePlayer* pAttacker = pPlayerPool->GetAt(attackerId);
					if (pAttacker && pAttacker->m_byteTeam == localTeam) {
						return;
					}
				}
			}
			// Case 2: teammate is driving _this, attacker is local player (or local player's vehicle)
			else if (_this->pDriver) {
				PLAYERID driverId = FindPlayerIDFromGtaPtr(_this->pDriver);
				if (driverId != INVALID_PLAYER_ID) {
					CRemotePlayer* pDriver = pPlayerPool->GetAt(driverId);
					if (pDriver && pDriver->m_byteTeam == localTeam) {
						sa::CVehicle* pLocalVeh = pLocalPlayer->GetPlayerPed()->GetGtaVehicle();
						if (pDamager == reinterpret_cast<sa::CEntity*>(pLocalGtaPed) ||
								(pLocalVeh && pDamager == reinterpret_cast<sa::CEntity*>(pLocalVeh))) {
							return;
						}
					}
				}
			}
		}
	}

	CVehicle_InflictDamage(_this, pDamager, eWeaponType, fIntensity, vecCoords);
}

void InstallDamageHooks()
{
	REG_HOOK(Boot, CPedDamageResponseCalculator_ComputeDamageResponse, CPedDamageResponseCalculator_ComputeDamageResponse,
		"Give/take damage reporting");
	REG_HOOK(Boot, CWeapon_GenerateDamageEvent, CWeapon_GenerateDamageEvent,
		"Suppress unsynced local melee hit anims & AI damage events on managed Actors");
	REG_HOOK(Boot, CVehicle_InflictDamage, CVehicle_InflictDamage, "Vehicle friendly-fire damage filter");
}
