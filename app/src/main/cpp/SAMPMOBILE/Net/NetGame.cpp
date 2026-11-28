#include "NetGame.h"
#include "NetCap.h"
#include "RpcTable.h"
#include "LocalPlayer.h"
#include "RemotePlayer.h"
#include "VehiclePool.h"
#include "ActorPool.h"
#include "PickupPool.h"
#include "TextLabelPool.h"
#include "TextDrawPool.h"
#include "PlayerBubblePool.h"
#include "GangZonePool.h"
#include "MenuPool.h"
#include "SessionHooks.h"
#include "Proto/Gpci.h"
#include "Proto/RpcOutbound.h"
#include "Proto/Sync.h"
#include "../Client.h"
#include "../Game/Game.h"
#include "../Game/Artwork/ArtworkManager.h"
#include "../Game/Animation.h"
#include "../Game/AudioStream.h"
#include "../Game/Camera.h"
#include "../Game/Object.h"
#include "../Game/PlayerPed.h"
#include "../Game/RadarColors.h"
#include "../Game/Scripting.h"
#include "../Game/Util.h"
#include "../UI/UI.h"
#include "../UI/SAMPWidgets/ButtonPanel.h"
#include "../UI/SAMPWidgets/Chat.h"
#include "../UI/SAMPWidgets/Keyboard.h"
#include "../UI/SAMPWidgets/NetStats.h"
#include "../UI/SAMPWidgets/Scoreboard.h"
#include "../UI/SAMPWidgets/Spawn.h"
#include "../L10n.h"
#include "../UI/SAMPWidgets/SplashScreen.h"
#include "../UI/SAMPWidgets/VoiceButton.h"
#include "../Java/Java.h"
#include "../Java/EditObject.h"
#include <RakNet/RakClientInterface.h>
#include <RakNet/RakNetworkFactory.h>
#include <RakNet/PacketEnumerations.h>
#include <RakNet/StringCompressor.h>
#include <RakNet/SAMP/SAMPRPC.h>
#include <RakNet/SAMP/NetCapHook.h>
#include <RakNet/SAMP/samp_auth.h>
#include <encoding.h>
#include <spdlog/spdlog.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <random>

using MonetNetGameCallback = void (*)(void* rakClient, const char* host, int port);
static std::mutex g_monetNetGameMutex;
static MonetNetGameCallback g_monetNetGameCb = nullptr;
static void* g_monetLastAttachedRakClient = nullptr;

static void notifyMonetNetGameLocked(void* rakClient, const char* host, int port)
{
	if (!g_monetNetGameCb || !rakClient || g_monetLastAttachedRakClient == rakClient) {
		return;
	}
	g_monetLastAttachedRakClient = rakClient;
	g_monetNetGameCb(rakClient, host ? host : "", port);
}

extern "C" __attribute__((visibility("default"))) void SAMPMOBILE_RegisterNetGameCallback(MonetNetGameCallback cb)
{
	std::lock_guard<std::mutex> lock(g_monetNetGameMutex);
	g_monetNetGameCb = cb;
	if (!cb) {
		g_monetLastAttachedRakClient = nullptr;
		return;
	}
	if (pNetGame && pNetGame->GetRakClient()) {
		notifyMonetNetGameLocked(pNetGame->GetRakClient(), pNetGame->m_host, pNetGame->m_port);
	}
}

unsigned char GetPacketID(Packet* p)
{
	if (p == 0 || p->data == 0 || p->length == 0) return 255;

	if ((unsigned char) p->data[0] == ID_TIMESTAMP) {
		if (p->length <= sizeof(unsigned char) + sizeof(RakNetTime)) return 255;
		return (unsigned char) p->data[sizeof(unsigned char) + sizeof(RakNetTime)];
	}
	else {
		return (unsigned char) p->data[0];
	}
}

