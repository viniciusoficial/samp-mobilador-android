//
// Created by ALYN on 2024/8/28.
//

#include "Object.h"
#include "Game.h"
#include "Common.h"
#include "MaterialTextGenerator.h"
#include "OobCollision.h"
#include "ObjectUVAnim.h"
#include "PlayerPed.h"
#include "Scripting.h"
#include "Util.h"
#include "Vehicle.h"
#include "Artwork/ArtworkCore.h"
#include "Artwork/ArtworkManager.h"
#include "Engine/Symbols.h"
#include "../Net/NetGame.h"
#include "../Net/RemotePlayer.h"
#include "../Net/VehiclePool.h"
#include "../Client.h"
#include "RenderWare/rpuvanim.h"
#include "Core/PtrNode.h"
#include <SA.h>
#include <spdlog/spdlog.h>
#include <cmath>
#include <strings.h>
#include <unordered_map>
#include <unordered_set>


RwTexture* g_blankTexture;

// O(1) lookup from GTA CEntity* to live CObject* (covers both CObjectPool
// objects and CPlayerPed attached objects, which do not live in CObjectPool).
static std::unordered_map<sa::CEntity*, CObject*> s_liveObjectByEntity;

CObject* CObject::FindByGtaEntity(sa::CEntity* pEntity)
{
	if (!pEntity) {
		return nullptr;
	}
	auto it = s_liveObjectByEntity.find(pEntity);
	if (it == s_liveObjectByEntity.end()) {
		return nullptr;
	}
	CObject* pObj = it->second;
	if (!pObj || !pObj->m_gtaId || GamePool_Object_GetAt(pObj->m_gtaId) != reinterpret_cast<sa::CObject*>(pEntity)) {
		s_liveObjectByEntity.erase(it);
		return nullptr;
	}
	return pObj;
}

CObject::CObject(int iModel, sa::CVector vecPos, sa::CVector vecRot, float fDrawDistance, uint8_t bAttached)
{
	spdlog::debug("CObject::CObject({}, [{} {} {}], [{} {} {}], {}, {}).", iModel, vecPos.x, vecPos.y, vecPos.z, vecRot.x, vecRot.y, vecRot.z, fDrawDistance, bAttached);
	m_AttachedVehicleID = INVALID_VEHICLE_ID;
	m_AttachedObjectID = INVALID_OBJECT_ID;
	m_AttachedPlayerID = INVALID_PLAYER_ID;
	m_bAttachedToPed = bAttached;

	m_entity = nullptr;
	m_gtaId = 0;

	m_vecAttachedPos.x = 0.0f;
	m_vecAttachedPos.y = 0.0f;
	m_vecAttachedPos.z = 0.0f;
	m_vecAttachedRot.x = 0.0f;
	m_vecAttachedRot.y = 0.0f;
	m_vecAttachedRot.z = 0.0f;
	m_bSyncRotation = true;

	m_byteMoving = 0;
	m_fMoveSpeed = 0.0f;
	m_bNeedRotate = false;
	m_iModel = iModel;
	m_iServerModel = iModel;
	m_bTimedModel = false;
	m_timeOn = 0;
	m_timeOff = 0;
	m_bTimedHidden = false;
	m_vecRotation = sa::CVector{0.0f, 0.0f, 0.0f};
	m_matTarget = {};
	m_quatStart = {};
	m_quatTarget = {};
	m_vecRotationTarget = sa::CVector{0.0f, 0.0f, 0.0f};
	m_vecSubRotationTarget = sa::CVector{0.0f, 0.0f, 0.0f};
	m_fDistanceToTargetPoint = 0.0f;
	m_dwMoveTick = 0;
	m_bNoCameraCollision = false;
	m_fDrawDistance = (std::isfinite(fDrawDistance) && fDrawDistance > 0.0f) ? fDrawDistance : 0.0f;

	if (artwork::isCustomObjectId(iModel)) {
		m_bTimedModel = artwork::ArtworkManager::instance().isTimedObject(iModel, m_timeOn, m_timeOff);
		const int32_t slot = artwork::ArtworkManager::instance().ensureCustomObjectLoaded(iModel);
		if (slot >= 0) {
			iModel = slot;
		} else {
			const int32_t fallback = artwork::ArtworkManager::instance().resolveFallbackObjectModel(iModel);
			spdlog::warn("CObject::CObject: custom object {} not ready/failed, using fallback {}", iModel, fallback);
			iModel = fallback;
		}
	}

	if (!IsValidModel(iModel)) {
		spdlog::info("Object model {} is invalid, using default model.", iModel);
		iModel = 18631;
	}

	uint32_t dwRetID = 0;
	ScriptCommand(&create_object, iModel, vecPos.x, vecPos.y, vecPos.z, &dwRetID);
	spdlog::debug("Object created with model {} at [{} {} {}].", iModel, vecPos.x, vecPos.y, vecPos.z);

	sa::CObject* pEntity = dwRetID ? GamePool_Object_GetAt(dwRetID) : nullptr;

	if (dwRetID && pEntity) {
		m_entity = pEntity;
		m_gtaId = dwRetID;
		s_liveObjectByEntity[m_entity] = this;

		m_iModel = iModel;

		EnsureRwObject();
		ApplySampObjectPhysics();

		// create_object may snap the spawn Z — re-assert the exact server
		// position through the full Remove -> SetMatrix -> Add cycle so the
		// repeat-sector records always match the final matrix
		RwMatrix mat = m_entity->GetMatrix().ToRwMatrix();
		mat.pos = vecPos;
		UpdateMatrix(mat);
		SetRotation(&vecRot);

		// out-of-bounds and long-draw-distance entities may bypass the world's
		// frame-sync pipeline — without this they render at the atomic frame's
		// stale transform (origin), not vecPos
		if (vecPos.x > 3000.0f || vecPos.x < -3000.0f ||
				vecPos.y > 3000.0f || vecPos.y < -3000.0f ||
				m_fDrawDistance > 250.0f) {
			UpdateRwMatrixAndFrame();
		}
	}
	else {
		spdlog::error("Failed to create object with model {} at [{} {} {}].", iModel, vecPos.x, vecPos.y, vecPos.z);
	}

	for (int i = 0; i < MAX_MATERIALS; i++) {
		memset(&m_Material[i], 0, sizeof(MATERIAL_OBJECT));
	}
	m_originalAtomicMaterials.clear();

	m_bHasCustomObjectMaterial = false;
	m_bObjectMaterialTextureChanged = false;
	m_bObjectMaterialColorChanged = false;
	m_pLastObjectMaterialRwObject = nullptr;

	m_bForceRender = (vecPos.x > 3000.0f || vecPos.x < -3000.0f ||
			vecPos.y > 3000.0f || vecPos.y < -3000.0f);
	m_pUVAnimRwObject = nullptr;
	m_bUVAnimEnabled = false;
	m_bDiagLogged = false;
	m_iPinnedColSlot = -1;
	m_dwLastColRequestTick = 0;
	m_bColDiagLogged = false;

	if (m_bForceRender && m_entity) {
		spdlog::info("OOB object: model={} entity={:p} rwObj={:p} moving={:p}", iModel,
				(void*) m_entity, (void*) m_entity->m_pRwObject,
				(void*) m_entity->m_pMovingList);
	}

	if (m_bTimedModel && m_entity && pGame) {
		int hour = 0;
		int minute = 0;
		pGame->GetWorldTime(&hour, &minute);
		if (!artwork::isTimedObjectVisible(hour, m_timeOn, m_timeOff)) {
			m_bTimedHidden = true;
			m_entity->Remove();
			SetCollisionChecking(false);
		}
	}

	TryEnableUVAnim();
}

