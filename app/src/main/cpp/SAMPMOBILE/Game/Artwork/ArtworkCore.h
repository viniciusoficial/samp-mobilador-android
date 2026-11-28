#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace artwork {

// SA-MP 0.3.DL / open.mp protocol constants
inline constexpr int32_t kMinCustomSkinId = 20001;
inline constexpr int32_t kMaxCustomSkinId = 30000;
inline constexpr int32_t kMinCustomObjectId = -30000;
inline constexpr int32_t kMaxCustomObjectId = -1000;
inline constexpr int32_t kFallbackObjectModelId = 18631; // SA-MP question mark
inline constexpr int32_t kFallbackSkinModelId = 0;       // CJ

// Engine-internal slot allocation for GTA:SA Android v2.10:
// Native CStreaming::ms_aInfoForModel occupies [0..26315] (Models 0..19999, TXD 20000..24999,
// COL 25000..25254, IPL/Nodes/IFP/RRR/SCM 25255..26315).
// Relocating both ms_modelInfoPtrs and ms_aInfoForModel to 32768 entries reserves [26500..32767]
// (6268 slots) which are strictly above all native streaming IDs AND strictly <= INT16_MAX so
// both signed (ldrsh) and unsigned (ldrh) 16-bit CEntity::m_nModelIndex reads agree identically.
inline constexpr int32_t kRelocatedModelTableSize = 32768;
inline constexpr int32_t kInternalSlotBegin = 26500;
inline constexpr int32_t kInternalSlotEnd = 32768; // exclusive
inline constexpr size_t kMaxConcurrentDownloads = 4;
inline constexpr uint8_t kMaxDownloadRetries = 5;
inline constexpr uint8_t kMaxCorruptCacheRecoveries = 1;
inline constexpr uint32_t kMaxArtworkFileBytes = 64u * 1024u * 1024u; // 64 MiB hard cap
inline constexpr size_t kMaxArtworkUrlLength = 1024u;                 // Unified C++/Java URL limit
inline constexpr size_t kMaxTransferHistory = 64u;                    // Cap completed rows in DL overlay

inline constexpr const char* kArtworkUserAgent = "SAMP/0.3";

enum class ModelType : uint8_t {
	None = 0,
	Skin = 1,
	Object = 2,
};

enum class FileType : uint8_t {
	None = 0,
	Dff = 1,
	Txd = 2,
};

struct ResourceKey {
	FileType type = FileType::None;
	uint32_t crc = 0;

	bool operator==(const ResourceKey& o) const noexcept
	{
		return type == o.type && crc == o.crc;
	}
};

struct ResourceKeyHash {
	size_t operator()(const ResourceKey& k) const noexcept
	{
		const uint64_t packed = (static_cast<uint64_t>(k.type) << 32) | static_cast<uint64_t>(k.crc);
		return std::hash<uint64_t>{}(packed);
	}
};

enum class ResourceState : uint8_t {
	Missing = 0,
	UrlRequested = 1, // RPC 181/182 sent, awaiting RPC 183
	Downloading = 2,  // HTTP/direct download + off-thread CRC/format verification in progress
	Cached = 3,       // Present in disk cache (verified on download commit & validated on engine load)
	Failed = 4,       // Permanently failed for this session
};

// Strongly-typed failure contract shared identically between Java ArtworkDownloader and C++ ArtworkCore.
// Never match on human-readable error strings to decide retry or cache invalidation policy.
enum class DownloadFailureKind : uint8_t {
	None = 0,
	TransientNet = 1,    // Socket/DNS/timeout/IO reset (retryable)
	HttpServerError = 2, // HTTP 5xx, 408, 429 (retryable)
	HttpClientError = 3, // HTTP 400/401/403/404/410 etc. (permanent)
	UnsafeUrl = 4,       // Invalid scheme/URL/redirect policy violation (permanent)
	SizeMismatch = 5,    // Truncated/mismatched byte length (retryable)
	CrcMismatch = 6,     // Corrupted transfer / CRC32 mismatch (retryable)
	InvalidFormat = 7,   // DFF/TXD/COL structural validation failed (permanent)
	Cancelled = 8,       // Superseded by newer session generation (ignored)
	BudgetExceeded = 9,  // TXD/VRAM decoded memory cap exceeded (permanent)
	Unsupported = 10,    // Unsupported protocol mode / missing bridge (permanent)
};

