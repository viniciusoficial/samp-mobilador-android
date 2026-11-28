#include "PlayerPed.h"
#include "Game.h"
#include "Animation.h"
#include "Common.h"
#include "Object.h"
#include "Scripting.h"
#include "Util.h"
#include "PlayerContextScope.h"
#include "Engine/Symbols.h"
#include "RenderWare/rphanim.h"
#include "../Net/NetGame.h"
#include "../Java/EditObject.h"
#include "../Client.h"
#include <SA.h>
#include <spdlog/spdlog.h>

void UpdateElementRpHAnim(sa::CEntity* ent);

bool IsTaskRunNamedAnim(sa::CTask* pTask)
{
	if (!pTask) return false;
	const uintptr_t vtable = *reinterpret_cast<uintptr_t*>(pTask);
	// eng::vmtSlot(sym, 1) == sym + 2*sizeof(void*), which is the Itanium ABI object vptr.
	if (vtable == eng::vmtSlot(eng::Sym::vtable_CTaskSimpleRunNamedAnim, 1)) {
		return true;
	}
	return GetTaskTypeFromTask(&pTask) == 401;
}

sa::CTask* GetSubTaskFromTask(sa::CTask* pTask)
{
	uintptr_t vtable = *(uintptr_t*) pTask;
	return Memory::callFunction<sa::CTask*>(*reinterpret_cast<void**>(vtable + 3 * sizeof(void*)), pTask);
}

// Disassembly-verified CTaskSimpleRunNamedAnim field offsets from libGTASA.so
// (_ZN23CTaskSimpleRunNamedAnimC2EPKcS1_ifibbbb). Itanium C++ ABI packs derived
// fields directly into CTaskSimpleAnim's tail padding (+13 arm32 / +25 arm64).
static constexpr size_t kRunAnimNameOff  = VER_x32 ? 0x0D : 0x19;
static constexpr size_t kRunAnimGroupOff = VER_x32 ? 0x25 : 0x31;
static constexpr size_t kRunAnimBlendOff = VER_x32 ? 0x38 : 0x44;
static constexpr size_t kRunAnimTimeOff  = VER_x32 ? 0x40 : 0x50;
static constexpr size_t kRunAnimFlagsOff = VER_x32 ? 0x5C : 0x6C;

uint32_t CPlayerPed::GetCurrentAnimationIndexFlag()
{
	if (m_ped && m_gtaId && GamePool_Ped_GetAt(m_gtaId) && m_ped->m_pRwObject && m_ped->m_pIntelligence
			&& IsEnteringVehicle() == 0 && !IsExitingVehicle()) {
		sa::CTask* pTask = m_ped->m_pIntelligence->m_TaskMgr.m_aPrimaryTasks[sa::TASK_PRIMARY_PRIMARY];
		while (pTask && !IsTaskRunNamedAnim(pTask)) {
			pTask = GetSubTaskFromTask(pTask);
		}
		if (pTask) {
			const auto* raw = reinterpret_cast<const uint8_t*>(pTask);
			const char* szName = reinterpret_cast<const char*>(raw + kRunAnimNameOff);
			const char* szGroup = reinterpret_cast<const char*>(raw + kRunAnimGroupOff);
			std::string szStr(szGroup);
			szStr += ":";
			szStr += szName;

			const int idx = GetAnimIdxByName(szStr.c_str());
			if (idx != -1) {
				const uint16_t usAnimIdx = static_cast<uint16_t>(idx + 1);
				const float fBlendDelta = *reinterpret_cast<const float*>(raw + kRunAnimBlendOff);
				int iSpeed = static_cast<int>(fBlendDelta);
				if (iSpeed < 0) iSpeed = 0;
				if (iSpeed > 255) iSpeed = 255;
				const uint32_t speed = static_cast<uint32_t>(iSpeed);

				const uint32_t animFlags = *reinterpret_cast<const uint32_t*>(raw + kRunAnimFlagsOff);
				const int32_t animTime = *reinterpret_cast<const int32_t*>(raw + kRunAnimTimeOff);
				const uint32_t direction = (animTime > 0) ? 2u : (animTime < 0 ? 1u : 0u);
				const uint32_t packedFlags =
						(direction << 4) |
						((animFlags & 0x08u) ? 0u : 8u) |
						((animFlags & 0x80u) ? 4u : 0u) |
						((animFlags & 0x40u) ? 2u : 0u) |
						((animFlags & 0x02u) ? 1u : 0u);

				return static_cast<uint32_t>(usAnimIdx) | (speed << 16) | (packedFlags << 24);
			}
		}
	}

	// Non-scripted movement: return last engine blended animation for local ped (PC parity)
	if (m_bytePlayerNumber == 0) {
		return PackLocalPedQueuedAnim();
	}
	return static_cast<uint32_t>(1189u) | (4u << 16) | (0x80u << 24);
}

