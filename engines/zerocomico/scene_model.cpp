/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: decoded 3D asset set
 */

#include "zerocomico/scene_model.h"

#include "zerocomico/model.h"

namespace ZeroComico {

void SceneModel::clear() {
	materials.clear();
	cameras.clear();
	lights.clear();
	meshes.clear();
	hierarchies.clear();
	clips.clear();
}

bool SceneModel::loadGeometry(const Common::Path &p3dPath) {
	ModelArchive archive;
	if (!archive.load(p3dPath))
		return false;

	for (uint32 i = 0; i < archive.records().size(); ++i) {
		const ModelRecord &record = archive.records()[i];
		if (record.group)
			continue;

		switch (record.type) {
		case 0xf000: {
			NamedMaterial value;
			value.name = record.name;
			if (!ModelDataDecoder::decodeMaterial(archive, record, value.data))
				return false;
			materials.push_back(value);
			break;
		}
		case 0xf001: {
			NamedCamera value;
			value.name = record.name;
			if (!ModelDataDecoder::decodeCamera(archive, record, value.data))
				return false;
			cameras.push_back(value);
			break;
		}
		case 0xf002: {
			NamedLight value;
			value.name = record.name;
			if (!ModelDataDecoder::decodeLight(archive, record, value.data))
				return false;
			lights.push_back(value);
			break;
		}
		case 0xf003: {
			NamedMesh value;
			value.name = record.name;
			if (!ModelDataDecoder::decodeMesh(archive, record, value.data))
				return false;
			meshes.push_back(value);
			break;
		}
		default:
			// Geometry files in the retail data use F000..F003. Keep the
			// loader forward compatible by ignoring an unknown record here.
			break;
		}
	}

	return true;
}

bool SceneModel::loadAnimation(const Common::Path &anjPath) {
	ModelArchive archive;
	if (!archive.load(anjPath))
		return false;

	for (uint32 i = 0; i < archive.records().size(); ++i) {
		const ModelRecord &record = archive.records()[i];
		if (record.group)
			continue;

		if (record.type == 0xf007) {
			NamedAnimationClip clip;
			clip.name = record.name;
			if (!AnimationDecoder::decodeClip(archive, record, clip.data))
				return false;
			clips.push_back(clip);
			continue;
		}

		// JACS hierarchy records reuse several of the same type numbers as
		// P3D objects. The body shape, rather than the type alone, identifies
		// these ANJ records.
		HierarchyData hierarchy;
		if (ModelDataDecoder::decodeHierarchy(archive, record, hierarchy)) {
			NamedHierarchy value;
			value.recordType = record.type;
			value.name = record.name;
			value.data = hierarchy;
			hierarchies.push_back(value);
			continue;
		}

		return false;
	}

	return true;
}

bool SceneModel::loadPair(const Common::Path &p3dPath, const Common::Path &anjPath) {
	clear();
	if (!loadGeometry(p3dPath))
		return false;
	if (!loadAnimation(anjPath)) {
		clear();
		return false;
	}
	return true;
}

const NamedMaterial *SceneModel::findMaterial(const Common::String &name) const {
	for (uint32 i = 0; i < materials.size(); ++i)
		if (materials[i].name.equalsIgnoreCase(name))
			return &materials[i];
	return nullptr;
}

const NamedCamera *SceneModel::findCamera(const Common::String &name) const {
	for (uint32 i = 0; i < cameras.size(); ++i)
		if (cameras[i].name.equalsIgnoreCase(name))
			return &cameras[i];
	return nullptr;
}

const NamedLight *SceneModel::findLight(const Common::String &name) const {
	for (uint32 i = 0; i < lights.size(); ++i)
		if (lights[i].name.equalsIgnoreCase(name))
			return &lights[i];
	return nullptr;
}

const NamedMesh *SceneModel::findMesh(const Common::String &name) const {
	for (uint32 i = 0; i < meshes.size(); ++i)
		if (meshes[i].name.equalsIgnoreCase(name))
			return &meshes[i];
	return nullptr;
}

const NamedAnimationClip *SceneModel::findClip(const Common::String &name) const {
	for (uint32 i = 0; i < clips.size(); ++i)
		if (clips[i].name.equalsIgnoreCase(name))
			return &clips[i];
	return nullptr;
}

} // namespace ZeroComico
