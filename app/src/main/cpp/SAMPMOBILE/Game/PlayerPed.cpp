#include "PlayerPed.h"
#include "Camera.h"
#include "Patches.h"
#include "Game.h"
#include "Common.h"
#include "Object.h"
#include "Pad.h"
#include "Scripting.h"
#include "Util.h"
#include "Artwork/ArtworkCore.h"
#include "Artwork/ArtworkManager.h"
#include "Vehicle.h"
#include "PlayerContextScope.h"
#include "Engine/PatchRegistry.h"
#include "Engine/Symbols.h"
#include "../Net/NetGame.h"
#include "../Net/RemotePlayer.h"
#include "../Net/VehiclePool.h"
#include "../Client.h"
#include <SA.h>
#include <spdlog/spdlog.h>


CPlayerPed::CPlayerPed()
{
	m_gtaId = 1;
	m_ped = GamePool_FindPlayerPed();
	m_entity = GamePool_FindPlayerPed();
	m_bytePlayerNumber = 0;
	SetPlayerPedPtrRecord(m_bytePlayerNumber, m_ped);
	ScriptCommand(&set_actor_weapon_droppable, m_gtaId, 1);
	ScriptCommand(&set_actor_can_be_decapitated, m_gtaId, 0);

	m_bCuffedState = false;
	m_bCarryState = false;

	m_iDanceState = 0;
	m_bCellPhoneEnabled = false;
	m_bPissingState = 0;
	m_dwPissParticlesHandle = 0;

	m_dwArrow = 0;

	m_iParachuteState = 0;
	m_iParachuteAnim = 0;
	m_dwParachuteObject = 0;

	m_iDrunkLevel = 0;
	m_dwDrunkLastUpdateTick = 0;
	m_bDrunkVisualsActive = false;
	m_bDrunkHudHidden = false;

	for (auto& m_attachedObject : m_attachedObjects) {
		m_attachedObject.slotUsed = false;
		memset(&m_attachedObject.info, 0, sizeof(NEW_ATTACHED_OBJECT));
		m_attachedObject.object = nullptr;
	}
}

CPlayerPed::CPlayerPed(int iNum, int iSkin, float fX, float fY, float fZ, float fRotation)
{
	spdlog::debug("CPlayerPed::CPlayerPed({}, {}, {}, {}, {}, {})", iNum, iSkin, fX, fY, fZ, fRotation);

	uint32_t dwPlayerActorID;
	ScriptCommand(&create_player, &iNum, fX, fY, fZ, &dwPlayerActorID);
	ScriptCommand(&create_actor_from_player, &iNum, &dwPlayerActorID);

	m_gtaId = dwPlayerActorID;
	m_ped = GamePool_Ped_GetAt(m_gtaId);
	m_entity = m_ped;
	m_bytePlayerNumber = iNum;

	SetPlayerPedPtrRecord(iNum, m_ped);

	ScriptCommand(&set_actor_weapon_droppable, m_gtaId, 1);
	ScriptCommand(&set_actor_immunities, m_gtaId, 0, 0, 1, 0, 0);
	ScriptCommand(&set_actor_can_be_decapitated, m_gtaId, 0);

	if (pNetGame) {
		ScriptCommand(&set_actor_money, m_gtaId, 0);
		ScriptCommand(&set_actor_money, m_gtaId, pNetGame->m_netSet->deathDropMoney);
	}

	m_bCuffedState = false;
	m_bCarryState = false;

	m_dwArrow = 0;

	m_iParachuteState = 0;
	m_iParachuteAnim = 0;
	m_dwParachuteObject = 0;

	m_iDrunkLevel = 0;
	m_dwDrunkLastUpdateTick = 0;
	m_bDrunkVisualsActive = false;
	m_bDrunkHudHidden = false;
	m_iDanceState = 0;
	m_bCellPhoneEnabled = false;
	m_bPissingState = 0;
	m_dwPissParticlesHandle = 0;
	m_iStuffType = STUFF_TYPE_NONE;
	m_dwStuffObject = 0;

	if (m_bytePlayerNumber > 0 && m_bytePlayerNumber < PLAYER_PED_SLOTS) {
		memset(&RemotePlayerKeys[m_bytePlayerNumber], 0, sizeof(PAD_KEYS));
		CAMERA_AIM zeroAim{};
		GameStoreRemotePlayerAim(m_bytePlayerNumber, &zeroAim);
		GameSetPlayerCameraMode(4, m_bytePlayerNumber);
		GameSetPlayerCameraExtZoomAndAspect(m_bytePlayerNumber, 1.0f, 0.5f);
		for (int s = 0; s < kWeaponSkillCount; ++s) {
			GameSetRemotePlayerSkill(m_bytePlayerNumber, s, 999);
		}
	}

	SetModelIndex(iSkin);

	if (m_ped) {
		SetTargetRotation(fRotation);
		RwMatrix mat = m_ped->GetMatrix().ToRwMatrix();
		mat.pos.x = fX;
		mat.pos.y = fY;
		mat.pos.z = fZ + 0.15f;
		m_ped->SetMatrix(mat);
	}

	for (auto& m_attachedObject : m_attachedObjects) {
		m_attachedObject.slotUsed = false;
		memset(&m_attachedObject.info, 0, sizeof(NEW_ATTACHED_OBJECT));
		m_attachedObject.object = nullptr;
	}

	m_bHaveBulletData = false;
	memset(&m_bulletData, 0, sizeof(BULLET_DATA));
}

