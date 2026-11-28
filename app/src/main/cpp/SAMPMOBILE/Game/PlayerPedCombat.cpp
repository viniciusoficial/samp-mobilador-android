#include "PlayerPed.h"
#include "Patches.h"
#include "Game.h"
#include "Common.h"
#include "Pad.h"
#include "Scripting.h"
#include "Util.h"
#include "BulletSync.h"
#include "PlayerContextScope.h"
#include "Engine/Symbols.h"
#include "../Net/NetGame.h"
#include "../Net/LocalPlayer.h"
#include "../Net/RemotePlayer.h"
#include "../Net/VehiclePool.h"
#include "../Net/Proto/Sync.h"
#include "../Client.h"
#include <RakNet/RakClientInterface.h>
#include <RakNet/PacketEnumerations.h>
#include <SA.h>
#include <spdlog/spdlog.h>

sa::CWeapon* CPlayerPed::GetCurrentWeaponSlot()
{
	if (!m_ped || m_ped->m_nActiveWeaponSlot >= 13) return nullptr;
	return &m_ped->m_aWeapons[m_ped->m_nActiveWeaponSlot];
}

uint8_t CPlayerPed::GetCameraMode()
{
	return m_bytePlayerNumber == 0 ? GameGetLocalPlayerCameraMode() : GameGetPlayerCameraMode(m_bytePlayerNumber);
}

float CPlayerPed::GetAimZ()
{
	if (!m_ped || !m_ped->m_pPlayerData) return 0;
	return m_ped->m_pPlayerData->m_fLookPitch;
}

CAMERA_AIM* CPlayerPed::GetCurrentAim()
{
	return GameGetInternalAim();
}

uint8_t CPlayerPed::GetCurrentWeapon()
{
	if (!m_ped || !GamePool_Ped_GetAt(m_gtaId)) {
		return 0;
	}

	// crashes
	/*uint8_t weapon = 0;
	ScriptCommand(&get_actor_armed_weapon, m_gtaId, &weapon);
	return weapon;*/

	sa::CWeapon* pWeaponSlot = GetCurrentWeaponSlot();
	return pWeaponSlot ? pWeaponSlot->dwType : 0;
}

void CPlayerPed::SetKeys(uint16_t lrAnalog, uint16_t udAnalog, uint16_t wKeys)
{
	PAD_KEYS* pad = &RemotePlayerKeys[m_bytePlayerNumber];
	// LEFT/RIGHT
	pad->wKeyLR = lrAnalog;
	// UP/DOWN
	pad->wKeyUD = udAnalog;

	// KEY_ACTION
	pad->bKeys[ePadKeys::KEY_ACTION] = (wKeys & 1);
	wKeys >>= 1;
	// KEY_CROUCH
	pad->bKeys[ePadKeys::KEY_CROUCH] = (wKeys & 1);
	wKeys >>= 1;
	// KEY_FIRE
	pad->bKeys[ePadKeys::KEY_FIRE] = (wKeys & 1);
	wKeys >>= 1;
	// KEY_SPRINT
	pad->bKeys[ePadKeys::KEY_SPRINT] = (wKeys & 1);
	wKeys >>= 1;
	// KEY_SECONDARY_ATTACK
	pad->bKeys[ePadKeys::KEY_SECONDARY_ATTACK] = (wKeys & 1);
	wKeys >>= 1;
	// KEY_JUMP
	pad->bKeys[ePadKeys::KEY_JUMP] = (wKeys & 1);
	if (!pad->bKeys[ePadKeys::KEY_JUMP]) {
		pad->bIgnoreJump = false;
	}
	wKeys >>= 1;
	// KEY_LOOK_RIGHT
	pad->bKeys[ePadKeys::KEY_LOOK_RIGHT] = (wKeys & 1);
	wKeys >>= 1;
	// KEY_HANDBRAKE
	pad->bKeys[ePadKeys::KEY_HANDBRAKE] = (wKeys & 1);
	wKeys >>= 1;
	// KEY_LOOK_LEFT
	pad->bKeys[ePadKeys::KEY_LOOK_LEFT] = (wKeys & 1);
	wKeys >>= 1;
	// KEY_SUBMISSION
	pad->bKeys[ePadKeys::KEY_SUBMISSION] = (wKeys & 1);
	wKeys >>= 1;
	// KEY_WALK
	pad->bKeys[ePadKeys::KEY_WALK] = (wKeys & 1);
	wKeys >>= 1;
	// KEY_ANALOG_UP
	pad->bKeys[ePadKeys::KEY_ANALOG_UP] = (wKeys & 1);
	wKeys >>= 1;
	// KEY_ANALOG_DOWN
	pad->bKeys[ePadKeys::KEY_ANALOG_DOWN] = (wKeys & 1);
	wKeys >>= 1;
	// KEY_ANALOG_LEFT
	pad->bKeys[ePadKeys::KEY_ANALOG_LEFT] = (wKeys & 1);
	wKeys >>= 1;
	// KEY_ANALOG_RIGHT
	pad->bKeys[ePadKeys::KEY_ANALOG_RIGHT] = (wKeys & 1);
}

