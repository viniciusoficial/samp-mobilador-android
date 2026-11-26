#include "native_camera_mouse.h"
#include "../main.h"
#include "game.h"
#include "Camera.h"
#include "Cam.h"
#include "CFirstPersonCamera.hpp"
#include "Entity/CPedGTA.h"
#include "Collision/ColPoint.h"
#include "pad.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>

extern uint32_t(*CWorld__ProcessLineOfSight)(
    CVector*, CVector*, CColPoint*, CEntityGTA**,
    bool, bool, bool, bool, bool, bool, bool, bool);

namespace
{
    constexpr uintptr_t kOff_FollowPed_Alpha = 0x009F9648;
    constexpr float kPi     = 3.14159265358979323846f;
    constexpr float kTwoPi  = 6.28318530717958647692f;
    constexpr float kHalfPi = 1.57079632679489661923f;

    // Mouse radians per input count (tuned for responsive desktop SA-MP feel)
    constexpr float kBaseSensH       = 0.0028f;
    constexpr float kBaseSensV       = 0.0024f;
    constexpr float kMaxAccumDelta   = 65536.0f;

    // Vertical pitch bounds (radians)
    constexpr float kMinOrbitAlpha   = -1.05f;
    constexpr float kMaxOrbitAlpha   =  0.72f;
    constexpr float kMinAimAlpha     = -1.32f;
    constexpr float kMaxAimAlpha     =  1.32f;

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

    float SafeNormalize(CVector& v)
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

    CVector FrontFromAngles(float alpha, float beta)
    {
        const float cosA = std::cos(alpha);
        return CVector(
            -cosA * std::cos(beta),
            -cosA * std::sin(beta),
            std::sin(alpha)
        );
    }

    CVector UpFromFront(const CVector& front)
    {
        // World up = (0, 0, 1); Right = Front x WorldUp = (front.y, -front.x, 0)
        CVector right(front.y, -front.x, 0.0f);
        if (SafeNormalize(right) <= 1e-5f)
            return CVector(0.0f, 0.0f, 1.0f);
        // Up = Right x Front
        CVector up(
            right.y * front.z - right.z * front.y,
            right.z * front.x - right.x * front.z,
            right.x * front.y - right.y * front.x
        );
        if (SafeNormalize(up) <= 1e-5f)
            return CVector(0.0f, 0.0f, 1.0f);
        return up;
    }

    bool IsThirdPersonOrbitMode(uint16_t mode)
    {
        switch (mode)
        {
            case MODE_FOLLOWPED:
            case MODE_FOLLOW_PED_WITH_BIND:
            case MODE_BEHINDCAR:
            case MODE_BEHINDBOAT:
            case MODE_CAM_ON_A_STRING:
            case MODE_WHEELCAM:
            case MODE_GTACLASSIC:
            case MODE_FIGHT_CAM:
            case MODE_FIGHT_CAM_RUNABOUT:
                return true;
            default:
                return false;
        }
    }

    bool IsVehicleCamMode(uint16_t mode)
    {
        switch (mode)
        {
            case MODE_BEHINDCAR:
            case MODE_BEHINDBOAT:
            case MODE_CAM_ON_A_STRING:
            case MODE_WHEELCAM:
                return true;
            default:
                return false;
        }
    }

