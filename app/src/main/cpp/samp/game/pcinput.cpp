#include "../main.h"
#include "game.h"
#include "pad.h"
#include "../gui/gui.h"
#include "../net/netgame.h"
#include "../java/jniutil.h"
#include "../vendor/encoding/encoding.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <mutex>

extern bool bNeedEnterVehicle;
class CNetGame;
extern CGame* pGame;
extern UI* pUI;
extern CNetGame* pNetGame;

namespace
{
    constexpr int kPcActionCount = static_cast<int>(PcInputAction::COUNT);
    std::array<bool, kPcActionCount> g_pressed{};
    std::array<bool, kPcActionCount> g_justPressed{};
    std::atomic<bool> g_inputEnabled{true};
    bool g_driverVehicleEntryRequested = false;
    int g_weaponWheelSteps = 0;
    std::mutex g_mouseDeltaMutex;
    float g_mouseDeltaX = 0.0f;
    float g_mouseDeltaY = 0.0f;
    float g_frameMouseDeltaX = 0.0f;
    float g_frameMouseDeltaY = 0.0f;
    float g_smoothedMouseDeltaX = 0.0f;
    float g_smoothedMouseDeltaY = 0.0f;
    std::atomic<float> g_mouseMaxDelta{120.0f};
    std::atomic<float> g_mouseSmoothing{0.0f};
    std::atomic<int> g_primaryMouseAction{-1};
    std::atomic<int> g_secondaryMouseAction{-1};
    std::atomic<bool> g_mouseAimControlActive{false};
    std::atomic<uint32_t> g_mouseReceivedCount{0};
    std::atomic<uint32_t> g_mouseAppliedCount{0};
    std::atomic<uint32_t> g_cameraAppliedCount{0};
    std::atomic<bool> g_textEntryActive{false};
    std::atomic<bool> g_nativeInterfaceActive{false};

    bool IsActionIndexValid(int action)
    {
        return action >= 0 && action < kPcActionCount;
    }

    std::atomic<int>* MouseActionForButton(int button)
    {
        if (button == 1)
            return &g_primaryMouseAction;
        if (button == 2)
            return &g_secondaryMouseAction;
        return nullptr;
    }

    std::string CodePointToUtf8(int codePoint)
    {
        std::string text;
        if (codePoint <= 0x7F)
        {
            text.push_back(static_cast<char>(codePoint));
        }
        else if (codePoint <= 0x7FF)
        {
            text.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
            text.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        }
        else if (codePoint <= 0xFFFF)
        {
            text.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
            text.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
            text.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        }
        else if (codePoint <= 0x10FFFF)
        {
            text.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
            text.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
            text.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
            text.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        }
        return text;
    }
}

bool PcInputIsPressed(PcInputAction action)
{
    const int index = static_cast<int>(action);
    if (!g_inputEnabled.load() || !IsActionIndexValid(index))
        return false;
    return g_pressed[index] || g_primaryMouseAction.load() == index ||
           g_secondaryMouseAction.load() == index;
}

bool PcInputConsumePress(PcInputAction action)
{
    const int index = static_cast<int>(action);
    if (!g_inputEnabled || !IsActionIndexValid(index) || !g_justPressed[index])
        return false;

    g_justPressed[index] = false;
    return true;
}

bool PcInputConsumeDriverVehicleEntry()
{
    const bool requested = g_driverVehicleEntryRequested;
    g_driverVehicleEntryRequested = false;
    return requested;
}

void PcInputSetEnabled(bool enabled)
{
    g_inputEnabled.store(enabled);
    g_weaponWheelSteps = 0;
    {
        std::lock_guard<std::mutex> lock(g_mouseDeltaMutex);
        g_mouseDeltaX = 0.0f;
        g_mouseDeltaY = 0.0f;
    }
    g_frameMouseDeltaX = 0.0f;
    g_frameMouseDeltaY = 0.0f;
    g_smoothedMouseDeltaX = 0.0f;
    g_smoothedMouseDeltaY = 0.0f;
    g_primaryMouseAction.store(-1);
    g_secondaryMouseAction.store(-1);
    g_mouseAimControlActive.store(false);

    if (!enabled)
    {
        g_pressed.fill(false);
        g_justPressed.fill(false);
        g_driverVehicleEntryRequested = false;
    }
    Log("PC input C++: enabled=%d", enabled);
}

