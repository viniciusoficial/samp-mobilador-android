#include "HookDomains.h"
#include "../Hooks.h"
#include "../Game.h"
#include "../Actor.h"
#include "../Animation.h"
#include "../Common.h"
#include "../Object.h"
#include "../PlayerPed.h"
#include "../RadarColors.h"
#include "../Vehicle.h"
#include "../Util.h"
#include "../RepeatSectorUtil.h"
#include "../Artwork/ArtworkCore.h"
#include "../Engine/PatchRegistry.h"
#include "../Engine/Symbols.h"
#include "../../Net/NetGame.h"
#include "../../Net/ActorPool.h"
#include "../../Net/GangZonePool.h"
#include "../../Net/LocalPlayer.h"
#include "../../Net/RemotePlayer.h"
#include "../../Net/VehiclePool.h"
#include "../../Net/Proto/RpcOutbound.h"
#include "../../UI/UI.h"
#include "../../UI/SAMPWidgets/Chat.h"
#include "../../Client.h"
#include <Memory.h>
#include <SA.h>
#include <RakNet/RakClientInterface.h>
#include <RakNet/SAMP/SAMPRPC.h>
#include <spdlog/spdlog.h>
#include "Core/PtrNode.h"
#include "Core/Rect.h"
#include "Model/Col/ColBox.h"
#include "Model/Col/ColSphere.h"
#include <algorithm>
#include "../../L10n.h"
#include <cmath>

// T34: SetObjectNoCameraCollision — let GTA camera LOS & sphere checks ignore flagged SA-MP objects,
// and ensure custom 0.3.DL objects collide with camera unless explicitly flagged noCameraCol.
DECL_HOOK(bool, CWorld_CameraToIgnoreThisObject, sa::CEntity *pEntity)
{
	if (pEntity)
	{
		if (CObject *pObj = CObject::FindByGtaEntity(pEntity))
		{
			if (pObj->m_bNoCameraCollision)
			{
				return true;
			}
			if (artwork::isInternalCustomSlot(pEntity->m_nModelIndex))
			{
				return false;
			}
		}
	}
	return CWorld_CameraToIgnoreThisObject(pEntity);
}

// Stock CCollision::BuildCacheOfCameraCollision clamps sector coords to [0, 119], so when the
// player/camera is out-of-bounds (|x|>3000 or |y|>3000), camera sphere-cast never scans the
// repeat-sector hash buckets (coord & 0xF) containing OOB objects. Scan unclamped OOB repeat
// buckets after the stock pass (CheckCameraCollisionObjects deduplicates via ms_nCurrentScanCode).
DECL_HOOK(bool, CCollision_BuildCacheOfCameraCollision, sa::CColSphere *sph1, sa::CColSphere *sph2)
{
	bool hit = CCollision_BuildCacheOfCameraCollision(sph1, sph2);
	if (!sph1 || !sph2)
	{
		return hit;
	}
	const uint8_t *pCollideObjs = eng::as<uint8_t *>(eng::Sym::CCollision_bCamCollideWithObjects);
	if (!pCollideObjs || *pCollideObjs == 0)
	{
		return hit;
	}

	const float r = sph1->m_fRadius;
	const float minX = std::min(sph1->m_vecCenter.x, sph2->m_vecCenter.x) - r;
	const float maxX = std::max(sph1->m_vecCenter.x, sph2->m_vecCenter.x) + r;
	const float minY = std::min(sph1->m_vecCenter.y, sph2->m_vecCenter.y) - r;
	const float maxY = std::max(sph1->m_vecCenter.y, sph2->m_vecCenter.y) + r;

	int rawLeft = static_cast<int>(floorf(minX / 50.0f + 60.0f));
	int rawRight = static_cast<int>(floorf(maxX / 50.0f + 60.0f));
	int rawBottom = static_cast<int>(floorf(minY / 50.0f + 60.0f));
	int rawTop = static_cast<int>(floorf(maxY / 50.0f + 60.0f));

	if (rawLeft >= 0 && rawRight <= 119 && rawBottom >= 0 && rawTop <= 119)
	{
		return hit;
	}

	if (rawRight - rawLeft > 15) rawRight = rawLeft + 15;
	if (rawTop - rawBottom > 15) rawTop = rawBottom + 15;

	const float minZ = std::min(sph1->m_vecCenter.z, sph2->m_vecCenter.z) - r;
	const float maxZ = std::max(sph1->m_vecCenter.z, sph2->m_vecCenter.z) + r;
	const float dx = maxX - minX;
	const float dy = maxY - minY;
	const float dz = maxZ - minZ;

	sa::CColSphere boundSph{};
	boundSph.m_vecCenter.x = minX + dx * 0.5f;
	boundSph.m_vecCenter.y = minY + dy * 0.5f;
	boundSph.m_vecCenter.z = minZ + dz * 0.5f;
	boundSph.m_fRadius = sqrtf(dx * dx + dy * dy + dz * dz) * 0.5f;
	boundSph.m_Surface.m_nMaterial = static_cast<sa::eSurfaceType>(0);
	boundSph.m_Surface.m_nPiece = 0;
	boundSph.m_Surface.m_nLighting = sa::tColLighting(0xFF);
	boundSph.m_Surface.m_nLight = 0;

	sa::CColBox dummyBox{};
	uint32_t seenMask[8] = {};
	for (int cy = rawBottom; cy <= rawTop; ++cy)
	{
		for (int cx = rawLeft; cx <= rawRight; ++cx)
		{
			const int group = ((cy & 0xF) << 4) | (cx & 0xF);
			const uint32_t bit = 1u << (group & 31);
			if (seenMask[group >> 5] & bit) continue;
			seenMask[group >> 5] |= bit;
			if (eng::fn::CCollision_CheckCameraCollisionObjects<bool>(cx, cy, &dummyBox, &boundSph, sph1, sph2))
			{
				hit = true;
			}
		}
	}
	return hit;
}

