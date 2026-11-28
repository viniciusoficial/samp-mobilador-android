#include "ButtonPanel.h"
#include "Chat.h"
#include "Keyboard.h"
#include "NetStats.h"
#include "Scoreboard.h"
#include "VoiceButton.h"
#include "../UI.h"
#include "../UISettings.h"
#include "../Widgets/Button.h"
#include "../Widgets/Layout.h"
#include "../../Client.h"
#include "../../InputArbiter.h"
#include "../../Game/Game.h"
#include "../../Game/Hooks.h"
#include "../../Game/Pad.h"
#include "../../Game/NativeCameraMouse.h"
#include "../../Game/PlayerPed.h"
#include "../../Game/Util.h"
#include "../../Game/Common.h"
#include "../../Net/NetGame.h"
#include "../../Net/LocalPlayer.h"
#include "../../Net/MenuPool.h"
#include "../../Net/VehiclePool.h"
#include "../../Game/Vehicle.h"
#include "../../Settings.h"
#include "../../Java/Java.h"

#include <cstring>

/* ButtonPanel */


/* layout metrics in 640x480 design space, scaled to the display in
 * performLayout(). The panel anchors itself to the bottom edge center:
 * collapsed it is a single handle button, expanded it grows upward into
 * three rows of keys above the handle. */
static constexpr float kPanelWidth   = 330.0f;
static constexpr float kRowHeight    = 36.0f;
static constexpr float kHandleWidth  = 34.0f;
static constexpr float kHandleHeight = 22.0f;
static constexpr float kGap          = 4.0f;
static constexpr float kBottomMargin = 47.0f;

/* UTF-8 escapes so captions survive any source-encoding mixups */
static const char* kCaptionExpand   = "\xE2\x96\xB2"; // BLACK UP-POINTING TRIANGLE
static const char* kCaptionCollapse = "\xE2\x96\xBC"; // BLACK DOWN-POINTING TRIANGLE
static const char* kCaptionLeft     = "\xE2\x86\x90"; // LEFT ARROW
static const char* kCaptionUp       = "\xE2\x86\x91"; // UP ARROW
static const char* kCaptionRight    = "\xE2\x86\x92"; // RIGHT ARROW
static const char* kCaptionDown     = "\xE2\x86\x93"; // DOWN ARROW

