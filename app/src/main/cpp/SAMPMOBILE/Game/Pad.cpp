#include "Pad.h"
#include "Patches.h"
#include "Game.h"
#include "Common.h"
#include "Hooks.h"
#include "PadInternal.h"
#include "PlayerPed.h"
#include "Scripting.h"
#include "TouchCombat.h"
#include "Util.h"
#include "Vehicle.h"
#include "VehicleControl.h"
#include "BulletSync.h"
#include "Damage.h"
#include "Hooks/HookDomains.h"
#include "PlayerContextScope.h"
#include "Engine/PatchRegistry.h"
#include "Engine/Symbols.h"
#include "../Addr.h"
#include "../UI/UI.h"
#include "../UI/SAMPWidgets/ButtonPanel.h"
#include "../Net/NetGame.h"
#include "../Net/LocalPlayer.h"
#include "../Net/RemotePlayer.h"
#include "../Net/VehiclePool.h"
#include "../Client.h"
#include "Enums/WidgetIDs.h"
#include <Memory.h>
#include <SA.h>
#include <spdlog/spdlog.h>
#include <atomic>


PAD_KEYS LocalPlayerKeys;
PAD_KEYS RemotePlayerKeys[PLAYER_PED_SLOTS];

sa::CPed* dwCurPlayerActor = nullptr;
uint8_t byteInternalPlayer = 0;
uint8_t byteCurPlayer = 0;
uintptr_t g_lastLocalPadPtr = 0;
bool g_bCancelCarTask = false;

bool IsPadMovementDisabled(uintptr_t pad)
{
	// Disassembly-verified on both arm32 (0x3fa220..0x3fa226) and arm64 (0x4dcd50..0x4dcd58):
	// CPad::DisablePlayerControls @ +272 (uint16_t), CPad::bDisablePlayerDuck/Move @ +287 (uint8_t).
	return !pad
		|| *reinterpret_cast<const uint16_t*>(pad + 272) != 0
		|| *reinterpret_cast<const uint8_t*>(pad + 287) != 0;
}

bool IsLocalActivePedMovementLocked()
{
	if (!g_lastLocalPadPtr || !IsPadMovementDisabled(g_lastLocalPadPtr)) {
		return false;
	}
	if (pNetGame && pNetGame->GetPlayerPool()) {
		if (CLocalPlayer* pLocal = pNetGame->GetPlayerPool()->GetLocalPlayer()) {
			// Keep ButtonPanel directional keys working during inactive class-selection / spectating,
			// where servers read lrAnalog/udAnalog while the local ped is intentionally frozen.
			if (!pLocal->m_bIsActive || pLocal->IsSpectating()) {
				return false;
			}
		}
	}
	return true;
}

// CTaskSimplePlayerOnFoot::m_nTimer offset (2000ms idle fighter timeout)
static constexpr uintptr_t kTaskPlayerOnFootTimerOffset = VER_x32 ? 40 : 56;
// Disassembly-verified CTaskSimpleUseGun / CTaskSimpleDuck offsets (arm32 / arm64)
static constexpr uintptr_t kTaskUseGunMoveCmdOff        = VER_x32 ? 0x14 : 0x1C;
static constexpr uintptr_t kTaskUseGunWeaponInfoOff     = VER_x32 ? 0x30 : 0x48;
static constexpr uintptr_t kTaskDuckMoveAnimOff         = VER_x32 ? 0x14 : 0x20;
static constexpr uintptr_t kAnimAssocAnimIdOff          = VER_x32 ? 0x2C : 0x44;

static std::atomic<bool> g_suppressPedUpdatePosition{false};
static std::atomic<bool> g_bHardwareCrouchEdge{false};
static std::atomic<bool> g_bHardwareCrouchHeld{false};
static bool g_suppressLocalDuckStandUp = false;

void NotifyLocalHardwareCrouchKey(bool isDown)
{
	const bool wasDown = g_bHardwareCrouchHeld.exchange(isDown, std::memory_order_acq_rel);
	if (isDown && !wasDown) {
		g_bHardwareCrouchEdge.store(true, std::memory_order_release);
	}
}

static bool IsDuckTaskRolling(uintptr_t taskDuck)
{
	if (!taskDuck) return false;
	const uintptr_t moveAnim = *reinterpret_cast<const uintptr_t*>(taskDuck + kTaskDuckMoveAnimOff);
	if (!moveAnim) return false;
	const int16_t animId = *reinterpret_cast<const int16_t*>(moveAnim + kAnimAssocAnimIdOff);
	return (animId | 2) == 0x3B; // CROUCH_ROLL_L (0x39) or CROUCH_ROLL_R (0x3B)
}

struct ScopedAtomicBool {
	std::atomic<bool>& flag;
	bool prev;
	ScopedAtomicBool(std::atomic<bool>& f, bool next)
		: flag(f), prev(f.exchange(next, std::memory_order_acq_rel)) {}
	~ScopedAtomicBool() { flag.store(prev, std::memory_order_release); }
};

DECL_HOOK(void, CPed_UpdatePosition_fromProcessControl, sa::CPed* _this)
{
	if (g_suppressPedUpdatePosition.load(std::memory_order_acquire) && _this == dwCurPlayerActor) {
		return;
	}
	CPed_UpdatePosition_fromProcessControl(_this);
}

DECL_HOOK(void, CTaskSimplePlayerOnFoot_PlayerControlZelda, void* _this, sa::CPed* pPed, bool bUnk)
{
	// Allow local player and non-player actors to update m_fAimingRotation from camera;
	// suppress camera-driven rotation for remote players without rewriting .text or global flags.
	if (!pPed || pPed == GamePool_FindPlayerPed() || FindPlayerNumFromPedPtr(pPed) == 0) {
		CTaskSimplePlayerOnFoot_PlayerControlZelda(_this, pPed, bUnk);
		return;
	}
	const float savedAimRot = pPed->m_fAimingRotation;
	CTaskSimplePlayerOnFoot_PlayerControlZelda(_this, pPed, bUnk);
	pPed->m_fAimingRotation = savedAimRot;
}

DECL_HOOK(void, CPed_ProcessControl, sa::CPed* _this)
{
	sa::CPed* prevCurActor = dwCurPlayerActor;
	uint8_t prevCurPlayer = byteCurPlayer;

	dwCurPlayerActor = _this;
	byteInternalPlayer = CurrentPlayerSlot();
	uint8_t curPlayer = FindPlayerNumFromPedPtr(_this);

	if (dwCurPlayerActor && (curPlayer != 0) && byteInternalPlayer == 0) {
		byteCurPlayer = curPlayer;
		// CPad::GetExitTargeting is only called inside local CCamera::CamControl and
		// never runs for remote peds. Clear stale remote targeting state as soon as
		// both KEY_HANDBRAKE and KEY_FIRE are released.
		if (curPlayer < PLAYER_PED_SLOTS
				&& !RemotePlayerKeys[curPlayer].bKeys[ePadKeys::KEY_HANDBRAKE]
				&& !RemotePlayerKeys[curPlayer].bKeys[ePadKeys::KEY_FIRE]) {
			if (_this->m_pPlayerData && _this->m_pPlayerData->m_bFreeAiming) {
				_this->m_pPlayerData->m_bFreeAiming = false;
			}
			if (_this->m_pEntLockOnTarget) {
				eng::fn::CPlayerPed_ClearWeaponTarget(_this);
			}
		}
		{
			ScopedPlayerContext ctx(curPlayer, ScopedPlayerContext::kFull);

			// Suppress CPed::UpdatePosition (via hookBL) and CWidget::SetEnabled (via hook)
			// for remote peds without rewriting .text every frame (nesting-safe RAII guards).
			ScopedAtomicBool posGuard(g_suppressPedUpdatePosition, true);
			ScopedAtomicBool widgetGuard(g_suppressWidgetSetEnabled, true);

			CPed_ProcessControl(_this);
		}
	}
	else {
		CPed_ProcessControl(_this);
	}

	dwCurPlayerActor = prevCurActor;
	byteCurPlayer = prevCurPlayer;
}

