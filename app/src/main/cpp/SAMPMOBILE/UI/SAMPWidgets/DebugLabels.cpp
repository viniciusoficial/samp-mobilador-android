#include "DebugLabels.h"
#include "../UI.h"
#include "../UISettings.h"
#include "../../Client.h"
#include "../../Game/Game.h"
#include "../../Game/PlayerPed.h"
#include "../../Game/Vehicle.h"
#include "../../Game/Util.h"
#include "../../Net/NetGame.h"
#include "../../Net/LocalPlayer.h"
#include "../../Net/VehiclePool.h"
#include <SA.h>
#include <sstream>


/* DebugLables */

void DebugLabels::draw(ImGuiRenderer* renderer)
{
	if (!pNetGame) return;

	if (m_showVehicleLabels) {
		CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
		if (pVehiclePool) {
			for (VEHICLEID x = 0; x < MAX_VEHICLES; x++) {
				CVehicle* pVehicle = pVehiclePool->GetAt(x);
				if (pVehicle && pVehicle->m_pVehicle) {
					if (pVehicle->GetDistanceFromLocalPlayerPed() <= 20.0f) {
						RwMatrix matVehicle = pVehicle->m_pVehicle->GetMatrix().ToRwMatrix();

						RwV3d vecPos;
						vecPos.x = matVehicle.pos.x;
						vecPos.y = matVehicle.pos.y;
						vecPos.z = matVehicle.pos.z;

						RwV3d vecOut;
						CalcScreenCoors(&vecPos, &vecOut, 0.0f, 0.0f, false, false);

						if (vecOut.z < 1.0f) {
							continue;
						}

						char szTextLabel[256];
						snprintf(szTextLabel, sizeof(szTextLabel),
								"[ID: %d | Model: %d | Subtype: %d | Health: %.1f | Preloaded: %d]\nDistance: %.2fm\nPassengerSeats: %d\ncPos: %.3f, %.3f, %.3f\nsPos: %.3f, %.3f, %.3f",
								x, pVehicle->m_pVehicle->GetModelId(), pVehicle->GetVehicleSubtype(),
								pVehicle->GetHealth(), pVehicle->m_bPreloaded,
								pVehicle->GetDistanceFromLocalPlayerPed(), pVehicle->m_pVehicle->m_nMaxPassengers,
								matVehicle.pos.x, matVehicle.pos.y, matVehicle.pos.z,
								pVehiclePool->GetVehiclePos(x).x, pVehiclePool->GetVehiclePos(x).y, pVehiclePool->GetVehiclePos(x).z
						);

						std::stringstream ss_data(szTextLabel);
						std::string s_row;
						while (std::getline(ss_data, s_row, '\n')) {
							ImVec2 sz = renderer->calculateTextSize(s_row, UISettings::fontSize() / 2);
							renderer->drawText(ImVec2(vecOut.x - (sz.x / 2), vecOut.y), __builtin_bswap32((0x358BD4FF) | (0x000000FF)), s_row, true, UISettings::fontSize() / 2);
							vecOut.y += UISettings::fontSize() / 2;
						}
					}
				}
			}
		}
	}

	if (m_showCameraTargets) {
		CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
		CLocalPlayer* pLocal = pPlayerPool ? pPlayerPool->GetLocalPlayer() : nullptr;
		if (pLocal) {
			const uint16_t* ct = pLocal->GetCameraTargets();
			char buf[256];
			snprintf(buf, sizeof(buf),
					"CameraTarget: Obj=%u Veh=%u Player=%u Actor=%u | Aim: Player=%u Actor=%u",
					(unsigned) ct[0], (unsigned) ct[1], (unsigned) ct[2], (unsigned) ct[3],
					(unsigned) pLocal->GetAimedPlayerId(), (unsigned) pLocal->GetAimedActorId());
			const float fontSize = UISettings::fontSize() * 0.45f;
			const ImVec2 pos(pUI ? pUI->ScaleX(20.0f) : 20.0f, pUI ? pUI->ScaleY(1020.0f) : 500.0f);
			renderer->drawText(pos, ImColor(0x88, 0xFF, 0xCC, 0xFF), buf, true, fontSize);
		}
	}
}
