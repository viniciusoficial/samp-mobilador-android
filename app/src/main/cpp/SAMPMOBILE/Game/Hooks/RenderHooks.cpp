#include "HookDomains.h"
#include "../Hooks.h"
#include "../Patches.h"
#include "../Game.h"
#include "../Camera.h"
#include "../Object.h"
#include "../ObjectUVAnim.h"
#include "../PlayerPed.h"
#include "../Util.h"
#include "../Engine/PatchRegistry.h"
#include "../Engine/Symbols.h"
#include "../../Addr.h"
#include "../../Net/NetGame.h"
#include "../../Net/GangZonePool.h"
#include "../../Net/TextDrawPool.h"
#include "../../Client.h"
#include "../../Settings.h"
#include <Memory.h>
#include <SA.h>
#include <spdlog/spdlog.h>
#include <EGL/egl.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>

DECL_HOOK(void, CRadar_ClearBlip, int32_t nIndex)
{
	uintptr_t dwRetAddr = 0;
	GET_LR(dwRetAddr);

	if ((uint16_t)nIndex > 249)
	{
		spdlog::warn("Invalid blip ID ({}). Called from 0x{:X}", nIndex, dwRetAddr);
		return;
	}

	return CRadar_ClearBlip(nIndex);
}

DECL_HOOK(sa::CPlayerInfo *, GetPlayerInfoForThisPlayerPed, sa::CPed *ped)
{
	if (!WORLD_PLAYERS || !ped) return nullptr;

	for (int i = 0; i < PLAYER_PED_SLOTS; i++)
	{
		if (WORLD_PLAYERS[i].m_pPed == ped)
		{
			return &WORLD_PLAYERS[i];
		}
	}

	return nullptr;
}

DECL_HOOK(uintptr_t *, RQCommand_rqSetAlphaTest, uintptr_t *result)
{
	if (result) {
		*result += 8;
	}
	return result;
}

DECL_HOOK(int, CWorld_FindPlayerSlotWithPedPointer, sa::CPed *pPlayersPed)
{
	if (!WORLD_PLAYERS || !pPlayersPed) return -1;

	for (int i = 0; i < PLAYER_PED_SLOTS; ++i)
	{
		if (WORLD_PLAYERS[i].m_pPed == pPlayersPed)
		{
			return i;
		}
	}
	return -1;
}

std::atomic<int> g_lastObjectModel{-1};

DECL_HOOK(int32_t, CObject_Render, sa::CObject *_this)
{
	if (_this)
	{
		g_lastObjectModel.store(_this->m_nModelIndex, std::memory_order_relaxed);
	}

	// object material (covers both CObjectPool objects and CPlayerPed attached objects in O(1))
	CObject *pObject = nullptr;
	if (_this && !IsEntityPlaceable(_this))
	{
		pObject = CObject::FindByGtaEntity(_this);
		if (pObject)
		{
			pObject->TryChangeToCustomObjectMaterial();
		}
	}

	int32_t result = CObject_Render(_this);
	g_lastObjectModel.store(-1, std::memory_order_relaxed);

	if (pObject)
	{
		pObject->ChangeToOriginalObjectMaterial();
	}

	return result;
}

// Manual SA-MP object rendering — covers the two cases the engine's own
// CRenderer::ScanWorld never reaches:
//   - out-of-bounds (m_bForceRender) objects: ScanWorld only scans the
//     in-bounds sector grid around the camera, OOB positions are never scanned
//   - in-bounds objects beyond the native scan/LOD range (~300m cap) but
//     within the server-specified m_fDrawDistance
// Mirrors the PC SA-MP pipeline (CObjectPool::Draw/DrawLast + CObject::Draw):
// server draw-distance culling, sphere-frustum culling through the engine's
// own CCamera::IsSphereVisible, opaque objects drawn immediately, transparent
// (drawLast = model flags bit1) objects collected and drawn far-to-near so
// alpha blending layers correctly.
namespace {

struct ManualObjectRenderEntry {
	CObject* obj;
	float distSq;
};

// render is single-threaded on the game thread — static storage avoids a
// 16KB+ stack allocation every frame
ManualObjectRenderEntry s_transparentObjects[MAX_OBJECTS];
int s_transparentObjectCount = 0;

} // namespace

