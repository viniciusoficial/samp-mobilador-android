#include "TextDrawPool.h"
#include "NetGame.h"
#include "Proto/RpcOutbound.h"
#include "../Client.h"
#include "../Game/Game.h"
#include "../Game/TextDraw.h"
#include "../Game/Util.h"
#include <RakNet/RakClientInterface.h>
#include <RakNet/SAMP/SAMPRPC.h>
#include <spdlog/spdlog.h>


CTextDrawPool::CTextDrawPool()
{
	for (int i = 0; i < MAX_TEXT_DRAWS; i++) {
		m_pTextDraw[i] = nullptr;
		m_bSlotState[i] = false;
	}

	ResetTextDrawTextures();

	m_bSelectState = false;
	m_dwHoverColor = 0;
	m_wClickedTextDrawID = 0xFFFF;
}

CTextDrawPool::~CTextDrawPool()
{
	Reset();
}

void CTextDrawPool::Reset()
{
	SetSelectState(false, 0, false);
	for (int i = 0; i < MAX_TEXT_DRAWS; i++) {
		Delete(i);
	}
	ResetTextDrawTextures();
}

/**
 * @param wTextDrawID
 * @param pTextDrawTransmit
 * @param szText
 */
void CTextDrawPool::New(uint16_t wTextDrawID, TEXT_DRAW_TRANSMIT* pTextDrawTransmit, const char* szText)
{
	if (wTextDrawID >= MAX_TEXT_DRAWS) return;

	if (m_pTextDraw[wTextDrawID]) {
		Delete(wTextDrawID);
	}

	CTextDraw* pTextDraw = new CTextDraw(pTextDrawTransmit, szText);
	if (pTextDraw == nullptr) return;

	m_pTextDraw[wTextDrawID] = pTextDraw;
	m_bSlotState[wTextDrawID] = true;
}

/**
 * @param wTextDrawID
 */
void CTextDrawPool::Delete(uint16_t wTextDrawID)
{
	if (wTextDrawID >= MAX_TEXT_DRAWS) return;

	if (m_pTextDraw[wTextDrawID]) {
		delete m_pTextDraw[wTextDrawID];
		m_pTextDraw[wTextDrawID] = nullptr;
		m_bSlotState[wTextDrawID] = false;
	}
}

void CTextDrawPool::Draw()
{
	for (int i = 0; i < MAX_TEXT_DRAWS; i++) {
		if (m_bSlotState[i]) {
			m_pTextDraw[i]->Draw();
		}
	}
}

void CTextDrawPool::DrawImGui(ImGuiRenderer* renderer)
{
	for (int i = 0; i < MAX_TEXT_DRAWS; i++) {
		if (m_bSlotState[i]) {
			m_pTextDraw[i]->DrawImGui(renderer);
		}
	}
}

void CTextDrawPool::SetSelectState(bool bState, uint32_t dwColor, bool bSendCancel)
{
	if (bState) {
		const bool alreadySelecting = m_bSelectState.exchange(true, std::memory_order_acq_rel);
		// SelectTextDraw RPC transmits hover color in RGBA (0xRRGGBBAA), while
		// CTextDraw::Draw / DrawImGui / DrawTextureUV consume ABGR (0xAABBGGRR).
		m_dwHoverColor = __builtin_bswap32(dwColor);
		m_wClickedTextDrawID = 0xFFFF;
		if (!alreadySelecting && pGame) {
			pGame->SetHudHidden(Game::HUD_HIDE_TEXTDRAW_SELECT, true);
		}
	}
	else {
		const bool wasSelecting = m_bSelectState.exchange(false, std::memory_order_acq_rel);
		m_dwHoverColor = 0;
		m_wClickedTextDrawID = 0xFFFF;
		{
			std::lock_guard<std::mutex> lock(m_touchMutex);
			m_pendingTouches.clear();
		}
		if (wasSelecting && pGame) {
			pGame->SetHudHidden(Game::HUD_HIDE_TEXTDRAW_SELECT, false);
		}

		if (wasSelecting && bSendCancel) {
			SendClick();
		}

		for (int i = 0; i < MAX_TEXT_DRAWS; i++) {
			if (m_bSlotState[i] && m_pTextDraw[i]) {
				CTextDraw* pTextDraw = m_pTextDraw[i];
				pTextDraw->m_bHovered = false;
				pTextDraw->m_dwHoverColor = 0;
			}
		}
	}
}

