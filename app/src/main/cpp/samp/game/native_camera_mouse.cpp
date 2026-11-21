#include "native_camera_mouse.h"

#include "../main.h"
#include "Camera.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>

namespace {
std::mutex g_mutex;
float g_pendingX = 0.0f;
float g_pendingY = 0.0f;
uint32_t g_received = 0;
uint32_t g_applied = 0;
float g_pendingApplyX = 0.0f;
float g_pendingApplyY = 0.0f;
float g_targetHorizontal = 0.0f;
float g_targetAlpha = 0.0f;
bool g_hasTarget = false;

constexpr float kSensitivity = 0.0025f;
constexpr float kMaxAccumulatedDelta = 65536.0f;
constexpr float kMinAlpha = -1.45f;
constexpr float kMaxAlpha = 1.45f;
}

void NativeCameraMouseEnqueue(float dx, float dy) {
    if (!std::isfinite(dx) || !std::isfinite(dy))
        return;

    std::lock_guard<std::mutex> lock(g_mutex);
    g_pendingX = std::clamp(g_pendingX + dx, -kMaxAccumulatedDelta, kMaxAccumulatedDelta);
    g_pendingY = std::clamp(g_pendingY + dy, -kMaxAccumulatedDelta, kMaxAccumulatedDelta);
    ++g_received;
    if (g_received % 60 == 1) {
        Log("[DIRECT-MOUSE] received=(%.2f,%.2f) pending=(%.2f,%.2f)",
            dx, dy, g_pendingX, g_pendingY);
    }
}

void NativeCameraMouseReset() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_pendingX = 0.0f;
    g_pendingY = 0.0f;
    g_pendingApplyX = 0.0f;
    g_pendingApplyY = 0.0f;
    g_hasTarget = false;
}

void NativeCameraMouseApply(CCamera& camera) {
    float dx = 0.0f;
    float dy = 0.0f;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        dx = g_pendingX;
        dy = g_pendingY;
        g_pendingX = 0.0f;
        g_pendingY = 0.0f;
        g_pendingApplyX = dx;
        g_pendingApplyY = dy;
    }
    if (dx == 0.0f && dy == 0.0f)
        return;

    CCam& cam = camera.m_aCams[camera.m_nActiveCam];
    if (!std::isfinite(cam.Alpha) || !std::isfinite(cam.m_fHorizontalAngle)) {
        Log("[DIRECT-MOUSE] ignored invalid angles mode=%d alpha=%f horizontal=%f",
            static_cast<int>(cam.m_nMode), cam.Alpha, cam.m_fHorizontalAngle);
        return;
    }

    // Apply each frame's mouse movement before CCamera::Process consumes the angles.
    cam.m_fHorizontalAngle -= dx * kSensitivity;
    cam.Alpha = std::clamp(cam.Alpha + dy * kSensitivity, kMinAlpha, kMaxAlpha);
    cam.AlphaSpeed = 0.0f;
    cam.BetaSpeed = 0.0f;
    cam.m_fAlphaSpeedOverOneFrame = 0.0f;
    cam.m_fBetaSpeedOverOneFrame = 0.0f;
    cam.m_fTrueAlpha = cam.Alpha;
    cam.m_fTrueBeta = cam.m_fHorizontalAngle;
    cam.m_fIdealAlpha = cam.Alpha;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_targetHorizontal = cam.m_fHorizontalAngle;
        g_targetAlpha = cam.Alpha;
        g_hasTarget = true;
    }

    const uint32_t applied = ++g_applied;
    if (applied % 30 == 1) {
        Log("[DIRECT-MOUSE] pre-process applied=%u mode=%d delta=(%.2f,%.2f) alpha=%.4f horizontal=%.4f",
            applied, static_cast<int>(cam.m_nMode), dx, dy, cam.Alpha, cam.m_fHorizontalAngle);
    }
}

void NativeCameraMouseReapply(CCamera& camera) {
    float targetHorizontal;
    float targetAlpha;
    bool hasTarget;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        targetHorizontal = g_targetHorizontal;
        targetAlpha = g_targetAlpha;
        hasTarget = g_hasTarget;
        g_pendingApplyX = 0.0f;
        g_pendingApplyY = 0.0f;
        g_hasTarget = false;
    }
    if (!hasTarget)
        return;

    CCam& cam = camera.m_aCams[camera.m_nActiveCam];
    if (!std::isfinite(targetAlpha) || !std::isfinite(targetHorizontal))
        return;

    cam.m_fHorizontalAngle = targetHorizontal;
    cam.Alpha = targetAlpha;
    cam.AlphaSpeed = 0.0f;
    cam.BetaSpeed = 0.0f;
    cam.m_fAlphaSpeedOverOneFrame = 0.0f;
    cam.m_fBetaSpeedOverOneFrame = 0.0f;
    cam.m_fTrueAlpha = targetAlpha;
    cam.m_fTrueBeta = targetHorizontal;
    cam.m_fIdealAlpha = targetAlpha;

    ((void (*)(CCamera*, bool, bool))(g_libGTASA + 0x472BF4))(&camera, false, false);
}