#pragma once

#include "NetTypes.h"
#include "Session.h"
// Kept lightweight (zero RakNet/Game/UI includes) to satisfy frozen Cef/ callers
// that dereference GetPlayerPool()->GetLocalPlayerID() and GetObjectPool()->GetAt().
#include "PlayerPool.h"
#include "ObjectPool.h"

class RakClientInterface;
struct Packet;

class CVehiclePool;
class CActorPool;
class CPickupPool;
class CTextLabelPool;
class CTextDrawPool;
class CPlayerBubblePool;
class CGangZonePool;
class CMenuPool;

class NetGame {
public:
	NetGame(const char* host, int port, const char* playerName, const char* password);
	~NetGame();

	int GetGameState() { return m_gameState; }
	void SetGameState(int gameState) { m_gameState = gameState; }

	RakClientInterface* GetRakClient() { return m_rakClient; }

	CPlayerPool* GetPlayerPool() { return m_pools->playerPool; }
	CVehiclePool* GetVehiclePool() { return m_pools->vehiclePool; }
	CObjectPool* GetObjectPool() { return m_pools->objectPool; }
	CActorPool* GetActorPool() { return m_pools->actorPool; }
	CPickupPool* GetPickupPool() { return m_pools->pickupPool; }
	CTextLabelPool* GetTextLabelPool() { return m_pools->textLabelPool; }
	CTextDrawPool* GetTextDrawPool() { return m_pools->textDrawPool; }
	CPlayerBubblePool* GetPlayerBubblePool() { return m_pools->playerBubblePool; }
	CGangZonePool* GetGangZonePool() { return m_pools->gangZonePool; }
	CMenuPool* GetMenuPool() { return m_pools->menuPool; }

	void Process();

	SessionState& session() { return m_session; }
	const SessionState& session() const { return m_session; }

	void ShutdownForGameModeRestart(SessionState::ResetReason reason = SessionState::ResetReason::Restart);
	void UpdatePlayerScoresAndPings();
	void InitGameLogic();

	void SetMapIcon(uint8_t byteIconID, float fPosX, float fPosY, float fPosZ, uint8_t byteType, uint32_t dwColor, uint8_t byteStyle);
	void DisableMapIcon(uint8_t byteIconID);

	void SendDialogResponse(uint16_t wDialogID, uint8_t byteButtonID, uint16_t wListBoxItem, const char* szInput);

	void SendPrevClass();
	void SendSpawn();
	void SendNextClass();

	void SendChatMessage(const char* szMessage);
	void SendChatCommand(const char* szCommand);

private:
	void InitializePools();
	void UninitializePools();
	void UpdateNetwork();
	void ProcessPools();
	void ProcessLoadingScreen();
	void ProcessConnecting();

	void Packet_AuthKey(Packet* pkt);
	void Packet_ConnectAttemptFailed(Packet* pkt);
	void Packet_NoFreeIncomingConnections(Packet* pkt);
	void Packet_DisconnectionNotification(Packet* pkt);
	void Packet_ConnectionSucceeded(Packet* pkt);
	void Packet_FailedInitializeEncription(Packet* pkt);
	void Packet_ConnectionBanned(Packet* pkt);
	void Packet_InvalidPassword(Packet* pkt);
	void Packet_ConnectionLost(Packet* pkt);
	void Packet_PlayerSync(Packet* pkt);
	void Packet_VehicleSync(Packet* pkt);
	void Packet_PassengerSync(Packet* pkt);
	void Packet_AimSync(Packet* pkt);
	void Packet_BulletSync(Packet* pkt);
	void Packet_MarkerSync(Packet* pkt);
	void Packet_UnoccupiedSync(Packet* pkt);
	void Packet_TrailerSync(Packet* pkt);

	void ResetVehiclePool();
	void ResetObjectPool();
	void ResetActorPool();
	void ResetPickupPool();
	void ResetTextLabelPool();
	void ResetTextDrawPool();
	void ResetPlayerBubblePool();
	void ResetGangZonePool();
	void ResetMapIcons();
	void ResetMenuPool();

	void ProcessDlArtworkHandshake();

public:
	void NotifyDlVirtualWorld(int32_t virtualWorld);
	void ResetDlArtworkState();

	using NET_SETTINGS = ServerSettings;

	char m_host[256 + 1];
	int m_port;

	NET_SETTINGS* m_netSet;

private:
	SessionState m_session;
	RakClientInterface* m_rakClient;

	int m_gameState;
	int m_lastConnectAttempt;
	uint32_t m_mapIcon[MAX_MAP_ICONS];

	struct NET_POOLS {
		CPlayerPool* playerPool;
		CVehiclePool* vehiclePool;
		CObjectPool* objectPool;
		CActorPool* actorPool;
		CPickupPool* pickupPool;
		CTextLabelPool* textLabelPool;
		CTextDrawPool* textDrawPool;
		CPlayerBubblePool* playerBubblePool;
		CGangZonePool* gangZonePool;
		CMenuPool* menuPool;
	} * m_pools;
};
