#pragma once

#include <cstdint>
#include <type_traits>

// Centralized GTA:SA Android v2.10 (arm64-v8a / armeabi-v7a) raw file offsets.
// Every raw libGTASA.so address must live here (one canonical name per address).
class addr {
public:
	// ---- Hooks & branch sites ----
	static constexpr uintptr_t CModelInfo_ms_modelInfoPtrs = !VER_x32 ? 0xB666E0 : 0x91DCB8;
	static constexpr uintptr_t ms_atomicModelStore = !VER_x32 ? 0xA06668 : 0x820738;
	static constexpr uintptr_t ms_pedModelStore = !VER_x32 ? 0xB5CB80 : 0x915FC8;
	static constexpr uintptr_t CPed_ProcessControl = !VER_x32 ? 0x598730 : 0x4A2540;
	static constexpr uintptr_t CPed_UpdatePosition_BranchBL1 = !VER_x32 ? CPed_ProcessControl + 0x5EC : CPed_ProcessControl + 0x552;
	static constexpr uintptr_t CPlayerPed_ProcessControl_AttackRelease_BranchBL = !VER_x32 ? 0x5C2F1C : 0x4C5626;
	static constexpr uintptr_t CTaskComplexEnterCarAsDriver_Branch1 = !VER_x32 ? 0x4EF504 : 0x40AC80;
	static constexpr uintptr_t CTaskComplexLeaveCar_Branch1 = !VER_x32 ? 0x4EE3C0 : 0x409A8E;
	static constexpr uintptr_t CTaskComplexLeaveCar_Branch2 = !VER_x32 ? 0x4EEE00 : 0x40A864;
	static constexpr uintptr_t CWeapon_FireInstantHit_branch1 = !VER_x32 ? 0x700E64 : 0x5DBBBA;
	static constexpr uintptr_t CWeapon_FireInstantHit_branch2 = !VER_x32 ? 0x700EFC : 0x5DBC12;
	static constexpr uintptr_t CPad_GetAbortClimb_arm32 = 0x3FBBB8; // Thumb-only short hook in Pad.cpp

	// ---- Game globals & data tables ----
	static constexpr uintptr_t ClockFormat = !VER_x32 ? 0x73E1DA : 0x2BD618;
	static constexpr uintptr_t CTheScripts_bDisplayHud = !VER_x32 ? 0x9FF3A8 : 0x819D88;
	static constexpr uintptr_t CHud_bScriptDontDisplayRadar = !VER_x32 ? 0xC20DFC : 0x991FD8;
	static constexpr uintptr_t SetGravity = !VER_x32 ? 0x75044C : 0x3FE810;
	static constexpr uintptr_t CPickups_aPickUps = !VER_x32 ? 0x991AB8 : 0x7AFD70;
	static constexpr uintptr_t glAlphaFuncQCOM_slot = !VER_x32 ? 0x89A1B0 : 0x6BCBF8;

	// ---- GOT slots (redirection targets & AML-safe live reads) ----
	static constexpr uintptr_t _ZN6CWorld7PlayersE_ptr = !VER_x32 ? 0x84E7A8 : 0x6783C0;
	static constexpr uintptr_t GOT_CModelInfo_ms_modelInfoPtrs = !VER_x32 ? 0x850DB8 : 0x6796CC;
	static constexpr uintptr_t GOT_CStreaming_ms_aInfoForModel = !VER_x32 ? 0x84DBD0 : 0x677DD0;
	static constexpr uintptr_t GOT_CMatrixLinkList_gMatrixList = !VER_x32 ? 0x84CD98 : 0x6776AC;
	static constexpr uintptr_t GOT_CTouchInterface_m_pWidgets = !VER_x32 ? 0x850910 : 0x679474; // redirected by libSAUtils
	static constexpr uintptr_t GOT_MobileSettings_settings = !VER_x32 ? 0x851498 : 0x679A3C;    // redirected by libSAUtils
	static constexpr uintptr_t GOT_MultiTouch_points = !VER_x32 ? 0x851D38 : 0x679E90;
	static constexpr uintptr_t GOT_MultiTouch_pointers = !VER_x32 ? 0x8B5028 : 0x6D7178;

