#include "../Client.h"
#include "../Game/Game.h"
#include "../Net/NetGame.h"
#include "RakNet/SocketLayer.h"

#include "Network.h"


bool Network::Init() noexcept
{
	if (Network::initStatus) {
		return false;
	}

	LogVoice("[sv:dbg:network:init] : module initializing...");

	Network::socketHandle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (Network::socketHandle == INVALID_SOCKET) return false;

	if (const int sendBufferSize{kSendBufferSize}, recvBufferSize{kRecvBufferSize};
			setsockopt(Network::socketHandle, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const void*>
			(&sendBufferSize), sizeof(sendBufferSize)) == SOCKET_ERROR ||
					setsockopt(Network::socketHandle, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const void*>
					(&recvBufferSize), sizeof(recvBufferSize)) == SOCKET_ERROR) {
		close(Network::socketHandle);
		Network::socketHandle = INVALID_SOCKET;
		return false;
	}

	// Set a 200ms receive timeout so VoiceThread can exit cleanly and be joined
	// without blocking indefinitely in recv() on disconnect/shutdown.
	struct timeval rcvTimeout{};
	rcvTimeout.tv_sec = 0;
	rcvTimeout.tv_usec = 200000;
	setsockopt(Network::socketHandle, SOL_SOCKET, SO_RCVTIMEO, &rcvTimeout, sizeof(rcvTimeout));

	// call the function so no need raknet file
	/*RakNet::AddConnectCallback(Network::OnRaknetConnect);
	RakNet::AddReceiveCallback(Network::OnRaknetReceive);
	RakNet::AddRpcCallback(Network::OnRaknetRpc);
	RakNet::AddDisconnectCallback(Network::OnRaknetDisconnect);*/

	Network::connectionStatus = ConnectionStatus::Disconnected;

	LogVoice("[sv:dbg:network:init] : module initialized");

	Network::initStatus = true;

	return true;
}

void Network::Free() noexcept
{
	if (!Network::initStatus) {
		return;
	}

	LogVoice("[sv:dbg:network:free] : module releasing...");

	Network::connectionStatus = ConnectionStatus::Disconnected;
	Network::voiceEpoch++;

	if (Network::socketHandle != INVALID_SOCKET) {
		shutdown(Network::socketHandle, SHUT_RDWR);
	}

	if (Network::voiceThread.joinable()) {
		Network::voiceThread.join();
	}

	if (Network::socketHandle != INVALID_SOCKET) {
		close(Network::socketHandle);
		Network::socketHandle = INVALID_SOCKET;
	}

	Network::serverIp.clear();
	Network::serverKey = NULL;

	/*ZeroMemory(Network::outputVoicePacket.GetData(), Network::outputVoicePacket.GetSize());*/
	memset(Network::outputVoicePacket.GetData(), 0, Network::outputVoicePacket.GetSize());

	{
		std::lock_guard<std::mutex> lock(controlQueueMutex);
		for (auto& packet : Network::controlQueue) {
			if (packet) {
				delete[] packet->data;
				delete packet;
			}
		}
		Network::controlQueue.clear();
	}

	while (!Network::voiceQueue.empty())
		Network::voiceQueue.pop();

	LogVoice("[sv:dbg:network:free] : module released");

	Network::initStatus = false;
}

bool Network::SendControlPacket(const uint16_t packet, const void* dataAddr, const uint16_t dataSize) noexcept
{
	if (Network::connectionStatus != ConnectionStatus::Connected) {
		return false;
	}

	RakNet::BitStream bsSend;

	bsSend.Write<uint8_t>(kRaknetPacketId);

	bsSend.Write<uint16_t>(packet);
	bsSend.Write<uint16_t>(dataSize);

	if (dataAddr != nullptr && dataSize != 0) {
		bsSend.Write(static_cast<const char*>(dataAddr), dataSize);
	}

	if (!pNetGame || !pNetGame->GetRakClient()) return false;

	return pNetGame->GetRakClient()->Send(&bsSend, HIGH_PRIORITY, RELIABLE, 0);
}