namespace {

void detachCustomPedAssets(CPlayerPed* ped, const char* who)
{
	if (!ped) return;
	for (int i = 0; i < 10; ++i) {
		if (ped->m_attachedObjects[i].slotUsed && ped->m_attachedObjects[i].object
			&& artwork::isInternalCustomSlot(ped->m_attachedObjects[i].object->GetModelIndex())) {
			ped->RemoveAttachedObject(i);
		}
	}
	if (ped->m_ped && artwork::isInternalCustomSlot(ped->m_ped->m_nModelIndex)) {
		spdlog::info("NetGame: reverting {} from custom slot {} to fallback skin", who, ped->m_ped->m_nModelIndex);
		ped->RevertCustomSkinBeforeUnload();
	}
}

void refreshCustomPedAssets(CPlayerPed* ped)
{
	if (!ped) return;
	auto& art = artwork::ArtworkManager::instance();
	ped->RefreshCustomSkin();
	for (int i = 0; i < 10; ++i) {
		if (ped->m_attachedObjects[i].slotUsed
			&& artwork::isCustomObjectId(ped->m_attachedObjects[i].info.iModel)
			&& ped->m_attachedObjects[i].object
			&& !artwork::isInternalCustomSlot(ped->m_attachedObjects[i].object->GetModelIndex())) {
			if (art.ensureCustomObjectLoaded(ped->m_attachedObjects[i].info.iModel) >= 0) {
				NEW_ATTACHED_OBJECT copy = ped->m_attachedObjects[i].info;
				ped->SetAttachedObject(i, &copy);
			}
		}
	}
}

void onArtworkBeforeUnloadModels()
{
	if (pGame) {
		detachCustomPedAssets(pGame->FindPlayerPed(), "local ped");
	}
	if (!pNetGame) return;

	if (CPlayerPool* pp = pNetGame->GetPlayerPool()) {
		for (PLAYERID i = 0; i < MAX_PLAYERS; ++i) {
			if (!pp->GetSlotState(i)) continue;
			if (CRemotePlayer* rp = pp->GetAt(i)) {
				detachCustomPedAssets(rp->GetPlayerPed(), "remote ped");
			}
		}
	}
	if (CActorPool* ap = pNetGame->GetActorPool()) {
		for (PLAYERID i = 0; i < MAX_ACTORS; ++i) {
			if (CActor* actor = ap->GetAt(i)) {
				actor->RevertCustomSkinBeforeUnload();
			}
		}
	}
	if (CObjectPool* op = pNetGame->GetObjectPool()) {
		op->RevertCustomModelsBeforeUnload();
	}
	if (CPickupPool* pk = pNetGame->GetPickupPool()) {
		pk->DestroyCustomModelsBeforeUnload();
	}
	if (CTextDrawPool* td = pNetGame->GetTextDrawPool()) {
		td->ReleaseCustomModelsBeforeUnload();
	}
}

void onArtworkResourcesReady()
{
	if (pGame) {
		refreshCustomPedAssets(pGame->FindPlayerPed());
	}
	if (!pNetGame) return;

	if (CObjectPool* op = pNetGame->GetObjectPool()) {
		op->RefreshCustomModels();
	}
	if (CPlayerPool* pp = pNetGame->GetPlayerPool()) {
		for (PLAYERID i = 0; i < MAX_PLAYERS; ++i) {
			if (!pp->GetSlotState(i)) continue;
			if (CRemotePlayer* rp = pp->GetAt(i)) {
				refreshCustomPedAssets(rp->GetPlayerPed());
			}
		}
	}
	if (CActorPool* ap = pNetGame->GetActorPool()) {
		for (PLAYERID i = 0; i < MAX_ACTORS; ++i) {
			if (CActor* actor = ap->GetAt(i)) {
				actor->RefreshCustomSkin();
			}
		}
	}
	if (CPickupPool* pk = pNetGame->GetPickupPool()) {
		pk->RefreshCustomModels();
	}
	if (CTextDrawPool* td = pNetGame->GetTextDrawPool()) {
		td->RefreshCustomModels();
	}
}

bool isLocalPlayerActiveForArtwork()
{
	if (!pNetGame || !pNetGame->GetPlayerPool()) return false;
	CLocalPlayer* local = pNetGame->GetPlayerPool()->GetLocalPlayer();
	return local != nullptr && local->m_bIsActive;
}

} // namespace