CPlayerPed::~CPlayerPed()
{
	spdlog::debug("Destroying PlayerPed({})", m_bytePlayerNumber);

	if (m_bytePlayerNumber > 0 && m_bytePlayerNumber < PLAYER_PED_SLOTS) {
		memset(&RemotePlayerKeys[m_bytePlayerNumber], 0, sizeof(PAD_KEYS));
		CAMERA_AIM zeroAim{};
		GameStoreRemotePlayerAim(m_bytePlayerNumber, &zeroAim);
		GameSetPlayerCameraMode(4, m_bytePlayerNumber);
		for (int s = 0; s < kWeaponSkillCount; ++s) {
			GameSetRemotePlayerSkill(m_bytePlayerNumber, s, 999);
		}
	}

	if (m_bPissingState || m_dwPissParticlesHandle) {
		TogglePissing(false);
	}

	SetPlayerPedPtrRecord(m_bytePlayerNumber, nullptr);

	// The engine destructor (below) clears CWorld::Players[n].m_pPed, but it is
	// skipped when the entity is placeable/recycled - clear our own record so
	// WORLD_PLAYERS never retains a dangling ped pointer.
	if (WORLD_PLAYERS && m_bytePlayerNumber < PLAYER_PED_SLOTS &&
			WORLD_PLAYERS[m_bytePlayerNumber].m_pPed == m_ped) {
		WORLD_PLAYERS[m_bytePlayerNumber].m_pPed = nullptr;
	}

	if (m_ped && (GamePool_Ped_GetAt(m_gtaId) != nullptr) && !IsEntityPlaceable(m_ped)) {
		if (m_dwParachuteObject) {
			ScriptCommand(&disassociate_object, m_dwParachuteObject, 0.0, 0.0, 0.0, 0);
			ScriptCommand(&destroy_object_with_fade, m_dwParachuteObject);
			m_dwParachuteObject = 0;
		}

		if (HasAttachedObject()) {
			RemoveAllAttachedObjects();
		}

		if (m_iStuffType != STUFF_TYPE_NONE || m_dwStuffObject != 0) {
			DropStuff();
		}

		if (m_ped->IsInVehicle()) {
			RemoveFromVehicleAndPutAt(100.0f, 100.0f, 20.0f);
		}

		if (m_ped->m_pPlayerData) {
			m_ped->m_pPlayerData->m_nPlayerGroup = 0;
		}

		// CPlayerPed::Destructor
		m_ped->Destructor();

		m_ped = nullptr;
		m_entity = nullptr;
	}
	else {
		m_ped = nullptr;
		m_entity = nullptr;
		m_gtaId = 0;
	}

	if (m_dwArrow) {
		ScriptCommand(&disable_marker, m_dwArrow);
		m_dwArrow = 0;
	}
}

void CPlayerPed::ShowMarker(int nIndex)
{
	if (m_dwArrow) {
		ScriptCommand(&disable_marker, m_dwArrow);
		m_dwArrow = 0;
	}
	if (!m_ped || !GamePool_Ped_GetAt(m_gtaId)) {
		return;
	}
	ScriptCommand(&create_arrow_above_actor, m_gtaId, &m_dwArrow);
	ScriptCommand(&set_marker_color, m_dwArrow, nIndex);
	ScriptCommand(&show_on_radar2, m_dwArrow, 2); //2==player marker
}

bool CPlayerPed::IsInVehicle()
{
	if (!m_ped) {
		return false;
	}

	if (m_ped->IsInVehicle()) {
		return true;
	}

	return false;
}

bool CPlayerPed::IsAPassenger()
{
	if (!m_ped) {
		return false;
	}

	if (m_ped->IsAPassenger()) {
		return true;
	}

	return false;
}

void CPlayerPed::RemoveFromVehicleAndPutAt(float fX, float fY, float fZ)
{
	if (!GamePool_Ped_GetAt(m_gtaId)) {
		return;
	}
	if (m_ped && m_ped->IsInVehicle()) {
		ScriptCommand(&remove_actor_from_car_and_put_at, m_gtaId, fX, fY, fZ);
	}
}

void CPlayerPed::SetInitialState()
{
	if (!m_ped) return;
	m_ped->SetInitialState(false);
}

void CPlayerPed::TogglePlayerControllable(bool bControllable)
{
	RwMatrix mat;

	if (GamePool_Ped_GetAt(m_gtaId)) {
		if (bControllable) {
			ScriptCommand(&toggle_player_controllable, m_bytePlayerNumber, 1);
			ScriptCommand(&lock_actor, m_gtaId, 0);

			if (!IsInVehicle()) {
				mat = m_ped->GetMatrix().ToRwMatrix();
				TeleportTo(mat.pos.x, mat.pos.y, mat.pos.z);
			}
		}
		else {
			ScriptCommand(&toggle_player_controllable, m_bytePlayerNumber, 0);
			ScriptCommand(&lock_actor, m_gtaId, 1);
		}
	}
}

void CPlayerPed::RemoveWeaponWhenEnteringVehicle()
{
	if (m_ped) {
		// CPed::RemoveWeaponWhenEnteringVehicle
		eng::fn::CPed_RemoveWeaponWhenEnteringVehicle( m_ped, 0);
	}
}

uint8_t CPlayerPed::GetActionTrigger()
{
	return m_ped ? m_ped->m_nPedState : 0;
}

bool CPlayerPed::IsDead()
{
	if (!m_ped) {
		return true;
	}
	if (m_ped->m_fHealth > 0.0f) {
		return false;
	}
	return true;
}

bool CPlayerPed::IsTakeDamageFallTask()
{
	if (m_ped && !IsInVehicle() && m_ped->m_pIntelligence && m_ped->m_pIntelligence->m_TaskMgr.m_aPrimaryTasks[sa::TASK_PRIMARY_PRIMARY]) {
		return GetTaskTypeFromTask(&m_ped->m_pIntelligence->m_TaskMgr.m_aPrimaryTasks[sa::TASK_PRIMARY_PRIMARY]) == sa::TASK_COMPLEX_FALL_AND_GET_UP;
	}

	return false;
}

