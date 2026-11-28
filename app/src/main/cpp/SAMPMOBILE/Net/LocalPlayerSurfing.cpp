#include "LocalPlayer.h"
#include "NetGame.h"
#include "RemotePlayer.h"
#include "VehiclePool.h"
#include "Proto/Sync.h"
#include "../Client.h"
#include "../Game/Game.h"
#include "../Game/Camera.h"
#include "../Game/Object.h"
#include "../Game/Pad.h"
#include "../Game/PlayerPed.h"
#include "../Game/Scripting.h"
#include "../Game/Vehicle.h"
#include "../Game/Util.h"
#include "../UI/UI.h"
#include <RakNet/RakClientInterface.h>
#include <RakNet/PacketEnumerations.h>

void CLocalPlayer::ToggleSpectating(bool bToggle)
{
	if (m_bIsSpectating && !m_bWantsAnotherClass) {
		if (!bToggle) {
			Spawn();
		}
	}
	else if (m_bWantsAnotherClass && !bToggle) {
		m_bIsActive = false;
		m_bHasSpawnInfo = false;
		m_bClearedToSpawn = false;
		m_bSpawnDialogShowed = false;
		m_bWantsAnotherClass = false;
	}

	m_bIsSpectating = bToggle;
	if (pGame) {
		pGame->SetHudHidden(Game::HUD_HIDE_SPECTATE, bToggle);
	}
	if (bToggle && m_surfSync.bIsActive) {
		DisableSurfing();
	}
	m_byteSpectateType = SPECTATE_TYPE_NONE;
	m_bSpectateProcessed = false;
	m_SpectateID = 0xFFFFFFFF;
}

void CLocalPlayer::ProcessSpectating()
{
	if (!m_pPlayerPed || !pGame || !pNetGame) return;

	RakNet::BitStream bsSpectatorSync;
	SPECTATOR_SYNC_DATA spSync{};
	RwMatrix matPos{};

	pGame->GetCamera()->GetMatrix(&matPos);

	CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
	CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();

	if (!pPlayerPool || !pVehiclePool) {
		return;
	}

	spSync.vecPos.x = matPos.pos.x;
	spSync.vecPos.y = matPos.pos.y;
	spSync.vecPos.z = matPos.pos.z;

	if ((GetTickCount() - m_dwLastSendSpecTick) > GetOptimumOnFootSendRate()) {
		uint16_t lrAnalog = 0, udAnalog = 0;
		uint8_t exKeys = 0;
		uint16_t wKeys = m_pPlayerPed->GetKeys(&lrAnalog, &udAnalog, &exKeys);
		spSync.lrAnalog = lrAnalog;
		spSync.udAnalog = udAnalog;
		spSync.wKeys = wKeys;

		m_dwLastSendSpecTick = GetTickCount();
		proto::OutboundSpectatorSync pkt{};
		pkt.lrAnalog = spSync.lrAnalog;
		pkt.udAnalog = spSync.udAnalog;
		pkt.keys = spSync.wKeys;
		pkt.position = {spSync.vecPos.x, spSync.vecPos.y, spSync.vecPos.z};
		proto::Writer w(bsSpectatorSync);
		pkt.serialize(w, proto::ProtoVersion::V037);
		pNetGame->GetRakClient()->Send(&bsSpectatorSync, HIGH_PRIORITY, UNRELIABLE, 0);

		if ((GetTickCount() - m_dwLastSendAimSyncTick) > (GetOptimumOnFootSendRate() * 2)) {
			m_dwLastSendAimSyncTick = GetTickCount();
			SendAimSyncData();
		}
	}

	pGame->DisplayHUD(false);

	m_pPlayerPed->SetHealth(100.0f);
	GetPlayerPed()->TeleportTo(spSync.vecPos.x, spSync.vecPos.y, spSync.vecPos.z + 20.0f);

	// handle spectate player left the server or is no longer active (ie Died)
	if (m_byteSpectateType == SPECTATE_TYPE_PLAYER) {
		CRemotePlayer* pSpecPlayer = (m_SpectateID < MAX_PLAYERS) ? pPlayerPool->GetAt((PLAYERID) m_SpectateID) : nullptr;
		if (!pSpecPlayer || !pSpecPlayer->IsActive() || pSpecPlayer->GetState() == PLAYER_STATE_WASTED) {
			m_byteSpectateType = SPECTATE_TYPE_NONE;
			m_bSpectateProcessed = false;
		}
	}

	if (m_bSpectateProcessed) {
		return;
	}

	if (m_byteSpectateType == SPECTATE_TYPE_NONE) {
		GetPlayerPed()->RemoveFromVehicleAndPutAt(0.0f, 0.0f, 10.0f);
		pGame->GetCamera()->SetPosition(50.0f, 50.0f, 50.0f, 0.0f, 0.0f, 0.0f);
		pGame->GetCamera()->LookAtPoint(60.0f, 60.0f, 50.0f, 2);
		m_bSpectateProcessed = true;
	}
	else if (m_byteSpectateType == SPECTATE_TYPE_PLAYER) {
		CRemotePlayer* pSpecPlayer = (m_SpectateID < MAX_PLAYERS) ? pPlayerPool->GetAt((PLAYERID) m_SpectateID) : nullptr;
		if (pSpecPlayer) {
			CPlayerPed* pPlayerPed = pSpecPlayer->GetPlayerPed();
			if (pPlayerPed) {
				ScriptCommand(&camera_on_actor, pPlayerPed->m_gtaId, m_byteSpectateMode, 2);
				m_bSpectateProcessed = true;
			}
		}
	}
	else if (m_byteSpectateType == SPECTATE_TYPE_VEHICLE) {
		CVehicle* pVehicle = (m_SpectateID < MAX_VEHICLES) ? pVehiclePool->GetAt((VEHICLEID) m_SpectateID) : nullptr;
		if (pVehicle) {
			ScriptCommand(&camera_on_vehicle, pVehicle->m_gtaId, m_byteSpectateMode, 2);
			m_bSpectateProcessed = true;
		}
	}
}