CObject::~CObject()
{
	if (m_entity) {
		auto it = s_liveObjectByEntity.find(m_entity);
		if (it != s_liveObjectByEntity.end() && it->second == this) {
			s_liveObjectByEntity.erase(it);
		}
	}

	// release the col-slot pin first — the CColDef pool is static and
	// outlives the entity, and RemoveRef is safe on any valid slot id
	// (the def array is 255 fixed entries, no quadtree entry needed)
	if (m_iPinnedColSlot >= 0) {
		eng::fn::CColStore_RemoveRef( m_iPinnedColSlot);
		m_iPinnedColSlot = -1;
	}

	sa::CObject* pEntity = m_gtaId ? GamePool_Object_GetAt(m_gtaId) : nullptr;

	if (pEntity && pEntity == m_entity && !IsEntityPlaceable(pEntity)) {
		ScriptCommand(&destroy_object, m_gtaId);
		if (GetModelRefCounts(m_iModel) == 0) {
			pGame->RemoveModel(m_iModel, false);
		}
	}

	if (m_bHasCustomObjectMaterial) {
		for (int i = 0; i < MAX_MATERIALS; i++) {
			if (m_Material[i].pMaterialTexture && m_Material[i].pMaterialTexture != g_blankTexture) {
				RwTextureDestroy(m_Material[i].pMaterialTexture);
				m_Material[i].pMaterialTexture = 0;
			}

			memset(&m_Material[i], 0, sizeof(MATERIAL_OBJECT));
		}
		m_originalAtomicMaterials.clear();
		m_bHasCustomObjectMaterial = false;
	}
}

/**
 * create_object always produces a real CObject (CDummyObject lives in a
 * separate 96B pool and only serves IPL instances) — but the atomic is
 * only instanced once the model has streamed in, so a fresh object can
 * sit with m_pRwObject == 0. Re-instance it via SetModelIndex once the
 * model arrives — the same retry the reference client's ProcessStreaming
 * uses for streamed-out objects.
 */
void CObject::EnsureRwObject()
{
	if (!m_entity || IsEntityPlaceable(m_entity) || m_entity->m_pRwObject) {
		return;
	}
	if (!pGame) {
		return;
	}
	if (!pGame->IsModelLoaded(m_iModel)) {
		// the engine created the entity without an atomic because the model
		// wasn't streamed yet — keep requesting like the reference client's
		// ProcessStreaming; retry next tick once it arrives
		pGame->RequestModel(m_iModel);
		return;
	}

	spdlog::debug("EnsureRwObject: re-instancing atomic for object {} (model {})", m_gtaId, m_iModel);
	CEntity::SetModelIndex(m_iModel);
	// engine SetModelIndex may re-apply object.dat physics data — restore
	// the SA-MP static-object configuration afterwards
	ApplySampObjectPhysics();
	UpdateRwMatrixAndFrame();
}

/**
 * PC SA-MP parity + static convergence for server-authoritative objects:
 *  - m_nFlags bit31 (0x80000000) is PC SA-MP's SetBackendHighFlag(): the
 *    hooked CPhysical::ProcessCollision early-outs on it, so SA-MP objects
 *    never run their own (meaningless and interpolation-fighting) collision
 *    sweep. PC sets this on every object at creation (object.cpp:748).
 *  - physicalFlags bits 1-3 forced to the engine's own heavy-static-object
 *    configuration (mass >= 99998 path in CObjectData::SetObjectData:
 *    (flags & ~0xE) | 0xC = bDisableCollisionForce | bit3). Bit 3 selects
 *    the 1-body path in CPhysical::ProcessCollisionSectorList — friction
 *    scaled by the normal-impact impulse exactly like buildings — instead
 *    of the 2-body path dynamic object.dat props get, where even a 1°
 *    graze eats the FULL unscaled static friction and the object wakes up
 *    (m_bIsStatic cleared) as a moving physics body. SA-MP object positions
 *    are server-authoritative, so no server-visible behavior is lost.
 * Idempotent; re-applied after every (re)creation since object.dat-driven
 * setup may overwrite the flags.
 */
void CObject::ApplySampObjectPhysics()
{
	// offsets verified against both binaries: arm64 CPed::UpdatePosition
	// reads contact+0x64 for the (flags & 0xC) == 0x4 clamp test, arm32
	// (0x4a1cb4) reads contact+0x44 — the same word CObjectData::SetObjectData
	// writes and ProcessCollisionSectorList branches on
#if defined(__aarch64__) || defined(__x86_64__)
	static_assert(offsetof(sa::CPhysical, m_nPhysicalFlags) == 0x64,
		"physicalFlags must match the +0x64 read in arm64 CPed::UpdatePosition");
#else
	static_assert(offsetof(sa::CPhysical, m_nPhysicalFlags) == 0x44,
		"physicalFlags must match the +0x44 read in arm32 CPed::UpdatePosition (0x4a1cb4)");
#endif
	if (!m_entity || IsEntityPlaceable(m_entity)) {
		return;
	}
	m_entity->m_nFlags |= 0x80000000u;
	m_entity->m_nPhysicalFlags = (m_entity->m_nPhysicalFlags & 0xFFFFFFF1u) | 0xCu;
}

void CObject::Detach()
{
	if (m_entity && m_gtaId && GamePool_Object_GetAt(m_gtaId) == reinterpret_cast<sa::CObject*>(m_entity)) {
		if (ScriptCommand(&is_object_attached, m_gtaId)) {
			ScriptCommand(&disassociate_object, m_gtaId, 0.0f, 0.0f, 0.0f, 0);
		}
	}
}

void CObject::AttachToPlayer(CPlayerPed* pPlayerPed)
{
	if (!m_entity || !m_gtaId || !pPlayerPed || !pPlayerPed->m_ped || !pPlayerPed->m_gtaId) {
		return;
	}
	if (!ScriptCommand(&is_object_attached, m_gtaId)) {
		ScriptCommand(&attach_object_to_actor,
				m_gtaId,
				pPlayerPed->m_gtaId,
				m_vecAttachedPos.x,
				m_vecAttachedPos.y,
				m_vecAttachedPos.z,
				m_vecAttachedRot.x,
				m_vecAttachedRot.y,
				m_vecAttachedRot.z);
	}
}

/**
 * @param pVehicle
 */
void CObject::AttachToVehicle(CVehicle* pVehicle)
{
	if (!m_entity || !m_gtaId || !pVehicle || !pVehicle->m_gtaId || !pVehicle->VerifyInstance()) {
		return;
	}

	if (!ScriptCommand(&is_object_attached, m_gtaId)) {
		ScriptCommand(&attach_object_to_car,
				m_gtaId,
				pVehicle->m_gtaId,
				m_vecAttachedPos.x,
				m_vecAttachedPos.y,
				m_vecAttachedPos.z,
				m_vecAttachedRot.x,
				m_vecAttachedRot.y,
				m_vecAttachedRot.z);
	}
}

/**
 * @param pObject
 */