uint16_t CPlayerPed::GetKeys(uint16_t* lrAnalog, uint16_t* udAnalog, uint8_t* ext, bool clear)
{
	*lrAnalog = LocalPlayerKeys.wKeyLR;
	*udAnalog = LocalPlayerKeys.wKeyUD;

	// ext
	if (ext != nullptr) {
		*ext = 0;
		if (LocalPlayerKeys.bKeys[ePadKeys::KEY_YES]) {
			*ext = 1;
		}
		if (LocalPlayerKeys.bKeys[ePadKeys::KEY_NO]) {
			*ext = 2;
		}
		if (LocalPlayerKeys.bKeys[ePadKeys::KEY_CTRL_BACK]) {
			*ext = 3;
		}
	}

	uint16_t wRet = 0;

	// KEY_ANALOG_RIGHT
	if (LocalPlayerKeys.bKeys[ePadKeys::KEY_ANALOG_RIGHT]) {
		wRet |= 1;
	}
	wRet <<= 1;
	// KEY_ANALOG_LEFT
	if (LocalPlayerKeys.bKeys[ePadKeys::KEY_ANALOG_LEFT]) {
		wRet |= 1;
	}
	wRet <<= 1;
	// KEY_ANALOG_DOWN
	if (LocalPlayerKeys.bKeys[ePadKeys::KEY_ANALOG_DOWN]) {
		wRet |= 1;
	}
	wRet <<= 1;
	// KEY_ANALOG_UP
	if (LocalPlayerKeys.bKeys[ePadKeys::KEY_ANALOG_UP]) {
		wRet |= 1;
	}
	wRet <<= 1;
	// KEY_WALK
	if (LocalPlayerKeys.bKeys[ePadKeys::KEY_WALK]) {
		wRet |= 1;
	}
	wRet <<= 1;
	// KEY_SUBMISSION
	if (LocalPlayerKeys.bKeys[ePadKeys::KEY_SUBMISSION]) {
		wRet |= 1;
	}
	wRet <<= 1;
	// KEY_LOOK_LEFT
	if (LocalPlayerKeys.bKeys[ePadKeys::KEY_LOOK_LEFT]) {
		wRet |= 1;
	}
	wRet <<= 1;
	// KEY_HANDBRAKE
	if (LocalPlayerKeys.bKeys[ePadKeys::KEY_HANDBRAKE]) {
		wRet |= 1;
	}
	wRet <<= 1;
	// KEY_LOOK_RIGHT
	if (LocalPlayerKeys.bKeys[ePadKeys::KEY_LOOK_RIGHT]) {
		wRet |= 1;
	}
	wRet <<= 1;
	// KEY_JUMP
	if (LocalPlayerKeys.bKeys[ePadKeys::KEY_JUMP]) {
		wRet |= 1;
	}
	wRet <<= 1;
	// KEY_SECONDARY_ATTACK
	if (LocalPlayerKeys.bKeys[ePadKeys::KEY_SECONDARY_ATTACK]) {
		wRet |= 1;
	}
	wRet <<= 1;
	// KEY_SPRINT
	if (LocalPlayerKeys.bKeys[ePadKeys::KEY_SPRINT]) {
		wRet |= 1;
	}
	wRet <<= 1;
	// KEY_FIRE
	if (LocalPlayerKeys.bKeys[ePadKeys::KEY_FIRE]) {
		wRet |= 1;
	}
	wRet <<= 1;
	// KEY_CROUCH
	if (LocalPlayerKeys.bKeys[ePadKeys::KEY_CROUCH]) {
		wRet |= 1;
	}
	wRet <<= 1;
	// KEY_ACTION
	if (LocalPlayerKeys.bKeys[ePadKeys::KEY_ACTION]) {
		wRet |= 1;
	}

	if (clear) {
		memset(LocalPlayerKeys.bKeys, 0, ePadKeys::SIZE);
	}

	return wRet;
}

