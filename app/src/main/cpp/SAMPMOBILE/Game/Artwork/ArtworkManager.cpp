#include "ArtworkManager.h"
#include "ArtworkTelemetry.h"
#include "DffValidator.h"
#include "Lzma1Decoder.h"
#include "TxdParser.h"

#include "../../Client.h"
#include "../../Game/Game.h"
#include "../../Game/PlayerPed.h"
#include "../../Game/Util.h"
#include "../../Java/Java.h"
#include "../../Net/NetGame.h"
#include "../../Net/Proto/RpcOutbound.h"

#include <RakNet/RakClientInterface.h>
#include <RakNet/SAMP/SAMPRPC.h>
#include <spdlog/spdlog.h>

#include <cstdio>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace artwork {

std::atomic<size_t> g_telemetryTxdBytes{0};
std::atomic<size_t> g_telemetryLoadedSlots{0};
std::atomic<size_t> g_telemetryLoadedTxds{0};

namespace {

constexpr uint32_t kUrlResponseTimeoutMs = 10000u;
constexpr uint32_t kRpc185WatchdogMs = 12000u;
constexpr uint32_t kVwUnfreezeDelayMs = 800u;
constexpr uint32_t kManifestSettleIdleMs = 600u;
constexpr uint32_t kSnapshotRebuildIntervalMs = 100u;

inline uint32_t nonZeroTickMs() noexcept
{
	const uint32_t t = GetTickCount();
	return (t != 0u) ? t : 1u;
}

void mkdirRecursive(const std::string& path)
{
	if (path.empty()) return;
	std::string cur;
	cur.reserve(path.size());
	for (size_t i = 0; i < path.size(); ++i) {
		cur.push_back(path[i]);
		if (path[i] == '/' && cur.size() > 1) {
			mkdir(cur.c_str(), 0755);
		}
	}
	mkdir(path.c_str(), 0755);
}

bool readEntireFile(const std::string& path, uint32_t maxBytes, std::vector<uint8_t>& out)
{
	struct stat st{};
	if (stat(path.c_str(), &st) != 0 || st.st_size <= 0) {
		return false;
	}
	if (static_cast<uint64_t>(st.st_size) > maxBytes) {
		return false;
	}
	FILE* fp = fopen(path.c_str(), "rb");
	if (!fp) {
		return false;
	}
	out.resize(static_cast<size_t>(st.st_size));
	const size_t n = fread(out.data(), 1, out.size(), fp);
	const bool ok = (n == out.size()) && !ferror(fp);
	fclose(fp);
	if (!ok) {
		out.clear();
	}
	return ok;
}

} // namespace

ArtworkManager& ArtworkManager::instance()
{
	static ArtworkManager s_instance;
	return s_instance;
}

DownloadFailureKind ArtworkManager::validateAndCommitDownloadedFile(
	FileType type, const std::string& tempPath, const std::string& finalPath)
{
	if (type == FileType::None || tempPath.empty() || finalPath.empty()) {
		return DownloadFailureKind::UnsafeUrl;
	}

	std::vector<uint8_t> bytes;
	if (!readEntireFile(tempPath, kMaxArtworkFileBytes, bytes)) {
		unlink(tempPath.c_str());
		return DownloadFailureKind::TransientNet;
	}

	if (type == FileType::Txd) {
		TxdParseFailure txdFail = TxdParseFailure::None;
		std::string txdErr;
		if (!validateTxdBytes(bytes.data(), bytes.size(), &txdErr, kMaxDecodedTxdBytes, &txdFail)) {
			spdlog::warn("Artwork: rejected downloaded TXD '{}' (failure={}): {}",
				finalPath, static_cast<int>(txdFail), txdErr);
			unlink(tempPath.c_str());
			return (txdFail == TxdParseFailure::BudgetExceeded)
				? DownloadFailureKind::BudgetExceeded
				: DownloadFailureKind::InvalidFormat;
		}
	} else if (type == FileType::Dff) {
		DffValidationResult vr = validateAndPrepareDff(bytes.data(), bytes.size(), true);
		if (!vr.ok) {
			spdlog::warn("Artwork: rejected downloaded DFF '{}' ({}B): {}",
				finalPath, bytes.size(), vr.error);
			unlink(tempPath.c_str());
			return DownloadFailureKind::InvalidFormat;
		}
		if (vr.hasEmbeddedColChunk && vr.embeddedCol.empty()) {
			// Keep the structurally valid DFF in cache; ArtworkStore strips the malformed embedded
			// COL chunk before RpClumpStreamRead and safely falls back to the base model's collision.
			spdlog::warn("Artwork: downloaded DFF '{}' has invalid embedded COL ({}); will strip COL and use base collision at load time",
				finalPath, vr.colError);
		}
	}

	if (rename(tempPath.c_str(), finalPath.c_str()) != 0) {
		unlink(tempPath.c_str());
		return DownloadFailureKind::TransientNet;
	}
	return DownloadFailureKind::None;
}

void ArtworkManager::ensureCacheDirectories()
{
	std::string root = Client::gameDir();
	if (!root.empty() && root.back() != '/') {
		root.push_back('/');
	}
	const std::string base = root + "SAMP/cache";
	m_localCacheDir = base + "/local";
	m_serverCacheDir = base + "/" + formatServerCacheDirName(m_serverHost, m_serverPort);

	mkdirRecursive(m_localCacheDir);
	mkdirRecursive(m_serverCacheDir);
}

