#include "LocalPlayer.h"
#include "NetGame.h"
#include "ActorPool.h"
#include "RemotePlayer.h"
#include "VehiclePool.h"
#include "Proto/RpcOutbound.h"
#include "Proto/Sync.h"
#include "../Client.h"
#include "../Game/Game.h"
#include "../Game/Aimstuff.h"
#include "../Game/Camera.h"
#include "../Game/Common.h"
#include "../Game/Object.h"
#include "../Game/Pad.h"
#include "../Game/PlayerPed.h"
#include "../Game/Scripting.h"
#include "../Game/Vehicle.h"
#include "../Game/Util.h"
#include <RakNet/RakClientInterface.h>
#include <RakNet/SAMP/SAMPRPC.h>

extern uint32_t dwEnterVehTimeElasped;

void CLocalPlayer::FindAimedTargets(PLAYERID& outPlayerId, PLAYERID& outActorId)
{
	outPlayerId = INVALID_PLAYER_ID;
	outActorId = INVALID_PLAYER_ID;

	if (!m_pPlayerPed || !m_pPlayerPed->m_ped || !pNetGame) {
		return;
	}

	CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
	CActorPool* pActorPool = pNetGame->GetActorPool();
	if (!pPlayerPool && !pActorPool) {
		return;
	}

	auto resolvePedTarget = [&](sa::CEntity* ent) -> bool {
		if (!ent || ent == m_pPlayerPed->m_ped) return false;
		if (pPlayerPool) {
			const PLAYERID pid = pPlayerPool->FindRemotePlayerIDFromGtaPtr(reinterpret_cast<sa::CPed*>(ent));
			if (pid != INVALID_PLAYER_ID) {
				outPlayerId = pid;
				return true;
			}
		}
		if (pActorPool) {
			const PLAYERID aid = pActorPool->FindIDFromGtaPtr(reinterpret_cast<sa::CPed*>(ent));
			if (aid != INVALID_PLAYER_ID) {
				outActorId = aid;
				return true;
			}
		}
		return false;
	};

	// 1) Engine lock-on target if available (auto-aim / melee lock)
	if (m_pPlayerPed->m_ped->m_pEntLockOnTarget && resolvePedTarget(m_pPlayerPed->m_ped->m_pEntLockOnTarget)) {
		return;
	}

	// 2) Free-aim camera ray when actively aiming a weapon
	const uint8_t camMode = m_pPlayerPed->GetCameraMode();
	const bool aimCam = (camMode == 7 || camMode == 8 || camMode == 46 || camMode == 51 ||
		camMode == 53 || camMode == 55 || camMode == 65);
	const bool isAiming = aimCam || m_pPlayerPed->m_ped->m_nPedFlags.bIsAimingGun ||
		(!m_pPlayerPed->IsInVehicle() && LocalPlayerKeys.bKeys[ePadKeys::KEY_HANDBRAKE]);
	if (!isAiming) {
		return;
	}

	CAMERA_AIM* pCam = GameGetInternalAim();
	if (!pCam) {
		return;
	}

	const float lenSq = pCam->f1x * pCam->f1x + pCam->f1y * pCam->f1y + pCam->f1z * pCam->f1z;
	if (lenSq < 1e-6f) {
		return;
	}
	const float invLen = 1.0f / std::sqrt(lenSq);
	const float dx = pCam->f1x * invLen;
	const float dy = pCam->f1y * invLen;
	const float dz = pCam->f1z * invLen;

	constexpr float kMaxRange = 70.0f;
	sa::CVector vecStart{pCam->pos1x + dx * 1.2f, pCam->pos1y + dy * 1.2f, pCam->pos1z + dz * 1.2f};
	sa::CVector vecEnd{pCam->pos1x + dx * kMaxRange, pCam->pos1y + dy * kMaxRange, pCam->pos1z + dz * kMaxRange};

	sa::CColPoint colPoint{};
	sa::CEntity* hitEntity = nullptr;
	ProcessLineOfSight(&vecStart, &vecEnd, &colPoint, &hitEntity,
		true, true, true, true, false, false, false, false);
	if (hitEntity && resolvePedTarget(hitEntity)) {
		return;
	}

	// 3) Crosshair cone fallback with building/object LOS check (covers free-aim edge misses)
	float bestT = kMaxRange;
	constexpr float kMaxRadiusSq = 0.70f * 0.70f;

	auto testPedCandidate = [&](sa::CPed* ped, PLAYERID id, bool isActor) {
		if (!ped || ped == m_pPlayerPed->m_ped) return;
		const sa::CVector pos = ped->GetPosition();
		const float rx = pos.x - pCam->pos1x;
		const float ry = pos.y - pCam->pos1y;
		const float rz = (pos.z + 0.2f) - pCam->pos1z;
		const float t = rx * dx + ry * dy + rz * dz;
		if (t < 1.0f || t >= bestT) return;
		const float px = rx - t * dx;
		const float py = ry - t * dy;
		const float pz = rz - t * dz;
		if (px * px + py * py + pz * pz > kMaxRadiusSq) return;

		int clear = ScriptCommand(&get_line_of_sight,
			pCam->pos1x, pCam->pos1y, pCam->pos1z,
			pos.x, pos.y, pos.z + 0.2f,
			1, 0, 0, 1, 0);
		if (!clear) return;

		bestT = t;
		if (isActor) {
			outPlayerId = INVALID_PLAYER_ID;
			outActorId = id;
		}
		else {
			outPlayerId = id;
			outActorId = INVALID_PLAYER_ID;
		}
	};

	if (pPlayerPool) {
		for (PLAYERID i = 0; i <= pPlayerPool->m_iLastPlayerID && i < MAX_PLAYERS; ++i) {
			if (!pPlayerPool->GetSlotState(i)) continue;
			CRemotePlayer* pRemote = pPlayerPool->GetAt(i);
			if (!pRemote || !pRemote->IsActive()) continue;
			CPlayerPed* pRemotePed = pRemote->GetPlayerPed();
			if (!pRemotePed || !pRemotePed->m_ped || !pRemotePed->IsAdded()) continue;
			testPedCandidate(pRemotePed->m_ped, i, false);
		}
	}

	if (pActorPool) {
		for (PLAYERID i = 0; i < MAX_ACTORS; ++i) {
			CActor* pActor = pActorPool->GetAt(i);
			if (!pActor || !pActor->m_actor) continue;
			testPedCandidate(pActor->m_actor, i, true);
		}
	}
}

