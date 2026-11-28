#include "TextLabelPool.h"
#include "NetGame.h"
#include "RemotePlayer.h"
#include "VehiclePool.h"
#include "../Client.h"
#include "../Settings.h"
#include "../Game/Game.h"
#include "../Game/PlayerPed.h"
#include "../Game/Scripting.h"
#include "../Game/Vehicle.h"
#include "../Game/Util.h"
#include "../UI/UI.h"
#include "../UI/UISettings.h"
#include "../UI/ImGuiRenderer.h"
#include <encoding.h>
#include <sstream>


CTextLabelPool::CTextLabelPool()
{
	for (int i = 0; i < MAX_TEXT_LABELS; i++) {
		strcpy(m_TextLabels[i].text, "");
		m_TextLabels[i].dwColor = 0;
		m_TextLabels[i].vecPos.x = 0.0f;
		m_TextLabels[i].vecPos.y = 0.0f;
		m_TextLabels[i].vecPos.z = 0.0f;
		m_TextLabels[i].fDistance = 0.0f;
		m_TextLabels[i].bTestLOS = false;
		m_TextLabels[i].playerId = 0;
		m_TextLabels[i].vehicleId = 0;

		m_bSlotUsed[i] = false;
	}
}

CTextLabelPool::~CTextLabelPool()
{
	for (int i = 0; i < MAX_TEXT_LABELS; i++) {
		if (m_bSlotUsed[i]) {
			ClearLabel(i);
		}
	}
}

/**
 * @param wLabelId
 * @param pLabel
 */
void CTextLabelPool::NewLabel(uint16_t wLabelId, TEXT_LABEL* pLabel)
{

	if (wLabelId < MAX_TEXT_LABELS) {
		if (m_bSlotUsed[wLabelId]) {
			TEXT_LABEL* pTextLabel = &m_TextLabels[wLabelId];
			strcpy(pTextLabel->text, "");
			pTextLabel->dwColor = 0;
			pTextLabel->vecPos.x = 0.0f;
			pTextLabel->vecPos.y = 0.0f;
			pTextLabel->vecPos.z = 0.0f;
			pTextLabel->fDistance = 0.0f;
			pTextLabel->bTestLOS = false;
			pTextLabel->playerId = 0;
			pTextLabel->vehicleId = 0;
		}

		TEXT_LABEL* pTextLabel = &m_TextLabels[wLabelId];
		std::string utf8Text = Encoding::cp2utf(pLabel->text);
		strncpy(pTextLabel->text, utf8Text.c_str(), sizeof(pTextLabel->text) - 1);
		pTextLabel->text[sizeof(pTextLabel->text) - 1] = 0;

		pTextLabel->dwColor = pLabel->dwColor;
		pTextLabel->vecPos.x = pLabel->vecPos.x;
		pTextLabel->vecPos.y = pLabel->vecPos.y;
		pTextLabel->vecPos.z = pLabel->vecPos.z;
		pTextLabel->fDistance = pLabel->fDistance;
		pTextLabel->bTestLOS = pLabel->bTestLOS;
		pTextLabel->playerId = pLabel->playerId;
		pTextLabel->vehicleId = pLabel->vehicleId;

		if (pLabel->vehicleId != INVALID_VEHICLE_ID || pLabel->playerId != INVALID_PLAYER_ID) {
			pTextLabel->vecOffsetCoords = pLabel->vecPos;
		}

		m_bSlotUsed[wLabelId] = true;
	}
}

/**
 * @param wLabelId
 */
void CTextLabelPool::ClearLabel(uint16_t wLabelId)
{
	if (wLabelId < MAX_TEXT_LABELS && m_bSlotUsed[wLabelId]) {

		TEXT_LABEL* label = &m_TextLabels[wLabelId];
		strcpy(label->text, "");
		label->dwColor = 0;
		label->vecPos.x = 0.0f;
		label->vecPos.y = 0.0f;
		label->vecPos.z = 0.0f;
		label->fDistance = 0.0f;
		label->bTestLOS = false;
		label->playerId = 0;
		label->vehicleId = 0;

		m_bSlotUsed[wLabelId] = false;
	}
}

/**
 * @param renderer
 */
