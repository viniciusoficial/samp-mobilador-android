#include <memory>
#include <atomic>
#include <cmath>
#include <mutex>
#include <string>
#include <strings.h>
#include <unistd.h>
#include <pthread.h>

#include "AudioStream.h"
#include "Aimstuff.h"
#include "Game.h"
#include "PlayerPed.h"
#include "Util.h"
#include "../Client.h"
#include <Bass.h>
#include <spdlog/spdlog.h>


namespace {
std::mutex g_audioStreamMutex;
HSTREAM g_bassStream = 0;
std::string g_audioStreamUrl;
float g_fAudioStreamX = 0.0f;
float g_fAudioStreamY = 0.0f;
float g_fAudioStreamZ = 0.0f;
float g_fAudioStreamRadius = 0.0f;
bool g_audioStreamUsePos = false;
std::atomic<uint32_t> g_audioStreamGeneration{0};
constexpr float kBaseStreamVolume = 0.5f;

// Computes volume for the active stream (caller must hold g_audioStreamMutex).
float computeStreamVolumeLocked(bool allowGameQuery)
{
	if (Game::IsGamePaused()) {
		return 0.0f;
	}
	if (!g_audioStreamUsePos) {
		return kBaseStreamVolume;
	}
	if (!allowGameQuery || !std::isfinite(g_fAudioStreamRadius) || g_fAudioStreamRadius <= 0.001f || !pGame) {
		return 0.0f;
	}

	sa::CVector listenerPos{};
	bool havePos = false;
	if (CPlayerPed* pPed = pGame->FindPlayerPed(); pPed && pPed->m_ped) {
		const RwMatrix mat = pPed->m_ped->GetMatrix().ToRwMatrix();
		listenerPos = sa::CVector{mat.pos.x, mat.pos.y, mat.pos.z};
		havePos = true;
	}
	else if (CAMERA_AIM* pCam = GameGetInternalAim()) {
		listenerPos = sa::CVector{pCam->pos1x, pCam->pos1y, pCam->pos1z};
		havePos = true;
	}
	if (!havePos) {
		return 0.0f;
	}

	const float dx = listenerPos.x - g_fAudioStreamX;
	const float dy = listenerPos.y - g_fAudioStreamY;
	const float dz = listenerPos.z - g_fAudioStreamZ;
	const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
	if (!std::isfinite(dist) || dist >= g_fAudioStreamRadius) {
		return 0.0f;
	}
	return kBaseStreamVolume * (1.0f - (dist / g_fAudioStreamRadius));
}

struct AudioStreamStartArgs {
	uint32_t generation;
	std::string url;
};

bool isAllowedAudioUrl(const char* url)
{
	if (!url || !*url) return false;
	return strncasecmp(url, "http://", 7) == 0 || strncasecmp(url, "https://", 8) == 0;
}

std::atomic<int> g_audioStreamInFlightThreads{0};

void* audioStreamThread(void* rawArg)
{
	struct InFlightGuard {
		~InFlightGuard() { g_audioStreamInFlightThreads.fetch_sub(1, std::memory_order_acq_rel); }
	} inFlightGuard;

	std::unique_ptr<AudioStreamStartArgs> args(static_cast<AudioStreamStartArgs*>(rawArg));
	if (!args) return nullptr;

	const uint32_t generation = args->generation;
	HSTREAM stream = BASS_StreamCreateURL(args->url.data(), 0, 0x940000, 0);
	if (!stream) {
		spdlog::warn("AudioStream: BASS_StreamCreateURL failed (err={})", BASS_ErrorGetCode());
		return nullptr;
	}

	{
		std::lock_guard<std::mutex> lock(g_audioStreamMutex);
		if (generation != g_audioStreamGeneration.load(std::memory_order_acquire)) {
			// Superseded by a newer Play/Stop while connecting.
			BASS_StreamFree(stream);
			return nullptr;
		}
		if (g_bassStream) {
			BASS_ChannelStop(g_bassStream);
			BASS_StreamFree(g_bassStream);
		}
		g_bassStream = stream;
		// Start muted for 3D positioned streams until GameMain Process() sets exact distance volume
		const float initialVol = (Game::IsGamePaused() || g_audioStreamUsePos) ? 0.0f : kBaseStreamVolume;
		BASS_ChannelSetAttribute(stream, BASS_ATTRIB_VOL, initialVol);
		BASS_ChannelPlay(stream, 0);
	}

	return nullptr;
}
}

