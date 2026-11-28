#include <atomic>
#include <algorithm>
#include <functional>
#include <iomanip>
#include <sstream>
#include <thread>
#include <vector>
#include <unistd.h>
#include <sys/syscall.h>
#include <dirent.h>
#include <fcntl.h>
#include <link.h>
#include <cstring>

#include <Memory.h>
#include <SA.h>
#include <encoding.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/android_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include "Client.h"
#include "ClientSubsystems.h"
#include "BackTrace.h"
#include "Settings.h"
#include "ClientCmds.h"
#include "Game/Camera.h"
#include "Game/PlayerPed.h"
#include "Game/Patches.h"
#include "Game/Hooks.h"
#include "Game/Util.h"
#include "Game/SnapShotHelper.h"
#include "Game/MaterialTextGenerator.h"
#include "Game/Engine/PatchRegistry.h"
#include "Enums/WidgetIDs.h"
#include "Java/Java.h"
#include "Java/MultiTouch.h"
#include "UI/UI.h"
#include "L10n.h"
#include "UI/SAMPWidgets/PlayerTags.h"
#include "UI/SAMPWidgets/SplashScreen.h"
#include "UI/SAMPWidgets/Chat.h"
#include "UI/SAMPWidgets/DeathWindow.h"
#include "UI/SAMPWidgets/ButtonPanel.h"
#include "UI/SAMPWidgets/Scoreboard.h"
#include "UI/SAMPWidgets/VoiceButton.h"
#include "Game/Game.h"
#include "Net/NetGame.h"
#include "Net/MenuPool.h"
#include "Net/TextDrawPool.h"
#include "Cef/CefBrowser.h"
#include "Cef/CefSession.h"
#include "Crashlytics.h"
#include "Game/Artwork/ArtworkTelemetry.h"

#include <cstdio>
#include <cstdlib>

JavaVM* Client::java_vm = nullptr;
std::string Client::game_dir;
bool Client::offline_mode = false;
std::string Client::samp_version;
int Client::samp_netgame = 0xFD9;

Java* g_java = nullptr;
UI* pUI = nullptr;
Game* pGame = nullptr;
NetGame* pNetGame = nullptr;
PlayerTags* pPlayerTags = nullptr;
AudioStream* pAudioStream = nullptr;
SnapShotHelper* pSnapShotHelper = nullptr;
MaterialTextGenerator* pMaterialTextGenerator = nullptr;

SymUtils* g_saSym = new SymUtils();
JavaVM* g_vm = nullptr;

static std::string logLevelFlagPath(const std::string& gameDir)
{
	return gameDir + "loglevel_debug.flag";
}

bool Client::isLogLevelDebug()
{
	if (auto logger = spdlog::default_logger()) {
		return logger->level() <= spdlog::level::debug;
	}
	return false;
}

bool Client::setLogLevelDebug(bool debug, bool persist)
{
	if (auto logger = spdlog::default_logger()) {
		logger->set_level(debug ? spdlog::level::debug : spdlog::level::info);
	}
	bool persisted = true;
	if (persist && !game_dir.empty()) {
		const std::string flag = logLevelFlagPath(game_dir);
		if (debug) {
			FILE* f = fopen(flag.c_str(), "wb");
			if (!f) {
				persisted = false;
			}
			else {
				fclose(f);
			}
		}
		else {
			unlink(flag.c_str());
		}
	}
	spdlog::info("[LOGLEVEL] set to {} (persisted={})", debug ? "debug" : "info", persisted ? 1 : 0);
	if (auto logger = spdlog::default_logger()) {
		logger->flush();
	}
	return persisted;
}

