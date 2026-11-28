#include "CleoInternal.h"

namespace cleo_internal {

bool g_enabled = false;
bool g_symbolsReady = false;
bool g_scriptsBooted = false;
std::string g_gameDir;
std::string g_cleoDir;
std::string g_currentDir;
std::string g_logPath;
FILE* g_logFile = nullptr;
std::mutex g_logMutex;

uintptr_t g_gtasaBase = 0;
uint32_t* g_scriptParams = nullptr;
uint8_t* g_scriptSpace = nullptr;
uint32_t* g_timerMs = nullptr;
void* g_theText = nullptr;

FnInitScript g_fnInitScript = nullptr;
FnProcessOneCmd g_fnProcessOneCmd = nullptr;
FnCollectParams g_fnCollectParams = nullptr;
FnStoreParams g_fnStoreParams = nullptr;
FnUpdateCmpFlag g_fnUpdateCmpFlag = nullptr;
FnGetVarPtr g_fnGetVarPtr = nullptr;
FnAsciiToGxt g_fnAsciiToGxt = nullptr;
FnSetHelpMsg g_fnSetHelpMsg = nullptr;
FnAddMessage g_fnAddMessage = nullptr;
FnAddMessage g_fnAddMessageJumpQ = nullptr;
FnAddBigMessage g_fnAddBigMessage = nullptr;
FnCTextGet g_fnCTextGet = nullptr;

std::vector<CleoScript> g_scripts;
std::unordered_map<std::string, std::vector<uint16_t>> g_customGxt;
std::mutex g_gxtMutex;

std::unordered_map<uint32_t, uint32_t> g_mutexVars;
std::array<uint32_t, kMaxSharedVars> g_sharedVars{};

std::unordered_map<uint32_t, PtrHandleEntry> g_ptrHandles;
uint32_t g_nextHandle = kHandleBase;

std::unordered_map<uint32_t, FILE*> g_openFiles;
uint32_t g_nextFileHandle = 0x5F000001u;

uint32_t g_lastTickMs = 0;

std::mutex g_touchMutex;
TouchFinger g_fingers[8]{};
SlideRecord g_lastSlide{};
bool g_menuBtnDown = false;
uint32_t g_menuBtnStart = 0;
float g_screenW = 1920.0f;
float g_screenH = 1080.0f;

ScriptMenuState g_scriptMenu{};
BuiltinMenuState g_builtinMenu{};
int g_pendingStartScriptIdx = -1;

// Hitboxes refreshed each rendered frame for accurate touch selection
RectF g_menuPanelRect{};
RectF g_menuCloseRect{};
RectF g_menuPrevRect{};
RectF g_menuNextRect{};
std::vector<std::pair<int, RectF>> g_menuRowRects;

// Ring buffer for temporary GXT messages (0ACA..0ACD)
uint16_t g_msgGxtBuf[4][512]{};
int g_msgGxtSlot = 0;

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------
void cleoLog(const char* fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	spdlog::info("[CLEO] {}", buf);
	std::lock_guard<std::mutex> lock(g_logMutex);
	if (g_logFile) {
		uint32_t ms = g_timerMs ? *g_timerMs : 0;
		fprintf(g_logFile, "[%u] %s\n", ms, buf);
		fflush(g_logFile);
	}
}

uint32_t nowMs()
{
	return g_timerMs ? *g_timerMs : 0;
}

std::string toUpperAscii(const std::string& s)
{
	std::string out = s;
	for (char& c : out) {
		c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
	}
	return out;
}

std::string toLowerAscii(const std::string& s)
{
	std::string out = s;
	for (char& c : out) {
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	return out;
}

bool endsWithIgnoreCase(const std::string& s, const std::string& suffix)
{
	if (s.size() < suffix.size()) return false;
	return toLowerAscii(s.substr(s.size() - suffix.size())) == toLowerAscii(suffix);
}

std::vector<uint16_t> asciiToWide(const std::string& s)
{
	std::vector<uint16_t> out;
	out.reserve(s.size() + 1);
	for (unsigned char c : s) {
		out.push_back(static_cast<uint16_t>(c));
	}
	out.push_back(0);
	return out;
}

std::string wideGxtToUtf8(const uint16_t* w)
{
	if (!w) return {};
	std::string out;
	for (size_t i = 0; i < 512 && w[i] != 0; ++i) {
		uint16_t ch = w[i];
		if (ch < 0x80) {
			out.push_back(static_cast<char>(ch));
		} else if (ch < 0x800) {
			out.push_back(static_cast<char>(0xC0 | (ch >> 6)));
			out.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
		} else {
			out.push_back(static_cast<char>(0xE0 | (ch >> 12)));
			out.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
		}
	}
	return out;
}

// ---------------------------------------------------------------------------
// 32-bit Pointer Handle Registry (safe across arm64 and arm32)
// ---------------------------------------------------------------------------
uint32_t registerHandle(void* ptr, size_t size, bool owned)
{
	if (!ptr) return 0;
	if (!owned) {
		for (const auto& kv : g_ptrHandles) {
			if (!kv.second.owned && kv.second.ptr == ptr) {
				return kv.first;
			}
		}
	}
	uint32_t h = g_nextHandle++;
	if (g_nextHandle < kHandleBase) g_nextHandle = kHandleBase;
	g_ptrHandles[h] = PtrHandleEntry{ptr, size, owned};
	return h;
}

void* resolveHandleOrAddr(uint32_t addr, bool addImageBase, size_t* outSize)
{
	if (outSize) *outSize = 0;
	if (!addImageBase) {
		auto it = g_ptrHandles.find(addr);
		if (it != g_ptrHandles.end()) {
			if (outSize) *outSize = it->second.size;
			return it->second.ptr;
		}
	}
	if (addImageBase) {
		return reinterpret_cast<void*>(g_gtasaBase + static_cast<uintptr_t>(addr));
	}
#if defined(__arm__)
	return reinterpret_cast<void*>(static_cast<uintptr_t>(addr));
#else
	return nullptr;
#endif
}

bool readBoundedCStr(const char*& cursor, const char* end, std::string& out, size_t maxLen)
{
	out.clear();
	if (!cursor || cursor >= end) return false;
	const char* limit = std::min(end, cursor + maxLen);
	const void* nul = memchr(cursor, '\0', static_cast<size_t>(limit - cursor));
	if (!nul) {
		out.assign(cursor, static_cast<size_t>(limit - cursor));
		cursor = limit;
		return false;
	}
	const char* nulPos = static_cast<const char*>(nul);
	out.assign(cursor, static_cast<size_t>(nulPos - cursor));
	cursor = nulPos + 1;
	return true;
}

void resetTouchFingers()
{
	for (auto& f : g_fingers) {
		f = TouchFinger{};
	}
}

// ---------------------------------------------------------------------------
// FXT Loader & GXT Lookup
// ---------------------------------------------------------------------------
void addCustomGxtEntry(const std::string& rawKey, const std::string& value)
{
	std::string key = toUpperAscii(rawKey);
	if (key.empty()) return;
	std::lock_guard<std::mutex> lock(g_gxtMutex);
	g_customGxt[key] = asciiToWide(value);
}

void removeCustomGxtEntry(const std::string& rawKey)
{
	std::string key = toUpperAscii(rawKey);
	std::lock_guard<std::mutex> lock(g_gxtMutex);
	g_customGxt.erase(key);
}

void loadFxtFile(const std::string& path)
{
	std::ifstream f(path);
	if (!f.is_open()) return;

	int count = 0;
	std::string line;
	while (std::getline(f, line)) {
		while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
			line.pop_back();
		}
		// Skip UTF-8 BOM if present
		if (line.size() >= 3 &&
		    static_cast<unsigned char>(line[0]) == 0xEF &&
		    static_cast<unsigned char>(line[1]) == 0xBB &&
		    static_cast<unsigned char>(line[2]) == 0xBF) {
			line.erase(0, 3);
		}
		size_t start = 0;
		while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) ++start;
		if (start >= line.size() || line[start] == '#' ||
		    (line[start] == '/' && start + 1 < line.size() && line[start + 1] == '/')) {
			continue;
		}
		size_t sep = start;
		while (sep < line.size() && line[sep] != ' ' && line[sep] != '\t') ++sep;
		if (sep >= line.size()) continue;
		std::string key = line.substr(start, sep - start);
		size_t valStart = sep;
		while (valStart < line.size() && (line[valStart] == ' ' || line[valStart] == '\t')) ++valStart;
		if (valStart >= line.size()) continue;
		std::string val = line.substr(valStart);
		addCustomGxtEntry(key, val);
		++count;
	}
	cleoLog("Loaded FXT '%s' (%d entries)", path.c_str(), count);
}

