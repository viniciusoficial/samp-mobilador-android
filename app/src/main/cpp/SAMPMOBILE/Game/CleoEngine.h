#pragma once

#include <cstdint>
#include <string>

class ImGuiRenderer;

class CleoEngine {
public:
	static void initialize(const std::string& gameDir);
	static bool isEnabled();

	// Called once per game tick on MainThread (both online & offline)
	static void process();

	// Called from CText::Get hook; returns custom .fxt / dynamic 0ADF GXT wide string or nullptr
	static uint16_t* getCustomGxt(const char* key);

	// Called from InputArbiter when no higher-priority modal UI is open.
	// Returns true if CLEO menu UI consumed the touch event.
	static bool handleTouch(int type, int num, int x, int y, float screenW, float screenH);

	// Called from ClientSubsystems::onUiRender() to draw built-in CLEO .csi launcher menu or script 0DF2 menu
	static void renderMenu(ImGuiRenderer* renderer, float displayW, float displayH);

	// Toggle built-in CLEO .csi script menu (invoked by /cleo chat command)
	static void toggleMenu();

	// Query / dismiss active CLEO menu overlay (for unified modal/back arbitration)
	static bool isMenuOpen();
	static bool dismissMenu();
};