DECL_HOOK(int, CRadar_SetCoordBlip, uint32_t BlpType, sa::CVector vecPosition, uint32_t nColour, uint32_t DispFlag, char *pScriptName)
{
	if (pScriptName && !strncmp(pScriptName, "CODEWAY", 7))
	{
		// CWorld::FindGroundZForCoord — only snap user-placed pause-map target markers ("CODEWAY"),
		// never server-supplied coord blips (e.g. SetPlayerMapIcon style 1/3).
		vecPosition.z = eng::fn::CWorld_FindGroundZForCoord(vecPosition.x, vecPosition.y);

		if (Client::offlineMode() && pGame)
		{
			CPlayerPed *pLocal = pGame->FindPlayerPed();
			if (pLocal && pLocal->m_ped)
			{
				RwMatrix mat = pLocal->m_ped->GetMatrix().ToRwMatrix();
				mat.pos = sa::CVector(vecPosition.x, vecPosition.y, vecPosition.z + 5.0f);
				if (pLocal->IsInVehicle() && pLocal->m_ped->pVehicle)
				{
					pLocal->m_ped->pVehicle->SetMatrix((sa::CMatrix &)mat);
				}
				pLocal->m_ped->SetMatrix((sa::CMatrix &)mat);

				if (pUI && pUI->chat())
				{
					pUI->chat()->addInfoMessage(L10N("-> Teleported to: %.2f %.2f %.2f", "-> 传送至：%.2f %.2f %.2f"), vecPosition.x, vecPosition.y, vecPosition.z);
				}
			}
		}

		if (pNetGame && pNetGame->GetRakClient())
		{
			RakNet::BitStream bsSend;
			proto::Writer writer(bsSend);
			proto::MapMarkerReq req{};
			req.position = {vecPosition.x, vecPosition.y, vecPosition.z};
			req.serialize(writer, proto::ProtoVersion::V037);
			pNetGame->GetRakClient()->RPC(&RPC_MapMarker, &bsSend, HIGH_PRIORITY, RELIABLE, 0, false, UNASSIGNED_NETWORK_ID, nullptr);
		}
	}

	return CRadar_SetCoordBlip(BlpType, vecPosition, nColour, DispFlag, pScriptName);
}

DECL_HOOK(void, CRadar_DrawRadarGangOverlay, bool bFullMap)
{
	if (pNetGame)
	{
		CGangZonePool *pGangZonePool = pNetGame->GetGangZonePool();
		if (pGangZonePool)
		{
			pGangZonePool->Draw(bFullMap);
		}
	}
}