ButtonPanel::ButtonPanel()
{
	m_extended = false;
	m_heldLR = 0;
	m_heldUD = 0;
	memset(m_heldBKeys, 0, sizeof(m_heldBKeys));

	const float fontSize = UISettings::fontSize() / 2.2f;

	m_extend = new Button(kCaptionExpand, fontSize);

	m_esc = new Button("ESC", fontSize);
	m_tab = new Button("TAB", fontSize);
	m_alt = new Button("ALT", fontSize);
	m_spc = new Button("SPC", fontSize);
	m_f = new Button("F", fontSize);
	m_g = new Button("G", fontSize);
	m_h = new Button("H", fontSize);

	m_ctrl = new Button("CTRL", fontSize);
	m_c = new Button("C", fontSize);
	m_2 = new Button("2", fontSize);
	m_y = new Button("Y", fontSize);
	m_n = new Button("N", fontSize);
	m_q = new Button("Q", fontSize);
	m_e = new Button("E", fontSize);

	m_left = new Button(kCaptionLeft, fontSize);
	m_up = new Button(kCaptionUp, fontSize);
	m_right = new Button(kCaptionRight, fontSize);
	m_down = new Button(kCaptionDown, fontSize);

	m_num8 = new Button("N8", fontSize);
	m_num4 = new Button("N4", fontSize);
	m_num6 = new Button("N6", fontSize);
	m_num2 = new Button("N2", fontSize);
	m_f8 = new Button("F8", fontSize);
	m_hud = new Button("HUD", fontSize);

	m_f8->setCallback([]() {
		Hooks::requestScreenshot(-1);
	});

	m_hud->setCallback([this]() {
		minimize();
		Hooks::openAdjustableHud();
	});

	m_extend->setCallback([this]() {
		if (extended()) {
			minimize();
		}
		else {
			maximize();
		}
	});

	m_esc->setCallback([]() {
		InputArbiter::dismissTopNativeModal();
	});

	m_tab->setCallback([]() {
		if (pUI && !pUI->keyboard()->visible()) {
			pUI->scoreboard()->setVisible(!pUI->scoreboard()->visible());
		}

		if (pGame) {
			if (CPlayerPed* pLocal = pGame->FindPlayerPed(); pLocal && !pLocal->IsInVehicle()) {
				LocalPlayerKeys.bKeys[ePadKeys::KEY_ACTION] = true;
			}
		}
	});

	m_alt->setCallback([]() {
		CPlayerPed* pLocal = pGame ? pGame->FindPlayerPed() : nullptr;
		if (pLocal && pLocal->IsInVehicle()) {
			LocalPlayerKeys.bKeys[ePadKeys::KEY_FIRE] = true;
		}
		else {
			LocalPlayerKeys.bKeys[ePadKeys::KEY_WALK] = true;
		}
	});

	m_spc->setCallback([]() {
		CPlayerPed* pLocal = pGame ? pGame->FindPlayerPed() : nullptr;
		if (pLocal && pLocal->IsInVehicle()) {
			LocalPlayerKeys.bKeys[ePadKeys::KEY_HANDBRAKE] = true;
		}
		else {
			LocalPlayerKeys.bKeys[ePadKeys::KEY_SPRINT] = true;
		}
	});

	m_f->setCallback([]() {
		LocalPlayerKeys.bKeys[ePadKeys::KEY_SECONDARY_ATTACK] = true;
	});

	m_g->setCallback([]() {
		if (pNetGame && pNetGame->GetPlayerPool() && pNetGame->GetPlayerPool()->GetLocalPlayer()) {
			pNetGame->GetPlayerPool()->GetLocalPlayer()->EnterVehicleAsPassenger();
		}
	});

	m_h->setCallback([]() {
		CPlayerPed* pLocal = pGame ? pGame->FindPlayerPed() : nullptr;
		if (pLocal && pLocal->IsInVehicle() && !pLocal->IsAPassenger()) {
			LocalPlayerKeys.bKeys[ePadKeys::KEY_CROUCH] = true;
		}
		else {
			LocalPlayerKeys.bKeys[ePadKeys::KEY_CTRL_BACK] = true;
		}
	});

	/* LCTRL/LMB on foot - held while pressed; also the "select" key in
	 * spectate-driven class selection (server reads KEY_FIRE in
	 * OnPlayerKeyStateChange) */
	m_ctrl->setHoldCallback(
			[this]() { m_heldBKeys[ePadKeys::KEY_FIRE] = true; },
			[this]() {
				m_heldBKeys[ePadKeys::KEY_FIRE] = false;
				LocalPlayerKeys.bKeys[ePadKeys::KEY_FIRE] = false;
			});
	m_ctrl->setCallback([]() {
		if (pNetGame) {
			pNetGame->SendSpawn();
		}
	});

	m_c->setCallback([]() {
		LocalPlayerKeys.bKeys[ePadKeys::KEY_CROUCH] = true;
	});

	m_2->setCallback([]() {
		LocalPlayerKeys.bKeys[ePadKeys::KEY_SUBMISSION] = true;
		if (pGame) {
			CPlayerPed* pPed = pGame->FindPlayerPed();
			if (pPed && pPed->IsInVehicle() && !pPed->IsAPassenger()) {
				sa::CVehicle* pVeh = pPed->GetGtaVehicle();
				if (pVeh && GetVehicleSubtype(pVeh) == VEHICLE_SUBTYPE_PLANE) {
					RequestLocalPlaneGearToggle();
				}
			}
		}
	});

	m_y->setCallback([]() {
		LocalPlayerKeys.bKeys[ePadKeys::KEY_YES] = true;
	});

	m_n->setCallback([]() {
		LocalPlayerKeys.bKeys[ePadKeys::KEY_NO] = true;
	});

	m_q->setCallback([]() {
		LocalPlayerKeys.bKeys[ePadKeys::KEY_LOOK_LEFT] = true;
	});

	m_e->setCallback([]() {
		LocalPlayerKeys.bKeys[ePadKeys::KEY_LOOK_RIGHT] = true;
	});

	/* direction keys: held lrAnalog/udAnalog (KEY_LEFT/UP=0xFF80,
	 * KEY_RIGHT/DOWN=0x0080) - servers running spectate-driven class
	 * selection read these via OnPlayerKeyStateChange. SendPrev/NextClass
	 * stays on tap for servers using the standard << Spawn >> flow. */
	m_left->setHoldCallback(
			[this]() {
				m_uiLeft = true;
				recomputeHeldAxes();
			},
			[this]() {
				m_uiLeft = false;
				recomputeHeldAxes();
			});
	m_left->setCallback([]() {
		if (pNetGame) {
			pNetGame->SendPrevClass();
		}
	});

	m_up->setHoldCallback(
			[this]() {
				m_uiUp = true;
				recomputeHeldAxes();
			},
			[this]() {
				m_uiUp = false;
				recomputeHeldAxes();
			});

	m_right->setHoldCallback(
			[this]() {
				m_uiRight = true;
				recomputeHeldAxes();
			},
			[this]() {
				m_uiRight = false;
				recomputeHeldAxes();
			});
	m_right->setCallback([]() {
		if (pNetGame) {
			pNetGame->SendNextClass();
		}
	});

	m_down->setHoldCallback(
			[this]() {
				m_uiDown = true;
				recomputeHeldAxes();
			},
			[this]() {
				m_uiDown = false;
				recomputeHeldAxes();
			});

	/* numpad 8/4/6/2 - KEY_ANALOG_* bits, also held semantics */
	m_num8->setHoldCallback(
			[this]() { m_heldBKeys[ePadKeys::KEY_ANALOG_UP] = true; },
			[this]() {
				m_heldBKeys[ePadKeys::KEY_ANALOG_UP] = false;
				LocalPlayerKeys.bKeys[ePadKeys::KEY_ANALOG_UP] = false;
			});

	m_num4->setHoldCallback(
			[this]() { m_heldBKeys[ePadKeys::KEY_ANALOG_LEFT] = true; },
			[this]() {
				m_heldBKeys[ePadKeys::KEY_ANALOG_LEFT] = false;
				LocalPlayerKeys.bKeys[ePadKeys::KEY_ANALOG_LEFT] = false;
			});

	m_num6->setHoldCallback(
			[this]() { m_heldBKeys[ePadKeys::KEY_ANALOG_RIGHT] = true; },
			[this]() {
				m_heldBKeys[ePadKeys::KEY_ANALOG_RIGHT] = false;
				LocalPlayerKeys.bKeys[ePadKeys::KEY_ANALOG_RIGHT] = false;
			});

	m_num2->setHoldCallback(
			[this]() { m_heldBKeys[ePadKeys::KEY_ANALOG_DOWN] = true; },
			[this]() {
				m_heldBKeys[ePadKeys::KEY_ANALOG_DOWN] = false;
				LocalPlayerKeys.bKeys[ePadKeys::KEY_ANALOG_DOWN] = false;
			});

	/* keyboard-flavoured rows:
	 *   top    - esc + number + letter keys
	 *   mid    - numpad + Q/UP/E (inverted-T head over LEFT/DOWN/RIGHT)
	 *   bottom - ctrl/alt + tab/space + left/down/right arrows;
	 *            stays reachable while the scoreboard covers row1 */
	m_rowTop = new Layout(Orientation::HORIZONTAL);
	m_rowTop->setPadding(0.0f);
	m_rowTop->addChild(m_esc);
	m_rowTop->addChild(m_2);
	m_rowTop->addChild(m_f);
	m_rowTop->addChild(m_g);
	m_rowTop->addChild(m_h);
	m_rowTop->addChild(m_c);
	m_rowTop->addChild(m_y);
	m_rowTop->addChild(m_n);
	if (Settings::showScreenshotBtn()) {
		m_rowTop->addChild(m_f8);
	}
	else {
		delete m_f8;
		m_f8 = nullptr;
	}
	m_rowTop->addChild(m_hud);

	/* Q/E flank UP in cells 5-7, mirroring LEFT/DOWN/RIGHT on the row
	 * below - keeps the same 7-cell grid without invisible spacers */
	m_rowMid = new Layout(Orientation::HORIZONTAL);
	m_rowMid->setPadding(0.0f);
	m_rowMid->addChild(m_num8);
	m_rowMid->addChild(m_num4);
	m_rowMid->addChild(m_num6);
	m_rowMid->addChild(m_num2);
	m_rowMid->addChild(m_q);
	m_rowMid->addChild(m_up);
	m_rowMid->addChild(m_e);

	m_rowBottom = new Layout(Orientation::HORIZONTAL);
	m_rowBottom->setPadding(0.0f);
	m_rowBottom->addChild(m_ctrl);
	m_rowBottom->addChild(m_alt);
	m_rowBottom->addChild(m_tab);
	m_rowBottom->addChild(m_spc);
	m_rowBottom->addChild(m_left);
	m_rowBottom->addChild(m_down);
	m_rowBottom->addChild(m_right);

	addChild(m_rowTop);
	addChild(m_rowMid);
	addChild(m_rowBottom);
	addChild(m_extend);

	m_rowTop->setVisible(false);
	m_rowMid->setVisible(false);
	m_rowBottom->setVisible(false);
}