static inline int16_t AdjustRemoteWalkAxis(int16_t val, int16_t other, const PAD_KEYS& keys)
{
	if (keys.bKeys[ePadKeys::KEY_WALK]) {
		if (val == -128) return -64;
		if (val == 128) return 64;
	}
	// CTaskSimpleUseGun::SetMoveAnim requires (|lr| + |ud|) / 128.0f >= 0.1f (~13)
	// to play GUNMOVE walk legs; gentle touch-stick tilts can fall just below that.
	if (val != 0 && keys.bKeys[ePadKeys::KEY_HANDBRAKE]) {
		const int absSum = std::abs(static_cast<int>(val)) + std::abs(static_cast<int>(other));
		if (absSum < 16) {
			return static_cast<int16_t>(val < 0 ? -16 : 16);
		}
	}
	return val;
}

DECL_HOOK(int16_t, CPad_GetPedWalkLeftRight, uintptr_t _this)
{
	if (CurrentPlayerSlot()) {
		const PAD_KEYS& rk = RemotePlayerKeys[byteCurPlayer];
		return AdjustRemoteWalkAxis(static_cast<int16_t>(rk.wKeyLR), static_cast<int16_t>(rk.wKeyUD), rk);
	}
	else {
		g_lastLocalPadPtr = _this;
		if (IsPadMovementDisabled(_this)) {
			LocalPlayerKeys.wKeyLR = 0;
			return 0;
		}
		int16_t lr = g_touchCombat.onFootStickSampled ? g_touchCombat.stickLR : 0;
		if (!g_touchCombat.stickTouched) {
			const int16_t padLR = CPad_GetPedWalkLeftRight(_this);
			if (padLR != 0) {
				lr = padLR;
			}
		}
		if (lr == 0 && pUI && pUI->buttonpanel() && pUI->buttonpanel()->heldLR() != 0) {
			lr = static_cast<int16_t>(pUI->buttonpanel()->heldLR());
		}
		LocalPlayerKeys.wKeyLR = static_cast<uint16_t>(lr);
		return lr;
	}
}
DECL_HOOK(int16_t, CPad_GetPedWalkUpDown, uintptr_t _this)
{
	if (CurrentPlayerSlot()) {
		const PAD_KEYS& rk = RemotePlayerKeys[byteCurPlayer];
		return AdjustRemoteWalkAxis(static_cast<int16_t>(rk.wKeyUD), static_cast<int16_t>(rk.wKeyLR), rk);
	}
	else {
		g_lastLocalPadPtr = _this;
		if (IsPadMovementDisabled(_this)) {
			LocalPlayerKeys.wKeyUD = 0;
			return 0;
		}
		int16_t ud = g_touchCombat.onFootStickSampled ? g_touchCombat.stickUD : 0;
		if (!g_touchCombat.stickTouched) {
			const int16_t padUD = CPad_GetPedWalkUpDown(_this);
			if (padUD != 0) {
				ud = padUD;
			}
		}
		if (ud == 0 && pUI && pUI->buttonpanel() && pUI->buttonpanel()->heldUD() != 0) {
			ud = static_cast<int16_t>(pUI->buttonpanel()->heldUD());
		}
		LocalPlayerKeys.wKeyUD = static_cast<uint16_t>(ud);
		return ud;
	}
}

// Fix GTA:SA high-FPS (>45 FPS) aim-walk deadlock:
// CTaskSimpleUseGun::ControlGunMove slews m_vecMoveCommand by at most
// (0.07f * CTimer::ms_fTimeStep) per frame (~0.058 at 60 FPS, ~0.029 at 120 FPS).
// On the same frame, CTaskSimpleUseGun::SetMoveAnim checks (|x| + |y|) < 0.10f
// and resets m_vecMoveCommand back to (0, 0), trapping aiming peds at zero speed
// forever when starting from rest or reversing strafe direction (both touch & WASD).
// When the caller commands active movement (|req.x| + |req.y| >= 0.10f) and the
// slewed result lands below the 0.10f threshold, nudge it cleanly past 0.105f
// along the commanded vector so SetMoveAnim enters GUNMOVE on frame 1.
DECL_HOOK(int, CTaskSimpleUseGun_ControlGunMove, uintptr_t _this, sa::CVector2D* pMove)
{
	const int ret = CTaskSimpleUseGun_ControlGunMove(_this, pMove);
	if (_this && pMove) {
		const float reqL1 = std::fabs(pMove->x) + std::fabs(pMove->y);
		if (reqL1 >= 0.10f) {
			auto* cmd = reinterpret_cast<sa::CVector2D*>(_this + kTaskUseGunMoveCmdOff);
			const float curL1 = std::fabs(cmd->x) + std::fabs(cmd->y);
			if (curL1 < 0.105f) {
				const float scale = std::min(1.0f, 0.105f / reqL1);
				cmd->x = pMove->x * scale;
				cmd->y = pMove->y * scale;
			}
		}
	}
	return ret;
}

// Fix GTA:SA CTaskSimpleUseGun::StartAnim bug when aiming starts while already
// moving: for non-MOVEFIRE weapons (Rifle 33, Sniper 34, RPG 35/36, Flamethrower 37,
// Minigun 38, Camera 43, or non-Hitman skill) and crouch-moving, StartAnim returns
// early at 0x5dec5c without advancing m_nLastCommand from 0 to 1, causing
// CTaskSimpleUseGun::ProcessPed (0x5deaa0) to run AbortIK every frame instead of AimGun.
DECL_HOOK(void, CTaskSimpleUseGun_StartAnim, uintptr_t _this, sa::CPed* pPed)
{
	CTaskSimpleUseGun_StartAnim(_this, pPed);
	if (!_this) return;

	constexpr uintptr_t kNextCmdOff = VER_x32 ? 0x0F : 0x17;
	constexpr uintptr_t kLastCmdOff = VER_x32 ? 0x10 : 0x18;
	constexpr uintptr_t kAnimPtrOff = VER_x32 ? 0x2C : 0x40;

	const uintptr_t pAnim = *reinterpret_cast<const uintptr_t*>(_this + kAnimPtrOff);
	int8_t& nextCmd = *reinterpret_cast<int8_t*>(_this + kNextCmdOff);
	int8_t& lastCmd = *reinterpret_cast<int8_t*>(_this + kLastCmdOff);
	if (pAnim == 0 && lastCmd == 0 && nextCmd == 1) {
		lastCmd = 1;
		nextCmd = 0;
	}
}

