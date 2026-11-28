#include "Game.h"
#include "Aimstuff.h"
#include "Camera.h"
#include "Font.h"
#include "Hooks.h"
#include "Object.h"
#include "Patches.h"
#include "PlayerPed.h"
#include "RadarColors.h"
#include "RemoveBuilding.h"
#include "Scripting.h"
#include "Util.h"
#include "Vehicle.h"
#include "Artwork/ArtworkCore.h"
#include "Artwork/ArtworkManager.h"
#include "Engine/PatchRegistry.h"
#include "Engine/Symbols.h"
#include "../Addr.h"
#include "../Client.h"
#include "../Settings.h"
#include "../Java/Java.h"
#include "../UI/UISettings.h"
#include "../UI/ImGuiRenderer.h"
#include <encoding.h>
#include <SA.h>
#include <Memory.h>
#include <spdlog/spdlog.h>
#include <sstream>

void InitScripting();

bool bUsedPlayerSlots[PLAYER_PED_SLOTS];

char* szGameTextMessage = nullptr;

inline int FindFirstFreePlayerPedSlot()
{
	//spdlog::info("FindFirstFreePlayerPedSlot");

	uint8_t x = 2;
	while (x != PLAYER_PED_SLOTS) {
		if (!bUsedPlayerSlots[x]) return x;
		x++;
	}

	return 0;
}

Game::Game()
{
	//spdlog::info("Game::Game");

	m_gameCamera = nullptr;
	m_gamePlayer = nullptr;
	m_checkpointsEnabled = false;
	m_checkpointMarker = 0;
	m_raceCheckpointsEnabled = false;
	m_raceCheckpointMarker = 0;
	m_raceCheckpointHandle = 0;
	m_imguiGameTextExpire = 0;
	m_imguiGameTextSize = 0;

	m_clockEnabled = false;
	m_gameInput = true;

	memset(bUsedPlayerSlots, 0, sizeof(bUsedPlayerSlots));
	memset(m_preloadedVehicleModels, 0, sizeof(m_preloadedVehicleModels));
}

Game::~Game()
{
	//spdlog::info("Game::Game");
}

CCamera* Game::GetCamera()
{
	if (m_gameCamera == nullptr) {
		m_gameCamera = new CCamera();
	}
	return m_gameCamera;
}

CPlayerPed* Game::FindPlayerPed()
{
	if (m_gamePlayer == nullptr) {
		if (!GamePool_FindPlayerPed()) {
			return nullptr;
		}
		m_gamePlayer = new CPlayerPed();
	}
	return m_gamePlayer;
}

void Game::Initialize()
{
	//spdlog::info("Game::Initialize");

	szGameTextMessage = static_cast<char*>(calloc(512 + 1, sizeof(uint16_t)));
	GameResetRadarColors();

	InitScripting();

	if (!Client::offlineMode()) {
		EnableStuntBonus(false);
	}
}

void Game::StartGame()
{
	//spdlog::info("Game::StartGame");

	Hooks::allowInitialWorldBoot();
	// StartGameScreen::OnNewGameCheck -> Menu_NewGame -> SettingsBeforeStartingGame
	eng::fn::StartGameScreen_OnNewGameCheck();

	GameAimSyncInit();
}

bool Game::IsModelLoaded(int iModel)
{
	//spdlog::info("Game::IsModelLoaded: {}", iModel);

	if (artwork::isInternalCustomSlot(iModel)) {
		sa::CBaseModelInfo* mi = GetModelInfoByID(iModel);
		return mi && mi->m_pRwObject != nullptr;
	}
	if (iModel > 20000 || iModel < 0) {
		return true;
	}
	else {
		return ScriptCommand(&is_model_available, iModel);
	}

	// return sa::CStreaming::HasModelLoaded(iModel);
}

void Game::RequestModel(int iModel)
{
	//spdlog::info("Game::RequestModel: {}", iModel);
	if (artwork::isInternalCustomSlot(iModel)) {
		return;
	}
	ScriptCommand(&request_model, iModel);
	// sa::CStreaming::RequestModel(iModel, 2);
}

void Game::LoadRequestedModels()
{
	//spdlog::info("Game::LoadRequestedModels");
	ScriptCommand(&load_requested_models);
	// sa::CStreaming::LoadAllRequestedModels(false);
}