void ButtonPanel::performLayout()
{
	Widget* p = parent();
	if (!p) return;

	const ImVec2 disp = p->size();
	const float sx = disp.x / 640.0f;
	const float sy = disp.y / 480.0f;

	const float rowH = kRowHeight * sy;
	const float gapX = kGap * sx;
	const float gapY = kGap * sy;
	const float handleH = kHandleHeight * sy;
	const float handleW = kHandleWidth * sx;

	ImVec2 sz = m_extended
			? ImVec2(kPanelWidth * sx, rowH * 3.0f + gapY * 3.0f + handleH)
			: ImVec2(handleW, handleH);

	setFixedSize(sz);
	setPosition(ImVec2((disp.x - sz.x) * 0.5f, disp.y - sz.y - kBottomMargin * sy));

	if (m_extended) {
		Layout* rows[3] = {m_rowTop, m_rowMid, m_rowBottom};
		for (int i = 0; i < 3; i++) {
			rows[i]->setSpacing(gapX);
			rows[i]->setFixedSize(ImVec2(sz.x, rowH));
			rows[i]->setPosition(ImVec2(0.0f, (rowH + gapY) * i));
			rows[i]->performLayout();
		}

		m_extend->setFixedSize(ImVec2(handleW, handleH));
		m_extend->setPosition(ImVec2((sz.x - handleW) * 0.5f, (rowH + gapY) * 3.0f));
	}
	else {
		m_extend->setFixedSize(sz);
		m_extend->setPosition(ImVec2(0.0f, 0.0f));
	}

	m_extend->performLayout();
}