// Enable movement while scoped in 1st-person weapon modes (Sniper=34, RPG=35, HS-RPG=36, Camera=43):
// Stock GTA:SA Mobile calls ControlGunMove({0, 0}) in PlayerControlZeldaWeapon when
// (weaponInfo->m_nFlags & 0x04) and !IsPlayerUse2PlayerControls. Because ControlGunMove
// slew-rate interpolates m_vecMoveCommand by +-dt*rate, calling it again afterward without
// undoing vanilla's {0, 0} slew cancels both steps and traps |x|+|y| around ~0.08 (< 0.10
// SetMoveAnim threshold), freezing remote Sniper legs on frame 0. Save and restore
// m_vecMoveCommand across the vanilla call so ControlGunMove runs once toward our target.
DECL_HOOK(void, CTaskSimplePlayerOnFoot_PlayerControlZeldaWeapon, void* _this, sa::CPed* pPed)
{
	const bool bIsRemote = pPed && pPed != GamePool_FindPlayerPed() && FindPlayerNumFromPedPtr(pPed) != 0;
	const float savedAimRot = bIsRemote ? pPed->m_fAimingRotation : 0.0f;

	uintptr_t preTaskUseGun = 0;
	sa::CVector2D savedMoveCmd{0.0f, 0.0f};
	bool hasSavedScopedMoveCmd = false;
	if (pPed && pPed->m_pIntelligence && !pPed->m_pAttachedTo) {
		preTaskUseGun = eng::fn::CPedIntelligence_GetTaskUseGun(pPed->m_pIntelligence);
		if (preTaskUseGun) {
			const uintptr_t weaponInfo = *reinterpret_cast<const uintptr_t*>(preTaskUseGun + kTaskUseGunWeaponInfoOff);
			if (weaponInfo && ((*reinterpret_cast<const uint8_t*>(weaponInfo + 0x18) & 0x04u) != 0)) {
				savedMoveCmd = *reinterpret_cast<const sa::CVector2D*>(preTaskUseGun + kTaskUseGunMoveCmdOff);
				hasSavedScopedMoveCmd = true;
			}
		}
	}

	CTaskSimplePlayerOnFoot_PlayerControlZeldaWeapon(_this, pPed);

	if (bIsRemote) {
		pPed->m_fAimingRotation = savedAimRot;
	}

	if (!pPed || !pPed->m_pIntelligence || pPed->m_pAttachedTo) {
		return;
	}

	uintptr_t taskUseGun = eng::fn::CPedIntelligence_GetTaskUseGun(pPed->m_pIntelligence);
	if (!taskUseGun) {
		return;
	}

	const uintptr_t weaponInfo = *reinterpret_cast<const uintptr_t*>(taskUseGun + kTaskUseGunWeaponInfoOff);
	if (!weaponInfo) {
		return;
	}

	const uint8_t weapFlags0 = *reinterpret_cast<const uint8_t*>(weaponInfo + 0x18);
	if ((weapFlags0 & 0x04u) == 0) {
		return;
	}

	if (hasSavedScopedMoveCmd && taskUseGun == preTaskUseGun) {
		*reinterpret_cast<sa::CVector2D*>(taskUseGun + kTaskUseGunMoveCmdOff) = savedMoveCmd;
	}

	int16_t lr = 0;
	int16_t ud = 0;
	if (CurrentPlayerSlot() != 0) {
		const PAD_KEYS& rk = RemotePlayerKeys[byteCurPlayer];
		lr = AdjustRemoteWalkAxis(static_cast<int16_t>(rk.wKeyLR), static_cast<int16_t>(rk.wKeyUD), rk);
		ud = AdjustRemoteWalkAxis(static_cast<int16_t>(rk.wKeyUD), static_cast<int16_t>(rk.wKeyLR), rk);
	}
	else if (g_lastLocalPadPtr != 0) {
		// Reuse unified local walk axis hook (lock check + touch stick + hardware WASD + ButtonPanel + LocalPlayerKeys).
		lr = CPad_GetPedWalkLeftRight_hook(g_lastLocalPadPtr);
		ud = CPad_GetPedWalkUpDown_hook(g_lastLocalPadPtr);
	}
	else if (!IsLocalActivePedMovementLocked()) {
		lr = g_touchCombat.onFootStickSampled ? g_touchCombat.stickLR : 0;
		ud = g_touchCombat.onFootStickSampled ? g_touchCombat.stickUD : 0;
		LocalPlayerKeys.wKeyLR = static_cast<uint16_t>(lr);
		LocalPlayerKeys.wKeyUD = static_cast<uint16_t>(ud);
	}

	sa::CVector2D move{static_cast<float>(lr) * (1.0f / 128.0f), static_cast<float>(ud) * (1.0f / 128.0f)};
	const float magSq = move.x * move.x + move.y * move.y;
	if (magSq > 1.0f) {
		const float invMag = 1.0f / std::sqrt(magSq);
		move.x *= invMag;
		move.y *= invMag;
	}
	eng::fn::CTaskSimpleUseGun_ControlGunMove(taskUseGun, &move);
}

// 1) Recover orphaned bIsDucking without an active CTaskSimpleDuck (otherwise
//    PlayerControlDucked exits early every frame, freezing movement while syncing
//    SPECIAL_ACTION_DUCK=1).
// 2) Preserve remote ped m_fAimingRotation from local camera orientation during
//    crouch movement (matching PC SA-MP's patch at 0x688200).
// 3) Suppress local stand-up triggers during CROUCH_ROLL_L/R (0x39/0x3b): vanilla
//    PlayerControlDucked calls ClearTaskDuckSecondary() (which fails mid-roll) and
//    unconditionally falls through to blend GUNMOVE_FWD, evicting WEAPON_CROUCH and
//    causing CTaskSimpleDuck to self-delete without clearing bIsDucking.
DECL_HOOK(void, CTaskSimplePlayerOnFoot_PlayerControlDucked, void* _this, sa::CPed* pPed)
{
	if (!pPed) {
		CTaskSimplePlayerOnFoot_PlayerControlDucked(_this, pPed);
		return;
	}

	const uintptr_t taskDuck = pPed->m_pIntelligence
		? eng::fn::CPedIntelligence_GetTaskDuck(pPed->m_pIntelligence, true)
		: 0;
	if (pPed->bIsDucking && !taskDuck) {
		pPed->m_nPedFlags.bIsDucking = false;
		return;
	}

	const bool bIsRemote = pPed != GamePool_FindPlayerPed() && FindPlayerNumFromPedPtr(pPed) != 0;
	if (bIsRemote) {
		const float savedAimRot = pPed->m_fAimingRotation;
		CTaskSimplePlayerOnFoot_PlayerControlDucked(_this, pPed);
		pPed->m_fAimingRotation = savedAimRot;
		return;
	}

	const bool prevSuppress = g_suppressLocalDuckStandUp;
	if (IsDuckTaskRolling(taskDuck)) {
		g_suppressLocalDuckStandUp = true;
	}
	CTaskSimplePlayerOnFoot_PlayerControlDucked(_this, pPed);
	g_suppressLocalDuckStandUp = prevSuppress;
}

// War Drum Mobile adds a touch-only guard in CTaskSimpleDuck::CanPedDuck that
// returns false when scoped in Sniper (34, camMode 7) or Camera (43, camMode 46).
// Allow ducking for those weapons to preserve PC parity for local crouch-sniping
// and prevent remote PC crouch-snipers from aborting duck every frame.
DECL_HOOK(bool, CTaskSimpleDuck_CanPedDuck, sa::CPed* pPed)
{
	if (pPed && pPed->m_nActiveWeaponSlot < 13) {
		const uint32_t wep = pPed->m_aWeapons[pPed->m_nActiveWeaponSlot].dwType;
		if (wep == sa::eWeaponType::WEAPONTYPE_SNIPERRIFLE
			|| wep == sa::eWeaponType::WEAPONTYPE_CAMERA) {
			return true;
		}
	}
	return CTaskSimpleDuck_CanPedDuck(pPed);
}

