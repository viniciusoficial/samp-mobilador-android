#include "OobCollision.h"
#include "Game.h"
#include "Object.h"
#include "Util.h"
#include "Vehicle.h"
#include "Engine/Symbols.h"
#include "../Net/NetGame.h"
#include "../Net/VehiclePool.h"
#include "../Client.h"
#include "Core/PtrNode.h"
#include <SA.h>
#include <spdlog/spdlog.h>
#include <unordered_map>
#include <unordered_set>

/**
 * Out-of-bounds (|x/y| > 3000) collision support is now ONLY about CColStore
 * streaming — repeat-sector registration needs no out-of-bounds handling at
 * all: the hooked CPhysical::Add no longer rejects out-of-range coords, and
 * every engine scanner (CheckCollision / ProcessShift / ProcessVerticalLine /
 * ProcessLineOfSight) hashes raw coords into the same mod-16 repeat-sector
 * buckets, while CPhysical::RemoveAndAdd never had a range rejection in the
 * first place. In-bounds and out-of-bounds entities share the engine's own
 * single path. (The old manual injection chain — InjectIntoRepeatSectors /
 * UnlinkFromRepeatSectors / per-entity injected-record bookkeeping — was
 * deleted together with the GetBoundRect dead-zone remapping it propped up.)
 *
 * CColStore streaming is position-driven: the quadtree only ever matches
 * in-bounds positions, so a shared colModel on an OOB entity stays an empty
 * shell — meaning ZERO collision, not a bound-sphere fallback
 * (ProcessColModels early-outs on null m_pColData). The def array is 255
 * entries and always present, so AddRef / RemoveRef are safe on any valid
 * slot id.
 * The engine's own force-load recipe (LoadAllCollision /
 * LoadAllBoundingBoxes) is RequestModel + LoadAllRequestedModels — the async
 * channel starves col requests queued outside position matching, so we pump
 * it synchronously, at most once a second across all OOB entities.
 */
static uint32_t s_dwOobColPumpTick = 0;
static std::unordered_set<int> s_oobColReloaded;

static bool OobColDefLoaded(int iSlot)
{
	auto ppStore = eng::as<uintptr_t*>(eng::Sym::CColStore_ms_pColPool);
	uintptr_t uStore = ppStore ? *ppStore : 0;
	char* pDefs = uStore ? *reinterpret_cast<char**>(uStore) : nullptr;
	return pDefs && pDefs[iSlot * 44 + 40];
}

bool PinOobColSlot(sa::CColModel* pCM, int& iPinnedSlot)
{
	int iSlot = (pCM && !pCM->m_bIsSingleColDataAlloc)
			? (int) pCM->m_nColSlot : -1;
	if (iSlot <= 0 || iSlot >= 255) iSlot = -1;
	if (iPinnedSlot == iSlot) return false;
	if (iPinnedSlot >= 0) {
		eng::fn::CColStore_RemoveRef( iPinnedSlot);
	}
	iPinnedSlot = -1;
	if (iSlot >= 0) {
		eng::fn::CColStore_AddRef( iSlot);
		iPinnedSlot = iSlot;
	}
	return true;
}

void RequestOobColSlot(sa::CColModel* pCM, int iPinnedSlot,
		uint32_t& dwLastReqTick)
{
	if (!pCM || pCM->m_pColData || iPinnedSlot < 0) return;
	uint32_t dwNow = GetTickCount();
	if (dwNow - dwLastReqTick <= 500) return;
	dwLastReqTick = dwNow;
	// a "loaded" col file whose colData never showed up was parsed at a
	// moment when binding skipped this model — binding only runs inside
	// LoadCol, so force exactly one reload cycle to re-run it
	if (OobColDefLoaded(iPinnedSlot)
			&& s_oobColReloaded.insert(iPinnedSlot).second) {
		eng::fn::CStreaming_RemoveModel(
				25000 + iPinnedSlot);
		spdlog::info("OOBCol: force reload slot={}", iPinnedSlot);
	}
	eng::fn::CStreaming_RequestModel(
			25000 + iPinnedSlot, 0x18);
	if (dwNow - s_dwOobColPumpTick > 1000) {
		s_dwOobColPumpTick = dwNow;
		eng::fn::CStreaming_LoadAllRequestedModels( true);
	}
}