void CLocalPlayer::CheckWeapons()
{
	if (!m_pPlayerPed || !m_pPlayerPed->m_ped || !pNetGame || !pNetGame->GetRakClient()) {
		return;
	}

	uint8_t byteCurWeapon = m_pPlayerPed->GetCurrentWeapon();
	PLAYERID TargetPlayerID = INVALID_PLAYER_ID;
	PLAYERID TargetActorID = INVALID_PLAYER_ID;
	FindAimedTargets(TargetPlayerID, TargetActorID);

	bool bChanged = false;
	if (TargetPlayerID != m_lastTargetPlayerId || TargetActorID != m_lastTargetActorId) {
		m_lastTargetPlayerId = TargetPlayerID;
		m_lastTargetActorId = TargetActorID;
		bChanged = true;
	}
	else if (byteCurWeapon == m_byteCurrentWeapon) {
		if (GetTickCount() - m_dwLastWeaponsUpdateTick < 100) {
			return;
		}
	}

	if (byteCurWeapon != m_byteCurrentWeapon) {
		m_byteCurrentWeapon = byteCurWeapon;
	}

	proto::OutboundWeaponsUpdate pkt{};
	pkt.targetPlayerId = m_lastTargetPlayerId;
	pkt.targetActorId = m_lastTargetActorId;

	for (int i = 0; i < 13; i++) {
		if (m_byteLastWeapon[i] != m_pPlayerPed->m_ped->m_aWeapons[i].dwType || m_dwLastAmmo[i] != m_pPlayerPed->m_ped->m_aWeapons[i].dwAmmo) {
			m_byteLastWeapon[i] = m_pPlayerPed->m_ped->m_aWeapons[i].dwType;
			m_dwLastAmmo[i] = m_pPlayerPed->m_ped->m_aWeapons[i].dwAmmo;
			pkt.slots.push_back({static_cast<uint8_t>(i), m_byteLastWeapon[i], static_cast<uint16_t>(m_dwLastAmmo[i])});
			bChanged = true;
		}
	}

	if (bChanged) {
		RakNet::BitStream bsSend;
		proto::Writer w(bsSend);
		pkt.serialize(w, proto::ProtoVersion::V037);
		m_dwLastWeaponsUpdateTick = GetTickCount();
		pNetGame->GetRakClient()->Send(&bsSend, PacketPriority::HIGH_PRIORITY, UNRELIABLE, 0);
	}
}

