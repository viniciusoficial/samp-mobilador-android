#include "NativeCameraMouse.h"

#include "Pad.h"
#include "Util.h"
#include "Engine/PatchRegistry.h"
#include "Engine/Symbols.h"
#include "../Settings.h"

#include <SA.h>
#include <Entity/Ped/Ped.h>
#include <Collision/ColPoint.h>
#include <Memory.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>

namespace
{
	constexpr float kPi     = 3.14159265358979323846f;
	constexpr float kTwoPi  = 6.28318530717958647692f;
	constexpr float kHalfPi = 1.57079632679489661923f;

	// Mouse radians per input count (same tuning as the mobilador build).
	constexpr float kBaseSensH     = 0.0028f;
	constexpr float kBaseSensV     = 0.0024f;
	constexpr float kMaxAccumDelta = 65536.0f;

	// Vertical pitch bounds (radians)
	constexpr float kMinOrbitAlpha = -1.05f;
	constexpr float kMaxOrbitAlpha =  0.72f;
	constexpr float kMinAimAlpha   = -1.32f;
	constexpr float kMaxAimAlpha   =  1.32f;

	std::mutex g_mutex;
	float g_pendingX = 0.0f;
	float g_pendingY = 0.0f;
	int   g_pendingWheel = 0;
	float g_smoothX = 0.0f;
	float g_smoothY = 0.0f;

	bool     g_anglesInitialized = false;
	float    g_desiredAlpha = -0.10f;
	float    g_desiredBeta  = -kHalfPi;
	uint16_t g_lastMode     = 0xFFFF;
	uint32_t g_mouseIdleFrames = 9999;

	std::atomic<bool>     g_mouseDrivenSession{false};
	std::atomic<uint32_t> g_applied{0};
	std::atomic<float>    g_maxDelta{0.0f};
	std::atomic<float>    g_smoothing{0.0f};
	std::atomic<float>    g_aimSensitivity{0.85f};
	std::atomic<bool>     g_vehicleAutoCenter{false};
	std::atomic<uint8_t>  g_buttonMask{0};

	float WrapAngle(float a)
	{
		if (!std::isfinite(a))
			return 0.0f;
		a = std::fmod(a + kPi, kTwoPi);
		if (a < 0.0f)
			a += kTwoPi;
		return a - kPi;
	}

	float SafeNormalize(sa::CVector& v)
	{
		const float lenSq = v.x * v.x + v.y * v.y + v.z * v.z;
		if (lenSq <= 1e-8f || !std::isfinite(lenSq))
			return 0.0f;
		const float len = std::sqrt(lenSq);
		const float inv = 1.0f / len;
		v.x *= inv;
		v.y *= inv;
		v.z *= inv;
		return len;
	}

	sa::CVector FrontFromAngles(float alpha, float beta)
	{
		const float cosA = std::cos(alpha);
		return sa::CVector(
				-cosA * std::cos(beta),
				-cosA * std::sin(beta),
				std::sin(alpha));
	}

	sa::CVector UpFromFront(const sa::CVector& front)
	{
		// World up = (0, 0, 1); Right = Front x WorldUp = (front.y, -front.x, 0)
		sa::CVector right(front.y, -front.x, 0.0f);
		if (SafeNormalize(right) <= 1e-5f)
			return sa::CVector(0.0f, 0.0f, 1.0f);
		// Up = Right x Front
		sa::CVector up(
				right.y * front.z - right.z * front.y,
				right.z * front.x - right.x * front.z,
				right.x * front.y - right.y * front.x);
		if (SafeNormalize(up) <= 1e-5f)
			return sa::CVector(0.0f, 0.0f, 1.0f);
		return up;
	}

	bool IsThirdPersonOrbitMode(uint16_t mode)
	{
		switch (mode) {
			case sa::MODE_FOLLOWPED:
			case sa::MODE_FOLLOW_PED_WITH_BIND:
			case sa::MODE_BEHINDCAR:
			case sa::MODE_BEHINDBOAT:
			case sa::MODE_CAM_ON_A_STRING:
			case sa::MODE_WHEELCAM:
			case sa::MODE_GTACLASSIC:
			case sa::MODE_FIGHT_CAM:
			case sa::MODE_FIGHT_CAM_RUNABOUT:
				return true;
			default:
				return false;
		}
	}