void CObject::AttachToObject(CObject* pObject)
{
	if (!m_entity || !m_gtaId || !pObject || pObject == this || !pObject->m_entity || !pObject->m_gtaId
			|| GamePool_Object_GetAt(pObject->m_gtaId) != reinterpret_cast<sa::CObject*>(pObject->m_entity)) {
		return;
	}

	if (!ScriptCommand(&is_object_attached, m_gtaId)) {
		ScriptCommand(&attach_object_to_object,
				m_gtaId,
				pObject->m_gtaId,
				m_vecAttachedPos.x,
				m_vecAttachedPos.y,
				m_vecAttachedPos.z,
				m_vecAttachedRot.x,
				m_vecAttachedRot.y,
				m_vecAttachedRot.z);
	}

	// When syncRotation is disabled, GTA's attach_object_to_object still inherits
	// the parent's rotation every tick — restore fixed world orientation using
	// m_vecAttachedRot to match PC SA-MP UpdateAttachedObjectRotation.
	if (!m_bSyncRotation) {
		RwMatrix mat = m_entity->GetMatrix().ToRwMatrix();
		sa::CVector pos = mat.pos;
		RotateMatrix(m_vecAttachedRot);
		mat = m_matTarget;
		mat.pos = pos;
		m_entity->SetMatrix(mat);
		UpdateRwMatrixAndFrame();
	}
}

/**
 * @param matPos
 * @return
 */
float CObject::DistanceRemaining(RwMatrix* matPos)
{
	float fSX, fSY, fSZ;
	fSX = (matPos->pos.x - m_matTarget.pos.x) * (matPos->pos.x - m_matTarget.pos.x);
	fSY = (matPos->pos.y - m_matTarget.pos.y) * (matPos->pos.y - m_matTarget.pos.y);
	fSZ = (matPos->pos.z - m_matTarget.pos.z) * (matPos->pos.z - m_matTarget.pos.z);
	return (float) sqrt(fSX + fSY + fSZ);
}

void CObject::ApplyMoveSpeed()
{
	if (m_entity) {
		float* pTimeStep = eng::as<float*>(eng::Sym::CTimer_ms_fTimeStep);
		if (!pTimeStep) {
			return;
		}
		float fTimeStep = *pTimeStep;

		RwMatrix mat = m_entity->GetMatrix().ToRwMatrix();
		mat.pos.x += fTimeStep * m_entity->GetMoveSpeed().x;
		mat.pos.y += fTimeStep * m_entity->GetMoveSpeed().y;
		mat.pos.z += fTimeStep * m_entity->GetMoveSpeed().z;
		m_entity->SetMatrix(mat);
	}
}

RpAtomic* DriveUVAnimAtomicCB(RpAtomic* atomic, void* data);

/**
 * @param fElapsedTime
 */
void CObject::Process(float fElapsedTime)
{
	if (!m_entity || !m_gtaId || GamePool_Object_GetAt(m_gtaId) != reinterpret_cast<sa::CObject*>(m_entity) || IsEntityPlaceable(m_entity)) {
		return;
	}

	// Timed 0.3.DL custom objects (AddSimpleModelTimed): toggle visibility & collision by world hour.
	if (m_bTimedModel && pGame) {
		int hour = 0;
		int minute = 0;
		pGame->GetWorldTime(&hour, &minute);
		const bool shouldBeVisible = artwork::isTimedObjectVisible(hour, m_timeOn, m_timeOff);
		if (!shouldBeVisible) {
			if (!m_bTimedHidden) {
				m_bTimedHidden = true;
				m_entity->Remove();
				SetCollisionChecking(false);
			}
		} else if (m_bTimedHidden) {
			m_bTimedHidden = false;
			SetCollisionChecking(true);
			m_entity->Add();
			EnsureRwObject();
			UpdateRwMatrixAndFrame();
		}
	}

	if (!m_bTimedHidden) {
		if (!m_entity->m_pRwObject) {
			EnsureRwObject();
		}

		UpdateUVAnim();
	}

	// keep the flag tracking the entity's actual position — previously only
	// the constructor and TeleportTo set it, so an object carried across
	// the boundary by MoveTo neither gained OOB collision nor released its
	// injected records when it came back in bounds
	if (m_entity) {
		const sa::CVector& pos = m_entity->GetPosition();
		m_bForceRender = pos.x > 3000.0f || pos.x < -3000.0f
				|| pos.y > 3000.0f || pos.y < -3000.0f;
	}

	if (!m_bTimedHidden && m_bForceRender && m_entity) {
		// Col data is position-streamed: CColStore's quadtree matches
		// col files to in-bounds positions only, so an OOB object's
		// shared colModel stays an empty shell — and an empty shell is
		// ZERO collision (ProcessColModels early-outs on null
		// m_pColData; there is no bound-sphere fallback). Pin the slot
		// so LoadCollision can't evict it, then force-load the file
		// synchronously — the async streaming channel starves requests
		// queued outside position matching. Single-alloc colModels own
		// their data outright and need no pin.
		sa::CBaseModelInfo* pModelInfo = GetModelInfoByID(m_iModel);
		sa::CColModel* pColModel = pModelInfo ? pModelInfo->m_pColModel : nullptr;
		if (PinOobColSlot(pColModel, m_iPinnedColSlot)) {
			LogOobColPin(m_iModel, m_iPinnedColSlot);
		}
		RequestOobColSlot(pColModel, m_iPinnedColSlot, m_dwLastColRequestTick);
		if (pColModel && pColModel->m_pColData && m_iPinnedColSlot >= 0
				&& !m_bColDiagLogged) {
			m_bColDiagLogged = true;
			sa::CCollisionData* pData = pColModel->m_pColData;
			spdlog::info("OOBCol: model={} col arrived slot={} "
					"sph={} box={} ln={} tris={}",
					m_iModel, m_iPinnedColSlot,
					(int) pData->m_nNumSpheres, (int) pData->m_nNumBoxes,
					(int) pData->m_nNumLines, (int) pData->m_nNumTriangles);
		}
	}

	if (m_AttachedVehicleID != INVALID_VEHICLE_ID) {
		if (pNetGame) {
			CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
			if (pVehiclePool) {
				CVehicle* pVehicle = pVehiclePool->GetAt(m_AttachedVehicleID);
				if (pVehicle) {
					if (pVehicle->IsAdded()) {
						AttachToVehicle(pVehicle);
					}
				}
			}
		}
	}
	else if (m_AttachedObjectID != INVALID_OBJECT_ID) {
		if (pNetGame) {
			CObjectPool* pObjectPool = pNetGame->GetObjectPool();
			if (pObjectPool) {
				CObject* pObject = pObjectPool->GetAt(m_AttachedObjectID);
				if (pObject) {
					AttachToObject(pObject);
				}
			}
		}
	}
	else if (m_AttachedPlayerID != INVALID_PLAYER_ID) {
		if (pNetGame && pNetGame->GetPlayerPool()) {
			CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
			CPlayerPed* pTargetPed = nullptr;
			if (pPlayerPool->GetLocalPlayerID() == m_AttachedPlayerID) {
				if (CLocalPlayer* pLocal = pPlayerPool->GetLocalPlayer()) {
					pTargetPed = pLocal->GetPlayerPed();
				}
			}
			else if (CRemotePlayer* pRemote = pPlayerPool->GetAt(m_AttachedPlayerID)) {
				pTargetPed = pRemote->GetPlayerPed();
			}
			if (pTargetPed) {
				AttachToPlayer(pTargetPed);
			}
		}
	}
	else if (m_entity && (m_byteMoving & 1)) {
		sa::CVector vecSpeed = {0.0f, 0.0f, 0.0f};

		RwMatrix matEnt = m_entity->GetMatrix().ToRwMatrix();

		float distance = fElapsedTime * m_fMoveSpeed;
		float remaining = DistanceRemaining(&matEnt);
		uint32_t dwThisTick = GetTickCount();

		float posX = matEnt.pos.x;
		float posY = matEnt.pos.y;
		float posZ = matEnt.pos.z;

		float f1 = ((float) (dwThisTick - m_dwMoveTick)) * 0.001f * m_fMoveSpeed;
		float f2 = m_fDistanceToTargetPoint - remaining;

		if (distance >= remaining) {
			m_entity->SetVelocity(vecSpeed);
			m_entity->SetTurnSpeed(vecSpeed);
			matEnt.pos.x = m_matTarget.pos.x;
			matEnt.pos.y = m_matTarget.pos.y;
			matEnt.pos.z = m_matTarget.pos.z;
			if (m_bNeedRotate) {
				m_quatTarget.GetMatrix(&matEnt);
			}
			UpdateMatrix(matEnt);
			StopMoving();
		}
		else if (fElapsedTime > 0.0f) {
			float delta = 1.0f / (remaining / distance);
			matEnt.pos.x += ((m_matTarget.pos.x - matEnt.pos.x) * delta);
			matEnt.pos.y += ((m_matTarget.pos.y - matEnt.pos.y) * delta);
			matEnt.pos.z += ((m_matTarget.pos.z - matEnt.pos.z) * delta);

			float progressRemain = (m_fDistanceToTargetPoint > 0.0001f)
					? (remaining / m_fDistanceToTargetPoint)
					: 0.0f;
			if (progressRemain < 0.0f) progressRemain = 0.0f;
			if (progressRemain > 1.0f) progressRemain = 1.0f;
			float slerpDelta = 1.0f - progressRemain;

			delta = 1.0f / fElapsedTime;
			vecSpeed.x = (matEnt.pos.x - posX) * delta * 0.02f;
			vecSpeed.y = (matEnt.pos.y - posY) * delta * 0.02f;
			vecSpeed.z = (matEnt.pos.z - posZ) * delta * 0.02f;

			if (FloatOffset(f1, f2) > 0.1f) {
				if (f1 > f2) {
					delta = (f1 - f2) * 0.1f + 1.0f;
					vecSpeed.x *= delta;
					vecSpeed.y *= delta;
					vecSpeed.z *= delta;
				}

				if (f2 > f1) {
					delta = 1.0f - (f2 - f1) * 0.1f;
					vecSpeed.x *= delta;
					vecSpeed.y *= delta;
					vecSpeed.z *= delta;
				}
			}

			m_entity->SetVelocity(vecSpeed);
			ApplyMoveSpeed();

			if (m_bNeedRotate) {
				float fx, fy, fz;
				GetRotation(&fx, &fy, &fz);
				float desiredZ = m_vecRotationTarget.z - progressRemain * m_vecSubRotationTarget.z;
				vecSpeed.x = 0.0f;
				vecSpeed.y = 0.0f;
				vecSpeed.z = subAngle(fz, desiredZ) * 0.01f;
				if (vecSpeed.z <= 0.001f) {
					if (vecSpeed.z < -0.001f) {
						vecSpeed.z = -0.001f;
					}
				}
				else {
					vecSpeed.z = 0.001f;
				}

				m_entity->SetTurnSpeed(vecSpeed);
				matEnt = m_entity->GetMatrix().ToRwMatrix();
				sa::CQuaternion quat;
				quat.Slerp(&m_quatStart, &m_quatTarget, slerpDelta);
				quat.Normalize();
				quat.GetMatrix(&matEnt);
			}
			else {
				matEnt = m_entity->GetMatrix().ToRwMatrix();
			}

			UpdateMatrix(matEnt);
		}
	}

	// Re-sync OOB state after moving/attaching so crossing +-3000 takes effect immediately
	if (m_entity) {
		const sa::CVector& pos = m_entity->GetPosition();
		m_bForceRender = pos.x > 3000.0f || pos.x < -3000.0f
				|| pos.y > 3000.0f || pos.y < -3000.0f;
	}

}

