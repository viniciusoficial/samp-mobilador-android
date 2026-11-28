// Porte do sistema de teclado/mouse PC (mobilador -> SA-MP Mobile 2.10)
// Fonte: samp/game/pcinput.cpp do mobilador, adaptado as APIs do xiaoniao.
#include "PcInput.h"

#include "Pad.h"
#include "PadInternal.h"
#include "NativeCameraMouse.h"
#include "PlayerPed.h"
#include "Game.h"
#include "TouchCombat.h"

#include "../Client.h"
#include "../InputArbiter.h"
#include "Util.h"
#include "../UI/UI.h"
#include "../UI/SAMPWidgets/Chat.h"
#include "../UI/SAMPWidgets/Keyboard.h"
#include "../UI/SAMPWidgets/Scoreboard.h"
#include "../Net/NetGame.h"
#include "../Net/PlayerPool.h"
#include "../Net/LocalPlayer.h"

#include <jni.h>
#include <encoding.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <deque>
#include <mutex>
#include <tuple>
#include <utility>
#include <cmath>
#include <cstdint>
#include <string>

namespace {

constexpr int kActionCount = static_cast<int>(PcInputAction::COUNT);
constexpr int kMaxMouseButtons = 6;

std::atomic<bool> g_inputEnabled{true};
std::atomic<bool> g_externalInputPresent{false};
std::atomic<bool> g_nativeInterfaceActive{false};
std::atomic<bool> g_textEntryActive{false};
std::atomic<bool> g_tabListOnly{false};

// Pedidos de interface vindos da thread Java/JNI: aplicados so na thread do jogo
// (PcInputBeginFrame), igual ao QueueEvent do mobilador.
std::mutex g_pendingMutex;
std::deque<std::pair<int, int>> g_pendingTextKeys;
std::deque<std::tuple<int, bool, bool>> g_pendingActions;
std::atomic<bool> g_escapeRequested{false};
std::atomic<bool> g_chatRequested{false};
std::atomic<int> g_tabRequested{-1};

std::atomic<bool> g_pressed[kActionCount];
std::atomic<bool> g_justPressed[kActionCount];
std::atomic<bool> g_latchedPress[kActionCount];
bool g_frameLatchedSnapshot[kActionCount] = {};

std::atomic<int> g_mouseButtonAction[kMaxMouseButtons];
std::atomic<bool> g_mouseButtonPressed[kMaxMouseButtons];

std::atomic<int> g_weaponWheelSteps{0};
std::atomic<int> g_pendingWeaponSlot{-1};

std::atomic<float> g_maxDelta{0.0f};
std::atomic<float> g_smoothing{0.0f};
std::atomic<float> g_aimSensitivity{0.85f};
std::atomic<float> g_strafeMultiplier{1.45f};
std::atomic<bool> g_pcSprintEnabled{true};
std::atomic<bool> g_fastWeaponSwitch{true};
std::atomic<bool> g_vehicleAutoCenter{false};

inline bool IsActionIndexValid(int index)
{
	return index >= 0 && index < kActionCount;
}

std::string CodePointToUtf8(int codePoint)
{
	std::string utf8;
	if (codePoint <= 0x7F) {
		utf8.push_back(static_cast<char>(codePoint));
	}
	else if (codePoint <= 0x7FF) {
		utf8.push_back(static_cast<char>(0xC0 | ((codePoint >> 6) & 0x1F)));
		utf8.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
	}
	else if (codePoint <= 0xFFFF) {
		utf8.push_back(static_cast<char>(0xE0 | ((codePoint >> 12) & 0x0F)));
		utf8.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
		utf8.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
	}
	else {
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

static void ApplyTextKey(int key, int unicode);
static void ApplyChatOpen();
static void ApplyEscape();
static void ApplyTabList(bool visible);
static void ApplyAction(int action, bool pressed, bool wasPressed);
static void ApplyWeaponSlotRequest();

void PcInputBeginFrame()
{
	// 1) Aplica os pedidos de interface enfileirados pela thread Java.
	if (g_chatRequested.exchange(false, std::memory_order_acq_rel))
		ApplyChatOpen();

	const int tab = g_tabRequested.exchange(-1, std::memory_order_acq_rel);
	if (tab >= 0)
		ApplyTabList(tab != 0);

	if (g_escapeRequested.exchange(false, std::memory_order_acq_rel))
		ApplyEscape();

	{
		std::deque<std::tuple<int, bool, bool>> actions;
		{
			std::lock_guard<std::mutex> lock(g_pendingMutex);
			actions.swap(g_pendingActions);
		}
		for (const auto& a : actions)
			ApplyAction(std::get<0>(a), std::get<1>(a), std::get<2>(a));

		// YES/NO sao teclas de pulso no GTA: nao podem ficar presas entre frames.
		LocalPlayerKeys.bKeys[ePadKeys::KEY_YES] = false;
		LocalPlayerKeys.bKeys[ePadKeys::KEY_NO] = false;
	}

	{
		std::deque<std::pair<int, int>> keys;
		{
			std::lock_guard<std::mutex> lock(g_pendingMutex);
			keys.swap(g_pendingTextKeys);
		}
		for (const auto& k : keys)
			ApplyTextKey(k.first, k.second);
	}

	// 3) Teclas 1-9: selecao direta de slot de arma (igual ao CPed::SetCurrentWeapon).
	ApplyWeaponSlotRequest();

	// Estado de chat/placar precisa refletir o que os widgets fizeram no frame anterior.
	PcInputRefreshNativeInterfaceState();

	// 2) Fecha o latch deste frame.
	for (int i = 0; i < kActionCount; ++i) {
		g_frameLatchedSnapshot[i] = g_latchedPress[i].exchange(false, std::memory_order_acq_rel);
	}
}

void PcInputEndFrame()
{
	for (int i = 0; i < kActionCount; ++i) {
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

void PcInputSetEnabled(bool enabled)
{
	g_inputEnabled.store(enabled);
	g_weaponWheelSteps.store(0);
	g_pendingWeaponSlot.store(-1);
	NativeCameraMouseReset();
	PcInputReleaseMouseButtons();

	if (!enabled) {
		for (int i = 0; i < kActionCount; ++i) {
			g_pressed[i].store(false, std::memory_order_release);
			g_justPressed[i].store(false, std::memory_order_release);
			g_latchedPress[i].store(false, std::memory_order_release);
			g_frameLatchedSnapshot[i] = false;
		}
		LocalPlayerKeys.bKeys[ePadKeys::KEY_SECONDARY_ATTACK] = false;
	}
	spdlog::info("PC input C++: enabled={}", enabled);
}

bool PcInputIsEnabled()
{
	return g_inputEnabled.load();
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
	if (wasPresent && !present) {
		for (int i = 0; i < kActionCount; ++i) {
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
	spdlog::info("PC input C++: externalInputPresent={}", present);
}

static void ApplyAction(int action, bool pressed, bool wasPressed)
{
	if (action == static_cast<int>(PcInputAction::VEHICLE) && pressed && !wasPressed &&
		!g_nativeInterfaceActive.load()) {
		CPlayerPed* playerPed = pGame ? pGame->FindPlayerPed() : nullptr;
		if (playerPed && playerPed->IsInVehicle()) {
			LocalPlayerKeys.bKeys[ePadKeys::KEY_SECONDARY_ATTACK] = false;
			playerPed->ExitCurrentVehicle();
			spdlog::info("PC input action: requested vehicle exit");
		}
		else {
			LocalPlayerKeys.bKeys[ePadKeys::KEY_SECONDARY_ATTACK] = true;
		}
	}
	if (action == static_cast<int>(PcInputAction::VEHICLE) && !pressed) {
		LocalPlayerKeys.bKeys[ePadKeys::KEY_SECONDARY_ATTACK] = false;
	}

	if (action == static_cast<int>(PcInputAction::PASSENGER) && pressed && !wasPressed &&
		!g_nativeInterfaceActive.load()) {
		CPlayerPed* playerPed = pGame ? pGame->FindPlayerPed() : nullptr;
		if (playerPed && !playerPed->IsInVehicle() && pNetGame && pNetGame->GetPlayerPool()) {
			if (CLocalPlayer* pLocal = pNetGame->GetPlayerPool()->GetLocalPlayer()) {
				pLocal->EnterVehicleAsPassenger();
			}
		}
	}

	if (action == static_cast<int>(PcInputAction::CONTROL_BACK) && pressed != wasPressed) {
		CPlayerPed* playerPed = pGame ? pGame->FindPlayerPed() : nullptr;
		if (playerPed && playerPed->IsInVehicle()) {
			LocalPlayerKeys.bKeys[ePadKeys::KEY_CROUCH] = pressed;
		}
		else {
			LocalPlayerKeys.bKeys[ePadKeys::KEY_CTRL_BACK] = pressed;
		}
	}

	if (action == static_cast<int>(PcInputAction::CROUCH) && pressed != wasPressed) {
		CPlayerPed* playerPed = pGame ? pGame->FindPlayerPed() : nullptr;
		if (playerPed && playerPed->IsInVehicle()) {
			LocalPlayerKeys.bKeys[ePadKeys::KEY_CROUCH] = pressed;
		}
		else {
			NotifyLocalHardwareCrouchKey(pressed);
		}
	}

	if (action == static_cast<int>(PcInputAction::AIM) && pressed != wasPressed &&
		!g_nativeInterfaceActive.load()) {
		TouchCombatSetExternalAim(pressed);
		LocalPlayerKeys.bKeys[ePadKeys::KEY_HANDBRAKE] = pressed;
	}

	if (action == static_cast<int>(PcInputAction::FIRE) && !g_nativeInterfaceActive.load()) {
		TouchCombatSetExternalFire(pressed, pressed && !wasPressed);
	}

	if (action == static_cast<int>(PcInputAction::LOOK_BEHIND) && pressed != wasPressed) {
		LocalPlayerKeys.bKeys[ePadKeys::KEY_LOOK_LEFT] = pressed;
		LocalPlayerKeys.bKeys[ePadKeys::KEY_LOOK_RIGHT] = pressed;
	}

	if (action == static_cast<int>(PcInputAction::YES) && pressed && !wasPressed) {
		LocalPlayerKeys.bKeys[ePadKeys::KEY_YES] = true;
	}
	if (action == static_cast<int>(PcInputAction::NO) && pressed && !wasPressed) {
		LocalPlayerKeys.bKeys[ePadKeys::KEY_NO] = true;
	}

	if (pressed && !wasPressed && !g_nativeInterfaceActive.load()) {
		if (action == static_cast<int>(PcInputAction::WEAPON_NEXT))
			g_weaponWheelSteps.store(std::clamp(g_weaponWheelSteps.load() + 1, -1, 1));
		else if (action == static_cast<int>(PcInputAction::WEAPON_PREVIOUS))
			g_weaponWheelSteps.store(std::clamp(g_weaponWheelSteps.load() - 1, -1, 1));
	}

	if (action == static_cast<int>(PcInputAction::TAB) && pressed != wasPressed) {
		g_tabRequested.store(pressed ? 1 : 0, std::memory_order_release);
	}

	if (action == static_cast<int>(PcInputAction::CHAT) && pressed && !wasPressed) {
		g_chatRequested.store(true, std::memory_order_release);
	}
}

void PcInputSetAction(int action, bool pressed)
{
	if (!IsActionIndexValid(action)) {
		spdlog::warn("PC input C++: rejected invalid action={}", action);
		return;
	}

	if (!g_inputEnabled.load())
		pressed = false;

	const bool wasPressed = g_pressed[action].load(std::memory_order_acquire);
	g_pressed[action].store(pressed, std::memory_order_release);
	if (pressed && !wasPressed) {
		g_justPressed[action].store(true, std::memory_order_release);
		g_latchedPress[action].store(true, std::memory_order_release);
	}

	// A thread Java so registra o evento; o efeito no jogo (pUI, player, teclas
	// do pad) roda na thread do jogo dentro de PcInputBeginFrame.
	std::lock_guard<std::mutex> lock(g_pendingMutex);
	if (g_pendingActions.size() < 256)
		g_pendingActions.emplace_back(action, pressed, wasPressed);
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
	while ((direction > 0 && cur > 0) || (direction < 0 && cur < 0)) {
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

void PcInputDropWeaponWheel()
{
	g_weaponWheelSteps.store(0, std::memory_order_release);
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

void PcInputSetCombatOptions(float aimSensitivity, float strafeSpeed, bool pcSprint,
	bool fastWeaponSwitch, bool vehicleAutoCenter)
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

	g_mouseButtonPressed[button].exchange(pressed);
	g_mouseButtonAction[button].store(pressed ? resolvedAction : -1);

	if (IsActionIndexValid(resolvedAction))
		PcInputSetAction(resolvedAction, pressed);

	uint8_t mask = 0;
	for (int b = 1; b < kMaxMouseButtons; ++b) {
		if (g_mouseButtonPressed[b].load(std::memory_order_acquire))
			mask |= static_cast<uint8_t>(1u << (b - 1));
	}
	NativeCameraMouseSetButtons(mask);
}

void PcInputReleaseMouseButtons()
{
	for (int button = 1; button < kMaxMouseButtons; ++button) {
		const int action = g_mouseButtonAction[button].exchange(-1);
		const bool wasDown = g_mouseButtonPressed[button].exchange(false);
		if (wasDown && IsActionIndexValid(action))
			PcInputSetAction(action, false);
	}
	// Garante que FIRE/AIM nunca ficam presos sem registro de mapeamento.
	g_pressed[static_cast<int>(PcInputAction::FIRE)].store(false, std::memory_order_release);
	g_pressed[static_cast<int>(PcInputAction::AIM)].store(false, std::memory_order_release);
	g_justPressed[static_cast<int>(PcInputAction::FIRE)].store(false, std::memory_order_release);
	g_justPressed[static_cast<int>(PcInputAction::AIM)].store(false, std::memory_order_release);
	g_latchedPress[static_cast<int>(PcInputAction::FIRE)].store(false, std::memory_order_release);
	g_latchedPress[static_cast<int>(PcInputAction::AIM)].store(false, std::memory_order_release);
	TouchCombatSetExternalAim(false);
	TouchCombatSetExternalFire(false, false);
	NativeCameraMouseSetButtons(0);
}

void PcInputMouseLeave()
{
	PcInputReleaseMouseButtons();
}

void PcInputHandleWheel(int direction)
{
	if (!g_inputEnabled.load() || g_nativeInterfaceActive.load() || direction == 0)
		return;

	const int step = (direction > 0 ? 1 : -1);
	g_weaponWheelSteps.store(std::clamp(g_weaponWheelSteps.load() + step, -2, 2));
	NativeCameraMouseEnqueueWheel(step);
}

void PcInputOpenChat()
{
	if (!g_inputEnabled.load())
		return;
	g_chatRequested.store(true, std::memory_order_release);
}

static void ApplyChatOpen()
{
	if (!g_inputEnabled.load() || pUI == nullptr || pGame == nullptr)
		return;

	if (pUI->chat()) {
		pUI->chat()->setVisible(true);
	}
	if (pUI->keyboard() && pUI->chat()) {
		pUI->keyboard()->show(pUI->chat());
	}
	PcInputRefreshNativeInterfaceState();
}

void PcInputHandleTextKey(int key, int unicode)
{
	std::lock_guard<std::mutex> lock(g_pendingMutex);
	if (g_pendingTextKeys.size() < 128)
		g_pendingTextKeys.emplace_back(key, unicode);
}

static void ApplyTextKey(int key, int unicode)
{
	if (!g_textEntryActive.load() || pUI == nullptr || !pUI->keyboard() || !pUI->keyboard()->visible())
		return;

	KeyboardInput* input = pUI->keyboard()->input();
	if (input == nullptr)
		return;

	switch (key) {
		case 0: // Backspace
			input->popCharFromInput();
			return;
		case 1: // Enter / Send
			pUI->keyboard()->send();
			return;
		case 2: // Escape / Hide
			pUI->keyboard()->hide();
			PcInputRefreshNativeInterfaceState();
			return;
		case 3: // Seta para cima -> mensagem mais antiga
			if (pUI->keyboard()->history())
				input->setInputString(pUI->keyboard()->history()->next());
			return;
		case 4: // Seta para baixo -> mensagem mais nova
			if (pUI->keyboard()->history())
				input->setInputString(pUI->keyboard()->history()->prev());
			return;
		case 5: // Tab -> prefixo de comando
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
	g_escapeRequested.store(true, std::memory_order_release);
}

static void ApplyEscape()
{
	if (pUI == nullptr)
		return;

	if (pUI->keyboard() && pUI->keyboard()->visible()) {
		pUI->keyboard()->hide();
		PcInputRefreshNativeInterfaceState();
		return;
	}
	if (Scoreboard* pScoreboard = pUI->scoreboard()) {
		if (pScoreboard->visible()) {
			pScoreboard->setVisible(false);
			PcInputRefreshNativeInterfaceState();
			return;
		}
	}
	InputArbiter::dismissTopNativeModal();
}

static void ApplyWeaponSlotRequest()
{
	const int slot = PcInputConsumeWeaponSlot();
	if (slot < 0 || !PcInputIsEnabledForTouch())
		return;


	CPlayerPed* pLocal = pGame ? pGame->FindPlayerPed() : nullptr;
	sa::CPed* ped = GamePool_FindPlayerPed();
	if (pLocal == nullptr || ped == nullptr || pLocal->IsDead() || ped->IsInVehicle())
		return;

	if (slot == 0) {
		pLocal->SetArmedWeapon(0);
		return;
	}

	const sa::CWeapon& weapon = ped->m_aWeapons[slot];
	if (weapon.dwType == 0 || (slot > 1 && weapon.dwAmmo == 0))
		return;

	pLocal->SetArmedWeapon(static_cast<uint8_t>(weapon.dwType));
}

static void ApplyTabList(bool visible)
{
	if (pUI == nullptr)
		return;
	if (Scoreboard* pScoreboard = pUI->scoreboard()) {
		pScoreboard->setVisible(visible);
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
	const bool textEntry = pUI != nullptr && pUI->keyboard() != nullptr && pUI->keyboard()->visible();
	const bool tabOpen = pUI != nullptr && pUI->scoreboard() != nullptr && pUI->scoreboard()->visible();

	const bool hardInterface = textEntry;
	const bool interfaceActive = hardInterface || tabOpen;

	const bool wasActive = g_nativeInterfaceActive.exchange(interfaceActive);

	g_textEntryActive.store(textEntry);
	g_tabListOnly.store(!hardInterface && tabOpen);

	if (interfaceActive && !wasActive) {
		PcInputReleaseMouseButtons();
		NativeCameraMouseReset();
	}
}

/* ---------------- JNI (com.sampmobile.game.input.KeyboardMouseBridge) ---------------- */

extern "C" {

JNIEXPORT void JNICALL
Java_com_sampmobile_game_input_KeyboardMouseBridge_nativeSetEnabled(JNIEnv*, jclass, jboolean enabled)
{
	PcInputSetEnabled(enabled == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_sampmobile_game_input_KeyboardMouseBridge_nativeSetExternalInputPresent(JNIEnv*, jclass, jboolean present)
{
	PcInputSetExternalInputPresent(present == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_sampmobile_game_input_KeyboardMouseBridge_nativeSetAction(JNIEnv*, jclass, jint action, jboolean pressed)
{
	PcInputSetAction(action, pressed == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_sampmobile_game_input_KeyboardMouseBridge_nativeSetMouseOptions(JNIEnv*, jclass, jfloat maxDelta, jfloat smoothing)
{
	PcInputSetMouseOptions(maxDelta, smoothing);
}

JNIEXPORT void JNICALL
Java_com_sampmobile_game_input_KeyboardMouseBridge_nativeSetCombatConfig(JNIEnv*, jclass,
	jfloat aimSensitivity, jfloat strafeSpeed, jboolean pcSprint,
	jboolean fastWeaponSwitch, jboolean vehicleAutoCenter)
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
Java_com_sampmobile_game_input_KeyboardMouseBridge_nativeSelectWeaponSlot(JNIEnv*, jclass, jint slot)
{
	PcInputSelectWeaponSlot(slot);
}

JNIEXPORT void JNICALL
Java_com_sampmobile_game_input_KeyboardMouseBridge_nativeMouseMove(JNIEnv*, jclass, jfloat dx, jfloat dy)
{
	PcInputAddMouseDelta(dx, dy);
}

JNIEXPORT jint JNICALL
Java_com_sampmobile_game_input_KeyboardMouseBridge_nativeGetMouseFrameCount(JNIEnv*, jclass)
{
	return static_cast<jint>(NativeCameraMouseGetAppliedCount());
}

JNIEXPORT void JNICALL
Java_com_sampmobile_game_input_KeyboardMouseBridge_nativeMouseButtonDown(JNIEnv*, jclass, jint button, jint action)
{
	PcInputSetMouseButton(button, action, true);
}

JNIEXPORT void JNICALL
Java_com_sampmobile_game_input_KeyboardMouseBridge_nativeMouseButtonUp(JNIEnv*, jclass, jint button)
{
	PcInputSetMouseButton(button, -1, false);
}

JNIEXPORT void JNICALL
Java_com_sampmobile_game_input_KeyboardMouseBridge_nativeSetCapture(JNIEnv*, jclass, jboolean captured)
{
	if (captured != JNI_TRUE)
		PcInputReleaseMouseButtons();
}

JNIEXPORT void JNICALL
Java_com_sampmobile_game_input_KeyboardMouseBridge_nativeMouseEnter(JNIEnv*, jclass)
{
}

JNIEXPORT void JNICALL
Java_com_sampmobile_game_input_KeyboardMouseBridge_nativeMouseLeave(JNIEnv*, jclass)
{
	PcInputMouseLeave();
}

JNIEXPORT void JNICALL
Java_com_sampmobile_game_input_KeyboardMouseBridge_nativeMouseWheel(JNIEnv*, jclass, jint direction)
{
	PcInputHandleWheel(direction);
}

JNIEXPORT void JNICALL
Java_com_sampmobile_game_input_KeyboardMouseBridge_nativeOpenChat(JNIEnv*, jclass)
{
	PcInputOpenChat();
}

JNIEXPORT void JNICALL
Java_com_sampmobile_game_input_KeyboardMouseBridge_nativeTextInputKey(JNIEnv*, jclass, jint key, jint unicode)
{
	PcInputHandleTextKey(key, unicode);
}

JNIEXPORT void JNICALL
Java_com_sampmobile_game_input_KeyboardMouseBridge_nativeHandleEscape(JNIEnv*, jclass)
{
	PcInputHandleEscape();
}

JNIEXPORT jboolean JNICALL
Java_com_sampmobile_game_input_KeyboardMouseBridge_nativeIsTextEntryActive(JNIEnv*, jclass)
{
	return PcInputIsTextEntryActive() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_sampmobile_game_input_KeyboardMouseBridge_nativeIsInterfaceActive(JNIEnv*, jclass)
{
	return PcInputIsNativeInterfaceActive() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_sampmobile_game_input_KeyboardMouseBridge_nativeSetAspectRatio(JNIEnv*, jclass, jfloat aspectRatio)
{
	// A 2.10 usa a proporcao real da tela; o hook de aspect ratio do mobilador
	// nao existe aqui. Mantido como no-op para a configuracao continuar valida.
	(void) aspectRatio;
}

JNIEXPORT jboolean JNICALL
Java_com_sampmobile_game_input_KeyboardMouseBridge_nativeIsTabListOnly(JNIEnv*, jclass)
{
	return PcInputIsTabListOnly() ? JNI_TRUE : JNI_FALSE;
}

} // extern "C"
