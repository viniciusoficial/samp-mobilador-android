#pragma once

#include <string>

class ImGuiRenderer;

// Explicit lifecycle boundary between core client orchestration (Client / UI / Settings)
// and optional/extension subsystems (ModLoader, CleoEngine, AudioStream, Voice).
// CEF remains an external black-box boundary called directly from Client.cpp.
namespace ClientSubsystems {

// Called when settings.json is first loaded during Client::initialize().
void onSettingsLoaded();

// Called when settings are applied to the GTA engine (startup and in-game sync).
void onSettingsApplied();

// Called during Client::initialize() after sa::Initialize() and BEFORE Hooks::install().
void onPreHooksInit(const std::string& gameDir);

// Called during Client::initialize() after Hooks/Patches/MultiTouch and Game creation.
void onPostBootInit();

// Called during Client::initializeUI() before the ImGui UI root is constructed.
void onUiDeviceReady();

// Called once on GameMain when the world and local player ped are ready.
void onWorldReady();

// Called once per GameMain tick inside Client::process().
void onGameFrame(bool netActive);

// Called during UI::drawList() to drive subsystem overlays (Voice SpeakerList, CLEO menu).
void onUiRender(ImGuiRenderer* renderer, float displayW, float displayH, bool netActive, bool hideVoiceSpeakers);

} // namespace ClientSubsystems
