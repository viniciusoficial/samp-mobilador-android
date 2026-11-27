#include "../main.h"
#include "../game/game.h"
#include "netgame.h"
#include "../game/RW/RenderWare.h"
#include "../gui/gui.h"
#include "playerbubblepool.h"

extern CNetGame *pNetGame;
//extern CChatWindow *pChatWindow;
//extern CGUI *pGUI;

/* Quebra de linha do balao de chat.

   A versao antiga estourava o buffer: ela copiava o resto da string para uma
   pilha local de 512 bytes a partir de um offset que podia passar do fim,
   escrevia '\n' em szText[w+1] mesmo com w >= strlen e ainda anexava o texto
   em szText + strlen(szText) (crescendo sem limite). Com texto grande isso
   corrompia o heap e o crash aparecia depois, em outro lugar (PC/lr lixo).
   Agora so troca caracteres ja existentes por '\n': nunca escreve fora do
   buffer e nunca aumenta o texto. Retorna a quantidade de linhas. */
int WrapText(char* szText, int iCapacity, int iWidth)
{
    if (!szText || iCapacity < 2 || iWidth < 2) return 1;

    int len = 0;
    while (len < iCapacity - 1 && szText[len]) len++;
    szText[len] = '\0';

    int lines = 1;
    int lineStart = 0;
    int i = 0;

    while (i < len)
    {
        if (szText[i] == '\n')
        {
            lines++;
            lineStart = i + 1;
            i++;
            continue;
        }

        if (i - lineStart < iWidth)
        {
            i++;
            continue;
        }

        // Passou da largura: quebra no ultimo espaco da linha, ou na forca.
        int brk = i;
        while (brk > lineStart && szText[brk] != ' ') brk--;

        if (brk > lineStart && szText[brk] == ' ')
            szText[brk] = '\n';
        else
            szText[i] = '\n';

        lineStart = (brk > lineStart ? brk : i) + 1;
        lines++;
        i = lineStart;
    }

    return lines;
}

CPlayerBubblePool::CPlayerBubblePool()
{
    for(int i = 0; i < MAX_PLAYERS; i++)
    {
        m_pPlayerBubble[i] = 0;
        m_bSlotState[i] = false;
    }
}

CPlayerBubblePool::~CPlayerBubblePool()
{
    for(int i = 0; i < MAX_PLAYERS; i++)
    {
        if(m_pPlayerBubble[i] && m_bSlotState[i])
        {
            delete m_pPlayerBubble[i];
        }

        m_pPlayerBubble[i] = 0;
        m_bSlotState[i] = false;
    }
}

PlayerBubbleStruct *CPlayerBubblePool::New(PLAYERID playerId, const char *text, uint32_t color, float distance, uint32_t time)
{
    // playerId vem do servidor: sem esse teste era escrita fora do array.
    if (playerId >= MAX_PLAYERS || !text) return nullptr;

    if(m_bSlotState[playerId] && m_pPlayerBubble[playerId])
    {
        delete m_pPlayerBubble[playerId];
        m_pPlayerBubble[playerId] = 0;
        m_bSlotState[playerId] = false;
    }

    PlayerBubbleStruct* playerBubbleStruct = new PlayerBubbleStruct;
    //Utils::cpToUtf8(m_pPlayerBubble[playerId]->szText, text);
    std::string GText = Encoding::cp2utf(text);

    // strcpy estourava szText[256] com mensagem longa (heap corrompido). Corta
    // no limite sem partir um caractere UTF-8 no meio.
    size_t iCopy = GText.size();
    if (iCopy > sizeof(playerBubbleStruct->szText) - 1)
        iCopy = sizeof(playerBubbleStruct->szText) - 1;
    while (iCopy > 0 && ((unsigned char)GText[iCopy] & 0xC0) == 0x80) iCopy--;
    memcpy(playerBubbleStruct->szText, GText.data(), iCopy);
    playerBubbleStruct->szText[iCopy] = '\0';

    playerBubbleStruct->fDistance = distance;
    playerBubbleStruct->uiExpireTime = GetTickCount() + time;
    playerBubbleStruct->dwLineCount = WrapText(playerBubbleStruct->szText, (int)sizeof(playerBubbleStruct->szText), 36) - 1;
    playerBubbleStruct->uiColor = color;

    m_pPlayerBubble[playerId] = playerBubbleStruct;
    m_bSlotState[playerId] = true;

    return m_pPlayerBubble[playerId];
}

