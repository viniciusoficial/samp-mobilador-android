#include "Aimstuff.h"
#include "Game.h"
#include "Pad.h"
#include "Patches.h"
#include "PlayerPed.h"
#include "Engine/Symbols.h"
#include "../Client.h"
#include <SA.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <cstring>

CAMERA_AIM* pcaInternalAim = nullptr;
uint8_t* pbyteCameraMode = nullptr;
float* pfCameraExtZoom = nullptr;
float* pfAspectRatio = nullptr;
uint16_t* wCameraMode2 = nullptr;

uint8_t* pbyteCurrentPlayer = nullptr;

CAMERA_AIM caLocalPlayerAim;
CAMERA_AIM caRemotePlayerAim[PLAYER_PED_SLOTS];
uint8_t byteCameraMode[PLAYER_PED_SLOTS];
float fCameraExtZoom[PLAYER_PED_SLOTS];
float fCameraAspectRatio[PLAYER_PED_SLOTS];
float fLocalCameraExtZoom;
float fLocalAspectRatio;
float fLocalPlayerSkills[kWeaponSkillCount];
float fRemotePlayerSkills[PLAYER_PED_SLOTS][kWeaponSkillCount];

extern PAD_KEYS RemotePlayerKeys[PLAYER_PED_SLOTS];

static inline uint8_t GameGetActiveCamIndex()
{
	sa::CCamera& cam = sa::getTheCamera();
	return (cam.m_nActiveCam < 3) ? cam.m_nActiveCam : 0;
}

/**
 * @return
 */
CAMERA_AIM* GameGetInternalAim()
{
	if (!pcaInternalAim) {
		return nullptr;
	}
	sa::CCamera& cam = sa::getTheCamera();
	return reinterpret_cast<CAMERA_AIM*>(&cam.m_aCams[GameGetActiveCamIndex()].Front);
}

/**
 * @return
 */
uint8_t GameGetLocalPlayerCameraMode()
{
	if (!pbyteCameraMode) {
		return 4;
	}
	sa::CCamera& cam = sa::getTheCamera();
	uint16_t mode = cam.m_aCams[GameGetActiveCamIndex()].m_nMode;
	if ((mode == 0 || mode == 4) && cam.PlayerWeaponMode.m_nMode != 0) {
		mode = cam.PlayerWeaponMode.m_nMode;
	}
	return static_cast<uint8_t>(mode);
}

/**
 * @param bytePlayerNumber
 * @return
 */
uint8_t GameGetPlayerCameraMode(uint8_t bytePlayerNumber)
{
	return bytePlayerNumber < PLAYER_PED_SLOTS ? byteCameraMode[bytePlayerNumber] : 4;
}

uint8_t GameResolveRemoteEffectiveCameraMode(uint8_t bytePlayerNumber)
{
	uint8_t mode = GameGetPlayerCameraMode(bytePlayerNumber);
	if (bytePlayerNumber == 0 || bytePlayerNumber >= PLAYER_PED_SLOTS || !WORLD_PLAYERS) {
		return mode;
	}

	sa::CPed* ped = WORLD_PLAYERS[bytePlayerNumber].m_pPed;
	if (!ped || ped->m_nPedFlags.bInVehicle) {
		return mode;
	}

	const PAD_KEYS& rk = RemotePlayerKeys[bytePlayerNumber];
	const bool bAimKey = rk.bKeys[ePadKeys::KEY_HANDBRAKE];
	const bool bFireKey = rk.bKeys[ePadKeys::KEY_FIRE];

	uint8_t weapon = 0;
	if (ped->m_nActiveWeaponSlot < 13) {
		weapon = static_cast<uint8_t>(ped->m_aWeapons[ped->m_nActiveWeaponSlot].dwType);
	}

	if (bAimKey) {
		switch (weapon) {
			case sa::eWeaponType::WEAPONTYPE_SNIPERRIFLE: return 7;
			case sa::eWeaponType::WEAPONTYPE_RLAUNCHER:   return 8;
			case sa::eWeaponType::WEAPONTYPE_RLAUNCHER_HS:return 51;
			case sa::eWeaponType::WEAPONTYPE_CAMERA:      return 46;
			default: break;
		}
		if ((weapon >= sa::eWeaponType::WEAPONTYPE_PISTOL && weapon <= sa::eWeaponType::WEAPONTYPE_MINIGUN)
				|| weapon == sa::eWeaponType::WEAPONTYPE_SPRAYCAN
				|| weapon == sa::eWeaponType::WEAPONTYPE_EXTINGUISHER) {
			if (mode == 0 || mode == 4) {
				return 53; // MODE_AIMWEAPON before first AimSync arrives
			}
		}
	}
	else if (!bFireKey) {
		if (mode == 7 || mode == 8 || mode == 46 || mode == 51 || mode == 53 || mode == 55) {
			return 4; // MODE_FOLLOWPED once remote player releases aim/fire
		}
	}

	return mode;
}

