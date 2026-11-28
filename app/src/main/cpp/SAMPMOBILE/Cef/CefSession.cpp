#include "CefSession.h"

#include "CefBrowser.h"
#include "CefResources.h"

#include <android/log.h>
#include <chrono>

// Game.h first: Net/*.h uses sa:: types from <SA.h> without including it.
#include "Game/Game.h"
#include "Net/NetGame.h"

#define CEF_LOG_TAG "CEF"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  CEF_LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  CEF_LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, CEF_LOG_TAG, __VA_ARGS__)

// Defined in Client.cpp.
extern NetGame* pNetGame;

namespace
{

uint64_t NowMs()
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

const char* PacketTypeName(cefproto::PacketType type)
{
    using PT = cefproto::PacketType;
    switch (type)
    {
        case PT::RequestJoin:        return "RequestJoin";
        case PT::HandshakeChallenge: return "HandshakeChallenge";
        case PT::HandshakeFinalize:  return "HandshakeFinalize";
        case PT::JoinResponse:       return "JoinResponse";
        case PT::ServerConfig:       return "ServerConfig";
        case PT::RequestFiles:       return "RequestFiles";
        case PT::FileData:           return "FileData";
        case PT::DownloadComplete:   return "DownloadComplete";
        case PT::PressKey:           return "PressKey";
        case PT::EmitEvent:          return "EmitEvent";
        case PT::EmitBrowserEvent:   return "EmitBrowserEvent";
        case PT::ClientEmitEvent:    return "ClientEmitEvent";
    }
    return "Unknown";
}

} // namespace

CefSession& CefSession::Get()
{
    static CefSession instance;
    return instance;
}

CefSession::CefSession()
{
    network_.SetPacketHandler([this](const cefproto::NetworkPacket& packet) {
        OnNetworkPacket(packet);
    });
    network_.SetSessionHandler([this](bool active) {
        OnSessionActive(active);
    });
    cefres::CefResources::Get().SetSender([this](const cefproto::NetworkPacket& packet) {
        return SendPacket(packet);
    });
}

bool CefSession::SendPacket(const cefproto::NetworkPacket& packet)
{
    if (network_.GetState() != CefNetwork::State::Connected)
        return false;
    network_.SendPacket(packet);
    return true;
}

void CefSession::OnNetworkPacket(const cefproto::NetworkPacket& packet)
{
    // Network thread: only queue, never touch game state here.
    std::lock_guard<std::mutex> lock(queueMutex_);
    packetQueue_.push_back(packet);
    if (packetQueue_.size() > 4096)
    {
        LOGW("[CefSession] packet queue overflow, dropping oldest");
        packetQueue_.pop_front();
    }
}

void CefSession::OnSessionActive(bool active)
{
    LOGI("[CefSession] session %s", active ? "active" : "inactive");

    // Handshake succeeded: the peer really speaks the CEF protocol, so it is
    // only now worth paying for a Chromium instance.普通（非 CEF）服务器
    // 永远不会走到这里，因此不会再白拉一个隐藏 WebView。
    if (active)
        CefBrowser::NotifyTransportConnected();
}

void CefSession::DrainPacketQueue()
{
    std::deque<cefproto::NetworkPacket> local;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        local.swap(packetQueue_);
    }
    for (const auto& packet : local)
        DispatchPacket(packet);
}