bool Network::SendVoicePacket(const void* dataAddr, const uint16_t dataSize) noexcept
{
	if (dataAddr == nullptr || dataSize == 0 || dataSize > kMaxVoiceDataSize) {
		return false;
	}

	if (Network::connectionStatus != ConnectionStatus::Connected) {
		return false;
	}

	Network::outputVoicePacket->length = dataSize;
	Network::outputVoicePacket->CalcHash();

	std::memcpy(Network::outputVoicePacket->data, dataAddr, dataSize);

	const auto voicePacketAddr = reinterpret_cast<const char*>(&Network::outputVoicePacket);
	const auto voicePacketSize = static_cast<int>(Network::outputVoicePacket->GetFullSize());

	const auto sended = send(Network::socketHandle, voicePacketAddr, voicePacketSize, NULL);

	++Network::outputVoicePacket->packid;

	return sended == voicePacketSize;
}

void Network::EndSequence() noexcept
{
	if (!Network::initStatus) {
		return;
	}

	Network::outputVoicePacket->packid = NULL;
}

ControlPacket* Network::ReceiveControlPacket() noexcept
{
	std::lock_guard<std::mutex> lock(controlQueueMutex);
	if (!Network::initStatus || Network::controlQueue.empty()) {
		return nullptr;
	}

	auto controlPacket = Network::controlQueue.front();
	Network::controlQueue.pop_front();

	return controlPacket;
}

VoicePacketContainerPtr Network::ReceiveVoicePacket() noexcept
{
	if (!Network::initStatus || Network::voiceQueue.empty()) {
		return nullptr;
	}

	auto voicePacket = std::move(*Network::voiceQueue.front());
	Network::voiceQueue.pop();

	return voicePacket;
}

std::size_t Network::AddConnectCallback(ConnectCallback callback) noexcept
{
	if (!Network::initStatus) return -1;

	Network::connectCallbacks.emplace_back(callback);
	return Network::connectCallbacks.size() - 1;
}

std::size_t Network::AddSvConnectCallback(SvConnectCallback callback) noexcept
{
	if (!Network::initStatus) return -1;

	Network::svConnectCallbacks.emplace_back(std::move(callback));
	return Network::svConnectCallbacks.size() - 1;
}

std::size_t Network::AddSvInitCallback(SvInitCallback callback) noexcept
{
	if (!Network::initStatus) return -1;

	Network::svInitCallbacks.emplace_back(std::move(callback));
	return Network::svInitCallbacks.size() - 1;
}

std::size_t Network::AddDisconnectCallback(DisconnectCallback callback) noexcept
{
	if (!Network::initStatus) return -1;

	Network::disconnectCallbacks.emplace_back(std::move(callback));
	return Network::disconnectCallbacks.size() - 1;
}

void Network::VoiceThread(const uint32_t generation) noexcept
{
	VoicePacket keepAlivePacket;
	VoicePacketContainer inputVoicePacket{kMaxVoiceDataSize};
	Timer::time_t keepAliveLastTime{NULL};

	//ZeroMemory(&keepAlivePacket, sizeof(keepAlivePacket));
	memset(&keepAlivePacket, 0, sizeof(keepAlivePacket));

	keepAlivePacket.svrkey = Network::serverKey;
	keepAlivePacket.packet = SV::VoicePacketType::keepAlive;
	keepAlivePacket.packid = NULL;
	keepAlivePacket.length = NULL;
	keepAlivePacket.sender = NULL;
	keepAlivePacket.stream = NULL;
	keepAlivePacket.CalcHash();

	while (true) {
		const auto curStatus = Network::connectionStatus.load();

		if (curStatus == ConnectionStatus::Disconnected) break;
		if (generation != Network::voiceEpoch.load()) break;

		// Sending keep-alive packets...
		// -----------------------------------------------------------------

		const auto curTime = Timer::Get();

		if (curTime - keepAliveLastTime >= kKeepAliveInterval) {
			send(Network::socketHandle, reinterpret_cast<const char*>(&keepAlivePacket),
					sizeof(keepAlivePacket), NULL);

			keepAliveLastTime = curTime;
		}

		// Receiving voice packets...
		// -----------------------------------------------------------------

		if (curStatus != ConnectionStatus::Connected) {
			SleepForMilliseconds(100);
			continue;
		}

		const auto received = recv(Network::socketHandle,
				static_cast<char*>(inputVoicePacket.GetData()),
				inputVoicePacket.GetSize(), NULL);

		if (received == SOCKET_ERROR) {
			if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
				continue;
			}
			break;
		}

		if (received < static_cast<decltype(received)>(sizeof(VoicePacket))) {
			continue;
		}

		if (!inputVoicePacket->CheckHeader()) continue;
		if (received != inputVoicePacket->GetFullSize()) continue;
		if (inputVoicePacket->packet == SV::VoicePacketType::keepAlive) continue;

		Network::voiceQueue.try_emplace(MakeVoicePacketContainer(inputVoicePacket));
	}
}

