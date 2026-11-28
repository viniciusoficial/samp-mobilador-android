#pragma once

#include "../CleoEngine.h"
#include "../../Client.h"
#include "../../Settings.h"
#include "../../Memory/include/Memory.h"
#include "../../GameSA/include/Scripts/RunningScript.h"
#include "../../UI/ImGuiRenderer.h"
#include "../Game.h"
#include "../Util.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <dlfcn.h>
#include <fstream>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <unordered_map>
#include <vector>

#include <spdlog/spdlog.h>

namespace cleo_internal {

constexpr uint32_t kCleoVerGtaSa200Plus = 17; // VER_GTASA_2_00_OR_HIGHER
constexpr uint32_t kHandleBase = 0x7E000001u;
constexpr int kMaxStepsPerTick = 4096;
constexpr int kMaxScmCallDepth = 32;
constexpr int kMaxSharedVars = 1024;
constexpr int kMenuItemsPerPage = 10;

struct ScmCallFrame {
	uint8_t* returnIp = nullptr;
	uint32_t savedLocals[32]{};
};

struct CleoScript {
	std::string fileName;
	std::string displayName;
	std::vector<uint8_t> code;
	sa::CRunningScript thread{};
	bool invokable = false;
	int32_t invokableId = -1;
	bool startedOnce = false;
	bool waitPassed = false;
	std::vector<ScmCallFrame> callFrames;
	uint32_t context[32]{};
};

struct PtrHandleEntry {
	void* ptr = nullptr;
	size_t size = 0;
	bool owned = false;
};

struct TouchFinger {
	bool down = false;
	int startZone = 0;
	int currentZone = 0;
	uint32_t startTime = 0;
	uint32_t lastZoneTime = 0;
};

struct SlideRecord {
	int fromZone = 0;
	int toZone = 0;
	uint32_t duration = 0;
	uint32_t finishTime = 0;
};

struct ScriptMenuState {
	bool active = false;
	std::string title;
	std::string closeLabel;
	std::vector<std::string> items;
	int activeIndex = 0;
	int page = 0;
	int lastTouchedIndex = -1;
	uint32_t lastTouchedTime = 0;
};

struct BuiltinMenuState {
	bool open = false;
	int page = 0;
	bool showArrowHint = false;
};

struct RectF {
	float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
	bool contains(float x, float y) const {
		return x >= x1 && x <= x2 && y >= y1 && y <= y2;
	}
};

using FnInitScript = void (*)(sa::CRunningScript*);
using FnProcessOneCmd = uint8_t (*)(sa::CRunningScript*);
using FnCollectParams = void (*)(sa::CRunningScript*, int16_t);
using FnStoreParams = void (*)(sa::CRunningScript*, int16_t);
using FnUpdateCmpFlag = void (*)(sa::CRunningScript*, uint8_t);
using FnGetVarPtr = uint32_t* (*)(sa::CRunningScript*, uint8_t);
using FnAsciiToGxt = void (*)(const char*, uint16_t*);
using FnSetHelpMsg = void (*)(const char*, uint16_t*, bool, bool, bool, uint32_t);
using FnAddMessage = void (*)(const char*, uint16_t*, uint32_t, uint16_t, bool);
using FnAddBigMessage = void (*)(uint16_t*, uint32_t, uint16_t);
using FnCTextGet = uint16_t* (*)(void*, const char*);

extern bool g_enabled;
extern bool g_symbolsReady;
extern bool g_scriptsBooted;
extern std::string g_gameDir;
extern std::string g_cleoDir;
extern std::string g_currentDir;
extern std::string g_logPath;
extern FILE* g_logFile;
extern std::mutex g_logMutex;

extern uintptr_t g_gtasaBase;
extern uint32_t* g_scriptParams;
extern uint8_t* g_scriptSpace;
extern uint32_t* g_timerMs;
extern void* g_theText;

extern FnInitScript g_fnInitScript;
extern FnProcessOneCmd g_fnProcessOneCmd;
extern FnCollectParams g_fnCollectParams;
extern FnStoreParams g_fnStoreParams;
extern FnUpdateCmpFlag g_fnUpdateCmpFlag;
extern FnGetVarPtr g_fnGetVarPtr;
extern FnAsciiToGxt g_fnAsciiToGxt;
extern FnSetHelpMsg g_fnSetHelpMsg;
extern FnAddMessage g_fnAddMessage;
extern FnAddMessage g_fnAddMessageJumpQ;
extern FnAddBigMessage g_fnAddBigMessage;
extern FnCTextGet g_fnCTextGet;

extern std::vector<CleoScript> g_scripts;
extern std::unordered_map<std::string, std::vector<uint16_t>> g_customGxt;
extern std::mutex g_gxtMutex;

extern std::unordered_map<uint32_t, uint32_t> g_mutexVars;
extern std::array<uint32_t, kMaxSharedVars> g_sharedVars;

extern std::unordered_map<uint32_t, PtrHandleEntry> g_ptrHandles;
extern uint32_t g_nextHandle;

extern std::unordered_map<uint32_t, FILE*> g_openFiles;
extern uint32_t g_nextFileHandle;

extern uint32_t g_lastTickMs;

extern std::mutex g_touchMutex;
extern TouchFinger g_fingers[8];
extern SlideRecord g_lastSlide;
extern bool g_menuBtnDown;
extern uint32_t g_menuBtnStart;
extern float g_screenW;
extern float g_screenH;

extern ScriptMenuState g_scriptMenu;
extern BuiltinMenuState g_builtinMenu;
extern int g_pendingStartScriptIdx;

extern RectF g_menuPanelRect;
extern RectF g_menuCloseRect;
extern RectF g_menuPrevRect;
extern RectF g_menuNextRect;
extern std::vector<std::pair<int, RectF>> g_menuRowRects;

extern uint16_t g_msgGxtBuf[4][512];
extern int g_msgGxtSlot;

// Core helpers (defined in CleoEngine.cpp)
void cleoLog(const char* fmt, ...);
uint32_t nowMs();
std::string toUpperAscii(const std::string& s);
std::string toLowerAscii(const std::string& s);
bool endsWithIgnoreCase(const std::string& s, const std::string& suffix);
std::vector<uint16_t> asciiToWide(const std::string& s);
std::string wideGxtToUtf8(const uint16_t* w);
uint32_t registerHandle(void* ptr, size_t size = 0, bool owned = false);
void* resolveHandleOrAddr(uint32_t addr, bool addImageBase, size_t* outSize = nullptr);
bool readBoundedCStr(const char*& cursor, const char* end, std::string& out, size_t maxLen = 256);
void resetTouchFingers();
void addCustomGxtEntry(const std::string& rawKey, const std::string& value);
void removeCustomGxtEntry(const std::string& rawKey);
void loadFxtFile(const std::string& path);
std::string lookupGxtUtf8(const std::string& keyOrLiteral);
std::string makeDisplayName(const std::string& fileName);
void scanDirectory(const std::string& dirPath, bool scanScripts);
void startOrRestartScript(CleoScript& sc, bool preserveLocals, uint32_t initialWaitMs);
int calcTouchZone(float x, float y, float w, float h);
bool isZoneTouchedTimed(int zone, uint32_t minTimeMs);
bool isAnyFingerDown();
bool checkSlideDone(int fromZone, int toZone, uint32_t minMs, uint32_t maxMs);

// Opcode VM (defined in CleoOpcodes.cpp)
bool readTargetLabelOffset(CleoScript& sc, uint8_t*& outTarget);
bool readStringParam(CleoScript& sc, std::string& out);
uint16_t* makeTempGxt(const std::string& text);
std::string resolveScriptPath(const std::string& rawPath);
bool handleCustomOpcode(CleoScript& sc, uint16_t rawOp);

// Menu UI (defined in CleoMenu.cpp)
bool isAnyMenuVisible();
std::vector<int> collectInvokableScriptIndices();

} // namespace cleo_internal
