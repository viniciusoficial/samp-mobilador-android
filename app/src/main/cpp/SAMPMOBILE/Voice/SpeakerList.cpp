#include "../Client.h"
#include "../UI/UI.h"
#include "../Game/Common.h"
#include "../Game/Util.h"
#include "../Game/Game.h"
#include "../Game/PlayerPed.h"
#include "../Net/NetGame.h"
#include "../Net/RemotePlayer.h"
#include <encoding.h>

#include "SpeakerList.h"

#include "PluginConfig.h"
#include "../Game/AudioStream.h"


bool SpeakerList::Init() noexcept
{
	if (SpeakerList::initStatus) {
		return false;
	}

	try {
		SpeakerList::tSpeakerIcon = LoadTextureFromTxd("samp", "voice_speaker");
	}
	catch (const std::exception& exception) {
		LogVoice("[sv:err:speakerlist:init] : failed to create speaker icon");
		SpeakerList::tSpeakerIcon = nullptr;
		return false;
	}

	if (!PluginConfig::IsSpeakerLoaded()) {
		PluginConfig::SetSpeakerLoaded(true);
	}

	SpeakerList::initStatus = true;

	return true;
}

void SpeakerList::Free() noexcept
{
	if (!SpeakerList::initStatus) {
		return;
	}

	SpeakerList::tSpeakerIcon = nullptr;

	SpeakerList::initStatus = false;
}

void SpeakerList::Show() noexcept
{
	SpeakerList::showStatus = true;
}

bool SpeakerList::IsShowed() noexcept
{
	return SpeakerList::showStatus;
}

void SpeakerList::Hide() noexcept
{
	SpeakerList::showStatus = false;
}

void SpeakerList::Render()
{
	if (!SpeakerList::initStatus || !SpeakerList::IsShowed()) {
		return;
	}

	if (!pNetGame || !pUI || !pUI->renderer()) return;

	CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
	if (!pPlayerPool) return;

	int curTextLine = 0;
	char szText[128], szText2[128];
	ImVec2 textPos = ImVec2(0, 530.0f);

	for (PLAYERID playerId{0}; playerId < MAX_PLAYERS; ++playerId) {
		CRemotePlayer* pPlayer = pPlayerPool->GetAt(playerId);
		if (pPlayer && pPlayer->IsActive()) {
			CPlayerPed* pPlayerPed = pPlayer->GetPlayerPed();
			if (pPlayerPed) {
				const auto playerName = pPlayerPool->GetPlayerName(playerId);
				if (playerName) {
					if (!SpeakerList::playerStreams[playerId].empty()) {
						for (const auto& playerStream : SpeakerList::playerStreams[playerId]) {
							if (playerStream.second.GetType() == StreamType::LocalStreamAtPlayer) {
								sa::CVector VecPos{0.0f, 0.0f, 0.0f};

								if (!pPlayerPed->m_ped || !pPlayerPed->IsAdded()) continue;
								pPlayerPed->m_ped->GetBonePosition(&VecPos, 8, false);

								if (pPlayerPed->GetDistanceFromLocalPlayerPed() < 60.0f) {
									SpeakerList::Draw(&VecPos, pPlayerPed->GetDistanceFromCamera());
								}
							}
						}

						if (curTextLine < 4 && playerName && strlen(playerName)) {
							if (SpeakerList::tSpeakerIcon && SpeakerList::tSpeakerIcon->raster) {
								ImVec2 a = ImVec2(textPos.x, textPos.y);
								ImVec2 b = ImVec2(textPos.x + pUI->GetFontSize() / 2, textPos.y + pUI->GetFontSize() / 2);
								ImGui::GetBackgroundDrawList()->AddImage((ImTextureID) SpeakerList::tSpeakerIcon->raster, a, b);
							}

							ImGui::PushFontOutline(0xFF000000, 2);
							textPos.x = pUI->GetFontSize();
							snprintf(szText, sizeof(szText), "%s (ID: %d) ", Encoding::cp2utf(playerName).c_str(), playerId);
							pUI->renderer()->drawText(textPos, 0xFFFFFFFF, szText, true, pUI->GetFontSize() / 2);
							ImGui::PopFontOutline();

							for (const auto& streamInfo : SpeakerList::playerStreams[playerId]) {
								if (streamInfo.second.GetColor() == 0) {
									continue;
								}

								ImGui::PushFontOutline(0xFF000000, 2);
								textPos.x = ImGui::CalcTextSize(szText).x;
								snprintf(szText2, sizeof(szText2), "[%s]", streamInfo.second.GetName().c_str());
								pUI->renderer()->drawText(textPos, streamInfo.second.GetColor(), szText2, true, pUI->GetFontSize() / 2);
								ImGui::PopFontOutline();
							}

							textPos.x -= ImGui::CalcTextSize(szText).x;
							textPos.y += pUI->GetFontSize();

							curTextLine++;
						}
					}
				}
			}
		}
	}
}

void SpeakerList::Draw(sa::CVector* vec, float fDist)
{
	if (!vec || !pUI || !SpeakerList::tSpeakerIcon || !SpeakerList::tSpeakerIcon->raster) return;

	sa::CVector TagPos;

	TagPos.x = vec->x;
	TagPos.y = vec->y;
	TagPos.z = vec->z;
	TagPos.z += 0.25f + (fDist * 0.0475f);

	sa::CVector Out;
	CalcScreenCoors(&TagPos, &Out, 0, 0, false, false);
	if (Out.z < 1.0f) {
		return;
	}

	ImVec2 pos = ImVec2(Out.x, Out.y);
	pos.x -= PluginConfig::kDefValSpeakerIconSize / 2;
	pos.y -= pUI->GetFontSize();

	ImVec2 a = ImVec2(pos.x, pos.y);
	ImVec2 b = ImVec2(pos.x + PluginConfig::kDefValSpeakerIconSize, pos.y + PluginConfig::kDefValSpeakerIconSize);
	ImGui::GetBackgroundDrawList()->AddImage((ImTextureID) SpeakerList::tSpeakerIcon->raster, a, b);
}

void SpeakerList::OnSpeakerPlay(const Stream& stream, const uint16_t speaker) noexcept
{
	if (speaker >= MAX_PLAYERS) return;
	SpeakerList::playerStreams[speaker][(Stream*) (&stream)] = stream.GetInfo();
}

void SpeakerList::OnSpeakerStop(const Stream& stream, const uint16_t speaker) noexcept
{
	if (speaker >= MAX_PLAYERS) return;
	SpeakerList::playerStreams[speaker].erase((Stream*) (&stream));
}

void SpeakerList::OnStreamDelete(const Stream* stream) noexcept
{
	if (!stream) return;
	for (auto& slot : SpeakerList::playerStreams) {
		slot.erase(const_cast<Stream*>(stream));
	}
}

void SpeakerList::ClearAll() noexcept
{
	for (auto& slot : SpeakerList::playerStreams) {
		slot.clear();
	}
}

std::array<std::unordered_map<Stream*, StreamInfo>, MAX_PLAYERS> SpeakerList::playerStreams;

bool SpeakerList::initStatus{false};
bool SpeakerList::showStatus{false};

RwTexture* SpeakerList::tSpeakerIcon{nullptr};