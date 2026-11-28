#include "LocalPlayer.h"
#include "NetGame.h"
#include "RemotePlayer.h"
#include "VehiclePool.h"
#include "Proto/RpcOutbound.h"
#include "Proto/Sync.h"
#include "../Client.h"
#include "../Game/Game.h"
#include "../Game/Aimstuff.h"
#include "../Game/Common.h"
#include "../Game/Pad.h"
#include "../Game/PlayerPed.h"
#include "../Game/Vehicle.h"
#include "../Game/Util.h"
#include <RakNet/RakClientInterface.h>
#include <RakNet/PacketEnumerations.h>
#include <RakNet/SAMP/SAMPRPC.h>
#include <spdlog/spdlog.h>

void ApplyEffectiveLocalOnFootKeys(CPlayerPed* pPlayerPed, uint16_t& wKeys, uint16_t& lrAnalog, uint16_t& udAnalog);

void CLocalPlayer::SendWastedNotification()
{
	uint8_t byteDeathReason;
	PLAYERID WhoWasResponsible;
	RakNet::BitStream bsPlayerDeath;
	byteDeathReason = m_pPlayerPed->FindDeathReasonAndResponsiblePlayer(&WhoWasResponsible);
	proto::DeathReq req{};
	req.reason = byteDeathReason;
	req.killerId = WhoWasResponsible;
	proto::Writer w(bsPlayerDeath);
	req.serialize(w, proto::ProtoVersion::V037);
	pNetGame->GetRakClient()->RPC(&RPC_Death, &bsPlayerDeath, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false, UNASSIGNED_NETWORK_ID, nullptr);
}

