#include "PlayerPed.h"
#include "Game.h"
#include "Common.h"
#include "Pad.h"
#include "Scripting.h"
#include "Util.h"
#include "PlayerContextScope.h"
#include "Engine/Symbols.h"
#include "RenderWare/rphanim.h"
#include "../Addr.h"
#include "../Net/NetGame.h"
#include "../Net/LocalPlayer.h"
#include "../Client.h"
#include <SA.h>
#include <spdlog/spdlog.h>

namespace {
// Disassembly-verified CTaskSimpleDuck & CAnimBlendAssociation offsets (arm32 / arm64)
constexpr uintptr_t kTaskDuckMoveAnimOff = VER_x32 ? 0x14 : 0x20;
constexpr uintptr_t kTaskDuckAbortingOff = VER_x32 ? 0x19 : 0x29;
constexpr uintptr_t kAnimAssocAnimIdOff  = VER_x32 ? 0x2C : 0x44;

uintptr_t GetPedTaskDuck(sa::CPed* pPed)
{
	if (!pPed || !pPed->m_pIntelligence) return 0;
	return eng::fn::CPedIntelligence_GetTaskDuck(pPed->m_pIntelligence, true);
}

bool IsDuckTaskAborting(uintptr_t taskDuck)
{
	return taskDuck && *reinterpret_cast<const uint8_t*>(taskDuck + kTaskDuckAbortingOff) != 0;
}

bool IsDuckTaskRolling(uintptr_t taskDuck)
{
	if (!taskDuck) return false;
	const uintptr_t moveAnim = *reinterpret_cast<const uintptr_t*>(taskDuck + kTaskDuckMoveAnimOff);
	if (!moveAnim) return false;
	const int16_t animId = *reinterpret_cast<const int16_t*>(moveAnim + kAnimAssocAnimIdOff);
	return (animId | 2) == 0x3B; // CROUCH_ROLL_L (0x39) or CROUCH_ROLL_R (0x3B)
}
} // namespace

void CPlayerPed::SetSpecialAction(uint8_t byteSpecialAction)
{
	// duck:start (also restart if an existing CTaskSimpleDuck is mid-abort)
	const uintptr_t taskDuck = GetPedTaskDuck(m_ped);
	if (byteSpecialAction == SPECIAL_ACTION_DUCK) {
		if (!IsCrouching() || IsDuckTaskAborting(taskDuck)) {
			ApplyCrouch();
		}
	}
	// duck:stop (also clean up if CTaskSimpleDuck survived without bIsDucking)
	else if (IsCrouching() || taskDuck != 0) {
		ResetCrouch();
	}

	// cellphone:start
	if (byteSpecialAction == SPECIAL_ACTION_USECELLPHONE && !IsCellphoneEnabled()) {
		ToggleCellphone(true);
	}

	// cellphone:stop
	if (byteSpecialAction != SPECIAL_ACTION_USECELLPHONE && IsCellphoneEnabled()) {
		ToggleCellphone(false);
	}

	// jetpack:start
	if (byteSpecialAction == SPECIAL_ACTION_USEJETPACK && !IsInJetpack()) {
		StartJetpack();
	}

	// jetpack:stop
	if (byteSpecialAction != SPECIAL_ACTION_USEJETPACK && IsInJetpack()) {
		StopJetpack();
	}

	// carry:start
	if (byteSpecialAction == SPECIAL_ACTION_CARRY && !IsCarry()) {
		ToggleCarry(true);
	}

	// carry:stop
	if (byteSpecialAction != SPECIAL_ACTION_CARRY && IsCarry()) {
		ToggleCarry(false);
	}

	// cuffed:start
	if (byteSpecialAction == SPECIAL_ACTION_CUFFED && !IsCuffed()) {
		ToggleCuffed(true);
	}

	// cuffed:stop
	if (byteSpecialAction != SPECIAL_ACTION_CUFFED && IsCuffed()) {
		ToggleCuffed(false);
	}

	// handsup:start
	if (byteSpecialAction == SPECIAL_ACTION_HANDSUP && !HasHandsUp()) {
		HandsUp();
	}

	// handsup:stop
	if (byteSpecialAction != SPECIAL_ACTION_HANDSUP && HasHandsUp()) {
		TogglePlayerControllable(true);
	}

	// urinating:start
	if (byteSpecialAction == SPECIAL_ACTION_PISSING && !IsPissing()) {
		TogglePissing(true);
	}

	// urinating:stop
	if (byteSpecialAction != SPECIAL_ACTION_PISSING && IsPissing()) {
		TogglePissing(false);
	}

	// T17: dancing special actions (5..8)
	int targetDance = -1;
	switch (byteSpecialAction) {
		case SPECIAL_ACTION_DANCE1: targetDance = 0; break;
		case SPECIAL_ACTION_DANCE2: targetDance = 1; break;
		case SPECIAL_ACTION_DANCE3: targetDance = 2; break;
		case SPECIAL_ACTION_DANCE4: targetDance = 3; break;
		default: break;
	}
	if (targetDance != -1) {
		if (GetDanceStyle() != targetDance) {
			StartDancing(targetDance);
		}
	}
	else if (GetDanceStyle() != -1) {
		StopDancing();
	}

	// T17: drinking & smoking special actions (20..23)
	int targetStuff = STUFF_TYPE_NONE;
	switch (byteSpecialAction) {
		case SPECIAL_ACTION_DRINK_BEER:   targetStuff = STUFF_TYPE_BEER; break;
		case SPECIAL_ACTION_SMOKE_CIGGY:  targetStuff = STUFF_TYPE_CIGGI; break;
		case SPECIAL_ACTION_DRINK_WINE:   targetStuff = STUFF_TYPE_DYN_BEER; break;
		case SPECIAL_ACTION_DRINK_SPRUNK: targetStuff = STUFF_TYPE_PINT_GLASS; break;
		default: break;
	}
	if (targetStuff != STUFF_TYPE_NONE) {
		if (GetStuff() != targetStuff) {
			GiveStuff(targetStuff);
		}
		else if (!IsInVehicle() && GetCurrentWeapon() != 0) {
			SetArmedWeapon(0);
		}
	}
	else if (GetStuff() != STUFF_TYPE_NONE) {
		DropStuff();
	}

	ProcessDancing();
	ProcessParachutes();
}

