#include "TouchWidgets.h"
#include "Common.h"
#include "Game.h"
#include "PlayerPed.h"
#include "Scripting.h"
#include "Util.h"
#include "Vehicle.h"
#include "Engine/Symbols.h"
#include "../Net/NetGame.h"
#include "../Net/VehiclePool.h"
#include "../Client.h"
#include "../InputArbiter.h"
#include "../Settings.h"
#include <SA.h>
#include <spdlog/spdlog.h>

// widget fix
uintptr_t* g_pWidgets;

uintptr_t* GetTouchWidgets()
{
	if (uintptr_t* liveWidgets = eng::live::touchWidgets()) {
		g_pWidgets = liveWidgets;
	}
	return g_pWidgets;
}

sa::WidgetIDs GetWidgetTypeFromWidget(uintptr_t pWidget)
{
	if (!pWidget) return static_cast<sa::WidgetIDs>(-1);
	uintptr_t* widgets = GetTouchWidgets();
	if (!widgets) return static_cast<sa::WidgetIDs>(-1);

	for (int i = 0; i < sa::NUM_WIDGETS; i++) {
		if (widgets[i] && pWidget == widgets[i]) {
			return static_cast<sa::WidgetIDs>(i);
		}
	}

	return static_cast<sa::WidgetIDs>(-1);
}

eWidgetState ProcessFixedWidget(uintptr_t pWidget)
{
	if (!pGame) return STATE_NONE;
	CPlayerPed* pPlayerPed = pGame->FindPlayerPed();
	if (!pPlayerPed) return STATE_NONE;

	sa::WidgetIDs widgetType = GetWidgetTypeFromWidget(pWidget);
	switch (widgetType) {
		case -1: return STATE_NONE;
			break;

		case sa::WidgetIDs::WIDGET_RADAR:
			if (!Settings::radarTapMap() || !Settings::showRadar() || !Settings::showGtaHud()
					|| InputArbiter::isRadarSuppressed()) {
				return STATE_DISABLED;
			}
			break;

		case sa::WidgetIDs::WIDGET_BUTTON_SWIM:
		case sa::WidgetIDs::WIDGET_BUTTON_DIVE:
			if (!ScriptCommand(&is_actor_in_the_water, pPlayerPed->m_gtaId)) {
				return STATE_DISABLED;
			}
			break;

		case sa::WidgetIDs::WIDGET_ATTACK:
			if (pPlayerPed->IsInPassengerDriveByMode()) {
				return STATE_NONE;
			}
			else if (pPlayerPed->IsInVehicle() && !pPlayerPed->IsAPassenger()) {
				return STATE_DISABLED;
			}
			break;

		case sa::WidgetIDs::WIDGET_BUTTON_SPRINT:
		case sa::WidgetIDs::WIDGET_SPRINT:
			if (pPlayerPed->IsInVehicle() || pPlayerPed->IsInJetpack() || ScriptCommand(&is_actor_in_the_water, pPlayerPed->m_gtaId)) {
				return STATE_DISABLED;
			}
			break;

		case sa::WidgetIDs::WIDGET_HORN:
		case sa::WidgetIDs::WIDGET_NITRO:
			if (!pPlayerPed->IsInVehicle()) {
				return STATE_DISABLED;
			}
			break;

		case sa::WidgetIDs::WIDGET_VEHICLE_SHOOT_LEFT:
		case sa::WidgetIDs::WIDGET_VEHICLE_SHOOT_RIGHT:
			if (!pPlayerPed->IsInVehicle() || pPlayerPed->IsAPassenger()) {
				return STATE_DISABLED;
			}
			break;

		case sa::WidgetIDs::WIDGET_ROCKET:
			if (pNetGame) {
				if (!pPlayerPed->IsInVehicle()) {
					return STATE_DISABLED;
				}
				else {
					if (!pPlayerPed->GetGtaVehicle()) return STATE_NONE;
					int model = pPlayerPed->GetGtaVehicle()->m_nModelIndex;
					if (pPlayerPed->IsAPassenger()) {
						return STATE_DISABLED;
					}
					// if is armed vehicle
					switch (model) {
						// Firetruck
						case 407:
							// hunter
						case 425:
							// Rhino Tank
						case 432:
							// Seasparrow
						case 447:
							// RC Baron
						case 464:
							// RC Raider
						case 465:
							// Rustler
						case 476:
							// Hydra
						case 520: return STATE_NONE;
							// block other non-armed vehicle enable shoot button
						default: return STATE_DISABLED;
					}
				}
			}
			break;

		case sa::WidgetIDs::WIDGET_DROP_CRANE:
		case sa::WidgetIDs::WIDGET_CRANE_UP:
		case sa::WidgetIDs::WIDGET_CRANE_DOWN:
			if (!pPlayerPed->IsInVehicle()) {
				return STATE_DISABLED;
			}
			break;

		case sa::WidgetIDs::WIDGET_ACCELERATE:
		case sa::WidgetIDs::WIDGET_BRAKE:
			if (!pPlayerPed->IsInVehicle() && !pPlayerPed->IsInJetpack()) {
				return STATE_DISABLED;
			}
			break;

		case sa::WidgetIDs::WIDGET_ENTER_CAR:
			if (pPlayerPed->IsInJetpack()) {
				return STATE_NONE;
			}

			if (pNetGame) {
				CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
				if (pVehiclePool) {
					// Get the vehicles near the player
					VEHICLEID vehicleId = pVehiclePool->FindNearestToLocalPlayerPed();
					if (vehicleId == INVALID_VEHICLE_ID) {
						return STATE_DISABLED;
					}
					else {
						CVehicle* pVehicle = pVehiclePool->GetAt(vehicleId);
						if (pVehicle) {
							// player is not in the car and the distance between the vehicle and the player > 10
							if (!pPlayerPed->IsInVehicle() && pVehicle->GetDistanceFromLocalPlayerPed() > 10.0f) {
								// Disable button display
								return STATE_DISABLED;
							}
						}
					}
				}
			}
			break;
	}

	return STATE_NONE;
}