	// ---- arm64 diagnostic GOT slots (Hooks.cpp widget/touch diag) ----
	static constexpr uintptr_t GOT_TakePhoto_arm64 = 0x84EA70;
	static constexpr uintptr_t GOT_CurInputInst_arm64 = 0x851340;
	static constexpr uintptr_t GOT_MobileMenu_arm64 = 0x8519B0;
	static constexpr uintptr_t GOT_InputDeviceArray_arm64 = 0x84B1E8;
	static constexpr uintptr_t GOT_TouchInitOwner_arm64 = 0x84AB30;
	static constexpr uintptr_t GOT_TouchRelOwner_arm64 = 0x84F5B8;
	static constexpr uintptr_t GOT_TouchActive_arm64 = 0x8506D0;
	static constexpr uintptr_t GOT_TouchPos_arm64 = 0x84C868;
	static constexpr uintptr_t GOT_TouchDownPos_arm64 = 0x84F8C8;

	// ---- Code patches ----
	// Canonical address shared by CPlayerPed::ProcessAnimGroups crash patch (Patches.cpp)
	// and Game::ToggleCJWalk (Game.cpp); merged into one name per F03.
	static constexpr uintptr_t ToggleCJWalk = !VER_x32 ? 0x5C3970 : 0x4C5F6A;
	static constexpr uintptr_t Menu_SwitchOffToGame_StartGameScreen_OnNewGameCheck = !VER_x32 ? 0x365F10 : 0x2A72C8;
	static constexpr uintptr_t _rwOpenGLRasterCreate_patch = !VER_x32 ? 0x23FDE0 : 0x1AE8DE;
	static constexpr uintptr_t CRealTimeShadowManager_ReturnRealTimeShadow_patch1 = !VER_x32 ? 0x4E054C : 0x3FCD84;
	static constexpr uintptr_t CRealTimeShadowManager_ReturnRealTimeShadow_patch2 = !VER_x32 ? 0x4E0598 : 0x3FCDC4;
	static constexpr uintptr_t CPlayerPed_CPlayerPed_patch = !VER_x32 ? 0x5C0BC4 : 0x4C36E2;
	static constexpr uintptr_t CAEVehicleAudioEntity_Service_patch1 = !VER_x32 ? 0x674610 : 0x553E96;
	static constexpr uintptr_t CAEVehicleAudioEntity_Service_patch2 = !VER_x32 ? 0x682C1C : 0x561AC2;
	static constexpr uintptr_t CAEVehicleAudioEntity_Service_patch3 = !VER_x32 ? 0x68DD0C : 0x56BED4;
	static constexpr uintptr_t CAEVehicleAudioEntity_Service_patch4 = !VER_x32 ? 0x6A0A68 : 0x57D0C4;
	static constexpr uintptr_t CAudioEngine_StartLoadingTune_patch = !VER_x32 ? 0x70A3D0 : 0x5E4966;
	static constexpr uintptr_t RwCameraEndUpdate_patch = !VER_x32 ? 0x4D9254 : 0x3F6CDC;
	static constexpr uintptr_t CBike_ProcessAI_patch = !VER_x32 ? 0x6859BC : 0x564D30;
	static constexpr uintptr_t CVehicle_SetDriver_rewardBranch = !VER_x32 ? 0x6A82A0 : 0x58463A;
	static constexpr uintptr_t CPlayerInfo_Process_parachuteBranch = !VER_x32 ? 0x4EEE38 : 0x40A88E;
	static constexpr uintptr_t CPlayerInfo_Process_passengerMoneyStore = !VER_x32 ? 0x4EDAD0 : 0x4092B4;
	static constexpr uintptr_t CMessages_AddBigMessage_patch = !VER_x32 ? 0x4F07DC : 0x40BF26;
	static constexpr uintptr_t CRadar_DrawCoordBlip_patch = !VER_x32 ? 0x52522C : 0x43FE5A;
	static constexpr uintptr_t CRadar_DrawEntityBlip_patch = !VER_x32 ? 0x525E14 : 0x4409AE;
	static constexpr uintptr_t FindPlayerInfoForThisPlayerPed_patch = !VER_x32 ? 0x59BAD0 : 0x4A5398;