void CLocalPlayer::SendOnFootFullSyncData()
{
	RakNet::BitStream bsPlayerSync;
	RwMatrix matPlayer;
	sa::CVector vecMoveSpeed;
	uint16_t lrAnalog, udAnalog;
	uint8_t exKeys;
	uint16_t wKeys = m_pPlayerPed->GetKeys(&lrAnalog, &udAnalog, &exKeys);
	ApplyEffectiveLocalOnFootKeys(m_pPlayerPed, wKeys, lrAnalog, udAnalog);

	ONFOOT_SYNC_DATA ofSync{};

	matPlayer = m_pPlayerPed->m_ped->GetMatrix().ToRwMatrix();
	vecMoveSpeed = m_pPlayerPed->m_ped->GetMoveSpeed();

	ofSync.lrAnalog = lrAnalog;
	ofSync.udAnalog = udAnalog;
	ofSync.wKeys = wKeys;

	ofSync.vecPos.x = matPlayer.pos.x;
	ofSync.vecPos.y = matPlayer.pos.y;
	ofSync.vecPos.z = matPlayer.pos.z;

	ofSync.quat.SetFromMatrix(&matPlayer);
	ofSync.quat.Normalize();

	if (FloatOffset(ofSync.quat.w, m_ofSync.quat.w) < 0.00001 &&
			FloatOffset(ofSync.quat.x, m_ofSync.quat.x) < 0.00001 &&
			FloatOffset(ofSync.quat.y, m_ofSync.quat.y) < 0.00001 &&
			FloatOffset(ofSync.quat.z, m_ofSync.quat.z) < 0.00001) {
		ofSync.quat.Set(m_ofSync.quat);
	}

	ofSync.byteHealth = (uint8_t) m_pPlayerPed->GetHealth();
	ofSync.byteArmour = (uint8_t) m_pPlayerPed->GetArmour();

	ofSync.byteCurrentWeapon = (exKeys << 6) | (ofSync.byteCurrentWeapon & 0x3F);
	ofSync.byteCurrentWeapon ^= (ofSync.byteCurrentWeapon ^ m_pPlayerPed->GetCurrentWeapon()) & 0x3F;
	ofSync.byteSpecialAction = GetSpecialAction();

	ofSync.vecMoveSpeed.x = vecMoveSpeed.x;
	ofSync.vecMoveSpeed.y = vecMoveSpeed.y;
	ofSync.vecMoveSpeed.z = vecMoveSpeed.z;

	ofSync.vecSurfOffsets.x = 0.0f;
	ofSync.vecSurfOffsets.y = 0.0f;
	ofSync.vecSurfOffsets.z = 0.0f;
	ofSync.wSurfInfo = 0;
	if (m_surfSync.bIsActive) {
		ofSync.vecSurfOffsets.x = m_surfSync.vecOffsetPos.x;
		ofSync.vecSurfOffsets.y = m_surfSync.vecOffsetPos.y;
		ofSync.vecSurfOffsets.z = m_surfSync.vecOffsetPos.z;
		if (m_surfSync.bIsVehicle) {
			ofSync.wSurfInfo = (uint16_t) m_surfSync.dwSurfVehID;
		}
		else {
			// object surfing is sent as objectId + MAX_VEHICLES
			ofSync.wSurfInfo = (uint16_t) (m_surfSync.dwSurfVehID + MAX_VEHICLES);
		}
	}

	ofSync.dwAnimation = m_pPlayerPed->GetCurrentAnimationIndexFlag();

	if ((GetTickCount() - m_dwLastOnFootSyncTick) > 500 || memcmp(&m_ofSync, &ofSync, sizeof(ONFOOT_SYNC_DATA))) {
		m_dwLastOnFootSyncTick = GetTickCount();

		proto::OutboundPlayerSync pkt{};
		pkt.lrAnalog = ofSync.lrAnalog;
		pkt.udAnalog = ofSync.udAnalog;
		pkt.keys = ofSync.wKeys;
		pkt.position = {ofSync.vecPos.x, ofSync.vecPos.y, ofSync.vecPos.z};
		pkt.quat = {ofSync.quat.w, ofSync.quat.x, ofSync.quat.y, ofSync.quat.z};
		pkt.health = ofSync.byteHealth;
		pkt.armour = ofSync.byteArmour;
		pkt.currentWeapon = ofSync.byteCurrentWeapon;
		pkt.specialAction = ofSync.byteSpecialAction;
		pkt.moveSpeed = {ofSync.vecMoveSpeed.x, ofSync.vecMoveSpeed.y, ofSync.vecMoveSpeed.z};
		pkt.surfOffsets = {ofSync.vecSurfOffsets.x, ofSync.vecSurfOffsets.y, ofSync.vecSurfOffsets.z};
		pkt.surfInfo = ofSync.wSurfInfo;
		pkt.animation = ofSync.dwAnimation;

		proto::Writer w(bsPlayerSync);
		pkt.serialize(w, proto::ProtoVersion::V037);
		pNetGame->GetRakClient()->Send(&bsPlayerSync, HIGH_PRIORITY, UNRELIABLE_SEQUENCED, 1);
		memcpy(&m_ofSync, &ofSync, sizeof(ONFOOT_SYNC_DATA));
	}
}