uint8_t CPlayerPed::IsEnteringVehicle()
{
	if (m_ped && m_ped->m_pIntelligence && m_ped->m_pIntelligence->m_TaskMgr.m_aPrimaryTasks[sa::TASK_PRIMARY_PRIMARY]) {
		int iType = GetTaskTypeFromTask(&m_ped->m_pIntelligence->m_TaskMgr.m_aPrimaryTasks[sa::TASK_PRIMARY_PRIMARY]);
		if (iType == sa::TASK_COMPLEX_ENTER_CAR_AS_PASSENGER || iType == sa::TASK_COMPLEX_ENTER_CAR_AS_PASSENGER_TIMED) {
			return 2;
		}
		if (iType == sa::TASK_COMPLEX_ENTER_CAR_AS_DRIVER || iType == sa::TASK_COMPLEX_ENTER_CAR_AS_DRIVER_TIMED) {
			return 1;
		}
	}
	return 0;
}

bool CPlayerPed::IsExitingVehicle()
{
	if (m_ped && m_ped->m_pIntelligence && m_ped->m_pIntelligence->m_TaskMgr.m_aPrimaryTasks[sa::TASK_PRIMARY_PRIMARY]) {
		return GetTaskTypeFromTask(&m_ped->m_pIntelligence->m_TaskMgr.m_aPrimaryTasks[sa::TASK_PRIMARY_PRIMARY]) == sa::TASK_COMPLEX_LEAVE_CAR;
	}

	return false;
}

bool CPlayerPed::IsJumpTask()
{
	if (m_ped && !IsInVehicle() && m_ped->m_pIntelligence && m_ped->m_pIntelligence->m_TaskMgr.m_aPrimaryTasks[sa::TASK_PRIMARY_PRIMARY]) {
		return GetTaskTypeFromTask(&m_ped->m_pIntelligence->m_TaskMgr.m_aPrimaryTasks[sa::TASK_PRIMARY_PRIMARY]) == sa::TASK_COMPLEX_JUMP;
	}

	return false;
}

bool CPlayerPed::IsSitTask()
{
	if (!m_ped || !GamePool_Ped_GetAt(m_gtaId) || IsInVehicle() || !m_ped->m_pIntelligence || !m_ped->m_pIntelligence->m_TaskMgr.m_aPrimaryTasks[sa::TASK_PRIMARY_PRIMARY]) {
		return false;
	}

	return GetTaskTypeFromTask(&m_ped->m_pIntelligence->m_TaskMgr.m_aPrimaryTasks[sa::TASK_PRIMARY_PRIMARY]) == sa::TASK_SIMPLE_SIT_DOWN ||
			GetTaskTypeFromTask(&m_ped->m_pIntelligence->m_TaskMgr.m_aPrimaryTasks[sa::TASK_PRIMARY_PRIMARY]) == sa::TASK_SIMPLE_SIT_IDLE;
}

bool CPlayerPed::CanDoPassengerDriveBy()
{
	if (!m_ped || !m_ped->IsInVehicle() || !m_ped->pVehicle) {
		return false;
	}
	int iWeapon = GetCurrentWeapon();
	switch (iWeapon) {
		case WEAPON_COLT45:
		case WEAPON_SILENCED:
		case WEAPON_DEAGLE:
		case WEAPON_TEC9:
		case WEAPON_UZI:
		case WEAPON_MP5:
		case WEAPON_AK47:
		case WEAPON_M4:
		case WEAPON_SAWEDOFF:
		case WEAPON_SHOTGSPA:
		case WEAPON_MINIGUN:
		case WEAPON_GRENADE:
		case WEAPON_MOLOTOV:
		case WEAPON_SATCHEL:
			return true;
		default:
			return false;
	}
}

bool CPlayerPed::IsInPassengerDriveByMode()
{
	if (!m_ped || !GamePool_Ped_GetAt(m_gtaId) || !IsInVehicle() || !m_ped->m_pIntelligence ||
			!m_ped->m_pIntelligence->m_TaskMgr.m_aPrimaryTasks[sa::TASK_PRIMARY_PRIMARY]) {
		return false;
	}

	return GetTaskTypeFromTask(&m_ped->m_pIntelligence->m_TaskMgr.m_aPrimaryTasks[sa::TASK_PRIMARY_PRIMARY]) == sa::TASK_SIMPLE_GANG_DRIVEBY;
}

bool CPlayerPed::StartPassengerDriveByMode()
{
	if (m_ped) {
		if (!m_ped->IsInVehicle() || !m_ped->pVehicle || m_ped->m_nPedState != ACTION_INCAR) {
			return false;
		}

		int iWeapon = GetCurrentWeapon();

		if (iWeapon == WEAPON_PARACHUTE) {
			SetArmedWeapon(0);
			return false;
		}

		if (!CanDoPassengerDriveBy()) {
			return false;
		}

		SetArmedWeapon(iWeapon);

		ScriptCommand(&enter_passenger_driveby, m_gtaId, -1, -1, 0.0f, 0.0f, 0.0f, 300.0f, 8, 1, 100);

		return true;
	}
	return false;
}

void CPlayerPed::StopPassengerDriveByMode()
{
	if (!m_ped || !GamePool_Ped_GetAt(m_gtaId) || !IsInVehicle()) {
		return;
	}

	if (IsInPassengerDriveByMode()) {
		sa::CTask* pDriveByTask = m_ped->m_pIntelligence->m_TaskMgr.m_aPrimaryTasks[sa::TASK_PRIMARY_PRIMARY];
		eng::fn::CTaskSimpleGangDriveBy_Dtor_CTaskSimpleGangDriveBy(pDriveByTask); // CTaskSimpleGangDriveBy::~CTaskSimpleGangDriveBy
		m_ped->m_pIntelligence->m_TaskMgr.m_aPrimaryTasks[sa::TASK_PRIMARY_PRIMARY] = nullptr;
	}
}

