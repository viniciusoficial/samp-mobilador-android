#include "HookDomains.h"
#include "../Hooks.h"
#include "../Camera.h"
#include "../Font.h"
#include "../Game.h"
#include "../ObjectUVAnim.h"
#include "../PlayerPed.h"
#include "../RGBA.h"
#include "../TouchCombat.h"
#include "../TouchWidgets.h"
#include "../ThreadOptimizer.h"
#include "../Util.h"
#include "../Engine/PatchRegistry.h"
#include "../Engine/Symbols.h"
#include "../../Addr.h"
#include "../../UI/UI.h"
#include "../../UI/SAMPWidgets/ButtonPanel.h"
#include "../../UI/SAMPWidgets/Chat.h"
#include "../../UI/SAMPWidgets/Keyboard.h"
#include "../../Net/NetGame.h"
#include "../../Net/TextDrawPool.h"
#include "../../Net/VehiclePool.h"
#include "../../Java/Java.h"
#include "../../Java/EditObject.h"
#include "../../Client.h"
#include "../../Settings.h"
#include <Memory.h>
#include <SA.h>
#include <RakNet/RakClientInterface.h>
#include <spdlog/spdlog.h>
#include <sys/sysinfo.h>
#include <atomic>
#include <thread>

DECL_HOOK(void, CTimer_StartUserPause)
{
	if (pUI)
	{
		pUI->setVisible(false);
	}

	if (g_java && pNetGame)
	{
		g_java->setPauseState(true);
	}

	if (auto* userPause = eng::as<bool *>(eng::Sym::CTimer_m_UserPause))
	{
		*userPause = true;
	}
}

DECL_HOOK(void, CTimer_EndUserPause)
{
	if (pUI)
	{
		pUI->setVisible(true);
	}

	if (g_java && pNetGame)
	{
		g_java->setPauseState(false);
	}

	if (auto* userPause = eng::as<bool *>(eng::Sym::CTimer_m_UserPause))
	{
		*userPause = false;
	}
}

unsigned int *msCollected;
unsigned int curMS;

static std::atomic<bool> s_bPendingAdjustableHud{false};
static bool s_bWatchingAdjustableHudExit = false;
static std::atomic<bool> s_bAllowInitialWorldBoot{false};
static std::atomic<bool> s_bExitRequested{false};

void Hooks::openAdjustableHud()
{
	s_bPendingAdjustableHud.store(true);
}

void Hooks::allowInitialWorldBoot()
{
	s_bAllowInitialWorldBoot.store(true, std::memory_order_release);
}

// Unified graceful exit path (aligned with PC SA-MP GameHookDeferGameShutdown + /q):
// Prevents MainGameFunc from exiting its loop into DoGameRestart -> CGame::Shutdown ->
// CAudioEngine::Shutdown (OpenSLES/BASS HAL hang on emulators) -> OS_ApplicationEvent UAF.
void Hooks::requestGracefulExit(const char* reason)
{
	if (sa::RsGlobal) {
		sa::RsGlobal->quit = 0;
	}

	if (s_bExitRequested.exchange(true, std::memory_order_acq_rel)) {
		return;
	}

	spdlog::info("Requesting graceful game exit ({})", reason ? reason : "unknown");

	if (pNetGame && pNetGame->GetRakClient()) {
		pNetGame->GetRakClient()->Disconnect(100);
	}

	if (auto logger = spdlog::default_logger()) {
		logger->flush();
	}

	std::thread([]() {
		std::this_thread::sleep_for(std::chrono::milliseconds(400));
		_exit(0);
	}).detach();

	if (g_java) {
		g_java->exitGame();
	}
	else {
		_exit(0);
	}
}

DECL_HOOK(void, MainMenuScreen_OnExit, void* _this)
{
	(void) _this;
	Hooks::requestGracefulExit("MainMenuScreen::OnExit");
}

DECL_HOOK(void, QuitApp)
{
	Hooks::requestGracefulExit("QuitApp");
}