	bool IsVehicleCamMode(uint16_t mode)
	{
		switch (mode) {
			case sa::MODE_BEHINDCAR:
			case sa::MODE_BEHINDBOAT:
			case sa::MODE_CAM_ON_A_STRING:
			case sa::MODE_WHEELCAM:
				return true;
			default:
				return false;
		}
	}

	bool IsAimCamMode(uint16_t mode)
	{
		switch (mode) {
			case sa::MODE_AIMING:
			case sa::MODE_SNIPER:
			case sa::MODE_ROCKETLAUNCHER:
			case sa::MODE_1STPERSON:
			case sa::MODE_M16_1STPERSON:
			case sa::MODE_SNIPER_RUNABOUT:
			case sa::MODE_ROCKETLAUNCHER_RUNABOUT:
			case sa::MODE_1STPERSON_RUNABOUT:
			case sa::MODE_M16_1STPERSON_RUNABOUT:
			case sa::MODE_HELICANNON_1STPERSON:
			case sa::MODE_CAMERA:
			case sa::MODE_ROCKETLAUNCHER_HS:
			case sa::MODE_ROCKETLAUNCHER_RUNABOUT_HS:
			case sa::MODE_AIMWEAPON:
			case sa::MODE_AIMWEAPON_FROMCAR:
			case sa::MODE_AIMWEAPON_ATTACHED:
				return true;
			default:
				return false;
		}
	}

	bool IsPedBodyAimMode(uint16_t mode)
	{
		switch (mode) {
			case sa::MODE_AIMWEAPON:
			case sa::MODE_AIMWEAPON_FROMCAR:
			case sa::MODE_AIMWEAPON_ATTACHED:
				return true;
			default:
				return false;
		}
	}

	bool IsControllableCamMode(uint16_t mode)
	{
		return IsThirdPersonOrbitMode(mode) || IsAimCamMode(mode);
	}

	bool IsMouseCameraActive()
	{
		return Settings::kmEnabled() && g_mouseDrivenSession.load(std::memory_order_acquire);
	}

	void InitAnglesFromCam(const sa::CCam* cam, bool aimMode)
	{
		float alpha = cam->Alpha;
		float beta  = cam->m_fHorizontalAngle;

		sa::CVector f = cam->Front;
		if (SafeNormalize(f) > 0.01f) {
			const float vecAlpha = std::asin(std::clamp(f.z, -0.95f, 0.95f));
			const float vecBeta  = std::atan2(-f.y, -f.x);
			if (!std::isfinite(alpha) || std::fabs(alpha) < 1e-5f)
				alpha = vecAlpha;
			beta = vecBeta;
		}

		const float minA = aimMode ? kMinAimAlpha : kMinOrbitAlpha;
		const float maxA = aimMode ? kMaxAimAlpha : kMaxOrbitAlpha;
		g_desiredAlpha = std::clamp(std::isfinite(alpha) ? alpha : -0.10f, minA, maxA);
		g_desiredBeta  = WrapAngle(std::isfinite(beta) ? beta : -kHalfPi);
		g_anglesInitialized = true;
	}

	void AlignPedToCameraDirection(sa::CCamera& camera, float beta)
	{
		const float pedYaw = WrapAngle(beta + kHalfPi);
		camera.m_fOrientation = pedYaw;

		sa::CPed* pPed = GamePool_FindPlayerPed();
		if (pPed != nullptr && !pPed->IsInVehicle()) {
			pPed->m_fCurrentRotation = pedYaw;
			pPed->m_fAimingRotation  = pedYaw;
		}
	}

	float SensitivityScale(uint16_t mode, bool aimMode)
	{
		// Same idea as the mobilador's aim sensitivity option, driven by the game's
		// own camera sensitivity sliders so keyboard/mouse matches touch tuning.
		int percent = 50;
		if (aimMode)
			percent = Settings::camSensAim();
		else if (IsVehicleCamMode(mode))
			percent = Settings::camSensVeh();
		else
			percent = Settings::camSensFoot();

		return std::clamp(static_cast<float>(percent) / 100.0f, 0.2f, 3.0f);
	}