void CPlayerPed::ExtinguishFire()
{
	if (!m_ped || !GamePool_Ped_GetAt(m_gtaId)) {
		return;
	}

	if (m_ped->m_pFire) {
		eng::fn::CFire_Extinguish(m_ped->m_pFire);
	}

	if (m_ped->m_nPedFlags.bInVehicle && m_ped->pVehicle && m_ped->pVehicle->m_pFire) {
		eng::fn::CFire_Extinguish(m_ped->pVehicle->m_pFire);
	}
}

void CPlayerPed::SetImmunities(int bullet, int flame, int explode, int collision, int melee)
{
	if (m_ped && GamePool_Ped_GetAt(m_gtaId)) {
		ScriptCommand(&set_actor_immunities, m_gtaId, bullet, flame, explode, collision, melee);
	}
}

void CPlayerPed::SetSkillLevel(int iSkillID, int iLevel)
{
	if (iSkillID < 0 || iSkillID >= kWeaponSkillCount) return;
	const uint16_t clamped = static_cast<uint16_t>(iLevel < 0 ? 0 : (iLevel > 1000 ? 1000 : iLevel));
	if (m_bytePlayerNumber == 0) {
		GameSetLocalPlayerSkill(iSkillID, clamped);
	}
	else {
		GameSetRemotePlayerSkill(m_bytePlayerNumber, iSkillID, clamped);
	}
}
void CPlayerPed::UpdateLocalPlayerSkill(int iSkillID, int iLevel)
{
	if (iSkillID < 0 || iSkillID >= kWeaponSkillCount) return;
	const uint16_t clamped = static_cast<uint16_t>(iLevel < 0 ? 0 : (iLevel > 1000 ? 1000 : iLevel));
	GameSetLocalPlayerSkill(iSkillID, clamped);
}

void CPlayerPed::SetFightingStyle(int iStyle)
{
	if (m_gtaId) {
		ScriptCommand(&set_fighting_style, m_gtaId, iStyle, 6);
	}
}

float CPlayerPed::GetHealth()
{
	return m_ped ? m_ped->m_fHealth : 0.0f;
}

int CPlayerPed::GetVehicleSeatID()
{
	if (!m_ped) return -1;
	if (m_ped->m_nPedState == ACTION_INCAR && m_ped->pVehicle) {
		sa::CVehicle* pVehicle = (sa::CVehicle*) m_ped->pVehicle;

		if (pVehicle->pDriver == m_ped) {
			return 0;
		}
		for (int i = 0; i < 8; i++) {
			if (pVehicle->m_apPassengers[i] == m_ped) {
				return i + 1;
			}
		}
	}

	return -1;
}

void CPlayerPed::SetStateFlags(sa::CPedFlags dwState)
{
	if (m_ped) {
		m_ped->m_nPedFlags = dwState;
	}
}

sa::CPedFlags CPlayerPed::GetStateFlags()
{
	if (m_ped) {
		return m_ped->m_nPedFlags;
	}
	return {};
}

bool CPlayerPed::IsOnGround()
{
	if (m_ped) {
		const auto& flags = m_ped->m_nPedFlags;
		return flags.bIsStanding || flags.bWasStanding;
	}
	return false;
}

void CPlayerPed::SetDead()
{
	RwMatrix mat;

	if (m_gtaId && m_ped) {
		if (IsInJetpack()) {
			StopJetpack();
		}
		if (m_iStuffType != STUFF_TYPE_NONE || m_dwStuffObject != 0) {
			DropStuff();
		}
		if (!m_ped->IsInVehicle()) {
			// ~
		}

		ExtinguishFire();
		mat = m_ped->GetMatrix().ToRwMatrix();
		TeleportTo(mat.pos.x, mat.pos.y, mat.pos.z);
		m_ped->m_fHealth = 0.0f;
		{
			ScopedPlayerContext ctx(m_bytePlayerNumber, ScopedPlayerContext::kFocusOnly);
			ScriptCommand(&kill_actor, m_gtaId);
		}
	}
}

void CPlayerPed::SetHealth(float fHealth)
{
	if (m_ped) {
		m_ped->m_fHealth = fHealth;
	}
}

float CPlayerPed::GetArmour()
{
	if (!m_ped) {
		return 0.0f;
	}
	return m_ped->m_fArmour;
}

void CPlayerPed::SetArmour(float fArmour)
{
	if (!m_ped) {
		return;
	}
	m_ped->m_fArmour = fArmour;
}

sa::CVehicle* CPlayerPed::GetGtaVehicle()
{
	return m_ped ? m_ped->pVehicle : nullptr;
}

void CPlayerPed::SatisfyHunger()
{
	// todo
}

void CPlayerPed::SetInterior(uint8_t byteInteriorId, bool bRefresh)
{
	if (!m_ped) return;
	if (m_bytePlayerNumber != 0) {
		ScriptCommand(&link_actor_to_interior, m_gtaId, byteInteriorId);
	}
	else {
		ScriptCommand(&select_interior, byteInteriorId);
		ScriptCommand(&link_actor_to_interior, m_gtaId, byteInteriorId);
		if (bRefresh) {
			RwMatrix mat = m_ped->GetMatrix().ToRwMatrix();
			spdlog::debug("refresh streaming at x: {}, y: {}", mat.pos.x, mat.pos.y);
			pGame->RefreshStreamingAt(mat.pos.x, mat.pos.y);
		}
	}
}