DECL_HOOK(bool, DoGameRestart)
{
	// MainGameFunc calls DoGameRestart() both on initial world boot (m_bWantToRestart == 1,
	// RsGlobal->quit == 0 -> sets gGameState = 8 and returns true) and on exit
	// (RsGlobal->quit != 0 -> runs CGame::Shutdown + CAudioEngine::Shutdown and returns false).
	if (sa::RsGlobal && sa::RsGlobal->quit != 0) {
		Hooks::requestGracefulExit("DoGameRestart:quit");
		return false;
	}
	return DoGameRestart();
}

DECL_HOOK(void, MainMenuScreen_OnStartGame, void* _this)
{
	if (!Client::offlineMode()) {
		spdlog::info("Blocked singleplayer Start Game menu in online session");
		return;
	}
	MainMenuScreen_OnStartGame(_this);
}

DECL_HOOK(void, Menu_NewGame)
{
	if (!Client::offlineMode() && !s_bAllowInitialWorldBoot.exchange(false, std::memory_order_acq_rel)) {
		spdlog::info("Blocked singleplayer Menu_NewGame in online session");
		return;
	}
	Menu_NewGame();
}

DECL_HOOK(void, StartGameScreen_OnLoadGame, void* _this)
{
	if (!Client::offlineMode()) {
		return;
	}
	StartGameScreen_OnLoadGame(_this);
}

DECL_HOOK(void, StartGameScreen_OnDeleteGame, void* _this)
{
	if (!Client::offlineMode()) {
		return;
	}
	StartGameScreen_OnDeleteGame(_this);
}

// GTA:SA v2.10 runs the frontend/menu on a separate frame path
// (DoGameState -> RsEventHandler -> AppEventHandler -> FrontendIdle) that
// never reaches Idle()/Render2dStuff(). Client::process() therefore has to
// be driven from here as well or StartGame() can never fire and the client
// sits in the menu forever behind the Java loading overlay.
static bool s_clientProcessedThisFrame = false;
extern volatile pid_t g_gameThreadTid;

DECL_HOOK(void, FrontendIdle)
{
	static bool fired = false;
	if (!fired) {
		fired = true;
		g_gameThreadTid = gettid();
		spdlog::info("hook fired: FrontendIdle tid={}", (int) g_gameThreadTid);
	}
	FrontendIdle();

	if (sa::RsGlobal && sa::RsGlobal->quit != 0) {
		Hooks::requestGracefulExit("FrontendIdle:RsGlobal.quit");
		return;
	}

	if (s_bWatchingAdjustableHudExit) {
		auto* pAdjInst = eng::as<uintptr_t*>(eng::Sym::CAdjustableHUD_m_pInstance);
		if (pAdjInst && *pAdjInst == 0) {
			s_bWatchingAdjustableHudExit = false;
			eng::fn::Menu_SwitchOffToGame();
			EnsureTouchWidgetsCreated();
		}
	}

	Client::process(false);
}