uint8_t CPlayerPed::GetSpecialAction()
{
	if (IsEnteringVehicle() != 0) {
		return SPECIAL_ACTION_ENTER_VEHICLE;
	}

	if (IsExitingVehicle()) {
		return SPECIAL_ACTION_EXIT_VEHICLE;
	}

	if (IsInJetpack()) {
		return SPECIAL_ACTION_USEJETPACK;
	}

	if (IsCuffed()) {
		return SPECIAL_ACTION_CUFFED;
	}

	if (IsCarry()) {
		return SPECIAL_ACTION_CARRY;
	}

	if (GetDanceStyle() != -1) {
		switch (GetDanceStyle()) {
			case 0: return SPECIAL_ACTION_DANCE1;
			case 1: return SPECIAL_ACTION_DANCE2;
			case 2: return SPECIAL_ACTION_DANCE3;
			case 3: return SPECIAL_ACTION_DANCE4;
		}
	}

	if (HasHandsUp()) {
		return SPECIAL_ACTION_HANDSUP;
	}

	if (IsCellphoneEnabled()) {
		return SPECIAL_ACTION_USECELLPHONE;
	}

	if (IsPissing()) {
		return SPECIAL_ACTION_PISSING;
	}

	if (GetStuff() == STUFF_TYPE_BEER) {
		return SPECIAL_ACTION_DRINK_BEER;
	}

	if (GetStuff() == STUFF_TYPE_DYN_BEER) {
		return SPECIAL_ACTION_DRINK_WINE;
	}

	if (GetStuff() == STUFF_TYPE_PINT_GLASS) {
		return SPECIAL_ACTION_DRINK_SPRUNK;
	}

	if (GetStuff() == STUFF_TYPE_CIGGI) {
		return SPECIAL_ACTION_SMOKE_CIGGY;
	}

	if (IsSitTask()) {
		return SPECIAL_ACTION_SITTING;
	}

	if (IsCrouching()) {
		return SPECIAL_ACTION_DUCK;
	}

	return SPECIAL_ACTION_NONE;
}

static char DanceStyleLibs[4][16] = {"WOP", "GFUNK", "RUNNINGMAN", "STRIP"};
char DanceIdleLoops[4][16] = {"DANCE_LOOP", "DANCE_LOOP", "DANCE_LOOP", "STR_Loop_B"};

static char szDanceAnimNamesFemale[16][16] = {
		"DANCE_G1", "DANCE_G2", "DANCE_G3", "DANCE_G4",
		"DANCE_G5", "DANCE_G6", "DANCE_G7", "DANCE_G8",
		"DANCE_G9", "DANCE_G10", "DANCE_G11", "DANCE_G12",
		"DANCE_G13", "DANCE_G14", "DANCE_G15", "DANCE_G16"};

static char szDanceAnimNamesMale[16][16] = {
		"DANCE_B1", "DANCE_B2", "DANCE_B3", "DANCE_B4",
		"DANCE_B5", "DANCE_B6", "DANCE_B7", "DANCE_B8",
		"DANCE_B9", "DANCE_B10", "DANCE_B11", "DANCE_B12",
		"DANCE_B13", "DANCE_B14", "DANCE_B15", "DANCE_B16"};

static char szStripAnims[16][16] = {
		"strip_A", "strip_B", "strip_C", "strip_D",
		"strip_E", "strip_F", "strip_G", "STR_A2B",
		"STR_B2A", "STR_B2C", "STR_C1", "STR_C2",
		"STR_C2B", "STR_A2B", "STR_B2C", "STR_C2"};

bool CPlayerPed::IsDancing()
{
	if (m_iDanceState) {
		return true;
	}
	return false;
}