void init_logger(const std::string& gameDir)
{
	try {
		auto now = std::chrono::system_clock::now();
		auto in_time_t = std::chrono::system_clock::to_time_t(now);
		std::stringstream ss;
		ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d_%H-%M-%S");
		std::string dateTime = ss.str();

		// Define log file path client+date+time.log
		std::string path = gameDir + "logcat/client_" + dateTime + ".log";

		// Create Android (logcat) sink
		auto android_sink = std::make_shared<spdlog::sinks::android_sink_mt>("SAMPMOBILE");

		// Create file sink
		auto max_size = 1048576 * 10; // Max file size 10MB
		auto max_files = 3; // Rotate files
		auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(path, max_size, max_files);

		// Combine both sinks
		std::vector<spdlog::sink_ptr> sinks{android_sink, file_sink};
		auto logger = std::make_shared<spdlog::logger>("SAMPMOBILE", sinks.begin(), sinks.end());

		// Set log level and pattern (debug if marker flag exists, else info)
		const bool debugEnabled = (access(logLevelFlagPath(gameDir).c_str(), F_OK) == 0);
		logger->set_level(debugEnabled ? spdlog::level::debug : spdlog::level::info);
		logger->flush_on(spdlog::level::warn);
		logger->set_pattern("[%d-%m-%Y %H:%M:%S] [%^%l%$] %v");

		// Set as default logger
		spdlog::set_default_logger(logger);
		if (debugEnabled) {
			spdlog::info("[LOGLEVEL] debug mode restored from flag file");
		}
	}
	catch (const spdlog::spdlog_ex& ex) {
		__android_log_print(ANDROID_LOG_ERROR, "SAMPMOBILE", "Log initialization failed: %s", ex.what());
	}
}

// One-time boot progress marker: flushes buffered log lines to disk so a
// process dying mid-boot leaves its last completed milestone in the file log
// instead of a seemingly empty file (spdlog buffers everything below warn).
static inline void bootMilestoneFlush()
{
	if (auto logger = spdlog::default_logger()) {
		logger->flush();
	}
}

static bool g_bGameStarted = false;
volatile pid_t g_gameThreadTid = -1;
std::atomic<bool> g_processRan{false};
std::atomic<bool> g_worldReady{false};

static struct sigaction s_prevFatalAction[32]{};

// Memory telemetry helpers (normal thread context — stdio is fine here).
// Returns the kB value of a "Key:  12345 kB" style /proc field, or -1.
static long readProcKbField(const char* path, const char* key)
{
	FILE* f = fopen(path, "r");
	if (!f) return -1;
	char line[256];
	long result = -1;
	const size_t keyLen = strlen(key);
	while (fgets(line, sizeof(line), f)) {
		if (strncmp(line, key, keyLen) == 0) {
			const char* p = line + keyLen;
			while (*p == ' ' || *p == '\t') ++p;
			result = strtol(p, nullptr, 10);
			break;
		}
	}
	fclose(f);
	return result;
}

// Periodic memory telemetry. The sampler exists primarily to make LMK / vendor
// kills diagnosable: those leave zero trace, but a memory ramp visible in the
// file log is almost as good as a crash report. Lines are flushed after every
// write (cheap at this cadence) so the curve survives process death; under
// pressure the level escalates to warn, which both forces spdlog's flush_on
// and keeps the samples in the feedback bundle's tail extraction.
static void startMemoryTelemetryThread()
{
	std::thread([]() {
		long lastLoggedRssKb = -1;
		size_t lastLoggedArtBytes = 0;
		size_t lastLoggedArtSlots = 0;
		uint64_t sinceLog = 0;
		for (;;) {
			sleep(5);
			++sinceLog;

			const long rssKb = readProcKbField("/proc/self/status", "VmRSS:");
			const long availKb = readProcKbField("/proc/meminfo", "MemAvailable:");
			if (rssKb < 0 && availKb < 0) {
				continue;
			}

			const size_t artTxdBytes = artwork::g_telemetryTxdBytes.load(std::memory_order_relaxed);
			const size_t artSlots = artwork::g_telemetryLoadedSlots.load(std::memory_order_relaxed);
			const size_t artTxds = artwork::g_telemetryLoadedTxds.load(std::memory_order_relaxed);

			const bool pressure =
					(availKb >= 0 && availKb < 500L * 1024L) ||
					(rssKb >= 0 && rssKb > 1600L * 1024L);

			// Activity-adaptive sampling: log every 5s while memory or artwork
			// accounting is actively moving (join bursts, VW switches, streaming
			// heavy areas — exactly the windows where LMK strikes), fall back to
			// a 60s heartbeat when the process is stable. Time-based join windows
			// misfire because heavy loading often starts minutes after joining.
			const bool rssMoved = (lastLoggedRssKb < 0) ||
					(rssKb >= 0 && (rssKb - lastLoggedRssKb > 8L * 1024L ||
							lastLoggedRssKb - rssKb > 8L * 1024L));
			const bool artMoved = (artTxdBytes != lastLoggedArtBytes) || (artSlots != lastLoggedArtSlots);
			const bool heartbeatDue = sinceLog >= 12; // 60s
			if (!pressure && !rssMoved && !artMoved && !heartbeatDue) {
				continue;
			}

			lastLoggedRssKb = rssKb;
			lastLoggedArtBytes = artTxdBytes;
			lastLoggedArtSlots = artSlots;
			sinceLog = 0;

			if (pressure) {
				spdlog::warn("mem: rss={}MB avail={}MB artwork={}MB/{}slots/{}txd (PRESSURE)",
						rssKb / 1024, availKb / 1024,
						artTxdBytes / (1024 * 1024), artSlots, artTxds);
			} else {
				spdlog::info("mem: rss={}MB avail={}MB artwork={}MB/{}slots/{}txd",
						rssKb / 1024, availKb / 1024,
						artTxdBytes / (1024 * 1024), artSlots, artTxds);
				if (auto logger = spdlog::default_logger()) {
					logger->flush();
				}
			}
		}
	}).detach();
}