void CLocalPlayer::SendInCarFullSyncData()
{
	RakNet::BitStream bsVehicleSync;
	CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
	CPlayerPed* pPlayerPed = m_pPlayerPed;

	if (!pPlayerPed || !pVehiclePool) {
		return;
	}

	uint16_t lrAnalog, udAnalog;
	uint8_t exKeys;
	uint16_t wKeys = m_pPlayerPed->GetKeys(&lrAnalog, &udAnalog, &exKeys);

	INCAR_SYNC_DATA icSync;
	memset(&icSync, 0, sizeof(INCAR_SYNC_DATA));

	sa::CVehicle* pGtaVehicle = pPlayerPed->GetGtaVehicle();
	icSync.VehicleID = pVehiclePool->FindIDFromGtaPtr(pGtaVehicle);

	if (icSync.VehicleID == INVALID_VEHICLE_ID ||
			icSync.VehicleID >= MAX_VEHICLES) {
		return;
	}

	CVehicle* pVehicle = pVehiclePool->GetAt(icSync.VehicleID);
	if (!pVehicle || !pVehicle->m_pVehicle || !pVehicle->VerifyInstance()) {
		return;
	}

	icSync.lrAnalog = lrAnalog;
	icSync.udAnalog = udAnalog;
	icSync.wKeys = wKeys;

	RwMatrix mat = pVehicle->m_pVehicle->GetMatrix().ToRwMatrix();
	sa::CVector vecMoveSpeed = pVehicle->m_pVehicle->GetMoveSpeed();

	icSync.quat.SetFromMatrix(&mat);
	icSync.quat.Normalize();

	if (FloatOffset(icSync.quat.w, m_icSync.quat.w) < 0.00001f
			&& FloatOffset(icSync.quat.x, m_icSync.quat.x) < 0.00001f
			&& FloatOffset(icSync.quat.y, m_icSync.quat.y) < 0.00001f
			&& FloatOffset(icSync.quat.z, m_icSync.quat.z) < 0.00001f) {
		icSync.quat.Set(m_icSync.quat);
	}

	icSync.vecPos.x = mat.pos.x;
	icSync.vecPos.y = mat.pos.y;
	icSync.vecPos.z = mat.pos.z;

	icSync.vecMoveSpeed.x = vecMoveSpeed.x;
	icSync.vecMoveSpeed.y = vecMoveSpeed.y;
	icSync.vecMoveSpeed.z = vecMoveSpeed.z;

	icSync.fCarHealth = pVehicle->GetHealth();
	icSync.bytePlayerHealth = m_pPlayerPed->GetHealth();
	icSync.bytePlayerArmour = m_pPlayerPed->GetArmour();

	icSync.byteCurrentWeapon = (exKeys << 6) | (icSync.byteCurrentWeapon & 0x3F);
	icSync.byteCurrentWeapon ^=
			(icSync.byteCurrentWeapon ^ m_pPlayerPed->GetCurrentWeapon()) & 0x3F;
	icSync.TrailerID = 0;

	CVehicle* pTrailer = pVehicle->GetTrailer();
	if (pTrailer && pTrailer->m_pVehicle && pTrailer->GetTractor() == pVehicle) {
		pVehicle->SetTrailer(pTrailer);
		icSync.TrailerID = pVehiclePool->FindIDFromGtaPtr(pTrailer->m_pVehicle);
	}
	else {
		pVehicle->SetTrailer(nullptr);
	}

	if (pVehicle->GetModelIndex() == TRAIN_PASSENGER_LOCO || pVehicle->GetModelIndex() == TRAIN_FREIGHT_LOCO || pVehicle->GetModelIndex() == TRAIN_TRAM) {
		icSync.fTrainSpeed = pVehicle->GetTrainSpeed();
	}
	else if (pVehicle->GetVehicleSubtype() == VEHICLE_SUBTYPE_BIKE || pVehicle->GetVehicleSubtype() == VEHICLE_SUBTYPE_PUSHBIKE) {
		icSync.fTrainSpeed = pVehicle->GetBikeLean();
	}
	else if (pVehicle->GetModelIndex() == HYDRA) {
		*(uint32_t*) icSync.sHydraThrustAngle = pVehicle->GetHydraThrusters();
	}
	else {
		icSync.fTrainSpeed = 0.0f;
	}

	icSync.byteSirenOn = pVehicle->IsSirenOn();
	icSync.byteLandingGearState = pVehicle->GetLandingGearState();

	if (IsNeedSyncDataSend(&m_icSync, &icSync, sizeof(INCAR_SYNC_DATA), m_dwLastInCarSyncTick)) {
		proto::OutboundVehicleSync pkt{};
		pkt.vehicleId = icSync.VehicleID;
		pkt.lrAnalog = icSync.lrAnalog;
		pkt.udAnalog = icSync.udAnalog;
		pkt.keys = icSync.wKeys;
		pkt.quat = {icSync.quat.w, icSync.quat.x, icSync.quat.y, icSync.quat.z};
		pkt.position = {icSync.vecPos.x, icSync.vecPos.y, icSync.vecPos.z};
		pkt.moveSpeed = {icSync.vecMoveSpeed.x, icSync.vecMoveSpeed.y, icSync.vecMoveSpeed.z};
		pkt.carHealth = icSync.fCarHealth;
		pkt.playerHealth = icSync.bytePlayerHealth;
		pkt.playerArmour = icSync.bytePlayerArmour;
		pkt.currentWeapon = icSync.byteCurrentWeapon;
		pkt.sirenOn = icSync.byteSirenOn;
		pkt.landingGearState = icSync.byteLandingGearState;
		pkt.trailerId = icSync.TrailerID;
		memcpy(&pkt.trainOrHydra, &icSync.fTrainSpeed, sizeof(pkt.trainOrHydra));

		proto::Writer w(bsVehicleSync);
		pkt.serialize(w, proto::ProtoVersion::V037);
		pNetGame->GetRakClient()->Send(&bsVehicleSync, HIGH_PRIORITY, UNRELIABLE_SEQUENCED, true);

		memcpy(&m_icSync, &icSync, sizeof(INCAR_SYNC_DATA));
	}

	if (icSync.TrailerID && icSync.TrailerID < MAX_VEHICLES) {
		SendTrailerFullSyncData(icSync.TrailerID);
	}

	if (pVehicle->HasTurret() || (GetTickCount() - m_dwLastSendAimSyncTick) > 1000) {
		m_dwLastSendAimSyncTick = GetTickCount();
		SendAimSyncData();
	}
}

