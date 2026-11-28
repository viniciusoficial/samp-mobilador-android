#pragma once

#include <jni.h>
#include <string>

class Client {
public:
	Client() = default;

	static void initialize(const std::string& gameDir, bool offlineMode);
	static void process(bool canRender2d = true);
	static void initializeUI();

	static JavaVM* javaVM() { return java_vm; }
	static char* gameDir() { return (char*) game_dir.c_str(); }
	static bool offlineMode() { return offline_mode; }
	static std::string sampVersion() { return samp_version; }
	static int netgameVersion() { return samp_netgame; }
	static bool isDL() { return samp_netgame == 0xFDE; }
	static bool setLogLevelDebug(bool debug, bool persist = true);
	static bool isLogLevelDebug();

private:
	static JavaVM* java_vm;
	static std::string game_dir;
	static bool offline_mode;
	static std::string samp_version;
	static int samp_netgame;
};

class UI;
class Game;
class NetGame;
class PlayerTags;
class AudioStream;
class SnapShotHelper;
class MaterialTextGenerator;
class SymUtils;

extern UI* pUI;
extern Game* pGame;
extern NetGame* pNetGame;
extern PlayerTags* pPlayerTags;
extern AudioStream* pAudioStream;
extern SnapShotHelper* pSnapShotHelper;
extern MaterialTextGenerator* pMaterialTextGenerator;
extern SymUtils* g_saSym;