void CPlayerBubblePool::Delete(PLAYERID playerId)
{
    if (playerId >= MAX_PLAYERS) return;

    if(m_bSlotState[playerId] && m_pPlayerBubble[playerId])
    {
        delete m_pPlayerBubble[playerId];
        m_bSlotState[playerId] = false;
    }
}

void CPlayerBubblePool::Render(ImGuiRenderer* renderer)
{
    this->Draw(renderer);
}

void CPlayerBubblePool::Draw(ImGuiRenderer* renderer)
{
    for(PLAYERID i = 0; i < MAX_PLAYERS; i++)
    {
        if(m_bSlotState[i] && m_pPlayerBubble[i])
        {   
            if(GetTickCount() >= m_pPlayerBubble[i]->uiExpireTime)
            {
                Delete(i);
                continue;
            }

            if(pNetGame) {

                CPlayerPool *pPlayerPool = pNetGame->GetPlayerPool();
                if (pPlayerPool->GetSlotState(i)) {
                    CRemotePlayer *pRemotePlayer = pPlayerPool->GetAt(i);
                    if (pRemotePlayer && pRemotePlayer->IsActive()) {
                        CPlayerPed *pPlayerPed = pRemotePlayer->GetPlayerPed();

                        if (pPlayerPed && pPlayerPed->m_pPed && pPlayerPed->m_pPed->IsAdded() &&
                            pPlayerPed->m_pPed->m_pRwClump) {
                            if (pPlayerPed->m_pPed->GetDistanceFromCamera() <=
                                m_pPlayerBubble[i]->fDistance) {
                                CVector VecPos;
                                VecPos.x = 0.0f;
                                VecPos.y = 0.0f;
                                VecPos.z = 0.0f;

                                pPlayerPed->GetBonePosition(8, &VecPos);

                                // Bone nao escrito: usa a posicao do ped em vez
                                // de (0,0,0) (origem do mapa).
                                if (VecPos.x == 0.0f && VecPos.y == 0.0f && VecPos.z == 0.0f)
                                {
                                    VecPos = pPlayerPed->m_pPed->GetPosition();
                                    VecPos.z += 0.9f;
                                }

                                CVector TagPos;
                                TagPos.x = VecPos.x;
                                TagPos.y = VecPos.y;
                                TagPos.z = VecPos.z;

                                float fTextHeight =
                                        ((m_pPlayerBubble[i]->dwLineCount * 0.0125f) + 0.065f) +
                                        (m_pPlayerBubble[i]->dwLineCount * 0.0125f);
                                TagPos.z +=
                                        pPlayerPed->m_pPed->GetDistanceFromCamera() * fTextHeight + 0.2f;

                                CVector Out;

                                // CSprite::CalcScreenCoors
                                //(( void (*)(CVector*, CVector*, float*, float*, bool, bool))(g_GTASAAdr+0x54EEC0+1))(&TagPos, &Out, 0, 0, 0, 0);
                                // CSprite::CalcScreenCoors
                                ((void (*)(CVector *, CVector *, float *, float *, bool, bool)) (
                                        g_libGTASA + 0x5F449C))(&TagPos, &Out, 0, 0, 0, 0);

                                if (Out.z < 1.0f) {
                                    // continue: um balao fora da tela nao pode
                                    // cancelar o desenho dos outros jogadores.
                                    continue;
                                }

                                //ImVec2 pos = ImVec2(Out.x, Out.y - (pGUI->GetFontSize() * 1.67));
                                ImVec2 pos = ImVec2(Out.x, Out.y - (UISettings::fontSize() * 1.0));
                                //pos.x -= ImGui::CalcTextSize(m_pPlayerBubble[i]->szText).x / 2;
                                pos.x -= renderer->calculateTextSize(m_pPlayerBubble[i]->szText,
                                                                     UISettings::fontSize() / 2).x /
                                         2;
                                //UIUtils::TextWithColors(pos, __builtin_bswap32(m_pPlayerBubble[i]->uiColor | (0x000000FF)), m_pPlayerBubble[i]->szText, pGUI->GetFontSize());
                                //renderer->drawText(pos, __builtin_bswap32(m_pPlayerBubble[i]->uiColor | (0x000000FF)), m_pPlayerBubble[i]->szText, true, UISettings::fontSize() / 2);
                                renderer->drawText(pos, __builtin_bswap32(
                                                           m_pPlayerBubble[i]->uiColor | (0x000000FF)),
                                                   std::string(m_pPlayerBubble[i]->szText), true,
                                                   UISettings::fontSize() / 2);
                            }
                        }
                    }
                }
            }
        }
    }
}