void ButtonPanel::recomputeHeldAxes()
{
	const bool left = m_uiLeft || m_hwA || m_hwDpadLeft;
	const bool right = m_uiRight || m_hwD || m_hwDpadRight;
	const bool up = m_uiUp || m_hwW || m_hwDpadUp;
	const bool down = m_uiDown || m_hwS || m_hwDpadDown;

	const uint16_t prevLR = m_heldLR;
	const uint16_t prevUD = m_heldUD;

	m_heldLR = (left == right) ? 0 : (left ? static_cast<uint16_t>(-128) : static_cast<uint16_t>(128));
	m_heldUD = (up == down) ? 0 : (up ? static_cast<uint16_t>(-128) : static_cast<uint16_t>(128));

	if (prevLR != 0 && m_heldLR == 0) {
		LocalPlayerKeys.wKeyLR = 0;
	}
	if (prevUD != 0 && m_heldUD == 0) {
		LocalPlayerKeys.wKeyUD = 0;
	}
}

void ButtonPanel::clearHeldKeys()
{
	for (int i = 0; i < 32; ++i) {
		if (m_heldBKeys[i]) {
			m_heldBKeys[i] = false;
			if (i < ePadKeys::SIZE) {
				LocalPlayerKeys.bKeys[i] = false;
			}
		}
	}
	m_uiLeft = m_uiRight = m_uiUp = m_uiDown = false;
	m_hwW = m_hwS = m_hwA = m_hwD = false;
	m_hwDpadUp = m_hwDpadDown = m_hwDpadLeft = m_hwDpadRight = false;
	if (m_heldLR != 0) {
		m_heldLR = 0;
		LocalPlayerKeys.wKeyLR = 0;
	}
	if (m_heldUD != 0) {
		m_heldUD = 0;
		LocalPlayerKeys.wKeyUD = 0;
	}
}

void ButtonPanel::applyHeldKeys()
{
	/* re-pin held keys every frame: GetKeys memsets bKeys on each read
	 * and engine pad hooks overwrite LocalPlayerKeys when polled, so a
	 * one-shot write can be lost before the next sync packet goes out */
	for (int i = 0; i < 32; i++) {
		if (m_heldBKeys[i]) {
			LocalPlayerKeys.bKeys[i] = true;
		}
	}
	if (m_heldLR) {
		LocalPlayerKeys.wKeyLR = m_heldLR;
	}
	if (m_heldUD) {
		LocalPlayerKeys.wKeyUD = m_heldUD;
	}
	/* same reason: mouse buttons (LMB/RMB/MMB) are held keys too */
	NativeCameraMouseApplyKeys();
}

