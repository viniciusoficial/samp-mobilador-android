#include "PickupPool.h"
#include "NetGame.h"
#include "Proto/RpcOutbound.h"
#include "../Client.h"
#include "../Game/Game.h"
#include "../Game/Scripting.h"
#include "../Game/Util.h"
#include "../Game/Artwork/ArtworkCore.h"
#include "../Game/Artwork/ArtworkManager.h"
#include <RakNet/RakClientInterface.h>
#include <RakNet/SAMP/SAMPRPC.h>
#include <spdlog/spdlog.h>


CPickupPool::CPickupPool()
{
	memset(m_Pickups, 0, sizeof(m_Pickups));
	memset(m_droppedWeapon, 0, sizeof(m_droppedWeapon));

	for (int i = 0; i < MAX_PICKUPS; i++) {
		m_dwHnd[i] = 0;
		m_cooldownUntilMs[i] = 0;
		m_dwGTAId[i] = -1;
		m_effectiveModel[i] = -1;
		m_droppedWeapon[i].PlayerID = INVALID_PLAYER_ID;
	}
	for (int i = 0; i < kMaxGtaPickups; i++) {
		m_gtaIdToSampId[i] = -1;
	}
	m_highestActiveSlot = -1;
}

CPickupPool::~CPickupPool()
{
	for (int i = 0; i <= m_highestActiveSlot; i++) {
		if (m_dwHnd[i] != 0) {
			ScriptCommand(&destroy_pickup, m_dwHnd[i]);
		}
	}
}

void CPickupPool::Process()
{
	if (m_highestActiveSlot < 0) return;

	const uint32_t nowMs = GetTickCount();
	int newHighest = -1;
	for (int i = 0; i <= m_highestActiveSlot; i++) {
		if (m_dwHnd[i] != 0) {
			newHighest = i;
			if (!m_droppedWeapon[i].bDroppedWeapon && m_cooldownUntilMs[i] != 0
					&& static_cast<int32_t>(nowMs - m_cooldownUntilMs[i]) >= 0) {
				m_cooldownUntilMs[i] = 0;
			}
		}
	}
	m_highestActiveSlot = newHighest;
}

/**
 * @param pPickup
 * @param iPickup
 */
void CPickupPool::New(PICKUP* pPickup, int iPickup)
{
	if (!pPickup || !pGame || iPickup < 0 || iPickup >= MAX_PICKUPS) return;

	if (m_dwHnd[iPickup] != 0) {
		Destroy(iPickup);
	}

	memcpy(&m_Pickups[iPickup], pPickup, sizeof(PICKUP));
	m_droppedWeapon[iPickup].bDroppedWeapon = false;
	m_droppedWeapon[iPickup].PlayerID = INVALID_PLAYER_ID;
	m_cooldownUntilMs[iPickup] = 0;

	int dwGTAId = -1;
	int effectiveModel = -1;
	uint32_t hnd = pGame->CreatePickup(pPickup->iModel, pPickup->iType,
			pPickup->fX, pPickup->fY, pPickup->fZ, &dwGTAId, &effectiveModel);
	m_dwHnd[iPickup] = hnd;
	m_effectiveModel[iPickup] = effectiveModel;
	if (hnd != 0) {
		if (iPickup > m_highestActiveSlot) {
			m_highestActiveSlot = iPickup;
		}
		if (dwGTAId >= 0 && dwGTAId < kMaxGtaPickups) {
			m_dwGTAId[iPickup] = dwGTAId;
			m_gtaIdToSampId[dwGTAId] = iPickup;
		}
		else {
			m_dwGTAId[iPickup] = -1;
		}
	}
	else {
		m_dwGTAId[iPickup] = -1;
	}
}

void CPickupPool::CreateWeapon(int iModel, float fX, float fY, float fZ, int iAmmo, PLAYERID owner)
{
	if (!pGame || iModel <= 0) return;

	// Replace any previous dropped weapon from the same owner
	DestroyWeaponByOwner(owner);

	int slot = -1;
	for (int i = 0; i < MAX_PICKUPS; ++i) {
		if (m_dwHnd[i] == 0) {
			slot = i;
			break;
		}
	}
	if (slot < 0) return;

	uint32_t hnd = pGame->CreateWeaponPickup(iModel, iAmmo, fX, fY, fZ);
	if (hnd == 0) return;

	const int gtaId = static_cast<int>(hnd & 0xFFFFu);
	m_Pickups[slot].iModel = iModel;
	m_Pickups[slot].iType = 4;
	m_Pickups[slot].fX = fX;
	m_Pickups[slot].fY = fY;
	m_Pickups[slot].fZ = fZ;
	m_dwHnd[slot] = hnd;
	m_dwGTAId[slot] = (gtaId >= 0 && gtaId < kMaxGtaPickups) ? gtaId : -1;
	if (m_dwGTAId[slot] >= 0) {
		m_gtaIdToSampId[m_dwGTAId[slot]] = slot;
	}
	m_cooldownUntilMs[slot] = 0;
	m_droppedWeapon[slot].bDroppedWeapon = true;
	m_droppedWeapon[slot].PlayerID = owner;
	if (slot > m_highestActiveSlot) {
		m_highestActiveSlot = slot;
	}
}

