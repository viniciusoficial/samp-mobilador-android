#include "NetStats.h"
#include "../UI.h"
#include "../UISettings.h"
#include "../../Client.h"
#include "../../Game/Game.h"
#include "../../Game/PlayerPed.h"
#include "../../Game/Vehicle.h"
#include "../../Game/Util.h"
#include "../../Net/NetGame.h"
#include "../../Net/VehiclePool.h"

#include <RakNet/RakClientInterface.h>
#include <RakNet/RakNetStatistics.h>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <unistd.h>


namespace {

template <typename T>
T readLe(const uint8_t* buf, size_t offset)
{
	T v{};
	std::memcpy(&v, buf + offset, sizeof(T));
	return v;
}

unsigned long readProcessRssKb()
{
	FILE* f = fopen("/proc/self/statm", "r");
	if (!f) return 0;
	unsigned long sizePages = 0, rssPages = 0;
	const int ok = fscanf(f, "%lu %lu", &sizePages, &rssPages);
	fclose(f);
	if (ok != 2) return 0;
	const long pageSize = sysconf(_SC_PAGESIZE);
	if (pageSize <= 0) return 0;
	return (rssPages * static_cast<unsigned long>(pageSize)) / 1024ul;
}

void drawStatsPanel(ImGuiRenderer* renderer, float x, float y, const char* title, const std::string& body)
{
	const float fontSize = UISettings::fontSize() * 0.44f;
	const float lineStep = fontSize * 1.15f;
	const float pad = pUI ? pUI->ScaleX(12.0f) : 10.0f;

	std::vector<std::string> lines;
	std::stringstream ss(body);
	std::string row;
	float maxW = renderer->calculateTextSize(title, fontSize).x;
	while (std::getline(ss, row, '\n')) {
		if (!row.empty() && row.back() == '\r') row.pop_back();
		// Expand tabs so RakNet StatisticsToString aligns cleanly
		std::string clean;
		clean.reserve(row.size());
		for (char c : row) {
			if (c == '\t') clean += "  ";
			else clean += c;
		}
		float w = renderer->calculateTextSize(clean, fontSize).x;
		if (w > maxW) maxW = w;
		lines.push_back(std::move(clean));
	}

	const float boxW = maxW + pad * 2.0f;
	const float boxH = (static_cast<float>(lines.size() + 1) * lineStep) + pad * 2.0f;
	renderer->drawRect(ImVec2(x, y), ImVec2(x + boxW, y + boxH), ImColor(0, 0, 0, 175), true);

	float cy = y + pad;
	renderer->drawText(ImVec2(x + pad, cy), ImColor(0x88, 0x88, 0xEE, 0xFF), title, true, fontSize);
	cy += lineStep;
	for (const auto& line : lines) {
		renderer->drawText(ImVec2(x + pad, cy), ImColor(0xFF, 0xFF, 0xFF, 0xFF), line, true, fontSize);
		cy += lineStep;
	}
}

} // namespace

void NetStats::updateServerStats(const std::array<uint8_t, 296>& raw)
{
	m_serverStatsRaw = raw;
	m_hasServerStats = true;
	m_lastServerStatsTick = GetTickCount();
}

void NetStats::resetServerStats()
{
	m_hasServerStats = false;
	m_lastServerStatsTick = 0;
	m_serverStatsRaw.fill(0);
}

void NetStats::draw(ImGuiRenderer* renderer)
{
	if (!pNetGame) return;
	if (m_showClientStats) {
		drawClientStats(renderer);
	}
	if (m_showServerStats) {
		drawServerStats(renderer);
	}
}