std::string ArtworkManager::tempPathFor(FileType type, uint32_t crc) const
{
	char buf[48];
	const char* ext = (type == FileType::Dff) ? "dff" : "txd";
	std::snprintf(buf, sizeof(buf), "0x%X.%s.tmp", crc, ext);
	return m_serverCacheDir + "/" + buf;
}

std::string ArtworkManager::serverCachePathFor(FileType type, uint32_t crc) const
{
	return m_serverCacheDir + "/" + formatCacheFileName(crc, type);
}

bool ArtworkManager::resolveCachedFilePath(FileType type, uint32_t crc, std::string& outPath) const
{
	if (type == FileType::None || crc == 0) {
		return false;
	}
	const std::string name = formatCacheFileName(crc, type);
	struct stat st{};
	if (!m_serverCacheDir.empty()) {
		std::string p = m_serverCacheDir + "/" + name;
		if (stat(p.c_str(), &st) == 0 && st.st_size > 0) {
			outPath = std::move(p);
			return true;
		}
	}
	if (!m_localCacheDir.empty()) {
		std::string p = m_localCacheDir + "/" + name;
		if (stat(p.c_str(), &st) == 0 && st.st_size > 0) {
			outPath = std::move(p);
			return true;
		}
	}
	return false;
}

bool ArtworkManager::invalidateServerCacheFile(FileType type, uint32_t crc)
{
	if (m_serverCacheDir.empty() || crc == 0) {
		return false;
	}
	const std::string path = serverCachePathFor(type, crc);
	if (!path.empty() && unlink(path.c_str()) == 0) {
		spdlog::warn("Artwork: removed corrupt cached file {}", path);
	}
	m_diskCacheIndex.erase(ResourceKey{type, crc});
	return m_manifest.requeueCorruptCacheResource(type, crc);
}

ArtworkStore::CacheAccess ArtworkManager::makeCacheAccess()
{
	ArtworkStore::CacheAccess access{};
	access.resolvePath = [this](FileType type, uint32_t crc, std::string& outPath) {
		return resolveCachedFilePath(type, crc, outPath);
	};
	access.invalidateCorrupt = [this](FileType type, uint32_t crc) {
		return invalidateServerCacheFile(type, crc);
	};
	return access;
}

void ArtworkManager::indexCacheDirectory(const std::string& dir)
{
	if (dir.empty()) {
		return;
	}
	DIR* d = opendir(dir.c_str());
	if (!d) {
		return;
	}
	struct dirent* e = nullptr;
	while ((e = readdir(d)) != nullptr) {
		const std::string name = e->d_name;
		if (name == "." || name == "..") {
			continue;
		}
		uint32_t crc = 0;
		FileType type = FileType::None;
		if (!parseCacheFileHash(name, crc, type) || crc == 0 || type == FileType::None) {
			continue;
		}
		const std::string full = dir + "/" + name;
		struct stat st{};
		if (stat(full.c_str(), &st) == 0 && S_ISREG(st.st_mode)
			&& st.st_size > 0 && static_cast<uint64_t>(st.st_size) <= kMaxArtworkFileBytes) {
			m_diskCacheIndex[ResourceKey{type, crc}] = static_cast<uint64_t>(st.st_size);
		}
	}
	closedir(d);
}

void ArtworkManager::rebuildDiskCacheIndex()
{
	m_diskCacheIndex.clear();
	// Index local cache first so server-specific cache overrides on CRC collision.
	indexCacheDirectory(m_localCacheDir);
	indexCacheDirectory(m_serverCacheDir);
}

bool ArtworkManager::isDiskCached(FileType type, uint32_t crc, uint32_t expectedSize) const noexcept
{
	if (type == FileType::None || crc == 0) {
		return false;
	}
	auto it = m_diskCacheIndex.find(ResourceKey{type, crc});
	if (it == m_diskCacheIndex.end() || it->second == 0) {
		return false;
	}
	return expectedSize == 0 || it->second == static_cast<uint64_t>(expectedSize);
}

void ArtworkManager::recordDiskCached(FileType type, uint32_t crc, uint32_t fallbackSize)
{
	if (type == FileType::None || crc == 0) {
		return;
	}
	const std::string path = serverCachePathFor(type, crc);
	struct stat st{};
	if (!path.empty() && stat(path.c_str(), &st) == 0 && st.st_size > 0) {
		m_diskCacheIndex[ResourceKey{type, crc}] = static_cast<uint64_t>(st.st_size);
	} else if (fallbackSize > 0) {
		m_diskCacheIndex[ResourceKey{type, crc}] = static_cast<uint64_t>(fallbackSize);
	}
}

void ArtworkManager::purgeUnusedServerCache()
{
	if (m_serverCacheDir.empty()) {
		return;
	}
	DIR* d = opendir(m_serverCacheDir.c_str());
	if (!d) {
		return;
	}
	struct dirent* e = nullptr;
	int purged = 0;
	while ((e = readdir(d)) != nullptr) {
		const std::string name = e->d_name;
		if (name == "." || name == "..") {
			continue;
		}
		const std::string full = m_serverCacheDir + "/" + name;
		if (name.size() > 4 && name.compare(name.size() - 4, 4, ".tmp") == 0) {
			unlink(full.c_str());
			++purged;
			continue;
		}
		uint32_t crc = 0;
		FileType type = FileType::None;
		if (parseCacheFileHash(name, crc, type) && !m_manifest.isResourceReferenced(type, crc)) {
			unlink(full.c_str());
			++purged;
		}
	}
	closedir(d);
	if (purged > 0) {
		spdlog::info("Artwork: purged {} stale/temp files from {}", purged, m_serverCacheDir);
	}
}