void CPlayerPed::StopDancing()
{
	if (!m_ped || !IsAdded()) {
		m_iDanceState = 0;
		m_iDanceStyle = -1;
		m_iLastDanceMove = -1;
		return;
	}

	m_iDanceState = 0;
	m_iDanceStyle = -1;
	m_iLastDanceMove = -1;
	RwMatrix mat = m_ped->GetMatrix().ToRwMatrix();
	TeleportTo(mat.pos.x, mat.pos.y, mat.pos.z);
}

void CPlayerPed::ProcessDancing()
{
	if (!m_ped || !IsAdded() || IsInVehicle()) {
		return;
	}

	if (!m_iDanceState || m_iDanceStyle < 0 || m_iDanceStyle > 3) return;
	if (m_bytePlayerNumber != 0) return;

	float fDummyBlend = 4.0f;
	if (GetCurrentAnimationIndex(fDummyBlend) == 0) {
		m_iLastDanceMove = -1;
		ApplyAnimation(DanceIdleLoops[m_iDanceStyle], DanceStyleLibs[m_iDanceStyle], 4.0f, 1, 0, 0, 0, 0);
	}

	const int16_t sUD = static_cast<int16_t>(LocalPlayerKeys.wKeyUD);
	const int16_t sLR = static_cast<int16_t>(LocalPlayerKeys.wKeyLR);
	const bool bExtra = LocalPlayerKeys.bKeys[ePadKeys::KEY_SPRINT];

	int iNewMove = -1;
	if (sUD < 0) {
		iNewMove = (sLR < 0) ? 4 : (sLR > 0) ? 5 : 0; // UP / UP-LEFT / UP-RIGHT
	}
	else if (sUD > 0) {
		iNewMove = (sLR < 0) ? 6 : (sLR > 0) ? 7 : 1; // DOWN / DOWN-LEFT / DOWN-RIGHT
	}
	else if (sLR < 0) {
		iNewMove = 2; // LEFT
	}
	else if (sLR > 0) {
		iNewMove = 3; // RIGHT
	}
	else {
		return;
	}

	if (bExtra) {
		iNewMove += 8;
	}

	if (iNewMove == m_iLastDanceMove) return; // don't allow the same move twice
	m_iLastDanceMove = iNewMove;

	char* szAnimName = GetDanceAnimForMove(iNewMove);
	if (!szAnimName || !szAnimName[0]) return;

	ApplyAnimation(szAnimName, DanceStyleLibs[m_iDanceStyle], 4.0f, 0, 0, 0, 0, 0);
}

char* CPlayerPed::GetDanceAnimForMove(int iMove)
{
	if (m_iDanceStyle == -1) return ""; // shouldn't ever happen but..

	// style 0-2 have the same anim names, but M/F versions
	if (m_iDanceStyle >= 0 && m_iDanceStyle <= 2) {
		if (GetPedStat() == 5 || GetPedStat() == 22) { // female or pro (only female types)
			return szDanceAnimNamesFemale[iMove];
		}

		return szDanceAnimNamesMale[iMove];
	}
	else if (m_iDanceStyle == 3) {
		return szStripAnims[iMove];
	}

	return "";
}

void CPlayerPed::ToggleCellphone(bool on)
{
	if (!on) {
		m_bCellPhoneEnabled = false;
		if (m_ped && GamePool_Ped_GetAt(m_gtaId) && IsAdded()) {
			ScriptCommand(&toggle_actor_cellphone, m_gtaId, 0);
		}
		return;
	}
	if (!m_ped || !GamePool_Ped_GetAt(m_gtaId) || !IsAdded()) {
		return;
	}

	m_bCellPhoneEnabled = true;
	ScriptCommand(&toggle_actor_cellphone, m_gtaId, 1);
}

void CPlayerPed::ApplyCrouch()
{
	if (!m_ped || !GamePool_Ped_GetAt(m_gtaId) || !m_ped->m_pIntelligence) {
		return;
	}

	spdlog::debug("Apply crouch");

	const uintptr_t taskDuck = GetPedTaskDuck(m_ped);
	if (!IsCrouching() || IsDuckTaskAborting(taskDuck)) {
		eng::fn::CPedIntelligence_SetTaskDuckSecondary(m_ped->m_pIntelligence, 0);
	}
}

void CPlayerPed::ResetCrouch()
{
	if (!m_ped || !GamePool_Ped_GetAt(m_gtaId)) {
		return;
	}

	spdlog::debug("Reset crouch");

	const uintptr_t taskDuck = GetPedTaskDuck(m_ped);
	if (!taskDuck) {
		// No duck task left: clear any orphaned bIsDucking flag directly.
		m_ped->m_nPedFlags.bIsDucking = false;
		return;
	}

	if (IsDuckTaskRolling(taskDuck)) {
		// CTaskSimpleDuck::MakeAbortable rejects priorities 0 and 1 during
		// CROUCH_ROLL_L/R (0x39/0x3b), which would leave WEAPON_CROUCH stuck
		// while bIsDucking is cleared. Priority 2 (ABORT_PRIORITY_IMMEDIATE)
		// blends IDLE_STANCE (1000.0f), clears callbacks, and clears bIsDucking.
		eng::fn::CTaskSimpleDuck_MakeAbortable(taskDuck, m_ped, 2, nullptr);
		return;
	}

	if (!IsDuckTaskAborting(taskDuck)) {
		// Let ClearTaskDuckSecondary (MakeAbortable priority 0) smoothly blend
		// WEAPON_CROUCH -> IDLE_STANCE and clear bIsDucking on completion, rather
		// than prematurely clearing bIsDucking while WEAPON_CROUCH is still active.
		eng::fn::CPedIntelligence_ClearTaskDuckSecondary(m_ped->m_pIntelligence);
	}
}

