/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: decoded JapoTek 3D records
 */

#include "zerocomico/model_data.h"
#include "zerocomico/model.h"

#include "common/endian.h"

namespace ZeroComico {

namespace {

static const uint32 kHierarchyVectorTag = 0xf005aabb;
static const uint32 kCameraClipTag = 0xf0f01234;

class BodyReader {
public:
	BodyReader(const ModelArchive &archive, const ModelRecord &record)
		: _data(nullptr), _size(0), _pos(0) {
		if (!record.group && record.bodyOffset <= archive.data().size() &&
		    record.bodySize <= archive.data().size() - record.bodyOffset) {
			_data = archive.data().data() + record.bodyOffset;
			_size = record.bodySize;
		}
	}

	uint32 pos() const { return _pos; }
	uint32 remaining() const { return _pos <= _size ? _size - _pos : 0; }
	bool valid() const { return _data != nullptr; }
	bool atEnd() const { return remaining() == 0; }

	bool readByte(byte &v) {
		if (remaining() < 1)
			return false;
		v = _data[_pos++];
		return true;
	}

	bool readU16(uint16 &v) {
		if (remaining() < 2)
			return false;
		v = READ_LE_UINT16(_data + _pos);
		_pos += 2;
		return true;
	}

	bool readU32(uint32 &v) {
		if (remaining() < 4)
			return false;
		v = READ_LE_UINT32(_data + _pos);
		_pos += 4;
		return true;
	}

	bool readFloat(float &v) {
		if (remaining() < 4)
			return false;
		v = READ_LE_FLOAT32(_data + _pos);
		_pos += 4;
		return true;
	}

	bool readVec2(Vec2f &v) {
		return readFloat(v.x) && readFloat(v.y);
	}

	bool readVec3(Vec3f &v) {
		return readFloat(v.x) && readFloat(v.y) && readFloat(v.z);
	}

