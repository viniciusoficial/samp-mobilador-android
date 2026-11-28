#include "ArtworkCore.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace artwork {

namespace {

constexpr std::array<uint32_t, 256> makeCrc32Table() noexcept
{
	std::array<uint32_t, 256> table{};
	for (uint32_t i = 0; i < 256; ++i) {
		uint32_t c = i;
		for (int bit = 0; bit < 8; ++bit) {
			c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
		}
		table[i] = c;
	}
	return table;
}

constexpr std::array<uint32_t, 256> kCrc32Table = makeCrc32Table();

bool isLoopbackHost(const std::string& host)
{
	return host == "127.0.0.1" || host == "0.0.0.0" || host == "localhost" || host == "::1";
}

} // namespace

bool isTimedObjectVisible(int hour, uint8_t timeOn, uint8_t timeOff) noexcept
{
	// Matches PC 0.3.DL CObject::IsTimeVisible() semantics:
	// valid hours are [0..23] without modulo wrapping, and timeOn == timeOff is never visible.
	if (hour < 0 || hour > 23 || timeOn > 23 || timeOff > 23 || timeOn == timeOff) {
		return false;
	}
	const int on = static_cast<int>(timeOn);
	const int off = static_cast<int>(timeOff);
	if (off > on) {
		return hour >= on && hour < off;
	}
	return hour >= on || hour < off;
}

uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t len) noexcept
{
	if (!data || len == 0) {
		return crc;
	}
	uint32_t c = ~crc;
	for (size_t i = 0; i < len; ++i) {
		c = kCrc32Table[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
	}
	return ~c;
}

uint32_t crc32Bytes(const uint8_t* data, size_t len) noexcept
{
	return crc32Update(0u, data, len);
}

std::string sanitizeHostForCache(const std::string& host)
{
	std::string out;
	out.reserve(25);
	for (char ch : host) {
		const unsigned char uc = static_cast<unsigned char>(ch);
		if (std::isalnum(uc) || ch == '.' || ch == '-') {
			out.push_back(static_cast<char>(std::tolower(uc)));
			if (out.size() >= 25) {
				break;
			}
		}
	}
	if (out.empty()) {
		return "server";
	}
	// Strip leading/trailing dots to prevent traversal artifacts like ".."
	while (!out.empty() && out.front() == '.') out.erase(out.begin());
	while (!out.empty() && out.back() == '.') out.pop_back();
	return out.empty() ? "server" : out;
}

std::string formatServerCacheDirName(const std::string& host, uint16_t port)
{
	return sanitizeHostForCache(host) + "." + std::to_string(port);
}

std::string formatCacheFileName(uint32_t crc, FileType type)
{
	char buf[32];
	const char* ext = (type == FileType::Dff) ? "dff" : "txd";
	std::snprintf(buf, sizeof(buf), "0x%X.%s", crc, ext);
	return std::string(buf);
}

bool parseCacheFileHash(const std::string& fileName, uint32_t& outCrc, FileType& outType)
{
	if (fileName.size() < 7) {
		return false;
	}
	if (fileName[0] != '0' || (fileName[1] != 'x' && fileName[1] != 'X')) {
		return false;
	}
	const size_t dot = fileName.rfind('.');
	if (dot == std::string::npos || dot <= 2 || dot - 2 > 8) {
		return false;
	}
	const std::string ext = fileName.substr(dot + 1);
	FileType parsedType = FileType::None;
	if (ext == "dff" || ext == "DFF") {
		parsedType = FileType::Dff;
	} else if (ext == "txd" || ext == "TXD") {
		parsedType = FileType::Txd;
	} else {
		return false;
	}
	uint32_t value = 0;
	for (size_t i = 2; i < dot; ++i) {
		const char ch = fileName[i];
		uint32_t digit = 0;
		if (ch >= '0' && ch <= '9') {
			digit = static_cast<uint32_t>(ch - '0');
		} else if (ch >= 'a' && ch <= 'f') {
			digit = static_cast<uint32_t>(10 + (ch - 'a'));
		} else if (ch >= 'A' && ch <= 'F') {
			digit = static_cast<uint32_t>(10 + (ch - 'A'));
		} else {
			return false;
		}
		value = (value << 4) | digit;
	}
	outCrc = value;
	outType = parsedType;
	return true;
}

bool isSafeHttpUrl(const std::string& url)
{
	if (url.size() < 8 || url.size() > kMaxArtworkUrlLength) {
		return false;
	}
	const bool isHttp = url.compare(0, 7, "http://") == 0;
	const bool isHttps = url.compare(0, 8, "https://") == 0;
	if (!isHttp && !isHttps) {
		return false;
	}
	for (char ch : url) {
		const unsigned char uc = static_cast<unsigned char>(ch);
		if (uc <= 0x20 || uc >= 0x7F) {
			return false;
		}
	}
	return true;
}

std::string redactUrlForLog(const std::string& url)
{
	const size_t schemeEnd = url.find("://");
	if (schemeEnd == std::string::npos) {
		return "<invalid-url>";
	}
	// Cut query string and fragment first so tokens/keys never hit logcat.
	size_t cut = url.find_first_of("?#", schemeEnd + 3);
	std::string clean = (cut == std::string::npos) ? url : url.substr(0, cut);
	// Strip any userinfo@ before host.
	const size_t hostStart = schemeEnd + 3;
	const size_t pathStart = clean.find('/', hostStart);
	const size_t atPos = clean.find('@', hostStart);
	if (atPos != std::string::npos && (pathStart == std::string::npos || atPos < pathStart)) {
		clean.erase(hostStart, atPos - hostStart + 1);
	}
	if (cut != std::string::npos) {
		clean += "?<redacted>";
	}
	return clean;
}

std::string rewriteLoopbackUrl(const std::string& url, const std::string& connectedServerHost)
{
	if (connectedServerHost.empty() || isLoopbackHost(connectedServerHost) || !isSafeHttpUrl(url)) {
		return url;
	}
	const size_t schemeEnd = url.find("://");
	if (schemeEnd == std::string::npos) {
		return url;
	}
	const size_t hostStart = schemeEnd + 3;
	size_t authorityEnd = url.find('/', hostStart);
	if (authorityEnd == std::string::npos) {
		authorityEnd = url.size();
	}
	const std::string authority = url.substr(hostStart, authorityEnd - hostStart);
	// Ignore URLs with userinfo '@'
	if (authority.find('@') != std::string::npos) {
		return url;
	}
	std::string host = authority;
	std::string portPart;
	const size_t colon = authority.rfind(':');
	if (colon != std::string::npos && authority.find(']') == std::string::npos) {
		host = authority.substr(0, colon);
		portPart = authority.substr(colon);
	}
	if (!isLoopbackHost(host)) {
		return url;
	}
	return url.substr(0, hostStart) + connectedServerHost + portPart + url.substr(authorityEnd);
}

namespace {
bool isPrivateOrLoopbackIpv4(const std::string& host)
{
	unsigned int a = 0, b = 0, c = 0, d = 0;
	char trailing = '\0';
	if (std::sscanf(host.c_str(), "%u.%u.%u.%u%c", &a, &b, &c, &d, &trailing) != 4) {
		return false;
	}
	if (a > 255 || b > 255 || c > 255 || d > 255) {
		return false;
	}
	if (a == 0 || a == 127 || a == 10) return true;
	if (a == 172 && b >= 16 && b <= 31) return true;
	if (a == 192 && b == 168) return true;
	if (a == 169 && b == 254) return true;
	return false;
}
} // namespace

std::string rewritePrivateHostToServer(const std::string& url, const std::string& connectedServerHost, uint16_t connectedServerPort)
{
	std::string out = rewriteLoopbackUrl(url, connectedServerHost);
	if (connectedServerHost.empty() || isLoopbackHost(connectedServerHost) || connectedServerPort == 0 || !isSafeHttpUrl(out)) {
		return out;
	}
	const size_t schemeEnd = out.find("://");
	if (schemeEnd == std::string::npos) {
		return out;
	}
	const size_t hostStart = schemeEnd + 3;
	size_t authorityEnd = out.find('/', hostStart);
	if (authorityEnd == std::string::npos) {
		authorityEnd = out.size();
	}
	const std::string authority = out.substr(hostStart, authorityEnd - hostStart);
	if (authority.find('@') != std::string::npos || authority.find(']') != std::string::npos) {
		return out;
	}
	const size_t colon = authority.rfind(':');
	if (colon == std::string::npos) {
		return out;
	}
	const std::string host = authority.substr(0, colon);
	const std::string portStr = authority.substr(colon + 1);
	if (portStr.empty() || host == connectedServerHost) {
		return out;
	}
	unsigned long parsedPort = 0;
	for (char ch : portStr) {
		if (ch < '0' || ch > '9') return out;
		parsedPort = parsedPort * 10ul + static_cast<unsigned long>(ch - '0');
		if (parsedPort > 65535ul) return out;
	}
	if (static_cast<uint16_t>(parsedPort) != connectedServerPort) {
		return out;
	}
	if (!isPrivateOrLoopbackIpv4(host)) {
		return out;
	}
	return out.substr(0, hostStart) + connectedServerHost + ":" + portStr + out.substr(authorityEnd);
}

void ArtworkManifest::reset()
{
	m_records.clear();
	m_recordOrder.clear();
	m_resources.clear();
	m_transferOrder.clear();
	releaseAllSlots();
	m_virtualWorld = 0;
	m_virtualWorldKnown = false;
	m_finishDownloadSent = false;
	m_downloadCompleted = false;
	m_manifestArrivalIdle = false;
	m_hadTransfersInCycle = false;
	m_expectedRecordCount = 0;
	m_urlRequestedCount = 0;
	m_downloadingCount = 0;
	m_requestCursor = 0;
	m_consecutiveNetErrors = 0;
	m_lastActiveLabel.clear();
	++m_revision;
	++m_stateRevision;
	m_pendingActiveCacheRev = 0;
	m_cachedHasPendingActive = false;
}

void ArtworkManifest::setResourceState(ResourceEntry& entry, ResourceState next) noexcept
{
	if (entry.state == next) {
		return;
	}
	if (entry.state == ResourceState::UrlRequested && m_urlRequestedCount > 0) {
		--m_urlRequestedCount;
	} else if (entry.state == ResourceState::Downloading && m_downloadingCount > 0) {
		--m_downloadingCount;
	}
	entry.state = next;
	if (next == ResourceState::UrlRequested) {
		++m_urlRequestedCount;
	} else if (next == ResourceState::Downloading) {
		++m_downloadingCount;
	}
	++m_stateRevision;
}

void ArtworkManifest::registerResourceRef(FileType type, uint32_t crc, uint32_t size)
{
	if (type == FileType::None || crc == 0) {
		return;
	}
	ResourceKey key{type, crc};
	auto it = m_resources.find(key);
	if (it == m_resources.end()) {
		ResourceEntry entry{};
		entry.state = ResourceState::Missing;
		entry.expectedSize = size;
		m_resources.emplace(key, entry);
	} else if (it->second.expectedSize == 0 && size > 0) {
		it->second.expectedSize = size;
	}
}

bool ArtworkManifest::addRecord(const CustomModelRecord& rec, int32_t serverCount,
	bool dffAlreadyCached, bool txdAlreadyCached)
{
	if (rec.type == ModelType::Skin) {
		if (!isCustomSkinId(rec.newId)) {
			return false;
		}
	} else if (rec.type == ModelType::Object) {
		if (!isCustomObjectId(rec.newId)) {
			return false;
		}
	} else {
		return false;
	}

	if (rec.baseId < 0 || rec.baseId >= 20000) {
		return false;
	}
	if (rec.dffCrc == 0 || rec.txdCrc == 0 || rec.dffSize == 0 || rec.txdSize == 0) {
		return false;
	}
	if (rec.dffSize > kMaxArtworkFileBytes || rec.txdSize > kMaxArtworkFileBytes) {
		return false;
	}

	auto it = m_records.find(rec.newId);
	if (it == m_records.end()) {
		m_recordOrder.push_back(rec.newId);
		m_records.emplace(rec.newId, rec);
	} else {
		it->second = rec;
	}

	if (serverCount > 0) {
		const size_t sc = static_cast<size_t>(serverCount);
		if (sc > m_expectedRecordCount) {
			m_expectedRecordCount = sc;
		}
	}
	m_manifestArrivalIdle = false;

	registerResourceRef(FileType::Dff, rec.dffCrc, rec.dffSize);
	registerResourceRef(FileType::Txd, rec.txdCrc, rec.txdSize);

	if (dffAlreadyCached && resourceState(FileType::Dff, rec.dffCrc) == ResourceState::Missing) {
		markResourceCached(FileType::Dff, rec.dffCrc);
	}
	if (txdAlreadyCached && resourceState(FileType::Txd, rec.txdCrc) == ResourceState::Missing) {
		markResourceCached(FileType::Txd, rec.txdCrc);
	}

	// Support runtime AddSimpleModel/AddCharModel (open.mp sends RPC 179 without toggling VW):
	// reopen the cycle ONLY if the newly added record is in active VW scope and genuinely needs
	// network downloading (never reopen for already-cached files, preventing overlay flicker).
	if (m_virtualWorldKnown && (m_finishDownloadSent || m_downloadCompleted) && isRecordInActiveScope(rec)) {
		const ResourceState dffState = resourceState(FileType::Dff, rec.dffCrc);
		const ResourceState txdState = resourceState(FileType::Txd, rec.txdCrc);
		if (dffState == ResourceState::Missing || txdState == ResourceState::Missing) {
			m_finishDownloadSent = false;
			m_downloadCompleted = false;
			m_hadTransfersInCycle = false;
		}
	}
	++m_revision;
	++m_stateRevision;
	return true;
}

void ArtworkManifest::markManifestArrivalIdle() noexcept
{
	if (!m_manifestArrivalIdle) {
		m_manifestArrivalIdle = true;
		++m_revision;
		++m_stateRevision;
	}
}

bool ArtworkManifest::isManifestSettled() const noexcept
{
	return m_records.size() >= m_expectedRecordCount || m_manifestArrivalIdle;
}

bool ArtworkManifest::isManifestCompleteForPurge() const noexcept
{
	return !m_records.empty() && m_records.size() >= m_expectedRecordCount;
}

bool ArtworkManifest::setVirtualWorld(int32_t vw)
{
	if (m_virtualWorldKnown && m_virtualWorld == vw) {
		return false;
	}
	m_virtualWorld = vw;
	m_virtualWorldKnown = true;
	m_finishDownloadSent = false;
	m_downloadCompleted = false;
	m_hadTransfersInCycle = false;
	m_requestCursor = 0;
	m_consecutiveNetErrors = 0;
	trimTransferHistory();
	++m_revision;
	++m_stateRevision;
	return true;
}

void ArtworkManifest::markResourceCached(FileType type, uint32_t crc)
{
	auto it = m_resources.find(ResourceKey{type, crc});
	if (it != m_resources.end()) {
		setResourceState(it->second, ResourceState::Cached);
		it->second.downloadedBytes = it->second.expectedSize;
		it->second.nextRetryAtMs = 0;
		it->second.lastError.clear();
		++m_revision;
	}
}

bool ArtworkManifest::isResourceCached(FileType type, uint32_t crc) const noexcept
{
	auto it = m_resources.find(ResourceKey{type, crc});
	return it != m_resources.end() && it->second.state == ResourceState::Cached;
}

ResourceState ArtworkManifest::resourceState(FileType type, uint32_t crc) const noexcept
{
	auto it = m_resources.find(ResourceKey{type, crc});
	return (it != m_resources.end()) ? it->second.state : ResourceState::Missing;
}

bool ArtworkManifest::isResourceReferenced(FileType type, uint32_t crc) const
{
	return m_resources.find(ResourceKey{type, crc}) != m_resources.end();
}

bool ArtworkManifest::isRecordInActiveScope(const CustomModelRecord& rec) const noexcept
{
	if (rec.type == ModelType::Skin) {
		return true;
	}
	if (rec.type == ModelType::Object) {
		return rec.virtualWorld == -1 || rec.virtualWorld == m_virtualWorld;
	}
	return false;
}

bool ArtworkManifest::isResourceInActiveScope(FileType type, uint32_t crc) const noexcept
{
	for (const auto& kv : m_records) {
		const CustomModelRecord& rec = kv.second;
		if (!isRecordInActiveScope(rec)) {
			continue;
		}
		if ((type == FileType::Dff && rec.dffCrc == crc) || (type == FileType::Txd && rec.txdCrc == crc)) {
			return true;
		}
	}
	return false;
}

void ArtworkManifest::trimTransferHistory()
{
	if (m_transferOrder.size() <= kMaxTransferHistory) {
		return;
	}
	// Keep all in-flight/failed entries plus the newest completed rows up to kMaxTransferHistory.
	size_t cachedCount = 0;
	for (const ResourceKey& key : m_transferOrder) {
		auto it = m_resources.find(key);
		if (it != m_resources.end() && it->second.state == ResourceState::Cached) {
			++cachedCount;
		}
	}
	if (cachedCount <= kMaxTransferHistory) {
		return;
	}
	size_t toDrop = cachedCount - kMaxTransferHistory;
	std::vector<ResourceKey> trimmed;
	trimmed.reserve(m_transferOrder.size() - toDrop);
	for (const ResourceKey& key : m_transferOrder) {
		auto it = m_resources.find(key);
		if (toDrop > 0 && it != m_resources.end() && it->second.state == ResourceState::Cached) {
			it->second.seenInTransfer = false;
			--toDrop;
			continue;
		}
		trimmed.push_back(key);
	}
	m_transferOrder.swap(trimmed);
}

namespace {
uint32_t retryBackoffMs(uint8_t retryCount) noexcept
{
	switch (retryCount) {
		case 1: return 1500u;
		case 2: return 3000u;
		case 3: return 6000u;
		case 4: return 12000u;
		default: return 20000u;
	}
}

bool isRetryReady(uint32_t nextRetryAtMs, uint32_t nowMs) noexcept
{
	if (nowMs == 0 || nextRetryAtMs == 0) {
		return true;
	}
	return static_cast<int32_t>(nowMs - nextRetryAtMs) >= 0;
}

bool isNetworkThrottleError(DownloadFailureKind kind) noexcept
{
	return kind == DownloadFailureKind::TransientNet
		|| kind == DownloadFailureKind::HttpServerError;
}
} // namespace

size_t ArtworkManifest::effectiveConcurrencyLimit() const noexcept
{
	if (m_consecutiveNetErrors >= 4u) {
		return 1u;
	}
	if (m_consecutiveNetErrors >= 2u) {
		return std::min<size_t>(2u, kMaxConcurrentDownloads);
	}
	return kMaxConcurrentDownloads;
}

bool ArtworkManifest::popNextResourceToRequest(ResourceDownloadTask& outTask, uint32_t nowMs)
{
	if (!m_virtualWorldKnown || m_finishDownloadSent || m_downloadCompleted || m_recordOrder.empty()) {
		return false;
	}
	// Limit concurrent URL requests in flight to 1 (matching server single-slot requestedFile_)
	// and total concurrent HTTP downloads to adaptive effectiveConcurrencyLimit().
	if (m_urlRequestedCount > 0u) {
		return false;
	}
	if (m_downloadingCount >= effectiveConcurrencyLimit()) {
		return false;
	}

	const size_t n = m_recordOrder.size();
	if (m_requestCursor >= n) {
		m_requestCursor = 0;
	}

	auto selectResource = [&](const ResourceKey& key, ResourceEntry& entry, size_t nextCursor) -> bool {
		setResourceState(entry, ResourceState::UrlRequested);
		entry.downloadedBytes = 0;
		entry.nextRetryAtMs = 0;
		entry.lastError.clear();
		if (!entry.seenInTransfer) {
			entry.seenInTransfer = true;
			m_transferOrder.push_back(key);
		}
		outTask.type = key.type;
		outTask.crc = key.crc;
		outTask.expectedSize = entry.expectedSize;
		m_hadTransfersInCycle = true;
		m_lastActiveLabel = formatCacheFileName(key.crc, key.type);
		m_requestCursor = nextCursor % n;
		++m_revision;
		return true;
	};

	bool hasFallbackRetry = false;
	ResourceKey fallbackKey{};
	size_t fallbackCursor = 0;

	// Scan from m_requestCursor (O(N) amortized across the whole manifest instead of O(N^2)),
	// preferring fresh (retryCount == 0) items over backed-off retries so a temporarily failing
	// resource never stalls fresh files.
	for (size_t step = 0; step < n; ++step) {
		const size_t idx = (m_requestCursor + step) % n;
		const int32_t modelId = m_recordOrder[idx];
		auto recIt = m_records.find(modelId);
		if (recIt == m_records.end() || !isRecordInActiveScope(recIt->second)) {
			continue;
		}
		const CustomModelRecord& rec = recIt->second;

		const ResourceKey keys[2] = {
			{FileType::Dff, rec.dffCrc},
			{FileType::Txd, rec.txdCrc},
		};
		for (size_t k = 0; k < 2; ++k) {
			const ResourceKey& key = keys[k];
			auto resIt = m_resources.find(key);
			if (resIt == m_resources.end() || resIt->second.state != ResourceState::Missing) {
				continue;
			}
			if (!isRetryReady(resIt->second.nextRetryAtMs, nowMs)) {
				continue;
			}
			const size_t nextCursor = (k == 0) ? idx : (idx + 1u);
			if (resIt->second.retryCount == 0) {
				return selectResource(key, resIt->second, nextCursor);
			}
			if (!hasFallbackRetry) {
				hasFallbackRetry = true;
				fallbackKey = key;
				fallbackCursor = nextCursor;
			}
		}
	}

	if (hasFallbackRetry) {
		auto resIt = m_resources.find(fallbackKey);
		if (resIt != m_resources.end() && resIt->second.state == ResourceState::Missing) {
			return selectResource(fallbackKey, resIt->second, fallbackCursor);
		}
	}
	return false;
}

bool ArtworkManifest::onUrlReceived(FileType type, uint32_t crc, uint32_t& outExpectedSize)
{
	auto it = m_resources.find(ResourceKey{type, crc});
	if (it == m_resources.end() || it->second.state != ResourceState::UrlRequested) {
		return false;
	}
	setResourceState(it->second, ResourceState::Downloading);
	it->second.downloadedBytes = 0;
	outExpectedSize = it->second.expectedSize;
	m_lastActiveLabel = formatCacheFileName(crc, type);
	++m_revision;
	return true;
}

bool ArtworkManifest::onDirectBegin(FileType type, uint32_t crc, uint32_t& outExpectedSize)
{
	return onUrlReceived(type, crc, outExpectedSize);
}

bool ArtworkManifest::requeueCorruptCacheResource(FileType type, uint32_t crc)
{
	auto it = m_resources.find(ResourceKey{type, crc});
	if (it == m_resources.end() || it->second.state != ResourceState::Cached) {
		return false;
	}
	if (it->second.corruptRecoveryCount >= kMaxCorruptCacheRecoveries) {
		setResourceState(it->second, ResourceState::Failed);
		it->second.lastError = "corrupt cache";
		++m_revision;
		return false;
	}
	++it->second.corruptRecoveryCount;
	setResourceState(it->second, ResourceState::Missing);
	it->second.downloadedBytes = 0;
	it->second.retryCount = 0;
	it->second.nextRetryAtMs = 0;
	it->second.lastError = "re-downloading corrupt cache";
	if (m_virtualWorldKnown && isResourceInActiveScope(type, crc)) {
		m_finishDownloadSent = false;
		m_downloadCompleted = false;
		m_requestCursor = 0;
		++m_stateRevision;
	}
	++m_revision;
	return true;
}

void ArtworkManifest::onResourceAborted(FileType type, uint32_t crc, DownloadFailureKind failureKind,
	const std::string& error, uint32_t nowMs)
{
	const std::string errMsg = error.empty() ? "aborted" : error;
	const bool retryable = isRetryableFailure(failureKind);
	if (isNetworkThrottleError(failureKind) && m_consecutiveNetErrors < 16u) {
		++m_consecutiveNetErrors;
	}
	auto applyAbort = [&](ResourceEntry& entry) {
		if (entry.state == ResourceState::Cached) {
			return;
		}
		if (retryable && entry.retryCount < kMaxDownloadRetries) {
			++entry.retryCount;
			setResourceState(entry, ResourceState::Missing);
			entry.downloadedBytes = 0;
			entry.nextRetryAtMs = (nowMs > 0) ? (nowMs + retryBackoffMs(entry.retryCount)) : 0u;
			entry.lastError = errMsg;
		} else {
			setResourceState(entry, ResourceState::Failed);
			entry.nextRetryAtMs = 0;
			entry.lastError = errMsg;
		}
		++m_revision;
	};
	if (crc == 0) {
		// Server sent generic abort (kind == 0) for the currently requested file.
		for (auto& kv : m_resources) {
			if (kv.second.state == ResourceState::UrlRequested) {
				applyAbort(kv.second);
			}
		}
		return;
	}
	auto it = m_resources.find(ResourceKey{type, crc});
	if (it != m_resources.end()) {
		applyAbort(it->second);
	}
}

void ArtworkManifest::onDownloadProgress(FileType type, uint32_t crc, uint64_t downloaded, uint64_t total)
{
	auto it = m_resources.find(ResourceKey{type, crc});
	if (it == m_resources.end() || it->second.state != ResourceState::Downloading) {
		return;
	}
	const uint64_t cap = (total > 0) ? total : it->second.expectedSize;
	const uint64_t clamped = (cap > 0) ? std::min(downloaded, cap) : downloaded;
	if (it->second.downloadedBytes != clamped) {
		it->second.downloadedBytes = clamped;
		++m_revision;
	}
}

void ArtworkManifest::onDownloadFinished(FileType type, uint32_t crc, bool success,
	DownloadFailureKind failureKind, const std::string& error, uint32_t nowMs)
{
	if (!success && failureKind == DownloadFailureKind::Cancelled) {
		return;
	}
	auto it = m_resources.find(ResourceKey{type, crc});
	if (it == m_resources.end()) {
		return;
	}
	if (success) {
		m_consecutiveNetErrors = 0;
		setResourceState(it->second, ResourceState::Cached);
		it->second.downloadedBytes = it->second.expectedSize;
		it->second.nextRetryAtMs = 0;
		it->second.lastError.clear();
		trimTransferHistory();
	} else {
		if (isNetworkThrottleError(failureKind) && m_consecutiveNetErrors < 16u) {
			++m_consecutiveNetErrors;
		}
		if (isRetryableFailure(failureKind) && it->second.retryCount < kMaxDownloadRetries) {
			++it->second.retryCount;
			setResourceState(it->second, ResourceState::Missing);
			it->second.downloadedBytes = 0;
			it->second.nextRetryAtMs = (nowMs > 0) ? (nowMs + retryBackoffMs(it->second.retryCount)) : 0u;
			it->second.lastError = error.empty() ? "retrying" : error;
		} else {
			setResourceState(it->second, ResourceState::Failed);
			it->second.nextRetryAtMs = 0;
			it->second.lastError = error.empty() ? "failed" : error;
		}
	}
	++m_revision;
}

bool ArtworkManifest::hasPendingActiveResources() const
{
	if (m_downloadCompleted || m_records.empty()) {
		return false;
	}
	if (m_pendingActiveCacheRev == m_stateRevision) {
		return m_cachedHasPendingActive;
	}

	bool found = false;
	for (int32_t modelId : m_recordOrder) {
		auto recIt = m_records.find(modelId);
		if (recIt == m_records.end() || !isRecordInActiveScope(recIt->second)) {
			continue;
		}
		const CustomModelRecord& rec = recIt->second;
		const ResourceKey keys[2] = {
			{FileType::Dff, rec.dffCrc},
			{FileType::Txd, rec.txdCrc},
		};
		for (const ResourceKey& key : keys) {
			auto resIt = m_resources.find(key);
			if (resIt != m_resources.end()) {
				const ResourceState st = resIt->second.state;
				if (st == ResourceState::Missing
					|| st == ResourceState::UrlRequested || st == ResourceState::Downloading) {
					found = true;
					break;
				}
			}
		}
		if (found) {
			break;
		}
	}
	m_cachedHasPendingActive = found;
	m_pendingActiveCacheRev = m_stateRevision;
	return found;
}

bool ArtworkManifest::shouldSendFinishDownload() const noexcept
{
	if (!m_virtualWorldKnown || m_finishDownloadSent || m_downloadCompleted) {
		return false;
	}
	if (!isManifestSettled()) {
		return false;
	}
	return !hasPendingActiveResources();
}

void ArtworkManifest::markFinishDownloadSent() noexcept
{
	m_finishDownloadSent = true;
	++m_revision;
	++m_stateRevision;
}

void ArtworkManifest::onDownloadCompletedRpc() noexcept
{
	m_downloadCompleted = true;
	m_hadTransfersInCycle = false;
	trimTransferHistory();
	++m_revision;
	++m_stateRevision;
}

bool ArtworkManifest::isDownloadGateActive() const noexcept
{
	if (!m_virtualWorldKnown || m_downloadCompleted) {
		return false;
	}
	// Gate engages while custom model records are streaming/downloading or FinishDownload is in flight.
	return !m_records.empty() || m_expectedRecordCount > 0;
}

DownloadProgressSnapshot ArtworkManifest::buildProgressSnapshot() const
{
	DownloadProgressSnapshot snap{};
	snap.currentLabel = m_lastActiveLabel;

	bool hasPendingTransfers = false;
	std::unordered_set<ResourceKey, ResourceKeyHash> seen;
	seen.reserve(m_resources.size());
	for (int32_t modelId : m_recordOrder) {
		auto recIt = m_records.find(modelId);
		if (recIt == m_records.end() || !isRecordInActiveScope(recIt->second)) {
			continue;
		}
		const CustomModelRecord& rec = recIt->second;
		const ResourceKey keys[2] = {
			{FileType::Dff, rec.dffCrc},
			{FileType::Txd, rec.txdCrc},
		};
		for (const ResourceKey& key : keys) {
			if (!seen.insert(key).second) {
				continue;
			}
			auto resIt = m_resources.find(key);
			if (resIt == m_resources.end()) {
				continue;
			}
			++snap.totalFiles;
			snap.totalBytes += resIt->second.expectedSize;
			if (resIt->second.state == ResourceState::Cached) {
				++snap.completedFiles;
				snap.downloadedBytes += resIt->second.expectedSize;
			} else if (resIt->second.state == ResourceState::Failed) {
				++snap.failedFiles;
				snap.downloadedBytes += resIt->second.expectedSize;
			} else {
				hasPendingTransfers = true;
				if (resIt->second.state == ResourceState::Downloading) {
					snap.downloadedBytes += std::min<uint64_t>(resIt->second.downloadedBytes, resIt->second.expectedSize);
				}
			}
		}
	}

	// Only display DownloadOverlay when actual network downloads are pending/in-flight (or
	// finishing the current download batch's RPC 184/185 handshake) — never flash on 100% cache hits.
	snap.active = hasPendingTransfers || (m_hadTransfersInCycle && m_finishDownloadSent && !m_downloadCompleted);

	snap.items.reserve(m_transferOrder.size());
	for (const ResourceKey& key : m_transferOrder) {
		auto resIt = m_resources.find(key);
		if (resIt == m_resources.end()) {
			continue;
		}
		DownloadListItem item{};
		item.type = key.type;
		item.crc = key.crc;
		item.state = resIt->second.state;
		item.downloadedBytes = resIt->second.downloadedBytes;
		item.totalBytes = resIt->second.expectedSize;
		item.error = resIt->second.lastError;
		snap.items.push_back(std::move(item));
	}
	return snap;
}

const CustomModelRecord* ArtworkManifest::findRecord(int32_t serverModelId) const
{
	auto it = m_records.find(serverModelId);
	return (it != m_records.end()) ? &it->second : nullptr;
}

bool ArtworkManifest::isModelReadyOnDisk(int32_t serverModelId) const
{
	const CustomModelRecord* rec = findRecord(serverModelId);
	if (!rec) {
		return false;
	}
	auto dffIt = m_resources.find(ResourceKey{FileType::Dff, rec->dffCrc});
	auto txdIt = m_resources.find(ResourceKey{FileType::Txd, rec->txdCrc});
	return dffIt != m_resources.end() && dffIt->second.state == ResourceState::Cached &&
	       txdIt != m_resources.end() && txdIt->second.state == ResourceState::Cached;
}

int32_t ArtworkManifest::allocateInternalSlot(int32_t serverModelId)
{
	auto existing = m_serverToSlot.find(serverModelId);
	if (existing != m_serverToSlot.end()) {
		return existing->second;
	}
	const int32_t span = kInternalSlotEnd - kInternalSlotBegin;
	for (int32_t i = 0; i < span; ++i) {
		const int32_t candidate = kInternalSlotBegin + ((m_nextSlotCursor - kInternalSlotBegin + i) % span);
		if (m_slotToServer.find(candidate) == m_slotToServer.end()) {
			m_serverToSlot.emplace(serverModelId, candidate);
			m_slotToServer.emplace(candidate, serverModelId);
			m_nextSlotCursor = kInternalSlotBegin + ((candidate - kInternalSlotBegin + 1) % span);
			return candidate;
		}
	}
	return -1;
}

int32_t ArtworkManifest::resolveInternalSlot(int32_t serverModelId) const
{
	auto it = m_serverToSlot.find(serverModelId);
	return (it != m_serverToSlot.end()) ? it->second : -1;
}

int32_t ArtworkManifest::resolveServerModelId(int32_t internalSlot) const
{
	auto it = m_slotToServer.find(internalSlot);
	return (it != m_slotToServer.end()) ? it->second : -1;
}

void ArtworkManifest::releaseInternalSlot(int32_t serverModelId)
{
	auto it = m_serverToSlot.find(serverModelId);
	if (it != m_serverToSlot.end()) {
		m_slotToServer.erase(it->second);
		m_serverToSlot.erase(it);
	}
}

void ArtworkManifest::releaseAllSlots()
{
	m_serverToSlot.clear();
	m_slotToServer.clear();
	m_nextSlotCursor = kInternalSlotBegin;
}

std::vector<int32_t> ArtworkManifest::allocatedInternalSlots() const
{
	std::vector<int32_t> slots;
	slots.reserve(m_slotToServer.size());
	for (const auto& kv : m_slotToServer) {
		slots.push_back(kv.first);
	}
	return slots;
}

} // namespace artwork
