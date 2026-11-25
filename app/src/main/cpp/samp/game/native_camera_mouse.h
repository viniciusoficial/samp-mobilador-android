#pragma once

#include <cstdint>

struct CCam;
struct CCamera;

void NativeCameraMouseEnqueue(float dx, float dy);
void NativeCameraMouseReset();
void NativeCameraMouseSetOptions(float maxDelta, float smoothing, float aimSensitivity, bool vehicleAutoCenter);
uint32_t NativeCameraMouseGetAppliedCount();

// Called inside CCam__Process_hook around original CCam::Process(cam)
void NativeCameraMousePreProcess(CCamera& camera, CCam* cam);
void NativeCameraMousePostProcess(CCamera& camera, CCam* cam);

// Legacy entry point kept for compatibility (now a light frame check)
void NativeCameraMouseApply(CCamera& camera);
