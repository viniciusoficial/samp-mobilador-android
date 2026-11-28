#pragma once

#include <cstdint>

void UpdateTouchCombatVisuals();
void RunWithUnscaledJoystickSx(uintptr_t widget, void (*fn)(uintptr_t));
void ApplyLocalCombatSyncKeys(uint16_t& wKeys, uint16_t& lrAnalog, uint16_t& udAnalog);
bool ConsumeStuffUseRequest();
bool ConsumePassengerDriveByToggle();
void InstallTouchCombatHooks();