// CGame::Shutdown -> DeleteAll wipes the widget array without resetting the
// engine's bInitWidgets flag, so Menu_SwitchOffToGame() keeps skipping
// CreateAll afterwards — recreate directly when the array is empty.
// the enter/exit-targeting widgets share the attack button's slot
// (posIdx TARGETING_AND_ATTACK, 607x310): stock only ever showed them for
// scoped weapons, so the overlap with the fire button was invisible. Now
// that every aimable weapon gets the aim button, move both to (553,310) —
// VC_SHOOT's slot, which is in-vehicle only. Their flags bit0 makes the
// hit-test circular, so rewriting +24/+28 moves the touch area with it.
// CWidget field offsets verified against arm32 (Thumb-2) and arm64 libGTASA.so:
// arm32: vptr(0) + mapping(4) + sprite(8) + WidgetPos(12..27) + CRect(32..47)
//        + alpha(76) + enabled(77) + boundFinger(120) + holdCount(124) + flags(128)
// arm64: vptr(0) + mapping(8) + sprite(16) + WidgetPos(24..39) + CRect(44..59)
//        + alpha(88) + enabled(89) + boundFinger(132) + holdCount(136) + flags(140)
static constexpr uintptr_t kWidgetPosOffset         = VER_x32 ? 12  : 24;
static constexpr uintptr_t kWidgetPosYOffset        = VER_x32 ? 16  : 28;
static constexpr uintptr_t kWidgetBoundFingerOffset = VER_x32 ? 120 : 132;
static constexpr uintptr_t kWidgetHoldCountOffset   = VER_x32 ? 124 : 136;

void FixTouchWidgetLayout()
{
	uintptr_t* widgets = GetTouchWidgets();
	if (!widgets) return;
	// WidgetPosition(16B) + padding/CRect/scale/margins = 56B block starting at kWidgetPosOffset
	uintptr_t donor = widgets[sa::WidgetIDs::WIDGET_VC_SHOOT];
	for (int32_t id : {sa::WidgetIDs::WIDGET_ENTER_TARGETING, sa::WidgetIDs::WIDGET_EXIT_TARGETING}) {
		uintptr_t w = widgets[id];
		if (!w) continue;
		if (donor) {
			memcpy(reinterpret_cast<void*>(w + kWidgetPosOffset),
				reinterpret_cast<void*>(donor + kWidgetPosOffset), 56);
		}
		else {
			*reinterpret_cast<float*>(w + kWidgetPosOffset) = 553.0f;
			*reinterpret_cast<float*>(w + kWidgetPosYOffset) = 310.0f;
		}
	}
	// custom combat widgets have no posIdx entry in the stock layout table
	uintptr_t wAim = widgets[sa::WidgetIDs::WIDGET_CAR_CHEATS];
	if (wAim) {
		*reinterpret_cast<float*>(wAim + kWidgetPosOffset) = 490.0f;
		*reinterpret_cast<float*>(wAim + kWidgetPosYOffset) = 235.0f;
	}
	uintptr_t wBlk = widgets[sa::WidgetIDs::WIDGET_MISSION_SKIP];
	if (wBlk) {
		*reinterpret_cast<float*>(wBlk + kWidgetPosOffset) = 490.0f;
		*reinterpret_cast<float*>(wBlk + kWidgetPosYOffset) = 310.0f;
	}
}

