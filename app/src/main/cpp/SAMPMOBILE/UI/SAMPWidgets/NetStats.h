#pragma once

#include <array>
#include <cstdint>
#include "../Widget.h"

class NetStats : public Widget {
public:
	NetStats() = default;
	virtual void draw(ImGuiRenderer* renderer) override;

	bool clientStatsVisible() const { return m_showClientStats; }
	void setClientStatsVisible(bool show)
	{
		m_showClientStats = show;
		setVisible(m_showClientStats || m_showServerStats);
	}

	bool serverStatsVisible() const { return m_showServerStats; }
	void setServerStatsVisible(bool show)
	{
		m_showServerStats = show;
		setVisible(m_showClientStats || m_showServerStats);
	}

	void updateServerStats(const std::array<uint8_t, 296>& raw);
	void resetServerStats();

private:
	void drawClientStats(ImGuiRenderer* renderer);
	void drawServerStats(ImGuiRenderer* renderer);

	bool m_showClientStats = false;
	bool m_showServerStats = false;

	uint32_t m_lastUpdateTick = 0;
	uint32_t m_lastTotalReceived = 0;
	uint32_t m_lastTotalSent = 0;
	uint32_t m_downloadRate = 0;
	uint32_t m_uploadRate = 0;

	bool m_hasServerStats = false;
	uint32_t m_lastServerStatsTick = 0;
	std::array<uint8_t, 296> m_serverStatsRaw{};
};
