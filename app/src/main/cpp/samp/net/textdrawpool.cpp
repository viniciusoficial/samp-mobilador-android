#include "../main.h"
#include "../game/game.h"
#include "netgame.h"
#include "textdrawpool.h"

extern CGame* pGame;
extern CNetGame* pNetGame;

// 0.3.7
CTextDrawPool::CTextDrawPool()
{
    for (int i = 0; i < MAX_TEXT_DRAWS; i++) {
        m_pTextDraw[i] = nullptr;
        m_bSlotState[i] = false;
    }

    ResetTextDrawTextures();

    // ====
    m_bSelectState = false;
    m_dwHoverColor = 0;
    m_wClickedTextDrawID = 0xFFFF;
}
// 0.3.7
CTextDrawPool::~CTextDrawPool()
{
    for (int i = 0; i < MAX_TEXT_DRAWS; i++) {
        Delete(i);
    }
}
// 0.3.7
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
// 0.3.7
void CTextDrawPool::Delete(uint16_t wTextDrawID)
{
    if (wTextDrawID >= MAX_TEXT_DRAWS) return;

    if (m_pTextDraw[wTextDrawID]) {
        delete m_pTextDraw[wTextDrawID];
        m_pTextDraw[wTextDrawID] = nullptr;
        m_bSlotState[wTextDrawID] = false;
    }
}
// 0.3.7
void CTextDrawPool::Draw()
{
    for (int i = 0; i < MAX_TEXT_DRAWS; i++)
    {
        if (m_bSlotState[i] && m_pTextDraw[i]) {
            m_pTextDraw[i]->Draw();
        }
    }
}

void CTextDrawPool::DrawImage()
{
    /*for (int i = 0; i < MAX_TEXT_DRAWS; i++)
    {
        if (m_bSlotState[i]) {
            m_pTextDraw[i]->DrawImage();
        }
    }*/
}

// ========

void CTextDrawPool::SetSelectState(bool bState, uint32_t dwColor)
{
    if (bState) {
        m_bSelectState = true;
        m_dwHoverColor = (((dwColor << 16) | dwColor & 0xFF00) << 8) | (((dwColor >> 16) | dwColor & 0xFF0000) >> 8);
        m_wClickedTextDrawID = 0xFFFF;
        if (pGame) {
            pGame->DisplayHUD(false);
            // FindPlayerPed fica nulo durante o spawn/entre conexoes.
            CPlayerPed* pPed = pGame->FindPlayerPed();
            if (pPed) pPed->TogglePlayerControllable(false);
        }
    }
    else {
        m_bSelectState = false;
        m_dwHoverColor = 0;
        m_wClickedTextDrawID = 0xFFFF;
        if (pGame) {
            pGame->DisplayHUD(true);
            CPlayerPed* pPed = pGame->FindPlayerPed();
            if (pPed) pPed->TogglePlayerControllable(true);
        }

        SendClick();

        for (int i = 0; i < MAX_TEXT_DRAWS; i++)
        {
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
    // 0xFFFF = nenhum textdraw clicado. SetSelectState(false) chamava SendClick
    // sempre e o servidor recebia um click em textdraw inexistente (podia
    // reabrir dialogo/menu que o jogador tinha acabado de fechar).
    if (m_wClickedTextDrawID == 0xFFFF) return;
    if (!pNetGame || !pNetGame->GetRakClient()) return;

    RakNet::BitStream bsClick;
    bsClick.Write(m_wClickedTextDrawID);
    pNetGame->GetRakClient()->RPC(&RPC_ClickTextDraw, &bsClick, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false, UNASSIGNED_NETWORK_ID, nullptr);
}

bool CTextDrawPool::onTouchEvent(int type, bool multi, int x, int y)
{
    if (m_bSelectState == false) return true;
    static bool bWannaClick = false;

    m_wClickedTextDrawID = 0xFFFF;


    for (int i = 0; i < MAX_TEXT_DRAWS; i++)
    {
        if (m_bSlotState[i] && m_pTextDraw[i])
        {
            CTextDraw* pTextDraw = m_pTextDraw[i];
            pTextDraw->m_bHovered = false;
            pTextDraw->m_dwHoverColor = 0;

            if (pTextDraw->m_TextDrawData.byteSelectable)
            {
                switch (type)
                {
                    case 2:
                        if (IsPointInRect(x, y, &(pTextDraw->m_rectArea)))
                        {
                            bWannaClick = true;
                            return false;
                        }
                        break;
                    case 3:
                        /*m_wClickedTextDrawID = 0xFFFF;
                        pTextDraw->m_bHovered = true;
                        pTextDraw->m_dwHoverColor = m_dwHoverColor;
                        Log("x: %f, y:%f, %d",IsPointInRect(x, y, &pTextDraw->m_rectArea));*/
                        break;

                    case 1:
                        if (IsPointInRect(x, y, &pTextDraw->m_rectArea)) {
                            m_wClickedTextDrawID = i;
                            SendClick();
                            pTextDraw->m_bHovered = false;
                            pTextDraw->m_dwHoverColor = 0;
                            return false;
                        }
                        bWannaClick = false;
                        break;
                }
            }
        }
    }

    return true;
}

void CTextDrawPool::SnapshotProcess()
{
    if (!pNetGame || !pGame) return;
    if (pGame->IsGamePaused()) return;

    for (int i = 0; i < MAX_TEXT_DRAWS; i++)
    {
        if (m_bSlotState[i] && m_pTextDraw[i]) {
            m_pTextDraw[i]->SnapshotProcess();
        }
    }
}