	void DrainAndIntegrateMouse(const sa::CCam* cam, uint16_t mode, bool aimMode)
	{
		float rawDx = 0.0f;
		float rawDy = 0.0f;
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			rawDx = g_pendingX;
			rawDy = g_pendingY;
			g_pendingX = 0.0f;
			g_pendingY = 0.0f;
			g_pendingWheel = 0;
		}

		const float maxDelta = g_maxDelta.load(std::memory_order_relaxed);
		if (maxDelta > 0.0f) {
			rawDx = std::clamp(rawDx, -maxDelta, maxDelta);
			rawDy = std::clamp(rawDy, -maxDelta, maxDelta);
		}

		const float smooth = g_smoothing.load(std::memory_order_relaxed);
		float dx = rawDx;
		float dy = rawDy;
		if (smooth > 0.001f) {
			const float blend = 1.0f - smooth;
			g_smoothX = g_smoothX * smooth + rawDx * blend;
			g_smoothY = g_smoothY * smooth + rawDy * blend;
			if (std::fabs(g_smoothX) < 0.0005f) g_smoothX = 0.0f;
			if (std::fabs(g_smoothY) < 0.0005f) g_smoothY = 0.0f;
			dx = g_smoothX;
			dy = g_smoothY;
		} else {
			g_smoothX = 0.0f;
			g_smoothY = 0.0f;
		}

		if (Settings::invertLook())
			dy = -dy;

		const float aimMult = aimMode ? g_aimSensitivity.load(std::memory_order_relaxed) : 1.0f;
		const float sens = SensitivityScale(mode, aimMode);

		float fovMult = 1.0f;
		if (aimMode && cam != nullptr && std::isfinite(cam->FOV) && cam->FOV > 5.0f && cam->FOV < 65.0f)
			fovMult = std::clamp(cam->FOV / 70.0f, 0.20f, 1.0f);

		if (dx != 0.0f || dy != 0.0f) {
			g_desiredBeta -= dx * kBaseSensH * aimMult * sens * fovMult;
			g_desiredBeta = WrapAngle(g_desiredBeta);

			const float minA = aimMode ? kMinAimAlpha : kMinOrbitAlpha;
			const float maxA = aimMode ? kMaxAimAlpha : kMaxOrbitAlpha;
			g_desiredAlpha = std::clamp(g_desiredAlpha - dy * kBaseSensV * aimMult * sens * fovMult, minA, maxA);

			g_mouseIdleFrames = 0;
			g_applied.fetch_add(1, std::memory_order_relaxed);
		} else if (g_mouseIdleFrames < 6000) {
			++g_mouseIdleFrames;
		}
	}

	float ResolveOrbitDistance(const sa::CCam* cam, const sa::CVector& oldFront, sa::CVector& outPivot)
	{
		sa::CVector entityPos(0.0f, 0.0f, 0.0f);
		bool hasEntityPos = false;

		if (sa::CPed* pPed = GamePool_FindPlayerPed()) {
			entityPos = pPed->GetPosition();
			hasEntityPos = std::isfinite(entityPos.x) && std::isfinite(entityPos.y) && std::isfinite(entityPos.z);
		}

		float dist = (std::isfinite(cam->m_fCameraDistance) && cam->m_fCameraDistance >= 1.2f && cam->m_fCameraDistance <= 25.0f)
				? cam->m_fCameraDistance
				: (IsVehicleCamMode(static_cast<uint16_t>(cam->m_nMode)) ? 6.2f : 3.4f);

		if (hasEntityPos) {
			sa::CVector approxTarget = entityPos;
			approxTarget.z += IsVehicleCamMode(static_cast<uint16_t>(cam->m_nMode)) ? 0.55f : 0.65f;

			const sa::CVector delta(
					approxTarget.x - cam->Source.x,
					approxTarget.y - cam->Source.y,
					approxTarget.z - cam->Source.z);
			const float proj = delta.x * oldFront.x + delta.y * oldFront.y + delta.z * oldFront.z;
			if (std::isfinite(proj) && proj >= 1.0f && proj <= 28.0f)
				dist = proj;
		}

		outPivot = sa::CVector(
				cam->Source.x + oldFront.x * dist,
				cam->Source.y + oldFront.y * dist,
				cam->Source.z + oldFront.z * dist);
		return dist;
	}

	void ApplyMouseButtons()
	{
		// Same three mappings as the mobilador (LMB fire / RMB aim+handbrake /
		// MMB submission), written on the engine's own pad key table
		// (Game/Pad.h) and re-pinned every frame by NativeCameraMouseApplyKeys().
		static bool fireOwned = false;
		static bool submissionOwned = false;
		static int rmbKey = -1;

		const uint8_t desired = Settings::kmEnabled()
				? g_buttonMask.load(std::memory_order_acquire)
				: static_cast<uint8_t>(0);

		if (desired & 0x1) {
			fireOwned = true;
			LocalPlayerKeys.bKeys[ePadKeys::KEY_FIRE] = true;
		} else if (fireOwned) {
			LocalPlayerKeys.bKeys[ePadKeys::KEY_FIRE] = false;
			fireOwned = false;
		}

		if (desired & 0x2) {
			if (rmbKey < 0) {
				sa::CPed* pPed = GamePool_FindPlayerPed();
				const bool inVeh = pPed != nullptr && pPed->IsInVehicle();
				rmbKey = inVeh ? ePadKeys::KEY_HANDBRAKE : ePadKeys::KEY_SECONDARY_ATTACK;
			}
			LocalPlayerKeys.bKeys[rmbKey] = true;
		} else if (rmbKey >= 0) {
			LocalPlayerKeys.bKeys[rmbKey] = false;
			rmbKey = -1;
		}

		if (desired & 0x4) {
			submissionOwned = true;
			LocalPlayerKeys.bKeys[ePadKeys::KEY_SUBMISSION] = true;
		} else if (submissionOwned) {
			LocalPlayerKeys.bKeys[ePadKeys::KEY_SUBMISSION] = false;
			submissionOwned = false;
		}
	}
} // namespace