NetGame::NetGame(const char* host, int port, const char* playerName, const char* password)
{
	spdlog::info("NetGame initializing..");

	m_netSet = &m_session.settings;
	m_session.protoVersion = proto::versionFromNetgame(static_cast<uint32_t>(Client::netgameVersion()));
	m_session.reset(SessionState::ResetReason::Connect);

	memset(m_host, 0, sizeof(m_host));
	if (host) {
		strncpy(m_host, host, sizeof(m_host) - 1);
		m_host[sizeof(m_host) - 1] = '\0';
	}
	m_port = port;

	NetCap::startIfEnabled(Client::gameDir(), (uint32_t) Client::netgameVersion());

	m_rakClient = RakNetworkFactory::GetRakClientInterface();
	{
		std::lock_guard<std::mutex> lock(g_monetNetGameMutex);
		notifyMonetNetGameLocked(m_rakClient, m_host, m_port);
	}
	InitializePools();

	GetPlayerPool()->SetLocalPlayerName(Encoding::utf2cp(playerName).c_str());

	RpcTable::registerAll(m_rakClient, m_session.protoVersion);
	m_rakClient->SetPassword(password);

	if (m_session.protoVersion == proto::ProtoVersion::V03DL) {
		artwork::ArtworkHostCallbacks cbs{};
		cbs.onBeforeUnloadModels = &onArtworkBeforeUnloadModels;
		cbs.onResourcesReady = &onArtworkResourcesReady;
		cbs.isLocalPlayerActive = &isLocalPlayerActiveForArtwork;
		artwork::ArtworkManager::instance().setHostCallbacks(cbs);
		artwork::ArtworkManager::instance().onSessionStart(m_host, static_cast<uint16_t>(m_port));
	}

	SessionHooks::NotifyConnect(host, static_cast<uint16_t>(port));

	memset(m_mapIcon, 0, sizeof(m_mapIcon));

	pGame->EnableClock(false);
	pGame->EnableZoneNames(false);

	m_lastConnectAttempt = GetTickCount();
	SetGameState(GAMESTATE_WAIT_CONNECT);

	if (pUI) {
		pUI->chat()->addDebugMessage(L10N("{FFFFFF}SA-MP {B9C9BF}%s {FFFFFF}Started", "{FFFFFF}SA-MP {B9C9BF}%s {FFFFFF}已启动"), Client::sampVersion().c_str());
		pUI->chat()->addInfoMessage(L10N("-> Use /clientcmds for client commands", "-> 输入 /clientcmds 查看客户端命令"));
	}
}

NetGame::~NetGame()
{
	{
		std::lock_guard<std::mutex> lock(g_monetNetGameMutex);
		if (g_monetLastAttachedRakClient == m_rakClient) {
			g_monetLastAttachedRakClient = nullptr;
		}
	}
	m_rakClient->Disconnect(0);
	RpcTable::unregisterAll(m_rakClient, m_session.protoVersion);
	RakNetworkFactory::DestroyRakClientInterface(m_rakClient);
	m_rakClient = nullptr;

	UninitializePools();
	artwork::ArtworkManager::instance().onSessionEnd();
	artwork::ArtworkManager::instance().clearHostCallbacks();

	if (Settings::voice()) {
		SessionHooks::NotifyDisconnect();
	}

	m_netSet = nullptr;
}

void NetGame::InitializePools()
{
	m_pools = new NET_POOLS;
	m_pools->playerPool = new CPlayerPool();
	m_pools->vehiclePool = new CVehiclePool();
	m_pools->objectPool = new CObjectPool();
	m_pools->actorPool = new CActorPool();
	m_pools->pickupPool = new CPickupPool();
	m_pools->textLabelPool = new CTextLabelPool();
	m_pools->textDrawPool = new CTextDrawPool();
	m_pools->playerBubblePool = new CPlayerBubblePool();
	m_pools->gangZonePool = new CGangZonePool();
	m_pools->menuPool = new CMenuPool();
}

void NetGame::UninitializePools()
{
	if (m_pools->playerPool) {
		delete m_pools->playerPool;
		m_pools->playerPool = nullptr;
	}

	if (m_pools->vehiclePool) {
		delete m_pools->vehiclePool;
		m_pools->vehiclePool = nullptr;
	}

	if (m_pools->objectPool) {
		delete m_pools->objectPool;
		m_pools->objectPool = nullptr;
	}

	if (m_pools->actorPool) {
		delete m_pools->actorPool;
		m_pools->actorPool = nullptr;
	}

	if (m_pools->pickupPool) {
		delete m_pools->pickupPool;
		m_pools->pickupPool = nullptr;
	}

	if (m_pools->textLabelPool) {
		delete m_pools->textLabelPool;
		m_pools->textLabelPool = nullptr;
	}

	if (m_pools->textDrawPool) {
		delete m_pools->textDrawPool;
		m_pools->textDrawPool = nullptr;
	}

	if (m_pools->playerBubblePool) {
		delete m_pools->playerBubblePool;
		m_pools->playerBubblePool = nullptr;
	}

	if (m_pools->gangZonePool) {
		delete m_pools->gangZonePool;
		m_pools->gangZonePool = nullptr;
	}

	if (m_pools->menuPool) {
		delete m_pools->menuPool;
		m_pools->menuPool = nullptr;
	}

	if (m_pools) {
		delete m_pools;
		m_pools = nullptr;
	}
}

