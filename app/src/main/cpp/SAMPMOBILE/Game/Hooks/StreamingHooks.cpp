#include "HookDomains.h"
#include "../Hooks.h"
#include "../Game.h"
#include "../Patches.h"
#include "../ModLoader.h"
#include "../CleoEngine.h"
#include "../Util.h"
#include "../Artwork/ArtworkCore.h"
#include "../Artwork/ArtworkManager.h"
#include "../Engine/PatchRegistry.h"
#include "../Engine/Symbols.h"
#include "../../Addr.h"
#include "../../Client.h"
#include "../../Settings.h"
#include <Memory.h>
#include <SA.h>
#include <spdlog/spdlog.h>
#include <dirent.h>
#include <strings.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static bool resolve_case_insensitive(const char* base, const char* relIn, char* relOut, size_t outSize)
{
	relOut[0] = 0;
	char cur[0x600];
	snprintf(cur, sizeof(cur), "%s", base);

	char tmp[0x1ff];
	snprintf(tmp, sizeof(tmp), "%s", relIn);
	char* save = nullptr;
	for (char* comp = strtok_r(tmp, "/\\", &save); comp; comp = strtok_r(nullptr, "/\\", &save))
	{
		DIR* d = opendir(cur);
		if (!d) return false;
		struct dirent* e;
		char found[0x1ff] = {0};
		while ((e = readdir(d)))
		{
			if (!strcasecmp(e->d_name, comp))
			{
				snprintf(found, sizeof(found), "%s", e->d_name);
				break;
			}
		}
		closedir(d);
		if (!found[0]) return false;
		size_t curLen = strlen(relOut);
		snprintf(relOut + curLen, outSize - curLen, "%s%s", curLen ? "/" : "", found);
		snprintf(cur, sizeof(cur), "%s/%s", base, relOut);
	}
	return relOut[0] != 0;
}

DECL_HOOK(void, DisplayScreen)
{
	// Loading screen is rendered on the Java layer; suppress the native GTA screen.
}

DECL_HOOK(int, OS_FileOpen, int a1, uintptr_t handle, char *name, int a2)
{
	char path[0x1ff] = {0};
	char modPath[0x1ff] = {0};
	const char *origName = name;
	bool isProtectedOnlineScript = false;

	if (name && !strncmp(name, "data\\script\\mainV1.scm", 22))
	{
		spdlog::info("Loading mainV1.scm..");
		snprintf(path, sizeof(path), "SAMP\\main.scm");
		name = path;
		isProtectedOnlineScript = !Client::offlineMode();
		goto check_mod;
	}

	if (name && !strncmp(name, "DATA\\SCRIPT\\SCRIPTV1.IMG", 24))
	{
		spdlog::info("Loading scriptV1.img..");
		snprintf(path, sizeof(path), "SAMP\\script.img");
		name = path;
		isProtectedOnlineScript = !Client::offlineMode();
		goto check_mod;
	}

	if (name && !strncmp(name, "DATA\\GTA.DAT", 12))
	{
		spdlog::info("Loading gta.dat..");
		snprintf(path, sizeof(path), "SAMP\\gta.dat");
		name = path;
		goto check_mod;
	}

	if (name && !strncmp(name, "DATA\\PEDS.IDE", 13))
	{
		spdlog::info("Loading peds.ide..");
		snprintf(path, sizeof(path), "SAMP\\peds.ide");
		name = path;
		goto check_mod;
	}

	if (name && !strncmp(name, "DATA\\TIMECYC.DAT", 16))
	{
		spdlog::info("Loading timecyc.dat..");
		snprintf(path, sizeof(path), "SAMP\\timecyc.dat");
		name = path;
		goto check_mod;
	}

	if (name && !strncmp(name, "data\\paths\\tracks2.dat", 22))
	{
		spdlog::info("Loading tracks2.dat...");
		snprintf(path, sizeof(path), "SAMP\\tracks2.dat");
		name = path;
		goto check_mod;
	}

	if (name && !strncmp(name, "data\\paths\\tracks4.dat", 22))
	{
		spdlog::info("Loading tracks4.dat...");
		snprintf(path, sizeof(path), "SAMP\\tracks4.dat");
		name = path;
		goto check_mod;
	}

check_mod:
	if (!isProtectedOnlineScript && ModLoader::isEnabled() && name)
	{
		// Check both the redirected name (e.g. SAMP\timecyc.dat) and original name (e.g. DATA\TIMECYC.DAT)
		if (ModLoader::resolve(name, modPath, sizeof(modPath)) ||
			(origName && origName != name && ModLoader::resolve(origName, modPath, sizeof(modPath))))
		{
			spdlog::info("ModLoader override: {} -> {}", origName ? origName : name, modPath);
			name = modPath;
		}
	}

	int result = OS_FileOpen(a1, handle, name, a2);
	if (result != 0 && name)
	{
		char relIn[0x1ff] = {0};
		snprintf(relIn, sizeof(relIn), "%s", name);
		size_t inLen = strlen(relIn);
		while (inLen > 0 && (relIn[inLen - 1] == ' ' || relIn[inLen - 1] == '\t'))
			relIn[--inLen] = 0;
		char fixed[0x1ff] = {0};
		if (resolve_case_insensitive(Client::gameDir(), relIn, fixed, sizeof(fixed)))
		{
			spdlog::info("OS_FileOpen retry (case-insensitive): {}", fixed);
			result = OS_FileOpen(a1, handle, fixed, a2);
		}
	}
	if (result != 0)
		spdlog::error("OS_FileOpen failed ({}): {}", result, name ? name : "<null>");
	return result;
}

