#pragma once

#include "Camera.h"

// Rota independente do mouse físico. Não usa CPad, vecMouseDelta ou RsMouseEventHandler.
void NativeCameraMouseEnqueue(float dx, float dy);
void NativeCameraMouseReset();
void NativeCameraMouseApply(CCamera& camera);
