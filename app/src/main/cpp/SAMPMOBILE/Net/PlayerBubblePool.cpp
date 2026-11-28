#include "PlayerBubblePool.h"
#include "NetGame.h"
#include "RemotePlayer.h"
#include "../Client.h"
#include "../Game/Game.h"
#include "../Game/PlayerPed.h"
#include "../Game/Util.h"
#include "../UI/UI.h"
#include "../UI/UISettings.h"
#include <encoding.h>


CPlayerBubblePool::CPlayerBubblePool()
{
	for (int i = 0; i < MAX_PLAYERS; i++) {
		m_pPlayerBubble[i] = 0;
		m_bSlotState[i] = false;
	}
}

CPlayerBubblePool::~CPlayerBubblePool()
{
	for (int i = 0; i < MAX_PLAYERS; i++) {
		if (m_pPlayerBubble[i] && m_bSlotState[i]) {
			delete m_pPlayerBubble[i];
		}

		m_pPlayerBubble[i] = 0;
		m_bSlotState[i] = false;
	}
}

PlayerBubbleStruct* CPlayerBubblePool::New(PLAYERID playerId, const char* text, uint32_t color, float distance, uint32_t time)
{
	if (playerId >= MAX_PLAYERS || !text) return nullptr;

	if (m_bSlotState[playerId] && m_pPlayerBubble[playerId]) {
		delete m_pPlayerBubble[playerId];
		m_pPlayerBubble[playerId] = nullptr;
		m_bSlotState[playerId] = false;
	}

	m_pPlayerBubble[playerId] = new PlayerBubbleStruct{};
	std::string GText = Encoding::cp2utf(text);
	const size_t maxBytes = sizeof(m_pPlayerBubble[playerId]->szText) - 1;
	size_t copyLen = std::min(GText.size(), maxBytes);
	// Never split a multi-byte UTF-8 sequence when truncating to szText[256].
	if (copyLen < GText.size()) {
		while (copyLen > 0 && (static_cast<unsigned char>(GText[copyLen]) & 0xC0) == 0x80) {
			--copyLen;
		}
	}
	memcpy(m_pPlayerBubble[playerId]->szText, GText.data(), copyLen);
	m_pPlayerBubble[playerId]->szText[copyLen] = '\0';

	m_pPlayerBubble[playerId]->fDistance = distance;
	m_pPlayerBubble[playerId]->uiExpireTime = GetTickCount() + time;
	m_pPlayerBubble[playerId]->uiColor = color;
	m_bSlotState[playerId] = true;

	return m_pPlayerBubble[playerId];
}

void CPlayerBubblePool::Delete(PLAYERID playerId)
{
	if (playerId >= MAX_PLAYERS) return;

	if (m_pPlayerBubble[playerId]) {
		delete m_pPlayerBubble[playerId];
		m_pPlayerBubble[playerId] = nullptr;
	}
	m_bSlotState[playerId] = false;
}