void CLocalPlayer::SpectatePlayer(PLAYERID PlayerID)
{
	CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();

	if (pPlayerPool && pPlayerPool->GetSlotState(PlayerID)) {
		CRemotePlayer* pRemotePlayer = pPlayerPool->GetAt(PlayerID);
		if (pRemotePlayer && pRemotePlayer->GetState() != PLAYER_STATE_NONE && pRemotePlayer->GetState() != PLAYER_STATE_WASTED) {
			m_byteSpectateType = SPECTATE_TYPE_PLAYER;
			m_SpectateID = PlayerID;
			m_bSpectateProcessed = false;
		}
	}
}

void CLocalPlayer::SpectateVehicle(VEHICLEID VehicleID)
{
	CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();

	if (pVehiclePool && pVehiclePool->GetSlotState(VehicleID)) {
		m_byteSpectateType = SPECTATE_TYPE_VEHICLE;
		m_SpectateID = VehicleID;
		m_bSpectateProcessed = false;
	}
}

void CLocalPlayer::ProcessSurfing()
{
	if (!m_pPlayerPed || !m_pPlayerPed->m_ped || !pNetGame) {
		DisableSurfing();
		return;
	}

	bool bEnteringOrExitingVeh = false;
	if (m_pPlayerPed->m_ped->m_pIntelligence) {
		void* pTaskMgr = &m_pPlayerPed->m_ped->m_pIntelligence->m_TaskMgr;
		bEnteringOrExitingVeh =
			eng::fn::CTaskManager_FindActiveTaskByType(pTaskMgr, 700) != 0 ||
			eng::fn::CTaskManager_FindActiveTaskByType(pTaskMgr, 701) != 0 ||
			eng::fn::CTaskManager_FindActiveTaskByType(pTaskMgr, 702) != 0;
	}

	if (m_pPlayerPed->IsDead() || m_pPlayerPed->IsInVehicle() || bEnteringOrExitingVeh
			|| LocalPlayerKeys.bKeys[ePadKeys::KEY_JUMP] || m_pPlayerPed->IsJumpTask()) {
		DisableSurfing();
		return;
	}

	const uint32_t nowMs = GetTickCount();
	CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
	CObjectPool* pObjectPool = pNetGame->GetObjectPool();
	auto contactEntity = m_pPlayerPed->GetEntityUnderPlayer();
	if (contactEntity) {
		VEHICLEID vehicleId = pVehiclePool ? pVehiclePool->FindIDFromGtaPtr(reinterpret_cast<sa::CVehicle*>(contactEntity)) : INVALID_VEHICLE_ID;
		if (vehicleId && vehicleId != INVALID_VEHICLE_ID) {
			CVehicle* pVeh = pVehiclePool->GetAt(vehicleId);
			if (pVeh && pVeh->m_pVehicle && pVeh->VerifyInstance()
					&& (pVeh->HasADriver() || pVeh->m_pVehicle->GetModelId() == 569 || pVeh->m_pVehicle->GetModelId() == 570)
					&& pVeh->GetDistanceFromLocalPlayerPed() < 30.0f) {
				if (m_surfSync.bIsActive && m_surfSync.bIsVehicle && m_surfSync.dwSurfVehID == vehicleId) {
					m_surfSync.dwLastContactTick = nowMs;
					return;
				}
				memset(&m_surfSync, 0, sizeof(m_surfSync));
				m_surfSync.vecOffsetPos = sa::CVector{0.0f, 0.0f, 0.0f};
				m_surfSync.dwSurfVehID = vehicleId;
				m_surfSync.pSurfInst = (uintptr_t) pVeh;
				m_surfSync.bIsVehicle = true;
				m_surfSync.dwLastContactTick = nowMs;

				RwMatrix matVeh = pVeh->m_pVehicle->GetMatrix().ToRwMatrix();
				RwMatrix matPed = m_pPlayerPed->m_ped->GetMatrix().ToRwMatrix();
				RwMatrix matOut{};
				RwMatrixInvert(&matOut, &matVeh);
				ProjectMatrix(&m_surfSync.vecOffsetPos, &matOut, &matPed.pos);
				m_surfSync.bIsActive = true;
				return;
			}
		}
		else if (pObjectPool) {
			uint32_t objectId = pObjectPool->FindIDFromGtaPtr(reinterpret_cast<sa::CObject*>(contactEntity));
			if (objectId && objectId != INVALID_OBJECT_ID) {
				CObject* pObject = pObjectPool->GetAt(objectId);
				if (pObject && pObject->m_entity && ((pObject->m_byteMoving & 1) || pObject->AttachedToMovingEntity())) {
					if (m_surfSync.bIsActive && !m_surfSync.bIsVehicle && m_surfSync.dwSurfVehID == (int) objectId) {
						m_surfSync.dwLastContactTick = nowMs;
						return;
					}
					memset(&m_surfSync, 0, sizeof(m_surfSync));
					m_surfSync.vecOffsetPos = sa::CVector{0.0f, 0.0f, 0.0f};
					m_surfSync.bIsVehicle = false;
					m_surfSync.dwSurfVehID = (int) objectId;
					m_surfSync.pSurfInst = (uintptr_t) pObject;
					m_surfSync.dwLastContactTick = nowMs;

					RwMatrix matObj = pObject->m_entity->GetMatrix().ToRwMatrix();
					RwMatrix matPed = m_pPlayerPed->m_ped->GetMatrix().ToRwMatrix();
					RwMatrix matOut{};
					RwMatrixInvert(&matOut, &matObj);
					ProjectMatrix(&m_surfSync.vecOffsetPos, &matOut, &matPed.pos);
					m_surfSync.bIsActive = true;
					return;
				}
			}
		}
	}

	// 2000ms grace period while briefly airborne over bumpy terrain
	if (m_surfSync.bIsActive && (nowMs - m_surfSync.dwLastContactTick) <= 2000) {
		if (m_surfSync.bIsVehicle && pVehiclePool && m_surfSync.dwSurfVehID >= 0 && m_surfSync.dwSurfVehID < MAX_VEHICLES) {
			CVehicle* pVeh = pVehiclePool->GetAt(static_cast<VEHICLEID>(m_surfSync.dwSurfVehID));
			if (pVeh && pVeh->VerifyInstance() && pVeh->GetDistanceFromLocalPlayerPed() < 30.0f) {
				return;
			}
		}
		else if (!m_surfSync.bIsVehicle && pObjectPool && m_surfSync.dwSurfVehID >= 0 && m_surfSync.dwSurfVehID < MAX_OBJECTS) {
			CObject* pObj = pObjectPool->GetAt(static_cast<OBJECTID>(m_surfSync.dwSurfVehID));
			if (pObj && pObj->m_entity && ((pObj->m_byteMoving & 1) || pObj->AttachedToMovingEntity())
					&& pObj->GetDistanceFromLocalPlayerPed() < 30.0f) {
				return;
			}
		}
	}

	m_surfSync.bIsActive = false;
	m_surfSync.dwSurfVehID = INVALID_VEHICLE_ID;
	m_surfSync.pSurfInst = 0;
	m_surfSync.dwLastContactTick = 0;
	m_surfSync.vecOffsetPos = sa::CVector{0.0f, 0.0f, 0.0f};
}