DECL_HOOK(sa::CPedModelInfo *, CModelInfo_AddPedModel, int id)
{
	if (id < 0 || id >= artwork::kRelocatedModelTableSize) return nullptr;
	auto model = new sa::CPedModelInfo{};
	model->vtable = eng::vmtSlot(eng::Sym::vtable_CPedModelInfo, 1); // CPedModelInfo::~CPedModelInfo

	typedef sa::CStore<sa::CPedModelInfo, 278> PedModelStore;
	auto ms_pedModelStore = g_saSym->Abs<PedModelStore *>(addr::ms_pedModelStore);
	ms_pedModelStore->AddItem();

	// CClumpModelInfo::Init
	model->Init();

	auto ms_modelInfoPtrs = Patches::modelInfoPtrs();
	if (ms_modelInfoPtrs) {
		ms_modelInfoPtrs[id] = model;
	}
	return model;
}

DECL_HOOK(sa::CAtomicModelInfo *, CModelInfo_AddAtomicModel, int id)
{
	if (id < 0 || id >= artwork::kRelocatedModelTableSize) return nullptr;
	auto model = new sa::CAtomicModelInfo{};
	model->vtable = eng::vmtSlot(eng::Sym::vtable_CAtomicModelInfo, 1); // CAtomicModelInfo::~CAtomicModelInfo

	typedef sa::CStore<sa::CAtomicModelInfo, 14000> AtomicModelStore;
	auto ms_atomicModelStore = g_saSym->Abs<AtomicModelStore *>(addr::ms_atomicModelStore);
	ms_atomicModelStore->AddItem();

	// CAtomicModelInfo::Init
	model->Init();

	// CModelInfo::ms_modelInfoPtrs
	auto ms_modelInfoPtrs = Patches::modelInfoPtrs();
	if (ms_modelInfoPtrs) {
		ms_modelInfoPtrs[id] = model;
	}
	return model;
}

DECL_HOOK(void, CStreaming_RequestModel_Art, int id, int flags)
{
	if (artwork::isInternalCustomSlot(id)) {
		return;
	}
	CStreaming_RequestModel_Art(id, flags);
}

DECL_HOOK(void, CStreaming_RemoveModel_Art, int id)
{
	if (artwork::isInternalCustomSlot(id)) {
		return;
	}
	CStreaming_RemoveModel_Art(id);
}

DECL_HOOK(void, CStreaming_SetModelIsDeletable_Art, int id)
{
	if (artwork::isInternalCustomSlot(id)) {
		return;
	}
	CStreaming_SetModelIsDeletable_Art(id);
}

DECL_HOOK(void, CStreaming_SetMissionDoesntRequireModel_Art, int id)
{
	if (artwork::isInternalCustomSlot(id)) {
		return;
	}
	CStreaming_SetMissionDoesntRequireModel_Art(id);
}

DECL_HOOK(RwTexture *, RwTextureRead_Art, const char *name, const char *mask)
{
	if (name && *name) {
		if (RwTexture *tex = artwork::findTextureInActiveCustomTxd(name)) {
			tex->refCount++;
			return tex;
		}
	}
	return RwTextureRead_Art(name, mask);
}

