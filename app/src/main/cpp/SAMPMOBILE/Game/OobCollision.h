#pragma once

#include <cstdint>

namespace sa {
class CColModel;
}

// CColStore pinning for out-of-bounds (|x/y| > 3000) entities: keeps shared
// colModel slots force-loaded where the position-driven quadtree streaming
// would otherwise leave them empty shells (zero collision).
// Repeat-sector registration needs no OOB support at all — see the file-level
// comment in OobCollision.cpp.
bool PinOobColSlot(sa::CColModel* pCM, int& iPinnedSlot);
void RequestOobColSlot(sa::CColModel* pCM, int iPinnedSlot, uint32_t& dwLastReqTick);
void LogOobColPin(int iModel, int iSlot);

// Per-frame col-slot pinning for out-of-bounds pool vehicles.
// Runs from CObjectPool::Process (game thread).
void ProcessOobVehicleColPins();
