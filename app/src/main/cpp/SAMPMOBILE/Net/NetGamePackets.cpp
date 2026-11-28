#include "NetGame.h"
#include "LocalPlayer.h"
#include "RemotePlayer.h"
#include "VehiclePool.h"
#include "SessionHooks.h"
#include "Proto/Gpci.h"
#include "Proto/RpcOutbound.h"
#include "Proto/Sync.h"
#include "../Client.h"
#include "../Game/Game.h"
#include "../Game/AudioStream.h"
#include "../Game/Util.h"
#include "../UI/UI.h"
#include "../UI/SAMPWidgets/Chat.h"
#include <RakNet/RakClientInterface.h>
#include <RakNet/PacketEnumerations.h>
#include <RakNet/SAMP/SAMPRPC.h>
#include <RakNet/SAMP/samp_auth.h>
#include <spdlog/spdlog.h>
#include <cstdio>
#include <fstream>
#include <random>
#include "../L10n.h"

namespace {

std::string loadOrCreateDeviceGpci()
{
	static std::string cachedGpci;
	if (!cachedGpci.empty()) {
		return cachedGpci;
	}

	std::string seed;
	const std::string dir = Client::gameDir();
	const std::string seedPath = dir.empty() ? std::string() : (dir + "/gpci_seed.dat");

	if (!seedPath.empty()) {
		std::ifstream in(seedPath, std::ios::binary);
		if (in.is_open()) {
			std::getline(in, seed);
			while (!seed.empty() && (seed.back() == '\r' || seed.back() == '\n' || seed.back() == ' ')) {
				seed.pop_back();
			}
		}
	}

	if (seed.size() < 16) {
		uint8_t rnd[32]{};
		bool gotRandom = false;
		if (FILE* f = std::fopen("/dev/urandom", "rb")) {
			gotRandom = (std::fread(rnd, 1, sizeof(rnd), f) == sizeof(rnd));
			std::fclose(f);
		}
		if (!gotRandom) {
			std::random_device rd;
			const uint64_t t = static_cast<uint64_t>(
				std::chrono::high_resolution_clock::now().time_since_epoch().count());
			for (size_t i = 0; i < sizeof(rnd); i += 4) {
				const uint32_t v = rd() ^ static_cast<uint32_t>(t >> ((i % 8) * 4));
				rnd[i + 0] = static_cast<uint8_t>(v & 0xFFu);
				rnd[i + 1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
				rnd[i + 2] = static_cast<uint8_t>((v >> 16) & 0xFFu);
				rnd[i + 3] = static_cast<uint8_t>((v >> 24) & 0xFFu);
			}
		}

		static const char kHex[] = "0123456789ABCDEF";
		seed.clear();
		seed.reserve(sizeof(rnd) * 2);
		for (uint8_t b : rnd) {
			seed.push_back(kHex[b >> 4]);
			seed.push_back(kHex[b & 0x0Fu]);
		}

		if (!seedPath.empty()) {
			const std::string tmpPath = seedPath + ".tmp";
			std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
			if (out.is_open()) {
				out.write(seed.data(), static_cast<std::streamsize>(seed.size()));
				out.flush();
				const bool ok = out.good();
				out.close();
				if (ok) {
					std::rename(tmpPath.c_str(), seedPath.c_str());
				}
				else {
					std::remove(tmpPath.c_str());
				}
			}
		}
	}

	cachedGpci = proto::generateGpci(seed);
	spdlog::info("NetGame: device GPCI initialized (len={})", cachedGpci.size());
	return cachedGpci;
}

} // namespace

void NetGame::Packet_AuthKey(Packet* pkt)
{
	if (!pkt || !pkt->data || pkt->length < 1) return;
	if (GetGameState() < GAMESTATE_WAIT_CONNECT || GetGameState() > GAMESTATE_AWAIT_JOIN) {
		return;
	}

	proto::Reader reader(pkt->data, pkt->length * 8);
	proto::AuthKeyPacket req{};
	const auto ver = proto::versionFromNetgame(static_cast<uint32_t>(Client::netgameVersion()));
	if (!req.serialize(reader, ver)) {
		return;
	}

	const char* auth_key = nullptr;

	for (auto& x : AuthKeyTable) {
		if (!strcmp(req.key.c_str(), x[0])) {
			auth_key = x[1];
			break;
		}
	}

	char szGenKey[260];
	if (!auth_key) {
		// table only covers 0.3.7 challenge names; 0.3.DL (and newer 0.3.7)
		// servers may send anything - compute the real algorithm
		gen_auth_key(szGenKey, req.key.c_str());
		auth_key = szGenKey;
		spdlog::info("[AUTH] challenge not in table, computed: {}", req.key);
	}

	RakNet::BitStream bsKey;
	proto::Writer writer(bsKey);
	proto::AuthKeyPacket resp{};
	resp.key = auth_key;
	resp.serialize(writer, ver);

	m_rakClient->Send(&bsKey, SYSTEM_PRIORITY, RELIABLE, NULL);

	spdlog::info("[AUTH] {} -> {}", req.key, auth_key);
}

void NetGame::Packet_ConnectAttemptFailed(Packet* pkt)
{
	if (pUI) {
		pUI->chat()->addDebugMessage(L10N("The server didn't respond. Retrying..", "服务器未响应，正在重试.."));
	}

	if (pAudioStream) {
		pAudioStream->Stop(true);
	}

	SessionHooks::NotifyHideOverlay();

	SetGameState(GAMESTATE_WAIT_CONNECT);
}

void NetGame::Packet_NoFreeIncomingConnections(Packet* pkt)
{
	if (pUI) {
		pUI->chat()->addDebugMessage(L10N("The server is full. Retrying..", "服务器已满，正在重试.."));
	}

	SessionHooks::NotifyHideOverlay();

	SetGameState(GAMESTATE_WAIT_CONNECT);
}

void NetGame::Packet_DisconnectionNotification(Packet* pkt)
{
	if (pUI) {
		pUI->chat()->addDebugMessage(L10N("Server closed the connection.", "服务器关闭了连接。"));
	}

	if (pAudioStream) {
		pAudioStream->Stop(true);
	}

	SessionHooks::NotifyDisconnect();

	m_rakClient->Disconnect(100);
}

void NetGame::Packet_ConnectionSucceeded(Packet* pkt)
{
	if (!pkt || !pkt->data || pkt->length < 1) return;

	const int iVersion = Client::netgameVersion();
	const auto ver = proto::versionFromNetgame(static_cast<uint32_t>(iVersion));
	proto::Reader reader(pkt->data, pkt->length * 8);
	proto::ConnectionSucceededPacket succ{};
	if (!succ.serialize(reader, ver)) {
		return;
	}

	uint8_t byteMod = 1;
	unsigned int uiChallenge = succ.challenge ^ static_cast<unsigned int>(iVersion);

	if (pUI) {
		pUI->chat()->addDebugMessage(L10N("Connected. Joining the game...", "已连接，正在进入游戏..."));
	}

	SetGameState(GAMESTATE_AWAIT_JOIN);

	RakNet::BitStream bsSend;
	proto::Writer writer(bsSend);
	proto::ClientJoin join{};
	join.version = iVersion;
	join.mod = byteMod;
	join.nickname = GetPlayerPool()->GetLocalPlayerName();
	join.challengeResponse = uiChallenge;
	join.authKey = loadOrCreateDeviceGpci();
	join.clientVersion = Client::sampVersion();
	join.serialize(writer, ver);

	SessionHooks::NotifyClientJoinRpc(RPC_ClientJoin, bsSend);

	m_rakClient->RPC(&RPC_ClientJoin, &bsSend, HIGH_PRIORITY, RELIABLE, 0, false, UNASSIGNED_NETWORK_ID, nullptr);
}

void NetGame::Packet_FailedInitializeEncription(Packet* pkt)
{
	if (pUI) {
		pUI->chat()->addDebugMessage(L10N("Failed to initialize encryption.", "初始化加密失败。"));
	}
}

void NetGame::Packet_ConnectionBanned(Packet* pkt)
{
	if (pUI) {
		pUI->chat()->addDebugMessage(L10N("You are banned from this server.", "你已被此服务器封禁。"));
	}
	if (pAudioStream) {
		pAudioStream->Stop(true);
	}
	SessionHooks::NotifyDisconnect();
	m_rakClient->Disconnect(0);
}

void NetGame::Packet_InvalidPassword(Packet* pkt)
{
	if (pUI) {
		pUI->chat()->addDebugMessage(L10N("Wrong server password.", "服务器密码错误。"));
	}
	if (pAudioStream) {
		pAudioStream->Stop(true);
	}
	SessionHooks::NotifyDisconnect();
	m_rakClient->Disconnect(0);
}

void NetGame::Packet_ConnectionLost(Packet* pkt)
{
	if (m_rakClient) {
		m_rakClient->Disconnect(0);
	}

	if (pUI) {
		pUI->chat()->addDebugMessage(L10N("Lost connection to the server. Reconnecting..", "与服务器失去连接，正在重连.."));
	}

	ShutdownForGameModeRestart(SessionState::ResetReason::Disconnect);

	CPlayerPool* pPlayerPool = GetPlayerPool();
	if (pPlayerPool) {
		for (PLAYERID playerId = 0; playerId < MAX_PLAYERS; playerId++) {
			if (pPlayerPool->GetSlotState(playerId)) {
				pPlayerPool->Delete(playerId, 0);
			}
		}
	}

	if (pAudioStream) {
		pAudioStream->Stop(true);
	}

	SetGameState(GAMESTATE_WAIT_CONNECT);
}

namespace {
inline bool isFiniteVec3(float x, float y, float z) {
	return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
}
inline bool isFiniteQuat(float x, float y, float z, float w) {
	return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) && std::isfinite(w);
}
}