DECL_HOOK(void, Render2dStuff)
{
	static bool fired = false;
	if (!fired) {
		fired = true;
		spdlog::info("hook fired: Render2dStuff tid={}", (int) gettid());
	}

	// Clean 3D Game-Only Screenshot: capture right before 2D GTA HUD, touch
	// widgets, and SA-MP ImGui Chat/UI are drawn over the 3D scene.
	if (ConsumePendingScreenshot(1)) {
		CaptureCurrentFramebufferToPng(1);
	}

	Render2dStuff();

	s_clientProcessedThisFrame = true;
	Client::process(true);

	// touch-widget watchdog: DeleteAll (CGame::Shutdown / gamemode restart /
	// death) or AML plugins like libSAUtils64.so redirecting m_pWidgets can
	// leave stock or custom combat widgets missing — recreate whenever needed.
	{
		static int wdFrames = 0;
		if (++wdFrames >= 60) {
			wdFrames = 0;
			uintptr_t* widgets = GetTouchWidgets();
			if ((pNetGame || Client::offlineMode()) && pGame && GamePool_FindPlayerPed() && pGame->FindPlayerPed() && widgets) {
				// engine rewrites targeting widgets back onto the attack slot
				// on every RepositionAdjustableWidgets — keep our layout fix
				FixTouchWidgetLayout();
				if (!widgets[sa::WidgetIDs::WIDGET_ATTACK]) {
					spdlog::info("touch widgets wiped, re-creating");
					eng::fn::Menu_SwitchOffToGame();
					EnsureTouchWidgetsCreated();
				}
				else if (!widgets[sa::WidgetIDs::WIDGET_BUTTON_CROUCH]
					|| !widgets[sa::WidgetIDs::WIDGET_CAR_CHEATS]
					|| !widgets[sa::WidgetIDs::WIDGET_MISSION_SKIP]) {
					spdlog::info("custom touch widgets missing in live m_pWidgets, re-creating");
					EnsureTouchWidgetsCreated();
				}
			}
		}
	}

	// aim-button latch + block button + on-foot camera toggle: driven every
	// frame in UpdateTouchCombatVisuals() (render visibility) and
	// TickTouchCombatInput() (logic-frame tap/hold sampling).
	if ((pNetGame || Client::offlineMode()) && pGame && GamePool_FindPlayerPed() && pGame->FindPlayerPed()) {
		UpdateTouchCombatVisuals();
	}

	if (Settings::displayfps() && msCollected)
	{
		// fps
		msCollected[curMS++ % 0x28] = eng::fn::OS_TimeMS();

		RGBA bgColor = {0, 0, 0, 0};
		RGBA color = {255, 255, 255, 255};

		CFont::SetBackgroundColor(&bgColor);
		CFont::SetScale(1.12f * 0.54, 1.12f);
		CFont::SetOrientation(1);
		CFont::SetJustify(0);
		CFont::SetWrapX(640.0);
		CFont::SetProportional(0);
		CFont::SetFontStyle(1);
		CFont::SetEdge(0);
		CFont::SetColor(&color);

		char szStr[64];
		const unsigned int spanMs = msCollected[(curMS - 1) % 0x28] - msCollected[curMS % 0x28];
		const float fps = (spanMs > 0) ? (39000.0f / static_cast<float>(spanMs)) : 0.0f;
		snprintf(szStr, sizeof(szStr), "FPS: %.2f", fps);
		CFont::PrintString((float)((float)sa::RsGlobal->screenWidth / 640.0) * 4.0, (float)((float)sa::RsGlobal->screenHeight / 448.0) * 200.0, szStr);

		// mem (throttled to ~1 Hz to avoid per-frame mallinfo2 / /proc/meminfo overhead)
		static uint32_t s_lastMemSampleMs = 0;
		static long long s_cachedAllocMB = 0;
		static long long s_cachedTotalMB = 0;
		const uint32_t nowMs = GetTickCount();
		if (s_lastMemSampleMs == 0 || (nowMs - s_lastMemSampleMs) >= 1000)
		{
			s_lastMemSampleMs = nowMs;
			struct mallinfo2 memInfo = mallinfo2();
			s_cachedAllocMB = static_cast<long long>(memInfo.uordblks / (1024 * 1024));
#if VER_x32
			FILE *meminfo = fopen("/proc/meminfo", "r");
			if (meminfo)
			{
				char line[256];
				while (fgets(line, sizeof(line), meminfo))
				{
					if (strncmp(line, "MemTotal:", 9) == 0)
					{
						s_cachedTotalMB = atoll(&line[9]) / 1024;
						break;
					}
				}
				fclose(meminfo);
			}
#else
			struct sysinfo info{};
			if (sysinfo(&info) == 0)
			{
				const unsigned long long unit = info.mem_unit ? info.mem_unit : 1ULL;
				s_cachedTotalMB = static_cast<long long>((static_cast<unsigned long long>(info.totalram) * unit) / (1024ULL * 1024ULL));
			}
#endif
		}

		CFont::SetBackgroundColor(&bgColor);
		CFont::SetScale(1.12f * 0.54, 1.12f);
		CFont::SetOrientation(1);
		CFont::SetJustify(0);
		CFont::SetWrapX(640.0);
		CFont::SetProportional(0);
		CFont::SetFontStyle(1);
		CFont::SetEdge(0);
		CFont::SetColor(&color);

		char szStrMem[64];
		snprintf(szStrMem, sizeof(szStrMem), "MEM: %lld / %lld MB", s_cachedAllocMB, s_cachedTotalMB);
		CFont::PrintString((float)((float)sa::RsGlobal->screenWidth / 640.0) * 4.0, (float)((float)sa::RsGlobal->screenHeight / 448.0) * 210.0, szStrMem);

		// arch
		CFont::SetBackgroundColor(&bgColor);
		CFont::SetScale(1.12f * 0.54, 1.12f);
		CFont::SetOrientation(1);
		CFont::SetJustify(0);
		CFont::SetWrapX(640.0);
		CFont::SetProportional(0);
		CFont::SetFontStyle(1);
		CFont::SetEdge(0);
		CFont::SetColor(&color);

		char szStrArch[64];
		snprintf(szStrArch, sizeof(szStrArch), "ARCH: %s", VER_x32 ? "x32" : "x64");
		CFont::PrintString((float)((float)sa::RsGlobal->screenWidth / 640.0) * 4.0, (float)((float)sa::RsGlobal->screenHeight / 448.0) * 220.0, szStrArch);
	}
}