DECL_HOOK(uint32_t, CHudColours_GetIntColour, uintptr_t _this, uint32_t index)
{
	return TranslateColorCodeToRGBA(index);
}

static inline bool HasOccupiedVehicleDriver(const sa::CVehicle *v)
{
	return v && (v->pDriver != nullptr || (v->m_pTowingVehicle != nullptr && v->m_pTowingVehicle->pDriver != nullptr));
}

DECL_HOOK(uint32_t, CAutoMobile_ProcessEntityCollision, sa::CVehicle *_this, sa::CEntity *pCollided, uintptr_t a3)
{
	if (!pNetGame || !pNetGame->m_netSet || !pNetGame->m_netSet->disableVehicleCollisions ||
		!_this || !pCollided || !pCollided->IsVehicle() ||
		pCollided->m_nModelIndex < 400 || pCollided->m_nModelIndex > 611 ||
		!HasOccupiedVehicleDriver(_this) || !HasOccupiedVehicleDriver(reinterpret_cast<sa::CVehicle*>(pCollided)))
	{
		return CAutoMobile_ProcessEntityCollision(_this, pCollided, a3);
	}

	return 0;
}

DECL_HOOK(uint32_t, CBike_ProcessEntityCollision, sa::CVehicle *_this, sa::CEntity *pCollided, uintptr_t a3)
{
	if (!pNetGame || !pNetGame->m_netSet || !pNetGame->m_netSet->disableVehicleCollisions ||
		!_this || !pCollided || !pCollided->IsVehicle() ||
		pCollided->m_nModelIndex < 400 || pCollided->m_nModelIndex > 611 ||
		!HasOccupiedVehicleDriver(_this) || !HasOccupiedVehicleDriver(reinterpret_cast<sa::CVehicle*>(pCollided)))
	{
		return CBike_ProcessEntityCollision(_this, pCollided, a3);
	}

	return 0;
}

DECL_HOOK(uint32_t, CMonsterTruck_ProcessEntityCollision, sa::CVehicle *_this, sa::CEntity *pCollided, uintptr_t a3)
{
	if (!pNetGame || !pNetGame->m_netSet || !pNetGame->m_netSet->disableVehicleCollisions ||
		!_this || !pCollided || !pCollided->IsVehicle() ||
		pCollided->m_nModelIndex < 400 || pCollided->m_nModelIndex > 611 ||
		!HasOccupiedVehicleDriver(_this) || !HasOccupiedVehicleDriver(reinterpret_cast<sa::CVehicle*>(pCollided)))
	{
		return CMonsterTruck_ProcessEntityCollision(_this, pCollided, a3);
	}

	return 0;
}

DECL_HOOK(uint32_t, CTrailer_ProcessEntityCollision, sa::CVehicle *_this, sa::CEntity *pCollided, uintptr_t a3)
{
	if (_this && pCollided && pCollided->IsVehicle()
		&& _this->m_pTowingVehicle == reinterpret_cast<sa::CVehicle*>(pCollided))
	{
		// skip self-collision between trailer and its own tractor
		return 0;
	}

	if (!pNetGame || !pNetGame->m_netSet || !pNetGame->m_netSet->disableVehicleCollisions ||
		!_this || !pCollided || !pCollided->IsVehicle() ||
		pCollided->m_nModelIndex < 400 || pCollided->m_nModelIndex > 611 ||
		!HasOccupiedVehicleDriver(_this) || !HasOccupiedVehicleDriver(reinterpret_cast<sa::CVehicle*>(pCollided)))
	{
		return CTrailer_ProcessEntityCollision(_this, pCollided, a3);
	}

	return 0;
}

DECL_HOOK(uint32_t, CPhysical_ProcessEntityCollision, sa::CPhysical *_this, sa::CEntity *pCollided, uintptr_t a3)
{
	if (pNetGame && pNetGame->m_netSet && pNetGame->m_netSet->disableVehicleCollisions
		&& _this && pCollided && _this->IsVehicle() && pCollided->IsVehicle()
		&& _this->m_nModelIndex >= 400 && _this->m_nModelIndex <= 611
		&& pCollided->m_nModelIndex >= 400 && pCollided->m_nModelIndex <= 611)
	{
		auto* pVehA = reinterpret_cast<sa::CVehicle*>(_this);
		auto* pVehB = reinterpret_cast<sa::CVehicle*>(pCollided);
		if (HasOccupiedVehicleDriver(pVehA) && HasOccupiedVehicleDriver(pVehB)) {
			return 0;
		}
	}

	return CPhysical_ProcessEntityCollision(_this, pCollided, a3);
}