void NetGame::Packet_PlayerSync(Packet* pkt)
{
	if (GetGameState() != GAMESTATE_CONNECTED) return;
	if (!pkt || !pkt->data || pkt->length < 1) return;

	proto::Reader reader(pkt->data, pkt->length * 8);
	proto::InboundPlayerSync p{};
	if (!p.serialize(reader, proto::ProtoVersion::V037)) return;
	if (!isFiniteVec3(p.position.x, p.position.y, p.position.z)
			|| !isFiniteQuat(p.quat.x, p.quat.y, p.quat.z, p.quat.w)
			|| !isFiniteVec3(p.moveSpeed.x, p.moveSpeed.y, p.moveSpeed.z)) {
		return;
	}

	ONFOOT_SYNC_DATA ofSync{};
	ofSync.lrAnalog = p.hasLR ? p.lrAnalog : 0;
	ofSync.udAnalog = p.hasUD ? p.udAnalog : 0;
	ofSync.wKeys = p.keys;
	ofSync.vecPos = sa::CVector(p.position.x, p.position.y, p.position.z);
	ofSync.quat.Set(p.quat.x, p.quat.y, p.quat.z, p.quat.w);
	ofSync.byteHealth = p.health;
	ofSync.byteArmour = p.armour;
	ofSync.byteCurrentWeapon = p.currentWeapon & 0x3F;
	ofSync.byteSpecialAction = p.specialAction;
	ofSync.vecMoveSpeed = sa::CVector(p.moveSpeed.x, p.moveSpeed.y, p.moveSpeed.z);
	ofSync.wSurfInfo = p.hasSurfing ? p.surfInfo : INVALID_VEHICLE_ID;
	ofSync.vecSurfOffsets = p.hasSurfing ? sa::CVector(p.surfOffsets.x, p.surfOffsets.y, p.surfOffsets.z) : sa::CVector{};
	ofSync.dwAnimation = p.hasAnim ? p.animation : 0x80000000u;

	CPlayerPool* pPlayerPool = GetPlayerPool();
	if (!pPlayerPool) return;
	CRemotePlayer* pRemotePlayer = pPlayerPool->GetAt(p.playerId);
	if (pRemotePlayer) {
		pRemotePlayer->StoreOnFootFullSyncData(&ofSync, p.timestamp);
	}
}
// vehicle sync
void NetGame::Packet_VehicleSync(Packet* pkt)
{
	if (GetGameState() != GAMESTATE_CONNECTED) return;
	if (!pkt || !pkt->data || pkt->length < 1) return;

	proto::Reader reader(pkt->data, pkt->length * 8);
	proto::InboundVehicleSync p{};
	if (!p.serialize(reader, proto::ProtoVersion::V037)) return;
	if (!isFiniteVec3(p.position.x, p.position.y, p.position.z)
			|| !isFiniteQuat(p.quat.x, p.quat.y, p.quat.z, p.quat.w)
			|| !isFiniteVec3(p.moveSpeed.x, p.moveSpeed.y, p.moveSpeed.z)) {
		return;
	}

	INCAR_SYNC_DATA icSync{};
	icSync.VehicleID = p.vehicleId;
	icSync.lrAnalog = p.lrAnalog;
	icSync.udAnalog = p.udAnalog;
	icSync.wKeys = p.keys;
	icSync.quat.Set(p.quat.x, p.quat.y, p.quat.z, p.quat.w);
	icSync.vecPos = sa::CVector(p.position.x, p.position.y, p.position.z);
	icSync.vecMoveSpeed = sa::CVector(p.moveSpeed.x, p.moveSpeed.y, p.moveSpeed.z);
	icSync.fCarHealth = static_cast<float>(p.carHealth);
	icSync.bytePlayerHealth = p.playerHealth;
	icSync.bytePlayerArmour = p.playerArmour;
	icSync.byteCurrentWeapon = p.currentWeapon & 0x3F;
	icSync.byteSirenOn = p.sirenOn ? 1 : 0;
	icSync.byteLandingGearState = p.landingGearState ? 1 : 0;
	if (p.hasTrainOrHydra) {
		memcpy(&icSync.fTrainSpeed, &p.trainOrHydra, sizeof(icSync.fTrainSpeed));
		if (!std::isfinite(icSync.fTrainSpeed)) {
			icSync.fTrainSpeed = 0.0f;
		}
	}
	if (p.hasTrailer) {
		icSync.TrailerID = p.trailerId;
	}

	CPlayerPool* pPlayerPool = GetPlayerPool();
	if (!pPlayerPool) return;
	CRemotePlayer* pRemotePlayer = pPlayerPool->GetAt(p.playerId);
	if (pRemotePlayer) {
		pRemotePlayer->StoreInCarFullSyncData(&icSync, p.timestamp);
	}
}

