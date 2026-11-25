#include "native_camera_mouse.h"
#include "../main.h"
#include "game.h"
#include "Camera.h"
#include "Cam.h"
#include "CFirstPersonCamera.hpp"
#include "pad.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>

namespace {
std::mutex g_mutex;
float g_pendingX = 0.0f;
float g_pendingY = 0.0f;
float g_smoothX = 0.0f;
float g_smoothY = 0.0f;

std::atomic<uint32_t> g_received{0};
std::atomic<uint32_t> g_applied{0};

std::atomic<float> g_maxDelta{0.0f};
std::atomic<float> g_smoothing{0.0f};
std::atomic<float> g_aimSensitivity{0.85f};
std::atomic<bool> g_vehicleAutoCenter{false};

// Persistent target orientation to prevent FollowPed_SA / FollowCar_SA
// from snapping vertical Alpha back toward 0.0f.
bool g_hasDesiredState = false;
CCam* g_lastCamPtr = nullptr;
uint16_t g_lastMode = 0xFFFF;
float g_desiredAlpha = 0.0f;
float g_desiredBeta = 0.0f;
bool g_hadMouseInputThisFrame = false;

constexpr float kBaseSensitivity = 0.0024f;
constexpr float kMaxAccumulatedDelta = 65536.0f;
constexpr float kFollowMinAlpha = -1.20f;
constexpr float kFollowMaxAlpha = 1.10f;
constexpr float kAimMinAlpha = -1.48f;
constexpr float kAimMaxAlpha = 1.48f;
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

float WrapAngle(float angle)
{
    if (!std::isfinite(angle))
        return 0.0f;
    while (angle > kPi)
        angle -= kTwoPi;
    while (angle < -kPi)
        angle += kTwoPi;
    return angle;
}

bool IsAimCamMode(uint16_t mode)
{
    switch (mode)
    {
        case MODE_SNIPER:
        case MODE_ROCKETLAUNCHER:
        case MODE_1STPERSON:
        case MODE_M16_1STPERSON:
        case MODE_SNIPER_RUNABOUT:
        case MODE_ROCKETLAUNCHER_RUNABOUT:
        case MODE_M16_1STPERSON_RUNABOUT:
        case MODE_HELICANNON_1STPERSON:
        case MODE_CAMERA:
        case MODE_ROCKETLAUNCHER_HS:
        case MODE_ROCKETLAUNCHER_RUNABOUT_HS:
        case MODE_AIMWEAPON:
        case MODE_AIMWEAPON_FROMCAR:
        case MODE_AIMWEAPON_ATTACHED:
            return true;
        default:
            return false;
    }
}

bool IsVehicleFollowMode(uint16_t mode)
{
    switch (mode)
    {
        case MODE_BEHINDCAR:
        case MODE_BEHINDBOAT:
        case MODE_CAM_ON_TRAIN_ROOF:
            return true;
        default:
            return false;
    }
}

bool IsPedFollowMode(uint16_t mode)
{
    switch (mode)
    {
        case MODE_FOLLOWPED:
        case MODE_CAM_ON_A_STRING:
        case MODE_FOLLOW_PED_WITH_BIND:
            return true;
        default:
            return false;
    }
}

bool IsControllableCamMode(uint16_t mode)
{
    return IsAimCamMode(mode) || IsPedFollowMode(mode) || IsVehicleFollowMode(mode);
}

void WriteStaticFollowAlphas(float alpha)
{
#if !VER_x32
    if (g_libGTASA != 0)
    {
        // Verified in libGame.so (arm64-v8a):
        // FollowPed_SA reads/lerps Alpha toward *(float*)(g_libGTASA + 0x9F9648)
        // FollowCar_SA reads/lerps Alpha toward *(float*)(g_libGTASA + 0x9F9670)
        *reinterpret_cast<float*>(g_libGTASA + 0x9F9648) = alpha;
        *reinterpret_cast<float*>(g_libGTASA + 0x9F9670) = alpha;
    }
#endif
}
} // namespace

void NativeCameraMouseEnqueue(float dx, float dy)
{
    if (!std::isfinite(dx) || !std::isfinite(dy))
        return;

    std::lock_guard<std::mutex> lock(g_mutex);
    g_pendingX = std::clamp(g_pendingX + dx, -kMaxAccumulatedDelta, kMaxAccumulatedDelta);
    g_pendingY = std::clamp(g_pendingY + dy, -kMaxAccumulatedDelta, kMaxAccumulatedDelta);
    const uint32_t count = ++g_received;
    if (count % 120 == 1)
    {
        Log("[DIRECT-MOUSE] received=(%.2f,%.2f) pending=(%.2f,%.2f)",
            dx, dy, g_pendingX, g_pendingY);
    }
}

void NativeCameraMouseReset()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_pendingX = 0.0f;
    g_pendingY = 0.0f;
    g_smoothX = 0.0f;
    g_smoothY = 0.0f;
    g_hadMouseInputThisFrame = false;
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
    return g_applied.load();
}

