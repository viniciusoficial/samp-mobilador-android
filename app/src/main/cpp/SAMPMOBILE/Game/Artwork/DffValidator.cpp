#include "DffValidator.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace artwork {

namespace {

constexpr uint32_t kRwStruct = 0x0001u;
constexpr uint32_t kRwString = 0x0002u;
constexpr uint32_t kRwExtension = 0x0003u;
constexpr uint32_t kRwTexture = 0x0006u;
constexpr uint32_t kRwMaterial = 0x0007u;
constexpr uint32_t kRwMaterialList = 0x0008u;
constexpr uint32_t kRwFrameList = 0x000Eu;
constexpr uint32_t kRwGeometry = 0x000Fu;
constexpr uint32_t kRwClump = 0x0010u;
constexpr uint32_t kRwAtomic = 0x0014u;
constexpr uint32_t kRwGeometryList = 0x001Au;
constexpr uint32_t kRwUvAnimDict = 0x002Bu;

constexpr uint32_t kRwBinMeshPlg = 0x050Eu;
constexpr uint32_t kRwSkinPlg = 0x0116u;
constexpr uint32_t kRw2dEffectPlg = 0x0253F2F8u;
constexpr uint32_t kRwGtaCollisionPlg = 0x0253F2FAu;
constexpr uint32_t kRwNodeNamePlg = 0x0253F2FEu;
constexpr uint32_t kRwSampCollisionPlg = 0x0253F2FFu;

constexpr uint32_t kFourCcCol1 = 0x4C4C4F43u; // "COLL"
constexpr uint32_t kFourCcCol2 = 0x324C4F43u; // "COL2"
constexpr uint32_t kFourCcCol3 = 0x334C4F43u; // "COL3"
constexpr uint32_t kFourCcCol4 = 0x344C4F43u; // "COL4"

constexpr uint32_t kMaxFrames = 256u;
constexpr uint32_t kMaxGeometries = 64u;
constexpr uint32_t kMaxVertices = 200000u;
constexpr uint32_t kMaxTriangles = 100000u;
constexpr uint32_t kMaxMaterials = 256u;
constexpr uint32_t kMaxBones = 64u;
constexpr uint32_t kMaxColBytes = 2u * 1024u * 1024u; // 2 MiB

inline uint16_t readLE16(const uint8_t* p) noexcept
{
	return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

inline uint32_t readLE32(const uint8_t* p) noexcept
{
	return static_cast<uint32_t>(p[0]) |
	       (static_cast<uint32_t>(p[1]) << 8) |
	       (static_cast<uint32_t>(p[2]) << 16) |
	       (static_cast<uint32_t>(p[3]) << 24);
}

inline int32_t readLE32S(const uint8_t* p) noexcept
{
	return static_cast<int32_t>(readLE32(p));
}

inline void writeLE32(uint8_t* p, uint32_t v) noexcept
{
	p[0] = static_cast<uint8_t>(v & 0xFFu);
	p[1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
	p[2] = static_cast<uint8_t>((v >> 16) & 0xFFu);
	p[3] = static_cast<uint8_t>((v >> 24) & 0xFFu);
}

struct ChunkView {
	uint32_t type = 0;
	uint32_t length = 0;
	uint32_t version = 0;
	size_t headerOffset = 0;
	size_t payloadStart = 0;
	size_t payloadEnd = 0;
};

bool readChunk(const uint8_t* data, size_t offset, size_t limit, ChunkView& out) noexcept
{
	if (offset > limit || (limit - offset) < 12u) {
		return false;
	}
	const uint32_t type = readLE32(data + offset);
	const uint32_t length = readLE32(data + offset + 4);
	const uint32_t version = readLE32(data + offset + 8);
	const size_t payStart = offset + 12u;
	if (length > (limit - payStart)) {
		return false;
	}
	out.type = type;
	out.length = length;
	out.version = version;
	out.headerOffset = offset;
	out.payloadStart = payStart;
	out.payloadEnd = payStart + length;
	return true;
}

uint32_t unpackRwVersion(uint32_t libId) noexcept
{
	if ((libId & 0xFFFF0000u) != 0) {
		return (((libId >> 14) & 0x3FF00u) + 0x30000u) | ((libId >> 16) & 0x3Fu);
	}
	return libId << 8;
}

void pushUniqueTexName(std::vector<std::string>& list, const std::string& name)
{
	if (name.empty()) return;
	for (const auto& existing : list) {
		if (existing == name) return;
	}
	list.push_back(name);
}

inline float readLEFloat(const uint8_t* p) noexcept
{
	const uint32_t bits = readLE32(p);
	float f = 0.0f;
	std::memcpy(&f, &bits, sizeof(float));
	return f;
}

bool validateFrameList(const uint8_t* data, const ChunkView& fl, uint32_t& outFrameCount, std::string& err)
{
	ChunkView st{};
	if (!readChunk(data, fl.payloadStart, fl.payloadEnd, st) || st.type != kRwStruct) {
		err = "FrameList missing struct";
		return false;
	}
	if (st.length < 4u) {
		err = "FrameList struct truncated";
		return false;
	}
	const uint32_t count = readLE32(data + st.payloadStart);
	if (count == 0 || count > kMaxFrames) {
		err = "FrameList count out of range";
		return false;
	}
	// Each RwFrame struct entry is 56 bytes: 3x3 matrix (36) + pos (12) + parentIndex (4) + flags (4)
	if (st.length < 4u + static_cast<size_t>(count) * 56u) {
		err = "FrameList entries truncated";
		return false;
	}
	std::vector<int32_t> parents(count, -1);
	for (uint32_t i = 0; i < count; ++i) {
		const size_t entryOff = st.payloadStart + 4u + static_cast<size_t>(i) * 56u;
		for (size_t f = 0; f < 12u; ++f) {
			const float v = readLEFloat(data + entryOff + f * 4u);
			if (!std::isfinite(v) || std::fabs(v) > 1.0e6f) {
				err = "FrameList contains non-finite/extreme transform value";
				return false;
			}
		}
		const int32_t parent = readLE32S(data + entryOff + 48u);
		if (parent < -1 || parent >= static_cast<int32_t>(count) || parent == static_cast<int32_t>(i)) {
			err = "FrameList has invalid parent index";
			return false;
		}
		parents[i] = parent;
	}
	// Acyclic check
	for (uint32_t i = 0; i < count; ++i) {
		int32_t cur = parents[i];
		uint32_t steps = 0;
		while (cur >= 0) {
			if (++steps > count) {
				err = "FrameList contains parent cycle";
				return false;
			}
			cur = parents[static_cast<size_t>(cur)];
		}
	}

	// Validate each frame's Extension chunk: match PC 0.3.DL CDffFile::VerifyFrameExtension()
	// by rejecting rwID_NODENAME (0x0253F2FE) >= 24 bytes, which overflows GTA:SA's fixed 24B
	// NodeNameStreamRead buffer.
	size_t off = st.payloadEnd;
	while (off < fl.payloadEnd) {
		ChunkView child{};
		if (!readChunk(data, off, fl.payloadEnd, child)) {
			err = "FrameList extension chunk truncated";
			return false;
		}
		if (child.type == kRwExtension) {
			size_t eOff = child.payloadStart;
			while (eOff < child.payloadEnd) {
				ChunkView ec{};
				if (!readChunk(data, eOff, child.payloadEnd, ec)) {
					err = "FrameList extension sub-chunk truncated";
					return false;
				}
				if (ec.type == kRwNodeNamePlg && ec.length >= 24u) {
					err = "FrameList NodeName exceeds 23-byte engine limit";
					return false;
				}
				eOff = ec.payloadEnd;
			}
		}
		off = child.payloadEnd;
	}

	outFrameCount = count;
	return true;
}

bool parseMaterialList(const uint8_t* data, const ChunkView& ml, uint32_t& outMaterialCount,
	std::vector<std::string>& outTexNames, std::string& err)
{
	ChunkView st{};
	if (!readChunk(data, ml.payloadStart, ml.payloadEnd, st) || st.type != kRwStruct || st.length < 4u) {
		err = "MaterialList missing struct";
		return false;
	}
	const uint32_t count = readLE32(data + st.payloadStart);
	if (count > kMaxMaterials) {
		err = "MaterialList count exceeds limit";
		return false;
	}
	if (st.length < 4u + static_cast<size_t>(count) * 4u) {
		err = "MaterialList indices truncated";
		return false;
	}
	outMaterialCount = count;

	size_t off = st.payloadEnd;
	while (off < ml.payloadEnd) {
		ChunkView child{};
		if (!readChunk(data, off, ml.payloadEnd, child)) {
			err = "MaterialList child chunk truncated";
			return false;
		}
		if (child.type == kRwMaterial) {
			size_t mOff = child.payloadStart;
			while (mOff < child.payloadEnd) {
				ChunkView mc{};
				if (!readChunk(data, mOff, child.payloadEnd, mc)) {
					err = "Material child chunk truncated";
					return false;
				}
				if (mc.type == kRwTexture) {
					size_t tOff = mc.payloadStart;
					bool gotName = false;
					while (tOff < mc.payloadEnd) {
						ChunkView tc{};
						if (!readChunk(data, tOff, mc.payloadEnd, tc)) {
							err = "Texture child chunk truncated";
							return false;
						}
						if (tc.type == kRwString && !gotName) {
							size_t len = 0;
							while (len < tc.length && data[tc.payloadStart + len] != 0) ++len;
							if (len > 0 && len < 64u) {
								pushUniqueTexName(outTexNames, std::string(reinterpret_cast<const char*>(data + tc.payloadStart), len));
							}
							gotName = true;
						}
						tOff = tc.payloadEnd;
					}
				}
				mOff = mc.payloadEnd;
			}
		}
		off = child.payloadEnd;
	}
	return true;
}

bool validateGeometry(const uint8_t* data, const ChunkView& geom, DffValidationResult& res,
	std::vector<size_t>& out2dfxLightSizePatches, std::string& err)
{
	ChunkView st{};
	if (!readChunk(data, geom.payloadStart, geom.payloadEnd, st) || st.type != kRwStruct) {
		err = "Geometry missing struct";
		return false;
	}
	if (st.length < 16u) {
		err = "Geometry struct truncated";
		return false;
	}

	const uint32_t format = readLE32(data + st.payloadStart);
	const uint32_t numTriangles = readLE32(data + st.payloadStart + 4u);
	const uint32_t numVertices = readLE32(data + st.payloadStart + 8u);
	const uint32_t numMorphTargets = readLE32(data + st.payloadStart + 12u);

	if (numVertices > kMaxVertices || numTriangles > kMaxTriangles || numMorphTargets == 0 || numMorphTargets > 8u) {
		err = "Geometry counts out of safe range";
		return false;
	}

	const uint32_t rwVer = unpackRwVersion(st.version);
	size_t cur = st.payloadStart + 16u;
	if (rwVer < 0x34000u) {
		// Older RW versions store 3 surface floats (ambient, specular, diffuse) here.
		if (st.payloadEnd - cur < 12u) {
			err = "Geometry pre-3.4 surface props truncated";
			return false;
		}
		cur += 12u;
	}

	const bool isNative = (format & 0x01000000u) != 0;
	uint32_t materialCount = 0;
	size_t off = st.payloadEnd;
	while (off < geom.payloadEnd) {
		ChunkView child{};
		if (!readChunk(data, off, geom.payloadEnd, child)) {
			err = "Geometry child chunk truncated";
			return false;
		}
		if (child.type == kRwMaterialList) {
			if (!parseMaterialList(data, child, materialCount, res.referencedTextures, err)) {
				return false;
			}
		} else if (child.type == kRwExtension) {
			size_t eOff = child.payloadStart;
			while (eOff < child.payloadEnd) {
				ChunkView ec{};
				if (!readChunk(data, eOff, child.payloadEnd, ec)) {
					err = "Geometry extension chunk truncated";
					return false;
				}
				if (ec.type == kRwSkinPlg) {
					if (ec.length < 4u) {
						err = "SkinPLG truncated";
						return false;
					}
					const uint8_t boneCount = data[ec.payloadStart];
					const uint8_t usedBoneCount = data[ec.payloadStart + 1u];
					if (boneCount > kMaxBones || usedBoneCount > boneCount) {
						err = "SkinPLG bone count exceeds limit (64)";
						return false;
					}
					if (!isNative) {
						// Standard RW SkinPLG payload: 4B header + usedBones + numVerts*(4B indices + 16B weights) + boneCount*64B invMatrices
						const size_t minSkinBytes = 4u + static_cast<size_t>(usedBoneCount)
							+ static_cast<size_t>(numVertices) * 20u
							+ static_cast<size_t>(boneCount) * 64u;
						if (ec.length < minSkinBytes) {
							err = "SkinPLG payload truncated";
							return false;
						}
					}
					res.hasSkinPlg = true;
				} else if (ec.type == kRwBinMeshPlg) {
					if (ec.length < 12u) {
						err = "BinMeshPLG truncated";
						return false;
					}
					const uint32_t numSplits = readLE32(data + ec.payloadStart + 4u);
					if (numSplits > 4096u) {
						err = "BinMeshPLG split count exceeds limit";
						return false;
					}
					size_t bmCur = ec.payloadStart + 12u;
					for (uint32_t s = 0; s < numSplits; ++s) {
						if (ec.payloadEnd - bmCur < 8u) {
							err = "BinMeshPLG split header truncated";
							return false;
						}
						const uint32_t idxCount = readLE32(data + bmCur);
						const uint32_t matIdx = readLE32(data + bmCur + 4u);
						bmCur += 8u;
						if (materialCount > 0 && matIdx >= materialCount) {
							err = "BinMeshPLG split material index out of bounds";
							return false;
						}
						if (!isNative) {
							if (idxCount > (ec.payloadEnd - bmCur) / 4u) {
								err = "BinMeshPLG split indices truncated";
								return false;
							}
							for (uint32_t k = 0; k < idxCount; ++k) {
								const uint32_t vIdx = readLE32(data + bmCur + static_cast<size_t>(k) * 4u);
								if (vIdx >= numVertices) {
									err = "BinMeshPLG vertex index out of bounds";
									return false;
								}
							}
							bmCur += static_cast<size_t>(idxCount) * 4u;
						}
					}
				} else if (ec.type == kRw2dEffectPlg && ec.length >= 4u) {
					// Known 3dsMax/Blender 2dfx exporter bug: Light entries (type == 0) contain a
					// 76-byte (0x4C) payload (total 96B per entry: 12B pos + 4B type + 4B size + 76B)
					// while erroneously writing size == 0x50 (80B). RpGeometry2dEffectStreamRead then
					// overreads 4B per entry into the following Atomic chunk, failing RpClumpStreamRead.
					// Patch each affected entry's size field to 0x4C (natively handled by GTA:SA).
					const uint32_t fxCount = readLE32(data + ec.payloadStart);
					if (fxCount > 0u && fxCount <= 1024u
						&& ec.length == 4u + static_cast<size_t>(fxCount) * 96u) {
						bool allMismatchedLights = true;
						for (uint32_t i = 0; i < fxCount; ++i) {
							const size_t entryOff = ec.payloadStart + 4u + static_cast<size_t>(i) * 96u;
							const uint32_t fxType = readLE32(data + entryOff + 12u);
							const uint32_t fxSize = readLE32(data + entryOff + 16u);
							if (fxType != 0u || fxSize != 0x50u) {
								allMismatchedLights = false;
								break;
							}
						}
						if (allMismatchedLights) {
							for (uint32_t i = 0; i < fxCount; ++i) {
								const size_t sizeFieldOff = ec.payloadStart + 4u + static_cast<size_t>(i) * 96u + 16u;
								out2dfxLightSizePatches.push_back(sizeFieldOff);
							}
						}
					}
				}
				eOff = ec.payloadEnd;
			}
		}
		off = child.payloadEnd;
	}

	// Non-native PC geometry payload bounds & triangle index validation
	if (!isNative) {
		const bool hasPrelit = (format & 0x00000008u) != 0;
		uint32_t numUvSets = (format >> 16) & 0xFFu;
		if (numUvSets == 0) {
			if (format & 0x00000004u) numUvSets = 1;
			if (format & 0x00000080u) numUvSets = 2;
		}
		if (numUvSets > 8u) {
			err = "Geometry UV set count out of range";
			return false;
		}
		if (hasPrelit) {
			const size_t bytes = static_cast<size_t>(numVertices) * 4u;
			if (st.payloadEnd - cur < bytes) {
				err = "Geometry prelit colors truncated";
				return false;
			}
			cur += bytes;
		}
		if (numUvSets > 0) {
			const size_t bytes = static_cast<size_t>(numUvSets) * numVertices * 8u;
			if (st.payloadEnd - cur < bytes) {
				err = "Geometry texcoords truncated";
				return false;
			}
			cur += bytes;
		}
		// Triangle list: numTriangles * 8 bytes (v2, v1, matId, v3)
		const size_t triBytes = static_cast<size_t>(numTriangles) * 8u;
		if (st.payloadEnd - cur < triBytes) {
			err = "Geometry triangles truncated";
			return false;
		}
		for (uint32_t t = 0; t < numTriangles; ++t) {
			const uint8_t* tp = data + cur + static_cast<size_t>(t) * 8u;
			const uint16_t v2 = readLE16(tp + 0);
			const uint16_t v1 = readLE16(tp + 2);
			const uint16_t matId = readLE16(tp + 4);
			const uint16_t v3 = readLE16(tp + 6);
			if (v1 >= numVertices || v2 >= numVertices || v3 >= numVertices) {
				err = "Geometry triangle vertex index out of bounds";
				return false;
			}
			if (materialCount > 0 && matId >= materialCount) {
				err = "Geometry triangle material index out of bounds";
				return false;
			}
		}
		cur += triBytes;

		// Morph targets
		for (uint32_t m = 0; m < numMorphTargets; ++m) {
			if (st.payloadEnd - cur < 24u) {
				err = "Geometry morph target header truncated";
				return false;
			}
			for (size_t f = 0; f < 4u; ++f) {
				const float v = readLEFloat(data + cur + f * 4u);
				if (!std::isfinite(v)) {
					err = "Geometry morph bounding sphere non-finite";
					return false;
				}
			}
			const uint32_t hasVerts = readLE32(data + cur + 16u);
			const uint32_t hasNorms = readLE32(data + cur + 20u);
			cur += 24u;
			if (hasVerts) {
				const size_t bytes = static_cast<size_t>(numVertices) * 12u;
				if (st.payloadEnd - cur < bytes) {
					err = "Geometry morph vertices truncated";
					return false;
				}
				cur += bytes;
			}
			if (hasNorms) {
				const size_t bytes = static_cast<size_t>(numVertices) * 12u;
				if (st.payloadEnd - cur < bytes) {
					err = "Geometry morph normals truncated";
					return false;
				}
				cur += bytes;
			}
		}
	}

	return true;
}

bool checkSectionFits(uint32_t rawOff, size_t headerSize, size_t bodySize, uint32_t count, size_t elemSize, size_t& outRelOff) noexcept
{
	if (count == 0) {
		outRelOff = 0;
		return true;
	}
	// In COL2/3/4, section offsets are relative to (colData + 4), while body starts at (colData + 32).
	if (rawOff < 28u) {
		return false;
	}
	const size_t relOff = static_cast<size_t>(rawOff - 28u);
	if (relOff < headerSize || relOff > bodySize) {
		return false;
	}
	const size_t avail = bodySize - relOff;
	if (static_cast<size_t>(count) > avail / elemSize) {
		return false;
	}
	outRelOff = relOff;
	return true;
}

bool validateEmbeddedCol(const uint8_t* colData, size_t colLen, ColFormat& outFmt, std::string& err)
{
	return validateColBytes(colData, colLen, outFmt, err);
}

} // namespace

bool validateColBytes(const uint8_t* colData, size_t colLen, ColFormat& outFmt, std::string& err)
{
	outFmt = ColFormat::None;
	if (!colData || colLen < 32u || colLen > kMaxColBytes) {
		err = "Embedded COL size out of range";
		return false;
	}
	const uint32_t fourcc = readLE32(colData);
	const uint32_t encodedSize = readLE32(colData + 4u);
	if (fourcc == kFourCcCol1) outFmt = ColFormat::Col1;
	else if (fourcc == kFourCcCol2) outFmt = ColFormat::Col2;
	else if (fourcc == kFourCcCol3) outFmt = ColFormat::Col3;
	else if (fourcc == kFourCcCol4) outFmt = ColFormat::Col4;
	else {
		err = "Embedded COL has unknown FourCC";
		return false;
	}
	// COL header: 4-byte FourCC + 4-byte size. Standard COL excludes those 8 bytes (so size <= colLen - 8),
	// but several GTA modding exporters (e.g. older COL Editor / Max scripts) write size == total chunk length.
	// Tolerate encodedSize up to colLen while clamping effectiveTotal to colLen so every downstream
	// bounds check stays strictly inside [colData, colData + colLen).
	if (encodedSize < 24u || static_cast<size_t>(encodedSize) > colLen) {
		err = "Embedded COL internal size exceeds chunk length";
		return false;
	}

	const size_t effectiveTotal = std::min<size_t>(colLen, static_cast<size_t>(encodedSize) + 8u);
	if (effectiveTotal < 32u) {
		err = "Embedded COL effective size truncated";
		return false;
	}
	const uint8_t* body = colData + 32u;
	const size_t bodySize = effectiveTotal - 32u;

	if (outFmt == ColFormat::Col1) {
		// COL1 body: 40B bounds (radius, center[3], min[3], max[3]) + sequential counts/arrays
		if (bodySize < 40u) {
			err = "COL1 body truncated before bounds";
			return false;
		}
		for (size_t i = 0; i < 10u; ++i) {
			const float v = readLEFloat(body + i * 4u);
			if (!std::isfinite(v) || std::fabs(v) > 1.0e6f) {
				err = "COL1 contains non-finite/extreme bounding coordinates";
				return false;
			}
		}
		const float radius = readLEFloat(body);
		if (radius < 0.0f || radius > 1.0e5f) {
			err = "COL1 bounding sphere radius out of range";
			return false;
		}
		size_t cur = 40u;
		auto readCountAndSkip = [&](size_t elemSize, uint32_t maxCount, uint32_t& outCount, const char* label) -> bool {
			if (bodySize - cur < 4u) {
				err = std::string("COL1 truncated before ") + label + " count";
				return false;
			}
			const uint32_t n = readLE32(body + cur);
			cur += 4u;
			if (n > maxCount || static_cast<size_t>(n) > (bodySize - cur) / elemSize) {
				err = std::string("COL1 ") + label + " array out of bounds";
				return false;
			}
			outCount = n;
			cur += static_cast<size_t>(n) * elemSize;
			return true;
		};
		uint32_t nSph = 0, nUnk = 0, nBox = 0, nVert = 0, nFace = 0;
		if (!readCountAndSkip(20u, 8192u, nSph, "spheres")) return false;
		if (!readCountAndSkip(24u, 8192u, nUnk, "unknown")) return false;
		if (!readCountAndSkip(28u, 8192u, nBox, "boxes")) return false;
		if (!readCountAndSkip(12u, 65536u, nVert, "vertices")) return false;
		const size_t facesCountOff = cur;
		if (!readCountAndSkip(16u, 65536u, nFace, "faces")) return false;
		const uint8_t* facePtr = body + facesCountOff + 4u;
		for (uint32_t i = 0; i < nFace; ++i) {
			const uint32_t a = readLE32(facePtr + i * 16u);
			const uint32_t b = readLE32(facePtr + i * 16u + 4u);
			const uint32_t c = readLE32(facePtr + i * 16u + 8u);
			if (a >= nVert || b >= nVert || c >= nVert) {
				err = "COL1 face vertex index out of bounds";
				return false;
			}
		}
		return true;
	}

	// COL2 (76B header), COL3 (88B header), COL4 (92B header)
	const size_t headerSize = (outFmt == ColFormat::Col2) ? 76u : ((outFmt == ColFormat::Col3) ? 88u : 92u);
	if (bodySize < headerSize) {
		err = "COL2/3/4 body truncated before header";
		return false;
	}

	// Bounds: min[3] (0..11), max[3] (12..23), center[3] (24..35), radius (36..39)
	for (size_t i = 0; i < 10u; ++i) {
		const float v = readLEFloat(body + i * 4u);
		if (!std::isfinite(v) || std::fabs(v) > 1.0e6f) {
			err = "COL2/3/4 contains non-finite/extreme bounding coordinates";
			return false;
		}
	}
	const float radius = readLEFloat(body + 36u);
	if (radius < 0.0f || radius > 1.0e5f) {
		err = "COL2/3/4 bounding sphere radius out of range";
		return false;
	}

	const uint16_t numSpheres = readLE16(body + 40u);
	const uint16_t numBoxes = readLE16(body + 42u);
	const uint16_t numFaces = readLE16(body + 44u);
	const uint8_t numLines = body[46u];
	const uint32_t flags = readLE32(body + 48u);

	// Mobile libGTASA.so (LoadCollisionModelVer2/3/4 & CCollisionData::GetNoVectors)
	// sign-extends numFaces via ldrsh; >= 32768 becomes negative and blows up malloc.
	if (numFaces > 32767u) {
		err = "COL2/3/4 face count exceeds int16 limit";
		return false;
	}

	const uint32_t offSpheres = readLE32(body + 52u);
	const uint32_t offBoxes = readLE32(body + 56u);
	const uint32_t offLines = readLE32(body + 60u);
	const uint32_t offVerts = readLE32(body + 64u);
	const uint32_t offFaces = readLE32(body + 68u);

	size_t relSph = 0, relBox = 0, relLin = 0, relFaces = 0;
	if (!checkSectionFits(offSpheres, headerSize, bodySize, numSpheres, 20u, relSph)) {
		err = "COL2/3/4 spheres section out of bounds";
		return false;
	}
	if (!checkSectionFits(offBoxes, headerSize, bodySize, numBoxes, 28u, relBox)) {
		err = "COL2/3/4 boxes section out of bounds";
		return false;
	}
	if (!checkSectionFits(offLines, headerSize, bodySize, numLines, 24u, relLin)) {
		err = "COL2/3/4 lines section out of bounds";
		return false;
	}
	if (!checkSectionFits(offFaces, headerSize, bodySize, numFaces, 8u, relFaces)) {
		err = "COL2/3/4 faces section out of bounds";
		return false;
	}

	if (numFaces > 0) {
		uint32_t maxVertIdx = 0;
		const uint8_t* facePtr = body + relFaces;
		for (uint32_t i = 0; i < numFaces; ++i) {
			const uint16_t a = readLE16(facePtr + i * 8u);
			const uint16_t b = readLE16(facePtr + i * 8u + 2u);
			const uint16_t c = readLE16(facePtr + i * 8u + 4u);
			maxVertIdx = std::max<uint32_t>(maxVertIdx, std::max<uint32_t>(a, std::max<uint32_t>(b, c)));
		}
		size_t relVerts = 0;
		if (!checkSectionFits(offVerts, headerSize, bodySize, maxVertIdx + 1u, 6u, relVerts)) {
			err = "COL2/3/4 compressed vertex array out of bounds for face indices";
			return false;
		}
		// Face groups (flags bit 3 == 0x08): count uint32 sits at (relFaces - 4), array of 28B groups before it
		if ((flags & 0x08u) != 0u) {
			if (relFaces < headerSize + 4u) {
				err = "COL2/3/4 face group count offset underflow";
				return false;
			}
			const uint32_t numGroups = readLE32(body + relFaces - 4u);
			const size_t availBeforeCount = (relFaces - 4u) - headerSize;
			if (numGroups > 4096u || static_cast<size_t>(numGroups) > availBeforeCount / 28u) {
				err = "COL2/3/4 face groups array out of bounds";
				return false;
			}
			const uint8_t* grpPtr = body + (relFaces - 4u) - static_cast<size_t>(numGroups) * 28u;
			for (uint32_t g = 0; g < numGroups; ++g) {
				const uint16_t startF = readLE16(grpPtr + g * 28u + 24u);
				const uint16_t endF = readLE16(grpPtr + g * 28u + 26u);
				if (startF > endF || endF >= numFaces) {
					err = "COL2/3/4 face group range out of bounds";
					return false;
				}
			}
		}
	}

	// COL3 / COL4 shadow mesh: mobile LoadCollisionModelVer3 checks offShadowFaces != 0
	// directly without gating on (flags & 0x10), so validate whenever shadow faces are
	// present (either flagged or pointed to by a non-zero offset). Stale non-zero
	// offShadowVerts with numShadowFaces == 0 are tolerated here and zeroed by
	// prepareColBodyForEngine() so valid collision meshes are not dropped.
	if (outFmt == ColFormat::Col3 || outFmt == ColFormat::Col4) {
		const uint32_t numShadowFaces = readLE32(body + 76u);
		const uint32_t offShadowVerts = readLE32(body + 80u);
		const uint32_t offShadowFaces = readLE32(body + 84u);
		const bool hasShadowFaces = numShadowFaces > 0u && ((flags & 0x10u) != 0u || offShadowFaces != 0u);
		if (hasShadowFaces) {
			if (numShadowFaces > 32767u) {
				err = "COL3/4 shadow face count exceeds limit";
				return false;
			}
			size_t relShFaces = 0;
			if (!checkSectionFits(offShadowFaces, headerSize, bodySize, numShadowFaces, 8u, relShFaces)) {
				err = "COL3/4 shadow faces section out of bounds";
				return false;
			}
			uint32_t maxShVert = 0;
			const uint8_t* sfPtr = body + relShFaces;
			for (uint32_t i = 0; i < numShadowFaces; ++i) {
				const uint16_t a = readLE16(sfPtr + i * 8u);
				const uint16_t b = readLE16(sfPtr + i * 8u + 2u);
				const uint16_t c = readLE16(sfPtr + i * 8u + 4u);
				maxShVert = std::max<uint32_t>(maxShVert, std::max<uint32_t>(a, std::max<uint32_t>(b, c)));
			}
			size_t relShVerts = 0;
			if (!checkSectionFits(offShadowVerts, headerSize, bodySize, maxShVert + 1u, 6u, relShVerts)) {
				err = "COL3/4 shadow vertex array out of bounds";
				return false;
			}
		}
	}

	return true;
}

bool prepareColBodyForEngine(const uint8_t* colData, size_t colLen,
	ColFormat& outFmt, std::vector<uint8_t>& outBody, std::string& outErr)
{
	outFmt = ColFormat::None;
	outBody.clear();
	ColFormat fmt = ColFormat::None;
	if (!validateColBytes(colData, colLen, fmt, outErr)) {
		return false;
	}

	const uint32_t encodedSize = readLE32(colData + 4u);
	const size_t effectiveTotal = std::min<size_t>(colLen, static_cast<size_t>(encodedSize) + 8u);
	if (effectiveTotal < 32u) {
		outErr = "Embedded COL effective size truncated";
		return false;
	}

	outBody.assign(colData + 32u, colData + effectiveTotal);
	if (fmt == ColFormat::Col2 || fmt == ColFormat::Col3 || fmt == ColFormat::Col4) {
		auto writeLE32 = [](uint8_t* p, uint32_t v) {
			p[0] = static_cast<uint8_t>(v & 0xFFu);
			p[1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
			p[2] = static_cast<uint8_t>((v >> 16) & 0xFFu);
			p[3] = static_cast<uint8_t>((v >> 24) & 0xFFu);
		};

		uint8_t* body = outBody.data();
		const uint16_t numSpheres = readLE16(body + 40u);
		const uint16_t numBoxes = readLE16(body + 42u);
		const uint16_t numFaces = readLE16(body + 44u);
		const uint8_t numLines = body[46u];
		uint32_t flags = readLE32(body + 48u);

		// Mobile libGTASA.so LoadCollisionModelVer2/3/4 guards vertex unpacking only on
		// (offVerts != 0) / (offShadowVerts != 0), and GetNoVectors()/GetNoShadVectors()
		// return 1 (not 0) when face count is 0. Clear stale offsets for empty sections so
		// the engine never OOB-reads 6 bytes past the tail of the COL buffer.
		if (numSpheres == 0u) writeLE32(body + 52u, 0u);
		if (numBoxes == 0u) writeLE32(body + 56u, 0u);
		if (numLines == 0u) writeLE32(body + 60u, 0u);
		if (numFaces == 0u) {
			writeLE32(body + 64u, 0u);
			writeLE32(body + 68u, 0u);
			flags &= ~0x08u; // face groups meaningless without faces
		}

		if (fmt == ColFormat::Col3 || fmt == ColFormat::Col4) {
			const uint32_t numShadowFaces = readLE32(body + 76u);
			const uint32_t offShadowVerts = readLE32(body + 80u);
			const uint32_t offShadowFaces = readLE32(body + 84u);
			if (numShadowFaces == 0u || offShadowVerts == 0u || offShadowFaces == 0u) {
				writeLE32(body + 76u, 0u);
				writeLE32(body + 80u, 0u);
				writeLE32(body + 84u, 0u);
				flags &= ~0x10u;
			}
		}
		writeLE32(body + 48u, flags);

		// Mobile arm64 CFileLoader::LoadCollisionModelVer4 has a broken 136-byte header
		// (64-bit pointers leaked into the on-disk header struct). Standard PC COL4 is
		// identical to COL3 except for 4 unused trailing header bytes at body[88..91];
		// strip them and shift non-zero section offsets by -4 so LoadCollisionModelVer3
		// loads it cleanly on both ABIs.
		if (fmt == ColFormat::Col4 && outBody.size() >= 92u) {
			static constexpr size_t kOffsetFields[] = {52u, 56u, 60u, 64u, 68u, 80u, 84u};
			for (size_t offPos : kOffsetFields) {
				const uint32_t rawOff = readLE32(body + offPos);
				if (rawOff >= 32u) {
					writeLE32(body + offPos, rawOff - 4u);
				}
			}
			outBody.erase(outBody.begin() + 88, outBody.begin() + 92);
			fmt = ColFormat::Col3;
		}
	}

	outFmt = fmt;
	return true;
}

DffValidationResult validateAndPrepareDff(const uint8_t* data, size_t size, bool extractAndStripCol)
{
	DffValidationResult res{};
	if (!data || size < 24u) {
		res.error = "DFF buffer too small";
		return res;
	}

	size_t topOff = 0;
	ChunkView clump{};
	bool foundClump = false;

	while (topOff + 12u <= size) {
		ChunkView top{};
		if (!readChunk(data, topOff, size, top)) {
			res.error = "DFF top-level chunk header/length invalid";
			return res;
		}
		if (top.type == kRwUvAnimDict) {
			res.hasUvAnimDict = true;
		} else if (top.type == kRwClump) {
			clump = top;
			foundClump = true;
			break;
		}
		topOff = top.payloadEnd;
	}

	if (!foundClump) {
		res.error = "DFF missing root Clump (0x10)";
		return res;
	}

	// Track any embedded collision sub-chunks inside Clump's Extension for optional stripping.
	struct StripSpan {
		size_t extHeaderOffset = 0;
		size_t start = 0;
		size_t end = 0;
	};
	struct AtomicRef {
		uint32_t frameIdx = 0;
		uint32_t geomIdx = 0;
		bool hasInlineGeom = false;
	};
	std::vector<StripSpan> stripSpans;
	std::vector<size_t> lightSizePatches;
	std::vector<AtomicRef> atomicRefs;
	uint32_t geomListCount = 0;

	size_t off = clump.payloadStart;
	bool sawClumpStruct = false;

	while (off < clump.payloadEnd) {
		ChunkView child{};
		if (!readChunk(data, off, clump.payloadEnd, child)) {
			res.error = "Clump child chunk truncated";
			return res;
		}

		if (child.type == kRwStruct && !sawClumpStruct) {
			if (child.length < 4u) {
				res.error = "Clump struct truncated";
				return res;
			}
			sawClumpStruct = true;
		} else if (child.type == kRwFrameList) {
			if (!validateFrameList(data, child, res.frameCount, res.error)) {
				return res;
			}
		} else if (child.type == kRwGeometryList) {
			ChunkView glStruct{};
			if (!readChunk(data, child.payloadStart, child.payloadEnd, glStruct) ||
			    glStruct.type != kRwStruct || glStruct.length < 4u) {
				res.error = "GeometryList missing struct";
				return res;
			}
			const uint32_t gCount = readLE32(data + glStruct.payloadStart);
			if (gCount > kMaxGeometries) {
				res.error = "GeometryList count out of range";
				return res;
			}
			size_t gOff = glStruct.payloadEnd;
			uint32_t parsedGeoms = 0;
			while (gOff < child.payloadEnd) {
				ChunkView gc{};
				if (!readChunk(data, gOff, child.payloadEnd, gc)) {
					res.error = "GeometryList entry truncated";
					return res;
				}
				if (gc.type == kRwGeometry) {
					if (!validateGeometry(data, gc, res, lightSizePatches, res.error)) {
						return res;
					}
					++parsedGeoms;
				}
				gOff = gc.payloadEnd;
			}
			// RenderWare allows gCount == 0 when geometries are embedded inline inside Atomic chunks.
			if (gCount > 0 && parsedGeoms == 0) {
				res.error = "GeometryList contains no Geometry chunks";
				return res;
			}
			geomListCount = parsedGeoms;
			res.geometryCount += parsedGeoms;
		} else if (child.type == kRwAtomic) {
			ChunkView aStruct{};
			if (!readChunk(data, child.payloadStart, child.payloadEnd, aStruct) ||
			    aStruct.type != kRwStruct || aStruct.length < 16u) {
				res.error = "Atomic struct truncated";
				return res;
			}
			const uint32_t frameIdx = readLE32(data + aStruct.payloadStart);
			const uint32_t geomIdx = readLE32(data + aStruct.payloadStart + 4u);
			bool hasInlineGeom = false;
			size_t aOff = aStruct.payloadEnd;
			while (aOff < child.payloadEnd) {
				ChunkView ac{};
				if (!readChunk(data, aOff, child.payloadEnd, ac)) {
					res.error = "Atomic child chunk truncated";
					return res;
				}
				if (ac.type == kRwGeometry) {
					if (res.geometryCount >= kMaxGeometries) {
						res.error = "Atomic inline geometry count exceeds limit";
						return res;
					}
					if (!validateGeometry(data, ac, res, lightSizePatches, res.error)) {
						return res;
					}
					hasInlineGeom = true;
					++res.geometryCount;
				}
				aOff = ac.payloadEnd;
			}
			atomicRefs.push_back(AtomicRef{frameIdx, geomIdx, hasInlineGeom});
			++res.atomicCount;
		} else if (child.type == kRwExtension) {
			size_t eOff = child.payloadStart;
			while (eOff < child.payloadEnd) {
				ChunkView ec{};
				if (!readChunk(data, eOff, child.payloadEnd, ec)) {
					res.error = "Clump extension sub-chunk truncated";
					return res;
				}
				if (ec.type == kRwSampCollisionPlg || ec.type == kRwGtaCollisionPlg) {
					res.hasEmbeddedColChunk = true;
					if (extractAndStripCol) {
						ColFormat fmt = ColFormat::None;
						std::string colErr;
						if (validateEmbeddedCol(data + ec.payloadStart, ec.length, fmt, colErr)) {
							if (res.embeddedCol.empty()) {
								res.colFormat = fmt;
								res.embeddedCol.assign(data + ec.payloadStart, data + ec.payloadEnd);
							}
						} else if (res.colError.empty()) {
							res.colError = std::move(colErr);
						}
						// Always strip embedded collision chunks before RpClumpStreamRead so GTA never
						// overflows its 64KB PC_Scratch or chokes on 0x0253F2FF.
						stripSpans.push_back(StripSpan{child.headerOffset, ec.headerOffset, ec.payloadEnd});
					}
				}
				eOff = ec.payloadEnd;
			}
		}

		off = child.payloadEnd;
	}

	if (!sawClumpStruct || res.frameCount == 0 || res.geometryCount == 0 || res.atomicCount == 0) {
		res.error = "DFF missing required Clump/FrameList/GeometryList/Atomic";
		return res;
	}
	for (const auto& ar : atomicRefs) {
		if (ar.frameIdx >= res.frameCount) {
			res.error = "Atomic frameIndex out of bounds";
			return res;
		}
		if (!ar.hasInlineGeom && ar.geomIdx >= geomListCount) {
			res.error = "Atomic geometryIndex out of bounds";
			return res;
		}
	}

	if (extractAndStripCol && !stripSpans.empty()) {
		// Rebuild the DFF bytes without the stripped collision extension sub-chunks,
		// updating the exact Clump and Clump Extension 4-byte length fields.
		std::vector<uint8_t> out;
		out.reserve(size);

		// All stripped spans come from the single Clump and its Extension(s) in ascending order.
		std::vector<size_t> extHeaders;
		std::vector<uint32_t> extRemoved;
		uint32_t totalRemoved = 0;

		size_t cursor = 0;
		for (const auto& sp : stripSpans) {
			if (sp.start > cursor) {
				out.insert(out.end(), data + cursor, data + sp.start);
			}
			const uint32_t removed = static_cast<uint32_t>(sp.end - sp.start);
			totalRemoved += removed;
			// Track per-extension removed byte count (map original offset -> new offset)
			const size_t origExtOff = sp.extHeaderOffset;
			bool foundExt = false;
			for (size_t i = 0; i < extHeaders.size(); ++i) {
				if (extHeaders[i] == origExtOff) {
					extRemoved[i] += removed;
					foundExt = true;
					break;
				}
			}
			if (!foundExt) {
				extHeaders.push_back(origExtOff);
				extRemoved.push_back(removed);
			}
			cursor = sp.end;
		}
		if (cursor < size) {
			out.insert(out.end(), data + cursor, data + size);
		}

		// Patch Clump length (clump.headerOffset is strictly before all strip spans)
		const uint32_t newClumpLen = clump.length - totalRemoved;
		writeLE32(out.data() + clump.headerOffset + 4u, newClumpLen);

		// Patch each affected Extension length (adjust offset by bytes removed strictly before it)
		for (size_t i = 0; i < extHeaders.size(); ++i) {
			size_t removedBefore = 0;
			for (const auto& sp : stripSpans) {
				if (sp.end <= extHeaders[i]) {
					removedBefore += (sp.end - sp.start);
				}
			}
			const size_t newExtOff = extHeaders[i] - removedBefore;
			const uint32_t origExtLen = readLE32(data + extHeaders[i] + 4u);
			writeLE32(out.data() + newExtOff + 4u, origExtLen - extRemoved[i]);
		}

		res.sanitizedDff = std::move(out);
	}

	if (!lightSizePatches.empty()) {
		if (res.sanitizedDff.empty()) {
			res.sanitizedDff.assign(data, data + size);
		}
		for (size_t origOff : lightSizePatches) {
			size_t removedBefore = 0;
			if (extractAndStripCol) {
				for (const auto& sp : stripSpans) {
					if (sp.end <= origOff) {
						removedBefore += (sp.end - sp.start);
					}
				}
			}
			const size_t newOff = origOff - removedBefore;
			if (newOff + 4u <= res.sanitizedDff.size()) {
				writeLE32(res.sanitizedDff.data() + newOff, 0x4Cu);
			}
		}
	}

	res.ok = true;
	return res;
}

} // namespace artwork