void CLocalPlayer::SendPassengerFullSyncData()
{
	RakNet::BitStream bsData;
	CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
	if (!pVehiclePool || !m_pPlayerPed || !m_pPlayerPed->m_ped) {
		return;
	}
	uint16_t lrAnalog, udAnalog;
	uint8_t exKeys;
	uint16_t wkeys = m_pPlayerPed->GetKeys(&lrAnalog, &udAnalog, &exKeys);

	PASSENGER_SYNC_DATA psSync;
	memset(&psSync, 0, sizeof(PASSENGER_SYNC_DATA));

	sa::CVehicle* pGtaVehicle = m_pPlayerPed->GetGtaVehicle();
	psSync.VehicleID = pVehiclePool->FindIDFromGtaPtr(pGtaVehicle);
	if (psSync.VehicleID == INVALID_VEHICLE_ID) {
		return;
	}

	psSync.lrAnalog = lrAnalog;
	psSync.udAnalog = udAnalog;
	psSync.wKeys = wkeys;

	psSync.bytePlayerHealth = m_pPlayerPed->GetHealth();
	psSync.bytePlayerArmour = m_pPlayerPed->GetArmour();

	psSync.byteSeatFlags ^= (psSync.byteSeatFlags ^ m_pPlayerPed->GetVehicleSeatID()) & 0x3F;
	uint8_t byteUnk = psSync.byteSeatFlags & 0x7F;
	if (m_pPlayerPed->IsCuffed()) byteUnk = psSync.byteSeatFlags | 0x80;
	psSync.byteSeatFlags = ((byteUnk ^ (m_bPassengerDriveByMode << 6)) & 0x40) ^ byteUnk;

	psSync.byteCurrentWeapon = (exKeys << 6) | (psSync.byteCurrentWeapon & 0x3F);
	psSync.byteCurrentWeapon ^= (psSync.byteCurrentWeapon ^ m_pPlayerPed->GetCurrentWeapon()) & 0x3F;

	RwMatrix mat = m_pPlayerPed->m_ped->GetMatrix().ToRwMatrix();
	psSync.vecPos.x = mat.pos.x;
	psSync.vecPos.y = mat.pos.y;
	psSync.vecPos.z = mat.pos.z;

	if (IsNeedSyncDataSend(&m_psSync, &psSync, sizeof(PASSENGER_SYNC_DATA), m_dwLastPassengerSyncTick)) {
		proto::OutboundPassengerSync pkt{};
		pkt.vehicleId = psSync.VehicleID;
		pkt.seatFlags = psSync.byteSeatFlags;
		pkt.currentWeapon = psSync.byteCurrentWeapon;
		pkt.playerHealth = psSync.bytePlayerHealth;
		pkt.playerArmour = psSync.bytePlayerArmour;
		pkt.lrAnalog = psSync.lrAnalog;
		pkt.udAnalog = psSync.udAnalog;
		pkt.keys = psSync.wKeys;
		pkt.position = {psSync.vecPos.x, psSync.vecPos.y, psSync.vecPos.z};

		proto::Writer w(bsData);
		pkt.serialize(w, proto::ProtoVersion::V037);
		pNetGame->GetRakClient()->Send(&bsData, HIGH_PRIORITY, UNRELIABLE_SEQUENCED, 1);
		memcpy(&m_psSync, &psSync, sizeof(PASSENGER_SYNC_DATA));
	}

	const uint32_t aimRate = m_bPassengerDriveByMode ? 100u : 1000u;
	if ((GetTickCount() - m_dwLastSendAimSyncTick) > aimRate) {
		m_dwLastSendAimSyncTick = GetTickCount();
		SendAimSyncData();
	}
}

