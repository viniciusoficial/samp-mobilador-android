#include "../Client.h"
#include "../UI/UI.h"
#include "../UI/SAMPWidgets/Chat.h"
#include "../UI/SAMPWidgets/VoiceButton.h"

#include "Plugin.h"
#include "ControlPacket.h"
#include "Header.h"
#include "Stream.h"
#include "include/util/Timer.h"

#include "Record.h"
#include "Playback.h"
#include "Network.h"
#include "PluginConfig.h"
#include "SpeakerList.h"
#include "GlobalStream.h"
#include "StreamAtPoint.h"
#include "StreamAtVehicle.h"
#include "StreamAtPlayer.h"
#include "StreamAtObject.h"

#include "../Net/SessionHooks.h"
#include "../Settings.h"
#include <RakNet/PacketEnumerations.h>
#include <android/log.h>
#include <spdlog/spdlog.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>

void LogVoice(const char* fmt, ...)
{
	static std::mutex s_logVoiceMutex;
	static FILE* flLog = nullptr;
	std::lock_guard<std::mutex> lock(s_logVoiceMutex);

	char buffer[512];
	if (flLog == nullptr && Client::gameDir() != nullptr) {
		snprintf(buffer, sizeof(buffer), "%sSAMP/%s", Client::gameDir(), SV::kLogFileName);
		flLog = fopen(buffer, "w");
	}

	memset(buffer, 0, sizeof(buffer));

	va_list arg;
	va_start(arg, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, arg);
	va_end(arg);

	__android_log_write(ANDROID_LOG_INFO, "SAMPMOBILE", buffer);

	if (flLog == nullptr) {
		return;
	}

	fprintf(flLog, "%s\n", buffer);
	if (Client::isLogLevelDebug()) {
		fflush(flLog);
	}
}

namespace {
bool g_initialized = false;
bool g_worldReady = false;
bool g_muteStatus = false;
bool g_recordStatus = false;
bool g_recordBusy = false;
std::map<uint32_t, StreamPtr> g_streamTable;

void ConnectHandler(const std::string& serverIp, uint16_t serverPort);
void PluginConnectHandler(SV::ConnectPacket& connectStruct);
bool PluginInitHandler(const SV::PluginInitPacket& initPacket);
void ControlPacketHandler(const ControlPacket& controlPacket);
void DisconnectHandler();
void MainLoop();
} // namespace

bool Plugin::initialize() noexcept
{
	if (g_initialized) {
		return true;
	}

	if (!Network::Init()) {
		LogVoice("[sv:err:plugin] : failed to init network");
		return false;
	}

	Network::AddConnectCallback(ConnectHandler);
	Network::AddSvConnectCallback(PluginConnectHandler);
	Network::AddSvInitCallback(PluginInitHandler);
	Network::AddDisconnectCallback(DisconnectHandler);

	if (!Playback::Init()) {
		LogVoice("[sv:err:plugin] : failed to init playback");
		Network::Free();
		return false;
	}

	SessionHookCallbacks hooks{};
	hooks.onConnect = [](const char* host, uint16_t port) {
		if (Settings::voice()) {
			spdlog::info("Voice session connect {}:{}", host ? host : "", port);
			Network::OnRaknetConnect(host, port);
		}
	};
	hooks.onDisconnect = []() {
		Network::OnRaknetDisconnect();
		SpeakerList::Hide();
	};
	hooks.onHideOverlay = []() {
		if (Settings::voice()) {
			SpeakerList::Hide();
		}
	};
	hooks.onClientJoinRpc = [](int rpcId, RakNet::BitStream& bs) {
		if (Settings::voice()) {
			Network::OnRaknetRpc(rpcId, bs);
			SpeakerList::Hide();
		}
	};
	hooks.onCustomPacket = [](uint8_t packetId, Packet* pkt) {
		if (packetId == ID_VOICE_SYNC && Settings::voice()) {
			Network::OnRaknetReceive(pkt);
		}
	};
	SessionHooks::Register(hooks);

	g_initialized = true;
	return true;
}