static void installFatalSignalHandlers()
{
	struct sigaction sig_action{};
	sig_action.sa_sigaction = [](int signum, siginfo_t* info, void* ctx) {
		static std::atomic_flag in_handler = ATOMIC_FLAG_INIT;
		if (in_handler.test_and_set()) {
			_exit(128 + signum);
		}
		crash_breadcrumb(signum, info, ctx);
		if (signum > 0 && signum < 32) {
			const struct sigaction& prev = s_prevFatalAction[signum];
			sigaction(signum, &prev, nullptr);
			if ((prev.sa_flags & SA_SIGINFO) && prev.sa_sigaction) {
				prev.sa_sigaction(signum, info, ctx);
				return;
			}
			if (prev.sa_handler != SIG_DFL && prev.sa_handler != SIG_IGN && prev.sa_handler != nullptr) {
				prev.sa_handler(signum);
				return;
			}
		}
		signal(signum, SIG_DFL);
	};
	sigemptyset(&sig_action.sa_mask);
	sig_action.sa_flags = SA_SIGINFO | SA_ONSTACK;
	for (int sig : {SIGSEGV, SIGBUS, SIGILL, SIGABRT, SIGFPE, SIGTRAP, SIGSYS}) {
		sigaction(sig, &sig_action, &s_prevFatalAction[sig]);
	}
}