std::string lookupGxtUtf8(const std::string& keyOrLiteral)
{
	if (uint16_t* custom = CleoEngine::getCustomGxt(keyOrLiteral.c_str())) {
		return wideGxtToUtf8(custom);
	}
	if (g_fnCTextGet && g_theText) {
		if (uint16_t* native = g_fnCTextGet(g_theText, keyOrLiteral.c_str())) {
			std::string s = wideGxtToUtf8(native);
			if (!s.empty() && s.find("missing") == std::string::npos) {
				return s;
			}
		}
	}
	return keyOrLiteral;
}

// ---------------------------------------------------------------------------
// Script Discovery & Initialization
// ---------------------------------------------------------------------------
std::string makeDisplayName(const std::string& fileName)
{
	std::string name = fileName;
	size_t dot = name.rfind('.');
	if (dot != std::string::npos) name = name.substr(0, dot);
	// Strip common "gtasa." prefix for cleaner CLEO menu display
	if (name.size() > 6 && toLowerAscii(name.substr(0, 6)) == "gtasa.") {
		name = name.substr(6);
	}
	return name;
}

void scanDirectory(const std::string& dirPath, bool scanScripts)
{
	DIR* d = opendir(dirPath.c_str());
	if (!d) return;

	std::vector<std::string> names;
	while (struct dirent* ent = readdir(d)) {
		if (ent->d_name[0] == '\0' || ent->d_name[0] == '.') continue;
		names.emplace_back(ent->d_name);
	}
	closedir(d);
	std::sort(names.begin(), names.end());

	for (const std::string& name : names) {
		if (endsWithIgnoreCase(name, ".disabled")) continue;
		std::string full = dirPath + name;
		struct stat st{};
		if (stat(full.c_str(), &st) != 0 || S_ISDIR(st.st_mode)) continue;

		if (endsWithIgnoreCase(name, ".fxt")) {
			loadFxtFile(full);
			continue;
		}

		if (!scanScripts) continue;

		bool isAuto = endsWithIgnoreCase(name, ".csa") || endsWithIgnoreCase(name, ".cs");
		bool isInvokable = endsWithIgnoreCase(name, ".csi");
		if (!isAuto && !isInvokable) continue;

		if (st.st_size < 2 || st.st_size > 8 * 1024 * 1024) {
			cleoLog("Skip script '%s' (invalid size %lld)", name.c_str(), (long long) st.st_size);
			continue;
		}

		std::ifstream in(full, std::ios::binary);
		if (!in.is_open()) continue;

		CleoScript sc{};
		sc.fileName = name;
		sc.displayName = makeDisplayName(name);
		sc.code.resize(static_cast<size_t>(st.st_size));
		in.read(reinterpret_cast<char*>(sc.code.data()), st.st_size);
		sc.invokable = isInvokable;
		if (isInvokable) {
			int32_t nextId = 0;
			for (const auto& existing : g_scripts) {
				if (existing.invokable) ++nextId;
			}
			sc.invokableId = nextId;
		}
		g_scripts.push_back(std::move(sc));
		cleoLog("Queued %s script '%s' (%zu bytes)",
		        isInvokable ? "invokable (.csi)" : "auto (.csa/.cs)",
		        name.c_str(),
		        g_scripts.back().code.size());
	}
}

