#pragma once

#include <atomic>
#include <deque>
#include <mutex>

#include "NetTypes.h"
#include "../Game/TextDraw.h"

class ImGuiRenderer;

#define MAX_TEXT_DRAW_LINE 800

class CTextDrawPool {
public:
	CTextDrawPool();
	~CTextDrawPool();

	void New(uint16_t wTextDrawID, TEXT_DRAW_TRANSMIT* pTextDrawTransmit, const char* szText);
	void Delete(uint16_t wTextDrawID);
	void Reset();
	void Draw();
	void DrawImGui(ImGuiRenderer* renderer);

	CTextDraw* GetAt(uint16_t wTextDrawID)
	{
		if (wTextDrawID >= MAX_TEXT_DRAWS) return nullptr;
		if (!m_bSlotState[wTextDrawID]) return nullptr;
		return m_pTextDraw[wTextDrawID];
	}

	bool GetState() const
	{
		return m_bSelectState.load(std::memory_order_acquire);
	}

	void SetSelectState(bool bState, uint32_t dwColor = 0, bool bSendCancel = true);
	bool onTouchEvent(int type, int x, int y);
	// Thread-safe touch entry point called from NVEvent thread; queues events
	// for GameMain processing instead of mutating pool / sending RPC cross-thread.
	bool enqueueTouchEvent(int type, int x, int y);
	void processPendingTouches();

	void SnapshotProcess();
	void RefreshCustomModels();
	void ReleaseCustomModelsBeforeUnload();

private:
	void SendClick();

private:
	struct PendingTouch {
		int type;
		int x;
		int y;
	};

	uint8_t m_bSlotState[MAX_TEXT_DRAWS];
	CTextDraw* m_pTextDraw[MAX_TEXT_DRAWS];

	std::atomic<bool> m_bSelectState{false};
	uint32_t m_dwHoverColor;
	uint16_t m_wClickedTextDrawID;

	std::mutex m_touchMutex;
	std::deque<PendingTouch> m_pendingTouches;
};