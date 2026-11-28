#pragma once

#include <cstdint>
#include "Aimstuff.h"

namespace sa {
class CPed;
}
class CPlayerPed;

// Shared internal state across Pad.cpp, TouchCombat.cpp, and VehicleControl.cpp.
extern sa::CPed* dwCurPlayerActor;
extern uint8_t byteInternalPlayer;
extern uint8_t byteCurPlayer;
extern uintptr_t g_lastLocalPadPtr;
extern bool g_bCancelCarTask;

inline uint8_t CurrentPlayerSlot()
{
	return pbyteCurrentPlayer ? *pbyteCurrentPlayer : 0;
}

bool IsPadMovementDisabled(uintptr_t pad);
bool IsLocalActivePedMovementLocked();

// Unified state for touch-screen combat & on-foot movement controls.
// Sampled once per game-logic frame right after CTouchInterface::Clear()
// so every CPad / UseGun hook and outbound sync packet reads consistent,
// zero-lag state without starving WIDGET_PED_MOVE's frame counter.
struct TouchCombatState {
	bool aimLatch = false;
	uint8_t lastAimWeapon = 0;
	bool onFootStickSampled = false;
	bool stickTouched = false;
	int16_t stickLR = 0;
	int16_t stickUD = 0;
	bool fireDown = false;
	bool firePrevDown = false;
	bool fireJustPressed = false;
	uint32_t fireDownTickMs = 0;
	bool blockDown = false;
	uint32_t meleeComboUntilMs = 0;
	uint32_t meleeFireUntilMs = 0;
};

extern TouchCombatState g_touchCombat;

bool IsLocalMeleeComboActive();
bool IsLocalMeleeFireActive();
bool IsLocalMeleeBlockActive();
bool CanLocalPedUseOnFootFire(uintptr_t pad, sa::CPed* pPed);
bool EvaluateLocalTouchFireLevel(uint8_t weaponType);
CPlayerPed* GetAimableLocalPed();
CPlayerPed* GetBlockableLocalPed();
bool Using1stPersonWeaponCam();
bool EngineIsAiming(CPlayerPed* pPed);