void startOrRestartScript(CleoScript& sc, bool preserveLocals, uint32_t initialWaitMs)
{
	uint32_t savedLocals[42]{};
	if (preserveLocals && sc.startedOnce) {
		memcpy(savedLocals, sc.thread.m_nLocals, sizeof(savedLocals));
	}

	memset(&sc.thread, 0, sizeof(sc.thread));
	if (g_fnInitScript) {
		g_fnInitScript(&sc.thread);
	}

	if (preserveLocals && sc.startedOnce) {
		memcpy(sc.thread.m_nLocals, savedLocals, sizeof(savedLocals));
	}

	std::string shortName = sc.displayName.substr(0, 7);
	snprintf(sc.thread.m_szScriptName, sizeof(sc.thread.m_szScriptName), "%s", shortName.c_str());
	sc.thread.m_pBaseAddressOfThisScript = sc.code.data();
	sc.thread.m_pPCPointer = sc.code.data();
	sc.thread.m_nStackDepth = 0;
	sc.thread.m_bActive = 1;
	sc.thread.m_bCmpFlag = 0;
	sc.thread.m_nAndOrState = 0;
	sc.thread.m_bNotForLatestExpression = 0;
	sc.thread.m_nActivateTime = static_cast<int32_t>(nowMs() + initialWaitMs);
	sc.startedOnce = true;
	sc.waitPassed = (initialWaitMs == 0);
	sc.callFrames.clear();

	cleoLog("Started script '%s' (invokable=%d, preserveLocals=%d)",
	        sc.fileName.c_str(), sc.invokable ? 1 : 0, preserveLocals ? 1 : 0);
}

