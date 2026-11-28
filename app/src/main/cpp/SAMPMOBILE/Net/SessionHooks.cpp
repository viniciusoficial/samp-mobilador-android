#include "SessionHooks.h"
#include <vector>

namespace {
std::vector<SessionHookCallbacks>& HookRegistry()
{
	static std::vector<SessionHookCallbacks> s_hooks;
	return s_hooks;
}
} // namespace

namespace SessionHooks {

void Register(const SessionHookCallbacks& callbacks)
{
	HookRegistry().push_back(callbacks);
}

void NotifyConnect(const char* host, uint16_t port)
{
	for (const auto& h : HookRegistry()) {
		if (h.onConnect) h.onConnect(host, port);
	}
}

void NotifyDisconnect()
{
	for (const auto& h : HookRegistry()) {
		if (h.onDisconnect) h.onDisconnect();
	}
}

void NotifyHideOverlay()
{
	for (const auto& h : HookRegistry()) {
		if (h.onHideOverlay) h.onHideOverlay();
	}
}

void NotifyClientJoinRpc(int rpcId, RakNet::BitStream& bs)
{
	for (const auto& h : HookRegistry()) {
		if (h.onClientJoinRpc) h.onClientJoinRpc(rpcId, bs);
	}
}

void NotifyCustomPacket(uint8_t packetId, Packet* pkt)
{
	for (const auto& h : HookRegistry()) {
		if (h.onCustomPacket) h.onCustomPacket(packetId, pkt);
	}
}

} // namespace SessionHooks