void CPlayerPed::GiveWeapon(int iWeaponId, int iAmmo)
{
	if (m_ped && GamePool_Ped_GetAt(m_gtaId)) {
		int iWeaponModelID = GameGetWeaponModelIDFromWeaponID(iWeaponId);
		if (iWeaponModelID != -1) {
			if (!pGame->IsModelLoaded(iWeaponModelID)) {
				pGame->RequestModel(iWeaponModelID);
				pGame->LoadRequestedModels();
				int waitTries = 0;
				while (!pGame->IsModelLoaded(iWeaponModelID)) {
					usleep(1000);
					if (++waitTries > 2000) {
						spdlog::error("CPlayerPed::GiveWeapon: weapon model {} wouldn't load in time", iWeaponModelID);
						return;
					}
				}
			}

			{
				const uint32_t ctxFlags = (m_bytePlayerNumber != 0)
					? (ScopedPlayerContext::kFocus | ScopedPlayerContext::kSkills)
					: ScopedPlayerContext::kFocusOnly;
				ScopedPlayerContext ctx(m_bytePlayerNumber, ctxFlags);
				// CPed::GiveWeapon
				eng::fn::CPed_GiveWeapon( m_ped, iWeaponId, iAmmo, true);
				SetArmedWeapon(iWeaponId);
			}
		}
	}
}

void CPlayerPed::SetArmedWeapon(uint8_t weapon)
{
	if (m_ped && GamePool_Ped_GetAt(m_gtaId)) {
		const uint32_t ctxFlags = (m_bytePlayerNumber != 0)
			? (ScopedPlayerContext::kFocus | ScopedPlayerContext::kSkills)
			: ScopedPlayerContext::kFocusOnly;
		ScopedPlayerContext ctx(m_bytePlayerNumber, ctxFlags);
		m_ped->SetCurrentWeapon(weapon);
	}
}

void CPlayerPed::SetAmmo(uint8_t byteWeapon, uint16_t wAmmo)
{
	if (m_ped) {
		sa::CWeapon* WeaponSlot = FindWeaponSlot(byteWeapon);
		if (WeaponSlot) {
			if (wAmmo == 0) {
				const bool wasArmed = (GetCurrentWeapon() == byteWeapon);
				WeaponSlot->dwAmmo = 0;
				WeaponSlot->dwAmmoInClip = 0;
				if (wasArmed) {
					SetArmedWeapon(0);
				}
			}
			else {
				WeaponSlot->dwAmmo = static_cast<uint32_t>(wAmmo);
				if (WeaponSlot->dwAmmoInClip > WeaponSlot->dwAmmo) {
					WeaponSlot->dwAmmoInClip = WeaponSlot->dwAmmo;
				}
			}
		}
	}
}

sa::CWeapon* CPlayerPed::FindWeaponSlot(uint8_t byteWeapon)
{
	if (!m_ped) {
		return nullptr;
	}

	for (auto& m_weaponSlot : m_ped->m_aWeapons) {
		if (m_weaponSlot.dwType == byteWeapon) {
			return &m_weaponSlot;
		}
	}

	return nullptr;
}

void CPlayerPed::SetCameraMode(uint8_t byteCameraMode)
{
	GameSetPlayerCameraMode(byteCameraMode, m_bytePlayerNumber);
}

void CPlayerPed::SetCurrentAim(CAMERA_AIM* pAim)
{
	GameStoreRemotePlayerAim(m_bytePlayerNumber, pAim);
}

void CPlayerPed::SetAimZ(float fAimZ)
{
	if (!isnan(fAimZ) && fAimZ <= 100.0f && fAimZ >= -100.0f) {
		if (m_ped && m_ped->m_pPlayerData) {
			m_ped->m_pPlayerData->m_fLookPitch = fAimZ;
		}
	}
}

