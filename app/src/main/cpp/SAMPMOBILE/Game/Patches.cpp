#include "Patches.h"
#include "Game.h"
#include "PlayerPed.h"
#include "Artwork/ArtworkCore.h"
#include "Engine/PatchRegistry.h"
#include "Engine/Symbols.h"
#include "../Addr.h"
#include "../Client.h"
#include "../Settings.h"
#include "VehicleColoursTable.h"
#include <Memory.h>
#include <SA.h>
#include <StreamingInfo.h>
#include <spdlog/spdlog.h>
#include <sys/stat.h>

sa::CPlayerInfo* WORLD_PLAYERS = nullptr;
static sa::CBaseModelInfo** s_modelInfoPtrs = nullptr;
static sa::CStreamingInfo* s_streamingInfoTable = nullptr;

bool Patches::hasFullAudioData()
{
	static int s_cached = -1;
	if (s_cached != -1) {
		return s_cached == 1;
	}
	const std::string dir = Client::gameDir();
	if (dir.empty()) {
		s_cached = 0;
		return false;
	}
	// Check both lowercase (Android) and uppercase (PC-style) audio bank paths.
	// Stripped 1.04GB data packs omit all .osw banks; leaving the audio engine
	// enabled without them crashes in CAEMP3TrackLoader::GetDataStream on spawn.
	const char* probes[] = {
		"audio/sfx/GENRL.osw",
		"audio/SFX/GENRL.osw",
		"audio/sfx/genrl.osw",
		"audio/sfx/FEET.osw",
		"audio/SFX/FEET.osw",
	};
	struct stat st{};
	for (const char* rel : probes) {
		const std::string full = (dir.back() == '/') ? (dir + rel) : (dir + "/" + rel);
		if (stat(full.c_str(), &st) == 0 && st.st_size > 0) {
			s_cached = 1;
			return true;
		}
	}
	s_cached = 0;
	return false;
}

void Patches::preApply()
{
	spdlog::info("Applying FPS patches...");

	// fps limit from Settings (0 = unlimited -> 250 FPS cap in engine)
	Settings::applyFpsLimitPatch();

	// show fps
	//*(bool*) (g_saSym->GetSymbol<uintptr_t>("FrontEndMenuManager") + (VER_x32 ? 0xB5 : 0xC1)) = true;

	// Set max stats and unlock all cities (STAT_CITIES_PASSED = 181 -> 2.0f)
	// Note: 181 is an integer stat (StatTypesInt[181 - 120]), NOT StatTypesFloat;
	// call CStats::SetStatValue before patching it out on the next line.
	sa::CCheat::WeaponSkillsCheat();
	sa::CCheat::VehicleSkillsCheat();
	sa::CCheat::StaminaCheat();
	eng::fn::CStats_SetStatValue(181, 2.0f);
	patches::set("CStats::SetStatValue", true);
}

void Patches::allocPlayers()
{
	spdlog::info("Allocating world players...");

	// relocate CWorld::Players[] using true ABI sizeof(sa::CPlayerInfo)
	// (404B on arm32, 472B on arm64)
	const size_t totalSize = sizeof(sa::CPlayerInfo) * PLAYER_PED_SLOTS;
	WORLD_PLAYERS = static_cast<sa::CPlayerInfo*>(calloc(PLAYER_PED_SLOTS, sizeof(sa::CPlayerInfo)));
	if (!WORLD_PLAYERS) {
		spdlog::error("Failed to allocate WORLD_PLAYERS ({} bytes)", totalSize);
		return;
	}

	Memory::protectAddr(g_saSym->Abs(addr::_ZN6CWorld7PlayersE_ptr));
	*g_saSym->Abs<sa::CPlayerInfo**>(addr::_ZN6CWorld7PlayersE_ptr) = WORLD_PLAYERS;
	patches::registerData("CWorld::Players", "Relocate CWorld::Players[] for multiplayer slots",
		patches::Scope::Boot, g_saSym->Abs<uintptr_t>(addr::_ZN6CWorld7PlayersE_ptr));

	spdlog::info("CWorld::Players new address: 0x{:X}, old address: 0x{:X}, size: {} ({} x {})",
		(uintptr_t) WORLD_PLAYERS, (uintptr_t) eng::ptr(eng::Sym::CWorld_Players),
		totalSize, PLAYER_PED_SLOTS, sizeof(sa::CPlayerInfo));
}

