#include "PcMouseLook.h"

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

namespace {

constexpr float kPi     = 3.14159265358979323846f;
constexpr float kTwoPi  = 6.28318530717958647692f;
constexpr float kHalfPi = 1.57079632679489661923f;

// Radians per input count at 100% sensitivity.
constexpr float kBaseSensH     = 0.0028f;
constexpr float kBaseSensV     = 0.0024f;
constexpr float kMaxAccumDelta = 65536.0f;

// Vertical pitch bounds (radians).
constexpr float kMinOrbitAlpha = -1.05f;
constexpr float kMaxOrbitAlpha =  0.72f;
constexpr float kMinAimAlpha   = -1.32f;
constexpr float kMaxAimAlpha   =  1.32f;

// The camera is only steered while the mouse is actually in use, so a plain
// touch player is never affected by this module.
constexpr uint32_t kIdleFramesBeforeRelease = 150;

std::mutex g_mutex;
float g_pendingX = 0.0f;
float g_pendingY = 0.0f;
float g_smoothX  = 0.0f;
float g_smoothY  = 0.0f;

bool g_anglesInitialized = false;
float g_desiredAlpha = -0.10f;
float g_desiredBeta  = -kHalfPi;
uint16_t g_lastMode  = 0xFFFF;

std::atomic<uint32_t> g_idleFrames{9999};
std::atomic<uint32_t> g_applied{0};
std::atomic<uint8_t>  g_buttonMask{0};

int g_rmbKey = -1;

using LineOfSightFn = bool (*)(const sa::CVector&, const sa::CVector&, sa::CColPoint&, sa::CEntity*&,
		bool, bool, bool, bool, bool, bool, bool, bool);

LineOfSightFn lineOfSight()
{
	return eng::as<LineOfSightFn>(eng::Sym::CWorld_ProcessLineOfSight);
}

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
	sa::CVector right(front.y, -front.x, 0.0f);
	if (SafeNormalize(right) <= 1e-5f)
		return sa::CVector(0.0f, 0.0f, 1.0f);

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

void InitAnglesFromCam(const sa::CCam* cam, bool aimMode)
{
	float alpha = cam->Alpha;
	float beta = cam->m_fHorizontalAngle;

	sa::CVector f = cam->Front;
	if (SafeNormalize(f) > 0.01f) {
		const float vecAlpha = std::asin(std::clamp(f.z, -0.95f, 0.95f));
		const float vecBeta = std::atan2(-f.y, -f.x);
		if (!std::isfinite(alpha) || std::fabs(alpha) < 1e-5f)
			alpha = vecAlpha;
		beta = vecBeta;
	}

	const float minA = aimMode ? kMinAimAlpha : kMinOrbitAlpha;
	const float maxA = aimMode ? kMaxAimAlpha : kMaxOrbitAlpha;
	g_desiredAlpha = std::clamp(std::isfinite(alpha) ? alpha : -0.10f, minA, maxA);
	g_desiredBeta = WrapAngle(std::isfinite(beta) ? beta : -kHalfPi);
	g_anglesInitialized = true;
}

void AlignPedToCameraDirection(sa::CCamera& camera, float beta)
{
	const float pedYaw = WrapAngle(beta + kHalfPi);
	camera.m_fOrientation = pedYaw;

	sa::CPed* pPed = GamePool_FindPlayerPed();
	if (pPed != nullptr && !pPed->IsInVehicle()) {
		pPed->m_fCurrentRotation = pedYaw;
		pPed->m_fAimingRotation = pedYaw;
	}
}

float SensitivityScale(uint16_t mode, bool aimMode)
{
	// 1..100% from the game's own camera sensitivity options, so the mouse obeys
	// the same tuning the touch camera uses.
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
	}

	rawDx = std::clamp(rawDx, -kMaxAccumDelta, kMaxAccumDelta);
	rawDy = std::clamp(rawDy, -kMaxAccumDelta, kMaxAccumDelta);