uint8_t CPlayerPed::FindDeathReasonAndResponsiblePlayer(unsigned short* pPlayerID)
{
	uint8_t byteDeathReason;
	PLAYERID PlayerIDWhoKilled;

	CVehiclePool* pVehiclePool;
	CPlayerPool* pPlayerPool;

	if (pNetGame) {
		pVehiclePool = pNetGame->GetVehiclePool();
		pPlayerPool = pNetGame->GetPlayerPool();
	}
	else {
		*pPlayerID = INVALID_PLAYER_ID;
		return 0xFF;
	}

	if (m_ped) {
		byteDeathReason = m_ped->m_nLastDamagedWeaponType;
		if (byteDeathReason < WEAPON_CAMERA || byteDeathReason == WEAPON_HELIBLADES || byteDeathReason == WEAPON_EXPLOSION) {
			if (m_ped->m_pLastEntityDamage) {
				PlayerIDWhoKilled = pPlayerPool->FindRemotePlayerIDFromGtaPtr(static_cast<sa::CPed*>(m_ped->m_pLastEntityDamage));

				if (PlayerIDWhoKilled != INVALID_PLAYER_ID) {
					*pPlayerID = PlayerIDWhoKilled;
					return byteDeathReason;
				}
				else {
					if (pVehiclePool->FindIDFromGtaPtr(static_cast<sa::CVehicle*>(m_ped->m_pLastEntityDamage)) != INVALID_VEHICLE_ID) {
						sa::CVehicle* pGtaVehicle = static_cast<sa::CVehicle*>(m_ped->m_pLastEntityDamage);
						PlayerIDWhoKilled = pPlayerPool->FindRemotePlayerIDFromGtaPtr(pGtaVehicle->pDriver);

						if (PlayerIDWhoKilled != INVALID_PLAYER_ID) {
							*pPlayerID = PlayerIDWhoKilled;
							return byteDeathReason;
						}
					}
				}
			}
			*pPlayerID = INVALID_PLAYER_ID;
			return 0xFF;
		}
		else if (byteDeathReason == WEAPON_DROWN) {
			*pPlayerID = INVALID_PLAYER_ID;
			return WEAPON_DROWN;
		}
		else if (byteDeathReason == WEAPON_VEHICLE) {
			if (m_ped->m_pLastEntityDamage) {
				if (pVehiclePool->FindIDFromGtaPtr(static_cast<sa::CVehicle*>(m_ped->m_pLastEntityDamage)) != INVALID_VEHICLE_ID) {
					sa::CVehicle* pGtaVehicle = static_cast<sa::CVehicle*>(m_ped->m_pLastEntityDamage);

					PlayerIDWhoKilled = pPlayerPool->FindRemotePlayerIDFromGtaPtr(pGtaVehicle->pDriver);

					if (PlayerIDWhoKilled != INVALID_PLAYER_ID) {
						*pPlayerID = PlayerIDWhoKilled;
						return WEAPON_VEHICLE;
					}
				}
			}
			*pPlayerID = INVALID_PLAYER_ID;
			return 0xFF;
		}
		else if (byteDeathReason == WEAPON_COLLISION) {
			if (m_ped->m_pLastEntityDamage) {
				if (pVehiclePool->FindIDFromGtaPtr(static_cast<sa::CVehicle*>(m_ped->m_pLastEntityDamage)) != INVALID_VEHICLE_ID) {
					sa::CVehicle* pGtaVehicle = static_cast<sa::CVehicle*>(m_ped->m_pLastEntityDamage);

					PlayerIDWhoKilled = pPlayerPool->FindRemotePlayerIDFromGtaPtr(pGtaVehicle->pDriver);

					if (PlayerIDWhoKilled != INVALID_PLAYER_ID) {
						*pPlayerID = PlayerIDWhoKilled;
						return WEAPON_COLLISION;
					}
				}
			}
			*pPlayerID = INVALID_PLAYER_ID;
			return WEAPON_COLLISION;
		}
	}

	*pPlayerID = INVALID_PLAYER_ID;
	return 0xFF;
}

