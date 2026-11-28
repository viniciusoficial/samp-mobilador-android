#include "ArtworkStore.h"
#include "DffValidator.h"
#include "TxdParser.h"

#include "../Patches.h"
#include "../Engine/Symbols.h"

#include <SA.h>
#include <StreamingInfo.h>
#include <RenderWare/rw.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <strings.h>
#include <vector>

namespace artwork {

namespace {

// Active custom TXD scope during RpClumpStreamRead / LoadAtomicFile (GameMain thread).
thread_local RwTexDictionary* tl_activeCustomTxd = nullptr;

class ScopedCustomTxd {
public:
	explicit ScopedCustomTxd(RwTexDictionary* dict) noexcept
		: m_prev(tl_activeCustomTxd)
	{
		tl_activeCustomTxd = dict;
	}
	~ScopedCustomTxd() noexcept
	{
		tl_activeCustomTxd = m_prev;
	}
	ScopedCustomTxd(const ScopedCustomTxd&) = delete;
	ScopedCustomTxd& operator=(const ScopedCustomTxd&) = delete;

private:
	RwTexDictionary* m_prev = nullptr;
};

struct FindTexCtx {
	const char* targetName = nullptr;
	RwTexture* result = nullptr;
};

RwTexture* findTextureCaseInsensitiveCB(RwTexture* tex, void* data)
{
	auto* ctx = static_cast<FindTexCtx*>(data);
	if (!tex || !ctx || !ctx->targetName) {
		return tex;
	}
	if (strcasecmp(tex->name, ctx->targetName) == 0) {
		ctx->result = tex;
		return nullptr; // stop iteration
	}
	return tex;
}

RwTexture* findTextureInDictCaseInsensitive(RwTexDictionary* dict, const char* textureName)
{
	if (!dict || !textureName || !*textureName) {
		return nullptr;
	}
	FindTexCtx ctx{textureName, nullptr};
	using TexCB = RwTexture* (*)(RwTexture*, void*);
	eng::fn::RwTexDictionaryForAllTextures(dict, static_cast<TexCB>(&findTextureCaseInsensitiveCB), &ctx);
	return ctx.result;
}

// Disassembly-verified RenderWare invariant (_Z22RwTexDictionaryDestroyP15RwTexDictionary /
// _Z16RwTextureDestroyP9RwTexture): RwTexDictionaryDestroy frees `dict` WITHOUT unlinking any
// texture whose refCount > 1, leaving tex->dict dangling until the external holder destroys it.
// Therefore:
//   1. Idle LRU eviction skips any TXD containing a texture with refCount > 1.
//   2. Any destruction of a TXD first unlinks refCount > 1 textures via RwTexDictionaryRemoveTexture
//      (clearing tex->dict = nullptr) and drops the dict's own ref via RwTextureDestroy.
RwTexture* checkExternalRefCB(RwTexture* tex, void* data)
{
	auto* hasExt = static_cast<bool*>(data);
	if (tex && tex->refCount > 1) {
		*hasExt = true;
		return nullptr; // stop early
	}
	return tex;
}

bool dictHasExternalTextureRefs(RwTexDictionary* dict)
{
	if (!dict) return false;
	bool hasExt = false;
	using TexCB = RwTexture* (*)(RwTexture*, void*);
	eng::fn::RwTexDictionaryForAllTextures(dict, static_cast<TexCB>(&checkExternalRefCB), &hasExt);
	return hasExt;
}

RwTexture* collectExternalRefsCB(RwTexture* tex, void* data)
{
	auto* list = static_cast<std::vector<RwTexture*>*>(data);
	if (tex && tex->refCount > 1) {
		list->push_back(tex);
	}
	return tex;
}

void safeDestroyTexDictionary(RwTexDictionary* dict)
{
	if (!dict) return;
	std::vector<RwTexture*> surviving;
	using TexCB = RwTexture* (*)(RwTexture*, void*);
	eng::fn::RwTexDictionaryForAllTextures(dict, static_cast<TexCB>(&collectExternalRefsCB), &surviving);
	for (RwTexture* tex : surviving) {
		eng::fn::RwTexDictionaryRemoveTexture(tex);
		RwTextureDestroy(tex);
	}
	eng::fn::RwTexDictionaryDestroy(dict);
}

struct ClumpGeomCheck {
	int atomicCount = 0;
	int totalVerts = 0;
};

RpAtomic* checkClumpGeometryCB(RpAtomic* atomic, void* data)
{
	auto* check = static_cast<ClumpGeomCheck*>(data);
	if (atomic && check) {
		++check->atomicCount;
		if (atomic->geometry && atomic->geometry->numVertices > 0) {
			check->totalVerts += atomic->geometry->numVertices;
		}
	}
	return atomic;
}

bool readFileBytes(const std::string& path, std::vector<uint8_t>& outBytes, size_t maxBytes)
{
	std::ifstream in(path, std::ios::binary | std::ios::ate);
	if (!in) {
		return false;
	}
	const std::streamoff sz = in.tellg();
	if (sz <= 0 || static_cast<size_t>(sz) > maxBytes) {
		return false;
	}
	in.seekg(0, std::ios::beg);
	outBytes.resize(static_cast<size_t>(sz));
	in.read(reinterpret_cast<char*>(outBytes.data()), sz);
	return in.good() || in.eof();
}

bool isAtomicObjectVtable(uintptr_t vptr)
{
	return vptr == eng::vmtSlot(eng::Sym::vtable_CAtomicModelInfo, 1)
		|| vptr == eng::vmtSlot(eng::Sym::vtable_CDamageAtomicModelInfo, 1)
		|| vptr == eng::vmtSlot(eng::Sym::vtable_CLodAtomicModelInfo, 1)
		|| vptr == eng::vmtSlot(eng::Sym::vtable_CTimeModelInfo, 1)
		|| vptr == eng::vmtSlot(eng::Sym::vtable_CLodTimeModelInfo, 1);
}

// Explicit engine-assisted CBaseModelInfo initializer: runs libGTASA's own CBaseModelInfo::Init()
// first so ABI-specific defaults match the engine verbatim, then applies custom model attributes.
void initCustomBaseModelInfo(sa::CBaseModelInfo* dst, const sa::CBaseModelInfo* baseInfo,
	uintptr_t vtable, const char* modelName, float minLodDistance)
{
	dst->vtable = vtable;
	eng::fn::CBaseModelInfo_Init(dst);
	char upperName[24]{};
	size_t nameLen = 0;
	for (; modelName && modelName[nameLen] && nameLen + 1 < sizeof(upperName); ++nameLen) {
		upperName[nameLen] = static_cast<char>(std::toupper(static_cast<unsigned char>(modelName[nameLen])));
	}
	dst->m_hashKey = crc32Bytes(reinterpret_cast<const uint8_t*>(upperName), nameLen);
	std::memset(dst->m_modelName, 0, sizeof(dst->m_modelName));
	std::snprintf(reinterpret_cast<char*>(dst->m_modelName), sizeof(dst->m_modelName), "%s", modelName);
	dst->pad[0] = 0;
	dst->m_numRefs = 0;
	dst->m_txdIndex = (baseInfo->m_txdIndex >= 0) ? baseInfo->m_txdIndex : 0;
	dst->m_alpha = baseInfo->m_alpha;
	dst->m_num2dEffects = 0;
	dst->m_n2dEffects = -1;
	// Keep m_dynamicIndex = -1 so CObjectData::SetObjectData falls back to ms_aObjectInfo[0]
	// (m_nCameraAvoidObject == 2: blocks camera) rather than inheriting camera-ignore flags
	// from an object.dat small-prop baseId.
	dst->m_dynamicIndex = -1;
	dst->m_flags = static_cast<uint16_t>(baseInfo->m_flags & ~(0x0080u | 0x0800u | 0x8000u));
	dst->pad2[0] = 0;
	dst->pad2[1] = 0;
	dst->m_pColModel = baseInfo->m_pColModel; // replaced by ownedCol for objects with embedded COL
	dst->m_lodDistance = (baseInfo->m_lodDistance >= minLodDistance) ? baseInfo->m_lodDistance : minLodDistance;
	dst->m_pRwObject = nullptr;
}

sa::CPedModelInfo* createCustomPedModelInfo(const sa::CPedModelInfo* basePed, uintptr_t pedVptr, int32_t serverSkinId)
{
	char nameBuf[21];
	std::snprintf(nameBuf, sizeof(nameBuf), "dl_p_%d", serverSkinId);

	auto* model = new sa::CPedModelInfo{};
	initCustomBaseModelInfo(model, basePed, pedVptr, nameBuf, 0.0f);
	model->m_animFileIndex = static_cast<uint32_t>(-1);
	model->m_motionAnimGroup = basePed->m_motionAnimGroup;
	model->m_defaultPedType = basePed->m_defaultPedType;
	model->m_defaultPedStats = basePed->m_defaultPedStats;
	model->m_drivesWhichCars = basePed->m_drivesWhichCars;
	model->m_flags = basePed->m_flags;
	model->m_pHitColModel = nullptr;
	model->m_radio1 = basePed->m_radio1;
	model->m_radio2 = basePed->m_radio2;
	model->m_Race = basePed->m_Race;
	model->m_AudioPedType = basePed->m_AudioPedType;
	model->m_FirstVoice = basePed->m_FirstVoice;
	model->m_LastVoice = basePed->m_LastVoice;
	model->m_NextVoice = basePed->m_NextVoice;
	return model;
}

sa::CAtomicModelInfo* createCustomAtomicModelInfo(const sa::CBaseModelInfo* baseInfo, int32_t serverModelId)
{
	char nameBuf[21];
	std::snprintf(nameBuf, sizeof(nameBuf), "dl_o_%d", serverModelId);

	auto* model = new sa::CAtomicModelInfo{};
	const uintptr_t atomicVptr = eng::vmtSlot(eng::Sym::vtable_CAtomicModelInfo, 1);
	initCustomBaseModelInfo(model, baseInfo, atomicVptr, nameBuf, 300.0f);
	return model;
}

RwTexture* createRwTextureFromDecoded(const DecodedTexture& dt)
{
	if (dt.width == 0 || dt.height == 0 || dt.rgba.size() < static_cast<size_t>(dt.width) * dt.height * 4) {
		return nullptr;
	}

	RwImage* img = RwImageCreate(static_cast<RwInt32>(dt.width), static_cast<RwInt32>(dt.height), 32);
	if (!img) {
		return nullptr;
	}
	RwImageAllocatePixels(img);
	if (!img->cpPixels) {
		RwImageDestroy(img);
		return nullptr;
	}

	const size_t rowBytes = static_cast<size_t>(dt.width) * 4;
	const size_t copyBytes = (static_cast<size_t>(img->stride) < rowBytes) ? static_cast<size_t>(img->stride) : rowBytes;
	for (uint32_t y = 0; y < dt.height; ++y) {
		std::memcpy(img->cpPixels + static_cast<size_t>(y) * img->stride,
			dt.rgba.data() + static_cast<size_t>(y) * rowBytes,
			copyBytes);
	}

	RwInt32 w = 0, h = 0, d = 0, flags = 0;
	RwImageFindRasterFormat(img, rwRASTERTYPETEXTURE, &w, &h, &d, &flags);

	// Disassembly-verified Android GTA:SA v2.10 invariant (_rwOpenGLRasterLock / _rwOpenGLRasterUnlock /
	// TextureRasterDefaultBuildMipmaps):
	//   - NEVER call RwTextureRasterGenerateMipmaps() on Android! Its read-lock on level 1 leaves
	//     raster->cpPixels allocated at 1/4 size (read-unlock skips free), and the subsequent level-0
	//     write-lock reuses that 1/4-size buffer and copies the full mip0 image past the chunk end!
	//   - Instead, setting rwRASTERFORMATMIPMAP (0x8000) | rwRASTERFORMATAUTOMIPMAP (0x1000) on
	//     RGBA8888 (0x0500) / RGB888 (0x0600) power-of-2 textures instructs _rwOpenGLRasterUnlock to
	//     allocate the full mip0 buffer once, upload level 0, box-filter in-place for levels 1..N,
	//     upload each level to GL, and free cpPixels cleanly.
	constexpr RwInt32 kRwRasterFormatMipmap = 0x8000;
	constexpr RwInt32 kRwRasterFormatAutoMipmap = 0x1000;
	const RwInt32 fmtMask = flags & 0x0F00;
	const bool isPow2 = (w >= 2 && h >= 2) && ((w & (w - 1)) == 0) && ((h & (h - 1)) == 0);
	const bool canAutoMipmap = isPow2 && (fmtMask == 0x0500 || fmtMask == 0x0600);
	RwRaster* raster = nullptr;
	bool hasMipmaps = false;

	if (canAutoMipmap) {
		raster = RwRasterCreate(w, h, d, flags | kRwRasterFormatMipmap | kRwRasterFormatAutoMipmap);
		if (raster) {
			RwRasterSetFromImage(raster, img);
			hasMipmaps = (eng::fn::RwRasterGetNumLevels<int32_t>(raster) > 1);
		}
	}
	if (!raster) {
		raster = RwRasterCreate(w, h, d, flags);
		if (!raster) {
			RwImageDestroy(img);
			return nullptr;
		}
		RwRasterSetFromImage(raster, img);
	}
	RwImageDestroy(img);

	RwTexture* tex = RwTextureCreate(raster);
	if (!tex) {
		RwRasterDestroy(raster);
		return nullptr;
	}

	eng::fn::RwTextureSetName(tex, dt.name.c_str());
	if (!dt.maskName.empty()) {
		eng::fn::RwTextureSetMaskName(tex, dt.maskName.c_str());
	}

	// Preserve mipmap filter modes when full mip chain was generated; otherwise clamp to LINEAR/NEAREST
	// so GLES2 drivers never sample an incomplete mipmap chain as black.
	const uint32_t rawFilter = dt.filterFlags & 0xFFu;
	uint32_t filterMode = 2u; // rwFILTERLINEAR
	if (hasMipmaps) {
		if (rawFilter >= 1u && rawFilter <= 6u) {
			filterMode = rawFilter;
		} else {
			filterMode = 6u; // rwFILTERLINEARMIPLINEAR (trilinear)
		}
	} else {
		filterMode = (rawFilter == 1u /* rwFILTERNEAREST */) ? 1u : 2u /* rwFILTERLINEAR */;
	}
	uint32_t addrU = (dt.filterFlags >> 8) & 0x0Fu;
	uint32_t addrV = (dt.filterFlags >> 12) & 0x0Fu;
	if (addrU == 0u || addrU > 4u) addrU = 1u; // rwTEXTUREADDRESSWRAP
	if (addrV == 0u || addrV > 4u) addrV = 1u;
	tex->filterAddressing = (filterMode & 0xFFu) | (addrU << 8) | (addrV << 12);
	return tex;
}

sa::CColModel* loadEmbeddedColModel(const std::vector<uint8_t>& colBytes, const char* debugName)
{
	ColFormat fmt = ColFormat::None;
	std::vector<uint8_t> mutableBody;
	std::string colErr;
	if (!prepareColBodyForEngine(colBytes.data(), colBytes.size(), fmt, mutableBody, colErr)) {
		spdlog::warn("ArtworkStore: rejected embedded COL for {}: {}", debugName, colErr);
		return nullptr;
	}
	const uint32_t bodySize = static_cast<uint32_t>(mutableBody.size());

	void* mem = eng::fn::CColModel_OpNew<void*>(sizeof(sa::CColModel));
	if (!mem) {
		spdlog::error("ArtworkStore: CColModel::operator new failed for {}", debugName);
		return nullptr;
	}
	eng::fn::CColModel_Ctor(mem);
	auto* col = static_cast<sa::CColModel*>(mem);

	uint8_t* bodyPtr = mutableBody.data();

	if (fmt == ColFormat::Col1) {
		eng::fn::CFileLoader_LoadCollisionModel(bodyPtr, col, debugName);
	} else if (fmt == ColFormat::Col2) {
		eng::fn::CFileLoader_LoadCollisionModelVer2(bodyPtr, bodySize, col, debugName);
	} else if (fmt == ColFormat::Col3) {
		eng::fn::CFileLoader_LoadCollisionModelVer3(bodyPtr, bodySize, col, debugName);
	} else {
		eng::fn::CFileLoader_LoadCollisionModelVer4(bodyPtr, bodySize, col, debugName);
	}

	eng::fn::CColModel_MakeMultipleAlloc(col);
	return col;
}

void markStreamingSlotLoaded(int32_t slot)
{
	sa::CStreamingInfo* table = Patches::streamingInfoTable();
	if (!table || !isInternalCustomSlot(slot)) {
		return;
	}
	table[slot].m_nNextIndex = -1;
	table[slot].m_nPrevIndex = -1;
	table[slot].m_nNextModelOnCd = -1;
	table[slot].m_nFlags = sa::STREAMING_GAME_REQUIRED | sa::STREAMING_KEEP_IN_MEMORY;
	table[slot].m_nStatus = sa::LOADSTATE_LOADED;
}

void resetStreamingSlot(int32_t slot)
{
	sa::CStreamingInfo* table = Patches::streamingInfoTable();
	if (!table || !isInternalCustomSlot(slot)) {
		return;
	}
	table[slot].m_nNextIndex = -1;
	table[slot].m_nPrevIndex = -1;
	table[slot].m_nNextModelOnCd = -1;
	table[slot].m_nFlags = 0;
	table[slot].m_nStatus = sa::LOADSTATE_NOT_LOADED;
}

} // namespace

RwTexture* findTextureInActiveCustomTxd(const char* textureName)
{
	if (!tl_activeCustomTxd || !textureName || !*textureName) {
		return nullptr;
	}
	return findTextureInDictCaseInsensitive(tl_activeCustomTxd, textureName);
}

void ArtworkStore::releaseTxdRef(uint32_t txdCrc)
{
	if (txdCrc == 0) {
		return;
	}
	auto it = m_loadedTxds.find(txdCrc);
	if (it == m_loadedTxds.end()) {
		return;
	}
	if (it->second.slotRefCount > 0) {
		--it->second.slotRefCount;
	}
	if (it->second.slotRefCount == 0) {
		if (it->second.dict) {
			safeDestroyTexDictionary(it->second.dict);
		}
		if (m_totalTxdBytes >= it->second.decodedBytes) {
			m_totalTxdBytes -= it->second.decodedBytes;
		} else {
			m_totalTxdBytes = 0;
		}
		m_loadedTxds.erase(it);
	}
}

void ArtworkStore::destroyLoadedSlot(ArtworkManifest& manifest, LoadedSlot& ls)
{
	sa::CBaseModelInfo** modelInfoPtrs = Patches::modelInfoPtrs();
	if (ls.modelInfo) {
		if (ls.isPed) {
			eng::fn::CPedModelInfo_DeleteRwObject(ls.modelInfo);
		} else {
			eng::fn::CAtomicModelInfo_DeleteRwObject(ls.modelInfo);
		}
		if (ls.ownedCol) {
			eng::fn::CBaseModelInfo_SetColModel(ls.modelInfo, static_cast<sa::CColModel*>(nullptr), false);
			eng::fn::CColModel_Dtor(ls.ownedCol);
			eng::fn::CColModel_OpDelete(ls.ownedCol);
			ls.ownedCol = nullptr;
		}
		if (ls.isPed) {
			delete static_cast<sa::CPedModelInfo*>(ls.modelInfo);
		} else {
			delete static_cast<sa::CAtomicModelInfo*>(ls.modelInfo);
		}
		ls.modelInfo = nullptr;
	}
	if (modelInfoPtrs && isInternalCustomSlot(ls.internalSlot)) {
		modelInfoPtrs[ls.internalSlot] = nullptr;
	}
	resetStreamingSlot(ls.internalSlot);
	manifest.releaseInternalSlot(ls.serverModelId);
	releaseTxdRef(ls.txdCrc);
}

bool ArtworkStore::evictOneIdleSlot(ArtworkManifest& manifest)
{
	int32_t victimSlot = -1;
	uint64_t oldestSeq = UINT64_MAX;
	for (const auto& kv : m_loadedSlots) {
		const LoadedSlot& ls = kv.second;
		if (ls.modelInfo && ls.modelInfo->m_numRefs == 0 && ls.lastUseSeq < oldestSeq) {
			oldestSeq = ls.lastUseSeq;
			victimSlot = kv.first;
		}
	}
	if (victimSlot < 0) {
		return false;
	}
	auto it = m_loadedSlots.find(victimSlot);
	if (it == m_loadedSlots.end()) {
		return false;
	}
	LoadedSlot victim = it->second;
	m_loadedSlots.erase(it);
	destroyLoadedSlot(manifest, victim);
	return true;
}

void ArtworkStore::evictIdleToBudget(ArtworkManifest& manifest, size_t targetTotalBytes)
{
	if (m_totalTxdBytes <= targetTotalBytes) {
		return;
	}

	// Pass 1: drop any orphaned TXDs (slotRefCount == 0 AND no live external texture refs), LRU first.
	{
		std::vector<std::pair<uint64_t, uint32_t>> orphanTxds;
		for (const auto& kv : m_loadedTxds) {
			if (kv.second.slotRefCount == 0 && !dictHasExternalTextureRefs(kv.second.dict)) {
				orphanTxds.emplace_back(kv.second.lastUseSeq, kv.first);
			}
		}
		std::sort(orphanTxds.begin(), orphanTxds.end());
		for (const auto& item : orphanTxds) {
			if (m_totalTxdBytes <= targetTotalBytes) {
				return;
			}
			releaseTxdRef(item.second);
		}
	}

	if (m_totalTxdBytes <= targetTotalBytes) {
		return;
	}

	// Pass 2: group slots by TXD and only evict whole TXD groups where EVERY referencing slot
	// is currently idle (m_numRefs == 0) and no external texture ref exists. Evicting idle slots
	// whose TXD is still pinned by an active object frees 0 bytes of VRAM and causes thrashing.
	struct TxdGroup {
		uint32_t txdCrc = 0;
		uint64_t newestUseSeq = 0;
		bool hasActiveSlot = false;
		std::vector<int32_t> idleSlots;
	};
	std::unordered_map<uint32_t, TxdGroup> groups;
	groups.reserve(m_loadedTxds.size());
	for (const auto& kv : m_loadedTxds) {
		TxdGroup& g = groups[kv.first];
		g.txdCrc = kv.first;
		g.newestUseSeq = kv.second.lastUseSeq;
	}
	for (const auto& kv : m_loadedSlots) {
		const LoadedSlot& ls = kv.second;
		if (ls.txdCrc == 0) {
			continue;
		}
		auto git = groups.find(ls.txdCrc);
		if (git == groups.end()) {
			continue;
		}
		TxdGroup& g = git->second;
		if (ls.lastUseSeq > g.newestUseSeq) {
			g.newestUseSeq = ls.lastUseSeq;
		}
		if (!ls.modelInfo || ls.modelInfo->m_numRefs > 0) {
			g.hasActiveSlot = true;
		} else {
			g.idleSlots.push_back(kv.first);
		}
	}

	std::vector<const TxdGroup*> reclaimable;
	reclaimable.reserve(groups.size());
	for (const auto& kv : groups) {
		const TxdGroup& g = kv.second;
		if (g.hasActiveSlot || g.idleSlots.empty()) {
			continue;
		}
		auto tit = m_loadedTxds.find(g.txdCrc);
		if (tit == m_loadedTxds.end() || dictHasExternalTextureRefs(tit->second.dict)) {
			continue;
		}
		reclaimable.push_back(&g);
	}

	std::sort(reclaimable.begin(), reclaimable.end(),
		[](const TxdGroup* a, const TxdGroup* b) { return a->newestUseSeq < b->newestUseSeq; });

	for (const TxdGroup* g : reclaimable) {
		if (m_totalTxdBytes <= targetTotalBytes) {
			break;
		}
		for (int32_t slot : g->idleSlots) {
			auto sit = m_loadedSlots.find(slot);
			if (sit == m_loadedSlots.end()) {
				continue;
			}
			LoadedSlot victim = sit->second;
			m_loadedSlots.erase(sit);
			destroyLoadedSlot(manifest, victim);
		}
	}
}

void ArtworkStore::evictIdleIfAboveWatermark(ArtworkManifest& manifest)
{
	if (m_totalTxdBytes <= kSoftEvictTxdBytes) {
		return;
	}
	const size_t beforeSlots = m_loadedSlots.size();
	const size_t beforeBytes = m_totalTxdBytes;
	evictIdleToBudget(manifest, kSoftEvictTxdBytes);
	if (m_loadedSlots.size() < beforeSlots || m_totalTxdBytes < beforeBytes) {
		spdlog::info("ArtworkStore: evicted idle resources ({} -> {} slots, {} -> {} MB TXD)",
			beforeSlots, m_loadedSlots.size(),
			beforeBytes / (1024u * 1024u), m_totalTxdBytes / (1024u * 1024u));
	}
}

RwTexDictionary* ArtworkStore::ensureTxdLoaded(ArtworkManifest& manifest, const CacheAccess& cache, uint32_t txdCrc)
{
	if (txdCrc == 0) {
		return nullptr;
	}
	if (auto it = m_loadedTxds.find(txdCrc); it != m_loadedTxds.end()) {
		it->second.lastUseSeq = ++m_useSeq;
		return it->second.dict;
	}
	if (m_failedTxds.count(txdCrc)) {
		return nullptr;
	}

	std::string txdPath;
	if (!cache.resolvePath || !cache.resolvePath(FileType::Txd, txdCrc, txdPath)) {
		return nullptr;
	}

	std::vector<uint8_t> bytes;
	if (!readFileBytes(txdPath, bytes, kMaxArtworkFileBytes)) {
		spdlog::warn("ArtworkStore: failed to read TXD {:08X} ({})", txdCrc, txdPath);
		const bool requeued = cache.invalidateCorrupt && cache.invalidateCorrupt(FileType::Txd, txdCrc);
		if (!requeued) {
			m_failedTxds.insert(txdCrc);
		}
		return nullptr;
	}

	// Fast header-only pass to learn the exact projected decoded RGBA footprint before deciding
	// whether any idle TXD groups actually need to be evicted.
	size_t projectedBytes = 0;
	TxdParseFailure preFail = TxdParseFailure::None;
	std::string preErr;
	if (!validateTxdBytes(bytes.data(), bytes.size(), &preErr, kMaxDecodedTxdBytes, &preFail, &projectedBytes)) {
		spdlog::warn("ArtworkStore: TXD {:08X} pre-check failed (failure={}): {}",
			txdCrc, static_cast<int>(preFail), preErr);
		if (preFail == TxdParseFailure::Malformed) {
			const bool requeued = cache.invalidateCorrupt && cache.invalidateCorrupt(FileType::Txd, txdCrc);
			if (!requeued) {
				m_failedTxds.insert(txdCrc);
			}
		} else {
			m_failedTxds.insert(txdCrc);
		}
		return nullptr;
	}

	const size_t neededBytes = std::min(projectedBytes, kMaxDecodedTxdBytes);
	if (m_totalTxdBytes + neededBytes > kMaxSessionTxdBytes) {
		const size_t target = (kMaxSessionTxdBytes > neededBytes)
			? (kMaxSessionTxdBytes - neededBytes) : 0u;
		evictIdleToBudget(manifest, target);
	}
	if (m_totalTxdBytes + neededBytes > kMaxSessionTxdBytes) {
		spdlog::warn("ArtworkStore: session TXD VRAM budget ({} MiB) full with active models (used={} MiB, need={} KB); refusing TXD {:08X}",
			kMaxSessionTxdBytes / (1024u * 1024u),
			m_totalTxdBytes / (1024u * 1024u),
			neededBytes / 1024u, txdCrc);
		return nullptr; // do not permanently poison m_failedTxds: may succeed once models become idle
	}

	RwTexDictionary* dict = eng::fn::RwTexDictionaryCreate<RwTexDictionary*>();
	if (!dict) {
		spdlog::error("ArtworkStore: RwTexDictionaryCreate failed for TXD {:08X}", txdCrc);
		m_failedTxds.insert(txdCrc);
		return nullptr;
	}

	const size_t remainingBudget = kMaxSessionTxdBytes - m_totalTxdBytes;
	const size_t txdCap = std::min(kMaxDecodedTxdBytes, remainingBudget);
	int uploaded = 0;
	int totalTextures = 0;
	size_t totalDecodedBytes = 0;
	TxdParseFailure failure = TxdParseFailure::None;
	std::string parseErr;

	// Stream one texture at a time: each RGBA buffer is uploaded and immediately released
	// instead of keeping all decoded textures resident in CPU RAM simultaneously.
	const bool ok = decodeTxdStreaming(bytes.data(), bytes.size(), 2048u, true, txdCap,
		[&](DecodedTexture&& dt) -> bool {
			++totalTextures;
			if (RwTexture* tex = createRwTextureFromDecoded(dt)) {
				eng::fn::RwTexDictionaryAddTexture(dict, tex);
				++uploaded;
			}
			return true;
		},
		&totalDecodedBytes, &failure, &parseErr);

	if (!ok) {
		safeDestroyTexDictionary(dict);
		spdlog::warn("ArtworkStore: TXD {:08X} parse failed (failure={}): {}",
			txdCrc, static_cast<int>(failure), parseErr);
		if (failure == TxdParseFailure::Malformed) {
			const bool requeued = cache.invalidateCorrupt && cache.invalidateCorrupt(FileType::Txd, txdCrc);
			if (!requeued) {
				m_failedTxds.insert(txdCrc);
			}
		}
		return nullptr;
	}

	LoadedTxd lt{};
	lt.dict = dict;
	lt.decodedBytes = totalDecodedBytes;
	lt.slotRefCount = 0;
	lt.lastUseSeq = ++m_useSeq;
	m_loadedTxds[txdCrc] = lt;
	m_totalTxdBytes += totalDecodedBytes;

	spdlog::info("ArtworkStore: loaded TXD {:08X} ({}/{} textures, {} KB, total={} MB)",
		txdCrc, uploaded, totalTextures,
		totalDecodedBytes / 1024u, m_totalTxdBytes / (1024u * 1024u));
	return dict;
}

int32_t ArtworkStore::ensureCustomSkinLoaded(ArtworkManifest& manifest, const CacheAccess& cache, int32_t serverSkinId)
{
	if (!isCustomSkinId(serverSkinId)) {
		return -1;
	}

	const int32_t existingSlot = manifest.resolveInternalSlot(serverSkinId);
	if (existingSlot >= 0) {
		auto it = m_loadedSlots.find(existingSlot);
		if (it != m_loadedSlots.end() && it->second.isPed && it->second.modelInfo && it->second.modelInfo->m_pRwObject) {
			it->second.lastUseSeq = ++m_useSeq;
			return existingSlot;
		}
	}

	if (m_failedModels.count(serverSkinId)) {
		return -1;
	}

	const CustomModelRecord* rec = manifest.findRecord(serverSkinId);
	if (!rec || rec->type != ModelType::Skin) {
		return -1;
	}

	std::string dffPath;
	if (!cache.resolvePath || !cache.resolvePath(FileType::Dff, rec->dffCrc, dffPath)) {
		return -1; // not yet on disk
	}
	RwTexDictionary* txd = nullptr;
	if (rec->txdCrc != 0) {
		std::string txdPath;
		if (!cache.resolvePath(FileType::Txd, rec->txdCrc, txdPath)) {
			return -1; // TXD still downloading
		}
		txd = ensureTxdLoaded(manifest, cache, rec->txdCrc);
		if (!txd) {
			spdlog::warn("ArtworkStore: custom skin {} TXD {:08X} unavailable; falling back to baseId {}",
				serverSkinId, rec->txdCrc, rec->baseId);
			if (m_failedTxds.count(rec->txdCrc)) {
				m_failedModels.insert(serverSkinId);
			}
			return -1;
		}
	}

	sa::CBaseModelInfo** modelInfoPtrs = Patches::modelInfoPtrs();
	if (!modelInfoPtrs) {
		return -1;
	}

	const uintptr_t pedVptr = eng::vmtSlot(eng::Sym::vtable_CPedModelInfo, 1);
	auto isPedBase = [&](int32_t id) -> sa::CPedModelInfo* {
		if (id < 0 || id >= kInternalSlotBegin) return nullptr;
		sa::CBaseModelInfo* mi = modelInfoPtrs[id];
		if (!mi || mi->vtable != pedVptr) return nullptr;
		return static_cast<sa::CPedModelInfo*>(mi);
	};
	sa::CPedModelInfo* basePed = isPedBase(rec->baseId);
	if (!basePed) basePed = isPedBase(29);
	if (!basePed) basePed = isPedBase(0);
	if (!basePed) {
		spdlog::error("ArtworkStore: no valid base CPedModelInfo for custom skin {}", serverSkinId);
		m_failedModels.insert(serverSkinId);
		return -1;
	}

	std::vector<uint8_t> dffBytes;
	if (!readFileBytes(dffPath, dffBytes, kMaxArtworkFileBytes)) {
		spdlog::warn("ArtworkStore: failed to read skin DFF {} ({})", serverSkinId, dffPath);
		const bool requeued = cache.invalidateCorrupt && cache.invalidateCorrupt(FileType::Dff, rec->dffCrc);
		if (!requeued) {
			m_failedModels.insert(serverSkinId);
		}
		return -1;
	}

	// Always strip any unexpected embedded collision chunks from skin DFFs.
	DffValidationResult validated = validateAndPrepareDff(dffBytes.data(), dffBytes.size(), true);
	if (!validated.ok) {
		spdlog::warn("ArtworkStore: invalid skin DFF {}: {}", serverSkinId, validated.error);
		const bool requeued = cache.invalidateCorrupt && cache.invalidateCorrupt(FileType::Dff, rec->dffCrc);
		if (!requeued) {
			m_failedModels.insert(serverSkinId);
		}
		return -1;
	}

	int32_t slot = manifest.allocateInternalSlot(serverSkinId);
	if (!isInternalCustomSlot(slot)) {
		if (evictOneIdleSlot(manifest)) {
			slot = manifest.allocateInternalSlot(serverSkinId);
		}
	}
	if (!isInternalCustomSlot(slot)) {
		spdlog::error("ArtworkStore: internal custom model slots exhausted (all active) for skin {}", serverSkinId);
		return -1;
	}

	sa::CPedModelInfo* model = createCustomPedModelInfo(basePed, pedVptr, serverSkinId);
	const std::vector<uint8_t>& dffPayload = validated.sanitizedDff.empty() ? dffBytes : validated.sanitizedDff;
	RpClump* clump = nullptr;
	{
		ScopedCustomTxd txdScope(txd);
		RwMemory rwMem{};
		rwMem.start = const_cast<RwUInt8*>(dffPayload.data());
		rwMem.length = static_cast<RwUInt32>(dffPayload.size());
		if (RwStream* stream = eng::fn::RwStreamOpen<RwStream*>(rwSTREAMMEMORY, rwSTREAMREAD, &rwMem)) {
			if (eng::fn::RwStreamFindChunk(stream, 0x10u /* rwID_CLUMP */, static_cast<uint32_t*>(nullptr), static_cast<uint32_t*>(nullptr))) {
				clump = eng::fn::RpClumpStreamRead<RpClump*>(stream);
			}
			eng::fn::RwStreamClose(stream, static_cast<void*>(nullptr));
		}
	}

	ClumpGeomCheck check{};
	if (clump) {
		using AtomicCB = RpAtomic* (*)(RpAtomic*, void*);
		eng::fn::RpClumpForAllAtomics(clump, static_cast<AtomicCB>(&checkClumpGeometryCB), &check);
	}
	if (!clump || check.atomicCount == 0 || check.totalVerts == 0) {
		spdlog::warn("ArtworkStore: skin {} clump invalid (clump={:p} atomics={} verts={})",
			serverSkinId, (void*)clump, check.atomicCount, check.totalVerts);
		if (clump) {
			eng::fn::RpClumpDestroy(clump);
		}
		delete model;
		manifest.releaseInternalSlot(serverSkinId);
		m_failedModels.insert(serverSkinId);
		return -1;
	}

	modelInfoPtrs[slot] = model;
	eng::fn::CPedModelInfo_SetClump(model, clump);
	if (!model->m_pRwObject) {
		spdlog::warn("ArtworkStore: CPedModelInfo::SetClump produced null RwObject for skin {}", serverSkinId);
		modelInfoPtrs[slot] = nullptr;
		delete model;
		manifest.releaseInternalSlot(serverSkinId);
		m_failedModels.insert(serverSkinId);
		return -1;
	}

	model->m_numRefs = 0;
	markStreamingSlotLoaded(slot);
	if (rec->txdCrc != 0) {
		if (auto txdIt = m_loadedTxds.find(rec->txdCrc); txdIt != m_loadedTxds.end()) {
			++txdIt->second.slotRefCount;
		}
	}

	LoadedSlot ls{};
	ls.internalSlot = slot;
	ls.serverModelId = serverSkinId;
	ls.txdCrc = rec->txdCrc;
	ls.isPed = true;
	ls.modelInfo = model;
	ls.ownedCol = nullptr;
	ls.lastUseSeq = ++m_useSeq;
	m_loadedSlots[slot] = ls;

	spdlog::info("ArtworkStore: loaded custom skin {} -> slot {} (base={}, atomics={}, verts={}, txd={:08X})",
		serverSkinId, slot, rec->baseId, check.atomicCount, check.totalVerts, rec->txdCrc);
	return slot;
}

int32_t ArtworkStore::ensureCustomObjectLoaded(ArtworkManifest& manifest, const CacheAccess& cache, int32_t serverModelId)
{
	if (!isCustomObjectId(serverModelId)) {
		return -1;
	}

	const int32_t existingSlot = manifest.resolveInternalSlot(serverModelId);
	if (existingSlot >= 0) {
		auto it = m_loadedSlots.find(existingSlot);
		if (it != m_loadedSlots.end() && !it->second.isPed && it->second.modelInfo && it->second.modelInfo->m_pRwObject) {
			it->second.lastUseSeq = ++m_useSeq;
			return existingSlot;
		}
	}

	if (m_failedModels.count(serverModelId)) {
		return -1;
	}

	const CustomModelRecord* rec = manifest.findRecord(serverModelId);
	if (!rec || rec->type != ModelType::Object) {
		return -1;
	}

	std::string dffPath;
	if (!cache.resolvePath || !cache.resolvePath(FileType::Dff, rec->dffCrc, dffPath)) {
		return -1; // still downloading
	}
	RwTexDictionary* txd = nullptr;
	if (rec->txdCrc != 0) {
		std::string txdPath;
		if (!cache.resolvePath(FileType::Txd, rec->txdCrc, txdPath)) {
			return -1; // TXD still downloading
		}
		txd = ensureTxdLoaded(manifest, cache, rec->txdCrc);
		if (!txd) {
			spdlog::warn("ArtworkStore: custom object {} TXD {:08X} unavailable; falling back to baseId {}",
				serverModelId, rec->txdCrc, rec->baseId);
			if (m_failedTxds.count(rec->txdCrc)) {
				m_failedModels.insert(serverModelId);
			}
			return -1;
		}
	}

	sa::CBaseModelInfo** modelInfoPtrs = Patches::modelInfoPtrs();
	if (!modelInfoPtrs) {
		return -1;
	}

	auto isObjectBase = [&](int32_t id) -> sa::CBaseModelInfo* {
		if (id < 0 || id >= kInternalSlotBegin) return nullptr;
		sa::CBaseModelInfo* mi = modelInfoPtrs[id];
		if (!mi || !isAtomicObjectVtable(mi->vtable)) return nullptr;
		return mi;
	};
	sa::CBaseModelInfo* baseInfo = isObjectBase(rec->baseId);
	if (!baseInfo) baseInfo = isObjectBase(18631);
	if (!baseInfo) baseInfo = isObjectBase(19374);
	if (!baseInfo) {
		for (int32_t probe = 321; probe < 19999; ++probe) {
			if (auto* candidate = isObjectBase(probe)) {
				baseInfo = candidate;
				break;
			}
		}
	}
	if (!baseInfo) {
		spdlog::error("ArtworkStore: no valid base CAtomicModelInfo for custom object {}", serverModelId);
		m_failedModels.insert(serverModelId);
		return -1;
	}

	std::vector<uint8_t> dffBytes;
	if (!readFileBytes(dffPath, dffBytes, kMaxArtworkFileBytes)) {
		spdlog::warn("ArtworkStore: failed to read object DFF {} ({})", serverModelId, dffPath);
		const bool requeued = cache.invalidateCorrupt && cache.invalidateCorrupt(FileType::Dff, rec->dffCrc);
		if (!requeued) {
			m_failedModels.insert(serverModelId);
		}
		return -1;
	}

	DffValidationResult validated = validateAndPrepareDff(dffBytes.data(), dffBytes.size(), true);
	if (!validated.ok) {
		spdlog::warn("ArtworkStore: invalid object DFF {}: {}", serverModelId, validated.error);
		const bool requeued = cache.invalidateCorrupt && cache.invalidateCorrupt(FileType::Dff, rec->dffCrc);
		if (!requeued) {
			m_failedModels.insert(serverModelId);
		}
		return -1;
	}

	if (validated.hasEmbeddedColChunk && validated.embeddedCol.empty()) {
		// Malformed embedded COL has already been stripped from validated.sanitizedDff; proceed
		// with loading the custom visual mesh and keep baseInfo->m_pColModel as collision fallback.
		spdlog::warn("ArtworkStore: custom object {} (dff={:08X}) has invalid embedded COL ({}); stripping COL and using baseId {} collision",
			serverModelId, rec->dffCrc, validated.colError, rec->baseId);
	}

	int32_t slot = manifest.allocateInternalSlot(serverModelId);
	if (!isInternalCustomSlot(slot)) {
		if (evictOneIdleSlot(manifest)) {
			slot = manifest.allocateInternalSlot(serverModelId);
		}
	}
	if (!isInternalCustomSlot(slot)) {
		spdlog::error("ArtworkStore: internal custom model slots exhausted (all active) for object {}", serverModelId);
		return -1;
	}

	sa::CAtomicModelInfo* model = createCustomAtomicModelInfo(baseInfo, serverModelId);
	modelInfoPtrs[slot] = model;

	const std::vector<uint8_t>& dffPayload = validated.sanitizedDff.empty() ? dffBytes : validated.sanitizedDff;
	bool loadedOk = false;
	{
		ScopedCustomTxd txdScope(txd);
		RwMemory rwMem{};
		rwMem.start = const_cast<RwUInt8*>(dffPayload.data());
		rwMem.length = static_cast<RwUInt32>(dffPayload.size());
		if (RwStream* stream = eng::fn::RwStreamOpen<RwStream*>(rwSTREAMMEMORY, rwSTREAMREAD, &rwMem)) {
			loadedOk = eng::fn::CFileLoader_LoadAtomicFile_Stream(stream, static_cast<uint32_t>(slot));
			eng::fn::RwStreamClose(stream, static_cast<void*>(nullptr));
		}
	}

	if (!loadedOk || !model->m_pRwObject) {
		spdlog::warn("ArtworkStore: LoadAtomicFile failed for custom object {} (slot={}, dff={:08X}, txd={:08X}, dffB={}, sanB={}, ok={}, rwObj={:p}, frames={}, geoms={}, atomics={}, uvAnim={})",
			serverModelId, slot, rec->dffCrc, rec->txdCrc, dffBytes.size(), dffPayload.size(),
			loadedOk ? 1 : 0, (void*)model->m_pRwObject,
			validated.frameCount, validated.geometryCount, validated.atomicCount,
			validated.hasUvAnimDict ? 1 : 0);
		if (model->m_pRwObject) {
			eng::fn::CAtomicModelInfo_DeleteRwObject(model);
		}
		modelInfoPtrs[slot] = nullptr;
		delete model;
		manifest.releaseInternalSlot(serverModelId);
		m_failedModels.insert(serverModelId);
		return -1;
	}

	sa::CColModel* ownedCol = nullptr;
	if (!validated.embeddedCol.empty()) {
		char colName[32];
		std::snprintf(colName, sizeof(colName), "dl_col_%d", serverModelId);
		ownedCol = loadEmbeddedColModel(validated.embeddedCol, colName);
		if (!ownedCol) {
			spdlog::warn("ArtworkStore: embedded COL load failed for custom object {}; failing cleanly", serverModelId);
			eng::fn::CAtomicModelInfo_DeleteRwObject(model);
			modelInfoPtrs[slot] = nullptr;
			delete model;
			manifest.releaseInternalSlot(serverModelId);
			m_failedModels.insert(serverModelId);
			return -1;
		}
		eng::fn::CBaseModelInfo_SetColModel(model, ownedCol, true);
	}

	model->m_numRefs = 0;
	markStreamingSlotLoaded(slot);
	if (rec->txdCrc != 0) {
		if (auto txdIt = m_loadedTxds.find(rec->txdCrc); txdIt != m_loadedTxds.end()) {
			++txdIt->second.slotRefCount;
		}
	}

	LoadedSlot ls{};
	ls.internalSlot = slot;
	ls.serverModelId = serverModelId;
	ls.txdCrc = rec->txdCrc;
	ls.isPed = false;
	ls.modelInfo = model;
	ls.ownedCol = ownedCol;
	ls.lastUseSeq = ++m_useSeq;
	m_loadedSlots[slot] = ls;

	spdlog::debug("ArtworkStore: loaded custom object {} -> slot {} (base={}, col={}B, txd={:08X})",
		serverModelId, slot, rec->baseId, validated.embeddedCol.size(), rec->txdCrc);
	return slot;
}

RwTexture* ArtworkStore::findTextureInCustomModel(ArtworkManifest& manifest, const CacheAccess& cache,
	int32_t serverModelId, const char* texName)
{
	if (!texName || !*texName) {
		return nullptr;
	}
	const CustomModelRecord* rec = manifest.findRecord(serverModelId);
	if (!rec) {
		return nullptr;
	}
	RwTexDictionary* dict = ensureTxdLoaded(manifest, cache, rec->txdCrc);
	if (!dict) {
		return nullptr;
	}
	RwTexture* tex = findTextureInDictCaseInsensitive(dict, texName);
	if (tex) {
		// Caller (CObject::SetMaterial / CTextDraw::LoadTexture) owns a ref and will call RwTextureDestroy.
		tex->refCount++;
	}
	return tex;
}

void ArtworkStore::unloadAll(ArtworkManifest& manifest)
{
	if (m_loadedSlots.empty() && m_loadedTxds.empty() && m_failedModels.empty() && m_failedTxds.empty()) {
		manifest.releaseAllSlots();
		return;
	}

	spdlog::info("ArtworkStore: unloading {} models and {} TXDs ({} MB)",
		m_loadedSlots.size(), m_loadedTxds.size(), m_totalTxdBytes / (1024u * 1024u));

	std::unordered_map<int32_t, LoadedSlot> retainedSlots;
	std::unordered_set<uint32_t> retainedTxdCrcs;
	for (auto& kv : m_loadedSlots) {
		LoadedSlot& ls = kv.second;
		if (ls.modelInfo && ls.modelInfo->m_numRefs > 0) {
			spdlog::error("ArtworkStore: invariant violation — slot {} (model {}) still has m_numRefs={}; retaining to prevent UAF",
				ls.internalSlot, ls.serverModelId, ls.modelInfo->m_numRefs);
			retainedSlots.emplace(kv.first, ls);
			if (ls.txdCrc != 0) {
				retainedTxdCrcs.insert(ls.txdCrc);
			}
			continue;
		}
		destroyLoadedSlot(manifest, ls);
	}
	m_loadedSlots = std::move(retainedSlots);
	m_failedModels.clear();

	for (auto it = m_loadedTxds.begin(); it != m_loadedTxds.end(); ) {
		if (retainedTxdCrcs.count(it->first)) {
			++it;
			continue;
		}
		if (it->second.dict) {
			safeDestroyTexDictionary(it->second.dict);
		}
		it = m_loadedTxds.erase(it);
	}
	m_failedTxds.clear();
	m_totalTxdBytes = 0;
	for (const auto& kv : m_loadedTxds) {
		m_totalTxdBytes += kv.second.decodedBytes;
	}
	m_useSeq = 0;

	if (m_loadedSlots.empty()) {
		manifest.releaseAllSlots();
	}
}

} // namespace artwork
