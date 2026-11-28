#pragma once

#include "Engine/Symbols.h"
#include "Core/PtrNode.h"
#include <SA.h>
#include <cstdint>

namespace sa_world {

// Shared canonical helper for linking a CPhysical entity into GTA:SA's 16x16
// CWorld::ms_aRepeatSectors hash grid, replicating CPhysical::Add's insertion
// for non-big-building entities — minus two stock flaws:
//   - stock inserts one node per overlapped cell WITHOUT deduplicating hash
//     groups (a 2500m custom map chunk inserts 1,100+ duplicate nodes into
//     only 256 buckets); we dedupe with a 256-bit mask, max 1 node per group
//   - stock never null-checks operator new() on CEntryInfoNode /
//     CPtrNodeDoubleLink, crashing with SIGSEGV if the pool ever fills up
//
// The bucket hash is (x & 0xF) | ((y & 0xF) << 4) — pure mod-16 bitwise math,
// so ANY sector coord (including negative out-of-bounds ones) lands in a
// valid bucket with no additional range handling. Iterating more than 16
// cells on an axis can only revisit residues already covered, so the loop
// is clamped to 16 cells per axis (256 iterations worst case).
//
// Used by WorldHooks::CPhysical_Add / CPhysical_RemoveAndAdd replacements.
struct RepeatSectorLinkResult {
	int added = 0;
	bool poolExhausted = false;
};

inline RepeatSectorLinkResult linkEntityToRepeatSectors(
	sa::CPhysical* entity,
	int minX, int minY, int maxX, int maxY)
{
	RepeatSectorLinkResult result{};
	if (!entity || minX > maxX || minY > maxY) {
		return result;
	}

	constexpr size_t kPtrSize = sizeof(void*);
	constexpr size_t kRepeatSectorSize = 3 * kPtrSize;
	size_t listOff = 0;
	switch (entity->m_nType) {
		case sa::ENTITY_TYPE_VEHICLE: listOff = 0; break;
		case sa::ENTITY_TYPE_PED:     listOff = kPtrSize; break;
		case sa::ENTITY_TYPE_OBJECT:  listOff = 2 * kPtrSize; break;
		default: return result;
	}

	const uintptr_t pSectors = eng::addr(eng::Sym::CWorld_ms_aRepeatSectors);
	if (!pSectors) {
		return result;
	}

	// cells repeat mod 16 — iterating further can never reach a new group
	if (maxX - minX > 15) maxX = minX + 15;
	if (maxY - minY > 15) maxY = minY + 15;

	uint32_t seenMask[8] = {};
	for (int cy = minY; cy <= maxY; ++cy) {
		for (int cx = minX; cx <= maxX; ++cx) {
			const int group = ((cy & 0xF) << 4) | (cx & 0xF);
			const uint32_t bit = 1u << (group & 31);
			if (seenMask[group >> 5] & bit) {
				continue;
			}
			seenMask[group >> 5] |= bit;

			auto* pInfo = eng::fn::CEntryInfoNode_OpNew<CEntryInfoNode*>(sizeof(CEntryInfoNode));
			auto* pNode = eng::fn::CPtrNodeDoubleLink_OpNew<sa::CPtrNodeDoubleLink*>(sizeof(sa::CPtrNodeDoubleLink));
			if (!pInfo || !pNode) {
				if (pNode) eng::fn::CPtrNodeDoubleLink_OpDelete(pNode);
				if (pInfo) eng::fn::CEntryInfoNode_OpDelete(pInfo);
				result.poolExhausted = true;
				return result;
			}

			auto** ppHead = reinterpret_cast<sa::CPtrNodeDoubleLink**>(
				pSectors + static_cast<size_t>(group) * kRepeatSectorSize + listOff);

			pNode->m_item = entity;
			pNode->m_next = *ppHead;
			pNode->m_prev = nullptr;
			if (*ppHead) (*ppHead)->m_prev = pNode;
			*ppHead = pNode;

			pInfo->m_pDoubleLinkList = reinterpret_cast<CPtrListDoubleLink*>(ppHead);
			pInfo->m_pDoubleLink = reinterpret_cast<CPtrNodeDoubleLink*>(pNode);
			pInfo->m_pRepeatSector = reinterpret_cast<CRepeatSector*>(
				pSectors + static_cast<size_t>(group) * kRepeatSectorSize);
			pInfo->m_pPrevious = nullptr;
			pInfo->m_pNext = entity->m_pCollisionList.m_pNode;
			if (pInfo->m_pNext) pInfo->m_pNext->m_pPrevious = pInfo;
			entity->m_pCollisionList.m_pNode = pInfo;

			++result.added;
		}
	}
	return result;
}

} // namespace sa_world