void NetGame::Process()
{
	UpdateNetwork();
	ProcessDlArtworkHandshake();
	if (m_netSet->holdTime) {
		pGame->SetWorldTime(m_netSet->worldTime_Hour, m_netSet->worldTime_Minute);
	}

	pGame->PreloadObjectsAnims();

	if (GetGameState() == GAMESTATE_CONNECTED) {
		// Match PC 0.3.DL CNetGame::Process(): gate world/pool ticks until active artwork
		// downloads complete and RPC 185 (or its watchdog) releases the gate.
		if (!artwork::ArtworkManager::instance().isDownloadGateActive()) {
			ProcessPools();
		}
		if (pGame) {
			pGame->ProcessAudio();
		}
	}
	else {
		ProcessLoadingScreen();
	}

	if (GetGameState() == GAMESTATE_WAIT_CONNECT) {
		ProcessConnecting();
	}
}

void NetGame::UpdateNetwork()
{
	Packet* pkt = nullptr;
	unsigned char packetIdentifier;
	while ((pkt = m_rakClient->Receive())) {
		if (pkt->data && pkt->length > 0) {
			const unsigned int bits = (pkt->bitSize > 0 && pkt->bitSize <= pkt->length * 8)
				? pkt->bitSize
				: pkt->length * 8;
			invokeRakNetCapHook(0, 1, pkt->data[0], pkt->data, bits);
		}
		packetIdentifier = GetPacketID(pkt);

		switch (packetIdentifier) {
			case ID_AUTH_KEY: Packet_AuthKey(pkt);
				break;

			case ID_CONNECTION_ATTEMPT_FAILED: Packet_ConnectAttemptFailed(pkt);
				break;

			case ID_NO_FREE_INCOMING_CONNECTIONS: Packet_NoFreeIncomingConnections(pkt);
				break;

			case ID_DISCONNECTION_NOTIFICATION: Packet_DisconnectionNotification(pkt);
				break;

			case ID_CONNECTION_LOST: Packet_ConnectionLost(pkt);
				break;

			case ID_CONNECTION_REQUEST_ACCEPTED: Packet_ConnectionSucceeded(pkt);
				break;

			case ID_FAILED_INITIALIZE_ENCRIPTION: Packet_FailedInitializeEncription(pkt);
				break;

			case ID_CONNECTION_BANNED: Packet_ConnectionBanned(pkt);
				break;

			case ID_INVALID_PASSWORD: Packet_InvalidPassword(pkt);
				break;

			case ID_VEHICLE_SYNC: Packet_VehicleSync(pkt);
				break;

			case ID_AIM_SYNC: Packet_AimSync(pkt);
				break;

			case ID_BULLET_SYNC: Packet_BulletSync(pkt);
				break;

			case ID_PLAYER_SYNC: Packet_PlayerSync(pkt);
				break;

			case ID_MARKERS_SYNC: Packet_MarkerSync(pkt);
				break;

			case ID_UNOCCUPIED_SYNC: Packet_UnoccupiedSync(pkt);
				break;

			case ID_TRAILER_SYNC: Packet_TrailerSync(pkt);
				break;

			case ID_PASSENGER_SYNC: Packet_PassengerSync(pkt);
				break;

			case ID_VOICE_SYNC:
				SessionHooks::NotifyCustomPacket(ID_VOICE_SYNC, pkt);
				break;
		}

		m_rakClient->DeallocatePacket(pkt);
	}
}

