#pragma once

namespace sa {
class CBaseModelInfo;
struct CPlayerInfo;
struct CStreamingInfo;
}

extern sa::CPlayerInfo* WORLD_PLAYERS;

class Patches {
public:
	static void preApply();
	static void apply();
	static bool hasFullAudioData();

	static void allocPlayers();
	static void allocModelTables();
	static void initBetterPoolsSize();

	static sa::CBaseModelInfo** modelInfoPtrs();
	static sa::CStreamingInfo* streamingInfoTable();
};