/**
 * @param ObjectID
 * @param vecPos
 * @param vecRot
 * @param bSyncRotation
 */
void CObject::SetAttachedObject(uint16_t ObjectID, sa::CVector* vecPos, sa::CVector* vecRot, bool bSyncRotation)
{
	Detach();
	m_AttachedVehicleID = INVALID_VEHICLE_ID;
	m_AttachedPlayerID = INVALID_PLAYER_ID;
	if (ObjectID == INVALID_OBJECT_ID) {
		m_AttachedObjectID = INVALID_OBJECT_ID;
		m_vecAttachedPos.x = 0.0f;
		m_vecAttachedPos.y = 0.0f;
		m_vecAttachedPos.z = 0.0f;
		m_vecAttachedRot.x = 0.0f;
		m_vecAttachedRot.y = 0.0f;
		m_vecAttachedRot.z = 0.0f;
		m_bSyncRotation = false;
	}
	else {
		m_AttachedObjectID = ObjectID;
		m_vecAttachedPos.x = vecPos->x;
		m_vecAttachedPos.y = vecPos->y;
		m_vecAttachedPos.z = vecPos->z;
		m_vecAttachedRot.x = vecRot->x;
		m_vecAttachedRot.y = vecRot->y;
		m_vecAttachedRot.z = vecRot->z;
		m_bSyncRotation = bSyncRotation;
	}
}

void CObject::SetScale(sa::CVector vecScale)
{
	// todo: GTA objects have no scale; scaling applies to attached-to-ped objects only
}

sa::CVector CObject::GetScale()
{
	// todo: see SetScale
	return sa::CVector{1.0f, 1.0f, 1.0f};
}

/**
 * @param vecRotation
 */
void CObject::SetRotation(sa::CVector* vecRotation)
{
	if (m_entity && GamePool_Object_GetAt(m_gtaId) == reinterpret_cast<sa::CObject*>(m_entity)) {
		ScriptCommand(&set_object_rotation, m_gtaId, vecRotation->x, vecRotation->y, vecRotation->z);
		m_vecRotation.x = vecRotation->x;
		m_vecRotation.y = vecRotation->y;
		m_vecRotation.z = vecRotation->z;
		if (m_bTimedHidden) {
			m_entity->Remove();
			SetCollisionChecking(false);
		} else if (m_bForceRender) {
			UpdateRwMatrixAndFrame();
		}
	}
}

void CObject::UpdateMatrix(RwMatrix matrix)
{
	if (!m_entity || !m_entity->m_matrix) {
		return;
	}
	if (m_bTimedHidden) {
		m_entity->SetMatrix(matrix);
		UpdateRwMatrixAndFrame();
		return;
	}
	CEntity::UpdateMatrix(matrix);
}

/**
 * @param pfX
 * @param pfY
 * @param pfZ
 */
void CObject::GetRotation(float* pfX, float* pfY, float* pfZ)
{
	*pfX = 0.0f;
	*pfY = 0.0f;
	*pfZ = 0.0f;

	if (m_entity) {
		sa::CMatrix* mat = m_entity->m_matrix;

		if (mat) {
			// CMatrix::ConvertToEulerAngles
			eng::fn::CMatrix_ConvertToEulerAngles( mat, pfX, pfY, pfZ, 21);
		}

		*pfX = *pfX * 57.295776 * -1.0;
		*pfY = *pfY * 57.295776 * -1.0;
		*pfZ = *pfZ * 57.295776 * -1.0;
	}
}

/**
 * @param fX
 * @param fY
 * @param fZ
 */
