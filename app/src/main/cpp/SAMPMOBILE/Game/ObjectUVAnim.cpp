#include "ObjectUVAnim.h"
#include "Game.h"
#include "Object.h"
#include "Util.h"
#include "Engine/Symbols.h"
#include "../Client.h"
#include "RenderWare/rpuvanim.h"
#include <SA.h>
#include <spdlog/spdlog.h>
#include <cmath>
#include <cstring>
#include <unordered_set>

static std::unordered_set<RpMaterial*> s_uvAnimUpdatedMaterialsThisFrame;
static float s_fUVAnimFrameDt = 0.016f;
static uint32_t s_dwUVAnimLastTick = 0;


void BeginUVAnimFrame()
{
	s_uvAnimUpdatedMaterialsThisFrame.clear();
	uint32_t dwNow = GetTickCount();
	if (s_dwUVAnimLastTick == 0) {
		s_dwUVAnimLastTick = dwNow;
		s_fUVAnimFrameDt = 0.016f;
		return;
	}
	float dt = (dwNow - s_dwUVAnimLastTick) / 1000.0f;
	s_dwUVAnimLastTick = dwNow;
	if (std::isfinite(dt) && dt > 0.0f) {
		s_fUVAnimFrameDt = dt > 0.1f ? 0.1f : dt;
	}
	else {
		s_fUVAnimFrameDt = 0.016f;
	}
}

void ForEachObjectAtomic(RwObject* rwObj, RpAtomic* (*cb)(RpAtomic*, void*), void* data)
{
	if (!rwObj || !cb) {
		return;
	}

	uint8_t type = *(uint8_t*) rwObj;
	if (type == 1 /*rpATOMIC*/) {
		cb(reinterpret_cast<RpAtomic*>(rwObj), data);
	}
	else if (type == 2 /*rpCLUMP*/) {
		eng::fn::RpClumpForAllAtomics<RpClump*>(
				reinterpret_cast<RpClump*>(rwObj), cb, data);
	}
	else if (rwObj->parent) {
		auto parent = reinterpret_cast<RwFrame*>(rwObj->parent);
		RwFrameForAllObjects(parent, reinterpret_cast<RpAtomicCallBack*>(cb), data);
	}
}

RpMaterial* EnableUVAnimMaterialCB(RpMaterial* material, void* data)
{
	if (!material) {
		return material;
	}

	bool bHasUVAnim = eng::fn::RpMaterialUVAnimExists( material);
	if (!bHasUVAnim) {
		return material;
	}

	if (data) {
		*(bool*) data = true;
	}

	// Ensure material texture uses WRAP addressing so scrolling UVs tile seamlessly.
	if (material->texture) {
		RwTextureSetAddressingMacro(material->texture, rwTEXTUREADDRESSWRAP);
	}

	// If the material uses MatFX UV transform (5 = UVTRANSFORM, 6 = DUALUVTRANSFORM),
	// link its matrix slots to ext->uv[0..1] updated by DriveUVAnimMaterialCB.
	int32_t iEffects = eng::fn::RpMatFXMaterialGetEffects( material);
	if (iEffects == 5 /*rpMATFXEFFECTUVTRANSFORM*/ || iEffects == 6 /*rpMATFXEFFECTDUALUVTRANSFORM*/) {
		auto* pUVAnimGlobals = eng::as<RpUVAnimMaterialGlobalVars*>(eng::Sym::RpUVAnimMaterialGlobals);
		if (pUVAnimGlobals) {
			auto* ext = (RpUVAnimMaterialExtension*) ((char*) material + pUVAnimGlobals->engineOffset);
			if (ext->uv[0] || ext->uv[1]) {
				eng::fn::RpMatFXMaterialSetUVTransformMatrices( material, ext->uv[0], ext->uv[1]);
			}
		}
	}

	return material;
}

