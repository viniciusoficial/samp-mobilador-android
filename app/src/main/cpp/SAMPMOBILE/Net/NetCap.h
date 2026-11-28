#pragma once

#include <cstdint>
#include <string>

// Packet capture switch (/netcap). Off by default; when off nothing is hooked.
// Captures land in <gameDir>netcap/cap_<date>_<time>.ncap (format: Proto/NetCapFormat.h).
class NetCap {
public:
	// Starts capturing if <gameDir>netcap/ENABLED exists. Call before the connection is made.
	static void startIfEnabled(const std::string& gameDir, uint32_t netgameVersion);

	// persist = also create/remove the ENABLED marker so the choice survives a restart.
	static bool start(const std::string& gameDir, uint32_t netgameVersion, bool persist);
	static void stop(const std::string& gameDir, bool persist);

	static bool isOn();
	static std::string currentFile();
	static uint32_t recordCount();
};
