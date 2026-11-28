#pragma once

#include <atomic>
#include <cstdint>

// Shared cross-hook state and sub-installer entry points for Game/Hooks/*.cpp.
extern std::atomic<bool> g_suppressWidgetSetEnabled;
extern std::atomic<int> g_lastObjectModel;
extern bool g_bCreatingSirenVehicle;
extern bool g_bIntentionalTrailerDetach;

// Frame-loop / Render / Screenshot cross-module helpers
void SanitizeAllAlphaLinkLists();
bool ConsumePendingScreenshot(int expectedMode);
void CaptureCurrentFramebufferToPng(int mode);

// Hook domain installers (called by Hooks::install())
void InstallStreamingHooks();
void InstallLifecycleHooks();
void InstallInputHooks();
void InstallRenderHooks();
void InstallWorldHooks();