static const char *texdb_format_ext(sa::TextureDatabaseFormat fmt)
{
	switch (fmt)
	{
	case sa::DF_UNC: return "unc";
	case sa::DF_DXT: return "dxt";
	case sa::DF_360: return "360";
	case sa::DF_PS3: return "ps3";
	case sa::DF_PVR: return "pvr";
	case sa::DF_ETC: return "etc";
	default: return nullptr;
	}
}

static bool texdb_format_exists_in_modloader(const char *dbName, sa::TextureDatabaseFormat fmt)
{
	const char *ext = texdb_format_ext(fmt);
	if (!ext || !dbName || !*dbName || !ModLoader::isEnabled()) return false;
	char rel[0xff] = {0};
	char fixed[0x1ff] = {0};
	snprintf(rel, sizeof(rel), "texdb/%s/%s.%s.toc", dbName, dbName, ext);
	return ModLoader::resolve(rel, fixed, sizeof(fixed));
}

static bool texdb_format_exists_in_base(const char *dbName, sa::TextureDatabaseFormat fmt)
{
	const char *ext = texdb_format_ext(fmt);
	if (!ext || !dbName || !*dbName) return false;
	char rel[0xff] = {0};
	char fixed[0x1ff] = {0};
	snprintf(rel, sizeof(rel), "texdb/%s/%s.%s.toc", dbName, dbName, ext);
	return resolve_case_insensitive(Client::gameDir(), rel, fixed, sizeof(fixed));
}

DECL_HOOK(sa::TextureDatabaseRuntime *, TextureDatabaseRuntime_Load, const char *withName, bool fullyLoad, sa::TextureDatabaseFormat forcedFormat)
{
	sa::TextureDatabaseFormat selected = forcedFormat;
	if (withName && *withName)
	{
		static const sa::TextureDatabaseFormat kPriority[] = {
			sa::DF_ETC, sa::DF_PVR, sa::DF_DXT, sa::DF_UNC
		};

		// Prefer any format supplied by an active ModLoader pack (e.g. .unc/.dxt HUD packs)
		// before falling back to official base files/, so modded texdb packs are never shadowed.
		bool matchedMod = false;
		if (ModLoader::isEnabled())
		{
			if (selected != sa::DF_Default && texdb_format_exists_in_modloader(withName, selected))
			{
				matchedMod = true;
			}
			else
			{
				for (auto candidate : kPriority)
				{
					if (texdb_format_exists_in_modloader(withName, candidate))
					{
						selected = candidate;
						matchedMod = true;
						break;
					}
				}
			}
		}

		if (!matchedMod && (selected == sa::DF_Default || !texdb_format_exists_in_base(withName, selected)))
		{
			for (auto candidate : kPriority)
			{
				if (texdb_format_exists_in_base(withName, candidate))
				{
					selected = candidate;
					break;
				}
			}
		}
		spdlog::info("TextureDatabaseRuntime::Load({}, fully={}, req={}, sel={}, mod={})",
			withName, fullyLoad, (int)forcedFormat, (int)selected, matchedMod);
	}
	return TextureDatabaseRuntime_Load(withName, fullyLoad, selected);
}

static RwTexture *findTextureInDatabase(const char *dbName, const char *texture_name)
{
	auto database_handle = sa::TextureDatabaseRuntime::GetDatabase(dbName);
	if (!database_handle)
	{
		return nullptr;
	}

	auto registered = sa::TextureDatabaseRuntime::registered();
	bool alreadyRegistered = false;
	if (registered.dataPtr && registered.numEntries > 0)
	{
		for (int index = 0; index < registered.numEntries; ++index)
		{
			if (registered.dataPtr[index] == database_handle)
			{
				alreadyRegistered = true;
				break;
			}
		}
	}

	if (alreadyRegistered)
	{
		return nullptr;
	}

	sa::TextureDatabaseRuntime::Register(database_handle);
	RwTexture *texture = sa::TextureDatabaseRuntime::GetTexture(texture_name);
	sa::TextureDatabaseRuntime::Unregister(database_handle);
	return texture;
}