void CObject::TeleportTo(float fX, float fY, float fZ)
{
	if (fX > 3000.0f || fX < -3000.0f || fY > 3000.0f || fY < -3000.0f) {
		m_bForceRender = true;
	}
	else {
		m_bForceRender = false;
	}

	if (m_bTimedHidden && m_entity && !IsEntityPlaceable(m_entity)) {
		RwMatrix mat = m_entity->GetMatrix().ToRwMatrix();
		mat.pos = sa::CVector{fX, fY, fZ};
		m_entity->SetMatrix(mat);
		UpdateRwMatrixAndFrame();
		return;
	}

	CEntity::TeleportTo(fX, fY, fZ);
}

bool CObject::SwapGtaModel(int iNewGtaModel)
{
	if (!m_entity || !m_gtaId || GamePool_Object_GetAt(m_gtaId) != reinterpret_cast<sa::CObject*>(m_entity) || IsEntityPlaceable(m_entity)) {
		return false;
	}
	if (!IsValidModel(iNewGtaModel)) {
		iNewGtaModel = 18631;
	}
	if (m_iModel == iNewGtaModel) {
		return true;
	}

	// Pre-load the replacement model BEFORE tearing down the live entity so a missing/unloadable
	// model never destroys a working object.
	if (pGame && !pGame->IsModelLoaded(iNewGtaModel)) {
		pGame->RequestModel(iNewGtaModel);
		pGame->LoadRequestedModels();
		if (!pGame->IsModelLoaded(iNewGtaModel)) {
			spdlog::warn("CObject::SwapGtaModel: target model {} failed to load; keeping model {}",
				iNewGtaModel, m_iModel);
			return false;
		}
	}

	const sa::CVector pos = m_entity->GetPosition();
	sa::CVector rot = m_vecRotation;

	// Cleanly detach and destroy the old GTA entity before creating the replacement.
	auto it = s_liveObjectByEntity.find(m_entity);
	if (it != s_liveObjectByEntity.end() && it->second == this) {
		s_liveObjectByEntity.erase(it);
	}
	if (m_iPinnedColSlot >= 0) {
		eng::fn::CColStore_RemoveRef(m_iPinnedColSlot);
		m_iPinnedColSlot = -1;
	}

	const int oldModel = m_iModel;
	ScriptCommand(&destroy_object, m_gtaId);
	if (pGame && GetModelRefCounts(oldModel) == 0) {
		pGame->RemoveModel(oldModel, false);
	}

	m_entity = nullptr;
	m_gtaId = 0;
	m_pUVAnimRwObject = nullptr;
	m_bUVAnimEnabled = false;
	m_pLastObjectMaterialRwObject = nullptr;
	m_originalAtomicMaterials.clear();
	m_bColDiagLogged = false;

	int createdModel = iNewGtaModel;
	uint32_t dwRetID = 0;
	ScriptCommand(&create_object, createdModel, pos.x, pos.y, pos.z, &dwRetID);
	sa::CObject* pNewEntity = dwRetID ? GamePool_Object_GetAt(dwRetID) : nullptr;
	bool swapped = (dwRetID != 0 && pNewEntity != nullptr);

	// Failure recovery: never leave a half-dead CObject with m_entity == nullptr.
	if (!swapped) {
		const int recoveryModel = (!artwork::isInternalCustomSlot(oldModel) && IsValidModel(oldModel))
			? oldModel : artwork::kFallbackObjectModelId;
		spdlog::error("CObject::SwapGtaModel: create_object failed (serverModel={}, newModel={}); recovering with model {}",
			m_iServerModel, iNewGtaModel, recoveryModel);
		if (pGame && !pGame->IsModelLoaded(recoveryModel)) {
			pGame->RequestModel(recoveryModel);
			pGame->LoadRequestedModels();
		}
		dwRetID = 0;
		ScriptCommand(&create_object, recoveryModel, pos.x, pos.y, pos.z, &dwRetID);
		pNewEntity = dwRetID ? GamePool_Object_GetAt(dwRetID) : nullptr;
		if (!dwRetID || !pNewEntity) {
			return false;
		}
		createdModel = recoveryModel;
	}

	m_entity = pNewEntity;
	m_gtaId = dwRetID;
	m_iModel = createdModel;
	s_liveObjectByEntity[m_entity] = this;

	EnsureRwObject();
	ApplySampObjectPhysics();
	RwMatrix mat = m_entity->GetMatrix().ToRwMatrix();
	mat.pos = pos;
	UpdateMatrix(mat);
	SetRotation(&rot);

	if (m_bForceRender || m_fDrawDistance > 250.0f) {
		UpdateRwMatrixAndFrame();
	}

	if (m_byteMoving & 1) {
		m_entity->m_nFlags &= 0xFFFFFFF7;
	}

	if (artwork::isInternalCustomSlot(createdModel)) {
		m_bTimedModel = artwork::ArtworkManager::instance().isTimedObject(m_iServerModel, m_timeOn, m_timeOff);
		if (m_bTimedModel && pGame) {
			int hour = 0;
			int minute = 0;
			pGame->GetWorldTime(&hour, &minute);
			m_bTimedHidden = !artwork::isTimedObjectVisible(hour, m_timeOn, m_timeOff);
		} else {
			m_bTimedHidden = false;
		}
	} else {
		m_bTimedModel = false;
		m_bTimedHidden = false;
	}
	if (m_bTimedHidden) {
		m_entity->Remove();
		SetCollisionChecking(false);
	}

	TryEnableUVAnim();
	RefreshCustomMaterials();
	if (m_bHasCustomObjectMaterial) {
		TryChangeToCustomObjectMaterial();
	}
	return swapped;
}

/**
 * @param VehicleID
 * @param vecPos
 * @param vecRot
 */
void CObject::SetAttachedVehicle(uint16_t VehicleID, sa::CVector* vecPos, sa::CVector* vecRot)
{
	Detach();
	m_AttachedObjectID = INVALID_OBJECT_ID;
	m_AttachedPlayerID = INVALID_PLAYER_ID;
	if (VehicleID == INVALID_VEHICLE_ID) {
		m_AttachedVehicleID = INVALID_VEHICLE_ID;
		m_vecAttachedPos.x = 0.0f;
		m_vecAttachedPos.y = 0.0f;
		m_vecAttachedPos.z = 0.0f;
		m_vecAttachedRot.x = 0.0f;
		m_vecAttachedRot.y = 0.0f;
		m_vecAttachedRot.z = 0.0f;
	}
	else {
		m_AttachedVehicleID = VehicleID;
		m_vecAttachedPos.x = vecPos->x;
		m_vecAttachedPos.y = vecPos->y;
		m_vecAttachedPos.z = vecPos->z;
		m_vecAttachedRot.x = vecRot->x;
		m_vecAttachedRot.y = vecRot->y;
		m_vecAttachedRot.z = vecRot->z;
	}
}

void CObject::SetAttachedPlayer(uint16_t PlayerID, sa::CVector* vecPos, sa::CVector* vecRot)
{
	Detach();
	m_AttachedVehicleID = INVALID_VEHICLE_ID;
	m_AttachedObjectID = INVALID_OBJECT_ID;
	if (PlayerID == INVALID_PLAYER_ID) {
		m_AttachedPlayerID = INVALID_PLAYER_ID;
		m_vecAttachedPos = sa::CVector{0.0f, 0.0f, 0.0f};
		m_vecAttachedRot = sa::CVector{0.0f, 0.0f, 0.0f};
	}
	else {
		m_AttachedPlayerID = PlayerID;
		m_vecAttachedPos = *vecPos;
		m_vecAttachedRot = *vecRot;
	}
}

