#include "TouchCombat.h"
#include "Common.h"
#include "Game.h"
#include "Pad.h"
#include "PadInternal.h"
#include "PcInput.h"
#include "PlayerPed.h"
#include "TouchWidgets.h"
#include "Util.h"
#include "Engine/PatchRegistry.h"
#include "Engine/Symbols.h"
#include "../Addr.h"
#include "../UI/UI.h"
#include "../UI/SAMPWidgets/ButtonPanel.h"
#include "../Net/NetGame.h"
#include "../Net/LocalPlayer.h"
#include "../Client.h"
#include "../Settings.h"
#include "Enums/WidgetIDs.h"
#include <Memory.h>
#include <SA.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <atomic>
#include <cmath>

static std::atomic<bool> g_bStuffUseRequested{false};
static std::atomic<bool> g_bPassengerDriveByToggleRequested{false};

bool ConsumeStuffUseRequest()
{
	return g_bStuffUseRequested.exchange(false, std::memory_order_acq_rel);
}

bool ConsumePassengerDriveByToggle()
{
	return g_bPassengerDriveByToggleRequested.exchange(false, std::memory_order_acq_rel);
}

TouchCombatState g_touchCombat{};

// Mouse/teclado PC (PcInput). O sampler de toque reescreve fireDown a cada
// frame, entao o estado externo entra como OR no mesmo ponto.
static std::atomic<bool> g_pcFireDown{false};
static std::atomic<bool> g_pcFireJustPressed{false};
static std::atomic<bool> g_pcAimDown{false};

void TouchCombatSetExternalFire(bool down, bool justPressed)
{
	g_pcFireDown.store(down, std::memory_order_release);
	if (justPressed)
		g_pcFireJustPressed.store(true, std::memory_order_release);
}

void TouchCombatSetExternalAim(bool down)
{
	g_pcAimDown.store(down, std::memory_order_release);
}


bool IsLocalMeleeComboActive()
{
	return GetBlockableLocalPed() != nullptr
		&& static_cast<int32_t>(g_touchCombat.meleeComboUntilMs - GetTickCount()) > 0;
}

bool IsLocalMeleeFireActive()
{
	return GetBlockableLocalPed() != nullptr
		&& static_cast<int32_t>(g_touchCombat.meleeFireUntilMs - GetTickCount()) > 0;
}

bool IsLocalMeleeBlockActive()
{
	return GetBlockableLocalPed() != nullptr && g_touchCombat.blockDown && !IsLocalMeleeComboActive();
}

static inline bool LocalPedHasCancellableSpecialAction(CPlayerPed* pPed)
{
	return pPed && !pPed->IsDead() && !pPed->IsInVehicle()
		&& (pPed->GetStuff() != STUFF_TYPE_NONE || pPed->IsDancing()
			|| pPed->HasHandsUp() || pPed->IsPissing());
}

// Tap vs hold threshold for regular firearms: a normal touch tap lasts
// ~40-150ms (3-9 frames), which would otherwise fire 2-3 rounds on automatic
// weapons. Press edge fires immediately; sustained hold repeats after 180ms.
static constexpr uint32_t kFireHoldRepeatDelayMs = 180;

// CWidget / CWidgetRegionMove / RsGlobal offsets verified on arm32 and arm64 libGTASA.so
static constexpr uintptr_t kWidgetSpriteOffset       = VER_x32 ? 8   : 16;
static constexpr uintptr_t kWidgetPosOffset          = VER_x32 ? 12  : 24;
static constexpr uintptr_t kWidgetScreenRectOffset   = VER_x32 ? 32  : 44;
static constexpr uintptr_t kWidgetTouchRectOffset    = VER_x32 ? 52  : 64;
static constexpr uintptr_t kWidgetAlphaOffset        = VER_x32 ? 76  : 88;
static constexpr uintptr_t kWidgetEnabledOffset      = VER_x32 ? 77  : 89;
static constexpr uintptr_t kWidgetCachedTouchOffset  = VER_x32 ? 120 : 132;
static constexpr uintptr_t kWidgetMoveScaleOffset    = VER_x32 ? 148 : 168;
static constexpr uintptr_t kWidgetMoveThumbPosOffset = VER_x32 ? 156 : 184;
static constexpr uintptr_t kWidgetMoveAnchorOffset   = VER_x32 ? 164 : 192;
static constexpr uintptr_t kRsGlobalWidthOffset      = VER_x32 ? 4   : 8;
static constexpr uintptr_t kRsGlobalHeightOffset     = VER_x32 ? 8   : 12;

static int32_t FindActiveWidgetTouchSlot(uintptr_t widget)
{
	if (!widget) return -1;
	auto* initialWidgets = eng::as<uintptr_t*>(eng::Sym::CWidget_m_pInitialTouchWidget);
	if (!initialWidgets) return -1;

	const int32_t cached = *reinterpret_cast<const int32_t*>(widget + kWidgetCachedTouchOffset);
	if (cached >= 0 && cached < 4
			&& initialWidgets[cached] == widget
			&& eng::fn::CTouchInterface_IsTouchDown(cached) != 0) {
		return cached;
	}
	for (int32_t i = 0; i < 4; ++i) {
		if (initialWidgets[i] == widget && eng::fn::CTouchInterface_IsTouchDown(i) != 0) {
			return i;
		}
	}
	return -1;
}

static uintptr_t g_activeScaledJoystickWidget = 0;
static float g_activeJoystickOrigSx = 0.0f;

// Temporarily scales CWidgetRegionMove's WidgetPosition.sx (outer ring radius,
// inner thumb radius, and max drag distance) while CWidget_Update always uses
// unscaled origSx so m_ScreenRect / m_TouchRect span the full left half of the
// screen (preventing CoorsAreInWidget in CWidgetRegionMove::Update from setting
// alpha = 0 mid-drag when joystickScale < 1.0f, and avoiding nested double-scale).
struct ScopedJoystickScale {
	uintptr_t w = 0;
	float origSx = 0.0f;