DECL_HOOK(void, CRenderer_RenderEverythingBarRoads)
{
	CRenderer_RenderEverythingBarRoads();

	if (!pNetGame)
	{
		return;
	}
	CObjectPool *pObjectPool = pNetGame->GetObjectPool();
	if (!pObjectPool)
	{
		return;
	}

	RwMatrix matCam{};
	bool hasCam = false;
	if (pGame && pGame->GetCamera())
	{
		pGame->GetCamera()->GetMatrix(&matCam);
		hasCam = true;
	}
	if (!hasCam)
	{
		return;
	}
	float lodScale = 1.0f;
	if (auto *pLodScale = eng::as<float *>(eng::Sym::CRenderer_ms_lodDistScale))
	{
		if (std::isfinite(*pLodScale) && *pLodScale > 0.1f)
		{
			lodScale = *pLodScale;
		}
	}
	sa::CCamera* pEngineCam = &sa::getTheCamera();

	s_transparentObjectCount = 0;

	for (OBJECTID i = 0; i < MAX_OBJECTS; i++)
	{
		CObject *pObject = pObjectPool->GetAt(i);
		if (!pObject || pObject->IsTimedHidden())
		{
			continue;
		}
		sa::CPhysical *pEntity = pObject->m_entity;
		if (!pEntity || !pEntity->m_pRwObject || IsEntityPlaceable(pEntity))
		{
			continue;
		}

		const sa::CVector &pos = pEntity->GetPosition();
		const float dx = pos.x - matCam.pos.x;
		const float dy = pos.y - matCam.pos.y;
		const float dz = pos.z - matCam.pos.z;
		const float distSq = dx * dx + dy * dy + dz * dz;

		// server-specified draw distance is position-independent: it caps
		// OOB objects exactly like in-bounds ones (an unset 0 means no
		// server-imposed cap — OOB map structures stay visible as before)
		const float drawDist = pObject->m_fDrawDistance;
		if (drawDist > 0.0f && distSq > drawDist * drawDist)
		{
			continue;
		}

		sa::CBaseModelInfo *pModelInfo = GetModelInfoByID(pObject->GetModelIndex());

		if (!pObject->m_bForceRender)
		{
			// in-bounds objects need manual rendering only when the server
			// asked for a draw distance beyond what the engine's ScanWorld
			// covers — inside the native range the engine already drew them
			if (drawDist <= 0.0f)
			{
				continue;
			}
			float nativeDist = 300.0f;
			if (pModelInfo && pModelInfo->m_lodDistance > 0.0f)
			{
				const float scaled = pModelInfo->m_lodDistance * lodScale;
				if (scaled < nativeDist)
				{
					nativeDist = scaled;
				}
			}
			const float minFarDist = nativeDist * 0.9f;
			if (distSq <= minFarDist * minFarDist)
			{
				continue;
			}
		}

		// sphere-frustum culling with the collision bound sphere transformed
		// into world space — the same test PC SA-MP applies per object in
		// CObject::Draw (GameTestCameraSphere wraps CCamera::IsSphereVisible)
		float boundRadius = 50.0f;
		sa::CVector worldCentre = pos;
		if (pModelInfo && pModelInfo->m_pColModel)
		{
			const sa::CSphere &sphere = pModelInfo->m_pColModel->m_boundSphere;
			if (sphere.m_fRadius > 0.0f)
			{
				boundRadius = sphere.m_fRadius;
			}
			const sa::CVector &c = sphere.m_vecCenter;
			if (pEntity->m_matrix)
			{
				const sa::CVector &right = pEntity->GetRight();
				const sa::CVector &fwd = pEntity->GetForward();
				const sa::CVector &up = pEntity->GetUp();
				worldCentre.x = pos.x + right.x * c.x + fwd.x * c.y + up.x * c.z;
				worldCentre.y = pos.y + right.y * c.x + fwd.y * c.y + up.y * c.z;
				worldCentre.z = pos.z + right.z * c.x + fwd.z * c.y + up.z * c.z;
			}
		}
		if (!eng::fn::CCamera_IsSphereVisible( pEngineCam, &worldCentre, boundRadius))
		{
			continue;
		}

		// model flags bit1 (drawLast) marks alpha/transparent objects — they
		// must render after all opaque geometry far-to-near
		const bool bDrawLast = pModelInfo && ((pModelInfo->m_flags >> 1) & 1);
		if (bDrawLast && s_transparentObjectCount < MAX_OBJECTS)
		{
			s_transparentObjects[s_transparentObjectCount++] = {pObject, distSq};
			continue;
		}

		pObject->UpdateRwMatrixAndFrame();
		pObject->Render();
	}

	if (s_transparentObjectCount > 1)
	{
		std::sort(s_transparentObjects, s_transparentObjects + s_transparentObjectCount,
			[](const ManualObjectRenderEntry &a, const ManualObjectRenderEntry &b) {
				return a.distSq > b.distSq;
			});
	}
	for (int t = 0; t < s_transparentObjectCount; ++t)
	{
		s_transparentObjects[t].obj->UpdateRwMatrixAndFrame();
		s_transparentObjects[t].obj->Render();
	}
}