int CPlayerPed::GetCurrentAnimationIndex(float& blendData)
{
	blendData = 4.0f;

	if (!m_ped || !m_gtaId) {
		return 0;
	}

	if (!GamePool_Ped_GetAt(m_gtaId)) {
		return 0;
	}

	if (!m_ped->m_pRwObject) {
		return 0;
	}
	sa::CPedIntelligence* pIntelligence = m_ped->m_pIntelligence;

	if (pIntelligence) {
		sa::CTask* pTask = pIntelligence->m_TaskMgr.m_aPrimaryTasks[sa::TASK_PRIMARY_PRIMARY];

		if (pTask) {
			while (!IsTaskRunNamedAnim(pTask)) {
				pTask = GetSubTaskFromTask(pTask);
				if (!pTask) {
					return 0;
				}
			}

			const auto* raw = reinterpret_cast<const uint8_t*>(pTask);
			const char* szName = reinterpret_cast<const char*>(raw + kRunAnimNameOff);
			const char* szGroupName = reinterpret_cast<const char*>(raw + kRunAnimGroupOff);
			blendData = *reinterpret_cast<const float*>(raw + kRunAnimBlendOff);

			std::string szStr = std::string(szGroupName);
			szStr += ":";
			szStr += szName;

			int idx = GetAnimIdxByName(szStr.c_str());
			if (idx == -1) {
				return 0;
			}
			else {
				return idx + 1;
			}
		}
	}
	return 0;
}

void CPlayerPed::ApplyAnimation(const char* szAnimName, const char* szAnimLib, float fT, int opt1, int opt2, int opt3, int opt4, int iTime)
{
	if (!m_ped) {
		return;
	}

	if (!GamePool_Ped_GetAt(m_gtaId)) {
		return;
	}

	if (!szAnimLib || !strlen(szAnimLib)) {
		return;
	}

	if (!strcasecmp(szAnimLib, "SEX")) {
		return;
	}
	if (!eng::fn::CAnimManager_GetAnimationBlock<void*>(szAnimLib)) {
		return;
	}

	int iWaitAnimLoad = 0;

	if (!pGame->IsAnimationLoaded(szAnimLib)) {
		pGame->RequestAnimation(szAnimLib);
		pGame->LoadRequestedModels();
		while (!pGame->IsAnimationLoaded(szAnimLib)) {
			usleep(1000);
			iWaitAnimLoad++;
			if (iWaitAnimLoad > 15) {
				spdlog::error("Animation {} not loaded!", szAnimLib);
				return;
			}
		}
	}

	spdlog::debug("Animation {} loaded!", szAnimLib);

	ScriptCommand(&apply_animation, m_gtaId, szAnimName, szAnimLib, fT, opt1, opt2, opt3, opt4, iTime);
}

void CPlayerPed::PlayAnimByIdx(int idx, float BlendData)
{
	if (!m_ped) return;
	if (!idx) {
		// Only clear primary task if it is a scripted CTaskSimpleRunNamedAnim (401);
		// never call TeleportTo() here (which wipes CTaskSimpleUseGun and CTaskComplexEnterCar).
		if (m_ped->m_pIntelligence) {
			if (eng::fn::CTaskManager_FindActiveTaskByType(&m_ped->m_pIntelligence->m_TaskMgr, 401) != nullptr) {
				ClearPrimaryTask();
			}
		}
		return;
	}

	std::string szAnim;
	std::string szBlock;

	char pszAnim[40] = {0};
	char pszBlock[40] = {0};

	bool bTest = false;
	const char* pBegin = GetAnimByIdx(idx - 1);
	if (!pBegin) {
		return;
	}
	while (*pBegin) {
		if (*pBegin == ':') {
			pBegin++;
			bTest = true;
			continue;
		}
		if (!bTest) {
			szBlock += *pBegin;
		}
		if (bTest) {
			szAnim += *pBegin;
		}
		pBegin++;
	}

	snprintf(pszAnim, sizeof(pszAnim), "%s", szAnim.c_str());
	snprintf(pszBlock, sizeof(pszBlock), "%s", szBlock.c_str());

	ApplyAnimation(&pszAnim[0], &pszBlock[0], BlendData, 0, 1, 1, 0, 0);
}

