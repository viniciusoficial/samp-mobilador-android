#pragma once

#include "ArtworkCore.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace sa {
class CBaseModelInfo;
class CColModel;
}
struct RwTexDictionary;
struct RwTexture;

namespace artwork {

// Self-contained engine resource store for 0.3.DL / open.mp custom models, collisions,
// and RenderWare texture dictionaries. Owned exclusively by ArtworkManager on GameMain.
//
// Design invariants:
//   - Zero module-level mutable globals (all state is instance-owned).
//   - Zero dependency on Net/ entity pools (callers detach/revert entities before unloadAll).
//   - Explicit field-by-field CAtomicModelInfo / CPedModelInfo construction (no raw memcpy).
//   - LRU eviction of idle (m_numRefs == 0) slots & unreferenced TXDs under slot/VRAM pressure.
class ArtworkStore {
public:
	struct CacheAccess {
		std::function<bool(FileType, uint32_t, std::string&)> resolvePath;
		// Removes corrupt server cache file and attempts bounded manifest re-queue.
		// Returns true if the resource was re-queued for re-download (caller should NOT
		// permanently poison m_failedModels / m_failedTxds).
		std::function<bool(FileType, uint32_t)> invalidateCorrupt;
	};

	static constexpr size_t kMaxSessionTxdBytes = 288u * 1024u * 1024u; // 288 MiB hard VRAM budget
	static constexpr size_t kSoftEvictTxdBytes  = 200u * 1024u * 1024u; // 200 MiB proactive VW-switch trim

	int32_t ensureCustomSkinLoaded(ArtworkManifest& manifest, const CacheAccess& cache, int32_t serverSkinId);
	int32_t ensureCustomObjectLoaded(ArtworkManifest& manifest, const CacheAccess& cache, int32_t serverModelId);

	RwTexture* findTextureInCustomModel(ArtworkManifest& manifest, const CacheAccess& cache,
		int32_t serverModelId, const char* texName);

	// Proactively evicts idle (m_numRefs == 0) slots & unreferenced TXDs when above soft VRAM watermark.
	void evictIdleIfAboveWatermark(ArtworkManifest& manifest);

	// Destroys all loaded models, collisions, and TXDs. Caller MUST detach/revert live entities first.
	void unloadAll(ArtworkManifest& manifest);

	size_t loadedSlotCount() const noexcept { return m_loadedSlots.size(); }
	size_t loadedTxdCount() const noexcept { return m_loadedTxds.size(); }
	size_t totalTxdBytes() const noexcept { return m_totalTxdBytes; }

private:
	struct LoadedTxd {
		RwTexDictionary* dict = nullptr;
		size_t decodedBytes = 0;
		uint32_t slotRefCount = 0;
		uint64_t lastUseSeq = 0;
	};

	struct LoadedSlot {
		int32_t internalSlot = -1;
		int32_t serverModelId = 0;
		uint32_t txdCrc = 0;
		bool isPed = false;
		sa::CBaseModelInfo* modelInfo = nullptr;
		sa::CColModel* ownedCol = nullptr;
		uint64_t lastUseSeq = 0;
	};

	RwTexDictionary* ensureTxdLoaded(ArtworkManifest& manifest, const CacheAccess& cache, uint32_t txdCrc);
	bool evictOneIdleSlot(ArtworkManifest& manifest);
	void evictIdleToBudget(ArtworkManifest& manifest, size_t targetTotalBytes);
	void destroyLoadedSlot(ArtworkManifest& manifest, LoadedSlot& ls);
	void releaseTxdRef(uint32_t txdCrc);

	std::unordered_map<uint32_t, LoadedTxd> m_loadedTxds;
	std::unordered_set<uint32_t> m_failedTxds;
	std::unordered_map<int32_t, LoadedSlot> m_loadedSlots; // key = internalSlot
	std::unordered_set<int32_t> m_failedModels;            // key = serverModelId
	size_t m_totalTxdBytes = 0;
	uint64_t m_useSeq = 0;
};

// Priority lookup called from RwTextureRead hook while loading a custom 0.3.DL DFF.
// Returns nullptr immediately when no custom TXD scope is active.
RwTexture* findTextureInActiveCustomTxd(const char* textureName);

} // namespace artwork