DECL_HOOK(void, CWorld_ProcessPedsAfterPreRender)
{
	CWorld_ProcessPedsAfterPreRender();

	if (pNetGame)
	{
		CPlayerPool *pPlayerPool = pNetGame->GetPlayerPool();
		if (pPlayerPool)
		{
			pPlayerPool->ProcessAttachedObjects();
		}
	}
}

static void SaveAndForceUVWrap(void*& savedAddr)
{
	savedAddr = (void*) (uintptr_t) rwTEXTUREADDRESSWRAP;
	eng::fn::RwRenderStateGet<RwBool>( rwRENDERSTATETEXTUREADDRESS, &savedAddr);
	if (savedAddr != (void*) (uintptr_t) rwTEXTUREADDRESSWRAP) {
		RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, (void*) (uintptr_t) rwTEXTUREADDRESSWRAP);
	}
}

static void RestoreUVWrap(void* savedAddr)
{
	if (savedAddr != (void*) (uintptr_t) rwTEXTUREADDRESSWRAP) {
		RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, savedAddr);
	}
}

DECL_HOOK(RpMaterial*, MaterialUpdateUVAnimCB, RpMaterial* material, void* data)
{
	return DriveUVAnimMaterialCB(material, data);
}

DECL_HOOK(void, rpMatFXOpenGLAllInOneRenderCB, RwResEntry *resEntry, uintptr_t object, uint8_t type, uint32_t flags)
{
	if (!resEntry || !object)
	{
		return;
	}

	bool bHasDualUV = false;
	RwMatrix* uvMat = (type == 1 /*rpATOMIC*/)
			? FindAtomicUVAnimMatrix(reinterpret_cast<RpAtomic*>(object), &bHasDualUV)
			: nullptr;
	void* savedAddr = nullptr;
	if (uvMat) {
		// _rwOpenGLSetRenderStateNoExtras applies the global TEXTUREADDRESS state
		// rather than RwTexture::filterAddressing; force WRAP so scrolling UVs tile.
		SaveAndForceUVWrap(savedAddr);
		// Stock _rpMatFXOpenGLAllInOneRenderCB only switches on effects 1..5 and
		// drops effect 6 (rpMATFXEFFECTDUALUVTRANSFORM) to default render.
		if (bHasDualUV) {
			eng::fn::emu_SetTextureMatrixEnabled( (float*) uvMat, (uint8_t) 1);
		}
	}
	rpMatFXOpenGLAllInOneRenderCB(resEntry, object, type, flags);
	if (uvMat) {
		if (bHasDualUV) {
			eng::fn::emu_SetTextureMatrixEnabled( (float*) nullptr, (uint8_t) 0);
		}
		RestoreUVWrap(savedAddr);
	}
}

DECL_HOOK(int, CustomPipeRenderCB, RwResEntry *resEntry, uintptr_t object, uint8_t type, uint32_t flags)
{
	if (!resEntry || !object)
	{
		spdlog::warn("CustomPipeRenderCB: Prevent crash - resEntry or object is null");
		return 0;
	}

	RwMatrix* uvMat = (type == 1 /*rpATOMIC*/) ? FindAtomicUVAnimMatrix(reinterpret_cast<RpAtomic*>(object)) : nullptr;
	void* savedAddr = nullptr;
	if (uvMat) {
		SaveAndForceUVWrap(savedAddr);
		eng::fn::emu_SetTextureMatrixEnabled( (float*) uvMat, (uint8_t) 1);
	}
	int ret = CustomPipeRenderCB(resEntry, object, type, flags);
	if (uvMat) {
		eng::fn::emu_SetTextureMatrixEnabled( (float*) nullptr, (uint8_t) 0);
		RestoreUVWrap(savedAddr);
	}
	return ret;
}