void NativeCameraMouseEnqueue(float dx, float dy)
{
	if (!std::isfinite(dx) || !std::isfinite(dy))
		return;
	if (dx != 0.0f || dy != 0.0f)
		g_mouseDrivenSession.store(true, std::memory_order_release);

	std::lock_guard<std::mutex> lock(g_mutex);
	g_pendingX = std::clamp(g_pendingX + dx, -kMaxAccumDelta, kMaxAccumDelta);
	g_pendingY = std::clamp(g_pendingY + dy, -kMaxAccumDelta, kMaxAccumDelta);
}

void NativeCameraMouseEnqueueWheel(int steps)
{
	if (steps == 0)
		return;
	std::lock_guard<std::mutex> lock(g_mutex);
	g_pendingWheel = std::clamp(g_pendingWheel + steps, -16, 16);
}

void NativeCameraMouseSetButtons(uint8_t buttonMask)
{
	g_buttonMask.store(buttonMask, std::memory_order_release);
}

void NativeCameraMouseReset()
{
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_pendingX = 0.0f;
		g_pendingY = 0.0f;
		g_pendingWheel = 0;
		g_smoothX = 0.0f;
		g_smoothY = 0.0f;
	}
	g_buttonMask.store(0, std::memory_order_release);
	g_mouseDrivenSession.store(false, std::memory_order_release);
	g_anglesInitialized = false;
}

void NativeCameraMouseSetOptions(float maxDelta, float smoothing, float aimSensitivity, bool vehicleAutoCenter)
{
	g_maxDelta.store(std::isfinite(maxDelta) ? std::max(0.0f, maxDelta) : 0.0f);
	g_smoothing.store(std::isfinite(smoothing) ? std::clamp(smoothing, 0.0f, 0.85f) : 0.0f);
	g_aimSensitivity.store(std::isfinite(aimSensitivity) ? std::clamp(aimSensitivity, 0.15f, 3.0f) : 0.85f);
	g_vehicleAutoCenter.store(vehicleAutoCenter);
}

uint32_t NativeCameraMouseGetAppliedCount()
{
	return g_applied.load(std::memory_order_relaxed);
}

