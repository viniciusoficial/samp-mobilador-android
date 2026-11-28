#pragma once

#include <cstdint>
#include <sys/types.h>

void GameResetRadarColors();
void ResetRadarColorSlot(int nIndex);
void SetRadarColor(int nIndex, uint32_t dwColor);
uint32_t TranslateColorCodeToRGBA(uint iCode);