DECL_HOOK(int, CustomBuildingPipeRenderCB, RwResEntry *resEntry, uintptr_t object, uint8_t type, uint32_t flags)
{
	if (!resEntry || !object)
	{
		return 0;
	}

	RwMatrix* uvMat = (type == 1 /*rpATOMIC*/) ? FindAtomicUVAnimMatrix(reinterpret_cast<RpAtomic*>(object)) : nullptr;
	void* savedAddr = nullptr;
	if (uvMat) {
		SaveAndForceUVWrap(savedAddr);
		eng::fn::emu_SetTextureMatrixEnabled( (float*) uvMat, (uint8_t) 1);
	}
	int ret = CustomBuildingPipeRenderCB(resEntry, object, type, flags);
	if (uvMat) {
		eng::fn::emu_SetTextureMatrixEnabled( (float*) nullptr, (uint8_t) 0);
		RestoreUVWrap(savedAddr);
	}
	return ret;
}

DECL_HOOK(int, rxOpenGLDefaultAllInOneRenderCB, RwResEntry *resEntry, uintptr_t object, uint8_t type, uint32_t flags)
{
	if (!resEntry || !object)
	{
		spdlog::warn("rxOpenGLDefaultAllInOneRenderCB: Prevent crash - resEntry or object is null");
		return 0;
	}

	RwMatrix* uvMat = (type == 1 /*rpATOMIC*/) ? FindAtomicUVAnimMatrix(reinterpret_cast<RpAtomic*>(object)) : nullptr;
	void* savedAddr = nullptr;
	if (uvMat) {
		SaveAndForceUVWrap(savedAddr);
		eng::fn::emu_SetTextureMatrixEnabled( (float*) uvMat, (uint8_t) 1);
	}
	int ret = rxOpenGLDefaultAllInOneRenderCB(resEntry, object, type, flags);
	if (uvMat) {
		eng::fn::emu_SetTextureMatrixEnabled( (float*) nullptr, (uint8_t) 0);
		RestoreUVWrap(savedAddr);
	}
	return ret;
}

DECL_HOOK(RwBool, _RwTextureDestroy, uintptr_t texture)
{
	if (!texture)
	{
		spdlog::warn("RwTextureDestroy: Prevent crash");
		return 0;
	}
	return _RwTextureDestroy(texture);
}

DECL_HOOK(void *, RwFrameAddChild, void *parent, void *child)
{
	if (!parent || !child)
	{
		spdlog::warn("RwFrameAddChild: Prevent crash");
		return nullptr;
	}
	return RwFrameAddChild(parent, child);
}

DECL_HOOK(uint8_t *, RLEDecompress, uint8_t *dest, unsigned int destSize, const uint8_t *src, unsigned int segSize, unsigned int escape)
{
	if (!dest || !src)
	{
		spdlog::warn("RLEDecompress: Prevent crash");
		return nullptr;
	}
	return RLEDecompress(dest, destSize, src, segSize, escape);
}

DECL_HOOK(int, RwResourcesFreeResEntry, RwResEntry *a1)
{
	if (!a1)
	{
		spdlog::warn("RwResourcesFreeResEntry: Prevent crash");
		return 0;
	}
	return RwResourcesFreeResEntry(a1);
}