void NetGame::ShutdownForGameModeRestart(SessionState::ResetReason reason)
{
	for (PLAYERID playerId = 0; playerId < MAX_PLAYERS; playerId++) {
		CRemotePlayer* pRemotePlayer = GetPlayerPool()->GetAt(playerId);
		if (pRemotePlayer) {
			pRemotePlayer->SetTeam(NO_TEAM);
			pRemotePlayer->ResetAllSyncAttributes();
		}
	}

	GetPlayerPool()->GetLocalPlayer()->ResetAllSyncAttributes();
	GetPlayerPool()->GetLocalPlayer()->ToggleSpectating(false);
	GameResetStats();

	if (pAudioStream) {
		pAudioStream->Stop(true);
	}

	SetGameState(GAMESTATE_RESTARTING);
	GetPlayerPool()->DeactivateAll();
	GetPlayerPool()->Process();
	ResetVehiclePool();
	ResetActorPool();
	ResetTextDrawPool();
	ResetPlayerBubblePool();
	ResetGangZonePool();
	ResetTextLabelPool();
	ResetMapIcons();
	ResetPickupPool();
	ResetObjectPool();
	ResetMenuPool();
	if (pUI && pUI->netstats()) {
		pUI->netstats()->resetServerStats();
	}

	m_session.reset(reason);

	if (EditObject::m_visible) {
		EditObject::cancelEdit();
	}
	if (g_java) {
		g_java->hideDialog();
	}
	if (pUI && pUI->spawn()) {
		pUI->spawn()->setVisible(false);
	}
	ResetLocalPedQueuedAnim();

	if (pGame) {
		pGame->ClearImGuiGameText();
		pGame->ClearAllHudHideReasons();
		pGame->EnableCheckpoints(false);
		pGame->EnableRaceCheckpoints(false);
		pGame->UpdateCheckpoints();

		if (CPlayerPed* pLocalPed = pGame->FindPlayerPed()) {
			if (pLocalPed->IsDancing()) pLocalPed->StopDancing();
			if (pLocalPed->IsCellphoneEnabled()) pLocalPed->ToggleCellphone(false);
			if (pLocalPed->IsPissing()) pLocalPed->TogglePissing(false);
			if (pLocalPed->IsCuffed()) pLocalPed->ToggleCuffed(false);
			if (pLocalPed->IsCarry()) pLocalPed->ToggleCarry(false);
			if (pLocalPed->IsInJetpack()) pLocalPed->StopJetpack();
			if (pLocalPed->GetStuff() != STUFF_TYPE_NONE) pLocalPed->DropStuff();
			if (pLocalPed->HasAttachedObject()) pLocalPed->RemoveAllAttachedObjects();
			pLocalPed->SetDrunkLevel(0);
			pLocalPed->DrunkProcess();
			pLocalPed->SetFightingStyle(4);
			pLocalPed->SetInterior(0, true);
			pLocalPed->SetDead();
			pLocalPed->SetArmour(0.0f);
		}
		ResetDlArtworkState();
		pGame->ResetLocalMoney();
		pGame->EnableZoneNames(false);
		GameResetRadarColors();
		pGame->SetGravity(m_netSet->gravity);
		pGame->SetWantedLevel(0);
		pGame->EnableClock(false);
		ScriptCommand(&toggle_widescreen, 0);
	}

	SessionHooks::NotifyDisconnect();

	if (pUI) {
		pUI->chat()->addInfoMessage(L10N("The server is restarting..", "服务器正在重启.."));
	}
}

void NetGame::ProcessPools()
{
	if (GetPlayerPool()) {
		GetPlayerPool()->Process();
	}

	static int iVehiclePoolProcessFlag = 0;
	if (GetVehiclePool() && iVehiclePoolProcessFlag > 2) {
		GetVehiclePool()->Process();
		iVehiclePoolProcessFlag = 0;
	}
	else {
		iVehiclePoolProcessFlag++;
	}

	static int iPickupPoolProcessFlag = 0;
	if (GetPickupPool() && iPickupPoolProcessFlag > 5) {
		GetPickupPool()->Process();
		iPickupPoolProcessFlag = 0;
	}
	else {
		iPickupPoolProcessFlag++;
	}
}