void CPlayerPed::ProcessBulletData(BULLET_DATA* btData)
{
	if (!m_ped || !GamePool_Ped_GetAt(m_gtaId)) {
		return;
	}

	BULLET_SYNC_DATA bulletSyncData;

	if (btData) {
		m_bHaveBulletData = true;
		m_bulletData.pEntity = btData->pEntity;
		m_bulletData.vecOrigin.x = btData->vecOrigin.x;
		m_bulletData.vecOrigin.y = btData->vecOrigin.y;
		m_bulletData.vecOrigin.z = btData->vecOrigin.z;

		m_bulletData.vecPos.x = btData->vecPos.x;
		m_bulletData.vecPos.y = btData->vecPos.y;
		m_bulletData.vecPos.z = btData->vecPos.z;

		m_bulletData.vecOffset.x = btData->vecOffset.x;
		m_bulletData.vecOffset.y = btData->vecOffset.y;
		m_bulletData.vecOffset.z = btData->vecOffset.z;

		uint8_t byteHitType = 0;
		unsigned short InstanceID = 0xFFFF;

		if (m_bytePlayerNumber == 0) {
			if (pNetGame) {
				CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
				if (pPlayerPool) {
					CPlayerPed* pPlayerPed = pPlayerPool->GetLocalPlayer()->GetPlayerPed();
					if (pPlayerPed) {
						memset(&bulletSyncData, 0, sizeof(BULLET_SYNC_DATA));
						if (pPlayerPed->GetCurrentWeapon() != WEAPON_SNIPER || btData->pEntity) {
							if (btData->pEntity) {
								CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
								CObjectPool* pObjectPool = pNetGame->GetObjectPool();

								PLAYERID PlayerID;
								VEHICLEID VehicleID;
								uint16_t ObjectID;

								if (pVehiclePool && pObjectPool) {
									PlayerID = pPlayerPool->FindRemotePlayerIDFromGtaPtr((sa::CPed*) btData->pEntity);
									if (PlayerID == INVALID_PLAYER_ID) {
										VehicleID = pVehiclePool->FindIDFromGtaPtr((sa::CVehicle*) btData->pEntity);
										if (VehicleID == INVALID_VEHICLE_ID) {
											ObjectID = pObjectPool->FindIDFromGtaPtr(btData->pEntity);
											if (ObjectID == INVALID_OBJECT_ID) {
												sa::CVector vecOut;
												vecOut.x = 0.0f;
												vecOut.y = 0.0f;
												vecOut.z = 0.0f;

												if (btData->pEntity->m_matrix) {
													ProjectMatrix(&vecOut, btData->pEntity->m_matrix, &btData->vecOffset);
													btData->vecOffset.x = vecOut.x;
													btData->vecOffset.y = vecOut.y;
													btData->vecOffset.z = vecOut.z;
												}
												else {
													btData->vecOffset.x = btData->pEntity->GetPosition().x + btData->vecOffset.x;
													btData->vecOffset.y = btData->pEntity->GetPosition().y + btData->vecOffset.y;
													btData->vecOffset.z = btData->pEntity->GetPosition().z + btData->vecOffset.z;
												}
											}
											else {
												// object
												byteHitType = 3;
												InstanceID = ObjectID;
											}
										}
										else {
											// vehicle
											byteHitType = 2;
											InstanceID = VehicleID;
										}
									}
									else {
										// player
										byteHitType = 1;
										InstanceID = PlayerID;
									}
								}
							}

							bulletSyncData.vecOrigin.x = btData->vecOrigin.x;
							bulletSyncData.vecOrigin.y = btData->vecOrigin.y;
							bulletSyncData.vecOrigin.z = btData->vecOrigin.z;

							bulletSyncData.vecPos.x = btData->vecPos.x;
							bulletSyncData.vecPos.y = btData->vecPos.y;
							bulletSyncData.vecPos.z = btData->vecPos.z;

							bulletSyncData.vecOffset.x = btData->vecOffset.x;
							bulletSyncData.vecOffset.y = btData->vecOffset.y;
							bulletSyncData.vecOffset.z = btData->vecOffset.z;

							bulletSyncData.targetType = byteHitType;
							bulletSyncData.targetId = InstanceID;
							bulletSyncData.weaponId = pPlayerPed->GetCurrentWeapon();

							RakNet::BitStream bsBullet;
							proto::Writer writer(bsBullet);
							proto::OutboundBulletSync pkt{};
							pkt.targetType = bulletSyncData.targetType;
							pkt.targetId = bulletSyncData.targetId;
							pkt.origin = {bulletSyncData.vecOrigin.x, bulletSyncData.vecOrigin.y, bulletSyncData.vecOrigin.z};
							pkt.pos = {bulletSyncData.vecPos.x, bulletSyncData.vecPos.y, bulletSyncData.vecPos.z};
							pkt.offset = {bulletSyncData.vecOffset.x, bulletSyncData.vecOffset.y, bulletSyncData.vecOffset.z};
							pkt.weaponId = bulletSyncData.weaponId;
							pkt.serialize(writer, proto::ProtoVersion::V037);
							pNetGame->GetRakClient()->Send(&bsBullet, HIGH_PRIORITY, UNRELIABLE_SEQUENCED, 0);
						}
					}
				}
			}
		}
	}
	else {
		m_bHaveBulletData = false;
		memset(&m_bulletData, 0, sizeof(BULLET_DATA));
	}
}