void Network::OnRaknetConnect(const char* ip, const uint32_t port) noexcept
{
	if (!Network::initStatus) {
		return;
	}

	LogVoice("[dbg:raknet:client:connect] : connecting to game server '*.*.*.*:****'...", ip, port);

	Network::serverIp = ip ? ip : "";
	Network::serverResolvedAddr = inet_addr(Network::serverIp.c_str());

	if (Network::connectionStatus == ConnectionStatus::Disconnected) {
		for (const auto& connectCallback : Network::connectCallbacks) {
			if (connectCallback != nullptr) {
				connectCallback(Network::serverIp, port);
			}
		}
	}

	Network::connectionStatus = ConnectionStatus::RNConnecting;

	LogVoice("[dbg:raknet:client:connect] : connected");
}

bool Network::OnRaknetRpc(const int id, RakNet::BitStream& parameters) noexcept
{
	if (!Network::initStatus || id != kRaknetConnectRcpId) {
		return true;
	}

	if (Network::svConnectCallbacks.empty()) {
		return true;
	}

	SV::ConnectPacket stData{};

	for (const auto& svConnectCallback : Network::svConnectCallbacks) {
		if (svConnectCallback != nullptr) {
			svConnectCallback(stData);
		}
	}

	parameters.Write(reinterpret_cast<const char*>(&stData), sizeof(stData));

	LogVoice("[sv:dbg:network:connect] : raknet connecting... "
			 "(version:%hhu;micro:%hhu)", stData.version, stData.micro);

	return true;
}