void ArtworkManager::unfreezeIfNeeded()
{
	if (m_vwFreezeActive && pGame) {
		if (CPlayerPed* ped = pGame->FindPlayerPed()) {
			ped->SetGravityProcessing(true);
			ped->SetCollisionChecking(true);
			ped->TogglePlayerControllable(true);
		}
	}
	m_vwFreezeActive = false;
	m_vwUnfreezeAtMs = 0;
}

void ArtworkManager::abortActiveDirectTransfer(bool notifyManifest, DownloadFailureKind kind, const char* reason)
{
	if (!m_direct.active) {
		return;
	}
	if (m_direct.fp) {
		fclose(static_cast<FILE*>(m_direct.fp));
		m_direct.fp = nullptr;
	}
	if (!m_direct.tempPath.empty()) {
		unlink(m_direct.tempPath.c_str());
	}
	const FileType ft = m_direct.type;
	const uint32_t crc = m_direct.crc;
	m_direct = {};
	if (notifyManifest) {
		m_manifest.onDownloadFinished(ft, crc, false, kind, reason ? reason : "direct transfer failed");
	}
}

void ArtworkManager::handleDirectBegin(FileType type, uint32_t crc)
{
	abortActiveDirectTransfer(true, DownloadFailureKind::TransientNet, "superseded direct transfer");

	if (m_waitingForUrl && m_waitingUrlType == type && m_waitingUrlCrc == crc) {
		m_waitingForUrl = false;
	}

	uint32_t expectedSize = 0;
	if (!m_manifest.onDirectBegin(type, crc, expectedSize)) {
		spdlog::warn("Artwork: ignored unsolicited direct-chunk begin for crc=0x{:X}", crc);
		return;
	}

	const std::string tmpPath = tempPathFor(type, crc);
	const std::string dstPath = serverCachePathFor(type, crc);
	FILE* fp = fopen(tmpPath.c_str(), "wb");
	if (!fp) {
		spdlog::error("Artwork: failed to open temp file for direct transfer crc=0x{:X}", crc);
		m_manifest.onDownloadFinished(type, crc, false, DownloadFailureKind::TransientNet, "temp file open failed");
		return;
	}

	m_direct.active = true;
	m_direct.type = type;
	m_direct.crc = crc;
	m_direct.expectedSize = expectedSize;
	m_direct.writtenBytes = 0;
	m_direct.runningCrc = 0;
	m_direct.tempPath = tmpPath;
	m_direct.finalPath = dstPath;
	m_direct.fp = fp;
	m_direct.compressedBuf.clear();
	m_direct.compressedBuf.reserve(4096u);
	spdlog::info("Artwork: direct-chunk download started {} 0x{:X} ({}B)",
		type == FileType::Dff ? "DFF" : "TXD", crc, expectedSize);
}

void ArtworkManager::handleDirectChunk(const proto::DlModelUrl& pkt)
{
	if (!m_direct.active || !m_direct.fp) {
		return;
	}
	if (pkt.chunkUnpackedLen == 0u || pkt.chunkUnpackedLen > 4096u) {
		abortActiveDirectTransfer(true, DownloadFailureKind::InvalidFormat, "direct chunk unpacked size invalid");
		return;
	}
	if (pkt.chunkData.size() > 4096u || m_direct.compressedBuf.size() + pkt.chunkData.size() > 4096u) {
		abortActiveDirectTransfer(true, DownloadFailureKind::InvalidFormat, "direct chunk compressed buffer overflow");
		return;
	}
	m_direct.compressedBuf.insert(m_direct.compressedBuf.end(), pkt.chunkData.begin(), pkt.chunkData.end());

	if (pkt.chunkStage == 1u) {
		return; // More compressed pieces coming for this 4KB block.
	}
	if (pkt.chunkStage != 2u) {
		abortActiveDirectTransfer(true, DownloadFailureKind::InvalidFormat, "direct chunk invalid stage");
		return;
	}

	uint8_t unpacked[4096];
	size_t decodedLen = 0;
	if (!decodeLzma1Chunk03dl(m_direct.compressedBuf.data(), m_direct.compressedBuf.size(),
			unpacked, pkt.chunkUnpackedLen, decodedLen)
		|| decodedLen != pkt.chunkUnpackedLen) {
		abortActiveDirectTransfer(true, DownloadFailureKind::InvalidFormat, "LZMA1 chunk decode failed");
		return;
	}
	m_direct.compressedBuf.clear();

	const uint64_t sizeCap = (m_direct.expectedSize > 0u) ? m_direct.expectedSize : kMaxArtworkFileBytes;
	if (m_direct.writtenBytes + decodedLen > sizeCap) {
		abortActiveDirectTransfer(true, DownloadFailureKind::SizeMismatch, "direct transfer exceeded expected size");
		return;
	}
	if (fwrite(unpacked, 1, decodedLen, static_cast<FILE*>(m_direct.fp)) != decodedLen) {
		abortActiveDirectTransfer(true, DownloadFailureKind::TransientNet, "direct transfer write failed");
		return;
	}
	m_direct.writtenBytes += decodedLen;
	m_direct.runningCrc = crc32Update(m_direct.runningCrc, unpacked, decodedLen);
	m_manifest.onDownloadProgress(m_direct.type, m_direct.crc, m_direct.writtenBytes, m_direct.expectedSize);
}