void CLocalPlayer::UpdateCameraTarget()
{
	if (!pNetGame || !pNetGame->GetRakClient()) {
		return;
	}

	if (!pNetGame->session().cameraTargetEnabled) {
		m_wCameraTargets[0] = INVALID_OBJECT_ID;
		m_wCameraTargets[1] = INVALID_VEHICLE_ID;
		m_wCameraTargets[2] = INVALID_PLAYER_ID;
		m_wCameraTargets[3] = 0xFFFF;
		return;
	}

	const uint32_t now = GetTickCount();
	if (now - m_dwLastCameraTargetTick <= 100) {
		return;
	}
	m_dwLastCameraTargetTick = now;

	uint16_t targets[4] = {
		INVALID_OBJECT_ID,
		INVALID_VEHICLE_ID,
		INVALID_PLAYER_ID,
		0xFFFF
	};

	CAMERA_AIM* pCam = GameGetInternalAim();
	if (pCam) {
		const float fLenSq = pCam->f1x * pCam->f1x + pCam->f1y * pCam->f1y + pCam->f1z * pCam->f1z;
		if (fLenSq > 1e-6f) {
			const float invLen = 1.0f / std::sqrt(fLenSq);
			const float dx = pCam->f1x * invLen;
			const float dy = pCam->f1y * invLen;
			const float dz = pCam->f1z * invLen;

			// PC SA-MP starts ray 1m ahead of camera position along front vector
			const sa::CVector origin{pCam->pos1x + dx, pCam->pos1y + dy, pCam->pos1z + dz};

			auto clipRay = [&](float maxDist, bool checkVeh, bool checkObj) -> float {
				sa::CVector rayEnd{origin.x + dx * maxDist, origin.y + dy * maxDist, origin.z + dz * maxDist};
				sa::CColPoint col{};
				sa::CEntity* hitEnt = nullptr;
				ProcessLineOfSight(const_cast<sa::CVector*>(&origin), &rayEnd, &col, &hitEnt,
					true, checkVeh, 0, checkObj, false, false, false, false);
				if (hitEnt) {
					const float hx = col.m_vecPoint.x - origin.x;
					const float hy = col.m_vecPoint.y - origin.y;
					const float hz = col.m_vecPoint.z - origin.z;
					const float d = std::sqrt(hx * hx + hy * hy + hz * hz);
					if (std::isfinite(d) && d < maxDist) {
						return d;
					}
				}
				return maxDist;
			};

			auto rayPointMetrics = [&](const sa::CVector& pos, float maxT, float& outT, float& outPerpSq) -> bool {
				const float rx = pos.x - origin.x;
				const float ry = pos.y - origin.y;
				const float rz = pos.z - origin.z;
				const float t = rx * dx + ry * dy + rz * dz;
				if (t < 0.0f || t > maxT) return false;
				const float px = rx - dx * t;
				const float py = ry - dy * t;
				const float pz = rz - dz * t;
				outT = t;
				outPerpSq = px * px + py * py + pz * pz;
				return true;
			};

			// Stage 0: Object (max 40m, clipped by buildings)
			if (CObjectPool* pObjPool = pNetGame->GetObjectPool()) {
				const float maxObjDist = clipRay(40.0f, false, false);
				sa::CVector rayEnd{origin.x + dx * maxObjDist, origin.y + dy * maxObjDist, origin.z + dz * maxObjDist};
				sa::CColPoint col{};
				sa::CEntity* hitEnt = nullptr;
				ProcessLineOfSight(const_cast<sa::CVector*>(&origin), &rayEnd, &col, &hitEnt,
					false, false, 0, true, false, false, false, false);
				if (hitEnt) {
					const OBJECTID hitId = pObjPool->FindIDFromGtaPtr(hitEnt);
					if (hitId != INVALID_OBJECT_ID) {
						targets[0] = hitId;
					}
				}
				if (targets[0] == INVALID_OBJECT_ID) {
					float bestT = maxObjDist;
					for (OBJECTID id = 0; id < MAX_OBJECTS; ++id) {
						CObject* pObj = pObjPool->GetAt(id);
						if (!pObj || !pObj->m_gtaId) continue;
						sa::CEntity* liveEnt = GamePool_Object_GetAt(pObj->m_gtaId);
						if (!liveEnt || liveEnt != pObj->m_entity) continue;
						const RwMatrix mat = liveEnt->GetMatrix().ToRwMatrix();
						float rad = GetModelColSphereRadius(pObj->GetModelIndex());
						if (!std::isfinite(rad) || rad < 0.65f) rad = 0.65f;
						if (rad > 6.0f) rad = 6.0f;
						float t = 0.0f, perpSq = 0.0f;
						if (rayPointMetrics({mat.pos.x, mat.pos.y, mat.pos.z}, bestT, t, perpSq) && perpSq <= rad * rad) {
							bestT = t;
							targets[0] = id;
						}
					}
				}
			}

			// Stage 1: Vehicle (max 40m, clipped by buildings + objects)
			if (CVehiclePool* pVehPool = pNetGame->GetVehiclePool()) {
				const float maxVehDist = clipRay(40.0f, false, true);
				sa::CVector rayEnd{origin.x + dx * maxVehDist, origin.y + dy * maxVehDist, origin.z + dz * maxVehDist};
				sa::CColPoint col{};
				sa::CEntity* hitEnt = nullptr;
				ProcessLineOfSight(const_cast<sa::CVector*>(&origin), &rayEnd, &col, &hitEnt,
					false, true, 0, false, false, false, false, false);
				if (hitEnt) {
					const VEHICLEID hitId = pVehPool->FindIDFromGtaPtr(reinterpret_cast<sa::CVehicle*>(hitEnt));
					if (hitId != INVALID_VEHICLE_ID) {
						targets[1] = hitId;
					}
				}
				if (targets[1] == INVALID_VEHICLE_ID) {
					float bestT = maxVehDist;
					for (VEHICLEID id = 0; id < MAX_VEHICLES; ++id) {
						CVehicle* pVeh = pVehPool->GetAt(id);
						if (!pVeh || !pVeh->m_pVehicle || !pVeh->VerifyInstance()) continue;
						const RwMatrix mat = pVeh->m_pVehicle->GetMatrix().ToRwMatrix();
						float rad = GetModelColSphereRadius(pVeh->m_pVehicle->m_nModelIndex);
						if (!std::isfinite(rad) || rad < 1.5f) rad = 1.5f;
						if (rad > 6.0f) rad = 6.0f;
						float t = 0.0f, perpSq = 0.0f;
						if (rayPointMetrics({mat.pos.x, mat.pos.y, mat.pos.z}, bestT, t, perpSq) && perpSq <= rad * rad) {
							bestT = t;
							targets[1] = id;
						}
					}
				}
			}

			// Stage 2: Player & Actor (max 20m, clipped by buildings + vehicles + objects)
			const float maxPedDist = clipRay(20.0f, true, true);
			constexpr float kPedRadiusSq = 0.85f * 0.85f;

			if (CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool()) {
				float bestT = maxPedDist;
				for (PLAYERID id = 0; id < MAX_PLAYERS; ++id) {
					CRemotePlayer* pRemote = pPlayerPool->GetAt(id);
					if (!pRemote || !pRemote->IsActive()) continue;
					CPlayerPed* pPed = pRemote->GetPlayerPed();
					if (!pPed || !pPed->m_ped || !pPed->IsAdded()) continue;
					const sa::CVector pos = pPed->m_ped->GetPosition();
					float t = 0.0f, perpSq = 0.0f;
					if (rayPointMetrics({pos.x, pos.y, pos.z + 0.15f}, bestT, t, perpSq) && perpSq <= kPedRadiusSq) {
						bestT = t;
						targets[2] = id;
					}
				}
			}

			if (CActorPool* pActorPool = pNetGame->GetActorPool()) {
				float bestT = maxPedDist;
				for (uint16_t id = 0; id < MAX_ACTORS; ++id) {
					CActor* pActor = pActorPool->GetAt(id);
					if (!pActor || !pActor->m_actor || !pActor->IsAdded()) continue;
					const sa::CVector pos = pActor->m_actor->GetPosition();
					float t = 0.0f, perpSq = 0.0f;
					if (rayPointMetrics({pos.x, pos.y, pos.z + 0.15f}, bestT, t, perpSq) && perpSq <= kPedRadiusSq) {
						bestT = t;
						targets[3] = id;
					}
				}
			}
		}
	}

	if (targets[0] != m_wCameraTargets[0] || targets[1] != m_wCameraTargets[1] ||
		targets[2] != m_wCameraTargets[2] || targets[3] != m_wCameraTargets[3]) {
		proto::CameraTargetReply reply{};
		reply.objectId = targets[0];
		reply.vehicleId = targets[1];
		reply.playerId = targets[2];
		reply.actorId = targets[3];

		RakNet::BitStream bs;
		proto::Writer writer(bs);
		reply.serialize(writer, proto::ProtoVersion::V037);
		pNetGame->GetRakClient()->RPC(&RPC_UpdateCameraTarget, &bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false, UNASSIGNED_NETWORK_ID, nullptr);

		for (int i = 0; i < 4; ++i) {
			m_wCameraTargets[i] = targets[i];
		}
	}
}