void ButtonPanel::draw(ImGuiRenderer* renderer)
{
	applyHeldKeys();
	Widget::draw(renderer);
}

void ButtonPanel::handleHardwareKey(int androidKeyCode, bool isDown)
{
	if (!Settings::kmEnabled()) {
		if (!isDown) {
			clearHeldKeys();
		}
		return;
	}
	if (!pUI || !pGame) return;

	// If in-game ImGui keyboard is open, still process key-up releases so held WASD/fire
	// never latches across modal focus transitions; block new key-down actions except ESC.
	if (isDown && pUI->keyboard() && pUI->keyboard()->visible()) {
		if (androidKeyCode == 111 /* KEYCODE_ESCAPE */) {
			pUI->keyboard()->hide();
		}
		return;
	}

	// If a SA-MP Menu is open, allow keyboard navigation (Up/Down/Enter/Space/F/ESC)
	if (isDown && pNetGame && pNetGame->GetMenuPool() && pNetGame->GetMenuPool()->IsVisible()) {
		if (androidKeyCode == 19 || androidKeyCode == 51) { // DPAD_UP / W
			pNetGame->GetMenuPool()->MoveSelection(-1);
			return;
		}
		if (androidKeyCode == 20 || androidKeyCode == 47) { // DPAD_DOWN / S
			pNetGame->GetMenuPool()->MoveSelection(1);
			return;
		}
		if (androidKeyCode == 66 || androidKeyCode == 62) { // ENTER / SPACE
			pNetGame->GetMenuPool()->SelectCurrentRow();
			return;
		}
		if (androidKeyCode == 34 || androidKeyCode == 111) { // F / ESC
			pNetGame->GetMenuPool()->ExitMenu();
			return;
		}
	}

	CPlayerPed* pPed = pGame->FindPlayerPed();
	bool inVeh = (pPed && pPed->IsInVehicle());

	switch (androidKeyCode) {
		case 48:  // KEYCODE_T
		case 136: // KEYCODE_F6
			if (isDown) {
				clearHeldKeys();
				if (Settings::systemkeyboard()) {
					if (g_java) g_java->showKeyboard(true);
				}
				else if (pUI->keyboard() && pUI->chat()) {
					pUI->keyboard()->show(pUI->chat());
				}
			}
			break;

		case 61: // KEYCODE_TAB
			if (isDown) {
				if (pUI->scoreboard()) {
					pUI->scoreboard()->setVisible(!pUI->scoreboard()->visible());
				}
				if (!inVeh) {
					LocalPlayerKeys.bKeys[ePadKeys::KEY_ACTION] = true;
				}
			}
			break;

		case 135: // KEYCODE_F5
			if (isDown && pUI->netstats()) {
				pUI->netstats()->setClientStatsVisible(!pUI->netstats()->clientStatsVisible());
			}
			break;

		case 137: // KEYCODE_F7
			if (isDown && pUI->chat()) {
				pUI->chat()->setVisible(!pUI->chat()->visible());
			}
			break;

		case 138: // KEYCODE_F8
			if (isDown) {
				Hooks::requestScreenshot(-1);
			}
			break;

		case 140: // KEYCODE_F10
			if (isDown && pUI->netstats()) {
				pUI->netstats()->setServerStatsVisible(!pUI->netstats()->serverStatsVisible());
			}
			break;

		case 111: // KEYCODE_ESCAPE
			if (isDown) {
				InputArbiter::dismissTopNativeModal();
			}
			break;

		case 35: // KEYCODE_G
			if (isDown && pNetGame && pNetGame->GetPlayerPool() && pNetGame->GetPlayerPool()->GetLocalPlayer()) {
				pNetGame->GetPlayerPool()->GetLocalPlayer()->EnterVehicleAsPassenger();
			}
			break;

		case 34: // KEYCODE_F
		case 66: // KEYCODE_ENTER
			if (isDown) {
				LocalPlayerKeys.bKeys[ePadKeys::KEY_SECONDARY_ATTACK] = true;
			}
			break;

		case 53: // KEYCODE_Y
			if (isDown) {
				LocalPlayerKeys.bKeys[ePadKeys::KEY_YES] = true;
			}
			break;

		case 42: // KEYCODE_N
			if (isDown) {
				LocalPlayerKeys.bKeys[ePadKeys::KEY_NO] = true;
			}
			break;

		case 36: // KEYCODE_H
			if (isDown) {
				if (inVeh && pPed && !pPed->IsAPassenger()) {
					LocalPlayerKeys.bKeys[ePadKeys::KEY_CROUCH] = true;
				}
				else {
					LocalPlayerKeys.bKeys[ePadKeys::KEY_CTRL_BACK] = true;
				}
			}
			break;

		case 31: // KEYCODE_C
			if (inVeh) {
				if (isDown) {
					LocalPlayerKeys.bKeys[ePadKeys::KEY_CROUCH] = true;
				}
			}
			else {
				NotifyLocalHardwareCrouchKey(isDown);
			}
			break;

		case 9: // KEYCODE_2
			if (isDown) {
				LocalPlayerKeys.bKeys[ePadKeys::KEY_SUBMISSION] = true;
				if (inVeh && pPed && !pPed->IsAPassenger()) {
					sa::CVehicle* pVeh = pPed->GetGtaVehicle();
					if (pVeh && GetVehicleSubtype(pVeh) == VEHICLE_SUBTYPE_PLANE) {
						RequestLocalPlaneGearToggle();
					}
				}
			}
			break;

		case 45: // KEYCODE_Q
			m_heldBKeys[ePadKeys::KEY_LOOK_LEFT] = isDown;
			if (!isDown) LocalPlayerKeys.bKeys[ePadKeys::KEY_LOOK_LEFT] = false;
			break;

		case 33: // KEYCODE_E
			m_heldBKeys[ePadKeys::KEY_LOOK_RIGHT] = isDown;
			if (!isDown) LocalPlayerKeys.bKeys[ePadKeys::KEY_LOOK_RIGHT] = false;
			break;

		case 57: // KEYCODE_ALT_LEFT
		case 58: // KEYCODE_ALT_RIGHT
			if (inVeh) {
				m_heldBKeys[ePadKeys::KEY_FIRE] = isDown;
				if (!isDown) LocalPlayerKeys.bKeys[ePadKeys::KEY_FIRE] = false;
			}
			else {
				m_heldBKeys[ePadKeys::KEY_WALK] = isDown;
				if (!isDown) LocalPlayerKeys.bKeys[ePadKeys::KEY_WALK] = false;
			}
			break;

		case 113: // KEYCODE_CTRL_LEFT
		case 114: // KEYCODE_CTRL_RIGHT
			m_heldBKeys[ePadKeys::KEY_FIRE] = isDown;
			if (!isDown) {
				LocalPlayerKeys.bKeys[ePadKeys::KEY_FIRE] = false;
			}
			else if (pNetGame) {
				pNetGame->SendSpawn();
			}
			break;

		case 62: // KEYCODE_SPACE
		case 59: // KEYCODE_SHIFT_LEFT
		case 60: // KEYCODE_SHIFT_RIGHT
			if (inVeh) {
				m_heldBKeys[ePadKeys::KEY_HANDBRAKE] = isDown;
				if (!isDown) LocalPlayerKeys.bKeys[ePadKeys::KEY_HANDBRAKE] = false;
			}
			else {
				m_heldBKeys[ePadKeys::KEY_SPRINT] = isDown;
				if (!isDown) LocalPlayerKeys.bKeys[ePadKeys::KEY_SPRINT] = false;
			}
			break;

		case 51: // KEYCODE_W
			m_hwW = isDown;
			recomputeHeldAxes();
			break;
		case 19: // KEYCODE_DPAD_UP
			m_hwDpadUp = isDown;
			recomputeHeldAxes();
			break;

		case 47: // KEYCODE_S
			m_hwS = isDown;
			recomputeHeldAxes();
			break;
		case 20: // KEYCODE_DPAD_DOWN
			m_hwDpadDown = isDown;
			recomputeHeldAxes();
			break;

		case 29: // KEYCODE_A
			m_hwA = isDown;
			recomputeHeldAxes();
			if (isDown && pNetGame) {
				pNetGame->SendPrevClass();
			}
			break;
		case 21: // KEYCODE_DPAD_LEFT
			m_hwDpadLeft = isDown;
			recomputeHeldAxes();
			if (isDown && pNetGame) {
				pNetGame->SendPrevClass();
			}
			break;

		case 32: // KEYCODE_D
			m_hwD = isDown;
			recomputeHeldAxes();
			if (isDown && pNetGame) {
				pNetGame->SendNextClass();
			}
			break;
		case 22: // KEYCODE_DPAD_RIGHT
			m_hwDpadRight = isDown;
			recomputeHeldAxes();
			if (isDown && pNetGame) {
				pNetGame->SendNextClass();
			}
			break;

		case 152: // KEYCODE_NUMPAD_8
			m_heldBKeys[ePadKeys::KEY_ANALOG_UP] = isDown;
			if (!isDown) LocalPlayerKeys.bKeys[ePadKeys::KEY_ANALOG_UP] = false;
			break;

		case 146: // KEYCODE_NUMPAD_2
			m_heldBKeys[ePadKeys::KEY_ANALOG_DOWN] = isDown;
			if (!isDown) LocalPlayerKeys.bKeys[ePadKeys::KEY_ANALOG_DOWN] = false;
			break;

		case 148: // KEYCODE_NUMPAD_4
			m_heldBKeys[ePadKeys::KEY_ANALOG_LEFT] = isDown;
			if (!isDown) LocalPlayerKeys.bKeys[ePadKeys::KEY_ANALOG_LEFT] = false;
			break;

		case 150: // KEYCODE_NUMPAD_6
			m_heldBKeys[ePadKeys::KEY_ANALOG_RIGHT] = isDown;
			if (!isDown) LocalPlayerKeys.bKeys[ePadKeys::KEY_ANALOG_RIGHT] = false;
			break;

		case 30: // KEYCODE_B
		case 50: // KEYCODE_V
			if (pUI->voicebutton() && Settings::voice() && Settings::voiceMicEnable()) {
				if (Settings::voiceMicMode() == 1) {
					if (isDown) {
						pUI->voicebutton()->setRecording(!pUI->voicebutton()->recording());
					}
				}
				else {
					pUI->voicebutton()->setRecording(isDown);
				}
			}
			break;

		default:
			break;
	}
}