float GameGetLocalPlayerCameraExtZoom()
{
	if (!pfCameraExtZoom) return 0.0f;
	sa::CCamera& cam = sa::getTheCamera();
	const float fov = cam.m_aCams[GameGetActiveCamIndex()].FOV;
	return std::clamp((fov - 35.0f) * 0.028571429f, 0.0f, 1.0f);
}

float GameGetAspectRatio()
{
	// SA-MP wire protocol encodes (aspectRatio - 1.0f) in [0.0f, 1.0f] -> uint8 [0..255]
	// (open.mp decodes as aspectRatio = (byte / 255.0f) + 1.0f).
	if (!pfAspectRatio) return 0.0f;
	return std::clamp(*pfAspectRatio - 1.0f, 0.0f, 1.0f);
}

void GameAimSyncInit()
{
	memset(&caLocalPlayerAim, 0, sizeof(caLocalPlayerAim));
	memset(&caRemotePlayerAim, 0, sizeof(caRemotePlayerAim));
	memset(byteCameraMode, 4, sizeof(byteCameraMode));

	for (int i = 0; i < PLAYER_PED_SLOTS; ++i) {
		fCameraAspectRatio[i] = 0.333333f;
		fCameraExtZoom[i] = 1.0f;
		for (int s = 0; s < kWeaponSkillCount; ++s) {
			fRemotePlayerSkills[i][s] = 999.0f;
		}
	}
	for (int s = 0; s < kWeaponSkillCount; ++s) {
		fLocalPlayerSkills[s] = 999.0f;
	}

	sa::CCamera& TheCamera = sa::getTheCamera();

//    sa::CCamera: 951FA8 TheCamera.Cams[0]: 952118
	pcaInternalAim = (CAMERA_AIM*) &TheCamera.m_aCams[0].Front; // +2D8(728) TheCamera.Cams[0].Front
	pbyteCameraMode = (uint8_t*) &TheCamera.m_aCams[0].m_nMode; //+17E(382) TheCamera.Cams[0].m_nMode
	pfAspectRatio = eng::as<float*>(eng::Sym::CDraw_ms_fAspectRatio); // _ZN5CDraw15ms_fAspectRatioE
	pfCameraExtZoom = &TheCamera.m_aCams[0].FOV; // TheCamera+368 +140 TheCamera.Cams[0].m_fFOV
	wCameraMode2 = (uint16_t*) &TheCamera.PlayerWeaponMode; // TheCamera.PlayerWeaponMode

	pbyteCurrentPlayer = eng::as<uint8_t*>(eng::Sym::CWorld_PlayerInFocus); // _ZN6CWorld13PlayerInFocusE
}

CAMERA_AIM* GameGetRemotePlayerAim(uint8_t bytePlayerNumber)
{
	return bytePlayerNumber < PLAYER_PED_SLOTS ? &caRemotePlayerAim[bytePlayerNumber] : &caRemotePlayerAim[0];
}

void GameStoreLocalPlayerCameraExtZoomAndAspect()
{
	if (!pfCameraExtZoom || !pfAspectRatio) return;
	fLocalCameraExtZoom = *pfCameraExtZoom;
	fLocalAspectRatio = *pfAspectRatio;
}

void GameSetRemotePlayerCameraExtZoomAndAspect(uint8_t bytePlayerNumber)
{
	if (!pfCameraExtZoom || !pfAspectRatio || bytePlayerNumber >= PLAYER_PED_SLOTS) return;
	*pfCameraExtZoom = fCameraExtZoom[bytePlayerNumber] * 35.0f + 35.0f;
	*pfAspectRatio = fCameraAspectRatio[bytePlayerNumber] + 1.0f;
}