bool CPlayerPed::IsCrouching()
{
	if (!m_ped || !GamePool_Ped_GetAt(m_gtaId)) {
		return false;
	}

	return m_ped->bIsDucking;
}

void CPlayerPed::HandsUp()
{
	if (!m_ped || !GamePool_Ped_GetAt(m_gtaId) || IsInVehicle() || !IsAdded()) {
		return;
	}

	ScriptCommand(&actor_task_handsup, m_gtaId, -2);
}

void CPlayerPed::GiveStuff(int iType)
{
	if (!m_ped || !GamePool_Ped_GetAt(m_gtaId) || !IsAdded() || IsInVehicle()) {
		return;
	}

	if (m_dwStuffObject) {
		DropStuff();
	}

	int iModel = 0;
	double x = 0.0, y = 0.0, z = 0.0;
	switch (iType) {
		case STUFF_TYPE_BEER:
			iModel = 1543;
			x = 0.05; y = 0.03; z = -0.30;
			break;
		case STUFF_TYPE_DYN_BEER:
			iModel = 1486;
			x = 0.05; y = 0.03; z = -0.05;
			break;
		case STUFF_TYPE_PINT_GLASS:
			iModel = 1546;
			x = 0.04; y = 0.10; z = -0.02;
			break;
		case STUFF_TYPE_CIGGI:
			iModel = 1485;
			x = 0.0; y = 0.0; z = 0.0;
			break;
		default:
			return;
	}

	if (pGame && !pGame->IsModelLoaded(iModel)) {
		pGame->RequestModel(iModel);
		pGame->LoadRequestedModels();
	}

	RwMatrix mat = m_ped->GetMatrix().ToRwMatrix();
	uint32_t dwObj = 0;
	ScriptCommand(&create_object, iModel, mat.pos.x, mat.pos.y, mat.pos.z, &dwObj);
	if (!dwObj || !GamePool_Object_GetAt(dwObj)) {
		m_dwStuffObject = 0;
		return;
	}

	m_dwStuffObject = dwObj;
	ScriptCommand(&task_pick_up_object, m_gtaId, m_dwStuffObject, x, y, z, 6, 16, "NULL", "NULL", -1);
	m_iStuffType = iType;
	SetArmedWeapon(0);
}

void CPlayerPed::DropStuff()
{
	if (m_ped && GamePool_Ped_GetAt(m_gtaId) && m_ped->m_pIntelligence) {
		// Detach & delete CTaskSimpleHoldEntity in secondary slot 4 BEFORE freeing the object pool slot
		// so m_aSecondaryTasks[4] never retains a dangling CObject* across pool slot reuse.
		eng::fn::CTaskManager_SetTaskSecondary(&m_ped->m_pIntelligence->m_TaskMgr, nullptr, 4);
	}

	if (m_dwStuffObject) {
		if (GamePool_Object_GetAt(m_dwStuffObject)) {
			ScriptCommand(&destroy_object, m_dwStuffObject);
		}
		m_dwStuffObject = 0;
	}

	m_iStuffType = STUFF_TYPE_NONE;
}

bool CPlayerPed::ApplyStuff()
{
	if (m_iStuffType == STUFF_TYPE_NONE || !m_ped || !GamePool_Ped_GetAt(m_gtaId) || !IsAdded() || IsInVehicle()) {
		return false;
	}

	SetArmedWeapon(0);
	float fDummyBlend = 4.0f;
	if (GetCurrentAnimationIndex(fDummyBlend) != 0) {
		return false;
	}

	if (m_iStuffType == STUFF_TYPE_CIGGI) {
		ApplyAnimation("smkcig_prtl", "GANGS", 700.0f, 0, 0, 0, 0, 2750);
		return true;
	}

	if (m_iStuffType == STUFF_TYPE_BEER || m_iStuffType == STUFF_TYPE_DYN_BEER || m_iStuffType == STUFF_TYPE_PINT_GLASS) {
		if (GetPedStat() == 5 || GetPedStat() == 22) {
			ApplyAnimation("dnk_stndF_loop", "BAR", 4.0f, 0, 0, 0, 0, -1);
		}
		else {
			ApplyAnimation("dnk_stndM_loop", "BAR", 4.0f, 0, 0, 0, 0, -1);
		}
		if (m_bytePlayerNumber == 0 && m_iStuffType != STUFF_TYPE_PINT_GLASS) {
			SetDrunkLevel(GetDrunkLevel() + 1250);
		}
		return true;
	}

	return false;
}