// PC SA-MP parity (hooks.cpp:1024-1040, GameHookGuardPhysicalProcessCollision):
// SetGravityProcessing(false) sets bit 31 (0x80000000u) on CEntity::m_nFlags.
// Returning before CPhysical::ProcessCollision prevents m_bIsInSafePosition = 1
// when m_bUsesCollision == 0, allowing CWorld::Process -> ProcessShift -> ApplySpeed()
// to integrate velocity smoothly for GhostMode, vehicle-surfing, and entering peds.
DECL_HOOK(void, CPhysical_ProcessCollision, sa::CPhysical *_this)
{
	if (!_this || (_this->m_nFlags & 0x80000000u) != 0)
	{
		return;
	}
	CPhysical_ProcessCollision(_this);
}

// PC SA-MP parity (hooks.cpp:1500-1540): update local & remote surfing attachments
// at the end of CWorld::Process() (after vehicle/object physics step, before RenderScene).
DECL_HOOK(void, CWorld_ProcessAttachedEntities)
{
	CWorld_ProcessAttachedEntities();

	if (!pNetGame)
	{
		return;
	}

	CPlayerPool *pPlayerPool = pNetGame->GetPlayerPool();
	if (!pPlayerPool)
	{
		return;
	}

	if (CLocalPlayer *pLocal = pPlayerPool->GetLocalPlayer())
	{
		if (pLocal->m_bIsActive && pLocal->m_surfSync.bIsActive)
		{
			pLocal->UpdateSurfing();
		}
	}

	for (PLAYERID i = 0; i <= pPlayerPool->m_iLastPlayerID && i < MAX_PLAYERS; ++i)
	{
		if (!pPlayerPool->GetSlotState(i)) continue;
		CRemotePlayer *pRemote = pPlayerPool->GetAt(i);
		if (pRemote && pRemote->IsActive() && pRemote->IsSurfing())
		{
			pRemote->ProcessSurfing();
		}
	}
}

// PC SA-MP parity (hooks.cpp:1195-1207, GameHookOverrideVerticalLineCollisionFlags):
// Force checkObjects = true whenever buildings are queried so custom CreateObject floors
// act as solid ground across FindGroundZ, SetPlayerPosFindZ, ped placement, and shadows.
DECL_HOOK(bool, CWorld_ProcessVerticalLine, const sa::CVector *vecOrigin, float fDistance, sa::CColPoint *pColPoint,
	sa::CEntity **ppEntity, bool bBuildings, bool bVehicles, bool bPeds, bool bObjects, bool bDummies,
	bool bSeeThrough, void *pIgnoreCar)
{
	if (pNetGame && bBuildings)
	{
		bObjects = true;
	}
	return CWorld_ProcessVerticalLine(vecOrigin, fDistance, pColPoint, ppEntity,
		bBuildings, bVehicles, bPeds, bObjects, bDummies, bSeeThrough, pIgnoreCar);
}

// PC SA-MP parity (patches.cpp:183-185): disable 2-player co-op screen-edge tether clamping.
DECL_HOOK(bool, CGameLogic_IsPlayerAllowedToGoInThisDirection, sa::CPed *pPed, sa::CVector vecDir, float fDistance)
{
	if (!Client::offlineMode())
	{
		return true;
	}
	return CGameLogic_IsPlayerAllowedToGoInThisDirection(pPed, vecDir, fDistance);
}