void CPlayerPed::ClearWeapons()
{
	if (!m_ped || !pbyteCurrentPlayer) {
		return;
	}

	ScopedPlayerContext ctx(m_bytePlayerNumber, ScopedPlayerContext::kFocusOnly);
	m_ped->ClearWeapons();
}

static RpAtomic* CountClumpVerticesCB(RpAtomic* atomic, void* data)
{
	if (atomic && data) {
		// Disassembly-verified RenderWare offsets in libGTASA.so v2.10 (matching Hooks.cpp)
		constexpr size_t kAtomicGeomOff   = VER_x32 ? 24 : 48;
		constexpr size_t kGeomNumVertsOff = VER_x32 ? 20 : 28;
		const auto* rawAtomic = reinterpret_cast<const uint8_t*>(atomic);
		const auto* geom = *reinterpret_cast<const uint8_t* const*>(rawAtomic + kAtomicGeomOff);
		if (geom) {
			const int32_t numVerts = *reinterpret_cast<const int32_t*>(geom + kGeomNumVertsOff);
			if (numVerts > 0) {
				*static_cast<int*>(data) += numVerts;
			}
		}
	}
	return atomic;
}

static int CountClumpVertices(RwObject* rwObj)
{
	if (!rwObj) {
		return 0;
	}
	int totalVerts = 0;
	using AtomicCB = RpAtomic* (*)(RpAtomic*, void*);
	eng::fn::RpClumpForAllAtomics(
		reinterpret_cast<RpClump*>(rwObj),
		static_cast<AtomicCB>(&CountClumpVerticesCB),
		&totalVerts);
	return totalVerts;
}

uint EnsureValidPedModelPrototype(uint uiModel)
{
	if (!IsValidPedModel(uiModel)) {
		spdlog::error("EnsureValidPedModelPrototype: invalid model {}", uiModel);
		uiModel = 0;
	}

	// P1-16: player.dff in gta3.img is a 1-atomic placeholder clump with only 2 triangles /
	// 6 vertices (skeleton holder); because clean SAMP/main.scm runs 0053 (create_player)
	// without 070D (rebuild_player), GetModelRWObject(0) is non-null with 1 atomic (6 verts)
	// and CJ renders invisible. Detect the placeholder via vertex count (< 100) and run
	// CClothes::ConstructPedModel(0, ...) once when texdb/player.img is present (no runtime
	// .text unpatch); fall back to skin 29 if unavailable.
	if (uiModel == 0 && pGame) {
		RwObject* rwModel0 = GetModelRWObject(0);
		if (!rwModel0) {
			pGame->RequestModel(0);
			pGame->LoadRequestedModels();
			rwModel0 = GetModelRWObject(0);
		}
		if (CountClumpVertices(rwModel0) < 100) {
			static int s_cjImgState = -1; // -1=unchecked, 0=missing, 1=present
			if (s_cjImgState < 0) {
				const char* dir = Client::gameDir();
				std::string base = dir ? std::string(dir) : std::string();
				if (!base.empty() && base.back() != '/') {
					base.push_back('/');
				}
				const bool hasPlayerImg = !base.empty() && (
					access((base + "texdb/player.img").c_str(), F_OK) == 0 ||
					access((base + "TEXDB/PLAYER.IMG").c_str(), F_OK) == 0 ||
					access((base + "texdb/PLAYER.IMG").c_str(), F_OK) == 0);
				s_cjImgState = hasPlayerImg ? 1 : 0;
				if (!hasPlayerImg) {
					spdlog::warn("EnsureValidPedModelPrototype(0): texdb/player.img missing; falling back to skin 29");
				}
			}

			if (s_cjImgState == 1 && rwModel0) {
				sa::CPed* pLocalGtaPed = GamePool_FindPlayerPed();
				if (pLocalGtaPed && pLocalGtaPed->m_pPlayerData && pLocalGtaPed->m_pPlayerData->m_pPedClothesDesc) {
					void* clothesDesc = pLocalGtaPed->m_pPlayerData->m_pPedClothesDesc;
					// Default classic CJ outfit (white tank top, default head, blue jeans, black sneakers)
					eng::fn::CPedClothesDesc_SetTextureAndModel(clothesDesc, "vest", "vest", 0);
					eng::fn::CPedClothesDesc_SetTextureAndModel(clothesDesc, "player_face", "head", 1);
					eng::fn::CPedClothesDesc_SetTextureAndModel(clothesDesc, "jeansdenim", "jeans", 2);
					eng::fn::CPedClothesDesc_SetTextureAndModel(clothesDesc, "sneakerbincblk", "sneaker", 3);
					eng::fn::CClothes_ConstructPedModel(0u, clothesDesc, static_cast<const void*>(nullptr), false);
					rwModel0 = GetModelRWObject(0);
					spdlog::info("EnsureValidPedModelPrototype(0): CClothes::ConstructPedModel built {} vertices",
						CountClumpVertices(rwModel0));
				}
			}

			if (CountClumpVertices(rwModel0) < 100) {
				spdlog::warn("EnsureValidPedModelPrototype(0): model 0 still placeholder (<100 verts); falling back to skin 29");
				uiModel = 29;
			}
		}
	}

	return uiModel;
}

void CPlayerPed::RememberServerSkin(int baseSkin, int customSkin)
{
	m_iFallbackSkin = baseSkin;
	m_iServerSkin = artwork::isCustomSkinId(customSkin) ? customSkin : baseSkin;
}

void CPlayerPed::SetSkin(int baseSkin, int customSkin)
{
	RememberServerSkin(baseSkin, customSkin);
	const int effective = ResolveEffectivePedSkin(baseSkin, customSkin);
	if (IsValidPedModel(static_cast<uint>(effective))) {
		ApplyGtaModelIndex(static_cast<uint>(effective));
	}
}

