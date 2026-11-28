#include "UI.h"
#include "UISettings.h"
#include "SAMPWidgets/PlayerTags.h"
#include "SAMPWidgets/Keyboard.h"
#include "SAMPWidgets/SplashScreen.h"
#include "SAMPWidgets/Chat.h"
#include "SAMPWidgets/Spawn.h"
#include "SAMPWidgets/ButtonPanel.h"
#include "SAMPWidgets/VoiceButton.h"
#include "SAMPWidgets/Scoreboard.h"
#include "SAMPWidgets/DebugLabels.h"
#include "SAMPWidgets/DeathWindow.h"
#include "SAMPWidgets/DownloadOverlay.h"
#include "SAMPWidgets/NetStats.h"
#include "../Client.h"
#include "../ClientSubsystems.h"
#include "../InputArbiter.h"
#include "../Settings.h"
#include "../Game/Game.h"
#include "../Game/PlayerPed.h"
#include "../Net/NetGame.h"
#include "../Net/MenuPool.h"
#include "../Net/PlayerBubblePool.h"
#include "../Net/TextDrawPool.h"
#include "../Net/TextLabelPool.h"

float UI::GetFontSize()
{
	return UISettings::fontSize();
}


UI::UI(const ImVec2& display_size, const std::string& fonts_path)
		: Widget(), ImGuiWrapper(display_size, fonts_path)
{
	UISettings::Initialize(display_size);
	this->setFixedSize(display_size);
	m_snapDisplaySize = display_size;
}

bool UI::initialize()
{
	if (!ImGuiWrapper::initialize()) return false;

	m_splashScreen = new SplashScreen();
	this->addChild(m_splashScreen);
	m_splashScreen->setFixedSize(size());
	m_splashScreen->setPosition(ImVec2(0.0f, 0.0f));
	m_splashScreen->setVisible(true);

	m_deathWindow = new DeathWindow();
	this->addChild(m_deathWindow);
	m_deathWindow->setSize(ImVec2(0, 0));
	m_deathWindow->setPosition(ImVec2(0, 0));
	m_deathWindow->setVisible(true);

	m_debugLabels = new DebugLabels();
	this->addChild(m_debugLabels);
	m_debugLabels->setSize(ImVec2(0, 0));
	m_debugLabels->setPosition(ImVec2(0, 0));
	m_debugLabels->setVisible(false);

	m_netStats = new NetStats();
	this->addChild(m_netStats);
	m_netStats->setSize(ImVec2(0, 0));
	m_netStats->setPosition(ImVec2(0, 0));
	m_netStats->setVisible(false);

	m_chat = new Chat();
	this->addChild(m_chat);
	m_chat->setFixedSize(UISettings::chatSize());
	m_chat->setPosition(UISettings::chatPos());
	m_chat->setItemSize(UISettings::chatItemSize());
	m_chat->setVisible(false);

	m_buttonPanel = new ButtonPanel();
	this->addChild(m_buttonPanel);
	m_buttonPanel->setVisible(false);

	auto passengerButton = new ButtonPanel::PassengerButton();
	this->addChild(passengerButton);
	passengerButton->setFixedSize(UISettings::buttonVoiceSize());
	passengerButton->setPosition(UISettings::buttonVoicePos() + ImVec2(ScaleX(150.0f), 0.0f));
	passengerButton->setVisible(true);

	m_voiceButton = new VoiceButton();
	this->addChild(m_voiceButton);
	m_voiceButton->setFixedSize(UISettings::buttonVoiceSize());
	m_voiceButton->setPosition(UISettings::buttonVoicePos());
	m_voiceButton->setVisible(false);

	m_spawn = new Spawn();
	this->addChild(m_spawn);
	m_spawn->setFixedSize(UISettings::spawnSize());
	m_spawn->setPosition(UISettings::spawnPos());
	m_spawn->setVisible(false);

	m_keyboard = new Keyboard();
	this->addChild(m_keyboard);
	m_keyboard->setFixedSize(UISettings::keyboardSize());
	m_keyboard->setPosition(UISettings::keyboardPos());
	m_keyboard->setVisible(false);

	m_scoreboard = new Scoreboard();
	this->addChild(m_scoreboard);
	m_scoreboard->setSize(UISettings::scoreboardSize());
	m_scoreboard->setPosition(ImVec2(((displaySize().x - UISettings::scoreboardSize().x) / 2), ((displaySize().y - UISettings::scoreboardSize().y) / 2)));
	m_scoreboard->setVisible(false);

	/*auto copyright = new Label(OBFUSCATE("SAMPMOBILE"), ImColor(1.0f, 1.0f, 1.0f), true, UISettings::fontSize() / 3);
	this->addChild(copyright);
	copyright->setPosition(ImVec2(RsGlobal->maximumWidth * 0.83, ScaleY(5)));*/

	return true;
}

