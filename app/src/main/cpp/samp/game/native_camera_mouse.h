#pragma once

#include <cstdint>

class CCamera;
struct CCam;

void NativeCameraMouseEnqueue(float dx, float dy);
void NativeCameraMouseEnqueueWheel(int steps);
void NativeCameraMouseSetButtons(uint8_t buttonMask);
void NativeCameraMouseReset();
void NativeCameraMouseSetOptions(float maxDelta, float smoothing, float aimSensitivity, bool vehicleAutoCenter);
uint32_t NativeCameraMouseGetAppliedCount();

void NativeCameraMousePreProcess(CCamera& camera, CCam* cam);
void NativeCameraMousePostProcess(CCamera& camera, CCam* cam);
void NativeCameraMouseApply(CCamera& camera);
