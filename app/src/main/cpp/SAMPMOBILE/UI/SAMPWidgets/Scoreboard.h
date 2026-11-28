#pragma once

#include <cstdint>
#include "../Widget.h"

typedef struct _PLAYER_SCORE_INFO PLAYER_SCORE_INFO;

class Scoreboard : public Widget {

public:
	Scoreboard();
	~Scoreboard();

	virtual void draw(ImGuiRenderer* renderer) override;

	void processUpdating();

private:
	PLAYER_SCORE_INFO* m_players;
	uint16_t m_playerCount;

	int m_offset;
	bool m_sorted;
	uint32_t m_tickProcessingUpdate;

	int m_lastSelectedItem;
	int m_selectedItem;
};