constexpr bool isRetryableFailure(DownloadFailureKind kind) noexcept
{
	return kind == DownloadFailureKind::TransientNet
		|| kind == DownloadFailureKind::HttpServerError
		|| kind == DownloadFailureKind::SizeMismatch
		|| kind == DownloadFailureKind::CrcMismatch;
}

struct CustomModelRecord {
	uint32_t poolId = 0;
	ModelType type = ModelType::None;
	int32_t virtualWorld = -1;
	int32_t baseId = 0;
	int32_t newId = 0;
	uint32_t dffCrc = 0;
	uint32_t txdCrc = 0;
	uint32_t dffSize = 0;
	uint32_t txdSize = 0;
	uint8_t timeOn = 0;
	uint8_t timeOff = 0;

	bool isTimed() const noexcept
	{
		return type == ModelType::Object && timeOn != timeOff;
	}
};

struct ResourceDownloadTask {
	FileType type = FileType::None;
	uint32_t crc = 0;
	uint32_t expectedSize = 0;
};

struct DownloadListItem {
	FileType type = FileType::None;
	uint32_t crc = 0;
	ResourceState state = ResourceState::Missing;
	uint64_t downloadedBytes = 0;
	uint64_t totalBytes = 0;
	std::string error;
};

struct DownloadProgressSnapshot {
	bool active = false;
	uint32_t totalFiles = 0;
	uint32_t completedFiles = 0;
	uint32_t failedFiles = 0;
	uint64_t totalBytes = 0;
	uint64_t downloadedBytes = 0;
	std::string currentLabel;
	std::vector<DownloadListItem> items;
};

// ID classification helpers
constexpr bool isCustomSkinId(int32_t id) noexcept
{
	return id >= kMinCustomSkinId && id <= kMaxCustomSkinId;
}

constexpr bool isCustomObjectId(int32_t id) noexcept
{
	return id >= kMinCustomObjectId && id <= kMaxCustomObjectId;
}

constexpr bool isCustomModelId(int32_t id) noexcept
{
	return isCustomSkinId(id) || isCustomObjectId(id);
}

constexpr bool isInternalCustomSlot(int32_t slot) noexcept
{
	return slot >= kInternalSlotBegin && slot < kInternalSlotEnd;
}

// Reinterpret wire uint16 model IDs (e.g. SetObjectMaterial / TextDraw) where negative
// custom object IDs (-30000..-1000) arrive as two's-complement int16_t bits while custom
// skins (20001..30000) stay positive.
constexpr int32_t decodeWireModelId16(uint16_t raw) noexcept
{
	const int16_t signedVal = static_cast<int16_t>(raw);
	if (signedVal < 0) {
		return static_cast<int32_t>(signedVal);
	}
	return static_cast<int32_t>(raw);
}

// Timed object visibility matching PC 0.3.DL CObject::IsTimeVisible().
bool isTimedObjectVisible(int hour, uint8_t timeOn, uint8_t timeOff) noexcept;

// Standard IEEE 802.3 CRC-32 (identical to zlib crc32 and open.mp crc32.hpp).
uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t len) noexcept;
uint32_t crc32Bytes(const uint8_t* data, size_t len) noexcept;

// Cache directory & filename helpers (aligned with PC 0.3.DL naming).
std::string sanitizeHostForCache(const std::string& host);
std::string formatServerCacheDirName(const std::string& host, uint16_t port);
std::string formatCacheFileName(uint32_t crc, FileType type);
bool parseCacheFileHash(const std::string& fileName, uint32_t& outCrc, FileType& outType);

// Rewrites http://127.0.0.1:<port>/... or http://0.0.0.0:<port>/... to the connected
// server IP when the client is connecting over a non-loopback address (common on LAN servers).
std::string rewriteLoopbackUrl(const std::string& url, const std::string& connectedServerHost);