void ArtworkManager::handleDirectFinish()
{
	if (!m_direct.active) {
		return;
	}
	if (m_direct.fp) {
		fclose(static_cast<FILE*>(m_direct.fp));
		m_direct.fp = nullptr;
	}
	const FileType ft = m_direct.type;
	const uint32_t crc = m_direct.crc;
	const uint32_t expectedSize = m_direct.expectedSize;
	const uint64_t written = m_direct.writtenBytes;
	const uint32_t actualCrc = m_direct.runningCrc;
	const std::string tmpPath = m_direct.tempPath;
	const std::string dstPath = m_direct.finalPath;
	m_direct = {};

	if (written == 0u || (expectedSize > 0u && written != expectedSize)) {
		unlink(tmpPath.c_str());
		m_manifest.onDownloadFinished(ft, crc, false, DownloadFailureKind::SizeMismatch, "direct transfer size mismatch");
		return;
	}
	if (actualCrc != crc) {
		unlink(tmpPath.c_str());
		m_manifest.onDownloadFinished(ft, crc, false, DownloadFailureKind::CrcMismatch, "direct transfer CRC32 mismatch");
		return;
	}

	const DownloadFailureKind commitFail = validateAndCommitDownloadedFile(ft, tmpPath, dstPath);
	if (commitFail != DownloadFailureKind::None) {
		m_manifest.onDownloadFinished(ft, crc, false, commitFail, "direct transfer validation failed");
		return;
	}
	spdlog::info("Artwork: cached direct {} 0x{:X} ({}B)", ft == FileType::Dff ? "DFF" : "TXD", crc, written);
	recordDiskCached(ft, crc, static_cast<uint32_t>(written));
	m_pendingHostRefresh = true;
	m_manifest.onDownloadFinished(ft, crc, true, DownloadFailureKind::None, "");
}

void ArtworkManager::onSessionStart(const std::string& serverHost, uint16_t serverPort)
{
	onSessionEnd();

	m_serverHost = serverHost;
	m_serverPort = serverPort;
	m_sessionActive = true;
	m_cachePurgedForSession = false;
	m_waitingForUrl = false;
	m_finishSentMs = 0;
	m_lastRecordArrivalMs = 0;
	m_pendingHostRefresh = false;
	m_lastSnapshotRevision = 0;
	m_lastSnapshotPublishMs = 0;
	m_lastPublishedGateActive = false;

	ensureCacheDirectories();
	rebuildDiskCacheIndex();
	spdlog::info("Artwork: session started for {}:{} (cache={}, indexed={})",
		m_serverHost, m_serverPort, m_serverCacheDir, m_diskCacheIndex.size());
}

void ArtworkManager::onGameModeRestart()
{
	const int32_t nextGen = m_generation.fetch_add(1, std::memory_order_acq_rel) + 1;
	if (g_java) {
		g_java->cancelArtworkDownloads(nextGen);
	}
	{
		std::lock_guard<std::mutex> lock(m_eventMutex);
		m_pendingEvents.clear();
		m_cachedSnapshot = {};
	}

	abortActiveDirectTransfer(false, DownloadFailureKind::Cancelled, "GMX");
	unfreezeIfNeeded();
	unloadAllEngineModels();
	m_manifest.reset();
	rebuildDiskCacheIndex();
	m_cachePurgedForSession = false;
	m_waitingForUrl = false;
	m_finishSentMs = 0;
	m_lastRecordArrivalMs = 0;
	m_pendingHostRefresh = false;
	m_lastSnapshotRevision = 0;
	m_lastSnapshotPublishMs = 0;
	m_lastPublishedGateActive = false;
	spdlog::info("Artwork: cleaned up for GMX / restart");
}

void ArtworkManager::onSessionEnd()
{
	const int32_t nextGen = m_generation.fetch_add(1, std::memory_order_acq_rel) + 1;
	if (g_java) {
		g_java->cancelArtworkDownloads(nextGen);
	}
	{
		std::lock_guard<std::mutex> lock(m_eventMutex);
		m_pendingEvents.clear();
		m_cachedSnapshot = {};
	}

	abortActiveDirectTransfer(false, DownloadFailureKind::Cancelled, "session end");
	unfreezeIfNeeded();
	unloadAllEngineModels();
	m_manifest.reset();
	m_diskCacheIndex.clear();
	m_sessionActive = false;
	m_cachePurgedForSession = false;
	m_waitingForUrl = false;
	m_finishSentMs = 0;
	m_lastRecordArrivalMs = 0;
	m_pendingHostRefresh = false;
	m_lastSnapshotRevision = 0;
	m_lastSnapshotPublishMs = 0;
	m_lastPublishedGateActive = false;
}