void NetGame::Packet_AimSync(Packet* pkt)
{
	if (GetGameState() != GAMESTATE_CONNECTED || !pkt || !pkt->data || pkt->length < 1) return;

	proto::Reader reader(pkt->data, pkt->length * 8);
	proto::InboundAimSync p{};
	if (!p.serialize(reader, proto::ProtoVersion::V037)) return;
	if (!isFiniteVec3(p.aimF1.x, p.aimF1.y, p.aimF1.z)
			|| !isFiniteVec3(p.aimPos.x, p.aimPos.y, p.aimPos.z)
			|| !std::isfinite(p.aimZ)) {
		return;
	}

	AIM_SYNC_DATA aimSync{};
	aimSync.byteCamMode = p.camMode;
	aimSync.vecAimf1 = sa::CVector(p.aimF1.x, p.aimF1.y, p.aimF1.z);
	aimSync.vecAimPos = sa::CVector(p.aimPos.x, p.aimPos.y, p.aimPos.z);
	aimSync.fAimZ = p.aimZ;
	aimSync.byteCamExtZoom = p.zoomWeaponState & 0x3F;
	aimSync.byteWeaponState = (p.zoomWeaponState >> 6) & 0x03;
	aimSync.byteAspectRatio = p.aspectRatio;

	CPlayerPool* pPlayerPool = GetPlayerPool();
	CRemotePlayer* pPlayer = pPlayerPool ? pPlayerPool->GetAt(p.playerId) : nullptr;
	if (pPlayer) {
		pPlayer->StoreAimFullSyncData(&aimSync);
	}
}

