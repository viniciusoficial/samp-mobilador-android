#pragma once

class CPlayerPool
{
public:
	CPlayerPool();
	~CPlayerPool();

	// Nome vem do servidor: sem o limite, um nome maior que MAX_PLAYER_NAME
	// escrevia no nome do jogador seguinte (corrompia o pool inteiro).
	void SetPlayerName(PLAYERID playerId, const char* szName) {
		if (playerId >= MAX_PLAYERS || !szName) return;
		strncpy(m_szPlayerNames[playerId], szName, MAX_PLAYER_NAME);
		m_szPlayerNames[playerId][MAX_PLAYER_NAME] = '\0';
	}
	char* GetPlayerName(PLAYERID playerId) {
		static char szEmptyName[MAX_PLAYER_NAME + 1] = { 0 };
		if (playerId >= MAX_PLAYERS) return szEmptyName;
		return m_szPlayerNames[playerId];
	}

	void SetLocalPlayerName(const char* szPlayerName) {
		if (!szPlayerName) return;
		strncpy(m_szLocalPlayerName, szPlayerName, MAX_PLAYER_NAME);
		m_szLocalPlayerName[MAX_PLAYER_NAME] = '\0';
	}
	
	char* GetLocalPlayerName() {
		return m_szLocalPlayerName;
	}

	void SetLocalPlayerID(PLAYERID playerId) {
		m_LocalPlayerID = playerId;
	}

	PLAYERID GetLocalPlayerID() {
		return m_LocalPlayerID;
	}

	int GetLocalPlayerScore() {
		return m_iLocalPlayerScore;
	}

	int GetLocalPlayerPing() {
		return m_iLocalPlayerPing;
	}

	CLocalPlayer* GetLocalPlayer() {
		return m_pLocalPlayer;
	}

	// 0.3.7
	CRemotePlayer* GetAt(PLAYERID playerId) {
		if (playerId >= MAX_PLAYERS ||
			m_bPlayerSlotState[playerId] == false ||
			m_pPlayers[playerId] == nullptr) {
			return nullptr;
		}

		return m_pPlayers[playerId];
	}
	// 0.3.7
	bool GetSlotState(PLAYERID playerId) {
		if (playerId >= MAX_PLAYERS) {
			return false;
		}

		return m_bPlayerSlotState[playerId];
	}

	PLAYERID FindRemotePlayerIDFromGtaPtr(CPedGTA* pActor);

	int GetPlayerScore(PLAYERID playerId);
	int GetPlayerPing(PLAYERID playerId);
	bool IsPlayerNPC(PLAYERID playerId);
	int GetTotalPlayers();

	void UpdatePlayerScore(PLAYERID playerId, int iScore);
	void UpdatePlayerPing(PLAYERID playerId, int iPing);

	void Process();

	bool New(PLAYERID playerId, const char* szPlayerName, bool bIsNPC);
	bool Delete(PLAYERID playerId, uint8_t byteReason);

	void DeactivateAll();

	void ProcessAttachedObjects();

	void ApplyCollisionChecking();
	void ResetCollisionChecking();

private:
	void FindLastPlayerID();

private:
	int				m_iLastPlayerID;
	CRemotePlayer*	m_pPlayers[MAX_PLAYERS];
	bool			m_bPlayerSlotState[MAX_PLAYERS];

	PLAYERID		m_LocalPlayerID;
	char			m_szLocalPlayerName[MAX_PLAYER_NAME + 1];
	CLocalPlayer*	m_pLocalPlayer;
	int				m_iLocalPlayerPing;
	int				m_iLocalPlayerScore;

	bool			m_bCollisionChecking[MAX_PLAYERS];

	char			m_szPlayerNames[MAX_PLAYERS][MAX_PLAYER_NAME+1];
	int				m_iPlayerScores[MAX_PLAYERS];
	uint32_t		m_dwPlayerPings[MAX_PLAYERS];
};