int CPlayerPed::HasHandsUp()
{
	if (!m_ped || !m_ped->m_pIntelligence || !IsAdded()) {
		return false;
	}

	if (m_ped->m_pIntelligence->m_TaskMgr.m_aPrimaryTasks[sa::TASK_PRIMARY_PRIMARY] == nullptr) {
		return false;
	}

	uintptr_t vtable = *(uintptr_t*) m_ped->m_pIntelligence->m_TaskMgr.m_aPrimaryTasks[sa::TASK_PRIMARY_PRIMARY];
	if (vtable == (uintptr_t) g_saSym->Abs(addr::vptr$CTaskSimpleHandsUp)) {
		return true;
	}

	return false;
}

void CPlayerPed::StartJetpack()
{
	if (!m_ped || m_ped->IsInVehicle()) {
		return;
	}

	if (!GamePool_Ped_GetAt(m_gtaId)) {
		return;
	}

	ScriptCommand(&actor_task_jetpack, m_gtaId);
}

void CPlayerPed::StopJetpack()
{
	if (!m_ped || !m_ped->m_pIntelligence || m_ped->IsInVehicle()) {
		return;
	}

	if (!GamePool_Ped_GetAt(m_gtaId)) {
		return;
	}

	if (m_ped->m_pIntelligence->m_TaskMgr.m_aPrimaryTasks[sa::TASK_PRIMARY_PRIMARY] == nullptr) {
		return;
	}

	if (IsInJetpack()) {
		sa::CTask* dwJetPackTask = m_ped->m_pIntelligence->m_TaskMgr.m_aPrimaryTasks[sa::TASK_PRIMARY_PRIMARY];
		// CTaskSimpleJetPack::~CTaskSimpleJetPack(CTaskSimpleJetPack *this)
		eng::fn::CTaskSimpleJetPack_Dtor_CTaskSimpleJetPack( dwJetPackTask);
		m_ped->m_pIntelligence->m_TaskMgr.m_aPrimaryTasks[sa::TASK_PRIMARY_PRIMARY] = nullptr;
	}
}

bool CPlayerPed::IsInJetpack()
{
	if (!m_ped || !m_ped->m_pIntelligence || m_ped->IsInVehicle()) {
		return false;
	}

	if (m_ped->m_pIntelligence->m_TaskMgr.m_aPrimaryTasks[sa::TASK_PRIMARY_PRIMARY] == nullptr) {
		return false;
	}

	uintptr_t vtable = *(uintptr_t*) m_ped->m_pIntelligence->m_TaskMgr.m_aPrimaryTasks[sa::TASK_PRIMARY_PRIMARY];
	if (vtable == (uintptr_t) g_saSym->Abs(addr::vptr$CTaskSimpleJetPack)) {
		return true;
	}

	return false;
}

void CPlayerPed::ToggleCuffed(bool cuff)
{
	if (!cuff) {
		m_bCuffedState = false;
		return;
	}
	if (!m_ped || !GamePool_Ped_GetAt(m_gtaId) || !IsAdded()) {
		return;
	}

	SetArmedWeapon(0);
	m_bCuffedState = true;
}

void CPlayerPed::ToggleCarry(bool carry)
{
	if (!carry) {
		m_bCarryState = false;
		if (m_ped && GamePool_Ped_GetAt(m_gtaId) && IsAdded() && !IsInVehicle()) {
			ApplyAnimation("CRRY_PRTIAL", "CARRY", 4.00, 0, 0, 0, 0, 1);
		}
		return;
	}
	if (!m_ped || !GamePool_Ped_GetAt(m_gtaId) || !IsAdded()) {
		return;
	}

	m_bCarryState = true;
	ApplyAnimation("CRRY_PRTIAL", "CARRY", 4.1, 0, 0, 0, 1, 1);
}

void CPlayerPed::TogglePissing(bool piss)
{
	if (!piss) {
		if (m_dwPissParticlesHandle) {
			ScriptCommand(&destroy_particle, m_dwPissParticlesHandle);
			m_dwPissParticlesHandle = 0;
		}
		const bool wasPissing = m_bPissingState;
		m_bPissingState = false;
		if (wasPissing && m_ped && GamePool_Ped_GetAt(m_gtaId) && IsAdded() && !IsInVehicle()) {
			RwMatrix mat = m_ped->GetMatrix().ToRwMatrix();
			TeleportTo(mat.pos.x, mat.pos.y, mat.pos.z);
		}
		return;
	}

	if (!m_ped || !GamePool_Ped_GetAt(m_gtaId) || IsInVehicle() || !IsAdded()) {
		return;
	}

	if (m_bPissingState) {
		return;
	}

	ApplyAnimation("PISS_LOOP", "PAULNMAC", 4.0f, 1, 0, 0, 0, -1);

	ScriptCommand(&attach_particle_to_actor2, "PETROLCAN", m_gtaId, 0.0f, 0.58f, -0.08f, 0.0f, 0.01f, 0.0f, 1, &m_dwPissParticlesHandle);
	ScriptCommand(&make_particle_visible, m_dwPissParticlesHandle);

	m_bPissingState = true;
}