void Patches::allocModelTables()
{
	constexpr size_t kTableSize = static_cast<size_t>(artwork::kRelocatedModelTableSize);

	s_modelInfoPtrs = static_cast<sa::CBaseModelInfo**>(calloc(kTableSize, sizeof(sa::CBaseModelInfo*)));
	if (s_modelInfoPtrs) {
		auto* gotSlot = g_saSym->Abs<sa::CBaseModelInfo***>(addr::GOT_CModelInfo_ms_modelInfoPtrs);
		Memory::protectAddr(gotSlot);
		*gotSlot = s_modelInfoPtrs;
		patches::registerData("CModelInfo::ms_modelInfoPtrs", "Relocate ms_modelInfoPtrs to 32768 slots for 0.3.DL artwork",
			patches::Scope::Boot, reinterpret_cast<uintptr_t>(gotSlot));
		spdlog::info("Relocated CModelInfo::ms_modelInfoPtrs -> {:p} ({} entries)", (void*)s_modelInfoPtrs, kTableSize);
	} else {
		spdlog::error("Failed to allocate relocated ms_modelInfoPtrs ({} entries)", kTableSize);
	}

	s_streamingInfoTable = static_cast<sa::CStreamingInfo*>(calloc(kTableSize, sizeof(sa::CStreamingInfo)));
	if (s_streamingInfoTable) {
		for (int i = artwork::kInternalSlotBegin; i < artwork::kInternalSlotEnd; ++i) {
			s_streamingInfoTable[i].m_nNextIndex = -1;
			s_streamingInfoTable[i].m_nPrevIndex = -1;
			s_streamingInfoTable[i].m_nNextModelOnCd = -1;
			s_streamingInfoTable[i].m_nStatus = sa::LOADSTATE_NOT_LOADED;
		}
		auto* gotSlot = g_saSym->Abs<sa::CStreamingInfo**>(addr::GOT_CStreaming_ms_aInfoForModel);
		Memory::protectAddr(gotSlot);
		*gotSlot = s_streamingInfoTable;
		patches::registerData("CStreaming::ms_aInfoForModel", "Relocate ms_aInfoForModel to 32768 slots for 0.3.DL artwork",
			patches::Scope::Boot, reinterpret_cast<uintptr_t>(gotSlot));
		spdlog::info("Relocated CStreaming::ms_aInfoForModel -> {:p} ({} entries)", (void*)s_streamingInfoTable, kTableSize);
	} else {
		spdlog::error("Failed to allocate relocated ms_aInfoForModel ({} entries)", kTableSize);
	}
}

sa::CBaseModelInfo** Patches::modelInfoPtrs()
{
	if (s_modelInfoPtrs) {
		return s_modelInfoPtrs;
	}
	if (g_saSym) {
		if (auto* gotSlot = g_saSym->Abs<sa::CBaseModelInfo***>(addr::GOT_CModelInfo_ms_modelInfoPtrs)) {
			if (*gotSlot) {
				return *gotSlot;
			}
		}
	}
	return eng::as<sa::CBaseModelInfo**>(eng::Sym::CModelInfo_ms_modelInfoPtrs);
}

