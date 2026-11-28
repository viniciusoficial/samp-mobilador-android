#include "CefNetwork.h"

#include <android/log.h>
#include <chrono>
#include <cstring>

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

extern "C" {
#include "thirdparty/ikcp.h"
}

#define CEF_LOG_TAG "CEF"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  CEF_LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  CEF_LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, CEF_LOG_TAG, __VA_ARGS__)

namespace
{

uint64_t NowMs()
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

uint32_t IClock()
{
    return static_cast<uint32_t>(NowMs() & 0xFFFFFFFFu);
}

} // namespace

CefNetwork::~CefNetwork()
{
    Disconnect();
}

bool CefNetwork::Initialize(const std::string& ip, uint16_t port)
{
    if (ip.empty() || port == 0)
        return false;

    host_ = ip;
    port_ = port;
    nonCefServer_.store(false, std::memory_order_release);
    FireSessionActive(false);

    LOGI("[CefNet] endpoint set to %s:%u", host_.c_str(), (unsigned)port_);
    return true;
}

void CefNetwork::Connect(int32_t playerId)
{
    State expected = state_.load(std::memory_order_acquire);
    if (expected != State::Disconnected)
    {
        LOGW("[CefNet] Connect() ignored: already in state %d", (int)expected);
        return;
    }

    // 上一轮 ThreadMain 可能自行退出（DNS 解析失败 / udp connect 失败 / 被服务端断开），
    // 那种情况下 state_ 已回 Disconnected 但 networkThread_ 仍然 joinable。
    // 直接给它赋新线程会触发 std::terminate() 闪退（断网、坏域名、纯 IPv6 环境必现）。
    // 所以先回收旧线程。
    if (networkThread_.joinable())
    {
        stopRequested_.store(true, std::memory_order_release);
        if (networkThread_.get_id() == std::this_thread::get_id())
        {
            networkThread_.detach();
        }
        else
        {
            networkThread_.join();
        }
    }

    playerId_ = playerId;
    joinAttempts_ = 0;
    stopRequested_.store(false, std::memory_order_release);

    socketFd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (socketFd_ < 0)
    {
        LOGE("[CefNet] socket() failed: %s", strerror(errno));
        return;
    }

    // 10 ms receive timeout: the same thread drives the KCP update loop.
    timeval tv{};
    tv.tv_sec = 0;
    tv.tv_usec = KCP_UPDATE_INTERVAL_MS * 1000;
    setsockopt(socketFd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    cefcrypto::GenerateKeyPair(clientKeys_);
    hasSessionKeys_ = false;

    state_.store(State::SendingJoin, std::memory_order_release);
    LOGI("[CefNet] connecting (playerid=%d)...", playerId);

    networkThread_ = std::thread([this]() { ThreadMain(); });
}

void CefNetwork::Disconnect()
{
    State prev = state_.exchange(State::Disconnected, std::memory_order_acq_rel);
    stopRequested_.store(true, std::memory_order_release);

    if (networkThread_.joinable())
    {
        if (networkThread_.get_id() == std::this_thread::get_id())
        {
            // Cannot join ourselves; finish inline below after the loop exits
            // is impossible, so just detach-free cleanup of transport state.
            CleanupTransport();
            networkThread_.detach();
            return;
        }
        networkThread_.join();
    }

    if (prev != State::Disconnected)
        LOGI("[CefNet] disconnected");

    CleanupTransport();
}

void CefNetwork::CleanupTransport()
{
    {
        std::lock_guard<std::mutex> lock(kcpMutex_);
        if (kcp_)
        {
            ikcp_release(kcp_);
            kcp_ = nullptr;
        }
    }

    if (socketFd_ >= 0)
    {
        close(socketFd_);
        socketFd_ = -1;
    }

    {
        std::lock_guard<std::mutex> lock(keysMutex_);
        std::memset(&clientKeys_, 0, sizeof(clientKeys_));
        std::memset(&sessionKeys_, 0, sizeof(sessionKeys_));
        hasSessionKeys_ = false;
    }
    joinAttempts_ = 0;
    playerId_ = -1;
}

void CefNetwork::ThreadMain()
{
    uint8_t buffer[65536];
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port_);

    if (inet_pton(AF_INET, host_.c_str(), &serverAddr.sin_addr) != 1)
    {
        // Hostname (not a dotted IP): resolve once here.
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        addrinfo* result = nullptr;
        if (getaddrinfo(host_.c_str(), nullptr, &hints, &result) != 0 || !result)
        {
            LOGE("[CefNet] cannot resolve %s", host_.c_str());
            state_.store(State::Disconnected, std::memory_order_release);
            CleanupTransport();
            return;
        }
        serverAddr.sin_addr = reinterpret_cast<sockaddr_in*>(result->ai_addr)->sin_addr;
        freeaddrinfo(result);
    }

    // Connect the UDP socket so recv/send only talk to the server endpoint
    // (the PC client drops datagrams from other endpoints the same way).
    if (connect(socketFd_, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) != 0)
    {
        LOGE("[CefNet] udp connect() failed: %s", strerror(errno));
        state_.store(State::Disconnected, std::memory_order_release);
        CleanupTransport();
        return;
    }

    nextJoinAttemptMs_ = 0;
    nextKcpUpdateMs_ = 0;

    while (!stopRequested_.load(std::memory_order_acquire))
    {
        const uint64_t now = NowMs();

        if (state_.load(std::memory_order_acquire) == State::SendingJoin && now >= nextJoinAttemptMs_)
            DoSendRequestJoin();

        {
            std::lock_guard<std::mutex> lock(kcpMutex_);
            if (kcp_ && now >= nextKcpUpdateMs_)
            {
                ikcp_update(kcp_, IClock());
                nextKcpUpdateMs_ = now + KCP_UPDATE_INTERVAL_MS;
            }
        }

        sockaddr_in from{};
        socklen_t fromLen = sizeof(from);
        const ssize_t received = recvfrom(socketFd_, buffer, sizeof(buffer), 0,
                                          reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (received > 0)
            HandleRawMessage(buffer, static_cast<size_t>(received));

        if (state_.load(std::memory_order_acquire) == State::Disconnected)
            break;
    }

    // 线程正常退出（被 Disconnect 或被服务端断开）：收尾自己的资源。
    // 早退路径（DNS/connect 失败）在 return 前也走这里，避免 socket 泄漏。
    CleanupTransport();
}

void CefNetwork::DoSendRequestJoin()
{
    if (state_.load(std::memory_order_acquire) != State::SendingJoin)
        return;

    ++joinAttempts_;
    if (joinAttempts_ > MAX_JOIN_ATTEMPTS)
    {
        LOGW("[CefNet] no handshake after %d attempts -> assuming non-CEF server", MAX_JOIN_ATTEMPTS);
        nonCefServer_.store(true, std::memory_order_release);
        FireSessionActive(false);
        Disconnect();
        return;
    }

    LOGI("[CefNet] RequestJoin attempt %d/%d", joinAttempts_, MAX_JOIN_ATTEMPTS);

    cefproto::NetworkPacket pkt;
    pkt.type = cefproto::PacketType::RequestJoin;
    pkt.requestJoin.playerid = playerId_;
    pkt.requestJoin.client_version = cefproto::PLUGIN_VERSION_U32;
    SendPacket(pkt);

    nextJoinAttemptMs_ = NowMs() + CONNECT_RETRY_INTERVAL_MS;
}

void CefNetwork::HandleRawMessage(const uint8_t* data, size_t size)
{
    // KCP channel up: feed the datagram, then drain and decrypt messages.
    // The mutex is NOT held while decrypted packets are deserialized and
    // dispatched, because handlers may call SendPacket() (which takes it).
    std::vector<std::vector<uint8_t>> kcpMessages;
    bool kcpActive = false;
    {
        std::lock_guard<std::mutex> lock(kcpMutex_);
        if (kcp_)
        {
            kcpActive = true;
            if (ikcp_input(kcp_, reinterpret_cast<const char*>(data), static_cast<long>(size)) < 0)
                LOGW("[CefNet] ikcp_input error");

            // 按 ikcp_peeksize 先问这条消息多大，再决定缓冲大小。
            // 原来固定 64KB 栈缓冲，服务端发一条更大的消息时 ikcp_recv 返回 -3
            // （缓冲区不够），循环退出但消息仍留在队列头部 —— 之后每次进来都
            // 撞同一条，接收流永久卡死。这里显式检测 -3 并按需扩容。
            std::vector<char> kcpBuf;
            for (;;)
            {
                const int peek = ikcp_peeksize(kcp_);
                if (peek < 0)
                    break;   // 没有完整消息

                if (static_cast<size_t>(peek) > kcpBuf.size())
                    kcpBuf.resize(static_cast<size_t>(peek));

                const int msgSize = ikcp_recv(kcp_, kcpBuf.data(),
                                              static_cast<int>(kcpBuf.size()));
                if (msgSize == -3)
                {
                    // 理论上前面的 resize 已经覆盖，走到这里说明 peeksize 与实际不符：
                    // 丢弃这条超限消息，否则会永远卡在队头。
                    LOGW("[CefNet] KCP message rejected (peeksize %d)", peek);
                    break;
                }
                if (msgSize < 0)
                    break;

                kcpMessages.emplace_back(kcpBuf.data(), kcpBuf.data() + msgSize);
            }
        }
    }

    if (kcpActive)
    {
        for (const auto& msg : kcpMessages)
        {
            uint8_t rxKey[cefcrypto::SESSION_KEY_BYTES]{};
            {
                std::lock_guard<std::mutex> lock(keysMutex_);
                std::memcpy(rxKey, sessionKeys_.rx, sizeof(rxKey));
            }
            std::vector<uint8_t> decrypted =
                cefcrypto::DecryptPacket(msg.data(), msg.size(), rxKey);
            if (decrypted.empty())
            {
                LOGW("[CefNet] failed to decrypt KCP message");
                continue;
            }

            cefproto::NetworkPacket packet;
            if (!cefproto::DeserializePacket(decrypted.data(), decrypted.size(), packet))
            {
                LOGW("[CefNet] failed to deserialize decrypted packet");
                continue;
            }

            FirePacket(packet);
        }
        return;
    }

    cefproto::NetworkPacket packet;
    if (!cefproto::DeserializePacket(data, size, packet))
    {
        LOGW("[CefNet] failed to deserialize raw packet (%zu bytes)", size);
        return;
    }

    switch (packet.type)
    {
        case cefproto::PacketType::HandshakeChallenge:
        {
            if (state_.load(std::memory_order_acquire) != State::SendingJoin)
                return;

            state_.store(State::AwaitingAcceptance, std::memory_order_release);

            cefproto::NetworkPacket finalize;
            finalize.type = cefproto::PacketType::HandshakeFinalize;
            finalize.handshakeFinalize.cookie = packet.handshakeChallenge.cookie;
            finalize.handshakeFinalize.client_public_key.assign(
                clientKeys_.publicKey, clientKeys_.publicKey + cefcrypto::KX_PUBLIC_KEY_BYTES);

            LOGI("[CefNet] sending HandshakeFinalize...");
            SendPacket(finalize);

            const auto& serverPk = packet.handshakeChallenge.server_public_key;
            if (serverPk.size() != cefcrypto::KX_PUBLIC_KEY_BYTES ||
                !cefcrypto::GenerateClientSessionKeys(clientKeys_, serverPk.data(), sessionKeys_))
            {
                LOGE("[CefNet] session key derivation failed");
                Disconnect();
                return;
            }
            hasSessionKeys_ = true;
            std::memset(clientKeys_.secretKey, 0, sizeof(clientKeys_.secretKey));
            break;
        }

        case cefproto::PacketType::JoinResponse:
        {
            const State st = state_.load(std::memory_order_acquire);
            if (st != State::AwaitingAcceptance && st != State::SendingJoin)
                return;

            if (!packet.joinResponse.accepted)
            {
                LOGE("[CefNet] join rejected: %s (reason=%u)",
                     packet.joinResponse.message.empty() ? "Join rejected" : packet.joinResponse.message.c_str(),
                     (unsigned)packet.joinResponse.reject_reason);
                nonCefServer_.store(true, std::memory_order_release);
                Disconnect();
                return;
            }

            if (!hasSessionKeys_)
            {
                LOGE("[CefNet] JoinResponse before session keys - dropping");
                Disconnect();
                return;
            }

            LOGI("[CefNet] join accepted, KCP conv=%u", packet.joinResponse.kcp_conv_id);
            nonCefServer_.store(false, std::memory_order_release);

            {
                std::lock_guard<std::mutex> lock(kcpMutex_);
                kcp_ = ikcp_create(packet.joinResponse.kcp_conv_id, this);
                kcp_->output = &CefNetwork::KcpOutputCallback;
                ikcp_nodelay(kcp_, 1, 10, 2, 1);
                ikcp_wndsize(kcp_, 128, 128);
            }

            FireSessionActive(true);
            state_.store(State::Connected, std::memory_order_release);

            // Deliver JoinResponse to the session layer (carries manifest).
            FirePacket(packet);
            break;
        }

        default:
            LOGW("[CefNet] unexpected raw packet type %u during handshake", (unsigned)packet.type);
            break;
    }
}

int CefNetwork::KcpOutputCallback(const char* buf, int len, ikcpcb* /*kcp*/, void* user)
{
    auto* self = static_cast<CefNetwork*>(user);
    self->SendRaw(reinterpret_cast<const uint8_t*>(buf), static_cast<size_t>(len));
    return 0;
}

void CefNetwork::SendRaw(const uint8_t* data, size_t size)
{
    if (socketFd_ < 0)
        return;
    if (send(socketFd_, data, size, 0) < 0)
        LOGE("[CefNet] udp send failed: %s", strerror(errno));
}

void CefNetwork::SendPacket(const cefproto::NetworkPacket& packet)
{
    std::vector<uint8_t> raw;
    if (!cefproto::SerializePacket(packet, raw))
    {
        LOGE("[CefNet] failed to serialize packet type %u", (unsigned)packet.type);
        return;
    }

    std::lock_guard<std::mutex> lock(kcpMutex_);

    if (kcp_ && state_.load(std::memory_order_acquire) == State::Connected)
    {
        if (!hasSessionKeys_)
        {
            LOGE("[CefNet] no tx key");
            return;
        }

        uint8_t txKey[cefcrypto::SESSION_KEY_BYTES]{};
        {
            std::lock_guard<std::mutex> lock(keysMutex_);
            std::memcpy(txKey, sessionKeys_.tx, sizeof(txKey));
        }
        std::vector<uint8_t> encrypted = cefcrypto::EncryptPacket(raw.data(), raw.size(), txKey);
        if (encrypted.empty())
        {
            LOGE("[CefNet] failed to encrypt packet");
            return;
        }

        if (ikcp_send(kcp_, reinterpret_cast<const char*>(encrypted.data()),
                      static_cast<int>(encrypted.size())) < 0)
        {
            LOGE("[CefNet] ikcp_send failed");
            return;
        }
        ikcp_flush(kcp_);
    }
    else
    {
        SendRaw(raw.data(), raw.size());
    }
}

void CefNetwork::SendBrowserCreateResult(int32_t browserId, bool success, int32_t code, const std::string& reason)
{
    cefproto::NetworkPacket pkt;
    pkt.type = cefproto::PacketType::ClientEmitEvent;
    pkt.emitEvent.browserId = browserId;
    pkt.emitEvent.name = cefproto::ClientEvent::BrowserCreateResult;
    pkt.emitEvent.args.push_back(cefproto::Argument::FromBool(success));
    pkt.emitEvent.args.push_back(cefproto::Argument::FromInt(code));
    pkt.emitEvent.args.push_back(cefproto::Argument::FromString(reason));
    SendPacket(pkt);
}

void CefNetwork::SendClientEmit(int32_t browserId, const std::string& name,
                                const std::vector<cefproto::Argument>& args)
{
    cefproto::NetworkPacket pkt;
    pkt.type = cefproto::PacketType::ClientEmitEvent;
    pkt.emitEvent.browserId = browserId;
    pkt.emitEvent.name = name;
    pkt.emitEvent.args = args;
    SendPacket(pkt);
}

void CefNetwork::SendRequestFiles(const std::vector<std::pair<std::string, std::string>>& files)
{
    cefproto::NetworkPacket pkt;
    pkt.type = cefproto::PacketType::RequestFiles;
    pkt.requestFiles.files = files;
    SendPacket(pkt);
}

void CefNetwork::SendDownloadComplete()
{
    cefproto::NetworkPacket pkt;
    pkt.type = cefproto::PacketType::DownloadComplete;
    SendPacket(pkt);
}

void CefNetwork::FireSessionActive(bool active)
{
    SessionHandler handler;
    {
        std::lock_guard<std::mutex> lock(handlerMutex_);
        handler = sessionHandler_;
    }
    if (handler)
        handler(active);
}

void CefNetwork::FirePacket(const cefproto::NetworkPacket& packet)
{
    PacketHandler handler;
    {
        std::lock_guard<std::mutex> lock(handlerMutex_);
        handler = packetHandler_;
    }
    if (handler)
        handler(packet);
}

void CefNetwork::SetPacketHandler(PacketHandler handler)
{
    std::lock_guard<std::mutex> lock(handlerMutex_);
    packetHandler_ = std::move(handler);
}

void CefNetwork::SetSessionHandler(SessionHandler handler)
{
    std::lock_guard<std::mutex> lock(handlerMutex_);
    sessionHandler_ = std::move(handler);
}