	explicit ScopedJoystickScale(uintptr_t widget)
	{
		float scale = Settings::joystickScale();
		if (!widget || scale == 1.0f || scale < 0.4f || scale > 2.0f) return;
		if (g_activeScaledJoystickWidget == widget) return;
		w = widget;
		origSx = *reinterpret_cast<float*>(w + kWidgetPosOffset + 8);
		g_activeScaledJoystickWidget = w;
		g_activeJoystickOrigSx = origSx;
		*reinterpret_cast<float*>(w + kWidgetPosOffset + 8) = origSx * scale;
	}

	~ScopedJoystickScale()
	{
		if (w) {
			*reinterpret_cast<float*>(w + kWidgetPosOffset + 8) = origSx;
			g_activeScaledJoystickWidget = 0;
			g_activeJoystickOrigSx = 0.0f;
		}
	}
};

static void ExpandPedMoveTouchZone(uintptr_t widget)
{
	if (!widget) return;
	uintptr_t* widgets = GetTouchWidgets();
	if (!widgets || widget != widgets[sa::WidgetIDs::WIDGET_PED_MOVE]) return;

	const auto* rsGlobal = eng::as<const uint8_t*>(eng::Sym::RsGlobal);
	if (!rsGlobal) return;
	const float screenW = static_cast<float>(*reinterpret_cast<const int32_t*>(rsGlobal + kRsGlobalWidthOffset));
	const float screenH = static_cast<float>(*reinterpret_cast<const int32_t*>(rsGlobal + kRsGlobalHeightOffset));
	if (screenW <= 1.0f || screenH <= 1.0f) return;

	// Stock TouchDefaultPhoneWidescreen.cfg restricts WIDGET_PED_MOVE to
	// [0..37.5% W] x [55.4%..100% H] (top=598px on 1080p), causing natural
	// left-thumb touches slightly above midline or right of 37.5% to fall
	// through to WIDGET_LOOK. Expand m_ScreenRect/m_TouchRect to [0..45% W] x
	// [28%..100% H] (below radar/chat) without changing WidgetPosition.sx
	// (keeping the visual joystick ring and drag radius untouched).
	const float targetRight = screenW * 0.45f;
	const float targetTop = screenH * 0.28f;
	for (uintptr_t off : {kWidgetScreenRectOffset, kWidgetTouchRectOffset}) {
		float* rect = reinterpret_cast<float*>(widget + off); // {left, bottom, right, top}
		rect[0] = 0.0f;
		rect[1] = std::max(rect[1], screenH);
		rect[2] = std::max(rect[2], targetRight);
		rect[3] = std::min(rect[3], targetTop);
	}
}

void RunWithUnscaledJoystickSx(uintptr_t widget, void (*fn)(uintptr_t))
{
	if (widget && widget == g_activeScaledJoystickWidget) {
		float& sx = *reinterpret_cast<float*>(widget + kWidgetPosOffset + 8);
		const float scaledSx = sx;
		sx = g_activeJoystickOrigSx;
		fn(widget);
		sx = scaledSx;
		ExpandPedMoveTouchZone(widget);
		return;
	}
	fn(widget);
	ExpandPedMoveTouchZone(widget);
}

// Protect an actively held left thumbstick (WIDGET_PED_MOVE) from having its
// bound touch slot & clamped anchor overwritten when a second finger touches
// the left half of the screen during multi-touch combat. Redirect the extra
// finger to WIDGET_LOOK (if enabled) instead of leaving it orphaned at 0.
DECL_HOOK(void, CWidget_SetInitialTouchWidget, int touchNum, uintptr_t widget)
{
	if (widget && touchNum >= 0 && touchNum < 4) {
		uintptr_t* widgets = GetTouchWidgets();
		if (widgets && widget == widgets[sa::WidgetIDs::WIDGET_PED_MOVE]) {
			const int32_t moveTouch = FindActiveWidgetTouchSlot(widget);
			if (moveTouch >= 0 && moveTouch != touchNum) {
				uintptr_t lookWidget = widgets[sa::WidgetIDs::WIDGET_LOOK];
				if (lookWidget && *reinterpret_cast<const uint8_t*>(lookWidget + kWidgetEnabledOffset) != 0) {
					CWidget_SetInitialTouchWidget(touchNum, lookWidget);
				}
				else if (auto* initialWidgets = eng::as<uintptr_t*>(eng::Sym::CWidget_m_pInitialTouchWidget)) {
					initialWidgets[touchNum] = 0;
				}
				return;
			}
		}
	}
	CWidget_SetInitialTouchWidget(touchNum, widget);
}

// Prevent CTouchInterface::Clear from locking WIDGET_LOOK into pinch-zoom mode
// while the player is simultaneously holding the movement thumbstick.
DECL_HOOK(void, CWidget_SetPinchZoomWidget, uintptr_t widget)
{
	if (widget) {
		uintptr_t* widgets = GetTouchWidgets();
		if (widgets && widget == widgets[sa::WidgetIDs::WIDGET_LOOK]
				&& FindActiveWidgetTouchSlot(widgets[sa::WidgetIDs::WIDGET_PED_MOVE]) >= 0) {
			CWidget_SetPinchZoomWidget(0);
			return;
		}
	}
	CWidget_SetPinchZoomWidget(widget);
}

DECL_HOOK(void, CWidgetRegionMove_OnInitialTouch, uintptr_t _this)
{
	ScopedJoystickScale guard(_this);
	CWidgetRegionMove_OnInitialTouch(_this);
}

DECL_HOOK(void, CWidgetRegionMove_Update, uintptr_t _this)
{
	ScopedJoystickScale guard(_this);
	CWidgetRegionMove_Update(_this);
}

DECL_HOOK(void, CWidgetRegionMove_ClampInternalTouchAnchor, uintptr_t _this)
{
	ScopedJoystickScale guard(_this);
	CWidgetRegionMove_ClampInternalTouchAnchor(_this);
}

DECL_HOOK(void, CWidgetRegionMove_Draw, uintptr_t _this)
{
	ScopedJoystickScale guard(_this);
	CWidgetRegionMove_Draw(_this);
}