void NetGame::Packet_BulletSync(Packet* pkt)
{
	if (GetGameState() != GAMESTATE_CONNECTED || !pkt || !pkt->data || pkt->length < 1) return;

	proto::Reader reader(pkt->data, pkt->length * 8);
	proto::InboundBulletSync p{};
	if (!p.serialize(reader, proto::ProtoVersion::V037)) return;

	BULLET_SYNC_DATA btSync{};
	btSync.targetType = p.targetType;
	btSync.targetId = p.targetId;
	btSync.vecOrigin = sa::CVector(p.origin.x, p.origin.y, p.origin.z);
	btSync.vecPos = sa::CVector(p.pos.x, p.pos.y, p.pos.z);
	btSync.vecOffset = sa::CVector(p.offset.x, p.offset.y, p.offset.z);
	btSync.weaponId = p.weaponId;

	CPlayerPool* pPlayerPool = GetPlayerPool();
	CRemotePlayer* pRemotePlayer = pPlayerPool ? pPlayerPool->GetAt(p.playerId) : nullptr;
	if (pRemotePlayer) {
		pRemotePlayer->StoreBulletFullSyncData(&btSync);
	}
}

void NetGame::Packet_PassengerSync(Packet* pkt)
{
	if (GetGameState() != GAMESTATE_CONNECTED || !pkt || !pkt->data || pkt->length < 1) return;

	proto::Reader reader(pkt->data, pkt->length * 8);
	proto::InboundPassengerSync p{};
	if (!p.serialize(reader, proto::ProtoVersion::V037)) return;
	if (!isFiniteVec3(p.position.x, p.position.y, p.position.z)) return;

	PASSENGER_SYNC_DATA psSync{};
	psSync.VehicleID = p.vehicleId;
	psSync.byteSeatFlags = p.seatFlags;
	psSync.byteCurrentWeapon = p.currentWeapon;
	psSync.bytePlayerHealth = p.playerHealth;
	psSync.bytePlayerArmour = p.playerArmour;
	psSync.lrAnalog = p.lrAnalog;
	psSync.udAnalog = p.udAnalog;
	psSync.wKeys = p.keys;
	psSync.vecPos = sa::CVector(p.position.x, p.position.y, p.position.z);

	CPlayerPool* pPlayerPool = GetPlayerPool();
	CRemotePlayer* pRemotePlayer = pPlayerPool ? pPlayerPool->GetAt(p.playerId) : nullptr;
	if (pRemotePlayer) {
		pRemotePlayer->StorePassengerFullSyncData(&psSync);
	}
}