/**
 * @param iPickup
 */
void CPickupPool::Destroy(int iPickup)
{
	if (iPickup < 0 || iPickup >= MAX_PICKUPS) return;

	if (m_dwHnd[iPickup]) {
		const int gtaId = m_dwGTAId[iPickup];
		if (gtaId >= 0 && gtaId < kMaxGtaPickups && m_gtaIdToSampId[gtaId] == iPickup) {
			m_gtaIdToSampId[gtaId] = -1;
		}
		ScriptCommand(&destroy_pickup, m_dwHnd[iPickup]);
		m_dwHnd[iPickup] = 0;
		m_cooldownUntilMs[iPickup] = 0;
		m_dwGTAId[iPickup] = -1;
		m_effectiveModel[iPickup] = -1;
		m_droppedWeapon[iPickup].bDroppedWeapon = false;
		m_droppedWeapon[iPickup].PlayerID = INVALID_PLAYER_ID;
	}
}

void CPickupPool::RefreshCustomModels()
{
	auto& art = artwork::ArtworkManager::instance();
	int refreshed = 0;
	for (int i = 0; i <= m_highestActiveSlot && i < MAX_PICKUPS; ++i) {
		if (m_dwHnd[i] == 0 || m_droppedWeapon[i].bDroppedWeapon) {
			continue;
		}
		const int32_t modelId = m_Pickups[i].iModel;
		if (!artwork::isCustomObjectId(modelId) || artwork::isInternalCustomSlot(m_effectiveModel[i])) {
			continue;
		}
		if (art.ensureCustomObjectLoaded(modelId) >= 0) {
			PICKUP copy = m_Pickups[i];
			New(&copy, i);
			++refreshed;
		}
	}
	if (refreshed > 0) {
		spdlog::info("CPickupPool::RefreshCustomModels: upgraded {} deferred custom pickup(s)", refreshed);
	}
}

void CPickupPool::DestroyCustomModelsBeforeUnload()
{
	for (int i = 0; i <= m_highestActiveSlot && i < MAX_PICKUPS; ++i) {
		if (m_dwHnd[i] != 0 && (artwork::isCustomObjectId(m_Pickups[i].iModel) || artwork::isInternalCustomSlot(m_effectiveModel[i]))) {
			Destroy(i);
		}
	}
}

/**
 * @param owner owner player id sent by ScrDestroyWeaponPickup
 */
void CPickupPool::DestroyWeaponByOwner(PLAYERID owner)
{
	for (int i = 0; i <= m_highestActiveSlot; i++) {
		if (m_dwHnd[i] && m_droppedWeapon[i].bDroppedWeapon && m_droppedWeapon[i].PlayerID == owner) {
			Destroy(i);
		}
	}
}

/**
 * @param dwGTAId
 * @return
 */
int CPickupPool::GetIDFromGTAId(int dwGTAId)
{
	if (dwGTAId >= 0 && dwGTAId < kMaxGtaPickups) {
		const int id = m_gtaIdToSampId[dwGTAId];
		if (id >= 0 && id < MAX_PICKUPS && m_dwHnd[id] != 0 && m_dwGTAId[id] == dwGTAId) {
			return id;
		}
	}
	return -1;
}

/**
 * @param dwGTAId
 */
void CPickupPool::PickedUp(int dwGTAId)
{
	if (!pNetGame || !pNetGame->GetRakClient()) return;
	int PickupID = GetIDFromGTAId(dwGTAId);
	if (PickupID < 0 || PickupID >= MAX_PICKUPS || m_dwHnd[PickupID] == 0) return;

	const uint32_t nowMs = GetTickCount();
	if (!m_droppedWeapon[PickupID].bDroppedWeapon && m_cooldownUntilMs[PickupID] != 0
			&& static_cast<int32_t>(nowMs - m_cooldownUntilMs[PickupID]) < 0) {
		return;
	}

	if (m_droppedWeapon[PickupID].bDroppedWeapon) {
		RakNet::BitStream bsPickup;
		proto::Writer writer(bsPickup);
		proto::PickedUpWeaponPickupReq req{};
		req.owner = m_droppedWeapon[PickupID].PlayerID;
		req.serialize(writer, proto::ProtoVersion::V037);
		pNetGame->GetRakClient()->RPC(&RPC_DestroyWeaponPickup, &bsPickup, HIGH_PRIORITY, RELIABLE_SEQUENCED, 0, false, UNASSIGNED_NETWORK_ID, nullptr);
		Destroy(PickupID);
		return;
	}

	RakNet::BitStream bsPickup;
	proto::Writer writer(bsPickup);
	proto::PickedUpPickupReq req{};
	req.pickupId = PickupID;
	req.serialize(writer, proto::ProtoVersion::V037);
	pNetGame->GetRakClient()->RPC(&RPC_PickedUpPickup, &bsPickup, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false, UNASSIGNED_NETWORK_ID, nullptr);
	m_cooldownUntilMs[PickupID] = (nowMs + 1500u) ? (nowMs + 1500u) : 1u;
}