DECL_HOOK(uint32_t, CWidgetRegionMove_IsTouched, uintptr_t _this, uintptr_t pVec2D)
{
	ScopedJoystickScale guard(_this);

	uintptr_t* widgets = GetTouchWidgets();
	const bool isPedMove = _this && widgets && _this == widgets[sa::WidgetIDs::WIDGET_PED_MOVE];
	if (!isPedMove) {
		return CWidgetRegionMove_IsTouched(_this, pVec2D);
	}

	// Touch path for WIDGET_PED_MOVE: compute live zero-latency displacement
	// directly from CTouchInterface::m_vecCachedPos[touch] and CWidget::m_vecTouchAnchor[touch]
	// instead of relying on last frame's stale m_vecThumbPos from CWidgetRegionMove::Update.
	auto* thumbPos = reinterpret_cast<sa::CVector2D*>(_this + kWidgetMoveThumbPosOffset);
	auto* clampedAnchor = reinterpret_cast<sa::CVector2D*>(_this + kWidgetMoveAnchorOffset);
	auto* outVec = reinterpret_cast<sa::CVector2D*>(pVec2D);

	const bool widgetEnabled = *reinterpret_cast<const uint8_t*>(_this + kWidgetEnabledOffset) != 0;
	const int32_t activeTouch = widgetEnabled ? FindActiveWidgetTouchSlot(_this) : -1;

	if (activeTouch < 0) {
		*thumbPos = *clampedAnchor;
		if (outVec) {
			outVec->x = 0.0f;
			outVec->y = 0.0f;
		}
		return 0;
	}

	*reinterpret_cast<int32_t*>(_this + kWidgetCachedTouchOffset) = activeTouch;

	const auto* cachedPos = eng::as<const sa::CVector2D*>(eng::Sym::CTouchInterface_m_vecCachedPos);
	const auto* touchAnchors = eng::as<const sa::CVector2D*>(eng::Sym::CWidget_m_vecTouchAnchor);
	const auto* rsGlobal = eng::as<const uint8_t*>(eng::Sym::RsGlobal);
	if (!cachedPos || !touchAnchors || !rsGlobal) {
		return CWidgetRegionMove_IsTouched(_this, pVec2D);
	}

	const float screenW = static_cast<float>(*reinterpret_cast<const int32_t*>(rsGlobal + kRsGlobalWidthOffset));
	const float screenH = static_cast<float>(*reinterpret_cast<const int32_t*>(rsGlobal + kRsGlobalHeightOffset));
	const float scaledSx = *reinterpret_cast<const float*>(_this + kWidgetPosOffset + 8);
	float moveScale = *reinterpret_cast<const float*>(_this + kWidgetMoveScaleOffset);
	if (moveScale <= 0.01f) moveScale = 0.45f;
	const float radius = moveScale * scaledSx * (screenW / 640.0f);
	if (radius <= 1.0f) {
		return CWidgetRegionMove_IsTouched(_this, pVec2D);
	}

	sa::CVector2D anchor = touchAnchors[activeTouch];
	if (anchor.x < radius) anchor.x = radius;
	if (anchor.y > screenH - radius) anchor.y = screenH - radius;
	*clampedAnchor = anchor;

	float dx = cachedPos[activeTouch].x - anchor.x;
	float dy = cachedPos[activeTouch].y - anchor.y;
	const float distSq = dx * dx + dy * dy;
	if (distSq > radius * radius) {
		const float invDist = radius / std::sqrt(distSq);
		dx *= invDist;
		dy *= invDist;
	}
	thumbPos->x = anchor.x + dx;
	thumbPos->y = anchor.y + dy;

	if (outVec) {
		outVec->x = (dx / radius) * 128.0f;
		outVec->y = (dy / radius) * 128.0f;
	}
	return 1;
}

// Replicates CPad::GetWeapon's top-level safety gates: pad controls disabled
// (+272), script fire disable (+303), ped alive/on-foot, and not entering a
// vehicle (tasks 700/701).
bool CanLocalPedUseOnFootFire(uintptr_t pad, sa::CPed* pPed)
{
	if (!pad || !pPed || !pPed->m_pPlayerData) return false;
	if (*reinterpret_cast<uint16_t*>(pad + 272) != 0) return false;
	if (*reinterpret_cast<uint8_t*>(pad + 303) != 0) return false;

	CPlayerPed* pLocal = pGame ? pGame->FindPlayerPed() : nullptr;
	if (!pLocal || pLocal->m_ped != pPed || pLocal->IsDead()
		|| pLocal->IsInVehicle() || pLocal->IsInJetpack()) {
		return false;
	}

	if (pPed->m_pIntelligence) {
		void* pTaskMgr = &pPed->m_pIntelligence->m_TaskMgr;
		if (eng::fn::CTaskManager_FindActiveTaskByType( pTaskMgr, 700)
			|| eng::fn::CTaskManager_FindActiveTaskByType( pTaskMgr, 701)) {
			return false;
		}
	}
	return true;
}

// Weapons that natively require continuous hold (throwables & continuous spray)
static bool WeaponUsesHoldFire(uint8_t type)
{
	switch (type) {
		case sa::eWeaponType::WEAPONTYPE_GRENADE:
		case sa::eWeaponType::WEAPONTYPE_TEARGAS:
		case sa::eWeaponType::WEAPONTYPE_MOLOTOV:
		case sa::eWeaponType::WEAPONTYPE_FLAMETHROWER:
		case sa::eWeaponType::WEAPONTYPE_SPRAYCAN:
		case sa::eWeaponType::WEAPONTYPE_EXTINGUISHER:
			return true;
	}
	return false;
}