// Rewrites a same-port private/loopback IPv4 URL (e.g. a server advertising its WSL/NAT
// interface 172.23.240.1:7777 while the client connected to 192.168.1.3:7777) to the
// connected server host so open.mp's IP-whitelisted built-in webserver does not 401/timeout.
// Public hostnames/IPs and different ports (external CDNs) are always preserved untouched.
std::string rewritePrivateHostToServer(const std::string& url, const std::string& connectedServerHost, uint16_t connectedServerPort);

// Validates that a download URL uses http:// or https://, fits within kMaxArtworkUrlLength,
// and contains no whitespace or control characters.
bool isSafeHttpUrl(const std::string& url);

// Strips userinfo, query string, and fragment from a URL before writing to logs.
std::string redactUrlForLog(const std::string& url);

// Pure state machine for 0.3.DL artwork manifest, virtual-world scoping, download dedup,
// and bidirectional serverModelId <-> internalSlot mapping. Zero engine/OS dependencies.
class ArtworkManifest {
public:
	void reset();

	// Registers a model from RPC 179 (DlModelRequest).
	// Ignores out-of-range IDs or invalid types. Pass dffAlreadyCached/txdAlreadyCached when the
	// disk cache index already has a size-matching file so cached runtime adds never reopen the
	// download cycle or flash the overlay.
	bool addRecord(const CustomModelRecord& rec, int32_t serverCount,
		bool dffAlreadyCached = false, bool txdAlreadyCached = false);

	// Marks the manifest as settled after an idle window with no further RPC 179 records
	// (fallback when invalid/duplicate records keep records().size() < expectedRecordCount()).
	void markManifestArrivalIdle() noexcept;
	bool isManifestSettled() const noexcept;
	bool isManifestCompleteForPurge() const noexcept;
	size_t expectedRecordCount() const noexcept { return m_expectedRecordCount; }

	// Handles RPC 48 (SetPlayerVirtualWorld). Returns true if VW actually changed and
	// re-triggered the download handshake cycle.
	bool setVirtualWorld(int32_t vw);
	int32_t virtualWorld() const noexcept { return m_virtualWorld; }
	bool virtualWorldKnown() const noexcept { return m_virtualWorldKnown; }

	// Marks a unique resource as already present on disk (called when scanning cache).
	void markResourceCached(FileType type, uint32_t crc);

	// Returns true if this unique resource is already verified and marked Cached.
	bool isResourceCached(FileType type, uint32_t crc) const noexcept;
	ResourceState resourceState(FileType type, uint32_t crc) const noexcept;

	// Returns true if any model record in the manifest references this (type, crc).
	bool isResourceReferenced(FileType type, uint32_t crc) const;

	// Selects the next missing, unrequested resource in the active VW scope (skins always,
	// objects matching active VW or -1), respecting adaptive concurrency, retry backoff,
	// and single in-flight URL request pacing. Marks it UrlRequested if returned.
	bool popNextResourceToRequest(ResourceDownloadTask& outTask, uint32_t nowMs = 0);

	// Effective concurrent download cap (automatically throttles on consecutive network timeouts).
	size_t effectiveConcurrencyLimit() const noexcept;

	// Called when RPC 183 (subOpcode 6) provides a URL for a requested resource.
	// Strictly requires UrlRequested -> Downloading transition and returns expectedSize.
	bool onUrlReceived(FileType type, uint32_t crc, uint32_t& outExpectedSize);

	// Called when RPC 183 (subOpcode 5) begins a legacy direct-chunk transfer.
	// Strictly requires UrlRequested -> Downloading transition and returns expectedSize.
	bool onDirectBegin(FileType type, uint32_t crc, uint32_t& outExpectedSize);

	// Called when a previously Cached file fails disk read / structural parse at engine load time.
	// Resets Cached -> Missing (at most kMaxCorruptCacheRecoveries times per session) and reopens
	// the download cycle if the resource is referenced in the active VW scope.
	bool requeueCorruptCacheResource(FileType type, uint32_t crc);

	// Monotonic dirty counter incremented on every manifest / download progress mutation.
	uint64_t revision() const noexcept { return m_revision; }

	// Called when RPC 183 (subOpcode 0) aborts a request or URL request times out.
	// Retries up to kMaxDownloadRetries with exponential backoff when isRetryableFailure(failureKind) is true.
	void onResourceAborted(FileType type, uint32_t crc,
		DownloadFailureKind failureKind = DownloadFailureKind::HttpClientError,
		const std::string& error = "",
		uint32_t nowMs = 0);