    bool IsAimCamMode(uint16_t mode)
    {
        switch (mode)
        {
            case MODE_AIMING:
            case MODE_SNIPER:
            case MODE_ROCKETLAUNCHER:
            case MODE_1STPERSON:
            case MODE_M16_1STPERSON:
            case MODE_SNIPER_RUNABOUT:
            case MODE_ROCKETLAUNCHER_RUNABOUT:
            case MODE_1STPERSON_RUNABOUT:
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

    bool IsPedBodyAimMode(uint16_t mode)
    {
        switch (mode)
        {
            case MODE_AIMWEAPON:
            case MODE_AIMWEAPON_FROMCAR:
            case MODE_AIMWEAPON_ATTACHED:
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
        if (PcInputIsNativeInterfaceActive())
            return false;
        return PcInputIsEnabledForTouch() || g_mouseDrivenSession.load(std::memory_order_acquire);
    }

    void InitAnglesFromCam(const CCam* cam, bool aimMode)
    {
        float alpha = cam->Alpha;
        float beta  = cam->m_fHorizontalAngle;

        CVector f = cam->Front;
        if (SafeNormalize(f) > 0.01f)
        {
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

    void AlignPedToCameraDirection(CCamera& camera, float beta)
    {
        const float pedYaw = WrapAngle(beta + kHalfPi);
        camera.m_fOrientation = pedYaw;
        CPedGTA* pPed = GamePool_FindPlayerPed();
        if (pPed != nullptr && !pPed->IsInVehicle())
        {
            pPed->m_fCurrentRotation = pedYaw;
            pPed->m_fAimingRotation  = pedYaw;
        }
    }

    void DrainAndIntegrateMouse(const CCam* cam, bool aimMode)
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
        if (maxDelta > 0.0f)
        {
            rawDx = std::clamp(rawDx, -maxDelta, maxDelta);
            rawDy = std::clamp(rawDy, -maxDelta, maxDelta);
        }

        const float smooth = g_smoothing.load(std::memory_order_relaxed);
        float dx = rawDx;
        float dy = rawDy;
        if (smooth > 0.001f)
        {
            const float blend = 1.0f - smooth;
            g_smoothX = g_smoothX * smooth + rawDx * blend;
            g_smoothY = g_smoothY * smooth + rawDy * blend;
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

        const float aimMult = aimMode ? g_aimSensitivity.load(std::memory_order_relaxed) : 1.0f;
        float fovMult = 1.0f;
        if (aimMode && cam != nullptr && std::isfinite(cam->FOV) && cam->FOV > 5.0f && cam->FOV < 65.0f)
        {
            fovMult = std::clamp(cam->FOV / 70.0f, 0.20f, 1.0f);
        }

        if (dx != 0.0f || dy != 0.0f)
        {
            g_desiredBeta -= dx * kBaseSensH * aimMult * fovMult;
            g_desiredBeta = WrapAngle(g_desiredBeta);

            const float minA = aimMode ? kMinAimAlpha : kMinOrbitAlpha;
            const float maxA = aimMode ? kMaxAimAlpha : kMaxOrbitAlpha;
            g_desiredAlpha = std::clamp(g_desiredAlpha - dy * kBaseSensV * aimMult * fovMult, minA, maxA);

            g_mouseIdleFrames = 0;
            g_applied.fetch_add(1, std::memory_order_relaxed);
        }
        else if (g_mouseIdleFrames < 6000)
        {
            ++g_mouseIdleFrames;
        }
    }

    float ResolveOrbitDistance(const CCam* cam, const CVector& oldFront, CVector& outPivot)
    {
        CVector entityPos(0.0f, 0.0f, 0.0f);
        bool hasEntityPos = false;

        if (cam->CamTargetEntity != nullptr)
        {
            const auto* targetEnt = reinterpret_cast<const CEntityGTA*>(cam->CamTargetEntity);
            entityPos = targetEnt->GetPosition();
            hasEntityPos = std::isfinite(entityPos.x) && std::isfinite(entityPos.y) && std::isfinite(entityPos.z);
        }
        if (!hasEntityPos)
        {
            if (const CPedGTA* pPed = GamePool_FindPlayerPed())
            {
                entityPos = pPed->GetPosition();
                hasEntityPos = std::isfinite(entityPos.x) && std::isfinite(entityPos.y) && std::isfinite(entityPos.z);
            }
        }

        float dist = (std::isfinite(cam->m_fCameraDistance) && cam->m_fCameraDistance >= 1.2f && cam->m_fCameraDistance <= 25.0f)
            ? cam->m_fCameraDistance
            : (IsVehicleCamMode(static_cast<uint16_t>(cam->m_nMode)) ? 6.2f : 3.4f);

        if (hasEntityPos)
        {
            CVector approxTarget = entityPos;
            approxTarget.z += IsVehicleCamMode(static_cast<uint16_t>(cam->m_nMode)) ? 0.55f : 0.65f;

            const CVector delta(
                approxTarget.x - cam->Source.x,
                approxTarget.y - cam->Source.y,
                approxTarget.z - cam->Source.z
            );
            const float proj = delta.x * oldFront.x + delta.y * oldFront.y + delta.z * oldFront.z;
            if (std::isfinite(proj) && proj >= 1.0f && proj <= 28.0f)
            {
                dist = proj;
            }
        }

        outPivot = CVector(
            cam->Source.x + oldFront.x * dist,
            cam->Source.y + oldFront.y * dist,
            cam->Source.z + oldFront.z * dist
        );
        return dist;
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
    if (!PcInputIsEnabledForTouch())
    {
        g_mouseDrivenSession.store(false, std::memory_order_release);
        g_anglesInitialized = false;
    }
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

void NativeCameraMousePreProcess(CCamera& camera, CCam* targetCam)
{
    if (targetCam == nullptr || camera.m_nActiveCam >= 3)
        return;
    CCam* activeCam = &camera.m_aCams[camera.m_nActiveCam];
    if (targetCam != activeCam)
        return;

    const uint16_t mode = static_cast<uint16_t>(targetCam->m_nMode);
    if (!IsMouseCameraActive() || !IsControllableCamMode(mode))
    {
        g_anglesInitialized = false;
        g_lastMode = mode;
        return;
    }

    const bool aimMode = IsAimCamMode(mode);
    const bool wasAimMode = (g_lastMode != 0xFFFF) && IsAimCamMode(g_lastMode);

    if (!g_anglesInitialized)
    {
        InitAnglesFromCam(targetCam, aimMode);
    }
    else if (aimMode && !wasAimMode)
    {
        // Entering aim from free-look: keep camera look direction and turn ped toward camera,
        // NOT camera toward ped's sideways body rotation.
        g_desiredAlpha = std::clamp(g_desiredAlpha, kMinAimAlpha, kMaxAimAlpha);
        AlignPedToCameraDirection(camera, g_desiredBeta);
    }
    else if (!aimMode && wasAimMode)
    {
        // Leaving aim back to orbit: preserve where player was aiming seamlessly.
        g_desiredAlpha = std::clamp(g_desiredAlpha, kMinOrbitAlpha, kMaxOrbitAlpha);
    }

    DrainAndIntegrateMouse(targetCam, aimMode);

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

    if (g_libGTASA != 0 && (mode == MODE_FOLLOWPED || mode == MODE_FOLLOW_PED_WITH_BIND))
    {
        *reinterpret_cast<float*>(g_libGTASA + kOff_FollowPed_Alpha) = g_desiredAlpha;
    }

    if (IsPedBodyAimMode(mode))
    {
        AlignPedToCameraDirection(camera, g_desiredBeta);
    }

    g_lastMode = mode;
}

void NativeCameraMousePostProcess(CCamera& camera, CCam* targetCam)
{
    if (targetCam == nullptr || camera.m_nActiveCam >= 3)
        return;
    CCam* activeCam = &camera.m_aCams[camera.m_nActiveCam];
    if (targetCam != activeCam || !g_anglesInitialized)
        return;

    const uint16_t mode = static_cast<uint16_t>(targetCam->m_nMode);
    if (!IsMouseCameraActive() || !IsControllableCamMode(mode))
        return;

    // Optional vehicle auto-center only if explicitly enabled and mouse is idle
    if (IsVehicleCamMode(mode) && g_vehicleAutoCenter.load(std::memory_order_relaxed) && g_mouseIdleFrames > 45)
    {
        if (std::isfinite(targetCam->m_fHorizontalAngle))
            g_desiredBeta = WrapAngle(targetCam->m_fHorizontalAngle);
        if (std::isfinite(targetCam->Alpha))
            g_desiredAlpha = std::clamp(targetCam->Alpha, kMinOrbitAlpha, kMaxOrbitAlpha);
        return;
    }

    const CVector newFront = FrontFromAngles(g_desiredAlpha, g_desiredBeta);
    const CVector newUp = UpFromFront(newFront);

    // For third-person orbit modes, move the actual world camera position (Source)
    // whenever FollowPed_SA / FollowCar_SA clamped Alpha or drifted HorizontalAngle.
    if (IsThirdPersonOrbitMode(mode) && !CFirstPersonCamera::IsEnabled())
    {
        const float alphaDiff = std::fabs(targetCam->Alpha - g_desiredAlpha);
        const float betaDiff  = std::fabs(WrapAngle(targetCam->m_fHorizontalAngle - g_desiredBeta));
        if (alphaDiff > 1e-4f || betaDiff > 1e-4f)
        {
            CVector oldFront = targetCam->Front;
            if (SafeNormalize(oldFront) <= 1e-4f)
                oldFront = FrontFromAngles(targetCam->Alpha, targetCam->m_fHorizontalAngle);

            CVector pivot;
            const float dist = ResolveOrbitDistance(targetCam, oldFront, pivot);
            CVector desiredSource(
                pivot.x - newFront.x * dist,
                pivot.y - newFront.y * dist,
                pivot.z - newFront.z * dist
            );

            // Prevent world camera from clipping under ground or through walls when pitching steeply.
            if (CWorld__ProcessLineOfSight != nullptr)
            {
                CColPoint colPoint{};
                CEntityGTA* hitEntity = nullptr;
                if (CWorld__ProcessLineOfSight(
                        &pivot, &desiredSource, &colPoint, &hitEntity,
                        true, false, false, true, false, false, true, false))
                {
                    const CVector toHit(
                        colPoint.m_vecPoint.x - pivot.x,
                        colPoint.m_vecPoint.y - pivot.y,
                        colPoint.m_vecPoint.z - pivot.z
                    );
                    const float hitDist = std::sqrt(toHit.x * toHit.x + toHit.y * toHit.y + toHit.z * toHit.z);
                    if (std::isfinite(hitDist))
                    {
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

    if (g_libGTASA != 0 && (mode == MODE_FOLLOWPED || mode == MODE_FOLLOW_PED_WITH_BIND))
    {
        *reinterpret_cast<float*>(g_libGTASA + kOff_FollowPed_Alpha) = g_desiredAlpha;
    }

    if (IsPedBodyAimMode(mode))
    {
        AlignPedToCameraDirection(camera, g_desiredBeta);
    }
}

void NativeCameraMouseApply(CCamera& camera)
{
    (void)camera;
}
