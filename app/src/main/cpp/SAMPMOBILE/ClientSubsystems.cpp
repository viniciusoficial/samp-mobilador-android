#include "ClientSubsystems.h"

#include "Client.h"
#include "Settings.h"
#include "Game/AudioStream.h"
#include "Game/CleoEngine.h"
#include "Game/ModLoader.h"
#include "Voice/Plugin.h"

#include <Bass.h>

namespace ClientSubsystems {

void onSettingsLoaded()
{
	Plugin::syncSettings(false);
}

void onSettingsApplied()
{
	Plugin::syncSettings(true);
}

void onPreHooksInit(const std::string& gameDir)
{
	LoadBassLibrary();
	ModLoader::initialize(gameDir);
	CleoEngine::initialize(gameDir);
}

void onPostBootInit()
{
	if (Settings::voice()) {
		Plugin::initialize();
	}
}

void onUiDeviceReady()
{
	if (Settings::voice()) {
		Plugin::onUiDeviceReady();
	}
}

void onWorldReady()
{
	pAudioStream = new AudioStream();
	pAudioStream->Initialize();

	if (Settings::voice()) {
		Plugin::onWorldReady();
	}
}

void onGameFrame(bool netActive)
{
	if (netActive && pAudioStream) {
		pAudioStream->Process();
	}
	CleoEngine::process();
}

void onUiRender(ImGuiRenderer* renderer, float displayW, float displayH, bool netActive, bool hideVoiceSpeakers)
{
	if (Settings::voice()) {
		Plugin::onUiRender(netActive, hideVoiceSpeakers);
	}
	CleoEngine::renderMenu(renderer, displayW, displayH);
}

} // namespace ClientSubsystems