DECL_HOOK(uint16_t, CPad_JumpJustDown, uintptr_t _this)
{
	if (CurrentPlayerSlot()) {
		if (!RemotePlayerKeys[byteCurPlayer].bIgnoreJump && RemotePlayerKeys[byteCurPlayer].bKeys[ePadKeys::KEY_JUMP] && !RemotePlayerKeys[byteCurPlayer].bKeys[ePadKeys::KEY_HANDBRAKE]) {
			RemotePlayerKeys[byteCurPlayer].bIgnoreJump = true;
			return RemotePlayerKeys[byteCurPlayer].bKeys[ePadKeys::KEY_JUMP];
		}

		return 0;
	}
	else {
		if (g_suppressLocalDuckStandUp) {
			return 0;
		}
		if (CPlayerPed* pLocal = pGame ? pGame->FindPlayerPed() : nullptr) {
			if (!pLocal->IsInVehicle() && pLocal->IsCarry()) {
				LocalPlayerKeys.bKeys[ePadKeys::KEY_JUMP] = false;
				return 0;
			}
		}
		const uint16_t jumpDown = CPad_JumpJustDown(_this);
		LocalPlayerKeys.bKeys[ePadKeys::KEY_JUMP] = (jumpDown != 0);
		return jumpDown;
	}
}
DECL_HOOK(uint16_t, CPad_GetJump, uintptr_t _this)
{
	if (CurrentPlayerSlot()) {
		if (RemotePlayerKeys[byteCurPlayer].bIgnoreJump || RemotePlayerKeys[byteCurPlayer].bKeys[ePadKeys::KEY_HANDBRAKE]) {
			return 0;
		}
		return RemotePlayerKeys[byteCurPlayer].bKeys[ePadKeys::KEY_JUMP];
	}
	else {
		if (CPlayerPed* pLocal = pGame ? pGame->FindPlayerPed() : nullptr) {
			if (!pLocal->IsInVehicle() && pLocal->IsCarry()) {
				LocalPlayerKeys.bKeys[ePadKeys::KEY_JUMP] = false;
				return 0;
			}
		}
		const uint16_t jumpHeld = CPad_GetJump(_this);
		LocalPlayerKeys.bKeys[ePadKeys::KEY_JUMP] = (jumpHeld != 0);
		return jumpHeld;
	}
}

DECL_HOOK(uint16_t, CPad_GetDuck, uintptr_t _this)
{
	// Match PC SA-MP: remote on-foot crouch is exclusively driven by
	// SPECIAL_ACTION_DUCK (ApplyCrouch/ResetCrouch), never by KEY_CROUCH
	// (in-vehicle horn/hydraulics use CPad_GetHorn / CPad_GetHydraulicJump).
	if (CurrentPlayerSlot()) {
		return 0;
	}
	else {
		if (CPlayerPed* pLocal = pGame ? pGame->FindPlayerPed() : nullptr) {
			if (!pLocal->IsInVehicle() && pLocal->IsCarry()) {
				LocalPlayerKeys.bKeys[ePadKeys::KEY_CROUCH] = false;
				return 0;
			}
		}
		const uint16_t duckHeld = (CPad_GetDuck(_this) != 0 || g_bHardwareCrouchHeld.load(std::memory_order_acquire)) ? 1u : 0u;
		LocalPlayerKeys.bKeys[ePadKeys::KEY_CROUCH] = (duckHeld != 0);
		return duckHeld;
	}
}

DECL_HOOK(uint16_t, CPad_MeleeAttackJustDown, uintptr_t _this)
{
	if (CurrentPlayerSlot()) {
		if (RemotePlayerKeys[byteCurPlayer].bKeys[ePadKeys::KEY_HANDBRAKE] && RemotePlayerKeys[byteCurPlayer].bKeys[ePadKeys::KEY_SECONDARY_ATTACK]) {
			return 2;
		}

		return RemotePlayerKeys[byteCurPlayer].bKeys[ePadKeys::KEY_FIRE];
	}
	else {
		uint32_t dwResult = CPad_MeleeAttackJustDown(_this);
		// Trigger a one-shot FightStyle combo when pressing F (KEY_SECONDARY_ATTACK)
		// while holding Block; never read back our own meleeComboUntilMs sync output,
		// which previously caused an infinite self-retriggering punch loop.
		if (dwResult == 0 && GetBlockableLocalPed() != nullptr
				&& g_touchCombat.blockDown
				&& LocalPlayerKeys.bKeys[ePadKeys::KEY_SECONDARY_ATTACK]) {
			LocalPlayerKeys.bKeys[ePadKeys::KEY_SECONDARY_ATTACK] = false;
			dwResult = 2;
		}
		if (dwResult == 2) {
			g_touchCombat.meleeComboUntilMs = GetTickCount() + 130;
			return 2;
		}
		if (dwResult == 1) {
			g_touchCombat.meleeFireUntilMs = GetTickCount() + 100;
			return 1;
		}
		return dwResult;
	}
}

DECL_HOOK(uint16_t, CPad_DuckJustDown, uintptr_t _this, sa::CPed* pPed)
{
	// Match PC SA-MP: remote on-foot crouch is exclusively driven by
	// SPECIAL_ACTION_DUCK (ApplyCrouch/ResetCrouch). Feeding KEY_CROUCH here
	// causes PlayerControlZelda to re-enter crouch right after ResetCrouch()
	// and clash WEAPON_CROUCH against IDLE_STANCE.
	if (CurrentPlayerSlot()) {
		return 0;
	}
	else {
		if (g_suppressLocalDuckStandUp) {
			g_bHardwareCrouchEdge.store(false, std::memory_order_release);
			LocalPlayerKeys.bKeys[ePadKeys::KEY_CROUCH] = false;
			return 0;
		}
		if (CPlayerPed* pLocal = pGame ? pGame->FindPlayerPed() : nullptr) {
			if (!pLocal->IsInVehicle() && pLocal->IsCarry()) {
				g_bHardwareCrouchEdge.store(false, std::memory_order_release);
				LocalPlayerKeys.bKeys[ePadKeys::KEY_CROUCH] = false;
				return 0;
			}
		}
		const bool hwEdge = g_bHardwareCrouchEdge.exchange(false, std::memory_order_acq_rel);
		const uint16_t duckEdge = (CPad_DuckJustDown(_this, pPed) != 0 || hwEdge) ? 1u : 0u;
		LocalPlayerKeys.bKeys[ePadKeys::KEY_CROUCH] = (duckEdge != 0);
		return duckEdge;
	}
}

DECL_HOOK(bool, CPad_GetTarget, uintptr_t _this, bool bCheckVehicle)
{
	if (const uint8_t slot = CurrentPlayerSlot(); slot > 0 && slot < PLAYER_PED_SLOTS) {
		return RemotePlayerKeys[slot].bKeys[ePadKeys::KEY_HANDBRAKE];
	}
	if (IsLocalMeleeComboActive()) {
		return true;
	}
	return CPad_GetTarget(_this, bCheckVehicle);
}

DECL_HOOK(uint32_t, CPad_GetSprint, uintptr_t _this, uint32_t unk)
{
	if (CurrentPlayerSlot()) {
		return RemotePlayerKeys[byteCurPlayer].bKeys[ePadKeys::KEY_SPRINT];
	}
	else {
		if (CPlayerPed* pLocal = pGame ? pGame->FindPlayerPed() : nullptr) {
			if (!pLocal->IsInVehicle() && (pLocal->IsCuffed() || pLocal->IsCarry())) {
				LocalPlayerKeys.bKeys[ePadKeys::KEY_SPRINT] = false;
				return 0;
			}
		}
		LocalPlayerKeys.bKeys[ePadKeys::KEY_SPRINT] = CPad_GetSprint(_this, unk);
		return LocalPlayerKeys.bKeys[ePadKeys::KEY_SPRINT];
	}
}