	// Updates live byte progress for an in-flight download.
	void onDownloadProgress(FileType type, uint32_t crc, uint64_t downloaded, uint64_t total);

	// Called when HTTP download + off-thread size + CRC32 + structure verification completes.
	// Retries up to kMaxDownloadRetries with exponential backoff when !success && isRetryableFailure(failureKind).
	void onDownloadFinished(FileType type, uint32_t crc, bool success,
		DownloadFailureKind failureKind = DownloadFailureKind::None,
		const std::string& error = "",
		uint32_t nowMs = 0);

	// True once all active-scope resources are either Cached or Failed, FinishDownload (RPC 184)
	// has not yet been sent for this VW epoch, and at least one VW signal was received.
	bool shouldSendFinishDownload() const noexcept;
	void markFinishDownloadSent() noexcept;

	// Called when RPC 185 (DownloadCompleted) arrives from server.
	void onDownloadCompletedRpc() noexcept;

	// True while the client should gate world pool processing / wait for active downloads + RPC 185.
	bool isDownloadGateActive() const noexcept;

	// True if any active-scope resource is currently Missing, UrlRequested, or Downloading.
	bool hasPendingActiveResources() const;

	// Counts & UI snapshot for active-scope resources.
	DownloadProgressSnapshot buildProgressSnapshot() const;

	// Record lookup by server model ID (`20001..30000` or `-30000..-1000`).
	const CustomModelRecord* findRecord(int32_t serverModelId) const;
	bool isModelReadyOnDisk(int32_t serverModelId) const;
	const std::unordered_map<int32_t, CustomModelRecord>& records() const noexcept { return m_records; }

	// Bidirectional slot mapping: serverModelId <-> internalSlot [26500..32767].
	int32_t allocateInternalSlot(int32_t serverModelId);
	int32_t resolveInternalSlot(int32_t serverModelId) const;
	int32_t resolveServerModelId(int32_t internalSlot) const;
	void releaseInternalSlot(int32_t serverModelId);
	void releaseAllSlots();
	std::vector<int32_t> allocatedInternalSlots() const;

private:
	struct ResourceEntry {
		ResourceState state = ResourceState::Missing;
		uint32_t expectedSize = 0;
		uint64_t downloadedBytes = 0;
		uint32_t nextRetryAtMs = 0;
		uint8_t retryCount = 0;
		uint8_t corruptRecoveryCount = 0;
		bool seenInTransfer = false; // true once requested/downloaded in this session (drives DL list rows)
		std::string lastError;
	};

	bool isRecordInActiveScope(const CustomModelRecord& rec) const noexcept;
	bool isResourceInActiveScope(FileType type, uint32_t crc) const noexcept;
	void registerResourceRef(FileType type, uint32_t crc, uint32_t size);
	void setResourceState(ResourceEntry& entry, ResourceState next) noexcept;
	void trimTransferHistory();

	std::unordered_map<int32_t, CustomModelRecord> m_records;
	std::vector<int32_t> m_recordOrder;
	std::unordered_map<ResourceKey, ResourceEntry, ResourceKeyHash> m_resources;
	std::vector<ResourceKey> m_transferOrder;

	std::unordered_map<int32_t, int32_t> m_serverToSlot;
	std::unordered_map<int32_t, int32_t> m_slotToServer;
	int32_t m_nextSlotCursor = kInternalSlotBegin;

	int32_t m_virtualWorld = 0;
	bool m_virtualWorldKnown = false;
	bool m_finishDownloadSent = false;
	bool m_downloadCompleted = false;
	bool m_manifestArrivalIdle = false;
	bool m_hadTransfersInCycle = false;
	size_t m_expectedRecordCount = 0;
	size_t m_urlRequestedCount = 0;
	size_t m_downloadingCount = 0;
	size_t m_requestCursor = 0;
	uint32_t m_consecutiveNetErrors = 0;
	uint64_t m_revision = 1;
	uint64_t m_stateRevision = 1;
	mutable uint64_t m_pendingActiveCacheRev = 0;
	mutable bool m_cachedHasPendingActive = false;
	std::string m_lastActiveLabel;
};

} // namespace artwork