void PcInputSetAction(int action, bool pressed)
{
    if (!IsActionIndexValid(action))
    {
        Log("PC input C++: rejected invalid action=%d", action);
        return;
    }

    if (!g_inputEnabled)
        pressed = false;

    const bool wasPressed = g_pressed[action];
    g_pressed[action] = pressed;
    if (pressed && !wasPressed)
        g_justPressed[action] = true;

    if (wasPressed != pressed)
        Log("PC input C++: action=%d pressed=%d", action, pressed);

    if (action == static_cast<int>(PcInputAction::VEHICLE) && pressed && !wasPressed)
    {
        CPlayerPed* playerPed = pGame != nullptr ? pGame->FindPlayerPed() : nullptr;
        if (playerPed != nullptr && playerPed->IsInVehicle())
        {
            playerPed->ExitCurrentVehicle();
            Log("PC input action: requested vehicle exit");
        }
        else if (playerPed != nullptr && pNetGame != nullptr)
        {
            bNeedEnterVehicle = true;
            g_driverVehicleEntryRequested = true;
            Log("PC input action: requested vehicle entry");
        }
    }

    if (action == static_cast<int>(PcInputAction::ENTER) && pressed && !wasPressed &&
        pUI != nullptr && pUI->dialog()->visible())
    {
        pUI->dialog()->confirm();
        Log("PC input action: confirmed native dialog");
    }

    if (pressed && !wasPressed && action == static_cast<int>(PcInputAction::WEAPON_NEXT))
        g_weaponWheelSteps = std::clamp(g_weaponWheelSteps + 1, -8, 8);
    else if (pressed && !wasPressed &&
             action == static_cast<int>(PcInputAction::WEAPON_PREVIOUS))
        g_weaponWheelSteps = std::clamp(g_weaponWheelSteps - 1, -8, 8);

    if (action == static_cast<int>(PcInputAction::TAB) && wasPressed != pressed && pUI)
    {
        if (pressed)
            pUI->playertablist()->show();
        else if (pUI->playertablist()->visible())
            pUI->playertablist()->hide();
    }
}

int PcInputConsumeWeaponWheel(int direction)
{
    if (!g_inputEnabled || direction == 0)
        return 0;

    if ((direction > 0 && g_weaponWheelSteps > 0) ||
        (direction < 0 && g_weaponWheelSteps < 0))
    {
        g_weaponWheelSteps -= direction > 0 ? 1 : -1;
        return direction;
    }
    return 0;
}

void PcInputAddMouseDelta(float dx, float dy)
{
    if (!std::isfinite(dx) || !std::isfinite(dy))
    {
        Log("PC input C++: rejected non-finite mouse delta");
        return;
    }
    if (!g_inputEnabled.load() || g_nativeInterfaceActive.load())
        return;

    std::lock_guard<std::mutex> lock(g_mouseDeltaMutex);
    g_mouseDeltaX = std::clamp(g_mouseDeltaX + dx, -32767.0f, 32767.0f);
    g_mouseDeltaY = std::clamp(g_mouseDeltaY + dy, -32767.0f, 32767.0f);
    const uint32_t received = g_mouseReceivedCount.fetch_add(1) + 1;
    if (received % 60 == 1)
    {
        Log("[CPP-MOUSE] received=(%.2f,%.2f) accumulated=(%.2f,%.2f)",
            dx, dy, g_mouseDeltaX, g_mouseDeltaY);
    }
}

