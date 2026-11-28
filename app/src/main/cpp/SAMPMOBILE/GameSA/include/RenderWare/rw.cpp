//
// Created by ALYN on 2024/8/7.
//

#include "rw.h"
#include "Core/Vector.h"
#include "Game/Engine/Symbols.h"

RpWorld* RpWorldAddCamera(RpWorld* world, RwCamera* camera)
{
	return eng::fn::RpWorldAddCamera<RpWorld*>( world, camera);
}

RpWorld* RpWorldAddLight(RpWorld* world, RpLight* light)
{
	return eng::fn::RpWorldAddLight<RpWorld*>( world, light);
}

RpWorld* RpWorldRemoveLight(RpWorld* world, RpLight* light)
{
	return eng::fn::RpWorldRemoveLight<RpWorld*>( world, light);
}

void setScissorRect(void* pRect)
{
	return eng::fn::CWidget_SetScissor( pRect);
}

RwReal getNearScreenZ()
{
	return eng::fn::RwIm2DGetNearScreenZ<RwReal>();
}

RwReal getRecipNearClip()
{
	return eng::fn::CSprite2d_SetRecipNearClip<RwReal>();
}

RwCamera* RwCameraCreate()
{
	return eng::fn::RwCameraCreate<RwCamera*>();
}

RwCamera* RwCameraClear(RwCamera* camera, RwRGBA* rgba, RwInt32 i)
{
	return eng::fn::RwCameraClear<RwCamera*>( camera, rgba, i);
}

RwCamera* RwCameraClone(RwCamera* camera)
{
	return eng::fn::RwCameraClone<RwCamera*>( camera);
}

RwCamera* RwCameraBeginUpdate(RwCamera* camera)
{
	return eng::fn::RwCameraBeginUpdate<RwCamera*>( camera);
}

RwCamera* RwCameraEndUpdate(RwCamera* camera)
{
	return eng::fn::RwCameraEndUpdate<RwCamera*>( camera);
}

RwCamera* RwCameraShowRaster(RwCamera* camera, void* pDev, RwUInt32 flags)
{
	return eng::fn::RwCameraShowRaster<RwCamera*>( camera, pDev, flags);
}

RwCamera* RwCameraSetFarClipPlane(RwCamera* camera, RwReal f)
{
	return eng::fn::RwCameraSetFarClipPlane<RwCamera*>( camera, f);
}

RwCamera* RwCameraSetNearClipPlane(RwCamera* camera, RwReal f)
{
	return eng::fn::RwCameraSetNearClipPlane<RwCamera*>( camera, f);
}

RwCamera* RwCameraSetViewWindow(RwCamera* camera, const RwV2d* v)
{
	return eng::fn::RwCameraSetViewWindow<RwCamera*>( camera, v);
}

RwCamera* RwCameraSetProjection(RwCamera* camera, RwCameraProjection projection)
{
	return eng::fn::RwCameraSetProjection<RwCamera*>( camera, projection);
}

RwFrame* RwFrameCreate()
{
	return eng::fn::RwFrameCreate<RwFrame*>();
}

RwFrame* RwFrameTranslate(RwFrame* rwFrame, const RwV3d* v, RwOpCombineType type)
{
	return eng::fn::RwFrameTranslate<RwFrame*>( rwFrame, v, type);
}

RwFrame* RwFrameRotate(RwFrame* rwFrame, const RwV3d* v, RwReal rot, RwOpCombineType type)
{
	return eng::fn::RwFrameRotate<RwFrame*>( rwFrame, v, rot, type);
}

RwFrame* RwFrameForAllObjects(RwFrame* rwFrame, void* callBack, void* data)
{
	// RwFrameForAllObjects(RwFrame *,RwObject * (*)(RwObject *,void *),void *)
	return eng::fn::RwFrameForAllObjects<RwFrame*>( rwFrame, callBack, data);
}

RpLight* RpLightCreate(RwInt32 type)
{
	return eng::fn::RpLightCreate<RpLight*>( type);
}

RpLight* RpLightSetColor(RpLight* rpLight, RwRGBAReal const* v)
{
	return eng::fn::RpLightSetColor<RpLight*>( rpLight, v);
}

RwRaster* RwRasterCreate(RwInt32 width, RwInt32 height, RwInt32 depth, RwInt32 flags)
{
	return eng::fn::RwRasterCreate<RwRaster*>( width, height, depth, flags);
}

RwBool RwRasterDestroy(RwRaster* raster)
{
	return eng::fn::RwRasterDestroy<RwBool>( raster);
}