bool CLocalPlayer::EnterVehicleAsPassenger()
{
	if (!pNetGame || !m_pPlayerPed || !m_pPlayerPed->m_ped
			|| m_pPlayerPed->IsInVehicle() || m_pPlayerPed->IsCuffed() || m_pPlayerPed->IsCarry()) {
		return false;
	}

	CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
	if (pVehiclePool) {
		VEHICLEID ClosetVehicleID = pVehiclePool->FindNearestToLocalPlayerPed();
		if (ClosetVehicleID < MAX_VEHICLES && pVehiclePool->GetSlotState(ClosetVehicleID)) {
			CVehicle* pVehicle = pVehiclePool->GetAt(ClosetVehicleID);
			if (pVehicle && pVehicle->m_pVehicle && !pVehicle->GetDoorState() && pVehicle->GetDistanceFromLocalPlayerPed() < 8.0f) {
				// Prevent 0x05CA (seat = -1) from falling back to checkSeatFree = false and
				// slow-dragging an existing passenger out when all passenger seats are occupied.
				const uint16_t modelId = pVehicle->m_pVehicle->m_nModelIndex;
				if (modelId != TRAIN_PASSENGER) {
					const int maxSeats = std::min<int>(static_cast<int>(pVehicle->m_pVehicle->m_nMaxPassengers), 8);
					bool bHasFreeSeat = false;
					for (int s = 0; s < maxSeats; ++s) {
						if (pVehicle->m_pVehicle->m_apPassengers[s] == nullptr) {
							bHasFreeSeat = true;
							break;
						}
					}
					if (!bHasFreeSeat) {
						return false;
					}
				}

				if (m_pPlayerPed->GetCurrentWeapon() == WEAPON_PARACHUTE) {
					m_pPlayerPed->SetArmedWeapon(0);
				}

				m_pPlayerPed->EnterVehicle(pVehicle->m_gtaId, true);
				SendEnterVehicleNotification(ClosetVehicleID, true);
				return true;
			}
		}
	}

	return false;
}