void NetGame::Packet_MarkerSync(Packet* pkt)
{
	if (GetGameState() != GAMESTATE_CONNECTED || !pkt || !pkt->data || pkt->length < 1) return;

	CPlayerPool* pPlayerPool = GetPlayerPool();
	if (!pPlayerPool) return;

	proto::Reader reader(pkt->data, pkt->length * 8);
	proto::InboundMarkersSync p{};
	if (!p.serialize(reader, proto::ProtoVersion::V037)) return;

	for (const auto& entry : p.entries) {
		if (entry.playerId < MAX_PLAYERS && pPlayerPool->GetSlotState(entry.playerId)) {
			CRemotePlayer* pRemotePlayer = pPlayerPool->GetAt(entry.playerId);
			if (pRemotePlayer) {
				if (entry.active) {
					pRemotePlayer->ShowGlobalMarker(entry.x, entry.y, entry.z);
				}
				else {
					pRemotePlayer->HideGlobalMarker();
				}
			}
		}
	}
}

void NetGame::Packet_TrailerSync(Packet* pkt)
{
	if (GetGameState() != GAMESTATE_CONNECTED || !pkt || !pkt->data || pkt->length < 1) return;

	proto::Reader reader(pkt->data, pkt->length * 8);
	proto::InboundTrailerSync p{};
	if (!p.serialize(reader, proto::ProtoVersion::V037)) return;
	if (!isFiniteVec3(p.position.x, p.position.y, p.position.z)
			|| !isFiniteQuat(p.quat.x, p.quat.y, p.quat.z, p.quat.w)
			|| !isFiniteVec3(p.moveSpeed.x, p.moveSpeed.y, p.moveSpeed.z)
			|| !isFiniteVec3(p.turnSpeed.x, p.turnSpeed.y, p.turnSpeed.z)) {
		return;
	}

	TRAILER_SYNC_DATA trSync{};
	trSync.trailerId = p.trailerId;
	trSync.vecPos = sa::CVector(p.position.x, p.position.y, p.position.z);
	trSync.quat.Set(p.quat.x, p.quat.y, p.quat.z, p.quat.w);
	trSync.vecMoveSpeed = sa::CVector(p.moveSpeed.x, p.moveSpeed.y, p.moveSpeed.z);
	trSync.vecTurnSpeed = sa::CVector(p.turnSpeed.x, p.turnSpeed.y, p.turnSpeed.z);

	if (GetPlayerPool()) {
		CRemotePlayer* pPlayer = GetPlayerPool()->GetAt(p.playerId);
		if (pPlayer) {
			pPlayer->StoreTrailerFullSyncData(&trSync);
		}
	}
}

void NetGame::Packet_UnoccupiedSync(Packet* pkt)
{
	if (GetGameState() != GAMESTATE_CONNECTED || !pkt || !pkt->data || pkt->length < 1) return;

	proto::Reader reader(pkt->data, pkt->length * 8);
	proto::InboundUnoccupiedSync p{};
	if (!p.serialize(reader, proto::ProtoVersion::V037)) return;

	UNOCCUPIED_SYNC_DATA unocSync{};
	unocSync.vehicleId = p.vehicleId;
	unocSync.byteSeatId = p.seatId;
	unocSync.vecRoll = sa::CVector(p.roll.x, p.roll.y, p.roll.z);
	unocSync.vecDirection = sa::CVector(p.direction.x, p.direction.y, p.direction.z);
	unocSync.vecPos = sa::CVector(p.position.x, p.position.y, p.position.z);
	unocSync.vecMoveSpeed = sa::CVector(p.moveSpeed.x, p.moveSpeed.y, p.moveSpeed.z);
	unocSync.vecTurnSpeed = sa::CVector(p.turnSpeed.x, p.turnSpeed.y, p.turnSpeed.z);
	unocSync.fCarHealth = p.carHealth;

	if (GetPlayerPool()) {
		CRemotePlayer* pPlayer = GetPlayerPool()->GetAt(p.playerId);
		if (pPlayer) {
			pPlayer->StoreUnoccupiedFullSyncData(&unocSync);
		}
	}
}