// PC SA-MP parity (hooks.cpp:1275-1344, GameHookPreservePedTransform):
// Preserve invulnerable Actor position and zero XY velocity across CCivilianPed::ProcessControl.
DECL_HOOK(void, CCivilianPed_ProcessControl, sa::CPed *_this)
{
	if (!_this || !pNetGame)
	{
		CCivilianPed_ProcessControl(_this);
		return;
	}

	const bool bInvulnFlags = _this->physicalFlags.bBulletProof
		&& _this->physicalFlags.bExplosionProof
		&& _this->physicalFlags.bMeleeProof;

	CActor *pManagedActor = nullptr;
	if (CActorPool *pActorPool = pNetGame->GetActorPool())
	{
		PLAYERID actorId = pActorPool->FindIDFromGtaPtr(_this);
		if (actorId != INVALID_PLAYER_ID)
		{
			pManagedActor = pActorPool->GetAt(actorId);
		}
	}

	const bool bLockTransform = bInvulnFlags || (pManagedActor && pManagedActor->m_invulnerable);
	sa::CVector savedPos{};
	bool bHasSavedPos = false;

	if (bLockTransform)
	{
		_this->m_vecMoveSpeed.x = 0.0f;
		_this->m_vecMoveSpeed.y = 0.0f;
		if (_this->m_matrix)
		{
			savedPos = _this->m_matrix->m_pos;
			bHasSavedPos = true;
		}
	}

	CCivilianPed_ProcessControl(_this);

	if (bLockTransform)
	{
		if (bHasSavedPos && _this->m_matrix)
		{
			_this->m_matrix->m_pos = savedPos;
		}
		_this->m_vecMoveSpeed.x = 0.0f;
		_this->m_vecMoveSpeed.y = 0.0f;
	}
}

bool g_bCreatingSirenVehicle = false;

DECL_HOOK(bool, CVehicle_UsesSiren, sa::CVehicle *_this)
{
	if (g_bCreatingSirenVehicle) {
		return true;
	}
	if (CVehicle_UsesSiren(_this)) {
		return true;
	}
	if (_this && pNetGame && pNetGame->GetVehiclePool()) {
		VEHICLEID vid = pNetGame->GetVehiclePool()->FindIDFromGtaPtr(_this);
		if (vid != INVALID_VEHICLE_ID) {
			CVehicle* pVeh = pNetGame->GetVehiclePool()->GetAt(vid);
			if (pVeh && pVeh->m_bSiren) {
				return true;
			}
		}
	}
	return false;
}

// Stock CPhysical::Add/RemoveAndAdd fetch the bound rect through vtable
// slot 10 (virtual GetBoundRect) — replicate the dispatch exactly so any
// subclass override keeps its say. GetBoundRect must always return TRUE
// world coords: an earlier revision of this file inline-hooked it to remap
// out-of-bounds rects into a mod-16-congruent "dead zone", which lied to
// every engine consumer (broad-phase rect tests included) — unnecessary
// once Add no longer rejects out-of-range coords.
static sa::CRect CallVirtualGetBoundRect(sa::CPhysical *pEnt)
{
	auto **ppVtbl = *reinterpret_cast<void ***>(pEnt);
	auto pfn = reinterpret_cast<sa::CRect (*)(sa::CPhysical *)>(
		ppVtbl[0x50 / sizeof(void *)]);
	return pfn(pEnt);
}

static inline void BoundRectToSectorCoords(const sa::CRect &r, int &minX, int &minY, int &maxX, int &maxY)
{
	minX = static_cast<int>(floorf(r.left / 50.0f + 60.0f));
	maxX = static_cast<int>(floorf(r.right / 50.0f + 60.0f));
	minY = static_cast<int>(floorf(r.bottom / 50.0f + 60.0f));
	maxY = static_cast<int>(floorf(r.top / 50.0f + 60.0f));
}

