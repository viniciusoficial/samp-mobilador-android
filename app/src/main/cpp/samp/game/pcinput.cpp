#include "../main.h"
#include "game.h"
#include "pad.h"
#include "../gui/gui.h"
#include "../java/jniutil.h"
#include "../vendor/encoding/encoding.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <dlfcn.h>

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
    bool g_inputEnabled = true;
    bool g_driverVehicleEntryRequested = false;
    int g_weaponWheelSteps = 0;
    float g_mouseDeltaX = 0.0f;
    float g_mouseDeltaY = 0.0f;
    uint32_t g_mouseAppliedCount = 0;
    std::atomic<bool> g_textEntryActive{false};
    std::atomic<bool> g_nativeInterfaceActive{false};

    struct MouseControllerState
    {
        uint8_t buttons[4];
        int16_t lookX;
        int16_t lookY;
        uint32_t wheel;
        float pointerX;
        float pointerY;
    };

    static_assert(sizeof(MouseControllerState) == 20);
    static_assert(offsetof(MouseControllerState, lookX) == 4);
    static_assert(offsetof(MouseControllerState, lookY) == 6);

    MouseControllerState* GetNewMouseControllerState()
    {
        static void* gameLibrary = dlopen("libGame.so", RTLD_NOW);
        static MouseControllerState* state = gameLibrary
            ? static_cast<MouseControllerState*>(
                dlsym(gameLibrary, "_ZN4CPad23NewMouseControllerStateE"))
            : nullptr;

        if (state == nullptr)
        {
            static bool logged = false;
            if (!logged)
            {
                Log("PC input: libGame CPad mouse state symbol is unavailable");
                logged = true;
            }
        }
        return state;
    }

    bool IsActionIndexValid(int action)
    {
        return action >= 0 && action < kPcActionCount;
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
    return g_inputEnabled && IsActionIndexValid(index) && g_pressed[index];
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
    g_inputEnabled = enabled;
    g_weaponWheelSteps = 0;
    g_mouseDeltaX = 0.0f;
    g_mouseDeltaY = 0.0f;

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
    if (!g_inputEnabled)
        return;

    g_mouseDeltaX = std::clamp(g_mouseDeltaX + dx, -32767.0f, 32767.0f);
    g_mouseDeltaY = std::clamp(g_mouseDeltaY + dy, -32767.0f, 32767.0f);
}

void PcInputApplyMouse()
{
    if (!g_inputEnabled)
    {
        MouseControllerState* state = GetNewMouseControllerState();
        if (state != nullptr)
        {
            state->lookX = 0;
            state->lookY = 0;
        }
        return;
    }

    const float dx = g_mouseDeltaX;
    const float dy = g_mouseDeltaY;
    g_mouseDeltaX = 0.0f;
    g_mouseDeltaY = 0.0f;

    MouseControllerState* state = GetNewMouseControllerState();
    if (state == nullptr)
        return;

    state->lookX = static_cast<int16_t>(std::clamp(dx, -32767.0f, 32767.0f));
    state->lookY = static_cast<int16_t>(std::clamp(dy, -32767.0f, 32767.0f));
    if (dx == 0.0f && dy == 0.0f)
        return;

    if (++g_mouseAppliedCount % 60 == 0)
    {
        Log("PC input action: camera delta=(%d,%d)",
            static_cast<int>(state->lookX), static_cast<int>(state->lookY));
    }
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
    Java_com_gta_game_input_KeyboardMouseBridge_nativeMouseDelta(
        JNIEnv*, jclass, jfloat dx, jfloat dy)
    {
        static uint32_t mouseEventCount = 0;
        if ((dx != 0.0f || dy != 0.0f) && ++mouseEventCount % 60 == 0)
            Log("PC input JNI: mouse delta=(%.2f,%.2f)", dx, dy);
        PcInputAddMouseDelta(dx, dy);
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
