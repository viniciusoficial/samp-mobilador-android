#pragma once

#include "Proto/RpcMenu.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>

class ImGuiRenderer;

class CMenuPool {
public:
	static constexpr uint8_t MAX_MENUS = 128;
	static constexpr uint8_t INVALID_MENU_ID = 0xFF;

	CMenuPool() = default;
	~CMenuPool() = default;

	void InitMenu(const proto::InitMenu& p);
	void ShowMenu(uint8_t menuId);
	void HideMenu(uint8_t menuId);
	void Reset();

	bool IsVisible() const { return m_visible.load(std::memory_order_acquire); }

	// Navigation / actions (called on GameMain)
	void MoveSelection(int delta);
	void SelectCurrentRow();
	void ExitMenu();

	// Thread-safe touch entry point called from NVEvent thread (AND_TouchEvent_hook).
	// Returns false if touch was consumed by the active menu panel.
	bool enqueueTouchEvent(int type, int x, int y);
	// Drains queued touches on GameMain (Client::process).
	void processPendingTouches();
	// Renders active menu panel on GameMain (UI::drawList).
	void Render(ImGuiRenderer* renderer);

private:
	struct MenuData {
		std::string title;
		float posX = 0.0f;
		float posY = 0.0f;
		uint8_t columns = 1;
		float colWidth[2] = {150.0f, 150.0f};
		bool menuEnabled = true;
		std::array<bool, proto::kMaxMenuItems> rowEnabled{};
		std::array<std::string, 2> columnHeaders{};
		std::array<uint8_t, 2> columnItemCount{};
		std::array<std::array<std::string, proto::kMaxMenuItems>, 2> items{};

		uint8_t rowCount() const
		{
			return columnItemCount[0];
		}
	};

	struct Rect {
		float minX = 0.0f;
		float minY = 0.0f;
		float maxX = 0.0f;
		float maxY = 0.0f;
		bool contains(float x, float y) const
		{
			return x >= minX && x <= maxX && y >= minY && y <= maxY;
		}
	};

	struct LayoutSnapshot {
		bool valid = false;
		Rect panel{};
		uint8_t rowCount = 0;
		std::array<Rect, proto::kMaxMenuItems> rowRects{};
		Rect selectBtn{};
		Rect exitBtn{};
	};

	struct PendingTouch {
		int type;
		int x;
		int y;
	};

	static std::string formatMenuText(const std::string& raw, const char* resetHex);
	int findFirstEnabledRow(const MenuData& menu) const;
	void sendMenuSelect(uint8_t row);
	void sendMenuQuit();
	void hideActiveInternal();

	std::array<std::unique_ptr<MenuData>, MAX_MENUS> m_menus{};
	uint8_t m_currentMenu = INVALID_MENU_ID;
	int m_selectedRow = -1;
	std::atomic<bool> m_visible{false};

	std::mutex m_touchMutex;
	std::deque<PendingTouch> m_pendingTouches;
	LayoutSnapshot m_layout{};
	// NVEvent thread press tracking
	bool m_touchInsidePanel = false;
};