void CPlayerPed::ApplyPackedAnimation(uint32_t dwPacked)
{
	if (!m_ped) return;
	const uint16_t idx = static_cast<uint16_t>(dwPacked & 0xFFFF);
	if (idx == 0) {
		PlayAnimByIdx(0, 4.0f);
		return;
	}

	const char* pCombined = GetAnimByIdx(idx - 1);
	if (!pCombined) {
		return;
	}
	const char* pColon = strchr(pCombined, ':');
	if (!pColon || pColon == pCombined || !pColon[1]) {
		return;
	}

	char pszBlock[40] = {0};
	char pszAnim[40] = {0};
	const size_t blockLen = static_cast<size_t>(pColon - pCombined);
	snprintf(pszBlock, sizeof(pszBlock), "%.*s", static_cast<int>(blockLen), pCombined);
	snprintf(pszAnim, sizeof(pszAnim), "%s", pColon + 1);

	float fSpeed = static_cast<float>((dwPacked >> 16) & 0xFF);
	if (fSpeed <= 0.0f) {
		fSpeed = 4.0f;
	}
	const uint32_t flags = (dwPacked >> 24) & 0xFF;
	const uint32_t direction = (flags >> 4) & 3u;
	const int iTime = (direction == 2u) ? 1 : ((direction == 1u) ? -1 : 0);

	ApplyAnimation(
			pszAnim,
			pszBlock,
			fSpeed,
			(flags & 1u) ? 1 : 0,
			(flags & 2u) ? 1 : 0,
			(flags & 4u) ? 1 : 0,
			(flags & 8u) ? 1 : 0,
			iTime);
}

bool CPlayerPed::IsPlayingAnim(int idx)
{
	if (!m_ped || !m_gtaId || (idx == 0)) {
		return false;
	}
	if (!GamePool_Ped_GetAt(m_gtaId)) {
		return false;
	}
	if (!m_ped->m_pRwObject) {
		return false;
	}

	const char* pAnim = GetAnimByIdx(idx - 1);
	if (!pAnim) {
		return false;
	}
	const char* pColon = strchr(pAnim, ':');
	if (!pColon || !pColon[1]) {
		return false;
	}
	const char* pNameAnim = pColon + 1;

	auto blendAssoc = eng::fn::RpAnimBlendClumpGetAssociation( m_ped->m_pRwObject, pNameAnim);

	return blendAssoc != 0;
}

void CPlayerPed::SetAttachedObject(int index, NEW_ATTACHED_OBJECT* pNewAttachedObject)
{
	if (m_ped && !IsEntityPlaceable(m_ped)) {
		if (m_ped->m_pRwObject) {
			if (index >= 0 && index < 10) {
				int iBoneID = pNewAttachedObject->iBoneID;
				if (iBoneID > 0 && iBoneID <= 18) {
					if (GetObjectSlotState(index)) {
						RemoveAttachedObject(index);
					}

					memcpy(&m_attachedObjects[index].info, pNewAttachedObject, sizeof(NEW_ATTACHED_OBJECT));

					RwMatrix matPlayer = m_ped->GetMatrix().ToRwMatrix();
					auto pNewObject = new CObject(pNewAttachedObject->iModel, {matPlayer.pos.x, matPlayer.pos.y, matPlayer.pos.z}, pNewAttachedObject->vecRot, 200.0f, true);
					if (!pNewObject || !pNewObject->m_entity) {
						delete pNewObject;
						memset(&m_attachedObjects[index].info, 0, sizeof(NEW_ATTACHED_OBJECT));
						return;
					}

					m_attachedObjects[index].object = pNewObject;
					m_attachedObjects[index].slotUsed = true;

					if (pNewAttachedObject->dwMaterialColor1) {
						pNewObject->SetMaterial(-1, 0, nullptr, nullptr, pNewAttachedObject->dwMaterialColor1);
					}
					if (pNewAttachedObject->dwMaterialColor2) {
						pNewObject->SetMaterial(-1, 1, nullptr, nullptr, pNewAttachedObject->dwMaterialColor2);
					}

					pNewObject->SetCollisionChecking(false);
				}
			}
		}
	}
}

void CPlayerPed::RemoveAttachedObject(int index)
{
	if (GetObjectSlotState(index)) {
		if (m_bytePlayerNumber == 0 && EditObject::m_visible && EditObject::m_type == EditObject::TYPE_ATTACHED_OBJECT && EditObject::m_id == index) {
			EditObject::cancelEdit();
		}
		if (m_attachedObjects[index].object) {
			delete m_attachedObjects[index].object;
			m_attachedObjects[index].object = nullptr;
		}

		memset(&m_attachedObjects[index].info, 0, sizeof(NEW_ATTACHED_OBJECT));
		m_attachedObjects[index].slotUsed = false;
	}
}

bool CPlayerPed::GetObjectSlotState(int index)
{
	if (index < 0 || index >= 10) {
		return false;
	}

	return m_attachedObjects[index].slotUsed;
}