	bool readName(Common::String &name) {
		if (remaining() < 32)
			return false;
		uint32 n = 0;
		while (n < 32 && _data[_pos + n] != 0)
			++n;
		name = Common::String(reinterpret_cast<const char *>(_data + _pos), n);
		_pos += 32;
		return true;
	}

private:
	const byte *_data;
	uint32 _size;
	uint32 _pos;
};

static bool readTransform(BodyReader &r, ObjectTransform &t) {
	if (!r.readVec3(t.pivot))
		return false;

	float raw[9];
	for (int i = 0; i < 9; ++i) {
		if (!r.readFloat(raw[i]))
			return false;
	}

	// japotek3d.dll transposes the on-disk 3x3 matrix while loading it.
	t.matrix[0] = raw[0]; t.matrix[1] = raw[3]; t.matrix[2] = raw[6];
	t.matrix[3] = raw[1]; t.matrix[4] = raw[4]; t.matrix[5] = raw[7];
	t.matrix[6] = raw[2]; t.matrix[7] = raw[5]; t.matrix[8] = raw[8];

	return r.readVec3(t.translation) && r.readVec3(t.scale);
}

static void applyRetailVertexOffset(Vec3f &v, const ObjectTransform &t) {
	v.x += t.translation.x - t.pivot.x;
	v.y += t.translation.y - t.pivot.y;
	v.z += t.translation.z - t.pivot.z;
}

static bool readFinalZero(BodyReader &r) {
	byte terminator = 0xff;
	return r.readByte(terminator) && terminator == 0 && r.atEnd();
}

} // namespace

bool ModelDataDecoder::decodeMaterial(const ModelArchive &archive, const ModelRecord &record, MaterialData &out) {
	if (record.group || record.type != 0xf000)
		return false;

	BodyReader r(archive, record);
	if (!r.valid() || !r.readU16(out.mode) || !r.readU16(out.flags) ||
	    !r.readVec3(out.color0) || !r.readVec3(out.color1) || !r.readVec3(out.color2) ||
	    !r.readU32(out.value0) || !r.readU32(out.value1))
		return false;

	out.hasTexture = (out.flags & 0x0002) != 0;
	out.textureName.clear();
	out.textureParams.x = out.textureParams.y = out.textureParams.z = 0.0f;

	if (out.hasTexture) {
		if (!r.readName(out.textureName) || !r.readVec3(out.textureParams))
			return false;
	}

	return readFinalZero(r);
}

bool ModelDataDecoder::decodeCamera(const ModelArchive &archive, const ModelRecord &record, CameraData &out) {
	if (record.group || record.type != 0xf001)
		return false;

	BodyReader r(archive, record);
	if (!r.valid() || !r.readVec3(out.position) || !r.readVec3(out.target) ||
	    !r.readFloat(out.scalar0) || !r.readFloat(out.fov))
		return false;

	out.hasClipRange = false;
	out.clipNear = out.clipFar = 0.0f;

	if (r.remaining() == 1)
		return readFinalZero(r);

	uint32 tag = 0;
	if (!r.readU32(tag))
		return false;
	if (tag == kCameraClipTag) {
		if (!r.readFloat(out.clipNear) || !r.readFloat(out.clipFar))
			return false;
		out.hasClipRange = true;
	}

	return readFinalZero(r);
}

bool ModelDataDecoder::decodeLight(const ModelArchive &archive, const ModelRecord &record, LightData &out) {
	if (record.group || record.type != 0xf002)
		return false;

	BodyReader r(archive, record);
	if (!r.valid() || !r.readU32(out.flags) ||
	    !r.readVec3(out.color) || !r.readVec3(out.position) || !r.readVec3(out.direction))
		return false;

	for (int i = 0; i < 5; ++i) {
		if (!r.readFloat(out.params[i]))
			return false;
	}

	out.linkedNames.clear();
	if (out.flags & 0x100) {
		uint32 count = 0;
		if (!r.readU32(count) || count > r.remaining() / 32)
			return false;
		for (uint32 i = 0; i < count; ++i) {
			Common::String name;
			if (!r.readName(name))
				return false;
			out.linkedNames.push_back(name);
		}
	}

	return readFinalZero(r);
}

bool ModelDataDecoder::decodeMesh(const ModelArchive &archive, const ModelRecord &record, MeshData &out) {
	if (record.group || record.type != 0xf003)
		return false;

	BodyReader r(archive, record);
	if (!r.valid() || !r.readU32(out.flags) || !readTransform(r, out.transform))
		return false;

	out.parentMesh.clear();
	out.vertexCount = out.faceCount = out.materialCount = 0;
	out.normalCount = 0;
	out.vertices.clear();
	out.influences.clear();
	out.indices.clear();
	out.materials.clear();
	out.texcoords.clear();
	out.normals.clear();
	out.normalMultiplicity.clear();
	out.normalIndices.clear();
	out.reserved.x = out.reserved.y = out.reserved.z = 0.0f;

	if (out.isFlesh()) {
		if (!r.readName(out.parentMesh) || !r.readU32(out.vertexCount))
			return false;
		if (out.vertexCount > r.remaining() / 12)
			return false;

		out.vertices.resize(out.vertexCount);
		for (uint32 i = 0; i < out.vertexCount; ++i) {
			if (!r.readVec3(out.vertices[i]))
				return false;
			applyRetailVertexOffset(out.vertices[i], out.transform);
		}

		if (out.vertexCount > r.remaining() / 8)
			return false;
		out.influences.resize(out.vertexCount);
		for (uint32 i = 0; i < out.vertexCount; ++i) {
			if (!r.readU32(out.influences[i].parentVertexIndex) ||
			    !r.readFloat(out.influences[i].weight))
				return false;
		}

		if (!r.readVec3(out.reserved))
			return false;
		return readFinalZero(r);
	}

	if (!r.readU32(out.vertexCount) || !r.readU32(out.faceCount) || !r.readU32(out.materialCount))
		return false;

	if (!out.isSkinnedParent()) {
		if (out.vertexCount > r.remaining() / 12)
			return false;
		out.vertices.resize(out.vertexCount);
		for (uint32 i = 0; i < out.vertexCount; ++i) {
			if (!r.readVec3(out.vertices[i]))
				return false;
			applyRetailVertexOffset(out.vertices[i], out.transform);
		}
	}

	if (!r.readVec3(out.reserved))
		return false;

	if (out.faceCount > r.remaining() / 6)
		return false;
	out.indices.resize(out.faceCount * 3);
	for (uint32 i = 0; i < out.faceCount * 3; ++i) {
		if (!r.readU16(out.indices[i]))
			return false;
	}

	if (out.materialCount > r.remaining() / 40)
		return false;
	out.materials.resize(out.materialCount);
	for (uint32 i = 0; i < out.materialCount; ++i) {
		if (!r.readName(out.materials[i].name) ||
		    !r.readU32(out.materials[i].firstFace) ||
		    !r.readU32(out.materials[i].faceCount))
			return false;
		if (out.materials[i].firstFace > out.faceCount ||
		    out.materials[i].faceCount > out.faceCount - out.materials[i].firstFace)
			return false;
	}

	if (out.hasTexcoords()) {
		const uint32 uvCount = out.faceCount * 3;
		if (uvCount > r.remaining() / 8)
			return false;
		out.texcoords.resize(uvCount);
		for (uint32 i = 0; i < uvCount; ++i) {
			if (!r.readVec2(out.texcoords[i]))
				return false;
		}
	}

	if (out.hasNormals()) {
		if (!r.readU32(out.normalCount))
			return false;
		if (out.normalCount > r.remaining() / 12)
			return false;

		out.normals.resize(out.normalCount);
		for (uint32 i = 0; i < out.normalCount; ++i) {
			if (!r.readVec3(out.normals[i]))
				return false;
		}

		if (out.vertexCount > r.remaining())
			return false;
		out.normalMultiplicity.resize(out.vertexCount);
		for (uint32 i = 0; i < out.vertexCount; ++i) {
			if (!r.readByte(out.normalMultiplicity[i]))
				return false;
		}

		const uint32 normalIndexCount = out.faceCount * 3;
		if (normalIndexCount > r.remaining() / 2)
			return false;
		out.normalIndices.resize(normalIndexCount);
		for (uint32 i = 0; i < normalIndexCount; ++i) {
			if (!r.readU16(out.normalIndices[i]))
				return false;
		}
	}

	return readFinalZero(r);
}

bool ModelDataDecoder::decodeHierarchy(const ModelArchive &archive, const ModelRecord &record, HierarchyData &out) {
	if (record.group || (record.type != 0x0e3d && record.type != 0xf001 &&
	                     record.type != 0xf002 && record.type != 0xf003 &&
	                     record.type != 0xf004 && record.type != 0xf011 &&
	                     record.type != 0xf022 && record.type != 0xf032))
		return false;

	BodyReader r(archive, record);
	if (!r.valid())
		return false;

	out.entries.clear();
	while (r.remaining() > 1) {
		HierarchyEntry entry;
		if (!r.readU32(entry.tag))
			return false;

		entry.hasVectors = entry.tag == kHierarchyVectorTag;
		entry.vector0.x = entry.vector0.y = entry.vector0.z = 0.0f;
		entry.vector1.x = entry.vector1.y = entry.vector1.z = 0.0f;
		if (entry.hasVectors && (!r.readVec3(entry.vector0) || !r.readVec3(entry.vector1)))
			return false;

		if (!r.readName(entry.name) || !r.readName(entry.parent))
			return false;
		out.entries.push_back(entry);
	}

	return readFinalZero(r);
}

} // namespace ZeroComico
