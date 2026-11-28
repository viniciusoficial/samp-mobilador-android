#include "ScmEvents.h"
#include "NetGame.h"
#include "LocalPlayer.h"
#include "RemotePlayer.h"
#include "VehiclePool.h"
#include "../Client.h"
#include "../Game/Game.h"
#include "../Game/PlayerPed.h"
#include "../Game/Vehicle.h"
#include "../Game/Util.h"
#include <spdlog/spdlog.h>

#define EVENT_TYPE_PAINTJOB            1
#define EVENT_TYPE_CARCOMPONENT        2
#define EVENT_TYPE_CARCOLOR            3
#define EVENT_ENTEREXIT_MODSHOP        4

void ProcessIncomingScmEvent(uint16_t playerId, int iEventType, uint32_t dwParam1, uint32_t dwParam2, uint32_t dwParam3)
{
	if (pNetGame == nullptr) return;

	CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
	CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
	if (!pVehiclePool || !pPlayerPool) return;

	CVehicle* pVehicle = nullptr;
	CRemotePlayer* pPlayer = nullptr;

	switch (iEventType) {
		case EVENT_TYPE_PAINTJOB:
			if (dwParam1 >= MAX_VEHICLES) return;
			pVehicle = pVehiclePool->GetAt(static_cast<VEHICLEID>(dwParam1));
			if (pVehicle) {
				pVehicle->SetPaintJob(dwParam2);
			}
			break;

		case EVENT_TYPE_CARCOMPONENT:
			if (dwParam1 >= MAX_VEHICLES) return;
			pVehicle = pVehiclePool->GetAt(static_cast<VEHICLEID>(dwParam1));
			if (pVehicle) {
				pVehicle->AddComponent(dwParam2);
			}
			break;

		case EVENT_TYPE_CARCOLOR:
			if (dwParam1 >= MAX_VEHICLES) return;
			pVehicle = pVehiclePool->GetAt(static_cast<VEHICLEID>(dwParam1));
			if (pVehicle) {
				pVehicle->SetColor(dwParam2, dwParam3);
			}
			break;

		case EVENT_ENTEREXIT_MODSHOP:
			if (playerId >= MAX_PLAYERS || dwParam1 >= MAX_VEHICLES) return;

			pVehicle = pVehiclePool->GetAt(static_cast<VEHICLEID>(dwParam1));
			pPlayer = pPlayerPool->GetAt(playerId);

			if (pPlayer && pVehicle) {
				CPlayerPed* pPlayerPed = pPlayer->GetPlayerPed();
				CLocalPlayer* pLocalPlayer = pPlayerPool->GetLocalPlayer();
				CPlayerPed* pLocalPlayerPed = pLocalPlayer ? pLocalPlayer->GetPlayerPed() : nullptr;

				if (pPlayerPed && pLocalPlayerPed) {
					if (pPlayerPed->IsInVehicle()) {
						if (pLocalPlayerPed->IsInVehicle()) {
							if (!pPlayerPed->IsAPassenger()) {
								if (pLocalPlayerPed->IsAPassenger()) {
									VEHICLEID VehicleID = pPlayerPed->GetCurrentVehicleID();
									if (VehicleID == pLocalPlayerPed->GetCurrentVehicleID()) {
										CVehicle* pCurrentVehicle = pPlayer->m_pCurrentVehicle;

										if (dwParam2) {
											if (pCurrentVehicle) {
												pCurrentVehicle->LinkToInterior(dwParam3);
											}
											pLocalPlayerPed->SetInterior(dwParam3, false);
											pLocalPlayerPed->TogglePlayerControllable(false);
										}
										else {
											if (pCurrentVehicle) {
												pCurrentVehicle->LinkToInterior(0);
											}
											pLocalPlayerPed->SetInterior(0, false);
											pLocalPlayerPed->TogglePlayerControllable(true);
										}
									}
								}
							}
						}
					}
				}
			}
			break;
	}
}