bool CPlayerPed::RefreshCustomSkin()
{
	if (!m_ped || !artwork::isCustomSkinId(m_iServerSkin) || artwork::isInternalCustomSlot(m_ped->m_nModelIndex)) {
		return false;
	}
	const int32_t slot = artwork::ArtworkManager::instance().ensureCustomSkinLoaded(m_iServerSkin);
	if (slot >= 0 && IsValidPedModel(static_cast<uint>(slot))) {
		ApplyGtaModelIndex(static_cast<uint>(slot));
		return true;
	}
	return false;
}

void CPlayerPed::RevertCustomSkinBeforeUnload()
{
	if (!m_ped || !artwork::isInternalCustomSlot(m_ped->m_nModelIndex)) {
		return;
	}
	const uint fallback = (m_iFallbackSkin >= 0 && !artwork::isInternalCustomSlot(m_iFallbackSkin)
		&& IsValidPedModel(static_cast<uint>(m_iFallbackSkin)))
		? static_cast<uint>(m_iFallbackSkin)
		: 0u;
	ApplyGtaModelIndex(fallback);
}

void CPlayerPed::SetModelIndex(uint uiModel)
{
	if (artwork::isCustomSkinId(static_cast<int>(uiModel))) {
		const int baseSkin = (m_iFallbackSkin >= 0 && !artwork::isCustomSkinId(m_iFallbackSkin)
			&& !artwork::isInternalCustomSlot(m_iFallbackSkin))
			? m_iFallbackSkin
			: artwork::ArtworkManager::instance().resolveFallbackSkinModel(static_cast<int32_t>(uiModel));
		SetSkin(baseSkin, static_cast<int>(uiModel));
		return;
	}
	if (!artwork::isInternalCustomSlot(static_cast<int>(uiModel))) {
		m_iServerSkin = static_cast<int>(uiModel);
		m_iFallbackSkin = static_cast<int>(uiModel);
	}
	ApplyGtaModelIndex(uiModel);
}

void CPlayerPed::ApplyGtaModelIndex(uint uiModel)
{
	spdlog::debug("ApplyGtaModelIndex: {}", uiModel);

	if (!m_ped) {
		spdlog::error("SetModelIndex: m_ped is null");
		return;
	}

	if (!GamePool_Ped_GetAt(m_gtaId)) {
		spdlog::error("SetModelIndex: invalid gta id");
		return;
	}

	uiModel = EnsureValidPedModelPrototype(uiModel);

	// Preserve seated vehicle state across DeleteRWObject -> SetModelIndex:
	// rebuilding RpClump/RpHAnimHierarchy while seated severs CAR_sit / BIKEs_Ride and T-poses.
	bool bRestoreVehicle = false;
	uint32_t savedVehGtaId = 0;
	uint8_t savedSeatId = 0;
	if (IsInVehicle()) {
		if (sa::CVehicle* pVeh = GetGtaVehicle()) {
			const uint32_t vehGtaId = GamePool_Vehicle_GetIndex(pVeh);
			const int seatId = GetVehicleSeatID();
			if (vehGtaId != 0 && GamePool_Vehicle_GetAt(vehGtaId) == pVeh && seatId >= 0) {
				bRestoreVehicle = true;
				savedVehGtaId = vehGtaId;
				savedSeatId = static_cast<uint8_t>(seatId);
				const RwMatrix vehMat = pVeh->GetMatrix().ToRwMatrix();
				RemoveFromVehicleAndPutAt(vehMat.pos.x, vehMat.pos.y, vehMat.pos.z + 0.5f);
			}
		}
	}

	// CClothes::RebuildPlayer (registered in PatchRegistry as Session patch)
	patches::set("CClothes::RebuildPlayer", true);

	DestroyFollowPedTask();

	CEntity::SetModelIndex(uiModel);

	// CAEPedSpeechAudioEntity::Initialise
	eng::fn::CAEPedSpeechAudioEntity_Initialise( &m_ped->m_PedSpeechAudioEntity, m_ped);

	if (bRestoreVehicle) {
		PutDirectlyInVehicle(savedVehGtaId, savedSeatId);
	}
}

void CPlayerPed::DestroyFollowPedTask()
{
	if (!m_ped || !m_ped->m_pIntelligence) {
		return;
	}

	sa::CTask* dwIK = m_ped->m_pIntelligence->m_TaskMgr.m_aSecondaryTasks[sa::TASK_SECONDARY_IK];
	if (dwIK) {
		// CTaskSimpleIKManager::~CTaskSimpleIKManager
		eng::fn::CTaskSimpleIKManager_Dtor_CTaskSimpleIKManager( dwIK);
		m_ped->m_pIntelligence->m_TaskMgr.m_aSecondaryTasks[sa::TASK_SECONDARY_IK] = nullptr;
	}
}

void CPlayerPed::RestartIfWastedAt(sa::CVector* vecRestart, float fRotation)
{
	ScriptCommand(&restart_if_wasted_at, vecRestart->x, vecRestart->y, vecRestart->z, fRotation, 0);
}

void CPlayerPed::SetTargetRotation(float fRotation)
{
	if (m_ped && GamePool_Ped_GetAt(m_gtaId)) {
		m_ped->m_fCurrentRotation = DegToRad(fRotation);
		m_ped->m_fAimingRotation = DegToRad(fRotation);
		ScriptCommand(&set_actor_z_angle, m_gtaId, fRotation);
	}
}