// Stock GTA:SA CPhysical::Add() inserts into the 16x16 CWorld::ms_aRepeatSectors
// hash grid with three flaws this replacement fixes:
//   - it REJECTS any bound rect whose sector coord lands outside [-120,120]
//     ((c+120) u<= 240), so out-of-bounds (|x/y|>3000) entities never get
//     sector records at all — yet disassembly shows the insertion itself only
//     ever writes ms_aRepeatSectors through the mod-16 bucket hash, which is
//     safe for ANY coord (the static ms_aSectorX/Y grids are only written by
//     CEntity::Add on the big-building path, which is passed through below).
//     Every scanner (CheckCollision / ProcessShift / ProcessVerticalLine /
//     ProcessLineOfSight) hashes RAW coords mod-16 with no clamping, so
//     simply dropping the rejection unifies in-bounds and out-of-bounds
//     entities on the engine's own single path — zero coordinate fakery.
//   - it inserts one node per overlapped cell WITHOUT deduplicating hash
//     groups (a 2500m custom 0.3.DL map chunk inserts 1,100+ duplicate nodes
//     into only 256 buckets);
//   - it never null-checks operator new() on CEntryInfoNode /
//     CPtrNodeDoubleLink, crashing with SIGSEGV if the pool ever fills up.
DECL_HOOK(void, CPhysical_Add, sa::CPhysical *_this)
{
	if (!_this) return;
	if (_this->m_bIsBIGBuilding) {
		CPhysical_Add(_this);
		return;
	}

	const sa::CRect r = CallVirtualGetBoundRect(_this);
	int minX, minY, maxX, maxY;
	BoundRectToSectorCoords(r, minX, minY, maxX, maxY);
	if (minX > maxX || minY > maxY) {
		return;
	}

	const auto res = sa_world::linkEntityToRepeatSectors(_this, minX, minY, maxX, maxY);
	if (res.poolExhausted) {
		static bool s_loggedPoolFull = false;
		if (!s_loggedPoolFull) {
			s_loggedPoolFull = true;
			spdlog::error("CPhysical::Add: repeat-sector pool exhausted (model={}); skipping remaining cells to prevent crash",
				static_cast<int>(_this->m_nModelIndex));
		}
	}
}

// Stock CPhysical::RemoveAndAdd() is an INLINED copy of the same insertion
// logic (it never calls Add/Remove for non-big buildings) — but unlike Add
// it has NO [-120,120] rejection: moving entities already re-register at any
// coordinate natively. Replace it anyway so moving entities get the same
// dedup + pool-exhaustion protection as static ones (a huge MoveTo object
// would otherwise re-insert 1,100+ duplicate nodes on every step).
// Net effect of the stock node-reuse shuffle is identical to a full
// remove + reinsert: reused nodes are relinked at the target bucket head,
// exactly where a fresh allocation would land.
DECL_HOOK(void, CPhysical_RemoveAndAdd, sa::CPhysical *_this)
{
	if (!_this) return;
	if (_this->m_bIsBIGBuilding) {
		CPhysical_RemoveAndAdd(_this);
		return;
	}

	// compute the target rect BEFORE unlinking — with a degenerate rect the
	// stock implementation also inserts nothing, but it never strips the
	// existing records either, and neither should we
	const sa::CRect r = CallVirtualGetBoundRect(_this);
	int minX, minY, maxX, maxY;
	BoundRectToSectorCoords(r, minX, minY, maxX, maxY);
	if (minX > maxX || minY > maxY) {
		return;
	}

	eng::fn::CPhysical_Remove( _this);

	const auto res = sa_world::linkEntityToRepeatSectors(_this, minX, minY, maxX, maxY);
	if (res.poolExhausted) {
		static bool s_loggedPoolFullRnA = false;
		if (!s_loggedPoolFullRnA) {
			s_loggedPoolFullRnA = true;
			spdlog::error("CPhysical::RemoveAndAdd: repeat-sector pool exhausted (model={}); skipping remaining cells to prevent crash",
				static_cast<int>(_this->m_nModelIndex));
		}
	}
}