void ArtworkManager::onModelRecord(const proto::DlModelRequest& pkt)
{
	if (!m_sessionActive) {
		return;
	}
	CustomModelRecord rec{};
	rec.poolId = pkt.poolId;
	rec.type = (pkt.type == 1) ? ModelType::Skin : ((pkt.type == 2) ? ModelType::Object : ModelType::None);
	rec.virtualWorld = pkt.virtualWorld;
	rec.baseId = pkt.baseId;
	rec.newId = pkt.newId;
	rec.dffCrc = pkt.dffChecksum;
	rec.txdCrc = pkt.txdChecksum;
	rec.dffSize = pkt.dffSize;
	rec.txdSize = pkt.txdSize;
	rec.timeOn = pkt.timeOn;
	rec.timeOff = pkt.timeOff;

	const bool dffCached = isDiskCached(FileType::Dff, rec.dffCrc, rec.dffSize);
	const bool txdCached = isDiskCached(FileType::Txd, rec.txdCrc, rec.txdSize);

	if (!m_manifest.addRecord(rec, pkt.count, dffCached, txdCached)) {
		spdlog::warn("Artwork: ignored invalid model record (type={} base={} new={} dffSize={} txdSize={})",
			pkt.type, pkt.baseId, pkt.newId, pkt.dffSize, pkt.txdSize);
		return;
	}

	m_lastRecordArrivalMs = nonZeroTickMs();

	spdlog::debug("Artwork: registered {} newId={} baseId={} vw={} dff=0x{:X}({}B) txd=0x{:X}({}B) time={}-{}",
		rec.type == ModelType::Skin ? "skin" : "object",
		rec.newId, rec.baseId, rec.virtualWorld,
		rec.dffCrc, rec.dffSize, rec.txdCrc, rec.txdSize,
		rec.timeOn, rec.timeOff);
}

void ArtworkManager::onModelReset()
{
	spdlog::info("Artwork: received DlModelReset (RPC 180)");
	onGameModeRestart();
}

void ArtworkManager::onVirtualWorldChanged(int32_t virtualWorld)
{
	if (!m_sessionActive) {
		return;
	}
	if (m_manifest.setVirtualWorld(virtualWorld)) {
		m_finishSentMs = 0;
		m_store.evictIdleIfAboveWatermark(m_manifest);
		spdlog::info("Artwork: virtual world -> {} (records={}/{})",
			virtualWorld, m_manifest.records().size(), m_manifest.expectedRecordCount());
	}
}

void ArtworkManager::onDownloadCommand(const proto::DlModelUrl& pkt)
{
	if (!m_sessionActive) {
		return;
	}

	if (pkt.kind == proto::DlModelUrl::kKindAbort) {
		const uint32_t now = nonZeroTickMs();
		spdlog::warn("Artwork: server aborted resource download");
		abortActiveDirectTransfer(true, DownloadFailureKind::HttpClientError, "server aborted");
		if (m_waitingForUrl) {
			m_manifest.onResourceAborted(m_waitingUrlType, m_waitingUrlCrc, DownloadFailureKind::HttpClientError, "server aborted", now);
			m_waitingForUrl = false;
		} else {
			m_manifest.onResourceAborted(FileType::None, 0, DownloadFailureKind::HttpClientError, "server aborted", now);
		}
		return;
	}

	if (pkt.kind == proto::DlModelUrl::kKindDirectBegin) {
		const FileType ft = (pkt.fileType == 1) ? FileType::Dff : ((pkt.fileType == 2) ? FileType::Txd : FileType::None);
		if (ft != FileType::None && pkt.fileChecksum != 0) {
			handleDirectBegin(ft, pkt.fileChecksum);
		}
		return;
	}

	if (pkt.kind == proto::DlModelUrl::kKindDirectChunk) {
		handleDirectChunk(pkt);
		return;
	}

	if (pkt.kind == proto::DlModelUrl::kKindDirectFinish) {
		handleDirectFinish();
		return;
	}

	if (pkt.kind != proto::DlModelUrl::kKindUrlRedirect) {
		spdlog::warn("Artwork: unknown download subOpcode {} for crc=0x{:X}", pkt.kind, pkt.fileChecksum);
		return;
	}

	const FileType ft = (pkt.fileType == 1) ? FileType::Dff : ((pkt.fileType == 2) ? FileType::Txd : FileType::None);
	if (ft == FileType::None || pkt.fileChecksum == 0) {
		m_waitingForUrl = false;
		return;
	}

	if (m_waitingForUrl && m_waitingUrlType == ft && m_waitingUrlCrc == pkt.fileChecksum) {
		m_waitingForUrl = false;
	}

	uint32_t expectedSize = 0;
	if (!m_manifest.onUrlReceived(ft, pkt.fileChecksum, expectedSize)) {
		return;
	}

	// Only rewrite loopback (127.0.0.1 / 0.0.0.0 / localhost) upfront so genuine LAN CDNs on a
	// separate private IP are never hijacked; pass same-port private-IP rewrite as fallbackUrl
	// so WSL/NAT open.mp servers still transparently recover on connect error or HTTP 401.
	const std::string primaryUrl = rewriteLoopbackUrl(pkt.downloadLink, m_serverHost);
	const std::string candidateFallback = rewritePrivateHostToServer(primaryUrl, m_serverHost, m_serverPort);
	const std::string fallbackUrl = (candidateFallback != primaryUrl && isSafeHttpUrl(candidateFallback))
		? candidateFallback : std::string{};

	if (!isSafeHttpUrl(primaryUrl)) {
		spdlog::warn("Artwork: rejected unsafe URL '{}' for crc=0x{:X}", redactUrlForLog(primaryUrl), pkt.fileChecksum);
		m_manifest.onDownloadFinished(ft, pkt.fileChecksum, false, DownloadFailureKind::UnsafeUrl, "unsafe URL");
		return;
	}

	if (!g_java) {
		spdlog::error("Artwork: Java bridge unavailable for HTTP download");
		m_manifest.onDownloadFinished(ft, pkt.fileChecksum, false, DownloadFailureKind::Unsupported, "no bridge");
		return;
	}

	const std::string tmpPath = tempPathFor(ft, pkt.fileChecksum);
	const std::string dstPath = serverCachePathFor(ft, pkt.fileChecksum);
	const int32_t gen = m_generation.load(std::memory_order_acquire);
	spdlog::info("Artwork: downloading {} 0x{:X} ({}B) from {}",
		ft == FileType::Dff ? "DFF" : "TXD", pkt.fileChecksum, expectedSize, redactUrlForLog(primaryUrl));
	g_java->startArtworkDownload(gen, static_cast<int32_t>(ft), pkt.fileChecksum, primaryUrl, fallbackUrl, tmpPath, dstPath, expectedSize);
}