void Plugin::onUiDeviceReady() noexcept
{
	if (!g_initialized) return;
	LogVoice("[sv:dbg:plugin] : device init");
	SpeakerList::Init();
}

void Plugin::onWorldReady() noexcept
{
	if (!g_initialized) return;
	LogVoice("[dbg:samp:load] : module loading...");
	g_worldReady = true;
	LogVoice("[dbg:samp:load] : module loaded");
}

void Plugin::onUiRender(bool netActive, bool hideSpeakerList)
{
	if (!g_initialized || !netActive) return;

	if (!Settings::voiceSpeakerList() || hideSpeakerList) {
		SpeakerList::Hide();
	}
	else {
		SpeakerList::Show();
	}

	Timer::Tick();
	MainLoop();
	SpeakerList::Render();
}

void Plugin::syncSettings(bool applyRuntime) noexcept
{
	const bool enabled = Settings::voice();
	const int vol = Settings::voiceVolume();
	const bool balancer = Settings::voiceBalancer();
	const bool filter = Settings::voiceFilter();
	const bool micEnable = enabled && Settings::voiceMicEnable();
	const int micVol = Settings::voiceMicVolume();

	PluginConfig::SetSoundEnable(enabled);
	PluginConfig::SetSoundVolume(vol);
	PluginConfig::SetSoundBalancer(balancer);
	PluginConfig::SetSoundFilter(filter);
	PluginConfig::SetMicroEnable(micEnable);
	PluginConfig::SetMicroVolume(micVol);

	if (applyRuntime) {
		Playback::SetSoundEnable(enabled);
		Playback::SetSoundVolume(vol);
		Playback::SetSoundBalancer(balancer);
		Playback::SetSoundFilter(filter);
		Record::SetMicroEnable(micEnable);
		Record::SetMicroVolume(micVol);
	}
}