RwRaster* RwRasterGetOffset(RwRaster* raster, RwInt16* xOffset, RwInt16* yOffset)
{
	return eng::fn::RwRasterGetOffset<RwRaster*>( raster, xOffset, yOffset);
}

RwInt32 RwRasterGetNumLevels(RwRaster* raster)
{
	return eng::fn::RwRasterGetNumLevels<RwInt32>( raster);
}

RwRaster* RwRasterSubRaster(RwRaster* subRaster, RwRaster* raster, RwRect* rect)
{
	return eng::fn::RwRasterSubRaster<RwRaster*>( subRaster, raster, rect);
}

RwRaster* RwRasterRenderFast(RwRaster* raster, RwInt32 x, RwInt32 y)
{
	return eng::fn::RwRasterRenderFast<RwRaster*>( raster, x, y);
}

RwRaster* RwRasterRender(RwRaster* raster, RwInt32 x, RwInt32 y)
{
	return eng::fn::RwRasterRender<RwRaster*>( raster, x, y);
}

RwRaster* RwRasterRenderScaled(RwRaster* raster, RwRect* rect)
{
	return eng::fn::RwRasterRenderScaled<RwRaster*>( raster, rect);
}

RwRaster* RwRasterPushContext(RwRaster* raster)
{
	return eng::fn::RwRasterPushContext<RwRaster*>( raster);
}

RwRaster* RwRasterPopContext()
{
	return eng::fn::RwRasterPopContext<RwRaster*>();
}

RwRaster* RwRasterGetCurrentContext()
{
	return eng::fn::RwRasterGetCurrentContext<RwRaster*>();
}

RwBool RwRasterClear(RwInt32 pixelValue)
{
	return eng::fn::RwRasterClear<RwBool>( pixelValue);
}

RwBool RwRasterClearRect(RwRect* rpRect, RwInt32 pixelValue)
{
	return eng::fn::RwRasterClearRect<RwBool>( rpRect, pixelValue);
}

RwRaster* RwRasterShowRaster(RwRaster* raster, void* dev, RwUInt32 flags)
{
	return eng::fn::RwRasterShowRaster<RwRaster*>( raster, dev, flags);
}

RwUInt8* RwRasterLock(RwRaster* raster, RwUInt8 level, RwInt32 lockMode)
{
	return eng::fn::RwRasterLock<RwUInt8*>( raster, level, lockMode);
}

RwRaster* RwRasterUnlock(RwRaster* raster)
{
	return eng::fn::RwRasterUnlock<RwRaster*>( raster);
}

RwUInt8* RwRasterLockPalette(RwRaster* raster, RwInt32 lockMode)
{
	return eng::fn::RwRasterLockPalette<RwUInt8*>( raster, lockMode);
}

RwRaster* RwRasterUnlockPalette(RwRaster* raster)
{
	return eng::fn::RwRasterUnlockPalette<RwRaster*>( raster);
}

RwImage* RwImageCreate(RwInt32 width, RwInt32 height, RwInt32 depth)
{
	return eng::fn::RwImageCreate<RwImage*>( width, height, depth);
}

RwBool RwImageDestroy(RwImage* image)
{
	return eng::fn::RwImageDestroy<RwBool>( image);
}

RwImage* RwImageAllocatePixels(RwImage* image)
{
	return eng::fn::RwImageAllocatePixels<RwImage*>( image);
}

RwImage* RwImageFreePixels(RwImage* image)
{
	return eng::fn::RwImageFreePixels<RwImage*>( image);
}

RwImage* RwImageCopy(RwImage* destImage, const RwImage* sourceImage)
{
	return eng::fn::RwImageCopy<RwImage*>( destImage, sourceImage);
}

RwImage* RwImageResize(RwImage* image, RwInt32 width, RwInt32 height)
{
	return eng::fn::RwImageResize<RwImage*>( image, width, height);
}

RwImage* RwImageApplyMask(RwImage* image, const RwImage* mask)
{
	return eng::fn::RwImageApplyMask<RwImage*>( image, mask);
}

RwImage* RwImageMakeMask(RwImage* image)
{
	return eng::fn::RwImageMakeMask<RwImage*>( image);
}

RwImage* RwImageReadMaskedImage(const RwChar* imageName, const RwChar* maskname)
{
	return eng::fn::RwImageReadMaskedImage<RwImage*>( imageName, maskname);
}

RwImage* RwImageRead(const RwChar* imageName)
{
	return eng::fn::RwImageRead<RwImage*>( imageName);
}