DECL_HOOK(RwTexture *, CTxdStore_TxdStoreFindCB, const char *texture_name)
{
	if (!texture_name || !*texture_name)
	{
		return nullptr;
	}

	if (ModLoader::isEnabled())
	{
		for (const auto &extraDb : ModLoader::extraTexDbs())
		{
			if (RwTexture *tex = findTextureInDatabase(extraDb.c_str(), texture_name))
			{
				return tex;
			}
		}
	}

	static const char *kBaseTexDbs[] = {"gta_int", "gta3", "samp", "menu", "mobile", "samp_extra", "cutscene"};
	for (const char *db : kBaseTexDbs)
	{
		if (RwTexture *tex = findTextureInDatabase(db, texture_name))
		{
			return tex;
		}
	}

	RwTexDictionary *current = eng::fn::RwTexDictionaryGetCurrent<RwTexDictionary *>();
	while (current)
	{
		RwTexture *texture = eng::fn::RwTexDictionaryFindNamedTexture<RwTexture *>( current, texture_name);
		if (texture)
		{
			return texture;
		}

		current = sa::CTxdStore::GetTxdParent(current);
	}

	return nullptr;
}

// Guard against AML plugins or game code calling TextureDatabaseRuntime::Register(nullptr)
// when an optional texdb fails to load; a null slot in registered[] crashes GetTexture() at +0x110.
DECL_HOOK(void, TextureDatabaseRuntime_Register, sa::TextureDatabaseRuntime *db)
{
	if (!db)
	{
		return;
	}
	TextureDatabaseRuntime_Register(db);
}

DECL_HOOK(int, CGame_InitialiseRenderWare)
{
	spdlog::info("Initializing samp texture database...");

	int result = CGame_InitialiseRenderWare();
	sa::TextureDatabaseRuntime::Load("samp", false, sa::DF_ETC);
	sa::TextureDatabaseRuntime::Load("samp_extra", false, sa::DF_ETC);
	if (ModLoader::isEnabled())
	{
		for (const auto &db : ModLoader::extraTexDbs())
		{
			spdlog::info("ModLoader: loading custom texture database '{}'", db);
			sa::TextureDatabaseRuntime::Load(db.c_str(), false, sa::DF_Default);
		}
	}
	Client::initializeUI();
	return result;
}

struct CStreamingFile
{
	unsigned char m_name[40];
	bool m_bRegister;
	uint8_t m_padding[3];
	int32_t m_lsn;
};
VALIDATE_SIZE(CStreamingFile, 0x30);

CStreamingFile *CStreaming_ms_files = nullptr;
DECL_HOOK(void, CStreaming_InitImageList)
{
	spdlog::info("Initializing image list...");

	CStreaming_ms_files = eng::as<CStreamingFile *>(eng::Sym::CStreaming_ms_files);

	if (CStreaming_ms_files)
	{
		for (int i = 0; i < 8; i++)
		{
			CStreaming_ms_files[i].m_name[0] = '\0';
			CStreaming_ms_files[i].m_lsn = 0;
		}

		// CStreaming::LoadCdDirectory iterates ms_files[0..7] in order and SKIPS
		// any modelId already registered (FIRST-WINS). Mounting ModLoader's virtual
		// models.img at slot 0 lets loose .dff/.ifp/.col/.ipl files override base
		// gta3.img non-destructively (uses 1 of 2 spare slots: 5 + 2 lazy = 7 <= 8).
		if (ModLoader::isEnabled() && ModLoader::hasVirtualModelsImg())
		{
			spdlog::info("ModLoader: registering virtual IMG '{}'", ModLoader::virtualModelsImgRelPath());
			eng::fn::CStreaming_AddImageToList( ModLoader::virtualModelsImgRelPath(), true);
		}

		eng::fn::CStreaming_AddImageToList( "TEXDB\\SAMPCOL.IMG", true);
		eng::fn::CStreaming_AddImageToList( "TEXDB\\GTA3.IMG", true);
		eng::fn::CStreaming_AddImageToList( "TEXDB\\GTA_INT.IMG", true);
		eng::fn::CStreaming_AddImageToList( "TEXDB\\SAMP.IMG", true);
		// PLAYER.IMG is registered lazily by the clothes system and CUTSCENE.IMG by
		// gta.dat's IMG line; registering them here too overflows the 8-slot ms_files
		// table and hangs CdStream during level load.
	}
	else
	{
		spdlog::error("Failed to get symbol for CStreaming_ms_files!");
	}
}