static void SanitizeAlphaLinkList(void *listPtr, const char *name)
{
	if (!listPtr) return;
	// CLinkList<AlphaObjectInfo>: 4 sentinel CLinks (usedHead, usedTail, freeHead, freeTail).
	// On arm64: sizeof(CLink)=40 (item 24B, prev +24, next +32).
	// On arm32: sizeof(CLink)=20 (item 12B, prev +12, next +16).
	constexpr size_t kLinkSize = VER_x32 ? 20 : 40;
	constexpr size_t kPrevOff  = VER_x32 ? 12 : 24;
	constexpr size_t kNextOff  = VER_x32 ? 16 : 32;

	uint8_t *head = reinterpret_cast<uint8_t *>(listPtr);
	uint8_t *tail = head + kLinkSize;

	auto getNext = [](uint8_t *node) -> uint8_t * {
		return *reinterpret_cast<uint8_t **>(node + kNextOff);
	};
	auto getPrev = [](uint8_t *node) -> uint8_t * {
		return *reinterpret_cast<uint8_t **>(node + kPrevOff);
	};
	auto setNext = [](uint8_t *node, uint8_t *v) {
		*reinterpret_cast<uint8_t **>(node + kNextOff) = v;
	};
	auto setPrev = [](uint8_t *node, uint8_t *v) {
		*reinterpret_cast<uint8_t **>(node + kPrevOff) = v;
	};

	uint8_t *cur = getNext(head);
	bool corrupted = (cur == nullptr);
	int steps = 0;
	while (!corrupted && cur != tail)
	{
		if (++steps > 4096)
		{
			corrupted = true;
			break;
		}
		uint8_t *p = getPrev(cur);
		uint8_t *n = getNext(cur);
		if (!p || !n)
		{
			corrupted = true;
			break;
		}
		cur = n;
	}

	if (corrupted)
	{
		static int s_logBudget = 20;
		if (s_logBudget > 0)
		{
			--s_logBudget;
			spdlog::warn("CVisibilityPlugins::InitAlphaEntityList: repaired corrupted {}", name);
		}
		setNext(head, tail);
		setPrev(tail, head);
	}
}

void SanitizeAllAlphaLinkLists()
{
	static void *s_alphaList = eng::as<void *>(eng::Sym::CVisibilityPlugins_m_alphaList);
	static void *s_boatList = eng::as<void *>(eng::Sym::CVisibilityPlugins_m_alphaBoatAtomicList);
	static void *s_entityList = eng::as<void *>(eng::Sym::CVisibilityPlugins_m_alphaEntityList);
	static void *s_underwaterList = eng::as<void *>(eng::Sym::CVisibilityPlugins_m_alphaUnderwaterEntityList);
	static void *s_lastList = eng::as<void *>(eng::Sym::CVisibilityPlugins_m_alphaReallyDrawLastList);

	SanitizeAlphaLinkList(s_alphaList, "m_alphaList");
	SanitizeAlphaLinkList(s_boatList, "m_alphaBoatAtomicList");
	SanitizeAlphaLinkList(s_entityList, "m_alphaEntityList");
	SanitizeAlphaLinkList(s_underwaterList, "m_alphaUnderwaterEntityList");
	SanitizeAlphaLinkList(s_lastList, "m_alphaReallyDrawLastList");
}