void ButtonPanel::minimize()
{
	m_extended = false;

	/* drop any held keys: wKeyLR/wKeyUD are never cleared by GetKeys so
	 * a leaked hold would stay in the sync stream indefinitely */
	memset(m_heldBKeys, 0, sizeof(m_heldBKeys));
	m_heldLR = 0;
	m_heldUD = 0;
	LocalPlayerKeys.wKeyLR = 0;
	LocalPlayerKeys.wKeyUD = 0;

	m_extend->setCaption(kCaptionExpand);
	m_extend->setVisible(true);

	m_rowTop->setVisible(false);
	m_rowMid->setVisible(false);
	m_rowBottom->setVisible(false);

	performLayout();
}

void ButtonPanel::maximize()
{
	m_extended = true;

	m_extend->setCaption(kCaptionCollapse);
	m_extend->setVisible(true);

	m_rowTop->setVisible(true);
	m_rowMid->setVisible(true);
	m_rowBottom->setVisible(true);

	performLayout();
}

/* PassengerButton */

ButtonPanel::PassengerButton::PassengerButton()
		: Image("samp", "gtexture")
{

}

void ButtonPanel::PassengerButton::draw(ImGuiRenderer* renderer)
{
	if (pNetGame) {
		CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
		CPlayerPed* pPlayerPed = pGame->FindPlayerPed();

		if (pVehiclePool && pPlayerPed && !pPlayerPed->IsInVehicle() && !pPlayerPed->IsAPassenger()) {
			VEHICLEID ClosetVehicleID = pVehiclePool->FindNearestToLocalPlayerPed();
			if (ClosetVehicleID < MAX_VEHICLES && pVehiclePool->GetSlotState(ClosetVehicleID)) {
				CVehicle* pVehicle = pVehiclePool->GetAt(ClosetVehicleID);
				if (pVehicle && pVehicle->GetDistanceFromLocalPlayerPed() < 4.0f) {
					Image::draw(renderer);
				}
			}
		}
	}
}

void ButtonPanel::PassengerButton::touchPopEvent()
{
	if (pNetGame && pNetGame->GetPlayerPool() && pNetGame->GetPlayerPool()->GetLocalPlayer()) {
		pNetGame->GetPlayerPool()->GetLocalPlayer()->EnterVehicleAsPassenger();
	}
}