DECL_HOOK(uint32_t, CPad_GetAutoClimb, uintptr_t _this)
{
	if (CurrentPlayerSlot()) {
		if (RemotePlayerKeys[byteCurPlayer].bKeys[ePadKeys::KEY_HANDBRAKE]) {
			return 0;
		}
		return RemotePlayerKeys[byteCurPlayer].bKeys[ePadKeys::KEY_JUMP];
	}
	else {
		const uint32_t climb = CPad_GetAutoClimb(_this);
		LocalPlayerKeys.bKeys[ePadKeys::KEY_JUMP] = (climb != 0);
		return climb;
	}
}

DECL_HOOK(uint32_t, CPad_GetAbortClimb, uintptr_t _this)
{
	if (CurrentPlayerSlot()) {
		return RemotePlayerKeys[byteCurPlayer].bKeys[ePadKeys::KEY_SECONDARY_ATTACK];
	}
	else {
		const uint32_t abortClimb = CPad_GetAbortClimb(_this);
		if (abortClimb != 0) {
			LocalPlayerKeys.bKeys[ePadKeys::KEY_SECONDARY_ATTACK] = true;
		}
		return abortClimb;
	}
}

DECL_HOOK(uint32_t, CPad_DiveJustDown, uintptr_t _this)
{
	if (CurrentPlayerSlot()) {
		return RemotePlayerKeys[byteCurPlayer].bKeys[ePadKeys::KEY_FIRE];
	}
	else {
		LocalPlayerKeys.bKeys[ePadKeys::KEY_FIRE] = CPad_DiveJustDown(_this);
		return LocalPlayerKeys.bKeys[ePadKeys::KEY_FIRE];
	}
}

DECL_HOOK(uint32_t, CPad_SwimJumpJustDown, uintptr_t _this)
{
	if (CurrentPlayerSlot()) {
		return RemotePlayerKeys[byteCurPlayer].bKeys[ePadKeys::KEY_JUMP];
	}
	else {
		LocalPlayerKeys.bKeys[ePadKeys::KEY_JUMP] = CPad_SwimJumpJustDown(_this);
		return LocalPlayerKeys.bKeys[ePadKeys::KEY_JUMP];
	}
}

// CTaskSimplePlayerOnFoot::PlayerControlFighter accumulates m_nTimer when
// ped->m_pTargetedObject == nullptr and CHID::GetInputType() != 2, forcing
// QUIT_FIGHTING (cmd 15) at >1999ms (~2s). Reset that idle fighter timeout
// whenever either the local player or a remote player is actively holding block.
DECL_HOOK(void, CTaskSimplePlayerOnFoot_PlayerControlFighter, uintptr_t _this, sa::CPed* pPed)
{
	if (_this && pPed) {
		const uint8_t slot = CurrentPlayerSlot();
		if (slot == 0) {
			if (pPed == GamePool_FindPlayerPed() && g_touchCombat.blockDown) {
				*reinterpret_cast<uint32_t*>(_this + kTaskPlayerOnFootTimerOffset) = 0;
			}
		}
		else if (slot < PLAYER_PED_SLOTS
				&& RemotePlayerKeys[slot].bKeys[ePadKeys::KEY_JUMP]
				&& RemotePlayerKeys[slot].bKeys[ePadKeys::KEY_HANDBRAKE]) {
			*reinterpret_cast<uint32_t*>(_this + kTaskPlayerOnFootTimerOffset) = 0;
		}
	}
	CTaskSimplePlayerOnFoot_PlayerControlFighter(_this, pPed);
}

DECL_HOOK(uint32_t, CPad_GetBlock, uintptr_t _this)
{
	if (CurrentPlayerSlot()) {
		return (RemotePlayerKeys[byteCurPlayer].bKeys[ePadKeys::KEY_JUMP] && RemotePlayerKeys[byteCurPlayer].bKeys[ePadKeys::KEY_HANDBRAKE]);
	}
	if (!_this || *reinterpret_cast<uint16_t*>(_this + 272) != 0) {
		return 0;
	}
	if (GetBlockableLocalPed()) {
		const bool bBlocking = g_touchCombat.blockDown
			|| (LocalPlayerKeys.bKeys[ePadKeys::KEY_JUMP] && LocalPlayerKeys.bKeys[ePadKeys::KEY_HANDBRAKE]);
		return bBlocking ? 1u : 0u;
	}
	return 0;
}

DECL_HOOK(int16_t, CPad_GetSteeringLeftRight, uintptr_t _this)
{
	if (CurrentPlayerSlot()) {
		return static_cast<int16_t>(RemotePlayerKeys[byteCurPlayer].wKeyLR);
	}
	else {
		g_lastLocalPadPtr = _this;
		if (IsPadMovementDisabled(_this)) {
			LocalPlayerKeys.wKeyLR = 0;
			return 0;
		}
		int16_t lr = CPad_GetSteeringLeftRight(_this);
		if (lr == 0 && pUI && pUI->buttonpanel() && pUI->buttonpanel()->heldLR() != 0) {
			lr = static_cast<int16_t>(pUI->buttonpanel()->heldLR());
		}
		LocalPlayerKeys.wKeyLR = static_cast<uint16_t>(lr);
		return lr;
	}
}

DECL_HOOK(int16_t, CPad_GetSteeringUpDown, uintptr_t _this)
{
	if (CurrentPlayerSlot()) {
		return static_cast<int16_t>(RemotePlayerKeys[byteCurPlayer].wKeyUD);
	}
	else {
		g_lastLocalPadPtr = _this;
		if (IsPadMovementDisabled(_this)) {
			LocalPlayerKeys.wKeyUD = 0;
			return 0;
		}
		int16_t ud = CPad_GetSteeringUpDown(_this);
		if (ud == 0 && pUI && pUI->buttonpanel() && pUI->buttonpanel()->heldUD() != 0) {
			ud = static_cast<int16_t>(pUI->buttonpanel()->heldUD());
		}
		LocalPlayerKeys.wKeyUD = static_cast<uint16_t>(ud);
		return ud;
	}
}

DECL_HOOK(uint16_t, CPad_GetAccelerate, uintptr_t _this)
{
	if (CurrentPlayerSlot()) {
		return RemotePlayerKeys[byteCurPlayer].bKeys[ePadKeys::KEY_SPRINT] ? 0xFF : 0x00;
	}
	else {
		uint16_t wResult = CPad_GetAccelerate(_this);
		LocalPlayerKeys.bKeys[ePadKeys::KEY_SPRINT] = wResult;
		sa::CPed* local = GamePool_FindPlayerPed();
		sa::CVehicle* pVehicle = local ? local->pVehicle : nullptr;
		if (pVehicle && pVehicle->pDriver == local && !pVehicle->m_nVehicleFlags.bEngineOn) {
			return 0;
		}
		return wResult;
	}
}

DECL_HOOK(uint16_t, CPad_GetBrake, uintptr_t _this)
{
	if (CurrentPlayerSlot()) {
		return RemotePlayerKeys[byteCurPlayer].bKeys[ePadKeys::KEY_JUMP] ? 0xFF : 0x00;
	}
	else {
		uint16_t wResult = CPad_GetBrake(_this);
		LocalPlayerKeys.bKeys[ePadKeys::KEY_JUMP] = wResult;
		sa::CPed* local = GamePool_FindPlayerPed();
		sa::CVehicle* pVehicle = local ? local->pVehicle : nullptr;
		if (pVehicle && pVehicle->pDriver == local && !pVehicle->m_nVehicleFlags.bEngineOn) {
			return 0;
		}
		return wResult;
	}
}

DECL_HOOK(uint32_t, CPad_GetHandBrake, uintptr_t _this)
{
	if (const uint8_t slot = CurrentPlayerSlot(); slot > 0 && slot < PLAYER_PED_SLOTS) {
		return RemotePlayerKeys[slot].bKeys[ePadKeys::KEY_HANDBRAKE] ? 0xFF : 0x00;
	}
	else {
		uint32_t dwResult = CPad_GetHandBrake(_this);
		LocalPlayerKeys.bKeys[ePadKeys::KEY_HANDBRAKE] = dwResult;
		return dwResult;
	}
}