	// Light smoothing: removes the one-frame jitter of Android's motion batching
	// without adding visible lag.
	float dx = rawDx;
	float dy = rawDy;
	if (dx != 0.0f || dy != 0.0f) {
		const float blend = 0.65f;
		g_smoothX = g_smoothX * (1.0f - blend) + rawDx * blend;
		g_smoothY = g_smoothY * (1.0f - blend) + rawDy * blend;
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

	const float sens = SensitivityScale(mode, aimMode);

	float fovMult = 1.0f;
	if (aimMode && cam != nullptr && std::isfinite(cam->FOV) && cam->FOV > 5.0f && cam->FOV < 65.0f)
		fovMult = std::clamp(cam->FOV / 70.0f, 0.20f, 1.0f);

	if (dx != 0.0f || dy != 0.0f) {
		g_desiredBeta -= dx * kBaseSensH * sens * fovMult;
		g_desiredBeta = WrapAngle(g_desiredBeta);

		const float minA = aimMode ? kMinAimAlpha : kMinOrbitAlpha;
		const float maxA = aimMode ? kMaxAimAlpha : kMaxOrbitAlpha;
		g_desiredAlpha = std::clamp(g_desiredAlpha - dy * kBaseSensV * sens * fovMult, minA, maxA);

		g_idleFrames.store(0, std::memory_order_release);
		g_applied.fetch_add(1, std::memory_order_relaxed);
	} else if (g_buttonMask.load(std::memory_order_acquire) == 0
			&& g_idleFrames.load(std::memory_order_acquire) < kIdleFramesBeforeRelease + 1) {
		g_idleFrames.fetch_add(1, std::memory_order_acq_rel);
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
	// Only touch the keys this module owns: the touch panel keeps writing its own
	// buttons every frame and must never be cancelled from here.
	static bool fireOwned = false;
	static bool submissionOwned = false;

	const uint8_t desired = Settings::kmEnabled()
			? g_buttonMask.load(std::memory_order_acquire)
			: static_cast<uint8_t>(0);

	if (desired & 0x1) { // left button -> fire / attack
		fireOwned = true;
		LocalPlayerKeys.bKeys[ePadKeys::KEY_FIRE] = true;
	} else if (fireOwned) {
		LocalPlayerKeys.bKeys[ePadKeys::KEY_FIRE] = false;
		fireOwned = false;
	}

	if (desired & 0x2) { // right button -> handbrake in a vehicle, enter/exit on foot
		if (g_rmbKey < 0) {
			sa::CPed* pPed = GamePool_FindPlayerPed();
			const bool inVeh = pPed != nullptr && pPed->IsInVehicle();
			g_rmbKey = inVeh ? ePadKeys::KEY_HANDBRAKE : ePadKeys::KEY_SECONDARY_ATTACK;
		}
		LocalPlayerKeys.bKeys[g_rmbKey] = true;
	} else if (g_rmbKey >= 0) {
		LocalPlayerKeys.bKeys[g_rmbKey] = false;
		g_rmbKey = -1;
	}

	if (desired & 0x4) { // middle button -> submission / secondary action
		submissionOwned = true;
		LocalPlayerKeys.bKeys[ePadKeys::KEY_SUBMISSION] = true;
	} else if (submissionOwned) {
		LocalPlayerKeys.bKeys[ePadKeys::KEY_SUBMISSION] = false;
		submissionOwned = false;
	}
}

} // namespace

DECL_HOOK(void, CCam_Process, uintptr_t thiz)
{
	sa::CCamera& camera = sa::getTheCamera();
	sa::CCam* cam = reinterpret_cast<sa::CCam*>(thiz);

	pcmouse::preProcess(camera, cam);
	CCam_Process(thiz);
	pcmouse::postProcess(camera, cam);
}

// Fallback used only when CCam::Process is not exported by the installed
// libGTASA: CCamera::Process dispatches the active CCam, so wrapping it gives
// the same pre/post window for the camera math.
DECL_HOOK(void, CCamera_Process)
{
	sa::CCamera& camera = sa::getTheCamera();
	sa::CCam* cam = (camera.m_nActiveCam < 3) ? &camera.m_aCams[camera.m_nActiveCam] : nullptr;

	pcmouse::preProcess(camera, cam);
	CCamera_Process();
	pcmouse::postProcess(camera, cam);
}

namespace pcmouse {

void addDelta(float dx, float dy)
{
	if (!std::isfinite(dx) || !std::isfinite(dy))
		return;
	if (dx == 0.0f && dy == 0.0f)
		return;

	std::lock_guard<std::mutex> lock(g_mutex);
	g_pendingX = std::clamp(g_pendingX + dx, -kMaxAccumDelta, kMaxAccumDelta);
	g_pendingY = std::clamp(g_pendingY + dy, -kMaxAccumDelta, kMaxAccumDelta);
}

void setButton(int button, bool pressed)
{
	if (button < 1 || button > 3)
		return;

	const uint8_t bit = static_cast<uint8_t>(1u << (button - 1));
	uint8_t mask = g_buttonMask.load(std::memory_order_acquire);
	for (;;) {
		const uint8_t next = static_cast<uint8_t>(pressed ? (mask | bit) : (mask & static_cast<uint8_t>(~bit)));
		if (g_buttonMask.compare_exchange_weak(mask, next, std::memory_order_acq_rel))
			break;
	}

	if (pressed)
		g_idleFrames.store(0, std::memory_order_release);
}

void addWheel(int steps)
{
	(void) steps; // wheel is reserved for future weapon cycling
}

void reset()
{
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_pendingX = 0.0f;
		g_pendingY = 0.0f;
		g_smoothX = 0.0f;
		g_smoothY = 0.0f;
	}
	g_buttonMask.store(0, std::memory_order_release);
}

bool isActive()
{
	return Settings::kmEnabled() && g_idleFrames.load(std::memory_order_acquire) < kIdleFramesBeforeRelease;
}

void preProcess(sa::CCamera& camera, sa::CCam* cam)
{
	if (cam == nullptr || camera.m_nActiveCam >= 3)
		return;
	if (cam != &camera.m_aCams[camera.m_nActiveCam])
		return;

	const uint16_t mode = static_cast<uint16_t>(cam->m_nMode);
	const bool controllable = IsControllableCamMode(mode);
	const bool usable = Settings::kmEnabled()
			&& g_idleFrames.load(std::memory_order_acquire) < kIdleFramesBeforeRelease;

	if (!usable || !controllable) {
		g_anglesInitialized = false;
		g_lastMode = mode;
		return;
	}

	const bool aimMode = IsAimCamMode(mode);
	const bool wasAimMode = (g_lastMode != 0xFFFF) && IsAimCamMode(g_lastMode);

	if (!g_anglesInitialized) {
		InitAnglesFromCam(cam, aimMode);
	} else if (aimMode && !wasAimMode) {
		// Entering aim from free look: keep the look direction and turn the ped
		// towards the camera instead of snapping the camera behind the ped.
		g_desiredAlpha = std::clamp(g_desiredAlpha, kMinAimAlpha, kMaxAimAlpha);
		AlignPedToCameraDirection(camera, g_desiredBeta);
	} else if (!aimMode && wasAimMode) {
		g_desiredAlpha = std::clamp(g_desiredAlpha, kMinOrbitAlpha, kMaxOrbitAlpha);
	}

	DrainAndIntegrateMouse(cam, mode, aimMode);

	cam->Alpha = g_desiredAlpha;
	cam->m_fTrueAlpha = g_desiredAlpha;
	cam->m_fIdealAlpha = g_desiredAlpha;
	cam->AlphaSpeed = 0.0f;

	cam->m_fHorizontalAngle = g_desiredBeta;
	cam->m_fTrueBeta = g_desiredBeta;
	cam->BetaSpeed = 0.0f;

	cam->m_bFixingBeta = false;
	cam->ResetKeyboardStatics = true;

	if (IsPedBodyAimMode(mode))
		AlignPedToCameraDirection(camera, g_desiredBeta);

	g_lastMode = mode;
}

void postProcess(sa::CCamera& camera, sa::CCam* cam)
{
	ApplyMouseButtons();

	if (cam == nullptr || camera.m_nActiveCam >= 3)
		return;
	if (cam != &camera.m_aCams[camera.m_nActiveCam] || !g_anglesInitialized)
		return;

	const uint16_t mode = static_cast<uint16_t>(cam->m_nMode);
	const bool usable = Settings::kmEnabled()
			&& g_idleFrames.load(std::memory_order_acquire) < kIdleFramesBeforeRelease;
	if (!usable || !IsControllableCamMode(mode))
		return;

	const sa::CVector newFront = FrontFromAngles(g_desiredAlpha, g_desiredBeta);
	const sa::CVector newUp = UpFromFront(newFront);

	// Third-person orbit: the engine's FollowPed/FollowCar clamps Alpha and drifts
	// HorizontalAngle, so the world camera position has to be re-seated from the
	// pivot whenever the mouse moved the look direction away from it.
	if (IsThirdPersonOrbitMode(mode)) {
		const float alphaDiff = std::fabs(cam->Alpha - g_desiredAlpha);
		const float betaDiff = std::fabs(WrapAngle(cam->m_fHorizontalAngle - g_desiredBeta));
		if (alphaDiff > 1e-4f || betaDiff > 1e-4f) {
			sa::CVector oldFront = cam->Front;
			if (SafeNormalize(oldFront) <= 1e-4f)
				oldFront = FrontFromAngles(cam->Alpha, cam->m_fHorizontalAngle);

			sa::CVector pivot;
			const float dist = ResolveOrbitDistance(cam, oldFront, pivot);
			sa::CVector desiredSource(
					pivot.x - newFront.x * dist,
					pivot.y - newFront.y * dist,
					pivot.z - newFront.z * dist);

			// Never let the camera end up behind geometry when pitching steeply.
			if (LineOfSightFn los = lineOfSight()) {
				sa::CColPoint colPoint{};
				sa::CEntity* hitEntity = nullptr;
				if (los(pivot, desiredSource, colPoint, hitEntity,
						true, false, false, true, false, false, true, false)) {
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

			cam->Source = desiredSource;
		}
	}

	cam->Alpha = g_desiredAlpha;
	cam->m_fTrueAlpha = g_desiredAlpha;
	cam->m_fIdealAlpha = g_desiredAlpha;
	cam->AlphaSpeed = 0.0f;

	cam->m_fHorizontalAngle = g_desiredBeta;
	cam->m_fTrueBeta = g_desiredBeta;
	cam->BetaSpeed = 0.0f;

	cam->Front = newFront;
	cam->Up = newUp;

	if (IsPedBodyAimMode(mode))
		AlignPedToCameraDirection(camera, g_desiredBeta);
}

void installHooks()
{
	if (eng::ptr(eng::Sym::CCam_Process) != nullptr) {
		REG_HOOK(Boot, CCam_Process, CCam_Process, "Physical mouse camera look (CCam::Process)");
		spdlog::info("PcMouseLook: CCam::Process hook installed");
		return;
	}

	if (eng::ptr(eng::Sym::CCamera_Process) != nullptr) {
		REG_HOOK(Boot, CCamera_Process, CCamera_Process, "Physical mouse camera look (CCamera::Process)");
		spdlog::info("PcMouseLook: CCamera::Process fallback hook installed");
		return;
	}

	spdlog::warn("PcMouseLook: no camera process symbol resolved, physical mouse camera disabled");
}

} // namespace pcmouse