void UI::render()
{
	processPendingTouchEvents();

	ImGuiWrapper::render();

	if (m_needClearMousePos) {
		ImGuiIO& io = ImGui::GetIO();
		io.MousePos = ImVec2(-1, -1);
		m_needClearMousePos = false;
	}

	updateTouchSnapshot();
}

void UI::enqueueTouchEvent(const ImVec2& pos, TouchType type, bool isCancel, int pointerId)
{
	std::lock_guard<std::mutex> lock(m_touchMutex);
	if (m_pendingTouches.size() >= 128) {
		m_pendingTouches.pop_front();
	}
	m_pendingTouches.push_back({pos, type, isCancel, pointerId});
}

bool UI::hitsAnyInteractiveChild(const ImVec2& pos)
{
	for (Widget* child : children()) {
		if (child && child->visible() && child->width() > 0.0f && child->height() > 0.0f && child->contains(pos)) {
			return true;
		}
	}
	return false;
}

void UI::processPendingTouchEvents()
{
	if (m_deferredPopPending) {
		m_deferredPopPending = false;
		m_activeTouchId = -1;
		touchEvent(m_deferredPopPos, TouchType::pop);
	}

	std::deque<PendingTouch> queue;
	{
		std::lock_guard<std::mutex> lock(m_touchMutex);
		if (m_pendingTouches.empty()) return;
		queue.swap(m_pendingTouches);
	}

	bool pushedThisFrame = false;
	for (const auto& ev : queue) {
		if (ev.isCancel) {
			m_activeTouchId = -1;
			m_deferredPopPending = false;
			pushedThisFrame = false;
			touchEvent(ImVec2(-10000.0f, -10000.0f), TouchType::pop);
			continue;
		}

		if (ev.type == TouchType::push) {
			if (m_activeTouchId == -1) {
				if (hitsAnyInteractiveChild(ev.pos)) {
					if (m_deferredPopPending) {
						m_deferredPopPending = false;
						touchEvent(m_deferredPopPos, TouchType::pop);
					}
					m_activeTouchId = ev.pointerId;
					pushedThisFrame = true;
					touchEvent(ev.pos, TouchType::push);
				}
				else {
					// Background tap outside all SAMP widgets: deactivate any active SAMP widget
					// without locking m_activeTouchId onto the GTA joystick/camera finger.
					touchEvent(ev.pos, TouchType::push);
					touchEvent(ImVec2(-10000.0f, -10000.0f), TouchType::pop);
				}
			}
		}
		else if (ev.type == TouchType::move) {
			if (ev.pointerId == m_activeTouchId) {
				touchEvent(ev.pos, TouchType::move);
			}
		}
		else if (ev.type == TouchType::pop) {
			if (ev.pointerId == m_activeTouchId) {
				if (pushedThisFrame) {
					// Sub-frame tap (push + pop within one render frame): hold MouseDown[0]
					// across one ImGui::NewFrame() so ImGui Selectable/Button sees the click.
					m_deferredPopPending = true;
					m_deferredPopPos = ev.pos;
				}
				else {
					m_activeTouchId = -1;
					touchEvent(ev.pos, TouchType::pop);
				}
			}
		}
	}
}

void UI::updateTouchSnapshot()
{
	InputArbiter::publishGameMainState();

	std::lock_guard<std::mutex> lock(m_touchMutex);
	m_snapDisplaySize = size();
	if (m_buttonPanel && m_buttonPanel->visible()) {
		m_snapButtonPanelVisible = true;
		m_snapButtonPanelPos = m_buttonPanel->absolutePosition();
		m_snapButtonPanelSize = m_buttonPanel->size();
	}
	else {
		m_snapButtonPanelVisible = false;
	}
}

bool UI::isButtonPanelHitSnapshot(const ImVec2& pos) const
{
	std::lock_guard<std::mutex> lock(m_touchMutex);
	if (!m_snapButtonPanelVisible) return false;
	const float dx = pos.x - m_snapButtonPanelPos.x;
	const float dy = pos.y - m_snapButtonPanelPos.y;
	return dx >= 0.0f && dy >= 0.0f && dx < m_snapButtonPanelSize.x && dy < m_snapButtonPanelSize.y;
}

ImVec2 UI::displaySizeSnapshot() const
{
	std::lock_guard<std::mutex> lock(m_touchMutex);
	return m_snapDisplaySize;
}

void UI::shutdown()
{
	ImGuiWrapper::shutdown();
}