void CefSession::DispatchPacket(const cefproto::NetworkPacket& packet)
{
    switch (packet.type)
    {
        case cefproto::PacketType::JoinResponse:
            manifestJson_ = packet.joinResponse.manifest_json;
            cefres::CefResources::Get().OnJoinResponse(manifestJson_);
            LOGI("[CefSession] join ok (conv=%u, manifest=%zu bytes)",
                 packet.joinResponse.kcp_conv_id, manifestJson_.size());
            break;

        case cefproto::PacketType::ServerConfig:
            masterResourceKey_ = packet.serverConfig.master_resource_key;
            cefres::CefResources::Get().OnServerConfig(masterResourceKey_);
            LOGI("[CefSession] ServerConfig: master key %zu bytes, loaderUi=%d",
                 masterResourceKey_.size(), packet.serverConfig.resources_loader_ui ? 1 : 0);
            break;

        case cefproto::PacketType::FileData:
            cefres::CefResources::Get().OnFileData(packet.fileData);
            break;

        case cefproto::PacketType::EmitEvent: {
            ServerEventHandler handler;
            {
                std::lock_guard<std::mutex> lock(handlerMutex_);
                handler = serverEventHandler_;
            }
            if (handler)
            {
                handler(packet.emitEvent);
            }
            else
            {
                LOGI("[CefSession] server event '%s' (%zu args) - no browser layer yet",
                     packet.emitEvent.name.c_str(), packet.emitEvent.args.size());
            }
            break;
        }

        case cefproto::PacketType::EmitBrowserEvent: {
            BrowserEventHandler handler;
            {
                std::lock_guard<std::mutex> lock(handlerMutex_);
                handler = browserEventHandler_;
            }
            if (handler)
            {
                handler(packet.emitEvent);
            }
            else
            {
                LOGI("[CefSession] browser event '%s' (browser %d, %zu args) - no page layer yet",
                     packet.emitEvent.name.c_str(), packet.emitEvent.browserId,
                     packet.emitEvent.args.size());
            }
            break;
        }

        default:
            LOGI("[CefSession] packet %s ignored at session layer", PacketTypeName(packet.type));
            break;
    }
}

void CefSession::SetServerEventHandler(ServerEventHandler handler)
{
    std::lock_guard<std::mutex> lock(handlerMutex_);
    serverEventHandler_ = std::move(handler);
}

void CefSession::SetBrowserEventHandler(BrowserEventHandler handler)
{
    std::lock_guard<std::mutex> lock(handlerMutex_);
    browserEventHandler_ = std::move(handler);
}

void CefSession::SetSessionLostHandler(SessionLostHandler handler)
{
    std::lock_guard<std::mutex> lock(handlerMutex_);
    sessionLostHandler_ = std::move(handler);
}

void CefSession::FireSessionLost()
{
    SessionLostHandler handler;
    {
        std::lock_guard<std::mutex> lock(handlerMutex_);
        handler = sessionLostHandler_;
    }
    if (handler)
        handler();
}

void CefSession::ResetSession()
{
    FireSessionLost();
    cefres::CefResources::Get().OnSessionLost();
    network_.Disconnect();
    endpointReady_ = false;
    connectedPlayerId_ = -1;
    manifestJson_.clear();
    masterResourceKey_.clear();

    std::lock_guard<std::mutex> lock(queueMutex_);
    packetQueue_.clear();
}

void CefSession::DebugConnect(const std::string& host, uint16_t port, int32_t playerId)
{
    // Hold debugMutex_ for the whole transition: Tick() calls
    // network_.Connect() under the same lock, otherwise the socket could be
    // torn down while the game thread is sending on it.
    std::lock_guard<std::mutex> lock(debugMutex_);
    debugActive_ = false;
    network_.Disconnect();
    {
        std::lock_guard<std::mutex> queueLock(queueMutex_);
        packetQueue_.clear();
    }

    debugHost_ = host;
    debugPort_ = port;
    debugPlayerId_ = playerId;
    endpointReady_ = false;
    nextConnectAttemptMs_ = 0;
    debugActive_ = true;
    LOGI("[CefSession] DEBUG connect %s:%u as player %d", host.c_str(), (unsigned)port, playerId);
}