bool Network::OnRaknetReceive(Packet* packet) noexcept
{
//    if(*packet->data != kRaknetPacketId)
//        return true;
	if (!packet || !packet->data || packet->length < 5) {
		return false;
	}
	if (packet->playerId.binaryAddress != 0 && packet->playerId.binaryAddress != INADDR_NONE) {
		Network::serverResolvedAddr = packet->playerId.binaryAddress;
	}

	auto controlPacketPtr = new ControlPacket();
	controlPacketPtr->data = nullptr;

	RakNet::BitStream bs((unsigned char*) packet->data, packet->length, false);
	bs.IgnoreBits(8); // skip packet and rpc id

	if (!bs.Read(controlPacketPtr->packet) || !bs.Read(controlPacketPtr->length) ||
			controlPacketPtr->length > (uint16_t) (bs.GetNumberOfUnreadBits() / 8)) {
		delete controlPacketPtr;
		return false;
	}

	if (controlPacketPtr->length) {
		controlPacketPtr->data = new uint8_t[controlPacketPtr->length];
		if (!bs.Read(reinterpret_cast<char*>(controlPacketPtr->data), controlPacketPtr->length)) {
			delete[] controlPacketPtr->data;
			delete controlPacketPtr;
			return false;
		}
	}

	//  memcpy(controlPacketPtr, packet->data, sizeof(ControlPacket));
	//controlPacketPtr = reinterpret_cast<ControlPacket*>(packet->data + sizeof(uint8_t));

	switch (controlPacketPtr->packet) {
		case SV::ControlPacketType::serverInfo: {
			if (controlPacketPtr->length != sizeof(SV::ServerInfoPacket)) {
				delete[] controlPacketPtr->data;
				delete controlPacketPtr;
				return false;
			}

			const auto& stData = *reinterpret_cast<SV::ServerInfoPacket*>(controlPacketPtr->data);

			LogVoice("[sv:dbg:network:serverInfo] : connecting to voiceserver "
					 "'*.*.*.*:%hu'...", Network::serverIp.c_str(), stData.serverPort);

			sockaddr_in serverAddress{};

			serverAddress.sin_family = AF_INET;
			uint32_t resolvedAddr = inet_addr(Network::serverIp.c_str());
			if (resolvedAddr == INADDR_NONE) {
				resolvedAddr = Network::serverResolvedAddr;
			}
			if (resolvedAddr == INADDR_NONE) {
				if (const char* ipStr = SocketLayer::Instance()->DomainNameToIP(Network::serverIp.c_str())) {
					resolvedAddr = inet_addr(ipStr);
				}
			}
			if (resolvedAddr == INADDR_NONE || resolvedAddr == 0) {
				LogVoice("[sv:err:network:serverInfo] : failed to resolve server address");
				delete[] controlPacketPtr->data;
				delete controlPacketPtr;
				return false;
			}
			serverAddress.sin_addr.s_addr = resolvedAddr;
			serverAddress.sin_port = htons(stData.serverPort);

			if (Network::voiceThread.joinable()) {
				Network::voiceEpoch++;
				Network::voiceThread.join();
			}

			if (connect(Network::socketHandle, reinterpret_cast<const sockaddr*>(&serverAddress),
					sizeof(serverAddress)) == SOCKET_ERROR) {
				LogVoice("[sv:err:network:serverInfo] : connect error.");
				delete[] controlPacketPtr->data;
				delete controlPacketPtr;
				return false;
			}

			Network::serverKey = stData.serverKey;

			Network::outputVoicePacket->svrkey = Network::serverKey;
			Network::outputVoicePacket->packet = SV::VoicePacketType::voicePacket;
			Network::outputVoicePacket->packid = NULL;
			Network::outputVoicePacket->length = NULL;
			Network::outputVoicePacket->sender = NULL;
			Network::outputVoicePacket->stream = NULL;
			Network::outputVoicePacket->CalcHash();

			Network::connectionStatus = ConnectionStatus::SVConnecting;

			Network::voiceThread = std::thread(Network::VoiceThread, Network::voiceEpoch.load());

			delete[] controlPacketPtr->data;
			delete controlPacketPtr;
		}
			break;
		case SV::ControlPacketType::pluginInit: {
			if (controlPacketPtr->length != sizeof(SV::PluginInitPacket)) {
				delete[] controlPacketPtr->data;
				delete controlPacketPtr;
				return false;
			}

			const auto& stData = *reinterpret_cast<SV::PluginInitPacket*>(controlPacketPtr->data);

			LogVoice("[sv:dbg:network:pluginInit] : plugin init packet "
					 "(bitrate:%u;mute:%hhu)", stData.bitrate, stData.mute);

			for (const auto& svInitCallback : Network::svInitCallbacks) {
				if (svInitCallback != nullptr) {
					svInitCallback(stData);
				}
			}

			Network::connectionStatus = ConnectionStatus::Connected;

			delete[] controlPacketPtr->data;
			delete controlPacketPtr;
		}
			break;
		default: {
			std::lock_guard<std::mutex> lock(controlQueueMutex);
			Network::controlQueue.push_back(controlPacketPtr);
		}
	}

	return false;
}

void Network::OnRaknetDisconnect() noexcept
{
	if (!Network::initStatus) {
		return;
	}

	LogVoice("[sv:dbg:network:disconnect] : raknet disconnected");

	if (Network::connectionStatus != ConnectionStatus::Disconnected) {
		for (const auto& disconnectCallback : Network::disconnectCallbacks) {
			if (disconnectCallback != nullptr) {
				disconnectCallback();
			}
		}
	}

	Network::connectionStatus = ConnectionStatus::Disconnected;
	Network::voiceEpoch++;

	if (Network::voiceThread.joinable()) {
		Network::voiceThread.join();
	}

	Network::serverIp.clear();
	Network::serverKey = NULL;

	/*ZeroMemory(Network::outputVoicePacket.GetData(), Network::outputVoicePacket.GetSize());*/
	memset(Network::outputVoicePacket.GetData(), 0, Network::outputVoicePacket.GetSize());

	{
		std::lock_guard<std::mutex> lock(controlQueueMutex);
		for (auto& packet : Network::controlQueue) {
			if (packet) {
				delete[] packet->data;
				delete packet;
			}
		}
		Network::controlQueue.clear();
	}

	while (!Network::voiceQueue.empty())
		Network::voiceQueue.pop();
}