DECL_HOOK(int, rxOpenGLDefaultAtomicAllInOneNode, void *self, const void *params)
{
	if (params)
	{
		const auto *atomic = *reinterpret_cast<const uint8_t *const *>(params);
		if (atomic)
		{
			// Disassembly-verified RenderWare offsets in libGTASA.so 2.10:
			//   arm32: _rxOpenGLDefaultAtomicAllInOneNode @ 0x220eb0, InstanceVertexArray @ 0x221060
			//   arm64: _rxOpenGLDefaultAtomicAllInOneNode @ 0x2cb5e0, InstanceVertexArray @ 0x2cb820
			// rpworld.h's RpGeometry/RpAtomic struct layouts drift by 8 bytes on arm64, so use raw offsets.
			constexpr size_t kAtomicRepEntryOff  = VER_x32 ? 20  : 40;
			constexpr size_t kAtomicGeomOff      = VER_x32 ? 24  : 48;
			constexpr size_t kAtomicStartMtOff   = VER_x32 ? 80  : 124;
			constexpr size_t kAtomicEndMtOff     = VER_x32 ? 82  : 126;
			constexpr size_t kGeomFlagsOff       = VER_x32 ? 8   : 16;
			constexpr size_t kGeomNumVertsOff    = VER_x32 ? 20  : 28;
			constexpr size_t kGeomNumMtOff       = VER_x32 ? 24  : 32;
			constexpr size_t kGeomMeshOff        = VER_x32 ? 88  : 144;
			constexpr size_t kGeomRepEntryOff    = VER_x32 ? 92  : 152;
			constexpr size_t kGeomMorphTargetOff = VER_x32 ? 96  : 160;
			constexpr size_t kMeshNumMeshesOff   = 4;
			constexpr size_t kMeshSerialOff      = 6;
			constexpr size_t kResEntrySerialOff  = VER_x32 ? 24  : 48;
			constexpr size_t kMorphTargetSize    = VER_x32 ? 28  : 40;
			constexpr size_t kMorphVertsOff      = VER_x32 ? 20  : 24;
			constexpr uint32_t kRpGeometryNative = 0x01000000u;

			const auto *geom = *reinterpret_cast<const uint8_t *const *>(atomic + kAtomicGeomOff);
			if (geom)
			{
				const uint32_t geomFlags = *reinterpret_cast<const uint32_t *>(geom + kGeomFlagsOff);
				const int32_t numVertices = *reinterpret_cast<const int32_t *>(geom + kGeomNumVertsOff);
				const auto *mesh = *reinterpret_cast<const uint8_t *const *>(geom + kGeomMeshOff);
				// Mobile GTA:SA DFFs use rpGEOMETRYNATIVE (verts stored in native GL stream, morphTarget.verts is intentionally null)
				if ((geomFlags & kRpGeometryNative) == 0 &&
					numVertices >= 1 && mesh && *reinterpret_cast<const uint16_t *>(mesh + kMeshNumMeshesOff) != 0)
				{
					const int32_t numMorphTargets = *reinterpret_cast<const int32_t *>(geom + kGeomNumMtOff);
					const auto *repEntry = (numMorphTargets == 1)
						? *reinterpret_cast<const uint8_t *const *>(geom + kGeomRepEntryOff)
						: *reinterpret_cast<const uint8_t *const *>(atomic + kAtomicRepEntryOff);

					bool needsInstance = (repEntry == nullptr);
					if (!needsInstance)
					{
						const uint16_t entrySerial = *reinterpret_cast<const uint16_t *>(repEntry + kResEntrySerialOff);
						const uint16_t meshSerial = *reinterpret_cast<const uint16_t *>(mesh + kMeshSerialOff);
						if (entrySerial != meshSerial)
						{
							needsInstance = true;
						}
					}

					if (needsInstance)
					{
						const int16_t startMt = *reinterpret_cast<const int16_t *>(atomic + kAtomicStartMtOff);
						const int16_t endMt = *reinterpret_cast<const int16_t *>(atomic + kAtomicEndMtOff);
						const auto *morphTargets = *reinterpret_cast<const uint8_t *const *>(geom + kGeomMorphTargetOff);

						auto hasValidVerts = [&](int16_t mt) -> bool {
							if (!morphTargets || mt < 0 || mt >= numMorphTargets) return false;
							const void *verts = *reinterpret_cast<const void *const *>(
								morphTargets + static_cast<size_t>(mt) * kMorphTargetSize + kMorphVertsOff);
							return verts != nullptr;
						};

						if (!hasValidVerts(startMt) || (endMt != startMt && !hasValidVerts(endMt)))
						{
							static int s_skipLogBudget = 20;
							if (s_skipLogBudget > 0)
							{
								--s_skipLogBudget;
								spdlog::warn("rxOpenGLDefaultAtomicAllInOneNode: skip atomic with null verts & no repEntry (model={}, numMt={}, startMt={}, endMt={})",
									g_lastObjectModel.load(std::memory_order_relaxed), numMorphTargets, startMt, endMt);
							}
							return 1;
						}
					}
				}
			}
		}
	}
	return rxOpenGLDefaultAtomicAllInOneNode(self, params);
}

DECL_HOOK(int, TextureDatabaseRuntime_GetEntry, uintptr_t _this, const char *a2, bool *a3)
{
	if (!_this)
	{
		spdlog::warn("TextureDatabaseRuntime_GetEntry: Prevent crash");
		return -1;
	}
	return TextureDatabaseRuntime_GetEntry(_this, a2, a3);
}

float *m_f3rdPersonCHairMultX, *m_f3rdPersonCHairMultY;

