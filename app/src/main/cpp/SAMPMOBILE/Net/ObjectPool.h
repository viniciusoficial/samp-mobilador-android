#pragma once

#include <atomic>
#include <deque>
#include <mutex>
#include <unordered_map>

#include <Core/Vector.h>
#include "NetTypes.h"

namespace sa {
class CEntity;
}
class CObject;
class ImGuiRenderer;

class CObjectPool {
public:
	CObjectPool();
	~CObjectPool();

	bool New(OBJECTID ObjectID, int iModel, sa::CVector vecPos, sa::CVector vecRot, float fDrawDistance);
	bool Delete(OBJECTID ObjectID);
	void Reset();

	void Process();

	CObject* GetAt(OBJECTID ObjectID)
	{
		if (ObjectID < MAX_OBJECTS && m_bObjectSlotState[ObjectID]) {
			return m_pObjects[ObjectID];
		}

		return nullptr;
	}

	OBJECTID FindIDFromGtaPtr(sa::CEntity* pGtaObject);

	CObject* FindObjectFromGtaPtr(sa::CEntity* pGtaObject);

	void ProcessMaterialText();

	// 0.3.DL artwork lifecycle: upgrade fallback objects once custom models finish downloading,
	// and revert live custom objects back to a vanilla model before unloading custom slots.
	void RefreshCustomModels();
	void RevertCustomModelsBeforeUnload();

	// T15: SelectObject mode (RPC 27)
	bool IsSelectState() const { return m_bSelectState.load(std::memory_order_acquire); }
	void SetSelectState(bool bEnable);
	// Called from NVEvent thread (AND_TouchEvent_hook). Returns false if touch was consumed (Cancel button).
	bool enqueueTouchEvent(int type, int x, int y);
	// Called from GameMain (Client::process).
	void processPendingTouches();
	// Called from GameMain (UI::drawList).
	void DrawSelectOverlay(ImGuiRenderer* renderer);

public:
	int m_iObjectCount;
	bool m_bObjectSlotState[MAX_OBJECTS];
	CObject* m_pObjects[MAX_OBJECTS];

private:
	struct PendingTouch {
		int type;
		int x;
		int y;
		bool isCancelBtn;
	};

	OBJECTID PickObjectAtScreenPos(float tapX, float tapY);
	void SendSelectObject(OBJECTID objectId);

	std::unordered_map<sa::CEntity*, OBJECTID> m_mapGtaPtrToID;
	uint32_t m_dwLastTick = 0;

	std::atomic<bool> m_bSelectState{false};
	std::mutex m_touchMutex;
	std::deque<PendingTouch> m_pendingTouches;
	// Cancel button snapshot (updated on GameMain in DrawSelectOverlay, read on NVEvent thread)
	float m_cancelMinX = 0.0f;
	float m_cancelMinY = 0.0f;
	float m_cancelMaxX = 0.0f;
	float m_cancelMaxY = 0.0f;
	bool m_cancelRectValid = false;
	// NVEvent thread gesture tracking (only accessed on NVEvent thread)
	bool m_touchTracking = false;
	bool m_touchStartedOnCancel = false;
	int m_touchStartX = 0;
	int m_touchStartY = 0;
	int m_touchMaxDragSq = 0;
};