void ArtworkManager::onDownloadCompletedRpc()
{
	spdlog::info("Artwork: received DownloadCompleted (RPC 185) for vw={}", m_manifest.virtualWorld());
	m_finishSentMs = 0;
	m_manifest.onDownloadCompletedRpc();
	if (m_vwFreezeActive) {
		m_vwUnfreezeAtMs = GetTickCount() + kVwUnfreezeDelayMs;
	}
	// Only purge unused cache files once the full promised manifest has arrived so a
	// partial or timed-out join can never delete valid cached files from disk.
	if (!m_cachePurgedForSession && m_manifest.isManifestCompleteForPurge()) {
		m_cachePurgedForSession = true;
		purgeUnusedServerCache();
		rebuildDiskCacheIndex();
	}
	m_pendingHostRefresh = true;
}

void ArtworkManager::notifyWorkerProgress(int32_t generation, uint8_t fileType, uint32_t crc, uint64_t downloaded, uint64_t total)
{
	if (generation != m_generation.load(std::memory_order_acquire)) {
		return;
	}
	PendingWorkerEvent ev{};
	ev.kind = WorkerEventKind::Progress;
	ev.generation = generation;
	ev.type = (fileType == 1) ? FileType::Dff : FileType::Txd;
	ev.crc = crc;
	ev.downloaded = downloaded;
	ev.total = total;

	std::lock_guard<std::mutex> lock(m_eventMutex);
	if (m_pendingEvents.size() < 256) {
		m_pendingEvents.push_back(std::move(ev));
	}
}

void ArtworkManager::notifyWorkerFinished(int32_t generation, uint8_t fileType, uint32_t crc, bool success,
	DownloadFailureKind failureKind, const std::string& error)
{
	if (generation != m_generation.load(std::memory_order_acquire)) {
		return;
	}
	PendingWorkerEvent ev{};
	ev.kind = WorkerEventKind::DownloadFinished;
	ev.generation = generation;
	ev.type = (fileType == 1) ? FileType::Dff : FileType::Txd;
	ev.crc = crc;
	ev.success = success;
	ev.failureKind = failureKind;
	ev.error = error;

	std::lock_guard<std::mutex> lock(m_eventMutex);
	m_pendingEvents.push_back(std::move(ev));
}

void ArtworkManager::drainWorkerEvents()
{
	std::deque<PendingWorkerEvent> queue;
	{
		std::lock_guard<std::mutex> lock(m_eventMutex);
		if (m_pendingEvents.empty()) {
			return;
		}
		queue.swap(m_pendingEvents);
	}

	const int32_t curGen = m_generation.load(std::memory_order_acquire);
	const uint32_t now = nonZeroTickMs();
	for (const auto& ev : queue) {
		if (ev.generation != curGen) {
			continue;
		}
		if (ev.kind == WorkerEventKind::Progress) {
			m_manifest.onDownloadProgress(ev.type, ev.crc, ev.downloaded, ev.total);
		} else if (ev.kind == WorkerEventKind::DownloadFinished) {
			if (ev.success) {
				spdlog::info("Artwork: cached {} 0x{:X}", ev.type == FileType::Dff ? "DFF" : "TXD", ev.crc);
				recordDiskCached(ev.type, ev.crc, 0u);
				m_pendingHostRefresh = true;
			} else {
				spdlog::warn("Artwork: failed {} 0x{:X} (kind={}, retryable={}): {}",
					ev.type == FileType::Dff ? "DFF" : "TXD", ev.crc,
					static_cast<int>(ev.failureKind), isRetryableFailure(ev.failureKind), ev.error);
			}
			m_manifest.onDownloadFinished(ev.type, ev.crc, ev.success, ev.failureKind, ev.error, now);
		}
	}
}