DECL_HOOK(void, DrawCrosshair)
{
	if (Settings::crosshairStyle() != 0)
	{
		// Custom crosshair (1..3) drawn via ImGui in UI::drawList(), or Hidden (4)
		return;
	}
	if (!m_f3rdPersonCHairMultX || !m_f3rdPersonCHairMultY)
	{
		DrawCrosshair();
		return;
	}
	float save1 = *m_f3rdPersonCHairMultX;
	*m_f3rdPersonCHairMultX = 0.52f;

	float save2 = *m_f3rdPersonCHairMultY;
	*m_f3rdPersonCHairMultY = 0.435f;

	DrawCrosshair();

	*m_f3rdPersonCHairMultX = save1;
	*m_f3rdPersonCHairMultY = save2;
}

DECL_HOOK(float, CDraw_CalculateAspectRatio)
{
	float ar = CDraw_CalculateAspectRatio();
	int mode = Settings::aspectRatio();
	if (mode > 0)
	{
		switch (mode)
		{
		case 1: ar = 16.0f / 9.0f; break;
		case 2: ar = 20.0f / 9.0f; break;
		case 3: ar = 21.0f / 9.0f; break;
		case 4: ar = 16.0f / 10.0f; break;
		case 5: ar = 4.0f / 3.0f; break;
		default: break;
		}
		if (auto* pAr = eng::as<float*>(eng::Sym::CDraw_ms_fAspectRatio))
		{
			*pAr = ar;
		}
	}
	return ar;
}

void Hooks::installCrosshairFixHooks()
{
	spdlog::info("Installing crosshair fix hooks...");

	m_f3rdPersonCHairMultX = eng::as<float *>(eng::Sym::CCamera_m_f3rdPersonCHairMultX);
	m_f3rdPersonCHairMultY = eng::as<float *>(eng::Sym::CCamera_m_f3rdPersonCHairMultY);

	REG_HOOK(Boot, CHud_DrawCrossHairs, DrawCrosshair, "Custom crosshair position & visibility");
}

DECL_HOOK(void*, RenderQueue_Ctor, void* self)
{
	void* ret = RenderQueue_Ctor(self);
	if (self)
	{
		// Mobile libGTASA.so allocates only 1MB (512KB on low-spec) for RenderQueue's inline
		// command ring buffer. High-poly 0.3.DL models upload >1MB vertex buffers inline via
		// RQVertexBuffer::Update() -> Flush() -> unconditional memcpy, overflowing into the
		// trailing Scudo guard page (SEGV_ACCERR). Expand to 8MB here before
		// RenderQueue::Initialize() spawns GraphicsThread (single-threaded, zero race).
		constexpr size_t kStartOff  = VER_x32 ? 0x260u : 0x3F8u;
		constexpr size_t kEndOff    = VER_x32 ? 0x264u : 0x400u;
		constexpr size_t kReadOff   = VER_x32 ? 0x268u : 0x408u;
		constexpr size_t kCommitOff = VER_x32 ? 0x270u : 0x418u;
		constexpr size_t kCursorOff = VER_x32 ? 0x274u : 0x420u;
		constexpr size_t kTargetBytes   = 8u * 1024u * 1024u;
		constexpr size_t kFallbackBytes = 4u * 1024u * 1024u;

		auto* base = static_cast<uint8_t*>(self);
		auto& bufStart  = *reinterpret_cast<uint8_t**>(base + kStartOff);
		auto& bufEnd    = *reinterpret_cast<uint8_t**>(base + kEndOff);
		auto& readPtr   = *reinterpret_cast<uint8_t**>(base + kReadOff);
		auto& commitPtr = *reinterpret_cast<uint8_t**>(base + kCommitOff);
		auto& cursorPtr = *reinterpret_cast<uint8_t**>(base + kCursorOff);

		if (bufStart && bufEnd > bufStart)
		{
			const size_t oldCap = static_cast<size_t>(bufEnd - bufStart);
			if (oldCap < kTargetBytes)
			{
				size_t newCap = kTargetBytes;
				auto* newBuf = static_cast<uint8_t*>(std::malloc(newCap));
				if (!newBuf)
				{
					newCap = kFallbackBytes;
					newBuf = static_cast<uint8_t*>(std::malloc(newCap));
				}
				if (newBuf)
				{
					std::free(bufStart);
					bufStart  = newBuf;
					bufEnd    = newBuf + newCap;
					readPtr   = newBuf;
					commitPtr = newBuf;
					cursorPtr = newBuf;
					spdlog::info("RenderQueue buffer expanded: {} KB -> {} KB", oldCap / 1024u, newCap / 1024u);
				}
			}
		}
	}
	return ret;
}

