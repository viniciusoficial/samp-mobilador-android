#include "PlayerTags.h"
#include "DeathWindow.h"
#include "../UI.h"
#include "../UISettings.h"
#include "../../Client.h"
#include "../../Game/Game.h"
#include "../../Game/PlayerPed.h"
#include "../../Game/Scripting.h"
#include "../../Game/Vehicle.h"
#include "../../Game/Util.h"
#include "../../Net/NetGame.h"
#include "../../Net/RemotePlayer.h"
#include "../../Settings.h"
#include <encoding.h>
#include <SA.h>


PlayerTags::PlayerTags()
{
	spdlog::info("Loading AFK icon..");
	m_afkTexture = LoadTextureFromTxd("samp", "afk_icon");
}

void PlayerTags::Render(ImGuiRenderer* renderer)
{
	sa::CVector vecPos;
	RwMatrix matLocal, matPlayer;
	int hitEntity = 0;
	char szNickBuf[64];

	if (pNetGame && pNetGame->m_netSet->showNameTags) {
		CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
		CPlayerPed* pLocalPed = pGame->FindPlayerPed();
		if (!pPlayerPool || !pLocalPed || !pLocalPed->m_ped) return;

		matLocal = pLocalPed->m_ped->GetMatrix().ToRwMatrix();

		const PLAYERID maxPlayerId = static_cast<PLAYERID>(
			(pPlayerPool->m_iLastPlayerID >= 0 && pPlayerPool->m_iLastPlayerID < MAX_PLAYERS)
				? pPlayerPool->m_iLastPlayerID
				: (MAX_PLAYERS - 1));
		const float screenW = pUI ? pUI->size().x : 1920.0f;
		const float screenH = pUI ? pUI->size().y : 1080.0f;

		for (PLAYERID playerId = 0; playerId <= maxPlayerId; playerId++) {
			if (pPlayerPool->GetSlotState(playerId)) {
				CRemotePlayer* pRemotePlayer = pPlayerPool->GetAt(playerId);

				if (pRemotePlayer && pRemotePlayer->IsActive() && pRemotePlayer->m_bShowNameTag && !pRemotePlayer->IsNPC()) {
					CPlayerPed* pPlayerPed = pRemotePlayer->GetPlayerPed();
					if (!pPlayerPed) continue;

					const float dist = pPlayerPed->GetDistanceFromCamera();
					if (dist <= pNetGame->m_netSet->nameTagDrawDistance) {
						if (pRemotePlayer->GetState() == PLAYER_STATE_DRIVER && pRemotePlayer->m_pCurrentVehicle && pRemotePlayer->m_pCurrentVehicle->m_pVehicle && pRemotePlayer->m_pCurrentVehicle->IsRCVehicle()) {
							matPlayer = pRemotePlayer->m_pCurrentVehicle->m_pVehicle->GetMatrix().ToRwMatrix();
							vecPos.x = matPlayer.pos.x;
							vecPos.y = matPlayer.pos.y;
							vecPos.z = matPlayer.pos.z;
						}
						else {
							if (!pPlayerPed->m_ped || !pPlayerPed->IsAdded()) continue;
							vecPos.x = 0.0f;
							vecPos.y = 0.0f;
							vecPos.z = 0.0f;
							pPlayerPed->m_ped->GetBonePosition(&vecPos, 8, false);
						}

						// Frustum/screen cull before running expensive 3D get_line_of_sight raycast
						RwV3d vecTagProbe = vecPos;
						vecTagProbe.z += 0.3f + (dist * 0.0475f);
						RwV3d vecScreen{};
						CalcScreenCoors(&vecTagProbe, &vecScreen, 0.0f, 0.0f, false, false);
						if (vecScreen.z < 1.0f
								|| vecScreen.x < -250.0f || vecScreen.x > screenW + 250.0f
								|| vecScreen.y < -150.0f || vecScreen.y > screenH + 150.0f) {
							continue;
						}

						CAMERA_AIM* pCam = GameGetInternalAim();

						if (pNetGame->m_netSet->nameTagLOS) {
							if (!pCam) continue;
							hitEntity = ScriptCommand(&get_line_of_sight, vecPos.x, vecPos.y, vecPos.z, pCam->pos1x, pCam->pos1y, pCam->pos1z, 1, 0, 0, 1, 0);
						}

						if (!pNetGame->m_netSet->nameTagLOS || hitEntity) {
							char* szPlayerName = pPlayerPool->GetPlayerName(playerId);
							snprintf(szNickBuf, sizeof(szNickBuf), "%s (%d)", Encoding::cp2utf(szPlayerName ? szPlayerName : "").c_str(), playerId);
							drawTag(renderer, &vecPos, szNickBuf, pRemotePlayer->GetPlayerColor(),
									dist, pRemotePlayer->m_fReportedHealth,
									pRemotePlayer->m_fReportedArmour, pRemotePlayer->m_bIsAFK,
									pPlayerPed->GetCurrentWeapon());
						}
					}
				}
			}
		}
	}
}