static RwCamera* (*Idle_RwCameraEndUpdate_orig)(RwCamera* camera) = nullptr;
static RwCamera* Idle_RwCameraEndUpdate_hook(RwCamera* camera)
{
	if (pUI)
	{
		bool bCaptureWithHud = ConsumePendingScreenshot(0);
		bool prevBtnPanel = false;
		if (bCaptureWithHud && pUI->buttonpanel()) {
			prevBtnPanel = pUI->buttonpanel()->visible();
			pUI->buttonpanel()->setVisible(false);
		}

		pUI->render();

		if (bCaptureWithHud) {
			CaptureCurrentFramebufferToPng(0);
			if (pUI->buttonpanel()) {
				pUI->buttonpanel()->setVisible(prevBtnPanel);
			}
		}
	}

	return Idle_RwCameraEndUpdate_orig ? Idle_RwCameraEndUpdate_orig(camera) : RwCameraEndUpdate(camera);
}

DECL_HOOK(uint32_t, Idle, void *param, bool firstFrame)
{
	static bool fired = false;
	if (!fired) {
		fired = true;
		g_gameThreadTid = gettid();
		spdlog::info("hook fired: Idle tid={}", (int) g_gameThreadTid);
	}
	SanitizeAllAlphaLinkLists();
	s_clientProcessedThisFrame = false;
	uint32_t result = Idle(param, firstFrame);
	if (sa::RsGlobal && sa::RsGlobal->quit != 0) {
		Hooks::requestGracefulExit("Idle:RsGlobal.quit");
		return result;
	}
	if (!s_clientProcessedThisFrame) {
		Client::process(false);
	}

	return result;
}

ThreadOptimizer g_threadOpt;
DECL_HOOK(void, AND_RunThread, void *a1)
{
	if (g_gameThreadTid == -1) {
		g_gameThreadTid = gettid();
	}
	g_threadOpt.pushThread(gettid());
	AND_RunThread(a1);
}