// GTA:SA treats a ped standing on ANY CObject like standing on a moving
// vehicle roof: CPed::ProcessEntityCollision writes m_pContactEntity
// (+0x6d8) for entity type 4, and CPed::UpdatePosition then clamps the
// per-frame XY velocity delta to 0.01f * CTimer::ms_fTimeStep (constant at
// 0x7109b4) unless (contactPhysFlags & 0xC) == 0x4 — a condition neither
// static (0xC) nor dynamic (0x0) objects satisfy. After any wall graze or
// friction speed loss the ped needs 10-20 frames to crawl back to speed —
// the "walking on glue" feel of every custom object map. Buildings never
// set the contact entity at all.
// Fix: for SA-MP objects that are not mid-MoveTo (attached objects carry
// zero velocity, so the contact coupling is a no-op for them too), clear
// the contact entity for the duration of UpdatePosition — static object
// floors then behave exactly like building floors. MoveTo platforms keep
// the coupling so elevator surfing still works.
DECL_HOOK(void, CPed_UpdatePosition, sa::CPed *_this)
{
#if defined(__aarch64__) || defined(__x86_64__)
	static_assert(offsetof(sa::CPed, m_pGroundPhysical) == 0x6d8,
		"m_pGroundPhysical must match the +0x6d8 contact-entity write in CPed::ProcessEntityCollision");
#else
	static_assert(offsetof(sa::CPed, m_pGroundPhysical) == 0x56c,
		"m_pGroundPhysical must match the +0x56c contact-entity write in arm32 CPed::ProcessEntityCollision (0x4a38bc)");
#endif
	sa::CPhysical *pContact = _this ? _this->m_pGroundPhysical : nullptr;
	CObject *pObj = (pContact && pContact->m_nType == sa::ENTITY_TYPE_OBJECT)
		? CObject::FindByGtaEntity(pContact) : nullptr;
	const bool bStaticSampObject = pObj && !(pObj->m_byteMoving & 1);

	if (bStaticSampObject) {
		_this->m_pGroundPhysical = nullptr;
	}
	CPed_UpdatePosition(_this);
	if (bStaticSampObject) {
		_this->m_pGroundPhysical = pContact;
	}
}

// set while a script/opcode-initiated attach or detach is running, so that
// the BreakTowLink hook can tell intentional breaks from GTA-spontaneous ones
bool g_bIntentionalTrailerDetach = false;

DECL_HOOK(void, CTrailer_BreakTowLink, sa::CVehicle *_this)
{
	if (!g_bIntentionalTrailerDetach && pNetGame && _this
			&& _this->m_pTowingVehicle && _this->fHealth > 0.0f)
	{
		CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
		if (pVehiclePool)
		{
			VEHICLEID tractorId = pVehiclePool->FindIDFromGtaPtr(_this->m_pTowingVehicle);
			CVehicle* pTractor = (tractorId != INVALID_VEHICLE_ID) ? pVehiclePool->GetAt(tractorId) : nullptr;
			if (pTractor && pTractor->VerifyInstance() && pTractor->m_pTrailer
					&& !pTractor->IsDriverLocalPlayer())
			{
				// server still owns this attachment: veto GTA-initiated break
				return;
			}
		}
	}

	CTrailer_BreakTowLink(_this);
}

// fix crosshair
DECL_HOOK(int, CPlantMgr_AddLocTris, void* entry, const sa::CVector* camPos, int a3, int startTri, int endTri)
{
	if (!entry) return 0;
	auto* entity = *reinterpret_cast<sa::CEntity**>(entry);
	if (!entity || IsEntityPlaceable(entity)) return 0;

	auto* colModel = eng::fn::CEntity_GetColModel<sa::CColModel*>( entity);
	if (!colModel || !colModel->m_pColData) return 0;

	auto* colData = colModel->m_pColData;
	if (!colData->m_pVertices || !colData->m_pTriangles || colData->m_nNumTriangles == 0) {
		return 0;
	}
	if (startTri < 0 || endTri < startTri || endTri >= static_cast<int>(colData->m_nNumTriangles)) {
		return 0;
	}
	for (int i = startTri; i <= endTri; ++i) {
		const sa::CColTriangle& tri = colData->m_pTriangles[i];
		if (tri.vA < 0 || tri.vB < 0 || tri.vC < 0 ||
			tri.vA > 65535 || tri.vB > 65535 || tri.vC > 65535) {
			return 0;
		}
	}
	return CPlantMgr_AddLocTris(entry, camPos, a3, startTri, endTri);
}

DECL_HOOK(void*, CAnimManager_AddAnimation, void* pClump, int groupId, int animId)
{
	void* pRes = CAnimManager_AddAnimation(pClump, groupId, animId);
	if (pRes && pClump) {
		sa::CPed* pLocal = GamePool_FindPlayerPed();
		if (pLocal && pLocal->m_pRwObject == pClump) {
			RecordLocalPedQueuedAnim(groupId, animId, 4.0f);
		}
	}
	return pRes;
}