void NativeCameraMousePreProcess(CCamera& camera, CCam* targetCam)
{
    if (!targetCam || !PcInputIsEnabledForTouch() || CFirstPersonCamera::IsEnabled())
        return;
    CCam* activeCam = &camera.m_aCams[camera.m_nActiveCam];
    if (targetCam != activeCam)
        return;

    float rawDx = 0.0f;
    float rawDy = 0.0f;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        rawDx = g_pendingX;
        rawDy = g_pendingY;
        g_pendingX = 0.0f;
        g_pendingY = 0.0f;
    }

    const float maxDelta = g_maxDelta.load();
    if (maxDelta > 0.0f)
    {
        rawDx = std::clamp(rawDx, -maxDelta, maxDelta);
        rawDy = std::clamp(rawDy, -maxDelta, maxDelta);
    }

    const float smooth = g_smoothing.load();
    float dx = rawDx;
    float dy = rawDy;
    if (smooth > 0.001f)
    {
        const float alphaBlend = 1.0f - smooth;
        g_smoothX = g_smoothX * smooth + rawDx * alphaBlend;
        g_smoothY = g_smoothY * smooth + rawDy * alphaBlend;
        if (std::fabs(g_smoothX) < 0.0005f) g_smoothX = 0.0f;
        if (std::fabs(g_smoothY) < 0.0005f) g_smoothY = 0.0f;
        dx = g_smoothX;
        dy = g_smoothY;
    }
    else
    {
        g_smoothX = 0.0f;
        g_smoothY = 0.0f;
    }

    const uint16_t mode = static_cast<uint16_t>(targetCam->m_nMode);
    if (!IsControllableCamMode(mode))
    {
        g_hasDesiredState = false;
        g_lastCamPtr = targetCam;
        g_lastMode = mode;
        g_hadMouseInputThisFrame = false;
        return;
    }

    if (!std::isfinite(targetCam->Alpha) || !std::isfinite(targetCam->m_fHorizontalAngle))
    {
        g_hasDesiredState = false;
        g_hadMouseInputThisFrame = false;
        return;
    }

    const bool aimMode = IsAimCamMode(mode);
    const bool vehicleMode = IsVehicleFollowMode(mode);
    const float minAlpha = aimMode ? kAimMinAlpha : kFollowMinAlpha;
    const float maxAlpha = aimMode ? kAimMaxAlpha : kFollowMaxAlpha;

    const bool camChanged = (targetCam != g_lastCamPtr) || (mode != g_lastMode) ||
                            targetCam->ResetStatics || targetCam->bBehindPlayerDesired;
    if (!g_hasDesiredState || camChanged)
    {
        g_desiredAlpha = std::clamp(targetCam->Alpha, minAlpha, maxAlpha);
        g_desiredBeta = WrapAngle(targetCam->m_fHorizontalAngle);
        g_hasDesiredState = true;
        g_lastCamPtr = targetCam;
        g_lastMode = mode;
    }

    const bool hasInput = (dx != 0.0f || dy != 0.0f);
    g_hadMouseInputThisFrame = hasInput;
    if (hasInput)
        ++g_applied;

    const float sens = kBaseSensitivity * (aimMode ? g_aimSensitivity.load() : 1.0f);
    if (hasInput)
    {
        g_desiredBeta = WrapAngle(g_desiredBeta - dx * sens);
        g_desiredAlpha = std::clamp(g_desiredAlpha + dy * sens, minAlpha, maxAlpha);

        targetCam->m_fHorizontalAngle = g_desiredBeta;
        targetCam->BetaSpeed = 0.0f;
        targetCam->m_fBetaSpeedOverOneFrame = 0.0f;
        targetCam->m_fTrueBeta = g_desiredBeta;

        targetCam->Alpha = g_desiredAlpha;
        targetCam->AlphaSpeed = 0.0f;
        targetCam->m_fAlphaSpeedOverOneFrame = 0.0f;
        targetCam->m_fTrueAlpha = g_desiredAlpha;
        targetCam->m_fIdealAlpha = g_desiredAlpha;
    }
    else if (vehicleMode && g_vehicleAutoCenter.load())
    {
        g_desiredBeta = WrapAngle(targetCam->m_fHorizontalAngle);
    }

    targetCam->ResetKeyboardStatics = true;
}

void NativeCameraMousePostProcess(CCamera& camera, CCam* targetCam)
{
    if (!targetCam || !g_hasDesiredState || !PcInputIsEnabledForTouch() || CFirstPersonCamera::IsEnabled())
        return;
    CCam* activeCam = &camera.m_aCams[camera.m_nActiveCam];
    if (targetCam != activeCam)
        return;
    const uint16_t mode = static_cast<uint16_t>(targetCam->m_nMode);
    if (!IsControllableCamMode(mode) || targetCam->LookingBehind || targetCam->bBehindPlayerDesired)
        return;

    if (g_hadMouseInputThisFrame)
    {
        targetCam->Alpha = g_desiredAlpha;
        targetCam->m_fTrueAlpha = g_desiredAlpha;
        targetCam->m_fIdealAlpha = g_desiredAlpha;
        targetCam->m_fHorizontalAngle = g_desiredBeta;
        targetCam->m_fTrueBeta = g_desiredBeta;
        targetCam->AlphaSpeed = 0.0f;
        targetCam->BetaSpeed = 0.0f;
        targetCam->m_fAlphaSpeedOverOneFrame = 0.0f;
        targetCam->m_fBetaSpeedOverOneFrame = 0.0f;
    }
    targetCam->ResetKeyboardStatics = true;
}

void NativeCameraMouseApply(CCamera& camera)
{
    // Mouse camera application now runs inside CCam__Process_hook around CCam::Process
    // to guarantee FollowPed_SA / FollowCar_SA / Process_AimWeapon cannot snap Alpha back.
    (void)camera;
}