void CefSession::DebugDisconnect()
{
    std::lock_guard<std::mutex> lock(debugMutex_);
    if (!debugActive_)
        return;

    debugActive_ = false;
    FireSessionLost();
    cefres::CefResources::Get().OnSessionLost();
    network_.Disconnect();
    endpointReady_ = false;
    connectedPlayerId_ = -1;
    manifestJson_.clear();
    masterResourceKey_.clear();
    {
        std::lock_guard<std::mutex> queueLock(queueMutex_);
        packetQueue_.clear();
    }
    nextConnectAttemptMs_ = 0;
    LOGI("[CefSession] DEBUG disconnect");
}

void CefSession::Tick()
{
    DrainPacketQueue();
    cefres::CefResources::Get().Tick();

    const uint64_t now = NowMs();

    {
        std::lock_guard<std::mutex> debugLock(debugMutex_);
        if (debugActive_)
        {
            if (network_.GetState() == CefNetwork::State::Disconnected && now >= nextConnectAttemptMs_)
            {
                if (!endpointReady_)
                {
                    if (!network_.Initialize(debugHost_, debugPort_))
                    {
                        LOGE("[CefSession] DEBUG cannot init endpoint %s:%u",
                             debugHost_.c_str(), (unsigned)debugPort_);
                        nextConnectAttemptMs_ = now + 2000;
                        return;
                    }
                    endpointReady_ = true;
                    cefres::CefResources::Get().SetServerIdentity(debugHost_, debugPort_);
                }
                connectedPlayerId_ = debugPlayerId_;
                network_.Connect(debugPlayerId_);
                nextConnectAttemptMs_ = now + CONNECT_RETRY_BACKOFF_MS;
            }
            return;
        }
    }

    if (!pNetGame || pNetGame->GetGameState() != GAMESTATE_CONNECTED)
    {
        if (network_.GetState() != CefNetwork::State::Disconnected || endpointReady_)
        {
            LOGI("[CefSession] SA-MP not connected - resetting CEF session");
            ResetSession();
        }
        return;
    }

    const int32_t localPlayerId = static_cast<int32_t>(pNetGame->GetPlayerPool()->GetLocalPlayerID());
    const std::string host = pNetGame->m_host;
    const int gamePort = pNetGame->m_port;

    if (localPlayerId < 0 || host.empty() || gamePort <= 0)
        return;

    const int cefPortInt = gamePort + CEF_PORT_OFFSET;
    if (cefPortInt <= 0 || cefPortInt > 65535)
        return;
    const uint16_t cefPort = static_cast<uint16_t>(cefPortInt);

    // Player changed (reconnect to another server / respawn as other id):
    // drop the old CEF session.
    if (connectedPlayerId_ >= 0 && localPlayerId != connectedPlayerId_)
    {
        LOGI("[CefSession] local player id changed %d -> %d - resetting",
             connectedPlayerId_, localPlayerId);
        ResetSession();
    }

    if (network_.GetState() == CefNetwork::State::Disconnected && !network_.IsNonCefServer())
    {
        if (now < nextConnectAttemptMs_)
            return;

        const bool endpointChanged = !endpointReady_ || host != endpointHost_ || cefPort != endpointPort_;
        if (endpointChanged)
        {
            endpointReady_ = false;
            if (!network_.Initialize(host, cefPort))
            {
                LOGE("[CefSession] cannot init endpoint %s:%u", host.c_str(), (unsigned)cefPort);
                nextConnectAttemptMs_ = now + 2000;
                return;
            }
            endpointReady_ = true;
            endpointHost_ = host;
            endpointPort_ = cefPort;
            cefres::CefResources::Get().SetServerIdentity(host, cefPort);
            LOGI("[CefSession] endpoint %s:%u (game %s:%d + %d)",
                 endpointHost_.c_str(), (unsigned)endpointPort_,
                 host.c_str(), gamePort, CEF_PORT_OFFSET);
        }

        connectedPlayerId_ = localPlayerId;
        network_.Connect(localPlayerId);
        nextConnectAttemptMs_ = now + CONNECT_RETRY_BACKOFF_MS;
    }
}
