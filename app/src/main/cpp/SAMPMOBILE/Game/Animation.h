#pragma once

#include <cstdint>

const char* GetAnimByIdx(int idx);
int GetAnimIdxByName(const char* szName);
uint16_t GetAnimIdxByHashKey(uint32_t hashKey);
void RecordLocalPedQueuedAnim(int groupId, int animId, float blendDelta);
void ResetLocalPedQueuedAnim();
uint32_t PackLocalPedQueuedAnim();