DECL_HOOK(uint32_t, CPad_GetHorn, uintptr_t _this, bool bCheckVehicle)
{
	if (const uint8_t slot = CurrentPlayerSlot(); slot > 0 && slot < PLAYER_PED_SLOTS) {
		return RemotePlayerKeys[slot].bKeys[ePadKeys::KEY_CROUCH];
	}
	else {
		uint32_t dwResult = CPad_GetHorn(_this, bCheckVehicle);
		LocalPlayerKeys.bKeys[ePadKeys::KEY_CROUCH] = dwResult;
		return dwResult;
	}
}

DECL_HOOK(uint32_t, CPad_ExitVehicleJustDown, uintptr_t _this, int a2, uintptr_t vehicle, int a4, uintptr_t vec)
{
	// Never leak local touch enter-car presses into remote peds (e.g., PlayerControlDucked).
	if (CurrentPlayerSlot() || g_suppressLocalDuckStandUp) {
		return 0;
	}

	static uint32_t dwPassengerEnterExit = 0;
	if (dwPassengerEnterExit != 0 && GetTickCount() - dwPassengerEnterExit < 500) {
		return 0;
	}

	if (CPlayerPed* pLocal = pGame ? pGame->FindPlayerPed() : nullptr) {
		if (!pLocal->IsInVehicle() && (pLocal->IsCuffed() || pLocal->IsCarry())) {
			return 0;
		}
	}

	if (pNetGame) {
		CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
		if (pPlayerPool) {
			CLocalPlayer* pLocalPlayer = pPlayerPool->GetLocalPlayer();
			if (pLocalPlayer) {
				if (pLocalPlayer->HandlePassengerEntry()) {
					dwPassengerEnterExit = GetTickCount();
					return 0;
				}
			}
		}
	}

	return CPad_ExitVehicleJustDown(_this, a2, vehicle, a4, vec);
}

DECL_HOOK(int, CPad_GetTurretLeft, uintptr_t _this)
{
	if (CurrentPlayerSlot()) {
		return RemotePlayerKeys[byteCurPlayer].bKeys[ePadKeys::KEY_LOOK_LEFT];
	}
	else {
		LocalPlayerKeys.bKeys[ePadKeys::KEY_LOOK_LEFT] = CPad_GetTurretLeft(_this);
		return LocalPlayerKeys.bKeys[ePadKeys::KEY_LOOK_LEFT];
	}
}

DECL_HOOK(int, CPad_GetTurretRight, uintptr_t _this)
{
	if (CurrentPlayerSlot()) {
		return RemotePlayerKeys[byteCurPlayer].bKeys[ePadKeys::KEY_LOOK_RIGHT];
	}
	else {
		LocalPlayerKeys.bKeys[ePadKeys::KEY_LOOK_RIGHT] = CPad_GetTurretRight(_this);
		return LocalPlayerKeys.bKeys[ePadKeys::KEY_LOOK_RIGHT];
	}
}

DECL_HOOK(int, CPad_GetNitroFired, uintptr_t _this)
{
	if (CurrentPlayerSlot()) {
		return RemotePlayerKeys[byteCurPlayer].bKeys[ePadKeys::KEY_FIRE];
	}
	else {
		LocalPlayerKeys.bKeys[ePadKeys::KEY_FIRE] = CPad_GetNitroFired(_this);
		return LocalPlayerKeys.bKeys[ePadKeys::KEY_FIRE];
	}
}

DECL_HOOK(int, CPad_GetHydraulicJump, uintptr_t _this)
{
	if (CurrentPlayerSlot()) {
		return RemotePlayerKeys[byteCurPlayer].bKeys[ePadKeys::KEY_CROUCH];
	}
	else {
		LocalPlayerKeys.bKeys[ePadKeys::KEY_CROUCH] = CPad_GetHydraulicJump(_this);
		return LocalPlayerKeys.bKeys[ePadKeys::KEY_CROUCH];
	}
}

DECL_HOOK(uint32_t, CTaskSimpleUseGun_SetPedPosition, uintptr_t _this, sa::CPed* ped)
{
	sa::CPed* prevCurActor = dwCurPlayerActor;
	const uint8_t prevCurPlayer = byteCurPlayer;

	byteInternalPlayer = CurrentPlayerSlot();
	const uint8_t curPlayer = ped ? FindPlayerNumFromPedPtr(ped) : 0;

	if (ped && (curPlayer != 0) && byteInternalPlayer == 0) // remote player
	{
		dwCurPlayerActor = ped;
		byteCurPlayer = curPlayer;
		ScopedPlayerContext ctx(curPlayer, ScopedPlayerContext::kFull);
		CTaskSimpleUseGun_SetPedPosition(_this, ped);
	}
	else // localplayer
	{
		CTaskSimpleUseGun_SetPedPosition(_this, ped);
	}

	dwCurPlayerActor = prevCurActor;
	byteCurPlayer = prevCurPlayer;
	return 0;
}

DECL_HOOK(uint32_t, CPad_GetWeapon, uintptr_t _this, sa::CPed* pPed, bool bShoulderPossible)
{
	if (CurrentPlayerSlot()) {
		return RemotePlayerKeys[byteCurPlayer].bKeys[ePadKeys::KEY_FIRE] ? 0xFF : 0x00;
	}

	CPlayerPed* pLocal = pGame ? pGame->FindPlayerPed() : nullptr;
	if (!pLocal || pLocal->IsInJetpack()) {
		uint32_t dwResult = CPad_GetWeapon(_this, pPed, bShoulderPossible);
		LocalPlayerKeys.bKeys[ePadKeys::KEY_FIRE] = dwResult;
		return dwResult;
	}

	if (pLocal->IsInVehicle()) {
		if (pLocal->IsAPassenger() && pLocal->IsInPassengerDriveByMode()) {
			const uint32_t orig = CPad_GetWeapon(_this, pPed, bShoulderPossible);
			const bool bFire = g_touchCombat.fireDown || LocalPlayerKeys.bKeys[ePadKeys::KEY_FIRE] || (orig != 0);
			LocalPlayerKeys.bKeys[ePadKeys::KEY_FIRE] = bFire;
			return bFire ? 0xFF : 0x00;
		}
		uint32_t dwResult = CPad_GetWeapon(_this, pPed, bShoulderPossible);
		LocalPlayerKeys.bKeys[ePadKeys::KEY_FIRE] = dwResult;
		return dwResult;
	}

	uint32_t dwResult = 0;
	if (CanLocalPedUseOnFootFire(_this, pPed)) {
		dwResult = EvaluateLocalTouchFireLevel(pLocal->GetCurrentWeapon()) ? 0xFF : 0x00;
	}
	LocalPlayerKeys.bKeys[ePadKeys::KEY_FIRE] = (dwResult != 0);
	return dwResult;
}

// Mobile libGTASA bug fix: CPlayerInfo::Process() probes whether the player
// can enter a nearby vehicle EVERY frame by calling
// activeTask->MakeAbortable(ped, ABORT_PRIORITY_URGENT=1, nullptr) BEFORE
// checking ExitVehicleJustDown(). For CTaskSimplePlayerOnFoot, MakeAbortable
// unconditionally aborts secondary task 0 (CTaskSimpleUseGun), which:
//   1) kills hipfire every frame near vehicles (stuck in raise-gun frame 1);
//   2) destroys & recreates CTaskSimpleUseGun between aim-tap shots, skipping
//      the weapon's post-shot cooldown animation (~160ms instead of ~720ms).
// Scope-guard CPlayerInfo::Process and suppress its speculative probe abort
// unless the player actually pressed/released the enter-car widget.
static bool s_bInPlayerInfoProcess = false;