void EnsureTouchWidgetsCreated()
{
	uintptr_t* widgets = GetTouchWidgets();
	if (!widgets) return;
	if (!widgets[sa::WidgetIDs::WIDGET_ATTACK]) {
		spdlog::info("Touch widgets missing, forcing CreateAll");
		eng::fn::CTouchInterface_CreateAll();
		widgets = GetTouchWidgets();
		if (!widgets) return;
	}
	// resolve hit-test rects (Adjustable.cfg is absent on fresh installs)
	eng::fn::CTouchInterface_RepositionAdjustableWidgets();
	FixTouchWidgetLayout();

	// widget 0x20 (crouch) is never instantiated by the engine —
	// SetupLayoutObjects clears the slot and recreates accel/swim/sprint only —
	// but GetDuck/DuckJustDown poll IsReleased(0x20) whenever it exists, so a
	// plain CWidgetButton with the stock "hud_crouch" texture is all it takes.
	if (!widgets[sa::WidgetIDs::WIDGET_BUTTON_CROUCH]) {
		// clone a neighbour button's 640x448-space {ox,oy,sx,sy} and move it
		// into the on-foot-free slot left of sprint (brake's x, in-vehicle only)
		float pos[4] = {541.0f, 381.0f, 40.0f, 0.0f};
		uintptr_t donor = widgets[sa::WidgetIDs::WIDGET_BUTTON_SPRINT];
		if (!donor) donor = widgets[sa::WidgetIDs::WIDGET_ATTACK];
		if (donor) {
			memcpy(pos, reinterpret_cast<void*>(donor + kWidgetPosOffset), sizeof(pos));
			pos[0] = 541.0f;
		}
		void* mem = ::operator new(200);
		if (mem) {
			memset(mem, 0, 200);
			eng::fn::CWidgetButton_CWidgetButton(
				mem, "hud_crouch", pos, 1u, 0x40u, 4u);
			widgets[sa::WidgetIDs::WIDGET_BUTTON_CROUCH] = reinterpret_cast<uintptr_t>(mem);
			eng::fn::CWidget_Update( reinterpret_cast<uintptr_t>(mem));
			spdlog::info("crouch button widget created pos=({},{},{},{})",
				pos[0], pos[1], pos[2], pos[3]);
		}
	}
	// custom aim-toggle button in slot 0x98 (WIDGET_CAR_CHEATS): CreateAll never
	// instantiates 0x97..0x9C. Placed at (490,235) clear of attack (607,310)
	// and VC_SHOOT (553,310).
	if (!widgets[sa::WidgetIDs::WIDGET_CAR_CHEATS]) {
		float pos[4] = {490.0f, 235.0f, 40.0f, 0.0f};
		uintptr_t donor = widgets[sa::WidgetIDs::WIDGET_VC_SHOOT];
		if (!donor) donor = widgets[sa::WidgetIDs::WIDGET_ATTACK];
		if (donor) {
			memcpy(pos, reinterpret_cast<void*>(donor + kWidgetPosOffset), sizeof(pos));
			pos[0] = 490.0f;
			pos[1] = 235.0f;
		}
		void* mem = ::operator new(200);
		if (mem) {
			memset(mem, 0, 200);
			eng::fn::CWidgetButton_CWidgetButton(
				mem, "hud_lockon", pos, 1u, 0u, 36u);
			widgets[sa::WidgetIDs::WIDGET_CAR_CHEATS] = reinterpret_cast<uintptr_t>(mem);
			eng::fn::CWidget_Update( reinterpret_cast<uintptr_t>(mem));
			spdlog::info("aim button widget created pos=({},{},{},{})",
				pos[0], pos[1], pos[2], pos[3]);
		}
	}
	// block button in slot 0x99 (WIDGET_MISSION_SKIP): CreateAll never fills
	// it — mission-skip only exists in SP missions SAMP doesn't run — so the
	// slot is free like 0x20. Sits directly below the aim button (490,235).
	if (!widgets[sa::WidgetIDs::WIDGET_MISSION_SKIP]) {
		float pos[4] = {490.0f, 310.0f, 40.0f, 0.0f};
		uintptr_t donor = widgets[sa::WidgetIDs::WIDGET_ATTACK];
		if (!donor) donor = widgets[sa::WidgetIDs::WIDGET_VC_SHOOT];
		if (donor) {
			memcpy(pos, reinterpret_cast<void*>(donor + kWidgetPosOffset), sizeof(pos));
			pos[0] = 490.0f;
			pos[1] = 310.0f;
		}
		void* mem = ::operator new(200);
		if (mem) {
			memset(mem, 0, 200);
			eng::fn::CWidgetButton_CWidgetButton(
				mem, "punch", pos, 1u, 0u, 4u);
			widgets[sa::WidgetIDs::WIDGET_MISSION_SKIP] = reinterpret_cast<uintptr_t>(mem);
			eng::fn::CWidget_Update( reinterpret_cast<uintptr_t>(mem));
			spdlog::info("block button widget created pos=({},{},{},{})",
				pos[0], pos[1], pos[2], pos[3]);
		}
	}
	// Reset cached touch slot and hold frame counter to CWidget::CWidget's safe
	// defaults (0, not -1). Writing -1 causes out-of-bounds reads in
	// LIB_PointerGetButton, CWidget::IsTouched, and CWidgetRegionMove::Update.
	for (int i = 0; i < sa::WidgetIDs::NUM_WIDGETS; ++i) {
		uintptr_t w = widgets[i];
		if (!w) continue;
		*reinterpret_cast<int32_t*>(w + kWidgetBoundFingerOffset) = 0;
		*reinterpret_cast<int32_t*>(w + kWidgetHoldCountOffset) = 0;
	}
}