void PlayerTags::drawTag(ImGuiRenderer* renderer, RwV3d* vec, const char* szNick, uint32_t dwColor, float fDist, float fHealth, float fArmour, bool bAfk, uint8_t byteWeapon)
{
	RwV3d vecTagPos = *vec;
	vecTagPos.z += 0.3f + (fDist * 0.0475f);

	RwV3d vecOut;
	CalcScreenCoors(&vecTagPos, &vecOut, 0.0f, 0.0f, false, false);
	if (vecOut.z < 1.0f) return;

	const float scale = Settings::nametagScale();
	const float tagFontSize = (UISettings::fontSize() / 2.0f) * scale;
	const ImVec2 barSize = ImVec2(UISettings::nametagBarSize().x * scale, UISettings::nametagBarSize().y * scale);

	// Optional weapon icon above player name
	if (Settings::showNametagWeapon() && pUI && pUI->weapFont()) {
		const char* weapGlyph = DeathWindow::spriteIDForWeapon(byteWeapon);
		if (weapGlyph && weapGlyph[0]) {
			const float weapFontSize = tagFontSize * 1.15f;
			ImVec2 weapSz = pUI->weapFont()->CalcTextSizeA(weapFontSize, FLT_MAX, 0.0f, "G");
			ImVec2 weapPos(vecOut.x - (weapSz.x * 0.5f), vecOut.y - weapFontSize * 1.25f);
			renderer->drawText(weapPos, 0xFF000000, "G", false, weapFontSize, pUI->weapFont());
			renderer->drawText(weapPos, 0xFFFFFFFF, weapGlyph, false, weapFontSize, pUI->weapFont());
		}
	}

	// name (id)
	ImVec2 pos = ImVec2(vecOut.x, vecOut.y);
	pos.x -= renderer->calculateTextSize(szNick, tagFontSize).x / 2;
	renderer->drawText(pos, __builtin_bswap32(dwColor | (0x000000FF)), szNick, true, tagFontSize);

	// Health bar
	if (fHealth < 0.0f) return;
	vecOut.x = (float) ((int) vecOut.x);
	vecOut.y = (float) ((int) vecOut.y);

	ImColor HealthBarBDRColor = ImColor(0x00, 0x00, 0x00, 0xFF);
	ImColor HealthBarColor = ImColor(0xB9, 0x22, 0x28, 0xFF);
	ImColor HealthBarBGColor = ImColor(0x4B, 0x0B, 0x14, 0xFF);

	float fOutline = UISettings::outlineSize();

	ImVec2 HealthBarBDR1;
	ImVec2 HealthBarBDR2;
	ImVec2 HealthBarBG1;
	ImVec2 HealthBarBG2;
	ImVec2 HealthBar1;
	ImVec2 HealthBar2;

	// top left
	HealthBarBDR1.x = vecOut.x - ((barSize.x / 2) + fOutline);
	HealthBarBDR1.y = vecOut.y + (tagFontSize * 1.2f);

	// bottom right
	HealthBarBDR2.x = vecOut.x + ((barSize.x / 2) + fOutline);
	HealthBarBDR2.y = vecOut.y + (tagFontSize * 1.2f) + barSize.y;

	// top left
	HealthBarBG1.x = HealthBarBDR1.x + fOutline;
	HealthBarBG1.y = HealthBarBDR1.y + fOutline;

	// bottom right
	HealthBarBG2.x = HealthBarBDR2.x - fOutline;
	HealthBarBG2.y = HealthBarBDR2.y - fOutline;

	// top left
	HealthBar1.x = HealthBarBG1.x;
	HealthBar1.y = HealthBarBG1.y;

	// bottom right
	HealthBar2.y = HealthBarBG2.y;

	if (fHealth > 100.0f) {
		fHealth = 100.0f;
	}

	fHealth *= barSize.x / 100.0f;
	fHealth -= (barSize.x / 2);
	HealthBar2.x = vecOut.x + fHealth;

	float offsetY = 13.0f * scale;

	if (fArmour > 0.0f) {
		HealthBarBDR1.y += offsetY;
		HealthBarBDR2.y += offsetY;
		HealthBarBG1.y += offsetY;
		HealthBarBG2.y += offsetY;
		HealthBar1.y += offsetY;
		HealthBar2.y += offsetY;
	}

	renderer->drawRect(HealthBarBDR1, HealthBarBDR2, HealthBarBDRColor, true);
	renderer->drawRect(HealthBarBG1, HealthBarBG2, HealthBarBGColor, true);
	renderer->drawRect(HealthBar1, HealthBar2, HealthBarColor, true);

	// Armour Bar
	if (fArmour > 0.0f) {
		HealthBarBDR1.y -= offsetY;
		HealthBarBDR2.y -= offsetY;
		HealthBarBG1.y -= offsetY;
		HealthBarBG2.y -= offsetY;
		HealthBar1.y -= offsetY;
		HealthBar2.y -= offsetY;

		HealthBarColor = ImColor(200, 200, 200, 255);
		HealthBarBGColor = ImColor(40, 40, 40, 255);

		if (fArmour > 100.0f) {
			fArmour = 100.0f;
		}

		fArmour *= barSize.x / 100.0f;
		fArmour -= (barSize.x / 2);
		HealthBar2.x = vecOut.x + fArmour;

		renderer->drawRect(HealthBarBDR1, HealthBarBDR2, HealthBarBDRColor, true);
		renderer->drawRect(HealthBarBG1, HealthBarBG2, HealthBarBGColor, true);
		renderer->drawRect(HealthBar1, HealthBar2, HealthBarColor, true);
	}

	// AFK icon
	if (bAfk && m_afkTexture && Settings::showNameTagStatus()) {
		ImVec2 a = ImVec2(HealthBarBDR1.x - (tagFontSize * 1.4f), HealthBarBDR1.y);
		ImVec2 b = ImVec2(a.x + (tagFontSize * 1.3f), a.y + (tagFontSize * 1.3f));
		renderer->drawImage(a, b, m_afkTexture->raster);
	}
}
