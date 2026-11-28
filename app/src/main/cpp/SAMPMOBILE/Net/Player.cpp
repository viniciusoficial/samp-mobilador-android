#include "Player.h"
#include "RemotePlayer.h"
#include "NetGame.h"
#include "../Game/Game.h"

CPlayer::CPlayer(const char* szPlayerName, bool bIsNPC)
{
	m_bIsNPC = bIsNPC;
	m_iScore = 0;
	m_iPing = 0;
	strncpy(m_szPlayerName, szPlayerName, MAX_PLAYER_NAME);
	m_szPlayerName[MAX_PLAYER_NAME] = 0;

	m_pRemotePlayer = new CRemotePlayer();
}

CPlayer::~CPlayer()
{
	if (m_pRemotePlayer) {
		delete m_pRemotePlayer;
		m_pRemotePlayer = nullptr;
	}
}