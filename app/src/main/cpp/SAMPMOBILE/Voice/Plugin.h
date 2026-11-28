#pragma once

class Plugin {
	Plugin() = delete;
	~Plugin() = delete;
	Plugin(const Plugin&) = delete;
	Plugin(Plugin&&) = delete;
	Plugin& operator=(const Plugin&) = delete;
	Plugin& operator=(Plugin&&) = delete;

public:
	static bool initialize() noexcept;
	static void onUiDeviceReady() noexcept;
	static void onWorldReady() noexcept;
	static void onUiRender(bool netActive, bool hideSpeakerList);
	static void syncSettings(bool applyRuntime) noexcept;
	static void SyncVoiceVolume() noexcept;
};