void NetStats::drawClientStats(ImGuiRenderer* renderer)
{
	RakClientInterface* rak = pNetGame->GetRakClient();
	if (!rak || !rak->IsConnected()) return;
	RakNetStatisticsStruct* stats = rak->GetStatistics();
	if (!stats) return;

	const uint32_t now = GetTickCount();
	if (m_lastUpdateTick == 0 || (now - m_lastUpdateTick) > 1000) {
		const uint32_t curRecv = stats->bitsReceived >> 3;
		const uint32_t curSent = stats->totalBitsSent >> 3;
		if (m_lastUpdateTick != 0) {
			m_downloadRate = curRecv - m_lastTotalReceived;
			m_uploadRate = curSent - m_lastTotalSent;
		}
		m_lastTotalReceived = curRecv;
		m_lastTotalSent = curSent;
		m_lastUpdateTick = now;
	}

	const double downKBs = static_cast<double>(m_downloadRate) / 1024.0;
	const double upKBs = static_cast<double>(m_uploadRate) / 1024.0;

	uint32_t vehicleCount = 0;
	uint32_t uniqueModels = 0;
	if (CVehiclePool* vehPool = pNetGame->GetVehiclePool()) {
		uint8_t modelSeen[212] = {};
		for (VEHICLEID id = 0; id < MAX_VEHICLES; ++id) {
			if (vehPool->GetSlotState(id)) {
				++vehicleCount;
				if (CVehicle* v = vehPool->GetAt(id); v && v->m_pVehicle) {
					const int model = v->m_pVehicle->m_nModelIndex;
					if (model >= 400 && model < 612) {
						modelSeen[model - 400] = 1;
					}
				}
			}
		}
		for (uint8_t s : modelSeen) {
			if (s) ++uniqueModels;
		}
	}

	uint32_t playerCount = 0;
	if (CPlayerPool* pp = pNetGame->GetPlayerPool()) {
		for (PLAYERID id = 0; id < MAX_PLAYERS; ++id) {
			if (pp->GetSlotState(id)) ++playerCount;
		}
	}

	uint32_t objectCount = 0;
	if (CObjectPool* op = pNetGame->GetObjectPool()) {
		for (uint16_t id = 0; id < MAX_OBJECTS; ++id) {
			if (op->GetAt(id) != nullptr) ++objectCount;
		}
	}

	std::string out;
	char line[256];
	snprintf(line, sizeof(line), "Download Rate: %.2f KB/s | Upload Rate: %.2f KB/s\n", downKBs, upKBs);
	out += line;

	snprintf(line, sizeof(line), "Vehicles: %u (Models: %u) | Players: %u | Objects: %u | LagComp: %d\n",
			vehicleCount, uniqueModels, playerCount, objectCount, pNetGame->session().lagCompensationMode);
	out += line;

	const SessionState& sess = pNetGame->session();
	CPlayerPed* pLocal = pGame ? pGame->FindPlayerPed() : nullptr;
	if (pLocal && pLocal->IsInVehicle()) {
		snprintf(line, sizeof(line), "InCar Send Rate: %d ms (Firing: %d ms)\n", sess.inCarSendRate, sess.firingSendRate);
	}
	else {
		snprintf(line, sizeof(line), "OnFoot Send Rate: %d ms (Firing: %d ms)\n", sess.onFootSendRate, sess.firingSendRate);
	}
	out += line;

	const auto* pMemUsed = eng::as<uint32_t*>(eng::Sym::CStreaming_ms_memoryUsed);
	const auto* pMemAvail = eng::as<uint32_t*>(eng::Sym::CStreaming_ms_memoryAvailable);
	const unsigned long rssKb = readProcessRssKb();
	if (pMemUsed && pMemAvail) {
		snprintf(line, sizeof(line), "Streaming Mem: %.1f MB / %.1f MB | Process RSS: %lu KB\n",
				static_cast<double>(*pMemUsed) / (1024.0 * 1024.0),
				static_cast<double>(*pMemAvail) / (1024.0 * 1024.0),
				rssKb);
		out += line;
	}
	else if (rssKb > 0) {
		snprintf(line, sizeof(line), "Process RSS: %lu KB\n", rssKb);
		out += line;
	}

	char rakBuf[4096] = {};
	StatisticsToString(stats, rakBuf, 1);
	out += rakBuf;

	const float x = pUI ? pUI->ScaleX(20.0f) : 20.0f;
	const float y = pUI ? pUI->ScaleY(20.0f) : 20.0f;
	drawStatsPanel(renderer, x, y, "Client Network Stats (F5 / /netstats)", out);
}

void NetStats::drawServerStats(ImGuiRenderer* renderer)
{
	std::string out;
	if (!m_hasServerStats) {
		out = "Waiting for server statistics (RPC 102)...\n"
		      "(Only sent by servers pushing SvrStats packets)\n";
	}
	else {
		const uint8_t* raw = m_serverStatsRaw.data();
		const uint64_t totalBitsResent = readLe<uint64_t>(raw, 0x94);
		const uint32_t packetsSent = readLe<uint32_t>(raw, 0xAC);
		const uint64_t totalBitsSent = readLe<uint64_t>(raw, 0xB8);
		const uint32_t packetsReceived = readLe<uint32_t>(raw, 0xD0);
		const uint32_t badCrcPackets = readLe<uint32_t>(raw, 0xD4);
		const uint64_t bitsReceived = readLe<uint64_t>(raw, 0xD8);
		const uint64_t bitsBadCrc = readLe<uint64_t>(raw, 0xE0);
		const uint32_t messagesReceived = readLe<uint32_t>(raw, 0xF0);
		const double bitsPerSec = readLe<double>(raw, 0x104);
		const double lossPct = (totalBitsSent > 0)
				? (100.0 * static_cast<double>(totalBitsResent) / static_cast<double>(totalBitsSent))
				: 0.0;
		const uint32_t ageSec = (GetTickCount() - m_lastServerStatsTick) / 1000;

		char buf[512];
		snprintf(buf, sizeof(buf),
				"Bytes Sent: %llu | Bytes Recv: %llu (BadCRC: %llu)\n"
				"Packets Sent: %u | Packets Recv: %u (BadCRC: %u)\n"
				"Messages Recv: %u | Packetloss: %.2f%%\n"
				"Inst. KBits/s: %.1f | Last Update: %us ago\n",
				static_cast<unsigned long long>(totalBitsSent >> 3),
				static_cast<unsigned long long>(bitsReceived >> 3),
				static_cast<unsigned long long>(bitsBadCrc >> 3),
				packetsSent, packetsReceived, badCrcPackets,
				messagesReceived, lossPct,
				bitsPerSec / 1000.0, ageSec);
		out = buf;
	}

	const float x = pUI ? pUI->ScaleX(980.0f) : 640.0f;
	const float y = pUI ? pUI->ScaleY(20.0f) : 20.0f;
	drawStatsPanel(renderer, x, y, "Server Network Stats (F10 / /svrnetstats)", out);
}
