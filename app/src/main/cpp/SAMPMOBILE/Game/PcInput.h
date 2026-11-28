#pragma once

#include <cstdint>

// Porte fiel do sistema de teclado/mouse do SA:MP PC (mobilador) para a 2.10.
// Ordem dos valores == ordem usada em KeyboardMouseSettings.java (ACTION_*).
enum class PcInputAction : int {
	MOVE_FORWARD = 0,
	MOVE_BACKWARD = 1,
	MOVE_LEFT = 2,
	MOVE_RIGHT = 3,
	SPRINT = 4,
	JUMP = 5,
	CROUCH = 6,
	FIRE = 7,
	AIM = 8,
	VEHICLE = 9,
	CONTROL_BACK = 10,
	YES = 11,
	NO = 12,
	WALK = 13,
	TAB = 14,
	ENTER = 15,
	CHAT = 16,
	WEAPON_NEXT = 17,
	WEAPON_PREVIOUS = 18,
	PASSENGER = 19,
	LOOK_BEHIND = 20,
	COUNT = 21
};

// Frame latch: chamado uma vez por frame de lógica (Client::process).
void PcInputBeginFrame();
void PcInputEndFrame();

bool PcInputIsPressed(PcInputAction action);
bool PcInputIsJustPressed(PcInputAction action);
bool PcInputConsumePress(PcInputAction action);

// Estado global
void PcInputSetEnabled(bool enabled);
bool PcInputIsEnabled();
void PcInputSetExternalInputPresent(bool present);
bool PcInputIsExternalInputPresent();
bool PcInputIsEnabledForTouch();
bool PcInputOwnsTargeting();

void PcInputSetAction(int action, bool pressed);

// Armas
int PcInputPeekWeaponWheel();
int PcInputConsumeWeaponWheel(int direction);
int PcInputConsumeWeaponSlot();
void PcInputDropWeaponWheel();
void PcInputSelectWeaponSlot(int slot);

// Mouse / câmera
void PcInputAddMouseDelta(float dx, float dy);
void PcInputSetMouseOptions(float maxDelta, float smoothing);
void PcInputSetCombatOptions(float aimSensitivity, float strafeSpeed, bool pcSprint,
	bool fastWeaponSwitch, bool vehicleAutoCenter);
void PcInputSetMouseButton(int button, int action, bool pressed);
void PcInputReleaseMouseButtons();
void PcInputMouseLeave();
void PcInputHandleWheel(int direction);

float PcInputGetStrafeMultiplier();
bool PcInputIsPcSprintEnabled();
bool PcInputIsFastWeaponSwitchEnabled();

// Interface nativa (chat / placar)
void PcInputOpenChat();
void PcInputHandleTextKey(int key, int unicode);
void PcInputHandleEscape();
bool PcInputIsTextEntryActive();
bool PcInputIsNativeInterfaceActive();
bool PcInputIsTabListOnly();
void PcInputRefreshNativeInterfaceState();