void CLocalPlayer::SendTrailerFullSyncData(VEHICLEID vehicleId)
{
	RwMatrix matTrailer;

	TRAILER_SYNC_DATA trSync;
	memset(&trSync, 0, sizeof(TRAILER_SYNC_DATA));

	CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
	if (!pVehiclePool) {
		return;
	}

	CVehicle* pTrailer = pVehiclePool->GetAt(vehicleId);
	if (pTrailer && pTrailer->m_pVehicle && pTrailer->VerifyInstance()) {
		matTrailer = pTrailer->m_pVehicle->GetMatrix().ToRwMatrix();

		trSync.quat.SetFromMatrix(&matTrailer);
		trSync.quat.Normalize();

		trSync.trailerId = vehicleId;

		trSync.vecPos.x = matTrailer.pos.x;
		trSync.vecPos.y = matTrailer.pos.y;
		trSync.vecPos.z = matTrailer.pos.z;

		trSync.vecMoveSpeed = pTrailer->m_pVehicle->GetMoveSpeed();
		trSync.vecTurnSpeed = pTrailer->m_pVehicle->GetTurnSpeed();
		auto clampTurn = [](float v) -> float {
			if (!std::isfinite(v)) return 0.0f;
			return std::clamp(v, -1.0f, 1.0f);
		};
		trSync.vecTurnSpeed.x = clampTurn(trSync.vecTurnSpeed.x);
		trSync.vecTurnSpeed.y = clampTurn(trSync.vecTurnSpeed.y);
		trSync.vecTurnSpeed.z = clampTurn(trSync.vecTurnSpeed.z);

		if (IsNeedSyncDataSend(&m_trSync, &trSync, sizeof(TRAILER_SYNC_DATA), m_dwLastTrailerSyncTick)) {
			RakNet::BitStream bsTrailerSync;
			proto::OutboundTrailerSync pkt{};
			pkt.trailerId = trSync.trailerId;
			pkt.position = {trSync.vecPos.x, trSync.vecPos.y, trSync.vecPos.z};
			pkt.quat = {trSync.quat.w, trSync.quat.x, trSync.quat.y, trSync.quat.z};
			pkt.moveSpeed = {trSync.vecMoveSpeed.x, trSync.vecMoveSpeed.y, trSync.vecMoveSpeed.z};
			pkt.turnSpeed = {trSync.vecTurnSpeed.x, trSync.vecTurnSpeed.y, trSync.vecTurnSpeed.z};

			proto::Writer w(bsTrailerSync);
			pkt.serialize(w, proto::ProtoVersion::V037);
			pNetGame->GetRakClient()->Send(&bsTrailerSync, HIGH_PRIORITY, UNRELIABLE_SEQUENCED, 1);
			memcpy(&m_trSync, &trSync, sizeof(TRAILER_SYNC_DATA));
		}
	}
}