void CPlayerPed::StartDancing(int danceId)
{
	if (!m_ped || IsInVehicle() || !IsAdded()) {
		return;
	}

	if (danceId < 0 || danceId > 3) return;

	m_iDanceState = 1;
	m_iDanceStyle = danceId;

	ApplyAnimation(DanceIdleLoops[m_iDanceStyle], DanceStyleLibs[m_iDanceStyle], 16.0, 1, 0, 0, 0, -1);
}

void CPlayerPed::SetDrunkLevel(int32_t iLevel)
{
	if (iLevel > 50000) {
		iLevel = 50000;
	}
	else if (iLevel < 0) {
		iLevel = 0;
	}

	m_iDrunkLevel = iLevel;

	// Clear visuals/handling immediately when dropping to <= 2000 (including 0)
	// so SetPlayerDrunkLevel(playerid, 0) never leaves permanent camera sway.
	if (!m_bytePlayerNumber && m_ped) {
		if (m_iDrunkLevel <= 2000 && m_bDrunkVisualsActive) {
			ScriptCommand(&set_player_drunk_visuals, m_bytePlayerNumber, 0);
			ScriptCommand(&set_player_drunk_handling, m_bytePlayerNumber, 0);
			m_bDrunkVisualsActive = false;
		}
		if (m_iDrunkLevel <= 5000 && m_bDrunkHudHidden) {
			if (pGame) {
				pGame->SetHudHidden(Game::HUD_HIDE_DRUNK, false);
			}
			m_bDrunkHudHidden = false;
		}
	}
}

void CPlayerPed::DrunkProcess()
{
	if (!m_ped || m_bytePlayerNumber != 0) {
		return;
	}

	int32_t iDrunkLevel = GetDrunkLevel();
	if (iDrunkLevel <= 0) {
		if (m_bDrunkVisualsActive) {
			ScriptCommand(&set_player_drunk_visuals, m_bytePlayerNumber, 0);
			ScriptCommand(&set_player_drunk_handling, m_bytePlayerNumber, 0);
			m_bDrunkVisualsActive = false;
		}
		if (m_bDrunkHudHidden) {
			if (pGame) {
				pGame->SetHudHidden(Game::HUD_HIDE_DRUNK, false);
			}
			m_bDrunkHudHidden = false;
		}
		return;
	}

	// Hide HUD/radar when drunk level > 5000 (SA-MP PC & docs contract)
	if (iDrunkLevel > 5000) {
		if (!m_bDrunkHudHidden && pGame) {
			pGame->SetHudHidden(Game::HUD_HIDE_DRUNK, true);
			m_bDrunkHudHidden = true;
		}
	}
	else if (m_bDrunkHudHidden) {
		if (pGame) {
			pGame->SetHudHidden(Game::HUD_HIDE_DRUNK, false);
		}
		m_bDrunkHudHidden = false;
	}

	if (iDrunkLevel <= 2000) {
		m_iDrunkLevel = iDrunkLevel - 1;
		if (m_bDrunkVisualsActive) {
			ScriptCommand(&set_player_drunk_visuals, m_bytePlayerNumber, 0);
			ScriptCommand(&set_player_drunk_handling, m_bytePlayerNumber, 0);
			m_bDrunkVisualsActive = false;
		}
	}
	else {
		int iDrunkVisual = static_cast<int>(iDrunkLevel * 0.02f);
		if (iDrunkVisual <= 250) {
			if (iDrunkVisual < 5) {
				iDrunkVisual = 0;
			}
		}
		else {
			iDrunkVisual = 250;
		}

		m_iDrunkLevel = iDrunkLevel - 1;
		ScriptCommand(&set_player_drunk_visuals, m_bytePlayerNumber, iDrunkVisual);
		m_bDrunkVisualsActive = (iDrunkVisual > 0);

		if (IsInVehicle() && !IsAPassenger()) {
			sa::CVehicle* _pVehicle = GetGtaVehicle();
			if (_pVehicle) {
				if (!m_dwDrunkLastUpdateTick || (GetTickCount() - m_dwDrunkLastUpdateTick) > 200) {
					int iRandNumber = rand() % 40;
					float fRotation = 0.0f;
					if (iRandNumber >= 20) {
						fRotation = (iDrunkLevel >= 5000) ? 0.015f : 0.012f;
						if (iRandNumber <= 30) {
							fRotation = -fRotation;
						}
					}

					if (FloatOffset(_pVehicle->m_vecMoveSpeed.x, 0.0) > 0.050000001f ||
							FloatOffset(_pVehicle->m_vecMoveSpeed.y, 0.0) > 0.050000001f) {
						_pVehicle->m_vecTurnSpeed.z = fRotation + _pVehicle->m_vecTurnSpeed.z;
					}

					m_dwDrunkLastUpdateTick = GetTickCount();
				}
			}
		}
	}
}