void UI::drawList()
{
	if (!visible()) return;

	const bool scoreboardOpen = (m_scoreboard && m_scoreboard->visible());
	if (!scoreboardOpen) {
		if (pPlayerTags && Settings::showNameTags()) pPlayerTags->Render(renderer());
		if (pNetGame && pNetGame->GetTextLabelPool()) pNetGame->GetTextLabelPool()->Render(renderer());
		if (pNetGame && pNetGame->GetPlayerBubblePool()) pNetGame->GetPlayerBubblePool()->Render(renderer());
		if (pNetGame && pNetGame->GetTextDrawPool()) pNetGame->GetTextDrawPool()->DrawImGui(renderer());
		if (pNetGame && pNetGame->GetObjectPool()) pNetGame->GetObjectPool()->DrawSelectOverlay(renderer());
		if (pNetGame && pNetGame->GetMenuPool()) pNetGame->GetMenuPool()->Render(renderer());
		if (pGame) pGame->DrawImGuiGameText(renderer());
	}

	artwork::DrawDownloadOverlay(renderer(), displaySize().x, displaySize().y);

	// Custom crosshair styles (1=精简十字, 2=中心圆点, 3=十字加圆点)
	int chStyle = Settings::crosshairStyle();
	if (chStyle >= 1 && chStyle <= 3 && pGame && pGame->FindPlayerPed()) {
		CPlayerPed* pPed = pGame->FindPlayerPed();
		uint8_t camMode = pPed->GetCameraMode();
		if (camMode == 53 || camMode == 55 || camMode == 7 || camMode == 8 || camMode == 51) {
			ImVec2 disp = displaySize();
			ImVec2 center(disp.x * 0.52f, disp.y * 0.435f);
			ImColor chWhite(255, 255, 255, 235);
			ImColor chBlack(0, 0, 0, 200);
			if (chStyle == 1 || chStyle == 3) {
				float gap = ScaleY(5.0f);
				float len = ScaleY(12.0f);
				float halfT = ScaleY(1.4f);
				// Outlines
				renderer()->drawRect(ImVec2(center.x - gap - len - 1, center.y - halfT - 1), ImVec2(center.x - gap + 1, center.y + halfT + 1), chBlack, true);
				renderer()->drawRect(ImVec2(center.x + gap - 1, center.y - halfT - 1), ImVec2(center.x + gap + len + 1, center.y + halfT + 1), chBlack, true);
				renderer()->drawRect(ImVec2(center.x - halfT - 1, center.y - gap - len - 1), ImVec2(center.x + halfT + 1, center.y - gap + 1), chBlack, true);
				renderer()->drawRect(ImVec2(center.x - halfT - 1, center.y + gap - 1), ImVec2(center.x + halfT + 1, center.y + gap + len + 1), chBlack, true);
				// Bars
				renderer()->drawRect(ImVec2(center.x - gap - len, center.y - halfT), ImVec2(center.x - gap, center.y + halfT), chWhite, true);
				renderer()->drawRect(ImVec2(center.x + gap, center.y - halfT), ImVec2(center.x + gap + len, center.y + halfT), chWhite, true);
				renderer()->drawRect(ImVec2(center.x - halfT, center.y - gap - len), ImVec2(center.x + halfT, center.y - gap), chWhite, true);
				renderer()->drawRect(ImVec2(center.x - halfT, center.y + gap), ImVec2(center.x + halfT, center.y + gap + len), chWhite, true);
			}
			if (chStyle == 2 || chStyle == 3) {
				float r = ScaleY(2.6f);
				renderer()->drawRect(ImVec2(center.x - r - 1, center.y - r - 1), ImVec2(center.x + r + 1, center.y + r + 1), chBlack, true);
				renderer()->drawRect(ImVec2(center.x - r, center.y - r), ImVec2(center.x + r, center.y + r), ImColor(255, 210, 60, 245), true);
			}
		}
	}

	const bool hideVoiceSpeakers = (scoreboard() && scoreboard()->visible()) || (keyboard() && keyboard()->visible());
	ClientSubsystems::onUiRender(renderer(), displaySize().x, displaySize().y, pNetGame != nullptr, hideVoiceSpeakers);

	if (m_buttonPanel && !m_buttonPanel->visible()) {
		m_buttonPanel->applyHeldKeys();
	}

	draw(renderer());
}

void UI::touchEvent(const ImVec2& pos, TouchType type)
{
	if (m_keyboard && m_keyboard->visible() && m_keyboard->contains(pos)) {
		m_keyboard->touchEvent(pos, type);
		return;
	}

	ImGuiIO& io = ImGui::GetIO();
	switch (type) {
		case TouchType::push: io.MousePos = ImVec2(pos.x, pos.y);
			io.MouseDown[0] = true;
			break;
		case TouchType::move: io.MousePos = ImVec2(pos.x, pos.y);
			break;
		case TouchType::pop: io.MouseDown[0] = false;
			m_needClearMousePos = true;
			break;
	}

	Widget::touchEvent(pos, type);
}