// ---------------------------------------------------------------------------
// Touch Zone Helpers (3x3 screen grid, CLEO Android standard 1..9)
// ---------------------------------------------------------------------------
int calcTouchZone(float x, float y, float w, float h)
{
	if (w <= 1.0f || h <= 1.0f) return 0;
	int col = static_cast<int>(x * 3.0f / w);
	int row = static_cast<int>(y * 3.0f / h);
	col = std::clamp(col, 0, 2);
	row = std::clamp(row, 0, 2);
	return row * 3 + col + 1; // 1..9
}

bool isZoneTouchedTimed(int zone, uint32_t minTimeMs)
{
	std::lock_guard<std::mutex> lock(g_touchMutex);
	uint32_t t = nowMs();
	for (const auto& f : g_fingers) {
		if (f.down && f.currentZone == zone) {
			if (minTimeMs == 0 || (t >= f.lastZoneTime && (t - f.lastZoneTime) >= minTimeMs)) {
				return true;
			}
		}
	}
	return false;
}

bool isAnyFingerDown()
{
	std::lock_guard<std::mutex> lock(g_touchMutex);
	for (const auto& f : g_fingers) {
		if (f.down) return true;
	}
	return false;
}

bool checkSlideDone(int fromZone, int toZone, uint32_t minMs, uint32_t maxMs)
{
	std::lock_guard<std::mutex> lock(g_touchMutex);
	uint32_t t = nowMs();
	if (g_lastSlide.fromZone == fromZone && g_lastSlide.toZone == toZone &&
	    g_lastSlide.finishTime > 0 && t >= g_lastSlide.finishTime && (t - g_lastSlide.finishTime) <= 350 &&
	    g_lastSlide.duration >= minMs && (maxMs == 0 || g_lastSlide.duration <= maxMs)) {
		g_lastSlide.finishTime = 0; // consume once
		return true;
	}
	return false;
}

// ---------------------------------------------------------------------------
// SCM Parameter Helpers
// ---------------------------------------------------------------------------