bool CLocalPlayer::HandlePassengerEntry()
{
	return false;
}

void CLocalPlayer::UpdateVehicleDamage(VEHICLEID vehicleID)
{
	if (!pNetGame || vehicleID == INVALID_VEHICLE_ID) {
		return;
	}

	CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
	if (!pVehiclePool) {
		return;
	}

	CVehicle* pVehicle = pVehiclePool->GetAt(vehicleID);
	if (!pVehicle) {
		m_DamageVehicle = INVALID_VEHICLE_ID;
		return;
	}

	uint32_t dwPanels, dwDoors;
	uint8_t byteLights, byteTires;
	pVehicle->GetDamageStatus(&dwPanels, &dwDoors, &byteLights, &byteTires);

	if (m_DamageVehicle != vehicleID) {
		m_DamageVehicle = vehicleID;
		m_dwDamagePanels = dwPanels;
		m_dwDamageDoors = dwDoors;
		m_byteDamageLights = byteLights;
		m_byteDamageTires = byteTires;
		return;
	}

	if (dwPanels == m_dwDamagePanels && dwDoors == m_dwDamageDoors
			&& byteLights == m_byteDamageLights && byteTires == m_byteDamageTires) {
		return;
	}

	m_dwDamagePanels = dwPanels;
	m_dwDamageDoors = dwDoors;
	m_byteDamageLights = byteLights;
	m_byteDamageTires = byteTires;

	RakNet::BitStream bsDamage;
	proto::DamageVehicleReq req{};
	req.vehicleId = vehicleID;
	req.panelDamageStatus = dwPanels;
	req.doorDamageStatus = dwDoors;
	req.lightDamageStatus = byteLights;
	req.tireDamageStatus = byteTires;
	proto::Writer w(bsDamage);
	req.serialize(w, proto::ProtoVersion::V037);
	pNetGame->GetRakClient()->RPC(&RPC_DamageVehicle, &bsDamage, HIGH_PRIORITY, RELIABLE_SEQUENCED, 0, false, UNASSIGNED_NETWORK_ID, nullptr);
}