RpAtomic* EnableUVAnimAtomicCB(RpAtomic* atomic, void* data)
{
	if (atomic && *(uint8_t*) atomic == 1 /*rpATOMIC*/ && atomic->geometry) {
		bool bHasUVAnimMaterial = false;
		eng::fn::RpGeometryForAllMaterials<RpGeometry*>( atomic->geometry, (RpMaterial* (*)(RpMaterial*, void*)) EnableUVAnimMaterialCB, &bHasUVAnimMaterial);
		if (!bHasUVAnimMaterial) {
			return atomic;
		}

		if (data) {
			auto* pObj = reinterpret_cast<CObject*>(data);
			pObj->m_bUVAnimEnabled = true;

			static std::unordered_set<int> s_loggedModels;
			if (s_loggedModels.size() < 24 && s_loggedModels.insert(pObj->GetModelIndex()).second) {
				spdlog::debug("UVAnim: enabled for object model {} (atomic={:p}, pipeline={:p})",
						pObj->GetModelIndex(), (void*) atomic, (void*) atomic->pipeline);
			}
		}
		// Do NOT switch atomic->pipeline here: switching an already-instanced
		// CustomBuilding atomic to MatFX triggers vertex re-instancing on a
		// geometry whose morphTarget->verts has been freed (SIGSEGV at +0x3d4).
		// Instead, CustomPipeRenderCB / rxOpenGLDefaultAllInOneRenderCB hooks
		// apply ext->uv[0] via emu_SetTextureMatrixEnabled directly.
	}

	return atomic;
}

