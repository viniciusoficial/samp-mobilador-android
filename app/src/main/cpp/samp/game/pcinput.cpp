#include "../main.h"
#include "game.h"
#include "native_camera_mouse.h"
#include "../net/netgame.h"
#include "../gui/gui.h"
extern void SetNexionAspectRatio(float aspectRatio);
#include "../vendor/encoding/encoding.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <string>

extern UI* pUI;
extern CGame* pGame;
extern CNetGame* pNetGame;
extern bool bNeedEnterVehicle;

namespace {
constexpr int kActionCount = static_cast<int>(PcInputAction::COUNT);
constexpr int kMaxMouseButtons = 6; // indices 1..5

std::array<std::atomic<bool>, kActionCount> g_pressed{};
std::array<std::atomic<bool>, kActionCount> g_justPressed{};
// Latched press keeps quick clicks/taps alive for at least 1 complete game tick.
std::array<std::atomic<bool>, kActionCount> g_latchedPress{};
std::array<bool, kActionCount> g_frameLatchedSnapshot{};

std::array<std::atomic<int>, kMaxMouseButtons> g_mouseButtonAction;
std::array<std::atomic<bool>, kMaxMouseButtons> g_mouseButtonPressed{};

std::atomic<bool> g_inputEnabled{true};
std::atomic<bool> g_externalInputPresent{false};
std::atomic<bool> g_textEntryActive{false};
std::atomic<bool> g_nativeInterfaceActive{false};
std::atomic<bool> g_tabListOnly{false};

std::atomic<bool> g_driverVehicleEntryRequested{false};
std::atomic<bool> g_passengerVehicleEntryRequested{false};
std::atomic<int> g_weaponWheelSteps{0};
std::atomic<int> g_pendingWeaponSlot{-1};

// PC gameplay tuning (updated via /mob settings)
std::atomic<float> g_maxDelta{0.0f};
std::atomic<float> g_smoothing{0.0f};
std::atomic<float> g_aimSensitivity{0.85f};
std::atomic<float> g_strafeMultiplier{1.45f};
std::atomic<bool> g_pcSprintEnabled{true};
std::atomic<bool> g_fastWeaponSwitch{true};
std::atomic<bool> g_vehicleAutoCenter{false};

struct InitMouseActions {
    InitMouseActions() {
        for (auto& a : g_mouseButtonAction) a.store(-1);
    }
} g_initMouseActions;

bool IsActionIndexValid(int action)
{
    return action >= 0 && action < kActionCount;
}

std::string CodePointToUtf8(int codePoint)
{
    if (codePoint < 0x20 || codePoint > 0x10FFFF ||
        (codePoint >= 0xD800 && codePoint <= 0xDFFF))
    {
        return {};
    }

    std::string utf8;
    if (codePoint <= 0x7F)
    {
        utf8.push_back(static_cast<char>(codePoint));
    }
    else if (codePoint <= 0x7FF)
    {
        utf8.push_back(static_cast<char>(0xC0 | ((codePoint >> 6) & 0x1F)));
        utf8.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
    else if (codePoint <= 0xFFFF)
    {
        utf8.push_back(static_cast<char>(0xE0 | ((codePoint >> 12) & 0x0F)));
        utf8.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        utf8.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
    else
    {
        utf8.push_back(static_cast<char>(0xF0 | ((codePoint >> 18) & 0x07)));
        utf8.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
        utf8.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        utf8.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
    return utf8;
}

int DefaultActionForMouseButton(int button)
{
    if (button == 1) return static_cast<int>(PcInputAction::FIRE);
    if (button == 2) return static_cast<int>(PcInputAction::AIM);
    if (button == 3) return static_cast<int>(PcInputAction::LOOK_BEHIND);
    return -1;
}

void SyncCameraOptions()
{
    NativeCameraMouseSetOptions(
        g_maxDelta.load(),
        g_smoothing.load(),
        g_aimSensitivity.load(),
        g_vehicleAutoCenter.load()
    );
}
} // namespace

void PcInputBeginFrame()
{
    for (int i = 0; i < kActionCount; ++i)
    {
        g_frameLatchedSnapshot[i] = g_latchedPress[i].exchange(false, std::memory_order_acq_rel);
    }
}

void PcInputEndFrame()
{
    for (int i = 0; i < kActionCount; ++i)
    {
        g_frameLatchedSnapshot[i] = false;
    }
}

bool PcInputIsPressed(PcInputAction action)
{
    const int index = static_cast<int>(action);
    if (!g_inputEnabled.load() || g_nativeInterfaceActive.load() || !IsActionIndexValid(index))
        return false;
    return g_pressed[index].load(std::memory_order_acquire) ||
           g_frameLatchedSnapshot[index] ||
           g_latchedPress[index].load(std::memory_order_acquire);
}

bool PcInputIsJustPressed(PcInputAction action)
{
    const int index = static_cast<int>(action);
    if (!g_inputEnabled.load() || g_nativeInterfaceActive.load() || !IsActionIndexValid(index))
        return false;
    return g_frameLatchedSnapshot[index] ||
           g_justPressed[index].load(std::memory_order_acquire);
}

bool PcInputOwnsTargeting()
{
    return g_inputEnabled.load() && !g_nativeInterfaceActive.load();
}

bool PcInputConsumePress(PcInputAction action)
{
    const int index = static_cast<int>(action);
    if (!g_inputEnabled.load() || g_nativeInterfaceActive.load() || !IsActionIndexValid(index))
        return false;
    const bool just = g_justPressed[index].exchange(false, std::memory_order_acq_rel);
    const bool latched = g_frameLatchedSnapshot[index];
    g_frameLatchedSnapshot[index] = false;
    return just || latched;
}

bool PcInputConsumeDriverVehicleEntry()
{
    return g_driverVehicleEntryRequested.exchange(false, std::memory_order_acq_rel);
}

bool PcInputConsumePassengerVehicleEntry()
{
    return g_passengerVehicleEntryRequested.exchange(false, std::memory_order_acq_rel);
}

void PcInputSetEnabled(bool enabled)
{
    g_inputEnabled.store(enabled);
    g_weaponWheelSteps.store(0);
    g_pendingWeaponSlot.store(-1);
    NativeCameraMouseReset();
    PcInputReleaseMouseButtons();

    if (!enabled)
    {
        for (int i = 0; i < kActionCount; ++i)
        {
            g_pressed[i].store(false, std::memory_order_release);
            g_justPressed[i].store(false, std::memory_order_release);
            g_latchedPress[i].store(false, std::memory_order_release);
            g_frameLatchedSnapshot[i] = false;
        }
        g_driverVehicleEntryRequested.store(false);
        g_passengerVehicleEntryRequested.store(false);
    }
    Log("PC input C++: enabled=%d", enabled);
}

bool PcInputIsExternalInputPresent()
{
    return g_inputEnabled.load() && g_externalInputPresent.load();
}

bool PcInputIsEnabledForTouch()
{
    return g_inputEnabled.load() && g_externalInputPresent.load() &&
           !g_nativeInterfaceActive.load();
}

void PcInputSetExternalInputPresent(bool present)
{
    const bool wasPresent = g_externalInputPresent.exchange(present);
    if (wasPresent && !present)
    {
        for (int i = 0; i < kActionCount; ++i)
        {
            g_pressed[i].store(false, std::memory_order_release);
            g_justPressed[i].store(false, std::memory_order_release);
            g_latchedPress[i].store(false, std::memory_order_release);
            g_frameLatchedSnapshot[i] = false;
        }
        g_weaponWheelSteps.store(0);
        g_pendingWeaponSlot.store(-1);
        NativeCameraMouseReset();
        PcInputReleaseMouseButtons();
    }
}

void PcInputSetAction(int action, bool pressed)
{
    if (!IsActionIndexValid(action))
    {
        Log("PC input C++: rejected invalid action=%d", action);
        return;
    }

    if (!g_inputEnabled.load())
        pressed = false;

    const bool wasPressed = g_pressed[action].load(std::memory_order_acquire);
    g_pressed[action].store(pressed, std::memory_order_release);
    if (pressed && !wasPressed)
    {
        g_justPressed[action].store(true, std::memory_order_release);
        g_latchedPress[action].store(true, std::memory_order_release);
    }

    if (action == static_cast<int>(PcInputAction::VEHICLE) && pressed && !wasPressed &&
        !g_nativeInterfaceActive.load())
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
            g_driverVehicleEntryRequested.store(true);
            g_passengerVehicleEntryRequested.store(false);
            Log("PC input action: requested driver vehicle entry");
        }
    }

    if (action == static_cast<int>(PcInputAction::PASSENGER) && pressed && !wasPressed &&
        !g_nativeInterfaceActive.load())
    {
        CPlayerPed* playerPed = pGame != nullptr ? pGame->FindPlayerPed() : nullptr;
        if (playerPed != nullptr && !playerPed->IsInVehicle() && pNetGame != nullptr)
        {
            bNeedEnterVehicle = true;
            g_driverVehicleEntryRequested.store(false);
            g_passengerVehicleEntryRequested.store(true);
            Log("PC input action: requested passenger vehicle entry");
        }
    }

    if (action == static_cast<int>(PcInputAction::ENTER) && pressed && !wasPressed &&
        pUI != nullptr && pUI->dialog()->visible())
    {
        pUI->dialog()->confirm();
        Log("PC input action: confirmed native dialog");
    }

    if (pressed && !wasPressed && !g_nativeInterfaceActive.load())
    {
        if (action == static_cast<int>(PcInputAction::WEAPON_NEXT))
            g_weaponWheelSteps.store(std::clamp(g_weaponWheelSteps.load() + 1, -8, 8));
        else if (action == static_cast<int>(PcInputAction::WEAPON_PREVIOUS))
            g_weaponWheelSteps.store(std::clamp(g_weaponWheelSteps.load() - 1, -8, 8));
    }

    if (action == static_cast<int>(PcInputAction::TAB) && wasPressed != pressed && pUI)
    {
        if (pressed)
            pUI->playertablist()->show();
        else if (pUI->playertablist()->visible())
            pUI->playertablist()->hide();
        PcInputRefreshNativeInterfaceState();
    }
}

int PcInputPeekWeaponWheel()
{
    if (!g_inputEnabled.load() || g_nativeInterfaceActive.load())
        return 0;
    return g_weaponWheelSteps.load();
}

int PcInputConsumeWeaponWheel(int direction)
{
    if (!g_inputEnabled.load() || g_nativeInterfaceActive.load() || direction == 0)
        return 0;

    int cur = g_weaponWheelSteps.load();
    while ((direction > 0 && cur > 0) || (direction < 0 && cur < 0))
    {
        const int next = cur - (direction > 0 ? 1 : -1);
        if (g_weaponWheelSteps.compare_exchange_weak(cur, next))
            return direction;
    }
    return 0;
}

int PcInputConsumeWeaponSlot()
{
    return g_pendingWeaponSlot.exchange(-1, std::memory_order_acq_rel);
}

void PcInputSelectWeaponSlot(int slot)
{
    if (!g_inputEnabled.load() || g_nativeInterfaceActive.load())
        return;
    if (slot >= 0 && slot < 13)
        g_pendingWeaponSlot.store(slot, std::memory_order_release);
}

void PcInputAddMouseDelta(float dx, float dy)
{
    if (!std::isfinite(dx) || !std::isfinite(dy))
        return;
    if (!g_inputEnabled.load() || g_nativeInterfaceActive.load())
        return;

    NativeCameraMouseEnqueue(dx, dy);
}

void PcInputSetMouseOptions(float maxDelta, float smoothing)
{
    g_maxDelta.store(std::isfinite(maxDelta) ? std::max(0.0f, maxDelta) : 0.0f);
    g_smoothing.store(std::isfinite(smoothing) ? std::clamp(smoothing, 0.0f, 0.85f) : 0.0f);
    SyncCameraOptions();
}

void PcInputSetCombatOptions(float aimSensitivity, float strafeSpeed, bool pcSprint, bool fastWeaponSwitch, bool vehicleAutoCenter)
{
    g_aimSensitivity.store(std::isfinite(aimSensitivity) ? std::clamp(aimSensitivity, 0.15f, 3.0f) : 0.85f);
    g_strafeMultiplier.store(std::isfinite(strafeSpeed) ? std::clamp(strafeSpeed, 1.0f, 2.5f) : 1.45f);
    g_pcSprintEnabled.store(pcSprint);
    g_fastWeaponSwitch.store(fastWeaponSwitch);
    g_vehicleAutoCenter.store(vehicleAutoCenter);
    SyncCameraOptions();
}

float PcInputGetStrafeMultiplier()
{
    return g_strafeMultiplier.load();
}

bool PcInputIsPcSprintEnabled()
{
    return g_pcSprintEnabled.load();
}

bool PcInputIsFastWeaponSwitchEnabled()
{
    return g_fastWeaponSwitch.load();
}

void PcInputSetMouseButton(int button, int action, bool pressed)
{
    if (button < 1 || button >= kMaxMouseButtons)
        return;

    if (!g_inputEnabled.load() || g_nativeInterfaceActive.load())
        pressed = false;

    const int prevAction = g_mouseButtonAction[button].load();
    int resolvedAction = IsActionIndexValid(action) ? action : DefaultActionForMouseButton(button);
    if (!pressed && IsActionIndexValid(prevAction))
        resolvedAction = prevAction;

    const bool changed = g_mouseButtonPressed[button].exchange(pressed) != pressed;
    g_mouseButtonAction[button].store(pressed ? resolvedAction : -1);

    if (IsActionIndexValid(resolvedAction))
        PcInputSetAction(resolvedAction, pressed);

    uint8_t mask = 0;
    for (int b = 1; b < kMaxMouseButtons; ++b)
    {
        if (g_mouseButtonPressed[b].load(std::memory_order_acquire))
            mask |= static_cast<uint8_t>(1u << (b - 1));
    }
    NativeCameraMouseSetButtons(mask);
    if (changed)
        Log("[GAME-MOUSE] button=%d action=%d pressed=%d", button, resolvedAction, pressed);
}

void PcInputReleaseMouseButtons()
{
    for (int button = 1; button < kMaxMouseButtons; ++button)
    {
        const int action = g_mouseButtonAction[button].exchange(-1);
        const bool wasDown = g_mouseButtonPressed[button].exchange(false);
        if (wasDown && IsActionIndexValid(action))
            PcInputSetAction(action, false);
    }
    // Always ensure FIRE & AIM aren't stuck if no mapping record was present.
    g_pressed[static_cast<int>(PcInputAction::FIRE)].store(false, std::memory_order_release);
    g_pressed[static_cast<int>(PcInputAction::AIM)].store(false, std::memory_order_release);
    g_justPressed[static_cast<int>(PcInputAction::FIRE)].store(false, std::memory_order_release);
    g_justPressed[static_cast<int>(PcInputAction::AIM)].store(false, std::memory_order_release);
    g_latchedPress[static_cast<int>(PcInputAction::FIRE)].store(false, std::memory_order_release);
    g_latchedPress[static_cast<int>(PcInputAction::AIM)].store(false, std::memory_order_release);
    NativeCameraMouseSetButtons(0);
}

void PcInputMouseLeave()
{
    PcInputReleaseMouseButtons();
}

void PcInputOpenChat()
{
    if (!g_inputEnabled.load() || pUI == nullptr || pGame == nullptr)
        return;

    pUI->chat()->setVisible(true);
    pUI->keyboard()->show(pUI->chat());
    PcInputRefreshNativeInterfaceState();
}

void PcInputHandleTextKey(int key, int unicode)
{
    if (!g_textEntryActive.load() || pUI == nullptr || !pUI->keyboard()->visible())
        return;

    KeyboardInput* input = pUI->keyboard()->input();
    switch (key)
    {
        case 0: // Backspace
            input->popCharFromInput();
            return;
        case 1: // Enter / Send
            pUI->keyboard()->send();
            return;
        case 2: // Escape / Hide
            pUI->keyboard()->hide();
            return;
        case 3: // Up arrow -> older message in history
            if (pUI->keyboard()->history())
                input->setInputString(pUI->keyboard()->history()->next());
            return;
        case 4: // Down arrow -> newer message in history
            if (pUI->keyboard()->history())
                input->setInputString(pUI->keyboard()->history()->prev());
            return;
        case 5: // Tab -> quick command prefix
            if (input->inputString().empty())
                input->setInputString("/");
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
}

void PcInputHandleEscape()
{
    if (pUI == nullptr)
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
        PcInputRefreshNativeInterfaceState();
        return;
    }
    if (pUI->playertablist()->visible())
    {
        pUI->playertablist()->hide();
        PcInputRefreshNativeInterfaceState();
        return;
    }

    if (pNetGame != nullptr && pNetGame->GetTextDrawPool() != nullptr &&
        pNetGame->GetTextDrawPool()->GetState())
    {
        pNetGame->GetTextDrawPool()->SetSelectState(false, 0);
    }
    PcInputRefreshNativeInterfaceState();
}

bool PcInputIsTextEntryActive()
{
    return g_textEntryActive.load();
}

bool PcInputIsNativeInterfaceActive()
{
    return g_nativeInterfaceActive.load();
}

bool PcInputIsTabListOnly()
{
    return g_tabListOnly.load();
}

void PcInputRefreshNativeInterfaceState()
{
    const bool textEntry = pUI != nullptr && pUI->keyboard()->visible();
    const bool dialogOpen = pUI != nullptr && pUI->dialog()->visible();
    const bool tabOpen = pUI != nullptr && pUI->playertablist()->visible();

    const bool hardInterface = textEntry || dialogOpen;
    const bool interfaceActive = hardInterface || tabOpen;

    g_textEntryActive.store(textEntry);
    g_tabListOnly.store(!hardInterface && tabOpen);
    g_nativeInterfaceActive.store(interfaceActive);

    if (hardInterface)
    {
        PcInputReleaseMouseButtons();
        NativeCameraMouseReset();
    }
    else if (tabOpen)
    {
        // While peeking scoreboard with TAB, release mouse combat/camera without stopping running.
        PcInputReleaseMouseButtons();
        NativeCameraMouseReset();
    }
}

extern "C"
{
    JNIEXPORT void JNICALL
    Java_com_gta_game_input_KeyboardMouseBridge_nativeSetEnabled(
        JNIEnv*, jclass, jboolean enabled)
    {
        PcInputSetEnabled(enabled == JNI_TRUE);
    }

    JNIEXPORT void JNICALL
    Java_com_gta_game_input_KeyboardMouseBridge_nativeSetExternalInputPresent(
        JNIEnv*, jclass, jboolean present)
    {
        PcInputSetExternalInputPresent(present == JNI_TRUE);
    }

    JNIEXPORT void JNICALL
    Java_com_gta_game_input_KeyboardMouseBridge_nativeSetAction(
        JNIEnv*, jclass, jint action, jboolean pressed)
    {
        PcInputSetAction(action, pressed == JNI_TRUE);
    }

    JNIEXPORT void JNICALL
    Java_com_gta_game_input_KeyboardMouseBridge_nativeSetMouseOptions(
        JNIEnv*, jclass, jfloat maxDelta, jfloat smoothing)
    {
        PcInputSetMouseOptions(maxDelta, smoothing);
    }

    JNIEXPORT void JNICALL
    Java_com_gta_game_input_KeyboardMouseBridge_nativeSetCombatConfig(
        JNIEnv*, jclass, jfloat aimSensitivity, jfloat strafeSpeed,
        jboolean pcSprint, jboolean fastWeaponSwitch, jboolean vehicleAutoCenter)
    {
        PcInputSetCombatOptions(
            aimSensitivity,
            strafeSpeed,
            pcSprint == JNI_TRUE,
            fastWeaponSwitch == JNI_TRUE,
            vehicleAutoCenter == JNI_TRUE
        );
    }

    JNIEXPORT void JNICALL
    Java_com_gta_game_input_KeyboardMouseBridge_nativeSelectWeaponSlot(
        JNIEnv*, jclass, jint slot)
    {
        PcInputSelectWeaponSlot(slot);
    }

    JNIEXPORT void JNICALL
    Java_com_gta_game_input_KeyboardMouseBridge_nativeMouseMove(
        JNIEnv*, jclass, jfloat dx, jfloat dy)
    {
        if (!std::isfinite(dx) || !std::isfinite(dy) ||
            !g_inputEnabled.load() || g_nativeInterfaceActive.load())
            return;

        PcInputAddMouseDelta(dx, dy);
    }

    JNIEXPORT jint JNICALL
    Java_com_gta_game_input_KeyboardMouseBridge_nativeGetMouseFrameCount(
        JNIEnv*, jclass)
    {
        return static_cast<jint>(NativeCameraMouseGetAppliedCount());
    }

    JNIEXPORT void JNICALL
    Java_com_gta_game_input_KeyboardMouseBridge_nativeMouseButtonDown(
        JNIEnv*, jclass, jint button, jint action)
    {
        PcInputSetMouseButton(button, action, true);
    }

    JNIEXPORT void JNICALL
    Java_com_gta_game_input_KeyboardMouseBridge_nativeMouseButtonUp(
        JNIEnv*, jclass, jint button)
    {
        PcInputSetMouseButton(button, -1, false);
    }

    JNIEXPORT void JNICALL
    Java_com_gta_game_input_KeyboardMouseBridge_nativeSetCapture(
        JNIEnv*, jclass, jboolean captured)
    {
        const bool active = captured == JNI_TRUE;
        if (!active)
            PcInputReleaseMouseButtons();
        Log("[JNI-MOUSE] native capture=%d", active ? 1 : 0);
    }

    JNIEXPORT void JNICALL
    Java_com_gta_game_input_KeyboardMouseBridge_nativeMouseEnter(JNIEnv*, jclass)
    {
    }

    JNIEXPORT void JNICALL
    Java_com_gta_game_input_KeyboardMouseBridge_nativeMouseLeave(JNIEnv*, jclass)
    {
        PcInputMouseLeave();
    }

    JNIEXPORT void JNICALL
    Java_com_gta_game_input_KeyboardMouseBridge_nativeMouseWheel(
        JNIEnv*, jclass, jint direction)
    {
        if (!g_inputEnabled.load() || g_nativeInterfaceActive.load() || direction == 0)
            return;

        const int step = (direction > 0 ? 1 : -1);
        g_weaponWheelSteps.store(std::clamp(g_weaponWheelSteps.load() + step, -8, 8));
        NativeCameraMouseEnqueueWheel(step);
    }

    JNIEXPORT void JNICALL
    Java_com_gta_game_input_KeyboardMouseBridge_nativeOpenChat(JNIEnv*, jclass)
    {
        PcInputOpenChat();
    }

    JNIEXPORT void JNICALL
    Java_com_gta_game_input_KeyboardMouseBridge_nativeTextInputKey(
        JNIEnv*, jclass, jint key, jint unicode)
    {
        PcInputHandleTextKey(key, unicode);
    }

    JNIEXPORT void JNICALL
    Java_com_gta_game_input_KeyboardMouseBridge_nativeHandleEscape(JNIEnv*, jclass)
    {
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

    JNIEXPORT void JNICALL
    Java_com_gta_game_input_KeyboardMouseBridge_nativeSetAspectRatio(
        JNIEnv*, jclass, jfloat aspectRatio)
    {
        SetNexionAspectRatio(aspectRatio);
    }

    JNIEXPORT jboolean JNICALL
    Java_com_gta_game_input_KeyboardMouseBridge_nativeIsTabListOnly(JNIEnv*, jclass)
    {
        return PcInputIsTabListOnly() ? JNI_TRUE : JNI_FALSE;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_rockstargames_oswrapper_GameNative_implOnNativeMouseMove(
    JNIEnv* env, jclass cls, jfloat dx, jfloat dy)
{
    Java_com_gta_game_input_KeyboardMouseBridge_nativeMouseMove(env, cls, dx, dy);
}

extern "C" JNIEXPORT void JNICALL
Java_com_rockstargames_oswrapper_GameNative_implOnNativeMouseButton(
    JNIEnv* env, jclass cls, jint button, jboolean pressed)
{
    const jint mask = (button >= 0 && button <= 4) ? (button + 1) : 1;
    if (pressed == JNI_TRUE)
        Java_com_gta_game_input_KeyboardMouseBridge_nativeMouseButtonDown(env, cls, mask, -1);
    else
        Java_com_gta_game_input_KeyboardMouseBridge_nativeMouseButtonUp(env, cls, mask);
}

extern "C" JNIEXPORT void JNICALL
Java_com_rockstargames_oswrapper_GameNative_implOnCaptureStatusChanged(
    JNIEnv* env, jclass cls, jboolean captured)
{
    Java_com_gta_game_input_KeyboardMouseBridge_nativeSetCapture(env, cls, captured);
}
