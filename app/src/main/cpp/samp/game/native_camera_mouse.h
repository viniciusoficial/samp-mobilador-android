#pragma once

struct CCamera;

void NativeCameraMouseEnqueue(float dx, float dy);
void NativeCameraMouseReset();
void NativeCameraMouseApply(CCamera& camera);
void NativeCameraMouseReapply(CCamera& camera);