void Client::initialize(const std::string& gameDir, bool offlineMode)
{
	// capture library load bases once so crash breadcrumbs can report pc as
	// an offset into libSAMPMOBILE/libGTASA (see crash_breadcrumb)
	dl_iterate_phdr([](struct dl_phdr_info* info, size_t, void*) -> int {
		if (info->dlpi_name && strstr(info->dlpi_name, "libSAMPMOBILE.so")) {
			g_sampLibBase = info->dlpi_addr;
			return 1;
		}
		return 0;
	}, nullptr);

	// SIGUSR2 = async-signal-safe thread identification breadcrumb only
	struct sigaction dump_action{};
	dump_action.sa_sigaction = [](int, siginfo_t*, void*) {
		char msg[128] = "Thread dump comm=";
		size_t pos = strlen(msg);
		int fd = open("/proc/self/comm", O_RDONLY);
		if (fd >= 0) {
			ssize_t n = read(fd, msg + pos, sizeof(msg) - pos - 1);
			if (n > 0) {
				pos += static_cast<size_t>(n);
				while (pos > 0 && (msg[pos - 1] == '\n' || msg[pos - 1] == '\r')) --pos;
			}
			close(fd);
		}
		msg[pos] = '\0';
		__android_log_write(ANDROID_LOG_WARN, "SAMPMOBILE", msg);
	};
	sigemptyset(&dump_action.sa_mask);
	dump_action.sa_flags = SA_SIGINFO | SA_ONSTACK;
	sigaction(SIGUSR2, &dump_action, nullptr);

	init_logger(gameDir);
	crash_set_report_dir(gameDir.c_str());

	// Install fatal handlers as early as possible: anything dying before this point
	// leaves no crash file at all. (Previously installed after Crashlytics init —
	// that whole middle section was a crash-file blind spot.)
	installFatalSignalHandlers();

	spdlog::info("Initializing SAMPMOBILE client...");
	bootMilestoneFlush();

	// Session liveness marker: written at startup, deleted only on intentional exit
	// (Java exitGame/onDestroy). A leftover means crash / LMK / vendor kill.
	{
		const std::string markerPath = gameDir + "logcat/session_alive";
		if (access(markerPath.c_str(), F_OK) == 0) {
			spdlog::error("last session did not exit cleanly (crash, LMK or vendor kill suspected)");
		}
		int mfd = open(markerPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (mfd >= 0) {
			char mbuf[64];
			int mlen = snprintf(mbuf, sizeof(mbuf), "pid=%d\n", getpid());
			if (mlen > 0) {
				ssize_t w = write(mfd, mbuf, static_cast<size_t>(mlen));
				(void) w;
			}
			close(mfd);
		}

		// Prune old crash report files, keeping the newest few (they are tiny
		// but unbounded accumulation would eventually spam the logcat dir).
		const std::string logcatDir = gameDir + "logcat";
		DIR* dir = opendir(logcatDir.c_str());
		if (dir) {
			std::vector<std::string> crashFiles;
			struct dirent* ent;
			while ((ent = readdir(dir)) != nullptr) {
				if (strncmp(ent->d_name, "crash_", 6) == 0) {
					crashFiles.emplace_back(ent->d_name);
				}
			}
			closedir(dir);
			if (crashFiles.size() > 5) {
				std::sort(crashFiles.begin(), crashFiles.end(), std::greater<>());
				for (size_t i = 5; i < crashFiles.size(); ++i) {
					unlink((logcatDir + "/" + crashFiles[i]).c_str());
				}
			}
		}
	}

	java_vm = g_vm;
	game_dir = gameDir;
	offline_mode = offlineMode;
	samp_version = "0.3.7";

	spdlog::info("Java VM: 0x{:X}", reinterpret_cast<uintptr_t>(java_vm));
	spdlog::info("Game version: {}", VER_x32 ? "GTA: SA v2.10 (armeabi-v7a)" : "GTA: SA v2.10 (arm64-v8a)");
	spdlog::info("Game directory: {}", game_dir);
	spdlog::info("Offline mode: {}", offline_mode);

	Settings::initialize();
	Encoding::setCharset(Settings::charset());

	// Pinpoint flush: if the process dies in Settings::initialize or below, the
	// buffered info lines above only reach disk once execution passes this point.
	bootMilestoneFlush();

	// Firebase Crashlytics NDK is intentionally NOT initialized: google-services.json
	// is absent from every build (tests/check.py enforces it), so reports have no
	// upload path and the native trampoline handler is pure dead weight — worse, its
	// low-level signal/stack manipulation is a device-compat liability (prime suspect
	// in the Android-16 early-boot death window). The project's own crash chain
	// (crash_* files + feedback bundle) replaced it. Restore only if firebase is ever
	// actually configured.
	startMemoryTelemetryThread();
	bootMilestoneFlush();

	samp_version = Settings::sampVersionName();
	samp_netgame = Settings::sampNetgameVersion();

	spdlog::info("SA-MP version: {} (0x{:X})", samp_version, samp_netgame);

	spdlog::info("Loading libGTASA.so...");
	g_saSym->Open("libGTASA.so");

	if (!g_saSym->GetLibAddr()) {
		spdlog::error("Failed to load libGTASA.so!");
		return;
	}

	spdlog::info("libGTASA.so loaded at 0x{:X}", g_saSym->GetLibAddr());
	g_gtasaLibBase = g_saSym->GetLibAddr();
	bootMilestoneFlush();

	if (!eng::resolveAll()) {
		spdlog::error("Aborting client initialization due to missing required libGTASA.so symbols!");
		return;
	}
	spdlog::info("libGTASA.so symbols resolved");
	bootMilestoneFlush();

	char str[100];
	snprintf(str, sizeof(str), "0x%llx", (unsigned long long) g_saSym->GetLibAddr());
	(void) str; // firebase crashlytics usage removed (see below)

	sa::Initialize();
	ClientSubsystems::onPreHooksInit(game_dir);

	Hooks::install();
	Patches::apply();
	spdlog::info("hooks and patches applied");
	bootMilestoneFlush();

	MultiTouch::initialize();
	patches::dumpToLog();

	pGame = new Game();

	ClientSubsystems::onPostBootInit();
	spdlog::info("game object constructed, boot handoff");
	bootMilestoneFlush();

	// Boot watchdog: every 5s log boot progress; if no frame was rendered
	// after ~15s (boot stall), dump all native game threads via SIGUSR2.
	// NOTE: ANDRunThread is the generic OS_ThreadLaunch trampoline — every
	// GTA worker thread passes through it, so g_gameThreadTid is only the
	// LAST spawned thread. The real render thread is comm="NVEvent".
	std::thread([]() {
		for (int i = 0;; ++i) {
			sleep(5);
			// Boot is over once GameMain marks worldReady; avoid reading GTA
			// globals/WORLD_PLAYERS from this background thread.
			bool worldReady = g_worldReady.load(std::memory_order_acquire);
			if (worldReady && g_processRan.load(std::memory_order_acquire)) {
				spdlog::info("watchdog: world ready, going quiet");
				return;
			}
			spdlog::info("watchdog: gameTid={}, processRan={}, worldReady={}",
					(int) g_gameThreadTid, g_processRan.load(), worldReady);
			if (i >= 2 && !worldReady && (i % 3) == 0) {
				spdlog::warn("watchdog: no rendered frame after ~{}s (check game data files & permissions)", (i + 1) * 5);
			}
		}
	}).detach();

}

void Client::process(bool canRender2d)
{
	static bool gameInited = false;
	static bool netgameInited = false;
	g_processRan = true;

	// StartGame must run on the game thread; calling it from a spawned
	// thread races with the game loop and crashes on some devices.
	// GetSymbol returns 0 on failure — never dereference unchecked.
	static uint32_t* pGameState = eng::as<uint32_t*>(eng::Sym::gGameState);
	if (!g_bGameStarted && pGame && pGameState && *pGameState == 7) {
		g_bGameStarted = true;
		Settings::applyToGtaEngine(false);
		pGame->StartGame();
	}

	// Everything below needs the loaded world: in the frontend/menu state the
	// local player ped doesn't exist yet (StartGame only schedules the load),
	// and CPlayerPool/CLocalPlayer construction would dereference null inside
	// the script command handlers. Wait until the ped shows up.
	if (!GamePool_FindPlayerPed()) {
		return;
	}
	g_worldReady.store(true, std::memory_order_release);

	if (!gameInited) {
		spdlog::info("Initializing game: core services...");
		pPlayerTags = new PlayerTags();
		pSnapShotHelper = new SnapShotHelper();
		pMaterialTextGenerator = new MaterialTextGenerator();

		spdlog::info("Initializing game: patches...");
		Patches::preApply();

		spdlog::info("Initializing game: game/subsystems...");
		pGame->Initialize();
		ClientSubsystems::onWorldReady();
		Settings::applyToGtaEngine(true);

		spdlog::info("Initializing game: ui...");
		if (pUI) {
			pUI->splashscreen()->setVisible(false);
			pUI->chat()->setVisible(true);
			pUI->deathwindow()->setVisible(Settings::showDeathWindow());
			pUI->buttonpanel()->setVisible(!offline_mode && Settings::showButtonPanel());
		}
		else {
			spdlog::error("gameInited: pUI is null");
		}

		if (g_java) {
			g_java->showLoadingScreen(false);
		}
		else {
			spdlog::error("gameInited: g_java is null");
		}

		ClientCmds::initialize();

		if (offline_mode) {
			if (pUI) {
				if (pUI->buttonpanel()) pUI->buttonpanel()->setVisible(false);
				if (pUI->chat()) {
					pUI->chat()->addInfoMessage(L10N("-> GTA: SA %s started", "-> GTA: SA %s 已启动"), VER_x32 ? "v2.10 (armeabi-v7a)" : "v2.10 (arm64-v8a)");
					pUI->chat()->addInfoMessage(L10N("-> Type /j to start jetpack", "-> 输入 /j 开启喷气背包"));
					pUI->chat()->addInfoMessage(L10N("-> Click on map to teleport anywhere", "-> 点击地图可传送到任意位置"));
				}
			}

			pGame->ToggleCJWalk(false); // so we can restore nop
			pGame->ToggleCJWalk(true);
			pGame->SetWorldTime(12, 0);
			pGame->AddToLocalMoney(100000000);
			pGame->GetCamera()->Restore();
			pGame->GetCamera()->SetBehindPlayer();
			if (CPlayerPed* pLocal = pGame->FindPlayerPed()) {
				pLocal->SetModelIndex(271);
				pLocal->SetHealth(1000.0f);
				pLocal->SetArmour(1000.0f);

				if (pLocal->m_ped) {
					RwMatrix mat = pLocal->m_ped->GetMatrix().ToRwMatrix();
					mat.pos = {2090.6255, 1164.5679, 11.6484};
					pLocal->m_ped->SetMatrix(mat);
					pLocal->SetTargetRotation(72.8293);
					pGame->NewVehicle(411, mat.pos.x, mat.pos.y + 5.0f, mat.pos.z, 0.0f, false);
				}
			}

			uintptr_t* widgets = GetTouchWidgets();
			if (!widgets || !widgets[sa::WidgetIDs::WIDGET_PED_MOVE] || !widgets[sa::WidgetIDs::WIDGET_ATTACK]) {
				spdlog::info("Widgets not inited, calling Menu_SwitchOffToGame");
				eng::fn::Menu_SwitchOffToGame();
			}
			EnsureTouchWidgetsCreated();
		}

		gameInited = true;
	}

	if (!netgameInited && !offline_mode) {
		spdlog::info("Initializing netgame...");
		NetGame* netGame = new NetGame(Settings::ip(), Settings::port(), Settings::nick(), Settings::pass());
		std::atomic_thread_fence(std::memory_order_release);
		pNetGame = netGame;
		netgameInited = true;
	}

	// Drain queued JNI inputs (keyboard chat, dialog responses, hardware keys) on GameMain.
	Java::processPendingInputs();

	// omp-cef compatible browser + transport. Kept outside the pNetGame check so
	// the debug direct-connect path (tools/cef_kat/run_browser_test.sh) works in
	// offline mode; Tick() itself resets when SA-MP is not connected.
	CefBrowser::Get().Install();
	// Run JNI debug entry points that arrived on the Android main thread, on
	// this (game) thread, before anything else touches CEF state this frame.
	CefBrowser::Get().DrainGameThreadQueue();
	CefSession::Get().Tick();
	// World2D: project every world-anchored page to screen space once per frame,
	// on the game thread (the same thread the PC client projects from).
	CefBrowser::Get().TickWorld2D();
	// WorldObject3D: pull one frame per attached page and upload it into the
	// RenderWare texture the object material is pointing at.
	CefBrowser::Get().TickWorldObject3D();

	if (pNetGame) {
		pNetGame->Process();

		if (CMenuPool* pMenuPool = pNetGame->GetMenuPool()) {
			pMenuPool->processPendingTouches();
		}

		if (CObjectPool* pObjectPool = pNetGame->GetObjectPool()) {
			pObjectPool->processPendingTouches();
		}

		CTextDrawPool* pTextDrawPool = pNetGame->GetTextDrawPool();
		if (pTextDrawPool) {
			pTextDrawPool->processPendingTouches();
			if (canRender2d && !Game::IsGamePaused() && (!pUI || !pUI->scoreboard() || !pUI->scoreboard()->visible())) {
				pTextDrawPool->Draw();
			}
		}

		if (g_java && !Game::IsGamePaused()) {
			g_java->showWantedLevel(true);
		}
	}

	ClientSubsystems::onGameFrame(pNetGame != nullptr);
}

void Client::initializeUI()
{
	ClientSubsystems::onUiDeviceReady();

	std::string fonts_path = std::string(Client::gameDir()) + "SAMP/fonts/";
	UI* ui = new UI(ImVec2(sa::RsGlobal->screenWidth, sa::RsGlobal->screenHeight), fonts_path);
	ui->initialize();
	ui->performLayout();
	ui->updateTouchSnapshot();
	std::atomic_thread_fence(std::memory_order_release);
	pUI = ui;
}

jint JNI_OnLoad(JavaVM* vm, [[maybe_unused]] void* reserved)
{
	__android_log_print(ANDROID_LOG_INFO, "SAMPMOBILE", "SAMPMOBILE library loaded! Build time: " __DATE__ " " __TIME__);

	JNIEnv* env;
	if (vm->GetEnv((void**) &env, JNI_VERSION_1_6) != JNI_OK) {
		return JNI_ERR;
	}

	g_vm = vm;

	// Resolve app-side classes here: threads attached later via
	// AttachCurrentThread use the bootstrap loader and cannot FindClass them.
	CefBrowser::OnJniLoad(env);

	return env->GetVersion();
}
