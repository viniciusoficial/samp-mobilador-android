#pragma once

#include "ArtworkCore.h"
#include "ArtworkStore.h"
#include "../../Net/Proto/RpcCore.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>

class NetGame;
struct RwTexture;

namespace artwork {

// Host-side callback seam registered by NetGame so Game/Artwork never reaches into Net/ pools.
struct ArtworkHostCallbacks {
	void (*onBeforeUnloadModels)() = nullptr; // Detach/revert all live entities using custom slots
	void (*onResourcesReady)() = nullptr;     // Upgrade any entities that fell back prior to completion
	bool (*isLocalPlayerActive)() = nullptr;  // Used for VW-switch download freeze gating
};

// Coordinates 0.3.DL / open.mp artwork download, off-thread cache verification, VW gating,
// and delegates engine model/texture/collision ownership cleanly to ArtworkStore.
class ArtworkManager {
public:
	static ArtworkManager& instance();

	// Register/clear host callbacks (owned by NetGame lifecycle).
	void setHostCallbacks(const ArtworkHostCallbacks& callbacks) noexcept { m_host = callbacks; }
	void clearHostCallbacks() noexcept { m_host = {}; }

	// Session lifecycle (called on GameMain thread)
	void onSessionStart(const std::string& serverHost, uint16_t serverPort);
	void onGameModeRestart();
	void onSessionEnd();

	// Inbound 0.3.DL RPC handlers (called on GameMain thread)
	void onModelRecord(const proto::DlModelRequest& pkt);
	void onModelReset();
	void onVirtualWorldChanged(int32_t virtualWorld);
	void onDownloadCommand(const proto::DlModelUrl& pkt);
	void onDownloadCompletedRpc();

	// Per-frame tick on GameMain thread (drains off-thread worker events, sends RPC 181/182/184,
	// enforces URL request & RPC 185 timeouts, and manages VW switch freeze/unfreeze).
	void processFrame(NetGame* netGame);

	// Thread-safe callbacks invoked from Java ArtworkDownloader worker threads via JNI.
	void notifyWorkerProgress(int32_t generation, uint8_t fileType, uint32_t crc, uint64_t downloaded, uint64_t total);
	void notifyWorkerFinished(int32_t generation, uint8_t fileType, uint32_t crc, bool success,
		DownloadFailureKind failureKind, const std::string& error);

	// Stateless, thread-safe structural validator + atomic commit called on Java worker thread.
	// Never accesses ArtworkManager instance state or GTA engine state.
	static DownloadFailureKind validateAndCommitDownloadedFile(
		FileType type, const std::string& tempPath, const std::string& finalPath);

	// Download gate & progress snapshot for NetGame and ImGui DownloadOverlay.
	bool isDownloadGateActive() const noexcept;
	DownloadProgressSnapshot progressSnapshot() const;

	// Disk cache lookup: searches server cache first, then local cache.
	bool resolveCachedFilePath(FileType type, uint32_t crc, std::string& outPath) const;
	bool invalidateServerCacheFile(FileType type, uint32_t crc);

	// Manifest & model queries (GameMain thread)
	const CustomModelRecord* findRecord(int32_t serverModelId) const;
	bool isCustomSkinRegistered(int32_t serverModelId) const;
	bool isCustomObjectRegistered(int32_t serverModelId) const;
	bool isTimedObject(int32_t serverModelId, uint8_t& outTimeOn, uint8_t& outTimeOff) const;
	int32_t resolveFallbackObjectModel(int32_t serverModelId) const;
	int32_t resolveFallbackSkinModel(int32_t serverSkinId) const;

	// Engine-facing resolution & loading (delegated to ArtworkStore on GameMain).
	int32_t ensureCustomSkinLoaded(int32_t serverSkinId);
	int32_t ensureCustomObjectLoaded(int32_t serverModelId);
	int32_t resolveInternalSlot(int32_t serverModelId) const;
	int32_t resolveServerModelId(int32_t internalSlot) const;
	RwTexture* findTextureInCustomModel(int32_t serverModelId, const char* texName);

	// Unloads all loaded custom models, collisions, and texture dictionaries from the engine.
	void unloadAllEngineModels();

	ArtworkManifest& manifest() noexcept { return m_manifest; }
	const ArtworkManifest& manifest() const noexcept { return m_manifest; }

private:
	ArtworkManager() = default;

	enum class WorkerEventKind : uint8_t {
		Progress = 0,
		DownloadFinished = 1,
	};

	struct PendingWorkerEvent {
		WorkerEventKind kind = WorkerEventKind::Progress;
		int32_t generation = 0;
		FileType type = FileType::None;
		uint32_t crc = 0;
		uint64_t downloaded = 0;
		uint64_t total = 0;
		bool success = false;
		DownloadFailureKind failureKind = DownloadFailureKind::None;
		std::string error;
	};

	void ensureCacheDirectories();
	void rebuildDiskCacheIndex();
	void indexCacheDirectory(const std::string& dir);
	bool isDiskCached(FileType type, uint32_t crc, uint32_t expectedSize) const noexcept;
	void recordDiskCached(FileType type, uint32_t crc, uint32_t fallbackSize);
	void purgeUnusedServerCache();
	std::string tempPathFor(FileType type, uint32_t crc) const;
	std::string serverCachePathFor(FileType type, uint32_t crc) const;
	ArtworkStore::CacheAccess makeCacheAccess();
	void drainWorkerEvents();
	void updateVwFreezeState();
	void unfreezeIfNeeded();
	void abortActiveDirectTransfer(bool notifyManifest, DownloadFailureKind kind, const char* reason);
	void handleDirectBegin(FileType type, uint32_t crc);
	void handleDirectChunk(const proto::DlModelUrl& pkt);
	void handleDirectFinish();

	struct DirectTransferState {
		bool active = false;
		FileType type = FileType::None;
		uint32_t crc = 0;
		uint32_t expectedSize = 0;
		uint64_t writtenBytes = 0;
		uint32_t runningCrc = 0;
		std::string tempPath;
		std::string finalPath;
		void* fp = nullptr; // FILE*
		std::vector<uint8_t> compressedBuf;
	};

	ArtworkManifest m_manifest;
	ArtworkStore m_store;
	ArtworkHostCallbacks m_host{};
	DirectTransferState m_direct{};

	std::string m_serverHost;
	uint16_t m_serverPort = 0;
	std::string m_serverCacheDir;
	std::string m_localCacheDir;

	std::atomic<int32_t> m_generation{1};
	bool m_sessionActive = false;
	bool m_cachePurgedForSession = false;

	// Single in-flight RPC 181/182 timeout guard
	bool m_waitingForUrl = false;
	FileType m_waitingUrlType = FileType::None;
	uint32_t m_waitingUrlCrc = 0;
	uint32_t m_waitingUrlSentMs = 0;

	// VW-switch download freeze, manifest arrival settle & RPC 185 watchdog state
	bool m_vwFreezeActive = false;
	uint32_t m_vwUnfreezeAtMs = 0;
	uint32_t m_finishSentMs = 0;
	uint32_t m_lastRecordArrivalMs = 0;
	bool m_pendingHostRefresh = false;
	uint64_t m_lastSnapshotRevision = 0;
	uint32_t m_lastSnapshotPublishMs = 0;
	bool m_lastPublishedGateActive = false;

	std::unordered_map<ResourceKey, uint64_t, ResourceKeyHash> m_diskCacheIndex;

	mutable std::mutex m_eventMutex;
	std::deque<PendingWorkerEvent> m_pendingEvents;
	DownloadProgressSnapshot m_cachedSnapshot{};
};

} // namespace artwork