// Ported from reference CLocalPlayer::ProcessUnoccupiedSync: the local client
// claims unoccupied sync for a vehicle when it is the closest eligible player
// (or the recorded "ghost" passenger), then streams the vehicle state.
bool CLocalPlayer::ProcessUnoccupiedSync(VEHICLEID vehicleId, CVehicle* pVehicle)
{
	static const float fLocalUnoccupiedRange = 90.0f;

	if (!pNetGame || !pVehicle || !pVehicle->VerifyInstance()
			|| !m_pPlayerPed || !m_pPlayerPed->m_ped || pVehicle->IsATrainPart()) {
		return false;
	}

	sa::CVehicle* pEnt = pVehicle->m_pVehicle;

	bool bClear;
	if (pEnt->pDriver && pEnt->pDriver->m_nPedFlags.bInVehicle) {
		bClear = true;
	}
	else {
		float fDist = sa::DistanceBetweenPoints(pEnt->GetMatrix().m_pos,
				m_pPlayerPed->m_ped->GetMatrix().m_pos);
		bool bStationary = pEnt->GetMoveSpeed().Magnitude() < 0.01f
				&& pEnt->GetTurnSpeed().Magnitude() < 0.01f;
		bClear = fDist > fLocalUnoccupiedRange || bStationary || pEnt->m_pTowingVehicle != nullptr;
	}

	if (bClear) {
		pVehicle->m_bLocalUnoccupiedSync = false;
		pVehicle->m_bRemoteUnoccupiedSync = false;
		return false;
	}

	// ghost passengers: peds recorded in the seat array (player ped type) —
	// the local one claims sync authority for the vehicle
	for (int seat = 0; seat < 8; ++seat) {
		sa::CPed* pPassenger = pEnt->m_apPassengers[seat];
		if (pPassenger && pPassenger->m_nPedType == sa::PEDTYPE_PLAYER1) {
			if (pPassenger == m_pPlayerPed->m_ped) {
				SendUnoccupiedFullSyncData(vehicleId, (uint8_t)(seat + 1));
				return true;
			}
			return false;
		}
	}

	CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
	if (!pPlayerPool) {
		return false;
	}

	sa::CVector vecVehiclePos = pEnt->GetMatrix().m_pos;
	PLAYERID iClosest = INVALID_PLAYER_ID;
	int iLargest = pPlayerPool->m_iLastPlayerID;
	if (iLargest < pPlayerPool->m_LocalPlayerID) {
		iLargest = pPlayerPool->m_LocalPlayerID;
	}

	float fBest = 100000.0f;
	for (int index = 0; index <= iLargest; ++index) {
		sa::CPed* pPed;
		if (index == pPlayerPool->m_LocalPlayerID) {
			pPed = m_pPlayerPed->m_ped;
		}
		else {
			CRemotePlayer* pRemote = pPlayerPool->GetAt((PLAYERID) index);
			if (!pRemote || !pRemote->GetPlayerPed()) {
				continue;
			}
			pPed = pRemote->GetPlayerPed()->m_ped;
		}
		if (!pPed) {
			continue;
		}
		float fDist = sa::DistanceBetweenPoints(pPed->GetMatrix().m_pos, vecVehiclePos);
		if (iClosest == INVALID_PLAYER_ID || fDist < fBest) {
			fBest = fDist;
			iClosest = (PLAYERID) index;
		}
	}

	if (iClosest == pPlayerPool->m_LocalPlayerID && fBest <= fLocalUnoccupiedRange) {
		SendUnoccupiedFullSyncData(vehicleId, 0);
		return true;
	}

	return false;
}

