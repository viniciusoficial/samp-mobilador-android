#pragma once

#include <atomic>
#include <cstddef>

// Lock-free memory accounting snapshot published by ArtworkManager::processFrame
// and sampled by the client memory-telemetry thread (Client.cpp). The counters
// are approximate by design: they exist so the memory curve leading up to an
// LMK / driver kill is visible in the file log, not for exact bookkeeping.
namespace artwork {

extern std::atomic<size_t> g_telemetryTxdBytes;
extern std::atomic<size_t> g_telemetryLoadedSlots;
extern std::atomic<size_t> g_telemetryLoadedTxds;

} // namespace artwork