void CPlayerBubblePool::Render(ImGuiRenderer* renderer)
{
	if (!renderer || !pNetGame) return;
	CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
	if (!pPlayerPool) return;

	for (PLAYERID i = 0; i < MAX_PLAYERS; i++) {
		if (m_bSlotState[i] && m_pPlayerBubble[i]) {
			if (!strlen(m_pPlayerBubble[i]->szText)) {
				continue;
			}

			if (static_cast<int32_t>(GetTickCount() - m_pPlayerBubble[i]->uiExpireTime) >= 0) {
				Delete(i);
				continue;
			}

			if (pPlayerPool->GetSlotState(i)) {
				CRemotePlayer* pRemotePlayer = pPlayerPool->GetAt(i);
				if (pRemotePlayer && pRemotePlayer->IsActive()) {
					CPlayerPed* pPlayerPed = pRemotePlayer->GetPlayerPed();
					if (pPlayerPed && pPlayerPed->m_ped && pPlayerPed->IsAdded()) {
						if (pPlayerPed->GetDistanceFromCamera() <= m_pPlayerBubble[i]->fDistance) {
							sa::CVector VecPos{0.0f, 0.0f, 0.0f};
							pPlayerPed->m_ped->GetBonePosition(&VecPos, 8, false);

							RwV3d TagPos;
							TagPos.x = VecPos.x;
							TagPos.y = VecPos.y;
							TagPos.z = VecPos.z + 0.25f + (pPlayerPed->GetDistanceFromCamera() * 0.0475f);

							RwV3d Out;
							CalcScreenCoors(&TagPos, &Out, 0.0f, 0.0f, false, false);

							if (Out.z < 1.0f) {
								continue;
							}

							const float bubbleFontSize = UISettings::fontSize() / 2.0f;
							const std::string text(m_pPlayerBubble[i]->szText);
							std::vector<std::string> lines;
							{
								auto isHexTagAt = [](const std::string& s, size_t idx) -> bool {
									if (idx + 7 >= s.size() || s[idx] != '{' || s[idx + 7] != '}') return false;
									for (size_t k = 1; k <= 6; ++k) {
										const unsigned char ch = static_cast<unsigned char>(s[idx + k]);
										if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F'))) {
											return false;
										}
									}
									return true;
								};
								constexpr size_t kMaxColsPerLine = 36;
								size_t posIdx = 0;
								std::string activeColor;
								while (posIdx < text.size()) {
									size_t cur = posIdx;
									size_t cols = 0;
									size_t lastSpace = std::string::npos;
									std::string lineEndColor = activeColor;
									std::string spaceColor = activeColor;
									while (cur < text.size() && cols < kMaxColsPerLine) {
										if (text[cur] == '\n') {
											break;
										}
										if (isHexTagAt(text, cur)) {
											lineEndColor = text.substr(cur, 8);
											cur += 8;
											continue;
										}
										if (text[cur] == ' ') {
											lastSpace = cur;
											spaceColor = lineEndColor;
										}
										const unsigned char c = static_cast<unsigned char>(text[cur]);
										size_t step = 1;
										if ((c & 0xE0) == 0xC0) step = 2;
										else if ((c & 0xF0) == 0xE0) step = 3;
										else if ((c & 0xF8) == 0xF0) step = 4;
										if (cur + step > text.size()) {
											step = 1; // malformed trailing UTF-8 byte: always advance
										}
										cur += step;
										cols += (step > 1) ? 2 : 1;
									}
									if (cur < text.size() && text[cur] == '\n') {
										lines.push_back(activeColor + text.substr(posIdx, cur - posIdx));
										activeColor = lineEndColor;
										posIdx = cur + 1;
									}
									else if (cur < text.size() && lastSpace != std::string::npos && lastSpace > posIdx) {
										lines.push_back(activeColor + text.substr(posIdx, lastSpace - posIdx));
										activeColor = spaceColor;
										posIdx = lastSpace + 1;
									}
									else {
										if (cur == posIdx) {
											cur = posIdx + 1; // guaranteed forward progress
										}
										lines.push_back(activeColor + text.substr(posIdx, cur - posIdx));
										activeColor = lineEndColor;
										posIdx = cur;
									}
								}
							}
							if (lines.empty()) {
								lines.push_back(text);
							}

							const float lineHeight = bubbleFontSize * 1.15f;
							float curY = Out.y - (UISettings::fontSize() * 1.0f) - static_cast<float>(lines.size() - 1) * lineHeight;
							const uint32_t abgr = __builtin_bswap32(m_pPlayerBubble[i]->uiColor | 0x000000FF);
							for (const auto& line : lines) {
								ImVec2 pos = ImVec2(Out.x - renderer->calculateTextSize(line, bubbleFontSize).x * 0.5f, curY);
								renderer->drawText(pos, abgr, line, true, bubbleFontSize);
								curY += lineHeight;
							}
						}
					}
				}
			}
		}
	}
}