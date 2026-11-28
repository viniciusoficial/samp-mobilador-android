#include "VehiclePool.h"
#include "LocalPlayer.h"
#include "RemotePlayer.h"
#include "NetGame.h"
#include "Proto/RpcOutbound.h"
#include "../Client.h"
#include "../Game/Game.h"
#include "../Game/PlayerPed.h"
#include "../Game/Vehicle.h"
#include "../Game/Util.h"
#include "../UI/UI.h"
#include "../UI/SAMPWidgets/Chat.h"
#include <RakNet/RakClientInterface.h>
#include <RakNet/SAMP/SAMPRPC.h>
#include <spdlog/spdlog.h>


CVehiclePool::CVehiclePool()
{
	m_iLastVehicleID = 0;
	for (int i = 0; i < MAX_VEHICLES; i++) {
		m_pVehicles[i] = nullptr;
		m_bVehicleSlotState[i] = false;
		m_pGTAVehicles[i] = nullptr;
		m_bIsActive[i] = false;
		m_bIsWasted[i] = false;
		m_dwWastedTime[i] = 0;
		m_bDeathNotified[i] = false;
		m_dwLastUnoccupiedSyncTick[i] = 0;
	}
}

CVehiclePool::~CVehiclePool()
{
	for (int i = 0; i < MAX_VEHICLES; i++) {
		Delete(i);
	}
}

void CVehiclePool::Process()
{
	uint32_t dwThisTick = GetTickCount();
	int unoccupiedSent = 0;

	for (VEHICLEID VehicleID = 0; VehicleID <= m_iLastVehicleID; VehicleID++) {
		if (m_bVehicleSlotState[VehicleID] && m_bIsActive[VehicleID]) {
			if (m_bIsWasted[VehicleID] && (dwThisTick - m_dwWastedTime[VehicleID]) > 15000) {
				Delete(VehicleID);
			}
			else {
				CVehicle* pVehicle = m_pVehicles[VehicleID];
				float fDistanceToLocal = pVehicle->GetDistanceFromLocalPlayerPed();

				// Server controls vehicle streaming (stream_distance up to 300m+);
				// do not remove vehicles client-side at 200m.
				if (!pVehicle->IsAdded()) {
					spdlog::debug("Vehicle {} is added", VehicleID);
					pVehicle->Add();
					if (pVehicle->m_bHaveColor) {
						pVehicle->SetColor(pVehicle->m_byteColor1, pVehicle->m_byteColor2);
					}
				}

				CVehicle* pTrailer = pVehicle->GetTrailer();
				if (pTrailer && !pTrailer->IsAdded() && pVehicle->m_pVehicle) {
					RwMatrix matPos = pVehicle->m_pVehicle->GetMatrix().ToRwMatrix();
					pTrailer->TeleportTo(matPos.pos.x, matPos.pos.y, matPos.pos.z);
					pTrailer->Add();
				}

				if (pVehicle->IsDriverLocalPlayer()) {
					if (pVehicle->m_bIsInvulnerable) {
						pVehicle->SetInvulnerable(false);
					}
				}
				else if (!pVehicle->m_bIsInvulnerable) {
					pVehicle->SetInvulnerable(true);
				}

				if (pVehicle->VerifyInstance() && pVehicle->GetHealth() == 0.0f) {
					NotifyVehicleDeath(VehicleID);
					if (!m_bIsWasted[VehicleID]) {
						m_bIsWasted[VehicleID] = true;
						m_dwWastedTime[VehicleID] = GetTickCount();
					}
				}
				else {
					if (pVehicle->GetVehicleSubtype() != VEHICLE_SUBTYPE_BOAT && fDistanceToLocal < 200.0f && pVehicle->HasSunk()) {
						NotifyVehicleDeath(VehicleID);
					}
					else {
						if (unoccupiedSent < 3 && (dwThisTick - m_dwLastUnoccupiedSyncTick[VehicleID]) > 100
								&& pNetGame && pNetGame->GetPlayerPool()) {
							CLocalPlayer* pLocalPlayer = pNetGame->GetPlayerPool()->GetLocalPlayer();
							if (pLocalPlayer && pLocalPlayer->ProcessUnoccupiedSync(VehicleID, pVehicle)) {
								m_dwLastUnoccupiedSyncTick[VehicleID] = dwThisTick;
								++unoccupiedSent;
							}
						}

						if (pNetGame && pNetGame->m_netSet->manualVehicleEngineAndLight) {
							// Match PC vehiclepool.cpp:152-156: when manual mode is ON, all vehicles
							// (including trains) have engine/lights OFF unless explicitly set > 0.
							const bool wantEngine = pVehicle->m_iEngineState > 0;
							if (static_cast<bool>(pVehicle->GetEngineState()) != wantEngine) {
								pVehicle->ApplyEngineStatus(wantEngine);
							}
							const bool wantLight = pVehicle->m_iLightState > 0;
							pVehicle->ApplyLightStatus(wantLight);
						}
						else {
							// Match PC vehiclepool.cpp:159-164: auto-mode (-1) follows driver occupancy
							// WITHOUT mutating m_iEngineState; explicit 0/1 overrides are respected.
							if (pVehicle->m_iEngineState == -1) {
								const bool wantEngine = pVehicle->HasADriver();
								if (static_cast<bool>(pVehicle->GetEngineState()) != wantEngine) {
									pVehicle->ApplyEngineStatus(wantEngine);
								}
							}
							else {
								const bool wantEngine = pVehicle->m_iEngineState != 0;
								if (static_cast<bool>(pVehicle->GetEngineState()) != wantEngine) {
									pVehicle->ApplyEngineStatus(wantEngine);
								}
							}

							if (pVehicle->m_iLightState >= 0) {
								pVehicle->ApplyLightStatus(pVehicle->m_iLightState != 0);
							}
						}

						if (pVehicle->m_pVehicle != m_pGTAVehicles[VehicleID]) {
							m_pGTAVehicles[VehicleID] = pVehicle->m_pVehicle;
						}

						if (pVehicle->m_pTrailer) {
							// GTA may still break the tow link (tractor destroyed/deleted,
							// local-driven tractor, trailer wrecked): drop stale wrapper link
							sa::CVehicle* pTrailerEnt = pVehicle->m_pTrailer->m_pVehicle;
							if (!pTrailerEnt || pTrailerEnt->m_pTowingVehicle != pVehicle->m_pVehicle) {
								pVehicle->m_pTrailer = nullptr;
							}
						}

						pVehicle->UpdateColor();
						pVehicle->ProcessMarkers();
					}
				}
			}
		}
	}
}