	// Radar color-translation patches (Patches.cpp: fix black markers without clobbering CHudColours* this)
	static constexpr uintptr_t CRadar_Draw3dMarkers_colorBranch   = !VER_x32 ? 0x52737C : 0x442120;
	static constexpr uintptr_t CRadar_DrawEntityBlip_colorBranch  = !VER_x32 ? 0x5258D8 : 0x4404C0;
	static constexpr uintptr_t CRadar_DrawEntityBlip_colorMov_x32 = 0x440534;
	static constexpr uintptr_t CRadar_DrawCoordBlip_colorBranch   = !VER_x32 ? 0x524F58 : 0x43FB5E;
	static constexpr uintptr_t CRadar_DrawCoordBlip_colorMov_x32  = 0x43FB82;

	// FPS limiter immediate patches (Settings.cpp)
	static constexpr uintptr_t FpsLimit_patch1_arm32 = 0x5E49E0;
	static constexpr uintptr_t FpsLimit_patch2_arm32 = 0x5E492E;
	static constexpr uintptr_t FpsLimit_patch1_arm64 = 0x70A38C; // mov w9, #fps
	static constexpr uintptr_t FpsLimit_patch2_arm64 = 0x70A43C; // mov w8, #fps
	static constexpr uintptr_t FpsLimit_patch3_arm64 = 0x70A458; // mov w9, #fps

	// MultiTouch pointer-count patches (Java/MultiTouch.cpp)
	static constexpr uintptr_t OS_PointerGetNumber_patch     = !VER_x32 ? 0x320844 : 0x26B03C;
	static constexpr uintptr_t OS_PointerGetType_patch       = !VER_x32 ? 0x320850 : 0x26B046;
	static constexpr uintptr_t LIB_PointerGetCoordinates_patch = !VER_x32 ? 0x326FEC : 0x2700AC;
	static constexpr uintptr_t LIB_PointerGetWheel_patch     = !VER_x32 ? 0x32706C : 0x270118;
	static constexpr uintptr_t LIB_PointerDoubleClicked_patch = !VER_x32 ? 0x3270AC : 0x270164;
	static constexpr uintptr_t LIB_PointerGetButton_patch    = !VER_x32 ? 0x327040 : 0x2700F2;

	// ---- Vtables ----
	static constexpr uintptr_t vptr$CPlaceable = !VER_x32 ? 0x830098 : 0x667D14;
	static constexpr uintptr_t vptr$CCivilianPed = !VER_x32 ? 0x831BA8 : 0x668A94;
	static constexpr uintptr_t vptr$CAutomobile = !VER_x32 ? 0x83BB50 : 0x66D678;
	static constexpr uintptr_t vptr$CBike = !VER_x32 ? 0x83BE40 : 0x66D7F0;
	static constexpr uintptr_t vptr$CBmx = !VER_x32 ? 0x83C070 : 0x66D908;
	static constexpr uintptr_t vptr$CBoat = !VER_x32 ? 0x83C2A0 : 0x66DA20;
	static constexpr uintptr_t vptr$CHeli = !VER_x32 ? 0x83C4C8 : 0x66DB34;
	static constexpr uintptr_t vptr$CMonsterTruck = !VER_x32 ? 0x83C718 : 0x66DC5C;
	static constexpr uintptr_t vptr$CPlane = !VER_x32 ? 0x83C968 : 0x66DD84;
	static constexpr uintptr_t vptr$CQuadBike = !VER_x32 ? 0x83CBB8 : 0x66DEAC;
	static constexpr uintptr_t vptr$CTrailer = !VER_x32 ? 0x83CE08 : 0x66DFD4;
	static constexpr uintptr_t vptr$CTrain = !VER_x32 ? 0x83D058 : 0x66E0FC;
	static constexpr uintptr_t vptr$CPedModelInfo = !VER_x32 ? 0x82F310 : 0x667658;
	static constexpr uintptr_t vptr$CTaskSimpleJetPack = !VER_x32 ? 0x83A6C0 : 0x66CC28;
	static constexpr uintptr_t vptr$CTaskSimpleHandsUp = !VER_x32 ? 0x82C0A0 : 0x6657F0;

	// ---- Start of vtables ----
	static constexpr uintptr_t start$CTaskAllocatorKillThreatsDriveby = !VER_x32 ? 0x83B9D8 : 0x66D5BC;
	static constexpr uintptr_t start$CTaskSimpleUninterruptable = !VER_x32 ? 0x82B7F8 : 0x6653DC;
};