void Game::RemoveModel(int iModel, bool bFromStreaming)
{
	//spdlog::info("Game::RemoveModel: {}", iModel);

	if (iModel >= 0 && iModel < 20000) {
		if (bFromStreaming) {
			if (ScriptCommand(&is_model_available, iModel)) {
				// CStreaming::RemoveModel
				eng::fn::CStreaming_RemoveModel( iModel);
			}
		}
		else {
			if (ScriptCommand(&is_model_available, iModel)) {
				ScriptCommand(&release_model, iModel);
			}
		}
	}
}

void Game::ToggleThePassingOfTime(bool bOnOff)
{
	patches::set("CClock::Update", !bOnOff);
	m_clockEnabled = bOnOff;
}

void Game::EnableClock(bool bEnable)
{
	const char clockFmt[10] = {'%', '0', '2', 'd', ':', '%', '0', '2', 'd', 0};
	const char emptyFmt[10] = {};

	ToggleThePassingOfTime(bEnable);
	patches::updateBytes("HUD.ClockFormat", "HUD clock printf format string",
		g_saSym->Abs<uintptr_t>(addr::ClockFormat),
		bEnable ? clockFmt : emptyFmt, sizeof(clockFmt));
}

void Game::EnableZoneNames(bool bEnable)
{
	//spdlog::info("Game::EnableZoneNames: {}", bEnable);
	ScriptCommand(&enable_zone_names, bEnable);
}

void Game::DisableMarker(uint32_t dwMarker)
{
	//spdlog::info("Game::DisableMarker: {}", dwMarker);
	ScriptCommand(&disable_marker, dwMarker);
}

/**
 * @param byteType
 * @param fPosX
 * @param fPosY
 * @param fPosZ
 * @param dwColor
 * @param byteStyle
 * @return
 */
uint32_t Game::CreateRadarMarkerIcon(uint8_t byteType, float fPosX, float fPosY, float fPosZ, uint32_t dwColor, uint8_t byteStyle)
{
	//spdlog::info("Game::CreateRadarMarkerIcon: {}, {}, {}, {}, {}, {}", byteType, fPosX, fPosY, fPosZ, dwColor, byteStyle);

	uint32_t dwMarkerID = 0;

	if (byteStyle == 1) {
		ScriptCommand(&create_marker_icon, fPosX, fPosY, fPosZ, byteType, &dwMarkerID);
	}
	else if (byteStyle == 2) {
		ScriptCommand(&create_radar_marker_icon, fPosX, fPosY, fPosZ, byteType, &dwMarkerID);
	}
	else if (byteStyle == 3) {
		ScriptCommand(&create_icon_marker_sphere, fPosX, fPosY, fPosZ, byteType, &dwMarkerID);
	}
	else {
		ScriptCommand(&create_radar_marker_without_sphere, fPosX, fPosY, fPosZ, byteType, &dwMarkerID);
	}

	if (byteType == 0) {
		if (dwColor >= 1004) {
			ScriptCommand(&set_marker_color, dwMarkerID, dwColor);
			ScriptCommand(&show_on_radar, dwMarkerID, 3);
		}
		else {
			ScriptCommand(&set_marker_color, dwMarkerID, dwColor);
			ScriptCommand(&show_on_radar, dwMarkerID, 2);
		}
	}

	return dwMarkerID;
}

void Game::UpdateCheckpoints()
{
	//spdlog::info("Game::UpdateCheckpoints");

	if (m_checkpointsEnabled) {
		CPlayerPed* pPlayerPed = FindPlayerPed();
		if (pPlayerPed) {
			ScriptCommand(&is_actor_near_point_3d, pPlayerPed->m_gtaId, m_checkpointPos.x, m_checkpointPos.y, m_checkpointPos.z, m_checkpointExtent.x, m_checkpointExtent.y, m_checkpointExtent.z, 1);

			if (!m_checkpointMarker) {
				m_checkpointMarker = CreateRadarMarkerIcon(0, m_checkpointPos.x, m_checkpointPos.y, m_checkpointPos.z, 1005, 0);
			}
		}
	}
	else if (m_checkpointMarker) {
		DisableMarker(m_checkpointMarker);
		m_checkpointMarker = 0;
	}

	if (m_raceCheckpointsEnabled) {
		CPlayerPed* pPlayerPed = FindPlayerPed();
		if (pPlayerPed) {
			if (!m_raceCheckpointMarker) {
				m_raceCheckpointMarker = CreateRadarMarkerIcon(0, m_raceCheckpointPos.x, m_raceCheckpointPos.y, m_raceCheckpointPos.z, 1005, 0);
			}
		}
	}
	else if (m_raceCheckpointMarker || m_raceCheckpointHandle) {
		if (m_raceCheckpointMarker) {
			DisableMarker(m_raceCheckpointMarker);
			m_raceCheckpointMarker = 0;
		}
		DisableRaceCheckpoint();
	}
}