void CTextLabelPool::Render(ImGuiRenderer* renderer)
{
	CPlayerPed* pLocalPed = pGame->FindPlayerPed();
	if (!pLocalPed || !pLocalPed->m_ped || !pNetGame) {
		return;
	}

	CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
	CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();

	for (int i = 0; i < MAX_TEXT_LABELS; i++) {
		if (!m_bSlotUsed[i]) {
			continue;
		}
		TEXT_LABEL* pTextLabel = &m_TextLabels[i];

		sa::CVector textPos{0.0f, 0.0f, 0.0f};
		if (pTextLabel->playerId != INVALID_PLAYER_ID) {
			if (!pPlayerPool || pTextLabel->playerId == pPlayerPool->GetLocalPlayerID()) {
				continue;
			}

			if (pPlayerPool->GetSlotState(pTextLabel->playerId)) {
				CRemotePlayer* pPlayer = pPlayerPool->GetAt(pTextLabel->playerId);
				if (!pPlayer) continue;

				CPlayerPed* pRemotePed = pPlayer->GetPlayerPed();
				if (!pRemotePed || !pRemotePed->m_ped || !pRemotePed->IsAdded()) continue;

				sa::CVector bonePos{};
				pRemotePed->m_ped->GetBonePosition(&bonePos, 8, false);
				const float fCamDist = pRemotePed->GetDistanceFromCamera();

				textPos.x = bonePos.x + pTextLabel->vecOffsetCoords.x;
				textPos.y = bonePos.y + pTextLabel->vecOffsetCoords.y;
				textPos.z = bonePos.z + (fCamDist * 0.0475f) + pTextLabel->vecOffsetCoords.z;
			}
			else {
				continue;
			}
		}
		else if (pTextLabel->vehicleId != INVALID_VEHICLE_ID) {
			if (pVehiclePool && pVehiclePool->GetSlotState(pTextLabel->vehicleId)) {
				CVehicle* pVehicle = pVehiclePool->GetAt(pTextLabel->vehicleId);
				if (!pVehicle || !pVehicle->m_pVehicle || !pVehicle->VerifyInstance()) continue;

				RwMatrix matVehicle = pVehicle->m_pVehicle->GetMatrix().ToRwMatrix();
				ProjectMatrix(&textPos, reinterpret_cast<sa::CMatrix*>(&matVehicle), &pTextLabel->vecOffsetCoords);
			}
			else {
				continue;
			}
		}
		else {
			textPos = pTextLabel->vecPos;
		}

		pTextLabel->vecPos = textPos;

		// Distance culling before the line-of-sight raycast
		if (pLocalPed->GetDistanceFromPoint(textPos) > pTextLabel->fDistance) {
			continue;
		}

		if (pTextLabel->bTestLOS) {
			CAMERA_AIM* pCam = GameGetInternalAim();
			if (pCam) {
				int hitEntity = ScriptCommand(&get_line_of_sight,
						textPos.x, textPos.y, textPos.z,
						pCam->pos1x, pCam->pos1y, pCam->pos1z,
						1, 0, 0, 1, 0);
				if (!hitEntity) {
					continue;
				}
			}
		}

		Draw(renderer, textPos, pTextLabel->text, pTextLabel->dwColor);
	}
}

/**
 * @param renderer
 * @param vecPos
 * @param text
 * @param dwColor
 */
void CTextLabelPool::Draw(ImGuiRenderer* renderer, sa::CVector vecPos, const char* text, uint32_t dwColor)
{
	const uint8_t alpha = static_cast<uint8_t>(dwColor & 0xFF);
	if (alpha == 0) return;

	RwV3d vPos = vecPos;
	RwV3d vecOut;

	CalcScreenCoors(&vPos, &vecOut, 0.0f, 0.0f, false, false);
	if (vecOut.z < 1.0f) return;

	const float labelFontSize = (UISettings::fontSize() / 2.0f) * Settings::textlabelScale();
	const uint32_t abgr = __builtin_bswap32(dwColor);
	const bool bOutline = (alpha == 0xFF);
	std::stringstream ss_data(text);
	std::string s_row;
	std::string carryColor;
	while (std::getline(ss_data, s_row, '\n')) {
		if (!carryColor.empty()) {
			s_row = carryColor + s_row;
		}
		for (size_t i = 0; i + 7 < s_row.size(); ++i) {
			if (s_row[i] == '{' && s_row[i + 7] == '}') {
				bool hex = true;
				for (size_t k = 1; k <= 6; ++k) {
					if (!std::isxdigit(static_cast<unsigned char>(s_row[i + k]))) { hex = false; break; }
				}
				if (hex) {
					carryColor = s_row.substr(i, 8);
					i += 7;
				}
			}
		}
		ImVec2 sz = renderer->calculateTextSize(s_row, labelFontSize);
		renderer->drawText(ImVec2(vecOut.x - (sz.x / 2), vecOut.y), abgr, s_row, bOutline, labelFontSize);
		vecOut.y += labelFontSize;
	}
}