/**
 * @param fX
 * @param fY
 * @param fZ
 * @param fSpeed
 * @param fRotX
 * @param fRotY
 * @param fRotZ
 */
void CObject::MoveTo(float fX, float fY, float fZ, float fSpeed, float fRotX, float fRotY, float fRotZ)
{
	if (!m_entity) {
		return;
	}

	// malformed packet coords must not propagate NaN into the engine
	// matrix — it spreads to every entity sharing the transform
	if (!std::isfinite(fX) || !std::isfinite(fY) || !std::isfinite(fZ)) {
		spdlog::warn("CObject::MoveTo: rejected non-finite target [{}, {}, {}]", fX, fY, fZ);
		return;
	}

	// a non-positive speed can never arrive — distance>=remaining is never
	// reached so the object creeps forever (the !fSpeed>0 test also catches
	// NaN speeds). Snap to the target instead.
	if (!(fSpeed > 0.0f)) {
		StopMoving();
		TeleportTo(fX, fY, fZ);
		return;
	}

	RwMatrix mat = m_entity->GetMatrix().ToRwMatrix();

	if (m_byteMoving & 1) {
		StopMoving();
		mat.pos.x = m_matTarget.pos.x;
		mat.pos.y = m_matTarget.pos.y;
		mat.pos.z = m_matTarget.pos.z;

		if (m_bNeedRotate) {
			m_quatTarget.GetMatrix(&mat);
		}

		UpdateMatrix(mat);
	}

	m_dwMoveTick = GetTickCount();
	m_fMoveSpeed = fSpeed;
	m_matTarget.pos.x = fX;
	m_matTarget.pos.y = fY;
	m_matTarget.pos.z = fZ;
	m_byteMoving |= 1;

	if (!std::isfinite(fRotX) || !std::isfinite(fRotY) || !std::isfinite(fRotZ)
			|| fRotX <= -999.0f || fRotY <= -999.0f || fRotZ <= -999.0f) {
		m_bNeedRotate = false;
	}
	else {
		m_bNeedRotate = true;

		sa::CVector vecRot;
		RwMatrix matrix;
		GetRotation(&vecRot.x, &vecRot.y, &vecRot.z);
		m_vecRotationTarget.x = fixAngle(fRotX);
		m_vecRotationTarget.y = fixAngle(fRotY);
		m_vecRotationTarget.z = fixAngle(fRotZ);

		m_vecSubRotationTarget.x = subAngle(vecRot.x, m_vecRotationTarget.x);
		m_vecSubRotationTarget.y = subAngle(vecRot.y, m_vecRotationTarget.y);
		m_vecSubRotationTarget.z = subAngle(vecRot.z, m_vecRotationTarget.z);

		RotateMatrix(sa::CVector{fRotX, fRotY, fRotZ});
		matrix = m_entity->GetMatrix().ToRwMatrix();
		m_quatStart.SetFromMatrix(&matrix);
		m_quatTarget.SetFromMatrix(&m_matTarget);
		m_quatStart.Normalize();
		m_quatTarget.Normalize();
	}

	m_fDistanceToTargetPoint = GetDistanceFromPoint(m_matTarget.pos);

	if (pNetGame) {
		CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
		if (pPlayerPool && pPlayerPool->GetLocalPlayer()) {
			pPlayerPool->GetLocalPlayer()->UpdateSurfing();
		}
	}

	// sub_1009F070
	m_entity->m_nFlags &= 0xFFFFFFF7;
}

/**
 * @param vecRot
 */
void CObject::RotateMatrix(sa::CVector vecRot)
{
	m_vecRotation = vecRot;

	vecRot.x *= 0.017453292f; // x * pi/180
	vecRot.y *= 0.017453292f; // y * pi/180
	vecRot.z *= 0.017453292f; // z * pi/180

	float cosx = cos(vecRot.x);
	float sinx = sin(vecRot.x);

	float cosy = cos(vecRot.y);
	float siny = sin(vecRot.y);

	float cosz = cos(vecRot.z);
	float sinz = sin(vecRot.z);

	float sinzx = sinz * sinx;
	float coszx = cosz * sinx;

	m_matTarget.right.x = cosz * cosy - sinzx * siny;
	m_matTarget.right.y = coszx * siny + sinz * cosy;
	m_matTarget.right.z = -(siny * cosx);
	m_matTarget.up.x = -(sinz * cosx);
	m_matTarget.up.y = cosz * cosx;
	m_matTarget.up.z = sinx;
	m_matTarget.at.x = sinzx * cosy + cosz * siny;
	m_matTarget.at.y = sinz * siny - coszx * cosy;
	m_matTarget.at.z = cosy * cosx;
}

void CObject::StopMoving()
{
	sa::CVector vec = {0.0f, 0.0f, 0.0f};
	if (m_entity) {
		m_entity->SetVelocity(vec);
		m_entity->SetTurnSpeed(vec);
	}

	m_byteMoving &= ~1;
}

/**
 * @return
 */
bool CObject::AttachedToMovingEntity()
{
	if (m_AttachedObjectID == INVALID_OBJECT_ID) {
		if (m_AttachedVehicleID != INVALID_VEHICLE_ID) {
			return true;
		}

		return (m_byteMoving & 1);
	}
	else {
		if (m_AttachedObjectID >= 0 && m_AttachedObjectID < MAX_OBJECTS) {
			if (pNetGame) {
				CObjectPool* pObjectPool = pNetGame->GetObjectPool();
				if (pObjectPool) {
					CObject* pObject = pObjectPool->GetAt(m_AttachedObjectID);
					if (pObject) return (pObject->m_byteMoving & 1);
				}
			}
		}
	}

	return false;
}

void CObject::SetMaterial(int iModel, uint8_t byteMaterialIndex, char* txdname, char* texturename, uint32_t dwColor)
{
	spdlog::debug("CObject::SetMaterial({}, {}, {}, {}, 0x{:X}).", iModel, byteMaterialIndex, txdname ? txdname : "", texturename ? texturename : "", dwColor);

	if (byteMaterialIndex >= MAX_MATERIALS) {
		return;
	}

	if (artwork::isCustomModelId(iModel)) {
		if (!texturename || !texturename[0]) {
			return;
		}
	} else if (iModel > 0 && (!IsValidModel(iModel) || !txdname || !txdname[0] || !texturename || !texturename[0])) {
		return;
	}

	if (m_Material[byteMaterialIndex].bTextureWasCreated && m_Material[byteMaterialIndex].pMaterialTexture
		&& m_Material[byteMaterialIndex].pMaterialTexture != g_blankTexture) {
		RwTextureDestroy(m_Material[byteMaterialIndex].pMaterialTexture);
	}
	m_Material[byteMaterialIndex].pMaterialTexture = nullptr;
	m_Material[byteMaterialIndex].bTextGenAttempts = 0;
	m_Material[byteMaterialIndex].iCustomTextureModel = 0;
	m_Material[byteMaterialIndex].szCustomTextureName[0] = '\0';

	bool bIsNoneTex = (txdname && strcasecmp(txdname, "none") == 0)
			|| (texturename && strcasecmp(texturename, "none") == 0);

	if (artwork::isCustomModelId(iModel)) {
		if (!bIsNoneTex) {
			m_Material[byteMaterialIndex].iCustomTextureModel = iModel;
			std::snprintf(m_Material[byteMaterialIndex].szCustomTextureName,
				sizeof(m_Material[byteMaterialIndex].szCustomTextureName), "%s", texturename);
			m_Material[byteMaterialIndex].pMaterialTexture =
				artwork::ArtworkManager::instance().findTextureInCustomModel(iModel, texturename);
		}
	}
	else if (iModel > 0) {
		if (!bIsNoneTex) {
			if (!pGame->IsModelLoaded(iModel)) {
				pGame->RequestModel(iModel);
				pGame->LoadRequestedModels();
			}

			if (pGame->IsModelLoaded(iModel)) {
				m_Material[byteMaterialIndex].pMaterialTexture = LoadTextureFromTxd(txdname, texturename);

				if (!ModelInfoGetReferenceCount(iModel)) {
					pGame->RemoveModel(iModel, false);
				}
			}
		}
		// When txd/texture is "none", leave pMaterialTexture null (matches PC SA-MP:
		// keeps the object's original texture while applying dwColor if non-zero).
	}
	else if (iModel == 0) {
		if (!g_blankTexture) {
			g_blankTexture = LoadTextureFromTxd("*", "blanktex");
		}

		m_Material[byteMaterialIndex].pMaterialTexture = g_blankTexture;
	}
	// iModel < 0: no texture, color only

	m_Material[byteMaterialIndex].iType = MATERIAL_TYPE_TEXTURE;
	m_Material[byteMaterialIndex].dwMaterialColor = dwColor;
	m_Material[byteMaterialIndex].bTextureWasCreated = true;
	m_bHasCustomObjectMaterial = true;
}