void CPlayerPed::ProcessParachuteSkydiving()
{
	int16_t udAnalog = 0;
	if (m_bytePlayerNumber != 0 && m_bytePlayerNumber < PLAYER_PED_SLOTS) {
		udAnalog = static_cast<int16_t>(RemotePlayerKeys[m_bytePlayerNumber].wKeyUD);
	}
	else {
		uint16_t lrRaw = 0, udRaw = 0;
		uint8_t exKeys = 0;
		GetKeys(&lrRaw, &udRaw, &exKeys, false);
		udAnalog = static_cast<int16_t>(udRaw);
	}
	RwMatrix mat;

	if ((udAnalog > 0) && (m_iParachuteAnim != FALL_SKYDIVE_ACCEL)) {
		ApplyAnimation("FALL_SKYDIVE_ACCEL", "PARACHUTE", 1.0f, 1, 0, 0, 1, -2);
		m_iParachuteAnim = FALL_SKYDIVE_ACCEL;
	}
	else if ((udAnalog <= 0) && (m_iParachuteAnim != FALL_SKYDIVE)) {
		ApplyAnimation("FALL_SKYDIVE", "PARACHUTE", 1.0f, 1, 0, 0, 1, -2);
		m_iParachuteAnim = FALL_SKYDIVE;
	}

	// if the parachute object isn't created, do it now.
	if (!m_dwParachuteObject) {
		spdlog::debug("Creating parachute object");
		mat = m_ped->GetMatrix().ToRwMatrix();
		ScriptCommand(&create_object, OBJECT_PARACHUTE, mat.pos.x, mat.pos.y, mat.pos.z, &m_dwParachuteObject);

		if (!GamePool_Object_GetAt(m_dwParachuteObject)) {
			spdlog::info("Parachute object creation failed");
			m_dwParachuteObject = 0;
			return;
		}

		ScriptCommand(&attach_object_to_actor, m_dwParachuteObject, m_gtaId, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
		ScriptCommand(&set_object_visible, m_dwParachuteObject, 0);
	}

	if (!GamePool_Object_GetAt(m_dwParachuteObject)) {
		spdlog::info("Parachute object creation failed");
		m_dwParachuteObject = 0;
		return;
	}

	// process parachute opening event
	if (m_bytePlayerNumber < PLAYER_PED_SLOTS && RemotePlayerKeys[m_bytePlayerNumber].bKeys[ePadKeys::KEY_FIRE]) {
		ApplyAnimation("PARA_OPEN", "PARACHUTE", 8.0f, 0, 0, 0, 1, -2);
		ScriptCommand(&apply_object_animation, m_dwParachuteObject, "PARA_OPEN_O", "PARACHUTE", 1000.0f, 0, 1);
		ScriptCommand(&set_object_visible, m_dwParachuteObject, 1);
		ScriptCommand(&set_object_scale, m_dwParachuteObject, 1.0f);
		m_iParachuteState = 2;
		m_iParachuteAnim = 0;
	}
}

void CPlayerPed::ProcessParachutes()
{
	if (!m_ped || !GamePool_Ped_GetAt(m_gtaId)) {
		return;
	}

	if (!IsAdded()) {
		return;
	}

	if (m_iParachuteState == 0) {
		if (m_dwParachuteObject) {
			ScriptCommand(&disassociate_object, m_dwParachuteObject, 0.0f, 0.0f, 0.0f, 0);
			ScriptCommand(&destroy_object_with_fade, m_dwParachuteObject);
			m_dwParachuteObject = 0;
		}

		// See if we should enter the initial parachuting state.
		if (GetCurrentWeapon() == WEAPON_PARACHUTE) // armed with para
		{
			if (ScriptCommand(&is_actor_falling_think, m_gtaId)) // is falling
			{
				float fDistanceFromGround = 0.0f;
				ScriptCommand(&get_actor_distance_from_ground, m_gtaId, &fDistanceFromGround);
				if (fDistanceFromGround > 20.0f) {
					// ScriptCommand(&actor_set_collision, m_dwGTAId, 0);
					m_iParachuteState = 1;
					m_iParachuteAnim = 0;
				}
			}
		}
		return;
	}

	if ((GetCurrentWeapon() != WEAPON_PARACHUTE) || ScriptCommand(&is_actor_in_the_water, m_gtaId)) {
		// A parachuting state is active, but they no longer have the parachute
		// or they've ended up in the water.
		if (m_dwParachuteObject) {
			ScriptCommand(&disassociate_object, m_dwParachuteObject, 0.0f, 0.0f, 0.0f, 0);
			ScriptCommand(&destroy_object_with_fade, m_dwParachuteObject);
			// ScriptCommand(&actor_set_collision, m_dwGTAId, 1);
			RwMatrix mat = m_ped->GetMatrix().ToRwMatrix();
			TeleportTo(mat.pos.x, mat.pos.y, mat.pos.z);
			m_dwParachuteObject = 0;
		}

		m_iParachuteState = 0;
		m_iParachuteAnim = 0;
	}

	if (m_iParachuteState == 1) {
		ProcessParachuteSkydiving();
		return;
	}

	if (m_iParachuteState == 2) {
		ProcessParachuting();
		return;
	}
}

void CPlayerPed::ProcessParachuting()
{
	int16_t udAnalog = 0;
	if (m_bytePlayerNumber != 0 && m_bytePlayerNumber < PLAYER_PED_SLOTS) {
		udAnalog = static_cast<int16_t>(RemotePlayerKeys[m_bytePlayerNumber].wKeyUD);
	}
	else {
		uint16_t lrRaw = 0, udRaw = 0;
		uint8_t exKeys = 0;
		GetKeys(&lrRaw, &udRaw, &exKeys, false);
		udAnalog = static_cast<int16_t>(udRaw);
	}

	const bool hasParaObj = (m_dwParachuteObject != 0 && GamePool_Object_GetAt(m_dwParachuteObject) != nullptr);
	if ((udAnalog > 0) && (m_iParachuteAnim != PARA_DECEL)) {
		ApplyAnimation("PARA_DECEL", "PARACHUTE", 1.0f, 1, 0, 0, 1, -2);
		if (hasParaObj) {
			ScriptCommand(&apply_object_animation, m_dwParachuteObject, "PARA_DECEL_O", "PARACHUTE", 1.0f, 1, 1);
		}
		m_iParachuteAnim = PARA_DECEL;
	}
	else if ((udAnalog <= 0) && (m_iParachuteAnim != PARA_FLOAT)) {
		ApplyAnimation("PARA_FLOAT", "PARACHUTE", 1.0f, 1, 0, 0, 1, -2);
		if (hasParaObj) {
			ScriptCommand(&apply_object_animation, m_dwParachuteObject, "PARA_FLOAT_O", "PARACHUTE", 1.0f, 1, 1);
		}
		m_iParachuteAnim = PARA_FLOAT;
	}
}

void CPlayerPed::CheckVehicleParachute()
{
	if (m_dwParachuteObject) {
		ScriptCommand(&disassociate_object, m_dwParachuteObject, 0.0, 0.0, 0.0, 0);
		ScriptCommand(&destroy_object, m_dwParachuteObject);
		m_dwParachuteObject = 0;
	}
}

// todo

sa::CAnimBlendClumpData** GetClumpData(RpClump* clump)
{
	RwInt32 ClumpOffset = *eng::as<RwInt32*>(eng::Sym::ClumpOffset);
	return RWPLUGINOFFSET(sa::CAnimBlendClumpData*, clump, ClumpOffset);
}

void UpdateElementRpHAnim(sa::CEntity* ent)
{
	if (ent) {
		// CEntity::UpdateRpHAnim
		eng::fn::CEntity_UpdateRpHAnim( ent);

		if (ent->m_nModelIndex == 0) { // CJ skin
			RpClump* clump = ent->m_pRpClump;
			if (clump) {
				// CPed::ShoulderBoneRotation
				eng::fn::CPed_ShoulderBoneRotation( clump);
			}
			else {
				spdlog::error("UpdateElementRpHAnim: clump is null");
			}
		}
	}
	else {
		spdlog::error("UpdateElementRpHAnim: ent is null");
	}
}

void CPlayerPed::ProcessCuff()
{
	if (!m_ped || !m_ped->m_pRpClump || !IsCuffed()) {
		return;
	}

	sa::CAnimBlendClumpData** ppClumpData = GetClumpData(m_ped->m_pRpClump);
	if (!ppClumpData || !*ppClumpData) {
		return;
	}
	sa::CAnimBlendClumpData* clumpData = *ppClumpData;

	int boneIds[] = {BONE_L_UPPER_ARM, BONE_L_FORE_ARM, BONE_L_HAND, BONE_R_UPPER_ARM, BONE_R_FORE_ARM, BONE_R_HAND};

	// yaw pitch roll
	float rotations[][3] = {
			{26.57374382019, 61.337575733622, 59.206573486328},
			{27.843754291534, 15.3639249801636, 46.40625},
			{-81.018516340527, 342.87482380867, 326.11833715439},
			{338.839179039, 53.49357098341, 298.45233917236},
			{307.68748283386, 22.110015869141, 313.59375},
			{96.047592163086, 357.88313293457, 56.739406585693}

	};

	for (int i = 0; i < 6; i++) {
		// updating the bone frame orientation will also update its children
		// This rotation is only applied when UpdateElementRpHAnim is called
		sa::AnimBlendFrameData* frameData = clumpData->GetFrameDataByNodeId(boneIds[i]);
		if (frameData) {
			RtQuat* boneOrientation = &frameData->GetFrameOrientation();
			RwV3d angles = {rotations[i][0], rotations[i][2], rotations[i][1]};
			// BoneNode_c::EulerToQuat(RwV3d_0 *euler, RtQuat_0 *quat)
			eng::fn::BoneNode_c_EulerToQuat( &angles, boneOrientation);
			m_ped->m_bDontUpdateHierarchy = false;
		}
	}

	UpdateElementRpHAnim(m_ped);
}