void Game::SetCheckpointInformation(sa::CVector* vecPos, sa::CVector* vecSize)
{
	//spdlog::info("Game::SetCheckpointInformation");

	m_checkpointPos.x = vecPos->x;
	m_checkpointPos.y = vecPos->y;
	m_checkpointPos.z = vecPos->z;

	m_checkpointExtent.x = vecSize->x;
	m_checkpointExtent.y = vecSize->y;
	m_checkpointExtent.z = vecSize->z;

	if (m_checkpointMarker) {
		DisableMarker(m_checkpointMarker);
		m_checkpointMarker = 0;
		m_checkpointMarker = CreateRadarMarkerIcon(0, m_checkpointPos.x, m_checkpointPos.y, m_checkpointPos.z, 1005, 0);
	}
}

void Game::SetRaceCheckpointInformation(uint8_t byteType, sa::CVector* vecPos, sa::CVector* vecNextPos, float fRadius)
{
	//spdlog::info("Game::SetRaceCheckpointInformation");

	m_raceCheckpointPos.x = vecPos->x;
	m_raceCheckpointPos.y = vecPos->y;
	m_raceCheckpointPos.z = vecPos->z;

	m_raceCheckpointNextPos.x = vecNextPos->x;
	m_raceCheckpointNextPos.y = vecNextPos->y;
	m_raceCheckpointNextPos.z = vecNextPos->z;

	m_raceType = byteType;
	m_raceCheckpointRadius = fRadius;

	if (m_raceCheckpointMarker) {
		DisableMarker(m_raceCheckpointMarker);
		m_raceCheckpointMarker = CreateRadarMarkerIcon(0, m_raceCheckpointPos.x, m_raceCheckpointPos.y, m_raceCheckpointPos.z, 1005, 0);
	}

	MakeRaceCheckpoint();
}

void Game::MakeRaceCheckpoint()
{
	//spdlog::info("Game::MakeRaceCheckpoint");

	DisableRaceCheckpoint();

	ScriptCommand(&create_racing_checkpoint, (int) m_raceType,
			m_raceCheckpointPos.x, m_raceCheckpointPos.y, m_raceCheckpointPos.z, m_raceCheckpointNextPos.x, m_raceCheckpointNextPos.y, m_raceCheckpointNextPos.z, m_raceCheckpointRadius, &m_raceCheckpointHandle);

	m_raceCheckpointsEnabled = true;
}

void Game::DisableRaceCheckpoint()
{
	//spdlog::info("Game::DisableRaceCheckpoint");

	if (m_raceCheckpointHandle) {
		ScriptCommand(&destroy_racing_checkpoint, m_raceCheckpointHandle);
		m_raceCheckpointHandle = 0;
	}

	m_raceCheckpointsEnabled = false;
}

/**
 * @return
 */
uint8_t Game::GetPedSlotsUsed()
{
	//spdlog::info("Game::GetPedSlotsUsed");

	uint8_t count = 0;
	for (int i = 2; i < PLAYER_PED_SLOTS; i++) {
		if (bUsedPlayerSlots[i]) {
			count++;
		}
	}

	return count;
}

void Game::DisplayGameText(const char* szStr, int iTime, int iSize)
{
	//spdlog::info("Game::DisplayGameText: {}, {}, {}", szStr, iTime, iSize);

	if (!szStr || iSize < 0 || iSize > 6) return;

	std::string utf8 = Encoding::cp2utf(szStr);

	if (StringHasNonAscii(utf8.c_str())) {
		// Clear any lingering engine ASCII big message before showing ImGui GameText.
		ScriptCommand(&text_clear_all);
		m_imguiGameText = GxtToImGuiColors(ReplaceKey(utf8), "{FFFFFF}");
		m_imguiGameTextExpire = GetTickCount() + iTime;
		m_imguiGameTextSize = iSize;
		return;
	}

	m_imguiGameText.clear();

	ScriptCommand(&text_clear_all);

	uint16_t m_szAscii[513];
	CFont::StringToAscii(utf8.c_str(), m_szAscii, sizeof(m_szAscii) / sizeof(m_szAscii[0]));
	CFont::AsciiToGxtCharU8(m_szAscii, reinterpret_cast<uint16_t*>(szGameTextMessage), 513);

	// CMessages::AddBigMesssage
	eng::fn::CMessages_AddBigMessage( szGameTextMessage, iTime, iSize);
}