void stepScript(CleoScript& sc, uint32_t currentTime)
{
	if (!sc.thread.m_bActive) return;
	if (currentTime < static_cast<uint32_t>(sc.thread.m_nActivateTime)) return;
	sc.waitPassed = true;

	uint8_t* codeStart = sc.code.data();
	uint8_t* codeEnd = codeStart + sc.code.size();

	for (int step = 0; step < kMaxStepsPerTick && sc.thread.m_bActive; ++step) {
		uint8_t* ip = sc.thread.m_pPCPointer;
		if (ip < codeStart || ip + 2 > codeEnd) {
			cleoLog("Script '%s': IP out of bounds (%td / %zu), terminating",
			        sc.fileName.c_str(), ip - codeStart, sc.code.size());
			sc.thread.m_bActive = 0;
			break;
		}

		uint16_t rawOp = 0;
		memcpy(&rawOp, ip, 2);

		// CLEO Android 3x3 touch-zone shortcut via opcode 00E1 / 80E1 with pad=3
		if ((rawOp == 0x00E1 || rawOp == 0x80E1) && ip + 6 <= codeEnd) {
			uint16_t p1 = 0;
			memcpy(&p1, ip + 2, 2);
			if (p1 == 0x0304 && ip[4] == 0x04) {
				int zone = ip[5];
				bool touched = isZoneTouchedTimed(zone, 0);
				sc.thread.m_bNotForLatestExpression = (rawOp == 0x80E1) ? 1 : 0;
				g_fnUpdateCmpFlag(&sc.thread, touched ? 1 : 0);
				sc.thread.m_pPCPointer = ip + 6;
				continue;
			}
		}

		// Try custom CLEO opcode first (advance past 2-byte opcode)
		sc.thread.m_pPCPointer = ip + 2;
		if (handleCustomOpcode(sc, rawOp)) {
			continue;
		}

		// Standard GTA:SA 2.10 CRunningScript::ProcessOneCommand only dispatches opcodes 0..2699 (0x0000..0x0A8B).
		// Reject any unhandled CLEO4/CLEO5/SAMPFUNCS opcode (> 2699) cleanly instead of risking native OOB crash.
		const uint16_t maskedOp = rawOp & 0x7FFFu;
		if (maskedOp > 2699u) {
			cleoLog("Script '%s': unsupported custom opcode 0x%04X at offset %td, terminating script safely",
			        sc.fileName.c_str(), maskedOp, ip - codeStart);
			sc.thread.m_bActive = 0;
			break;
		}

		// Rewind and delegate standard GTA:SA opcode to native CRunningScript::ProcessOneCommand
		sc.thread.m_pPCPointer = ip;
		uint8_t yielded = g_fnProcessOneCmd(&sc.thread);
		if (yielded != 0) {
			break;
		}
	}
}

void syncPlayerScriptGlobals()
{
	if (!g_scriptSpace) return;
	sa::CPed* localPed = GamePool_FindPlayerPed();
	if (!localPed) return;

	// $PLAYER_CHAR (+0x08) = 0, $PLAYER_ACTOR (+0x0C) = local player ped handle
	int32_t playerChar = 0;
	int32_t playerActor = GamePool_Ped_GetIndex(localPed);
	memcpy(g_scriptSpace + 0x08, &playerChar, 4);
	memcpy(g_scriptSpace + 0x0C, &playerActor, 4);
}

// ---------------------------------------------------------------------------
// Menu Helpers (Built-in .csi Launcher & Script 0DF2 Menu)
// ---------------------------------------------------------------------------

} // namespace cleo_internal

using namespace cleo_internal;