void GameStoreLocalPlayerAim()
{
	if (!pcaInternalAim) return;
	memcpy(&caLocalPlayerAim, pcaInternalAim, sizeof(CAMERA_AIM));
}

void GameSetRemotePlayerAim(uint8_t bytePlayerNumber)
{
	if (!pcaInternalAim || bytePlayerNumber >= PLAYER_PED_SLOTS) return;
	memcpy(pcaInternalAim, &caRemotePlayerAim[bytePlayerNumber], sizeof(CAMERA_AIM));
}

void GameSetLocalPlayerCameraExtZoomAndAspect()
{
	if (!pfCameraExtZoom || !pfAspectRatio) return;
	*pfCameraExtZoom = fLocalCameraExtZoom;
	*pfAspectRatio = fLocalAspectRatio;
}

void GameSetLocalPlayerAim()
{
	if (!pcaInternalAim) return;
	memcpy(pcaInternalAim, &caLocalPlayerAim, sizeof(CAMERA_AIM));
}

/**
 * @param byteMode
 * @param bytePlayerNumber
 */
void GameSetPlayerCameraMode(uint8_t byteMode, uint8_t bytePlayerNumber)
{
	if (bytePlayerNumber >= PLAYER_PED_SLOTS) return;
	byteCameraMode[bytePlayerNumber] = byteMode;
}

/**
 * @param vec1
 * @param vec2
 */
void calculateAimVector(sa::CVector* vec1, sa::CVector* vec2)
{
	float f1, f2, f3;

	f1 = atan2(vec1->x, vec1->y) - 1.570796370506287; // PI/2
	f2 = sin(f1);
	f3 = cos(f1);
	vec2->x = vec1->y * 0.0 - f3 * vec1->z;
	vec2->y = f2 * vec1->z - vec1->x * 0.0;
	vec2->z = f3 * vec1->x - f2 * vec1->y;
}

/**
 * @param bytePlayerNumber
 * @param pAim
 */
void GameStoreRemotePlayerAim(uint8_t bytePlayerNumber, CAMERA_AIM* pAim)
{
	if (!pAim || bytePlayerNumber >= PLAYER_PED_SLOTS) return;
	memcpy(&caRemotePlayerAim[bytePlayerNumber], pAim, sizeof(CAMERA_AIM));
}

/**
 * @param bytePlayerNumber
 * @param fExtZoom
 * @param fAspectRatio
 */
void GameSetPlayerCameraExtZoomAndAspect(uint8_t bytePlayerNumber, float fExtZoom, float fAspectRatio)
{
	if (bytePlayerNumber >= PLAYER_PED_SLOTS) return;
	fCameraExtZoom[bytePlayerNumber] = fExtZoom;
	fCameraAspectRatio[bytePlayerNumber] = fAspectRatio;
}

float* GameGetWeaponSkillStatsPtr()
{
	auto* stats = eng::as<float*>(eng::Sym::CStats_StatTypesFloat);
	return stats ? (stats + kWeaponSkillStatBase) : nullptr;
}

void GameSetLocalPlayerSkill(int skill, uint16_t level)
{
	if (skill < 0 || skill >= kWeaponSkillCount) return;
	const float val = static_cast<float>(level);
	fLocalPlayerSkills[skill] = val;
	if (float* stats = GameGetWeaponSkillStatsPtr()) {
		stats[skill] = val;
	}
}

void GameSetRemotePlayerSkill(uint8_t bytePlayerNumber, int skill, uint16_t level)
{
	if (bytePlayerNumber >= PLAYER_PED_SLOTS || skill < 0 || skill >= kWeaponSkillCount) return;
	fRemotePlayerSkills[bytePlayerNumber][skill] = static_cast<float>(level);
}

void GameApplyRemotePlayerSkills(uint8_t bytePlayerNumber)
{
	if (bytePlayerNumber >= PLAYER_PED_SLOTS) return;
	if (float* stats = GameGetWeaponSkillStatsPtr()) {
		memcpy(stats, fRemotePlayerSkills[bytePlayerNumber], sizeof(float) * kWeaponSkillCount);
	}
}

void GameResetLocalPlayerSkills()
{
	float* stats = GameGetWeaponSkillStatsPtr();
	for (int s = 0; s < kWeaponSkillCount; ++s) {
		fLocalPlayerSkills[s] = 999.0f;
		if (stats) {
			stats[s] = 999.0f;
		}
	}
}