void CPlayerPed::ApplyCommandTask(const char* szTaskName, void* p1, int p2, int p3, sa::CVector* p4, uint8_t p5, float p6, int p7, int p8, uint8_t p9)
{
	if (!m_ped) {
		return;
	}

	// IKChainManager_c::LookAt
	eng::fn::IKChainManager_c_LookAt( eng::ptr(eng::Sym::g_ikChainMan), szTaskName, (uintptr_t) m_ped, p1, p2, p3, p4, p5, p6, p7, p8, p9);
}

void CPlayerPed::ClearPrimaryTask()
{
	if (m_ped && GamePool_Ped_GetAt(m_gtaId) && m_ped->m_pIntelligence
			&& m_ped->m_pIntelligence->m_TaskMgr.m_aPrimaryTasks[sa::TASK_PRIMARY_PRIMARY]) {
		eng::fn::CTaskManager_SetTask(&m_ped->m_pIntelligence->m_TaskMgr, nullptr, sa::TASK_PRIMARY_PRIMARY, false);
	}
}

void CPlayerPed::SetRotation(float fRotation)
{
	if (m_ped) {
		if (GamePool_Ped_GetAt(m_gtaId)) {
			m_ped->m_fCurrentRotation = DegToRad(fRotation);
			m_ped->m_fAimingRotation = DegToRad(fRotation);
		}
	}
}

void CPlayerPed::ExitCurrentVehicle()
{
	if (!m_ped) {
		return;
	}
	if (!GamePool_Ped_GetAt(m_gtaId)) {
		return;
	}

	if (m_ped->IsInVehicle()) {
		int dwGtaVehicleID = GetCurrentVehicleID();
		if (dwGtaVehicleID) {
			sa::CVehicle* pGtaVehicle = GamePool_Vehicle_GetAt(dwGtaVehicleID);
			if (pGtaVehicle) {
				int iModelIndex = pGtaVehicle->m_nModelIndex;
				if (iModelIndex != TRAIN_PASSENGER && iModelIndex != TRAIN_PASSENGER_LOCO) {
					//trigger script command LeaveCar(ped,vehicle);
					ScriptCommand(&make_actor_leave_car, m_gtaId, dwGtaVehicleID);
				}
			}
		}
	}
	else if (IsEnteringVehicle() != 0) {
		ClearPrimaryTask();
	}
}

int CPlayerPed::GetCurrentVehicleID()
{
	if (m_ped) {
		return GamePool_Vehicle_GetIndex(m_ped->pVehicle);
	}

	return 0;
}

void CPlayerPed::ProcessVehicleHorn()
{

}

void CPlayerPed::PutDirectlyInVehicle(uint32_t dwVehicleGTAId, uint8_t byteSeatID)
{
	spdlog::debug("PutDirectlyInVehicle: {} {}", dwVehicleGTAId, byteSeatID);

	if (!m_ped) {
		return;
	}

	if (!GamePool_Vehicle_GetAt(dwVehicleGTAId)) {
		return;
	}

	if (!GamePool_Ped_GetAt(m_gtaId)) {
		return;
	}

	if (GetCurrentWeapon() == WEAPON_PARACHUTE) {
		SetArmedWeapon(0);
	}

	sa::CVehicle* pGtaVehicle = GamePool_Vehicle_GetAt(dwVehicleGTAId);

	if (pGtaVehicle->fHealth != 0.0f && !IsEntityPlaceable(pGtaVehicle)) {
		uint8_t vehSubtype = 0;
		if (pNetGame && pNetGame->GetVehiclePool()) {
			VEHICLEID vid = pNetGame->GetVehiclePool()->FindIDFromGtaPtr(pGtaVehicle);
			CVehicle* pVeh = (vid != INVALID_VEHICLE_ID) ? pNetGame->GetVehiclePool()->GetAt(vid) : nullptr;
			if (pVeh) vehSubtype = pVeh->GetVehicleSubtype();
		}
		if ((vehSubtype == VEHICLE_SUBTYPE_CAR || vehSubtype == VEHICLE_SUBTYPE_BIKE)
				&& byteSeatID > pGtaVehicle->m_nMaxPassengers) {
			return;
		}

		// Cleanly detach from any previous vehicle/seat first so pDriver / m_apPassengers[]
		// never retains a stale CPed* across direct seat or vehicle switches.
		if (m_ped->IsInVehicle()) {
			const bool bSameVehAndSeat = (m_ped->pVehicle == pGtaVehicle)
				&& (GetVehicleSeatID() == static_cast<int>(byteSeatID));
			if (bSameVehAndSeat) {
				return;
			}
			const RwMatrix curMat = pGtaVehicle->GetMatrix().ToRwMatrix();
			RemoveFromVehicleAndPutAt(curMat.pos.x, curMat.pos.y, curMat.pos.z + 0.5f);
		}
		else if (IsEnteringVehicle() != 0) {
			ClearPrimaryTask();
		}

		if (byteSeatID) {
			const int psgrSlot = static_cast<int>(byteSeatID) - 1;
			sa::CPed* pLocalGtaPed = GamePool_FindPlayerPed();
			// Never allow a remote PutDirectlyInVehicle (0x0430) to run CTaskSimpleCarSetPedOut
			// on the local player if both clients transiently target the same passenger slot.
			if (m_ped != pLocalGtaPed && psgrSlot >= 0 && psgrSlot < 8
					&& pLocalGtaPed && pGtaVehicle->m_apPassengers[psgrSlot] == pLocalGtaPed
					&& pLocalGtaPed->IsInVehicle()) {
				int fallbackSlot = -1;
				const int maxSlots = std::min<int>(static_cast<int>(pGtaVehicle->m_nMaxPassengers), 8);
				for (int s = 0; s < maxSlots; ++s) {
					if (pGtaVehicle->m_apPassengers[s] == nullptr) {
						fallbackSlot = s;
						break;
					}
				}
				if (fallbackSlot < 0) {
					return;
				}
				ScriptCommand(&put_actor_in_car2, m_gtaId, dwVehicleGTAId, fallbackSlot);
			}
			else {
				ScriptCommand(&put_actor_in_car2, m_gtaId, dwVehicleGTAId, psgrSlot);
			}
		}
		else {
			if (pGtaVehicle->pDriver && pGtaVehicle->pDriver != m_ped && pGtaVehicle->pDriver->IsInVehicle()) {
				sa::CPed* pLocalGtaPed = GamePool_FindPlayerPed();
				if (pGtaVehicle->pDriver == pLocalGtaPed && m_ped != pLocalGtaPed) {
					return;
				}
				// Evict stale remote driver (e.g. rapid seat swap before old driver's OnFootSync arrives)
				if (pNetGame && pNetGame->GetPlayerPool()) {
					PLAYERID oldDriverId = pNetGame->GetPlayerPool()->FindRemotePlayerIDFromGtaPtr(pGtaVehicle->pDriver);
					if (oldDriverId != INVALID_PLAYER_ID) {
						if (CRemotePlayer* pOldDriver = pNetGame->GetPlayerPool()->GetAt(oldDriverId)) {
							if (CPlayerPed* pOldPed = pOldDriver->GetPlayerPed()) {
								const RwMatrix curMat = pGtaVehicle->GetMatrix().ToRwMatrix();
								pOldPed->RemoveFromVehicleAndPutAt(curMat.pos.x, curMat.pos.y, curMat.pos.z + 1.0f);
							}
						}
					}
				}
				if (pGtaVehicle->pDriver && pGtaVehicle->pDriver != m_ped && pGtaVehicle->pDriver->IsInVehicle()) {
					return;
				}
			}

			ScriptCommand(&put_actor_in_car, m_gtaId, dwVehicleGTAId);
		}

		if (m_ped == GamePool_FindPlayerPed()) {
			if (m_ped->IsInVehicle()) {
				pGame->GetCamera()->SetBehindPlayer();
			}
		}

		if (pNetGame) {
			CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
			if (!pVehiclePool) {
				return;
			}
			VEHICLEID VehicleID = pVehiclePool->FindIDFromGtaPtr(pGtaVehicle);
			if (VehicleID != INVALID_VEHICLE_ID && VehicleID < MAX_VEHICLES) {
				CVehicle* pVehicle = pVehiclePool->GetAt(VehicleID);
				if (pVehicle) {
					if (pVehicle->IsATrainPart()) {
						if (m_ped == GamePool_FindPlayerPed()) {
							ScriptCommand(&camera_on_vehicle, pVehicle->m_gtaId, 3, 2);
						}
					}
				}
			}
		}
	}
}