void ArtworkManager::updateVwFreezeState()
{
	// Fast path: when no download gate is active and we aren't currently frozen, avoid any
	// engine/manifest work so idle GameMain frames stay O(1) (~1-2 us).
	const bool gateActive = m_manifest.isDownloadGateActive();
	if (!gateActive && !m_vwFreezeActive) {
		return;
	}
	if (!pGame) {
		return;
	}
	CPlayerPed* ped = pGame->FindPlayerPed();
	if (!ped) {
		return;
	}

	if (gateActive && m_manifest.virtualWorld() != 0) {
		const bool localActive = m_host.isLocalPlayerActive && m_host.isLocalPlayerActive();
		if (localActive && m_manifest.hasPendingActiveResources()) {
			if (!m_vwFreezeActive) {
				m_vwFreezeActive = true;
				m_vwUnfreezeAtMs = 0;
				ped->TogglePlayerControllable(false);
				ped->SetCollisionChecking(false);
				ped->SetGravityProcessing(false);
			}
			return;
		}
	}

	if (m_vwFreezeActive && !gateActive) {
		const uint32_t now = GetTickCount();
		if (m_vwUnfreezeAtMs == 0 || static_cast<int32_t>(now - m_vwUnfreezeAtMs) >= 0) {
			unfreezeIfNeeded();
		}
	}
}

void ArtworkManager::processFrame(NetGame* netGame)
{
	// Publish memory accounting snapshot for the telemetry thread every frame;
	// store state only mutates on this thread so the reads are race-free.
	g_telemetryTxdBytes.store(m_store.totalTxdBytes(), std::memory_order_relaxed);
	g_telemetryLoadedSlots.store(m_store.loadedSlotCount(), std::memory_order_relaxed);
	g_telemetryLoadedTxds.store(m_store.loadedTxdCount(), std::memory_order_relaxed);

	if (!m_sessionActive || !netGame) {
		return;
	}

	drainWorkerEvents();

	RakClientInterface* rak = netGame->GetRakClient();
	if (!rak || !rak->IsConnected() || netGame->session().protoVersion != proto::ProtoVersion::V03DL) {
		return;
	}

	const uint32_t nowTick = nonZeroTickMs();

	// Guard against unresponsive server on RPC 181/182 so join never deadlocks.
	if (m_waitingForUrl) {
		if (nowTick - m_waitingUrlSentMs > kUrlResponseTimeoutMs) {
			spdlog::warn("Artwork: timed out waiting for RPC 183 URL for {} 0x{:X}",
				m_waitingUrlType == FileType::Dff ? "DFF" : "TXD", m_waitingUrlCrc);
			m_manifest.onResourceAborted(m_waitingUrlType, m_waitingUrlCrc, DownloadFailureKind::TransientNet, "URL timeout", nowTick);
			m_waitingForUrl = false;
		}
	}

	// Dispatch next resource request (RPC 181 or 182)
	ResourceDownloadTask task{};
	if (m_manifest.popNextResourceToRequest(task, nowTick)) {
		RakNet::BitStream bsOut;
		proto::Writer writer(bsOut);
		if (task.type == FileType::Dff) {
			proto::DlRequestDffReq req{task.crc};
			req.serialize(writer, proto::ProtoVersion::V03DL);
			rak->RPC(&RPC_DlRequestDff, &bsOut, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false, UNASSIGNED_NETWORK_ID, nullptr);
		} else {
			proto::DlRequestTxdReq req{task.crc};
			req.serialize(writer, proto::ProtoVersion::V03DL);
			rak->RPC(&RPC_DlRequestTxd, &bsOut, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false, UNASSIGNED_NETWORK_ID, nullptr);
		}
		m_waitingForUrl = true;
		m_waitingUrlType = task.type;
		m_waitingUrlCrc = task.crc;
		m_waitingUrlSentMs = nowTick;
		spdlog::debug("Artwork: requested {} 0x{:X} ({}B)",
			task.type == FileType::Dff ? "DFF" : "TXD", task.crc, task.expectedSize);
	}

	// Settle the manifest if invalid/duplicate RPC 179 records kept records().size() slightly
	// below the server-advertised count once records stop arriving for kManifestSettleIdleMs.
	if (!m_manifest.isManifestSettled() && m_lastRecordArrivalMs != 0) {
		if (nowTick - m_lastRecordArrivalMs >= kManifestSettleIdleMs) {
			m_manifest.markManifestArrivalIdle();
		}
	}

	// Once all active-scope files are Cached or Failed and manifest has settled, notify server via RPC 184.
	if (m_manifest.shouldSendFinishDownload()) {
		m_manifest.markFinishDownloadSent();
		m_finishSentMs = nowTick;
		RakNet::BitStream bsOut;
		proto::Writer writer(bsOut);
		proto::DlFinishDownloadReq req{};
		req.serialize(writer, proto::ProtoVersion::V03DL);
		rak->RPC(&RPC_DlFinishDownload, &bsOut, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false, UNASSIGNED_NETWORK_ID, nullptr);
		spdlog::info("Artwork: sent FinishDownload (RPC 184) for vw={} (records={}/{})",
			m_manifest.virtualWorld(), m_manifest.records().size(), m_manifest.expectedRecordCount());
	}

	// Watchdog for RPC 185 (DownloadCompleted): release gate/freeze if server never responds.
	if (m_finishSentMs != 0 && m_manifest.isDownloadGateActive()) {
		if (nowTick - m_finishSentMs > kRpc185WatchdogMs) {
			spdlog::warn("Artwork: timed out waiting for RPC 185 (DownloadCompleted) after {}ms; releasing gate",
				kRpc185WatchdogMs);
			onDownloadCompletedRpc();
		}
	}

	if (m_pendingHostRefresh && !m_manifest.hasPendingActiveResources()) {
		m_pendingHostRefresh = false;
		if (m_host.onResourcesReady) {
			m_host.onResourcesReady();
		}
	}

	updateVwFreezeState();

	// Publish snapshot for ImGui DownloadOverlay ONLY while download gate is active (throttled
	// to ~10 Hz), plus a single clear-to-inactive transition when the gate drops. Never rebuild
	// a 6,000+ record unordered_set during pre-VW RPC 179 bursts or idle gameplay.
	const bool gateActive = m_manifest.isDownloadGateActive();
	const uint64_t rev = m_manifest.revision();
	if (!gateActive) {
		if (m_lastPublishedGateActive) {
			m_lastPublishedGateActive = false;
			m_lastSnapshotRevision = rev;
			DownloadProgressSnapshot emptySnap{};
			std::lock_guard<std::mutex> lock(m_eventMutex);
			m_cachedSnapshot = std::move(emptySnap);
		}
	} else {
		const bool justActivated = !m_lastPublishedGateActive;
		const bool intervalElapsed = (nowTick - m_lastSnapshotPublishMs) >= kSnapshotRebuildIntervalMs;
		if (justActivated || (rev != m_lastSnapshotRevision && intervalElapsed)) {
			m_lastPublishedGateActive = true;
			m_lastSnapshotRevision = rev;
			m_lastSnapshotPublishMs = nowTick;
			DownloadProgressSnapshot snap = m_manifest.buildProgressSnapshot();
			std::lock_guard<std::mutex> lock(m_eventMutex);
			m_cachedSnapshot = std::move(snap);
		}
	}
}