void CObject::RefreshCustomMaterials()
{
	if (!m_bHasCustomObjectMaterial) {
		return;
	}
	for (int i = 0; i < MAX_MATERIALS; ++i) {
		if (m_Material[i].iType == MATERIAL_TYPE_TEXTURE
			&& !m_Material[i].pMaterialTexture
			&& artwork::isCustomModelId(m_Material[i].iCustomTextureModel)
			&& m_Material[i].szCustomTextureName[0] != '\0') {
			m_Material[i].pMaterialTexture = artwork::ArtworkManager::instance().findTextureInCustomModel(
				m_Material[i].iCustomTextureModel, m_Material[i].szCustomTextureName);
		}
	}
}

void CObject::ReleaseCustomMaterialsBeforeUnload()
{
	if (!m_bHasCustomObjectMaterial) {
		return;
	}
	ChangeToOriginalObjectMaterial();
	for (int i = 0; i < MAX_MATERIALS; ++i) {
		if (m_Material[i].iType == MATERIAL_TYPE_TEXTURE
			&& artwork::isCustomModelId(m_Material[i].iCustomTextureModel)
			&& m_Material[i].pMaterialTexture
			&& m_Material[i].pMaterialTexture != g_blankTexture) {
			RwTextureDestroy(m_Material[i].pMaterialTexture);
			m_Material[i].pMaterialTexture = nullptr;
		}
	}
}

void CObject::SetMaterialText(uint8_t byteMaterialIndex, uint8_t byteMaterialSize, const char* szFontName, uint8_t byteFontSize, uint8_t byteFontBold, uint32_t dwFontColor, uint32_t dwBackgroundColor, uint8_t byteAlign, const char* szText)
{
	spdlog::debug("CObject::SetMaterialText({}, {}, {}, {}, {}, 0x{:X}, 0x{:X}, {}, {}).", byteMaterialIndex, byteMaterialSize, szFontName ? szFontName : "", byteFontSize, byteFontBold, dwFontColor, dwBackgroundColor, byteAlign, szText ? szText : "");

	if (byteMaterialIndex < MAX_MATERIALS) {
		if (!szText) {
			return;
		}
		if (szText[0] == '\0' && dwBackgroundColor == 0) {
			return;
		}

		if (m_Material[byteMaterialIndex].bTextureWasCreated && m_Material[byteMaterialIndex].pMaterialTexture
			&& m_Material[byteMaterialIndex].pMaterialTexture != g_blankTexture) {
			RwTextureDestroy(m_Material[byteMaterialIndex].pMaterialTexture);
		}
		m_Material[byteMaterialIndex].pMaterialTexture = nullptr;
		m_Material[byteMaterialIndex].iCustomTextureModel = 0;
		m_Material[byteMaterialIndex].szCustomTextureName[0] = '\0';

		m_Material[byteMaterialIndex].TextInfo.byteMaterialSize = byteMaterialSize;

		memset(m_Material[byteMaterialIndex].TextInfo.szFontName, 0, 32);
		strncpy(m_Material[byteMaterialIndex].TextInfo.szFontName, szFontName ? szFontName : "", 31);

		m_Material[byteMaterialIndex].TextInfo.byteFontSize = byteFontSize;
		m_Material[byteMaterialIndex].TextInfo.byteFontBold = byteFontBold;
		m_Material[byteMaterialIndex].TextInfo.dwFontColor = dwFontColor;
		m_Material[byteMaterialIndex].TextInfo.dwBackgroundColor = dwBackgroundColor;
		m_Material[byteMaterialIndex].TextInfo.byteAlign = byteAlign;

		memset(m_Material[byteMaterialIndex].TextInfo.szText, 0, 2048);
		strncpy(m_Material[byteMaterialIndex].TextInfo.szText, szText, 2047);

		m_Material[byteMaterialIndex].iType = MATERIAL_TYPE_TEXT;
		m_Material[byteMaterialIndex].dwMaterialColor = 0;
		m_Material[byteMaterialIndex].bTextureWasCreated = false;
		m_Material[byteMaterialIndex].bTextGenAttempts = 0;

		if (!m_bHasCustomObjectMaterial) m_bHasCustomObjectMaterial = true;
	}
}

void CObject::GetMaterialTextSize(int iMatSize, int* iSizeX, int* iSizeY)
{
	int sizes[14][2] = {
			{32, 32}, {64, 32}, {64, 64}, {128, 32}, {128, 64}, {128, 128}, {256, 32},
			{256, 64}, {256, 128}, {256, 256}, {512, 64}, {512, 128}, {512, 256}, {512, 512}
	};

	int del = (iMatSize / 10) - 1;
	if (del < 0) del = 0;
	if (del > 13) del = 13;
	*iSizeX = sizes[del][0];
	*iSizeY = sizes[del][1];
}

