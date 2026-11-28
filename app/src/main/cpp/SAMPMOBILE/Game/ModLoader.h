#pragma once

#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

class ModLoader {
public:
	// Scans <gameDir>/modloader/ once during Client::initialize() (single-threaded, before hooks).
	// Safe to call when disabled (clears state and returns immediately).
	static void initialize(const std::string& gameDir);

	static bool isEnabled() { return m_enabled.load(std::memory_order_acquire); }

	// Resolves a GTA-relative path (e.g. "data\\timecyc.dat" or "texdb/gta3/gta3.etc.toc")
	// to a modloader-relative path (e.g. "modloader/HD_Roads/texdb/gta3/gta3.etc.toc").
	// Returns true and writes outRelPath on hit. Read-only & lock-free after initialize().
	static bool resolve(const char* gtaRelPath, char* outRelPath, size_t outSize);

	// Custom TextureDatabaseRuntime names found under modloader/<mod>/texdb/<dbName>/
	// that are NOT part of the standard built-in GTA/SAMP texture databases.
	static const std::vector<std::string>& extraTexDbs() { return m_extraTexDbs; }

	// True when loose .dff/.ifp/.col/.ipl files across active mods were auto-packed
	// into <gameDir>/modloader/models.img (VER2) for zero-pollution CStreaming override.
	static bool hasVirtualModelsImg() { return m_hasVirtualImg.load(std::memory_order_acquire); }
	static const char* virtualModelsImgRelPath() { return "modloader\\models.img"; }

private:
	static std::atomic<bool> m_enabled;
	static std::atomic<bool> m_hasVirtualImg;
	static std::vector<std::string> m_extraTexDbs;
};