bool ArtworkManager::isDownloadGateActive() const noexcept
{
	return m_sessionActive && m_manifest.isDownloadGateActive();
}

DownloadProgressSnapshot ArtworkManager::progressSnapshot() const
{
	std::lock_guard<std::mutex> lock(m_eventMutex);
	return m_cachedSnapshot;
}

const CustomModelRecord* ArtworkManager::findRecord(int32_t serverModelId) const
{
	return m_manifest.findRecord(serverModelId);
}

bool ArtworkManager::isCustomSkinRegistered(int32_t serverModelId) const
{
	const CustomModelRecord* rec = m_manifest.findRecord(serverModelId);
	return rec != nullptr && rec->type == ModelType::Skin;
}

bool ArtworkManager::isCustomObjectRegistered(int32_t serverModelId) const
{
	const CustomModelRecord* rec = m_manifest.findRecord(serverModelId);
	return rec != nullptr && rec->type == ModelType::Object;
}

bool ArtworkManager::isTimedObject(int32_t serverModelId, uint8_t& outTimeOn, uint8_t& outTimeOff) const
{
	const CustomModelRecord* rec = m_manifest.findRecord(serverModelId);
	if (!rec || !rec->isTimed()) {
		return false;
	}
	outTimeOn = rec->timeOn;
	outTimeOff = rec->timeOff;
	return true;
}

int32_t ArtworkManager::resolveFallbackObjectModel(int32_t serverModelId) const
{
	const CustomModelRecord* rec = m_manifest.findRecord(serverModelId);
	if (rec && rec->type == ModelType::Object && rec->baseId >= 0 && rec->baseId < kInternalSlotBegin) {
		if (IsValidModel(rec->baseId)) {
			return rec->baseId;
		}
	}
	return kFallbackObjectModelId;
}

int32_t ArtworkManager::resolveFallbackSkinModel(int32_t serverSkinId) const
{
	const CustomModelRecord* rec = m_manifest.findRecord(serverSkinId);
	if (rec && rec->type == ModelType::Skin && rec->baseId >= 0 && rec->baseId < kInternalSlotBegin) {
		if (IsValidPedModel(static_cast<uint>(rec->baseId))) {
			return rec->baseId;
		}
	}
	return kFallbackSkinModelId;
}

int32_t ArtworkManager::ensureCustomSkinLoaded(int32_t serverSkinId)
{
	return m_store.ensureCustomSkinLoaded(m_manifest, makeCacheAccess(), serverSkinId);
}

int32_t ArtworkManager::ensureCustomObjectLoaded(int32_t serverModelId)
{
	return m_store.ensureCustomObjectLoaded(m_manifest, makeCacheAccess(), serverModelId);
}

int32_t ArtworkManager::resolveInternalSlot(int32_t serverModelId) const
{
	return m_manifest.resolveInternalSlot(serverModelId);
}

int32_t ArtworkManager::resolveServerModelId(int32_t internalSlot) const
{
	return m_manifest.resolveServerModelId(internalSlot);
}

RwTexture* ArtworkManager::findTextureInCustomModel(int32_t serverModelId, const char* texName)
{
	return m_store.findTextureInCustomModel(m_manifest, makeCacheAccess(), serverModelId, texName);
}

void ArtworkManager::unloadAllEngineModels()
{
	if (m_host.onBeforeUnloadModels) {
		m_host.onBeforeUnloadModels();
	}
	m_store.unloadAll(m_manifest);
	g_telemetryTxdBytes.store(0, std::memory_order_relaxed);
	g_telemetryLoadedSlots.store(0, std::memory_order_relaxed);
	g_telemetryLoadedTxds.store(0, std::memory_order_relaxed);
}

} // namespace artwork