void PcInputApplyMouse()
{
    float dx = 0.0f;
    float dy = 0.0f;
    {
        std::lock_guard<std::mutex> lock(g_mouseDeltaMutex);
        dx = g_mouseDeltaX;
        dy = g_mouseDeltaY;
        g_mouseDeltaX = 0.0f;
        g_mouseDeltaY = 0.0f;
    }
    const uint32_t frame = g_mouseAppliedCount.fetch_add(1) + 1;
    if (frame % 120 == 1)
    {
        Log("[FRAME-MOUSE] frame=%u consumed=(%.2f,%.2f)",
            frame, dx, dy);
    }

    if (!g_inputEnabled.load() || g_nativeInterfaceActive.load())
    {
        g_frameMouseDeltaX = 0.0f;
        g_frameMouseDeltaY = 0.0f;
        g_smoothedMouseDeltaX = 0.0f;
        g_smoothedMouseDeltaY = 0.0f;
        return;
    }

    if (dx == 0.0f && dy == 0.0f)
    {
        g_frameMouseDeltaX = 0.0f;
        g_frameMouseDeltaY = 0.0f;
        g_smoothedMouseDeltaX = 0.0f;
        g_smoothedMouseDeltaY = 0.0f;
        return;
    }

    const float maxDelta = g_mouseMaxDelta.load();
    const float smoothing = g_mouseSmoothing.load();
    dx = std::clamp(dx, -maxDelta, maxDelta);
    dy = std::clamp(dy, -maxDelta, maxDelta);
    g_smoothedMouseDeltaX = dx * (1.0f - smoothing) +
                            g_smoothedMouseDeltaX * smoothing;
    g_smoothedMouseDeltaY = dy * (1.0f - smoothing) +
                            g_smoothedMouseDeltaY * smoothing;
    g_frameMouseDeltaX = g_smoothedMouseDeltaX;
    g_frameMouseDeltaY = g_smoothedMouseDeltaY;
}

void PcInputSetMouseOptions(float maxDelta, float smoothing)
{
    if (!std::isfinite(maxDelta) || !std::isfinite(smoothing))
    {
        Log("PC input C++: rejected non-finite mouse options");
        return;
    }
    g_mouseMaxDelta.store(std::clamp(maxDelta, 1.0f, 500.0f));
    g_mouseSmoothing.store(std::clamp(smoothing, 0.0f, 0.9f));
}

void PcInputSetMouseButton(int button, int action, bool pressed)
{
    std::atomic<int>* mouseAction = MouseActionForButton(button);
    if (mouseAction == nullptr)
    {
        Log("PC input C++: rejected mouse button=%d", button);
        return;
    }

    if (!g_inputEnabled.load() || g_nativeInterfaceActive.load() ||
        !IsActionIndexValid(action))
    {
        pressed = false;
    }
    mouseAction->store(pressed ? action : -1);
    if (pressed && action == static_cast<int>(PcInputAction::AIM))
        g_mouseAimControlActive.store(true);
    Log("[GAME-MOUSE] button=%d action=%d pressed=%d",
        button, action, pressed);
}

bool PcInputIsMouseAimControlActive()
{
    return g_mouseAimControlActive.load();
}

void PcInputMouseLeave()
{
    {
        std::lock_guard<std::mutex> lock(g_mouseDeltaMutex);
        g_mouseDeltaX = 0.0f;
        g_mouseDeltaY = 0.0f;
    }
    g_primaryMouseAction.store(-1);
    g_secondaryMouseAction.store(-1);
}

int16_t PcInputApplyMouseLookX(int16_t current)
{
    const float delta = g_frameMouseDeltaX;
    g_frameMouseDeltaX = 0.0f;
    const int appliedDelta = static_cast<int>(std::lround(delta));
    const int result = std::clamp(static_cast<int>(current) + appliedDelta, -32768, 32767);
    static uint32_t lookCallCount = 0;
    const bool logCall = ++lookCallCount % 120 == 1;
    const bool logMotion = appliedDelta != 0 &&
        g_cameraAppliedCount.fetch_add(1) % 60 == 0;
    if (logCall || logMotion)
    {
        Log("[GAME-MOUSE] camera lookX call=%u base=%d mouse=%.2f applied=%d",
            lookCallCount, static_cast<int>(current), delta, result);
    }
    return static_cast<int16_t>(result);
}

