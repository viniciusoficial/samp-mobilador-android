#pragma once
#include <stdint.h>
#include "common.h"

enum ePadKeys
{
    // KEY_ 						OnFoot 				InCar
    KEY_ACTION = 0, 		// TAB 			| 	ALT GR/LCTRL/NUM0	|	1
    KEY_CROUCH, 			// C 			|	H / CAPSLOCK		| 	2
    KEY_FIRE,				// LCTRL/LMB	|	LALT				| 	4
    KEY_SPRINT,				// SPACE		| 	W 					|	8
    KEY_SECONDARY_ATTACK, 	// ENTER		| 	ENTER 				|	16
    KEY_JUMP,				// LSHIFT		| 	S 					| 	32
    KEY_LOOK_RIGHT,			// 				| 	E 					| 	64
    KEY_HANDBRAKE,			// RMB			|	SPACE 				| 	128
    KEY_LOOK_LEFT,			// 				|	Q 					| 	256
    //bool KEY_LOOK_BEHIND;	// KEY_LOOK_LEFT + KEY_LOOK_RIGHT 		| 	320
    KEY_SUBMISSION,			// NUM1/MMB		| 	2/NUMPAD+ 			| 	512
    KEY_WALK,				// LALT			| 	 					| 	1024
    KEY_ANALOG_UP,			// NUM8			| 	NUM8				|	2048
    KEY_ANALOG_DOWN,		// NUM2 		| 	NUM2				|	4096
    KEY_ANALOG_LEFT,		// NUM4			| 	NUM4				|	8192
    KEY_ANALOG_RIGHT,		// NUM6			| 	NUM6				|	16384
    SIZE,
    KEY_YES,				// Y 			| 	Y 					| 	65536
    KEY_NO,					// N 			| 	N 					| 	131072
    KEY_CTRL_BACK, 			// H 			|   H 					|	262144
};

enum class PcInputAction
{
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

typedef struct
{
    uint16_t wKeyLR;
    uint16_t wKeyUD;
    bool bKeys[ePadKeys::SIZE+4];
    bool bIgnoreJump;
} PAD_KEYS;

VALIDATE_SIZE(PAD_KEYS, 24);

extern PAD_KEYS LocalPlayerKeys;
extern PAD_KEYS RemotePlayerKeys[PLAYER_PED_SLOTS];

void PcInputBeginFrame();
void PcInputEndFrame();
void PcInputSetEnabled(bool enabled);
void PcInputSetExternalInputPresent(bool present);
bool PcInputIsEnabledForTouch();
bool PcInputIsExternalInputPresent();
void PcInputSetAction(int action, bool pressed);
bool PcInputIsPressed(PcInputAction action);
bool PcInputIsJustPressed(PcInputAction action);
bool PcInputOwnsTargeting();
bool PcInputConsumePress(PcInputAction action);
bool PcInputConsumeDriverVehicleEntry();
bool PcInputConsumePassengerVehicleEntry();
int PcInputConsumeWeaponWheel(int direction);
int PcInputPeekWeaponWheel();
int PcInputConsumeWeaponSlot();
void PcInputSelectWeaponSlot(int slot);

void PcInputAddMouseDelta(float dx, float dy);
void PcInputSetMouseOptions(float maxDelta, float smoothing);
void PcInputSetCombatOptions(float aimSensitivity, float strafeSpeed, bool pcSprint, bool fastWeaponSwitch, bool vehicleAutoCenter);
float PcInputGetStrafeMultiplier();
bool PcInputIsPcSprintEnabled();
bool PcInputIsFastWeaponSwitchEnabled();

void PcInputSetMouseButton(int button, int action, bool pressed);
void PcInputMouseLeave();
void PcInputReleaseMouseButtons();
void PcInputOpenChat();
void PcInputHandleTextKey(int key, int unicode);
void PcInputHandleEscape();
bool PcInputIsTextEntryActive();
bool PcInputIsNativeInterfaceActive();
bool PcInputIsTabListOnly();
void PcInputRefreshNativeInterfaceState();
void HookCPad();