void CLocalPlayer::SyncVehicleDamageCache(VEHICLEID vehicleID, uint32_t panels, uint32_t doors, uint8_t lights, uint8_t tires)
{
	if (m_DamageVehicle == vehicleID) {
		m_dwDamagePanels = panels;
		m_dwDamageDoors = doors;
		m_byteDamageLights = lights;
		m_byteDamageTires = tires;
	}
}

void CLocalPlayer::SendEnterVehicleNotification(VEHICLEID VehicleID, bool bPassenger)
{
	// open.mp drops RPC_EnterVehicle unless peer.getState() == PlayerState_OnFoot.
	// When re-entering immediately after exiting a vehicle before CLocalPlayer::Process()
	// has emitted an OnFootSync packet, flush one OnFootSync first so the server transitions state.
	if (m_CurrentVehicle != 0xFFFF && m_pPlayerPed && !m_pPlayerPed->IsInVehicle()) {
		m_LastVehicle = m_CurrentVehicle;
		m_CurrentVehicle = 0xFFFF;
		SendOnFootFullSyncData();
	}

	CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
	if (pVehiclePool) {
		CVehicle* pVehicle = pVehiclePool->GetAt(VehicleID);
		if (pVehicle) {
			if (pVehicle->IsATrainPart()) {
				ScriptCommand(&camera_on_vehicle, pVehicle->m_gtaId, 3, 2);
				dwEnterVehTimeElasped = GetTickCount();
			}
		}
	}

	RakNet::BitStream bsSend;
	proto::EnterVehicleReq req{};
	req.vehicleId = VehicleID;
	req.passenger = static_cast<uint8_t>(bPassenger);
	proto::Writer w(bsSend);
	req.serialize(w, proto::ProtoVersion::V037);
	pNetGame->GetRakClient()->RPC(&RPC_EnterVehicle, &bsSend, HIGH_PRIORITY, RELIABLE_SEQUENCED, 0, false, UNASSIGNED_NETWORK_ID, nullptr);
}