DECL_HOOK(void, CPlayerInfo_Process, uintptr_t _this, int playerNumber)
{
	bool prev = s_bInPlayerInfoProcess;
	if (playerNumber == 0) {
		s_bInPlayerInfoProcess = true;
	}
	CPlayerInfo_Process(_this, playerNumber);
	s_bInPlayerInfoProcess = prev;

	if (playerNumber == 0 && g_bCancelCarTask) {
		g_bCancelCarTask = false;
		CPlayerPed* pLocal = pGame ? pGame->FindPlayerPed() : nullptr;
		if (pLocal && pLocal->m_ped && pLocal->m_ped->m_pIntelligence && pLocal->m_gtaId) {
			sa::CTask* pPrimary = pLocal->m_ped->m_pIntelligence->m_TaskMgr.m_aPrimaryTasks[sa::TASK_PRIMARY_PRIMARY];
			const int taskType = pPrimary ? GetTaskTypeFromTask(&pPrimary) : -1;
			// Only clear if GTA actually installed an enter/leave car task (700/704),
			// avoiding wiping unrelated scripted or Carry/Dance animations.
			if (taskType == 700 || taskType == 704 || taskType == 711) {
				ScriptCommand(&clear_char_tasks, pLocal->m_gtaId);
			}
		}
	}
}

DECL_HOOK(uint32_t, CTaskSimplePlayerOnFoot_MakeAbortable, uintptr_t _this, sa::CPed* pPed, int priority, uintptr_t pEvent)
{
	if (s_bInPlayerInfoProcess && !CurrentPlayerSlot() && priority == 1 && pEvent == 0
		&& pPed && pPed == GamePool_FindPlayerPed() && pPed->m_pIntelligence) {
		uintptr_t useGun = eng::fn::CPedIntelligence_GetTaskUseGun( pPed->m_pIntelligence);
		if (useGun) {
			bool bEnterCarInput = false;
			uintptr_t* widgets = GetTouchWidgets();
			if (widgets && widgets[sa::WidgetIDs::WIDGET_ENTER_CAR]) {
				bEnterCarInput =
					eng::fn::CTouchInterface_IsTouched(
						sa::WidgetIDs::WIDGET_ENTER_CAR, static_cast<void*>(nullptr), 0) != 0
					|| eng::fn::CTouchInterface_IsReleased(
						sa::WidgetIDs::WIDGET_ENTER_CAR, static_cast<void*>(nullptr), 0) != 0;
			}
			if (!bEnterCarInput) {
				bool bAiming = (pPed->m_pPlayerData && pPed->m_pPlayerData->m_bFreeAiming)
					|| Using1stPersonWeaponCam();
				// Aiming: report non-abortable (no enter-car prompt);
				// Hipfire: report abortable so enter-car prompt stays visible,
				// without prematurely killing CTaskSimpleUseGun.
				return bAiming ? 0u : 1u;
			}
		}
	}
	return CTaskSimplePlayerOnFoot_MakeAbortable(_this, pPed, priority, pEvent);
}

DECL_HOOK(uint32_t, CPad_WeaponJustDown, uintptr_t _this, sa::CPed* pPed, bool bCheckControl, bool bCheckVehicle)
{
	if (CurrentPlayerSlot()) {
		return RemotePlayerKeys[byteCurPlayer].bKeys[ePadKeys::KEY_FIRE] ? 1u : 0u;
	}

	CPlayerPed* pLocal = pGame ? pGame->FindPlayerPed() : nullptr;
	if (!pLocal || pLocal->IsInJetpack()) {
		return CPad_WeaponJustDown(_this, pPed, bCheckControl, bCheckVehicle);
	}
	if (pLocal->IsInVehicle()) {
		if (pLocal->IsAPassenger() && pLocal->IsInPassengerDriveByMode()) {
			const uint32_t orig = CPad_WeaponJustDown(_this, pPed, bCheckControl, bCheckVehicle);
			return (g_touchCombat.fireJustPressed || orig != 0) ? 1u : 0u;
		}
		return CPad_WeaponJustDown(_this, pPed, bCheckControl, bCheckVehicle);
	}

	if (!CanLocalPedUseOnFootFire(_this, pPed)) {
		return 0;
	}
	return g_touchCombat.fireJustPressed ? 1u : 0u;
}

DECL_HOOK(bool, CPad_CycleWeaponLeftJustDown, uintptr_t _this)
{
	if (CurrentPlayerSlot()) {
		return false;
	}
	else {
		return CPad_CycleWeaponLeftJustDown(_this);
	}
}

DECL_HOOK(uint32_t, CPad_CycleWeaponRightJustDown, uintptr_t _this)
{
	if (CurrentPlayerSlot()) {
		return 0;
	}
	else {
		return CPad_CycleWeaponRightJustDown(_this);
	}
}

DECL_HOOK(uint32_t, CPad_GetEnterTargeting, uintptr_t _this)
{
	const uint8_t slot = CurrentPlayerSlot();
	if (slot != 0) {
		if (slot >= PLAYER_PED_SLOTS || !RemotePlayerKeys[slot].bKeys[ePadKeys::KEY_HANDBRAKE]) {
			return 0;
		}
		sa::CPed* pRemotePed = (WORLD_PLAYERS != nullptr) ? WORLD_PLAYERS[slot].m_pPed : dwCurPlayerActor;
		if (pRemotePed && pRemotePed->m_pPlayerData && !pRemotePed->m_pPlayerData->m_bFreeAiming) {
			return 1u;
		}
		return 0;
	}
	g_lastLocalPadPtr = _this;
	if (!_this || *reinterpret_cast<uint16_t*>(_this + 272) != 0) {
		return 0;
	}
	CPlayerPed* pPed = GetAimableLocalPed();
	return (pPed && g_touchCombat.aimLatch) ? 1u : 0u;
}

// CPad::GetExitTargeting is only called inside local CCamera::CamControl
// (remote exit is handled directly in CPed_ProcessControl).
DECL_HOOK(uint32_t, CPad_GetExitTargeting, uintptr_t _this)
{
	if (CurrentPlayerSlot() != 0) {
		return 0;
	}
	CPlayerPed* pPed = GetAimableLocalPed();
	const bool bFiring = g_touchCombat.fireDown || LocalPlayerKeys.bKeys[ePadKeys::KEY_FIRE];
	if (pPed && !g_touchCombat.aimLatch && !bFiring && EngineIsAiming(pPed)) {
		eng::fn::CPlayerPed_ClearWeaponTarget(pPed->m_ped);
		if (pPed->m_ped->m_pPlayerData) {
			pPed->m_ped->m_pPlayerData->m_bFreeAiming = false;
		}
		return 1;
	}
	return 0;
}

DECL_HOOK(uint32_t, CCamera_IsTargetingActive, uintptr_t _this, sa::CPed* pPed)
{
	if (pPed == GamePool_FindPlayerPed()) {
		const uint32_t bIsTargeting = CCamera_IsTargetingActive(_this, pPed);
		LocalPlayerKeys.bKeys[ePadKeys::KEY_HANDBRAKE] = (bIsTargeting != 0);
		return bIsTargeting;
	}

	const uint8_t slot = pPed ? FindPlayerNumFromPedPtr(pPed) : CurrentPlayerSlot();
	if (slot == 0 || slot >= PLAYER_PED_SLOTS) {
		return CCamera_IsTargetingActive(_this, pPed);
	}
	if (!RemotePlayerKeys[slot].bKeys[ePadKeys::KEY_HANDBRAKE]) {
		return 0;
	}
	// Delegate to native check (m_bFreeAiming, m_pTargetedObject, or scoped
	// PlayerWeaponMode set by ScopedPlayerContext) so Frame 1 of non-scoped
	// aiming returns 0 and enters through CPad_GetEnterTargeting cleanly
	// (setting m_bFreeAiming = 1 for all guns including Rifle 33 & Flamethrower 37).
	return CCamera_IsTargetingActive(_this, pPed);
}