// Single-shot / scoped / trigger-only weapons: fire strictly on press edge
static bool WeaponUsesEdgeOnlyFire(uint8_t type)
{
	switch (type) {
		case sa::eWeaponType::WEAPONTYPE_SNIPERRIFLE:
		case sa::eWeaponType::WEAPONTYPE_RLAUNCHER:
		case sa::eWeaponType::WEAPONTYPE_RLAUNCHER_HS:
		case sa::eWeaponType::WEAPONTYPE_DETONATOR:
		case sa::eWeaponType::WEAPONTYPE_CAMERA:
		case sa::eWeaponType::WEAPONTYPE_NIGHTVISION:
		case sa::eWeaponType::WEAPONTYPE_INFRARED:
			return true;
	}
	return false;
}

bool EvaluateLocalTouchFireLevel(uint8_t weaponType)
{
	if (!g_touchCombat.fireDown) {
		return false;
	}
	if (WeaponUsesHoldFire(weaponType)) {
		return true;
	}
	if (WeaponUsesEdgeOnlyFire(weaponType)) {
		return g_touchCombat.fireJustPressed;
	}
	// Regular firearms: immediate shot on press edge; sustained hold enters
	// continuous fire only after kFireHoldRepeatDelayMs so a tap never bursts.
	if (g_touchCombat.fireJustPressed) {
		return true;
	}
	return (GetTickCount() - g_touchCombat.fireDownTickMs) >= kFireHoldRepeatDelayMs;
}

// the engine only polls the targeting buttons (widgets 0x13/0x14) for
// scoped/special weapons {33..36,43,44,45} while in lock-on aim mode —
// regular guns get no aim button unless the aim-mode setting is free-aim,
// and its free-aim set additionally drops flamethrower/minigun/spraycan
// which SAMP lets you aim. Show the button for everything aimable:
// any weapon <=45 except the knife/katana melee specials.
static bool WeaponShowsAimButton(uint8_t type)
{
	if (type == sa::eWeaponType::WEAPONTYPE_UNARMED
		|| type > sa::eWeaponType::WEAPONTYPE_INFRARED) {
		return false;
	}
	return type != sa::eWeaponType::WEAPONTYPE_KNIFE
		&& type != sa::eWeaponType::WEAPONTYPE_KATANA;
}

CPlayerPed* GetAimableLocalPed()
{
	CPlayerPed* pPed = pGame ? pGame->FindPlayerPed() : nullptr;
	if (!pPed || pPed->IsDead() || pPed->IsInVehicle() || pPed->IsInJetpack()
		|| !WeaponShowsAimButton(pPed->GetCurrentWeapon())) {
		return nullptr;
	}
	return pPed;
}

// blocking is melee-only: fists and melee weapons (unarmed..cane).
CPlayerPed* GetBlockableLocalPed()
{
	CPlayerPed* pPed = pGame ? pGame->FindPlayerPed() : nullptr;
	if (!pPed || pPed->IsDead() || pPed->IsInVehicle() || pPed->IsInJetpack()
		|| pPed->GetCurrentWeapon() > sa::eWeaponType::WEAPONTYPE_CANE) {
		return nullptr;
	}
	return pPed;
}

// scoped weapons aim through a camera weapon-mode + PED_SNIPER_MODE, not the
// free-aim flag: ProcessPlayerWeapon's enter jump-table maps weapon -> mode
// (34->7, 35->8, 36->51, 43->46). For the projectiles (35/36) the native
// enter-poll is unreachable entirely (ProcessPlayerWeapon diverts before it),
// so TickTouchCombatWidgets drives this path directly.
static int ScopedAimCamMode(uint8_t type)
{
	switch (type) {
		case sa::eWeaponType::WEAPONTYPE_SNIPERRIFLE: return 7;
		case sa::eWeaponType::WEAPONTYPE_RLAUNCHER: return 8;
		case sa::eWeaponType::WEAPONTYPE_RLAUNCHER_HS: return 51;
		case sa::eWeaponType::WEAPONTYPE_CAMERA: return 46;
	}
	return 0;
}

bool Using1stPersonWeaponCam()
{
	return eng::fn::CCamera_Using1stPersonWeaponMode(
		&sa::getTheCamera());
}

// Engine-side aim state: CPlayerPedData::m_bFreeAiming, lock-on target, or
// first-person weapon camera (CCamera::IsTargetingActive covers all three).
bool EngineIsAiming(CPlayerPed* pPed)
{
	if (!pPed || !pPed->m_ped) return false;
	if (pPed->m_ped->m_pPlayerData && pPed->m_ped->m_pPlayerData->m_bFreeAiming) {
		return true;
	}
	return Using1stPersonWeaponCam()
		|| eng::fn::CCamera_IsTargetingActive(&sa::getTheCamera());
}

static uint32_t PollTouchRelease(int32_t widgetID)
{
	return eng::fn::CTouchInterface_IsReleased(
		widgetID, static_cast<void*>(nullptr), 1);
}

// Custom aim-toggle button lives in slot 0x98 (WIDGET_CAR_CHEATS — unused in SAMP).
// Engine widgets 19/20 stay disabled so their icons never cover the attack slot.
static constexpr int32_t kAimToggleWidget = sa::WidgetIDs::WIDGET_CAR_CHEATS;
static constexpr int32_t kBlockWidget     = sa::WidgetIDs::WIDGET_MISSION_SKIP;

static bool PollTouchDown(int32_t widgetID)
{
	uintptr_t* widgets = GetTouchWidgets();
	if (!widgets || !widgets[widgetID]) return false;
	return eng::fn::CTouchInterface_IsTouched(
		widgetID, static_cast<void*>(nullptr), 1) != 0;
}