int16_t PcInputApplyMouseLookY(int16_t current)
{
    const float delta = g_frameMouseDeltaY;
    g_frameMouseDeltaY = 0.0f;
    const int appliedDelta = static_cast<int>(std::lround(delta));
    const int result = std::clamp(static_cast<int>(current) + appliedDelta, -32768, 32767);
    static uint32_t lookCallCount = 0;
    const bool logCall = ++lookCallCount % 120 == 1;
    const bool logMotion = appliedDelta != 0 &&
        g_cameraAppliedCount.fetch_add(1) % 60 == 0;
    if (logCall || logMotion)
    {
        Log("[GAME-MOUSE] camera lookY call=%u base=%d mouse=%.2f applied=%d",
            lookCallCount, static_cast<int>(current), delta, result);
    }
    return static_cast<int16_t>(result);
}

void PcInputOpenChat()
{
    if (!g_inputEnabled || pUI == nullptr || pGame == nullptr)
        return;

    pUI->chat()->setVisible(true);
    pUI->keyboard()->show(pUI->chat());
}

void PcInputHandleTextKey(int key, int unicode)
{
    if (!g_textEntryActive.load() || pUI == nullptr || !pUI->keyboard()->visible())
        return;

    KeyboardInput* input = pUI->keyboard()->input();
    switch (key)
    {
        case 0:
            input->popCharFromInput();
            return;
        case 1:
            pUI->keyboard()->send();
            return;
        case 2:
            pUI->keyboard()->hide();
            return;
        default:
            break;
    }

    if (unicode < 0x20 || unicode > 0x10FFFF)
        return;

    const std::string utf8 = CodePointToUtf8(unicode);
    if (utf8.empty())
        return;

    const std::string encoded = Encoding::utf2cp(utf8);
    if (!encoded.empty())
        input->addTextToInput(encoded);
    else
        Log("PC input: Unicode U+%04X is not representable in Windows-1251", unicode);
}

void PcInputHandleEscape()
{
    if (!g_nativeInterfaceActive.load() || pUI == nullptr)
        return;

    if (pUI->keyboard()->visible())
    {
        pUI->keyboard()->hide();
        return;
    }
    if (pUI->dialog()->visible())
    {
        Dialog* dialog = pUI->dialog();
        if (pNetGame != nullptr)
        {
            pNetGame->SendDialogResponse(
                dialog->dialogID(), 0, dialog->content()->listItem(),
                dialog->content()->inputString().c_str());
        }
        dialog->hide();
        Log("PC input: cancelled native dialog with Escape");
        return;
    }
    if (pUI->playertablist()->visible())
    {
        pUI->playertablist()->hide();
    }
}

bool PcInputIsTextEntryActive()
{
    return g_textEntryActive.load();
}

bool PcInputIsNativeInterfaceActive()
{
    return g_nativeInterfaceActive.load();
}

void PcInputRefreshNativeInterfaceState()
{
    const bool textEntry = pUI != nullptr && pUI->keyboard()->visible();
    const bool interfaceActive = textEntry ||
        (pUI != nullptr && (pUI->dialog()->visible() || pUI->playertablist()->visible()));
    g_textEntryActive.store(textEntry);
    g_nativeInterfaceActive.store(interfaceActive);
    if (interfaceActive)
    {
        std::lock_guard<std::mutex> lock(g_mouseDeltaMutex);
        g_mouseDeltaX = 0.0f;
        g_mouseDeltaY = 0.0f;
        g_frameMouseDeltaX = 0.0f;
        g_frameMouseDeltaY = 0.0f;
        g_smoothedMouseDeltaX = 0.0f;
        g_smoothedMouseDeltaY = 0.0f;
        g_primaryMouseAction.store(-1);
        g_secondaryMouseAction.store(-1);
    }
}

