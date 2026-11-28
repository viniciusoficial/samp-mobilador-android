//
// Created by ALYN on 16-Jan-25.
//

#include "RemoveBuilding.h"
#include "Game.h"
#include "Util.h"
#include "Engine/PatchRegistry.h"
#include "Engine/Symbols.h"
#include "../Client.h"
#include <Memory.h>
#include <SA.h>
#include <spdlog/spdlog.h>

void RemoveBuilding::add(uint32_t model, sa::CVector pos, float radius)
{
	spdlog::info("RemoveBuilding::add {}, ({}, {}, {}), {}", model, pos.x, pos.y, pos.z, radius);

	if (model == 19300) {
		return;
	}

	// Deduplicate BEFORE scanning 60k pool slots so repeated GMX removals are O(1)
	for (uint32_t i = 0; i < m_buildingToRemoveCount; ++i) {
		const auto& e = m_buildingToRemove[i];
		if (e.model == model
				&& fabsf(e.pos.x - pos.x) < 0.01f
				&& fabsf(e.pos.y - pos.y) < 0.01f
				&& fabsf(e.pos.z - pos.z) < 0.01f
				&& fabsf(e.radius - radius) < 0.01f) {
			return;
		}
	}

	removeObjectInRange(model, pos, radius);

	if (m_buildingToRemoveCount >= 1000) {
		return;
	}

	m_buildingToRemove[m_buildingToRemoveCount].model = model;
	m_buildingToRemove[m_buildingToRemoveCount].pos = pos;
	m_buildingToRemove[m_buildingToRemoveCount].radius = radius;
	m_buildingToRemoveCount++;
}

void RemoveBuilding::removeObjectInRange(uint32_t model, sa::CVector pos, float radius)
{
	// Match PC building_removal.cpp:79 — remove occluders within 500.0f so small-radius
	// removals don't leave occluders culling replacement custom objects.
	removeOccludersInRadius(pos, 500.0f);

	typedef sa::CPool<uint8_t, uint8_t> RawPool;

	auto scanPool = [&](eng::Sym sym, size_t elemStride) {
		auto** ppPool = eng::as<RawPool**>(sym);
		if (!ppPool || !*ppPool) {
			spdlog::error("RemoveBuilding::removeObjectInRange: {} is null", eng::name(sym));
			return;
		}
		RawPool* pool = *ppPool;
		if (!pool->m_aStorage || !pool->m_aFlags || pool->m_nSize <= 0) {
			return;
		}
		for (int i = 0; i < pool->m_nSize; i++) {
			if (pool->IsFreeSlotAtIndex(i)) continue;
			auto* pEntity = reinterpret_cast<sa::CEntity*>(pool->m_aStorage + static_cast<size_t>(i) * elemStride);
			if (!pEntity || IsEntityPlaceable(pEntity)) continue;
			if (model == static_cast<uint32_t>(-1) || pEntity->m_nModelIndex == model) {
				float fDistance = sa::DistanceBetweenPoints(pEntity->GetPosition(), pos);
				if (fDistance <= radius) {
					eng::fn::CPlantMgr_ColEntityCache_Remove( pEntity);
					// Match PC GameHideObjectEntity: drop BOTH m_placement and m_matrix, and set interior area code = 1
					pEntity->m_placement.m_vPosn.z -= 2000.0f;
					if (pEntity->m_matrix) {
						pEntity->m_matrix->m_pos.z -= 2000.0f;
					}
					pEntity->m_nAreaCode = static_cast<sa::eAreaCodes>(1);
				}
			}
		}
	};

	constexpr size_t kBuildingStride = VER_x32 ? 60 : 96;
	constexpr size_t kDummyStride    = VER_x32 ? 60 : 96;
	constexpr size_t kObjectStride   = VER_x32 ? 420 : 544;

	scanPool(eng::Sym::CPools_ms_pBuildingPool, kBuildingStride);
	scanPool(eng::Sym::CPools_ms_pDummyPool,    kDummyStride);
	scanPool(eng::Sym::CPools_ms_pObjectPool,   kObjectStride);
}

class COccluder {
public:
	int16_t fMidX;
	int16_t fMidY;
	int16_t fMidZ;
	uint16_t fWidthX;
	uint16_t fWidthY;
	uint16_t fHeight;
	char cRotZ;
	char cRotY;
	char cRotX;
	char cPad;
	uint16_t nFlags;
};
VALIDATE_SIZE(COccluder, 0x12);

COccluder* aOccluders = nullptr;
int32_t* pNumOccludersOnMap = nullptr;

void RemoveBuilding::removeOccludersInRadius(sa::CVector pos, float radius)
{
	if (!aOccluders || !pNumOccludersOnMap) return;
	int32_t count = *pNumOccludersOnMap;
	if (count < 1) return;
	if (count > 1000) count = 1000;

	for (int i = 0; i < count; i++) {
		sa::CVector vecOccluderPos;
		vecOccluderPos.x = static_cast<float>(aOccluders[i].fMidX) * 0.25f;
		vecOccluderPos.y = static_cast<float>(aOccluders[i].fMidY) * 0.25f;
		vecOccluderPos.z = static_cast<float>(aOccluders[i].fMidZ) * 0.25f;

		float fDistance = sa::DistanceBetweenPoints(vecOccluderPos, pos);
		if (fDistance <= radius) {
			aOccluders[i].fMidX = 0;
			aOccluders[i].fMidY = 0;
			aOccluders[i].fMidZ = 0;
			aOccluders[i].fWidthX = 0;
			aOccluders[i].fWidthY = 0;
			aOccluders[i].fHeight = 0;
		}
	}
}

bool RemoveBuilding::needsRemove(uint32_t model, sa::CVector pos)
{
	if (m_buildingToRemoveCount == 0) {
		return false;
	}

	for (int i = 0; i < m_buildingToRemoveCount; i++) {
		if (m_buildingToRemove[i].model == -1 || m_buildingToRemove[i].model == model) {
			if (sa::DistanceBetweenPoints(pos, m_buildingToRemove[i].pos) <= m_buildingToRemove[i].radius) {
				return true;
			}
		}
	}

	return false;
}

DECL_HOOK(void, CFileLoader_LoadObjectInstance, sa::CFileObjectInstance* instance, char* name)
{
	if (instance) {
		sa::CVector* pos = &instance->m_vecPosition;
		int* model = &instance->m_nModelId;

		if (RemoveBuilding::needsRemove(*model, *pos)) {
			spdlog::info("RemoveBuilding hook: model {}", *model);
			*model = 19300;
		}
	}

	CFileLoader_LoadObjectInstance(instance, name);
}

void RemoveBuilding::installHooks()
{
	spdlog::info("Installing remove building hooks...");

	aOccluders = eng::as<COccluder*>(eng::Sym::COcclusion_aOccluders);
	pNumOccludersOnMap = eng::as<int32_t*>(eng::Sym::COcclusion_NumOccludersOnMap);

	REG_HOOK(Boot, CFileLoader_LoadObjectInstance, CFileLoader_LoadObjectInstance, "Filter RemoveBuildingForPlayer objects");
}