// Render-phase helper (called from Render2dStuff): keeps layout and button
// visibility/sprites in sync without mutating game-logic input state.
void UpdateTouchCombatVisuals()
{
	uintptr_t* widgets = GetTouchWidgets();
	if (!widgets) return;

	// Igual ao mobilador: teclado/mouse conectado => nenhum widget de toque na tela.
	constexpr int32_t kWidgetCount = static_cast<int32_t>(sa::WidgetIDs::NUM_WIDGETS);
	static bool s_pcWidgetsHidden = false;
	static uint8_t s_savedEnabled[kWidgetCount] = {};
	static uint8_t s_savedAlpha[kWidgetCount] = {};

	if (PcInputIsEnabledForTouch()) {
		if (!s_pcWidgetsHidden) {
			for (int32_t id = 0; id < kWidgetCount; ++id) {
				uintptr_t w = widgets[id];
				if (!w) continue;
				s_savedEnabled[id] = *reinterpret_cast<uint8_t*>(w + kWidgetEnabledOffset);
				s_savedAlpha[id] = *reinterpret_cast<uint8_t*>(w + kWidgetAlphaOffset);
			}
			s_pcWidgetsHidden = true;
		}
		for (int32_t id = 0; id < kWidgetCount; ++id) {
			uintptr_t w = widgets[id];
			if (!w) continue;
			*reinterpret_cast<uint8_t*>(w + kWidgetEnabledOffset) = 0;
			*reinterpret_cast<uint8_t*>(w + kWidgetAlphaOffset) = 0;
		}
		return;
	}

	// Teclado/mouse desconectado: devolve os widgets ao estado que tinham antes.
	if (s_pcWidgetsHidden) {
		for (int32_t id = 0; id < kWidgetCount; ++id) {
			uintptr_t w = widgets[id];
			if (!w) continue;
			*reinterpret_cast<uint8_t*>(w + kWidgetEnabledOffset) = s_savedEnabled[id];
			*reinterpret_cast<uint8_t*>(w + kWidgetAlphaOffset) = s_savedAlpha[id];
		}
		s_pcWidgetsHidden = false;
	}

	FixTouchWidgetLayout();

	CPlayerPed* pLocal = pGame ? pGame->FindPlayerPed() : nullptr;
	bool bOnFoot = pLocal && !pLocal->IsDead() && !pLocal->IsInVehicle() && !pLocal->IsInJetpack();
	bool bPassenger = pLocal && !pLocal->IsDead() && pLocal->IsInVehicle() && pLocal->IsAPassenger();
	bool bPassengerDriveBy = bPassenger && pLocal->IsInPassengerDriveByMode();

	if ((bOnFoot || bPassengerDriveBy) && widgets[sa::WidgetIDs::WIDGET_ATTACK]) {
		uintptr_t wAtk = widgets[sa::WidgetIDs::WIDGET_ATTACK];
		*reinterpret_cast<uint8_t*>(wAtk + kWidgetEnabledOffset) = 1;
		*reinterpret_cast<uint8_t*>(wAtk + kWidgetAlphaOffset) = 0xff;
	}

	// Keep sprint button visible while dancing (WIDGET_PED_MOVE is natively kept
	// enabled every logic frame on foot via TickTouchCombatInput).
	if (bOnFoot && pLocal->IsDancing()) {
		if (uintptr_t wSprint = widgets[sa::WidgetIDs::WIDGET_SPRINT]) {
			*reinterpret_cast<uint8_t*>(wSprint + kWidgetEnabledOffset) = 1;
			*reinterpret_cast<uint8_t*>(wSprint + kWidgetAlphaOffset) = 0xff;
		}
	}

	// Keep enter-car button visible on foot while holding Stuff / Dancing / HandsUp / Pissing
	// so touch players can cancel/drop them (KEY_SECONDARY_ATTACK) even when no vehicle is nearby.
	if (bOnFoot && LocalPedHasCancellableSpecialAction(pLocal)) {
		if (uintptr_t wEnter = widgets[sa::WidgetIDs::WIDGET_ENTER_CAR]) {
			*reinterpret_cast<uint8_t*>(wEnter + kWidgetEnabledOffset) = 1;
			*reinterpret_cast<uint8_t*>(wEnter + kWidgetAlphaOffset) = 0xff;
		}
	}

	// On-foot camera toggle (widget 0x12): GTA:SA Mobile only polls
	// CycleCameraModeDownJustDown while in a vehicle, leaving the top-right
	// camera button hidden on foot. Keep it visible on foot (outside 1st-person
	// weapon scope) without touching it in vehicles where the engine owns it.
	if (bOnFoot && widgets[sa::WidgetIDs::WIDGET_CAM_TOGGLE]) {
		bool bShowCamToggle = !Using1stPersonWeaponCam();
		uintptr_t wCam = widgets[sa::WidgetIDs::WIDGET_CAM_TOGGLE];
		*reinterpret_cast<uint8_t*>(wCam + kWidgetEnabledOffset) = bShowCamToggle ? 1 : 0;
		*reinterpret_cast<uint8_t*>(wCam + kWidgetAlphaOffset) = bShowCamToggle ? 0xff : 0;
	}

	if (uintptr_t wAim = widgets[kAimToggleWidget]) {
		bool bAimable = (GetAimableLocalPed() != nullptr) || (bPassenger && pLocal->CanDoPassengerDriveBy());
		*reinterpret_cast<uint8_t*>(wAim + kWidgetEnabledOffset) = bAimable ? 1 : 0;
		*reinterpret_cast<uint8_t*>(wAim + kWidgetAlphaOffset) = bAimable ? 0xff : 0;
		uintptr_t wSrc = widgets[sa::WidgetIDs::WIDGET_ENTER_TARGETING];
		if (wSrc && *reinterpret_cast<uintptr_t*>(wSrc + kWidgetSpriteOffset)) {
			*reinterpret_cast<uintptr_t*>(wAim + kWidgetSpriteOffset) =
				*reinterpret_cast<uintptr_t*>(wSrc + kWidgetSpriteOffset);
		}
	}

	if (uintptr_t wBlk = widgets[kBlockWidget]) {
		bool bBlockable = GetBlockableLocalPed() != nullptr;
		*reinterpret_cast<uint8_t*>(wBlk + kWidgetEnabledOffset) = bBlockable ? 1 : 0;
		*reinterpret_cast<uint8_t*>(wBlk + kWidgetAlphaOffset) = bBlockable ? 0xff : 0;
		uintptr_t wSrc = widgets[sa::WidgetIDs::WIDGET_ATTACK];
		if (wSrc && *reinterpret_cast<uintptr_t*>(wSrc + kWidgetSpriteOffset)
			&& !*reinterpret_cast<uintptr_t*>(wBlk + kWidgetSpriteOffset)) {
			*reinterpret_cast<uintptr_t*>(wBlk + kWidgetSpriteOffset) =
				*reinterpret_cast<uintptr_t*>(wSrc + kWidgetSpriteOffset);
		}
	}
}