extern "C"
{
    JNIEXPORT void JNICALL
    Java_com_gta_game_input_KeyboardMouseBridge_nativeSetEnabled(
        JNIEnv*, jclass, jboolean enabled)
    {
        Log("PC input JNI: enabled=%d", enabled);
        PcInputSetEnabled(enabled == JNI_TRUE);
    }

    JNIEXPORT void JNICALL
    Java_com_gta_game_input_KeyboardMouseBridge_nativeSetAction(
        JNIEnv*, jclass, jint action, jboolean pressed)
    {
        Log("PC input JNI: action=%d pressed=%d", action, pressed);
        PcInputSetAction(action, pressed == JNI_TRUE);
    }

    JNIEXPORT void JNICALL
    Java_com_gta_game_input_KeyboardMouseBridge_nativeSetMouseOptions(
        JNIEnv*, jclass, jfloat maxDelta, jfloat smoothing)
    {
        PcInputSetMouseOptions(maxDelta, smoothing);
    }

    JNIEXPORT void JNICALL
    Java_com_gta_game_input_KeyboardMouseBridge_nativeMouseMove(
        JNIEnv*, jclass, jfloat dx, jfloat dy)
    {
        static uint32_t mouseEventCount = 0;
        if ((dx != 0.0f || dy != 0.0f) && ++mouseEventCount % 60 == 1)
            Log("[JNI-MOUSE] delta=(%.2f,%.2f)", dx, dy);
        PcInputAddMouseDelta(dx, dy);
    }

    JNIEXPORT jint JNICALL
    Java_com_gta_game_input_KeyboardMouseBridge_nativeGetMouseFrameCount(
        JNIEnv*, jclass)
    {
        return static_cast<jint>(g_mouseAppliedCount.load());
    }

    JNIEXPORT void JNICALL
    Java_com_gta_game_input_KeyboardMouseBridge_nativeMouseButtonDown(
        JNIEnv*, jclass, jint button, jint action)
    {
        Log("[JNI-MOUSE] buttonDown button=%d action=%d", button, action);
        PcInputSetMouseButton(button, action, true);
    }

    JNIEXPORT void JNICALL
    Java_com_gta_game_input_KeyboardMouseBridge_nativeMouseButtonUp(
        JNIEnv*, jclass, jint button)
    {
        Log("[JNI-MOUSE] buttonUp button=%d", button);
        PcInputSetMouseButton(button, -1, false);
    }

    JNIEXPORT void JNICALL
    Java_com_gta_game_input_KeyboardMouseBridge_nativeMouseEnter(JNIEnv*, jclass)
    {
        Log("[JNI-MOUSE] enter");
    }

    JNIEXPORT void JNICALL
    Java_com_gta_game_input_KeyboardMouseBridge_nativeMouseLeave(JNIEnv*, jclass)
    {
        Log("[JNI-MOUSE] leave");
        PcInputMouseLeave();
    }

    JNIEXPORT void JNICALL
    Java_com_gta_game_input_KeyboardMouseBridge_nativeMouseWheel(
        JNIEnv*, jclass, jint direction)
    {
        if (!g_inputEnabled || direction == 0)
            return;

        g_weaponWheelSteps = std::clamp(g_weaponWheelSteps + (direction > 0 ? 1 : -1), -8, 8);
        Log("PC input JNI: weapon wheel direction=%d", direction);
    }

    JNIEXPORT void JNICALL
    Java_com_gta_game_input_KeyboardMouseBridge_nativeOpenChat(JNIEnv*, jclass)
    {
        Log("PC input JNI: open local chat");
        PcInputOpenChat();
    }

    JNIEXPORT void JNICALL
    Java_com_gta_game_input_KeyboardMouseBridge_nativeTextInputKey(
        JNIEnv*, jclass, jint key, jint unicode)
    {
        Log("PC input JNI: text key=%d unicode=%d", key, unicode);
        PcInputHandleTextKey(key, unicode);
    }

    JNIEXPORT void JNICALL
    Java_com_gta_game_input_KeyboardMouseBridge_nativeHandleEscape(JNIEnv*, jclass)
    {
        Log("PC input JNI: escape");
        PcInputHandleEscape();
    }

    JNIEXPORT jboolean JNICALL
    Java_com_gta_game_input_KeyboardMouseBridge_nativeIsTextEntryActive(JNIEnv*, jclass)
    {
        return PcInputIsTextEntryActive() ? JNI_TRUE : JNI_FALSE;
    }

    JNIEXPORT jboolean JNICALL
    Java_com_gta_game_input_KeyboardMouseBridge_nativeIsInterfaceActive(JNIEnv*, jclass)
    {
        return PcInputIsNativeInterfaceActive() ? JNI_TRUE : JNI_FALSE;
    }
}