void CleoEngine::initialize(const std::string& gameDir)
{
	// Clean up any previous session state before (re)initializing
	{
		std::lock_guard<std::mutex> lock(g_logMutex);
		if (g_logFile) {
			fclose(g_logFile);
			g_logFile = nullptr;
		}
	}
	for (auto& kv : g_openFiles) {
		if (kv.second) fclose(kv.second);
	}
	g_openFiles.clear();
	for (auto& kv : g_ptrHandles) {
		if (kv.second.owned && kv.second.ptr) free(kv.second.ptr);
	}
	g_ptrHandles.clear();
	g_nextHandle = kHandleBase;
	g_nextFileHandle = 0x5F000001u;
	{
		std::lock_guard<std::mutex> lock(g_gxtMutex);
		g_customGxt.clear();
	}
	g_mutexVars.clear();
	g_sharedVars.fill(0);
	{
		std::lock_guard<std::mutex> lock(g_touchMutex);
		g_scriptMenu = ScriptMenuState{};
		g_builtinMenu = BuiltinMenuState{};
		g_menuRowRects.clear();
		g_pendingStartScriptIdx = -1;
		resetTouchFingers();
		g_lastSlide = SlideRecord{};
	}

	g_enabled = Settings::cleo();
	g_scripts.clear();
	g_scriptsBooted = false;
	g_gameDir = gameDir;
	if (!g_gameDir.empty() && g_gameDir.back() != '/') g_gameDir.push_back('/');
	g_cleoDir = g_gameDir + "cleo/";
	g_currentDir = g_cleoDir;

	if (!g_enabled) {
		spdlog::info("[CLEO] Disabled by settings (cleo_scripts=false)");
		return;
	}

	mkdir(g_cleoDir.c_str(), 0775);
	g_logPath = g_cleoDir + "cleo.log";
	{
		std::lock_guard<std::mutex> lock(g_logMutex);
		g_logFile = fopen(g_logPath.c_str(), "w");
	}

	g_gtasaBase = g_saSym->GetLibAddr();
	g_scriptParams = eng::as<uint32_t*>(eng::Sym::ScriptParams);
	g_scriptSpace = eng::as<uint8_t*>(eng::Sym::CTheScripts_ScriptSpace);
	g_timerMs = eng::as<uint32_t*>(eng::Sym::CTimer_m_snTimeInMilliseconds);
	g_theText = eng::ptr(eng::Sym::TheText);

	g_fnInitScript = eng::as<FnInitScript>(eng::Sym::CRunningScript_Init);
	g_fnProcessOneCmd = eng::as<FnProcessOneCmd>(eng::Sym::CRunningScript_ProcessOneCommand);
	g_fnCollectParams = eng::as<FnCollectParams>(eng::Sym::CRunningScript_CollectParameters);
	g_fnStoreParams = eng::as<FnStoreParams>(eng::Sym::CRunningScript_StoreParameters);
	g_fnUpdateCmpFlag = eng::as<FnUpdateCmpFlag>(eng::Sym::CRunningScript_UpdateCompareFlag);
	g_fnGetVarPtr = eng::as<FnGetVarPtr>(eng::Sym::CRunningScript_GetPointerToScriptVariable);
	g_fnAsciiToGxt = eng::as<FnAsciiToGxt>(eng::Sym::AsciiToGxtChar);
	g_fnSetHelpMsg = eng::as<FnSetHelpMsg>(eng::Sym::CHud_SetHelpMessage);
	g_fnAddMessage = eng::as<FnAddMessage>(eng::Sym::CMessages_AddMessage);
	g_fnAddMessageJumpQ = eng::as<FnAddMessage>(eng::Sym::CMessages_AddMessageJumpQ);
	g_fnAddBigMessage = eng::as<FnAddBigMessage>(eng::Sym::CMessages_AddBigMessage);
	g_fnCTextGet = eng::as<FnCTextGet>(eng::Sym::CText_Get);

	g_symbolsReady = (g_scriptParams && g_scriptSpace && g_timerMs &&
	                  g_fnInitScript && g_fnProcessOneCmd && g_fnCollectParams &&
	                  g_fnStoreParams && g_fnUpdateCmpFlag && g_fnGetVarPtr);

	cleoLog("SAMP-Mobile Native CLEO Engine initialized (%s, GTA:SA 2.10)",
	        VER_x32 ? "armeabi-v7a" : "arm64-v8a");
	if (!g_symbolsReady) {
		cleoLog("ERROR: Required libGTASA.so script symbols missing; CLEO runtime paused.");
		g_enabled = false;
		return;
	}

	// Scan files/cleo/ (scripts + .fxt) and files/cleo/cleo_text/ (.fxt only)
	scanDirectory(g_cleoDir, true);
	scanDirectory(g_cleoDir + "cleo_text/", false);

	size_t autoCount = 0, invCount = 0;
	for (const auto& s : g_scripts) {
		if (s.invokable) ++invCount;
		else ++autoCount;
	}
	cleoLog("Scan complete: %zu auto (.csa/.cs), %zu invokable (.csi), %zu custom GXT entries",
	        autoCount, invCount, g_customGxt.size());
}