void CVehiclePool::ProcessLicensePlates()
{
	for (VEHICLEID id = 0; id < MAX_VEHICLES; ++id) {
		if (m_bVehicleSlotState[id] && m_pVehicles[id]) {
			m_pVehicles[id]->ProcessLicensePlate();
		}
	}
}

bool CVehiclePool::New(NEW_VEHICLE* new_veh)
{
	if (new_veh->VehicleID >= MAX_VEHICLES) return false;

	if (m_pVehicles[new_veh->VehicleID]) {
		if (pUI) pUI->chat()->addDebugMessage("Warning: vehicle %u was not deleted", new_veh->VehicleID);
		Delete(new_veh->VehicleID);
	}

	CVehicle* pNewVehicle = pGame->NewVehicle(
			new_veh->iVehicleType,
			new_veh->vecPos.x,
			new_veh->vecPos.y,
			new_veh->vecPos.z,
			new_veh->fRotation,
			new_veh->byteAddSiren);

	if (!pNewVehicle || !pNewVehicle->m_pVehicle) {
		delete pNewVehicle;
		return false;
	}

	m_pVehicles[new_veh->VehicleID] = pNewVehicle;

	if (new_veh->byteInteriorColor1 != 0xFF || new_veh->byteInteriorColor2 != 0xFF) {
		pNewVehicle->SetColor(new_veh->byteInteriorColor1, new_veh->byteInteriorColor2);
	}

	pNewVehicle->SetHealth(new_veh->fHealth);
	m_pGTAVehicles[new_veh->VehicleID] = pNewVehicle->m_pVehicle;
	m_bVehicleSlotState[new_veh->VehicleID] = true;

	m_vecPos[new_veh->VehicleID].x = new_veh->vecPos.x;
	m_vecPos[new_veh->VehicleID].y = new_veh->vecPos.y;
	m_vecPos[new_veh->VehicleID].z = new_veh->vecPos.z;

	if (new_veh->byteInterior) {
		pNewVehicle->LinkToInterior(new_veh->byteInterior);
	}

	if (new_veh->dwPanelDamageStatus || new_veh->dwDoorDamageStatus || new_veh->byteLightDamageStatus) {
		pNewVehicle->SetDamageStatus(new_veh->dwPanelDamageStatus, new_veh->dwDoorDamageStatus, new_veh->byteLightDamageStatus);
	}

	pNewVehicle->SetTireDamageStatus(new_veh->byteTireDamageStatus);

	m_bIsActive[new_veh->VehicleID] = true;
	m_bIsWasted[new_veh->VehicleID] = false;
	m_dwWastedTime[new_veh->VehicleID] = 0;
	m_bDeathNotified[new_veh->VehicleID] = false;
	m_dwLastUnoccupiedSyncTick[new_veh->VehicleID] = 0;

	if ((int) new_veh->VehicleID > m_iLastVehicleID) {
		m_iLastVehicleID = new_veh->VehicleID;
	}

	//CountVehicles();

	return true;
}

