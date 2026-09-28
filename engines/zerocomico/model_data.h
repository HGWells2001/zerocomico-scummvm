/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: decoded JapoTek 3D records
 */

#ifndef ZEROCOMICO_MODEL_DATA_H
#define ZEROCOMICO_MODEL_DATA_H

#include "common/array.h"
#include "common/scummsys.h"
#include "common/str.h"

namespace ZeroComico {

class ModelArchive;
struct ModelRecord;

struct Vec2f {
	float x, y;
};

struct Vec3f {
	float x, y, z;
};

struct ObjectTransform {
	Vec3f pivot;
	float matrix[9];
	Vec3f translation;
	Vec3f scale;
};

struct MaterialData {
	uint16 mode;
	uint16 flags;
	Vec3f color0;
	Vec3f color1;
	Vec3f color2;
	uint32 value0;
	uint32 value1;
	Common::String textureName;
	Vec3f textureParams;
	bool hasTexture;
};

struct CameraData {
	Vec3f position;
	Vec3f target;
	float scalar0;
	float fov;
	bool hasClipRange;
	float clipNear;
	float clipFar;
};

struct LightData {
	uint32 flags;
	Vec3f color;
	Vec3f position;
	Vec3f direction;
	float params[5];
	Common::Array<Common::String> linkedNames;
};

struct MeshMaterialRange {
	Common::String name;
	uint32 firstFace;
	uint32 faceCount;
};

struct SkinInfluence {
	uint32 parentVertexIndex;
	float weight;
};

struct MeshData {
	uint32 flags;
	ObjectTransform transform;

	// Flesh records (flags & 0x20000) reference a parent mesh and carry
	// position/influence pairs instead of faces/materials.
	Common::String parentMesh;

	uint32 vertexCount;
	uint32 faceCount;
	uint32 materialCount;

	// Positions are converted to the same local representation produced by
	// the retail loader: filePosition + translation - pivot.
	Common::Array<Vec3f> vertices;
	Common::Array<SkinInfluence> influences;

	Vec3f reserved;
	Common::Array<uint16> indices;
	Common::Array<MeshMaterialRange> materials;
	Common::Array<Vec2f> texcoords;

	uint32 normalCount;
	Common::Array<Vec3f> normals;
	Common::Array<byte> normalMultiplicity;
	Common::Array<uint16> normalIndices;

	bool isSkinnedParent() const { return (flags & 0x10000) != 0; }
	bool isFlesh() const { return (flags & 0x20000) != 0; }
	bool hasTexcoords() const { return (flags & 0x20) != 0; }
	bool hasNormals() const { return (flags & 0x40) != 0; }
};

struct HierarchyEntry {
	uint32 tag;
	bool hasVectors;
	Vec3f vector0;
	Vec3f vector1;
	Common::String name;
	Common::String parent;
};

struct HierarchyData {
	Common::Array<HierarchyEntry> entries;
};

class ModelDataDecoder {
public:
	static bool decodeMaterial(const ModelArchive &archive, const ModelRecord &record, MaterialData &out);
	static bool decodeCamera(const ModelArchive &archive, const ModelRecord &record, CameraData &out);
	static bool decodeLight(const ModelArchive &archive, const ModelRecord &record, LightData &out);
	static bool decodeMesh(const ModelArchive &archive, const ModelRecord &record, MeshData &out);
	static bool decodeHierarchy(const ModelArchive &archive, const ModelRecord &record, HierarchyData &out);
};

} // namespace ZeroComico

#endif
