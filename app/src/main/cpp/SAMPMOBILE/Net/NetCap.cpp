#include "NetCap.h"
#include "Proto/NetCapFormat.h"

#include <RakNet/SAMP/NetCapHook.h>

#include <atomic>
#include <chrono>
#include <ctime>
#include <mutex>
#include <sys/stat.h>

namespace {

constexpr long kMaxFileBytes = 256L * 1024L * 1024L;

std::mutex g_mutex;
FILE* g_file = nullptr;
std::string g_path;
long g_bytes = 0;
std::atomic<uint32_t> g_records{0};
std::atomic<bool> g_on{false};
std::chrono::steady_clock::time_point g_t0;

std::string capDir(const std::string& gameDir) { return gameDir + "netcap/"; }
std::string markerPath(const std::string& gameDir) { return capDir(gameDir) + "ENABLED"; }

void onPacket(int dir, int kind, int id, const unsigned char* data, unsigned int bitLength)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	if (!g_file) return;
	const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - g_t0).count();
	if (!netcap::writeRecord(g_file, static_cast<uint32_t>(ms), static_cast<uint8_t>(dir), static_cast<uint8_t>(kind),
			static_cast<uint16_t>(id), data, bitLength)) {
		// Stop cleanly on I/O failure so a partial write cannot corrupt subsequent framing
		setRakNetCapHook(nullptr);
		fclose(g_file);
		g_file = nullptr;
		g_on.store(false, std::memory_order_release);
		return;
	}
	fflush(g_file);
	g_records.fetch_add(1, std::memory_order_relaxed);
	g_bytes += static_cast<long>(netcap::kRecordHeaderSize + (bitLength + 7) / 8);
	if (g_bytes > kMaxFileBytes) {
		setRakNetCapHook(nullptr);
		fclose(g_file);
		g_file = nullptr;
		g_on.store(false, std::memory_order_release);
	}
}

}

void NetCap::startIfEnabled(const std::string& gameDir, uint32_t netgameVersion)
{
	struct stat st{};
	if (stat(markerPath(gameDir).c_str(), &st) == 0) {
		start(gameDir, netgameVersion, false);
	}
}

bool NetCap::start(const std::string& gameDir, uint32_t netgameVersion, bool persist)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	mkdir(capDir(gameDir).c_str(), 0775);
	if (g_file) {
		if (persist) {
			if (FILE* m = fopen(markerPath(gameDir).c_str(), "wb")) fclose(m);
		}
		return true;
	}

	const time_t now = time(nullptr);
	struct tm tmNow{};
	localtime_r(&now, &tmNow);
	char name[64];
	strftime(name, sizeof(name), "cap_%Y%m%d_%H%M%S.ncap", &tmNow);
	g_path = capDir(gameDir) + name;

	g_file = fopen(g_path.c_str(), "wb");
	if (!g_file) return false;
	if (!netcap::writeHeader(g_file, netgameVersion, static_cast<int64_t>(now))) {
		fclose(g_file);
		g_file = nullptr;
		return false;
	}
	fflush(g_file);
	if (persist) {
		if (FILE* m = fopen(markerPath(gameDir).c_str(), "wb")) fclose(m);
	}
	g_bytes = static_cast<long>(netcap::kHeaderSize);
	g_records.store(0, std::memory_order_relaxed);
	g_t0 = std::chrono::steady_clock::now();
	g_on.store(true, std::memory_order_release);
	setRakNetCapHook(&onPacket);
	return true;
}

void NetCap::stop(const std::string& gameDir, bool persist)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	setRakNetCapHook(nullptr);
	if (g_file) {
		fclose(g_file);
		g_file = nullptr;
	}
	g_on.store(false, std::memory_order_release);
	if (persist) remove(markerPath(gameDir).c_str());
}

bool NetCap::isOn() { return g_on.load(std::memory_order_acquire); }

std::string NetCap::currentFile()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	return g_path;
}

uint32_t NetCap::recordCount() { return g_records.load(std::memory_order_relaxed); }
