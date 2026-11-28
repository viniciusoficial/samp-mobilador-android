#pragma once

#include <cstdint>

struct Packet;
namespace RakNet {
class BitStream;
}

// Lightweight lifecycle seam allowing optional subsystems (e.g. Voice)
// to observe NetGame session events without hardcoding includes in Net/.
struct SessionHookCallbacks {
	void (*onConnect)(const char* host, uint16_t port) = nullptr;
	void (*onDisconnect)() = nullptr;
	void (*onHideOverlay)() = nullptr;
	void (*onClientJoinRpc)(int rpcId, RakNet::BitStream& bs) = nullptr;
	void (*onCustomPacket)(uint8_t packetId, Packet* pkt) = nullptr;
};

namespace SessionHooks {
void Register(const SessionHookCallbacks& callbacks);
void NotifyConnect(const char* host, uint16_t port);
void NotifyDisconnect();
void NotifyHideOverlay();
void NotifyClientJoinRpc(int rpcId, RakNet::BitStream& bs);
void NotifyCustomPacket(uint8_t packetId, Packet* pkt);
} // namespace SessionHooks