void NativeCameraMouseApplyKeys()
{
	ApplyMouseButtons();
}

void NativeCameraMousePreProcess(sa::CCamera& camera, sa::CCam* targetCam)
{
	if (targetCam == nullptr || camera.m_nActiveCam >= 3)
		return;
	sa::CCam* activeCam = &camera.m_aCams[camera.m_nActiveCam];
	if (targetCam != activeCam)
		return;

	const uint16_t mode = static_cast<uint16_t>(targetCam->m_nMode);
	if (!IsMouseCameraActive() || !IsControllableCamMode(mode)) {
		g_anglesInitialized = false;
		g_lastMode = mode;
		return;
	}

	const bool aimMode = IsAimCamMode(mode);
	const bool wasAimMode = (g_lastMode != 0xFFFF) && IsAimCamMode(g_lastMode);

	if (!g_anglesInitialized) {
		InitAnglesFromCam(targetCam, aimMode);
	} else if (aimMode && !wasAimMode) {
		// Entering aim from free-look: keep camera look direction and turn ped toward camera,
		// NOT camera toward ped's sideways body rotation.
		g_desiredAlpha = std::clamp(g_desiredAlpha, kMinAimAlpha, kMaxAimAlpha);
		AlignPedToCameraDirection(camera, g_desiredBeta);
	} else if (!aimMode && wasAimMode) {
		// Leaving aim back to orbit: preserve where player was aiming seamlessly.
		g_desiredAlpha = std::clamp(g_desiredAlpha, kMinOrbitAlpha, kMaxOrbitAlpha);
	}

	DrainAndIntegrateMouse(targetCam, mode, aimMode);

	// Push desired world camera angles directly before CCam::Process runs.
	targetCam->Alpha = g_desiredAlpha;
	targetCam->m_fTrueAlpha = g_desiredAlpha;
	targetCam->m_fIdealAlpha = g_desiredAlpha;
	targetCam->AlphaSpeed = 0.0f;

	targetCam->m_fHorizontalAngle = g_desiredBeta;
	targetCam->m_fTrueBeta = g_desiredBeta;
	targetCam->BetaSpeed = 0.0f;

	targetCam->m_bFixingBeta = false;
	targetCam->ResetKeyboardStatics = true;

	if (IsPedBodyAimMode(mode))
		AlignPedToCameraDirection(camera, g_desiredBeta);

	g_lastMode = mode;
}

