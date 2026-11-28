#pragma once

// Game-thread session layer for the omp-cef compatible Android client.
// Mirrors the connect/disconnect supervision of src/client/core/app.cpp:
//
//   * watches the SA-MP NetGame on the game thread
//   * derives the CEF endpoint as <server host>:<game port + 2>
//   * (re)connects CefNetwork with the local player id
//   * resets the CEF session when SA-MP disconnects
//   * moves packets from the network thread to the game thread
//
// Stage 2 scope: transport + handshake + packet dispatch plumbing with
// logging. Browser/resource behaviour (stages 3-5) hooks into the
// BrowserEventHandler / resource callbacks without touching this file.

#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "CefNetwork.h"
#include "CefProtocol.h"

class CefSession
{
public:
    static CefSession& Get();

    // Called every game tick from Client::process() (game thread).
    void Tick();

    // Debug-only: run the handshake against an explicit endpoint without a
    // live SA-MP session. Used by tools/cef_kat/cef_server to verify the
    // browser layer on a device that is not connected to a game server.
    // The normal SA-MP supervised path resumes after DebugDisconnect().
    void DebugConnect(const std::string& host, uint16_t port, int32_t playerId);
    void DebugDisconnect();

    CefNetwork& Network() { return network_; }

    bool IsConnected() const
    {
        return network_.GetState() == CefNetwork::State::Connected;
    }

    // Server -> client browser command (CreateBrowser, SetBrowserVisible, ...).
    // Fired on the game thread. Installed by the browser layer (stage 3).
    using ServerEventHandler = std::function<void(const cefproto::EmitEventPacket&)>;
    // Server -> page event (EmitBrowserEvent packet), game thread (stage 4).
    using BrowserEventHandler = std::function<void(const cefproto::EmitEventPacket&)>;
    // The CEF session is being torn down (SA-MP disconnected, player id
    // changed, or the debug override was cancelled). Fired on the game thread
    // before the state is reset, so live WebViews can be closed. Stage 4.
    using SessionLostHandler = std::function<void()>;

    void SetServerEventHandler(ServerEventHandler handler);
    void SetBrowserEventHandler(BrowserEventHandler handler);
    void SetSessionLostHandler(SessionLostHandler handler);

    // Accessors for the resource layer (stage 5).
    const std::string& ManifestJson() const { return manifestJson_; }
    const std::vector<uint8_t>& MasterResourceKey() const { return masterResourceKey_; }

    // Single entry point used by the resource layer to put RequestFiles /
    // DownloadComplete on the wire. Returns false when not connected.
    bool SendPacket(const cefproto::NetworkPacket& packet);

private:
    CefSession();

    void OnNetworkPacket(const cefproto::NetworkPacket& packet);   // network thread
    void OnSessionActive(bool active);                              // network thread
    void DrainPacketQueue();                                        // game thread
    void DispatchPacket(const cefproto::NetworkPacket& packet);     // game thread
    void ResetSession();
    void FireSessionLost();

private:
    static constexpr int CEF_PORT_OFFSET = 2;              // app.hpp: cef_port_offset_
    static constexpr uint64_t CONNECT_RETRY_BACKOFF_MS = 500;

    CefNetwork network_;

    std::mutex queueMutex_;
    std::deque<cefproto::NetworkPacket> packetQueue_;

    std::mutex handlerMutex_;
    ServerEventHandler serverEventHandler_;
    BrowserEventHandler browserEventHandler_;
    SessionLostHandler sessionLostHandler_;

    // Supervision state (game thread only).
    bool endpointReady_ = false;
    std::string endpointHost_;
    uint16_t endpointPort_ = 0;
    int32_t connectedPlayerId_ = -1;
    uint64_t nextConnectAttemptMs_ = 0;

    // Debug override (guarded by debugMutex_ because it is set from the UI
    // thread by the debug broadcast receiver).
    std::mutex debugMutex_;
    bool debugActive_ = false;
    std::string debugHost_;
    uint16_t debugPort_ = 0;
    int32_t debugPlayerId_ = -1;

    std::string manifestJson_;
    std::vector<uint8_t> masterResourceKey_;
};