void CTextDrawPool::SendClick()
{
	if (!pNetGame || !pNetGame->GetRakClient()) return;
	RakNet::BitStream bsClick;
	proto::Writer writer(bsClick);
	proto::ClickTextDrawReq req{};
	req.textDrawId = m_wClickedTextDrawID;
	req.serialize(writer, proto::ProtoVersion::V037);
	pNetGame->GetRakClient()->RPC(&RPC_ClickTextDraw, &bsClick, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false, UNASSIGNED_NETWORK_ID, nullptr);
}

bool CTextDrawPool::enqueueTouchEvent(int type, int x, int y)
{
	if (!m_bSelectState.load(std::memory_order_acquire)) return true;

	std::lock_guard<std::mutex> lock(m_touchMutex);
	if (m_pendingTouches.size() >= 64) {
		m_pendingTouches.pop_front();
	}
	m_pendingTouches.push_back({type, x, y});
	return false;
}

void CTextDrawPool::processPendingTouches()
{
	std::deque<PendingTouch> queue;
	{
		std::lock_guard<std::mutex> lock(m_touchMutex);
		if (m_pendingTouches.empty()) return;
		queue.swap(m_pendingTouches);
	}
	for (const auto& ev : queue) {
		onTouchEvent(ev.type, ev.x, ev.y);
	}
}

bool CTextDrawPool::onTouchEvent(int type, int x, int y)
{
	if (!m_bSelectState.load(std::memory_order_acquire)) return true;

	m_wClickedTextDrawID = 0xFFFF;
	int topHitId = -1;

	for (int i = 0; i < MAX_TEXT_DRAWS; i++) {
		if (m_bSlotState[i] && m_pTextDraw[i]) {
			CTextDraw* pTextDraw = m_pTextDraw[i];
			pTextDraw->m_bHovered = false;
			pTextDraw->m_dwHoverColor = 0;

			if (pTextDraw->m_TextDrawData.byteSelectable && pTextDraw->m_TextDrawData.bHasRectArea) {
				sa::CRect* rect = &pTextDraw->m_rectArea;
				if (IsPointInRect(x, y, rect)) {
					topHitId = i;
				}
			}
		}
	}

	if (topHitId >= 0 && m_pTextDraw[topHitId]) {
		CTextDraw* pTop = m_pTextDraw[topHitId];
		switch (type) {
			case 2:
			case 3:
				m_wClickedTextDrawID = 0xFFFF;
				pTop->m_bHovered = true;
				pTop->m_dwHoverColor = m_dwHoverColor;
				break;
			case 1:
				m_wClickedTextDrawID = static_cast<uint16_t>(topHitId);
				pTop->m_bHovered = false;
				pTop->m_dwHoverColor = 0;
				break;
		}
	}

	if (m_wClickedTextDrawID != 0xFFFF) {
		SendClick();
	}

	return false;
}

void CTextDrawPool::SnapshotProcess()
{
	for (int i = 0; i < MAX_TEXT_DRAWS; i++) {
		if (m_bSlotState[i] && m_pTextDraw[i]) {
			m_pTextDraw[i]->SnapshotProcess();
		}
	}
}

void CTextDrawPool::RefreshCustomModels()
{
	for (int i = 0; i < MAX_TEXT_DRAWS; i++) {
		if (m_bSlotState[i] && m_pTextDraw[i]) {
			m_pTextDraw[i]->RefreshCustomAsset();
		}
	}
}

void CTextDrawPool::ReleaseCustomModelsBeforeUnload()
{
	for (int i = 0; i < MAX_TEXT_DRAWS; i++) {
		if (m_bSlotState[i] && m_pTextDraw[i]) {
			m_pTextDraw[i]->ReleaseCustomAssetBeforeUnload();
		}
	}
}