CPlayerPed* g_pCurrentFiredPed = nullptr;
BULLET_DATA* g_pCurrentBulletData = nullptr;

void CPlayerPed::FireInstant()
{
	spdlog::debug("CPlayerPed::FireInstant");

	ScopedPlayerContext ctx(m_bytePlayerNumber, ScopedPlayerContext::kFireInstant, m_bytePlayerNumber != 0);

	g_pCurrentFiredPed = this;

	if (m_bHaveBulletData) {
		g_pCurrentBulletData = &m_bulletData;
	}
	else {
		g_pCurrentBulletData = nullptr;
	}

	sa::CVector vecBonePos{0.0f, 0.0f, 0.0f}, vecOut{0.0f, 0.0f, 0.0f};

	if (m_ped) {
		if (GetCurrentWeapon() == WEAPON_SNIPER) {
			if (sa::CWeapon* pSlot = GetCurrentWeaponSlot()) {
				CWeapon_FireSniper(pSlot, m_ped, nullptr, nullptr);
			}
		}
		else {
			GetWeaponInfoForFire(false, &vecBonePos, &vecOut);
			if (sa::CWeapon* pSlot = GetCurrentWeaponSlot()) {
				CWeapon_FireInstantHit(pSlot, m_ped, &vecBonePos, &vecOut, nullptr, nullptr, nullptr, false, true);
			}
		}
	}

	g_pCurrentFiredPed = nullptr;
	g_pCurrentBulletData = nullptr;
}

void CPlayerPed::GetWeaponInfoForFire(bool bLeftWrist, sa::CVector* vecBonePos, sa::CVector* vecOut)
{
	if (!m_ped || !GamePool_Ped_GetAt(m_gtaId)) {
		return;
	}

	sa::CVector* pFireOffset = GetCurrentWeaponFireOffset();
	if (pFireOffset && vecBonePos && vecOut) {
		vecOut->x = pFireOffset->x;
		vecOut->y = pFireOffset->y;
		vecOut->z = pFireOffset->z;

		int bone_id = 24;
		if (bLeftWrist) {
			bone_id = 34;
		}

		m_ped->GetBonePosition(vecBonePos, bone_id, false);

		vecBonePos->z += pFireOffset->z + 0.15f;
		GetTransformedBonePosition(bone_id, vecOut);
	}
}

uintptr_t GetWeaponInfo(int iWeapon, int iSkill)
{
	// CWeaponInfo::GetWeaponInfo
	return eng::fn::CWeaponInfo_GetWeaponInfo( iWeapon, iSkill);
}

sa::CVector* CPlayerPed::GetCurrentWeaponFireOffset()
{
	if (!m_ped || !GamePool_Ped_GetAt(m_gtaId)) {
		return nullptr;
	}

	sa::CWeapon* pSlot = GetCurrentWeaponSlot();
	if (pSlot) {
		uintptr_t weaponInfo = GetWeaponInfo(pSlot->dwType, 1);
		if (weaponInfo) {
			return reinterpret_cast<sa::CVector*>(weaponInfo + 0x24);
		}
	}
	return nullptr;
}

void CPlayerPed::GetTransformedBonePosition(int iBoneID, sa::CVector* vecOut)
{
	if (!m_ped) {
		return;
	}

	// CPed::GetTransformedBonePosition
	eng::fn::CPed_GetTransformedBonePosition( m_ped, vecOut, iBoneID, false);
}

void CPlayerPed::SetCameraZoomAndAspect(float fExtZoom, float fAspectRatio)
{
	GameSetPlayerCameraExtZoomAndAspect(m_bytePlayerNumber, fExtZoom, fAspectRatio);
}