bool CPlayerPed::HasAttachedObject()
{
	for (auto& m_attachedObject : m_attachedObjects) {
		if (m_attachedObject.slotUsed) {
			return true;
		}
	}

	return false;
}

void CPlayerPed::RemoveAllAttachedObjects()
{
	if (m_bytePlayerNumber == 0 && EditObject::m_visible && EditObject::m_type == EditObject::TYPE_ATTACHED_OBJECT) {
		EditObject::cancelEdit();
	}
	for (auto& m_attachedObject : m_attachedObjects) {
		if (m_attachedObject.slotUsed) {
			if (m_attachedObject.object) {
				delete m_attachedObject.object;
				m_attachedObject.object = nullptr;
			}

			memset(&m_attachedObject.info, 0, sizeof(NEW_ATTACHED_OBJECT));
			m_attachedObject.slotUsed = false;
		}
	}
}

void CPlayerPed::ProcessAttachedObjects()
{
	bool bAnimUpdated = false;
	RwMatrix boneMatrix{};
	sa::CVector vecOut;

	for (auto& m_attachedObject : m_attachedObjects) {
		if (m_attachedObject.slotUsed) {
			CObject* pAttached = m_attachedObject.object;
			// re-verify the entity still owns its pool slot — the engine
			// recycles object slots, and Remove()/Add() on a recycled entity
			// would splice somebody else's collision records
			bool bEntityAlive = pAttached && pAttached->m_entity
					&& pAttached->m_gtaId
					&& GamePool_Object_GetAt(pAttached->m_gtaId) == pAttached->m_entity;
			if (bEntityAlive) {
				if (IsAdded()) {
					if (!bAnimUpdated) {
						if (m_ped) {
							UpdateElementRpHAnim(m_ped);
							bAnimUpdated = true;
						}
					}

					int iBoneID = m_attachedObject.info.iBoneID;
					int iBoneIndex = 0;

					// skip just this slot — returning here used to freeze
					// every attached object after the broken one
					if (!m_ped || iBoneID < 0 || iBoneID >= 19 || m_ped->m_apBones[iBoneID] == nullptr) {
						continue;
					}

					iBoneIndex = m_ped->m_apBones[iBoneID]->m_nNodeId;

					if (!GetBoneMatrix(&boneMatrix, iBoneIndex)) {
						continue;
					}

					// CPhysical::Remove
					m_attachedObject.object->m_entity->Remove();

					ProjectMatrix(&vecOut, reinterpret_cast<sa::CMatrix*>(&boneMatrix), &m_attachedObject.info.vecOffset);
					boneMatrix.pos = vecOut;

					if (m_attachedObject.info.vecRot.x != 0.0f) {
						RwMatrixRotate(&boneMatrix, 0, m_attachedObject.info.vecRot.x);
					}
					if (m_attachedObject.info.vecRot.y != 0.0f) {
						RwMatrixRotate(&boneMatrix, 1, m_attachedObject.info.vecRot.y);
					}
					if (m_attachedObject.info.vecRot.z != 0.0f) {
						RwMatrixRotate(&boneMatrix, 2, m_attachedObject.info.vecRot.z);
					}

					RwMatrixScale(&boneMatrix, &m_attachedObject.info.vecScale);

					m_attachedObject.object->m_entity->SetMatrix(boneMatrix);
					m_attachedObject.object->UpdateRwMatrixAndFrame();

					// Ped-attached objects are not in CObjectPool, so drive UVAnim here
					pAttached->UpdateUVAnim();

					// CPhysical::add
					m_attachedObject.object->m_entity->Add();
				}
				else {
					m_attachedObject.object->TeleportTo(0.0f, 0.0f, 0.0f);
				}
			}
		}
	}
}

bool CPlayerPed::GetBoneMatrix(RwMatrix* matOut, int iBoneID)
{
	if (!matOut || !m_ped || IsEntityPlaceable(m_ped) || !m_ped->m_pRpClump) {
		return false;
	}
	auto pAnimHierarchy = eng::fn::GetAnimHierarchyFromSkinClump<RpHAnimHierarchy*>( m_ped->m_pRpClump); // GetAnimHierarchyFromSkinClump
	if (!pAnimHierarchy || !pAnimHierarchy->pMatrixArray) {
		return false;
	}
	int iAnimIndex = eng::fn::RpHAnimIDGetIndex( pAnimHierarchy, iBoneID); // RpHAnimIDGetIndex
	if (iAnimIndex < 0 || iAnimIndex >= pAnimHierarchy->numNodes) {
		return false;
	}
	memcpy(matOut, &pAnimHierarchy->pMatrixArray[iAnimIndex], sizeof(RwMatrix));
	return true;
}