sa::CStreamingInfo* Patches::streamingInfoTable()
{
	if (s_streamingInfoTable) {
		return s_streamingInfoTable;
	}
	if (g_saSym) {
		if (auto* gotSlot = g_saSym->Abs<sa::CStreamingInfo**>(addr::GOT_CStreaming_ms_aInfoForModel)) {
			return *gotSlot;
		}
	}
	return nullptr;
}
void Patches::initBetterPoolsSize()
{
	spdlog::info("Initializing better pools size...");

	typedef sa::CPool<void> CPool;
	auto setPool = [](eng::Sym sym, int count, int elemSize) {
		if (auto** p = eng::as<CPool**>(sym)) {
			*p = new CPool(count, elemSize);
		}
	};

#if VER_x32
	setPool(eng::Sym::CPools_ms_pPtrNodeSingleLinkPool, 100000, 8);
	setPool(eng::Sym::CPools_ms_pPtrNodeDoubleLinkPool, 150000, 12);
	setPool(eng::Sym::CPools_ms_pEntryInfoNodePool,     150000, 20);
	setPool(eng::Sym::CPools_ms_pPedPool,               240,   1996);
	setPool(eng::Sym::CPools_ms_pVehiclePool,           2000,  2604);
	setPool(eng::Sym::CPools_ms_pBuildingPool,          20000, 60);
	setPool(eng::Sym::CPools_ms_pObjectPool,            3000,  420);
	setPool(eng::Sym::CPools_ms_pDummyPool,             40000, 60);
	setPool(eng::Sym::CPools_ms_pColModelPool,          50000, 48);
	setPool(eng::Sym::CPools_ms_pTaskPool,              5000,  128);
	setPool(eng::Sym::CPools_ms_pEventPool,             1000,  68);
	setPool(eng::Sym::CPools_ms_pPointRoutePool,        200,   100);
	setPool(eng::Sym::CPools_ms_pPatrolRoutePool,       200,   420);
	setPool(eng::Sym::CPools_ms_pNodeRoutePool,         200,   36);
	setPool(eng::Sym::CPools_ms_pTaskAllocatorPool,     3000,  32);
	setPool(eng::Sym::CPools_ms_pPedIntelligencePool,   240,   664);
	setPool(eng::Sym::CPools_ms_pPedAttractorPool,      200,   236);
#else
	setPool(eng::Sym::CPools_ms_pPtrNodeSingleLinkPool, 100000, 16);   // 1200000/75000 = 16
	setPool(eng::Sym::CPools_ms_pPtrNodeDoubleLinkPool, 150000, 24);   // expanded for large 0.3.DL custom maps
	setPool(eng::Sym::CPools_ms_pEntryInfoNodePool,     150000, 40);   // expanded for large 0.3.DL custom maps
	setPool(eng::Sym::CPools_ms_pPedPool,               240,    2504); // 350560/140 = 2504
	setPool(eng::Sym::CPools_ms_pVehiclePool,           2000,   3176); // 349360/110 = 3176
	setPool(eng::Sym::CPools_ms_pBuildingPool,          20000,  96);   // 1344000/14000 = 96
	setPool(eng::Sym::CPools_ms_pObjectPool,            3000,   544);  // 190400/350 = 544
	setPool(eng::Sym::CPools_ms_pDummyPool,             40000,  96);   // 336000/3500 = 96
	setPool(eng::Sym::CPools_ms_pColModelPool,          50000,  56);   // 568400/10150 = 56
	setPool(eng::Sym::CPools_ms_pTaskPool,              5000,   168);  // 84000/500 = 168
	setPool(eng::Sym::CPools_ms_pEventPool,             1000,   88);   // 17600/200 = 88
	setPool(eng::Sym::CPools_ms_pPointRoutePool,        200,    100);  // 6400/64 = 100
	setPool(eng::Sym::CPools_ms_pPatrolRoutePool,       200,    420);  // 13440/32 = 420
	setPool(eng::Sym::CPools_ms_pNodeRoutePool,         200,    36);   // 2304/64 = 36
	setPool(eng::Sym::CPools_ms_pTaskAllocatorPool,     3000,   48);   // 768/16 = 48
	setPool(eng::Sym::CPools_ms_pPedIntelligencePool,   240,    1088); // 152320/140 = 1088
	setPool(eng::Sym::CPools_ms_pPedAttractorPool,      200,    236);  // 15104/64 = 236
#endif
}