void Game::DrawImGuiGameText(ImGuiRenderer* renderer)
{
	if (m_imguiGameText.empty()) return;
	if (static_cast<int32_t>(GetTickCount() - m_imguiGameTextExpire) >= 0) {
		m_imguiGameText.clear();
		return;
	}

	// Map SA-MP GameText styles 0..6 to distinct screen vertical anchors and scales.
	float yRatio = 0.50f;
	float scale = 1.25f;
	bool alignRight = false;
	switch (m_imguiGameTextSize) {
		case 0: yRatio = 0.45f; scale = 1.60f; break; // Big center-middle
		case 1: yRatio = 0.78f; scale = 1.30f; alignRight = true; break; // Lower-right mission text
		case 2: yRatio = 0.42f; scale = 1.75f; break; // Wasted/Mission title
		case 3: yRatio = 0.50f; scale = 1.15f; break; // Center small
		case 4: yRatio = 0.35f; scale = 1.15f; break; // Upper-middle
		case 5: yRatio = 0.52f; scale = 1.05f; break; // Center status
		case 6: yRatio = 0.25f; scale = 1.25f; break; // Top-center
		default: break;
	}

	const float fontSize = (UISettings::fontSize() / 2.0f) * scale;
	std::vector<std::string> lines;
	{
		std::stringstream ss(m_imguiGameText);
		std::string row;
		std::string carryColor;
		while (std::getline(ss, row, '\n')) {
			if (!carryColor.empty()) {
				row = carryColor + row;
			}
			// Track last {RRGGBB} color code to carry across newlines
			for (size_t i = 0; i + 7 < row.size(); ++i) {
				if (row[i] == '{' && row[i + 7] == '}') {
					bool hex = true;
					for (size_t k = 1; k <= 6; ++k) {
						if (!std::isxdigit(static_cast<unsigned char>(row[i + k]))) { hex = false; break; }
					}
					if (hex) {
						carryColor = row.substr(i, 8);
						i += 7;
					}
				}
			}
			lines.push_back(std::move(row));
		}
	}

	const float totalHeight = static_cast<float>(lines.size()) * fontSize * 1.15f;
	float curY = (float) sa::RsGlobal->screenHeight * yRatio - totalHeight * 0.5f;
	const float screenW = (float) sa::RsGlobal->screenWidth;
	for (const auto& line : lines) {
		ImVec2 sz = renderer->calculateTextSize(line, fontSize);
		float posX = alignRight ? (screenW * 0.94f - sz.x) : ((screenW - sz.x) * 0.5f);
		renderer->drawText(ImVec2(posX, curY), ImColor(1.0f, 1.0f, 1.0f), line, true, fontSize);
		curY += fontSize * 1.15f;
	}
}

/**
 * @return
 */
uint8_t Game::GetActiveInterior()
{
	//spdlog::info("Game::GetActiveInterior");
	auto* pCurrArea = eng::as<uint8_t*>(eng::Sym::CGame_currArea);
	return pCurrArea ? *pCurrArea : 0;
}

/**
 * @return
 */
int Game::GetLocalMoney()
{
	//spdlog::info("Game::GetLocalMoney");
	return WORLD_PLAYERS ? WORLD_PLAYERS->Score : 0;
}

/**
 * @param iAmmount
 */
void Game::AddToLocalMoney(int iAmmount)
{
	//spdlog::info("Game::AddToLocalMoney: {}", iAmmount);
	ScriptCommand(&add_to_player_money, 0, iAmmount);
}

void Game::ResetLocalMoney()
{
	//spdlog::info("Game::ResetLocalMoney");

	int iMoney = GetLocalMoney();
	if (!iMoney) return;

	if (iMoney < 0) {
		AddToLocalMoney(abs(iMoney));
	}
	else {
		AddToLocalMoney(-(iMoney));
	}
}

/**
 * @param iHour
 * @param iMinute
 */
void Game::SetWorldTime(int iHour, int iMinute)
{
	//spdlog::info("Game::SetWorldTime: {}, {}", iHour, iMinute);

	if (auto* pMin = eng::as<uint8_t*>(eng::Sym::CClock_ms_nGameClockMinutes)) {
		*pMin = static_cast<uint8_t>(iMinute);
	}
	if (auto* pHr = eng::as<uint8_t*>(eng::Sym::CClock_ms_nGameClockHours)) {
		*pHr = static_cast<uint8_t>(iHour);
	}
	ScriptCommand(&set_current_time, iHour, iMinute);
}

