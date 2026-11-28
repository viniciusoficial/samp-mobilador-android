#pragma once

#include <cstdint>

void UpdateTouchCombatVisuals();
void RunWithUnscaledJoystickSx(uintptr_t widget, void (*fn)(uintptr_t));
void ApplyLocalCombatSyncKeys(uint16_t& wKeys, uint16_t& lrAnalog, uint16_t& udAnalog);
bool ConsumeStuffUseRequest();
bool ConsumePassengerDriveByToggle();
void InstallTouchCombatHooks();

// Entrada de mouse/teclado PC (PcInput): atirar e mirar entram no mesmo
// pipeline de combate usado pelos widgets de toque.
void TouchCombatSetExternalFire(bool down, bool justPressed);
void TouchCombatSetExternalAim(bool down);