bool CleoEngine::isEnabled()
{
	return g_enabled && g_symbolsReady;
}

void CleoEngine::process()
{
	if (!g_enabled || !g_symbolsReady) return;
	if (Game::IsGamePaused()) {
		std::lock_guard<std::mutex> lock(g_touchMutex);
		resetTouchFingers();
		return;
	}
	if (!GamePool_FindPlayerPed()) return;

	uint32_t t = nowMs();
	int startIdx = -1;
	{
		std::lock_guard<std::mutex> lock(g_touchMutex);
		// Expire any stuck touch finger if a POP event was lost across background/dialog transitions
		for (auto& f : g_fingers) {
			if (f.down && t > f.lastZoneTime && (t - f.lastZoneTime) > 15000u) {
				f = TouchFinger{};
			}
		}
		startIdx = g_pendingStartScriptIdx;
		g_pendingStartScriptIdx = -1;
	}
	if (startIdx >= 0 && startIdx < static_cast<int>(g_scripts.size())) {
		CleoScript& sc = g_scripts[startIdx];
		if (!sc.thread.m_bActive) {
			startOrRestartScript(sc, sc.startedOnce, 0);
		}
	}
	if (!g_scriptsBooted) {
		g_scriptsBooted = true;
		g_lastTickMs = t;
		syncPlayerScriptGlobals();
		for (auto& sc : g_scripts) {
			if (!sc.invokable) {
				startOrRestartScript(sc, false, 1500);
			}
		}
		cleoLog("World ready — booted auto CLEO scripts (timer=%u)", t);
		return;
	}

	uint32_t delta = (t >= g_lastTickMs) ? std::min<uint32_t>(t - g_lastTickMs, 250) : 0;
	g_lastTickMs = t;

	syncPlayerScriptGlobals();

	for (auto& sc : g_scripts) {
		if (!sc.thread.m_bActive) continue;
		sc.thread.m_nLocals[32] += delta;
		sc.thread.m_nLocals[33] += delta;
		stepScript(sc, t);
	}
}

uint16_t* CleoEngine::getCustomGxt(const char* key)
{
	if (!g_enabled || !key || !key[0]) return nullptr;
	std::string upper = toUpperAscii(key);
	std::lock_guard<std::mutex> lock(g_gxtMutex);
	auto it = g_customGxt.find(upper);
	if (it != g_customGxt.end() && !it->second.empty()) {
		return it->second.data();
	}
	return nullptr;
}