void NetGame::ProcessLoadingScreen()
{
	CPlayerPed* pPlayerPed = pGame->FindPlayerPed();
	if (!pPlayerPed || !pPlayerPed->m_ped) {
		return;
	}
	if (pPlayerPed->IsInVehicle()) {
		pPlayerPed->RemoveFromVehicleAndPutAt(1093.4, -2036.5, 82.710602);
	}
	else {
		pPlayerPed->TeleportTo(1133.0504, -2038.4034, 69.099998);
	}

	pGame->GetCamera()->SetPosition(1093.0, -2036.0, 90.0, 0.0, 0.0, 0.0);
	pGame->GetCamera()->LookAtPoint(384.0, -1557.0, 20.0, 2);
	pGame->SetWorldWeather(1);
	pGame->DisplayHUD(false);
}

void NetGame::ProcessConnecting()
{
	if ((GetTickCount() - m_lastConnectAttempt) > 3000) {
		if (pUI) {
			pUI->chat()->addDebugMessage(L10N("Connecting to %s:%d...", "正在连接 %s:%d..."), m_host, m_port);
		}

		m_rakClient->Connect(m_host, m_port, 0, 0, 2);

		SessionHooks::NotifyConnect(m_host, static_cast<uint16_t>(m_port));

		m_lastConnectAttempt = GetTickCount();
		SetGameState(GAMESTATE_CONNECTING);
	}
}

void NetGame::UpdatePlayerScoresAndPings()
{
	static uint32_t dwLastUpdateTick = 0;

	if (GetTickCount() - dwLastUpdateTick > 3000) {
		dwLastUpdateTick = GetTickCount();

		RakNet::BitStream bsSend;
		proto::Writer writer(bsSend);
		proto::UpdateScoresPingsReq req{};
		req.serialize(writer, proto::ProtoVersion::V037);
		m_rakClient->RPC(&RPC_UpdateScoresPingsIPs, &bsSend, HIGH_PRIORITY, RELIABLE, 0, false,
				UNASSIGNED_NETWORK_ID, nullptr);
	}
}

void NetGame::SendDialogResponse(uint16_t wDialogID, uint8_t byteButtonID, uint16_t wListBoxItem, const char* szInput)
{
	if (GetGameState() != GAMESTATE_CONNECTED || !m_rakClient) return;
	if (!szInput) szInput = "";

	size_t rawLen = strlen(szInput);
	if (rawLen > 128) rawLen = 128;

	RakNet::BitStream bsSend;
	proto::Writer writer(bsSend);
	proto::DialogResponse resp{};
	resp.id = static_cast<int16_t>(wDialogID);
	resp.response = byteButtonID;
	resp.listItem = static_cast<int16_t>(wListBoxItem);
	resp.input.assign(szInput, rawLen);
	resp.serialize(writer, proto::ProtoVersion::V037);
	m_rakClient->RPC(&RPC_DialogResponse, &bsSend, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false, UNASSIGNED_NETWORK_ID, nullptr);
}

void NetGame::SendChatMessage(const char* szMsg)
{
	if (GetGameState() != GAMESTATE_CONNECTED || !m_rakClient || !szMsg) return;

	size_t rawLen = strlen(szMsg);
	if (rawLen > 144) rawLen = 144;

	RakNet::BitStream bsSend;
	proto::Writer writer(bsSend);
	proto::ChatReq req{};
	req.message.assign(szMsg, rawLen);
	req.serialize(writer, proto::ProtoVersion::V037);

	m_rakClient->RPC(&RPC_Chat, &bsSend, HIGH_PRIORITY, RELIABLE, 0, false, UNASSIGNED_NETWORK_ID,
			NULL);
}

void NetGame::SendChatCommand(const char* szCommand)
{
	if (GetGameState() != GAMESTATE_CONNECTED || !m_rakClient || !szCommand) return;

	size_t rawLen = strlen(szCommand);
	if (rawLen > 255) rawLen = 255;

	RakNet::BitStream bsParams;
	proto::Writer writer(bsParams);
	proto::ServerCommandReq req{};
	req.command.assign(szCommand, rawLen);
	req.serialize(writer, proto::ProtoVersion::V037);
	m_rakClient->RPC(&RPC_ServerCommand, &bsParams, HIGH_PRIORITY, RELIABLE, 0, false,
			UNASSIGNED_NETWORK_ID, NULL);
}

void NetGame::SetMapIcon(uint8_t byteIconID, float fPosX, float fPosY, float fPosZ, uint8_t byteType, uint32_t dwColor, uint8_t byteStyle)
{
	if (byteIconID < MAX_MAP_ICONS) {
		if (m_mapIcon[byteIconID] != 0) {
			DisableMapIcon(byteIconID);
		}

		m_mapIcon[byteIconID] = pGame->CreateRadarMarkerIcon(byteType, fPosX, fPosY, fPosZ, dwColor, byteStyle);
	}
}