RpMaterial* DriveUVAnimMaterialCB(RpMaterial* material, void* data)
{
	if (!material) {
		return material;
	}

	// Multiple object instances of the same model share one RpMaterial —
	// advance each material at most once per game frame.
	if (!s_uvAnimUpdatedMaterialsThisFrame.insert(material).second) {
		return material;
	}

	if (!eng::fn::RpMaterialUVAnimExists( material)) {
		return material;
	}

	auto* pUVAnimGlobals = eng::as<RpUVAnimMaterialGlobalVars*>(eng::Sym::RpUVAnimMaterialGlobals);
	if (!pUVAnimGlobals) {
		return material;
	}

	auto* ext = (RpUVAnimMaterialExtension*) ((char*) material + pUVAnimGlobals->engineOffset);
	if (!ext->uv[0] && !ext->uv[1]) {
		return material;
	}

	// War Drum's mobile port gates RtAnimInterpolatorAddAnimTime behind
	// Set360UVAnimHack (default false), and enabling it hits an infinite loop
	// in RtAnimInterpolatorAddAnimTime on multi-keyframe DFFs (e.g. 19620/19797)
	// plus RpUVAnimParamKeyFrameApply has a 64-bit struct offset bug (+12 vs +16).
	// Step and evaluate UV keyframes directly in a bounded, crash-proof loop.
	//
	// RenderWare interpolator typeIDs (verified from libGTASA.so):
	//   0x1c0 (448) = _rpUVAnimLinearInterpolatorInfo (uv[6])
	//   0x1c1 (449) = _rpUVAnimParamInterpolatorInfo  (theta, s0, s1, skew, x, y)
	constexpr int32_t kUVAnimTypeLinear = 0x1c0;
	constexpr int32_t kUVAnimTypeParam  = 0x1c1;
	constexpr float kPi    = 3.14159265358979323846f;
	constexpr float kTwoPi = 6.28318530717958647692f;

	bool bChanWritten[RP_UVANIM_APPLIEDCHANNELS] = {false, false};

	for (int slot = 0; slot < RP_UVANIM_MAXSLOTS; ++slot) {
		RtAnimInterpolator* interp = ext->interp[slot];
		if (!interp || !interp->pCurrentAnim) {
			continue;
		}
		RtAnimAnimation* anim = interp->pCurrentAnim;
		if (!anim->pFrames || anim->numFrames < 2 || anim->duration <= 0.0001f || !std::isfinite(anim->duration)) {
			continue;
		}

		// Advance and wrap currentTime strictly inside [0, duration)
		float curTime = interp->currentTime;
		if (!std::isfinite(curTime) || curTime < 0.0f) {
			curTime = 0.0f;
		}
		curTime = std::fmod(curTime + s_fUVAnimFrameDt, anim->duration);
		if (curTime < 0.0f || !std::isfinite(curTime)) {
			curTime = 0.0f;
		}
		interp->currentTime = curTime;

		int32_t typeID = anim->interpInfo ? anim->interpInfo->typeID : kUVAnimTypeParam;
		if (typeID != kUVAnimTypeLinear && typeID != kUVAnimTypeParam) {
			continue;
		}

		const auto* kfs = reinterpret_cast<const RpUVAnimKeyFrame*>(anim->pFrames);
		int count = anim->numFrames;
		if (count > 1024) {
			count = 1024;
		}

		int numNodes = interp->numNodes;
		if (numNodes < 1) {
			numNodes = 1;
		}
		if (numNodes > RP_UVANIM_MAXSLOTS) {
			numNodes = RP_UVANIM_MAXSLOTS;
		}
		if (numNodes > count) {
			numNodes = 1;
		}

		// For multi-node animations (e.g. dual-channel waterfalls), classify
		// each keyframe's owning node via validated prevFrame back-links.
		int8_t nodeOf[1024];
		if (numNodes > 1) {
			const uintptr_t baseAddr = reinterpret_cast<uintptr_t>(kfs);
			const size_t stride = sizeof(RpUVAnimKeyFrame);
			for (int k = 0; k < count; ++k) {
				if (k < numNodes) {
					nodeOf[k] = static_cast<int8_t>(k);
				}
				else {
					int8_t owner = 0;
					const uintptr_t prevAddr = reinterpret_cast<uintptr_t>(kfs[k].prevFrame);
					if (prevAddr >= baseAddr && prevAddr < baseAddr + static_cast<size_t>(k) * stride
							&& ((prevAddr - baseAddr) % stride) == 0) {
						int prevIdx = static_cast<int>((prevAddr - baseAddr) / stride);
						owner = nodeOf[prevIdx];
					}
					nodeOf[k] = owner;
				}
			}
		}

		const auto* customData = reinterpret_cast<const _rpUVAnimCustomData*>(anim->customData);

		for (int node = 0; node < numNodes; ++node) {
			uint32_t chan = customData ? customData->nodeToUVChannelMap[node] : 0u;
			if (chan >= RP_UVANIM_APPLIEDCHANNELS) {
				continue;
			}
			RwMatrix* dst = ext->uv[chan];
			if (!dst) {
				continue;
			}

			// Find bracketing keyframe pair for this node using strict upper bound
			// so step/duplicate timestamps (e.g. LightBar1 0.500/0.500) switch cleanly.
			int idx1 = -1;
			int idx2 = -1;
			int prevNodeIdx = -1;
			for (int k = 0; k < count; ++k) {
				if (numNodes > 1 && nodeOf[k] != node) {
					continue;
				}
				if (prevNodeIdx >= 0) {
					float t1 = kfs[prevNodeIdx].time;
					float t2 = kfs[k].time;
					if (curTime >= t1 && curTime < t2) {
						idx1 = prevNodeIdx;
						idx2 = k;
						break;
					}
				}
				prevNodeIdx = k;
			}
			if (idx1 < 0) {
				// curTime is either before first keyframe or at/past last keyframe
				int firstNodeIdx = -1;
				for (int k = 0; k < count; ++k) {
					if (numNodes == 1 || nodeOf[k] == node) {
						firstNodeIdx = k;
						break;
					}
				}
				if (firstNodeIdx < 0) {
					continue;
				}
				if (curTime < kfs[firstNodeIdx].time) {
					idx1 = firstNodeIdx;
					idx2 = firstNodeIdx;
				}
				else {
					idx1 = prevNodeIdx;
					idx2 = prevNodeIdx;
				}
			}

			const RpUVAnimKeyFrame& kf1 = kfs[idx1];
			const RpUVAnimKeyFrame& kf2 = kfs[idx2];

			float span = kf2.time - kf1.time;
			float alpha = (span > 0.00001f) ? ((curTime - kf1.time) / span) : 0.0f;
			if (alpha < 0.0f) alpha = 0.0f;
			if (alpha > 1.0f) alpha = 1.0f;

			float rx = 1.0f, ry = 0.0f;
			float ux = 0.0f, uy = 1.0f;
			float tx = 0.0f, ty = 0.0f;

			if (typeID == kUVAnimTypeLinear) {
				float uv[6];
				for (int j = 0; j < 6; ++j) {
					uv[j] = kf1.data.linear.uv[j] + (kf2.data.linear.uv[j] - kf1.data.linear.uv[j]) * alpha;
				}
				rx = uv[0]; ry = uv[1];
				ux = uv[2]; uy = uv[3];
				tx = uv[4]; ty = uv[5];
			}
			else {
				// Parametric keyframe (matches RpUVAnimParamKeyFrameInterpolate + Apply):
				//   M = Base(s0, skew, s1, x, y) * T(-0.5, -0.5) * R_z(theta) * T(+0.5, +0.5)
				float dTheta = kf2.data.param.theta - kf1.data.param.theta;
				if (dTheta > kPi) {
					dTheta -= kTwoPi;
				}
				else if (dTheta < -kPi) {
					dTheta += kTwoPi;
				}
				float theta = kf1.data.param.theta + dTheta * alpha;
				float s0   = kf1.data.param.s0   + (kf2.data.param.s0   - kf1.data.param.s0)   * alpha;
				float s1   = kf1.data.param.s1   + (kf2.data.param.s1   - kf1.data.param.s1)   * alpha;
				float skew = kf1.data.param.skew + (kf2.data.param.skew - kf1.data.param.skew) * alpha;
				float px   = kf1.data.param.x    + (kf2.data.param.x    - kf1.data.param.x)    * alpha;
				float py   = kf1.data.param.y    + (kf2.data.param.y    - kf1.data.param.y)    * alpha;

				float c = std::cos(theta);
				float s = std::sin(theta);
				rx = s0 * c - skew * s;
				ry = s0 * s + skew * c;
				ux = -s1 * s;
				uy =  s1 * c;
				tx = (px - 0.5f) * c - (py - 0.5f) * s + 0.5f;
				ty = (px - 0.5f) * s + (py - 0.5f) * c + 0.5f;
			}

			if (!bChanWritten[chan]) {
				dst->right.x = rx;
				dst->right.y = ry;
				dst->right.z = 0.0f;
				dst->up.x = ux;
				dst->up.y = uy;
				dst->up.z = 0.0f;
				dst->at.x = 0.0f;
				dst->at.y = 0.0f;
				dst->at.z = 1.0f;
				dst->pos.x = tx;
				dst->pos.y = ty;
				dst->pos.z = 0.0f;
				// Clear rwMATRIXINTERNALIDENTITY (0x20000) so _rpMatFXOpenGLUVAnimRender uploads it
				dst->flags = 0;
				bChanWritten[chan] = true;
			}
			else {
				// Compose multiple slots targeting the same UV channel: dst = dst * slotMat
				float arx = dst->right.x, ary = dst->right.y;
				float aux = dst->up.x,    auy = dst->up.y;
				float atx = dst->pos.x,   aty = dst->pos.y;
				dst->right.x = arx * rx + ary * ux;
				dst->right.y = arx * ry + ary * uy;
				dst->up.x    = aux * rx + auy * ux;
				dst->up.y    = aux * ry + auy * uy;
				dst->pos.x   = atx * rx + aty * ux + tx;
				dst->pos.y   = atx * ry + aty * uy + ty;
				dst->flags = 0;
			}
		}
	}

	if (bChanWritten[0] || bChanWritten[1]) {
		// Guard against SIGSEGV: RpMatFXMaterialSetUVTransformMatrices dereferences
		// material's MatFX extension without a null check — only call when MatFX UV
		// transform effect (5 or 6) is present.
		int32_t iEffects = eng::fn::RpMatFXMaterialGetEffects( material);
		if (iEffects == 5 /*rpMATFXEFFECTUVTRANSFORM*/ || iEffects == 6 /*rpMATFXEFFECTDUALUVTRANSFORM*/) {
			eng::fn::RpMatFXMaterialSetUVTransformMatrices(
					material, ext->uv[0], ext->uv[1]);
		}
	}
	return material;
}

