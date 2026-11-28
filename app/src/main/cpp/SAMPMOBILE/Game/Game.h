#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include <Core/Vector.h>

class CCamera;
class CObject;
class CPlayerPed;
class CVehicle;
class ImGuiRenderer;

class Game {
public:
	Game();
	~Game();

	void Initialize();
	void StartGame();

	void ToggleThePassingOfTime(bool bOnOff);
	void EnableClock(bool bEnable);
	void EnableZoneNames(bool bEnable);
	void SetWorldTime(int iHour, int iMinute);
	void GetWorldTime(int* iHour, int* iMinute);
	void PreloadObjectsAnims();
	void SetWorldWeather(int iWeatherID);
	enum HudHideReason : uint32_t {
		HUD_HIDE_GENERAL         = 1u << 0,
		HUD_HIDE_CLASS_SELECT    = 1u << 1,
		HUD_HIDE_SPECTATE        = 1u << 2,
		HUD_HIDE_TEXTDRAW_SELECT = 1u << 3,
		HUD_HIDE_DRUNK           = 1u << 4,
		HUD_HIDE_SCOREBOARD      = 1u << 5,
	};

	void DisplayHUD(bool bDisp);
	void SetHudHidden(uint32_t reasonBit, bool hidden);
	void ClearAllHudHideReasons();
	[[nodiscard]] bool IsHudHidden() const { return m_hudHideMask != 0; }
	void UpdateCheckpoints();
	uint8_t GetActiveInterior();
	uint8_t GetPedSlotsUsed();
	void PlaySound(int iSound, float fX, float fY, float fZ);
	void RefreshStreamingAt(float x, float y);
	void DisableTrainTraffic();
	void UpdateGlobalTimer(uint32_t dwTime);
	void SetGravity(float fGravity);
	float FindGroundZForCoord(float fX, float fY, float fZ);

	void DrawGangZone(float fPos[], uint32_t dwColor, bool bFullMap);

	uint32_t CreatePickup(int iModel, int iType, float fX, float fY, float fZ, int* pdwindex, int* pEffectiveModel = nullptr);
	uint32_t CreateWeaponPickup(int iModel, int iAmmo, float fX, float fY, float fZ);
	CObject* NewObject(int iModel, sa::CVector vecPos, sa::CVector vecRot, float fDrawDistance);
	CPlayerPed* NewPlayer(int iSkin, float fX, float fY, float fZ, float fRotation, bool unk, bool bIsNPC);
	bool RemovePlayer(CPlayerPed* pPlayer);

	CVehicle* NewVehicle(int iVehicleType, float fX, float fY, float fZ, float fRotation, bool bAddSiren);

	void DisableMarker(uint32_t dwMarker);

	uint32_t CreateRadarMarkerIcon(uint8_t byteType, float fPosX, float fPosY, float fPosZ, uint32_t dwColor, uint8_t byteStyle);

	bool IsModelLoaded(int iModel);
	void RequestModel(int iModel);
	void LoadRequestedModels();
	void RemoveModel(int iModel, bool bFromStreaming);

	bool IsAnimationLoaded(const char* szAnimLib);
	void RequestAnimation(const char* szAnimLib);

	CCamera* GetCamera();
	CPlayerPed* FindPlayerPed();

	static bool IsGamePaused();
	void DisableAutoAim();
	void SetWantedLevel(uint8_t level);
	void EnableStuntBonus(bool bEnable);
	void LoadShoppingDataSubsection(const char* szSectionName);
	void PlayCrimeReport(uint32_t crimeId, const sa::CVector& pos);
	void DisplayGameText(const char* szStr, int iTime, int iSize);
	void AddToLocalMoney(int iAmmount);
	void ResetLocalMoney();
	int GetLocalMoney();
	void DisableEnterExits();

	void SetCheckpointInformation(sa::CVector* vecPos, sa::CVector* vecSize);
	void SetRaceCheckpointInformation(uint8_t byteType, sa::CVector* vecPos, sa::CVector* vecNextPos, float fRadius);
	void MakeRaceCheckpoint();
	void DisableRaceCheckpoint();

	void EnableGameInput(bool enable) { m_gameInput.store(enable, std::memory_order_release); }
	[[nodiscard]] bool IsGameInputEnabled() const { return m_gameInput.load(std::memory_order_acquire); }

	void ToggleCJWalk(bool bUseCJWalk);

	void EnableCheckpoints(bool enable) { m_checkpointsEnabled = enable; }
	[[nodiscard]] bool AreCheckpointsEnabled() const { return m_checkpointsEnabled; }

	void EnableRaceCheckpoints(bool enable) { m_raceCheckpointsEnabled = enable; }
	[[nodiscard]] bool AreRaceCheckpointsEnabled() const { return m_raceCheckpointsEnabled; }

	// GameText containing characters the engine font cannot render is
	// redirected here and drawn by ImGui during the UI pass.
	void DrawImGuiGameText(ImGuiRenderer* renderer);
	void ClearImGuiGameText() { m_imguiGameText.clear(); m_imguiGameTextExpire = 0; }

	// T06: SA-MP PlayerPlaySound segmented handler & mission audio polling
	void PlaySoundEx(int iSound, float fX, float fY, float fZ);
	void ProcessAudio();

private:
	CCamera* m_gameCamera;
	CPlayerPed* m_gamePlayer;

	std::atomic<bool> m_gameInput{true};
	uint32_t m_hudHideMask = 0;
	bool m_clockEnabled;
	bool m_preloadedVehicleModels[212];

	bool m_checkpointsEnabled;
	sa::CVector m_checkpointPos;
	sa::CVector m_checkpointExtent;
	uint32_t m_checkpointMarker;

	bool m_raceCheckpointsEnabled;
	sa::CVector m_raceCheckpointPos;
	sa::CVector m_raceCheckpointNextPos;
	uint8_t m_raceType;
	float m_raceCheckpointRadius;
	uint32_t m_raceCheckpointMarker;
	uint32_t m_raceCheckpointHandle;

	std::string m_imguiGameText;
	uint32_t m_imguiGameTextExpire;
	int m_imguiGameTextSize;

	int m_missionAudioState = 0; // 0 = none, 1 = loading slot 1, 2 = playing slot 1
};
