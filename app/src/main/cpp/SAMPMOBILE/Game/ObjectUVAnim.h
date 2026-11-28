#pragma once

#include <RenderWare/rw.h>

// Software RenderWare UVAnim keyframe evaluator for objects and world materials.
void BeginUVAnimFrame();
void ForEachObjectAtomic(RwObject* rwObj, RpAtomic* (*cb)(RpAtomic*, void*), void* data);
RpMaterial* DriveUVAnimMaterialCB(RpMaterial* material, void* data);
RwMatrix* FindAtomicUVAnimMatrix(RpAtomic* atomic, bool* outHasDualUV = nullptr);