bool CVehiclePool::Delete(VEHICLEID VehicleID)
{
	CVehicle* pVehicle = GetAt(VehicleID);
	if (!pVehicle) return false;

	// Clear every cross-reference before deleting pVehicle so neither
	// wasted-timeout deletion, duplicate New(), nor WorldVehicleRemove can
	// leave dangling m_pCurrentVehicle / m_pTrailer / surfing / map pointers.
	if (pNetGame) {
		if (CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool()) {
			CLocalPlayer* pLocalPlayer = pPlayerPool->GetLocalPlayer();
			if (pLocalPlayer) {
				if (pLocalPlayer->m_surfSync.bIsActive && pLocalPlayer->m_surfSync.bIsVehicle
						&& pLocalPlayer->m_surfSync.dwSurfVehID == (int) VehicleID) {
					pLocalPlayer->DisableSurfing();
				}
				if (pLocalPlayer->IsSpectating()
						&& pLocalPlayer->m_byteSpectateType == SPECTATE_TYPE_VEHICLE
						&& pLocalPlayer->m_SpectateID == VehicleID) {
					pLocalPlayer->m_byteSpectateType = SPECTATE_TYPE_NONE;
					pLocalPlayer->m_bSpectateProcessed = false;
				}
			}

			if (pGame) {
				CPlayerPed* pPlayerPed = pGame->FindPlayerPed();
				if (pPlayerPed && pPlayerPed->m_ped && pPlayerPed->IsInVehicle()) {
					sa::CVehicle* pGtaVehicle = pPlayerPed->GetGtaVehicle();
					if (pGtaVehicle && pGtaVehicle == m_pGTAVehicles[VehicleID]) {
						RwMatrix matPlayer = pPlayerPed->m_ped->GetMatrix().ToRwMatrix();
						pPlayerPed->RemoveFromVehicleAndPutAt(matPlayer.pos.x, matPlayer.pos.y, matPlayer.pos.z);
					}
				}
			}

			for (int i = 0; i < MAX_PLAYERS; i++) {
				CRemotePlayer* pRemotePlayer = pPlayerPool->GetAt(i);
				if (pRemotePlayer && pRemotePlayer->m_pCurrentVehicle == pVehicle) {
					if (pRemotePlayer->GetPlayerPed() && pRemotePlayer->GetState() != PLAYER_STATE_NONE) {
						pRemotePlayer->RemoveFromVehicle();
					}
					pRemotePlayer->m_pCurrentVehicle = nullptr;
				}
			}
		}
	}

	for (int i = 0; i < MAX_VEHICLES; i++) {
		if (i != (int) VehicleID && m_bVehicleSlotState[i] && m_pVehicles[i] && m_pVehicles[i]->m_pTrailer == pVehicle) {
			m_pVehicles[i]->DetachTrailer();
			m_pVehicles[i]->SetTrailer(nullptr);
		}
	}

	if (m_pGTAVehicles[VehicleID]) {
		m_mapGtaToId.erase(m_pGTAVehicles[VehicleID]);
	}
	if (pVehicle->m_pVehicle && pVehicle->m_pVehicle != m_pGTAVehicles[VehicleID]) {
		m_mapGtaToId.erase(pVehicle->m_pVehicle);
	}

	m_bVehicleSlotState[VehicleID] = false;
	m_pVehicles[VehicleID] = nullptr;
	m_pGTAVehicles[VehicleID] = nullptr;
	m_bIsActive[VehicleID] = false;
	m_bIsWasted[VehicleID] = false;
	m_dwWastedTime[VehicleID] = 0;
	m_bDeathNotified[VehicleID] = false;
	m_dwLastUnoccupiedSyncTick[VehicleID] = 0;

	delete pVehicle;

	if ((int) VehicleID == m_iLastVehicleID) {
		FindLastVehicleID();
	}

	return true;
}