void CObject::ProcessMaterialText()
{
	if (!pMaterialTextGenerator) {
		return;
	}

	for (int i = 0; i < MAX_MATERIALS; i++) {
		if (m_Material[i].bTextureWasCreated || m_Material[i].iType != MATERIAL_TYPE_TEXT) {
			continue;
		}

		if (m_Material[i].TextInfo.szText[0] == '\0' && m_Material[i].TextInfo.dwBackgroundColor == 0) {
			continue;
		}

		if (m_Material[i].TextInfo.byteMaterialSize < MATERIAL_SIZE_32X32) {
			m_Material[i].TextInfo.byteMaterialSize = MATERIAL_SIZE_32X32;
		}
		else if (m_Material[i].TextInfo.byteMaterialSize > MATERIAL_SIZE_512X512) {
			m_Material[i].TextInfo.byteMaterialSize = MATERIAL_SIZE_512X512;
		}

		int iSizeX, iSizeY;
		GetMaterialTextSize(m_Material[i].TextInfo.byteMaterialSize, &iSizeX, &iSizeY);

		if (m_Material[i].TextInfo.byteAlign < MATERIAL_ALIGN_LEFT || m_Material[i].TextInfo.byteAlign > MATERIAL_ALIGN_RIGHT) {
			m_Material[i].TextInfo.byteAlign = MATERIAL_ALIGN_CENTER;
		}

		// Match PC SA-MP (D3DXCreateFontA passes fontSize directly as pixel height).
		// Clamp 0 -> 1 so ImGuiRenderer::drawText does not fall back to m_font->FontSize.
		uint8_t byteFontSize = m_Material[i].TextInfo.byteFontSize;
		if (byteFontSize == 0) {
			byteFontSize = 1;
		}

		m_Material[i].pMaterialTexture = pMaterialTextGenerator->Create(iSizeX, iSizeY, m_Material[i].TextInfo.szFontName, byteFontSize, m_Material[i].TextInfo.byteFontBold, m_Material[i].TextInfo.dwFontColor, m_Material[i].TextInfo.dwBackgroundColor, m_Material[i].TextInfo.byteAlign, m_Material[i].TextInfo.szText);

		if (m_Material[i].pMaterialTexture) {
			m_Material[i].bTextureWasCreated = true;
			spdlog::debug("ProcessMaterialText: texture created {}x{} for slot {}", iSizeX, iSizeY, i);
		}
		else {
			// a failed first attempt used to latch bTextureWasCreated and
			// render the material textless forever — retry a few times
			if (++m_Material[i].bTextGenAttempts >= 5) {
				spdlog::error("ProcessMaterialText: giving up on slot {} after {} attempts", i, (int) m_Material[i].bTextGenAttempts);
				m_Material[i].bTextureWasCreated = true;
			}
		}
	}
}

/**
 * Mobile libGTASA only renders UV-animated textures through the MatFX
 * pipeline: CEntity::PreRender advances the animation (MaterialUpdateUVAnimCB)
 * only when RpMatFXAtomicQueryEffects(atomic) != 0, and the animated UV
 * matrix is applied exclusively by _rpMatFXOpenGLAllInOneRenderCB via
 * _rpMatFXOpenGLUVAnimRender (rpMATFXEFFECTUVTRANSFORM = 5). The default
 * all-in-one render callback contains no UV-anim code at all, so models
 * carrying plain RpUVAnim material data (e.g. water planes 19603/19604)
 * render static. The exported RunUVAnim flag is dead — nothing reads it.
 * Fix: enable the MatFX UV transform effect on materials that have UVAnim
 * data, link the matrix slots to the UVAnim extension matrices, and switch
 * the atomic to the MatFX pipeline so the engine drives the animation.
 */
void CObject::TryChangeToCustomObjectMaterial()
{
	if (m_bHasCustomObjectMaterial) {
		ChangeToCustomObjectMaterial();
	}
}

RpAtomic* ChangeCustomMaterialObjectCB(RpAtomic* result, void* data)
{
	auto* pObject = reinterpret_cast<CObject*>(data);
	if (result && pObject && *(uint8_t*) result == 1) {
		RpGeometry* geometry = result->geometry;
		if (geometry) {
			int count = geometry->matList.numMaterials;
			RpMaterial** materials = geometry->matList.materials;
			if (count >= 1 && materials) {
				for (int i = 0; i < count; i++) {
					if (i >= MAX_MATERIALS) break;
					if (!materials[i]) continue;

					if (pObject->m_Material[i].pMaterialTexture) {
						materials[i]->texture = pObject->m_Material[i].pMaterialTexture;
						pObject->m_bObjectMaterialTextureChanged = true;
					}

					if (pObject->m_Material[i].dwMaterialColor) {
						geometry->flags |= rpGEOMETRYMODULATEMATERIALCOLOR;
						geometry->flags &= 0xFFFFFFF7;
						materials[i]->color = *(RwRGBA*) &pObject->m_Material[i].dwMaterialColor;
						materials[i]->surfaceProps.ambient = 1.0f;
						materials[i]->surfaceProps.specular = 0.0f;
						materials[i]->surfaceProps.diffuse = 1.0f;
						pObject->m_bObjectMaterialColorChanged = true;
					}
				}
			}
		}
	}

	return result;
}

RpAtomic* StoreOriginalMaterialObjectCB(RpAtomic* result, void* data)
{
	auto* pObject = reinterpret_cast<CObject*>(data);
	if (result && pObject && *(uint8_t*) result == 1) {
		RpGeometry* geometry = result->geometry;
		if (geometry) {
			CObject::AtomicMaterialBackup backup{};
			backup.atomic = result;
			backup.geomFlags = geometry->flags;
			int count = geometry->matList.numMaterials;
			RpMaterial** materials = geometry->matList.materials;
			if (count >= 1 && materials) {
				for (int i = 0; i < count && i < MAX_MATERIALS; i++) {
					if (!materials[i]) continue;
					backup.textures[i] = materials[i]->texture;
					backup.colors[i] = *(RwRGBA*) &materials[i]->color;
					backup.surfaces[i] = materials[i]->surfaceProps;
				}
			}
			pObject->m_originalAtomicMaterials.push_back(backup);
		}
	}

	return result;
}

RpAtomic* ChangeOriginalMaterialObjectCB(RpAtomic* result, void* data)
{
	auto* pObject = reinterpret_cast<CObject*>(data);
	if (result && pObject && *(uint8_t*) result == 1) {
		RpGeometry* geometry = result->geometry;
		if (geometry) {
			const CObject::AtomicMaterialBackup* backup = nullptr;
			for (const auto& entry : pObject->m_originalAtomicMaterials) {
				if (entry.atomic == result) {
					backup = &entry;
					break;
				}
			}
			if (backup) {
				int count = geometry->matList.numMaterials;
				RpMaterial** materials = geometry->matList.materials;
				if (count >= 1 && materials) {
					geometry->flags = backup->geomFlags;
					for (int i = 0; i < count && i < MAX_MATERIALS; i++) {
						if (!materials[i]) continue;
						if (pObject->m_Material[i].pMaterialTexture && materials[i]->texture != backup->textures[i]) {
							materials[i]->texture = backup->textures[i];
						}
						if (pObject->m_bObjectMaterialColorChanged && pObject->m_Material[i].dwMaterialColor) {
							materials[i]->color = *(RwRGBA*) &backup->colors[i];
							materials[i]->surfaceProps = backup->surfaces[i];
						}
					}
				}
			}
		}
	}

	return result;
}

void CObject::ChangeToCustomObjectMaterial()
{
	if (!m_bHasCustomObjectMaterial || !m_entity || !m_entity->m_pRwObject) {
		return;
	}

	if (m_pLastObjectMaterialRwObject != m_entity->m_pRwObject) {
		m_originalAtomicMaterials.clear();
		ForEachObjectAtomic(m_entity->m_pRwObject, StoreOriginalMaterialObjectCB, this);
		m_pLastObjectMaterialRwObject = m_entity->m_pRwObject;
	}

	ForEachObjectAtomic(m_entity->m_pRwObject, ChangeCustomMaterialObjectCB, this);
}

void CObject::ChangeToOriginalObjectMaterial()
{
	if (m_bHasCustomObjectMaterial) {
		if (m_bObjectMaterialTextureChanged || m_bObjectMaterialColorChanged) {
			if (m_entity && m_entity->m_pRwObject && m_pLastObjectMaterialRwObject == m_entity->m_pRwObject) {
				ForEachObjectAtomic(m_entity->m_pRwObject, ChangeOriginalMaterialObjectCB, this);
				m_bObjectMaterialTextureChanged = false;
				m_bObjectMaterialColorChanged = false;
			}
		}
	}
}
