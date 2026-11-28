#pragma once

#define DEBUG_GUI 0

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>

#include "ImGuiWrapper.h"
#include "Widget.h"

class SplashScreen;
class Chat;
class Spawn;
class Keyboard;
class ButtonPanel;
class Scoreboard;
class VoiceButton;
class DebugLabels;
class DeathWindow;
class NetStats;

class UI : public Widget, public ImGuiWrapper {
public:
	UI(const ImVec2& display_size, const std::string& fonts_path);

	bool initialize() override;
	void render() override;
	void shutdown() override;

	SplashScreen* splashscreen() const { return m_splashScreen; }
	Chat* chat() const { return m_chat; }
	Spawn* spawn() const { return m_spawn; }
	Keyboard* keyboard() const { return m_keyboard; }
	ButtonPanel* buttonpanel() const { return m_buttonPanel; }
	Scoreboard* scoreboard() const { return m_scoreboard; }
	VoiceButton* voicebutton() const { return m_voiceButton; }
	DebugLabels* debuglabels() const { return m_debugLabels; }
	DeathWindow* deathwindow() const { return m_deathWindow; }
	NetStats* netstats() const { return m_netStats; }

	static const ImColor fixcolor(uint32_t color)
	{
		return ImColor(
				(int) ((color & 0xFF000000) >> 24),
				(int) ((color & 0x00FF0000) >> 16),
				(int) ((color & 0x0000FF00) >> 8));
	}

	float ScaleX(float x)
	{
		return x * displaySize().x * (1.0f / 1920.0f);
	}

	float ScaleY(float y)
	{
		return y * displaySize().y * (1.0f / 1080.0f);
	}

	float GetFontSize();

	virtual void touchEvent(const ImVec2& pos, TouchType type) override;

	// Thread-safe touch bridge for NVEvent thread -> GameMain thread
	void enqueueTouchEvent(const ImVec2& pos, TouchType type, bool isCancel = false, int pointerId = 0);
	void processPendingTouchEvents();
	void updateTouchSnapshot();
	bool isButtonPanelHitSnapshot(const ImVec2& pos) const;
	ImVec2 displaySizeSnapshot() const;

protected:
	void drawList() override;

private:
	bool hitsAnyInteractiveChild(const ImVec2& pos);

	struct PendingTouch {
		ImVec2 pos;
		TouchType type;
		bool isCancel;
		int pointerId;
	};

	Keyboard* m_keyboard = nullptr;
	SplashScreen* m_splashScreen = nullptr;
	Chat* m_chat = nullptr;
	Spawn* m_spawn = nullptr;
	ButtonPanel* m_buttonPanel = nullptr;
	Scoreboard* m_scoreboard = nullptr;
	VoiceButton* m_voiceButton = nullptr;
	DebugLabels* m_debugLabels = nullptr;
	DeathWindow* m_deathWindow = nullptr;
	NetStats* m_netStats = nullptr;

	mutable std::mutex m_touchMutex;
	std::deque<PendingTouch> m_pendingTouches;
	bool m_snapButtonPanelVisible = false;
	ImVec2 m_snapButtonPanelPos{0.0f, 0.0f};
	ImVec2 m_snapButtonPanelSize{0.0f, 0.0f};
	ImVec2 m_snapDisplaySize{1920.0f, 1080.0f};
	int m_activeTouchId = -1;
	bool m_deferredPopPending = false;
	ImVec2 m_deferredPopPos{0.0f, 0.0f};

public:
	bool m_needClearMousePos = false;
};
