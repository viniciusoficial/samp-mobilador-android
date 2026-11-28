#pragma once

#include "Proto/Version.h"

#include <cstdint>
#include <vector>

class RakClientInterface;

namespace RpcTable {

struct StatEntry {
	int id = 0;
	const char* name = "";
	uint64_t hits = 0;
	uint64_t readFails = 0;
};

void registerAll(RakClientInterface* rakClient, proto::ProtoVersion version);
void unregisterAll(RakClientInterface* rakClient, proto::ProtoVersion version);

std::vector<StatEntry> snapshotStats();
void resetStats();
size_t entryCount();

} // namespace RpcTable
