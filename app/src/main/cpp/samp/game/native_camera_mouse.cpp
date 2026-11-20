#include "native_camera_mouse.h"

#include "../main.h"

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

constexpr float kSensitivity = 0.0025f;
constexpr float kMaxFrameDelta = 180.0f;
constexpr float kMinAlpha = -1.45f;
constexpr float kMaxAlpha = 1.45f;
}

void NativeCameraMouseEnqueue(float dx, float dy) {
    if (!std::isfinite(dx) || !std::isfinite(dy))
        return;

    dx = std::clamp(dx, -4096.0f, 4096.0f);
    dy = std::clamp(dy, -4096.0f, 4096.0f);
    std::lock_guard<std::mutex> lock(g_mutex);
    g_pendingX = std::clamp(g_pendingX + dx, -32767.0f, 32767.0f);
    g_pendingY = std::clamp(g_pendingY + dy, -32767.0f, 32767.0f);
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
    }
    if (dx == 0.0f && dy == 0.0f)
        return;

    dx = std::clamp(dx, -kMaxFrameDelta, kMaxFrameDelta);
    dy = std::clamp(dy, -kMaxFrameDelta, kMaxFrameDelta);

    CCam& cam = camera.m_aCams[camera.m_nActiveCam];
    if (!std::isfinite(cam.Alpha) || !std::isfinite(cam.m_fHorizontalAngle)) {
        Log("[DIRECT-MOUSE] ignored invalid angles mode=%d alpha=%f horizontal=%f",
            static_cast<int>(cam.m_nMode), cam.Alpha, cam.m_fHorizontalAngle);
        return;
    }

    // FollowPed and the aiming modes consume these native CCam angles while
    // CCamera::Process is running. Applying them before Process is important:
    // changing Front after Process is too late for this build's camera path.
    cam.m_fHorizontalAngle -= dx * kSensitivity;
    cam.Alpha = std::clamp(cam.Alpha + dy * kSensitivity, kMinAlpha, kMaxAlpha);
    cam.m_fTrueAlpha = cam.Alpha;
    cam.m_fTrueBeta = cam.m_fHorizontalAngle;
    cam.m_fIdealAlpha = cam.Alpha;

    const uint32_t applied = ++g_applied;
    if (applied % 30 == 1) {
        Log("[DIRECT-MOUSE] pre-process applied=%u mode=%d delta=(%.2f,%.2f) alpha=%.4f horizontal=%.4f",
            applied, static_cast<int>(cam.m_nMode), dx, dy, cam.Alpha, cam.m_fHorizontalAngle);
    }
}