/**
 * @param iHour
 * @param iMinute
 */
void Game::GetWorldTime(int* iHour, int* iMinute)
{
	//spdlog::info("Game::GetWorldTime");

	if (iMinute) {
		auto* pMin = eng::as<uint8_t*>(eng::Sym::CClock_ms_nGameClockMinutes);
		*iMinute = pMin ? static_cast<int>(*pMin) : 0;
	}
	if (iHour) {
		auto* pHr = eng::as<uint8_t*>(eng::Sym::CClock_ms_nGameClockHours);
		*iHour = pHr ? static_cast<int>(*pHr) : 0;
	}
}

/**
 * @param dwTimer
 */
void Game::UpdateGlobalTimer(uint32_t dwTimer)
{
	//spdlog::info("Game::UpdateGlobalTimer: {}", dwTimer);

	if (!m_clockEnabled) {
		*eng::as<uint32_t*>(eng::Sym::CTimer_m_snTimeInMilliseconds) = dwTimer & 0x3FFFFFFF;
	}
}

/**
 * @param byteWeatherID
 */
void Game::SetWorldWeather(int byteWeatherID)
{
	//spdlog::info("Game::SetWorldWeather: {}", byteWeatherID);
	// CWeather::ForcedWeatherType / NewWeatherType / OldWeatherType are int16_t.
	// CWeather::Update is patched to ret in multiplayer, so update all three
	// unconditionally even when TogglePlayerClock(true) is active.
	const int16_t weather = static_cast<int16_t>(byteWeatherID);
	if (auto* pForced = eng::as<int16_t*>(eng::Sym::CWeather_ForcedWeatherType)) {
		*pForced = weather;
	}
	if (auto* pNew = eng::as<int16_t*>(eng::Sym::CWeather_NewWeatherType)) {
		*pNew = weather;
	}
	if (auto* pOld = eng::as<int16_t*>(eng::Sym::CWeather_OldWeatherType)) {
		*pOld = weather;
	}
}

void Game::SetHudHidden(uint32_t reasonBit, bool hidden)
{
	if (hidden) {
		m_hudHideMask |= reasonBit;
	}
	else {
		m_hudHideMask &= ~reasonBit;
	}

	const bool show = (m_hudHideMask == 0);
	*g_saSym->Abs<uint8_t*>(addr::CTheScripts_bDisplayHud) = show ? 1 : 0;
	*g_saSym->Abs<uint8_t*>(addr::CHud_bScriptDontDisplayRadar) = show ? 0 : 1;
}

void Game::ClearAllHudHideReasons()
{
	m_hudHideMask = 0;
	*g_saSym->Abs<uint8_t*>(addr::CTheScripts_bDisplayHud) = 1;
	*g_saSym->Abs<uint8_t*>(addr::CHud_bScriptDontDisplayRadar) = 0;
}

void Game::DisplayHUD(bool bDisp)
{
	SetHudHidden(HUD_HIDE_GENERAL, !bDisp);
}

/**
 * @param toggle
 */
void Game::ToggleCJWalk(bool toggle)
{
	patches::set("ToggleCJWalk", !toggle);
}

void Game::DisableTrainTraffic()
{
	//spdlog::info("Game::DisableTrainTraffic");
	ScriptCommand(&enable_train_traffic, 0);
}

/**
 * @param x
 * @param y
 */
void Game::RefreshStreamingAt(float x, float y)
{
	//spdlog::info("Game::RefreshStreamingAt: {}, {}", x, y);
	ScriptCommand(&refresh_streaming_at, x, y);
}