namespace {

void MainLoop()
{
	if (!g_worldReady) return;

	while (const auto controlPacket = Network::ReceiveControlPacket()) {
		ControlPacketHandler(*controlPacket);
		delete[] controlPacket->data;
		delete controlPacket;
	}

	while (const auto voicePacket = Network::ReceiveVoicePacket()) {
		const auto& voicePacketRef = *voicePacket;

		const auto iter = g_streamTable.find(voicePacketRef->stream);
		if (iter == g_streamTable.end()) continue;

		iter->second->Push(*&voicePacketRef);
	}

	for (const auto& stream : g_streamTable)
		stream.second->Tick();

	Playback::Tick();
	Record::Tick();

	int defaultKey = 0x42;
	VoiceButton* vbutton = pUI ? pUI->voicebutton() : nullptr;

	if (!g_muteStatus) {
		if (vbutton && vbutton->recording() && g_recordStatus == 0) {
			if (PluginConfig::GetMicroEnable()) {
				LogVoice("[sv:dbg:plugin] : activation key(%hhu) pressed", defaultKey);

				if (!g_recordBusy) {
					g_recordStatus = true;
					Record::StartRecording();
				}

				SV::PressKeyPacket pressKeyPacket{};

				pressKeyPacket.keyId = defaultKey;

				if (!Network::SendControlPacket(SV::ControlPacketType::pressKey, &pressKeyPacket, sizeof(pressKeyPacket))) {
					LogVoice("[sv:err:main:HookWndProc] : failed to send PressKey packet");
				}
			}
		}
		if ((!vbutton || !vbutton->recording()) && g_recordStatus == 1) {
			if (PluginConfig::GetMicroEnable()) {
				LogVoice("[sv:dbg:plugin] : activation key(%hhu) released", defaultKey);

				if (!g_recordBusy) {
					g_recordStatus = false;
				}

				SV::ReleaseKeyPacket releaseKeyPacket{};

				releaseKeyPacket.keyId = defaultKey;

				if (!Network::SendControlPacket(SV::ControlPacketType::releaseKey, &releaseKeyPacket, sizeof(releaseKeyPacket))) {
					LogVoice("[sv:err:main:HookWndProc] : failed to send ReleaseKey packet");
				}
			}
		}
	}
	else {
		static bool s_mutedNoticeShown = false;
		const bool pressed = vbutton && vbutton->recording();
		if (pressed && !s_mutedNoticeShown) {
			s_mutedNoticeShown = true;
			if (pUI && pUI->chat()) {
				pUI->chat()->addInfoMessage("-> You are muted");
			}
		}
		else if (!pressed) {
			s_mutedNoticeShown = false;
		}
	}

	uint8_t frameBuffer[Network::kMaxVoiceDataSize];
	if (const auto frameSize = Record::GetFrame(frameBuffer, sizeof(frameBuffer))) {
		if (!Network::SendVoicePacket(frameBuffer, frameSize)) {
			LogVoice("[sv:err:plugin] : failed to send voice packet");
		}

		if (!g_recordStatus) {
			Record::StopRecording();
			Network::EndSequence();
		}
	}
}

void ConnectHandler(const std::string& serverIp, const uint16_t serverPort)
{
	(void) serverIp;
	(void) serverPort;
}

void PluginConnectHandler(SV::ConnectPacket& connectStruct)
{
	connectStruct.signature = SV::kSignature;
	connectStruct.version = SV::kVersion;
	connectStruct.micro = Record::HasMicro();
}

bool PluginInitHandler(const SV::PluginInitPacket& initPacket)
{
	g_muteStatus = initPacket.mute;

	if (!Record::Init(initPacket.bitrate)) {
		LogVoice("[sv:inf:plugin:packet:init] : failed init record");
	}

	return true;
}

void ControlPacketHandler(const ControlPacket& controlPacket)
{
	switch (controlPacket.packet) {
		case SV::ControlPacketType::muteEnable: {
			if (controlPacket.length != 0) break;

			LogVoice("[sv:dbg:plugin:muteenable]");

			g_muteStatus = true;
			g_recordStatus = false;
			g_recordBusy = false;
		}
			break;
		case SV::ControlPacketType::muteDisable: {
			if (controlPacket.length != 0) break;

			LogVoice("[sv:dbg:plugin:mutedisable]");

			g_muteStatus = false;
		}
			break;
		case SV::ControlPacketType::startRecord: {
			if (controlPacket.length != 0) break;

			LogVoice("[sv:dbg:plugin:startrecord]");

			if (g_muteStatus) break;

			g_recordBusy = true;
			g_recordStatus = true;

			Record::StartRecording();
		}
			break;
		case SV::ControlPacketType::stopRecord: {
			if (controlPacket.length != 0) break;

			LogVoice("[sv:dbg:plugin:stoprecord]");

			if (g_muteStatus) break;

			g_recordStatus = false;
			g_recordBusy = false;
		}
			break;
		case SV::ControlPacketType::addKey: {
			if (!controlPacket.data || controlPacket.length != sizeof(SV::AddKeyPacket)) break;
			const auto& stData = *reinterpret_cast<const SV::AddKeyPacket*>(controlPacket.data);

			LogVoice("[sv:dbg:plugin:addkey] : keyid(0x%hhx)", stData.keyId);
			LogVoice("[dbg:keyfilter] : adding key (0x%hhx)", stData.keyId);
		}
			break;
		case SV::ControlPacketType::removeKey: {
			if (!controlPacket.data || controlPacket.length != sizeof(SV::RemoveKeyPacket)) break;
			const auto& stData = *reinterpret_cast<const SV::RemoveKeyPacket*>(controlPacket.data);

			LogVoice("[sv:dbg:plugin:removekey] : keyid(0x%hhx)", stData.keyId);
			LogVoice("[dbg:keyfilter] : removing key (0x%hhx)", stData.keyId);
		}
			break;
		case SV::ControlPacketType::removeAllKeys: {
			if (controlPacket.length) break;

			LogVoice("[sv:dbg:plugin:removeallkeys]");
			LogVoice("[dbg:keyfilter] : removing all keys");
		}
			break;
		case SV::ControlPacketType::createGStream: {
			if (!controlPacket.data || controlPacket.length < sizeof(SV::CreateGStreamPacket)) break;
			const auto& stData = *reinterpret_cast<const SV::CreateGStreamPacket*>(controlPacket.data);
			const size_t nameMax = controlPacket.length - sizeof(SV::CreateGStreamPacket);
			const std::string safeName(stData.name, strnlen(stData.name, nameMax));

			LogVoice("[sv:dbg:plugin:creategstream] : stream(%p), color(0x%x), name(%s)",
					stData.stream, stData.color, stData.color ? safeName.c_str() : "");

			auto oldIt = g_streamTable.find(stData.stream);
			if (oldIt != g_streamTable.end()) {
				SpeakerList::OnStreamDelete(oldIt->second.get());
			}
			const auto& streamPtr = g_streamTable[stData.stream] =
											MakeGlobalStream(stData.color, safeName);

			streamPtr->AddPlayCallback(SpeakerList::OnSpeakerPlay);
			streamPtr->AddStopCallback(SpeakerList::OnSpeakerStop);
		}
			break;
		case SV::ControlPacketType::createLPStream: {
			if (!controlPacket.data || controlPacket.length < sizeof(SV::CreateLPStreamPacket)) break;
			const auto& stData = *reinterpret_cast<const SV::CreateLPStreamPacket*>(controlPacket.data);
			const size_t nameMax = controlPacket.length - sizeof(SV::CreateLPStreamPacket);
			const std::string safeName(stData.name, strnlen(stData.name, nameMax));

			LogVoice("[sv:dbg:plugin:createlpstream] : "
					 "stream(%p), dist(%.2f), pos(%.2f;%.2f;%.2f), color(0x%x), name(%s)",
					stData.stream, stData.distance, stData.position.x, stData.position.y, stData.position.z,
					stData.color, stData.color ? safeName.c_str() : "");

			auto oldIt = g_streamTable.find(stData.stream);
			if (oldIt != g_streamTable.end()) {
				SpeakerList::OnStreamDelete(oldIt->second.get());
			}
			const auto& streamPtr = g_streamTable[stData.stream] =
											MakeStreamAtPoint(stData.color, safeName, stData.distance, stData.position);

			streamPtr->AddPlayCallback(SpeakerList::OnSpeakerPlay);
			streamPtr->AddStopCallback(SpeakerList::OnSpeakerStop);
		}
			break;
		case SV::ControlPacketType::createLStreamAtVehicle: {
			if (!controlPacket.data || controlPacket.length < sizeof(SV::CreateLStreamAtPacket)) break;
			const auto& stData = *reinterpret_cast<const SV::CreateLStreamAtPacket*>(controlPacket.data);
			const size_t nameMax = controlPacket.length - sizeof(SV::CreateLStreamAtPacket);
			const std::string safeName(stData.name, strnlen(stData.name, nameMax));

			LogVoice("[sv:dbg:plugin:createlstreamatvehicle] : "
					 "stream(%p), dist(%.2f), vehicle(%hu), color(0x%x), name(%s)",
					stData.stream, stData.distance, stData.target,
					stData.color, stData.color ? safeName.c_str() : "");

			auto oldIt = g_streamTable.find(stData.stream);
			if (oldIt != g_streamTable.end()) {
				SpeakerList::OnStreamDelete(oldIt->second.get());
			}
			const auto& streamPtr = g_streamTable[stData.stream] =
											MakeStreamAtVehicle(stData.color, safeName, stData.distance, stData.target);

			streamPtr->AddPlayCallback(SpeakerList::OnSpeakerPlay);
			streamPtr->AddStopCallback(SpeakerList::OnSpeakerStop);
		}
			break;
		case SV::ControlPacketType::createLStreamAtPlayer: {
			if (!controlPacket.data || controlPacket.length < sizeof(SV::CreateLStreamAtPacket)) break;
			const auto& stData = *reinterpret_cast<const SV::CreateLStreamAtPacket*>(controlPacket.data);
			const size_t nameMax = controlPacket.length - sizeof(SV::CreateLStreamAtPacket);
			const std::string safeName(stData.name, strnlen(stData.name, nameMax));

			LogVoice("[sv:dbg:plugin:createlstreamatplayer] : "
					 "stream(%p), dist(%.2f), player(%hu), color(0x%x), name(%s)",
					stData.stream, stData.distance, stData.target,
					stData.color, stData.color ? safeName.c_str() : "");

			auto oldIt = g_streamTable.find(stData.stream);
			if (oldIt != g_streamTable.end()) {
				SpeakerList::OnStreamDelete(oldIt->second.get());
			}
			const auto& streamPtr = g_streamTable[stData.stream] =
											MakeStreamAtPlayer(stData.color, safeName, stData.distance, stData.target);

			streamPtr->AddPlayCallback(SpeakerList::OnSpeakerPlay);
			streamPtr->AddStopCallback(SpeakerList::OnSpeakerStop);
		}
			break;
		case SV::ControlPacketType::createLStreamAtObject: {
			if (!controlPacket.data || controlPacket.length < sizeof(SV::CreateLStreamAtPacket)) break;
			const auto& stData = *reinterpret_cast<const SV::CreateLStreamAtPacket*>(controlPacket.data);
			const size_t nameMax = controlPacket.length - sizeof(SV::CreateLStreamAtPacket);
			const std::string safeName(stData.name, strnlen(stData.name, nameMax));

			LogVoice("[sv:dbg:plugin:createlstreamatobject] : "
					 "stream(%p), dist(%.2f), object(%hu), color(0x%x), name(%s)",
					stData.stream, stData.distance, stData.target,
					stData.color, stData.color ? safeName.c_str() : "");

			auto oldIt = g_streamTable.find(stData.stream);
			if (oldIt != g_streamTable.end()) {
				SpeakerList::OnStreamDelete(oldIt->second.get());
			}
			const auto& streamPtr = g_streamTable[stData.stream] =
											MakeStreamAtObject(stData.color, safeName, stData.distance, stData.target);

			streamPtr->AddPlayCallback(SpeakerList::OnSpeakerPlay);
			streamPtr->AddStopCallback(SpeakerList::OnSpeakerStop);
		}
			break;
		case SV::ControlPacketType::updateLStreamDistance: {
			if (!controlPacket.data || controlPacket.length != sizeof(SV::UpdateLStreamDistancePacket)) break;
			const auto& stData = *reinterpret_cast<const SV::UpdateLStreamDistancePacket*>(controlPacket.data);

			LogVoice("[sv:dbg:plugin:updatelpstreamdistance] : stream(%p), dist(%.2f)",
					stData.stream, stData.distance);

			const auto iter = g_streamTable.find(stData.stream);
			if (iter == g_streamTable.end()) break;
			const StreamType stype = iter->second->GetInfo().GetType();
			if (stype == StreamType::GlobalStream || stype == StreamType::None) break;

			static_cast<LocalStream*>(iter->second.get())->SetDistance(stData.distance);
		}
			break;
		case SV::ControlPacketType::updateLPStreamPosition: {
			if (!controlPacket.data || controlPacket.length != sizeof(SV::UpdateLPStreamPositionPacket)) break;
			const auto& stData = *reinterpret_cast<const SV::UpdateLPStreamPositionPacket*>(controlPacket.data);

			LogVoice("[sv:dbg:plugin:updatelpstreamcoords] : stream(%p), pos(%.2f;%.2f;%.2f)",
					stData.stream, stData.position.x, stData.position.y, stData.position.z);

			const auto iter = g_streamTable.find(stData.stream);
			if (iter == g_streamTable.end()) break;
			if (iter->second->GetInfo().GetType() != StreamType::LocalStreamAtPoint) break;

			static_cast<StreamAtPoint*>(iter->second.get())->SetPosition(stData.position);
		}
			break;
		case SV::ControlPacketType::deleteStream: {
			if (!controlPacket.data || controlPacket.length != sizeof(SV::DeleteStreamPacket)) break;
			const auto& stData = *reinterpret_cast<const SV::DeleteStreamPacket*>(controlPacket.data);

			LogVoice("[sv:dbg:plugin:deletestream] : stream(%p)", stData.stream);

			auto it = g_streamTable.find(stData.stream);
			if (it != g_streamTable.end()) {
				SpeakerList::OnStreamDelete(it->second.get());
				g_streamTable.erase(it);
			}
		}
			break;
		case SV::ControlPacketType::setStreamParameter: {
			if (!controlPacket.data || controlPacket.length != sizeof(SV::SetStreamParameterPacket)) break;
			const auto& stData = *reinterpret_cast<const SV::SetStreamParameterPacket*>(controlPacket.data);

			LogVoice("[sv:dbg:plugin:streamsetparameter] : stream(%p), parameter(%hhu), value(%.2f)",
					stData.stream, stData.parameter, stData.value);

			const auto iter = g_streamTable.find(stData.stream);
			if (iter == g_streamTable.end()) break;

			iter->second->SetParameter(stData.parameter, stData.value);
		}
			break;
		case SV::ControlPacketType::slideStreamParameter: {
			if (!controlPacket.data || controlPacket.length != sizeof(SV::SlideStreamParameterPacket)) break;
			const auto& stData = *reinterpret_cast<const SV::SlideStreamParameterPacket*>(controlPacket.data);

			LogVoice("[sv:dbg:plugin:streamslideparameter] : "
					 "stream(%p), parameter(%hhu), startvalue(%.2f), endvalue(%.2f), time(%u)",
					stData.stream, stData.parameter, stData.startvalue, stData.endvalue, stData.time);

			const auto iter = g_streamTable.find(stData.stream);
			if (iter == g_streamTable.end()) break;

			iter->second->SlideParameter(stData.parameter, stData.startvalue, stData.endvalue, stData.time);
		}
			break;
		case SV::ControlPacketType::createEffect: {
			if (!controlPacket.data || controlPacket.length < sizeof(SV::CreateEffectPacket)) break;
			const auto& stData = *reinterpret_cast<const SV::CreateEffectPacket*>(controlPacket.data);

			LogVoice("[sv:dbg:plugin:effectcreate] : "
					 "stream(%p), effect(%p), number(%hhu), priority(%d)",
					stData.stream, stData.effect, stData.number, stData.priority);

			const auto iter = g_streamTable.find(stData.stream);
			if (iter == g_streamTable.end()) break;

			iter->second->EffectCreate(stData.effect, stData.number, stData.priority,
					stData.params, controlPacket.length - sizeof(stData));
		}
			break;
		case SV::ControlPacketType::deleteEffect: {
			if (!controlPacket.data || controlPacket.length != sizeof(SV::DeleteEffectPacket)) break;
			const auto& stData = *reinterpret_cast<const SV::DeleteEffectPacket*>(controlPacket.data);

			LogVoice("[sv:dbg:plugin:effectdelete] : stream(%p), effect(%p)",
					stData.stream, stData.effect);

			const auto iter = g_streamTable.find(stData.stream);
			if (iter == g_streamTable.end()) break;

			iter->second->EffectDelete(stData.effect);
		}
			break;
	}
}

void DisconnectHandler()
{
	SpeakerList::ClearAll();
	g_streamTable.clear();

	g_muteStatus = false;
	g_recordStatus = false;
	g_recordBusy = false;

	Record::Free();
}

} // namespace

void Plugin::SyncVoiceVolume() noexcept
{
	for (const auto& entry : g_streamTable) {
		if (entry.second) {
			entry.second->SyncVoiceVolume();
		}
	}
}