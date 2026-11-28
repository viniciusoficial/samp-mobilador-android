#pragma once

// omp-cef compatible UDP/KCP transport for the Android client.
// Reimplements the client half of src/client/core/network/network_manager.cpp
// with POSIX sockets instead of asio:
//
//   1. plaintext UDP  RequestJoin(playerid, version)         (retry 5x / 2s)
//   2. plaintext UDP  HandshakeChallenge -> HandshakeFinalize
//   3. session keys   = crypto_kx_client_session_keys equivalent
//   4. plaintext UDP  JoinResponse(kcp conv id, manifest)
//   5. KCP (nodelay 1,10,2,1; wnd 128/128; 10ms update), every KCP message
//      is one crypto_secretbox_easy-encrypted serialized packet
//
// Packet/session callbacks fire on the network thread; consumers must hand
// data over to the game thread themselves (see CefSession).

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "CefCrypto.h"
#include "CefProtocol.h"

typedef struct IKCPCB ikcpcb;

class CefNetwork
{
public:
    enum class State
    {
        Disconnected = 0,
        SendingJoin,
        AwaitingAcceptance,
        Connected,
    };

    using PacketHandler = std::function<void(const cefproto::NetworkPacket&)>;
    using SessionHandler = std::function<void(bool active)>;

    CefNetwork() = default;
    ~CefNetwork();

    CefNetwork(const CefNetwork&) = delete;
    CefNetwork& operator=(const CefNetwork&) = delete;

    // Stores the server endpoint (does not connect). Returns false if the
    // host cannot be resolved.
    bool Initialize(const std::string& ip, uint16_t port);

    // Starts (or restarts) the join handshake for the given local player id.
    void Connect(int32_t playerId);

    // Tears down socket, KCP and keys. Safe to call from any thread.
    void Disconnect();

    State GetState() const { return state_.load(std::memory_order_acquire); }
    bool IsNonCefServer() const { return nonCefServer_.load(std::memory_order_acquire); }

    // Serializes and sends a packet. Before JoinResponse it goes out as raw
    // UDP; once CONNECTED it is encrypted and sent through KCP.
    void SendPacket(const cefproto::NetworkPacket& packet);

    // Convenience senders matching the PC client's helpers.
    void SendBrowserCreateResult(int32_t browserId, bool success, int32_t code, const std::string& reason);
    void SendClientEmit(int32_t browserId, const std::string& name, const std::vector<cefproto::Argument>& args);
    void SendRequestFiles(const std::vector<std::pair<std::string, std::string>>& files);
    void SendDownloadComplete();

    void SetPacketHandler(PacketHandler handler);
    void SetSessionHandler(SessionHandler handler);

private:
    void ThreadMain();
    void CleanupTransport();
    void FireSessionActive(bool active);
    void FirePacket(const cefproto::NetworkPacket& packet);

    void SendRaw(const uint8_t* data, size_t size);
    void DoSendRequestJoin();
    void HandleRawMessage(const uint8_t* data, size_t size);

    static int KcpOutputCallback(const char* buf, int len, ikcpcb* kcp, void* user);

private:
    static constexpr int MAX_JOIN_ATTEMPTS = 5;
    static constexpr int CONNECT_RETRY_INTERVAL_MS = 2000;
    static constexpr int KCP_UPDATE_INTERVAL_MS = 10;

    std::string host_;
    uint16_t port_ = 0;

    std::atomic<State> state_{State::Disconnected};
    std::atomic<bool> nonCefServer_{false};
    std::atomic<bool> stopRequested_{false};

    int socketFd_ = -1;
    std::thread networkThread_;

    int32_t playerId_ = -1;
    int joinAttempts_ = 0;
    uint64_t nextJoinAttemptMs_ = 0;
    uint64_t nextKcpUpdateMs_ = 0;

    cefcrypto::KeyPair clientKeys_{};
    cefcrypto::SessionKeys sessionKeys_{};
    bool hasSessionKeys_ = false;

    std::mutex kcpMutex_;
    ikcpcb* kcp_ = nullptr;

    std::mutex handlerMutex_;

    // 保护 sessionKeys_ / hasSessionKeys_：网络线程在收包解密、游戏线程在
    // SendPacket 加密，而 CleanupTransport 会 memset 清零。没有这把锁时
    // 断线瞬间可能读到半清零的密钥（解出垃圾或崩）。
    mutable std::mutex keysMutex_;
    PacketHandler packetHandler_;
    SessionHandler sessionHandler_;
};