void CVehiclePool::FindLastVehicleID()
{
	int iLastVehicleID = 0;
	for (int i = 0; i < MAX_VEHICLES; i++) {
		if (m_bVehicleSlotState[i]) {
			iLastVehicleID = i;
		}
	}
	m_iLastVehicleID = iLastVehicleID;
}

uint32_t CVehiclePool::FindGtaIDFromID(VEHICLEID VehicleID)
{
	if (VehicleID < MAX_VEHICLES && m_pGTAVehicles[VehicleID]) {
		return GamePool_Vehicle_GetIndex(m_pGTAVehicles[VehicleID]);
	}

	return INVALID_VEHICLE_ID;
}

/**
 * @param VehicleID
 */
void CVehiclePool::NotifyVehicleDeath(VEHICLEID VehicleID)
{
	if (!pNetGame || VehicleID >= MAX_VEHICLES || m_bDeathNotified[VehicleID]) return;
	CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
	if (!pPlayerPool) return;
	CLocalPlayer* pLocalPlayer = pPlayerPool->GetLocalPlayer();
	if (!pLocalPlayer) return;
	CVehicle* pVehicle = GetAt(VehicleID);
	if (!pVehicle) return;

	// Match PC vehiclepool.cpp:246-257: only report vehicle death if local player
	// is currently driving it or was its last driver (do not claim unrelated nearby cars).
	const bool bEligible = pVehicle->IsDriverLocalPlayer()
		|| pLocalPlayer->m_LastVehicle == VehicleID;
	if (!bEligible) return;

	m_bDeathNotified[VehicleID] = true;

	RakNet::BitStream bsVehicleDeath;
	proto::Writer writer(bsVehicleDeath);
	proto::VehicleDestroyedReq req{};
	req.vehicleId = VehicleID;
	req.serialize(writer, proto::ProtoVersion::V037);
	pNetGame->GetRakClient()->RPC(&RPC_VehicleDestroyed, &bsVehicleDeath, HIGH_PRIORITY, RELIABLE_SEQUENCED, 0, false, UNASSIGNED_NETWORK_ID, nullptr);
	if (pLocalPlayer->m_LastVehicle == VehicleID) {
		pLocalPlayer->m_LastVehicle = INVALID_VEHICLE_ID;
	}
}

/**
 * @return
 */
VEHICLEID CVehiclePool::FindNearestToLocalPlayerPed()
{
	float fLeastDistance = 10000.0f;
	float fThisDistance;
	VEHICLEID ClosestSoFar = INVALID_VEHICLE_ID;

	VEHICLEID x = 0;
	while (x < MAX_VEHICLES) {
		if (m_bVehicleSlotState[x] && m_bIsActive[x]) {
			fThisDistance = m_pVehicles[x]->GetDistanceFromLocalPlayerPed();
			if (fThisDistance < fLeastDistance) {
				fLeastDistance = fThisDistance;
				ClosestSoFar = x;
			}
		}
		x++;
	}

	return ClosestSoFar;
}

/**
 * @param VehicleID
 * @param byteObjective
 * @param byteDoorsLocked
 */
void CVehiclePool::AssignSpecialParamsToVehicle(VEHICLEID VehicleID, uint8_t byteObjective, uint8_t byteDoorsLocked)
{
	if (VehicleID < MAX_VEHICLES && m_bVehicleSlotState[VehicleID]) {
		CVehicle* pVehicle = m_pVehicles[VehicleID];
		if (pVehicle && m_bIsActive[VehicleID]) {
			pVehicle->m_byteObjectiveVehicle = byteObjective;
			if (byteObjective) {
				pVehicle->m_bSpecialMarkerEnabled = false;
			}

			pVehicle->SetDoorState(byteDoorsLocked);
		}
	}
}

void CVehiclePool::LinkToInterior(VEHICLEID VehicleID, uint8_t byteInterior)
{
	if (VehicleID < MAX_VEHICLES && m_bVehicleSlotState[VehicleID] && m_pVehicles[VehicleID]) {
		m_pVehicles[VehicleID]->LinkToInterior(byteInterior);
	}
}