void Game::PreloadObjectsAnims()
{
	//spdlog::info("Game::PreloadObjectsAnims");

	// keep the throwable weapon models loaded
	if (!IsModelLoaded(WEAPON_MODEL_TEARGAS)) RequestModel(WEAPON_MODEL_TEARGAS);
	if (!IsModelLoaded(WEAPON_MODEL_GRENADE)) RequestModel(WEAPON_MODEL_GRENADE);
	if (!IsModelLoaded(WEAPON_MODEL_MOLOTOV)) RequestModel(WEAPON_MODEL_MOLOTOV);

	// special action object
	if (!IsModelLoaded(330)) RequestModel(330);
	if (!IsModelLoaded(OBJECT_PARACHUTE)) RequestModel(OBJECT_PARACHUTE);
	if (!IsModelLoaded(OBJECT_CJ_CIGGY)) RequestModel(OBJECT_CJ_CIGGY);
	if (!IsModelLoaded(OBJECT_DYN_BEER_1)) RequestModel(OBJECT_DYN_BEER_1);
	if (!IsModelLoaded(OBJECT_CJ_BEER_B_2)) RequestModel(OBJECT_CJ_BEER_B_2);
	if (!IsModelLoaded(OBJECT_CJ_PINT_GLASS)) RequestModel(OBJECT_CJ_PINT_GLASS);
	if (!IsModelLoaded(18631)) RequestModel(18631);

	// special action anim
	if (IsAnimationLoaded("PARACHUTE") == 0) RequestAnimation("PARACHUTE");
	if (IsAnimationLoaded("PAULNMAC") == 0) RequestAnimation("PAULNMAC");
	if (IsAnimationLoaded("BAR") == 0) RequestAnimation("BAR");
	if (IsAnimationLoaded("SMOKING") == 0) RequestAnimation("SMOKING");
	if (IsAnimationLoaded("DANCING") == 0) RequestAnimation("DANCING");
	if (IsAnimationLoaded("GFUNK") == 0) RequestAnimation("GFUNK");
	if (IsAnimationLoaded("RUNNINGMAN") == 0) RequestAnimation("RUNNINGMAN");
	if (IsAnimationLoaded("STRIP") == 0) RequestAnimation("STRIP");
	if (IsAnimationLoaded("WOP") == 0) RequestAnimation("WOP");
	if (IsAnimationLoaded("CARRY") == 0) RequestAnimation("CARRY");
	if (IsAnimationLoaded("RAPPING") == 0) RequestAnimation("RAPPING");
	if (IsAnimationLoaded("PED") == 0) RequestAnimation("PED");
	if (IsAnimationLoaded("COP_AMBIENT") == 0) RequestAnimation("COP_AMBIENT");
	if (IsAnimationLoaded("SWEET") == 0) RequestAnimation("SWEET");
	if (IsAnimationLoaded("CRACK") == 0) RequestAnimation("CRACK");
}

void Game::DisableAutoAim()
{
	patches::set("DisableAutoAim.FindTarget", true);
	patches::set("DisableAutoAim.FindNextTarget", true);
	patches::set("DisableAutoAim.SetTarget", true);
	patches::set("DisableAutoAim.TapToTarget", true);
}

void Game::SetWantedLevel(uint8_t level)
{
	//spdlog::info("Game::SetWantedLevel: {}", level);

	if (g_java) {
		g_java->setWantedLevel(level);
	}
}

void Game::EnableStuntBonus(bool bEnable)
{
	spdlog::debug("Game::EnableStuntBonus: {}", bEnable);
	eng::fn::CStuntJumpManager_SetActive(bEnable);
}

void Game::LoadShoppingDataSubsection(const char* szSectionName)
{
	if (!szSectionName || !szSectionName[0]) {
		eng::fn::CShopping_RemoveLoadedShop();
		return;
	}
	if (strlen(szSectionName) <= 8) {
		eng::fn::CShopping_LoadShop(szSectionName);
	}
}

void Game::PlayCrimeReport(uint32_t crimeId, const sa::CVector& pos)
{
	if (!Patches::hasFullAudioData()) {
		return;
	}
	if (crimeId < 2 || crimeId > 22) {
		return;
	}
	CPlayerPed* pLocalPed = FindPlayerPed();
	if (!pLocalPed || !pLocalPed->m_ped || !pLocalPed->m_ped->m_pPlayerData || !pLocalPed->m_ped->m_pPlayerData->m_pWanted) {
		return;
	}
	// CWanted::m_PoliceScannerAudioEntity offset verified via libGTASA.so CWanted::ReportCrimeNow:
	// arm64-v8a: +0x308 (#776), armeabi-v7a: +0x21C (#540)
	constexpr size_t kPoliceScannerOffset = VER_x32 ? 0x21C : 0x308;
	void* pScanner = reinterpret_cast<uint8_t*>(pLocalPed->m_ped->m_pPlayerData->m_pWanted) + kPoliceScannerOffset;
	eng::fn::CAEPoliceScannerAudioEntity_AddAudioEvent(pScanner, 0xA4, static_cast<int>(crimeId), &pos);
}