void Hooks::installPadHooks()
{
	spdlog::info("Installing pad hooks...");

	memset(&LocalPlayerKeys, 0, sizeof(PAD_KEYS));

	// playerped & per-frame guard hooks (replaces per-frame .text nop/restoreNop)
	REG_HOOK(Boot, CPlayerPed_ProcessControl, CPed_ProcessControl, "Remote player ped control context switch");
	REG_HOOK_BL(Boot, addr::CPed_UpdatePosition_BranchBL1, "CPed_UpdatePosition.ProcessControl",
		&CPed_UpdatePosition_fromProcessControl_hook, &CPed_UpdatePosition_fromProcessControl,
		"Suppress CPed::UpdatePosition for remote peds");
	REG_HOOK(Boot, CTaskSimplePlayerOnFoot_PlayerControlZelda, CTaskSimplePlayerOnFoot_PlayerControlZelda,
		"Preserve remote ped m_fAimingRotation from camera");
	REG_HOOK(Boot, CTaskSimplePlayerOnFoot_PlayerControlZeldaWeapon, CTaskSimplePlayerOnFoot_PlayerControlZeldaWeapon,
		"Enable 1st-person scoped weapon movement and preserve remote aim rotation");
	REG_HOOK(Boot, CTaskSimplePlayerOnFoot_PlayerControlDucked, CTaskSimplePlayerOnFoot_PlayerControlDucked,
		"Preserve remote crouch rotation, recover orphan duck flag, and guard mid-roll stand-up");
	REG_HOOK(Boot, CTaskSimpleDuck_CanPedDuck, CTaskSimpleDuck_CanPedDuck,
		"Allow Sniper/Camera crouch parity under touch input");

	InstallVehicleControlHooks();

	REG_HOOK(Boot, CPad_GetPedWalkLeftRight, CPad_GetPedWalkLeftRight, "Pad walk left/right sync");
	REG_HOOK(Boot, CPad_GetPedWalkUpDown, CPad_GetPedWalkUpDown, "Pad walk up/down sync");
	REG_HOOK(Boot, CPad_GetSprint, CPad_GetSprint, "Pad sprint sync");
	REG_HOOK(Boot, CPad_GetJump, CPad_GetJump, "Pad jump sync");
	REG_HOOK(Boot, CPad_JumpJustDown, CPad_JumpJustDown, "Pad jump just-down sync");
	REG_HOOK(Boot, CPad_GetDuck, CPad_GetDuck, "Pad duck sync");
	REG_HOOK(Boot, CPad_DuckJustDown, CPad_DuckJustDown, "Pad duck just-down sync");
	REG_HOOK(Boot, CPad_MeleeAttackJustDown, CPad_MeleeAttackJustDown, "Pad melee attack sync");
	REG_HOOK(Boot, CPad_GetBlock, CPad_GetBlock, "Pad block widget override");
	REG_HOOK(Boot, CTaskSimplePlayerOnFoot_PlayerControlFighter, CTaskSimplePlayerOnFoot_PlayerControlFighter, "Touch block timeout guard");

	InstallTouchCombatHooks();

#if VER_x32
	REG_HOOK_ADDR(Boot, addr::CPad_GetAbortClimb_arm32, CPad_GetAbortClimb, true, "Pad abort climb sync (arm32)");
#else
	REG_HOOK(Boot, CPad_GetAbortClimb, CPad_GetAbortClimb, "Pad abort climb sync");
#endif
	REG_HOOK(Boot, CPad_GetAutoClimb, CPad_GetAutoClimb, "Pad auto climb sync");
	REG_HOOK(Boot, CPad_SwimJumpJustDown, CPad_SwimJumpJustDown, "Pad swim jump sync");
	REG_HOOK(Boot, CPad_DiveJustDown, CPad_DiveJustDown, "Pad dive sync");

	REG_HOOK(Boot, CPad_GetSteeringUpDown, CPad_GetSteeringUpDown, "Pad vehicle steer up/down sync");
	REG_HOOK(Boot, CPad_GetSteeringLeftRight, CPad_GetSteeringLeftRight, "Pad vehicle steer left/right sync");
	REG_HOOK(Boot, CPad_GetHandBrake, CPad_GetHandBrake, "Pad handbrake sync");
	REG_HOOK(Boot, CPad_GetBrake, CPad_GetBrake, "Pad brake sync");
	REG_HOOK(Boot, CPad_GetAccelerate, CPad_GetAccelerate, "Pad accelerate sync");
	REG_HOOK(Boot, CPad_GetHorn, CPad_GetHorn, "Pad horn sync");
	REG_HOOK(Boot, CPad_ExitVehicleJustDown, CPad_ExitVehicleJustDown, "Pad exit vehicle sync");
	REG_HOOK(Boot, CPad_GetTurretRight, CPad_GetTurretRight, "Pad turret right sync");
	REG_HOOK(Boot, CPad_GetTurretLeft, CPad_GetTurretLeft, "Pad turret left sync");
	REG_HOOK(Boot, CPad_GetHydraulicJump, CPad_GetHydraulicJump, "Pad hydraulic jump sync");
	REG_HOOK(Boot, CPad_GetNitroFired, CPad_GetNitroFired, "Pad nitro sync");

	REG_HOOK(Boot, CTaskSimpleUseGun_StartAnim, CTaskSimpleUseGun_StartAnim, "Fix UseGun aim-while-moving m_nLastCommand transition");
	REG_HOOK(Boot, CTaskSimpleUseGun_ControlGunMove, CTaskSimpleUseGun_ControlGunMove, "Fix high-FPS aiming walk deadlock");
	REG_HOOK(Boot, CTaskSimpleUseGun_SetPedPosition, CTaskSimpleUseGun_SetPedPosition, "Remote player aim/gun context switch");
	REG_HOOK(Boot, CPlayerInfo_Process, CPlayerInfo_Process, "Car-proximity hipfire abort guard");
	REG_HOOK(Boot, CTaskSimplePlayerOnFoot_MakeAbortable, CTaskSimplePlayerOnFoot_MakeAbortable, "Protect UseGun from proximity probe");

	REG_HOOK(Boot, CPad_CycleWeaponRightJustDown, CPad_CycleWeaponRightJustDown, "Pad weapon cycle right");
	REG_HOOK(Boot, CPad_CycleWeaponLeftJustDown, CPad_CycleWeaponLeftJustDown, "Pad weapon cycle left");
	REG_HOOK(Boot, CPad_GetWeapon, CPad_GetWeapon, "Pad weapon fire sync");
	REG_HOOK(Boot, CPad_WeaponJustDown, CPad_WeaponJustDown, "Pad weapon just-down sync");
	REG_HOOK(Boot, CCamera_IsTargetingActivePed, CCamera_IsTargetingActive, "Remote player targeting check");
	REG_HOOK(Boot, CPad_GetTarget, CPad_GetTarget, "Remote player aim/roll target sync");
	REG_HOOK(Boot, CPad_GetEnterTargeting, CPad_GetEnterTargeting, "Touch aim button enter");
	REG_HOOK(Boot, CPad_GetExitTargeting, CPad_GetExitTargeting, "Touch aim button exit");

	InstallBulletSyncHooks();
	InstallDamageHooks();
}