void NetGame::DisableMapIcon(uint8_t byteIconID)
{
	if (byteIconID < MAX_MAP_ICONS) {
		if (m_mapIcon[byteIconID] != 0) {
			if (pGame) {
				pGame->DisableMarker(m_mapIcon[byteIconID]);
			}
			m_mapIcon[byteIconID] = 0;
		}
	}
}

void NetGame::ResetVehiclePool()
{
	if (m_pools->vehiclePool) {
		delete m_pools->vehiclePool;
	}

	m_pools->vehiclePool = new CVehiclePool();
}

void NetGame::ResetActorPool()
{
	if (m_pools->actorPool) {
		delete m_pools->actorPool;
	}

	m_pools->actorPool = new CActorPool();
}

void NetGame::ResetTextDrawPool()
{
	// Reset in-place (never delete/reallocate while NVEvent/Java threads may access it)
	if (m_pools->textDrawPool) {
		m_pools->textDrawPool->Reset();
	}
	else {
		m_pools->textDrawPool = new CTextDrawPool();
	}
}

void NetGame::ResetPlayerBubblePool()
{
	if (m_pools->playerBubblePool) {
		delete m_pools->playerBubblePool;
	}

	m_pools->playerBubblePool = new CPlayerBubblePool();
}

void NetGame::ResetGangZonePool()
{
	if (m_pools->gangZonePool) {
		delete m_pools->gangZonePool;
	}

	m_pools->gangZonePool = new CGangZonePool();
}

void NetGame::ResetTextLabelPool()
{
	if (m_pools->textLabelPool) {
		delete m_pools->textLabelPool;
	}

	m_pools->textLabelPool = new CTextLabelPool();
}

void NetGame::ResetMapIcons()
{
	for (unsigned int& i : m_mapIcon) {
		if (i) {
			ScriptCommand(&disable_marker, i);
			i = 0;
		}
	}
}

void NetGame::ResetPickupPool()
{
	if (m_pools->pickupPool) {
		delete m_pools->pickupPool;
	}

	m_pools->pickupPool = new CPickupPool();
}

void NetGame::ResetObjectPool()
{
	// Reset in-place (never delete/reallocate while NVEvent/Java threads may access it)
	if (m_pools->objectPool) {
		m_pools->objectPool->Reset();
	}
	else {
		m_pools->objectPool = new CObjectPool();
	}
}

void NetGame::ResetMenuPool()
{
	// Reset in-place (never delete/reallocate while NVEvent/Java threads may access it)
	if (m_pools->menuPool) {
		m_pools->menuPool->Reset();
	}
	else {
		m_pools->menuPool = new CMenuPool();
	}
}

void NetGame::InitGameLogic()
{
	m_netSet->worldBounds[0] = 20000.0f;
	m_netSet->worldBounds[1] = -20000.0f;
	m_netSet->worldBounds[2] = 20000.0f;
	m_netSet->worldBounds[3] = -20000.0f;
}

void NetGame::SendPrevClass()
{
	CPlayerPool* pPlayerPool = GetPlayerPool();
	if (pPlayerPool) {
		pPlayerPool->GetLocalPlayer()->SendPrevClass();
	}
}

void NetGame::SendSpawn()
{
	CPlayerPool* pPlayerPool = GetPlayerPool();
	if (pPlayerPool) {
		pPlayerPool->GetLocalPlayer()->SendSpawn();
	}
}

void NetGame::SendNextClass()
{
	CPlayerPool* pPlayerPool = GetPlayerPool();
	if (pPlayerPool) {
		pPlayerPool->GetLocalPlayer()->SendNextClass();
	}
}

void NetGame::NotifyDlVirtualWorld(int32_t virtualWorld)
{
	artwork::ArtworkManager::instance().onVirtualWorldChanged(virtualWorld);
}

void NetGame::ResetDlArtworkState()
{
	artwork::ArtworkManager::instance().onGameModeRestart();
}

void NetGame::ProcessDlArtworkHandshake()
{
	if (m_session.protoVersion != proto::ProtoVersion::V03DL) {
		return;
	}
	artwork::ArtworkManager::instance().processFrame(this);
}