void NativeCameraMousePostProcess(sa::CCamera& camera, sa::CCam* targetCam)
{
	ApplyMouseButtons();

	if (targetCam == nullptr || camera.m_nActiveCam >= 3)
		return;
	sa::CCam* activeCam = &camera.m_aCams[camera.m_nActiveCam];
	if (targetCam != activeCam || !g_anglesInitialized)
		return;

	const uint16_t mode = static_cast<uint16_t>(targetCam->m_nMode);
	if (!IsMouseCameraActive() || !IsControllableCamMode(mode))
		return;

	// Optional vehicle auto-center only if explicitly enabled and mouse is idle.
	if (IsVehicleCamMode(mode) && g_vehicleAutoCenter.load(std::memory_order_relaxed) && g_mouseIdleFrames > 45) {
		if (std::isfinite(targetCam->m_fHorizontalAngle))
			g_desiredBeta = WrapAngle(targetCam->m_fHorizontalAngle);
		if (std::isfinite(targetCam->Alpha))
			g_desiredAlpha = std::clamp(targetCam->Alpha, kMinOrbitAlpha, kMaxOrbitAlpha);
		return;
	}

	const sa::CVector newFront = FrontFromAngles(g_desiredAlpha, g_desiredBeta);
	const sa::CVector newUp = UpFromFront(newFront);

	// For third-person orbit modes, move the actual world camera position (Source)
	// whenever FollowPed_SA / FollowCar_SA clamped Alpha or drifted HorizontalAngle.
	if (IsThirdPersonOrbitMode(mode)) {
		const float alphaDiff = std::fabs(targetCam->Alpha - g_desiredAlpha);
		const float betaDiff  = std::fabs(WrapAngle(targetCam->m_fHorizontalAngle - g_desiredBeta));
		if (alphaDiff > 1e-4f || betaDiff > 1e-4f) {
			sa::CVector oldFront = targetCam->Front;
			if (SafeNormalize(oldFront) <= 1e-4f)
				oldFront = FrontFromAngles(targetCam->Alpha, targetCam->m_fHorizontalAngle);

			sa::CVector pivot;
			const float dist = ResolveOrbitDistance(targetCam, oldFront, pivot);
			sa::CVector desiredSource(
					pivot.x - newFront.x * dist,
					pivot.y - newFront.y * dist,
					pivot.z - newFront.z * dist);

			// Prevent the world camera from clipping under ground or through walls when pitching steeply.
			if (eng::ptr(eng::Sym::CWorld_ProcessLineOfSight) != nullptr) {
				sa::CColPoint colPoint{};
				sa::CEntity* hitEntity = nullptr;
				eng::fn::CWorld_ProcessLineOfSight(
						&pivot, &desiredSource, &colPoint, &hitEntity,
						true, false, false, true, false, false, true, false);
				if (hitEntity != nullptr) {
					const sa::CVector toHit(
							colPoint.m_vecPoint.x - pivot.x,
							colPoint.m_vecPoint.y - pivot.y,
							colPoint.m_vecPoint.z - pivot.z);
					const float hitDist = std::sqrt(toHit.x * toHit.x + toHit.y * toHit.y + toHit.z * toHit.z);
					if (std::isfinite(hitDist)) {
						const float safeDist = std::clamp(hitDist - 0.22f, 0.55f, dist);
						desiredSource.x = pivot.x - newFront.x * safeDist;
						desiredSource.y = pivot.y - newFront.y * safeDist;
						desiredSource.z = pivot.z - newFront.z * safeDist;
					}
				}
			}

			targetCam->Source = desiredSource;
		}
	}

	targetCam->Alpha = g_desiredAlpha;
	targetCam->m_fTrueAlpha = g_desiredAlpha;
	targetCam->m_fIdealAlpha = g_desiredAlpha;
	targetCam->AlphaSpeed = 0.0f;

	targetCam->m_fHorizontalAngle = g_desiredBeta;
	targetCam->m_fTrueBeta = g_desiredBeta;
	targetCam->BetaSpeed = 0.0f;

	targetCam->Front = newFront;
	targetCam->Up = newUp;

	if (IsPedBodyAimMode(mode))
		AlignPedToCameraDirection(camera, g_desiredBeta);
}

DECL_HOOK(void, CCam_Process, uintptr_t thiz)
{
	sa::CCamera& camera = sa::getTheCamera();
	sa::CCam* cam = reinterpret_cast<sa::CCam*>(thiz);

	NativeCameraMousePreProcess(camera, cam);
	CCam_Process(thiz);
	NativeCameraMousePostProcess(camera, cam);
}

// Fallback used only when CCam::Process is not exported by the installed
// libGTASA: CCamera::Process dispatches the active CCam, so wrapping it gives
// the same pre/post window for the camera math.
DECL_HOOK(void, CCamera_Process)
{
	sa::CCamera& camera = sa::getTheCamera();
	sa::CCam* cam = (camera.m_nActiveCam < 3) ? &camera.m_aCams[camera.m_nActiveCam] : nullptr;

	NativeCameraMousePreProcess(camera, cam);
	CCamera_Process();
	NativeCameraMousePostProcess(camera, cam);
}

void InstallNativeCameraMouseHooks()
{
	if (eng::ptr(eng::Sym::CCam_Process) != nullptr) {
		REG_HOOK(Boot, CCam_Process, CCam_Process, "Physical mouse camera look (CCam::Process)");
		spdlog::info("NativeCameraMouse: CCam::Process hook installed");
		return;
	}

	if (eng::ptr(eng::Sym::CCamera_Process) != nullptr) {
		REG_HOOK(Boot, CCamera_Process, CCamera_Process, "Physical mouse camera look (CCamera::Process)");
		spdlog::info("NativeCameraMouse: CCamera::Process fallback hook installed");
		return;
	}

	spdlog::warn("NativeCameraMouse: no camera process symbol resolved, physical mouse camera disabled");
}