void Patches::apply()
{
	spdlog::info("Applying patches...");

	allocPlayers();
	allocModelTables();

	// ---- Boot-scope patches (active in both online & offline modes) ----
	patches::registerNop("StartGameScreen.SkipMenuSwitchOff", "NOP Menu_SwitchOffToGame in OnNewGameCheck",
		patches::Scope::Boot, addr::Menu_SwitchOffToGame_StartGameScreen_OnNewGameCheck, 2, false);

	// Reveal all zones
	if (uintptr_t zonesVisited = eng::addr(eng::Sym::CTheZones_ZonesVisited)) {
		uint8_t visitedBuf[100];
		memset(visitedBuf, 1, sizeof(visitedBuf));
		patches::registerBytes("CTheZones::ZonesVisited", "Reveal all map zones",
			patches::Scope::Boot, zonesVisited, visitedBuf, sizeof(visitedBuf), false);
	}
	if (int* zonesRevealed = eng::as<int*>(eng::Sym::CTheZones_ZonesRevealed)) {
		*zonesRevealed = 100;
		patches::registerData("CTheZones::ZonesRevealed", "Mark 100 map zones revealed",
			patches::Scope::Boot, eng::addr(eng::Sym::CTheZones_ZonesRevealed));
	}

	// ---- Session-scope patches (registered unconditionally; toggled on demand) ----
	patches::registerRet("CStats::SetStatValue", "Lock max stats after preApply", patches::Scope::Session, eng::Sym::CStats_SetStatValue, true);
	patches::registerRet("CClock::Update", "Freeze/unfreeze GTA world clock", patches::Scope::Session, eng::Sym::CClock_Update, true);
	patches::registerNop("ToggleCJWalk", "Toggle CJ walk anim group (shared with ProcessAnimGroups)", patches::Scope::Session, addr::ToggleCJWalk, 2, true);
	patches::registerRet("DisableAutoAim.FindTarget", "Disable auto-aim lock-on search", patches::Scope::Session, eng::Sym::CPlayerPed_FindWeaponLockOnTarget, true);
	patches::registerRet("DisableAutoAim.FindNextTarget", "Disable auto-aim next-target search", patches::Scope::Session, eng::Sym::CPlayerPed_FindNextWeaponLockOnTarget, true);
	patches::registerRet("DisableAutoAim.SetTarget", "Disable auto-aim set target", patches::Scope::Session, eng::Sym::CPed_SetWeaponLockOnTarget, true);
	patches::registerRet("DisableAutoAim.TapToTarget", "Disable tap-to-target auto-aim & aim reset", patches::Scope::Session, eng::Sym::CPlayerPed_HandleTapToTarget, true);
	patches::registerRet("CClothes::RebuildPlayer", "Skip CJ clothes rebuild on model change", patches::Scope::Session, eng::Sym::CClothes_RebuildPlayer, true);

	patches::apply(patches::Scope::Boot);

	if (Client::offlineMode()) {
		return;
	}

	// ---- Online-scope patches ----
	patches::registerBytes("DefaultPCSaveFileName", "Rename save file prefix to GTASAMP",
		patches::Scope::Online, eng::addr(eng::Sym::DefaultPCSaveFileName), "GTASAMP", 8, false);

	patches::registerRet("CWeather::Update", "Disable local weather cycling", patches::Scope::Online, eng::Sym::CWeather_Update);

	patches::registerRet("CHud::SetHelpMessage", "Suppress singleplayer help popups", patches::Scope::Online, eng::Sym::CHud_SetHelpMessage);
	patches::registerRet("CHud::SetHelpMessageStatUpdate", "Suppress stat update popups", patches::Scope::Online, eng::Sym::CHud_SetHelpMessageStatUpdate);
	patches::registerRet("CHud::DrawBustedWastedMessage", "Suppress Busted/Wasted banner", patches::Scope::Online, eng::Sym::CHud_DrawBustedWastedMessage);

	patches::registerRet("CCheat::ProcessCheatMenu", "Disable cheat menu", patches::Scope::Online, eng::Sym::CCheat_ProcessCheatMenu);
	patches::registerRet("CCheat::ProcessCheats", "Disable cheat processing", patches::Scope::Online, eng::Sym::CCheat_ProcessCheats);
	patches::registerRet("CCheat::AddToCheatString", "Disable cheat string input", patches::Scope::Online, eng::Sym::CCheat_AddToCheatString);
	patches::registerRet("CCheat::DoCheats", "Disable cheats", patches::Scope::Online, eng::Sym::CCheat_DoCheats);

	// fix alpha bug in _rwOpenGLRasterCreate
	patches::registerBytes("_rwOpenGLRasterCreate.alpha", "Fix OpenGL raster alpha flag",
		patches::Scope::Online, g_saSym->Abs<uintptr_t>(addr::_rwOpenGLRasterCreate_patch),
		!VER_x32 ? "\x22\x00\x80\x52" : "\x01\x22", !VER_x32 ? 4 : 2);

	// fix texture animation
	if (auto* pRunUVAnim = eng::as<bool*>(eng::Sym::RunUVAnim)) {
		*pRunUVAnim = true;
		patches::registerData("RunUVAnim", "Enable RenderWare UV texture animations",
			patches::Scope::Online, eng::addr(eng::Sym::RunUVAnim));
	}

	// fix black markers (CRadar color translation; preserve r0/x0 = &HudColour for CHudColours::GetIntColour)
#if VER_x32
	patches::registerBytes("CRadar.Draw3dMarkers.branch", "Radar 3D marker color branch", patches::Scope::Online, g_saSym->Abs<uintptr_t>(addr::CRadar_Draw3dMarkers_colorBranch), "\x31\x46\x2B\xE0", 4);
	patches::registerBytes("CRadar.DrawEntityBlip.branch", "Radar entity blip color branch", patches::Scope::Online, g_saSym->Abs<uintptr_t>(addr::CRadar_DrawEntityBlip_colorBranch), "\x36\xE0", 2);
	patches::registerBytes("CRadar.DrawEntityBlip.mov", "Radar entity blip color arg r1", patches::Scope::Online, g_saSym->Abs<uintptr_t>(addr::CRadar_DrawEntityBlip_colorMov_x32), "\x31\x46", 2);
	patches::registerBytes("CRadar.DrawCoordBlip.branch", "Radar coord blip color branch", patches::Scope::Online, g_saSym->Abs<uintptr_t>(addr::CRadar_DrawCoordBlip_colorBranch), "\x0F\xE0", 2);
	patches::registerBytes("CRadar.DrawCoordBlip.mov", "Radar coord blip color arg r1", patches::Scope::Online, g_saSym->Abs<uintptr_t>(addr::CRadar_DrawCoordBlip_colorMov_x32), "\x49\x46", 2);
#else
	patches::registerBytes("CRadar.Draw3dMarkers.branch", "Radar 3D marker color arg w1 & branch", patches::Scope::Online, g_saSym->Abs<uintptr_t>(addr::CRadar_Draw3dMarkers_colorBranch), "\xE1\x03\x14\x2A\x1D\x00\x00\x14", 8);
	patches::registerBytes("CRadar.DrawEntityBlip.branch", "Radar entity blip color arg w1 & branch", patches::Scope::Online, g_saSym->Abs<uintptr_t>(addr::CRadar_DrawEntityBlip_colorBranch), "\xE1\x03\x16\x2A\x20\x00\x00\x14", 8);
	patches::registerBytes("CRadar.DrawCoordBlip.branch", "Radar coord blip color arg w1 & branch", patches::Scope::Online, g_saSym->Abs<uintptr_t>(addr::CRadar_DrawCoordBlip_colorBranch), "\xE1\x03\x16\x2A\xCA\xFF\xFF\x17", 8);
#endif

	// CRealTimeShadowManager crash fixes
	patches::registerNop("CRealTimeShadowManager.Return1", "RealTimeShadow crash fix 1", patches::Scope::Online, addr::CRealTimeShadowManager_ReturnRealTimeShadow_patch1, 2);
	patches::registerNop("CRealTimeShadowManager.Return2", "RealTimeShadow crash fix 2", patches::Scope::Online, addr::CRealTimeShadowManager_ReturnRealTimeShadow_patch2, 2);
	patches::registerRet("CRealTimeShadowManager::Update", "RealTimeShadow update crash fix", patches::Scope::Online, eng::Sym::CRealTimeShadowManager_Update);

	// CPlayerPed::CPlayerPed remove player count restriction (orig: 7)
#if VER_x32
	patches::registerBytes("CPlayerPed.PlayerCountLimit", "Lift 7-player cap in CPlayerPed ctor", patches::Scope::Online, g_saSym->Abs<uintptr_t>(addr::CPlayerPed_CPlayerPed_patch), "\xE0", 1);
#else
	patches::registerBytes("CPlayerPed.PlayerCountLimit", "Lift 7-player cap in CPlayerPed ctor", patches::Scope::Online, g_saSym->Abs<uintptr_t>(addr::CPlayerPed_CPlayerPed_patch), "\x34\x00\x80\x52", 4);
#endif

	patches::registerRet("CCarEnterExit::SetPedInCarDirect", "Avoid direct teleport into vehicles", patches::Scope::Online, eng::Sym::CCarEnterExit_SetPedInCarDirect);
	patches::registerRet0("CPopulation::AddPed", "Disable ambient pedestrian spawns (return nullptr)", patches::Scope::Online, eng::Sym::CPopulation_AddPed);
	patches::registerRet0("CGameLogic::IsCoopGameGoingOn", "Disable PS2 2-player shared-screen co-op mode", patches::Scope::Online, eng::Sym::CGameLogic_IsCoopGameGoingOn);
	patches::registerRet0("CGameLogic::IsPlayerUse2PlayerControls", "Disable 2P co-op camera-relative aim/melee controls", patches::Scope::Online, eng::Sym::CGameLogic_IsPlayerUse2PlayerControls);

	// Vehicle audio processing: only disable when .osw audio data is absent (T06)
	const bool hasAudio = hasFullAudioData();
	if (!hasAudio) {
		patches::registerNop("CAEVehicleAudio.CAutomobile", "Disable automobile audio service (no .osw)", patches::Scope::Online, addr::CAEVehicleAudioEntity_Service_patch1, 2);
		patches::registerNop("CAEVehicleAudio.CBike", "Disable bike audio service (no .osw)", patches::Scope::Online, addr::CAEVehicleAudioEntity_Service_patch2, 2);
		patches::registerNop("CAEVehicleAudio.CBoat", "Disable boat audio service (no .osw)", patches::Scope::Online, addr::CAEVehicleAudioEntity_Service_patch3, 2);
		patches::registerNop("CAEVehicleAudio.CTrain", "Disable train audio service (no .osw)", patches::Scope::Online, addr::CAEVehicleAudioEntity_Service_patch4, 2);
	}
	patches::registerNop("CAudioEngine.StartLoadingTune", "Disable loading tune audio", patches::Scope::Online, addr::CAudioEngine_StartLoadingTune_patch, 2);

	// P2-19: arm32 CBike_ProcessAI_patch (0x564D30) is a 2-byte Thumb-16 bhi -> word_count=1; arm64 is 4-byte -> word_count=2
	patches::registerNop("CBike::ProcessAI", "Allow remote bike ProcessControlInputs",
		patches::Scope::Online, addr::CBike_ProcessAI_patch, VER_x32 ? 1 : 2);

	patches::registerNop("CPlayerInfo.KillPlayer.BigMsg", "Suppress wasted big message", patches::Scope::Online, addr::CMessages_AddBigMessage_patch, 2);
	patches::registerRet("CAEGlobalWeaponAudio.AmbientGunFire", "Disable ambient gunfire audio", patches::Scope::Online, eng::Sym::CAEGlobalWeaponAudioEntity_ServiceAmbientGunFire);
	patches::registerRet("CPlane::DoPlaneGenerationAndRemoval", "Disable ambient plane spawns", patches::Scope::Online, eng::Sym::CPlane_DoPlaneGenerationAndRemoval);
	patches::registerRet("CFileLoader::LoadPickup", "Disable singleplayer map pickups", patches::Scope::Online, eng::Sym::CFileLoader_LoadPickup);
	patches::registerRet("CEntryExit::GenerateAmbientPeds", "Disable interior ambient peds", patches::Scope::Online, eng::Sym::CEntryExit_GenerateAmbientPeds);
	patches::registerRet("CPlayerInfo::AddHealth", "Disable hospital/ambulance auto-heal", patches::Scope::Online, eng::Sym::CPlayerInfo_AddHealth);
	patches::registerRet("CPlayerInfo::ArrestPlayer", "Disable singleplayer busted arrest flow", patches::Scope::Online, eng::Sym::CPlayerInfo_ArrestPlayer);
	patches::registerRet("CPlayerInfo::StreamParachuteWeapon", "Disable auto parachute streaming", patches::Scope::Online, eng::Sym::CPlayerInfo_StreamParachuteWeapon);
	patches::registerRet("CPlayerInfo::GivePlayerParachute", "Disable auto parachute grant", patches::Scope::Online, eng::Sym::CPlayerInfo_GivePlayerParachute);
	patches::registerNop("CVehicle.SetDriver.Rewards", "Disable emergency/taxi/caddy vehicle entry weapons, armour & money",
		patches::Scope::Online, addr::CVehicle_SetDriver_rewardBranch, VER_x32 ? 1 : 2);
	patches::registerBytes("CPlayerInfo.Process.AutoParachute", "Skip auto GiveWeapon(46) on aircraft bailout",
		patches::Scope::Online, g_saSym->Abs<uintptr_t>(addr::CPlayerInfo_Process_parachuteBranch),
		!VER_x32 ? "\x09\x00\x00\x14" : "\x09\xE0", !VER_x32 ? 4 : 2);
	patches::registerRet("CGameLogic::SetPlayerWantedLevelForForbiddenTerritories", "Disable forbidden-city 4-star wanted level",
		patches::Scope::Online, eng::Sym::CGameLogic_SetPlayerWantedLevelForForbiddenTerritories);
	patches::registerRet("CHeli::UpdateHelis", "Disable singleplayer police helicopters", patches::Scope::Online, eng::Sym::CHeli_UpdateHelis);
	patches::registerRet("CRoadBlocks::GenerateRoadBlocks", "Disable singleplayer police roadblocks", patches::Scope::Online, eng::Sym::CRoadBlocks_GenerateRoadBlocks);
	patches::registerRet("CWanted::Update", "Disable singleplayer wanted pursuit update", patches::Scope::Online, eng::Sym::CWanted_Update);
	patches::registerRet("CWanted::UpdateEachFrame", "Disable singleplayer wanted per-frame update", patches::Scope::Online, eng::Sym::CWanted_UpdateEachFrame);

	// P2-18 & P3-9: Singleplayer leakage & idle loop suppression
	patches::registerRet("CPlayerInfo::WorkOutEnergyFromHunger", "Disable singleplayer hunger starvation HP drain", patches::Scope::Online, eng::Sym::CPlayerInfo_WorkOutEnergyFromHunger);
	patches::registerRet("CStats::IncrementStat", "Disable singleplayer stat drift (increment)", patches::Scope::Online, eng::Sym::CStats_IncrementStat);
	patches::registerRet("CStats::DecrementStat", "Disable singleplayer stat drift (decrement)", patches::Scope::Online, eng::Sym::CStats_DecrementStat);
	patches::registerRet("CTrain::DoTrainGenerationAndRemoval", "Disable singleplayer ambient train generation", patches::Scope::Online, eng::Sym::CTrain_DoTrainGenerationAndRemoval);
	patches::registerRet("InteriorManager_c::Update", "Disable singleplayer interior furniture manager", patches::Scope::Online, eng::Sym::InteriorManager_c_Update);
	patches::registerRet("CAEVehicleAudioEntity::TurnOnRadioForVehicle", "Disable auto-radio on vehicle enter", patches::Scope::Online, eng::Sym::CAEVehicleAudioEntity_TurnOnRadioForVehicle);
	patches::registerNop("CPlayerInfo.Process.PassengerMoney", "Disable singleplayer +$1/sec passenger money reward",
		patches::Scope::Online, addr::CPlayerInfo_Process_passengerMoneyStore, 2);
	patches::registerRet("CGangWars::Update", "Disable singleplayer gang wars loop", patches::Scope::Online, eng::Sym::CGangWars_Update);
	patches::registerRet("CDarkel::Update", "Disable singleplayer rampage/darkel loop", patches::Scope::Online, eng::Sym::CDarkel_Update);
	patches::registerRet("CSetPieces::Update", "Disable singleplayer set-pieces loop", patches::Scope::Online, eng::Sym::CSetPieces_Update);
	patches::registerRet("CGarages::Update", "Disable singleplayer garages loop", patches::Scope::Online, eng::Sym::CGarages_Update);
	patches::registerRet("CConversations::Update", "Disable singleplayer ped conversations loop", patches::Scope::Online, eng::Sym::CConversations_Update);
	patches::registerRet("CPedToPlayerConversations::Update", "Disable singleplayer ped-to-player conversations", patches::Scope::Online, eng::Sym::CPedToPlayerConversations_Update);
	patches::registerRet("CAutomobile::ProcessFlyingCarStuff", "Disable singleplayer flying car cheat loop", patches::Scope::Online, eng::Sym::CAutomobile_ProcessFlyingCarStuff);
	patches::registerRet("CCarCtrl::RemoveDistantCars", "Disable singleplayer distant car despawn", patches::Scope::Online, eng::Sym::CCarCtrl_RemoveDistantCars);
	patches::registerRet("CCarCtrl::RemoveCarsIfThePoolGetsFull", "Disable singleplayer full-pool car despawn", patches::Scope::Online, eng::Sym::CCarCtrl_RemoveCarsIfThePoolGetsFull);
	patches::registerRet("CRadar::DrawLegend", "Disable radar legend draw", patches::Scope::Online, eng::Sym::CRadar_DrawLegend);
	patches::registerRet("CRadar::AddBlipToLegendList", "Disable radar legend list", patches::Scope::Online, eng::Sym::CRadar_AddBlipToLegendList);
	patches::registerNop("CRadar.DrawCoordBlip.SpriteDraw", "Suppress vanilla coord blip sprite", patches::Scope::Online, addr::CRadar_DrawCoordBlip_patch, 2);
	patches::registerNop("CRadar.DrawEntityBlip.SpriteDraw", "Suppress vanilla entity blip sprite", patches::Scope::Online, addr::CRadar_DrawEntityBlip_patch, 2);

	// CVehicleModelInfo::ms_vehicleColourTable
	if (auto* pColourTable = eng::as<uintptr_t*>(eng::Sym::CVehicleModelInfo_ms_vehicleColourTable)) {
		*pColourTable = reinterpret_cast<uintptr_t>(VehicleColoursTableRGBA);
		patches::registerData("CVehicleModelInfo::ms_vehicleColourTable", "Install SA-MP 256-color vehicle palette",
			patches::Scope::Online, eng::addr(eng::Sym::CVehicleModelInfo_ms_vehicleColourTable));
	}

	patches::registerRet("CWanted::ReportCrimeNow", "Disable singleplayer police crime reports", patches::Scope::Online, eng::Sym::CWanted_ReportCrimeNow);
	patches::registerRet("SaveGameForPause", "Disable pause menu auto-save", patches::Scope::Online, eng::Sym::SaveGameForPause);
	patches::registerRet("C_PcSave::SaveSlot", "Disable PC save slot writes", patches::Scope::Online, eng::Sym::C_PcSave_SaveSlot);
	patches::registerNop("FindPlayerInfoForThisPlayerPed.patch", "Bypass singleplayer playerinfo lookup", patches::Scope::Online, addr::FindPlayerInfoForThisPlayerPed_patch, 6);
	patches::registerRet("CTaskSimplePlayerOnFoot::PlayIdleAnimations", "Disable singleplayer idle anims", patches::Scope::Online, eng::Sym::CTaskSimplePlayerOnFoot_PlayIdleAnimations);
	patches::registerRet("DoFade", "Disable singleplayer screen fades", patches::Scope::Online, eng::Sym::DoFade);
	patches::registerRet("CCarCtrl::GenerateRandomCars", "Disable ambient traffic cars", patches::Scope::Online, eng::Sym::CCarCtrl_GenerateRandomCars);
	patches::registerRet("CTheCarGenerators::Process", "Disable parked car generators", patches::Scope::Online, eng::Sym::CTheCarGenerators_Process);

	// Disable built-in GTA:SA audio servicing only when .osw audio files are absent (T06);
	// when full audio banks exist, let the engine play SFX, vehicle sounds, and PlayerPlaySound.
	if (!hasAudio) {
		spdlog::info("Patches: no .osw audio banks detected -> keeping GTA audio engine patched");
		patches::registerRet("CAERadioTrackManager::Service", "Disable GTA radio service (no .osw)", patches::Scope::Online, eng::Sym::CAERadioTrackManager_Service);
		patches::registerRet("CAudioEngine::Service", "Disable GTA audio engine service (no .osw)", patches::Scope::Online, eng::Sym::CAudioEngine_Service);
		patches::registerRet("CAEStreamThread::Service", "Disable GTA audio stream thread (no .osw)", patches::Scope::Online, eng::Sym::CAEStreamThread_Service);
		patches::registerRet("CAEFrontendAudioEntity::AddAudioEvent", "Disable GTA frontend audio events (no .osw)", patches::Scope::Online, eng::Sym::CAEFrontendAudioEntity_AddAudioEvent);
		patches::registerRet("CAudioEngine::ReportFrontendAudioEvent", "Disable GTA frontend audio report (no .osw)", patches::Scope::Online, eng::Sym::CAudioEngine_ReportFrontendAudioEvent);
	}
	else {
		spdlog::info("Patches: full .osw audio banks detected -> GTA audio engine enabled");
	}

	patches::apply(patches::Scope::Online);

	// Initial online state for Session patches (exact parity with pre-F04 startup)
	patches::set("CClock::Update", true);
	patches::set("ToggleCJWalk", true);
}
