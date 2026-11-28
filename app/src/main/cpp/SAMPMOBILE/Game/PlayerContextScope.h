#pragma once

#include <cstdint>
#include <cstring>
#include "Aimstuff.h"
#include "../GameSA/include/Camera.h"

namespace sa {
CCamera& getTheCamera();
}

// RAII scope that temporarily switches the engine's global local-player state
// (active camera index, camera mode, weapon camera mode, zoom/aspect, aim vectors,
// PlayerInFocus) to a remote player slot, and restores the saved state on scope exit.
//
// GTA:SA's CCamera::StartTransition toggles TheCamera.m_nActiveCam between 0 and 1
// on every camera switch, while CTaskSimpleUseGun::ProcessPed reads m_aCams[0].m_nMode
// directly and Find3rdPersonCamTargetVector / ProcessPlayerWeapon read m_aCams[m_nActiveCam].
// Pinning m_nActiveCam = 0 inside remote scopes unifies both paths onto m_aCams[0].
class ScopedPlayerContext {
public:
	enum Flags : uint32_t {
		kNone       = 0u,
		kCamMode    = 1u << 0,
		kZoomAspect = 1u << 1,
		kAim        = 1u << 2,
		kFocus      = 1u << 3,
		kSkills     = 1u << 4, // T08 weapon skill levels

		// Presets matching the four existing switching forms
		kFull          = kCamMode | kZoomAspect | kAim | kFocus | kSkills, // CPed_ProcessControl, CTaskSimpleUseGun_SetPedPosition
		kFireInstant   = kCamMode | kZoomAspect | kAim | kSkills,          // CPlayerPed::FireInstant (no PlayerInFocus switch)
		kVehicle       = kCamMode | kAim | kFocus,               // Combined vehicle context
		kVehicleCamAim = kCamMode | kAim,                        // Vehicle outer scope (focus switches after AudioService)
		kFocusOnly     = kFocus,                                 // PlayerPed focus-only sites & vehicle inner scope
	};

	ScopedPlayerContext(uint8_t playerNum, uint32_t flags, bool active = true)
		: flags_(active ? flags : kNone)
	{
		if (flags_ == kNone) {
			return;
		}

		if (flags_ & (kCamMode | kZoomAspect | kAim)) {
			sa::CCamera& cam = sa::getTheCamera();
			savedActiveCam_ = cam.m_nActiveCam;
			cam.m_nActiveCam = 0;
			savedActiveCamValid_ = true;
		}

		if (flags_ & kCamMode) {
			const uint8_t remoteMode = GameResolveRemoteEffectiveCameraMode(playerNum);
			if (pbyteCameraMode) {
				sa::CCamera& cam = sa::getTheCamera();
				savedCameraMode_ = cam.m_aCams[0].m_nMode;
				cam.m_aCams[0].m_nMode = static_cast<sa::eCamMode>(remoteMode);
				savedCamMode1_ = true;
			}
			if (wCameraMode2) {
				// Save full 12-byte sa::CQueuedMode (m_nMode, pad, m_fDuration, m_nMinZoom, m_nMaxZoom)
				// so remote ProcessPlayerWeapon -> SetNewPlayerWeaponMode cannot clobber local sniper zoom fields.
				std::memcpy(savedWeaponMode_, wCameraMode2, sizeof(savedWeaponMode_));
				*wCameraMode2 = (remoteMode == 4) ? 0 : remoteMode;
				savedCamMode2_ = true;
			}
		}

		if (flags_ & kZoomAspect) {
			if (pfCameraExtZoom && pfAspectRatio) {
				savedExtZoom_ = *pfCameraExtZoom;
				savedAspectRatio_ = *pfAspectRatio;
				savedZoomAspect_ = true;
				GameSetRemotePlayerCameraExtZoomAndAspect(playerNum);
			}
		}

		if (flags_ & kAim) {
			sa::CCamera& cam = sa::getTheCamera();
			auto* cam0Aim = reinterpret_cast<CAMERA_AIM*>(&cam.m_aCams[0].Front);
			std::memcpy(&savedAim_, cam0Aim, sizeof(CAMERA_AIM));
			savedAimValid_ = true;
			GameSetRemotePlayerAim(playerNum);
		}

		if (flags_ & kSkills) {
			if (float* stats = GameGetWeaponSkillStatsPtr()) {
				std::memcpy(savedSkills_, stats, sizeof(savedSkills_));
				savedSkillsValid_ = true;
				GameApplyRemotePlayerSkills(playerNum);
			}
		}

		if (flags_ & kFocus) {
			if (pbyteCurrentPlayer) {
				savedCurrentPlayer_ = *pbyteCurrentPlayer;
				*pbyteCurrentPlayer = playerNum;
				switchedFocus_ = true;
			}
		}
	}

	~ScopedPlayerContext()
	{
		if (flags_ == kNone) {
			return;
		}

		if (switchedFocus_ && pbyteCurrentPlayer) {
			*pbyteCurrentPlayer = savedCurrentPlayer_;
		}

		if (savedSkillsValid_) {
			if (float* stats = GameGetWeaponSkillStatsPtr()) {
				std::memcpy(stats, savedSkills_, sizeof(savedSkills_));
			}
		}

		if (savedCamMode1_ && pbyteCameraMode) {
			sa::CCamera& cam = sa::getTheCamera();
			cam.m_aCams[0].m_nMode = static_cast<sa::eCamMode>(savedCameraMode_);
		}
		if (savedCamMode2_ && wCameraMode2) {
			std::memcpy(wCameraMode2, savedWeaponMode_, sizeof(savedWeaponMode_));
		}

		if (savedZoomAspect_ && pfCameraExtZoom && pfAspectRatio) {
			*pfCameraExtZoom = savedExtZoom_;
			*pfAspectRatio = savedAspectRatio_;
		}

		if (savedAimValid_) {
			sa::CCamera& cam = sa::getTheCamera();
			auto* cam0Aim = reinterpret_cast<CAMERA_AIM*>(&cam.m_aCams[0].Front);
			std::memcpy(cam0Aim, &savedAim_, sizeof(CAMERA_AIM));
		}

		if (savedActiveCamValid_) {
			sa::CCamera& cam = sa::getTheCamera();
			cam.m_nActiveCam = savedActiveCam_;
		}
	}

	ScopedPlayerContext(const ScopedPlayerContext&) = delete;
	ScopedPlayerContext& operator=(const ScopedPlayerContext&) = delete;
	ScopedPlayerContext(ScopedPlayerContext&&) = delete;
	ScopedPlayerContext& operator=(ScopedPlayerContext&&) = delete;

private:
	uint32_t flags_ = kNone;
	uint16_t savedCameraMode_ = 0;
	uint8_t savedActiveCam_ = 0;
	uint8_t savedWeaponMode_[12]{};
	uint8_t savedCurrentPlayer_ = 0;
	float savedExtZoom_ = 0.0f;
	float savedAspectRatio_ = 0.0f;
	CAMERA_AIM savedAim_{};
	float savedSkills_[kWeaponSkillCount]{};
	bool savedActiveCamValid_ = false;
	bool savedCamMode1_ = false;
	bool savedCamMode2_ = false;
	bool savedZoomAspect_ = false;
	bool savedAimValid_ = false;
	bool savedSkillsValid_ = false;
	bool switchedFocus_ = false;
};