void CLocalPlayer::SendExitVehicleNotification(VEHICLEID VehicleID)
{
	RakNet::BitStream bsSend;

	CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
	if (!pVehiclePool) {
		return;
	}
	CVehicle* pVehicle = pVehiclePool->GetAt(VehicleID);
	if (pVehicle) {
		if (!m_pPlayerPed->IsAPassenger()) {
			m_LastVehicle = VehicleID;
		}

		if (pVehicle->IsATrainPart()) {
			pGame->GetCamera()->SetBehindPlayer();
		}

		if (!pVehicle->IsRCVehicle()) {
			proto::ExitVehicleReq req{};
			req.vehicleId = VehicleID;
			proto::Writer w(bsSend);
			req.serialize(w, proto::ProtoVersion::V037);
			pNetGame->GetRakClient()->RPC(&RPC_ExitVehicle, &bsSend, HIGH_PRIORITY, RELIABLE_SEQUENCED, 0, false, UNASSIGNED_NETWORK_ID, nullptr);
		}
	}
}

void CLocalPlayer::SendTakeDamageEvent(PLAYERID PlayerID, float fDamageFactor, int weaponType, int pedPieceType)
{
	spdlog::info("SendTakeDamageEvent: PlayerID: {}, fDamageFactor: {}, weaponType: {}, pedPieceType: {}", PlayerID, fDamageFactor, weaponType, pedPieceType);

	fDamageFactor *= 0.33f;

	RakNet::BitStream bsSend;
	proto::GiveTakeDamageReq req{};
	req.taking = true;
	req.playerId = PlayerID;
	req.damage = fDamageFactor;
	req.weaponId = weaponType;
	req.bodyPart = pedPieceType;
	proto::Writer w(bsSend);
	req.serialize(w, proto::ProtoVersion::V037);

	pNetGame->GetRakClient()->RPC(&RPC_PlayerGiveTakeDamage, &bsSend, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false, UNASSIGNED_NETWORK_ID, nullptr);
}

void CLocalPlayer::SendGiveDamageEvent(PLAYERID PlayerID, float fDamageFactor, int weaponType, int pedPieceType)
{
	spdlog::info("SendGiveDamageEvent: PlayerID: {}, fDamageFactor: {}, weaponType: {}, pedPieceType: {}", PlayerID, fDamageFactor, weaponType, pedPieceType);

	fDamageFactor *= 0.33f;

	RakNet::BitStream bsSend;
	proto::GiveTakeDamageReq req{};
	req.taking = false;
	req.playerId = PlayerID;
	req.damage = fDamageFactor;
	req.weaponId = weaponType;
	req.bodyPart = pedPieceType;
	proto::Writer w(bsSend);
	req.serialize(w, proto::ProtoVersion::V037);

	pNetGame->GetRakClient()->RPC(&RPC_PlayerGiveTakeDamage, &bsSend, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false, UNASSIGNED_NETWORK_ID, nullptr);
}

void CLocalPlayer::SendGiveDamageActorEvent(PLAYERID ActorID, float fDamageFactor, int weaponType, int pedPieceType)
{
	RakNet::BitStream bsSend;
	proto::GiveDamageActorReq req{};
	req.unknown = false;
	req.actorId = ActorID;
	req.damage = fDamageFactor;
	req.weaponId = weaponType;
	req.bodyPart = pedPieceType;
	proto::Writer w(bsSend);
	req.serialize(w, proto::ProtoVersion::V037);

	pNetGame->GetRakClient()->RPC(&RPC_GiveDamageActor, &bsSend, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false, UNASSIGNED_NETWORK_ID, nullptr);
}