void CPlayerPed::EnterVehicle(uint32_t dwVehicleGTAId, bool bPassenger)
{
	if (!m_ped) {
		return;
	}

	sa::CVehicle* pGtaVehicle = GamePool_Vehicle_GetAt(dwVehicleGTAId);
	if (!pGtaVehicle) {
		return;
	}

	if (!GamePool_Ped_GetAt(m_gtaId)) {
		return;
	}

	// hide parachute
	if (GetCurrentWeapon() == WEAPON_PARACHUTE) {
		SetArmedWeapon(0);
	}

	// Pass time = -1 so CTaskComplexEnterCarAsPassengerTimed / DriverTimed uses GTA's
	// built-in untimed path (tbnz w8, #31) instead of warping into the seat at 3000ms
	// and skipping CarOpenDoorFromOutside / CarGetIn when walking >3s to reach the door.
	if (bPassenger) {
		if (pGtaVehicle->m_nModelIndex != TRAIN_PASSENGER || m_ped != GamePool_FindPlayerPed()) {
			ScriptCommand(&send_actor_to_car_passenger, m_gtaId, dwVehicleGTAId, -1, -1);
		}
		else {
			ScriptCommand(&put_actor_in_car2, m_gtaId, dwVehicleGTAId, -1);
		}
	}
	else {
		ScriptCommand(&send_actor_to_car_driverseat, m_gtaId, dwVehicleGTAId, -1);
	}
}

sa::CEntity* CPlayerPed::GetEntityUnderPlayer()
{
	if (!m_ped || !GamePool_Ped_GetAt(m_gtaId)) {
		return nullptr;
	}

	sa::CEntity* entity = nullptr;
	sa::CVector vecStart;
	sa::CVector vecEnd;
	sa::CColPoint colPoint{};

	if (IsInVehicle()) {
		return nullptr;
	}

	const sa::CVector pos = m_ped->GetPosition();
	vecStart.x = pos.x;
	vecStart.y = pos.y;
	vecStart.z = pos.z - 0.25f;

	vecEnd.x = pos.x;
	vecEnd.y = pos.y;
	vecEnd.z = vecStart.z - 1.75f;

	ProcessLineOfSight(&vecStart, &vecEnd, &colPoint, &entity, false, true, false, true, false, false, false, false);
	return entity;
}

void CPlayerPed::ResetDamageEntity()
{
	if (!m_ped || !GamePool_Ped_GetAt(m_gtaId)) {
		return;
	}
	m_ped->m_pLastEntityDamage = nullptr;
	m_ped->m_nLastDamagedWeaponType = 0xFF;
	m_ped->m_pDamageEntity = nullptr;
}

int CPlayerPed::GetPedStat()
{
	if (!m_ped) {
		return -1;
	}

	auto* pModelInfo = reinterpret_cast<sa::CPedModelInfo*>(GetModelInfoByID(m_ped->m_nModelIndex));
	if (!pModelInfo) {
		return -1;
	}
	return (int) pModelInfo->m_defaultPedStats;
}
