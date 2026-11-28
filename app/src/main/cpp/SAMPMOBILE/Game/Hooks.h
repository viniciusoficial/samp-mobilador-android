#pragma once

class Hooks {
public:
	static void install();
	static void installPadHooks();
	static void installWidgetFixHooks();
	static void installCrosshairFixHooks();
	static void requestScreenshot(int modeOverride = -1);
	static void openAdjustableHud();
	static void allowInitialWorldBoot();
	static void requestGracefulExit(const char* reason = "exit");
};