void InstallRenderHooks()
{
	REG_HOOK(Boot, RenderQueue_Ctor, RenderQueue_Ctor, "Expand RenderQueue ring buffer for >1MB 0.3.DL VBOs");
	REG_HOOK(Boot, CDraw_CalculateAspectRatio, CDraw_CalculateAspectRatio, "Custom aspect ratio override");
	REG_HOOK(Boot, CObject_Render, CObject_Render, "Object custom material/text & UVAnim");
	REG_HOOK(Boot, CRenderer_RenderEverythingBarRoads, CRenderer_RenderEverythingBarRoads, "Force-render OOB objects");
	REG_HOOK(Boot, CWorld_ProcessPedsAfterPreRender, CWorld_ProcessPedsAfterPreRender, "Render player attached objects");

	REG_HOOK(Boot, CRadar_ClearBlip, CRadar_ClearBlip, "Guard invalid radar blip clear");
	REG_HOOK(Boot, CWorld_FindPlayerSlotWithPedPointer, CWorld_FindPlayerSlotWithPedPointer, "Resolve remote player slot");
	REG_HOOK(Boot, MaterialUpdateUVAnimCB, MaterialUpdateUVAnimCB, "World/object UVAnim material update");
	REG_HOOK(Boot, rpMatFXOpenGLAllInOneRenderCB, rpMatFXOpenGLAllInOneRenderCB, "Null-safe MatFX render");
	REG_HOOK(Boot, CCustomBuildingDNPipeline_CustomPipeRenderCB, CustomPipeRenderCB, "Custom building DN pipeline render");
	REG_HOOK(Boot, CCustomBuildingPipeline_CustomPipeRenderCB, CustomBuildingPipeRenderCB, "Custom building pipeline render");
	REG_HOOK(Boot, rxOpenGLDefaultAllInOneRenderCB, rxOpenGLDefaultAllInOneRenderCB, "OpenGL default render wrap/guard");
	REG_HOOK(Boot, RwTextureDestroy, _RwTextureDestroy, "Guard RwTexture double-destroy");
	REG_HOOK(Boot, RwFrameAddChild, RwFrameAddChild, "Guard RwFrameAddChild null/cycle");
	REG_HOOK(Boot, RLEDecompress, RLEDecompress, "Bounds-safe RLEDecompress");
	REG_HOOK(Boot, RwResourcesFreeResEntry, RwResourcesFreeResEntry, "Guard RwResourcesFreeResEntry");
	REG_HOOK(Boot, rxOpenGLDefaultAtomicAllInOneNode, rxOpenGLDefaultAtomicAllInOneNode, "Guard OpenGL atomic node render");
	REG_HOOK(Boot, TextureDatabaseRuntime_GetEntry, TextureDatabaseRuntime_GetEntry, "Texture DB entry lookup guard");
	REG_HOOK(Boot, CPlayerPed_GetPlayerInfoForThisPlayerPed, GetPlayerInfoForThisPlayerPed, "Map ped to relocated WORLD_PLAYERS");

	if (!eglGetProcAddress("glAlphaFuncQCOM"))
	{
		if (eglGetProcAddress("glAlphaFunc"))
		{
			*((void **)(g_saSym->Abs(addr::glAlphaFuncQCOM_slot))) = (void *)eglGetProcAddress("glAlphaFunc");
			patches::registerData("glAlphaFuncQCOM.fallback", "Bind glAlphaFunc into glAlphaFuncQCOM slot",
				patches::Scope::Boot, g_saSym->Abs<uintptr_t>(addr::glAlphaFuncQCOM_slot));
		}
		else
		{
			REG_HOOK(Boot, RQ_Command_rqSetAlphaTest, RQCommand_rqSetAlphaTest, "Fallback alpha test hook when glAlphaFunc absent");
		}
	}
}