bool CleoEngine::handleTouch(int type, int num, int x, int y, float screenW, float screenH)
{
	if (!g_enabled || !g_symbolsReady) return false;
	std::lock_guard<std::mutex> lock(g_touchMutex);
	if (screenW > 1.0f && screenH > 1.0f) {
		g_screenW = screenW;
		g_screenH = screenH;
	}

	const float fx = static_cast<float>(x);
	const float fy = static_cast<float>(y);
	const uint32_t t = nowMs();

	// 1) If a CLEO menu overlay is visible, consume touches on it
	if (isAnyMenuVisible()) {
		if (type == 1) { // POP (tap release)
			if (g_menuCloseRect.contains(fx, fy) || !g_menuPanelRect.contains(fx, fy)) {
				if (g_scriptMenu.active) {
					g_scriptMenu.lastTouchedIndex = -2;
					g_scriptMenu.lastTouchedTime = t;
					g_scriptMenu.active = false;
				} else {
					g_builtinMenu.open = false;
				}
				g_menuRowRects.clear();
				return true;
			}
			if (g_menuPrevRect.contains(fx, fy)) {
				if (g_scriptMenu.active && g_scriptMenu.page > 0) --g_scriptMenu.page;
				else if (g_builtinMenu.open && g_builtinMenu.page > 0) --g_builtinMenu.page;
				return true;
			}
			if (g_menuNextRect.contains(fx, fy)) {
				if (g_scriptMenu.active) {
					int maxPage = std::max(0, static_cast<int>(g_scriptMenu.items.size() - 1) / kMenuItemsPerPage);
					if (g_scriptMenu.page < maxPage) ++g_scriptMenu.page;
				} else if (g_builtinMenu.open) {
					auto inv = collectInvokableScriptIndices();
					int maxPage = inv.empty() ? 0 : static_cast<int>(inv.size() - 1) / kMenuItemsPerPage;
					if (g_builtinMenu.page < maxPage) ++g_builtinMenu.page;
				}
				return true;
			}
			for (const auto& entry : g_menuRowRects) {
				if (entry.second.contains(fx, fy)) {
					if (g_scriptMenu.active) {
						g_scriptMenu.activeIndex = entry.first;
						g_scriptMenu.lastTouchedIndex = entry.first;
						g_scriptMenu.lastTouchedTime = t;
					} else if (g_builtinMenu.open) {
						int idx = entry.first;
						if (idx >= 0 && idx < static_cast<int>(g_scripts.size())) {
							// Defer startOrRestartScript to GameMain::CleoEngine::process() (never call GTA CRunningScript::Init on NVEvent thread!)
							g_pendingStartScriptIdx = idx;
							g_builtinMenu.open = false;
							g_menuRowRects.clear();
						}
					}
					return true;
				}
			}
		}
		return true;
	}

	// 2) Track 3x3 CLEO touch zones & swipe gestures (2 -> 8 opens CLEO .csi menu)
	if (type == 4) { // ACTION_CANCEL resets all fingers so none stay stuck
		resetTouchFingers();
		return false;
	}
	if (num >= 0 && num < 8) {
		int zone = calcTouchZone(fx, fy, g_screenW, g_screenH);
		TouchFinger& f = g_fingers[num];
		if (type == 2) { // PUSH
			f.down = true;
			f.startZone = zone;
			f.currentZone = zone;
			f.startTime = t;
			f.lastZoneTime = t;
		} else if (type == 3) { // MOVE
			if (!f.down) {
				f.down = true;
				f.startZone = zone;
				f.startTime = t;
			}
			if (zone != f.currentZone) {
				f.currentZone = zone;
				f.lastZoneTime = t;
			}
		} else if (type == 1 || type == 4) { // POP / CANCEL
			if (f.down && f.startZone > 0 && zone > 0 && f.startZone != zone) {
				uint32_t dur = (t >= f.startTime) ? (t - f.startTime) : 0;
				g_lastSlide = SlideRecord{f.startZone, zone, dur, t};

				// Classic Android CLEO swipe gesture: top-center (2) -> bottom-center (8)
				if (f.startZone == 2 && zone == 8 && dur >= 100 && dur <= 1200 && !g_scripts.empty()) {
					g_builtinMenu.open = true;
					g_builtinMenu.page = 0;
					cleoLog("Opened CLEO menu via swipe gesture (2->8, %ums)", dur);
				}
			}
			f.down = false;
			f.currentZone = 0;
		}
	}

	return false;
}