// Logic-phase input sampler (called right after CTouchInterface::Clear() inside
// CGame::Process(), before CWorld::Process / CPlayerPed::ProcessControl).
// Samples fire/block/aim once per logic frame so every CPad hook sees coherent,
// zero-lag state.
static void TickTouchCombatInput()
{
	// Modo teclado/mouse: os widgets touch nao sao mais fonte de input; apenas
	// o pipeline PC (PcInput) alimenta FIRE/AIM/BLOCK.
	if (PcInputIsEnabledForTouch()) {
		LocalPlayerKeys.wKeyLR = 0;
		LocalPlayerKeys.wKeyUD = 0;
		g_touchCombat.onFootStickSampled = false;
		g_touchCombat.stickTouched = false;
		g_touchCombat.stickLR = 0;
		g_touchCombat.stickUD = 0;
		g_touchCombat.blockDown = false;

		const bool pcFire = g_pcFireDown.load(std::memory_order_acquire);
		g_touchCombat.fireJustPressed = pcFire && !g_touchCombat.firePrevDown;
		if (g_pcFireJustPressed.exchange(false, std::memory_order_acq_rel))
			g_touchCombat.fireJustPressed = true;
		g_touchCombat.firePrevDown = pcFire;
		g_touchCombat.fireDown = pcFire;
		if (g_touchCombat.fireJustPressed)
			g_touchCombat.fireDownTickMs = GetTickCount();

		g_touchCombat.aimLatch = g_pcAimDown.load(std::memory_order_acquire);
		return;
	}

	// Reset analog axes and per-frame stick state each logic frame so tasks that
	// bypass CPad::GetPedWalk* never broadcast stale non-zero lr/ud.
	LocalPlayerKeys.wKeyLR = 0;
	LocalPlayerKeys.wKeyUD = 0;
	g_touchCombat.onFootStickSampled = false;
	g_touchCombat.stickTouched = false;
	g_touchCombat.stickLR = 0;
	g_touchCombat.stickUD = 0;

	CPlayerPed* pLocal = pGame ? pGame->FindPlayerPed() : nullptr;
	const bool bOnFoot = pLocal && !pLocal->IsDead() && !pLocal->IsInVehicle() && !pLocal->IsInJetpack();
	const bool bPassenger = pLocal && !pLocal->IsDead() && pLocal->IsInVehicle() && pLocal->IsAPassenger();
	const bool bPassengerDriveBy = bPassenger && pLocal->IsInPassengerDriveByMode();

	// Clear on-foot KEY_HANDBRAKE before re-applying ButtonPanel held keys so
	// frames that did not call GetKeys(clear=true) never retain stale aim state.
	if (bOnFoot) {
		LocalPlayerKeys.bKeys[ePadKeys::KEY_HANDBRAKE] = false;
	}

	if (pUI && pUI->buttonpanel() && !IsLocalActivePedMovementLocked()) {
		pUI->buttonpanel()->applyHeldKeys();
	}

	uintptr_t* widgets = GetTouchWidgets();
	if (!widgets) return;

	// 1) Fire button edge + hold tracking (on foot & passenger drive-by)
	const bool bFirePrev = g_touchCombat.firePrevDown;
	bool bFireNow = (bOnFoot || bPassengerDriveBy) && PollTouchDown(sa::WidgetIDs::WIDGET_ATTACK);
	bFireNow = bFireNow || g_pcFireDown.load(std::memory_order_acquire);
	g_touchCombat.fireJustPressed = bFireNow && !bFirePrev;
	if (g_pcFireJustPressed.exchange(false, std::memory_order_acq_rel))
		g_touchCombat.fireJustPressed = true;
	if (g_touchCombat.fireJustPressed) {
		g_touchCombat.fireDownTickMs = GetTickCount();
		if (bOnFoot && pLocal && pLocal->GetStuff() != STUFF_TYPE_NONE) {
			g_bStuffUseRequested.store(true, std::memory_order_release);
		}
	}
	g_touchCombat.firePrevDown = bFireNow;
	g_touchCombat.fireDown = bFireNow;

	// Allow cancelling Stuff / Dance / HandsUp / Pissing via WIDGET_ENTER_CAR on foot
	if (bOnFoot && LocalPedHasCancellableSpecialAction(pLocal)
			&& widgets[sa::WidgetIDs::WIDGET_ENTER_CAR]
			&& PollTouchRelease(sa::WidgetIDs::WIDGET_ENTER_CAR) != 0) {
		LocalPlayerKeys.bKeys[ePadKeys::KEY_SECONDARY_ATTACK] = true;
	}

	// 1.2) Aim button toggles passenger drive-by on touch screens
	if (bPassenger && pLocal->CanDoPassengerDriveBy() && widgets[kAimToggleWidget] && PollTouchRelease(kAimToggleWidget) != 0) {
		g_bPassengerDriveByToggleRequested.store(true, std::memory_order_release);
	}

	// 2) Aim-toggle & scoped camera mode state machine (runs BEFORE stick & key
	// sampling so entering/exiting 1st-person scope takes effect with 0-frame lag).
	if (PcInputIsEnabledForTouch() &&
		g_pcAimDown.load(std::memory_order_acquire) != g_touchCombat.aimLatch) {
		g_touchCombat.aimLatch = g_pcAimDown.load(std::memory_order_acquire);
	}
	if (CPlayerPed* pAimPed = GetAimableLocalPed()) {
		const uint8_t curAimWep = pAimPed->GetCurrentWeapon();
		if (g_touchCombat.lastAimWeapon != curAimWep) {
			g_touchCombat.aimLatch = false;
			g_touchCombat.lastAimWeapon = curAimWep;
		}

		if (widgets[kAimToggleWidget] && PollTouchRelease(kAimToggleWidget) != 0) {
			g_touchCombat.aimLatch = !g_touchCombat.aimLatch;
		}

		const int scopedMode = ScopedAimCamMode(curAimWep);
		if (scopedMode) {
			if (g_touchCombat.aimLatch && !Using1stPersonWeaponCam()) {
				eng::fn::CCamera_SetNewPlayerWeaponMode(
					&sa::getTheCamera(), scopedMode, 0, 0);
				eng::fn::CPed_SetPedState(
					pAimPed->m_ped, sa::ePedState::PED_SNIPER_MODE);
			}
			else if (!g_touchCombat.aimLatch && Using1stPersonWeaponCam()) {
				eng::fn::CCamera_ClearPlayerWeaponMode(
					&sa::getTheCamera());
				if (pAimPed->m_ped->m_pPlayerData) {
					pAimPed->m_ped->m_pPlayerData->m_bFreeAiming = false;
				}
				eng::fn::CPlayerPed_ClearWeaponTarget(pAimPed->m_ped);
			}
		}
	}
	else {
		g_touchCombat.aimLatch = false;
		g_touchCombat.lastAimWeapon = 0;
	}

	// 3) Unified on-foot virtual thumbstick poll: always poll WIDGET_PED_MOVE once
	// per logic frame on foot (when movement is unlocked) so m_nFrameCount never
	// starves to 0 across 3rd-person Sniper/RPG hipfire, scope transitions, duck/roll,
	// jump/climb, dancing, or skydiving.
	// CWidgetRegionMove::IsTouched outputs stick.x / stick.y in [-128.0f .. +128.0f].
	if (bOnFoot && !IsLocalActivePedMovementLocked()) {
		if (widgets[sa::WidgetIDs::WIDGET_PED_MOVE]) {
			sa::CVector2D stick{0.0f, 0.0f};
			g_touchCombat.onFootStickSampled = true;
			if (eng::fn::CTouchInterface_IsTouched(sa::WidgetIDs::WIDGET_PED_MOVE, &stick, 1) != 0) {
				g_touchCombat.stickTouched = true;
				if (pLocal->IsDancing()) {
					if (stick.x < -32.0f)      g_touchCombat.stickLR = -128;
					else if (stick.x > 32.0f)  g_touchCombat.stickLR = 128;
					else                       g_touchCombat.stickLR = 0;

					if (stick.y < -32.0f)      g_touchCombat.stickUD = -128;
					else if (stick.y > 32.0f)  g_touchCombat.stickUD = 128;
					else                       g_touchCombat.stickUD = 0;
				}
				else {
					// Apply a small radial deadzone to remove resting-thumb jitter and
					// smoothly rescale [deadzone .. 128] -> [0 .. 128] for true analog feel.
					constexpr float kStickDeadzone = 6.0f;
					const float mag = std::hypot(stick.x, stick.y);
					int lr = 0;
					int ud = 0;
					if (mag > kStickDeadzone) {
						const float scaledMag = std::min(
							128.0f,
							(mag - kStickDeadzone) * (128.0f / (128.0f - kStickDeadzone)));
						const float factor = scaledMag / mag;
						lr = std::clamp(static_cast<int>(std::lround(stick.x * factor)), -128, 128);
						ud = std::clamp(static_cast<int>(std::lround(stick.y * factor)), -128, 128);
						// While aiming, ensure non-zero tilts clear CTaskSimpleUseGun::SetMoveAnim's
						// (|x| + |y|) / 128 >= 0.1f (~13) leg-animation threshold without distorting
						// normal slow-walk responsiveness.
						if (g_touchCombat.aimLatch || EngineIsAiming(pLocal)) {
							const int absSum = std::abs(lr) + std::abs(ud);
							if (absSum > 0 && absSum < 16) {
								if (std::abs(lr) >= std::abs(ud)) {
									lr = (lr < 0) ? -16 : 16;
								}
								else {
									ud = (ud < 0) ? -16 : 16;
								}
							}
						}
					}
					g_touchCombat.stickLR = static_cast<int16_t>(lr);
					g_touchCombat.stickUD = static_cast<int16_t>(ud);
				}
			}
		}

		// Only pre-populate LocalPlayerKeys.wKeyLR/UD in modes where the engine does
		// not route movement through CPad::GetPedWalkLeftRight/UpDown (dancing,
		// skydiving/parachuting, class selection, spectating). During active on-foot
		// gameplay, CPad::GetPedWalk* (and scoped PlayerControlZeldaWeapon) writes
		// LocalPlayerKeys only when the ped actually consumes walk input, preventing
		// blocked/frozen states from broadcasting phantom stepping-in-place to remotes.
		bool bDirectAxisSyncMode = pLocal->IsDancing() || pLocal->IsParachuting();
		if (!bDirectAxisSyncMode && pNetGame && pNetGame->GetPlayerPool()) {
			if (CLocalPlayer* pNetLocal = pNetGame->GetPlayerPool()->GetLocalPlayer()) {
				bDirectAxisSyncMode = !pNetLocal->m_bIsActive || pNetLocal->IsSpectating();
			}
		}
		if (bDirectAxisSyncMode) {
			int16_t finalLR = g_touchCombat.stickLR;
			int16_t finalUD = g_touchCombat.stickUD;
			if (finalLR == 0 && pUI && pUI->buttonpanel() && pUI->buttonpanel()->heldLR() != 0) {
				finalLR = static_cast<int16_t>(pUI->buttonpanel()->heldLR());
			}
			if (finalUD == 0 && pUI && pUI->buttonpanel() && pUI->buttonpanel()->heldUD() != 0) {
				finalUD = static_cast<int16_t>(pUI->buttonpanel()->heldUD());
			}
			LocalPlayerKeys.wKeyLR = static_cast<uint16_t>(finalLR);
			LocalPlayerKeys.wKeyUD = static_cast<uint16_t>(finalUD);
		}

		if (pLocal->IsDancing() && PollTouchDown(sa::WidgetIDs::WIDGET_SPRINT)) {
			LocalPlayerKeys.bKeys[ePadKeys::KEY_SPRINT] = true;
		}
	}

	// 4) Block button & authoritative on-foot aim KEY_HANDBRAKE state
	// (Melee block/combo/fire outbound bits are injected exclusively in
	// ApplyLocalCombatSyncKeys so they never feed back into local pad reads.)
	g_touchCombat.blockDown = (GetBlockableLocalPed() != nullptr) && PollTouchDown(kBlockWidget);
	if (bOnFoot && pLocal && (g_touchCombat.aimLatch || EngineIsAiming(pLocal))) {
		LocalPlayerKeys.bKeys[ePadKeys::KEY_HANDBRAKE] = true;
	}

	// 5) On-foot camera distance toggle (close=1, normal=2, far=3)
	if (bOnFoot && !Using1stPersonWeaponCam()
		&& widgets[sa::WidgetIDs::WIDGET_CAM_TOGGLE]
		&& PollTouchRelease(sa::WidgetIDs::WIDGET_CAM_TOGGLE) != 0) {
		auto& cam = sa::getTheCamera();
		int32_t cur = cam.m_nPedZoom;
		cam.m_nPedZoom = (cur >= 1 && cur < 3) ? (cur + 1) : 1;
	}
}