DECL_HOOK(void, CGame_Process)
{
	static bool fired = false;
	if (!fired) {
		fired = true;
		spdlog::info("hook fired: CGame::Process tid={}", (int) gettid());
	}
	BeginUVAnimFrame();
	CGame_Process();

	Settings::applyDynamicCameraAndFreeCam();

	const bool hudHiddenByGame = (pGame && pGame->IsHudHidden());
	if (auto* pDrawHud = eng::as<bool*>(eng::Sym::CHud_m_Wants_To_Draw_Hud)) {
		*pDrawHud = Settings::showGtaHud() && !hudHiddenByGame;
	}
	if (auto* pHideRadar = eng::as<bool*>(eng::Sym::CHud_bScriptDontDisplayRadar)) {
		*pHideRadar = (!Settings::showRadar() || !Settings::showGtaHud() || hudHiddenByGame);
	}

	if (pNetGame && pGame && pGame->FindPlayerPed()) {
		if (Settings::openHudEditOnStart()) {
			static int s_hudEditStartDelay = 0;
			if (++s_hudEditStartDelay >= 45) {
				s_hudEditStartDelay = 0;
				Settings::setOpenHudEditOnStart(false);
				s_bPendingAdjustableHud.store(true);
			}
		}
		if (s_bPendingAdjustableHud.exchange(false)) {
			uintptr_t pMobileMenu = eng::addr(eng::Sym::gMobileMenu);
			if (pMobileMenu) {
				eng::fn::MobileMenu_InitForPause( pMobileMenu);
				eng::fn::MobileMenu_ProcessPending( pMobileMenu);
				eng::fn::CAdjustableHUD_Toggle();
				s_bWatchingAdjustableHudExit = true;
			}
		}
	}

	if (pNetGame)
	{
		CTextDrawPool *pTextDrawPool = pNetGame->GetTextDrawPool();
		if (pTextDrawPool)
		{
			pTextDrawPool->SnapshotProcess();
		}

		CObjectPool *pObjectPool = pNetGame->GetObjectPool();
		if (pObjectPool)
		{
			pObjectPool->Process();
			pObjectPool->ProcessMaterialText();
		}

		CVehiclePool *pVehiclePool = pNetGame->GetVehiclePool();
		if (pVehiclePool)
		{
			pVehiclePool->ProcessLicensePlates();
		}

		// JNI edit callbacks queue ops from the Java UI thread — apply them
		// here on the game thread where GTA/ScriptCommand access is safe
		EditObject::processPending();

		if (pGame && pGame->GetCamera())
		{
			pGame->GetCamera()->SetToOwner();
		}
	}
}

void InstallLifecycleHooks()
{
	msCollected = eng::as<unsigned int *>(eng::Sym::msCollected);
	if (auto* pCurMS = eng::as<unsigned int *>(eng::Sym::curMS))
	{
		curMS = *pCurMS;
	}

	REG_HOOK(Boot, CTimer_StartUserPause, CTimer_StartUserPause, "Pause state tracking");
	REG_HOOK(Boot, CTimer_EndUserPause, CTimer_EndUserPause, "Resume state tracking");
	REG_HOOK(Boot, ANDRunThread, AND_RunThread, "Capture GameMain thread id");
	REG_HOOK(Boot, FrontendIdle, FrontendIdle, "Auto-start game & adjustable HUD exit");
	REG_HOOK(Boot, MainMenuScreen_OnExit, MainMenuScreen_OnExit, "Graceful process exit on pause-menu Quit Game");
	REG_HOOK(Boot, QuitApp, QuitApp, "Graceful process exit on QuitApp");
	REG_HOOK(Boot, DoGameRestart, DoGameRestart, "Prevent engine shutdown UAF/deadlock on exit or restart");
	REG_HOOK(Boot, MainMenuScreen_OnStartGame, MainMenuScreen_OnStartGame, "Block singleplayer Start Game menu in online session");
	REG_HOOK(Boot, Menu_NewGame, Menu_NewGame, "Block singleplayer New Game restart in online session");
	REG_HOOK(Boot, StartGameScreen_OnLoadGame, StartGameScreen_OnLoadGame, "Block singleplayer Load Game in online session");
	REG_HOOK(Boot, StartGameScreen_OnDeleteGame, StartGameScreen_OnDeleteGame, "Block singleplayer Delete Save in online session");
	REG_HOOK(Boot, Render2dStuff, Render2dStuff, "ImGui HUD & SA-MP 2D overlay render");
	REG_HOOK(Boot, Idle, Idle, "Main render frame & skipped-frame net pump");
	REG_HOOK_BL(Boot, addr::RwCameraEndUpdate_patch, "Idle.RwCameraEndUpdate", Idle_RwCameraEndUpdate_hook, &Idle_RwCameraEndUpdate_orig, "Render ImGui UI & HUD screenshot before RwCameraEndUpdate");
	REG_HOOK(Boot, CGame_Process, CGame_Process, "Per-frame SA-MP client loop");
}