DECL_HOOK(void*, CAnimManager_BlendAnimation, void* pClump, int groupId, int animId, float blendDelta)
{
	void* pRes = CAnimManager_BlendAnimation(pClump, groupId, animId, blendDelta);
	if (pRes && pClump) {
		sa::CPed* pLocal = GamePool_FindPlayerPed();
		if (pLocal && pLocal->m_pRwObject == pClump) {
			RecordLocalPedQueuedAnim(groupId, animId, blendDelta);
		}
	}
	return pRes;
}

void InstallWorldHooks()
{
	REG_HOOK(Boot, CRadar_SetCoordBlip, CRadar_SetCoordBlip, "Map click teleport/marker RPC");
	REG_HOOK(Boot, CRadar_DrawRadarGangOverlay, CRadar_DrawRadarGangOverlay, "SA-MP gang zone radar overlay");
	REG_HOOK(Boot, CHudColours_GetIntColour, CHudColours_GetIntColour, "SA-MP radar blip RGBA colors");

	REG_HOOK(Boot, CAnimManager_AddAnimation, CAnimManager_AddAnimation, "Capture local ped AddAnimation for OnFootSync");
	REG_HOOK(Boot, CAnimManager_BlendAnimation, CAnimManager_BlendAnimation, "Capture local ped BlendAnimation for OnFootSync");

	REG_HOOK(Boot, CAutomobile_ProcessEntityCollision, CAutoMobile_ProcessEntityCollision, "DisableRemoteVehicleCollisions (automobile)");
	REG_HOOK(Boot, CBike_ProcessEntityCollision, CBike_ProcessEntityCollision, "DisableRemoteVehicleCollisions (bike)");
	REG_HOOK(Boot, CMonsterTruck_ProcessEntityCollision, CMonsterTruck_ProcessEntityCollision, "DisableRemoteVehicleCollisions (monster)");
	REG_HOOK(Boot, CTrailer_ProcessEntityCollision, CTrailer_ProcessEntityCollision, "DisableRemoteVehicleCollisions (trailer)");
	REG_HOOK(Boot, CPhysical_ProcessEntityCollision, CPhysical_ProcessEntityCollision, "DisableRemoteVehicleCollisions (boats/other)");
	REG_HOOK(Boot, CPhysical_ProcessCollision, CPhysical_ProcessCollision, "Guard CPhysical::ProcessCollision for 0x80000000 gravity-disabled peds");
	REG_HOOK(Boot, CWorld_ProcessAttachedEntities, CWorld_ProcessAttachedEntities, "Pre-render local & remote surfing matrix sync");
	REG_HOOK(Boot, CWorld_ProcessVerticalLine, CWorld_ProcessVerticalLine, "Include dynamic CObject floors in vertical ground queries");
	REG_HOOK(Boot, CGameLogic_IsPlayerAllowedToGoInThisDirection, CGameLogic_IsPlayerAllowedToGoInThisDirection, "Disable 2P co-op screen tether clamping");
	REG_HOOK(Boot, CCivilianPed_ProcessControl, CCivilianPed_ProcessControl, "Preserve invulnerable Actor transform in ProcessControl");
	REG_HOOK(Boot, CTrailer_BreakTowLink, CTrailer_BreakTowLink, "Prevent spurious trailer detach");
	REG_HOOK(Boot, CVehicle_UsesSiren, CVehicle_UsesSiren, "Enable horn-siren on non-emergency vehicles with addSiren");
	REG_HOOK(Boot, CPhysical_Add, CPhysical_Add, "Deduped, null-safe, range-unclamped repeat-sector registration");
	REG_HOOK(Boot, CPhysical_RemoveAndAdd, CPhysical_RemoveAndAdd, "Deduped & null-safe repeat-sector re-registration for moving entities");
	REG_HOOK(Boot, CPed_UpdatePosition, CPed_UpdatePosition, "Static object floors: no vehicle-roof acceleration clamp");
	REG_HOOK(Boot, CWorld_CameraToIgnoreThisObject, CWorld_CameraToIgnoreThisObject, "SetObjectNoCameraCollision camera ignore");
	REG_HOOK(Boot, CCollision_BuildCacheOfCameraCollision, CCollision_BuildCacheOfCameraCollision, "OOB object camera collision scan");
	REG_HOOK(Boot, CPlantMgr_ProcessEntryCollisionDataSections_AddLocTris, CPlantMgr_AddLocTris, "Guard bad col triangle indices");
}