/*
 * CObject::PreRender is self-contained and does not run CEntity::PreRender's
 * MaterialUpdateUVAnimCB driver, so nothing advances the animation for game
 * objects. Drive UV-animated materials per frame from CObject::Process and
 * CPlayerPed::ProcessAttachedObjects.
 */
RpAtomic* DriveUVAnimAtomicCB(RpAtomic* atomic, void* data)
{
	if (atomic && *(uint8_t*) atomic == 1 /*rpATOMIC*/ && atomic->geometry) {
		eng::fn::RpGeometryForAllMaterials<RpGeometry*>(
				atomic->geometry, (RpMaterial* (*)(RpMaterial*, void*)) DriveUVAnimMaterialCB, nullptr);
	}

	return atomic;
}

RwMatrix* FindAtomicUVAnimMatrix(RpAtomic* atomic, bool* outHasDualUV)
{
	if (outHasDualUV) {
		*outHasDualUV = false;
	}
	if (!atomic || *(uint8_t*) atomic != 1 /*rpATOMIC*/ || !atomic->geometry) {
		return nullptr;
	}
	RpGeometry* geo = atomic->geometry;
	if (!geo->matList.materials || geo->matList.numMaterials <= 0) {
		return nullptr;
	}
	auto* pUVAnimGlobals = eng::as<RpUVAnimMaterialGlobalVars*>(eng::Sym::RpUVAnimMaterialGlobals);
	if (!pUVAnimGlobals) {
		return nullptr;
	}
	int count = geo->matList.numMaterials;
	if (count > 64) count = 64;
	RwMatrix* firstUv = nullptr;
	for (int i = 0; i < count; ++i) {
		RpMaterial* mat = geo->matList.materials[i];
		if (!mat) continue;
		if (!eng::fn::RpMaterialUVAnimExists( mat)) {
			continue;
		}
		auto* ext = (RpUVAnimMaterialExtension*) ((char*) mat + pUVAnimGlobals->engineOffset);
		RwMatrix* uv = ext->uv[0] ? ext->uv[0] : ext->uv[1];
		if (uv && (uv->flags & 0x20000) == 0) {
			if (!firstUv) {
				firstUv = uv;
			}
			if (outHasDualUV) {
				int32_t iEffects = eng::fn::RpMatFXMaterialGetEffects( mat);
				if (iEffects == 6 /*rpMATFXEFFECTDUALUVTRANSFORM*/) {
					*outHasDualUV = true;
				}
			}
			else {
				return firstUv;
			}
		}
	}
	return firstUv;
}

void CObject::TryEnableUVAnim()
{
	if (!m_entity || !m_entity->m_pRwObject) {
		m_pUVAnimRwObject = nullptr;
		m_bUVAnimEnabled = false;
		return;
	}

	if (m_pUVAnimRwObject == m_entity->m_pRwObject) {
		return;
	}

	m_pUVAnimRwObject = m_entity->m_pRwObject;
	m_bUVAnimEnabled = false;
	ForEachObjectAtomic(m_entity->m_pRwObject, EnableUVAnimAtomicCB, this);
}

void CObject::UpdateUVAnim()
{
	TryEnableUVAnim();

	if (m_bUVAnimEnabled && m_entity && m_entity->m_pRwObject) {
		ForEachObjectAtomic(m_entity->m_pRwObject, DriveUVAnimAtomicCB, this);
	}
}
