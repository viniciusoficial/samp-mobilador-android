#include "Pickups.h"
#include "Game.h"
#include "PlayerPed.h"
#include "Util.h"
#include "Engine/PatchRegistry.h"
#include "Engine/Symbols.h"
#include "../Addr.h"
#include "../Client.h"
#include "../Net/NetGame.h"
#include "../Net/LocalPlayer.h"
#include "../Net/PickupPool.h"
#include <Memory.h>
#include <SA.h>
#include <spdlog/spdlog.h>


DECL_HOOK(void, CPickups_Update)
{
	static std::array<sa::CPickup, sa::MAX_NUM_PICKUPS>& aPickUps = *(std::array<sa::CPickup, sa::MAX_NUM_PICKUPS>*) (g_saSym->Abs(addr::CPickups_aPickUps));
	const uint32_t frameCounter = *eng::as<uint32_t*>(eng::Sym::CTimer_m_FrameCounter);
	const uint32_t nowMs = GetTickCount();

	// 1) Amortized 1/32 visibility streaming pass
	auto start = sa::MAX_NUM_PICKUPS * (frameCounter % 32) / 32;
	auto end = sa::MAX_NUM_PICKUPS * (frameCounter % 32 + 1) / 32;
	for (auto i = start; i < end; i++) {
		auto& pickup = aPickUps[i];
		if (pickup.m_nPickupType == sa::CPickup::PICKUP_NONE) {
			continue;
		}

		pickup.m_nFlags.bVisible = pickup.IsVisible();
		if (pickup.m_nFlags.bVisible) {
			if (!pickup.m_nFlags.bDisabled && !pickup.m_pObject) {
				pickup.GiveUsAPickUpObject(&pickup.m_pObject, -1);

				if (auto& obj = pickup.m_pObject; obj) {
					eng::fn::CWorld_Add(obj);
				}
			}
		}
		else {
			pickup.GetRidOfObjects();
		}
	}

	// 2) Per-frame regeneration & pickup collision check for all visible pickups
	CPickupPool* pPickups = pNetGame ? pNetGame->GetPickupPool() : nullptr;
	for (uint32_t i = 0; i < sa::MAX_NUM_PICKUPS; ++i) {
		auto& pickup = aPickUps[i];
		if (pickup.m_nPickupType == sa::CPickup::PICKUP_NONE || !pickup.m_nFlags.bVisible) {
			continue;
		}

		if (pickup.m_nFlags.bDisabled) {
			if (pickup.m_nRegenerationTime != 0 && static_cast<int32_t>(nowMs - pickup.m_nRegenerationTime) >= 0) {
				pickup.m_nFlags.bDisabled = 0;
				pickup.m_nRegenerationTime = 0;
				pickup.m_bIsPicked = 0;
				if (!pickup.m_pObject) {
					pickup.GiveUsAPickUpObject(&pickup.m_pObject, -1);
					if (auto& obj = pickup.m_pObject; obj) {
						eng::fn::CWorld_Add(obj);
					}
				}
			}
			continue;
		}

		if (pickup.Update()) {
			if (pPickups) {
				pPickups->PickedUp(static_cast<int>(i));
			}
		}
	}
}

void Pickups::installHooks()
{
	spdlog::info("Installing pickup hooks...");
	REG_HOOK(Boot, CPickups_Update, CPickups_Update, "SA-MP pickup collection detection");
}

bool sa::CPickup::Update()
{
	if (m_nFlags.bDisabled) {
		return false;
	}
	if (!pNetGame || !pNetGame->GetPlayerPool()) {
		return false;
	}
	CLocalPlayer* pLocal = pNetGame->GetPlayerPool()->GetLocalPlayer();
	if (!pLocal || !pLocal->GetPlayerPed() || !pLocal->GetPlayerPed()->m_ped) {
		return false;
	}

	auto ped = pLocal->GetPlayerPed()->m_ped;
	auto oldState = m_bIsPicked;

	auto checkPickup = [&]() -> bool {
		// SA-MP pickup type 14 (PICKUP_FLOATINGPACKAGE_FLOATING): vehicle-only pickup
		if (m_nPickupType == PICKUP_FLOATINGPACKAGE_FLOATING) {
			if (!ped->IsInVehicle() || !ped->pVehicle) {
				return false;
			}
			return DistanceBetweenPoints(GetPosn(), ped->pVehicle->GetPosition()) < 2.8f;
		}
		else {
			if (ped->IsInVehicle()) {
				return false;
			}
			return DistanceBetweenPoints(GetPosn(), ped->GetPosition()) < 1.3f;
		}
	};

	m_bIsPicked = checkPickup();

	if (oldState == 0 && m_bIsPicked != 0) {
		// Apply PC SA-MP disappear/regeneration rules per pickup type
		switch (m_nPickupType) {
			case PICKUP_ON_STREET: // 2: respawn after 30s
				m_nFlags.bDisabled = 1;
				m_nRegenerationTime = GetTickCount() + 30000;
				GetRidOfObjects();
				break;
			case PICKUP_ON_STREET_SLOW: // 15: respawn after 300s
				m_nFlags.bDisabled = 1;
				m_nRegenerationTime = GetTickCount() + 300000;
				GetRidOfObjects();
				break;
			case PICKUP_ONCE: // 3
			case PICKUP_ONCE_TIMEOUT: // 4
			case PICKUP_ONCE_TIMEOUT_SLOW: // 5
			case PICKUP_MONEY: // 8
			case PICKUP_FLOATINGPACKAGE_FLOATING: // 14 (vehicle-only, disappears on pickup)
			case PICKUP_ONCE_FOR_MISSION: // 22
				m_nFlags.bDisabled = 1;
				m_nRegenerationTime = 0;
				GetRidOfObjects();
				break;
			default:
				break;
		}
		return true;
	}

	return false;
}