void CLocalPlayer::SendUnoccupiedFullSyncData(VEHICLEID vehicleId, uint8_t byteSeat)
{
	CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
	if (!pVehiclePool) {
		return;
	}

	CVehicle* pVehicle = pVehiclePool->GetAt(vehicleId);
	if (!pVehicle || !pVehicle->VerifyInstance()) {
		return;
	}

	sa::CVehicle* pEnt = pVehicle->m_pVehicle;
	sa::CMatrix mat = pEnt->GetMatrix();

	// Orthonormalize vecRoll (right) & vecDirection (forward) in double precision
	// so open.mp's strict unit-length & orthogonality checks never drop the packet.
	double rx = mat.m_right.x, ry = mat.m_right.y, rz = mat.m_right.z;
	double rLen = std::sqrt(rx * rx + ry * ry + rz * rz);
	if (rLen > 1e-6 && std::isfinite(rLen)) {
		rx /= rLen; ry /= rLen; rz /= rLen;
	}
	else {
		rx = 1.0; ry = 0.0; rz = 0.0;
	}

	double fx = mat.m_forward.x, fy = mat.m_forward.y, fz = mat.m_forward.z;
	double dot = rx * fx + ry * fy + rz * fz;
	fx -= dot * rx; fy -= dot * ry; fz -= dot * rz;
	double fLen = std::sqrt(fx * fx + fy * fy + fz * fz);
	if (fLen > 1e-6 && std::isfinite(fLen)) {
		fx /= fLen; fy /= fLen; fz /= fLen;
	}
	else {
		fx = 0.0; fy = 1.0; fz = 0.0;
		rx = 1.0; ry = 0.0; rz = 0.0;
	}

	auto clampTurn = [](float v) -> float {
		if (!std::isfinite(v)) return 0.0f;
		return std::clamp(v, -1.0f, 1.0f);
	};

	UNOCCUPIED_SYNC_DATA ucSync;
	memset(&ucSync, 0, sizeof(UNOCCUPIED_SYNC_DATA));
	ucSync.vehicleId = vehicleId;
	ucSync.byteSeatId = byteSeat;
	ucSync.vecRoll = sa::CVector(static_cast<float>(rx), static_cast<float>(ry), static_cast<float>(rz));
	ucSync.vecDirection = sa::CVector(static_cast<float>(fx), static_cast<float>(fy), static_cast<float>(fz));
	ucSync.vecPos = mat.m_pos;
	ucSync.vecMoveSpeed = pEnt->GetMoveSpeed();
	ucSync.vecTurnSpeed = pEnt->GetTurnSpeed();
	ucSync.vecTurnSpeed.x = clampTurn(ucSync.vecTurnSpeed.x);
	ucSync.vecTurnSpeed.y = clampTurn(ucSync.vecTurnSpeed.y);
	ucSync.vecTurnSpeed.z = clampTurn(ucSync.vecTurnSpeed.z);
	ucSync.fCarHealth = pEnt->fHealth;

	// Per-vehicle 100ms throttle & 3/tick cap are already enforced in CVehiclePool::Process();
	// send directly matching PC localplayer.cpp:574-602 so slots aren't wasted.
	{
		RakNet::BitStream bsSend;
		proto::OutboundUnoccupiedSync pkt{};
		pkt.vehicleId = ucSync.vehicleId;
		pkt.seatId = ucSync.byteSeatId;
		pkt.roll = {ucSync.vecRoll.x, ucSync.vecRoll.y, ucSync.vecRoll.z};
		pkt.direction = {ucSync.vecDirection.x, ucSync.vecDirection.y, ucSync.vecDirection.z};
		pkt.position = {ucSync.vecPos.x, ucSync.vecPos.y, ucSync.vecPos.z};
		pkt.moveSpeed = {ucSync.vecMoveSpeed.x, ucSync.vecMoveSpeed.y, ucSync.vecMoveSpeed.z};
		pkt.turnSpeed = {ucSync.vecTurnSpeed.x, ucSync.vecTurnSpeed.y, ucSync.vecTurnSpeed.z};
		pkt.carHealth = ucSync.fCarHealth;

		proto::Writer w(bsSend);
		pkt.serialize(w, proto::ProtoVersion::V037);
		pNetGame->GetRakClient()->Send(&bsSend, HIGH_PRIORITY, UNRELIABLE_SEQUENCED, 1);
		memcpy(&m_ucSync, &ucSync, sizeof(UNOCCUPIED_SYNC_DATA));
		pVehicle->m_bLocalUnoccupiedSync = true;
	}
}