AudioStream::AudioStream()
{
	m_bInited = false;
}

bool AudioStream::Initialize()
{
	spdlog::info("AudioStream::Initialize()");

	if (!IsBassLoaded()) {
		spdlog::error("AudioStream::Initialize: libBASS.so is not loaded");
		return false;
	}

	{
		std::lock_guard<std::mutex> lock(g_audioStreamMutex);
		if (g_bassStream) {
			BASS_ChannelStop(g_bassStream);
			BASS_StreamFree(g_bassStream);
			g_bassStream = 0;
		}
	}

	// Do NOT call BASS_Free() if Voice Playback already initialized BASS —
	// doing so would tear down Voice's 3D audio device and streams.
	if (!BASS_IsStarted()) {
		if (!BASS_Init(-1, 44100, BASS_DEVICE_3D)) {
			int err = BASS_ErrorGetCode();
			if (err != BASS_ERROR_ALREADY) {
				spdlog::error("AudioStream::Initialize: BASS_Init failed (err={})", err);
				return false;
			}
		}
	}

	BASS_SetConfigPtr(16, "SA-MP/0.3");
	BASS_SetConfig(21, 1);            // BASS_CONFIG_NET_PLAYLIST
	BASS_SetConfig(11, 10000);        // BASS_CONFIG_NET_TIMEOUT

	m_bInited = true;
	return true;
}

void AudioStream::Process()
{
	if (!m_bInited) return;
	std::lock_guard<std::mutex> lock(g_audioStreamMutex);
	if (g_bassStream) {
		// Control only our own AudioStream channel volume instead of global
		// BASS_CONFIG_GVOL_STREAM so Voice streams are unaffected.
		BASS_ChannelSetAttribute(g_bassStream, BASS_ATTRIB_VOL, computeStreamVolumeLocked(true));
	}
}

bool AudioStream::Play(const char* szUrl, float fX, float fY, float fZ, float fRadius, bool bUsePos)
{
	if (!m_bInited || !isAllowedAudioUrl(szUrl)) {
		spdlog::warn("AudioStream::Play rejected invalid URL");
		return false;
	}
	spdlog::info("Play: {}", szUrl);

	Stop(false);

	uint32_t generation = 0;
	{
		std::lock_guard<std::mutex> lock(g_audioStreamMutex);
		g_audioStreamUrl = szUrl;
		g_fAudioStreamX = fX;
		g_fAudioStreamY = fY;
		g_fAudioStreamZ = fZ;
		g_fAudioStreamRadius = fRadius;
		g_audioStreamUsePos = bUsePos;
		generation = ++g_audioStreamGeneration;
	}

	static constexpr int kMaxInFlightAudioThreads = 2;
	if (g_audioStreamInFlightThreads.fetch_add(1, std::memory_order_acq_rel) >= kMaxInFlightAudioThreads) {
		g_audioStreamInFlightThreads.fetch_sub(1, std::memory_order_acq_rel);
		spdlog::warn("AudioStream::Play: too many in-flight connect threads, dropping request");
		return false;
	}

	auto* args = new AudioStreamStartArgs{generation, szUrl};
	pthread_t thread;
	if (pthread_create(&thread, nullptr, audioStreamThread, args) == 0) {
		pthread_detach(thread);
		return true;
	}
	g_audioStreamInFlightThreads.fetch_sub(1, std::memory_order_acq_rel);
	delete args;
	return false;
}

bool AudioStream::Stop(bool /*bWaitThread*/)
{
	if (!m_bInited) {
		return false;
	}

	// Generation increment supersedes any in-flight connect thread without
	// blocking GameMain on up to 10s BASS_StreamCreateURL network I/O.
	g_audioStreamGeneration.fetch_add(1, std::memory_order_acq_rel);

	HSTREAM stream = 0;
	{
		std::lock_guard<std::mutex> lock(g_audioStreamMutex);
		spdlog::info("Stop: {}", g_audioStreamUrl);
		stream = g_bassStream;
		g_bassStream = 0;
	}
	if (stream) {
		BASS_ChannelStop(stream);
		BASS_StreamFree(stream);
	}
	return true;
}
