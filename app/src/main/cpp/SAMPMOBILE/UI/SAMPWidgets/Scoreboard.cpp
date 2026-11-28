#include "Scoreboard.h"
#include "../UI.h"
#include "../UISettings.h"
#include "../../Client.h"
#include "../../Game/Game.h"
#include "../../Game/Util.h"
#include "../../Net/NetGame.h"
#include "../../Net/RemotePlayer.h"
#include "../../Net/Proto/RpcOutbound.h"
#include <RakNet/RakClientInterface.h>
#include <RakNet/SAMP/SAMPRPC.h>
#include <encoding.h>
#include <sstream>
#include "../../L10n.h"


struct _PLAYER_SCORE_INFO {
	uint32_t dwID;
	char szName[MAX_PLAYER_NAME + 1];
	int iScore;
	int32_t dwPing;
	uint32_t dwColor;
	int iState;
};

void SwapPlayerInfo(PLAYER_SCORE_INFO* psi1, PLAYER_SCORE_INFO* psi2)
{
	PLAYER_SCORE_INFO plrinf;
	memcpy(&plrinf, psi1, sizeof(PLAYER_SCORE_INFO));
	memcpy(psi1, psi2, sizeof(PLAYER_SCORE_INFO));
	memcpy(psi2, &plrinf, sizeof(PLAYER_SCORE_INFO));
}

Scoreboard::Scoreboard()
		: m_players(nullptr), m_playerCount(0), m_offset(0), m_sorted(false),
			m_tickProcessingUpdate(0), m_lastSelectedItem(-1), m_selectedItem(-1)
{

}

Scoreboard::~Scoreboard()
{
	free(m_players);
}

bool isAnyScrollBarActive()
{
	ImGuiWindow* window = ImGui::GetCurrentWindow();
	ImGuiID active_id = ImGui::GetActiveID();
	return (active_id && (active_id == ImGui::GetScrollbarID(window, ImGuiAxis_X) || active_id == ImGui::GetScrollbarID(window, ImGuiAxis_Y)));
}

void scrollWhenDragging(bool x, bool y)
{
	ImGuiIO& io = ImGui::GetIO();
	ImVec2 windowPos = ImGui::GetWindowPos();
	ImVec2 windowSize = ImGui::GetWindowSize();

	if (!isAnyScrollBarActive() &&
			io.MousePos.x >= windowPos.x && io.MousePos.x <= windowPos.x + windowSize.x &&
			io.MousePos.y >= windowPos.y && io.MousePos.y <= windowPos.y + windowSize.y) {
		ImVec2 mouse_delta = io.MouseDelta;
		if (x && mouse_delta.x != 0.0f && mouse_delta.x <= 100.0f) ImGui::SetScrollX(ImGui::GetScrollX() + -mouse_delta.x);
		if (y && mouse_delta.y != 0.0f && mouse_delta.y <= 100.0f) ImGui::SetScrollY(ImGui::GetScrollY() + -mouse_delta.y);
	}
}