// WarDrum's CModelInfoAccelerator caches a sequential model-ID array in models/MINFO.BIN.
// Whenever ModLoader mounts virtual models.img or swaps gta3.img, the IMG entry sequence shifts
// by +N and stale MINFO.BIN causes GetModelInfoFast() to miss every subsequent .dff!
// Resolving directly via CModelInfo::GetModelInfo() avoids desync AND the 20550-entry cap (~3 ms).
DECL_HOOK(void, CModelInfoAccelerator_GetEntry, void *self, void **outInfo, int32_t *outId, char *name)
{
	using FnGetModelInfo = void *(*)(const char *, int32_t *);
	static FnGetModelInfo s_fnGetModelInfo = eng::as<FnGetModelInfo>(eng::Sym::CModelInfo_GetModelInfo);
	if (s_fnGetModelInfo && outInfo)
	{
		*outInfo = s_fnGetModelInfo(name, outId);
		return;
	}
	CModelInfoAccelerator_GetEntry(self, outInfo, outId, name);
}

DECL_HOOK(void, CStreaming_Init2)
{
	CStreaming_Init2();

	// CStreaming::ms_memoryAvailable (GetSymbol returns 0 on failure)
	if (auto* memAvailable = eng::as<uint32_t *>(eng::Sym::CStreaming_ms_memoryAvailable))
	{
		*memAvailable *= 3;
	}
}

DECL_HOOK(void, CPools_Initialise)
{
	spdlog::info("GTA pools initializing..");
	Patches::initBetterPoolsSize();
}

DECL_HOOK(uint16_t*, CText_Get, void* _this, const char* key)
{
	if (uint16_t* custom = CleoEngine::getCustomGxt(key))
	{
		return custom;
	}
	if (!_this || !key)
	{
		return nullptr;
	}
	return CText_Get(_this, key);
}

void InstallStreamingHooks()
{
	REG_HOOK(Boot, OS_FileOpen, OS_FileOpen, "ModLoader & custom path file override");
	REG_HOOK(Boot, CStreaming_InitImageList, CStreaming_InitImageList, "Register virtual modloader/models.img");
	REG_HOOK(Boot, CModelInfoAccelerator_GetEntry, CModelInfoAccelerator_GetEntry, "Bypass stale MINFO.BIN for virtual models");
	REG_HOOK(Boot, TextureDatabaseRuntime_Register, TextureDatabaseRuntime_Register, "Null-safe texture DB register");
	REG_HOOK(Boot, CStreaming_Init2, CStreaming_Init2, "Expand streaming memory budget");
	REG_HOOK(Boot, CGame_InitialiseRenderWare, CGame_InitialiseRenderWare, "Load extra texdbs & custom fonts");
	REG_HOOK(Boot, CModelInfo_AddPedModel, CModelInfo_AddPedModel, "Ped model store override");
	REG_HOOK(Boot, CModelInfo_AddAtomicModel, CModelInfo_AddAtomicModel, "Atomic model store override");
	REG_HOOK(Boot, CPools_Initialise, CPools_Initialise, "Expanded entity pools");
	REG_HOOK(Boot, TextureDatabaseRuntime_Load, TextureDatabaseRuntime_Load, "ModLoader texdb load intercept");
	REG_HOOK(Boot, CTxdStore_TxdStoreFindCB, CTxdStore_TxdStoreFindCB, "Custom TXD lookup fallback");
	REG_HOOK(Boot, RwTextureRead, RwTextureRead_Art, "0.3.DL custom TXD priority texture lookup");
	REG_HOOK(Boot, CStreaming_RequestModel, CStreaming_RequestModel_Art, "0.3.DL custom slot streaming guard (Request)");
	REG_HOOK(Boot, CStreaming_RemoveModel, CStreaming_RemoveModel_Art, "0.3.DL custom slot streaming guard (Remove)");
	REG_HOOK(Boot, CStreaming_SetModelIsDeletable, CStreaming_SetModelIsDeletable_Art, "0.3.DL custom slot streaming guard (Deletable)");
	REG_HOOK(Boot, CStreaming_SetMissionDoesntRequireModel, CStreaming_SetMissionDoesntRequireModel_Art, "0.3.DL custom slot streaming guard (MissionRelease)");
	REG_HOOK(Boot, CLoadingScreen_DisplayPCScreen, DisplayScreen, "Custom loading splash screen");
	if (CleoEngine::isEnabled())
	{
		REG_HOOK(Boot, CText_Get, CText_Get, "CLEO custom GXT text lookup");
	}
}