void Game::SetGravity(float fGravity)
{
	//spdlog::info("Game::SetGravity: {}", fGravity);
	Memory::protectAddr(g_saSym->Abs(addr::SetGravity));
	*g_saSym->Abs<float*>(addr::SetGravity) = -fGravity;
}

float Game::FindGroundZForCoord(float fX, float fY, float fZ)
{
	//spdlog::info("Game::FindGroundZForCoord: {}, {}, {}", fX, fY, fZ);

	float fPosZ;
	ScriptCommand(&get_ground_z, fX, fY, fZ, &fPosZ);
	return fPosZ;
}

void Game::DrawGangZone(float fPos[], uint32_t dwColor, bool bFullMap)
{
	//spdlog::info("Game::DrawGangZone: {}, {}, {}", fPos[0], fPos[1], fPos[2]);

	// CRadar::DrawAreaOnRadar
	eng::fn::CRadar_DrawAreaOnRadar( fPos, &dwColor, bFullMap);
}

// Disable scene entry and exit
void Game::DisableEnterExits()
{
	//spdlog::info("Game::DisableEnterExits");

	typedef sa::CPool<sa::CEntryExit, sa::CEntryExit> CEntryExitPool;
	auto ppPool = eng::as<CEntryExitPool**>(eng::Sym::CEntryExitManager_mp_poolEntryExits);
	if (!ppPool || !*ppPool) return;
	auto poolEntryExit = *ppPool;

	if (!poolEntryExit->m_aStorage || poolEntryExit->m_nSize <= 0) {
		return;
	}

	for (int i = 0; i < poolEntryExit->m_nSize; i++) {
		sa::CEntryExit* pEntry = &poolEntryExit->m_aStorage[i];
		pEntry->flags = 0;
	}
}

bool Game::IsAnimationLoaded(const char* szAnimLib)
{
	//spdlog::info("Game::IsAnimationLoaded: {}", szAnimLib);

	// CAnimManager::GetAnimationBlock
	auto pAnimBlock = eng::fn::CAnimManager_GetAnimationBlock<CAnimBlock*>( szAnimLib);
	if (pAnimBlock == nullptr) return false;
	return pAnimBlock->m_loaded;

	//return ScriptCommand(&is_animation_loaded, szAnimLib);
}

void Game::RequestAnimation(const char* szAnimLib)
{
	//spdlog::info("Game::RequestAnimation: {}", szAnimLib);
	ScriptCommand(&request_animation, szAnimLib);
}

/**
 * @param iSound
 * @param fX
 * @param fY
 * @param fZ
 */
void Game::PlaySound(int iSound, float fX, float fY, float fZ)
{
	//spdlog::info("Game::PlaySound: {}, {}, {}, {}", iSound, fX, fY, fZ);
	ScriptCommand(&play_sound, fX, fY, fZ, iSound);
}

void Game::PlaySoundEx(int iSound, float fX, float fY, float fZ)
{
	if (iSound == 0) {
		if (m_missionAudioState != 0) {
			ScriptCommand(&clear_mission_audio, 1);
			m_missionAudioState = 0;
		}
		return;
	}
	if (iSound == 1) {
		return;
	}
	if (iSound >= 1000 && iSound < 2000) {
		PlaySound(iSound, fX, fY, fZ);
		return;
	}
	if (iSound >= 2000 && Patches::hasFullAudioData()) {
		ScriptCommand(&clear_mission_audio, 1);
		ScriptCommand(&load_mission_audio, 1, iSound);
		ScriptCommand(&set_mission_audio_position, 1, fX, fY, fZ);
		m_missionAudioState = 1;
	}
}

void Game::ProcessAudio()
{
	if (m_missionAudioState == 1 && Patches::hasFullAudioData()) {
		if (ScriptCommand(&has_mission_audio_loaded, 1)) {
			ScriptCommand(&play_mission_audio, 1);
			m_missionAudioState = 2;
		}
	}
}

/**
 * @return
 */
bool Game::IsGamePaused()
{
	auto* pPaused = eng::as<bool*>(eng::Sym::CTimer_m_UserPause);
	return pPaused ? *pPaused : false;
}

/**
 * @param pPed
 * @return
 */
bool Game::RemovePlayer(CPlayerPed* pPed)
{
	//spdlog::info("Game::RemovePlayer");

	if (!pPed) return false;
	bUsedPlayerSlots[pPed->m_bytePlayerNumber] = false;
	delete pPed;
	return true;
}