void CLocalPlayer::SendAimSyncData()
{
	if (!m_pPlayerPed || !m_pPlayerPed->m_ped || !pNetGame || !pNetGame->GetRakClient()) return;

	AIM_SYNC_DATA aimSync{};

	CAMERA_AIM* caAim = m_pPlayerPed->GetCurrentAim();
	if (!caAim) return;

	aimSync.byteCamMode = m_pPlayerPed->GetCameraMode();
	aimSync.vecAimf1.x = caAim->f1x;
	aimSync.vecAimf1.y = caAim->f1y;
	aimSync.vecAimf1.z = caAim->f1z;
	aimSync.vecAimPos.x = caAim->pos1x;
	aimSync.vecAimPos.y = caAim->pos1y;
	aimSync.vecAimPos.z = caAim->pos1z;

	aimSync.fAimZ = m_pPlayerPed->GetAimZ();

	aimSync.byteCamExtZoom = (uint8_t) (m_pPlayerPed->GetCameraExtendedZoom() * 63.0f) & 0x3F;
	aimSync.byteAspectRatio = (uint8_t) (GameGetAspectRatio() * 255.0f);

	sa::CWeapon* pwstWeapon = m_pPlayerPed->GetCurrentWeaponSlot();
	if (pwstWeapon) {
		if (pwstWeapon->dwState == 2) {
			aimSync.byteWeaponState = WS_RELOADING;
		}
		else {
			aimSync.byteWeaponState = (pwstWeapon->dwAmmoInClip > 1) ? WS_MORE_BULLETS : pwstWeapon->dwAmmoInClip;
		}
	}

	if ((GetTickCount() - m_dwLastAimDataSyncTick) > 500 || memcmp(&m_aimSync, &aimSync, sizeof(AIM_SYNC_DATA))) {
		m_dwLastAimDataSyncTick = GetTickCount();
		RakNet::BitStream bsAimSync;
		proto::OutboundAimSync pkt{};
		pkt.camMode = aimSync.byteCamMode;
		pkt.aimF1 = {aimSync.vecAimf1.x, aimSync.vecAimf1.y, aimSync.vecAimf1.z};
		pkt.aimPos = {aimSync.vecAimPos.x, aimSync.vecAimPos.y, aimSync.vecAimPos.z};
		pkt.aimZ = aimSync.fAimZ;
		pkt.zoomWeaponState = static_cast<uint8_t>(((aimSync.byteWeaponState & 0x03) << 6) | (aimSync.byteCamExtZoom & 0x3F));
		pkt.aspectRatio = aimSync.byteAspectRatio;

		proto::Writer w(bsAimSync);
		pkt.serialize(w, proto::ProtoVersion::V037);
		pNetGame->GetRakClient()->Send(&bsAimSync, HIGH_PRIORITY, UNRELIABLE_SEQUENCED, 1);
		memcpy(&m_aimSync, &aimSync, sizeof(AIM_SYNC_DATA));
	}
}

void CLocalPlayer::SendStatsUpdate()
{
	// Match PC localplayer.cpp: send unconditionally on the 1000ms cadence so an
	// unreliable drop never permanently desyncs server money/drunk/FPS tracking.
	if (!pGame || !m_pPlayerPed || !pNetGame || !pNetGame->GetRakClient()) return;

	m_statsSync.dwLastMoney = static_cast<uint32_t>(pGame->GetLocalMoney());
	m_statsSync.dwLastDrunkLevel = static_cast<uint32_t>(m_pPlayerPed->GetDrunkLevel());

	RakNet::BitStream bsStats;
	proto::OutboundStatsUpdate pkt{};
	pkt.money = pGame->GetLocalMoney();
	pkt.drunkLevel = m_pPlayerPed->GetDrunkLevel();
	proto::Writer w(bsStats);
	pkt.serialize(w, proto::ProtoVersion::V037);
	pNetGame->GetRakClient()->Send(&bsStats, HIGH_PRIORITY, UNRELIABLE, 0);
}

bool CLocalPlayer::CompareOnFootSyncKeys(uint16_t wKeys, uint16_t lrAnalog, uint16_t udAnalog)
{
	return wKeys != m_ofSync.wKeys || udAnalog != m_ofSync.udAnalog || lrAnalog != m_ofSync.lrAnalog;
}

bool CLocalPlayer::CompareInCarSyncKeys(uint16_t wKeys, uint16_t lrAnalog, uint16_t udAnalog)
{
	return wKeys != m_icSync.wKeys || udAnalog != m_icSync.udAnalog || lrAnalog != m_icSync.lrAnalog;
}

int CLocalPlayer::GetOptimumOnFootSendRate()
{
	if (!m_pPlayerPed) {
		return 1000;
	}
	if (pNetGame->session().lanMode) {
		return 15;
	}

	return pNetGame->session().onFootSendRate + pGame->GetPedSlotsUsed();
}

int CLocalPlayer::GetOptimumInCarSendRate()
{
	if (!m_pPlayerPed) {
		return 1000;
	}
	if (pNetGame->session().lanMode) {
		return 15;
	}

	return pNetGame->session().inCarSendRate + pGame->GetPedSlotsUsed();
}

bool CLocalPlayer::IsNeedSyncDataSend(const void* data1, const void* data2, size_t size, uint32_t& lastSyncTick)
{
	if (GetTickCount() - lastSyncTick <= 500 && !memcmp(data1, data2, size)) {
		return false;
	}

	lastSyncTick = GetTickCount();
	return true;
}