// War Drum Mobile's CPlayerPed::ProcessControl clears m_bFreeAiming whenever
// WIDGET_ATTACK (1) is released for non-scoped guns, causing MODE_AIMWEAPON (53)
// to drop for 1 frame and re-enter with ResetStatics=1 (rightward shoulder yaw jump)
// while our touch aim-toggle latch is active. Suppress that release while latched.
DECL_HOOK(uint32_t, CTouchInterface_IsReleased_fromProcessControl, int32_t widgetID, void* pOutVec, int32_t frames)
{
	if (CurrentPlayerSlot() != 0 || (g_touchCombat.aimLatch && widgetID == sa::WidgetIDs::WIDGET_ATTACK)) {
		return 0;
	}
	return CTouchInterface_IsReleased_fromProcessControl(widgetID, pOutVec, frames);
}

DECL_HOOK(void, CTouchInterface_Clear)
{
	CTouchInterface_Clear();
	TickTouchCombatInput();
}

void ApplyLocalCombatSyncKeys(uint16_t& wKeys, uint16_t& lrAnalog, uint16_t& udAnalog)
{
	// Only inject ButtonPanel directional axes directly during inactive/spectating/
	// dancing/parachuting states; during active on-foot play, CPad::GetPedWalk*
	// already propagates them iff the ped actually consumes movement input.
	bool bAllowDirectAxis = false;
	if (CPlayerPed* pLocal = pGame ? pGame->FindPlayerPed() : nullptr) {
		bAllowDirectAxis = pLocal->IsDancing() || pLocal->IsParachuting();
	}
	if (!bAllowDirectAxis && pNetGame && pNetGame->GetPlayerPool()) {
		if (CLocalPlayer* pNetLocal = pNetGame->GetPlayerPool()->GetLocalPlayer()) {
			bAllowDirectAxis = !pNetLocal->m_bIsActive || pNetLocal->IsSpectating();
		}
	}
	if (bAllowDirectAxis && pUI && pUI->buttonpanel() && !IsLocalActivePedMovementLocked()) {
		if (lrAnalog == 0 && pUI->buttonpanel()->heldLR() != 0) {
			lrAnalog = pUI->buttonpanel()->heldLR();
		}
		if (udAnalog == 0 && pUI->buttonpanel()->heldUD() != 0) {
			udAnalog = pUI->buttonpanel()->heldUD();
		}
	}

	if (IsLocalMeleeComboActive()) {
		wKeys |= (128u | 16u); // KEY_HANDBRAKE | KEY_SECONDARY_ATTACK (FightStyle combo)
	}
	else if (IsLocalMeleeBlockActive()) {
		wKeys |= (128u | 32u); // KEY_HANDBRAKE | KEY_JUMP (Melee block)
	}
	else if (IsLocalMeleeFireActive()) {
		wKeys |= 4u; // KEY_FIRE
	}
}