RwImage* RwImageWrite(RwImage* image, const RwChar* imageName)
{
	return eng::fn::RwImageWrite<RwImage*>( image, imageName);
}

RwImage* RwImageSetFromRaster(RwImage* image, RwRaster* raster)
{
	return eng::fn::RwImageSetFromRaster<RwImage*>( image, raster);
}

RwRaster* RwRasterSetFromImage(RwRaster* raster, RwImage* image)
{
	return eng::fn::RwRasterSetFromImage<RwRaster*>( raster, image);
}

RwRaster* RwRasterRead(const RwChar* filename)
{
	return eng::fn::RwRasterRead<RwRaster*>( filename);
}

RwRaster* RwRasterReadMaskedRaster(const RwChar* filename, const RwChar* maskname)
{
	return eng::fn::RwRasterReadMaskedRaster<RwRaster*>( filename, maskname);
}

RwImage* RwImageFindRasterFormat(RwImage* ipImage, RwInt32 nRasterType, RwInt32* npWidth, RwInt32* npHeight, RwInt32* npDepth, RwInt32* npFormat)
{
	return eng::fn::RwImageFindRasterFormat<RwImage*>( ipImage, nRasterType, npWidth, npHeight, npDepth, npFormat);
}

RwImage* RtPNGImageWrite(RwImage* image, const RwChar* imageName)
{
	return eng::fn::RtPNGImageWrite<RwImage*>( image, imageName);
}

RwImage* RtPNGImageRead(const RwChar* imageName)
{
	return eng::fn::RtPNGImageRead<RwImage*>( imageName);
}

RwTexture* RwTextureCreate(RwRaster* raster)
{
	return eng::fn::RwTextureCreate<RwTexture*>( raster);
}

RwTexture* RwTextureRead(const RwChar* filename, const RwChar* imageName)
{
	return eng::fn::RwTextureRead<RwTexture*>( filename, imageName);
}

RwTexture* RwTextureDestroy(RwTexture* rwTexture)
{
	return eng::fn::RwTextureDestroy<RwTexture*>( rwTexture);
}

RwReal RwIm2DGetNearScreenZ()
{
	return eng::fn::RwIm2DGetNearScreenZ<RwReal>();
}

RwReal RwIm2DGetFarScreenZ()
{
	return eng::fn::RwIm2DGetFarScreenZ<RwReal>();
}

RwBool RwRenderStateGet(RwRenderState state, void* value)
{
	return eng::fn::RwRenderStateGet<RwReal>( state, value);
}

RwBool RwRenderStateSet(RwRenderState state, void* value)
{
	return eng::fn::RwRenderStateSet<RwReal>( state, value);
}

RwBool RwIm2DRenderLine(RwIm2DVertex* vertices, RwInt32 numVertices, RwInt32 vert1, RwInt32 vert2)
{
	return eng::fn::RwIm2DRenderLine<RwReal>( vertices, numVertices, vert1, vert2);
}

RwBool RwIm2DRenderTriangle(RwIm2DVertex* vertices, RwInt32 numVertices, RwInt32 vert1, RwInt32 vert2, RwInt32 vert3)
{
	return eng::fn::RwIm2DRenderTriangle<RwReal>( vertices, numVertices, vert1, vert2, vert3);
}

RwBool RwIm2DRenderPrimitive(RwPrimitiveType primType, RwIm2DVertex* vertices, RwInt32 numVertices)
{
	return eng::fn::RwIm2DRenderPrimitive<RwReal>( primType, vertices, numVertices);
}

RwBool RwIm2DRenderIndexedPrimitive(RwPrimitiveType primType, RwIm2DVertex* vertices, RwInt32 numVertices, RwImVertexIndex* indices, RwInt32 numIndices)
{
	return eng::fn::RwIm2DRenderIndexedPrimitive<RwBool>( primType, vertices, numVertices, indices, numIndices);
}

RwObject* RwObjectHasFrameSetFrame(RwCamera* camera, RwFrame* rwFrame)
{
	return eng::fn::rwObjectHasFrameSetFrame<RwObject*>( camera, rwFrame);
}


// RW MATRIX

RwMatrix* RwMatrixUpdate(RwMatrix* matrix)
{
	matrix->flags &= 0xFFFDFFFC;
	return matrix;
}

RwBool RwMatrixDestroy(RwMatrix* matrix)
{
	return eng::fn::RwMatrixDestroy<RwBool>( matrix);
}