/** one-shot state dump when a pin lands on a slot — reads the CColDef
 *  (refcount @+38 / loaded flag @+40) via ms_pColPool's def array, and
 *  whether streaming has a file registered for 25000+slot at all. */
void LogOobColPin(int iModel, int iSlot)
{
	if (iSlot < 0) return;
	int iRef = -1, iLoaded = -1, iMin = -1, iMax = -1;
	auto ppStore = eng::as<uintptr_t*>(eng::Sym::CColStore_ms_pColPool);
	uintptr_t uStore = ppStore ? *ppStore : 0;
	char* pDefs = uStore ? *reinterpret_cast<char**>(uStore) : nullptr;
	if (pDefs) {
		char* pDef = pDefs + iSlot * 44;
		iMin = (int) *reinterpret_cast<int16_t*>(pDef + 34);
		iMax = (int) *reinterpret_cast<int16_t*>(pDef + 36);
		iRef = (int) *reinterpret_cast<int16_t*>(pDef + 38);
		iLoaded = (int) *reinterpret_cast<uint8_t*>(pDef + 40);
	}
	bool bInImg = eng::fn::CStreaming_IsObjectInCdImage( 25000 + iSlot);
	spdlog::info("OOBCol: model={} pin slot={} ref={} loaded={} "
			"range=[{},{}] inImg={}",
			iModel, iSlot, iRef, iLoaded, iMin, iMax, (int) bInImg);
}

/**
 * Vehicles share the same position-streamed colModel problem as objects:
 * an unpinned slotted colModel stays an empty shell out of bounds, so the
 * vehicle's own mesh collision (against object floors and the map) is zero
 * and it wades straight through geometry. Pin the col slot of every OOB
 * pool vehicle; release the pin when it comes back in bounds or dies.
 * (Their repeat-sector records need no help — the hooked CPhysical::Add
 * accepts any coordinate and ProcessShift/ProcessCollision re-register
 * moving vehicles natively through the hooked RemoveAndAdd.)
 */
struct OobVehicleColPin {
	int pinnedColSlot = -1;
	uint32_t lastColReqTick = 0;
};
static std::unordered_map<sa::CVehicle*, OobVehicleColPin> s_oobVehColPins;

void ProcessOobVehicleColPins()
{
	CVehiclePool* pVehiclePool = pNetGame ? pNetGame->GetVehiclePool() : nullptr;
	if (!pVehiclePool) return;

	std::unordered_map<sa::CVehicle*, bool> live;
	for (int i = 0; i < MAX_VEHICLES; i++) {
		CVehicle* pVeh = pVehiclePool->GetAt((VEHICLEID) i);
		if (!pVeh || !pVeh->m_pVehicle || !pVeh->VerifyInstance()) continue;
		sa::CVehicle* pEnt = pVeh->m_pVehicle;
		live[pEnt] = true;

		const sa::CVector& vp = pEnt->GetPosition();
		bool bOob = vp.x < -3000.0f || vp.x > 3000.0f
				|| vp.y < -3000.0f || vp.y > 3000.0f;
		auto it = s_oobVehColPins.find(pEnt);
		if (!bOob) {
			if (it != s_oobVehColPins.end()) {
				if (it->second.pinnedColSlot >= 0) {
					eng::fn::CColStore_RemoveRef(it->second.pinnedColSlot);
				}
				s_oobVehColPins.erase(it);
			}
			continue;
		}

		auto& pin = s_oobVehColPins[pEnt];
		sa::CBaseModelInfo* pMI = GetModelInfoByID(pEnt->m_nModelIndex);
		sa::CColModel* pCM = pMI ? pMI->m_pColModel : nullptr;
		if (PinOobColSlot(pCM, pin.pinnedColSlot)) {
			LogOobColPin(pEnt->m_nModelIndex, pin.pinnedColSlot);
		}
		RequestOobColSlot(pCM, pin.pinnedColSlot, pin.lastColReqTick);
	}

	for (auto it = s_oobVehColPins.begin(); it != s_oobVehColPins.end();) {
		if (live.count(it->first)) ++it;
		else {
			if (it->second.pinnedColSlot >= 0) {
				eng::fn::CColStore_RemoveRef(it->second.pinnedColSlot);
			}
			it = s_oobVehColPins.erase(it);
		}
	}
}