void CLocalPlayer::UpdateSurfing()
{
	static RwMatrix surfInstMatrix;
	static RwMatrix surfPedMatrix;
	static sa::CVector surfInstMoveSpeed;
	static sa::CVector surfInstTurnSpeed;

	if (m_pPlayerPed && m_pPlayerPed->m_ped) {
		if (LocalPlayerKeys.bKeys[ePadKeys::KEY_JUMP] || m_pPlayerPed->IsJumpTask()) {
			return;
		}
		if (m_surfSync.bIsActive) {
			if (m_surfSync.bIsVehicle) {
				// re-resolve the wrapper every frame: the pool slot may have been deleted/reused
				CVehicle* pVeh = nullptr;
				if (pNetGame && pNetGame->GetVehiclePool()
						&& m_surfSync.dwSurfVehID >= 0 && m_surfSync.dwSurfVehID < MAX_VEHICLES) {
					pVeh = pNetGame->GetVehiclePool()->GetAt((VEHICLEID) m_surfSync.dwSurfVehID);
				}
				if (!pVeh || !pVeh->m_pVehicle || !pVeh->VerifyInstance()) {
					DisableSurfing();
					return;
				}
				m_surfSync.pSurfInst = (uintptr_t) pVeh;
				surfInstMatrix = pVeh->m_pVehicle->GetMatrix().ToRwMatrix();
				surfPedMatrix = m_pPlayerPed->m_ped->GetMatrix().ToRwMatrix();
				surfInstMoveSpeed = pVeh->m_pVehicle->GetMoveSpeed();
				surfInstTurnSpeed = pVeh->m_pVehicle->GetTurnSpeed();

				uint16_t lrAnalog;
				uint16_t udAnalog;
				m_pPlayerPed->GetKeys(&lrAnalog, &udAnalog, nullptr, false);

				if (lrAnalog || udAnalog) {
					static RwMatrix matOut;
					RwMatrixInvert(&matOut, &surfInstMatrix);
					ProjectMatrix(&m_surfSync.vecOffsetPos, &matOut, &surfPedMatrix.pos);
				}
				else {
					ProjectMatrix(&surfPedMatrix.pos, &surfInstMatrix, &m_surfSync.vecOffsetPos);

					m_pPlayerPed->m_ped->SetMatrix(surfPedMatrix);
					m_pPlayerPed->UpdateRwMatrixAndFrame();
					sa::CVector vecMoveSpeed;
					vecMoveSpeed = m_pPlayerPed->m_ped->GetMoveSpeed();
					m_pPlayerPed->m_ped->SetVelocity(sa::CVector{surfInstMoveSpeed.x, surfInstMoveSpeed.y, vecMoveSpeed.z});

					sa::CVector vecTurnSpeed = m_pPlayerPed->m_ped->GetTurnSpeed();
					m_pPlayerPed->m_ped->SetTurnSpeed(sa::CVector{vecTurnSpeed.x, vecTurnSpeed.y, surfInstTurnSpeed.z});
				}
			}
			else {
				CObject* pObject = nullptr;
				if (pNetGame && pNetGame->GetObjectPool()
						&& m_surfSync.dwSurfVehID >= 0 && m_surfSync.dwSurfVehID < MAX_OBJECTS) {
					pObject = pNetGame->GetObjectPool()->GetAt((OBJECTID) m_surfSync.dwSurfVehID);
				}
				if (!pObject || !pObject->m_entity || GamePool_Object_GetAt(pObject->m_gtaId) != pObject->m_entity) {
					DisableSurfing();
					return;
				}
				m_surfSync.pSurfInst = (uintptr_t) pObject;
				if ((pObject->m_byteMoving & 1) || pObject->AttachedToMovingEntity()) {
					surfInstMatrix = pObject->m_entity->GetMatrix().ToRwMatrix();
					surfPedMatrix = m_pPlayerPed->m_ped->GetMatrix().ToRwMatrix();
					surfInstMoveSpeed = pObject->m_entity->GetMoveSpeed();
					surfInstTurnSpeed = pObject->m_entity->GetTurnSpeed();

					uint16_t lrAnalog;
					uint16_t udAnalog;
					m_pPlayerPed->GetKeys(&lrAnalog, &udAnalog, nullptr, false);

					if (lrAnalog || udAnalog) {
						static RwMatrix matOut;
						RwMatrixInvert(&matOut, &surfInstMatrix);
						ProjectMatrix(&m_surfSync.vecOffsetPos, &matOut, &surfPedMatrix.pos);
					}
					else {
						ProjectMatrix(&surfPedMatrix.pos, &surfInstMatrix, &m_surfSync.vecOffsetPos);

						m_pPlayerPed->m_ped->SetMatrix(surfPedMatrix);
						m_pPlayerPed->UpdateRwMatrixAndFrame();
						sa::CVector vecMoveSpeed = m_pPlayerPed->m_ped->GetMoveSpeed();
						m_pPlayerPed->m_ped->SetVelocity(sa::CVector{surfInstMoveSpeed.x, surfInstMoveSpeed.y, vecMoveSpeed.z});

						sa::CVector vecTurnSpeed = m_pPlayerPed->m_ped->GetTurnSpeed();
						m_pPlayerPed->m_ped->SetTurnSpeed(sa::CVector{vecTurnSpeed.x, vecTurnSpeed.y, surfInstTurnSpeed.z});
					}
				}
			}
		}
	}
}
