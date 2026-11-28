#pragma once

#include <cstring>
#include "NetTypes.h"

class CRemotePlayer;
class CPlayer {
public:
	CPlayer(const char* szPlayerName, bool bIsNPC);
	~CPlayer();

	void SetPlayerName(const char* szPlayerName)
	{
		strncpy(m_szPlayerName, szPlayerName, MAX_PLAYER_NAME);
		m_szPlayerName[MAX_PLAYER_NAME] = 0;
	}

public:
	CRemotePlayer* m_pRemotePlayer;
	char m_szPlayerName[MAX_PLAYER_NAME + 1];
	bool m_bIsNPC;
	int m_iScore;
	int m_iPing;
};