/**
 * @param iModel
 * @param iType
 * @param x
 * @param y
 * @param z
 * @param pdwIndex
 * @return
 */
uint32_t Game::CreatePickup(int iModel, int iType, float x, float y, float z, int* pdwIndex, int* pEffectiveModel)
{
	//spdlog::info("Game::CreatePickup: {}, {}, {}, {}, {}, {}", iModel, iType, x, y, z, *pdwIndex);

	if (artwork::isCustomObjectId(iModel)) {
		const int32_t slot = artwork::ArtworkManager::instance().ensureCustomObjectLoaded(iModel);
		iModel = (slot >= 0) ? slot : artwork::ArtworkManager::instance().resolveFallbackObjectModel(iModel);
	}

	if (!IsValidModel(iModel)) {
		iModel = 18631;
	}

	if (!IsModelLoaded(iModel)) {
		RequestModel(iModel);
		LoadRequestedModels();
		if (!IsModelLoaded(iModel)) {
			spdlog::error("Game::CreatePickup: model {} failed to load", iModel);
			if (pdwIndex) *pdwIndex = -1;
			if (pEffectiveModel) *pEffectiveModel = -1;
			return 0;
		}
	}

	uint32_t hnd = 0;
	ScriptCommand(&create_pickup, iModel, iType, x, y, z, &hnd);
	if (pdwIndex) {
		*pdwIndex = (hnd != 0) ? static_cast<int>(hnd & 0xFFFFu) : -1;
	}
	if (pEffectiveModel) {
		*pEffectiveModel = (hnd != 0) ? iModel : -1;
	}

	return hnd;
}

uint32_t Game::CreateWeaponPickup(int iModel, int iAmmo, float x, float y, float z)
{
	if (!IsValidModel(iModel)) {
		return 0;
	}

	if (!IsModelLoaded(iModel)) {
		RequestModel(iModel);
		LoadRequestedModels();
		if (!IsModelLoaded(iModel)) {
			spdlog::error("Game::CreateWeaponPickup: model {} failed to load", iModel);
			return 0;
		}
	}

	uint32_t hnd = 0;
	ScriptCommand(&create_pickup_with_ammo, iModel, 4, iAmmo, x, y, z, &hnd);
	return hnd;
}

/**
 * @param iModel
 * @param vecPos
 * @param vecRot
 * @param fDrawDistance
 * @return
 */
CObject* Game::NewObject(int iModel, sa::CVector vecPos, sa::CVector vecRot, float fDrawDistance)
{
	//spdlog::info("Game::NewObject: {}, {}, {}, {}, {}", iModel, vecPos.x, vecPos.y, vecPos.z, fDrawDistance);
	auto pObject = new CObject(iModel, vecPos, vecRot, fDrawDistance, 0);
	return pObject;
}

/**
 * @param iSkin
 * @param fX
 * @param fY
 * @param fZ
 * @param fRotation
 * @param unk
 * @param bIsNPC
 * @return
 */
CPlayerPed* Game::NewPlayer(int iSkin, float fX, float fY, float fZ, float fRotation, bool unk, bool bIsNPC)
{
	//spdlog::info("Game::NewPlayer: {}, {}, {}, {}, {}, {}, {}", iSkin, fX, fY, fZ, fRotation, unk, bIsNPC);
	uint8_t bytePedSlot = FindFirstFreePlayerPedSlot();
	if (!bytePedSlot) return nullptr;
	auto pPed = new CPlayerPed(bytePedSlot, iSkin, fX, fY, fZ, fRotation);
	if (!pPed->m_ped) {
		delete pPed;
		return nullptr;
	}
	bUsedPlayerSlots[bytePedSlot] = true;
	return pPed;
}

/**
 * @param iVehicleType
 * @param fX
 * @param fY
 * @param fZ
 * @param fRotation
 * @param bAddSiren
 * @return
 */
CVehicle* Game::NewVehicle(int iVehicleType, float fX, float fY, float fZ, float fRotation, bool bAddSiren)
{
	//spdlog::info("Game::NewVehicle: {}, {}, {}, {}, {}, {}", iVehicleType, fX, fY, fZ, fRotation, bAddSiren);
	bool bPreloaded = false;
	if (iVehicleType >= 400 && iVehicleType <= 611 && m_preloadedVehicleModels[iVehicleType - 400]) {
		bPreloaded = true;
	}

	auto pVehicle = new CVehicle(iVehicleType, fX, fY, fZ, fRotation, bPreloaded, bAddSiren);
	return pVehicle;
}