void Scoreboard::draw(ImGuiRenderer* renderer)
{
	processUpdating();

	if (!m_players) return;

	if (!visible()) {
		if (m_players) {
			memset(m_players, 0, m_playerCount * sizeof(PLAYER_SCORE_INFO));
			free(m_players);
		}

		m_players = 0;
		return;
	}

	PLAYERID endplayer = m_playerCount;
	char szPlayerCount[30], szPlayerId[30], szScore[30], szPing[30];

	sprintf(szPlayerCount, "Players: %d", m_playerCount);
	uint16_t wPlayerTextSize = ImGui::CalcTextSize(szPlayerCount).x;

	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.13f, 0.13f, 0.13f, 0.70f));
	ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.75f, 0.75f, 0.75f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.80f));

	ImGui::Begin("Scoreboard", nullptr,
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);

	ImGui::Columns(2, "ScoreboardInfo", false);
	ImGui::Text("%s", pNetGame ? Encoding::cp2utf(pNetGame->session().hostName).c_str() : "");
	ImGui::SetColumnWidth(-1, size().x - (wPlayerTextSize + (ImGui::GetStyle().ItemSpacing.x * 2)));
	ImGui::NextColumn();
	ImGui::Text("%s", szPlayerCount);
	ImGui::SetColumnWidth(-1, size().x / 100 + wPlayerTextSize + ImGui::GetStyle().ItemSpacing.x);
	ImGui::Columns();

	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5);

	ImGui::Columns(4, "ScoreboardHeader", false);
	ImGui::TextColored(ImVec4(0.57f, 0.65f, 0.78f, 1.1f), L10N(" id", " ID"));
	ImGui::SetColumnWidth(-1, size().x * 0.1);
	ImGui::NextColumn();
	ImGui::TextColored(ImVec4(0.57f, 0.65f, 0.78f, 1.1f), L10N(" name", " 名称"));
	ImGui::SetColumnWidth(-1, size().x * 0.45);
	ImGui::NextColumn();
	ImGui::TextColored(ImVec4(0.57f, 0.65f, 0.78f, 1.1f), L10N(" score", " 积分"));
	ImGui::SetColumnWidth(-1, size().x * 0.2);
	ImGui::NextColumn();
	ImGui::TextColored(ImVec4(0.57f, 0.65f, 0.78f, 1.1f), L10N(" ping", " 延迟"));
	ImGui::SetColumnWidth(-1, size().x * 0.25);
	ImGui::Columns();

	ImGui::BeginChild("ScoreboardChild", ImVec2(-1, (size().y - (UISettings::fontSize() / 2) * 3) - ImGui::GetStyle().ItemSpacing.y), true,
			ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_NoSavedSettings);

	ImGui::Columns(4, "ScoreboardBody", false);
	for (uint32_t line = m_offset; line < endplayer; line++) {
		bool closeAfterClick = false;
		const int32_t rowIdx = static_cast<int32_t>(line);
		ImGui::PushID(rowIdx);

		char itemid[24];
		snprintf(itemid, sizeof(itemid), "##row_%u", line);
		const bool is_selected = (m_selectedItem == rowIdx);

		if (ImGui::Selectable(itemid, is_selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0.0f, (UISettings::fontSize() / 2) + ImGui::GetStyle().ItemSpacing.y))) {
			const bool isDoubleActivate = ImGui::IsMouseDoubleClicked(0)
				|| (m_lastSelectedItem >= 0 && m_lastSelectedItem == rowIdx);
			if (isDoubleActivate) {
				if (pNetGame && pNetGame->GetRakClient()) {
					RakNet::BitStream bsSend;
					proto::Writer writer(bsSend);
					proto::ClickPlayerReq req{};
					req.playerId = static_cast<PLAYERID>(m_players[line].dwID);
					req.source = 0; // 0 from scoreboard
					req.serialize(writer, proto::ProtoVersion::V037);
					pNetGame->GetRakClient()->RPC(&RPC_ClickPlayer, &bsSend, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false, UNASSIGNED_NETWORK_ID, nullptr);
				}

				m_lastSelectedItem = -1;
				m_selectedItem = -1;
				if (visible()) {
					setVisible(false);
					closeAfterClick = true;
				}
			}
			else {
				m_selectedItem = rowIdx;
				m_lastSelectedItem = rowIdx;
			}
		}

		if (ImGui::IsItemHovered()) {
			m_selectedItem = rowIdx;
		}

		if (is_selected) {
			ImGui::SetItemDefaultFocus();
		}

		ImGui::SameLine();
		ImGui::PopID();

		if (closeAfterClick) {
			break;
		}

		sprintf(szPlayerId, "%d", m_players[line].dwID);
		sprintf(szScore, "%d", m_players[line].iScore);
		sprintf(szPing, "%d", m_players[line].dwPing);

		ImGui::TextColored(UI::fixcolor(m_players[line].dwColor), "%s", szPlayerId);
		ImGui::SetColumnWidth(-1, size().x / 100 * 10);
		ImGui::NextColumn();

		ImGui::TextColored(UI::fixcolor(m_players[line].dwColor), "%s", Encoding::cp2utf(m_players[line].szName).c_str());
		ImGui::SetColumnWidth(-1, size().x / 100 * 45);
		ImGui::NextColumn();

		ImGui::TextColored(UI::fixcolor(m_players[line].dwColor), "%s", szScore);
		ImGui::SetColumnWidth(-1, size().x / 100 * 20);
		ImGui::NextColumn();

		ImGui::TextColored(UI::fixcolor(m_players[line].dwColor), "%s", szPing);
		ImGui::SetColumnWidth(-1, size().x / 100 * 25);
		ImGui::NextColumn();
	}

	ImGui::EndColumns();

	scrollWhenDragging(false, true);

	ImGui::EndChild();

	ImGui::SetWindowSize(ImVec2(size().x, size().y));
	ImGui::SetWindowPos(position());
	ImGui::End();

	ImGui::PopStyleColor(3);
}