void InstallTouchCombatHooks()
{
	REG_HOOK_BL(Boot, addr::CPlayerPed_ProcessControl_AttackRelease_BranchBL, "CTouchInterface_IsReleased.ProcessControl",
		&CTouchInterface_IsReleased_fromProcessControl_hook, &CTouchInterface_IsReleased_fromProcessControl,
		"Keep m_bFreeAiming across touch fire release while aim-latched");
	REG_HOOK(Boot, CTouchInterface_Clear, CTouchInterface_Clear, "Touch combat & virtual stick logic-frame sampler");
	REG_HOOK(Boot, CWidget_SetInitialTouchWidget, CWidget_SetInitialTouchWidget, "Protect active left thumbstick & redirect extra touch to look");
	REG_HOOK(Boot, CWidget_SetPinchZoomWidget, CWidget_SetPinchZoomWidget, "Prevent pinch-zoom lock while left thumbstick is held");
	REG_HOOK(Boot, CWidgetRegionMove_OnInitialTouch, CWidgetRegionMove_OnInitialTouch, "Virtual joystick scale (OnInitialTouch)");
	REG_HOOK(Boot, CWidgetRegionMove_Update, CWidgetRegionMove_Update, "Virtual joystick scale (Update)");
	REG_HOOK(Boot, CWidgetRegionMove_ClampInternalTouchAnchor, CWidgetRegionMove_ClampInternalTouchAnchor, "Virtual joystick scale (Anchor)");
	REG_HOOK(Boot, CWidgetRegionMove_Draw, CWidgetRegionMove_Draw, "Virtual joystick scale (Draw)");
	REG_HOOK(Boot, CWidgetRegionMove_IsTouched, CWidgetRegionMove_IsTouched, "Virtual joystick zero-latency live input & scale (IsTouched)");
}