void Scoreboard::processUpdating()
{
	if (!pNetGame) return;
	if ((GetTickCount() - m_tickProcessingUpdate) <= 2000) return;
	m_tickProcessingUpdate = GetTickCount();

	// Get player list
	pNetGame->UpdatePlayerScoresAndPings();

	CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
	if (!pPlayerPool || !pPlayerPool->GetLocalPlayer()) return;

	PLAYERID playercount = pPlayerPool->GetTotalPlayers();

	const int maxOffset = static_cast<int>(playercount) - 20;
	if (m_offset > maxOffset) m_offset = maxOffset;
	if (m_offset < 0) m_offset = 0;

	PLAYER_SCORE_INFO* players = (PLAYER_SCORE_INFO*) malloc(playercount * sizeof(PLAYER_SCORE_INFO));
	if (!players) return;
	memset(players, 0, playercount * sizeof(PLAYER_SCORE_INFO));

	PLAYERID i = 0;
	players[i].dwID = pPlayerPool->GetLocalPlayerID();
	strncpy(players[i].szName, pPlayerPool->GetLocalPlayerName(), MAX_PLAYER_NAME);
	players[i].szName[MAX_PLAYER_NAME] = 0;
	players[i].iScore = pPlayerPool->GetLocalPlayerScore();
	players[i].dwPing = pPlayerPool->GetLocalPlayerPing();
	players[i].dwColor = pPlayerPool->GetLocalPlayer()->GetPlayerColorAsRGBA();
	i++;

	for (PLAYERID x = 0; x < MAX_PLAYERS && i < playercount; x++) {
		if (!pPlayerPool->GetSlotState(x)) {
			continue;
		}

		CRemotePlayer* pPlayer = pPlayerPool->GetAt(x);
		if (!pPlayer || pPlayer->IsNPC()) {
			continue;
		}

		players[i].dwID = x;
		char* szName = pPlayerPool->GetPlayerName(x);
		strncpy(players[i].szName, szName ? szName : "", MAX_PLAYER_NAME);
		players[i].szName[MAX_PLAYER_NAME] = 0;
		players[i].iScore = pPlayerPool->GetPlayerScore(x);
		players[i].dwPing = pPlayerPool->GetPlayerPing(x);
		players[i].dwColor = pPlayer->GetPlayerColor();
		players[i].iState = (int) pPlayer->GetState();
		i++;
	}

	if (m_sorted) {
		for (PLAYERID a = 0; a + 1 < i; a++) {
			for (PLAYERID j = 0; j + 1 < i - a; j++) {
				if (players[j + 1].iScore > players[j].iScore) {
					SwapPlayerInfo(&players[j], &players[j + 1]);
				}
			}
		}
	}

	free(m_players);
	m_players = players;
	m_playerCount = i;
}
